#include "tools/session_tools.h"
#include "session/session.h"
#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Session *g_current_session = NULL;

ToolDef session_save_def = {
    .name = "session_save",
    .desc = "Start (or switch to) a named session. The current conversation log "
            "is moved into <workdir>/.agent/sessions/<session_id>.log and all "
            "subsequent messages are appended there. Use a fresh id (e.g. the "
            "current timestamp) to start a brand-new log; use an existing id "
            "to resume appending to that log.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"session_id\":{\"type\":\"string\",\"description\":\"会话ID\"}},"
                    "\"required\":[\"session_id\"]}",
    .exec = session_save_exec,
    .read_only = false,
};

ToolDef session_load_def = {
    .name = "session_load",
    .desc = "Open an existing session log for reading. The current in-memory "
            "session is closed; subsequent messages are written to the loaded log.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"session_id\":{\"type\":\"string\",\"description\":\"会话ID\"}},"
                    "\"required\":[\"session_id\"]}",
    .exec = session_load_exec,
    .read_only = true,
};

ToolDef session_clear_def = {
    .name = "session_clear",
    .desc = "Delete the current session log file and start an empty log under "
            "the same id.",
    .param_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
    .exec = session_clear_exec,
    .read_only = false,
};

ToolDef session_list_def = {
    .name = "session_list",
    .desc = "List all saved session ids found in .agent/sessions/.",
    .param_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
    .exec = session_list_exec,
    .read_only = true,
};

ToolDef session_new_def = {
    .name = "session_new",
    .desc = "Close the current session and start a brand-new one with a "
            "fresh timestamp id. All further messages are appended to the new log.",
    .param_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
    .exec = session_new_exec,
    .read_only = false,
};

char *session_tools_auto_start(const char *workdir) {
    if (g_current_session) {
        return xstrdup(session_get_id(g_current_session));
    }
    if (!workdir)
        workdir = g_config.workdir;

    char *id = session_generate_unique_id(workdir);
    g_current_session = session_create(workdir, id);
    if (!g_current_session) {
        free(id);
        return NULL;
    }
    return id;
}

char *session_tools_new_session(const char *workdir) {
    if (g_current_session) {
        session_free(g_current_session);
        g_current_session = NULL;
    }
    if (!workdir)
        workdir = g_config.workdir;

    char *id = session_generate_unique_id(workdir);
    g_current_session = session_create(workdir, id);
    if (!g_current_session) {
        free(id);
        return NULL;
    }
    return id;
}

char *session_tools_load(const char *workdir, const char *session_id) {
    if (!session_id || !*session_id)
        return NULL;
    if (!workdir)
        workdir = g_config.workdir;

    Session *s = session_open(workdir, session_id);
    if (!s)
        return NULL;
    session_load(s);
    session_reopen_for_write(s);

    if (g_current_session) {
        session_free(g_current_session);
    }
    g_current_session = s;
    return xstrdup(session_id);
}

char **session_tools_list(int *out_count) {
    return session_list(g_config.workdir, out_count);
}

ToolResult session_save_exec(cJSON *args) {
    cJSON *session_id_json = cJSON_GetObjectItem(args, "session_id");
    if (!cJSON_IsString(session_id_json) || !session_id_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'session_id' argument")};
    }

    if (g_current_session) {
        session_free(g_current_session);
        g_current_session = NULL;
    }

    g_current_session = session_create(g_config.workdir, session_id_json->valuestring);
    if (!g_current_session) {
        return (ToolResult){.ok = false, .output = xstrdup("Failed to create session")};
    }

    char *output = xasprintf("Session saved: %s (log: %s/.agent/sessions/%s.log)",
                             session_id_json->valuestring,
                             g_config.workdir,
                             session_id_json->valuestring);
    return (ToolResult){.ok = true, .output = output};
}

ToolResult session_load_exec(cJSON *args) {
    cJSON *session_id_json = cJSON_GetObjectItem(args, "session_id");
    if (!cJSON_IsString(session_id_json) || !session_id_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'session_id' argument")};
    }

    if (g_current_session) {
        session_free(g_current_session);
        g_current_session = NULL;
    }

    g_current_session = session_open(g_config.workdir, session_id_json->valuestring);
    if (!g_current_session) {
        char *output = xasprintf("Failed to open session %s - file may not exist", session_id_json->valuestring);
        return (ToolResult){.ok = false, .output = output};
    }

    session_load(g_current_session);
    session_reopen_for_write(g_current_session);

    int msg_count = session_get_message_count(g_current_session);

    char *output = xasprintf("Session loaded: %s (%d messages)",
                              session_id_json->valuestring, msg_count);
    return (ToolResult){.ok = true, .output = output};
}

ToolResult session_clear_exec(cJSON *args) {
    (void)args;

    if (g_current_session) {
        session_clear(g_current_session);
        return (ToolResult){.ok = true, .output = xstrdup("Session cleared")};
    }

    return (ToolResult){.ok = false, .output = xstrdup("No active session")};
}

ToolResult session_list_exec(cJSON *args) {
    (void)args;

    int n = 0;
    char **ids = session_list(g_config.workdir, &n);
    if (!ids || n == 0) {
        session_list_free(ids);
        return (ToolResult){.ok = true, .output = xstrdup("No saved sessions.")};
    }

    size_t buf_size = 256;
    char *buf = xmalloc(buf_size);
    size_t off = 0;
    off += (size_t)snprintf(buf + off, buf_size - off, "Saved sessions (%d):\n", n);
    for (int i = 0; i < n; i++) {
        const char *marker = (g_current_session &&
                              strcmp(session_get_id(g_current_session), ids[i]) == 0)
                                 ? " *"
                                 : "  ";
        int written = snprintf(buf + off, buf_size - off, "%s %s\n", marker, ids[i]);
        if (written < 0 || (size_t)written >= buf_size - off) {
            buf_size *= 2;
            buf = xrealloc(buf, buf_size);
            written = snprintf(buf + off, buf_size - off, "%s %s\n", marker, ids[i]);
        }
        off += (size_t)written;
    }

    session_list_free(ids);
    return (ToolResult){.ok = true, .output = buf};
}

ToolResult session_new_exec(cJSON *args) {
    (void)args;

    char *id = session_tools_new_session(g_config.workdir);
    if (!id) {
        return (ToolResult){.ok = false, .output = xstrdup("Failed to start a new session")};
    }

    char *output = xasprintf("New session started: %s (log: %s/.agent/sessions/%s.log)",
                             id, g_config.workdir, id);
    free(id);
    return (ToolResult){.ok = true, .output = output};
}

Session *session_tools_get_session(void) {
    return g_current_session;
}

void session_tools_cleanup(void) {
    if (g_current_session) {
        session_free(g_current_session);
        g_current_session = NULL;
    }
}
