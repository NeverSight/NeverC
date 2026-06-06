#include "neverc/path.h"
#include <string.h>

const char *neverc_path_ext(const char *path) {
    if (!path)
        return "";

    size_t len = strlen(path);

    for (size_t i = len; i > 0; i--) {
        if (path[i - 1] == '/')
            break;
        if (path[i - 1] == '.')
            return path + i - 1;
    }
    return "";
}
