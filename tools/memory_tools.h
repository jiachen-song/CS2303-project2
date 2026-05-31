#ifndef MEMORY_TOOLS_H
#define MEMORY_TOOLS_H

#include "cJSON.h"
#include "tools/tools.h"

ToolDef memory_write_def;
ToolDef memory_read_def;
ToolDef memory_list_def;
ToolDef memory_clear_def;

ToolResult memory_write_exec(cJSON *args);
ToolResult memory_read_exec(cJSON *args);
ToolResult memory_list_exec(cJSON *args);
ToolResult memory_clear_exec(cJSON *args);

#endif