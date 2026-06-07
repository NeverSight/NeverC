#include "neverc/std/path.h"
#include <string.h>

int neverc_path_join2(const char *a, const char *b, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0)
        return -1;

    int a_empty = (!a || *a == '\0');
    int b_empty = (!b || *b == '\0');

    if (a_empty && b_empty) {
        buf[0] = '\0';
        return 0;
    }

    char tmp[4096];
    size_t len = 0;

    if (!a_empty) {
        size_t alen = strlen(a);
        if (alen >= sizeof(tmp)) return -1;
        memcpy(tmp, a, alen);
        len = alen;
    }

    if (!a_empty && !b_empty) {
        if (len + 1 >= sizeof(tmp)) return -1;
        tmp[len++] = '/';
    }

    if (!b_empty) {
        size_t blen = strlen(b);
        if (len + blen >= sizeof(tmp)) return -1;
        memcpy(tmp + len, b, blen);
        len += blen;
    }

    tmp[len] = '\0';
    return neverc_path_clean(tmp, buf, bufsize);
}
