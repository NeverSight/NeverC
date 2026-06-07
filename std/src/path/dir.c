#include "neverc/std/path.h"
#include <string.h>

int neverc_path_dir(const char *path, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0)
        return -1;

    if (!path || *path == '\0')
        return neverc_path_clean("", buf, bufsize);

    /* find last slash */
    size_t len = strlen(path);
    size_t i = len;
    while (i > 0 && path[i - 1] != '/')
        i--;

    /* dir part is path[:i] */
    char tmp[4096];
    if (i == 0) {
        tmp[0] = '\0';
    } else {
        if (i >= sizeof(tmp)) i = sizeof(tmp) - 1;
        memcpy(tmp, path, i);
        tmp[i] = '\0';
    }

    return neverc_path_clean(tmp, buf, bufsize);
}
