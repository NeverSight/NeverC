// RUN: %nevercc -fsyntax-only %s -include neverc/strhash/strhash.h 2>&1 | FileCheck %s --check-prefix=CHECK-OK

// CHECK-OK-NOT: error

// Demonstrates the "array of compile-time hashes + runtime variable comparison"
// pattern — the primary use case equivalent to:
//   constexpr uint32 Hash_XXX = "XXX"h;
//   if (fnv1a_hash666(Name, strlen(Name)) == ItemHash) ...

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// --- Pattern 1: Pre-computed hash table (compile-time constants) ---
static const uint64_t valuable_items[] = {
    NC_STRHASH("苹果"),
    NC_STRHASH("香蕉"),
    NC_STRHASH("葡萄"),
    NC_STRHASH("咖啡杯"),
    NC_STRHASH("笔记本"),
};

#define VALUABLE_COUNT (sizeof(valuable_items) / sizeof(valuable_items[0]))

// Runtime function: hash a variable string and compare against the table.
int is_valuable_item(const char *name) {
    uint64_t h = neverc_strhash_rt(name, strlen(name));
    for (int i = 0; i < VALUABLE_COUNT; i++) {
        if (h == valuable_items[i])
            return 1;
    }
    return 0;
}

// --- Pattern 2: NC_STRHASH_AUTO with variable ---
// This should compile cleanly and generate a runtime call.
int match_item(const char *name) {
    return NC_STRHASH_AUTO(name) == NC_STRHASH("苹果");
}

// --- Pattern 3: NC_STRHASH_AUTO with literal ---
// With -fstrhash-fold, the IR pass folds this to a compile-time constant.
uint64_t auto_literal(void) {
    return NC_STRHASH_AUTO("hello");
}

// --- Pattern 4: switch-case style matching ---
int classify_item(const char *name) {
    uint64_t h = NC_STRHASH_AUTO(name);

    if (h == NC_STRHASH("苹果"))
        return 1;
    if (h == NC_STRHASH("香蕉"))
        return 2;
    if (h == NC_STRHASH("葡萄"))
        return 3;
    return 0;
}

// --- Pattern 5: Array of strings iterated with NC_STRHASH_AUTO ---
int find_in_array(const char *items[], int count, const char *target) {
    uint64_t target_hash = NC_STRHASH_AUTO(target);
    for (int i = 0; i < count; i++) {
        if (NC_STRHASH_AUTO(items[i]) == target_hash)
            return i;
    }
    return -1;
}
