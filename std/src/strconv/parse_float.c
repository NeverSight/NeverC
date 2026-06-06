#include "neverc/strconv.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

int neverc_strconv_parse_float(const char *s, double *result) {
    if (!s || !result)
        return NEVERC_STRCONV_ERR_SYNTAX;

    while (isspace((unsigned char)*s))
        s++;

    if (*s == '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;

    char *end;
    errno = 0;
    double val = strtod(s, &end);

    if (end == s)
        return NEVERC_STRCONV_ERR_SYNTAX;

    while (isspace((unsigned char)*end))
        end++;

    if (*end != '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;

    if (errno == ERANGE) {
        *result = val;
        return NEVERC_STRCONV_ERR_RANGE;
    }

    *result = val;
    return NEVERC_STRCONV_OK;
}
