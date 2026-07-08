// RUN: %nevercc -fsyntax-only %s -include neverc/strhash/strhash.h 2>&1 | FileCheck %s --check-prefix=CHECK-OK

// CHECK-OK-NOT: error

// Test NC_STRHASH with various string literal kinds (u8, L, u, U).
// Wide/UTF-16/UTF-32 literals are folded to UTF-8 before hashing, so
// the same text content produces the same hash regardless of prefix.

#include <stdint.h>

// u8"..." — UTF-8 explicit prefix
static const uint64_t kU8 = NC_STRHASH(u8"hello");

// L"..." — wide string (folded to UTF-8)
static const uint64_t kWide = NC_STRHASH(L"hello");

// u"..." — UTF-16 (folded to UTF-8)
static const uint64_t kU16 = NC_STRHASH(u"hello");

// U"..." — UTF-32 (folded to UTF-8)
static const uint64_t kU32 = NC_STRHASH(U"hello");

// All encodings of the same content should produce the same hash
// (they all fold to the same UTF-8 bytes before hashing).
_Static_assert(NC_STRHASH("hello") == NC_STRHASH(u8"hello"),
               "ordinary and u8 must match");
_Static_assert(NC_STRHASH("hello") == NC_STRHASH(L"hello"),
               "ordinary and wide must match");
_Static_assert(NC_STRHASH("hello") == NC_STRHASH(u"hello"),
               "ordinary and u16 must match");
_Static_assert(NC_STRHASH("hello") == NC_STRHASH(U"hello"),
               "ordinary and u32 must match");

// Non-ASCII: CJK characters produce consistent hashes across encodings.
_Static_assert(NC_STRHASH("你好") == NC_STRHASH(u8"你好"),
               "CJK ordinary vs u8 must match");
_Static_assert(NC_STRHASH("你好") == NC_STRHASH(L"你好"),
               "CJK ordinary vs wide must match");
_Static_assert(NC_STRHASH("你好") == NC_STRHASH(U"你好"),
               "CJK ordinary vs u32 must match");

// Different content must produce different hashes.
_Static_assert(NC_STRHASH(L"alpha") != NC_STRHASH(L"beta"),
               "different wide strings must differ");

// Usable in function context.
uint64_t hash_wide_api(void) {
  return NC_STRHASH(L"NtQuerySystemInformation");
}

uint64_t hash_u8_api(void) {
  return NC_STRHASH(u8"CreateFileW");
}
