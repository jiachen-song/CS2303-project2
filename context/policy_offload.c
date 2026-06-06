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

static int ensure_dir(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
    return 0;
  return mkdir(path, 0755);
}

/*
 * Per-role length threshold for offload eligibility. Tool outputs frequently
 * contain the bulk of a long turn, so we offload them aggressively. User /
 * assistant messages are kept verbatim more often — we only offload when they
 * are clearly bloating the context (e.g. a large paste) so we do not break the
 * flow of conversation.
 */
#define OFFLOAD_MIN_TOOL_LEN 50
#define OFFLOAD_MIN_OTHER_LEN 500

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

    const char *content = json_str(m, "content");
    if (!content)
      content = "";

    bool is_tool = (strcmp(role, "tool") == 0);

    if (strstr(content, "load_storage") != NULL ||
        strstr(content, "read_file") != NULL) {
      cJSON_Delete(m);
      continue;
    }

    int content_len = (int)strlen(content);
    int threshold = is_tool ? OFFLOAD_MIN_TOOL_LEN : OFFLOAD_MIN_OTHER_LEN;
    if (content_len < threshold) {
      cJSON_Delete(m);
      continue;
    }

    char fpath[PATH_MAX];
    int offload_id = ctx->next_offload_id++;
    snprintf(fpath, sizeof(fpath), "%s/%d.txt", offload_dir, offload_id);

    FILE *f = fopen(fpath, "w");
    if (!f) {
      cJSON_Delete(m);
      continue;
    }
    fprintf(f, "%s", content);
    fclose(f);

    const char *tool_call_id = json_str(m, "tool_call_id");
    char *saved_tool_call_id = (is_tool && tool_call_id) ? xstrdup(tool_call_id) : NULL;
    char *saved_content = xstrdup(content);
    char *saved_role = xstrdup(role);

    cJSON_Delete(m);

    char *preview = xasprintf(
        "%.4sload_storage: save to %s/.agent/offload/%d.txt, "
        "use read_file to retrieve. Content: \"%.100s...\"",
        saved_content, g_config.workdir, offload_id, saved_content);
    cJSON *new_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(new_msg, "role", saved_role);
    cJSON_AddStringToObject(new_msg, "content", preview);
    if (saved_tool_call_id) {
      cJSON_AddStringToObject(new_msg, "tool_call_id", saved_tool_call_id);
      free(saved_tool_call_id);
    }

    char *new_json = cJSON_PrintUnformatted(new_msg);
    ctx_replace_msg(ctx, i, new_json);
    cJSON_Delete(new_msg);
    free(preview);

    fprintf(stderr,
            "[context] offload: %s message at index %d -> %s (%d bytes)\n",
            saved_role, i, fpath, content_len);

    free(saved_content);
    free(saved_role);
  }

  return 0;
}

ContextPolicy offload_policy = {
    .name = "offload",
    .should_apply = offload_should_apply,
    .apply = offload_apply,
};