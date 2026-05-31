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
    .desc = "保存当前会话状态到磁盘",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"session_id\":{\"type\":\"string\",\"description\":\"会话ID\"}},"
                    "\"required\":[\"session_id\"]}",
    .exec = session_save_exec,
    .read_only = false,
};

ToolDef session_load_def = {
    .name = "session_load",
    .desc = "从磁盘加载会话状态",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"session_id\":{\"type\":\"string\",\"description\":\"会话ID\"}},"
                    "\"required\":[\"session_id\"]}",
    .exec = session_load_exec,
    .read_only = true,
};

ToolDef session_clear_def = {
    .name = "session_clear",
    .desc = "清除当前会话",
    .param_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
    .exec = session_clear_exec,
    .read_only = false,
};

ToolResult session_save_exec(cJSON *args) {
    cJSON *session_id_json = cJSON_GetObjectItem(args, "session_id");
    if (!cJSON_IsString(session_id_json) || !session_id_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'session_id' argument")};
    }

    if (g_current_session) {
        session_free(g_current_session);
    }

    g_current_session = session_create(g_config.workdir, session_id_json->valuestring);
    if (!g_current_session) {
        return (ToolResult){.ok = false, .output = xstrdup("Failed to create session")};
    }

    char *output = xasprintf("Session saved: %s", session_id_json->valuestring);
    return (ToolResult){.ok = true, .output = output};
}

ToolResult session_load_exec(cJSON *args) {
    cJSON *session_id_json = cJSON_GetObjectItem(args, "session_id");
    if (!cJSON_IsString(session_id_json) || !session_id_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'session_id' argument")};
    }

    if (g_current_session) {
        session_free(g_current_session);
    }

    g_current_session = session_create(g_config.workdir, session_id_json->valuestring);
    if (!g_current_session) {
        return (ToolResult){.ok = false, .output = xstrdup("Failed to create session")};
    }

    int msg_count = session_load(g_current_session);
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

Session *session_tools_get_session(void) {
    return g_current_session;
}

void session_tools_cleanup(void) {
    if (g_current_session) {
        session_free(g_current_session);
        g_current_session = NULL;
    }
}