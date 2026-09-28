#ifndef AI_WORKSPACE_H
#define AI_WORKSPACE_H
#include <stdbool.h>
#include <stddef.h>
void workspace_init(bool available, const char *base);
bool workspace_select(unsigned project);
bool workspace_projects(unsigned after, unsigned *items, unsigned capacity,
                        unsigned *count);
bool workspace_create(unsigned *project);
const char *workspace_root(void);
char *workspace_read(const char *path, size_t limit);
bool workspace_load_entry(char *output, size_t capacity, const char *demo);
bool workspace_write(const char *path, const char *data);
bool workspace_remove(const char *path);
bool workspace_missing(const char *path);
bool workspace_path(const char *path, char *output, size_t capacity);
const char *workspace_error(void);
#endif
