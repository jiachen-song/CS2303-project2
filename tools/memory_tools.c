#include "tools/memory_tools.h"
#include "memory/memory.h"
#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MemoryStore *g_memory_store = NULL;

ToolDef memory_write_def = {
    .name = "memory_write",
    .desc = "将信息写入项目记忆存储",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"key\":{\"type\":\"string\",\"description\":\"记忆键名\"},"
                    "\"value\":{\"type\":\"string\",\"description\":\"要存储的信息\"}},"
                    "\"required\":[\"key\",\"value\"]}",
    .exec = memory_write_exec,
    .read_only = false,
};

ToolDef memory_read_def = {
    .name = "memory_read",
    .desc = "从项目记忆存储读取信息",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"key\":{\"type\":\"string\",\"description\":\"记忆键名\"}},"
                    "\"required\":[\"key\"]}",
    .exec = memory_read_exec,
    .read_only = true,
};

ToolDef memory_list_def = {
    .name = "memory_list",
    .desc = "列出所有项目记忆",
    .param_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
    .exec = memory_list_exec,
    .read_only = true,
};

ToolDef memory_clear_def = {
    .name = "memory_clear",
    .desc = "清除所有项目记忆",
    .param_schema = "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
    .exec = memory_clear_exec,
    .read_only = false,
};

MemoryStore *memory_tools_get_store(void) {
    if (!g_memory_store) {
        g_memory_store = memory_create(g_config.workdir);
    }
    return g_memory_store;
}

ToolResult memory_write_exec(cJSON *args) {
    cJSON *key_json = cJSON_GetObjectItem(args, "key");
    cJSON *value_json = cJSON_GetObjectItem(args, "value");

    if (!cJSON_IsString(key_json) || !key_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'key' argument")};
    }
    if (!cJSON_IsString(value_json)) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'value' argument")};
    }

    MemoryStore *store = memory_tools_get_store();
    if (!store) {
        return (ToolResult){.ok = false, .output = xstrdup("Failed to access memory store")};
    }

    if (memory_add(store, key_json->valuestring, value_json->valuestring) == 0) {
        char *output = xasprintf("Memory written: %s", key_json->valuestring);
        return (ToolResult){.ok = true, .output = output};
    }

    return (ToolResult){.ok = false, .output = xstrdup("Failed to write memory")};
}

ToolResult memory_read_exec(cJSON *args) {
    cJSON *key_json = cJSON_GetObjectItem(args, "key");

    if (!cJSON_IsString(key_json) || !key_json->valuestring[0]) {
        return (ToolResult){.ok = false, .output = xstrdup("missing 'key' argument")};
    }

    MemoryStore *store = memory_tools_get_store();
    if (!store) {
        return (ToolResult){.ok = false, .output = xstrdup("Failed to access memory store")};
    }

    const char *value = memory_get(store, key_json->valuestring);
    if (value) {
        char *output = xasprintf("%s", value);
        return (ToolResult){.ok = true, .output = output};
    }

    return (ToolResult){.ok = false, .output = xasprintf("Key not found: %s", key_json->valuestring)};
}

ToolResult memory_list_exec(cJSON *args) {
    (void)args;

    MemoryStore *store = memory_tools_get_store();
    if (!store) {
        return (ToolResult){.ok = false, .output = xstrdup("Failed to access memory store")};
    }

    cJSON *all = memory_get_all(store);
    if (!all) {
        return (ToolResult){.ok = true, .output = xstrdup("No memory entries")};
    }

    char *output = cJSON_Print(all);
    cJSON_Delete(all);
    return (ToolResult){.ok = true, .output = output ? output : xstrdup("")};
}

ToolResult memory_clear_exec(cJSON *args) {
    (void)args;

    MemoryStore *store = memory_tools_get_store();
    if (!store) {
        return (ToolResult){.ok = false, .output = xstrdup("Failed to access memory store")};
    }

    if (memory_clear(store) == 0) {
        return (ToolResult){.ok = true, .output = xstrdup("Memory cleared")};
    }

    return (ToolResult){.ok = false, .output = xstrdup("Failed to clear memory")};
}

void memory_tools_cleanup(void) {
    if (g_memory_store) {
        memory_free(g_memory_store);
        g_memory_store = NULL;
    }
}