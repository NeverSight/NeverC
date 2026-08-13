#include "neverc/std/os.h"
#include <stdio.h>
#include <string.h>

static int entropy_fails(unsigned char *buffer, size_t length) {
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

#define NCI_OS_RANDOM entropy_fails
#include "../../../std/src/os/os.c"
#undef NCI_OS_RANDOM

int main(void) {
    if (neverc_os_create_temp(".", "neverc_entropy_") != NULL) {
        fputs("create_temp ignored entropy failure\n", stderr);
        return 1;
    }

    char path[128] = "unchanged";
    if (neverc_os_mkdir_temp(
            ".", "neverc_entropy_", path, sizeof(path)) != -1 ||
        strcmp(path, "unchanged") != 0) {
        fputs("mkdir_temp ignored entropy failure\n", stderr);
        return 1;
    }

    puts("passed");
    return 0;
}
