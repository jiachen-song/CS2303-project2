/*
 * bash tool — run a shell command in a forked child and return its output
 * as a ToolResult.
 *
 * The child side (pipe setup, fork, dup2, execl) is given below — you have
 * already written this pattern in Project 1. What the LLM actually needs is
 * the *other half*: after fork, the parent holds pipefd[0] and pid, and
 * must turn that into a ToolResult the agent can send back.
 */
#include "tools.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

const char *BASH_TOOL_NAME = "bash";
const char *BASH_TOOL_DESC =
    "Run a shell command and return its combined stdout/stderr.";
const char *BASH_TOOL_SCHEMA =
    "{\"type\":\"object\","
    "\"properties\":{\"command\":{\"type\":\"string\","
    "\"description\":\"The shell command to execute\"}},"
    "\"required\":[\"command\"]}";

//change
ToolDef bash_def = {
    .name = "bash",
    .desc = "运行一个 shell 命令并返回其合并的 stdout/stderr。",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{\"command\":{\"type\":\"string\","
                    "\"description\":\"要执行的 shell 命令\"}},"
                    "\"required\":[\"command\"]}",
    .exec = bash_tool_exec,
    .read_only=false
};

// void tool_result_free(ToolResult *r) {
//   if (!r)
//     return;
//   free(r->output);
//   r->output = NULL;
// }

ToolResult bash_tool_exec(cJSON *args) {
  const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(args, "command"));
  if (!cmd)
    return (ToolResult){.ok = false,
                        .output = xstrdup("missing 'command' argument")};

  int pipefd[2];
  if (pipe(pipefd) != 0)
    return (ToolResult){
        .ok = false,
        .output = xasprintf("pipe failed: %s", strerror(errno)),
    };

  pid_t pid = fork();
  if (pid < 0) {
    int e = errno;
    close(pipefd[0]);
    close(pipefd[1]);
    return (ToolResult){.ok = false,
                        .output = xasprintf("fork: %s", strerror(e))};
  }

  if (pid == 0) {
    close(pipefd[0]);
    /* dprintf + _exit avoid stdio-buffer double-flush after fork. */
    if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
        dup2(pipefd[1], STDERR_FILENO) < 0)
      _exit(127);
    close(pipefd[1]);
    execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
    dprintf(STDERR_FILENO, "exec failed: %s\n", strerror(errno));
    _exit(127);
  }

  close(pipefd[1]);
  char buf[4096];
  size_t total=0;
  char *output = NULL;

  while(1){
    ssize_t n = read(pipefd[0], buf, sizeof(buf)-1);
    if (n < 0) {
      int e = errno;
      free(output);
      close(pipefd[0]);
      return (ToolResult){.ok = false,
                          .output = xasprintf("read: %s", strerror(e))};
    }
    if (n == 0)
      break; /* EOF */
    //read some data
    char *new_output = realloc(output, total + n + 1);
    if (!new_output) {
      free(output);
      close(pipefd[0]);
      return (ToolResult){.ok = false,
                          .output = xstrdup("read: out of memory")};
    }
    output = new_output;
    memcpy(output + total, buf, n);
    total += n;
    output[total] = '\0';
  }

  if(!output){
    output = xstrdup("");
  }

  int status;
  waitpid(pid, &status, 0);

  bool ok = false;
  if (WIFEXITED(status)) {
    int ec = WEXITSTATUS(status);
    ok = (ec == 0);
    if (ec!=0) {
      char *tmp= output;
      output = xasprintf("%s\n[process exited with code %d]", output, ec);
      free(tmp);
    }
  }

  else if(WIFSIGNALED(status)){
    int sig = WTERMSIG(status);
    char *tmp= output;
    output = xasprintf("%s\n[process killed by signal %d]", output, sig);
    free(tmp);
  }
  /*
   * TODO(student, Part 1A):
   *
   * You are in the parent. The child is running `cmd` with its stdout and
   * stderr both wired to pipefd[1]; you hold pipefd[0] and pid.
   *
   * Produce a ToolResult describing what the child did. The LLM will read
   * whatever you put in .output verbatim on the next turn, so think about
   * what it would actually want to see — *including* when the command
   * failed. A non-zero exit or a fatal signal is information the LLM needs
   * to react to; see waitpid(2) and the WIFEXITED / WEXITSTATUS /
   * WIFSIGNALED / WTERMSIG macros in <sys/wait.h>.
   *
   * Remember to close pipefd[0] and reap the child before returning.
   */
  // (void)pid;
  close(pipefd[0]);
  return (ToolResult){.ok = ok,
                      .output = output};
  
}