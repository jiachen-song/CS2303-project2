#include "agent/agent.h"
#include "config.h"
#include "ui/ui.h"
#include "context/context.h"
#include "tools/session_tools.h"
#include "session/session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tools/tools.h"
#define INPUT_BUF 4096

static void handle_slash_command(Agent *a, const char *input) {
    if (strcmp(input, "/new") == 0) {
        char *id = session_tools_new_session(g_config.workdir);
        if (!id) {
            fprintf(stderr, "Failed to start a new session\n");
            return;
        }
        printf("Started new session: %s\n", id);
        printf("Log: %s/.agent/sessions/%s.log\n", g_config.workdir, id);
        free(id);
        return;
    }

    if (strcmp(input, "/sessions") == 0) {
        int n = 0;
        char **ids = session_tools_list(&n);
        Session *cur = session_tools_get_session();
        const char *cur_id = cur ? session_get_id(cur) : NULL;
        if (!ids || n == 0) {
            printf("No saved sessions.\n");
        } else {
            printf("Saved sessions (%d):\n", n);
            for (int i = 0; i < n; i++) {
                const char *marker = (cur_id && strcmp(cur_id, ids[i]) == 0) ? "*" : " ";
                printf(" %s %s\n", marker, ids[i]);
            }
        }
        session_list_free(ids);
        return;
    }

    if (strncmp(input, "/load ", 6) == 0) {
      const char *id = input + 6;
      while (*id == ' ')
        id++;
      if (*id == '\0') {
        printf("Usage: /load <session_id>\n");
        return;
      }
      int n = agent_load_session(a, id);
      if (n < 0) {
        printf("No such session: %s\n", id);
        return;
      }
      printf("Loaded session: %s (%d messages replayed into context)\n",
             id, n);
      return;
    }

    if (strcmp(input, "/clear") == 0) {
      if (agent_clear_session(a) != 0) {
        printf("Failed to clear session\n");
        return;
      }
      Session *cur = session_tools_get_session();
      printf("Session cleared: %s\n",
             cur ? session_get_id(cur) : "(no active session)");
      return;
    }

    if (strcmp(input, "/session") == 0) {
        Session *cur = session_tools_get_session();
        if (!cur) {
            printf("No active session.\n");
        } else {
            printf("Active session: %s\n", session_get_id(cur));
            printf("Log: %s/.agent/sessions/%s.log\n",
                   g_config.workdir, session_get_id(cur));
            printf("Messages so far: %d\n", session_get_message_count(cur));
        }
        return;
    }

    if (strcmp(input, "/help") == 0) {
      printf("Slash commands:\n");
      printf("  /new             start a brand-new session (auto-id)\n");
      printf("  /sessions        list saved session logs\n");
      printf("  /session         show the active session\n");
      printf("  /load <id>       switch to a saved session (replay log into context)\n");
      printf("  /clear           clear the current session's log and reset context\n");
      printf("  /help            show this help\n");
      printf("Regular commands: exit | quit | q\n");
      return;
    }

    printf("Unknown command: %s (try /help)\n", input);
}

int main(void) {
  config_init();
  ui_init();
  ui_start();
  ui_banner();

  Agent *a = agent_create();
  if (!a) {
    fprintf(stderr, "agent_create failed\n");
    return 1;
  }
 while(1){
  ui_prompt();
  char input[INPUT_BUF];
  if (!fgets(input, sizeof(input), stdin)) {
    break;
  }

  size_t len = strlen(input);
  if (len > 0 && input[len - 1] == '\n')
    input[len - 1] = '\0';

  if(strcmp(input, "exit") == 0||strcmp(input, "quit") == 0||strcmp(input,"q")==0){
    break;
  }

  if (input[0] == '/') {
      handle_slash_command(a, input);
      continue;
  }

  const char *reply = agent_chat(a, input);
  if (reply){
    ui_idle();
    printf("%s\n", reply);
    fprintf(stderr, "[context] session: %.1f%% (%d / %d tokens)\n",
            ctx_budget_usage(agent_ctx(a)) * 100.0f,
            ctx_total_tokens(agent_ctx(a)),
            ctx_context_window(agent_ctx(a)));
  }
 }
  ui_stop();
  agent_free(a);
  return 0;
}
