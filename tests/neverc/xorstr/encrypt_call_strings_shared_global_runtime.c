static volatile unsigned call_count;
static const char shared_literal[] = "shared";

__attribute__((noinline)) int accepts_shared(const char *value) {
  ++call_count;
  return value[0] == 's' && value[1] == 'h' && value[2] == 'a' &&
         value[3] == 'r' && value[4] == 'e' && value[5] == 'd' &&
         value[6] == '\0';
}

int main(void) {
  if (!accepts_shared(shared_literal))
    return 1;
  if (!accepts_shared(shared_literal))
    return 2;
  return call_count == 2 ? 0 : 3;
}
