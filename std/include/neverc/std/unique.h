#ifndef NEVERC_UNIQUE_H
#define NEVERC_UNIQUE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NeverC unique — value canonicalization (interning).
 * C adaptation of Go's unique package.
 *
 * Provides a global intern table so that equal values share a single
 * canonical copy.  Handles are trivially comparable (pointer equality)
 * and carry no ownership—freeing happens when the table is destroyed.
 *
 * Thread-safe: table operations use internal locking and interned values are
 * immutable.  Destroy invalidates every handle and must not run concurrently
 * with handle/value access.  An empty byte slice may be interned with a NULL
 * pointer and length 0; a non-zero length still requires a non-NULL buffer.
 */

typedef struct { const void *ptr; } neverc_unique_handle_t;

void  neverc_unique_init(void);
void  neverc_unique_destroy(void);

neverc_unique_handle_t neverc_unique_make_string(const char *s);
neverc_unique_handle_t neverc_unique_make_int64(int64_t v);
neverc_unique_handle_t neverc_unique_make_uint64(uint64_t v);
neverc_unique_handle_t neverc_unique_make_bytes(const void *data, size_t len);

/* string_value requires a NUL-terminated intern (make_string). Returns
 * NULL if the handle is invalid or the interned bytes are not a C string.
 * int64/uint64_value require an intern of exactly 8 bytes; otherwise 0.
 * Handles do not store kind: an 8-byte blob can still be read as an
 * integer, but shorter interns are never over-read. */
const char    *neverc_unique_string_value(neverc_unique_handle_t h);
int64_t        neverc_unique_int64_value(neverc_unique_handle_t h);
uint64_t       neverc_unique_uint64_value(neverc_unique_handle_t h);
const void    *neverc_unique_bytes_value(neverc_unique_handle_t h, size_t *len);

int  neverc_unique_handle_equal(neverc_unique_handle_t a, neverc_unique_handle_t b);
int  neverc_unique_handle_valid(neverc_unique_handle_t h);
size_t neverc_unique_count(void);

/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
struct __neverc_std_unique_t { char __tag; };
extern struct __neverc_std_unique_t __neverc_mod_unique;
extern struct __neverc_std_unique_t unique;
#endif

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_UNIQUE_H */
