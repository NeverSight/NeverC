#include "neverc/std/path.h"
#include <string.h>

int neverc_path_clean(const char *path, char *buf, size_t bufsize) {
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

    if (rooted) {
        if (w + 1 >= bufsize) return -1;
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
                    if (w + 1 >= bufsize) return -1;
                    out[w++] = '/';
                }
                if (w + 2 >= bufsize) return -1;
                out[w++] = '.';
                out[w++] = '.';
                dotdot = w;
            }
        } else {
            if ((rooted && w != 1) || (!rooted && w != 0)) {
                if (w + 1 >= bufsize) return -1;
                out[w++] = '/';
            }
            for (; r < n && path[r] != '/'; r++) {
                if (w + 1 >= bufsize) return -1;
                out[w++] = path[r];
            }
        }
    }

    if (w == 0) {
        if (bufsize < 2) return -1;
        out[0] = '.';
        out[1] = '\0';
        return 1;
    }

    out[w] = '\0';
    return (int)w;
}
