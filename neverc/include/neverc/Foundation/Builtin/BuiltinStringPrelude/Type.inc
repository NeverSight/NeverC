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

#ifndef NEVERC_STRING_FREE
#define NEVERC_STRING_FREE __builtin_free
#else
extern void NEVERC_STRING_FREE(void *);
#endif

static __inline__ const char *neverc_string_cstr(string s) {
  return s.data ? s.data : "";
}

static __inline__ void *neverc_string_data(string s) {
  return s.data ? (void *)s.data : (void *)"";
}

static __inline__ unsigned char __neverc_ascii_lower(unsigned char c) {
  return c | (((unsigned char)(c - 'A') < 26u) << 5);
}

)prelude"
