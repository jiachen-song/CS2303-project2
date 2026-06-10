#ifndef SESSION_H
#define SESSION_H

#include "message.h"
#include <stdbool.h>

typedef struct Session Session;

Session *session_create(const char *workdir, const char *session_id);
Session *session_open(const char *workdir, const char *session_id);
void session_free(Session *s);

int session_save_message(Session *s, const char *role, const char *content);
int session_save_raw(Session *s, const char *json_message);

int session_load(Session *s);
int session_replay(Session *s, MessageList *out_messages);
int session_reopen_for_write(Session *s);

const char *session_get_id(Session *s);
int session_get_message_count(Session *s);

int session_clear(Session *s);

/* Generate a unique session id like "20260610-132500" (caller frees). */
char *session_generate_id(void);

/* Like session_generate_id but ensures the id does not collide with an
 * existing log file under <workdir>/.agent/sessions/. */
char *session_generate_unique_id(const char *workdir);

/* List all session log files in workdir/.agent/sessions/.
 * Returns a NULL-terminated array of session ids (basename without .log).
 * Caller frees with session_list_free. Returns NULL on error. */
char **session_list(const char *workdir, int *out_count);
void session_list_free(char **ids);

#endif