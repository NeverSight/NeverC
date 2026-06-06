#include "neverc/strconv.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

int neverc_strconv_format_float(double f, char fmt, int prec, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0)
        return -1;

    char fmtstr[32];
    int len;

    switch (fmt) {
    case 'e':
        if (prec < 0)
            snprintf(fmtstr, sizeof(fmtstr), "%%e");
        else
            snprintf(fmtstr, sizeof(fmtstr), "%%.%de", prec);
        break;
    case 'E':
        if (prec < 0)
            snprintf(fmtstr, sizeof(fmtstr), "%%E");
        else
            snprintf(fmtstr, sizeof(fmtstr), "%%.%dE", prec);
        break;
    case 'f':
        if (prec < 0)
            snprintf(fmtstr, sizeof(fmtstr), "%%f");
        else
            snprintf(fmtstr, sizeof(fmtstr), "%%.%df", prec);
        break;
    case 'g':
        if (prec < 0)
            snprintf(fmtstr, sizeof(fmtstr), "%%g");
        else
            snprintf(fmtstr, sizeof(fmtstr), "%%.%dg", prec);
        break;
    case 'G':
        if (prec < 0)
            snprintf(fmtstr, sizeof(fmtstr), "%%G");
        else
            snprintf(fmtstr, sizeof(fmtstr), "%%.%dG", prec);
        break;
    default:
        if (prec < 0)
            snprintf(fmtstr, sizeof(fmtstr), "%%g");
        else
            snprintf(fmtstr, sizeof(fmtstr), "%%.%dg", prec);
        break;
    }

    len = snprintf(buf, bufsize, fmtstr, f);
    if (len < 0 || (size_t)len >= bufsize)
        return -1;

    return len;
}
