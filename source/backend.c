#include "backend.h"
#include <cJSON.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    CATALOG_SCHEMA_VERSION = 1,
    PROVIDER_ID_BYTES = 40,
    PROVIDER_URL_BYTES = 180,
    PROVIDER_HEADER_BYTES = 64,
    MODEL_ID_BYTES = 160,
    MODEL_NAME_BYTES = 64,
    API_MODEL_BYTES = 100,
    CATALOG_PATH_BYTES = 256,
    CATALOG_DIRECTORY_BYTES = 200,
    CATALOG_ERROR_BYTES = 160
};

typedef struct {
    cJSON *document;
    Provider providers[CATALOG_MAX_PROVIDERS];
    Backend models[CATALOG_MAX_MODELS];
    unsigned count, default_model;
} Catalog;

static Catalog *active;
const Backend *backends;
unsigned backend_count;
static char error[CATALOG_ERROR_BYTES];

const char *catalog_error(void) { return error; }
unsigned catalog_default(void) { return active ? active->default_model : 0; }

static bool fail(const char *message) {
    snprintf(error, sizeof(error), "Catalog: %s", message);
    return false;
}

static const char *text(cJSON *object, const char *name) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}

static bool printable(const char *value, size_t limit) {
    if (!value || !*value || strlen(value) > limit)
        return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
        if (*p < ' ' || *p > '~')
            return false;
    return true;
}

static bool identifier(const char *value) {
    if (!printable(value, PROVIDER_ID_BYTES))
        return false;
    for (const char *p = value; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_')
            return false;
    return true;
}

static bool fields(cJSON *object, const char *const *names, unsigned count) {
    if (!cJSON_IsObject(object))
        return false;
    cJSON *item;
    cJSON_ArrayForEach(item, object) {
        bool known = false;
        for (unsigned i = 0; i < count; i++)
            if (!strcmp(item->string, names[i]))
                known = true;
        if (!known ||
            cJSON_GetObjectItemCaseSensitive(object, item->string) != item)
            return false;
    }
    return true;
}

static cJSON *read_json(const char *path, bool optional) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (optional && errno == ENOENT)
            return cJSON_CreateObject();
        fail("cannot open models.json or models.local.json");
        return NULL;
    }
    char *data = malloc(CATALOG_MAX_BYTES + 1);
    if (!data) {
        fclose(file);
        fail("not enough memory");
        return NULL;
    }
    size_t length = fread(data, 1, CATALOG_MAX_BYTES, file);
    bool invalid = ferror(file) || length == CATALOG_MAX_BYTES;
    if (fclose(file) != 0)
        invalid = true;
    data[length] = 0;
    cJSON *root = invalid || memchr(data, 0, length)
                      ? NULL
                      : cJSON_ParseWithOpts(data, NULL, true);
    free(data);
    if (!root)
        fail("invalid JSON or file exceeds byte limit");
    return root;
}

static bool merge(cJSON *base, cJSON *local, const char *field,
                  unsigned limit) {
    cJSON *items = cJSON_GetObjectItemCaseSensitive(local, field);
    if (!items)
        return true;
    cJSON *target = cJSON_GetObjectItemCaseSensitive(base, field);
    if (!cJSON_IsArray(items) || !cJSON_IsArray(target) ||
        (unsigned)cJSON_GetArraySize(items) > limit)
        return false;
    cJSON *item;
    cJSON_ArrayForEach(item, items) {
        const char *id = text(item, "id");
        if (!id)
            return false;
        for (cJSON *previous = items->child; previous != item;
             previous = previous->next)
            if (text(previous, "id") && !strcmp(text(previous, "id"), id))
                return false;
        cJSON *copy = cJSON_Duplicate(item, true);
        if (!copy)
            return false;
        int index = 0;
        cJSON *existing;
        cJSON_ArrayForEach(existing, target) {
            const char *old = text(existing, "id");
            if (old && !strcmp(old, id))
                break;
            index++;
        }
        if (existing)
            cJSON_ReplaceItemInArray(target, index, copy);
        else
            cJSON_AddItemToArray(target, copy);
    }
    return true;
}

static Catalog *validate(cJSON *root) {
    static const char *const root_fields[] = {"version", "default_model",
                                              "providers", "models"};
    static const char *const provider_fields[] = {
        "id", "base_url", "auth_header", "session_header"};
    static const char *const model_fields[] = {
        "id", "name", "provider", "model", "protocol", "reasoning_effort"};
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *providers = cJSON_GetObjectItemCaseSensitive(root, "providers");
    cJSON *models = cJSON_GetObjectItemCaseSensitive(root, "models");
    const char *default_id = text(root, "default_model");
    if (!fields(root, root_fields,
                sizeof(root_fields) / sizeof(*root_fields)) ||
        !cJSON_IsNumber(version) ||
        version->valuedouble != CATALOG_SCHEMA_VERSION || !default_id ||
        !cJSON_IsArray(providers) || !cJSON_IsArray(models))
        return NULL;
    int provider_count = cJSON_GetArraySize(providers),
        count = cJSON_GetArraySize(models);
    if (provider_count < 1 || provider_count > CATALOG_MAX_PROVIDERS ||
        count < 1 || count > CATALOG_MAX_MODELS)
        return NULL;
    Catalog *candidate = calloc(1, sizeof(*candidate));
    if (!candidate)
        return NULL;
    for (int i = 0; i < provider_count; i++) {
        cJSON *item = cJSON_GetArrayItem(providers, i);
        Provider *provider = &candidate->providers[i];
        *provider =
            (Provider){text(item, "id"), text(item, "base_url"),
                       text(item, "auth_header"), text(item, "session_header")};
        if (!fields(item, provider_fields,
                    sizeof(provider_fields) / sizeof(*provider_fields)) ||
            !identifier(provider->id) ||
            !printable(provider->base_url, PROVIDER_URL_BYTES) ||
            strncmp(provider->base_url, "https://", 8) ||
            !provider->base_url[8] || strpbrk(provider->base_url, "@?# ") ||
            !printable(provider->auth_header, PROVIDER_HEADER_BYTES) ||
            !strchr(provider->auth_header, ':') ||
            (cJSON_HasObjectItem(item, "session_header") &&
             (!identifier(provider->session_header))))
            goto invalid;
        for (int j = 0; j < i; j++)
            if (!strcmp(provider->id, candidate->providers[j].id))
                goto invalid;
    }
    bool found_default = false;
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(models, i);
        Backend *model = &candidate->models[i];
        const char *provider_id = text(item, "provider"),
                   *protocol = text(item, "protocol");
        model->id = text(item, "id");
        model->label = text(item, "name");
        model->model = text(item, "model");
        model->reasoning_effort = text(item, "reasoning_effort");
        if (cJSON_HasObjectItem(item, "reasoning_effort") &&
            !identifier(model->reasoning_effort))
            goto invalid;
        if (!fields(item, model_fields,
                    sizeof(model_fields) / sizeof(*model_fields)) ||
            !printable(model->id, MODEL_ID_BYTES) ||
            !printable(model->label, MODEL_NAME_BYTES) ||
            !printable(model->model, API_MODEL_BYTES) || !provider_id ||
            !protocol)
            goto invalid;
        for (int j = 0; j < provider_count; j++)
            if (!strcmp(provider_id, candidate->providers[j].id))
                model->provider = &candidate->providers[j];
        char qualified[PROVIDER_ID_BYTES + API_MODEL_BYTES + sizeof("/")];
        snprintf(qualified, sizeof(qualified), "%s/%s", provider_id,
                 model->model);
        if (!model->provider || strcmp(model->id, qualified))
            goto invalid;
        if (!strcmp(protocol, "responses"))
            model->protocol = API_RESPONSES;
        else if (!strcmp(protocol, "chat-completions"))
            model->protocol = API_CHAT_COMPLETIONS;
        else
            goto invalid;
        for (int j = 0; j < i; j++)
            if (!strcmp(model->id, candidate->models[j].id))
                goto invalid;
        if (!strcmp(model->id, default_id)) {
            found_default = true;
            candidate->default_model = (unsigned)i;
        }
    }
    if (!found_default)
        goto invalid;
    candidate->count = (unsigned)count;
    candidate->document = root;
    return candidate;
invalid:
    free(candidate);
    return NULL;
}

static Catalog *prepare(cJSON *root, const char *directory) {
    Catalog *base = validate(root);
    if (!base) {
        cJSON_Delete(root);
        fail("invalid base catalog");
        return NULL;
    }
    free(base);
    char path[CATALOG_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/models.local.json", directory);
    cJSON *local = read_json(path, true);
    static const char *const local_fields[] = {"providers", "models",
                                               "default_model"};
    bool ok = local &&
              fields(local, local_fields,
                     sizeof(local_fields) / sizeof(*local_fields)) &&
              merge(root, local, "providers", CATALOG_MAX_PROVIDERS) &&
              merge(root, local, "models", CATALOG_MAX_MODELS);
    if (ok && cJSON_HasObjectItem(local, "default_model")) {
        cJSON *value = cJSON_Duplicate(
            cJSON_GetObjectItemCaseSensitive(local, "default_model"), true);
        ok = value && cJSON_ReplaceItemInObjectCaseSensitive(
                          root, "default_model", value);
    }
    cJSON_Delete(local);
    Catalog *candidate = ok ? validate(root) : NULL;
    if (!candidate) {
        cJSON_Delete(root);
        fail("invalid local entries or merged catalog");
    }
    return candidate;
}

static void activate(Catalog *candidate) {
    if (active) {
        cJSON_Delete(active->document);
        free(active);
    }
    active = candidate;
    backends = active->models;
    backend_count = active->count;
    error[0] = 0;
}

static bool paths(const char *directory, char *path, char *backup,
                  char *temporary) {
    if (!directory || strlen(directory) > CATALOG_DIRECTORY_BYTES)
        return fail("directory path too long");
    snprintf(path, CATALOG_PATH_BYTES, "%s/models.json", directory);
    snprintf(backup, CATALOG_PATH_BYTES, "%s/models.json.bak", directory);
    snprintf(temporary, CATALOG_PATH_BYTES, "%s/models.json.tmp", directory);
    struct stat information;
    if (stat(path, &information) != 0 && errno == ENOENT &&
        stat(backup, &information) == 0 && rename(backup, path) != 0)
        return fail("cannot restore catalog backup");
    return true;
}

bool catalog_load(const char *directory) {
    char path[CATALOG_PATH_BYTES], backup[CATALOG_PATH_BYTES],
        temporary[CATALOG_PATH_BYTES];
    if (!paths(directory, path, backup, temporary))
        return false;
    cJSON *root = read_json(path, false);
    Catalog *candidate = root ? prepare(root, directory) : NULL;
    if (!candidate)
        return false;
    activate(candidate);
    return true;
}

bool catalog_install(const char *directory, const char *data, size_t length) {
    if (!data || length >= CATALOG_MAX_BYTES || memchr(data, 0, length))
        return fail("download exceeds byte limit or contains NUL");
    char *copy = malloc(length + 1);
    if (!copy)
        return fail("not enough memory");
    memcpy(copy, data, length);
    copy[length] = 0;
    cJSON *root = cJSON_ParseWithOpts(copy, NULL, true);
    free(copy);
    Catalog *candidate = root ? prepare(root, directory) : NULL;
    if (!candidate)
        return fail("download is not a valid catalog");
    char path[CATALOG_PATH_BYTES], backup[CATALOG_PATH_BYTES],
        temporary[CATALOG_PATH_BYTES];
    bool ok = paths(directory, path, backup, temporary);
    FILE *file = ok ? fopen(temporary, "wb") : NULL;
    ok = file && fwrite(data, 1, length, file) == length;
    if (file && fclose(file) != 0)
        ok = false;
    bool moved = false;
    if (ok && unlink(backup) != 0 && errno != ENOENT)
        ok = false;
    if (ok) {
        moved = rename(path, backup) == 0;
        ok = moved || errno == ENOENT;
    }
    if (ok && rename(temporary, path) != 0) {
        if (moved)
            rename(backup, path);
        ok = false;
    }
    if (!ok) {
        cJSON_Delete(candidate->document);
        free(candidate);
        return fail("cannot replace catalog; previous catalog retained");
    }
    activate(candidate);
    return true;
}
