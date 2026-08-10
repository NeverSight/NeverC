extern const char *const xorstr_lto_selected;
extern int xorstr_lto_consume(const char *value);

__attribute__((noinline)) int xorstr_lto_run(void) {
  return xorstr_lto_consume(xorstr_lto_selected);
}
