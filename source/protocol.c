#include "protocol.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char arguments[32769], call_id[128], name[64];
} ProtocolCall;

typedef struct {
    ApiProtocol api;
    NetworkEvent callback;
    bool failed, finished;
    bool limited;
    char text[32769], reasoning[32769];
    ProtocolCall calls[4];
} ProtocolStream;
static ProtocolStream stream;

static const char *text(cJSON *object, const char *key) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : "";
}

static cJSON *message(cJSON *messages, const char *role, const char *content) {
    cJSON *value = cJSON_CreateObject();
    cJSON_AddStringToObject(value, "role", role);
    cJSON_AddStringToObject(value, "content", content);
    cJSON_AddItemToArray(messages, value);
    return value;
}

char *protocol_request(const char *body, ApiProtocol api) {
    if (api == API_RESPONSES) {
        size_t length = strlen(body) + 1;
        char *copy = malloc(length);
        if (copy)
            memcpy(copy, body, length);
        return copy;
    }
    cJSON *source = cJSON_Parse(body);
    if (!source)
        return NULL;
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "model", text(source, "model"));
    cJSON_AddBoolToObject(request, "stream", true);
    cJSON_AddNumberToObject(request, "max_tokens", 4096);
    const char *effort =
        text(cJSON_GetObjectItemCaseSensitive(source, "reasoning"), "effort");
    if (*effort)
        cJSON_AddStringToObject(request, "reasoning_effort", effort);
    cJSON *messages = cJSON_AddArrayToObject(request, "messages");
    message(messages, "system", text(source, "instructions"));
    cJSON *input = cJSON_GetObjectItemCaseSensitive(source, "input");
    cJSON *item, *assistant = NULL;
    const char *reasoning = "";
    cJSON_ArrayForEach(item, input) {
        const char *type = text(item, "type");
        if (!strcmp(type, "reasoning")) {
            cJSON *summary = cJSON_GetObjectItemCaseSensitive(item, "summary");
            reasoning = text(cJSON_GetArrayItem(summary, 0), "text");
        } else if (!strcmp(type, "function_call")) {
            if (!assistant) {
                assistant = message(messages, "assistant", "");
                cJSON_AddStringToObject(assistant, "reasoning_content",
                                        reasoning);
            }
            cJSON *calls =
                cJSON_GetObjectItemCaseSensitive(assistant, "tool_calls");
            if (!calls)
                calls = cJSON_AddArrayToObject(assistant, "tool_calls");
            cJSON *call = cJSON_CreateObject();
            cJSON_AddStringToObject(call, "id", text(item, "call_id"));
            cJSON_AddStringToObject(call, "type", "function");
            cJSON *function = cJSON_AddObjectToObject(call, "function");
            cJSON_AddStringToObject(function, "name", text(item, "name"));
            cJSON_AddStringToObject(function, "arguments",
                                    text(item, "arguments"));
            cJSON_AddItemToArray(calls, call);
        } else if (!strcmp(type, "function_call_output")) {
            cJSON *output = message(messages, "tool", text(item, "output"));
            cJSON_AddStringToObject(output, "tool_call_id",
                                    text(item, "call_id"));
            assistant = NULL;
            reasoning = "";
        } else if (*text(item, "role")) {
            const char *role = text(item, "role");
            cJSON *content = cJSON_GetObjectItemCaseSensitive(item, "content");
            cJSON *output = NULL;
            if (cJSON_IsString(content))
                output = message(messages, role, content->valuestring);
            else {
                output = cJSON_CreateObject();
                cJSON_AddStringToObject(output, "role", role);
                cJSON *parts = cJSON_AddArrayToObject(output, "content");
                cJSON *part;
                cJSON_ArrayForEach(part, content) {
                    cJSON *converted = cJSON_CreateObject();
                    if (!strcmp(text(part, "type"), "input_image")) {
                        cJSON_AddStringToObject(converted, "type", "image_url");
                        cJSON *image =
                            cJSON_AddObjectToObject(converted, "image_url");
                        cJSON_AddStringToObject(image, "url",
                                                text(part, "image_url"));
                    } else {
                        cJSON_AddStringToObject(converted, "type", "text");
                        cJSON_AddStringToObject(converted, "text",
                                                text(part, "text"));
                    }
                    cJSON_AddItemToArray(parts, converted);
                }
                cJSON_AddItemToArray(messages, output);
            }
            assistant = !strcmp(role, "assistant") ? output : NULL;
            if (assistant)
                cJSON_AddStringToObject(assistant, "reasoning_content",
                                        reasoning);
            else
                reasoning = "";
        }
    }
    cJSON *tools = cJSON_AddArrayToObject(request, "tools");
    cJSON *definition;
    cJSON_ArrayForEach(definition,
                       cJSON_GetObjectItemCaseSensitive(source, "tools")) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "type", "function");
        cJSON *function = cJSON_Duplicate(definition, 1);
        cJSON_DeleteItemFromObjectCaseSensitive(function, "type");
        cJSON_DeleteItemFromObjectCaseSensitive(function, "strict");
        cJSON_AddItemToObject(tool, "function", function);
        cJSON_AddItemToArray(tools, tool);
    }
    cJSON_AddBoolToObject(request, "parallel_tool_calls", false);
    char *encoded = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    cJSON_Delete(source);
    return encoded;
}

void protocol_start(ApiProtocol api, NetworkEvent callback) {
    memset(&stream, 0, sizeof(stream));
    stream.api = api;
    stream.callback = callback;
}

static void emit(cJSON *event) {
    char *encoded = cJSON_PrintUnformatted(event);
    if (encoded && stream.callback)
        stream.callback(encoded);
    if (!encoded)
        stream.failed = true;
    free(encoded);
    cJSON_Delete(event);
}

static void append(char *target, size_t capacity, const char *value) {
    size_t used = strlen(target), length = strlen(value);
    if (length >= capacity - used) {
        stream.failed = true;
        stream.limited = true;
        return;
    }
    memcpy(target + used, value, length + 1);
}

static void output(cJSON *item) {
    cJSON *event = cJSON_CreateObject();
    cJSON_AddStringToObject(event, "type", "response.output_item.done");
    cJSON_AddItemToObject(event, "item", item);
    emit(event);
}

static void finish(void) {
    if (stream.reasoning[0]) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "reasoning");
        cJSON *summary = cJSON_AddArrayToObject(item, "summary");
        cJSON *part = cJSON_CreateObject();
        cJSON_AddStringToObject(part, "type", "summary_text");
        cJSON_AddStringToObject(part, "text", stream.reasoning);
        cJSON_AddItemToArray(summary, part);
        output(item);
    }
    if (stream.text[0]) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "message");
        cJSON_AddStringToObject(item, "role", "assistant");
        cJSON *content = cJSON_AddArrayToObject(item, "content");
        cJSON *part = cJSON_CreateObject();
        cJSON_AddStringToObject(part, "type", "output_text");
        cJSON_AddStringToObject(part, "text", stream.text);
        cJSON_AddItemToArray(content, part);
        output(item);
    }
    for (unsigned i = 0; i < 4; i++) {
        ProtocolCall *call = &stream.calls[i];
        if (!call->call_id[0])
            continue;
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "function_call");
        cJSON_AddStringToObject(item, "call_id", call->call_id);
        cJSON_AddStringToObject(item, "name", call->name);
        cJSON_AddStringToObject(item, "arguments", call->arguments);
        output(item);
    }
    cJSON *event = cJSON_CreateObject();
    cJSON_AddStringToObject(event, "type", "response.completed");
    emit(event);
    stream.finished = true;
}

void protocol_event(const char *data) {
    if (stream.failed || stream.finished)
        return;
    if (stream.api == API_RESPONSES) {
        if (strstr(data, "response.incomplete")) {
            cJSON *object = cJSON_Parse(data);
            if (!strcmp(text(object, "type"), "response.incomplete")) {
                cJSON *response =
                    cJSON_GetObjectItemCaseSensitive(object, "response");
                cJSON *details = cJSON_GetObjectItemCaseSensitive(
                    response, "incomplete_details");
                stream.limited =
                    !strcmp(text(details, "reason"), "max_output_tokens");
                stream.failed = true;
            }
            cJSON_Delete(object);
        }
        if (stream.callback)
            stream.callback(data);
        return;
    }
    if (!strcmp(data, "[DONE]")) {
        stream.failed = true;
        return;
    }
    cJSON *object = cJSON_Parse(data);
    if (!object) {
        stream.failed = true;
        return;
    }
    if (cJSON_GetObjectItemCaseSensitive(object, "error"))
        stream.failed = true;
    cJSON *choices = cJSON_GetObjectItemCaseSensitive(object, "choices");
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *delta = cJSON_GetObjectItemCaseSensitive(choice, "delta");
    const char *content = text(delta, "content");
    append(stream.text, sizeof(stream.text), content);
    append(stream.reasoning, sizeof(stream.reasoning),
           text(delta, "reasoning_content"));
    if (*content && !stream.failed) {
        cJSON *event = cJSON_CreateObject();
        cJSON_AddStringToObject(event, "type", "response.output_text.delta");
        cJSON_AddStringToObject(event, "delta", content);
        emit(event);
    }
    cJSON *calls = cJSON_GetObjectItemCaseSensitive(delta, "tool_calls"), *call;
    cJSON_ArrayForEach(call, calls) {
        cJSON *index = cJSON_GetObjectItemCaseSensitive(call, "index");
        if (!cJSON_IsNumber(index) || index->valuedouble < 0 ||
            index->valuedouble != index->valueint) {
            stream.failed = true;
            break;
        }
        if (index->valueint >= 4) {
            stream.failed = stream.limited = true;
            break;
        }
        ProtocolCall *pending = &stream.calls[index->valueint];
        cJSON *function = cJSON_GetObjectItemCaseSensitive(call, "function");
        append(pending->call_id, sizeof(pending->call_id), text(call, "id"));
        append(pending->name, sizeof(pending->name), text(function, "name"));
        append(pending->arguments, sizeof(pending->arguments),
               text(function, "arguments"));
    }
    const char *reason = text(choice, "finish_reason");
    if (!strcmp(reason, "length"))
        stream.limited = true;
    if (*reason && strcmp(reason, "stop") && strcmp(reason, "tool_calls"))
        stream.failed = true;
    if (*reason) {
        unsigned calls_count = 0;
        for (unsigned i = 0; i < 4; i++) {
            ProtocolCall *pending = &stream.calls[i];
            if (!pending->call_id[0] && !pending->name[0] &&
                !pending->arguments[0])
                continue;
            calls_count++;
            if (!pending->call_id[0] || !pending->name[0])
                stream.failed = true;
        }
        if (!strcmp(reason, "tool_calls") && !calls_count)
            stream.failed = true;
    }
    if (*reason && !stream.failed)
        finish();
    cJSON_Delete(object);
}

bool protocol_failed(void) { return stream.failed; }
bool protocol_output_limited(void) { return stream.limited; }
