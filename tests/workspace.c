#include "workspace.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned rename_calls, rename_failures;
static bool short_write, close_failure, unlink_failure;
static unsigned unlink_calls, unlink_fail_at;
int __real_rename(const char *, const char *);
int __real_unlink(const char *);
size_t __real_fwrite(const void *, size_t, size_t, FILE *);
int __real_fclose(FILE *);

int __wrap_rename(const char *from, const char *to) {
    unsigned call = rename_calls++;
    if (call < 32 && (rename_failures & (1u << call))) {
        errno = EIO;
        return -1;
    }
    struct stat information;
    if (stat(to, &information) == 0) {
        errno = EEXIST;
        return -1;
    }
    return __real_rename(from, to);
}

int __wrap_unlink(const char *path) {
    if (unlink_failure || ++unlink_calls == unlink_fail_at) {
        errno = EIO;
        return -1;
    }
    return __real_unlink(path);
}

size_t __wrap_fwrite(const void *data, size_t size, size_t count, FILE *file) {
    if (short_write) {
        errno = ENOSPC;
        return __real_fwrite(data, size, count / 2, file);
    }
    return __real_fwrite(data, size, count, file);
}

int __wrap_fclose(FILE *file) {
    int result = __real_fclose(file);
    if (close_failure) {
        errno = EIO;
        return EOF;
    }
    return result;
}

static void expect(const char *text) {
    char *data = workspace_read("main.lua", 128);
    assert(data && strcmp(data, text) == 0);
    free(data);
}

int main(void) {
    workspace_init(true, "storage");
    unsigned created, projects[12], count;
    for (unsigned i = 2; i <= 16; i++)
        assert(workspace_create(&created) && created == i);
    assert(workspace_select(1234));
    assert(workspace_projects(0, projects, 12, &count) && count == 12);
    assert(projects[0] == 1 && projects[11] == 12);
    assert(workspace_projects(12, projects, 12, &count) && count == 5);
    assert(projects[0] == 13 && projects[4] == 1234);
    assert(mkdir("storage/projects/0017", 0700) == 0);
    FILE *not_directory = fopen("storage/projects/18", "wb");
    assert(not_directory && fclose(not_directory) == 0);
    assert(workspace_projects(16, projects, 12, &count) && count == 1 &&
           projects[0] == 1234);
    assert(workspace_create(&created) && created == 1235);
    assert(workspace_select(1));
    assert(workspace_write("main.lua", "old"));
    assert(workspace_write("main.lua", "new"));
    expect("new");
    const unsigned cases[] = {1, 2, 3, 6};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        assert(workspace_write("main.lua", "old"));
        rename_calls = 0;
        rename_failures = cases[i];
        assert(!workspace_write("main.lua", "replacement"));
        rename_failures = 0;
        expect("old");
    }
    short_write = true;
    assert(!workspace_write("main.lua", "partial data"));
    short_write = false;
    expect("old");
    close_failure = true;
    assert(!workspace_write("main.lua", "close failed"));
    close_failure = false;
    expect("old");
    unlink_failure = true;
    assert(!workspace_write("main.lua", "cleanup failed"));
    unlink_failure = false;
    expect("old");
    unlink_calls = 0;
    unlink_fail_at = 2;
    assert(workspace_write("main.lua", "cleanup pending"));
    unlink_fail_at = 0;
    expect("cleanup pending");
    assert(workspace_write("main.lua", "old"));
    assert(unlink("storage/projects/1/.write.tmp") == -1 && errno == ENOENT);
    assert(mkdir("storage/projects/1/.write.tmp", 0700) == 0);
    assert(!workspace_write("main.lua", "blocked temporary"));
    expect("old");
    assert(rmdir("storage/projects/1/.write.tmp") == 0);
    assert(rename("storage/projects/1/main.lua",
                  "storage/projects/1/.main.lua.bak") == 0);
    rename_calls = 0;
    rename_failures = 1;
    assert(!workspace_read("main.lua", 128));
    rename_failures = 0;
    workspace_init(true, "storage");
    expect("old");
    assert(workspace_write("main.lua", "committed"));
    FILE *backup = fopen("storage/projects/1/.main.lua.bak", "wb");
    assert(backup && fwrite("stale", 1, 5, backup) == 5 && fclose(backup) == 0);
    workspace_init(true, "storage");
    expect("committed");
    FILE *collision = fopen("storage/projects/18", "wb");
    assert(collision && fclose(collision) == 0);
    assert(!workspace_select(18));
    assert(strcmp(workspace_root(), "storage/projects/1") == 0);
    expect("committed");
    assert(workspace_select(3));
    assert(workspace_missing("main.lua"));
    assert(workspace_write("main.lua", "other project"));
    assert(workspace_select(1));
    expect("committed");
    puts("Workspace: FAT replacement, injected I/O failures and restart "
         "recovery passed");
    return 0;
}
