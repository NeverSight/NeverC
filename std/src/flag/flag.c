#include "neverc/flag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FLAG_STRING, FLAG_INT, FLAG_BOOL, FLAG_DOUBLE };

typedef struct {
    const char *name;
    const char *usage;
    int         type;
    union {
        const char **s;
        int         *i;
        double      *d;
    } ptr;
} flag_entry_t;

static flag_entry_t flags[NEVERC_FLAG_MAX];
static int flag_count = 0;
static const char **remaining_args = NULL;
static int remaining_count = 0;

void neverc_flag_string(const char *name, const char *defval,
                        const char *usage, const char **ptr) {
    if (flag_count >= NEVERC_FLAG_MAX) return;
    *ptr = defval;
    flag_entry_t *f = &flags[flag_count++];
    f->name = name; f->usage = usage;
    f->type = FLAG_STRING; f->ptr.s = ptr;
}

void neverc_flag_int(const char *name, int defval,
                     const char *usage, int *ptr) {
    if (flag_count >= NEVERC_FLAG_MAX) return;
    *ptr = defval;
    flag_entry_t *f = &flags[flag_count++];
    f->name = name; f->usage = usage;
    f->type = FLAG_INT; f->ptr.i = ptr;
}

void neverc_flag_bool(const char *name, int defval,
                      const char *usage, int *ptr) {
    if (flag_count >= NEVERC_FLAG_MAX) return;
    *ptr = defval;
    flag_entry_t *f = &flags[flag_count++];
    f->name = name; f->usage = usage;
    f->type = FLAG_BOOL; f->ptr.i = ptr;
}

void neverc_flag_double(const char *name, double defval,
                        const char *usage, double *ptr) {
    if (flag_count >= NEVERC_FLAG_MAX) return;
    *ptr = defval;
    flag_entry_t *f = &flags[flag_count++];
    f->name = name; f->usage = usage;
    f->type = FLAG_DOUBLE; f->ptr.d = ptr;
}

static int parse_int(const char *s) {
    int neg = 0, val = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    return neg ? -val : val;
}

static double parse_double(const char *s) {
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    double val = 0.0;
    while (*s >= '0' && *s <= '9') { val = val * 10.0 + (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') {
            val += (*s - '0') * frac;
            frac *= 0.1; s++;
        }
    }
    return neg ? -val : val;
}

static flag_entry_t *find_flag(const char *name) {
    for (int i = 0; i < flag_count; i++) {
        if (strcmp(flags[i].name, name) == 0) return &flags[i];
    }
    return NULL;
}

int neverc_flag_parse(int argc, char **argv) {
    remaining_args = NULL;
    remaining_count = 0;

    int i = 1;
    while (i < argc) {
        char *arg = argv[i];
        if (arg[0] != '-') {
            remaining_args = (const char **)(argv + i);
            remaining_count = argc - i;
            return 0;
        }

        if (arg[0] == '-' && arg[1] == '-' && arg[2] == '\0') {
            remaining_args = (const char **)(argv + i + 1);
            remaining_count = argc - i - 1;
            return 0;
        }

        const char *name = arg + 1;
        if (name[0] == '-') name++;

        const char *eq = NULL;
        for (const char *p = name; *p; p++) {
            if (*p == '=') { eq = p; break; }
        }

        char name_buf[128];
        const char *value = NULL;
        if (eq) {
            size_t nlen = (size_t)(eq - name);
            if (nlen >= sizeof(name_buf)) nlen = sizeof(name_buf) - 1;
            memcpy(name_buf, name, nlen);
            name_buf[nlen] = '\0';
            name = name_buf;
            value = eq + 1;
        }

        flag_entry_t *f = find_flag(name);
        if (!f) {
            fprintf(stderr, "unknown flag: -%s\n", name);
            return -1;
        }

        switch (f->type) {
        case FLAG_BOOL:
            if (value) {
                *f->ptr.i = (strcmp(value, "true") == 0 ||
                             strcmp(value, "1") == 0);
            } else {
                *f->ptr.i = 1;
            }
            break;
        case FLAG_STRING:
            if (!value) {
                if (++i >= argc) {
                    fprintf(stderr, "flag -%s needs value\n", f->name);
                    return -1;
                }
                value = argv[i];
            }
            *f->ptr.s = value;
            break;
        case FLAG_INT:
            if (!value) {
                if (++i >= argc) {
                    fprintf(stderr, "flag -%s needs value\n", f->name);
                    return -1;
                }
                value = argv[i];
            }
            *f->ptr.i = parse_int(value);
            break;
        case FLAG_DOUBLE:
            if (!value) {
                if (++i >= argc) {
                    fprintf(stderr, "flag -%s needs value\n", f->name);
                    return -1;
                }
                value = argv[i];
            }
            *f->ptr.d = parse_double(value);
            break;
        }
        i++;
    }
    return 0;
}

int neverc_flag_narg(void) { return remaining_count; }

const char *neverc_flag_arg(int i) {
    if (i < 0 || i >= remaining_count) return NULL;
    return remaining_args[i];
}

void neverc_flag_print_defaults(void) {
    for (int i = 0; i < flag_count; i++) {
        flag_entry_t *f = &flags[i];
        fprintf(stderr, "  -%s", f->name);
        if (f->usage) fprintf(stderr, "\n    \t%s", f->usage);
        fprintf(stderr, "\n");
    }
}

void neverc_flag_reset(void) {
    flag_count = 0;
    remaining_args = NULL;
    remaining_count = 0;
}
