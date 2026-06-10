#define _GNU_SOURCE
#include "context/internal.h"
#include "config.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool offload_should_apply(Context *ctx) {
  return ctx_budget_usage(ctx) >= g_config.offload_threshold;
}

#define ensure_dir(path) ensure_dir_recursive(path)

/*
 * Per handbook §2: the offload policy examines tool-role messages located
 * outside the most recent KEEP_RECENT_MSGS entries. User and assistant
 * messages are NEVER offloaded — they represent the conversation flow
 * itself, so the policy leaves them verbatim regardless of length.
 *
 * Tool outputs are offloaded aggressively when their body is long enough to
 * bloat the window. The placeholder retains a short head preview of the
 * original bytes (so a downstream `read_file` is rarely needed) and
 * embeds the offload path plus the `read_file` retrieval hint.
 */
#define OFFLOAD_MIN_TOOL_LEN 50
#define OFFLOAD_HEAD_PREVIEW 200

static int offload_apply(Context *ctx, char *err, size_t err_cap) {
  (void)err;
  (void)err_cap;

  char agent_dir[PATH_MAX];
  snprintf(agent_dir, sizeof(agent_dir), "%s/.agent", g_config.workdir);
  if (ensure_dir(agent_dir) != 0)
    return -1;

  char offload_dir[PATH_MAX];
  snprintf(offload_dir, sizeof(offload_dir), "%s/.agent/offload", g_config.workdir);
  if (ensure_dir(offload_dir) != 0)
    return -1;

  int cutoff = ctx->history.len - KEEP_RECENT_MSGS;
  if (cutoff <= 0)
    return 0;

  for (int i = 0; i < cutoff; i++) {
    cJSON *m = cJSON_Parse(ctx->history.items[i]);
    if (!m)
      continue;

    const char *role = json_str(m, "role");
    if (!role) {
      cJSON_Delete(m);
      continue;
    }

    /* Handbook: only tool-role messages are offloaded. */
    if (strcmp(role, "tool") != 0) {
      cJSON_Delete(m);
      continue;
    }

    const char *content = json_str(m, "content");
    if (!content)
      content = "";

    /* Idempotency: a message that has already been offloaded contains
     * both "read_file" and ".agent/offload/" in its placeholder. */
    if (strstr(content, "read_file") != NULL &&
        strstr(content, ".agent/offload/") != NULL) {
      cJSON_Delete(m);
      continue;
    }

    int content_len = (int)strlen(content);
    if (content_len < OFFLOAD_MIN_TOOL_LEN) {
      cJSON_Delete(m);
      continue;
    }

    /* Build the placeholder first and compare lengths. If the placeholder
     * would not shrink the message, leave the message verbatim and never
     * touch the filesystem. */
    int head_n = content_len < OFFLOAD_HEAD_PREVIEW ? content_len
                                                   : OFFLOAD_HEAD_PREVIEW;
    char *placeholder = xasprintf(
        "%.*s\n[...truncated; full content at %s/.agent/offload/%d.txt — "
        "use read_file to retrieve]",
        head_n, content, g_config.workdir, ctx->next_offload_id);

    if ((int)strlen(placeholder) >= content_len) {
      fprintf(stderr,
              "[context] offload: skipped index %d (%s, %d bytes) — placeholder not smaller\n",
              i, role, content_len);
      free(placeholder);
      cJSON_Delete(m);
      continue;
    }

    int offload_id = ctx->next_offload_id++;
    char fpath[PATH_MAX];
    snprintf(fpath, sizeof(fpath), "%s/%d.txt", offload_dir, offload_id);

    FILE *f = fopen(fpath, "w");
    if (!f) {
      free(placeholder);
      cJSON_Delete(m);
      continue;
    }
    fprintf(f, "%s", content);
    fclose(f);

    const char *tool_call_id = json_str(m, "tool_call_id");
    char *saved_tool_call_id = tool_call_id ? xstrdup(tool_call_id) : NULL;
    char *saved_role = xstrdup(role);

    cJSON_Delete(m);

    cJSON *new_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(new_msg, "role", saved_role);
    cJSON_AddStringToObject(new_msg, "content", placeholder);
    if (saved_tool_call_id) {
      cJSON_AddStringToObject(new_msg, "tool_call_id", saved_tool_call_id);
      free(saved_tool_call_id);
    }

    char *new_json = cJSON_PrintUnformatted(new_msg);
    ctx_replace_msg(ctx, i, new_json);
    cJSON_Delete(new_msg);
    free(placeholder);

    fprintf(stderr,
            "[context] offload: %s message at index %d -> %s (%d bytes)\n",
            saved_role, i, fpath, content_len);

    free(saved_role);
  }

  return 0;
}

ContextPolicy offload_policy = {
    .name = "offload",
    .should_apply = offload_should_apply,
    .apply = offload_apply,
};