#ifndef SESSION_H
#define SESSION_H

#include "message.h"
#include <stdbool.h>

typedef struct Session Session;

Session *session_create(const char *workdir, const char *session_id);
void session_free(Session *s);

int session_save_message(Session *s, const char *role, const char *content);
int session_save_raw(Session *s, const char *json_message);

int session_load(Session *s);
int session_replay(Session *s, MessageList *out_messages);

const char *session_get_id(Session *s);
int session_get_message_count(Session *s);

int session_clear(Session *s);

#endif