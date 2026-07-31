#ifndef NEVERC_NET_TIMER_H
#define NEVERC_NET_TIMER_H

#include "_net_platform.h"

/*
 * O(1) timer wheel for connection deadlines.
 * Granularity is one millisecond with 512 hashed slots.
 */
#define NC_TW_SLOTS 512

typedef struct nc_timer nc_timer_t;
typedef void (*nc_timer_cb_t)(nc_timer_t *timer, void *data);

struct nc_timer {
    nc_timer_cb_t cb;
    void *data;
    uint64_t expire_ms;
    int active;
    nc_timer_t *tw_next;
    nc_timer_t *tw_prev;
};

typedef struct {
    nc_timer_t *slots[NC_TW_SLOTS];
    uint64_t last_ms;
} nc_timer_wheel_t;

static inline void nc_tw_init(nc_timer_wheel_t *tw) {
    memset(tw, 0, sizeof(*tw));
    tw->last_ms = nc_monotonic_ms();
}

static inline void nc_timer_init(nc_timer_t *t, nc_timer_cb_t cb, void *data) {
    memset(t, 0, sizeof(*t));
    t->cb = cb;
    t->data = data;
}

static inline void nc_tw_unlink(nc_timer_wheel_t *tw, nc_timer_t *t) {
    int slot = (int)(t->expire_ms % NC_TW_SLOTS);
    nc_timer_t *p = t->tw_prev;
    nc_timer_t *n = t->tw_next;
    if (p) p->tw_next = n;
    else   tw->slots[slot] = n;
    if (n) n->tw_prev = p;
    t->tw_prev = NULL;
    t->tw_next = NULL;
}

static inline void nc_tw_add(nc_timer_wheel_t *tw, nc_timer_t *t,
                             uint32_t delay_ms) {
    if (t->active)
        nc_tw_unlink(tw, t);

    t->expire_ms = nc_monotonic_ms() + delay_ms;
    t->active = 1;
    int slot = (int)(t->expire_ms % NC_TW_SLOTS);
    t->tw_prev = NULL;
    t->tw_next = tw->slots[slot];
    if (tw->slots[slot]) tw->slots[slot]->tw_prev = t;
    tw->slots[slot] = t;
}

static inline void nc_tw_cancel(nc_timer_wheel_t *tw, nc_timer_t *t) {
    if (!t->active) return;
    nc_tw_unlink(tw, t);
    t->active = 0;
}

static inline void nc_tw_tick(nc_timer_wheel_t *tw) {
    uint64_t now = nc_monotonic_ms();
    if (now <= tw->last_ms) return;

    uint64_t elapsed = now - tw->last_ms;
    uint64_t steps =
        elapsed >= NC_TW_SLOTS ? NC_TW_SLOTS : elapsed;
    uint64_t first = now - steps + 1;
    /*
     * Publish progress before callbacks so a callback cannot recursively
     * rescan the same interval. A long scheduler pause scans at most one
     * complete wheel rotation.
     */
    tw->last_ms = now;
    for (uint64_t i = 0; i < steps; i++) {
        uint64_t ms = first + i;
        int slot = (int)(ms % NC_TW_SLOTS);
        nc_timer_t *t = tw->slots[slot];
        while (t) {
            nc_timer_t *next = t->tw_next;
            if (t->active && t->expire_ms <= now) {
                nc_tw_unlink(tw, t);
                t->active = 0;
                if (t->cb) t->cb(t, t->data);
            }
            t = next;
        }
    }
}

#endif /* NEVERC_NET_TIMER_H */
