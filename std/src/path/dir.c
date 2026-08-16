#include "neverc/std/path.h"
#include <stdlib.h>
#include <string.h>

int neverc_path_dir(const char *path, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0)
        return -1;

    if (!path || *path == '\0')
        return neverc_path_clean("", buf, bufsize);

    size_t len = strlen(path);
    size_t i = len;
    while (i > 0 && path[i - 1] != '/')
        i--;

    char *tmp = (char *)malloc(i + 1);
    if (!tmp)
        return -1;
    if (i > 0)
        memcpy(tmp, path, i);
    tmp[i] = '\0';

    int n = neverc_path_clean(tmp, buf, bufsize);
    free(tmp);
    return n;
}
