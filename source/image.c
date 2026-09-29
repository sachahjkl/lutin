#include "image.h"
#include "runtime.h"
#include "workspace.h"
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

enum { IMAGE_FILE_BYTES = 200 * 1024, IMAGE_CHANNELS = 3 };

static void big_endian(unsigned char *output, uint32_t value) {
    for (unsigned i = 0; i < 4; i++)
        output[i] = value >> (24 - i * 8);
}

static bool chunk(FILE *file, const char *type, const unsigned char *data,
                  size_t size) {
    unsigned char header[8], tail[4];
    big_endian(header, size);
    memcpy(header + 4, type, 4);
    uLong crc = crc32(0, (const unsigned char *)type, 4);
    if (size)
        crc = crc32(crc, data, size);
    big_endian(tail, crc);
    return fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
           (!size || fwrite(data, 1, size, file) == size) &&
           fwrite(tail, 1, sizeof(tail), file) == sizeof(tail);
}

bool image_capture(char *name, size_t capacity) {
    const uint16_t *pixels = runtime_pixels();
    if (!pixels)
        return false;
    DIR *directory = opendir(workspace_root());
    if (!directory)
        return false;
    unsigned last = 0;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        unsigned number;
        char tail;
        if (sscanf(entry->d_name, "capture-%u.png%c", &number, &tail) == 1 &&
            number > last)
            last = number;
    }
    closedir(directory);
    if (last == UINT_MAX ||
        snprintf(name, capacity, "capture-%u.png", last + 1) >= (int)capacity)
        return false;
    const size_t stride = RUNTIME_SCREEN_WIDTH * IMAGE_CHANNELS + 1;
    const size_t raw_size = stride * RUNTIME_SCREEN_HEIGHT;
    uLongf compressed_size = compressBound(raw_size);
    /* One arena owns raw pixels and compressed output for this capture. */
    unsigned char *arena = malloc(raw_size + compressed_size);
    if (!arena)
        return false;
    for (unsigned y = 0; y < RUNTIME_SCREEN_HEIGHT; y++) {
        unsigned char *row = arena + y * stride;
        row[0] = 0;
        for (unsigned x = 0; x < RUNTIME_SCREEN_WIDTH; x++)
            for (unsigned c = 0; c < IMAGE_CHANNELS; c++)
                row[1 + x * IMAGE_CHANNELS + c] =
                    ((pixels[y * RUNTIME_SCREEN_WIDTH + x] >> (c * 5)) & 31) *
                    255 / 31;
    }
    bool ok = compress2(arena + raw_size, &compressed_size, arena, raw_size,
                        Z_BEST_SPEED) == Z_OK;
    char path[192], temporary[192];
    ok = ok && workspace_path(name, path, sizeof(path)) &&
         snprintf(temporary, sizeof(temporary), "%s/.capture.tmp",
                  workspace_root()) < (int)sizeof(temporary);
    FILE *file = ok ? fopen(temporary, "wb") : NULL;
    if (file) {
        const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        unsigned char header[13] = {0};
        big_endian(header, RUNTIME_SCREEN_WIDTH);
        big_endian(header + 4, RUNTIME_SCREEN_HEIGHT);
        header[8] = 8;
        header[9] = 2;
        ok = fwrite(signature, 1, sizeof(signature), file) ==
                 sizeof(signature) &&
             chunk(file, "IHDR", header, sizeof(header)) &&
             chunk(file, "IDAT", arena + raw_size, compressed_size) &&
             chunk(file, "IEND", NULL, 0);
        if (fclose(file) != 0)
            ok = false;
        if (ok)
            ok = rename(temporary, path) == 0;
        if (!ok)
            remove(temporary);
    } else
        ok = false;
    free(arena);
    return ok;
}

char *image_data_url(const char *name) {
    if (!name || strncmp(name, "capture-", 8) || !strstr(name, ".png"))
        return NULL;
    char path[192];
    if (!workspace_path(name, path, sizeof(path)))
        return NULL;
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    unsigned char *data = malloc(IMAGE_FILE_BYTES);
    if (!data) {
        fclose(file);
        return NULL;
    }
    size_t length = fread(data, 1, IMAGE_FILE_BYTES, file);
    bool ok = !ferror(file) && length && length < IMAGE_FILE_BYTES &&
              fgetc(file) == EOF;
    fclose(file);
    static const char prefix[] = "data:image/png;base64,";
    size_t size = ((length + 2) / 3) * 4 + sizeof(prefix);
    char *output = ok ? malloc(size) : NULL;
    if (output) {
        memcpy(output, prefix, sizeof(prefix) - 1);
        char *out = output + sizeof(prefix) - 1;
        static const char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = 0; i < length; i += 3) {
            unsigned value = (unsigned)data[i] << 16;
            if (i + 1 < length)
                value |= (unsigned)data[i + 1] << 8;
            if (i + 2 < length)
                value |= data[i + 2];
            *out++ = alphabet[(value >> 18) & 63];
            *out++ = alphabet[(value >> 12) & 63];
            *out++ = i + 1 < length ? alphabet[(value >> 6) & 63] : '=';
            *out++ = i + 2 < length ? alphabet[value & 63] : '=';
        }
        *out = 0;
    }
    free(data);
    return output;
}
