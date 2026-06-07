#include "neverc/std/path.h"

int neverc_path_isabs(const char *path) {
    return path && path[0] == '/';
}
