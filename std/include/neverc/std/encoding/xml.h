#ifndef NEVERC_ENCODING_XML_H
#define NEVERC_ENCODING_XML_H

/*
 * NeverC encoding/xml — XML parsing and generation (mirrors Go encoding/xml).
 *
 * SAX-style event-driven parser + simple DOM tree builder.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Token types */
#define NEVERC_XML_START_ELEMENT 1
#define NEVERC_XML_END_ELEMENT   2
#define NEVERC_XML_CHAR_DATA     3
#define NEVERC_XML_COMMENT       4
#define NEVERC_XML_PROC_INST     5
#define NEVERC_XML_EOF           0
#define NEVERC_XML_ERROR        (-1)

typedef struct {
    char *name;
    char *value;
} neverc_xml_attr_t;

typedef struct {
    int              type;
    char            *name;
    char            *data;
    size_t           data_len;
    neverc_xml_attr_t *attrs;
    int              nattrs;
    int              self_closing;
} neverc_xml_token_t;

/* SAX-style tokenizer */
typedef struct {
    const char *src;
    size_t      len;
    size_t      pos;
} neverc_xml_decoder_t;

void neverc_xml_decoder_init(neverc_xml_decoder_t *d, const char *data, size_t len);
int  neverc_xml_decode_token(neverc_xml_decoder_t *d, neverc_xml_token_t *tok);
void neverc_xml_token_free(neverc_xml_token_t *tok);

/* Simple DOM node */
typedef struct neverc_xml_node {
    char                    *tag;
    char                    *text;
    neverc_xml_attr_t       *attrs;
    int                      nattrs;
    struct neverc_xml_node **children;
    int                      nchildren;
    int                      cap_children;
} neverc_xml_node_t;

neverc_xml_node_t *neverc_xml_parse(const char *data, size_t len);
void               neverc_xml_node_free(neverc_xml_node_t *node);
const char        *neverc_xml_node_attr(const neverc_xml_node_t *node,
                                        const char *name);
neverc_xml_node_t *neverc_xml_node_child(const neverc_xml_node_t *node,
                                          const char *tag);

/* Escape/unescape XML text */
char *neverc_xml_escape(const char *s, size_t *outlen);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/encoding.h>
#endif


#endif /* NEVERC_ENCODING_XML_H */
