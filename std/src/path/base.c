#include "neverc/std/path.h"
#include <limits.h>
#include <string.h>

int neverc_path_base(const char *path, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0)
        return -1;

    if (!path || *path == '\0') {
        if (bufsize < 2) return -1;
        buf[0] = '.';
        buf[1] = '\0';
        return 1;
    }

    size_t len = strlen(path);

    /* strip trailing slashes */
    while (len > 0 && path[len - 1] == '/')
        len--;

    if (len == 0) {
        if (bufsize < 2) return -1;
        buf[0] = '/';
        buf[1] = '\0';
        return 1;
    }

    /* find last slash */
    size_t i = len;
    while (i > 0 && path[i - 1] != '/')
        i--;

    size_t result_len = len - i;
    if (result_len + 1 > bufsize || result_len > (size_t)INT_MAX)
        return -1;

    memcpy(buf, path + i, result_len);
    buf[result_len] = '\0';
    return (int)result_len;
}
