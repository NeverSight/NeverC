R"prelude(
#ifndef NEVERC_LIKELY
#define NEVERC_LIKELY(x) __builtin_expect(!!(x), 1)
#endif
#ifndef NEVERC_UNLIKELY
#define NEVERC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

typedef struct __neverc_string {
  const char *data;
  __SIZE_TYPE__ len;
  __SIZE_TYPE__ cap;
} string;

#ifndef NEVERC_STRING_NPOS
#define NEVERC_STRING_NPOS ((__SIZE_TYPE__)-1)
#endif

#ifndef NEVERC_STRING_MAX_LEN
#define NEVERC_STRING_MAX_LEN ((__SIZE_TYPE__)-2)
#endif

static __inline__ string __neverc_string_make_view(const char *data,
                                                   __SIZE_TYPE__ len) {
  return (string){data, len, 0};
}

static __inline__ string __neverc_string_empty(void) {
  return __neverc_string_make_view((const char *)0, 0);
}

static __inline__ int __neverc_string_invalid(string s) {
  return NEVERC_UNLIKELY((s.len != 0 && !s.data) || s.len > NEVERC_STRING_MAX_LEN);
}

#ifndef NEVERC_STRING_ALLOC
#define NEVERC_STRING_ALLOC __builtin_malloc
#else
extern void *NEVERC_STRING_ALLOC(__SIZE_TYPE__);
#endif

#ifndef NEVERC_STRING_DECRYPT_BYTE
#define NEVERC_STRING_DECRYPT_BYTE(byte, key, idx) \
  ((char)((unsigned char)(byte) ^ \
          (unsigned char)((key) >> (8 * ((idx) % sizeof(__SIZE_TYPE__))))))
#endif

static __attribute__((always_inline)) string
__neverc_string_decrypt_literal(const char *enc, __SIZE_TYPE__ len,
                                __SIZE_TYPE__ key) {
  if (len == 0)
    return __neverc_string_empty();
  char *p = (char *)NEVERC_STRING_ALLOC(len + 1);
  if (!p)
    return __neverc_string_empty();
  volatile __SIZE_TYPE__ vkey = key;
  for (__SIZE_TYPE__ i = 0; i < len; i++)
    p[i] = NEVERC_STRING_DECRYPT_BYTE(enc[i], vkey, i);
  p[len] = 0;
  return (string){p, len, len + 1};
}

)prelude"
