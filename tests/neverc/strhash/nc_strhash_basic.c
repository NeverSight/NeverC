// RUN: %nevercc -fsyntax-only %s -include neverc/strhash/strhash.h 2>&1 | FileCheck %s --check-prefix=CHECK-OK
// RUN: %nevercc -fsyntax-only %s -include neverc/strhash/strhash.h -DTEST_BAD_ARG 2>&1 | FileCheck %s --check-prefix=CHECK-ERR

// CHECK-OK-NOT: error

// Basic usage: NC_STRHASH with string literal should compile cleanly and
// produce a compile-time integer constant.
#ifndef TEST_BAD_ARG

#include <stdint.h>

// Should be usable in file-scope constant initializer.
static const uint64_t kHelloHash = NC_STRHASH("hello");
static const uint64_t kEmptyHash = NC_STRHASH("");
static const uint64_t kUTF8Hash = NC_STRHASH("你好世界");

// Verify that different strings produce different hashes.
_Static_assert(NC_STRHASH("hello") != NC_STRHASH("world"),
               "different strings must hash differently");

// Same string must produce same hash.
_Static_assert(NC_STRHASH("test") == NC_STRHASH("test"),
               "same string must hash identically");

uint64_t get_hash(void) {
  return NC_STRHASH("NtQuerySystemInformation");
}

#else

// Non-string-literal argument should produce an error.
// CHECK-ERR: error: expression is not a string literal
uint64_t bad_usage(const char *s) {
  return NC_STRHASH(s);
}

#endif
