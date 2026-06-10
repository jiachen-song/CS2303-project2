#define _GNU_SOURCE

#include "skills/skill.h"
#include "util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    return mkdir(path, 0755);
}

/*
 * Skill name whitelist: alnum, '-', '_' only. Length 1..64. No '.' to
 * prevent path-traversal payloads like "..". Path is built by
 * snprintf("%s/%s.skill", skills_dir, name), so the name is the only
 * attacker-controlled component of the resulting path.
 */
static bool is_valid_skill_name(const char *name) {
    if (!name || !*name)
        return false;
    size_t len = strlen(name);
    if (len > 64)
        return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '-' || c == '_'))
            return false;
    }
    return true;
}

static void skill_init(Skill *s, const char *name, const char *description, const char *full_prompt) {
    s->name = xstrdup(name);
    s->description = xstrdup(description ? description : "");
    s->full_prompt = xstrdup(full_prompt ? full_prompt : "");
    s->filepath = NULL;
}

Skill *skill_create(const char *name, const char *description, const char *full_prompt) {
    if (!name)
        return NULL;

    Skill *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;

    skill_init(s, name, description, full_prompt);
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
        Skill **new_skills = xrealloc(store->skills, new_cap * sizeof(Skill *));
        store->skills = new_skills;
        store->cap = new_cap;
    }

    store->skills[store->len++] = skill;
    return 0;
}

/*
 * Parse a .skill file. The on-disk format is JSON:
 *   { "name": "...", "description": "...", "full_prompt": "..." }
 *
 * On success, the caller owns *out_name / *out_description / *out_full_prompt
 * (each heap-allocated, may be NULL if the field is missing).
 * Returns 0 on success, -1 on read/parse error.
 */
static int parse_skill_file(const char *filepath, char **out_name,
                            char **out_description, char **out_full_prompt) {
    *out_name = NULL;
    *out_description = NULL;
    *out_full_prompt = NULL;

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

    char *content = xmalloc((size_t)len + 1);
    size_t n = fread(content, 1, (size_t)len, f);
    content[n] = '\0';
    bool read_err = ferror(f);
    fclose(f);
    if (read_err || n == 0) {
        free(content);
        return -1;
    }

    cJSON *root = cJSON_Parse(content);
    free(content);
    if (!root) {
        fprintf(stderr, "[skills] WARNING: %s is not valid JSON, skipping\n",
                filepath);
        return -1;
    }

    cJSON *name_node = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *desc_node = cJSON_GetObjectItemCaseSensitive(root, "description");
    cJSON *prompt_node = cJSON_GetObjectItemCaseSensitive(root, "full_prompt");

    if (cJSON_IsString(name_node) && name_node->valuestring)
        *out_name = xstrdup(name_node->valuestring);
    if (cJSON_IsString(desc_node) && desc_node->valuestring)
        *out_description = xstrdup(desc_node->valuestring);
    if (cJSON_IsString(prompt_node) && prompt_node->valuestring)
        *out_full_prompt = xstrdup(prompt_node->valuestring);

    cJSON_Delete(root);
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
        if (entry->d_name[0] == '.')
            continue;

        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".skill") != 0)
            continue;

        /* d_type is DT_UNKNOWN on some filesystems (reiserfs, FUSE, …);
         * fall back to stat() in that case. */
        bool is_reg;
        if (entry->d_type == DT_UNKNOWN) {
            char p[PATH_MAX];
            snprintf(p, sizeof(p), "%s/%s", skills_dir, entry->d_name);
            struct stat st;
            if (stat(p, &st) != 0 || !S_ISREG(st.st_mode))
                continue;
            is_reg = true;
        } else {
            if (entry->d_type != DT_REG)
                continue;
            is_reg = true;
        }
        (void)is_reg;

        char filepath[PATH_MAX];
        snprintf(filepath, sizeof(filepath), "%s/%s", skills_dir, entry->d_name);

        char *name = NULL, *description = NULL, *full_prompt = NULL;
        if (parse_skill_file(filepath, &name, &description, &full_prompt) != 0) {
            free(name);
            free(description);
            free(full_prompt);
            continue;
        }

        if (!name || !is_valid_skill_name(name)) {
            fprintf(stderr, "[skills] WARNING: %s has invalid 'name' field, skipping\n",
                    filepath);
            free(name);
            free(description);
            free(full_prompt);
            continue;
        }

        if (skill_find(store, name)) {
            fprintf(stderr, "[skills] WARNING: duplicate skill '%s' in %s, skipping\n",
                    name, filepath);
            free(name);
            free(description);
            free(full_prompt);
            continue;
        }

        Skill *skill = skill_create(name, description, full_prompt);
        free(name);
        free(description);
        free(full_prompt);
        if (!skill)
            continue;

        skill->filepath = xstrdup(filepath);
        if (skill_add(store, skill) != 0) {
            fprintf(stderr, "[skills] WARNING: out of memory adding '%s'\n", filepath);
            skill_free(skill);
        }
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
    if (!is_valid_skill_name(skill->name))
        return -1;

    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.skill", store->skills_dir, skill->name);

    cJSON *root = cJSON_CreateObject();
    if (!root)
        return -1;
    cJSON_AddStringToObject(root, "name", skill->name);
    cJSON_AddStringToObject(root, "description", skill->description ? skill->description : "");
    cJSON_AddStringToObject(root, "full_prompt", skill->full_prompt ? skill->full_prompt : "");

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json)
        return -1;

    /* Atomic write: temp + rename. */
    char *tmp = xasprintf("%s.tmp", filepath);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        free(tmp);
        free(json);
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

    if (rename(tmp, filepath) != 0) {
        remove(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

char *skill_build_intro(SkillStore *store) {
    if (!store || store->len == 0)
        return xstrdup("");

    size_t total_len = 0;
    for (size_t i = 0; i < store->len; i++) {
        /* Per skill: "- " (2) + name + ": " (2) + description + "\n" (1) */
        total_len += 2 + strlen(store->skills[i]->name) + 2 +
                     strlen(store->skills[i]->description) + 1;
    }

    char *result = xmalloc(total_len + 1);
    result[0] = '\0';
    for (size_t i = 0; i < store->len; i++) {
        /* Use a running offset so we don't strcat (O(n²)). */
        size_t off = strlen(result);
        snprintf(result + off, total_len + 1 - off, "- %s: %s\n",
                 store->skills[i]->name, store->skills[i]->description);
    }

    return result;
}

int skill_delete(SkillStore *store, const char *name) {
    if (!store || !name)
        return -1;
    if (!is_valid_skill_name(name))
        return -1;

    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.skill", store->skills_dir, name);

    int file_err = remove(filepath);
    if (file_err != 0 && errno != ENOENT)
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
