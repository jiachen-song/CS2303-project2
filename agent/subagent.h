#ifndef SUBAGENT_H
#define SUBAGENT_H

#include "cJSON.h"
#include <stdbool.h>

typedef struct SubAgent SubAgent;

typedef struct {
    char *scenario_name;
    int rounds;
    int tool_calls;
    int prompt_tokens;
    int completion_tokens;
} SubAgentMetrics;

SubAgent *subagent_create(const char *workdir, int context_window);
void subagent_free(SubAgent *s);

/*
 * Execute a single bounded task in the subagent's isolated context.
 *
 * `id` is the subagent's spawn_id (e.g. "sa-1"); it is used purely as a
 * session-log marker so the parent can later tell which messages came
 * from this subagent. The caller is responsible for generating and
 * managing the id.
 *
 * `metrics`, if non-NULL, is filled in with the per-execute counters.
 *
 * Returns the subagent's final text reply (heap-allocated, caller frees).
 * NULL is returned on error.
 */
char *subagent_execute(SubAgent *s, const char *task, const char *id,
                       SubAgentMetrics *metrics);

void subagent_set_memory(SubAgent *s, cJSON *memory);

#endif
