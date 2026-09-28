#include "config.h"
#include "backend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void write_config(const char *text) {
    FILE *file = fopen("config.json", "wb");
    assert(file);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}

int main(void) {
    assert(catalog_load("."));
    assert(config_load("absent.json"));
    assert(config_get()->default_model == 0);
    write_config(
        "{\"default_model\":\"opencode-go/deepseek-v4-flash\",\"tool_details\":"
        "true,\"startup_view\":\"sessions\"}");
    assert(config_load("config.json"));
    assert(config_get()->default_model == 3 && config_get()->tool_details &&
           config_get()->start_in_sessions);
    const char *invalid[] = {
        "[]",
        "null",
        "{",
        "{} trailing",
        "{\"default_model\":\"unknown\"}",
        "{\"default_model\":\"deepseek-v4-flash\"}",
        "{\"default_model\":\"other-provider/deepseek-v4-flash\"}",
        "{\"tool_details\":1}",
        "{\"startup_view\":\"play\"}",
        "{\"token\":\"secret\"}",
        "{\"tool_details\":true,\"tool_details\":false}",
        "{\"default_model\":\"opencode-go/"
        "deepseek-v4-flash\",\"startup_view\":false}"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        write_config(invalid[i]);
        assert(!config_load("config.json"));
        assert(config_error()[0]);
        assert(config_get()->default_model == 0 && !config_get()->tool_details);
    }
    char large[5000];
    memset(large, ' ', sizeof(large) - 1);
    large[sizeof(large) - 1] = 0;
    write_config(large);
    assert(!config_load("config.json"));
    write_config("{}");
    assert(config_load("config.json") && !config_error()[0]);
    puts("Config: defaults, preferences, invalid inputs and atomic rejection "
         "passed");
    return 0;
}
