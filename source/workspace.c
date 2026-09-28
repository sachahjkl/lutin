#include "workspace.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool storage;
static char base_path[80];
static char root[112];
static char error[96];

static bool file_error(const char *operation) {
    snprintf(error, sizeof(error), "%s: %s", operation, strerror(errno));
    return false;
}

static bool recover(const char *filename, const char *backup) {
    struct stat information;
    if (stat(filename, &information) == 0)
        return true;
    if (errno != ENOENT)
        return file_error("Check file");
    if (stat(backup, &information) != 0)
        return errno == ENOENT ? true : file_error("Check backup");
    if (rename(backup, filename) != 0)
        return file_error("Restore backup");
    return true;
}

void workspace_init(bool available, const char *base) {
    storage = available;
    snprintf(base_path, sizeof(base_path), "%s", base);
    if (storage) {
        mkdir(base_path, 0777);
        snprintf(root, sizeof(root), "%s/projects", base_path);
        mkdir(root, 0777);
        workspace_select(1);
    }
}

bool workspace_select(unsigned project) {
    if (!project)
        return false;
    if (!storage)
        return false;
    char selected[sizeof(root)];
    snprintf(selected, sizeof(selected), "%s/projects/%u", base_path, project);
    if (mkdir(selected, 0777) != 0 && errno != EEXIST)
        return file_error("Create project");
    struct stat information;
    if (stat(selected, &information) != 0)
        return file_error("Check project");
    if (!S_ISDIR(information.st_mode)) {
        snprintf(error, sizeof(error), "Project path is not a directory");
        return false;
    }
    snprintf(root, sizeof(root), "%s", selected);
    return true;
}

const char *workspace_root(void) { return root; }
const char *workspace_error(void) { return error; }

bool workspace_projects(unsigned after, unsigned *items, unsigned capacity,
                        unsigned *count) {
    *count = 0;
    if (!storage) {
        snprintf(error, sizeof(error), "SD card unavailable");
        return false;
    }
    char path[112];
    snprintf(path, sizeof(path), "%s/projects", base_path);
    DIR *directory = opendir(path);
    if (!directory)
        return file_error("List projects");
    struct dirent *entry;
    bool ok = true;
    while (true) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) {
            if (errno)
                ok = file_error("Read projects");
            break;
        }
        const char *name = entry->d_name;
        if (*name < '1' || *name > '9')
            continue;
        unsigned number = 0;
        const char *digit = name;
        for (; *digit >= '0' && *digit <= '9'; digit++) {
            unsigned value = (unsigned)(*digit - '0');
            if (number > (UINT_MAX - value) / 10)
                break;
            number = number * 10 + value;
        }
        if (*digit || number <= after)
            continue;
        snprintf(path, sizeof(path), "%s/projects/%u", base_path, number);
        struct stat information;
        if (stat(path, &information) != 0 || !S_ISDIR(information.st_mode))
            continue;
        unsigned position = 0;
        while (position < *count && items[position] < number)
            position++;
        if (position >= capacity)
            continue;
        if (*count < capacity)
            (*count)++;
        for (unsigned i = *count - 1; i > position; i--)
            items[i] = items[i - 1];
        items[position] = number;
    }
    closedir(directory);
    return ok;
}

bool workspace_create(unsigned *project) {
    if (!storage) {
        snprintf(error, sizeof(error), "SD card unavailable");
        return false;
    }
    unsigned items[WORKSPACE_PROJECT_PAGE_SIZE], count, last = 0;
    do {
        if (!workspace_projects(last, items, WORKSPACE_PROJECT_PAGE_SIZE,
                                &count))
            return false;
        if (count)
            last = items[count - 1];
    } while (count == WORKSPACE_PROJECT_PAGE_SIZE);
    if (last == UINT_MAX) {
        snprintf(error, sizeof(error), "Project identifiers exhausted");
        return false;
    }
    char path[112];
    snprintf(path, sizeof(path), "%s/projects/%u", base_path, last + 1);
    if (mkdir(path, 0777) != 0)
        return file_error("Create project");
    *project = last + 1;
    return true;
}

bool workspace_remove(const char *path) {
    char filename[192], backup[256];
    if (!workspace_path(path, filename, sizeof(filename)))
        return false;
    snprintf(backup, sizeof(backup), "%s/.%s.bak", root, path);
    if (unlink(backup) != 0 && errno != ENOENT)
        return file_error("Remove backup");
    if (unlink(filename) != 0 && errno != ENOENT)
        return file_error("Remove file");
    return true;
}

bool workspace_missing(const char *path) {
    char filename[192];
    char backup[256];
    struct stat information;
    if (!workspace_path(path, filename, sizeof(filename)))
        return false;
    snprintf(backup, sizeof(backup), "%s/.%s.bak", root, path);
    return recover(filename, backup) && stat(filename, &information) != 0 &&
           errno == ENOENT;
}

bool workspace_path(const char *path, char *output, size_t capacity) {
    if (!storage) {
        snprintf(error, sizeof(error), "SD card unavailable");
        return false;
    }
    if (!path || !*path || strlen(path) > 80 || path[0] == '.' ||
        strstr(path, "..")) {
        snprintf(error, sizeof(error), "Invalid project filename");
        return false;
    }
    for (const char *p = path; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-' ||
              *p == '.')) {
            snprintf(error, sizeof(error),
                     "Use letters, digits, dot, dash or underscore");
            return false;
        }
    }
    if (snprintf(output, capacity, "%s/%s", root, path) >= (int)capacity)
        return false;
    return true;
}

char *workspace_read(const char *path, size_t limit) {
    char filename[160];
    if (!workspace_path(path, filename, sizeof(filename)))
        return NULL;
    char backup[256];
    snprintf(backup, sizeof(backup), "%s/.%s.bak", root, path);
    if (!recover(filename, backup))
        return NULL;
    FILE *file = fopen(filename, "rb");
    if (!file) {
        snprintf(error, sizeof(error), "File not found: %.64s", path);
        return NULL;
    }
    char *data = malloc(limit + 1);
    if (!data) {
        fclose(file);
        snprintf(error, sizeof(error), "Not enough memory");
        return NULL;
    }
    size_t length = fread(data, 1, limit, file);
    bool failed = ferror(file) || fgetc(file) != EOF;
    fclose(file);
    if (failed) {
        free(data);
        snprintf(error, sizeof(error), "File exceeds read limit");
        return NULL;
    }
    data[length] = 0;
    return data;
}

bool workspace_write(const char *path, const char *data) {
    char filename[160], temporary[168];
    if (!workspace_path(path, filename, sizeof(filename)))
        return false;
    char backup[256];
    snprintf(backup, sizeof(backup), "%s/.%s.bak", root, path);
    if (!recover(filename, backup))
        return false;
    if (unlink(backup) != 0 && errno != ENOENT)
        return file_error("Remove old backup");
    snprintf(temporary, sizeof(temporary), "%s/.write.tmp", root);
    FILE *file = fopen(temporary, "wb");
    if (!file) {
        return file_error("Open temporary file");
    }
    bool ok = fwrite(data, 1, strlen(data), file) == strlen(data);
    if (fclose(file) != 0)
        ok = false;
    if (!ok)
        return file_error("Write temporary file");
    bool previous = rename(filename, backup) == 0;
    if (!previous && errno != ENOENT)
        return file_error("Back up file");
    if (rename(temporary, filename) != 0) {
        int failure = errno;
        if (previous && rename(backup, filename) != 0)
            return file_error("Restore backup");
        errno = failure;
        return file_error("Install saved file");
    }
    /* The new file is committed. A leftover backup is removed on the next
     * write. */
    if (previous)
        unlink(backup);
    return true;
}

bool workspace_load_entry(char *output, size_t capacity, const char *demo) {
    char *data = workspace_read("main.lua", capacity - 1);
    if (!data && storage && !workspace_missing("main.lua"))
        return false;
    snprintf(output, capacity, "%s", data ? data : demo);
    free(data);
    return true;
}
