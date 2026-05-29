#include "tools.h"
#include "sandbox.h"
#include "util.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ToolResult tool_edit_file_exec(cJSON *args);

ToolDef edit_file_def = {
    .name = "edit_file",
    .desc = "替换文件中第一个精确匹配的 old_text",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"path\":{\"type\":\"string\",\"description\":\"工作区内的相对路径\"},"
                    "\"old_text\":{\"type\":\"string\",\"description\":\"要查找的精确子字符串\"},"
                    "\"new_text\":{\"type\":\"string\",\"description\":\"替换文本\"}},"
                    "\"required\":[\"path\",\"old_text\",\"new_text\"]}",
    .exec = tool_edit_file_exec,
    .read_only = false,
};

static ToolResult tool_edit_file_exec(cJSON *args) {
    cJSON *path_json = cJSON_GetObjectItem(args, "path");
    if (!cJSON_IsString(path_json) || !path_json->valuestring[0])
        return (ToolResult){.ok = false, .output = xstrdup("missing 'path' argument")};

    cJSON *old_text_json = cJSON_GetObjectItem(args, "old_text");
    if (!cJSON_IsString(old_text_json) || !old_text_json->valuestring[0])
        return (ToolResult){.ok = false, .output = xstrdup("missing 'old_text' argument")};

    cJSON *new_text_json = cJSON_GetObjectItem(args, "new_text");
    if (!cJSON_IsString(new_text_json))
        return (ToolResult){.ok = false, .output = xstrdup("missing 'new_text' argument")};

    char *resolved = resolve_workspace_path(path_json->valuestring);
    if (!resolved)
        return (ToolResult){.ok = false, .output = xstrdup("sandbox: path outside workspace")};

    FILE *fp = fopen(resolved, "r");
    if (!fp) {
        free(resolved);
        return (ToolResult){.ok = false, .output = xasprintf("edit_file: %s", strerror(errno))};
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 0) {
        fclose(fp);
        free(resolved);
        return (ToolResult){.ok = false, .output = xstrdup("edit_file: ftell failed")};
    }

    char *content = xmalloc((size_t)file_size + 1);
    size_t read_size = fread(content, 1, (size_t)file_size, fp);
    content[read_size] = '\0';
    fclose(fp);

    const char *old_text = old_text_json->valuestring;
    const char *new_text = new_text_json->valuestring;
    char *match = strstr(content, old_text);

    if (!match) {
        free(content);
        free(resolved);
        return (ToolResult){.ok = false, .output = xstrdup("edit_file: old_text not found")};
    }

    size_t old_text_len = strlen(old_text);
    size_t new_text_len = strlen(new_text);
    size_t prefix_len = match - content;
    size_t suffix_len = read_size - prefix_len - old_text_len;

    char *new_content = xmalloc(prefix_len + new_text_len + suffix_len + 1);
    memcpy(new_content, content, prefix_len);
    memcpy(new_content + prefix_len, new_text, new_text_len);
    memcpy(new_content + prefix_len + new_text_len, content + prefix_len + old_text_len, suffix_len);
    new_content[prefix_len + new_text_len + suffix_len] = '\0';

    fp = fopen(resolved, "w");
    if (!fp) {
        free(new_content);
        free(content);
        free(resolved);
        return (ToolResult){.ok = false, .output = xasprintf("edit_file: %s", strerror(errno))};
    }

    //size_t wrote = 
    fwrite(new_content, 1, strlen(new_content), fp);
    fclose(fp);
    free(new_content);
    free(content);
    free(resolved);

    return (ToolResult){.ok = true, .output = xasprintf("edited %s", path_json->valuestring)};
}