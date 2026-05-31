#ifndef EVAL_TOOLS_H
#define EVAL_TOOLS_H

#include "cJSON.h"
#include "tools/tools.h"

ToolDef eval_start_def;
ToolDef eval_end_def;
ToolDef eval_report_def;

ToolResult eval_start_exec(cJSON *args);
ToolResult eval_end_exec(cJSON *args);
ToolResult eval_report_exec(cJSON *args);

#endif