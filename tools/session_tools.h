#ifndef SESSION_TOOLS_H
#define SESSION_TOOLS_H

#include "cJSON.h"
#include "tools/tools.h"

ToolDef session_save_def;
ToolDef session_load_def;
ToolDef session_clear_def;

ToolResult session_save_exec(cJSON *args);
ToolResult session_load_exec(cJSON *args);
ToolResult session_clear_exec(cJSON *args);

#endif