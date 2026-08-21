#ifndef NEVERC_HTML_H
#define NEVERC_HTML_H

/*
 * NeverC html — HTML escaping/unescaping (mirrors Go html package).
 *
 * Returns malloc'd strings; caller frees.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

char *neverc_html_escape_string(const char *s, size_t *outlen);
char *neverc_html_unescape_string(const char *s, size_t *outlen);

#ifdef __cplusplus
}
#endif

#include "html/template.h"

#ifdef __neverc__
struct __neverc_std_html_template_t { char __tag; };

struct __neverc_std_html_t {
    char __tag;
    struct __neverc_std_html_template_t template_mod;
};
extern struct __neverc_std_html_t __neverc_mod_html;
extern struct __neverc_std_html_t html;
#endif

#endif /* NEVERC_HTML_H */
