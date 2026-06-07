#include "neverc/std/strconv.h"
#include <string.h>

int neverc_strconv_format_bool(int b, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0)
        return -1;

    const char *s = b ? "true" : "false";
    size_t len = strlen(s);

    if (len + 1 > bufsize)
        return -1;

    memcpy(buf, s, len + 1);
    return (int)len;
}
