#include "neverc/std/strconv.h"
#include <string.h>

int neverc_strconv_parse_bool(const char *s, int *result) {
    if (!s || !result)
        return NEVERC_STRCONV_ERR_SYNTAX;

    if (strcmp(s, "1") == 0 || strcmp(s, "t") == 0 || strcmp(s, "T") == 0 ||
        strcmp(s, "true") == 0 || strcmp(s, "TRUE") == 0 || strcmp(s, "True") == 0) {
        *result = 1;
        return NEVERC_STRCONV_OK;
    }
    if (strcmp(s, "0") == 0 || strcmp(s, "f") == 0 || strcmp(s, "F") == 0 ||
        strcmp(s, "false") == 0 || strcmp(s, "FALSE") == 0 || strcmp(s, "False") == 0) {
        *result = 0;
        return NEVERC_STRCONV_OK;
    }
    return NEVERC_STRCONV_ERR_SYNTAX;
}
