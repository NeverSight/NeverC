// RUN: %nevercc -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=CHECK-OK
// RUN: %nevercc -S -emit-llvm -O1 -fstrhash-fold %s -o - | FileCheck %s --check-prefix=CHECK-IR

// CHECK-OK-NOT: error

// Test custom hash function override via NC_STRHASH_HASH_FN.
// When defined before including strhash.h, neverc_strhash_rt() dispatches
// to the user-provided function instead of the built-in algorithm.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

static uint64_t djb2_hash(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint64_t hash = 5381;
  for (size_t i = 0; i < len; i++)
    hash = ((hash << 5) + hash) + p[i];
  return hash;
}

#define NC_STRHASH_HASH_FN(data, len) djb2_hash(data, len)
#include <neverc/strhash/strhash.h>

// NC_STRHASH_AUTO with custom hash should call djb2_hash at runtime.
// CHECK-IR: define {{.*}}@test_custom_rt
// CHECK-IR-NOT: call {{.*}}neverc_fnv
int test_custom_rt(const char *name) {
  return neverc_strhash_rt(name, strlen(name)) == NC_STRHASH("target");
}

// NC_STRHASH still works as a compile-time constant (uses builtin algo).
static const uint64_t kHash = NC_STRHASH("constant");

_Static_assert(NC_STRHASH("a") != NC_STRHASH("b"),
               "builtin still works with custom hash override");
