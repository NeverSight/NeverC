#ifndef __NEVERC_MAX_ALIGN_T_DEFINED
#define __NEVERC_MAX_ALIGN_T_DEFINED

#if defined(_MSC_VER)
typedef double max_align_t;
#elif defined(__APPLE__)
typedef long double max_align_t;
#else
// Define 'max_align_t' to match the GCC definition.
typedef struct {
  long long __neverc_max_align_nonce1
      __attribute__((__aligned__(__alignof__(long long))));
  long double __neverc_max_align_nonce2
      __attribute__((__aligned__(__alignof__(long double))));
} max_align_t;
#endif

#endif
