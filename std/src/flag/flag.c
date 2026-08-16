#include "neverc/std/flag.h"
#include "neverc/std/strconv.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

enum { FLAG_STRING, FLAG_INT, FLAG_BOOL, FLAG_DOUBLE, FLAG_INT64, FLAG_UINT64 };

typedef struct {
    const char *name;
    const char *usage;
    int         type;
    int         was_set;
    union {
        const char      **s;
        int              *i;
        long long        *i64;
        unsigned long long *u64;
        double           *d;
    } ptr;
} flag_entry_t;

static flag_entry_t flags[NEVERC_FLAG_MAX];
static int flag_count = 0;
static int flag_parsed = 0;
static char **remaining_args = NULL;
static int remaining_count = 0;

static flag_entry_t *register_flag(const char *name, const char *usage,
                                   int type) {
    if (!name || name[0] == '\0' || flag_count >= NEVERC_FLAG_MAX) return NULL;
    for (int i = 0; i < flag_count; i++) {
        if (strcmp(flags[i].name, name) == 0) return NULL;
    }
    flag_entry_t *f = &flags[flag_count++];
    f->name = name;
    f->usage = usage;
    f->type = type;
    f->was_set = 0;
    return f;
}

void neverc_flag_string(const char *name, const char *defval,
                        const char *usage, const char **ptr) {
    if (!ptr) return;
    flag_entry_t *f = register_flag(name, usage, FLAG_STRING);
    if (!f) return;
    *ptr = defval;
    f->ptr.s = ptr;
}

void neverc_flag_int(const char *name, int defval,
                     const char *usage, int *ptr) {
    if (!ptr) return;
    flag_entry_t *f = register_flag(name, usage, FLAG_INT);
    if (!f) return;
    *ptr = defval;
    f->ptr.i = ptr;
}

void neverc_flag_bool(const char *name, int defval,
                      const char *usage, int *ptr) {
    if (!ptr) return;
    flag_entry_t *f = register_flag(name, usage, FLAG_BOOL);
    if (!f) return;
    *ptr = defval;
    f->ptr.i = ptr;
}

void neverc_flag_double(const char *name, double defval,
                        const char *usage, double *ptr) {
    if (!ptr) return;
    flag_entry_t *f = register_flag(name, usage, FLAG_DOUBLE);
    if (!f) return;
    *ptr = defval;
    f->ptr.d = ptr;
}

void neverc_flag_int64(const char *name, long long defval,
                       const char *usage, long long *ptr) {
    if (!ptr) return;
    flag_entry_t *f = register_flag(name, usage, FLAG_INT64);
    if (!f) return;
    *ptr = defval;
    f->ptr.i64 = ptr;
}

void neverc_flag_uint64(const char *name, unsigned long long defval,
                        const char *usage, unsigned long long *ptr) {
    if (!ptr) return;
    flag_entry_t *f = register_flag(name, usage, FLAG_UINT64);
    if (!f) return;
    *ptr = defval;
    f->ptr.u64 = ptr;
}

static flag_entry_t *find_flag_n(const char *name, size_t name_len) {
    if (!name) return NULL;
    for (int i = 0; i < flag_count; i++) {
        if (strlen(flags[i].name) == name_len &&
            memcmp(flags[i].name, name, name_len) == 0)
            return &flags[i];
    }
    return NULL;
}

static flag_entry_t *find_flag(const char *name) {
    return name ? find_flag_n(name, strlen(name)) : NULL;
}

static int set_entry_value(flag_entry_t *f, const char *value,
                           int implicit_bool) {
    if (!f) return -1;
    switch (f->type) {
    case FLAG_BOOL: {
        int parsed = 1;
        if (!implicit_bool &&
            neverc_strconv_parse_bool(value, &parsed) != NEVERC_STRCONV_OK)
            return -1;
        *f->ptr.i = parsed;
        return 0;
    }
    case FLAG_STRING:
        if (!value) return -1;
        *f->ptr.s = value;
        return 0;
    case FLAG_INT: {
        long long parsed;
        if (neverc_strconv_parse_int(value, 0, &parsed) != NEVERC_STRCONV_OK)
            return -1;
        if (parsed < (long long)INT_MIN || parsed > (long long)INT_MAX)
            return -1;
        *f->ptr.i = (int)parsed;
        return 0;
    }
    case FLAG_INT64: {
        long long parsed;
        if (neverc_strconv_parse_int(value, 0, &parsed) != NEVERC_STRCONV_OK)
            return -1;
        *f->ptr.i64 = parsed;
        return 0;
    }
    case FLAG_UINT64: {
        unsigned long long parsed;
        if (neverc_strconv_parse_uint(value, 0, &parsed) != NEVERC_STRCONV_OK)
            return -1;
        *f->ptr.u64 = parsed;
        return 0;
    }
    case FLAG_DOUBLE: {
        double parsed;
        if (neverc_strconv_parse_float(value, &parsed) != NEVERC_STRCONV_OK)
            return -1;
        *f->ptr.d = parsed;
        return 0;
    }
    }
    return -1;
}

int neverc_flag_parse(int argc, char **argv) {
    flag_parsed = 1;
    remaining_args = NULL;
    remaining_count = 0;
    if (argc < 0 || (argc > 0 && !argv)) return -1;

    int i = 1;
    while (i < argc) {
        char *arg = argv[i];
        if (!arg) return -1;
        /* A lone "-" is a positional argument (stdin convention), matching Go. */
        if (arg[0] != '-' || arg[1] == '\0') {
            remaining_args = argv + i;
            remaining_count = argc - i;
            return 0;
        }

        if (arg[1] == '-' && arg[2] == '\0') {
            remaining_args = argv + i + 1;
            remaining_count = argc - i - 1;
            return 0;
        }

        const char *name = arg + 1;
        if (name[0] == '-') name++;

        const char *eq = strchr(name, '=');
        const char *value = eq ? eq + 1 : NULL;
        flag_entry_t *f = eq ? find_flag_n(name, (size_t)(eq - name))
                             : find_flag(name);
        if (!f) {
            fprintf(stderr, "unknown flag: %s\n", arg);
            return -1;
        }

        int implicit_bool = f->type == FLAG_BOOL && !value;
        if (f->type != FLAG_BOOL && !value) {
            if (++i >= argc || !argv[i]) {
                fprintf(stderr, "flag -%s needs value\n", f->name);
                return -1;
            }
            if (strcmp(argv[i], "--") == 0) {
                fprintf(stderr, "flag -%s needs value\n", f->name);
                return -1;
            }
            value = argv[i];
        }
        if (set_entry_value(f, value, implicit_bool) != 0) {
            fprintf(stderr, "invalid value for flag -%s\n", f->name);
            return -1;
        }
        f->was_set = 1;
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

int neverc_flag_parsed(void) { return flag_parsed; }

int neverc_flag_nflag(void) {
    int n = 0;
    for (int i = 0; i < flag_count; i++)
        if (flags[i].was_set) n++;
    return n;
}

int neverc_flag_set(const char *name, const char *value) {
    flag_entry_t *f = find_flag(name);
    if (!f) return -1;
    int implicit_bool = f->type == FLAG_BOOL && !value;
    if (set_entry_value(f, value, implicit_bool) != 0) return -1;
    f->was_set = 1;
    return 0;
}

int neverc_flag_lookup(const char *name, const char **usage_out) {
    flag_entry_t *f = find_flag(name);
    if (!f) return -1;
    if (usage_out) *usage_out = f->usage;
    return 0;
}

void neverc_flag_visit(neverc_flag_visit_fn fn, void *ctx) {
    if (!fn) return;
    flag_entry_t snapshot[NEVERC_FLAG_MAX];
    int count = flag_count;
    memcpy(snapshot, flags, (size_t)count * sizeof(snapshot[0]));
    for (int i = 0; i < count; i++) {
        if (snapshot[i].was_set)
            fn(snapshot[i].name, snapshot[i].usage, ctx);
    }
}

void neverc_flag_visit_all(neverc_flag_visit_fn fn, void *ctx) {
    if (!fn) return;
    flag_entry_t snapshot[NEVERC_FLAG_MAX];
    int count = flag_count;
    memcpy(snapshot, flags, (size_t)count * sizeof(snapshot[0]));
    for (int i = 0; i < count; i++)
        fn(snapshot[i].name, snapshot[i].usage, ctx);
}

void neverc_flag_reset(void) {
    memset(flags, 0, sizeof(flags));
    flag_count = 0;
    flag_parsed = 0;
    remaining_args = NULL;
    remaining_count = 0;
}
