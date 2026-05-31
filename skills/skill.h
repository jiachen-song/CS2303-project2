#ifndef SKILL_H
#define SKILL_H

#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct Skill {
    char *name;
    char *description;
    char *full_prompt;
    char *filepath;
} Skill;

typedef struct SkillStore {
    char *workdir;
    char *skills_dir;
    Skill **skills;
    size_t len;
    size_t cap;
} SkillStore;

SkillStore *skill_store_create(const char *workdir);
void skill_store_free(SkillStore *store);

int skill_load_directory(SkillStore *store, const char *skills_dir);
Skill *skill_create(const char *name, const char *description, const char *full_prompt);
void skill_free(Skill *skill);

const Skill *skill_find(SkillStore *store, const char *name);
cJSON *skill_list_all(SkillStore *store);

int skill_save(SkillStore *store, Skill *skill);
int skill_delete(SkillStore *store, const char *name);

const char *skill_get_name(const Skill *s);
const char *skill_get_description(const Skill *s);
const char *skill_get_full_prompt(const Skill *s);

char *skill_build_intro(SkillStore *store);

#endif