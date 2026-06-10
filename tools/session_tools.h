#ifndef SESSION_TOOLS_H
#define SESSION_TOOLS_H

#include "cJSON.h"
#include "tools/tools.h"
#include "session/session.h"

extern ToolDef session_save_def;
extern ToolDef session_load_def;
extern ToolDef session_clear_def;
extern ToolDef session_list_def;
extern ToolDef session_new_def;

ToolResult session_save_exec(cJSON *args);
ToolResult session_load_exec(cJSON *args);
ToolResult session_clear_exec(cJSON *args);
ToolResult session_list_exec(cJSON *args);
ToolResult session_new_exec(cJSON *args);

/* Auto-start a session on agent init. If a session is already active, this
 * is a no-op. The returned id is the active session id (caller frees). */
char *session_tools_auto_start(const char *workdir);

/* Force a brand-new session, closing the current one. Returns the new id
 * (caller frees), or NULL on failure. */
char *session_tools_new_session(const char *workdir);

/* Load an existing session, replacing the current one. Returns the id on
 * success (caller frees), NULL on failure. */
char *session_tools_load(const char *workdir, const char *session_id);

/* List available session ids (caller frees with session_list_free). */
char **session_tools_list(int *out_count);

Session *session_tools_get_session(void);

#endif