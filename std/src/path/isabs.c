#include "neverc/std/path.h"
#include <stdlib.h>
#include <string.h>

int neverc_path_isabs(const char *path) {
    return path && path[0] == '/';
}

int neverc_path_is_local(const char *path) {
    if (!path || *path == '\0' || path[0] == '/')
        return 0;

    int has_dots = 0;
    const char *p = path;
    while (*p) {
        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t n = (size_t)(p - start);
        if ((n == 1 && start[0] == '.') ||
            (n == 2 && start[0] == '.' && start[1] == '.'))
            has_dots = 1;
        if (*p)
            p++;
    }
    if (!has_dots)
        return 1;

    size_t cap = strlen(path) + 2;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return 0;
    int ok = 0;
    if (neverc_path_clean(path, buf, cap) >= 0 &&
        strcmp(buf, "..") != 0 && strncmp(buf, "../", 3) != 0)
        ok = 1;
    free(buf);
    return ok;
}
