#ifndef SUBAGENT_H
#define SUBAGENT_H

#include "cJSON.h"
#include <stdbool.h>

typedef struct SubAgent SubAgent;

typedef struct {
    char *task_description;
    char *result;
    bool success;
    char *error;
} SubAgentTask;

typedef struct {
    char *scenario_name;
    int rounds;
    int tool_calls;
    int prompt_tokens;
    int completion_tokens;
} SubAgentMetrics;

SubAgent *subagent_create(const char *workdir, int context_window);
void subagent_free(SubAgent *s);

const char *subagent_execute(SubAgent *s, const char *task, SubAgentMetrics *metrics);

void subagent_set_memory(SubAgent *s, cJSON *memory);

/* Optional path for an independent debug log under .agent/subagents/. */
void subagent_set_log_path(SubAgent *s, const char *log_path);

char *subagent_format_result(const char *task, const char *result, bool success);

#endif