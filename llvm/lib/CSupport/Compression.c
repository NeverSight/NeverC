/*===- Compression.c - Data compression (pure C) ----------------*- C -*-===*/
#include "include/csupport/lcompression.h"
#include "llvm/Config/config.h"

#if LLVM_ENABLE_ZLIB
#include <zlib.h>

const char *csupport_zlib_error_string(int code) {
  switch (code) {
  case Z_MEM_ERROR:   return "zlib error: Z_MEM_ERROR";
  case Z_BUF_ERROR:   return "zlib error: Z_BUF_ERROR";
  case Z_STREAM_ERROR: return "zlib error: Z_STREAM_ERROR";
  case Z_DATA_ERROR:  return "zlib error: Z_DATA_ERROR";
  default:            return "zlib error: unknown";
  }
}

#else

const char *csupport_zlib_error_string(int code) {
  (void)code;
  return "zlib not available";
}

#endif
