#include "evaluation/evaluation.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void eval_result_init(EvaluationResult *result, const char *scenario_name) {
    memset(result, 0, sizeof(*result));
    result->scenario_name = xstrdup(scenario_name);
    result->success = false;
    result->tool_call_rounds = 0;
    result->prompt_tokens = 0;
    result->completion_tokens = 0;
    result->duration_seconds = 0.0;
}

void eval_result_free(EvaluationResult *result) {
    if (!result)
        return;
    free(result->scenario_name);
    free(result->error_message);
    memset(result, 0, sizeof(*result));
}

EvaluationSuite *eval_suite_create(const char *name) {
    EvaluationSuite *suite = calloc(1, sizeof(*suite));
    if (!suite)
        return NULL;
    suite->name = xstrdup(name);
    return suite;
}

void eval_suite_free(EvaluationSuite *suite) {
    if (!suite)
        return;
    free(suite->name);
    for (int i = 0; i < suite->len; i++) {
        eval_result_free(&suite->results[i]);
    }
    free(suite->results);
    free(suite);
}

void eval_suite_add(EvaluationSuite *suite, EvaluationResult *result) {
    if (!suite || !result)
        return;
    if (suite->len >= suite->cap) {
        suite->cap = suite->cap == 0 ? 8 : suite->cap * 2;
        suite->results = realloc(suite->results, suite->cap * sizeof(EvaluationResult));
    }
    memcpy(&suite->results[suite->len], result, sizeof(EvaluationResult));
    suite->len++;
}

int eval_suite_run(EvaluationSuite *suite) {
    (void)suite;
    return 0;
}

int eval_save_results(EvaluationSuite *suite, const char *filepath) {
    if (!suite || !filepath)
        return -1;

    FILE *f = fopen(filepath, "w");
    if (!f)
        return -1;

    fprintf(f, "Evaluation Suite: %s\n", suite->name);
    fprintf(f, "Total Scenarios: %d\n\n", suite->len);

    int total_success = 0;
    int total_prompt_tokens = 0;
    int total_completion_tokens = 0;

    for (int i = 0; i < suite->len; i++) {
        EvaluationResult *r = &suite->results[i];
        fprintf(f, "Scenario %d: %s\n", i + 1, r->scenario_name);
        fprintf(f, "  Success: %s\n", r->success ? "true" : "false");
        fprintf(f, "  Tool Call Rounds: %d\n", r->tool_call_rounds);
        fprintf(f, "  Prompt Tokens: %d\n", r->prompt_tokens);
        fprintf(f, "  Completion Tokens: %d\n", r->completion_tokens);
        fprintf(f, "  Duration: %.2f seconds\n", r->duration_seconds);
        if (r->error_message) {
            fprintf(f, "  Error: %s\n", r->error_message);
        }
        fprintf(f, "\n");

        if (r->success)
            total_success++;
        total_prompt_tokens += r->prompt_tokens;
        total_completion_tokens += r->completion_tokens;
    }

    fprintf(f, "Summary:\n");
    fprintf(f, "  Success Rate: %.1f%% (%d/%d)\n",
            suite->len > 0 ? (double)total_success / suite->len * 100.0 : 0.0,
            total_success, suite->len);
    fprintf(f, "  Total Prompt Tokens: %d\n", total_prompt_tokens);
    fprintf(f, "  Total Completion Tokens: %d\n", total_completion_tokens);

    fclose(f);
    return 0;
}

int eval_load_results(EvaluationSuite *suite, const char *filepath) {
    (void)suite;
    (void)filepath;
    return -1;
}

void eval_print_summary(EvaluationSuite *suite) {
    if (!suite)
        return;

    int total_success = 0;
    int total_prompt_tokens = 0;
    int total_completion_tokens = 0;
    int total_tool_calls = 0;

    for (int i = 0; i < suite->len; i++) {
        EvaluationResult *r = &suite->results[i];
        if (r->success)
            total_success++;
        total_prompt_tokens += r->prompt_tokens;
        total_completion_tokens += r->completion_tokens;
        total_tool_calls += r->tool_call_rounds;
    }

    fprintf(stderr, "\n=== Evaluation Summary: %s ===\n", suite->name);
    fprintf(stderr, "Scenarios Run: %d\n", suite->len);
    fprintf(stderr, "Success Rate: %.1f%% (%d/%d)\n",
            suite->len > 0 ? (double)total_success / suite->len * 100.0 : 0.0,
            total_success, suite->len);
    fprintf(stderr, "Avg Tool Calls: %.1f\n",
            suite->len > 0 ? (double)total_tool_calls / suite->len : 0.0);
    fprintf(stderr, "Total Prompt Tokens: %d\n", total_prompt_tokens);
    fprintf(stderr, "Total Completion Tokens: %d\n", total_completion_tokens);
    fprintf(stderr, "==============================\n\n");
}