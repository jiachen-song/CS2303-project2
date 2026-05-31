#include "agent/subagent.h"
#include "agent/llm_client.h"
#include "context/context.h"
#include "context/internal.h"
#include "message.h"
#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SubAgent {
    char *workdir;
    int context_window;
    Context *ctx;
    char *system_prompt;
    cJSON *memory;
};

static const char SUBAGENT_SYSTEM_TEMPLATE[] =
    "You are a focused sub-agent completing a specific subtask.\n"
    "Your task is: %s\n"
    "Return a concise, structured result when complete.";

SubAgent *subagent_create(const char *workdir, int context_window) {
    SubAgent *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;

    s->workdir = xstrdup(workdir);
    s->context_window = context_window;
    s->ctx = ctx_create(context_window);
    if (!s->ctx) {
        free(s->workdir);
        free(s);
        return NULL;
    }

    ctx_add_policy(s->ctx, &offload_policy);
    ctx_add_policy(s->ctx, &summary_policy);

    return s;
}

void subagent_free(SubAgent *s) {
    if (!s)
        return;
    free(s->workdir);
    free(s->system_prompt);
    ctx_free(s->ctx);
    cJSON_Delete(s->memory);
    free(s);
}

void subagent_set_memory(SubAgent *s, cJSON *memory) {
    if (s->memory)
        cJSON_Delete(s->memory);
    s->memory = memory ? cJSON_Duplicate(memory, true) : NULL;
}

const char *subagent_execute(SubAgent *s, const char *task, SubAgentMetrics *metrics) {
    if (!s || !task)
        return NULL;

    char *system_prompt = xasprintf(SUBAGENT_SYSTEM_TEMPLATE, task);
    if (!system_prompt)
        return NULL;

    char *user_msg = msg_user_json(task);
    if (!user_msg) {
        free(system_prompt);
        return NULL;
    }

    ctx_push(s->ctx, user_msg);

    LLMResponse response = {0};
    llm_response_init(&response);
    char err[256];
    int prompt_tokens = 0;
    int completion_tokens = 0;

    if (ctx_reclaim(s->ctx, err, sizeof(err)) != 0) {
        free(system_prompt);
        return NULL;
    }

    if (llm_chat(ctx_history(s->ctx), system_prompt, g_config.model,
                 &response, err, sizeof(err)) < 0) {
        free(system_prompt);
        llm_response_free(&response);
        return NULL;
    }

    prompt_tokens = ctx_total_tokens(s->ctx);

    char *result = NULL;
    if (response.content && response.content[0]) {
        result = xstrdup(response.content);
    } else if (response.n_tool_calls > 0) {
        result = xasprintf("Subtask completed with %d tool calls", response.n_tool_calls);
    }

    completion_tokens = response.content ? (int)strlen(response.content) / 4 : 0;

    if (metrics) {
        metrics->rounds = 1;
        metrics->prompt_tokens = prompt_tokens;
        metrics->completion_tokens = completion_tokens;
    }

    llm_response_free(&response);
    free(system_prompt);

    return result;
}

char *subagent_format_result(const char *task, const char *result, bool success) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "task", task ? task : "");
    cJSON_AddStringToObject(obj, "result", result ? result : "");
    cJSON_AddBoolToObject(obj, "success", success);

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return json;
}