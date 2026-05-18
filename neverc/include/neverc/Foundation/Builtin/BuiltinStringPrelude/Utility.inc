R"prelude(

#ifndef NEVERC_STRING_ASCII_CASE_DELTA
#define NEVERC_STRING_ASCII_CASE_DELTA ('a' - 'A')
#endif

static __inline__ int __neverc_string_is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
         c == '\f' || c == '\v';
}

static __inline__ char __neverc_string_to_lower_char(char c) {
  unsigned char uc = (unsigned char)c;
  return (char)(uc | (((unsigned char)(uc - 'A') < 26u) << 5));
}

static __inline__ char __neverc_string_to_upper_char(char c) {
  unsigned char uc = (unsigned char)c;
  return (char)(uc ^ (((unsigned char)(uc - 'a') < 26u) << 5));
}

static __inline__ __SIZE_TYPE__ __neverc_string_cstr_len(const char *s) {
  return s ? (__SIZE_TYPE__)__builtin_strlen(s) : 0;
}

static string neverc_string_from_cstr(const char *s) {
  if (!s)
    return __neverc_string_empty();
  return __neverc_string_make_owned(s, __neverc_string_cstr_len(s));
}

static string neverc_string_from_char(char ch) {
  char buf[2];
  buf[0] = ch;
  buf[1] = 0;
  return __neverc_string_make_owned(buf, 1);
}

static string neverc_string_repeat(string s, __SIZE_TYPE__ n) {

  if (__neverc_string_invalid(s) || n == 0 || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ total = s.len * n;
  if (total / n != s.len)
    return __neverc_string_consume(s);
  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);
  if (s.len == 1) {
    __builtin_memset(p, (unsigned char)s.data[0], n);
  } else {
    __builtin_memcpy(p, s.data, s.len);

    __SIZE_TYPE__ filled = s.len;
    while (filled < total) {
      __SIZE_TYPE__ chunk = total - filled;
      if (chunk > filled)
        chunk = filled;
      __builtin_memcpy(p + filled, p, chunk);
      filled += chunk;
    }
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, total);
}

static string neverc_string_to_lower(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ first_upper = s.len;
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    if ((unsigned char)((unsigned char)s.data[i] - 'A') < 26u) {
      first_upper = i;
      break;
    }
  }
  if (first_upper == s.len) {
    if (s.cap)
      return s;
    return __neverc_string_borrow_copy(s, s.data, s.len);
  }
  if (s.cap) {
    char *buf = (char *)s.data;
    for (__SIZE_TYPE__ i = first_upper; i < s.len; i++)
      buf[i] = __neverc_string_to_lower_char(buf[i]);
    return s;
  }
  char *p = __neverc_string_alloc_payload(s.len);
  if (!p)
    return __neverc_string_consume(s);
  if (first_upper)
    __builtin_memcpy(p, s.data, first_upper);
  for (__SIZE_TYPE__ i = first_upper; i < s.len; i++)
    p[i] = __neverc_string_to_lower_char(s.data[i]);
  __SIZE_TYPE__ len = s.len;
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, len);
}

static string neverc_string_to_upper(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ first_lower = s.len;
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    if ((unsigned char)((unsigned char)s.data[i] - 'a') < 26u) {
      first_lower = i;
      break;
    }
  }
  if (first_lower == s.len) {
    if (s.cap)
      return s;
    return __neverc_string_borrow_copy(s, s.data, s.len);
  }
  if (s.cap) {
    char *buf = (char *)s.data;
    for (__SIZE_TYPE__ i = first_lower; i < s.len; i++)
      buf[i] = __neverc_string_to_upper_char(buf[i]);
    return s;
  }
  char *p = __neverc_string_alloc_payload(s.len);
  if (!p)
    return __neverc_string_consume(s);
  if (first_lower)
    __builtin_memcpy(p, s.data, first_lower);
  for (__SIZE_TYPE__ i = first_lower; i < s.len; i++)
    p[i] = __neverc_string_to_upper_char(s.data[i]);
  __SIZE_TYPE__ len = s.len;
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, len);
}

static string neverc_string_ltrim(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ start = 0;
  while (start < s.len && __neverc_string_is_whitespace(s.data[start]))
    start++;
  if (start >= s.len)
    return __neverc_string_consume(s);
  if (start == 0) {
    if (s.cap)
      return s;
    return __neverc_string_borrow_copy(s, s.data, s.len);
  }
  return __neverc_string_borrow_copy(s, s.data + start, s.len - start);
}

static string neverc_string_rtrim(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ end = s.len;
  while (end > 0 && __neverc_string_is_whitespace(s.data[end - 1]))
    end--;
  if (end == 0)
    return __neverc_string_consume(s);
  if (end == s.len) {
    if (s.cap)
      return s;
    return __neverc_string_borrow_copy(s, s.data, s.len);
  }
  return __neverc_string_borrow_copy(s, s.data, end);
}

static string neverc_string_trim(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ start = 0;
  while (start < s.len && __neverc_string_is_whitespace(s.data[start]))
    start++;
  __SIZE_TYPE__ end = s.len;
  while (end > start && __neverc_string_is_whitespace(s.data[end - 1]))
    end--;
  if (start >= end)
    return __neverc_string_consume(s);
  if (start == 0 && end == s.len) {
    if (s.cap)
      return s;
    return __neverc_string_borrow_copy(s, s.data, s.len);
  }
  return __neverc_string_borrow_copy(s, s.data + start, end - start);
}

#ifndef NEVERC_STRING_INT_BUF
#define NEVERC_STRING_INT_BUF 24
#endif

#ifndef NEVERC_STRING_INT_BASE_BUF
#define NEVERC_STRING_INT_BASE_BUF 66
#endif

static string neverc_string_from_int(__PTRDIFF_TYPE__ val) {
  char buf[NEVERC_STRING_INT_BUF];
  int neg = val < 0;
  __SIZE_TYPE__ uval;
  if (neg)
    uval = (__SIZE_TYPE__)(-(val + 1)) + 1;
  else
    uval = (__SIZE_TYPE__)val;
  int pos = NEVERC_STRING_INT_BUF - 1;
  buf[pos] = 0;
  do {
    buf[--pos] = (char)('0' + (int)(uval % 10));
    uval /= 10;
  } while (uval);
  if (neg)
    buf[--pos] = '-';
  return __neverc_string_make_owned(
      buf + pos, (__SIZE_TYPE__)(NEVERC_STRING_INT_BUF - 1 - pos));
}

static string neverc_string_from_uint(__SIZE_TYPE__ val) {
  char buf[NEVERC_STRING_INT_BUF];
  int pos = NEVERC_STRING_INT_BUF - 1;
  buf[pos] = 0;
  do {
    buf[--pos] = (char)('0' + (int)(val % 10));
    val /= 10;
  } while (val);
  return __neverc_string_make_owned(
      buf + pos, (__SIZE_TYPE__)(NEVERC_STRING_INT_BUF - 1 - pos));
}

static __inline__ char __neverc_string_base_digit(int n) {
  return n < 10 ? (char)('0' + n) : (char)('a' + (n - 10));
}

static string neverc_string_from_int_base(__PTRDIFF_TYPE__ val,
                                              int base) {
  if (base < 2 || base > 36)
    return __neverc_string_empty();
  char buf[NEVERC_STRING_INT_BASE_BUF];
  int neg = val < 0;
  __SIZE_TYPE__ uval;
  if (neg)
    uval = (__SIZE_TYPE__)(-(val + 1)) + 1;
  else
    uval = (__SIZE_TYPE__)val;
  int pos = NEVERC_STRING_INT_BASE_BUF - 1;
  buf[pos] = 0;
  __SIZE_TYPE__ ubase = (__SIZE_TYPE__)base;
  do {
    buf[--pos] = __neverc_string_base_digit((int)(uval % ubase));
    uval /= ubase;
  } while (uval);
  if (neg)
    buf[--pos] = '-';
  return __neverc_string_make_owned(
      buf + pos,
      (__SIZE_TYPE__)(NEVERC_STRING_INT_BASE_BUF - 1 - pos));
}

static string neverc_string_from_uint_base(__SIZE_TYPE__ val, int base) {
  if (base < 2 || base > 36)
    return __neverc_string_empty();
  char buf[NEVERC_STRING_INT_BASE_BUF];
  int pos = NEVERC_STRING_INT_BASE_BUF - 1;
  buf[pos] = 0;
  __SIZE_TYPE__ ubase = (__SIZE_TYPE__)base;
  do {
    buf[--pos] = __neverc_string_base_digit((int)(val % ubase));
    val /= ubase;
  } while (val);
  return __neverc_string_make_owned(
      buf + pos,
      (__SIZE_TYPE__)(NEVERC_STRING_INT_BASE_BUF - 1 - pos));
}

static __PTRDIFF_TYPE__ neverc_string_to_int(string s) {
  __PTRDIFF_TYPE__ result = 0;
  int neg = 0;
  __SIZE_TYPE__ i = 0;

  if (!__neverc_string_invalid(s) && s.data && s.len > 0) {
    if (s.data[0] == '-') {
      neg = 1;
      i = 1;
    } else if (s.data[0] == '+') {
      i = 1;
    }
    for (; i < s.len; i++) {
      unsigned char d = (unsigned char)((unsigned char)s.data[i] - '0');
      if (d >= 10u)
        break;
      result = result * 10 + (__PTRDIFF_TYPE__)d;
    }
  }
  if (neg)
    result = -result;
  neverc_string_free(s);
  return result;
}

static __SIZE_TYPE__ neverc_string_to_uint(string s) {
  __SIZE_TYPE__ result = 0;
  __SIZE_TYPE__ i = 0;
  if (!__neverc_string_invalid(s) && s.data && s.len > 0) {
    if (s.data[0] == '+')
      i = 1;
    for (; i < s.len; i++) {
      unsigned char d = (unsigned char)((unsigned char)s.data[i] - '0');
      if (d >= 10u)
        break;
      result = result * 10 + (__SIZE_TYPE__)d;
    }
  }
  neverc_string_free(s);
  return result;
}

static unsigned long long neverc_string_hash(string s) {
  unsigned long long h = 0xcbf29ce484222325ULL;
  if (!__neverc_string_invalid(s) && s.data) {
    for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
      h ^= (unsigned long long)(unsigned char)s.data[i];
      h *= 0x100000001b3ULL;
    }
  }
  neverc_string_free(s);
  return h;
}

static string neverc_string_reverse(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  if (s.cap) {
    char *buf = (char *)s.data;
    __SIZE_TYPE__ left = 0, right = s.len - 1;
    while (left < right) {
      char tmp = buf[left];
      buf[left] = buf[right];
      buf[right] = tmp;
      left++;
      right--;
    }
    return s;
  }
  char *p = __neverc_string_alloc_payload(s.len);
  if (!p)
    return __neverc_string_consume(s);
  for (__SIZE_TYPE__ i = 0; i < s.len; i++)
    p[i] = s.data[s.len - 1 - i];
  __SIZE_TYPE__ len = s.len;
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, len);
}

static __SIZE_TYPE__ neverc_string_count_char(string s, char ch) {
  __SIZE_TYPE__ n = 0;
  if (!__neverc_string_invalid(s) && s.data) {
    const char *p = s.data;
    __SIZE_TYPE__ remaining = s.len;
    while (remaining) {
      const char *hit = (const char *)__builtin_memchr(
          p, (unsigned char)ch, remaining);
      if (!hit)
        break;
      n++;
      __SIZE_TYPE__ skip = (__SIZE_TYPE__)(hit - p) + 1;
      p += skip;
      remaining -= skip;
    }
  }
  neverc_string_free(s);
  return n;
}

static string neverc_string_split_first(string s, string sep) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(sep)) {
    __neverc_string_release2(s, sep);
    return __neverc_string_empty();
  }

  if (sep.len == 0) {
    neverc_string_free(sep);
    if (s.cap)
      return s;
    string out = __neverc_string_make_owned(s.data, s.len);
    neverc_string_free(s);
    return out;
  }
  __SIZE_TYPE__ pos =
      __neverc_string_find_raw(s.data, s.len, sep.data, sep.len);
  if (pos == NEVERC_STRING_NPOS) {
    neverc_string_free(sep);
    if (s.cap)
      return s;
    string out = __neverc_string_make_owned(s.data, s.len);
    neverc_string_free(s);
    return out;
  }
  string out = __neverc_string_make_owned(s.data, pos);
  __neverc_string_release2(s, sep);
  return out;
}

static string neverc_string_split_rest(string s, string sep) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(sep)) {
    __neverc_string_release2(s, sep);
    return __neverc_string_empty();
  }
  if (sep.len == 0) {
    neverc_string_free(sep);
    if (s.cap)
      return s;
    string out = __neverc_string_make_owned(s.data, s.len);
    neverc_string_free(s);
    return out;
  }
  __SIZE_TYPE__ pos =
      __neverc_string_find_raw(s.data, s.len, sep.data, sep.len);
  if (pos == NEVERC_STRING_NPOS) {
    __neverc_string_release2(s, sep);
    return __neverc_string_empty();
  }
  __SIZE_TYPE__ start = pos + sep.len;
  string out =
      __neverc_string_make_owned(s.data + start, s.len - start);
  __neverc_string_release2(s, sep);
  return out;
}

static string neverc_string_split_before_last(string s,
                                                          string sep) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(sep)) {
    __neverc_string_release2(s, sep);
    return __neverc_string_empty();
  }
  __SIZE_TYPE__ pos =
      __neverc_string_rfind_raw(s.data, s.len, sep.data, sep.len);
  if (pos == NEVERC_STRING_NPOS) {
    neverc_string_free(sep);
    if (s.cap)
      return s;
    string out = __neverc_string_make_owned(s.data, s.len);
    neverc_string_free(s);
    return out;
  }
  string out = __neverc_string_make_owned(s.data, pos);
  __neverc_string_release2(s, sep);
  return out;
}

static string neverc_string_split_after_last(string s, string sep) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(sep)) {
    __neverc_string_release2(s, sep);
    return __neverc_string_empty();
  }
  if (sep.len == 0) {
    neverc_string_free(sep);
    if (s.cap)
      return s;
    string out = __neverc_string_make_owned(s.data, s.len);
    neverc_string_free(s);
    return out;
  }
  __SIZE_TYPE__ pos =
      __neverc_string_rfind_raw(s.data, s.len, sep.data, sep.len);
  if (pos == NEVERC_STRING_NPOS) {
    __neverc_string_release2(s, sep);
    return __neverc_string_empty();
  }
  __SIZE_TYPE__ start = pos + sep.len;
  string out =
      __neverc_string_make_owned(s.data + start, s.len - start);
  __neverc_string_release2(s, sep);
  return out;
}

static string neverc_string_join(const string *items,
                                             __SIZE_TYPE__ count, string sep) {
  if (__neverc_string_invalid(sep))
    return __neverc_string_consume(sep);
  if (!items || count == 0)
    return __neverc_string_consume(sep);
  __SIZE_TYPE__ total = 0;
  for (__SIZE_TYPE__ i = 0; i < count; i++) {
    if (__neverc_string_invalid(items[i]))
      return __neverc_string_consume(sep);
    if (total > NEVERC_STRING_MAX_LEN - items[i].len)
      return __neverc_string_consume(sep);
    total += items[i].len;
  }
  if (count > 1 && sep.len > 0) {
    __SIZE_TYPE__ gap = count - 1;
    if (sep.len > NEVERC_STRING_MAX_LEN / gap)
      return __neverc_string_consume(sep);
    __SIZE_TYPE__ sep_total = gap * sep.len;
    if (total > NEVERC_STRING_MAX_LEN - sep_total)
      return __neverc_string_consume(sep);
    total += sep_total;
  }
  if (total == 0)
    return __neverc_string_consume(sep);
  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(sep);
  __SIZE_TYPE__ written = 0;
  for (__SIZE_TYPE__ i = 0; i < count; i++) {
    if (i > 0 && sep.len) {
      __builtin_memcpy(p + written, sep.data, sep.len);
      written += sep.len;
    }
    if (items[i].len) {
      __builtin_memcpy(p + written, items[i].data, items[i].len);
      written += items[i].len;
    }
  }
  neverc_string_free(sep);
  return __neverc_string_finalize_owned(p, total);
}

static void neverc_string_split(string s, string sep,
                                            string **out_items,
                                            __SIZE_TYPE__ *out_count) {
  if (out_items)
    *out_items = (string *)0;
  if (out_count)
    *out_count = 0;
  if (!out_items || !out_count) {
    __neverc_string_release2(s, sep);
    return;
  }
  if (__neverc_string_invalid(s) || __neverc_string_invalid(sep)) {
    __neverc_string_release2(s, sep);
    return;
  }

  if (sep.len == 0 || sep.len > s.len) {
    string *items = (string *)NEVERC_STRING_ALLOC(sizeof(string));
    if (!items) {
      __neverc_string_release2(s, sep);
      return;
    }
    items[0] = __neverc_string_make_owned(s.data, s.len);
    *out_items = items;
    *out_count = 1;
    __neverc_string_release2(s, sep);
    return;
  }

  __SIZE_TYPE__ matches = 0;
  {
    __SIZE_TYPE__ pos = 0;
    for (;;) {
      __SIZE_TYPE__ hit = __neverc_string_find_raw_from(
          s.data, s.len, sep.data, sep.len, pos);
      if (hit == NEVERC_STRING_NPOS)
        break;
      matches++;
      pos = hit + sep.len;
    }
  }
  __SIZE_TYPE__ pieces = matches + 1;

  if (pieces > NEVERC_STRING_MAX_LEN / sizeof(string)) {
    __neverc_string_release2(s, sep);
    return;
  }
  string *items =
      (string *)NEVERC_STRING_ALLOC(pieces * sizeof(string));
  if (!items) {
    __neverc_string_release2(s, sep);
    return;
  }

  for (__SIZE_TYPE__ k = 0; k < pieces; k++)
    items[k] = __neverc_string_empty();

  __SIZE_TYPE__ idx = 0;
  __SIZE_TYPE__ start = 0;
  for (;;) {
    __SIZE_TYPE__ hit = __neverc_string_find_raw_from(
        s.data, s.len, sep.data, sep.len, start);
    if (hit == NEVERC_STRING_NPOS)
      break;
    string piece = __neverc_string_make_owned(s.data + start, hit - start);
    if ((hit - start) > 0 && piece.cap == 0) {
      for (__SIZE_TYPE__ k = 0; k < idx; k++)
        neverc_string_free(items[k]);
      NEVERC_STRING_FREE((void *)items);
      __neverc_string_release2(s, sep);
      return;
    }
    items[idx++] = piece;
    start = hit + sep.len;
  }

  {
    string piece =
        __neverc_string_make_owned(s.data + start, s.len - start);
    if ((s.len - start) > 0 && piece.cap == 0) {
      for (__SIZE_TYPE__ k = 0; k < idx; k++)
        neverc_string_free(items[k]);
      NEVERC_STRING_FREE((void *)items);
      __neverc_string_release2(s, sep);
      return;
    }
    items[idx++] = piece;
  }

  *out_items = items;
  *out_count = idx;
  __neverc_string_release2(s, sep);
}

static void neverc_string_split_free(string *items,
                                                __SIZE_TYPE__ count) {
  if (!items)
    return;
  for (__SIZE_TYPE__ i = 0; i < count; i++) {
    neverc_string_free(items[i]);
    items[i] = __neverc_string_empty();
  }
  NEVERC_STRING_FREE((void *)items);
}

static __SIZE_TYPE__ neverc_string_count(string s, string needle) {
  __SIZE_TYPE__ n = 0;
  if (__neverc_string_invalid(s) || __neverc_string_invalid(needle)) {
    __neverc_string_release2(s, needle);
    return 0;
  }
  if (needle.len == 0) {
    n = s.len + 1;
    __neverc_string_release2(s, needle);
    return n;
  }
  {
    __SIZE_TYPE__ pos = 0;
    for (;;) {
      __SIZE_TYPE__ hit = __neverc_string_find_raw_from(
          s.data, s.len, needle.data, needle.len, pos);
      if (hit == NEVERC_STRING_NPOS)
        break;
      n++;
      pos = hit + needle.len;
    }
  }
  __neverc_string_release2(s, needle);
  return n;
})prelude"
