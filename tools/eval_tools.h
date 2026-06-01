#ifndef EVAL_TOOLS_H
#define EVAL_TOOLS_H

#include "cJSON.h"
#include "tools/tools.h"
#include "evaluation/evaluation.h"
#include "context/context.h"

extern ToolDef eval_start_def;
extern ToolDef eval_end_def;
extern ToolDef eval_report_def;

ToolResult eval_start_exec(cJSON *args);
ToolResult eval_end_exec(cJSON *args);
ToolResult eval_report_exec(cJSON *args);

void eval_tools_set_suite(EvaluationSuite *suite);
void eval_tools_set_ctx(Context *ctx);
void eval_tools_inc_rounds(void);
void eval_tools_cleanup(void);

#endif