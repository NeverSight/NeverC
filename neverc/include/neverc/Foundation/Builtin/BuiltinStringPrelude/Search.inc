R"prelude(

static __inline__ __SIZE_TYPE__
__neverc_string_find_raw_from(const char *data, __SIZE_TYPE__ len,
                              const char *needle, __SIZE_TYPE__ needle_len,
                              __SIZE_TYPE__ pos) {
  if (NEVERC_UNLIKELY((len && !data) || (needle_len && !needle)))
    return NEVERC_STRING_NPOS;
  if (NEVERC_UNLIKELY(needle_len == 0))
    return pos <= len ? pos : NEVERC_STRING_NPOS;
  if (NEVERC_UNLIKELY(pos > len || needle_len > len - pos))
    return NEVERC_STRING_NPOS;

  if (needle_len == 1) {
    const void *found =
        __builtin_memchr(data + pos, (unsigned char)needle[0], len - pos);
    return found ? (__SIZE_TYPE__)((const char *)found - data)
                 : NEVERC_STRING_NPOS;
  }

  unsigned char first = (unsigned char)needle[0];
  const char *p = data + pos;
  __SIZE_TYPE__ remaining = len - pos;
  while (remaining >= needle_len) {
    const char *hit =
        (const char *)__builtin_memchr(p, first, remaining - needle_len + 1);
    if (!hit)
      return NEVERC_STRING_NPOS;
    if (__builtin_memcmp(hit, needle, needle_len) == 0)
      return (__SIZE_TYPE__)(hit - data);
    __SIZE_TYPE__ skip = (__SIZE_TYPE__)(hit - p) + 1;
    p += skip;
    remaining -= skip;
  }
  return NEVERC_STRING_NPOS;
}

static __inline__ __SIZE_TYPE__
__neverc_string_find_raw(const char *data, __SIZE_TYPE__ len,
                         const char *needle, __SIZE_TYPE__ needle_len) {
  return __neverc_string_find_raw_from(data, len, needle, needle_len, 0);
}

static __inline__ __SIZE_TYPE__
__neverc_string_rfind_raw_to(const char *data, __SIZE_TYPE__ len,
                             const char *needle, __SIZE_TYPE__ needle_len,
                             __SIZE_TYPE__ pos) {
  if (NEVERC_UNLIKELY((len && !data) || (needle_len && !needle)))
    return NEVERC_STRING_NPOS;
  if (NEVERC_UNLIKELY(needle_len == 0))
    return pos < len ? pos : len;
  if (NEVERC_UNLIKELY(needle_len > len))
    return NEVERC_STRING_NPOS;

  __SIZE_TYPE__ last = len - needle_len;
  if (pos < last)
    last = pos;

  if (needle_len == 1) {
    unsigned char ch = (unsigned char)needle[0];
    for (__SIZE_TYPE__ i = last + 1; i-- > 0;) {
      if ((unsigned char)data[i] == ch)
        return i;
    }
    return NEVERC_STRING_NPOS;
  }

  unsigned char first = (unsigned char)needle[0];
  for (__SIZE_TYPE__ i = last + 1; i-- > 0;) {
    if ((unsigned char)data[i] == first &&
        __builtin_memcmp(data + i, needle, needle_len) == 0)
      return i;
  }
  return NEVERC_STRING_NPOS;
}

static __inline__ __SIZE_TYPE__
__neverc_string_rfind_raw(const char *data, __SIZE_TYPE__ len,
                          const char *needle, __SIZE_TYPE__ needle_len) {
  return __neverc_string_rfind_raw_to(data, len, needle, needle_len,
                                      NEVERC_STRING_NPOS);
}

static __inline__ __SIZE_TYPE__
__neverc_string_search_release_npos(string s, string needle) {
  __neverc_string_release2(s, needle);
  return NEVERC_STRING_NPOS;
}

static __SIZE_TYPE__ neverc_string_find(string s, string needle) {
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s) || __neverc_string_invalid(needle)))
    return __neverc_string_search_release_npos(s, needle);
  __SIZE_TYPE__ result =
      __neverc_string_find_raw(s.data, s.len, needle.data, needle.len);
  __neverc_string_release2(s, needle);
  return result;
}

static __SIZE_TYPE__ neverc_string_find_from(string s, string needle,
                                                 __SIZE_TYPE__ pos) {
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s) || __neverc_string_invalid(needle)))
    return __neverc_string_search_release_npos(s, needle);
  __SIZE_TYPE__ result =
      __neverc_string_find_raw_from(s.data, s.len, needle.data, needle.len,
                                    pos);
  __neverc_string_release2(s, needle);
  return result;
}

static __SIZE_TYPE__ neverc_string_rfind(string s, string needle) {
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s) || __neverc_string_invalid(needle)))
    return __neverc_string_search_release_npos(s, needle);
  __SIZE_TYPE__ result =
      __neverc_string_rfind_raw(s.data, s.len, needle.data, needle.len);
  __neverc_string_release2(s, needle);
  return result;
}

static __SIZE_TYPE__ neverc_string_rfind_to(string s, string needle,
                                                __SIZE_TYPE__ pos) {
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s) || __neverc_string_invalid(needle)))
    return __neverc_string_search_release_npos(s, needle);
  __SIZE_TYPE__ result =
      __neverc_string_rfind_raw_to(s.data, s.len, needle.data, needle.len, pos);
  __neverc_string_release2(s, needle);
  return result;
}

static __attribute__((always_inline)) int
__neverc_string_decrypt_starts_with(string s, const char *enc,
                                    __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  int result = 0;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s)))
    goto done;
  if (len > s.len)
    goto done;
  if (len == 0) {
    result = 1;
    goto done;
  }
  {
    result = 1;
    volatile __SIZE_TYPE__ vkey = key;
    for (__SIZE_TYPE__ i = 0; i < len; i++) {
      if (s.data[i] != NEVERC_STRING_DECRYPT_BYTE(enc[i], vkey, i)) {
        result = 0;
        break;
      }
    }
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) int
__neverc_string_decrypt_ends_with(string s, const char *enc,
                                  __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  int result = 0;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s)))
    goto done;
  if (len > s.len)
    goto done;
  if (len == 0) {
    result = 1;
    goto done;
  }
  {
    result = 1;
    __SIZE_TYPE__ offset = s.len - len;
    volatile __SIZE_TYPE__ vkey = key;
    for (__SIZE_TYPE__ i = 0; i < len; i++) {
      if (s.data[offset + i] != NEVERC_STRING_DECRYPT_BYTE(enc[i], vkey, i)) {
        result = 0;
        break;
      }
    }
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) int
__neverc_string_decrypt_contains(string s, const char *enc,
                                 __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  int result = 0;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s)))
    goto done;
  if (len == 0) {
    result = 1;
    goto done;
  }
  if (len > s.len)
    goto done;
  {
    volatile __SIZE_TYPE__ vkey = key;
    unsigned char first = (unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[0], vkey, 0);
    __SIZE_TYPE__ limit = s.len - len;
    for (__SIZE_TYPE__ pos = 0; pos <= limit; pos++) {
      if ((unsigned char)s.data[pos] != first)
        continue;
      __SIZE_TYPE__ j = 1;
      for (; j < len; j++) {
        if (s.data[pos + j] != NEVERC_STRING_DECRYPT_BYTE(enc[j], vkey, j))
          break;
      }
      if (j == len) {
        result = 1;
        goto done;
      }
    }
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) int
__neverc_string_decrypt_starts_with_ic(string s, const char *enc,
                                       __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  int result = 0;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s)))
    goto done;
  if (len > s.len)
    goto done;
  if (len == 0) {
    result = 1;
    goto done;
  }
  {
    result = 1;
    volatile __SIZE_TYPE__ vkey = key;
    for (__SIZE_TYPE__ i = 0; i < len; i++) {
      unsigned char sb = (unsigned char)s.data[i];
      unsigned char db = (unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[i], vkey, i);
      if (sb != db && __neverc_ascii_lower(sb) != __neverc_ascii_lower(db)) {
        result = 0;
        break;
      }
    }
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) int
__neverc_string_decrypt_ends_with_ic(string s, const char *enc,
                                     __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  int result = 0;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s)))
    goto done;
  if (len > s.len)
    goto done;
  if (len == 0) {
    result = 1;
    goto done;
  }
  {
    result = 1;
    __SIZE_TYPE__ offset = s.len - len;
    volatile __SIZE_TYPE__ vkey = key;
    for (__SIZE_TYPE__ i = 0; i < len; i++) {
      unsigned char sb = (unsigned char)s.data[offset + i];
      unsigned char db = (unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[i], vkey, i);
      if (sb != db && __neverc_ascii_lower(sb) != __neverc_ascii_lower(db)) {
        result = 0;
        break;
      }
    }
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) int
__neverc_string_decrypt_contains_ic(string s, const char *enc,
                                    __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  int result = 0;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s)))
    goto done;
  if (len == 0) {
    result = 1;
    goto done;
  }
  if (len > s.len)
    goto done;
  {
    volatile __SIZE_TYPE__ vkey = key;
    unsigned char first =
        __neverc_ascii_lower((unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[0], vkey, 0));
    __SIZE_TYPE__ limit = s.len - len;
    for (__SIZE_TYPE__ pos = 0; pos <= limit; pos++) {
      if (__neverc_ascii_lower((unsigned char)s.data[pos]) != first)
        continue;
      __SIZE_TYPE__ j = 1;
      for (; j < len; j++) {
        unsigned char sb = (unsigned char)s.data[pos + j];
        unsigned char db = (unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[j], vkey, j);
        if (sb != db && __neverc_ascii_lower(sb) != __neverc_ascii_lower(db))
          break;
      }
      if (j == len) {
        result = 1;
        goto done;
      }
    }
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) __SIZE_TYPE__
__neverc_string_decrypt_find(string s, const char *enc,
                             __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s)))
    goto done;
  if (len == 0) {
    result = 0;
    goto done;
  }
  if (len > s.len)
    goto done;
  {
    volatile __SIZE_TYPE__ vkey = key;
    unsigned char first = (unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[0], vkey, 0);
    __SIZE_TYPE__ limit = s.len - len;
    for (__SIZE_TYPE__ pos = 0; pos <= limit; pos++) {
      if ((unsigned char)s.data[pos] != first)
        continue;
      __SIZE_TYPE__ j = 1;
      for (; j < len; j++) {
        if (s.data[pos + j] != NEVERC_STRING_DECRYPT_BYTE(enc[j], vkey, j))
          break;
      }
      if (j == len) {
        result = pos;
        goto done;
      }
    }
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) __SIZE_TYPE__
__neverc_string_decrypt_find_from(string s, const char *enc,
                                  __SIZE_TYPE__ len, __SIZE_TYPE__ key,
                                  __SIZE_TYPE__ pos) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s)))
    goto done;
  if (len == 0) {
    result = pos <= s.len ? pos : NEVERC_STRING_NPOS;
    goto done;
  }
  if (pos > s.len || len > s.len - pos)
    goto done;
  {
    volatile __SIZE_TYPE__ vkey = key;
    unsigned char first = (unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[0], vkey, 0);
    __SIZE_TYPE__ limit = s.len - len;
    for (__SIZE_TYPE__ p = pos; p <= limit; p++) {
      if ((unsigned char)s.data[p] != first)
        continue;
      __SIZE_TYPE__ j = 1;
      for (; j < len; j++) {
        if (s.data[p + j] != NEVERC_STRING_DECRYPT_BYTE(enc[j], vkey, j))
          break;
      }
      if (j == len) {
        result = p;
        goto done;
      }
    }
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) __SIZE_TYPE__
__neverc_string_decrypt_rfind(string s, const char *enc,
                              __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s)))
    goto done;
  if (len == 0) {
    result = s.len;
    goto done;
  }
  if (len > s.len)
    goto done;
  {
    volatile __SIZE_TYPE__ vkey = key;
    unsigned char first = (unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[0], vkey, 0);
    for (__SIZE_TYPE__ i = s.len - len + 1; i-- > 0;) {
      if ((unsigned char)s.data[i] != first)
        continue;
      __SIZE_TYPE__ j = 1;
      for (; j < len; j++) {
        if (s.data[i + j] != NEVERC_STRING_DECRYPT_BYTE(enc[j], vkey, j))
          break;
      }
      if (j == len) {
        result = i;
        goto done;
      }
    }
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) __SIZE_TYPE__
__neverc_string_decrypt_rfind_to(string s, const char *enc,
                                 __SIZE_TYPE__ len, __SIZE_TYPE__ key,
                                 __SIZE_TYPE__ pos) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s)))
    goto done;
  if (len == 0) {
    result = pos < s.len ? pos : s.len;
    goto done;
  }
  if (len > s.len)
    goto done;
  {
    volatile __SIZE_TYPE__ vkey = key;
    unsigned char first = (unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[0], vkey, 0);
    __SIZE_TYPE__ last = s.len - len;
    if (pos < last)
      last = pos;
    for (__SIZE_TYPE__ i = last + 1; i-- > 0;) {
      if ((unsigned char)s.data[i] != first)
        continue;
      __SIZE_TYPE__ j = 1;
      for (; j < len; j++) {
        if (s.data[i + j] != NEVERC_STRING_DECRYPT_BYTE(enc[j], vkey, j))
          break;
      }
      if (j == len) {
        result = i;
        goto done;
      }
    }
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) __SIZE_TYPE__
__neverc_string_decrypt_find_ic(string s, const char *enc,
                                __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s)))
    goto done;
  if (len == 0) {
    result = 0;
    goto done;
  }
  if (len > s.len)
    goto done;
  {
    volatile __SIZE_TYPE__ vkey = key;
    unsigned char first =
        __neverc_ascii_lower((unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[0], vkey, 0));
    __SIZE_TYPE__ limit = s.len - len;
    for (__SIZE_TYPE__ pos = 0; pos <= limit; pos++) {
      if (__neverc_ascii_lower((unsigned char)s.data[pos]) != first)
        continue;
      __SIZE_TYPE__ j = 1;
      for (; j < len; j++) {
        unsigned char sb = (unsigned char)s.data[pos + j];
        unsigned char db = (unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[j], vkey, j);
        if (sb != db && __neverc_ascii_lower(sb) != __neverc_ascii_lower(db))
          break;
      }
      if (j == len) {
        result = pos;
        goto done;
      }
    }
  }
done:
  neverc_string_free(s);
  return result;
}

static __attribute__((always_inline)) __SIZE_TYPE__
__neverc_string_decrypt_count(string s, const char *enc,
                              __SIZE_TYPE__ len, __SIZE_TYPE__ key) {
  __SIZE_TYPE__ n = 0;
  if (__neverc_string_invalid(s))
    goto done;
  if (len == 0) {
    n = s.len + 1;
    goto done;
  }
  if (len > s.len)
    goto done;
  {
    volatile __SIZE_TYPE__ vkey = key;
    unsigned char first = (unsigned char)NEVERC_STRING_DECRYPT_BYTE(enc[0], vkey, 0);
    __SIZE_TYPE__ limit = s.len - len;
    for (__SIZE_TYPE__ pos = 0; pos <= limit;) {
      if ((unsigned char)s.data[pos] != first) {
        pos++;
        continue;
      }
      __SIZE_TYPE__ j = 1;
      for (; j < len; j++) {
        if (s.data[pos + j] != NEVERC_STRING_DECRYPT_BYTE(enc[j], vkey, j))
          break;
      }
      if (j == len) {
        n++;
        pos += len;
      } else {
        pos++;
      }
    }
  }
done:
  neverc_string_free(s);
  return n;
}

static int neverc_string_contains(string s, string needle) {
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s) || __neverc_string_invalid(needle))) {
    __neverc_string_release2(s, needle);
    return 0;
  }
  __SIZE_TYPE__ found =
      __neverc_string_find_raw(s.data, s.len, needle.data, needle.len);
  __neverc_string_release2(s, needle);
  return found != NEVERC_STRING_NPOS;
}

static int neverc_string_starts_with(string s, string prefix) {
  int result = 0;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s) || __neverc_string_invalid(prefix)))
    goto done;
  if (prefix.len > s.len)
    goto done;
  if (prefix.len == 0)
    result = 1;
  else
    result = __builtin_memcmp(s.data, prefix.data, prefix.len) == 0;

done:
  __neverc_string_release2(s, prefix);
  return result;
}

static int neverc_string_ends_with(string s, string suffix) {
  int result = 0;
  if (NEVERC_UNLIKELY(__neverc_string_invalid(s) || __neverc_string_invalid(suffix)))
    goto done;
  if (suffix.len > s.len)
    goto done;
  if (suffix.len == 0)
    result = 1;
  else
    result = __builtin_memcmp(s.data + s.len - suffix.len, suffix.data,
                              suffix.len) == 0;

done:
  __neverc_string_release2(s, suffix);
  return result;
}

static __inline__ __SIZE_TYPE__ neverc_string_find_char(string s, char ch) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (NEVERC_LIKELY(!__neverc_string_invalid(s) && s.data && s.len)) {
    const char *p = (const char *)__builtin_memchr(s.data, (unsigned char)ch, s.len);
    if (p)
      result = (__SIZE_TYPE__)(p - s.data);
  }
  neverc_string_free(s);
  return result;
}

static __inline__ __SIZE_TYPE__ neverc_string_find_char_from(string s, char ch,
                                                       __SIZE_TYPE__ pos) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (NEVERC_LIKELY(!__neverc_string_invalid(s) && s.data && pos < s.len)) {
    const char *p = (const char *)__builtin_memchr(s.data + pos,
                                                    (unsigned char)ch,
                                                    s.len - pos);
    if (p)
      result = (__SIZE_TYPE__)(p - s.data);
  }
  neverc_string_free(s);
  return result;
}

static __SIZE_TYPE__ neverc_string_rfind_char(string s, char ch) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (!__neverc_string_invalid(s) && s.data && s.len) {

    for (__SIZE_TYPE__ i = s.len; i-- > 0;) {
      if (s.data[i] == ch) {
        result = i;
        break;
      }
    }
  }
  neverc_string_free(s);
  return result;
}

static __SIZE_TYPE__ neverc_string_rfind_char_to(string s, char ch,
                                                      __SIZE_TYPE__ pos) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (!__neverc_string_invalid(s) && s.data && s.len) {
    __SIZE_TYPE__ last = pos < s.len ? pos : s.len - 1;
    for (__SIZE_TYPE__ i = last + 1; i-- > 0;) {
      if (s.data[i] == ch) {
        result = i;
        break;
      }
    }
  }
  neverc_string_free(s);
  return result;
}

static __SIZE_TYPE__ neverc_string_find_char_not(string s, char ch) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (!__neverc_string_invalid(s) && s.data) {
    for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
      if (s.data[i] != ch) {
        result = i;
        break;
      }
    }
  }
  neverc_string_free(s);
  return result;
}

static __SIZE_TYPE__ neverc_string_find_char_not_from(string s, char ch,
                                                           __SIZE_TYPE__ pos) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (!__neverc_string_invalid(s) && s.data && pos < s.len) {
    for (__SIZE_TYPE__ i = pos; i < s.len; i++) {
      if (s.data[i] != ch) {
        result = i;
        break;
      }
    }
  }
  neverc_string_free(s);
  return result;
}

static __SIZE_TYPE__ neverc_string_rfind_char_not(string s, char ch) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (!__neverc_string_invalid(s) && s.data && s.len) {
    for (__SIZE_TYPE__ i = s.len; i-- > 0;) {
      if (s.data[i] != ch) {
        result = i;
        break;
      }
    }
  }
  neverc_string_free(s);
  return result;
}

static __SIZE_TYPE__ neverc_string_rfind_char_not_to(string s, char ch,
                                                          __SIZE_TYPE__ pos) {
  __SIZE_TYPE__ result = NEVERC_STRING_NPOS;
  if (!__neverc_string_invalid(s) && s.data && s.len) {
    __SIZE_TYPE__ last = pos < s.len ? pos : s.len - 1;
    for (__SIZE_TYPE__ i = last + 1; i-- > 0;) {
      if (s.data[i] != ch) {
        result = i;
        break;
      }
    }
  }
  neverc_string_free(s);
  return result;
}

static string neverc_string_substr(string s, __SIZE_TYPE__ pos,
                                      __SIZE_TYPE__ len) {
  if (__neverc_string_invalid(s) || pos > s.len)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ remaining = s.len - pos;
  if (len > remaining)
    len = remaining;
  if (len == 0)
    return __neverc_string_consume(s);
  if (s.cap) {
    char *buf = (char *)s.data;
    if (pos)
      __builtin_memmove(buf, buf + pos, len);
    buf[len] = 0;
    return __neverc_string_make_owned_handle(buf, len, s.cap);
  }
  return __neverc_string_borrow_copy(s, s.data + pos, len);
}

static __inline__ void
__neverc_string_build_charset(const char *chars, __SIZE_TYPE__ chars_len,
                              unsigned char bitmap[32]) {
  __builtin_memset(bitmap, 0, 32);
  for (__SIZE_TYPE__ i = 0; i < chars_len; i++) {
    unsigned char c = (unsigned char)chars[i];
    bitmap[c >> 3] |= (unsigned char)(1u << (c & 7));
  }
}

static __inline__ int
__neverc_string_charset_has(const unsigned char bitmap[32], unsigned char c) {
  return (bitmap[c >> 3] >> (c & 7)) & 1;
}

static __inline__ __SIZE_TYPE__
__neverc_string_find_first_of_raw(const char *data, __SIZE_TYPE__ len,
                                  const char *chars, __SIZE_TYPE__ chars_len,
                                  __SIZE_TYPE__ pos) {
  if (!data || len == 0 || chars_len == 0 || !chars || pos >= len)
    return NEVERC_STRING_NPOS;
  unsigned char bitmap[32];
  __neverc_string_build_charset(chars, chars_len, bitmap);
  for (__SIZE_TYPE__ i = pos; i < len; i++)
    if (__neverc_string_charset_has(bitmap, (unsigned char)data[i]))
      return i;
  return NEVERC_STRING_NPOS;
}

static __inline__ __SIZE_TYPE__
__neverc_string_find_last_of_raw(const char *data, __SIZE_TYPE__ len,
                                 const char *chars, __SIZE_TYPE__ chars_len,
                                 __SIZE_TYPE__ pos) {
  if (!data || len == 0 || chars_len == 0 || !chars)
    return NEVERC_STRING_NPOS;
  unsigned char bitmap[32];
  __neverc_string_build_charset(chars, chars_len, bitmap);
  __SIZE_TYPE__ last = pos < len ? pos : len - 1;
  for (__SIZE_TYPE__ i = last + 1; i-- > 0;)
    if (__neverc_string_charset_has(bitmap, (unsigned char)data[i]))
      return i;
  return NEVERC_STRING_NPOS;
}

static __inline__ __SIZE_TYPE__
__neverc_string_find_first_not_of_raw(const char *data, __SIZE_TYPE__ len,
                                      const char *chars,
                                      __SIZE_TYPE__ chars_len,
                                      __SIZE_TYPE__ pos) {
  if (!data || len == 0 || pos >= len)
    return NEVERC_STRING_NPOS;
  unsigned char bitmap[32];
  __neverc_string_build_charset(chars, chars_len, bitmap);
  for (__SIZE_TYPE__ i = pos; i < len; i++)
    if (!__neverc_string_charset_has(bitmap, (unsigned char)data[i]))
      return i;
  return NEVERC_STRING_NPOS;
}

static __inline__ __SIZE_TYPE__
__neverc_string_find_last_not_of_raw(const char *data, __SIZE_TYPE__ len,
                                     const char *chars,
                                     __SIZE_TYPE__ chars_len,
                                     __SIZE_TYPE__ pos) {
  if (!data || len == 0)
    return NEVERC_STRING_NPOS;
  unsigned char bitmap[32];
  __neverc_string_build_charset(chars, chars_len, bitmap);
  __SIZE_TYPE__ last = pos < len ? pos : len - 1;
  for (__SIZE_TYPE__ i = last + 1; i-- > 0;)
    if (!__neverc_string_charset_has(bitmap, (unsigned char)data[i]))
      return i;
  return NEVERC_STRING_NPOS;
}

static __SIZE_TYPE__ neverc_string_find_first_of(string s, string chars) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(chars))
    return __neverc_string_search_release_npos(s, chars);
  __SIZE_TYPE__ result = __neverc_string_find_first_of_raw(
      s.data, s.len, chars.data, chars.len, 0);
  __neverc_string_release2(s, chars);
  return result;
}

static __SIZE_TYPE__ neverc_string_find_first_of_from(string s, string chars,
                                                          __SIZE_TYPE__ pos) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(chars))
    return __neverc_string_search_release_npos(s, chars);
  __SIZE_TYPE__ result = __neverc_string_find_first_of_raw(
      s.data, s.len, chars.data, chars.len, pos);
  __neverc_string_release2(s, chars);
  return result;
}

static __SIZE_TYPE__ neverc_string_find_last_of(string s, string chars) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(chars))
    return __neverc_string_search_release_npos(s, chars);
  __SIZE_TYPE__ result = __neverc_string_find_last_of_raw(
      s.data, s.len, chars.data, chars.len, NEVERC_STRING_NPOS);
  __neverc_string_release2(s, chars);
  return result;
}

static __SIZE_TYPE__ neverc_string_find_last_of_to(string s, string chars,
                                                       __SIZE_TYPE__ pos) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(chars))
    return __neverc_string_search_release_npos(s, chars);
  __SIZE_TYPE__ result = __neverc_string_find_last_of_raw(
      s.data, s.len, chars.data, chars.len, pos);
  __neverc_string_release2(s, chars);
  return result;
}

static __inline__ __SIZE_TYPE__
neverc_string_find_first_not_of(string s, string chars) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(chars))
    return __neverc_string_search_release_npos(s, chars);
  __SIZE_TYPE__ result = __neverc_string_find_first_not_of_raw(
      s.data, s.len, chars.data, chars.len, 0);
  __neverc_string_release2(s, chars);
  return result;
}

static __inline__ __SIZE_TYPE__
neverc_string_find_first_not_of_from(string s, string chars, __SIZE_TYPE__ pos) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(chars))
    return __neverc_string_search_release_npos(s, chars);
  __SIZE_TYPE__ result = __neverc_string_find_first_not_of_raw(
      s.data, s.len, chars.data, chars.len, pos);
  __neverc_string_release2(s, chars);
  return result;
}

static __inline__ __SIZE_TYPE__
neverc_string_find_last_not_of(string s, string chars) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(chars))
    return __neverc_string_search_release_npos(s, chars);
  __SIZE_TYPE__ result = __neverc_string_find_last_not_of_raw(
      s.data, s.len, chars.data, chars.len, NEVERC_STRING_NPOS);
  __neverc_string_release2(s, chars);
  return result;
}

static __inline__ __SIZE_TYPE__
neverc_string_find_last_not_of_to(string s, string chars, __SIZE_TYPE__ pos) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(chars))
    return __neverc_string_search_release_npos(s, chars);
  __SIZE_TYPE__ result = __neverc_string_find_last_not_of_raw(
      s.data, s.len, chars.data, chars.len, pos);
  __neverc_string_release2(s, chars);
  return result;
}

static __inline__ __SIZE_TYPE__
__neverc_string_copy_raw(string s, char *out, __SIZE_TYPE__ count,
                         __SIZE_TYPE__ pos) {
  __SIZE_TYPE__ copied = 0;
  if (__neverc_string_invalid(s) || pos > s.len)
    goto done;
  __SIZE_TYPE__ remaining = s.len - pos;
  if (count > remaining)
    count = remaining;
  if (out) {
    __builtin_memcpy(out, s.data + pos, count);
    copied = count;
  } else {
    copied = count;
  }

done:
  neverc_string_free(s);
  return copied;
}

static __SIZE_TYPE__ neverc_string_copy(string s, char *out,
                                             __SIZE_TYPE__ count) {
  return __neverc_string_copy_raw(s, out, count, 0);
}

static __SIZE_TYPE__ neverc_string_copy_from(string s, char *out,
                                                  __SIZE_TYPE__ count,
                                                  __SIZE_TYPE__ pos) {
  return __neverc_string_copy_raw(s, out, count, pos);
}

static __inline__ __SIZE_TYPE__
__neverc_string_find_raw_ic(const char *haystack, __SIZE_TYPE__ hlen,
                            const char *needle, __SIZE_TYPE__ nlen) {
  if (nlen == 0)
    return 0;
  if (nlen > hlen)
    return NEVERC_STRING_NPOS;

  unsigned char first_lower = __neverc_ascii_lower((unsigned char)needle[0]);
  int first_is_alpha = (unsigned char)(first_lower - 'a') < 26u;
  unsigned char first_upper =
      first_is_alpha ? (unsigned char)(first_lower ^ 0x20) : first_lower;

  const char *p = haystack;
  __SIZE_TYPE__ remaining = hlen;
  while (remaining >= nlen) {
    __SIZE_TYPE__ scan_len = remaining - nlen + 1;
    const char *hit = (const char *)__builtin_memchr(
        p, first_lower, scan_len);
    if (first_is_alpha) {
      if (!hit) {
        hit = (const char *)__builtin_memchr(p, first_upper, scan_len);
      } else {
        __SIZE_TYPE__ before = (__SIZE_TYPE__)(hit - p);
        if (before > 0) {
          const char *hit2 =
              (const char *)__builtin_memchr(p, first_upper, before);
          if (hit2)
            hit = hit2;
        }
      }
    }
    if (!hit)
      return NEVERC_STRING_NPOS;

    __SIZE_TYPE__ off = (__SIZE_TYPE__)(hit - haystack);
    __SIZE_TYPE__ j = 1;
    for (; j < nlen; j++) {
      unsigned char hb = (unsigned char)hit[j];
      unsigned char nb = (unsigned char)needle[j];
      if (hb != nb &&
          __neverc_ascii_lower(hb) != __neverc_ascii_lower(nb))
        break;
    }
    if (j == nlen)
      return off;
    __SIZE_TYPE__ skip = (__SIZE_TYPE__)(hit - p) + 1;
    p += skip;
    remaining -= skip;
  }
  return NEVERC_STRING_NPOS;
}

static __SIZE_TYPE__ neverc_string_find_ic(string s, string needle) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(needle))
    return __neverc_string_search_release_npos(s, needle);
  __SIZE_TYPE__ result =
      __neverc_string_find_raw_ic(s.data, s.len, needle.data, needle.len);
  __neverc_string_release2(s, needle);
  return result;
}

static int neverc_string_contains_ic(string s, string needle) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(needle)) {
    __neverc_string_release2(s, needle);
    return 0;
  }
  __SIZE_TYPE__ found =
      __neverc_string_find_raw_ic(s.data, s.len, needle.data, needle.len);
  __neverc_string_release2(s, needle);
  return found != NEVERC_STRING_NPOS;
}

static int neverc_string_starts_with_ic(string s, string prefix) {
  int result = 0;
  if (__neverc_string_invalid(s) || __neverc_string_invalid(prefix))
    goto done;
  if (prefix.len > s.len)
    goto done;
  if (prefix.len == 0) {
    result = 1;
    goto done;
  }

  if (__builtin_memcmp(s.data, prefix.data, prefix.len) == 0) {
    result = 1;
    goto done;
  }
  result = 1;
  for (__SIZE_TYPE__ i = 0; i < prefix.len; i++) {
    unsigned char sb = (unsigned char)s.data[i];
    unsigned char pb = (unsigned char)prefix.data[i];
    if (sb != pb &&
        __neverc_ascii_lower(sb) != __neverc_ascii_lower(pb)) {
      result = 0;
      break;
    }
  }

done:
  __neverc_string_release2(s, prefix);
  return result;
}

static int neverc_string_ends_with_ic(string s, string suffix) {
  int result = 0;
  if (__neverc_string_invalid(s) || __neverc_string_invalid(suffix))
    goto done;
  if (suffix.len > s.len)
    goto done;
  if (suffix.len == 0) {
    result = 1;
    goto done;
  }
  __SIZE_TYPE__ offset = s.len - suffix.len;

  if (__builtin_memcmp(s.data + offset, suffix.data, suffix.len) == 0) {
    result = 1;
    goto done;
  }
  result = 1;
  for (__SIZE_TYPE__ i = 0; i < suffix.len; i++) {
    unsigned char sb = (unsigned char)s.data[offset + i];
    unsigned char fb = (unsigned char)suffix.data[i];
    if (sb != fb &&
        __neverc_ascii_lower(sb) != __neverc_ascii_lower(fb)) {
      result = 0;
      break;
    }
  }

done:
  __neverc_string_release2(s, suffix);
  return result;
})prelude"
