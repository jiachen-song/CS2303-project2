#include "tools/subagent_tools.h"
#include "agent/subagent.h"
#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SUBAGENT_RESULTS 16

typedef struct {
    char *id;
    char *task;
    char *result;
    SubAgentMetrics metrics;
} SubAgentResultEntry;

static SubAgentResultEntry g_results[MAX_SUBAGENT_RESULTS];
static int g_results_count = 0;
static int g_results_seq = 0;

static void entry_clear(SubAgentResultEntry *e) {
    free(e->id);
    free(e->task);
    free(e->result);
    memset(e, 0, sizeof(*e));
}

static SubAgentResultEntry *find_entry(const char *id) {
    if (!id)
        return NULL;
    for (int i = 0; i < g_results_count; i++) {
        if (g_results[i].id && strcmp(g_results[i].id, id) == 0)
            return &g_results[i];
    }
    return NULL;
}

ToolDef subagent_spawn_def = {
    .name = "subagent_spawn",
    .desc = "【首选工具】当用户请求包含 2 个或以上互相独立的子任务（编号列表、"
            "项目符号列表、\"做 A、做 B、做 C\" 等），**应优先调用本工具**为每个"
            "子任务生成一个子代理。子代理拥有独立的 context window，可使用 "
            "bash / read_file / write_file / edit_file 等全部工具，完成后返回"
            "一段简短中文结论。\n"
            "\n"
            "参数 `task` (string) 是子代理的任务描述。返回 spawn_id，"
            "之后用 `subagent_result(spawn_id=...)` 读取子代理的最终答复。\n"
            "\n"
            "不要用 `bash` 在主循环里串行执行这些独立子任务——那会浪费主上下文。"
            "只有子任务之间有数据依赖（B 依赖 A 的输出）时才退回主循环用 bash。",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"task\":{\"type\":\"string\",\"description\":\"子代理的任务描述\"},"
                    "\"memory\":{\"type\":\"object\",\"description\":\"可选：注入到子代理的只读 memory 快照\"}},"
                    "\"required\":[\"task\"]}",
    .exec = subagent_spawn_exec,
    .read_only = true,
};

ToolDef subagent_result_def = {
    .name = "subagent_result",
    .desc = "Retrieve the final reply of a previously spawned sub-agent by its "
            "spawn_id. Returns the sub-agent's final text plus metrics "
            "(rounds, tool_calls, prompt_tokens, completion_tokens).",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"spawn_id\":{\"type\":\"string\",\"description\":\"subagent_spawn 返回的 id\"}},"
                    "\"required\":[\"spawn_id\"]}",
    .exec = subagent_result_exec,
    .read_only = true,
};

static char *make_spawn_id(void) {
    unsigned int seq = (unsigned int)++g_results_seq;
    return xasprintf("sa-%u", seq);
}

static void record_result(const char *id, const char *task,
                          const char *result, const SubAgentMetrics *m) {
    /* If a slot with the same id exists (shouldn't), overwrite. */
    SubAgentResultEntry *e = find_entry(id);
    if (e) {
        free(e->task);
        free(e->result);
        e->task = task ? xstrdup(task) : NULL;
        e->result = result ? xstrdup(result) : NULL;
        if (m) e->metrics = *m;
        return;
    }
    if (g_results_count >= MAX_SUBAGENT_RESULTS) {
        /* Drop the oldest to make room. */
        entry_clear(&g_results[0]);
        for (int i = 1; i < g_results_count; i++)
            g_results[i - 1] = g_results[i];
        g_results_count--;
    }
    e = &g_results[g_results_count++];
    memset(e, 0, sizeof(*e));
    e->id = xstrdup(id);
    e->task = task ? xstrdup(task) : NULL;
    e->result = result ? xstrdup(result) : NULL;
    if (m) e->metrics = *m;
}

ToolResult subagent_spawn_exec(cJSON *args) {
    cJSON *task_json = cJSON_GetObjectItem(args, "task");
    if (!cJSON_IsString(task_json) || !task_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'task' argument")};
    }

    cJSON *memory_json = cJSON_GetObjectItem(args, "memory");
    if (memory_json && !cJSON_IsObject(memory_json)) {
        return (ToolResult){.ok = false,
                            .output = xstrdup("'memory' must be a JSON object")};
    }

    char *id = make_spawn_id();
    SubAgent *subagent = subagent_create(g_config.workdir,
                                         g_config.context_window / 2);
    if (!subagent) {
        free(id);
        return (ToolResult){.ok = false, .output = xstrdup("Failed to create subagent")};
    }

    if (memory_json)
        subagent_set_memory(subagent, memory_json);

    SubAgentMetrics metrics = {0};
    char *result = subagent_execute(subagent, task_json->valuestring, id,
                                    &metrics);

    record_result(id, task_json->valuestring, result, &metrics);
    free(result);

    char *output = xasprintf("spawn_id=%s (%d rounds, %d tool_calls, %d prompt_tokens)",
                             id, metrics.rounds, metrics.tool_calls,
                             metrics.prompt_tokens);
    subagent_free(subagent);
    free(id);
    return (ToolResult){.ok = true, .output = output};
}

ToolResult subagent_result_exec(cJSON *args) {
    cJSON *id_json = cJSON_GetObjectItem(args, "spawn_id");
    if (!cJSON_IsString(id_json) || !id_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'spawn_id' argument")};
    }

    SubAgentResultEntry *e = find_entry(id_json->valuestring);
    if (!e) {
        return (ToolResult){.ok = false,
                            .output = xasprintf("unknown spawn_id: %s",
                                                id_json->valuestring)};
    }

    char *output = xasprintf(
        "task: %s\n"
        "rounds: %d\n"
        "tool_calls: %d\n"
        "prompt_tokens: %d\n"
        "completion_tokens: %d\n"
        "result:\n%s",
        e->task ? e->task : "",
        e->metrics.rounds, e->metrics.tool_calls, e->metrics.prompt_tokens,
        e->metrics.completion_tokens,
        e->result ? e->result : "(no result)");
    return (ToolResult){.ok = true, .output = output};
}
