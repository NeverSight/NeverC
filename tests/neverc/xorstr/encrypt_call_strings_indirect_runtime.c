typedef int (*checker_fn)(const char *);

__attribute__((noinline)) int accepts_indirect_secret(const char *value) {
  return value[0] == 'i' && value[1] == 'n' && value[2] == 'd' &&
         value[3] == 'i' && value[4] == 'r' && value[5] == 'e' &&
         value[6] == 'c' && value[7] == 't' && value[8] == '-' &&
         value[9] == 's' && value[10] == 'e' && value[11] == 'c' &&
         value[12] == 'r' && value[13] == 'e' && value[14] == 't' &&
         value[15] == '\0';
}

static checker_fn volatile checker = accepts_indirect_secret;

int main(void) { return checker("indirect-secret") ? 0 : 1; }
