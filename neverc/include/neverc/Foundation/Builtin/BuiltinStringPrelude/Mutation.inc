R"prelude(

static string neverc_string_append(string s, string other) {
  return __neverc_string_cat(s, other);
}

static string neverc_string_append_char(string s, __SIZE_TYPE__ count,
                                             char ch) {
  if (__neverc_string_invalid(s))
    return __neverc_string_consume(s);
  if (count == 0)
    return __neverc_string_own_or_copy(s);
  if (s.len > NEVERC_STRING_MAX_LEN - count)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ new_len = s.len + count;
  if (s.cap > new_len) {
    char *buf = (char *)s.data;
    __builtin_memset(buf + s.len, (unsigned char)ch, count);
    buf[new_len] = 0;
    return __neverc_string_make_owned_handle(buf, new_len, s.cap);
  }
  char *p = __neverc_string_alloc_payload(new_len);
  if (!p)
    return __neverc_string_consume(s);
  if (s.len)
    __builtin_memcpy(p, s.data, s.len);
  __builtin_memset(p + s.len, (unsigned char)ch, count);
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, new_len);
}

static string neverc_string_push_back(string s, char ch) {
  if (__neverc_string_invalid(s))
    return __neverc_string_consume(s);
  if (s.len > NEVERC_STRING_MAX_LEN - 1)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ new_len = s.len + 1;
  if (s.cap > new_len) {
    char *buf = (char *)s.data;
    buf[s.len] = ch;
    buf[new_len] = 0;
    return __neverc_string_make_owned_handle(buf, new_len, s.cap);
  }
  char *p = __neverc_string_alloc_payload(new_len);
  if (!p)
    return __neverc_string_consume(s);
  if (s.len)
    __builtin_memcpy(p, s.data, s.len);
  p[s.len] = ch;
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, new_len);
}

static string neverc_string_pop_back(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ new_len = s.len - 1;
  if (new_len == 0)
    return __neverc_string_consume(s);
  if (s.cap) {
    char *buf = (char *)s.data;
    buf[new_len] = 0;
    return __neverc_string_make_owned_handle(buf, new_len, s.cap);
  }
  return __neverc_string_borrow_copy(s, s.data, new_len);
}

static string neverc_string_insert_char(string s, __SIZE_TYPE__ pos,
                                            __SIZE_TYPE__ count, char ch) {
  if (__neverc_string_invalid(s))
    return __neverc_string_consume(s);
  if (pos > s.len)
    pos = s.len;
  if (count == 0)
    return __neverc_string_own_or_copy(s);
  if (s.len > NEVERC_STRING_MAX_LEN - count)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ new_len = s.len + count;
  if (s.cap > new_len) {
    char *buf = (char *)s.data;
    if (pos < s.len)
      __builtin_memmove(buf + pos + count, buf + pos, s.len - pos);
    __builtin_memset(buf + pos, (unsigned char)ch, count);
    buf[new_len] = 0;
    return __neverc_string_make_owned_handle(buf, new_len, s.cap);
  }
  char *p = __neverc_string_alloc_payload(new_len);
  if (!p)
    return __neverc_string_consume(s);
  if (pos)
    __builtin_memcpy(p, s.data, pos);
  __builtin_memset(p + pos, (unsigned char)ch, count);
  if (pos < s.len)
    __builtin_memcpy(p + pos + count, s.data + pos, s.len - pos);
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, new_len);
}

static string neverc_string_insert(string s, __SIZE_TYPE__ pos,
                                       string other) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(other))
    return __neverc_string_consume2(s, other);
  if (pos > s.len)
    pos = s.len;
  if (other.len == 0) {
    neverc_string_free(other);
    return __neverc_string_own_or_copy(s);
  }
  if (s.len > NEVERC_STRING_MAX_LEN - other.len)
    return __neverc_string_consume2(s, other);
  __SIZE_TYPE__ new_len = s.len + other.len;
  if (s.cap > new_len) {
    char *buf = (char *)s.data;
    if (pos < s.len)
      __builtin_memmove(buf + pos + other.len, buf + pos, s.len - pos);
    __builtin_memmove(buf + pos, other.data, other.len);
    buf[new_len] = 0;
    neverc_string_free(other);
    return __neverc_string_make_owned_handle(buf, new_len, s.cap);
  }
  char *p = __neverc_string_alloc_payload(new_len);
  if (!p)
    return __neverc_string_consume2(s, other);
  if (pos)
    __builtin_memcpy(p, s.data, pos);
  if (other.len)
    __builtin_memcpy(p + pos, other.data, other.len);
  if (pos < s.len)
    __builtin_memcpy(p + pos + other.len, s.data + pos, s.len - pos);
  __neverc_string_release2(s, other);
  return __neverc_string_finalize_owned(p, new_len);
}

static string neverc_string_erase(string s, __SIZE_TYPE__ pos,
                                      __SIZE_TYPE__ count) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);
  if (pos >= s.len)
    return __neverc_string_own_or_copy(s);
  __SIZE_TYPE__ remaining = s.len - pos;
  if (count > remaining)
    count = remaining;
  __SIZE_TYPE__ new_len = s.len - count;
  if (new_len == 0)
    return __neverc_string_consume(s);
  if (s.cap) {
    char *buf = (char *)s.data;
    if (pos + count < s.len)
      __builtin_memmove(buf + pos, buf + pos + count, s.len - pos - count);
    buf[new_len] = 0;
    return __neverc_string_make_owned_handle(buf, new_len, s.cap);
  }
  char *p = __neverc_string_alloc_payload(new_len);
  if (!p)
    return __neverc_string_consume(s);
  if (pos)
    __builtin_memcpy(p, s.data, pos);
  if (pos + count < s.len)
    __builtin_memcpy(p + pos, s.data + pos + count, s.len - pos - count);
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, new_len);
}

static string neverc_string_replace(string s, __SIZE_TYPE__ pos,
                                        __SIZE_TYPE__ count, string other) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(other))
    return __neverc_string_consume2(s, other);
  if (pos > s.len)
    pos = s.len;
  __SIZE_TYPE__ avail = s.len - pos;
  if (count > avail)
    count = avail;
  __SIZE_TYPE__ base = s.len - count;
  if (base > NEVERC_STRING_MAX_LEN - other.len)
    return __neverc_string_consume2(s, other);
  __SIZE_TYPE__ new_len = base + other.len;
  if (new_len == 0)
    return __neverc_string_consume2(s, other);
  if (s.cap && other.len == count) {
    char *buf = (char *)s.data;
    if (other.len)
      __builtin_memmove(buf + pos, other.data, other.len);
    neverc_string_free(other);
    return s;
  }
  if (s.cap && s.cap > new_len) {
    char *buf = (char *)s.data;
    __SIZE_TYPE__ tail = pos + count;
    if (tail < s.len)
      __builtin_memmove(buf + pos + other.len, buf + tail, s.len - tail);
    if (other.len)
      __builtin_memcpy(buf + pos, other.data, other.len);
    buf[new_len] = 0;
    neverc_string_free(other);
    return __neverc_string_make_owned_handle(buf, new_len, s.cap);
  }
  char *p = __neverc_string_alloc_payload(new_len);
  if (!p)
    return __neverc_string_consume2(s, other);
  if (pos)
    __builtin_memcpy(p, s.data, pos);
  if (other.len)
    __builtin_memcpy(p + pos, other.data, other.len);
  __SIZE_TYPE__ tail = pos + count;
  if (tail < s.len)
    __builtin_memcpy(p + pos + other.len, s.data + tail, s.len - tail);
  __neverc_string_release2(s, other);
  return __neverc_string_finalize_owned(p, new_len);
}

static string neverc_string_replace_char(string s, __SIZE_TYPE__ pos,
                                              __SIZE_TYPE__ count,
                                              __SIZE_TYPE__ count2, char ch) {
  if (__neverc_string_invalid(s))
    return __neverc_string_consume(s);
  if (pos > s.len)
    pos = s.len;
  __SIZE_TYPE__ avail = s.len - pos;
  if (count > avail)
    count = avail;
  __SIZE_TYPE__ base = s.len - count;
  if (base > NEVERC_STRING_MAX_LEN - count2)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ new_len = base + count2;
  if (new_len == 0)
    return __neverc_string_consume(s);
  if (s.cap && count2 == count) {
    char *buf = (char *)s.data;
    if (count2)
      __builtin_memset(buf + pos, (unsigned char)ch, count2);
    return s;
  }
  if (s.cap && s.cap > new_len) {
    char *buf = (char *)s.data;
    __SIZE_TYPE__ tail = pos + count;
    if (tail < s.len)
      __builtin_memmove(buf + pos + count2, buf + tail, s.len - tail);
    if (count2)
      __builtin_memset(buf + pos, (unsigned char)ch, count2);
    buf[new_len] = 0;
    return __neverc_string_make_owned_handle(buf, new_len, s.cap);
  }
  char *p = __neverc_string_alloc_payload(new_len);
  if (!p)
    return __neverc_string_consume(s);
  if (pos)
    __builtin_memcpy(p, s.data, pos);
  if (count2)
    __builtin_memset(p + pos, (unsigned char)ch, count2);
  __SIZE_TYPE__ tail = pos + count;
  if (tail < s.len)
    __builtin_memcpy(p + pos + count2, s.data + tail, s.len - tail);
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, new_len);
}

static string neverc_string_clear(string s) {

  return __neverc_string_consume(s);
}

static void neverc_string_swap(string *a, string *b) {
  if (!a || !b)
    return;
  string tmp = *a;
  *a = *b;
  *b = tmp;
}

static string neverc_string_resize(string s, __SIZE_TYPE__ n, char fill) {
  if (__neverc_string_invalid(s) || n == 0)
    return __neverc_string_consume(s);
  if (s.cap) {
    if (n <= s.len) {
      char *buf = (char *)s.data;
      buf[n] = 0;
      return __neverc_string_make_owned_handle(buf, n, s.cap);
    }
    if (s.cap > n) {
      char *buf = (char *)s.data;
      __builtin_memset(buf + s.len, (unsigned char)fill, n - s.len);
      buf[n] = 0;
      return __neverc_string_make_owned_handle(buf, n, s.cap);
    }
  }
  char *p = __neverc_string_alloc_payload(n);
  if (!p)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ keep = s.len < n ? s.len : n;
  if (keep)
    __builtin_memcpy(p, s.data, keep);
  if (n > keep)
    __builtin_memset(p + keep, (unsigned char)fill, n - keep);
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, n);
}

static string neverc_string_pad_left(string s, __SIZE_TYPE__ width,
                                          char ch) {
  if (__neverc_string_invalid(s))
    return __neverc_string_consume(s);
  if (width <= s.len)
    return __neverc_string_own_or_copy(s);
  if (width > NEVERC_STRING_MAX_LEN)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ pad = width - s.len;
  if (s.cap > width) {
    char *buf = (char *)s.data;
    __builtin_memmove(buf + pad, buf, s.len);
    __builtin_memset(buf, (unsigned char)ch, pad);
    buf[width] = 0;
    return __neverc_string_make_owned_handle(buf, width, s.cap);
  }
  char *p = __neverc_string_alloc_payload(width);
  if (!p)
    return __neverc_string_consume(s);
  __builtin_memset(p, (unsigned char)ch, pad);
  if (s.len)
    __builtin_memcpy(p + pad, s.data, s.len);
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, width);
}

static string neverc_string_pad_right(string s, __SIZE_TYPE__ width,
                                           char ch) {
  if (__neverc_string_invalid(s))
    return __neverc_string_consume(s);
  if (width <= s.len)
    return __neverc_string_own_or_copy(s);
  if (width > NEVERC_STRING_MAX_LEN)
    return __neverc_string_consume(s);
  if (s.cap > width) {
    char *buf = (char *)s.data;
    __builtin_memset(buf + s.len, (unsigned char)ch, width - s.len);
    buf[width] = 0;
    return __neverc_string_make_owned_handle(buf, width, s.cap);
  }
  char *p = __neverc_string_alloc_payload(width);
  if (!p)
    return __neverc_string_consume(s);
  if (s.len)
    __builtin_memcpy(p, s.data, s.len);
  __builtin_memset(p + s.len, (unsigned char)ch, width - s.len);
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, width);
}

static string neverc_string_replace_all(string s, string from,
                                             string to) {
  if (__neverc_string_invalid(s) || __neverc_string_invalid(from) ||
      __neverc_string_invalid(to)) {
    __neverc_string_release2(from, to);
    return __neverc_string_consume(s);
  }
  if (from.len == 0 || from.len > s.len) {
    __neverc_string_release2(from, to);
    return __neverc_string_own_or_copy(s);
  }

  __SIZE_TYPE__ matches = 0;
  {
    __SIZE_TYPE__ pos = 0;
    for (;;) {
      __SIZE_TYPE__ hit = __neverc_string_find_raw_from(
          s.data, s.len, from.data, from.len, pos);
      if (hit == NEVERC_STRING_NPOS)
        break;
      matches++;
      pos = hit + from.len;
    }
  }
  if (matches == 0) {
    __neverc_string_release2(from, to);
    return __neverc_string_own_or_copy(s);
  }

  if (from.len == to.len && s.cap) {
    char *buf = (char *)s.data;
    __SIZE_TYPE__ pos = 0;
    for (;;) {
      __SIZE_TYPE__ hit = __neverc_string_find_raw_from(
          buf, s.len, from.data, from.len, pos);
      if (hit == NEVERC_STRING_NPOS)
        break;
      if (to.len)
        __builtin_memmove(buf + hit, to.data, to.len);
      pos = hit + from.len;
    }
    __neverc_string_release2(from, to);
    return s;
  }

  __SIZE_TYPE__ grow_per = (to.len > from.len) ? (to.len - from.len) : 0;
  __SIZE_TYPE__ shrink_per = (to.len < from.len) ? (from.len - to.len) : 0;
  int overflow = 0;
  __SIZE_TYPE__ grow = 0;
  if (grow_per) {
    if (matches > NEVERC_STRING_MAX_LEN / grow_per)
      overflow = 1;
    else
      grow = matches * grow_per;
    if (!overflow && s.len > NEVERC_STRING_MAX_LEN - grow)
      overflow = 1;
  }
  if (overflow) {
    neverc_string_free(s);
    __neverc_string_release2(from, to);
    return __neverc_string_empty();
  }
  __SIZE_TYPE__ shrink = matches * shrink_per;
  __SIZE_TYPE__ new_len = s.len + grow - shrink;
  if (new_len == 0) {
    neverc_string_free(s);
    __neverc_string_release2(from, to);
    return __neverc_string_empty();
  }

  char *p = __neverc_string_alloc_payload(new_len);
  if (!p) {
    neverc_string_free(s);
    __neverc_string_release2(from, to);
    return __neverc_string_empty();
  }

  __SIZE_TYPE__ src = 0;
  __SIZE_TYPE__ dst = 0;
  for (;;) {
    __SIZE_TYPE__ hit = __neverc_string_find_raw_from(
        s.data, s.len, from.data, from.len, src);
    if (hit == NEVERC_STRING_NPOS)
      break;
    if (hit > src) {
      __builtin_memcpy(p + dst, s.data + src, hit - src);
      dst += hit - src;
    }
    if (to.len)
      __builtin_memcpy(p + dst, to.data, to.len);
    dst += to.len;
    src = hit + from.len;
  }
  if (src < s.len) {
    __builtin_memcpy(p + dst, s.data + src, s.len - src);
    dst += s.len - src;
  }
  neverc_string_free(s);
  __neverc_string_release2(from, to);
  return __neverc_string_finalize_owned(p, new_len);
})prelude"
