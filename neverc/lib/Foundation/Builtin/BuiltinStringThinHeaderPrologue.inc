R"thin_header(

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

#ifndef NEVERC_STRING_ALLOC
#define NEVERC_STRING_ALLOC __builtin_malloc
#endif
#ifndef NEVERC_STRING_FREE
#define NEVERC_STRING_FREE __builtin_free
#endif

struct __neverc_format_spec;

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

extern void neverc_string_free(string s);

static __inline__ void __neverc_string_cleanup(string *s) {
  if (s) {
    neverc_string_free(*s);
    *s = __neverc_string_empty();
  }
}

static __inline__ const char *neverc_string_cstr(string s) {
  return s.data ? s.data : "";
}

static __inline__ void *neverc_string_data(string s) {
  return s.data ? (void *)s.data : (void *)"";
}

extern __UINT16_TYPE__ *neverc_string_to_utf16_owned(string s);
extern __UINT32_TYPE__ *neverc_string_to_utf32_owned(string s);

static __inline__ __WCHAR_TYPE__ *neverc_string_w_str(string s) {
#if __SIZEOF_WCHAR_T__ == 2
  return (__WCHAR_TYPE__ *)neverc_string_to_utf16_owned(s);
#elif __SIZEOF_WCHAR_T__ == 4
  return (__WCHAR_TYPE__ *)neverc_string_to_utf32_owned(s);
#else
  neverc_string_free(s);
  return (__WCHAR_TYPE__ *)0;
#endif
}

static __inline__ void __neverc_string_wfree_and_null(void **pp) {
  if (*pp) {
    NEVERC_STRING_FREE(*pp);
    *pp = (void *)0;
  }
}
#define neverc_string_wfree(p) __neverc_string_wfree_and_null((void *)&(p))

static __inline__ void __neverc_wptr_cleanup(void *p) {
  __neverc_string_wfree_and_null((void **)p);
}

)thin_header"
