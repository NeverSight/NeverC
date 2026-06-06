#include "neverc/path.h"
#include <string.h>

int neverc_path_split(const char *path, char *dir, size_t dirsize,
                      char *file, size_t filesize) {
    if (!path)
        path = "";

    size_t len = strlen(path);

    /* find last slash */
    size_t i = len;
    while (i > 0 && path[i - 1] != '/')
        i--;

    /* dir = path[:i], file = path[i:] */
    if (dir && dirsize > 0) {
        if (i + 1 > dirsize) return -1;
        memcpy(dir, path, i);
        dir[i] = '\0';
    }

    if (file && filesize > 0) {
        size_t flen = len - i;
        if (flen + 1 > filesize) return -1;
        memcpy(file, path + i, flen);
        file[flen] = '\0';
    }

    return 0;
}
