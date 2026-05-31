#ifndef EVALUATION_H
#define EVALUATION_H

#include <stdbool.h>

typedef struct EvaluationResult {
    char *scenario_name;
    bool success;
    int tool_call_rounds;
    int prompt_tokens;
    int completion_tokens;
    double duration_seconds;
    char *error_message;
} EvaluationResult;

typedef struct {
    char *name;
    EvaluationResult *results;
    int len;
    int cap;
} EvaluationSuite;

EvaluationSuite *eval_suite_create(const char *name);
void eval_suite_free(EvaluationSuite *suite);

void eval_suite_add(EvaluationSuite *suite, EvaluationResult *result);
int eval_suite_run(EvaluationSuite *suite);

void eval_result_init(EvaluationResult *result, const char *scenario_name);
void eval_result_free(EvaluationResult *result);

int eval_save_results(EvaluationSuite *suite, const char *filepath);
int eval_load_results(EvaluationSuite *suite, const char *filepath);

void eval_print_summary(EvaluationSuite *suite);

#endif