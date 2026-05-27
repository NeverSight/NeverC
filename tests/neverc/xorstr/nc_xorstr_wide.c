// RUN: %nevercc -fsyntax-only %s -include neverc/xorstr.h 2>&1 | FileCheck %s --check-prefix=CHECK-OK

// CHECK-OK-NOT: error

// Test NC_XORSTR with various string literal kinds.

// UTF-8 explicit prefix
const char *test_u8(void) {
  return NC_XORSTR(u8"hello UTF-8 世界");
}

// Wide string literal (L"...")
// This gets folded to UTF-8 by foldNeverCStringWideLiteralToUtf8
const char *test_wide(void) {
  return NC_XORSTR(L"NtQuerySystemInformation");
}

// UTF-16 string literal (u"...")
const char *test_u16(void) {
  return NC_XORSTR(u"GetProcAddress");
}

// UTF-32 string literal (U"...")
const char *test_u32(void) {
  return NC_XORSTR(U"LoadLibraryA");
}

// Plain ordinary string
const char *test_ordinary(void) {
  return NC_XORSTR("ordinary ASCII string");
}
