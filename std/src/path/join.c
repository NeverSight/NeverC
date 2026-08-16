#include "neverc/std/path.h"
#include <stdint.h>
#include <stdlib.h>
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

    size_t alen = a_empty ? 0 : strlen(a);
    size_t blen = b_empty ? 0 : strlen(b);
    int need_slash = (!a_empty && !b_empty);
    if (blen > SIZE_MAX - 2 || alen > SIZE_MAX - blen - (need_slash ? 2 : 1))
        return -1;
    size_t need = alen + (need_slash ? 1 : 0) + blen + 1;

    char *tmp = (char *)malloc(need);
    if (!tmp)
        return -1;

    size_t len = 0;
    if (!a_empty) {
        memcpy(tmp, a, alen);
        len = alen;
    }
    if (need_slash)
        tmp[len++] = '/';
    if (!b_empty) {
        memcpy(tmp + len, b, blen);
        len += blen;
    }
    tmp[len] = '\0';

    int n = neverc_path_clean(tmp, buf, bufsize);
    free(tmp);
    return n;
}
