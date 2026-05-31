#include "tools/eval_tools.h"
#include "evaluation/evaluation.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static EvaluationSuite *g_active_eval = NULL;
static EvaluationResult *g_current_result = NULL;
static clock_t g_start_time;

ToolDef eval_start_def = {
    .name = "eval_start",
    .desc = "开始一个评估场景，用于跟踪代理的表现",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"scenario\":{\"type\":\"string\",\"description\":\"场景名称\"}},"
                    "\"required\":[\"scenario\"]}",
    .exec = eval_start_exec,
    .read_only = true,
};

ToolDef eval_end_def = {
    .name = "eval_end",
    .desc = "结束当前评估场景，记录结果",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"success\":{\"type\":\"boolean\",\"description\":\"任务是否成功\"}},"
                    "\"required\":[\"success\"]}",
    .exec = eval_end_exec,
    .read_only = true,
};

ToolDef eval_report_def = {
    .name = "eval_report",
    .desc = "生成评估报告",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"path\":{\"type\":\"string\",\"description\":\"报告文件路径\"}},"
                    "\"required\":[]}",
    .exec = eval_report_exec,
    .read_only = true,
};

ToolResult eval_start_exec(cJSON *args) {
    cJSON *scenario_json = cJSON_GetObjectItem(args, "scenario");
    if (!cJSON_IsString(scenario_json) || !scenario_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'scenario' argument")};
    }

    if (g_current_result) {
        eval_result_free(g_current_result);
        free(g_current_result);
    }

    g_current_result = malloc(sizeof(EvaluationResult));
    if (!g_current_result)
        return (ToolResult){.ok = false, .output = xstrdup("malloc failed")};

    eval_result_init(g_current_result, scenario_json->valuestring);
    g_start_time = clock();

    char *output = xasprintf("Started evaluation: %s", scenario_json->valuestring);
    return (ToolResult){.ok = true, .output = output};
}

ToolResult eval_end_exec(cJSON *args) {
    cJSON *success_json = cJSON_GetObjectItem(args, "success");
    bool success = cJSON_IsTrue(success_json);

    if (g_current_result) {
        g_current_result->success = success;

        clock_t end_time = clock();
        g_current_result->duration_seconds = (double)(end_time - g_start_time) / CLOCKS_PER_SEC;

        if (g_active_eval) {
            eval_suite_add(g_active_eval, g_current_result);
        }
    }

    char *output = xstrdup("Evaluation ended");
    return (ToolResult){.ok = true, .output = output};
}

ToolResult eval_report_exec(cJSON *args) {
    cJSON *path_json = cJSON_GetObjectItem(args, "path");
    const char *path = cJSON_IsString(path_json) ? path_json->valuestring : ".agent/eval_report.txt";

    if (g_active_eval) {
        if (eval_save_results(g_active_eval, path) == 0) {
            char *output = xasprintf("Report saved to %s", path);
            return (ToolResult){.ok = true, .output = output};
        }
    }

    return (ToolResult){.ok = false, .output = xstrdup("No active evaluation or save failed")};
}

void eval_tools_set_suite(EvaluationSuite *suite) {
    g_active_eval = suite;
}