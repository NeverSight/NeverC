#ifndef NEVERC_MAPS_H
#define NEVERC_MAPS_H

/*
 * NeverC maps — string-keyed hash map.
 * C adaptation of Go maps package.
 * Uses open-addressing hash table with string keys and void* values.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_map neverc_map_t;

typedef void (*neverc_maps_iter_func_t)(const char *key, void *value, void *user_data);
typedef int (*neverc_maps_filter_func_t)(const char *key, void *value);

neverc_map_t *neverc_maps_new(void);
void          neverc_maps_free(neverc_map_t *m);
void          neverc_maps_clear(neverc_map_t *m);

int    neverc_maps_set(neverc_map_t *m, const char *key, void *value);
void  *neverc_maps_get(const neverc_map_t *m, const char *key);
int    neverc_maps_has(const neverc_map_t *m, const char *key);
int    neverc_maps_delete(neverc_map_t *m, const char *key);

size_t neverc_maps_len(const neverc_map_t *m);

char **neverc_maps_keys(const neverc_map_t *m, size_t *count);
void **neverc_maps_values(const neverc_map_t *m, size_t *count);

void   neverc_maps_foreach(const neverc_map_t *m, neverc_maps_iter_func_t fn, void *user_data);
void   neverc_maps_delete_func(neverc_map_t *m, neverc_maps_filter_func_t fn);

neverc_map_t *neverc_maps_clone(const neverc_map_t *m);
int           neverc_maps_equal(const neverc_map_t *a, const neverc_map_t *b);

/* Copies src into dst. Returns 0 on success, -1 on NULL args or if an
 * insertion fails (dst may then contain a prefix of src). */
int           neverc_maps_copy(neverc_map_t *dst, const neverc_map_t *src);

/* Backward-compat aliases */
#define neverc_map_new         neverc_maps_new
#define neverc_map_free        neverc_maps_free
#define neverc_map_clear       neverc_maps_clear
#define neverc_map_set         neverc_maps_set
#define neverc_map_get         neverc_maps_get
#define neverc_map_has         neverc_maps_has
#define neverc_map_delete      neverc_maps_delete
#define neverc_map_len         neverc_maps_len
#define neverc_map_keys        neverc_maps_keys
#define neverc_map_values      neverc_maps_values
#define neverc_map_foreach     neverc_maps_foreach
#define neverc_map_delete_func neverc_maps_delete_func
#define neverc_map_clone       neverc_maps_clone
#define neverc_map_equal       neverc_maps_equal
#define neverc_map_copy        neverc_maps_copy

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_maps_t { char __tag; };
extern struct __neverc_std_maps_t __neverc_mod_maps;
extern struct __neverc_std_maps_t maps;
#endif

#endif
