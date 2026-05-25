R"prelude(
static __inline__ string neverc_string_view(const char *data, __SIZE_TYPE__ len) {
  return data ? __neverc_string_make_view(data, len) : __neverc_string_empty();
}

static __inline__ __SIZE_TYPE__ neverc_string_len(string s) {
  __SIZE_TYPE__ len = s.len;
  neverc_string_free(s);
  return len;
}

static __inline__ int neverc_string_empty(string s) {
  int empty = s.len == 0;
  neverc_string_free(s);
  return empty;
}

static __inline__ char neverc_string_at(string s, __SIZE_TYPE__ pos) {
  char ch = 0;
  if (!__neverc_string_invalid(s) && pos < s.len && s.data)
    ch = s.data[pos];
  neverc_string_free(s);
  return ch;
}

static __inline__ char neverc_string_front(string s) {
  char ch = 0;
  if (!__neverc_string_invalid(s) && s.len && s.data)
    ch = s.data[0];
  neverc_string_free(s);
  return ch;
}

static __inline__ char neverc_string_back(string s) {
  char ch = 0;
  if (!__neverc_string_invalid(s) && s.len && s.data)
    ch = s.data[s.len - 1];
  neverc_string_free(s);
  return ch;
})prelude"
