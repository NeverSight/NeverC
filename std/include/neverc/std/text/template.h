#ifndef NEVERC_TEXT_TEMPLATE_H
#define NEVERC_TEXT_TEMPLATE_H

/*
 * NeverC text/template -- small string-keyed template engine.
 *
 * Supports literal text, {{.Key}}, {{if .Key}}...{{else}}...{{end}}, and
 * {{range .Key}}...{{end}}. {{- and -}} trim adjacent whitespace like Go.
 * Data keys are copied and owned by the data object; values are borrowed
 * strings. Since the data model has no collection
 * type, range executes its body once when the named value exists; pipelines
 * and template functions are rejected as invalid syntax rather than treated
 * as key names. Selectors require a leading '.' (unlike html/template).
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *key;
    const char *value;
} neverc_template_var_t;

typedef struct {
    neverc_template_var_t *vars;
    int                    nvars;
    int                    cap;
} neverc_template_data_t;

typedef struct neverc_template neverc_template_t;

/* Returns NULL for invalid syntax or allocation failure. errp, when non-NULL,
 * receives a static diagnostic string on failure and NULL on success. */
neverc_template_t *neverc_template_parse(const char *text, const char **errp);
void               neverc_template_free(neverc_template_t *tmpl);

void neverc_template_data_init(neverc_template_data_t *d);
/* Copies key and borrows value. Updating an existing key only replaces the
 * borrowed value. */
void neverc_template_data_set(neverc_template_data_t *d,
                               const char *key, const char *value);
const char *neverc_template_data_get(const neverc_template_data_t *d,
                                     const char *key);
void neverc_template_data_free(neverc_template_data_t *d);

char *neverc_template_execute(neverc_template_t *tmpl,
                               const neverc_template_data_t *data,
                               size_t *outlen);

/* One-shot: parse + execute + free */
char *neverc_template_render(const char *tmpl_text,
                              const neverc_template_data_t *data,
                              size_t *outlen);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/text.h>
#endif


#endif /* NEVERC_TEXT_TEMPLATE_H */
