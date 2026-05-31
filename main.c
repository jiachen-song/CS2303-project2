#include "agent/agent.h"
#include "config.h"
#include "ui/ui.h"
#include "context/context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tools/tools.h"
#define INPUT_BUF 4096

int main(void) {
  config_init();
  ui_init();
  ui_start();

  ui_banner();
  //change
  // fprintf(stderr,"BEFORE tools_init\n");
  // tools_init();
  // fprintf(stderr,"AFTER tools_init\n");
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
