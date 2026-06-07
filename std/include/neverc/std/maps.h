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

typedef void (*neverc_map_iter_func_t)(const char *key, void *value, void *user_data);
typedef int (*neverc_map_filter_func_t)(const char *key, void *value);

neverc_map_t *neverc_map_new(void);
void          neverc_map_free(neverc_map_t *m);
void          neverc_map_clear(neverc_map_t *m);

int    neverc_map_set(neverc_map_t *m, const char *key, void *value);
void  *neverc_map_get(const neverc_map_t *m, const char *key);
int    neverc_map_has(const neverc_map_t *m, const char *key);
int    neverc_map_delete(neverc_map_t *m, const char *key);

size_t neverc_map_len(const neverc_map_t *m);

char **neverc_map_keys(const neverc_map_t *m, size_t *count);
void **neverc_map_values(const neverc_map_t *m, size_t *count);

void   neverc_map_foreach(const neverc_map_t *m, neverc_map_iter_func_t fn, void *user_data);
void   neverc_map_delete_func(neverc_map_t *m, neverc_map_filter_func_t fn);

neverc_map_t *neverc_map_clone(const neverc_map_t *m);
int           neverc_map_equal(const neverc_map_t *a, const neverc_map_t *b);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_maps_t { char __tag; };
extern struct __neverc_std_maps_t __neverc_mod_maps;
extern struct __neverc_std_maps_t maps;
#endif

#endif
