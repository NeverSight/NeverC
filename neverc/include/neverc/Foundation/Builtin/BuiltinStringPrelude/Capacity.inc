R"prelude(

static string neverc_string_reserve(string s, __SIZE_TYPE__ cap) {
  if (__neverc_string_invalid(s))
    return __neverc_string_consume(s);
  if (cap < s.len)
    cap = s.len;
  if (cap > NEVERC_STRING_MAX_LEN)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ alloc_cap = cap + 1;
  if (s.cap >= alloc_cap)
    return s;
  string copy =
      __neverc_string_make_owned_with_cap(s.data, s.len, alloc_cap);
  neverc_string_free(s);
  return copy;
}

static string neverc_string_shrink_to_fit(string s) {
  if (__neverc_string_invalid(s))
    return __neverc_string_consume(s);
  if (s.cap == s.len + 1)
    return s;
  return __neverc_string_borrow_copy(s, s.data, s.len);
}

static __SIZE_TYPE__ neverc_string_max_size(string s) {
  neverc_string_free(s);
  return NEVERC_STRING_MAX_LEN;
}

static __SIZE_TYPE__ neverc_string_capacity(const string *s) {
  if (!s)
    return 0;
  return s->cap > 0 ? s->cap - 1 : s->len;
})prelude"
