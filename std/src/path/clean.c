#include "neverc/std/path.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int clean_into(const char *path, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0)
        return -1;

    if (!path || *path == '\0') {
        if (bufsize < 2) return -1;
        buf[0] = '.';
        buf[1] = '\0';
        return 1;
    }

    size_t n = strlen(path);
    int rooted = (path[0] == '/');

    /* work buffer — use the output buffer directly */
    char *out = buf;
    size_t w = 0, dotdot = 0;
    size_t r = 0;

#define CLEAN_FAIL() do { buf[0] = '\0'; return -1; } while (0)

    if (rooted) {
        if (w + 1 >= bufsize) CLEAN_FAIL();
        out[w++] = '/';
        r = 1;
        dotdot = 1;
    }

    while (r < n) {
        if (path[r] == '/') {
            r++;
        } else if (path[r] == '.' && (r + 1 == n || path[r + 1] == '/')) {
            r++;
        } else if (path[r] == '.' && path[r + 1] == '.' &&
                   (r + 2 == n || path[r + 2] == '/')) {
            r += 2;
            if (w > dotdot) {
                w--;
                while (w > dotdot && out[w] != '/')
                    w--;
            } else if (!rooted) {
                if (w > 0) {
                    if (w + 1 >= bufsize) CLEAN_FAIL();
                    out[w++] = '/';
                }
                if (w + 2 >= bufsize) CLEAN_FAIL();
                out[w++] = '.';
                out[w++] = '.';
                dotdot = w;
            }
        } else {
            if ((rooted && w != 1) || (!rooted && w != 0)) {
                if (w + 1 >= bufsize) CLEAN_FAIL();
                out[w++] = '/';
            }
            for (; r < n && path[r] != '/'; r++) {
                if (w + 1 >= bufsize) CLEAN_FAIL();
                out[w++] = path[r];
            }
        }
    }

#undef CLEAN_FAIL

    if (w == 0) {
        if (bufsize < 2) {
            buf[0] = '\0';
            return -1;
        }
        out[0] = '.';
        out[1] = '\0';
        return 1;
    }

    out[w] = '\0';
    if (w > (size_t)INT_MAX) return -1;
    return (int)w;
}

/* clean_into never writes past the bytes it has read, so a buffer longer
 * than the input always suffices. A shorter buffer may still hold the
 * result even though the output written before a ".." backs up would not,
 * so clean into scratch space sized for the input first. */
int neverc_path_clean(const char *path, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0)
        return -1;
    size_t n = path ? strlen(path) : 0;
    if (bufsize > n)
        return clean_into(path, buf, bufsize);

    char *tmp = (char *)malloc(n + 1);
    if (!tmp) {
        buf[0] = '\0';
        return -1;
    }
    int len = clean_into(path, tmp, n + 1);
    if (len < 0 || (size_t)len >= bufsize) {
        buf[0] = '\0';
        len = -1;
    } else {
        memcpy(buf, tmp, (size_t)len + 1);
    }
    free(tmp);
    return len;
}
