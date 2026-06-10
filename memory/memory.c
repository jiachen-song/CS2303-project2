#define _GNU_SOURCE

#include "memory/memory.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>


#define MEMORY_FILE ".agent/memory.json"

struct MemoryStore {
    char *workdir;
    char *filepath;
    cJSON *entries;
};

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    return mkdir(path, 0755);
}

static char *timestamp_str(void) {
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return xstrdup(buf);
}

MemoryStore *memory_create(const char *workdir) {
    if (!workdir)
        return NULL;

    MemoryStore *m = calloc(1, sizeof(*m));
    if (!m)
        return NULL;

    m->workdir = xstrdup(workdir);

    char agent_dir[PATH_MAX];
    snprintf(agent_dir, sizeof(agent_dir), "%s/.agent", workdir);
    if (ensure_dir(agent_dir) != 0) {
        free(m->workdir);
        free(m);
        return NULL;
    }

    m->filepath = xasprintf("%s/%s", agent_dir, "memory.json");
    m->entries = cJSON_CreateObject();

    if (memory_load(m) != 0)
        fprintf(stderr, "[memory] WARNING: starting with empty memory\n");

    return m;
}

void memory_free(MemoryStore *m) {
    if (!m)
        return;
    free(m->workdir);
    free(m->filepath);
    cJSON_Delete(m->entries);
    free(m);
}

int memory_save(MemoryStore *m) {
    if (!m || !m->filepath)
        return -1;

    /* Atomic write: serialize to a temp file then rename. A crash or
     * partial write leaves the previous good memory.json untouched. */
    char *tmp = xasprintf("%s.tmp", m->filepath);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        free(tmp);
        return -1;
    }

    char *json = cJSON_PrintUnformatted(m->entries);
    if (!json) {
        fclose(f);
        remove(tmp);
        free(tmp);
        return -1;
    }

    if (fprintf(f, "%s", json) < 0) {
        fclose(f);
        remove(tmp);
        free(tmp);
        free(json);
        return -1;
    }
    if (fclose(f) != 0) {
        remove(tmp);
        free(tmp);
        free(json);
        return -1;
    }
    free(json);

    if (rename(tmp, m->filepath) != 0) {
        remove(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

int memory_load(MemoryStore *m) {
    if (!m || !m->filepath)
        return -1;

    FILE *f = fopen(m->filepath, "r");
    if (!f)
        return 0;  /* first start, no file yet — that's fine */

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        fclose(f);
        return 0;
    }

    char *content = xmalloc((size_t)len + 1);
    size_t n = fread(content, 1, (size_t)len, f);
    content[n] = '\0';
    bool read_err = ferror(f);
    fclose(f);

    if (read_err || n == 0) {
        free(content);
        return -1;
    }

    cJSON *parsed = cJSON_Parse(content);
    free(content);

    if (!parsed) {
        /* Corrupt file: rename it to .broken for forensics and let the
         * caller fall back to the empty entries already in m. */
        char *bak = xasprintf("%s.broken", m->filepath);
        rename(m->filepath, bak);
        fprintf(stderr, "[memory] WARNING: %s corrupt, backed up to %s\n",
                m->filepath, bak);
        free(bak);
        return -1;
    }

    cJSON_Delete(m->entries);
    m->entries = parsed;
    return 0;
}

int memory_add(MemoryStore *m, const char *key, const char *value) {
    if (!m || !key)
        return -1;

    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "value", value ? value : "");
    char *ts = timestamp_str();
    cJSON_AddStringToObject(entry, "timestamp", ts);
    free(ts);

    cJSON_AddItemToObject(m->entries, key, entry);
    return memory_save(m);
}

int memory_remove(MemoryStore *m, const char *key) {
    if (!m || !key)
        return -1;

    cJSON_DeleteItemFromObject(m->entries, key);
    return memory_save(m);
}

const char *memory_get(MemoryStore *m, const char *key) {
    if (!m || !key)
        return NULL;

    cJSON *entry = cJSON_GetObjectItem(m->entries, key);
    if (!entry)
        return NULL;

    cJSON *value = cJSON_GetObjectItem(entry, "value");
    if (!value || !cJSON_IsString(value))
        return NULL;

    return value->valuestring;
}

cJSON *memory_get_all(MemoryStore *m) {
    return m ? cJSON_Duplicate(m->entries, true) : NULL;
}

int memory_clear(MemoryStore *m) {
    if (!m)
        return -1;

    cJSON_Delete(m->entries);
    m->entries = cJSON_CreateObject();
    return memory_save(m);
}