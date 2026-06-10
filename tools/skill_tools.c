#include "tools/skill_tools.h"
#include "skills/skill.h"
#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SkillStore *g_skill_store = NULL;

ToolDef skill_list_def = {
    .name = "skill_list",
    .desc = "列出所有可用技能",
    .param_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
    .exec = skill_list_exec,
    .read_only = true,
};

ToolDef skill_load_def = {
    .name = "skill_load",
    .desc = "加载技能完整提示（当LLM识别到相关技能时调用）",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"name\":{\"type\":\"string\",\"description\":\"技能名称\"}},"
                    "\"required\":[\"name\"]}",
    .exec = skill_load_exec,
    .read_only = true,
};

ToolDef skill_info_def = {
    .name = "skill_info",
    .desc = "获取技能的简要信息和描述",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"name\":{\"type\":\"string\",\"description\":\"技能名称\"}},"
                    "\"required\":[\"name\"]}",
    .exec = skill_info_exec,
    .read_only = true,
};

ToolResult skill_list_exec(cJSON *args) {
    (void)args;

    if (!g_skill_store) {
        return (ToolResult){.ok = false, .output = xstrdup("Skill store not initialized")};
    }

    cJSON *list = skill_list_all(g_skill_store);
    if (!list) {
        return (ToolResult){.ok = true, .output = xstrdup("No skills available")};
    }

    char *output = cJSON_Print(list);
    cJSON_Delete(list);
    return (ToolResult){.ok = true, .output = output ? output : xstrdup("")};
}

ToolResult skill_load_exec(cJSON *args) {
    cJSON *name_json = cJSON_GetObjectItem(args, "name");
    if (!cJSON_IsString(name_json) || !name_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'name' argument")};
    }

    if (!g_skill_store) {
        return (ToolResult){.ok = false, .output = xstrdup("Skill store not initialized")};
    }

    const Skill *skill = skill_find(g_skill_store, name_json->valuestring);
    if (!skill) {
        char *output = xasprintf("Skill not found: %s", name_json->valuestring);
        return (ToolResult){.ok = false, .output = output};
    }

    char *output = xasprintf("=== Skill: %s ===\n%s", skill->name, skill->full_prompt);
    return (ToolResult){.ok = true, .output = output};
}

ToolResult skill_info_exec(cJSON *args) {
    cJSON *name_json = cJSON_GetObjectItem(args, "name");
    if (!cJSON_IsString(name_json) || !name_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'name' argument")};
    }

    if (!g_skill_store) {
        return (ToolResult){.ok = false, .output = xstrdup("Skill store not initialized")};
    }

    const Skill *skill = skill_find(g_skill_store, name_json->valuestring);
    if (!skill) {
        char *output = xasprintf("Skill not found: %s", name_json->valuestring);
        return (ToolResult){.ok = false, .output = output};
    }

    char *output = xasprintf("Name: %s\nDescription: %s", skill->name, skill->description);
    return (ToolResult){.ok = true, .output = output};
}

void skill_tools_set_store(SkillStore *store) {
    g_skill_store = store;
}

SkillStore *skill_tools_get_store(void) {
    return g_skill_store;
}

void skill_tools_cleanup(void) {
    if (g_skill_store) {
        skill_store_free(g_skill_store);
        g_skill_store = NULL;
    }
}