#ifndef NEVERC_NET_BUFFER_H
#define NEVERC_NET_BUFFER_H

#include "_net_platform.h"

/* Growable byte buffer shared by HTTP and connection state. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} nc_buf_t;

#ifndef NC_NET_REALLOC
#define NC_NET_REALLOC realloc
#endif

static inline void nc_buf_init(nc_buf_t *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static inline void nc_buf_free(nc_buf_t *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static inline int nc_buf_grow(nc_buf_t *b, size_t need) {
    if (b->cap >= need) return 0;
    size_t nc = b->cap < 256 ? 256 : b->cap;
    while (nc < need) {
        size_t next = nc * 2;
        if (next <= nc) { nc = need; break; }
        nc = next;
    }
    char *nd = (char *)NC_NET_REALLOC(b->data, nc);
    if (!nd) return -1;
    b->data = nd;
    b->cap = nc;
    return 0;
}

static inline int nc_buf_append(nc_buf_t *b, const void *data, size_t len) {
    if (!b || (len > 0 && !data) || b->len == SIZE_MAX ||
        len > SIZE_MAX - b->len - 1)
        return -1;
    if (nc_buf_grow(b, b->len + len + 1) != 0)
        return -1;
    if (len > 0)
        memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
    return 0;
}

static inline void nc_buf_reset(nc_buf_t *b) {
    b->len = 0;
    if (b->data) b->data[0] = '\0';
}

static inline void nc_buf_consume(nc_buf_t *b, size_t n) {
    if (n >= b->len) {
        nc_buf_reset(b);
        return;
    }
    size_t remaining = b->len - n;
    memmove(b->data, b->data + n, remaining);
    b->len = remaining;
    b->data[b->len] = '\0';
}

/* Fixed-size buffer cache for reducing allocator pressure. */
#define NC_BUFPOOL_MAX_CACHED 512

typedef struct nc_bufpool_node {
    struct nc_bufpool_node *next;
} nc_bufpool_node_t;

typedef struct {
    nc_bufpool_node_t *volatile head;
    volatile int count;
    size_t buf_size;
    nc_mutex_t lock;
} nc_bufpool_t;

static inline void nc_bufpool_init(nc_bufpool_t *pool, size_t buf_size) {
    pool->head = NULL;
    pool->count = 0;
    pool->buf_size = buf_size < sizeof(nc_bufpool_node_t)
                   ? sizeof(nc_bufpool_node_t) : buf_size;
    nc_mutex_init(&pool->lock);
}

static inline void *nc_bufpool_pop(nc_bufpool_t *pool) {
    nc_mutex_lock(&pool->lock);
    nc_bufpool_node_t *node = pool->head;
    if (node) {
        pool->head = node->next;
        pool->count--;
        nc_mutex_unlock(&pool->lock);
        memset(node, 0, pool->buf_size);
        return node;
    }
    nc_mutex_unlock(&pool->lock);
    return calloc(1, pool->buf_size);
}

static inline void nc_bufpool_push(nc_bufpool_t *pool, void *buf) {
    if (!buf) return;
    nc_mutex_lock(&pool->lock);
    if (pool->count >= NC_BUFPOOL_MAX_CACHED) {
        nc_mutex_unlock(&pool->lock);
        free(buf);
        return;
    }
    nc_bufpool_node_t *node = (nc_bufpool_node_t *)buf;
    node->next = pool->head;
    pool->head = node;
    pool->count++;
    nc_mutex_unlock(&pool->lock);
}

static inline void nc_bufpool_destroy(nc_bufpool_t *pool) {
    nc_mutex_lock(&pool->lock);
    nc_bufpool_node_t *n = pool->head;
    while (n) {
        nc_bufpool_node_t *next = n->next;
        free(n);
        n = next;
    }
    pool->head = NULL;
    pool->count = 0;
    nc_mutex_unlock(&pool->lock);
    nc_mutex_destroy(&pool->lock);
}

#endif /* NEVERC_NET_BUFFER_H */
