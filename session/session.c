#define _GNU_SOURCE

#include "session/session.h"
#include "util.h"
#include "message.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

struct Session {
    char *session_id;
    char *workdir;
    char *log_path;
    FILE *log_file;
    int message_count;
};

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    return mkdir(path, 0755);
}

Session *session_create(const char *workdir, const char *session_id) {
    if (!workdir || !session_id)
        return NULL;

    Session *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;

    s->session_id = xstrdup(session_id);
    s->workdir = xstrdup(workdir);

    char session_dir[PATH_MAX];
    snprintf(session_dir, sizeof(session_dir), "%s/.agent/sessions", workdir);
    if (ensure_dir(session_dir) != 0) {
        free(s->session_id);
        free(s->workdir);
        free(s);
        return NULL;
    }

    s->log_path = xasprintf("%s/%s.log", session_dir, session_id);

    if (access(s->log_path, F_OK) == 0) {
        s->log_file = fopen(s->log_path, "a");
    } else {
        s->log_file = fopen(s->log_path, "w");
    }
    if (!s->log_file) {
        free(s->session_id);
        free(s->workdir);
        free(s->log_path);
        free(s);
        return NULL;
    }

    return s;
}

Session *session_open(const char *workdir, const char *session_id) {
    if (!workdir || !session_id)
        return NULL;

    Session *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;

    s->session_id = xstrdup(session_id);
    s->workdir = xstrdup(workdir);

    char session_dir[PATH_MAX];
    snprintf(session_dir, sizeof(session_dir), "%s/.agent/sessions", workdir);
    if (ensure_dir(session_dir) != 0) {
        free(s->session_id);
        free(s->workdir);
        free(s);
        return NULL;
    }

    s->log_path = xasprintf("%s/%s.log", session_dir, session_id);

    s->log_file = fopen(s->log_path, "r");
    if (!s->log_file) {
        free(s->session_id);
        free(s->workdir);
        free(s->log_path);
        free(s);
        return NULL;
    }

    return s;
}

void session_free(Session *s) {
    if (!s)
        return;
    if (s->log_file)
        fclose(s->log_file);
    free(s->session_id);
    free(s->workdir);
    free(s->log_path);
    free(s);
}

const char *session_get_id(Session *s) {
    return s ? s->session_id : NULL;
}

int session_get_message_count(Session *s) {
    return s ? s->message_count : 0;
}

int session_save_message(Session *s, const char *role, const char *content) {
    if (!s || !role || !content)
        return -1;

    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    int rc = fprintf(s->log_file, "[%s] %s: %s\n", timestamp, role, content);
    if (rc < 0)
        return -1;

    fflush(s->log_file);
    s->message_count++;
    return 0;
}

int session_save_raw(Session *s, const char *json_message) {
    if (!s || !json_message)
        return -1;

    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    int rc = fprintf(s->log_file, "[%s] RAW: %s\n", timestamp, json_message);
    if (rc < 0)
        return -1;

    fflush(s->log_file);
    s->message_count++;
    return 0;
}

int session_load(Session *s) {
    if (!s || !s->log_path)
        return -1;

    FILE *f = fopen(s->log_path, "r");
    if (!f)
        return -1;

    char line[8192];
    s->message_count = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "RAW: ") != NULL) {
            s->message_count++;
        }
    }

    fclose(f);
    return 0;
}

int session_reopen_for_write(Session *s) {
    if (!s || !s->log_path)
        return -1;

    if (s->log_file) {
        fclose(s->log_file);
        s->log_file = NULL;
    }

    s->log_file = fopen(s->log_path, "a");
    if (!s->log_file)
        return -1;

    return 0;
}

int session_replay(Session *s, MessageList *out_messages) {
    if (!s || !s->log_path || !out_messages)
        return -1;

    FILE *f = fopen(s->log_path, "r");
    if (!f)
        return -1;

    char line[8192];
    int count = 0;

    while (fgets(line, sizeof(line), f)) {
        char *json_start = strstr(line, "RAW: ");
        if (json_start) {
            json_start += 5;
            size_t len = strlen(json_start);
            while (len > 0 && (json_start[len - 1] == '\n' || json_start[len - 1] == '\r')) {
                json_start[--len] = '\0';
            }
            if (len > 0) {
                char *msg = xstrdup(json_start);
                msg_list_push(out_messages, msg);
                count++;
            }
        }
    }

    fclose(f);
    return count;
}

int session_clear(Session *s) {
    if (!s)
        return -1;

    if (s->log_file) {
        fclose(s->log_file);
        s->log_file = NULL;
    }

    if (s->log_path) {
        remove(s->log_path);
        s->log_file = fopen(s->log_path, "a");
        if (!s->log_file)
            return -1;
    }

    s->message_count = 0;
    return 0;
}