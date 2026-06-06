/*
 * agent.c — orchestration between user input, LLM turns, and tool execution.
 */
#include "agent.h"
#include "ui/ui.h"
#include "config.h"
#include "llm_client.h"
#include "message.h"
#include "tools/tools.h"
#include "util.h"
#include "tools/executor.h"
#include "tools/eval_tools.h"
#include "tools/skill_tools.h"
#include "tools/session_tools.h"
#include "session/session.h"
#include "skills/skill.h"
#include "context/context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char AGENT_SYSTEM_TEMPLATE[] =
    "You are a coding agent running in the CLI at %s.\n"
    "Return a short, final text reply when the task is done.\n"
    "\n"
    "Tool usage guidance:\n"
    "- `bash` runs shell commands. Prefer it for inspection, search, build, and test.\n"
    "- `read_file` / `write_file` / `edit_file` operate on workspace-relative paths.\n"
    "- `subagent_spawn` runs a self-contained subtask in an isolated context window\n"
    "  and returns a structured result. Use it proactively for independent work\n"
    "  so the main context stays lean. Triggers:\n"
    "    * Large pasted input (e.g. a long log, dump, or code block) — let a\n"
    "      subagent parse or summarize it instead of inflating the main context.\n"
    "    * Bounded investigations that do not need the rest of the conversation\n"
    "      (e.g. 'analyze module X', 'find usages of Y', 'run benchmark Z').\n"
    "    * Multiple independent questions in one turn — spawn one subagent per\n"
    "      question, then aggregate the results with `subagent_result`.\n"
    "- `memory_write` / `memory_read` / `memory_list` persist facts across\n"
    "  sessions in `.agent/memory.json`.\n"
    "- `session_save` / `session_load` / `session_clear` persist the conversation\n"
    "  log in `.agent/sessions/`.\n"
    "\n"
    "Offload awareness: tool results stored in `.agent/offload/N.txt` are not in\n"
    "your context window. If you need the body of an offloaded result, call\n"
    "`read_file` on the path shown in its placeholder.\n"
    "\n"
    "When the user pastes a long snippet (e.g. `[Pasted ~N lines]`), prefer\n"
    "either `write_file` to save it to disk or `subagent_spawn` to process it —\n"
    "do not echo the entire body back in your reply.";

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

  EvaluationSuite *eval_suite = eval_suite_create("agent_eval");
  eval_tools_set_suite(eval_suite);
  eval_tools_set_ctx(a->ctx);

  SkillStore *skill_store = skill_store_create(g_config.workdir);
  skill_load_directory(skill_store, skill_store->skills_dir);
  skill_tools_set_store(skill_store);

  char *skills_intro = skill_build_intro(skill_store);
  if (skills_intro) {
    char *new_prompt = xasprintf("%s\n\nAvailable skills:\n%s", a->system_prompt, skills_intro);
    free(a->system_prompt);
    a->system_prompt = new_prompt;
    free(skills_intro);
  }

  return a;
}

void agent_free(Agent *a) {
  if (!a)
    return;
  eval_tools_cleanup();
  skill_tools_cleanup();
  free(a->system_prompt);
  free(a->last_reply);
  ctx_free(a->ctx);
  free(a);
}

Context *agent_ctx(Agent *a) {
  return a->ctx;
}

const char *agent_chat(Agent *a, const char *user_input) {
  char *user_message = msg_user_json(user_input);
  if(!user_message){
    fprintf(stderr, "agent_chat: msg_user_json failed\n");
    return NULL;
  }
  ctx_push(a->ctx, user_message);

  Session *session = session_tools_get_session();
  if (session) {
    session_save_raw(session, user_message);
  }

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
      view[i].args_display = args_str;
    }
    ui_begin_tools(response.n_tool_calls, view);
    for(int i=0;i<response.n_tool_calls && i<MAX_TOOL_CALLS;i++){
      free((char *)view[i].args_display);
      view[i].args_display = NULL;
    }

    for(int i=0;i<response.n_tool_calls;i++){
      ToolResult tool_result = {0};
      if(response.n_tool_calls>0 && response.tool_calls){
        ToolDef *def = tool_find(response.tool_calls[i].name);
        if(def){
          eval_tools_inc_rounds();
          tool_result = def->exec(response.tool_calls[i].args);
        }
        else{
          tool_result.ok = false;
          fprintf(stderr, "agent_chat: unknown tool: %s\n", response.tool_calls[i].name);
          tool_result.output = xasprintf("unknown tool: %s",response.tool_calls[i].name);
        }
      }

      ui_tool_done(i,tool_result.ok,tool_result.output);

      Session *session = session_tools_get_session();
      if (session) {
        char *json_msg = msg_user_json(tool_result.output);
        if (json_msg) {
          session_save_raw(session, json_msg);
          free(json_msg);
        }
      }

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