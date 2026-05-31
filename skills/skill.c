#define _GNU_SOURCE

#include "skills/skill.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    return mkdir(path, 0755);
}

static void skill_init(Skill *s, const char *name, const char *description, const char *full_prompt) {
    s->name = xstrdup(name);
    s->description = xstrdup(description);
    s->full_prompt = xstrdup(full_prompt);
    s->filepath = NULL;
}

Skill *skill_create(const char *name, const char *description, const char *full_prompt) {
    if (!name)
        return NULL;

    Skill *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;

    skill_init(s, name, description ? description : "", full_prompt ? full_prompt : "");
    if (full_prompt) {
        free(s->full_prompt);
        s->full_prompt = xstrdup(full_prompt);
    }

    return s;
}

void skill_free(Skill *s) {
    if (!s)
        return;
    free(s->name);
    free(s->description);
    free(s->full_prompt);
    free(s->filepath);
    free(s);
}

SkillStore *skill_store_create(const char *workdir) {
    if (!workdir)
        return NULL;

    SkillStore *store = calloc(1, sizeof(*store));
    if (!store)
        return NULL;

    store->workdir = xstrdup(workdir);
    store->skills_dir = xasprintf("%s/.agent/skills", workdir);

    if (ensure_dir(store->skills_dir) != 0) {
        free(store->workdir);
        free(store->skills_dir);
        free(store);
        return NULL;
    }

    return store;
}

void skill_store_free(SkillStore *store) {
    if (!store)
        return;
    for (size_t i = 0; i < store->len; i++) {
        skill_free(store->skills[i]);
    }
    free(store->skills);
    free(store->workdir);
    free(store->skills_dir);
    free(store);
}

const char *skill_get_name(const Skill *s) {
    return s ? s->name : NULL;
}

const char *skill_get_description(const Skill *s) {
    return s ? s->description : NULL;
}

const char *skill_get_full_prompt(const Skill *s) {
    return s ? s->full_prompt : NULL;
}

const Skill *skill_find(SkillStore *store, const char *name) {
    if (!store || !name)
        return NULL;
    for (size_t i = 0; i < store->len; i++) {
        if (strcmp(store->skills[i]->name, name) == 0)
            return store->skills[i];
    }
    return NULL;
}

static int skill_add(SkillStore *store, Skill *skill) {
    if (!store || !skill)
        return -1;

    if (store->len >= store->cap) {
        size_t new_cap = store->cap == 0 ? 8 : store->cap * 2;
        Skill **new_skills = realloc(store->skills, new_cap * sizeof(Skill *));
        if (!new_skills)
            return -1;
        store->skills = new_skills;
        store->cap = new_cap;
    }

    store->skills[store->len++] = skill;
    return 0;
}

static int parse_skill_file(const char *filepath, char **out_name, char **out_description, char **out_full_prompt) {
    FILE *f = fopen(filepath, "r");
    if (!f)
        return -1;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        fclose(f);
        return -1;
    }

    char *content = malloc(len + 1);
    if (!content) {
        fclose(f);
        return -1;
    }

    fread(content, 1, len, f);
    content[len] = '\0';
    fclose(f);

    *out_name = NULL;
    *out_description = NULL;
    *out_full_prompt = NULL;

    char *desc_start = strstr(content, "---\n");
    if (desc_start) {
        char *desc_end = strstr(desc_start + 4, "---\n");
        if (desc_end) {
            size_t desc_len = desc_end - (desc_start + 4);
            *out_description = malloc(desc_len + 1);
            if (*out_description) {
                memcpy(*out_description, desc_start + 4, desc_len);
                (*out_description)[desc_len] = '\0';
            }

            char *name_start = content;
            char *name_end = desc_start;
            while (name_end > name_start && (name_end[-1] == '\n' || name_end[-1] == '\r' || name_end[-1] == ' ' || name_end[-1] == '\t')) {
                name_end--;
            }
            size_t name_len = name_end - name_start;
            *out_name = malloc(name_len + 1);
            if (*out_name) {
                memcpy(*out_name, name_start, name_len);
                (*out_name)[name_len] = '\0';
            }

            *out_full_prompt = xstrdup(desc_end + 4);
        }
    }

    free(content);
    return 0;
}

int skill_load_directory(SkillStore *store, const char *skills_dir) {
    if (!store || !skills_dir)
        return -1;

    DIR *dir = opendir(skills_dir);
    if (!dir)
        return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG)
            continue;

        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".skill") != 0)
            continue;

        char filepath[PATH_MAX];
        snprintf(filepath, sizeof(filepath), "%s/%s", skills_dir, entry->d_name);

        char *name = NULL, *description = NULL, *full_prompt = NULL;
        if (parse_skill_file(filepath, &name, &description, &full_prompt) == 0 && name) {
            Skill *skill = skill_create(name, description, full_prompt);
            if (skill) {
                skill->filepath = xstrdup(filepath);
                skill_add(store, skill);
            }
        }
        free(name);
        free(description);
        free(full_prompt);
    }

    closedir(dir);
    return 0;
}

cJSON *skill_list_all(SkillStore *store) {
    if (!store)
        return NULL;

    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < store->len; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", store->skills[i]->name);
        cJSON_AddStringToObject(obj, "description", store->skills[i]->description);
        cJSON_AddItemToArray(arr, obj);
    }
    return arr;
}

int skill_save(SkillStore *store, Skill *skill) {
    if (!store || !skill || !skill->name)
        return -1;

    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.skill", store->skills_dir, skill->name);

    FILE *f = fopen(filepath, "w");
    if (!f)
        return -1;

    fprintf(f, "%s\n---\n%s\n---\n%s\n", skill->name, skill->description, skill->full_prompt);
    fclose(f);

    return 0;
}

char *skill_build_intro(SkillStore *store) {
    if (!store || store->len == 0)
        return xstrdup("");

    size_t total_len = 0;
    for (size_t i = 0; i < store->len; i++) {
        total_len += strlen(store->skills[i]->name) + 2;
        total_len += strlen(store->skills[i]->description) + 2;
    }

    char *result = malloc(total_len + 1);
    if (!result)
        return xstrdup("");

    result[0] = '\0';
    for (size_t i = 0; i < store->len; i++) {
        strcat(result, "- ");
        strcat(result, store->skills[i]->name);
        strcat(result, ": ");
        strcat(result, store->skills[i]->description);
        strcat(result, "\n");
    }

    return result;
}

int skill_delete(SkillStore *store, const char *name) {
    if (!store || !name)
        return -1;

    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.skill", store->skills_dir, name);

    if (remove(filepath) != 0)
        return -1;

    for (size_t i = 0; i < store->len; i++) {
        if (strcmp(store->skills[i]->name, name) == 0) {
            skill_free(store->skills[i]);
            memmove(&store->skills[i], &store->skills[i + 1],
                    (store->len - i - 1) * sizeof(Skill *));
            store->len--;
            return 0;
        }
    }
    return 0;
}