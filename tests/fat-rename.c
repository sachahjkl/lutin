#include <errno.h>
#include <sys/stat.h>

int __real_rename(const char *source, const char *destination);

int __wrap_rename(const char *source, const char *destination) {
    struct stat information;
    if (stat(destination, &information) == 0) {
        errno = EEXIST;
        return -1;
    }
    return __real_rename(source, destination);
}
