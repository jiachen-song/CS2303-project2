#ifndef AGENT_RUN_H
#define AGENT_RUN_H

#include "context/context.h"
#include "session/session.h"

/*
 * Shared turn loop used by both the main agent and sub-agents.
 *
 * Runs up to max_turns LLM turns against the given context, executing any
 * tool calls the model asks for. The loop is identical to the main agent's
 * per-turn flow but with no UI hooks — the caller drives rendering.
 *
 * On success, the assistant's final text reply is returned (heap-allocated,
 * caller frees). NULL is returned on error (caller should inspect stderr).
 *
 * If `session` is non-NULL, every user / assistant / tool message is also
 * appended to that session log via session_save_raw — the same behaviour
 * the main agent already had.
 *
 * If `metrics` is non-NULL it is filled in with the per-loop counters.
 */
typedef struct {
    int rounds;
    int tool_calls;
    int prompt_tokens;     /* last-ctx token count after the final turn */
    int completion_tokens; /* estimated; real accounting would need API usage */
} AgentRunMetrics;

/*
 * Run the LLM turn loop. `source_tag`, if non-NULL, is passed through to
 * session_save_raw so the caller can mark the source of each message
 * (e.g. "subagent:sa-1"). `forbidden_tools`, if non-NULL, is a NULL-
 * terminated array of tool names that the loop will refuse to execute —
 * any call to a forbidden tool is short-circuited with an error message.
 * Pass both as NULL for the main agent.
 */
char *agent_run_turns(Context *ctx,
                      const char *system_prompt,
                      const char *model,
                      int max_turns,
                      Session *session,
                      const char *source_tag,
                      const char *const *forbidden_tools,
                      AgentRunMetrics *metrics);

#endif
