#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* White-box coverage is intentional: public deletion may replace a control
 * byte with EMPTY when that is probe-safe, so it cannot deterministically
 * construct the exact tombstone occupancy boundary under every hash/group
 * implementation. */
#include "../../../std/src/maps/maps.c"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    size_t planned_cap = 0;
    CHECK(!map_ideal_cap_for_insert(SIZE_MAX, &planned_cap));
    CHECK(map_ideal_cap_for_insert(14, &planned_cap));
    CHECK(planned_cap == 32);

    neverc_map_t *m = neverc_maps_new();
    CHECK(m != NULL);
    CHECK(map_resize(m, 32) == 0);

    int values[29] = {0};
    char key[32];
    for (int i = 0; i < 28; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        values[i] = i + 1;
        CHECK(neverc_maps_set(m, key, &values[i]) == 0);
    }
    CHECK(m->cap == 32);
    CHECK(m->len == 28);
    CHECK(m->tombstones == 0);

    int deleted[28] = {0};
    size_t deleted_count = 0;
    for (size_t i = 0; i < m->cap && deleted_count < 14; i++) {
        if (!NCI_IS_FULL(m->ctrl[i])) continue;

        int key_index = -1;
        CHECK(sscanf(m->slots[i].key, "key_%d", &key_index) == 1);
        CHECK(key_index >= 0 && key_index < 28);
        CHECK(!deleted[key_index]);
        deleted[key_index] = 1;

        free(m->slots[i].key);
        memset(&m->slots[i], 0, sizeof(m->slots[i]));
        nci_set_ctrl(m->ctrl, m->cap, i, NCI_DELETED);
        m->len--;
        m->tombstones++;
        deleted_count++;
    }

    CHECK(deleted_count == 14);
    CHECK(m->cap == 32);
    CHECK(m->len == 14);
    CHECK(m->tombstones == 14);
    CHECK(m->len + m->tombstones == map_max_load(m->cap));

    for (int i = 0; i < 28; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        CHECK(neverc_maps_has(m, key) == !deleted[i]);
    }

    values[28] = 29;
    CHECK(neverc_maps_set(m, "pending", &values[28]) == 0);
    CHECK(m->cap == 32);
    CHECK(m->len == 15);
    CHECK(m->tombstones == 0);
    CHECK(m->len <= map_max_load(m->cap));
    CHECK(neverc_maps_get(m, "pending") == &values[28]);

    for (int i = 0; i < 28; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        CHECK(neverc_maps_has(m, key) == !deleted[i]);
        if (!deleted[i])
            CHECK(neverc_maps_get(m, key) == &values[i]);
    }

    neverc_maps_free(m);
    puts("passed");
    return 0;
}
