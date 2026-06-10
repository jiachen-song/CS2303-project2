/*
 * llm_client.c — HTTP+JSON glue between the Agent and the LLM service.
 *
 * Your job: implement llm_chat. Everything else in this file is yours to
 * design. You will certainly want helpers (request construction, response
 * parsing, …); whether and how you decompose them is a decision for you.
 */
#include "llm_client.h"

#include "config.h"
#include "http.h"
#include "tools/tools.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LLM_TIMEOUT_SEC 120

void llm_response_init(LLMResponse *r) { memset(r, 0, sizeof(*r)); }

void llm_response_free(LLMResponse *r) {
  if (!r)
    return;
  free(r->content);
  free(r->raw_message);
  for (int i = 0; i < r->n_tool_calls; i++) {
    free(r->tool_calls[i].id);
    free(r->tool_calls[i].name);
    cJSON_Delete(r->tool_calls[i].args);
  }
  /* calloc(0, ...) returns a non-NULL unique pointer when the LLM response
   * has no tool_calls, so we always free it here rather than gating on
   * n_tool_calls. */
  free(r->tool_calls);
  memset(r, 0, sizeof(*r));
}

int llm_chat(const MessageList *messages, const char *system_prompt,
             const char *model, LLMResponse *out, char *err, size_t err_cap) {
  cJSON *body = NULL;
  cJSON *messages_array = NULL;
  cJSON *tools_array = NULL;
  //cJSON *bash_tool = NULL;
  cJSON *tool_obj =NULL;
  cJSON *func = NULL;
  cJSON *root = NULL;
  cJSON *raw_message = NULL;
  char *body_str = NULL;
  char *response = NULL;
  int ret = -1;

  body = cJSON_CreateObject();
  if (!body) {
    snprintf(err, err_cap, "cJSON_CreateObject failed");
    goto done;
  }

  cJSON_AddStringToObject(body, "model", model);

  messages_array = cJSON_CreateArray();
  if (!messages_array) {
    snprintf(err, err_cap, "cJSON_CreateArray failed");
    goto done;
  }

  if (system_prompt) {
    cJSON *system_message = cJSON_CreateObject();
    if (!system_message) {
      snprintf(err, err_cap, "cJSON_CreateObject failed");
      goto done;
    }
    //add system prompt to messages array
    cJSON_AddStringToObject(system_message, "role", "system");
    cJSON_AddStringToObject(system_message, "content", system_prompt);
    cJSON_AddItemToArray(messages_array, system_message);
  }
  //add user messages to messages array

  for (int i = 0; i < messages->len; i++) {
    cJSON *one_message = cJSON_Parse(messages->items[i]);
    if (!one_message) {
      snprintf(err, err_cap, "cJSON_Parse failed");
      goto done;
    }
    cJSON_AddItemToArray(messages_array, one_message);
  }

  //merge messages array into body
  cJSON_AddItemToObject(body, "messages", messages_array);
  messages_array = NULL;
  //add tools
  
  tools_array = cJSON_CreateArray();
  if (!tools_array) {
    snprintf(err, err_cap, "cJSON_CreateArray failed");
    goto done;
  }
  cJSON_AddItemToObject(body, "tools", tools_array);

  //change
  int tool_count =0;
  ToolDef *const *tools =tool_list(&tool_count);
  for(int i=0;i<tool_count;i++){
    ToolDef *def =tools[i];

    tool_obj =cJSON_CreateObject();
    if (!tool_obj) {
      snprintf(err, err_cap, "cJSON_CreateObject failed");
      goto done;
    }
    cJSON_AddStringToObject(tool_obj, "name", def->name);
    cJSON_AddStringToObject(tool_obj, "type", "function");

    func = cJSON_CreateObject();
    if (!func) {
      //cJSON_Delete(tool_obj);
      snprintf(err, err_cap, "cJSON_CreateObject failed");
      goto done;
    }
    cJSON_AddStringToObject(func, "name", def->name);
    cJSON_AddStringToObject(func, "description", def->desc);
    //cJSON_AddStringToObject(func, "parameters", BASH_TOOL_SCHEMA);
    cJSON *params= cJSON_Parse(def->param_schema);
    if(!params){
      //cJSON_Delete(func);
      snprintf(err, err_cap, "cJSON_Parse failed");
      goto done;
    }
    cJSON_AddItemToObject(func, "parameters", params);
    
    cJSON_AddItemToObject(tool_obj,"function",func);
    func = NULL;

    cJSON_AddItemToArray(tools_array,tool_obj);
    tool_obj = NULL;
    params = NULL;

  }
  tools_array = NULL;

  // bash_tool = cJSON_CreateObject();
  // if (!bash_tool) {
  //   snprintf(err, err_cap, "cJSON_CreateObject failed");
  //   goto done;
  // }
  // cJSON_AddStringToObject(bash_tool, "name", BASH_TOOL_NAME);
  // cJSON_AddStringToObject(bash_tool, "type", "function");

  // func = cJSON_CreateObject();
  // if (!func) {
  //   snprintf(err, err_cap, "cJSON_CreateObject failed");
  //   goto done;
  // }
  // cJSON_AddStringToObject(func, "name", BASH_TOOL_NAME);
  // cJSON_AddStringToObject(func, "description", BASH_TOOL_DESC);
  // //cJSON_AddStringToObject(func, "parameters", BASH_TOOL_SCHEMA);
  // cJSON *params= cJSON_Parse(BASH_TOOL_SCHEMA);
  // if(!params){
  //   snprintf(err, err_cap, "cJSON_Parse failed");
  //   goto done;
  // }
  // cJSON_AddItemToObject(func, "parameters", params);
  // params = NULL;

  // cJSON_AddItemToObject(bash_tool, "function", func);
  // func = NULL;

  // cJSON_AddItemToArray(tools_array, bash_tool);
  // bash_tool = NULL;
  // tools_array = NULL;

  cJSON_AddNumberToObject(body, "max_tokens", g_config.max_tokens);

  body_str = cJSON_PrintUnformatted(body);
  if (!body_str) {
    snprintf(err, err_cap, "cJSON_PrintUnformatted failed");
    goto done;
  }

  //build HTTP request
  char header[1024];
  snprintf(header, sizeof(header),
           "POST /api/v1/chat/completions HTTP/1.1\r\n"
           "Host: %s:%d\r\n"
           "Authorization: Bearer %s\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: %zu\r\n"
           "Connection: close\r\n"
           "\r\n",
           g_config.llm_host, g_config.llm_port,
           g_config.api_key, strlen(body_str));

  //tcp connect
  int sockfd = tcp_connect(g_config.llm_host, g_config.llm_port, err, err_cap);
  if (sockfd < 0) {
    goto done;
  }
  //fprintf(stderr, "llm_chat: connected to LLM service at %s:%d\n", g_config.llm_host, g_config.llm_port);
  if (send_all(sockfd, header, strlen(header)) < 0 ||
      send_all(sockfd, body_str, strlen(body_str)) < 0) {
    close(sockfd);
    snprintf(err, err_cap, "send_all failed");
    goto done;
  }
  //fprintf(stderr, "llm_chat: sent request:\n%s%s\n", header, body_str);
  
  //read response
  size_t response_len = 0;
  if (recv_all(sockfd, LLM_TIMEOUT_SEC, &response, &response_len, err, err_cap) < 0) {
    close(sockfd);
    goto done;
  }
  //fprintf(stderr, "llm_chat: received response:\n%.*s\n", (int)response_len, response);
  close(sockfd);

  int status = 0;
  const char *body_start = NULL;
  if (http_parse_response(response, &status, &body_start) < 0) {
    snprintf(err, err_cap, "http_parse_response failed");
    goto done;
  }
  if (status != 200) {
    snprintf(err, err_cap, "LLM service returned status %d", status);
    goto done;
  }

  root = cJSON_Parse(body_start);
  if (!root) {
    snprintf(err, err_cap, "cJSON_Parse failed");
    goto done;
  }

  cJSON *choices = cJSON_GetObjectItem(root, "choices");
  if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
    snprintf(err, err_cap, "invalid response: missing choices");
    goto done;
  }

  cJSON *choice0 = cJSON_GetArrayItem(choices, 0);
  cJSON *message = cJSON_GetObjectItem(choice0, "message");
  if (!message) {
    snprintf(err, err_cap, "invalid response: missing message");
    goto done;
  }

  cJSON *content = cJSON_GetObjectItem(message, "content");
  if (content && cJSON_IsString(content)) {
    out->content = xstrdup(content->valuestring);//strdup->xstrdup
  } else {
    out->content = xstrdup("");//strdup->xstrdup
  }
  if (!out->content) {
    snprintf(err, err_cap, "strdup failed");
    goto done;
  }

  //extract tool_calls
  cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
  out->n_tool_calls = 0;
  out->tool_calls = NULL;

  if (tool_calls && !cJSON_IsNull(tool_calls) && !cJSON_IsArray(tool_calls)) {
    snprintf(err, err_cap, "invalid response: tool_calls is not an array");
    goto done;
  }

  if (tool_calls && !cJSON_IsNull(tool_calls) && cJSON_IsArray(tool_calls)) {
    int n = cJSON_GetArraySize(tool_calls);
    out->tool_calls = calloc(n, sizeof(LLMToolCall));
    if (!out->tool_calls) {
      snprintf(err, err_cap, "calloc failed");
      goto done;
    }
    out->n_tool_calls = n;

    for (int i = 0; i < n; i++) {
      cJSON *tool_call = cJSON_GetArrayItem(tool_calls, i);
      cJSON *id_item = cJSON_GetObjectItem(tool_call, "id");
      cJSON *function = cJSON_GetObjectItem(tool_call, "function");
      if (!id_item || !cJSON_IsString(id_item) || !function) {
        snprintf(err, err_cap, "invalid response: tool_call missing id or function");
        goto done;
      }
      out->tool_calls[i].id = strdup(id_item->valuestring);

      cJSON *name_item = cJSON_GetObjectItem(function, "name");
      if (!name_item || !cJSON_IsString(name_item)) {
        snprintf(err, err_cap, "invalid response: function missing name");
        goto done;
      }
      out->tool_calls[i].name = strdup(name_item->valuestring);

      cJSON *args_item = cJSON_GetObjectItem(function, "arguments");
      const char *args_str = cJSON_IsString(args_item) ? args_item->valuestring : "";
      if (args_str[0] == '\0') {
        out->tool_calls[i].args = cJSON_CreateObject();
      } else {
        out->tool_calls[i].args = cJSON_Parse(args_str);
        if (!out->tool_calls[i].args) {
          out->tool_calls[i].args = cJSON_CreateObject();
        }
      }
    }
  }

  //save raw_message
  raw_message = cJSON_Duplicate(message, 1);
  out->raw_message = cJSON_PrintUnformatted(raw_message);

  ret = 0;
  goto done;

done:
  cJSON_Delete(root);
  free(body_str);
  cJSON_Delete(body);
  cJSON_Delete(messages_array);
  cJSON_Delete(tools_array);
  //cJSON_Delete(bash_tool);
  cJSON_Delete(tool_obj);
  cJSON_Delete(func);
  cJSON_Delete(raw_message);
  free(response);
  return ret;
}
