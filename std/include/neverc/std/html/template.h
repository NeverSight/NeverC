#ifndef NEVERC_HTML_TEMPLATE_H
#define NEVERC_HTML_TEMPLATE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_html_template neverc_html_template_t;

typedef struct {
    const char **keys;
    const char **values;
    size_t count;
    size_t capacity;
} neverc_html_template_data_t;

void neverc_html_template_data_init(neverc_html_template_data_t *d);
void neverc_html_template_data_set(neverc_html_template_data_t *d,
                                    const char *key, const char *value);
const char *neverc_html_template_data_get(const neverc_html_template_data_t *d,
                                           const char *key);
void neverc_html_template_data_free(neverc_html_template_data_t *d);

neverc_html_template_t *neverc_html_template_parse(const char *src);
void neverc_html_template_free(neverc_html_template_t *t);

char *neverc_html_template_execute(const neverc_html_template_t *t,
                                    const neverc_html_template_data_t *data);

char *neverc_html_template_render(const char *src,
                                   const neverc_html_template_data_t *data);

char *neverc_html_escape(const char *s);
char *neverc_html_attr_escape(const char *s);
char *neverc_html_js_escape(const char *s);
char *neverc_html_css_escape(const char *s);
char *neverc_html_url_query_escape(const char *s);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/html.h>
#endif


#endif /* NEVERC_HTML_TEMPLATE_H */
