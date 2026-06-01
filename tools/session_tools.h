#ifndef SESSION_TOOLS_H
#define SESSION_TOOLS_H

#include "cJSON.h"
#include "tools/tools.h"
#include "session/session.h"

extern ToolDef session_save_def;
extern ToolDef session_load_def;
extern ToolDef session_clear_def;

ToolResult session_save_exec(cJSON *args);
ToolResult session_load_exec(cJSON *args);
ToolResult session_clear_exec(cJSON *args);

Session *session_tools_get_session(void);

#endif