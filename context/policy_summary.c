#include "context/internal.h"
#include "config.h"
#include "agent/llm_client.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool summary_should_apply(Context *ctx) {
  if (ctx_budget_usage(ctx) < g_config.summary_threshold)
    return false;
  if (ctx->history.len <= KEEP_RECENT_MSGS)
    return false;
  return true;
}

static int summary_apply(Context *ctx, char *err, size_t err_cap) {
  int cutoff = ctx->history.len - KEEP_RECENT_MSGS;
  if (cutoff <= 0)
    return 0;

  size_t buf_size = 4096;
  char *buf = malloc(buf_size);
  if (!buf)
    return -1;
  buf[0] = '\0';

  strncat(buf,
          "Summarize the following conversation concisely, keeping only key "
          "facts, decisions, and outcomes:\n\n",
          buf_size - 1);

  for (int i = 0; i < cutoff; i++) {
    cJSON *m = cJSON_Parse(ctx->history.items[i]);
    if (m) {
      const char *role = json_str(m, "role");
      const char *content = json_str(m, "content");
      if (role && content) {
        size_t len = strlen(buf);
        snprintf(buf + len, buf_size - len, "%s: %s\n", role, content);
      }
      cJSON_Delete(m);
    }
  }

  MessageList msgs;
  msg_list_init(&msgs);

  msg_list_push(&msgs, msg_user_json(buf));
  free(buf);

  LLMResponse response;
  llm_response_init(&response);

  int rc = llm_chat(&msgs, "", g_config.model, &response, err, err_cap);
  msg_list_free(&msgs);

  if (rc != 0) {
    return -1;
  }

  if (!response.content || !response.content[0]) {
    llm_response_free(&response);
    snprintf(err, err_cap, "summary: empty LLM response");
    return -1;
  }

  cJSON *summary_msg = cJSON_CreateObject();
  cJSON_AddStringToObject(summary_msg, "role", "user");
  cJSON_AddStringToObject(summary_msg, "content", response.content);

  char *summary_json = cJSON_PrintUnformatted(summary_msg);
  ctx_replace_range(ctx, 0, cutoff, summary_json);

  cJSON_Delete(summary_msg);
  llm_response_free(&response);

  return 0;
}

ContextPolicy summary_policy = {
    .name = "summary",
    .should_apply = summary_should_apply,
    .apply = summary_apply,
};