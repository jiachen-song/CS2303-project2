#include "agent/agent_run.h"
#include "agent/llm_client.h"
#include "config.h"
#include "context/internal.h"
#include "message.h"
#include "tools/tools.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void llm_response_free(LLMResponse *r) {
    if (!r)
        return;
    free(r->content);
    free(r->raw_message);
    if (r->tool_calls) {
        for (int i = 0; i < r->n_tool_calls; i++) {
            free(r->tool_calls[i].id);
            free(r->tool_calls[i].name);
            cJSON_Delete(r->tool_calls[i].args);
        }
        free(r->tool_calls);
    }
}

static int save_raw_to_session(Session *session, const char *json) {
    if (!session || !json)
        return 0;
    return session_save_raw(session, json);
}

char *agent_run_turns(Context *ctx,
                      const char *system_prompt,
                      const char *model,
                      int max_turns,
                      Session *session,
                      AgentRunMetrics *metrics) {
    if (!ctx || !model)
        return NULL;

    if (metrics) {
        metrics->rounds = 0;
        metrics->tool_calls = 0;
        metrics->prompt_tokens = 0;
        metrics->completion_tokens = 0;
    }

    char *final_text = NULL;
    char err[256];

    for (int turn = 0; turn < max_turns; turn++) {
        LLMResponse response = {0};

        if (ctx_reclaim(ctx, err, sizeof(err)) != 0) {
            fprintf(stderr, "agent_run_turns: ctx_reclaim failed: %s\n", err);
            return NULL;
        }

        if (llm_chat(ctx_history(ctx), system_prompt, model,
                      &response, err, sizeof(err)) < 0) {
            fprintf(stderr, "agent_run_turns: llm_chat failed: %s\n", err);
            llm_response_free(&response);
            return NULL;
        }

        if (metrics) {
            metrics->rounds++;
            metrics->prompt_tokens = ctx_total_tokens(ctx);
        }

        if (response.n_tool_calls == 0) {
            final_text = response.content ? xstrdup(response.content) : xstrdup("");
            llm_response_free(&response);
            break;
        }

        if (response.raw_message) {
            ctx_push(ctx, xstrdup(response.raw_message));
            save_raw_to_session(session, response.raw_message);
        }

        for (int i = 0; i < response.n_tool_calls; i++) {
            ToolResult tool_result = {0};
            ToolDef *def = tool_find(response.tool_calls[i].name);
            if (def) {
                tool_result = def->exec(response.tool_calls[i].args);
            } else {
                tool_result.ok = false;
                tool_result.output = xasprintf("unknown tool: %s",
                                               response.tool_calls[i].name);
            }

            if (metrics)
                metrics->tool_calls++;

            char *tool_message = msg_tool_json(response.tool_calls[i].id,
                                               tool_result.output);
            if (tool_message) {
                ctx_push(ctx, tool_message);
                save_raw_to_session(session, tool_message);
            }
            tool_result_free(&tool_result);
        }
        llm_response_free(&response);
    }

    if (metrics) {
        metrics->completion_tokens = final_text ? (int)strlen(final_text) / 4 : 0;
    }

    return final_text;
}
