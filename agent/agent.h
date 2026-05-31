#ifndef AGENT_H
#define AGENT_H

#include "context/context.h"

/*
 * Opaque agent handle. Early phases can keep only transient per-call state
 * here; Phase C will likely add persistent history for multi-turn dialogue.
 */
typedef struct Agent Agent;

Agent *agent_create(void);
void agent_free(Agent *a);

/*
 * Run one "turn": send user_input to the LLM, execute any tool it asks for,
 * and return the assistant's final text reply. Returned pointer is owned by
 * the agent and is invalidated by the next agent_chat call. Returns NULL on
 * error (and writes a human-readable message via stderr).
 */
const char *agent_chat(Agent *a, const char *user_input);

/* Get agent's context for inspection (e.g., token usage). */
Context *agent_ctx(Agent *a);

#endif
