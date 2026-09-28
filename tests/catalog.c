#include "backend.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}

int main(int argc, char **argv) {
    if (argc == 2) {
        if (!catalog_load(argv[1])) {
            fprintf(stderr, "%s\n", catalog_error());
            return 1;
        }
        printf("Published catalog: %u models validated\n", BACKEND_COUNT);
        return 0;
    }
    assert(catalog_load("."));
    assert(BACKEND_COUNT == 4);
    FILE *file = fopen("models.json", "rb");
    assert(file);
    char original[CATALOG_MAX_BYTES];
    size_t length = fread(original, 1, sizeof(original) - 1, file);
    original[length] = 0;
    assert(fclose(file) == 0);
    write_text(
        "models.local.json",
        "{\"providers\":[{\"id\":\"custom\",\"base_url\":\"https://example.com/"
        "v1\",\"auth_header\":\"Authorization: "
        "Bearer\"}],\"models\":[{\"id\":\"custom/test\",\"name\":\"Custom "
        "test\",\"provider\":\"custom\",\"model\":\"test\",\"protocol\":"
        "\"responses\"}],\"default_model\":\"custom/test\"}");
    assert(catalog_load(".") && BACKEND_COUNT == 5 && catalog_default() == 4);
    assert(!strcmp(backends[4].provider->id, "custom"));
    assert(backends[4].provider->session_header == NULL);
    assert(catalog_install(".", original, length));
    assert(BACKEND_COUNT == 5 && catalog_default() == 4);
    const Backend *previous = backends;
    assert(!catalog_install(".", "{}", 2) && backends == previous);
    write_text("models.local.json",
               "{\"models\":[{\"id\":\"custom/test\",\"name\":\"Wrong "
               "provider\",\"provider\":\"missing\",\"model\":\"test\","
               "\"protocol\":\"responses\"}]}");
    assert(!catalog_load(".") && backends == previous);
    write_text("models.local.json", "{\"models\":[],\"models\":[]}");
    assert(!catalog_load(".") && backends == previous);
    write_text("models.local.json",
               "{\"providers\":[{\"id\":\"opencode-go\",\"base_url\":\"http://"
               "example.com\",\"auth_header\":\"Authorization: Bearer\"}]}");
    assert(!catalog_load(".") && backends == previous);
    write_text("models.local.json", "{}");
    assert(catalog_load(".") && BACKEND_COUNT == 4);
    previous = backends;
    assert(mkdir("models.json.tmp", 0700) == 0);
    assert(!catalog_install(".", original, length) && backends == previous);
    assert(rmdir("models.json.tmp") == 0);
    assert(catalog_install(".", original, length));
    assert(unlink("models.json") == 0);
    assert(catalog_load("."));
    assert(!strcmp(backends[3].id, "opencode-go/deepseek-v4-flash"));
    puts("Catalog: local providers, defaults, atomic rejection, FAT "
         "replacement and recovery passed");
    return 0;
}
