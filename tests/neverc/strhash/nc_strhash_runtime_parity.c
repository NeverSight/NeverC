#include <neverc/strhash/strhash.h>

#include <stdint.h>

int main(void) {
  static const char binary[] = {'a', '\0', 'b'};

  if (NC_STRHASH("") != neverc_strhash_rt("", 0))
    return 1;
  if (NC_STRHASH("hello") != neverc_strhash_rt("hello", 5))
    return 2;
  if (NC_STRHASH("a\0b") != neverc_strhash_rt(binary, sizeof(binary)))
    return 3;
  return 0;
}
