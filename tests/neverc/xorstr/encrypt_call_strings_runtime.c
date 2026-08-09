static int matched;

static __attribute__((noinline)) void consume(const char *value) {
  matched = value[0] == 'h' && value[1] == 'e' && value[2] == 'l' &&
            value[3] == 'l' && value[4] == 'o' && value[5] == ' ' &&
            value[6] == 'a' && value[7] == 'u' && value[8] == 't' &&
            value[9] == 'o' && value[10] == 0;
}

int main(void) {
  consume("hello auto");
  return matched ? 0 : 1;
}
