#define _GNU_SOURCE

#include "memory/memory.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
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
    char *ts = xasprintf("%ld", (long)now);
    return ts;
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

    memory_load(m);

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

    FILE *f = fopen(m->filepath, "w");
    if (!f)
        return -1;

    char *json = cJSON_Print(m->entries);
    if (!json) {
        fclose(f);
        return -1;
    }

    fprintf(f, "%s", json);
    fclose(f);
    free(json);
    return 0;
}

int memory_load(MemoryStore *m) {
    if (!m || !m->filepath)
        return -1;

    FILE *f = fopen(m->filepath, "r");
    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        fclose(f);
        return 0;
    }

    char *content = malloc(len + 1);
    if (!content) {
        fclose(f);
        return -1;
    }

    fread(content, 1, len, f);
    content[len] = '\0';
    fclose(f);

    cJSON_Delete(m->entries);
    m->entries = cJSON_Parse(content);
    free(content);

    if (!m->entries) {
        m->entries = cJSON_CreateObject();
        return -1;
    }

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

int memory_update(MemoryStore *m, const char *key, const char *value) {
    if (!m || !key)
        return -1;

    cJSON *entry = cJSON_GetObjectItem(m->entries, key);
    if (!entry) {
        return memory_add(m, key, value);
    }

    cJSON_ReplaceItemInObject(entry, "value", cJSON_CreateString(value ? value : ""));
    char *ts = timestamp_str();
    cJSON_ReplaceItemInObject(entry, "timestamp", cJSON_CreateString(ts));
    free(ts);

    return memory_save(m);
}

int memory_clear(MemoryStore *m) {
    if (!m)
        return -1;

    cJSON_Delete(m->entries);
    m->entries = cJSON_CreateObject();
    return memory_save(m);
}

int memory_add_entry(MemoryStore *m, MemoryEntry *entry) {
    if (!m || !entry || !entry->key)
        return -1;
    return memory_add(m, entry->key, entry->value);
}