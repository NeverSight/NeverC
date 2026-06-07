#ifndef NEVERC_TEXT_TEMPLATE_H
#define NEVERC_TEXT_TEMPLATE_H

/*
 * NeverC text/template — simple template engine (mirrors Go text/template).
 *
 * Supports: {{.Key}}, {{if .Key}}...{{end}}, {{range .Key}}...{{end}},
 * {{.Key | func}}, literal text. Template variables are string key-value pairs.
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

neverc_template_t *neverc_template_parse(const char *text, const char **errp);
void               neverc_template_free(neverc_template_t *tmpl);

void neverc_template_data_init(neverc_template_data_t *d);
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
