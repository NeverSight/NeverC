#include "neverc/path.h"
#include <string.h>
#include <stdio.h>

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
    int len;

    if (a_empty) {
        len = snprintf(tmp, sizeof(tmp), "%s", b);
    } else if (b_empty) {
        len = snprintf(tmp, sizeof(tmp), "%s", a);
    } else {
        len = snprintf(tmp, sizeof(tmp), "%s/%s", a, b);
    }

    if (len < 0 || (size_t)len >= sizeof(tmp))
        return -1;

    return neverc_path_clean(tmp, buf, bufsize);
}
