#define _GNU_SOURCE

#include "session/session.h"
#include "util.h"
#include "message.h"

#include <dirent.h>
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

int session_save_raw(Session *s, const char *source_tag, const char *json_message) {
    if (!s || !json_message)
        return -1;

    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    int rc;
    if (source_tag && *source_tag)
        rc = fprintf(s->log_file, "[%s] [%s] RAW: %s\n", timestamp, source_tag, json_message);
    else
        rc = fprintf(s->log_file, "[%s] RAW: %s\n", timestamp, json_message);
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

char *session_generate_id(void) {
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm_buf);
    return xstrdup(buf);
}

/* Like session_generate_id but appends a numeric suffix when the same
 * timestamp already exists on disk, ensuring a fresh log file. Caller
 * frees. */
char *session_generate_unique_id(const char *workdir) {
    char base[64];
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(base, sizeof(base), "%Y%m%d-%H%M%S", &tm_buf);

    char path[PATH_MAX];
    if (!workdir)
        workdir = ".";

    for (unsigned int seq = 0; ; seq++) {
        char id[80];
        if (seq == 0)
            snprintf(id, sizeof(id), "%s", base);
        else
            snprintf(id, sizeof(id), "%s-%u", base, seq);
        snprintf(path, sizeof(path), "%s/.agent/sessions/%s.log", workdir, id);
        if (access(path, F_OK) != 0)
            return xstrdup(id);
    }
}

char **session_list(const char *workdir, int *out_count) {
    if (out_count)
        *out_count = 0;
    if (!workdir)
        return NULL;

    char session_dir[PATH_MAX];
    snprintf(session_dir, sizeof(session_dir), "%s/.agent/sessions", workdir);

    DIR *d = opendir(session_dir);
    if (!d)
        return NULL;

    int cap = 0;
    int len = 0;
    char **ids = NULL;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".log") != 0)
            continue;

        if (len >= cap) {
            cap = cap ? cap * 2 : 8;
            ids = xrealloc(ids, (size_t)(cap + 1) * sizeof(char *));
        }

        size_t namelen = (size_t)(dot - ent->d_name);
        char *id = xmalloc(namelen + 1);
        memcpy(id, ent->d_name, namelen);
        id[namelen] = '\0';
        ids[len++] = id;
    }
    closedir(d);

    if (ids) {
        ids[len] = NULL;
    }
    if (out_count)
        *out_count = len;
    return ids;
}

void session_list_free(char **ids) {
    if (!ids)
        return;
    for (int i = 0; ids[i]; i++)
        free(ids[i]);
    free(ids);
}