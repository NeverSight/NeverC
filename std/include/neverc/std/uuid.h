#ifndef NEVERC_UUID_H
#define NEVERC_UUID_H

/*
 * NeverC uuid — UUID generation (mirrors Go uuid package).
 *
 * Supports UUID v4 (random) and parsing/formatting.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t bytes[16];
} neverc_uuid_t;

/* Generate a new UUID v4 (random) */
neverc_uuid_t neverc_uuid_new(void);

/* Format UUID to string (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx) */
void neverc_uuid_to_string(neverc_uuid_t u, char out[37]);

/* Parse UUID from string */
int neverc_uuid_parse(const char *s, neverc_uuid_t *out);

/* Compare two UUIDs */
int neverc_uuid_equal(neverc_uuid_t a, neverc_uuid_t b);

/* Check if UUID is nil (all zeros) */
int neverc_uuid_is_nil(neverc_uuid_t u);

/* Get version field */
int neverc_uuid_version(neverc_uuid_t u);

/* Get variant field */
int neverc_uuid_variant(neverc_uuid_t u);

/* Nil UUID constant */
neverc_uuid_t neverc_uuid_nil(void);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_uuid_t { char __tag; };
extern struct __neverc_std_uuid_t __neverc_mod_uuid;
#endif

#endif /* NEVERC_UUID_H */
