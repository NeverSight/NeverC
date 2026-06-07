#include "neverc/path.h"
#include <string.h>

int neverc_path_match(const char *pattern, const char *name) {
    if (!pattern || !name) return -1;

    const char *p = pattern, *n = name;
    const char *p_star = NULL, *n_star = NULL;

    while (*n) {
        if (*p == '*') {
            p_star = p++;
            n_star = n;
        } else if (*p == '?') {
            if (*n == '/') return 0;
            p++; n++;
        } else if (*p == '[') {
            p++;
            int negated = 0;
            if (*p == '^' || *p == '!') { negated = 1; p++; }
            int matched = 0;
            if (*p == ']') { if (*n == ']') matched = 1; p++; }
            while (*p && *p != ']') {
                char lo = *p;
                if (p[1] == '-' && p[2] && p[2] != ']') {
                    char hi = p[2];
                    if ((unsigned char)*n >= (unsigned char)lo &&
                        (unsigned char)*n <= (unsigned char)hi)
                        matched = 1;
                    p += 3;
                } else {
                    if (*n == lo) matched = 1;
                    p++;
                }
            }
            if (*p != ']') return -1;
            p++;
            if (matched == negated) {
                if (p_star) { p = p_star + 1; n = ++n_star; continue; }
                return 0;
            }
            n++;
        } else if (*p == *n) {
            p++; n++;
        } else if (p_star) {
            p = p_star + 1;
            n = ++n_star;
        } else {
            return 0;
        }
    }

    while (*p == '*') p++;
    return *p == '\0' ? 1 : 0;
}
