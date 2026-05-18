R"prelude(
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

static __inline__ int __neverc_string_can_allocate(__SIZE_TYPE__ len) {
  return len <= NEVERC_STRING_MAX_LEN;
}

static void neverc_string_free(string s) {
  if (s.cap && s.data)
    NEVERC_STRING_FREE((void *)s.data);
}

static __inline__ void __neverc_string_cleanup(string *s) {
  if (s) {
    neverc_string_free(*s);
    *s = __neverc_string_empty();
  }
}

static void neverc_string_array_free(string *items,
                                                __SIZE_TYPE__ count) {
  if (!items)
    return;
  for (__SIZE_TYPE__ i = 0; i < count; i++) {
    neverc_string_free(items[i]);
    items[i] = __neverc_string_empty();
  }
}

static __inline__ string __neverc_string_consume(string s) {
  neverc_string_free(s);
  return __neverc_string_empty();
}

static __inline__ string __neverc_string_consume2(string a, string b) {
  neverc_string_free(a);
  neverc_string_free(b);
  return __neverc_string_empty();
}

static __inline__ void __neverc_string_release2(string a, string b) {
  neverc_string_free(a);
  neverc_string_free(b);
}

static __inline__ string __neverc_string_make_owned_handle(char *data,
                                                           __SIZE_TYPE__ len,
                                                           __SIZE_TYPE__ cap) {
  return (string){data, len, cap};
}

static __inline__ string __neverc_string_finalize_owned(char *p,
                                                        __SIZE_TYPE__ len) {
  if (NEVERC_UNLIKELY(!p))
    return __neverc_string_empty();
  p[len] = 0;
  return __neverc_string_make_owned_handle(p, len, len + 1);
}

static __inline__ char *__neverc_string_alloc_payload(__SIZE_TYPE__ len) {
  if (NEVERC_UNLIKELY(!__neverc_string_can_allocate(len)))
    return (char *)0;
  return (char *)NEVERC_STRING_ALLOC(len + 1);
}

static __inline__ int __neverc_string_classify_view(string owner,
                                                    const char *data,
                                                    __SIZE_TYPE__ len) {
  if (!owner.cap || !owner.data || !data)
    return 0;
  __UINTPTR_TYPE__ begin = (__UINTPTR_TYPE__)owner.data;
  __UINTPTR_TYPE__ point = (__UINTPTR_TYPE__)data;
  if (point < begin)
    return 0;
  __SIZE_TYPE__ offset = (__SIZE_TYPE__)(point - begin);
  if (offset > owner.cap)
    return 0;
  if (offset <= owner.len && len <= owner.len - offset)
    return 1;
  return 2;
}

static __inline__ int __neverc_string_view_in_range(string owner,
                                                    const char *data,
                                                    __SIZE_TYPE__ len) {
  return __neverc_string_classify_view(owner, data, len) == 1;
}

static __inline__ int __neverc_string_view_touches_allocation(string owner,
                                                              const char *data) {
  return __neverc_string_classify_view(owner, data, 0) != 0;
}

static __inline__ string __neverc_string_make_owned_with_cap(const char *data,
                                                              __SIZE_TYPE__ len,
                                                              __SIZE_TYPE__ cap) {
  if (NEVERC_UNLIKELY(len > NEVERC_STRING_MAX_LEN))
    return __neverc_string_empty();
  if (NEVERC_UNLIKELY(len && !data))
    return __neverc_string_empty();
  if (cap < len + 1)
    cap = len + 1;
  if (!__neverc_string_can_allocate(cap == 0 ? 0 : cap - 1))
    return __neverc_string_empty();
  char *p = (char *)NEVERC_STRING_ALLOC(cap);
  if (!p)
    return __neverc_string_empty();
  if (len)
    __builtin_memcpy(p, data, len);
  p[len] = 0;
  if (cap > len + 1)
    __builtin_memset(p + len + 1, 0, cap - len - 1);
  return __neverc_string_make_owned_handle(p, len, cap);
}

static __inline__ string __neverc_string_make_owned(const char *data,
                                                   __SIZE_TYPE__ len) {

  if (len == 0)
    return __neverc_string_empty();
  return __neverc_string_make_owned_with_cap(data, len, len + 1);
}

static __inline__ string __neverc_string_borrow_copy(string s,
                                                     const char *data,
                                                     __SIZE_TYPE__ len) {
  string copy = __neverc_string_make_owned(data, len);
  neverc_string_free(s);
  return copy;
}

static __inline__ string __neverc_string_cat(string a, string b) {
  if (__neverc_string_invalid(a) || __neverc_string_invalid(b))
    return __neverc_string_consume2(a, b);
  if (a.len == 0) {
    neverc_string_free(a);
    return b.cap ? b : __neverc_string_make_owned(b.data, b.len);
  }
  if (b.len == 0) {
    neverc_string_free(b);
    return a.cap ? a : __neverc_string_make_owned(a.data, a.len);
  }
  if (a.len > NEVERC_STRING_MAX_LEN - b.len)
    return __neverc_string_consume2(a, b);
  __SIZE_TYPE__ n = a.len + b.len;

  if (a.cap > n) {
    char *buf = (char *)a.data;
    __builtin_memcpy(buf + a.len, b.data, b.len);
    buf[n] = 0;
    neverc_string_free(b);
    return __neverc_string_make_owned_handle(buf, n, a.cap);
  }
  char *p = __neverc_string_alloc_payload(n);
  if (!p)
    return __neverc_string_consume2(a, b);
  __builtin_memcpy(p, a.data, a.len);
  __builtin_memcpy(p + a.len, b.data, b.len);
  __neverc_string_release2(a, b);
  return __neverc_string_finalize_owned(p, n);
}

static __inline__ string __neverc_string_retain(string s) {
  if (s.len == 0)
    return __neverc_string_empty();
  return __neverc_string_make_owned(s.data, s.len);
}

static string neverc_string_clone(string s) {
  if (s.cap) {
    if (__neverc_string_invalid(s)) {
      neverc_string_free(s);
      return __neverc_string_empty();
    }
    return s;
  }
  return __neverc_string_retain(s);
}

static __inline__ string __neverc_string_assign_borrowed(string *dst,
                                                         string src) {
  string copy = __neverc_string_make_owned(src.data, src.len);
  neverc_string_free(src);

  if (src.len > 0 && copy.len == 0)
    return __neverc_string_retain(*dst);
  neverc_string_free(*dst);
  *dst = copy;
  return __neverc_string_retain(*dst);
}

static __inline__ string __neverc_string_assign(string *dst, string src) {
  if (!dst)
    return __neverc_string_consume(src);

  if (src.cap) {
    if (!(dst->cap && dst->data == src.data)) {
      neverc_string_free(*dst);
      *dst = src;
    }
    return __neverc_string_retain(*dst);
  }

  if (dst->cap) {

    int alias = __neverc_string_classify_view(*dst, src.data, src.len);
    if (alias == 1)
      return __neverc_string_assign_borrowed(dst, src);
    if (alias == 2) {
      neverc_string_free(src);
      return __neverc_string_retain(*dst);
    }
  }
  if (src.data && src.len)
    return __neverc_string_assign_borrowed(dst, src);

  neverc_string_free(*dst);
  *dst = src;
  return __neverc_string_retain(*dst);
}

static void neverc_string_assign(string *dst, string src) {
  string chain = __neverc_string_assign(dst, src);
  neverc_string_free(chain);
}

static void neverc_string_assign_char(string *dst, __SIZE_TYPE__ count,
                                           char ch) {
  if (!dst)
    return;
  if (count == 0 || count > NEVERC_STRING_MAX_LEN) {
    neverc_string_assign(dst, __neverc_string_empty());
    return;
  }
  char *p = __neverc_string_alloc_payload(count);
  if (!p) {
    neverc_string_assign(dst, __neverc_string_empty());
    return;
  }
  __builtin_memset(p, (unsigned char)ch, count);
  neverc_string_assign(dst, __neverc_string_finalize_owned(p, count));
})prelude"
