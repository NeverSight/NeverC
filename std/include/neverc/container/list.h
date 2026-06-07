#ifndef NEVERC_CONTAINER_LIST_H
#define NEVERC_CONTAINER_LIST_H

/*
 * NeverC container/list — doubly linked list
 * (mirrors Go container/list package).
 *
 * Each element stores a void* value. Nodes are heap-allocated internally.
 * The list uses a sentinel root node for O(1) front/back access.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_list_element neverc_list_element_t;
typedef struct neverc_list        neverc_list_t;

struct neverc_list_element {
    neverc_list_element_t *next;
    neverc_list_element_t *prev;
    neverc_list_t         *list;
    void                  *value;
};

struct neverc_list {
    neverc_list_element_t root;
    int                   len;
};

neverc_list_t *neverc_list_new(void);
void           neverc_list_init(neverc_list_t *l);
void           neverc_list_free(neverc_list_t *l);

int neverc_list_len(const neverc_list_t *l);
neverc_list_element_t *neverc_list_front(const neverc_list_t *l);
neverc_list_element_t *neverc_list_back(const neverc_list_t *l);

neverc_list_element_t *neverc_list_push_front(neverc_list_t *l, void *value);
neverc_list_element_t *neverc_list_push_back(neverc_list_t *l, void *value);
void *neverc_list_remove(neverc_list_t *l, neverc_list_element_t *e);

neverc_list_element_t *neverc_list_insert_before(neverc_list_t *l, void *value,
                                                  neverc_list_element_t *mark);
neverc_list_element_t *neverc_list_insert_after(neverc_list_t *l, void *value,
                                                 neverc_list_element_t *mark);

void neverc_list_move_to_front(neverc_list_t *l, neverc_list_element_t *e);
void neverc_list_move_to_back(neverc_list_t *l, neverc_list_element_t *e);
void neverc_list_move_before(neverc_list_t *l, neverc_list_element_t *e,
                              neverc_list_element_t *mark);
void neverc_list_move_after(neverc_list_t *l, neverc_list_element_t *e,
                             neverc_list_element_t *mark);

neverc_list_element_t *neverc_list_element_next(const neverc_list_element_t *e);
neverc_list_element_t *neverc_list_element_prev(const neverc_list_element_t *e);

#ifdef __cplusplus
}
#endif

#endif
