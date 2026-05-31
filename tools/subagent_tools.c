#include "tools/subagent_tools.h"
#include "agent/subagent.h"
#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SubAgentMetrics *g_last_metrics = NULL;
static char *g_last_result = NULL;

ToolDef subagent_spawn_def = {
    .name = "subagent_spawn",
    .desc = "生成一个子代理来执行独立任务",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"task\":{\"type\":\"string\",\"description\":\"子代理的任务描述\"}},"
                    "\"required\":[\"task\"]}",
    .exec = subagent_spawn_exec,
    .read_only = true,
};

ToolDef subagent_result_def = {
    .name = "subagent_result",
    .desc = "获取上一个子代理的执行结果",
    .param_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
    .exec = subagent_result_exec,
    .read_only = true,
};

ToolResult subagent_spawn_exec(cJSON *args) {
    cJSON *task_json = cJSON_GetObjectItem(args, "task");
    if (!cJSON_IsString(task_json) || !task_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'task' argument")};
    }

    SubAgent *subagent = subagent_create(g_config.workdir, g_config.context_window / 2);
    if (!subagent) {
        return (ToolResult){.ok = false, .output = xstrdup("Failed to create subagent")};
    }

    SubAgentMetrics metrics = {0};
    const char *result = subagent_execute(subagent, task_json->valuestring, &metrics);

    free(g_last_result);
    g_last_result = NULL;
    free(g_last_metrics);

    if (result) {
        g_last_result = xstrdup(result);
        g_last_metrics = malloc(sizeof(SubAgentMetrics));
        if (g_last_metrics) {
            memcpy(g_last_metrics, &metrics, sizeof(SubAgentMetrics));
        }
    }

    char *output = xasprintf("Subagent spawned. Result: %s", result ? result : "no result");
    subagent_free(subagent);
    return (ToolResult){.ok = true, .output = output};
}

ToolResult subagent_result_exec(cJSON *args) {
    (void)args;

    if (!g_last_result) {
        return (ToolResult){.ok = false, .output = xstrdup("No subagent result available")};
    }

    if (g_last_metrics) {
        char *output = xasprintf("Result: %s\nMetrics: rounds=%d, prompt_tokens=%d, completion_tokens=%d",
                                 g_last_result, g_last_metrics->rounds,
                                 g_last_metrics->prompt_tokens, g_last_metrics->completion_tokens);
        return (ToolResult){.ok = true, .output = output};
    }

    return (ToolResult){.ok = true, .output = xstrdup(g_last_result)};
}