#ifndef MEMORY_H
#define MEMORY_H

#include "cJSON.h"
#include <stdbool.h>

typedef struct MemoryStore MemoryStore;

MemoryStore *memory_create(const char *workdir);
void memory_free(MemoryStore *m);

int memory_save(MemoryStore *m);
int memory_load(MemoryStore *m);

int memory_add(MemoryStore *m, const char *key, const char *value);
int memory_remove(MemoryStore *m, const char *key);

const char *memory_get(MemoryStore *m, const char *key);
cJSON *memory_get_all(MemoryStore *m);

int memory_update(MemoryStore *m, const char *key, const char *value);

int memory_clear(MemoryStore *m);

typedef struct {
    char *key;
    char *value;
    char *timestamp;
} MemoryEntry;

int memory_add_entry(MemoryStore *m, MemoryEntry *entry);

#endif