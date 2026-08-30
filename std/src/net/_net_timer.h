#ifndef NEVERC_NET_TIMER_H
#define NEVERC_NET_TIMER_H

#include "_net_platform.h"

/*
 * O(1) timer wheel for connection deadlines.
 * Granularity is one millisecond with 512 hashed slots.
 */
#define NC_TW_SLOTS 512

typedef struct nc_timer nc_timer_t;
/*
 * Callbacks run synchronously on the wheel's owning thread and must return
 * normally. An active timer must remain alive and be changed through its
 * owning wheel. Callbacks may call nc_tw_add(), nc_tw_cancel(), or nested
 * nc_tw_tick(), but must not access the wheel concurrently, reinitialize or
 * destroy it, inspect or retain its slot/link pointers, modify those pointers
 * directly, or escape via longjmp or exception unwinding.
 */
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

    uint64_t now = nc_monotonic_ms();
    uint64_t delay = (uint64_t)delay_ms;
    uint64_t expire_ms =
        delay > UINT64_MAX - now ? UINT64_MAX : now + delay;
    if (expire_ms <= tw->last_ms)
        expire_ms =
            tw->last_ms == UINT64_MAX ? UINT64_MAX : tw->last_ms + 1;
    t->expire_ms = expire_ms;
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
            if (!t->active || t->expire_ms > now) {
                t = t->tw_next;
                continue;
            }

            nc_timer_cb_t cb = t->cb;
            void *data = t->data;
            nc_timer_t bookmark = {0};
            /*
             * Replace the firing timer with an inactive bookmark. Callback
             * mutations then update its continuation through the ordinary
             * doubly-linked-list operations.
             */
            bookmark.expire_ms = t->expire_ms;
            bookmark.tw_prev = t->tw_prev;
            bookmark.tw_next = t->tw_next;
            if (bookmark.tw_prev)
                bookmark.tw_prev->tw_next = &bookmark;
            else
                tw->slots[slot] = &bookmark;
            if (bookmark.tw_next)
                bookmark.tw_next->tw_prev = &bookmark;

            t->tw_prev = NULL;
            t->tw_next = NULL;
            t->active = 0;
            if (cb)
                cb(t, data);

            nc_timer_t *next = bookmark.tw_next;
            if (bookmark.tw_prev)
                bookmark.tw_prev->tw_next = bookmark.tw_next;
            else
                tw->slots[slot] = bookmark.tw_next;
            if (bookmark.tw_next)
                bookmark.tw_next->tw_prev = bookmark.tw_prev;
            t = next;
        }
    }
}

#endif /* NEVERC_NET_TIMER_H */
