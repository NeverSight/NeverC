#include "neverc/std/hash/fnv.h"

#include <stdio.h>
#include <string.h>

static int equal256(neverc_fnv_256_t value, uint64_t w0, uint64_t w1,
                    uint64_t w2, uint64_t w3) {
  return value.words[0] == w0 && value.words[1] == w1 && value.words[2] == w2 &&
         value.words[3] == w3;
}

static int equal512(neverc_fnv_512_t value, const uint64_t expected[8]) {
  for (size_t i = 0; i < 8; ++i) {
    if (value.words[i] != expected[i])
      return 0;
  }
  return 1;
}

static int equal1024(neverc_fnv_1024_t value, const uint64_t expected[16]) {
  for (size_t i = 0; i < 16; ++i) {
    if (value.words[i] != expected[i])
      return 0;
  }
  return 1;
}

static int check_words(const char *name, const uint64_t *actual,
                       const uint64_t *expected, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (actual[i] != expected[i]) {
      printf("FAIL: %s word %zu: got 0x%016llx, "
             "expected 0x%016llx\n",
             name, i, (unsigned long long)actual[i],
             (unsigned long long)expected[i]);
      return 1;
    }
  }
  return 0;
}

static int test_additional_vectors(void) {
  static const uint64_t fnv1_256_foobar[4] = {
      UINT64_C(0xb055ea2f2cc3908d), UINT64_C(0xddb794c02d3889dc),
      UINT64_C(0x32453dad5ae35b75), UINT64_C(0x3ac86c6c2ac80d72)};
  static const uint64_t fnv1a_256_fox[4] = {
      UINT64_C(0xde8de01f19056b20), UINT64_C(0xfd89451c1046c678),
      UINT64_C(0x01f99f71264e4fff), UINT64_C(0x078e67f490022ab0)};
  static const uint64_t fnv1a_256_binary[4] = {
      UINT64_C(0xf4f7a1c2efd0e1e4), UINT64_C(0xbb19e34525c0721a),
      UINT64_C(0x06dd328fa3d7a914), UINT64_C(0x39a07343501cf4f4)};
  static const uint64_t fnv1_512_foobar[8] = {
      UINT64_C(0xb0ec738d9c6fd969), UINT64_C(0xd05f0b35f6c0effd),
      UINT64_C(0x2020946529000000), UINT64_C(0x4bf99f58ee4196af),
      UINT64_C(0xb9700e20110830fe), UINT64_C(0xa5396b76280e47fd),
      UINT64_C(0x022b6e81331ca1a9), UINT64_C(0xcf6faf7123c3fc56)};
  static const uint64_t fnv1a_512_fox[8] = {
      UINT64_C(0xcd74f564c0520cbd), UINT64_C(0x816ee4a6a9efebce),
      UINT64_C(0x4865dd1c4152d79e), UINT64_C(0xcbb3acbb2e55d860),
      UINT64_C(0xbc17f0b55ae46865), UINT64_C(0x37b02bced854f29c),
      UINT64_C(0x3cc8785e72d03024), UINT64_C(0x9267163b61128df8)};
  static const uint64_t fnv1a_512_binary[8] = {
      UINT64_C(0x7317dfed6c70dfec), UINT64_C(0x6adfced2a5e04d7e),
      UINT64_C(0xec744e3ce9000000), UINT64_C(0x0000000017933d7a),
      UINT64_C(0xf45d70def423a316), UINT64_C(0xf14117df272cd0fd),
      UINT64_C(0x6b85f0f7c9bf6c51), UINT64_C(0x96b3160d02975f38)};
  static const uint64_t fnv1_1024_foobar[16] = {
      UINT64_C(0x00000631175fa7ae), UINT64_C(0x643ad08723d312c9),
      UINT64_C(0xfd024adb91f77f6b), UINT64_C(0x19587197a22bcdf2),
      UINT64_C(0x3727166c3e596993), UINT64_C(0xcf5a8d0000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000042), UINT64_C(0x70d11ef418ef08b8),
      UINT64_C(0xa49e1e825e547eb3), UINT64_C(0x9937f819222f3b7f),
      UINT64_C(0xc92a0e4707900888), UINT64_C(0x82a53ca30e08f65c)};
  static const uint64_t fnv1a_1024_fox[16] = {
      UINT64_C(0x0480a708ee10a221), UINT64_C(0x7801aad08aa2c906),
      UINT64_C(0xaca079f6094db158), UINT64_C(0x0606e97a3f6e9845),
      UINT64_C(0x98ccdba1e41d93a0), UINT64_C(0xed3a40000000002c),
      UINT64_C(0x78c4ffbd2756340f), UINT64_C(0x2832a6ab43a50f84),
      UINT64_C(0x596efc9c7ce1ea25), UINT64_C(0xed87cfae6f961834),
      UINT64_C(0x6863feabd80d9e02), UINT64_C(0x5589dc2c79905a9f),
      UINT64_C(0x4a72e8adca100c3d), UINT64_C(0x2adf5090e1da9093),
      UINT64_C(0x2a039a0112125dc4), UINT64_C(0x4de5ba7fe1234040)};
  static const uint64_t fnv1a_1024_binary[16] = {
      UINT64_C(0x00000000000000f4), UINT64_C(0x6ef41cd23a4dcdd4),
      UINT64_C(0x06834963b78e8224), UINT64_C(0x1a6f5cb06f403cbd),
      UINT64_C(0x5a7c8903cef6a5f4), UINT64_C(0xfdd2950000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000b7cd7fb20),
      UINT64_C(0xc3631dc8903952e9), UINT64_C(0xeeb7f618698f4c87),
      UINT64_C(0xda23ad74b2c5f6f1), UINT64_C(0xfec4a64b546618a2)};
  static const char fox[] = "The quick brown fox jumps over the lazy dog";
  static const uint8_t binary[] = {'a', 0};
  int failed = 0;
  neverc_fnv_256_t h256 = neverc_fnv_sum256("foobar", 6);
  failed |= check_words("fnv1-256(foobar)", h256.words, fnv1_256_foobar, 4);
  h256 = neverc_fnv_sum256a(fox, sizeof(fox) - 1);
  failed |= check_words("fnv1a-256(fox)", h256.words, fnv1a_256_fox, 4);
  h256 = neverc_fnv_sum256a(binary, sizeof(binary));
  failed |= check_words("fnv1a-256(binary)", h256.words, fnv1a_256_binary, 4);

  neverc_fnv_512_t h512 = neverc_fnv_sum512("foobar", 6);
  failed |= check_words("fnv1-512(foobar)", h512.words, fnv1_512_foobar, 8);
  h512 = neverc_fnv_sum512a(fox, sizeof(fox) - 1);
  failed |= check_words("fnv1a-512(fox)", h512.words, fnv1a_512_fox, 8);
  h512 = neverc_fnv_sum512a(binary, sizeof(binary));
  failed |= check_words("fnv1a-512(binary)", h512.words, fnv1a_512_binary, 8);

  neverc_fnv_1024_t h1024 = neverc_fnv_sum1024("foobar", 6);
  failed |= check_words("fnv1-1024(foobar)", h1024.words, fnv1_1024_foobar, 16);
  h1024 = neverc_fnv_sum1024a(fox, sizeof(fox) - 1);
  failed |= check_words("fnv1a-1024(fox)", h1024.words, fnv1a_1024_fox, 16);
  h1024 = neverc_fnv_sum1024a(binary, sizeof(binary));
  failed |=
      check_words("fnv1a-1024(binary)", h1024.words, fnv1a_1024_binary, 16);
  return failed;
}

static int test_incremental_and_null(void) {
  static const uint8_t data[] = {'f', 'o', 'o', 0, 'b', 'a', 'r', 0xff, 0x80,
                                 1,   2,   3,   4, 5,   6,   7,   8};
  int failed = 0;

  neverc_fnv_256_t h256 = NEVERC_FNV256_OFFSET_BASIS_INITIALIZER;
  h256 = neverc_fnv_update256(h256, data, 3);
  h256 = neverc_fnv_update256(h256, data + 3, 5);
  h256 = neverc_fnv_update256(h256, data + 8, sizeof(data) - 8);
  neverc_fnv_256_t full256 = neverc_fnv_sum256(data, sizeof(data));
  failed |= check_words("fnv1-256 incremental", h256.words, full256.words, 4);

  h256 = (neverc_fnv_256_t)NEVERC_FNV256_OFFSET_BASIS_INITIALIZER;
  h256 = neverc_fnv_update256a(h256, data, 3);
  h256 = neverc_fnv_update256a(h256, data + 3, 5);
  h256 = neverc_fnv_update256a(h256, data + 8, sizeof(data) - 8);
  full256 = neverc_fnv_sum256a(data, sizeof(data));
  failed |= check_words("fnv1a-256 incremental", h256.words, full256.words, 4);

  neverc_fnv_512_t h512 = NEVERC_FNV512_OFFSET_BASIS_INITIALIZER;
  h512 = neverc_fnv_update512(h512, data, 3);
  h512 = neverc_fnv_update512(h512, data + 3, 5);
  h512 = neverc_fnv_update512(h512, data + 8, sizeof(data) - 8);
  neverc_fnv_512_t full512 = neverc_fnv_sum512(data, sizeof(data));
  failed |= check_words("fnv1-512 incremental", h512.words, full512.words, 8);

  h512 = (neverc_fnv_512_t)NEVERC_FNV512_OFFSET_BASIS_INITIALIZER;
  h512 = neverc_fnv_update512a(h512, data, 3);
  h512 = neverc_fnv_update512a(h512, data + 3, 5);
  h512 = neverc_fnv_update512a(h512, data + 8, sizeof(data) - 8);
  full512 = neverc_fnv_sum512a(data, sizeof(data));
  failed |= check_words("fnv1a-512 incremental", h512.words, full512.words, 8);

  neverc_fnv_1024_t h1024 = NEVERC_FNV1024_OFFSET_BASIS_INITIALIZER;
  h1024 = neverc_fnv_update1024(h1024, data, 3);
  h1024 = neverc_fnv_update1024(h1024, data + 3, 5);
  h1024 = neverc_fnv_update1024(h1024, data + 8, sizeof(data) - 8);
  neverc_fnv_1024_t full1024 = neverc_fnv_sum1024(data, sizeof(data));
  failed |=
      check_words("fnv1-1024 incremental", h1024.words, full1024.words, 16);

  h1024 = (neverc_fnv_1024_t)NEVERC_FNV1024_OFFSET_BASIS_INITIALIZER;
  h1024 = neverc_fnv_update1024a(h1024, data, 3);
  h1024 = neverc_fnv_update1024a(h1024, data + 3, 5);
  h1024 = neverc_fnv_update1024a(h1024, data + 8, sizeof(data) - 8);
  full1024 = neverc_fnv_sum1024a(data, sizeof(data));
  failed |=
      check_words("fnv1a-1024 incremental", h1024.words, full1024.words, 16);

  neverc_fnv_256_t zero256 = {{0}};
  zero256 = neverc_fnv_update256(zero256, data, 8);
  zero256 = neverc_fnv_update256(zero256, data + 8, sizeof(data) - 8);
  full256 = neverc_fnv0_sum256(data, sizeof(data));
  failed |=
      check_words("fnv0-256 incremental", zero256.words, full256.words, 4);

  neverc_fnv_512_t zero512 = {{0}};
  zero512 = neverc_fnv_update512(zero512, data, 8);
  zero512 = neverc_fnv_update512(zero512, data + 8, sizeof(data) - 8);
  full512 = neverc_fnv0_sum512(data, sizeof(data));
  failed |=
      check_words("fnv0-512 incremental", zero512.words, full512.words, 8);

  neverc_fnv_1024_t zero1024 = {{0}};
  zero1024 = neverc_fnv_update1024(zero1024, data, 8);
  zero1024 = neverc_fnv_update1024(zero1024, data + 8, sizeof(data) - 8);
  full1024 = neverc_fnv0_sum1024(data, sizeof(data));
  failed |=
      check_words("fnv0-1024 incremental", zero1024.words, full1024.words, 16);

  neverc_fnv_256_t unchanged256 =
      (neverc_fnv_256_t)NEVERC_FNV256_OFFSET_BASIS_INITIALIZER;
  h256 = neverc_fnv_update256a(unchanged256, NULL, 99);
  failed |=
      check_words("fnv256 null update", h256.words, unchanged256.words, 4);

  neverc_fnv_512_t unchanged512 =
      (neverc_fnv_512_t)NEVERC_FNV512_OFFSET_BASIS_INITIALIZER;
  h512 = neverc_fnv_update512a(unchanged512, NULL, 99);
  failed |=
      check_words("fnv512 null update", h512.words, unchanged512.words, 8);

  neverc_fnv_1024_t unchanged1024 =
      (neverc_fnv_1024_t)NEVERC_FNV1024_OFFSET_BASIS_INITIALIZER;
  h1024 = neverc_fnv_update1024a(unchanged1024, NULL, 99);
  failed |=
      check_words("fnv1024 null update", h1024.words, unchanged1024.words, 16);
  return failed;
}

int main(void) {
  static const uint64_t fnv256_offset[4] = {
      UINT64_C(0xdd268dbcaac55036), UINT64_C(0x2d98c384c4e576cc),
      UINT64_C(0xc8b1536847b6bbb3), UINT64_C(0x1023b4c8caee0535)};
  static const uint64_t fnv512_offset[8] = {
      UINT64_C(0xb86db0b1171f4416), UINT64_C(0xdca1e50f309990ac),
      UINT64_C(0xac87d059c9000000), UINT64_C(0x0000000000000d21),
      UINT64_C(0xe948f68a34c192f6), UINT64_C(0x2ea79bc942dbe7ce),
      UINT64_C(0x182036415f56e34b), UINT64_C(0xac982aac4afe9fd9)};
  static const uint64_t fnv1024_offset[16] = {
      UINT64_C(0x0000000000000000), UINT64_C(0x005f7a76758ecc4d),
      UINT64_C(0x32e56d5a591028b7), UINT64_C(0x4b29fc4223fdada1),
      UINT64_C(0x6c3bf34eda3674da), UINT64_C(0x9a21d90000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x000000000004c6d7),
      UINT64_C(0xeb6e73802734510a), UINT64_C(0x555f256cc005ae55),
      UINT64_C(0x6bde8cc9c6a93b21), UINT64_C(0xaff4b16c71ee90b3)};
  neverc_fnv_256_t empty256 = neverc_fnv_sum256("", 0);
  neverc_fnv_512_t empty512 = neverc_fnv_sum512a("", 0);
  if (check_words("fnv256 offset basis", empty256.words, fnv256_offset, 4) ||
      check_words("fnv512 offset basis", empty512.words, fnv512_offset, 8)) {
    return 1;
  }
  if (!equal1024(neverc_fnv_sum1024a("", 0), fnv1024_offset)) {
    puts("FAIL: fnv1024 offset basis");
    return 1;
  }

  neverc_fnv_256_t hash = neverc_fnv_sum256a("a", 1);
  if (!equal256(hash, UINT64_C(0x63323fb0f35303ec),
                UINT64_C(0x28dc751d0a33bdfa), UINT64_C(0x4de6a99b7266494f),
                UINT64_C(0x6183b2716811637c))) {
    puts("FAIL: fnv256a(a)");
    return 1;
  }

  neverc_fnv_256_t fnv0 = neverc_fnv0_sum256("foobar", 6);
  if (!equal256(fnv0, UINT64_C(0x0000000000075a62),
                UINT64_C(0x1ef5aa0000000000), UINT64_C(0x0000000000000000),
                UINT64_C(0x000209d27d06710f))) {
    puts("FAIL: fnv0-256(foobar)");
    return 1;
  }

  uint8_t big_endian[32];
  uint8_t little_endian[32];
  neverc_fnv_store256_be(big_endian, hash);
  neverc_fnv_store256_le(little_endian, hash);
  for (size_t i = 0; i < sizeof(big_endian); ++i) {
    if (big_endian[i] != little_endian[sizeof(big_endian) - i - 1]) {
      puts("FAIL: fnv256 byte order");
      return 1;
    }
  }
  static const uint8_t fnv256a_a_prefix[] = {0x63, 0x32, 0x3f, 0xb0};
  if (memcmp(big_endian, fnv256a_a_prefix, sizeof(fnv256a_a_prefix)) != 0) {
    puts("FAIL: fnv256 big-endian serialization");
    return 1;
  }

  static const uint64_t fnv512a_a[8] = {
      UINT64_C(0xe43a992dc8fc5ad7), UINT64_C(0xde493e3d696d6f85),
      UINT64_C(0xd64326ec07000000), UINT64_C(0x000000000011986f),
      UINT64_C(0x90c2532caf5be7d8), UINT64_C(0x8291baa894a39522),
      UINT64_C(0x5328b196bd6a8a64), UINT64_C(0x3fe12cd87b27ff88)};
  if (!equal512(neverc_fnv_sum512a("a", 1), fnv512a_a)) {
    puts("FAIL: fnv512a(a)");
    return 1;
  }

  static const uint64_t fnv0_512_foobar[8] = {
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000006),
      UINT64_C(0x6c927edf9a000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0001b8c2bbbc218f)};
  neverc_fnv_512_t fnv0_512 = neverc_fnv0_sum512("foobar", 6);
  if (!equal512(fnv0_512, fnv0_512_foobar)) {
    puts("FAIL: fnv0-512(foobar)");
    return 1;
  }

  static const uint64_t fnv1024a_a[16] = {
      UINT64_C(0x0000000000000000), UINT64_C(0x98d7c19fbce653df),
      UINT64_C(0x221b9f717d3490ff), UINT64_C(0x95ca87fdaef30d1b),
      UINT64_C(0x823372f85b24a372), UINT64_C(0xf50e570000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000007685cd8),
      UINT64_C(0x1a491dbccc21ad06), UINT64_C(0x648d09a5c8cf5a78),
      UINT64_C(0x482054e91470b33d), UINT64_C(0xde77252caef695aa)};
  if (!equal1024(neverc_fnv_sum1024a("a", 1), fnv1024a_a)) {
    puts("FAIL: fnv1024a(a)");
    return 1;
  }

  static const uint64_t fnv0_1024_foobar[16] = {
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x00000000000b86c3), UINT64_C(0xdbb99e0000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000000), UINT64_C(0x00039348798173b7)};
  neverc_fnv_1024_t fnv0_1024 = neverc_fnv0_sum1024("foobar", 6);
  if (!equal1024(fnv0_1024, fnv0_1024_foobar)) {
    puts("FAIL: fnv0-1024(foobar)");
    return 1;
  }

  uint8_t be512[64], le512[64], be1024[128], le1024[128];
  neverc_fnv_store512_be(be512, fnv0_512);
  neverc_fnv_store512_le(le512, fnv0_512);
  neverc_fnv_store1024_be(be1024, fnv0_1024);
  neverc_fnv_store1024_le(le1024, fnv0_1024);
  neverc_fnv_store256_be(NULL, hash);
  neverc_fnv_store512_le(NULL, fnv0_512);
  neverc_fnv_store1024_be(NULL, fnv0_1024);
  for (size_t i = 0; i < sizeof(be512); ++i) {
    if (be512[i] != le512[sizeof(be512) - i - 1]) {
      puts("FAIL: fnv512 byte order");
      return 1;
    }
  }
  for (size_t i = 0; i < sizeof(be1024); ++i) {
    if (be1024[i] != le1024[sizeof(be1024) - i - 1]) {
      puts("FAIL: fnv1024 byte order");
      return 1;
    }
  }

  if (test_additional_vectors() || test_incremental_and_null())
    return 1;

  if (sizeof(neverc_fnv_256_t) != 32 || sizeof(neverc_fnv_512_t) != 64 ||
      sizeof(neverc_fnv_1024_t) != 128) {
    puts("FAIL: wide FNV state sizes");
    return 1;
  }

  puts("passed");
  return 0;
}
