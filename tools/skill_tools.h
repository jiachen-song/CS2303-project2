#ifndef SKILL_TOOLS_H
#define SKILL_TOOLS_H

#include "cJSON.h"
#include "tools/tools.h"

typedef struct SkillStore SkillStore;

extern ToolDef skill_list_def;
extern ToolDef skill_load_def;
extern ToolDef skill_info_def;

ToolResult skill_list_exec(cJSON *args);
ToolResult skill_load_exec(cJSON *args);
ToolResult skill_info_exec(cJSON *args);

void skill_tools_set_store(SkillStore *store);
SkillStore *skill_tools_get_store(void);
void skill_tools_cleanup(void);

#endif