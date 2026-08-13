#ifndef NEVERC_ENCODING_JSON_H
#define NEVERC_ENCODING_JSON_H

/*
 * NeverC encoding/json — JSON parsing and generation.
 * Mirrors Go encoding/json concepts adapted for C.
 *
 * Provides a DOM-style API: parse JSON text into a value tree,
 * query values by path, and marshal values back to JSON text.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEVERC_JSON_NULL,
    NEVERC_JSON_BOOL,
    NEVERC_JSON_NUMBER,
    NEVERC_JSON_STRING,
    NEVERC_JSON_ARRAY,
    NEVERC_JSON_OBJECT
} neverc_json_type_t;

typedef struct neverc_json_value neverc_json_value_t;

typedef struct neverc_json_pair {
    char                *key;
    neverc_json_value_t *value;
    /* Byte length of key, allowing escaped U+0000 in object member names. */
    size_t               key_len;
} neverc_json_pair_t;

struct neverc_json_value {
    neverc_json_type_t type;
    union {
        int      bool_val;
        double   num_val;
        char    *str_val;
        struct {
            neverc_json_value_t **items;
            int                   len;
            int                   cap;
        } arr;
        struct {
            neverc_json_pair_t *pairs;
            int                 len;
            int                 cap;
        } obj;
    } u;
    /* Byte length of u.str_val for string values, including embedded NULs. */
    size_t string_len;
    /* Internal ownership link used to reject cycles and shared child nodes. */
    neverc_json_value_t *parent;
};

/*
 * Parse JSON text into a value tree.
 * Returns the root value, or NULL on parse error.
 * Caller must call neverc_json_free() when done.
 */
neverc_json_value_t *neverc_json_parse(const char *text, size_t len);

/*
 * Free a root JSON value tree (recursive). Passing an attached child is a
 * safe no-op because its parent owns it.
 */
void neverc_json_free(neverc_json_value_t *v);

/*
 * Marshal a JSON value tree to text.
 * Returns bytes written, or -1 on error.
 * If `indent` is non-NULL, pretty-prints with that JSON-whitespace-only
 * indentation string.
 */
int neverc_json_marshal(const neverc_json_value_t *v,
                        char *dst, size_t dst_len,
                        const char *indent);

/*
 * Query helpers.
 */
neverc_json_type_t    neverc_json_type(const neverc_json_value_t *v);
int                   neverc_json_bool(const neverc_json_value_t *v);
double                neverc_json_number(const neverc_json_value_t *v);
const char           *neverc_json_string(const neverc_json_value_t *v);
size_t                neverc_json_string_len(const neverc_json_value_t *v);
int                   neverc_json_array_len(const neverc_json_value_t *v);
neverc_json_value_t  *neverc_json_array_get(const neverc_json_value_t *v, int idx);
int                   neverc_json_object_len(const neverc_json_value_t *v);
neverc_json_value_t  *neverc_json_object_get(const neverc_json_value_t *v, const char *key);
neverc_json_value_t  *neverc_json_object_get_n(const neverc_json_value_t *v,
                                               const char *key, size_t key_len);

/*
 * Value constructors.
 * array_append/object_set take ownership of `val` on success. A value can have
 * only one parent; attempts to share a child or create a cycle return -1.
 */
neverc_json_value_t *neverc_json_new_null(void);
neverc_json_value_t *neverc_json_new_bool(int val);
neverc_json_value_t *neverc_json_new_number(double val);
neverc_json_value_t *neverc_json_new_string(const char *s);
neverc_json_value_t *neverc_json_new_string_n(const char *s, size_t len);
neverc_json_value_t *neverc_json_new_array(void);
neverc_json_value_t *neverc_json_new_object(void);
int  neverc_json_array_append(neverc_json_value_t *arr, neverc_json_value_t *val);
int  neverc_json_object_set(neverc_json_value_t *obj, const char *key, neverc_json_value_t *val);
int  neverc_json_object_set_n(neverc_json_value_t *obj,
                              const char *key, size_t key_len,
                              neverc_json_value_t *val);

/*
 * Validity check: returns 1 if text is valid JSON, 0 otherwise.
 */
int neverc_json_valid(const char *text, size_t len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/encoding.h>
#endif


#endif
