#include "protocol.h"
#include <assert.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned completed, calls, deltas;
static char arguments[128];
static void event(const char *data) {
    cJSON *object = cJSON_Parse(data);
    assert(object);
    const char *type =
        cJSON_GetObjectItemCaseSensitive(object, "type")->valuestring;
    if (!strcmp(type, "response.completed"))
        completed++;
    if (!strcmp(type, "response.output_text.delta"))
        deltas++;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, "item");
    cJSON *value = cJSON_GetObjectItemCaseSensitive(item, "arguments");
    if (cJSON_IsString(value)) {
        calls++;
        snprintf(arguments, sizeof(arguments), "%s", value->valuestring);
    }
    cJSON_Delete(object);
}

int main(void) {
    const char *request =
        "{\"model\":\"deepseek-v4-flash\",\"reasoning\":{\"effort\":\"low\"},"
        "\"instructions\":\"Code\",\"tools\":["
        "{\"type\":\"function\",\"name\":\"execute\",\"parameters\":{}}],"
        "\"input\":[{\"role\":\"user\",\"content\":\"build\"},{\"type\":"
        "\"reasoning\",\"summary\":[{\"text\":\"plan\"}]},{\"type\":\"function_"
        "call\",\"call_id\":\"one\",\"name\":\"execute\",\"arguments\":\"{}\"},"
        "{\"type\":\"function_call_output\",\"call_id\":\"one\",\"output\":"
        "\"done\"}]}";
    char *encoded = protocol_request(request, API_CHAT_COMPLETIONS);
    assert(encoded && strstr(encoded, "tool_call_id") &&
           strstr(encoded, "reasoning_content"));
    cJSON *body = cJSON_Parse(encoded);
    cJSON *messages = cJSON_GetObjectItemCaseSensitive(body, "messages");
    assert(cJSON_GetArraySize(messages) == 4);
    assert(cJSON_GetObjectItemCaseSensitive(body, "max_tokens")->valueint ==
           4096);
    assert(!strcmp(
        cJSON_GetObjectItemCaseSensitive(body, "reasoning_effort")->valuestring,
        "low"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(
                       cJSON_GetArrayItem(messages, 2), "reasoning_content")
                       ->valuestring,
                   "plan"));
    cJSON_Delete(body);
    free(encoded);
    encoded = protocol_request("{}", API_CHAT_COMPLETIONS);
    body = cJSON_Parse(encoded);
    assert(body && !cJSON_HasObjectItem(body, "reasoning_effort"));
    cJSON_Delete(body);
    free(encoded);
    cJSON *multiple = cJSON_Parse(request);
    cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(multiple, "tools"),
                         cJSON_Parse("{\"type\":\"function\",\"name\":\"read_"
                                     "file\",\"parameters\":{}}"));
    char *multiple_request = cJSON_PrintUnformatted(multiple);
    encoded = protocol_request(multiple_request, API_CHAT_COMPLETIONS);
    body = cJSON_Parse(encoded);
    assert(cJSON_GetArraySize(
               cJSON_GetObjectItemCaseSensitive(body, "tools")) == 2);
    cJSON_Delete(body);
    cJSON_Delete(multiple);
    free(multiple_request);
    free(encoded);
    protocol_start(API_CHAT_COMPLETIONS, event);
    protocol_event("{\"choices\":[{\"delta\":{\"content\":\"hello\","
                   "\"reasoning_content\":\"plan\"}}]}");
    protocol_event("{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
                   "\"id\":\"call-1\",\"function\":{\"name\":\"execute\","
                   "\"arguments\":\"{\\\"code\\\":\"}}]}}]}");
    protocol_event("{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
                   "\"function\":{\"arguments\":\"\\\"return "
                   "1\\\"}\"}}]},\"finish_reason\":\"tool_calls\"}]}");
    protocol_event("[DONE]");
    assert(!protocol_failed() && completed == 1 && calls == 1 && deltas == 1);
    assert(!strcmp(arguments, "{\"code\":\"return 1\"}"));
    const char *failures[] = {
        "broken",
        "[DONE]",
        "{\"error\":{}}",
        "{\"choices\":[{\"finish_reason\":\"length\"}]}",
        "{\"choices\":[{\"finish_reason\":\"tool_calls\"}]}",
        "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":4}]}}]}"};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(*failures); i++) {
        protocol_start(API_CHAT_COMPLETIONS, event);
        protocol_event(failures[i]);
        assert(protocol_failed() && completed == 1);
    }
    protocol_start(API_CHAT_COMPLETIONS, event);
    for (unsigned i = 0; i < 40000; i++)
        protocol_event("{\"choices\":[{\"delta\":{\"content\":\"x\"}}]}");
    assert(protocol_failed() && completed == 1);
    assert(protocol_output_limited());
    protocol_start(API_RESPONSES, event);
    protocol_event(
        "{\"type\":\"response.incomplete\",\"response\":{\"incomplete_"
        "details\":{\"reason\":\"max_output_tokens\"}}}");
    assert(protocol_failed() && protocol_output_limited() && completed == 1);
    protocol_start(API_CHAT_COMPLETIONS, event);
    protocol_event(
        "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":"
        "\"first\",\"function\":{\"name\":\"read_file\",\"arguments\":\"{}\"}},"
        "{\"index\":1,\"id\":\"second\",\"function\":{\"name\":\"list_files\","
        "\"arguments\":\"{}\"}}]},\"finish_reason\":\"tool_calls\"}]}");
    assert(!protocol_failed() && completed == 2 && calls == 3);
    puts("Protocol: request translation, fragmented tools, reasoning, limits "
         "and invalid streams passed");
    return 0;
}
