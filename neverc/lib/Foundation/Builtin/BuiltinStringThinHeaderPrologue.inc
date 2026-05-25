R"thin_header(

#ifndef NEVERC_STRING_FREE
#define NEVERC_STRING_FREE __builtin_free
#endif

struct __neverc_format_spec;

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

static __attribute__((always_inline)) int
__neverc_string_decrypt_equals(string s, const char *enc,
                               __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  int equal;
  if (s.len != len) {
    equal = 0;
  } else if (len == 0) {
    equal = (s.len == 0) ? 1 : 0;
  } else if (NEVERC_UNLIKELY(__neverc_string_invalid(s))) {
    equal = 0;
  } else {
    equal = 1;
    volatile __SIZE_TYPE__ vkey = key;
    for (__SIZE_TYPE__ i = 0; i < len; i++) {
      if (s.data[i] != NEVERC_STRING_DECRYPT_BYTE(enc[i], vkey, i)) {
        equal = 0;
        break;
      }
    }
  }
  neverc_string_free(s);
  return equal;
}

)thin_header"
