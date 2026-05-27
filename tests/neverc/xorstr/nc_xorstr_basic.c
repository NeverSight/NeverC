// RUN: %nevercc -fsyntax-only %s -include neverc/xorstr.h 2>&1 | FileCheck %s --check-prefix=CHECK-OK
// RUN: %nevercc -fsyntax-only %s -include neverc/xorstr.h -DTEST_BAD_ARG 2>&1 | FileCheck %s --check-prefix=CHECK-ERR

// CHECK-OK-NOT: error

// Basic usage: NC_XORSTR with string literal should compile cleanly.
#ifndef TEST_BAD_ARG

const char *get_encrypted(void) {
  return NC_XORSTR("hello world");
}

const char *get_api_name(void) {
  return NC_XORSTR("NtQuerySystemInformation");
}

const char *get_empty(void) {
  return NC_XORSTR("");
}

#else

// Non-string-literal argument should produce an error.
// CHECK-ERR: error: expression is not a string literal
const char *bad_usage(const char *s) {
  return NC_XORSTR(s);
}

#endif
