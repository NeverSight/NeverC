#ifndef NEVERC_TEXT_H
#define NEVERC_TEXT_H

/*
 * NeverC text — umbrella header for text processing submodules.
 */

#include "text/template.h"
#include "text/scanner.h"
#include "text/tabwriter.h"

#ifdef __neverc__
/* Marker name must match neverc_template_* so text.template_mod.parse()
 * resolves to neverc_template_parse, not neverc_text_template_parse. */
struct __neverc_std_template_t { char __tag; };
struct __neverc_std_scanner_t { char __tag; };
struct __neverc_std_tabwriter_t { char __tag; };

struct __neverc_std_text_t {
    struct __neverc_std_template_t template_mod;
    struct __neverc_std_scanner_t scanner;
    struct __neverc_std_tabwriter_t tabwriter;
};
extern struct __neverc_std_text_t __neverc_mod_text;
extern struct __neverc_std_text_t text;
#endif

#endif /* NEVERC_TEXT_H */
