#include "tools.h"
#include "sandbox.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static ToolResult tool_read_file_exec(cJSON *args);
ToolDef read_file_def = {
    .name = "read_file",
    .desc = "读取工作区内的文件内容（UTF-8）。",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"path\":{\"type\":\"string\",\"description\":\"工作区内的相对路径\"},"
                    "\"limit\":{\"type\":\"integer\",\"description\":\"可选的最大行数\"}},"
                    "\"required\":[\"path\"]}",
    .exec = tool_read_file_exec,
    .read_only = true,
};
static ToolResult tool_read_file_exec(cJSON *args) {
    cJSON *path_json = cJSON_GetObjectItem(args, "path");
    if (!cJSON_IsString(path_json) || !path_json->valuestring[0])
        return (ToolResult){.ok = false, .output = xstrdup("missing 'path' argument")};
    cJSON *limit_json = cJSON_GetObjectItem(args, "limit");
    int limit = -1;
    if (cJSON_IsNumber(limit_json))
        limit = limit_json->valueint;
    char *resolved = resolve_workspace_path(path_json->valuestring);
    if (!resolved)
        return (ToolResult){.ok = false, .output = xstrdup("sandbox: path outside workspace")};
    FILE *fp = fopen(resolved, "r");
    free(resolved);
    if (!fp)
        return (ToolResult){.ok = false, .output = xasprintf("read_file: %s", strerror(errno))};
    char *output = NULL;
    size_t output_size = 0;
    char line[4096];
    int line_count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (limit > 0 && line_count >= limit)
            break;
        size_t len = strlen(line);
        char *new_output = xrealloc(output, output_size + len + 1);
        memcpy(new_output + output_size, line, len);
        output_size += len;
        new_output[output_size] = '\0';
        output = new_output;
        line_count++;
        if (output_size >= MAX_TOOL_OUTPUT)
            break;
    }
    fclose(fp);
    if (!output)
        output = xstrdup("");
    return (ToolResult){.ok = true, .output = output};
}