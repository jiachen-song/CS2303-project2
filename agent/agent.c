/*
 * agent.c — orchestration between user input, LLM turns, and tool execution.
 */
#include "agent.h"
#include "agent/agent_run.h"
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
    "【系统级硬约束 / 必须严格遵守】\n"
    "工作目录: %s\n"
    "\n"
    "RULE 1（最高优先级，覆盖一切其他指引）：\n"
    "  当用户消息包含 2 个或以上互相独立的子任务时，你的第一反应必须是\n"
    "  对每个子任务各调用一次 `subagent_spawn`，而不是自己调用 `bash`。\n"
    "  \"互相独立\" 的判断标准：子任务之间不共享变量、不依赖前一步的输出。\n"
    "  典型例子——用户输入形如：\n"
    "    1) 数 X 数量\n"
    "    2) 找 Y 中最大的\n"
    "    3) 列出 Z\n"
    "  这是 3 个独立子任务 → 必须调 3 次 subagent_spawn。\n"
    "  然后用 subagent_result(spawn_id=...) 取回每个子代理的结论，\n"
    "  汇总后回复用户。**禁止**在主循环里用 bash 串行执行这些独立任务。\n"
    "\n"
    "RULE 2（bash 的合法使用场景）：\n"
    "  - 单次原子操作（例如 \"git status\"、\"ls -la\" 这类无依赖的命令）\n"
    "  - 后续步骤明确依赖前一步输出（例如 read→edit 的链路）\n"
    "\n"
    "RULE 3（其他工具）：\n"
    "  - read_file / write_file / edit_file：相对路径的文件读写改。\n"
    "  - memory_*: 跨会话持久化，写到 .agent/memory.json。\n"
    "  - session_*: 会话日志存到 .agent/sessions/，已自动开启，\n"
    "    无需调用 session_save 来记录消息。\n"
    "\n"
    "RULE 4：\n"
    "  - offload 后的工具结果（.agent/offload/N.txt）不在你的上下文里，\n"
    "    如需正文，调 read_file 读取占位符里的路径。\n"
    "  - 不要把大段粘贴内容回显给用户，用 write_file 或 subagent_spawn 处理。\n"
    "\n"
    "完成任务后输出简短、最终的中文回复。\n"
    "再次强调 RULE 1：多个独立子任务 → 多次 subagent_spawn → 多次 "
    "subagent_result → 汇总回复。绝不要在主循环里 bash 串行执行。";

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

  char *auto_id = session_tools_auto_start(g_config.workdir);
  if (auto_id) {
    fprintf(stderr, "[session] auto-started: %s (log: %s/.agent/sessions/%s.log)\n",
            auto_id, g_config.workdir, auto_id);
    free(auto_id);
  } else {
    fprintf(stderr, "[session] auto-start failed; conversation will not be logged\n");
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
    session_save_raw(session, NULL, user_message);
  }

  /* Drive the LLM through the shared turn loop. The loop itself records
   * assistant + tool messages into the session log; we just need to render
   * tool activity to the user. */
  const int MAX_TURNS = 20;

  /* We need to render tool calls the same way the original agent did. The
   * shared loop does not know about the UI, so we re-implement the wrapper
   * here on top of agent_run_turns by calling it round-by-round. Simpler:
   * drive ctx_reclaim + llm_chat + tool execution inline so we keep UI. */

  for(int turn=0; turn < MAX_TURNS; turn++){
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
      if (session) session_save_raw(session, NULL, response.raw_message);
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

      if (session) {
        char *json_msg = msg_tool_json(response.tool_calls[i].id, tool_result.output);
        if (json_msg) {
          session_save_raw(session, NULL, json_msg);
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
