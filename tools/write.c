#include "tools.h"
#include "sandbox.h"
#include "util.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ToolResult tool_write_file_exec(cJSON *args);

ToolDef write_file_def = {
    .name = "write_file",
    .desc = "写入文件内容到工作区内的相对路径",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"path\":{\"type\":\"string\",\"description\":\"工作区内的相对路径\"},"
                    "\"content\":{\"type\":\"string\",\"description\":\"要写入的完整文件内容\"}},"
                    "\"required\":[\"path\",\"content\"]}",
    .exec = tool_write_file_exec,
    .read_only = false,
};

static ToolResult tool_write_file_exec(cJSON *args) {
    cJSON *path_json = cJSON_GetObjectItem(args, "path");
    if (!cJSON_IsString(path_json) || !path_json->valuestring[0])
        return (ToolResult){.ok = false, .output = xstrdup("missing 'path' argument")};

    cJSON *content_json = cJSON_GetObjectItem(args, "content");
    if (!cJSON_IsString(content_json))
        return (ToolResult){.ok = false, .output = xstrdup("missing 'content' argument")};

    char *resolved = resolve_workspace_path(path_json->valuestring);
    if (!resolved)
        return (ToolResult){.ok = false, .output = xstrdup("sandbox: path outside workspace")};

    FILE *fp = fopen(resolved, "w");
    if (!fp) {
        free(resolved);
        return (ToolResult){.ok = false, .output = xasprintf("write_file: %s", strerror(errno))};
    }

    size_t content_len = strlen(content_json->valuestring);
    size_t written = fwrite(content_json->valuestring, 1, content_len, fp);
    fclose(fp);
    free(resolved);

    if (written != content_len)
        return (ToolResult){.ok = false, .output = xasprintf("write_file: partial write (%zu of %zu)", written, content_len)};

    return (ToolResult){.ok = true, .output = xasprintf("wrote %zu bytes to %s", written, path_json->valuestring)};
}