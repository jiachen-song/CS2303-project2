#include "config.h"
#include "context/context.h"
#include "context/internal.h"
#include "message.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int passed = 0, total = 0;
static char ws[256];

static void check(const char *name, int ok) {
  total++;
  if (ok) {
    passed++;
    printf("  [PASS] %s\n", name);
  } else {
    printf("  [FAIL] %s\n", name);
  }
}

static char *make_msg(const char *role, const char *content,
                      const char *tool_id) {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "role", role);
  cJSON_AddStringToObject(m, "content", content);
  if (tool_id)
    cJSON_AddStringToObject(m, "tool_call_id", tool_id);
  char *out = cJSON_PrintUnformatted(m);
  cJSON_Delete(m);
  return out;
}

static char *make_long(char pad, int n) {
  char *b = malloc((size_t)n + 1);
  memset(b, pad, (size_t)n);
  b[n] = '\0';
  return b;
}

static const char *content_at(const Context *ctx, int i) {
  static char buf[8192];
  cJSON *m = cJSON_Parse(ctx_history(ctx)->items[i]);
  const char *c = json_str(m, "content");
  snprintf(buf, sizeof(buf), "%s", c ? c : "");
  cJSON_Delete(m);
  return buf;
}

static int file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static long file_size(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0)
    return -1;
  return (long)st.st_size;
}

static void cleanup_workspace(void) {
  char cmd[512];
  if (ws[0]) {
    snprintf(cmd, sizeof(cmd), "rm -rf %s", ws);
    int rc = system(cmd);
    (void)rc;
  }
}

int main(void) {
  snprintf(ws, sizeof(ws), "/tmp/test_offload_%d", (int)getpid());
  mkdir(ws, 0755);
  snprintf(g_config.workdir, sizeof(g_config.workdir), "%s", ws);

  g_config.context_window = 500;
  g_config.offload_threshold = 0.1f; /* always trigger for the test */

  printf("=== offload: long tool messages move to disk, short ones stay ===\n");

  Context *ctx = ctx_create(g_config.context_window);

  /* Non-tool message — must never be offloaded even if long. */
  char *long_payload = make_long('A', 600);
  ctx_push(ctx, make_msg("assistant", long_payload, NULL));

  /* Short tool message — below preview threshold. */
  ctx_push(ctx, make_msg("tool", "small", "call-1"));

  /* Long tool message — the one we expect to land in a file. */
  char *long_tool = make_long('Z', 1200);
  ctx_push(ctx, make_msg("tool", long_tool, "call-2"));

  /* Padding so we are past KEEP_RECENT_MSGS for the in-flight messages. */
  for (int i = 0; i < KEEP_RECENT_MSGS; i++)
    ctx_push(ctx, make_msg("user", "padding", NULL));

  int rc = offload_policy.apply(ctx, NULL, 0);
  check("apply returned 0", rc == 0);

  /* Index 0: assistant with 600B content — also eligible now
   * (OFFLOAD_MIN_OTHER_LEN=500), so it gets a placeholder too. */
  check("assistant message also offloaded when >= 500B",
        strstr(content_at(ctx, 0), "offloaded to ") != NULL &&
            strstr(content_at(ctx, 0), "read_file") != NULL);

  /* Index 1: short tool, untouched. */
  check("short tool message untouched",
        strcmp(content_at(ctx, 1), "small") == 0);

  /* Index 2: long tool, now contains a slim placeholder pointing at the
   * .agent/offload/ file (no head/tail preview, no "load_storage" magic). */
  const char *third = content_at(ctx, 2);
  check("long tool message replaced with slim placeholder",
        strncmp(third, "offloaded to ", 12) == 0 && strlen(third) < 1200);
  check("placeholder contains read_file hint",
        strstr(third, "read_file") != NULL);
  check("placeholder mentions .agent/offload",
        strstr(third, ".agent/offload/") != NULL);
  check("placeholder omits the old load_storage magic",
        strstr(third, "load_storage") == NULL);

  /* Two messages qualified: assistant (600B) and the long tool (1200B).
   * They get written to 0.txt and 1.txt in iteration order. */
  char fpath0[512], fpath1[512];
  snprintf(fpath0, sizeof(fpath0), "%s/.agent/offload/0.txt", ws);
  snprintf(fpath1, sizeof(fpath1), "%s/.agent/offload/1.txt", ws);
  check("offload file 0 created (assistant 600B)", file_exists(fpath0));
  check("offload file 0 holds assistant payload", file_size(fpath0) == 600);
  check("offload file 1 created (tool 1200B)", file_exists(fpath1));
  check("offload file 1 holds tool payload", file_size(fpath1) == 1200);

  printf("\n=== offload: second apply is idempotent ===\n");
  int hist_len_before = ctx_history(ctx)->len;
  rc = offload_policy.apply(ctx, NULL, 0);
  check("second apply returned 0", rc == 0);
  check("history length unchanged", ctx_history(ctx)->len == hist_len_before);

  char fpath2[512];
  snprintf(fpath2, sizeof(fpath2), "%s/.agent/offload/2.txt", ws);
  check("second apply did not create another file", !file_exists(fpath2));

  printf("\n=== offload: A — skip when placeholder would not shrink ===\n");
  /* Build a fresh context with a tool message whose content is bigger than
   * OFFLOAD_MIN_TOOL_LEN (50) but smaller than the slim placeholder (~80B at
   * this short workdir). The offload must decline and leave the message
   * verbatim, without writing any .txt. */
  cleanup_workspace();
  mkdir(ws, 0755);

  Context *ctx_neg = ctx_create(g_config.context_window);
  /* 60-byte tool body: above 50B threshold, below the ~80B placeholder. */
  char *tiny = make_long('Q', 60);
  ctx_push(ctx_neg, make_msg("tool", tiny, "neg-1"));
  for (int i = 0; i < KEEP_RECENT_MSGS; i++)
    ctx_push(ctx_neg, make_msg("user", "padding", NULL));

  int offload_id_before = ctx_neg->next_offload_id;
  rc = offload_policy.apply(ctx_neg, NULL, 0);
  check("apply returned 0 on negative-savings path", rc == 0);
  check("offload_id not advanced on skip",
        ctx_neg->next_offload_id == offload_id_before);
  check("tool body preserved verbatim when savings would be negative",
        strcmp(content_at(ctx_neg, 0), tiny) == 0);
  char neg_fpath[512];
  snprintf(neg_fpath, sizeof(neg_fpath), "%s/.agent/offload/0.txt", ws);
  check("no .txt written when skipping negative-savings offload",
        !file_exists(neg_fpath));

  free(tiny);
  ctx_free(ctx_neg);

  free(long_payload);
  free(long_tool);
  ctx_free(ctx);

  cleanup_workspace();

  printf("\n%d / %d passed\n", passed, total);
  return passed == total ? 0 : 1;
}
