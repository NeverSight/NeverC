#include "neverc/std/errors.h"
#include <stdlib.h>
#include <string.h>

static char *dup_string(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = (char *)malloc(len + 1);
    if (!d) return NULL;
    for (size_t i = 0; i <= len; i++) d[i] = s[i];
    return d;
}

neverc_error_t *neverc_errors_new(const char *text) {
    if (!text) return NULL;
    neverc_error_t *e = (neverc_error_t *)malloc(sizeof(neverc_error_t));
    if (!e) return NULL;
    e->msg = dup_string(text);
    if (!e->msg) {
        free(e);
        return NULL;
    }
    e->wrapped = NULL;
    e->owned = 1;
    return e;
}

const char *neverc_errors_message(const neverc_error_t *err) {
    return err ? err->msg : NULL;
}

neverc_error_t *neverc_errors_unwrap(const neverc_error_t *err) {
    return err ? err->wrapped : NULL;
}

int neverc_errors_is(const neverc_error_t *err, const neverc_error_t *target) {
    if (!target) return err == NULL;
    const neverc_error_t *cur = err;
    while (cur) {
        if (cur == target) return 1;
        if (cur->msg && target->msg && strcmp(cur->msg, target->msg) == 0)
            return 1;
        cur = cur->wrapped;
    }
    return 0;
}

neverc_error_t *neverc_errors_wrap(const char *text, neverc_error_t *cause) {
    if (!text) return NULL;
    neverc_error_t *e = (neverc_error_t *)malloc(sizeof(neverc_error_t));
    if (!e) return NULL;

    if (cause && cause->msg) {
        size_t tlen = strlen(text);
        size_t clen = strlen(cause->msg);
        char *combined = (char *)malloc(tlen + 2 + clen + 1);
        if (combined) {
            for (size_t i = 0; i < tlen; i++) combined[i] = text[i];
            combined[tlen] = ':';
            combined[tlen + 1] = ' ';
            for (size_t i = 0; i <= clen; i++) combined[tlen + 2 + i] = cause->msg[i];
            e->msg = combined;
        } else {
            e->msg = dup_string(text);
        }
    } else {
        e->msg = dup_string(text);
    }
    if (!e->msg) {
        free(e);
        return NULL;
    }
    e->wrapped = cause;
    e->owned = 1;
    return e;
}

neverc_error_t *neverc_errors_join(neverc_error_t **errs, size_t count) {
    if (!errs) return NULL;
    size_t valid = 0;
    size_t total_len = 0;
    for (size_t i = 0; i < count; i++) {
        if (errs[i] && errs[i]->msg) {
            if (valid > 0) total_len += 1;
            total_len += strlen(errs[i]->msg);
            valid++;
        }
    }
    if (valid == 0) return NULL;

    char *combined = (char *)malloc(total_len + 1);
    if (!combined) return NULL;
    size_t pos = 0;
    int first = 1;
    for (size_t i = 0; i < count; i++) {
        if (errs[i] && errs[i]->msg) {
            if (!first) combined[pos++] = '\n';
            size_t mlen = strlen(errs[i]->msg);
            for (size_t j = 0; j < mlen; j++) combined[pos++] = errs[i]->msg[j];
            first = 0;
        }
    }
    combined[pos] = '\0';

    neverc_error_t *e = (neverc_error_t *)malloc(sizeof(neverc_error_t));
    if (!e) { free(combined); return NULL; }
    e->msg = combined;
    e->wrapped = NULL;
    e->owned = 1;
    return e;
}

void neverc_errors_free(neverc_error_t *err) {
    if (!err) return;
    if (err->owned && err->msg)
        free((void *)err->msg);
    if (err->wrapped)
        neverc_errors_free(err->wrapped);
    free(err);
}
