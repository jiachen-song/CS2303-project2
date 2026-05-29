/*
 * agent.c — orchestration between user input, LLM turns, and tool execution.
 *
 * The skeleton below is sized for Phase A: one request in, one request out,
 * no persistent state to speak of. Phase B and Phase C will both require
 * you to revisit `struct Agent`, agent_create, and agent_free — treat what
 * is here as a starting point, not a contract.
 */
#include "agent.h"
#include "ui/ui.h"
#include "config.h"
#include "llm_client.h"
#include "message.h"
#include "tools/tools.h"
#include "util.h"
#include "tools/executor.h"
#include "context/context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char AGENT_SYSTEM_TEMPLATE[] =
    "You are a coding agent running in the CLI at %s.\n"
    "Use the provided tools when you need to run shell commands.\n"
    "Return a short, final text reply when the task is done.";

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

struct Agent {
  char *system_prompt;
  char *last_reply;
  Context *ctx;
};

Agent *agent_create(void) {
  Agent *a = calloc(1, sizeof(*a));
  if (!a)
    return NULL;
  a->system_prompt = xasprintf(AGENT_SYSTEM_TEMPLATE, g_config.workdir);
  a->ctx = ctx_create(g_config.context_window);
  if (!a->ctx) {
    free(a->system_prompt);
    free(a);
    return NULL;
  }
  ctx_add_policy(a->ctx, &offload_policy);
  ctx_add_policy(a->ctx, &summary_policy);
  tools_init();
  return a;
}

void agent_free(Agent *a) {
  if (!a)
    return;
  free(a->system_prompt);
  free(a->last_reply);
  ctx_free(a->ctx);
  free(a);
}

const char *agent_chat(Agent *a, const char *user_input) {
  char *user_message = msg_user_json(user_input);
  if(!user_message){
    fprintf(stderr, "agent_chat: msg_user_json failed\n");
    return NULL;
  }
  ctx_push(a->ctx, user_message);

  const int MAX_TURNS = 20;

  for(int turn=0;turn <MAX_TURNS;turn++){
    LLMResponse response={0};
    char err[256];

    if(ctx_reclaim(a->ctx, err, sizeof(err)) != 0){
      ui_idle();
      fprintf(stderr, "agent_chat: ctx_reclaim failed: %s\n", err);
      return NULL;
    }

    ui_begin_thinking();
    if(llm_chat(ctx_history(a->ctx), a->system_prompt, g_config.model, &response,err, sizeof(err)) < 0){
      ui_idle();
      fprintf(stderr, "agent_chat: llm_chat failed: %s\n", err);
      return NULL;
    }

    if(response.n_tool_calls == 0){
      if(!response.content){
        response.content = xstrdup("");
      }
      free(a->last_reply);
      a->last_reply = xstrdup(response.content);
      free(response.raw_message);
      free(response.content);
      response.content = NULL;
      response.raw_message = NULL;
      ui_idle();
      return a->last_reply;
    }

    if(response.raw_message){
      ctx_push(a->ctx, xstrdup(response.raw_message));
      free(response.raw_message);
      response.raw_message = NULL;
    }

    const int MAX_TOOL_CALLS = 16;
    ToolCallView view[MAX_TOOL_CALLS];
    for(int i=0;i<response.n_tool_calls && i<MAX_TOOL_CALLS;i++){
      view[i].name = response.tool_calls[i].name;
      char *args_str = cJSON_PrintUnformatted(response.tool_calls[i].args);
      view[i].args_display = args_str ? args_str : "";
      free(args_str);
    }
    ui_begin_tools(response.n_tool_calls, view);

    for(int i=0;i<response.n_tool_calls;i++){
      ToolResult tool_result = {0};
      if(response.n_tool_calls>0 && response.tool_calls){
        ToolDef *def = tool_find(response.tool_calls[i].name);
        if(def){
          tool_result = def->exec(response.tool_calls[i].args);
        }
        else{
          tool_result.ok = false;
          fprintf(stderr, "agent_chat: unknown tool: %s\n", response.tool_calls[i].name);
          tool_result.output = xasprintf("unknown tool: %s",response.tool_calls[i].name);
        }
      }

      ui_tool_done(i,tool_result.ok,tool_result.output);
      char *tool_message = msg_tool_json(response.tool_calls[i].id, tool_result.output);
      if(tool_message){
        ctx_push(a->ctx, tool_message);
      }
      else{
        fprintf(stderr, "agent_chat: msg_tool_json failed\n");
        tool_result_free(&tool_result);
        llm_response_free(&response);
        return NULL;
      }
      tool_result_free(&tool_result);
    }
    llm_response_free(&response);
  }
  return NULL;
}
