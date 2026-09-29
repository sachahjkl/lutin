#ifndef LUTIN_IMAGE_H
#define LUTIN_IMAGE_H
#include <stdbool.h>
#include <stddef.h>
bool image_capture(char *name, size_t capacity);
char *image_data_url(const char *name);
#endif
