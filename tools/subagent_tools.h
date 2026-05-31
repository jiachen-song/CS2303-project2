#ifndef SUBAGENT_TOOLS_H
#define SUBAGENT_TOOLS_H

#include "cJSON.h"
#include "tools/tools.h"

ToolDef subagent_spawn_def;
ToolDef subagent_result_def;

ToolResult subagent_spawn_exec(cJSON *args);
ToolResult subagent_result_exec(cJSON *args);

#endif