#include "agent/subagent.h"
#include "agent/agent_run.h"
#include "agent/llm_client.h"
#include "context/context.h"
#include "context/internal.h"
#include "message.h"
#include "config.h"
#include "tools/tools.h"
#include "tools/session_tools.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SubAgent {
    char *workdir;
    int context_window;
    Context *ctx;
    char *system_prompt;   /* base system prompt (re-used across executes) */
    cJSON *memory;
    char *log_path;         /* optional independent subagent log id */
};

static const char SUBAGENT_BASE_PROMPT[] =
    "You are a focused sub-agent completing a specific subtask inside a\n"
    "larger coding agent. The main agent has handed you a bounded piece of\n"
    "work so the main context window stays lean.\n"
    "\n"
    "Working directory: %s\n"
    "\n"
    "You have the same tools as the main agent (bash, read_file, write_file,\n"
    "edit_file, etc.). Use them to complete the task. When you are done,\n"
    "reply with a short, structured summary in plain text — do not call any\n"
    "more tools. The main agent will read your final reply via\n"
    "subagent_result.\n"
    "\n"
    "Important:\n"
    "- Do not try to talk to the user; your final text is the deliverable.\n"
    "- Do not spawn further sub-agents.\n"
    "- Keep the reply concise; the main agent will integrate it into its own\n"
    "  answer.";

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

    s->system_prompt = xasprintf(SUBAGENT_BASE_PROMPT, workdir);
    return s;
}

void subagent_free(SubAgent *s) {
    if (!s)
        return;
    free(s->workdir);
    free(s->system_prompt);
    free(s->log_path);
    ctx_free(s->ctx);
    cJSON_Delete(s->memory);
    free(s);
}

void subagent_set_memory(SubAgent *s, cJSON *memory) {
    if (s->memory)
        cJSON_Delete(s->memory);
    s->memory = memory ? cJSON_Duplicate(memory, true) : NULL;
}

void subagent_set_log_path(SubAgent *s, const char *log_path) {
    free(s->log_path);
    s->log_path = log_path ? xstrdup(log_path) : NULL;
}

static char *build_task_prompt(SubAgent *s, const char *task) {
    char *base = s->system_prompt ? s->system_prompt : (char *)"";
    if (s->memory) {
        char *mem_json = cJSON_PrintUnformatted(s->memory);
        char *full = xasprintf(
            "%s\n\nRelevant project memory (read-only):\n%s\n\n"
            "Your specific task:\n%s\n\n"
            "When finished, reply with your final result in plain text.",
            base, mem_json ? mem_json : "{}", task);
        free(mem_json);
        return full;
    }
    return xasprintf(
        "%s\n\nYour specific task:\n%s\n\n"
        "When finished, reply with your final result in plain text.",
        base, task);
}

const char *subagent_execute(SubAgent *s, const char *task, SubAgentMetrics *metrics) {
    if (!s || !task)
        return NULL;

    /* Sub-agent's per-turn messages are appended to the active main session
     * log so the whole conversation is replayable from one place. */
    Session *main_session = session_tools_get_session();

    char *full_prompt = build_task_prompt(s, task);
    if (!full_prompt)
        return NULL;

    char *user_msg = msg_user_json(task);
    if (user_msg)
        ctx_push(s->ctx, user_msg);

    AgentRunMetrics run_metrics = {0};
    char *result = agent_run_turns(s->ctx, full_prompt, g_config.model,
                                   /*max_turns*/ 8, main_session,
                                   &run_metrics);
    free(full_prompt);

    if (metrics) {
        metrics->rounds = run_metrics.rounds;
        metrics->tool_calls = run_metrics.tool_calls;
        metrics->prompt_tokens = run_metrics.prompt_tokens;
        metrics->completion_tokens = run_metrics.completion_tokens;
    }
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
