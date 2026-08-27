/*
 * Dot-syntax test suite — comprehensive.
 * Verifies that module.function() calls are correctly resolved
 * across ALL std modules and submodules.
 *
 * Top-level: math.abs(x), sort.ints(...), bytes.equal(...)
 * Submodules: encoding.hex.encode(...), hash.crc32.ieee(...)
 */
#include "neverc/std/math.h"
#include "neverc/std/math/rand.h"
#include "neverc/std/math/bits.h"
#include "neverc/std/strconv.h"
#include "neverc/std/encoding.h"
#include "neverc/std/hash.h"
#include "neverc/std/container.h"
#include "neverc/std/sort.h"
#include "neverc/std/cmp.h"
#include "neverc/std/errors.h"
#include "neverc/std/crypto.h"
#include "neverc/std/bytes.h"
#include "neverc/std/unicode.h"
#include "neverc/std/html.h"
#include "neverc/std/path.h"
#include "neverc/std/slices.h"
#include "neverc/std/maps.h"
#include "neverc/std/regexp.h"
#include "neverc/std/uuid.h"
#include "neverc/std/fmt.h"
#include "neverc/std/cstring.h"
#include "neverc/std/context.h"
#include "neverc/std/io.h"
#include "neverc/std/time.h"
#include "neverc/std/compress.h"
#include "neverc/std/log.h"
#include "neverc/std/os.h"
#include "neverc/std/sync.h"
#include "neverc/std/sync/atomic.h"
#include "neverc/std/flag.h"
#include "neverc/std/image.h"
#include "neverc/std/path/filepath.h"
#include "neverc/std/debug.h"
#include "neverc/std/archive.h"
#include "neverc/std/text.h"
#include "neverc/std/index.h"
#include "neverc/std/bufio.h"
#include "neverc/std/mime.h"
#include "neverc/std/arena.h"
#include "neverc/std/unique.h"
#include "neverc/std/weak.h"
#include "neverc/std/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define CHECK(name, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s\n", name); } \
} while(0)

#define APPROX(a, b) (((a) - (b)) < 1e-9 && ((b) - (a)) < 1e-9)

#ifdef __neverc__

static void test_math_dot_syntax(void) {
    CHECK("math.abs_positive", APPROX(math.abs(3.14), 3.14));
    CHECK("math.abs_negative", APPROX(math.abs(-2.718), 2.718));
    CHECK("math.sqrt_4", APPROX(math.sqrt(4.0), 2.0));
    CHECK("math.sqrt_9", APPROX(math.sqrt(9.0), 3.0));
    CHECK("math.ceil_1_3", APPROX(math.ceil(1.3), 2.0));
    CHECK("math.floor_1_7", APPROX(math.floor(1.7), 1.0));
    CHECK("math.max_vals", APPROX(math.max(3.0, 5.0), 5.0));
    CHECK("math.min_vals", APPROX(math.min(3.0, 5.0), 3.0));
    CHECK("math.pow_2_10", APPROX(math.pow(2.0, 10.0), 1024.0));
    CHECK("math.log2_8", APPROX(math.log2(8.0), 3.0));
    CHECK("math.sin_0", APPROX(math.sin(0.0), 0.0));
    CHECK("math.cos_0", APPROX(math.cos(0.0), 1.0));
    CHECK("math.copysign_neg", APPROX(math.copysign(5.0, -1.0), -5.0));

    double sn, cs;
    math.sincos(0.0, &sn, &cs);
    CHECK("math.sincos_zero_sin", APPROX(sn, 0.0));
    CHECK("math.sincos_zero_cos", APPROX(cs, 1.0));

    CHECK("math.exp_0", APPROX(math.exp(0.0), 1.0));
    CHECK("math.log_1", APPROX(math.log(1.0), 0.0));
    CHECK("math.log10_100", APPROX(math.log10(100.0), 2.0));
    CHECK("math.cbrt_27", APPROX(math.cbrt(27.0), 3.0));
    CHECK("math.hypot_3_4", APPROX(math.hypot(3.0, 4.0), 5.0));
    CHECK("math.round_2_5", APPROX(math.round(2.5), 3.0));
    CHECK("math.trunc_2_9", APPROX(math.trunc(2.9), 2.0));
}

static void test_strconv_dot_syntax(void) {
    char buf[64];
    int n = strconv.format_int(42, 10, buf, sizeof(buf));
    CHECK("strconv.format_int_42", n > 0 && strcmp(buf, "42") == 0);

    n = strconv.format_int(-7, 10, buf, sizeof(buf));
    CHECK("strconv.format_int_neg", n > 0 && strcmp(buf, "-7") == 0);

    long long val;
    int ok = strconv.parse_int("12345", 10, &val);
    CHECK("strconv.parse_int_ok", ok == 0 && val == 12345);

    n = strconv.format_bool(1, buf, sizeof(buf));
    CHECK("strconv.format_bool_true", n > 0 && strcmp(buf, "true") == 0);
}

static void test_encoding_dot_syntax(void) {
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    char hexbuf[16];
    size_t hexlen = encoding.hex.encode(hexbuf, data, 4);
    CHECK("encoding.hex.encode",
          hexlen == 8 && memcmp(hexbuf, "deadbeef", 8) == 0);

    uint8_t decoded[4];
    int rc = encoding.hex.decode(decoded, hexbuf, 8);
    CHECK("encoding.hex.decode_ok", rc == 4);
    CHECK("encoding.hex.decode_match", decoded[0] == 0xDE && decoded[3] == 0xEF);
}

static void test_hash_dot_syntax(void) {
    uint32_t c = hash.crc32.ieee((const uint8_t *)"hello", 5);
    CHECK("hash.crc32.ieee_nonzero", c != 0);
    CHECK("hash.crc32.ieee_known", c == 0x3610a686);
}

static void test_container_vector_dot_syntax(void) {
    neverc_vector_t *v = container.vector.new(sizeof(int));
    CHECK("container.vector.new", v != NULL);
    CHECK("container.vector.empty", container.vector.empty(v));
    CHECK("container.vector.size_0", container.vector.size(v) == 0);

    int val = 42;
    container.vector.push_back(v, &val);
    CHECK("container.vector.push_back", container.vector.size(v) == 1);
    CHECK("container.vector.at", *(int *)container.vector.at(v, 0) == 42);

    val = 99;
    container.vector.push_back(v, &val);
    CHECK("container.vector.back", *(int *)container.vector.back(v) == 99);

    container.vector.clear(v);
    CHECK("container.vector.clear", container.vector.size(v) == 0);

    container.vector.free(v);
    CHECK("container.vector.free", 1 == 1);
}

static void test_sort_dot_syntax(void) {
    int arr[] = {5, 3, 1, 4, 2};
    sort.ints(arr, 5);
    CHECK("sort.ints_0", arr[0] == 1);
    CHECK("sort.ints_4", arr[4] == 5);
    CHECK("sort.ints_are_sorted", sort.ints_are_sorted(arr, 5));

    int found = sort.search_ints(arr, 5, 3);
    CHECK("sort.search_ints_found", found == 2);
}

static void test_rand_dot_syntax(void) {
    math.rand.seed(12345);
    uint64_t v = math.rand.uint64();
    CHECK("math.rand.uint64_nonzero", v != 0);

    double f = math.rand.float64();
    CHECK("math.rand.float64_range", f >= 0.0 && f < 1.0);

    int64_t n = math.rand.intn(100);
    CHECK("math.rand.intn_range", n >= 0 && n < 100);

    int perm[5];
    math.rand.perm(5, perm);
    int sum = 0;
    for (int i = 0; i < 5; i++) sum += perm[i];
    CHECK("math.rand.perm_sum", sum == 10);

    int32_t i32 = math.rand.int32();
    CHECK("math.rand.int32_nonneg", i32 >= 0);

    int32_t i32n = math.rand.int32n(50);
    CHECK("math.rand.int32n_range", i32n >= 0 && i32n < 50);

    uint32_t u32n = math.rand.uint32n(200);
    CHECK("math.rand.uint32n_range", u32n < 200);

    uint64_t u64n = math.rand.uint64n(999999);
    CHECK("math.rand.uint64n_range", u64n < 999999);

    int64_t i63n = math.rand.int63n(1000);
    CHECK("math.rand.int63n_range", i63n >= 0 && i63n < 1000);

    double nf = math.rand.norm_float64();
    CHECK("math.rand.norm_float64_finite", nf == nf && nf > -100.0 && nf < 100.0);

    double ef = math.rand.exp_float64();
    CHECK("math.rand.exp_float64_positive", ef > 0.0);

    unsigned char rbuf[16];
    math.rand.read(rbuf, sizeof(rbuf));
    int any_nonzero = 0;
    for (int i = 0; i < 16; i++) if (rbuf[i] != 0) any_nonzero = 1;
    CHECK("math.rand.read_fills", any_nonzero);

    int rin = math.rand.intn_int(77);
    CHECK("math.rand.intn_int_range", rin >= 0 && rin < 77);

    unsigned int ruin = math.rand.uintn(300);
    CHECK("math.rand.uintn_range", ruin < 300);
}

static void test_cmp_dot_syntax(void) {
    CHECK("cmp.compare_int_lt", cmp.compare_int(-1, 1) < 0);
    CHECK("cmp.compare_int_eq", cmp.compare_int(5, 5) == 0);
    CHECK("cmp.compare_int_gt", cmp.compare_int(10, 5) > 0);
    CHECK("cmp.compare_float64_lt", cmp.compare_float64(1.0, 2.0) < 0);
    CHECK("cmp.min_int", cmp.min_int(3, 7) == 3);
    CHECK("cmp.max_int", cmp.max_int(3, 7) == 7);
}

static void test_errors_dot_syntax(void) {
    neverc_error_t *e = errors.new("test error");
    CHECK("errors.new", e != NULL);
    const char *msg = errors.message(e);
    CHECK("errors.message", strcmp(msg, "test error") == 0);
    errors.free(e);
    CHECK("errors.free", 1 == 1);
}

static void test_crypto_sha256_dot_syntax(void) {
    uint8_t hash_out[32];
    crypto.sha256.sum((const uint8_t *)"hello", 5, hash_out);
    CHECK("crypto.sha256.sum_nonzero", hash_out[0] != 0 || hash_out[1] != 0);
    CHECK("crypto.sha256.sum_known_byte0", hash_out[0] == 0x2c);
    CHECK("crypto.sha256.sum_known_byte1", hash_out[1] == 0xf2);

    uint8_t sha1_out[20];
    crypto.sha1.sum((const uint8_t *)"hello", 5, sha1_out);
    CHECK("crypto.sha1.sum_known", sha1_out[0] == 0xaa && sha1_out[1] == 0xf4);

    uint8_t md5_out[16];
    crypto.md5.sum((const uint8_t *)"hello", 5, md5_out);
    CHECK("crypto.md5.sum_known", md5_out[0] == 0x5d && md5_out[1] == 0x41);
}

static void test_crypto_hkdf_sha512_dot_syntax(void) {
    uint8_t ikm[16] = {1, 2, 3};
    uint8_t salt[8] = {4, 5, 6};
    uint8_t info[8] = {7, 8, 9};
    uint8_t dot[80], direct[80], prk_dot[64], prk_direct[64];

    CHECK("crypto.hkdf.sha512",
          crypto.hkdf.sha512(dot, sizeof(dot), ikm, sizeof(ikm),
                             salt, sizeof(salt), info, sizeof(info)) == 0 &&
          neverc_hkdf_sha512(direct, sizeof(direct), ikm, sizeof(ikm),
                             salt, sizeof(salt), info, sizeof(info)) == 0 &&
          memcmp(dot, direct, sizeof(dot)) == 0);
    CHECK("crypto.hkdf.extract_sha512",
          crypto.hkdf.extract_sha512(prk_dot, salt, sizeof(salt),
                                     ikm, sizeof(ikm)) == 0 &&
          neverc_hkdf_extract_sha512(prk_direct, salt, sizeof(salt),
                                     ikm, sizeof(ikm)) == 0 &&
          memcmp(prk_dot, prk_direct, sizeof(prk_dot)) == 0);
    CHECK("crypto.hkdf.expand_sha512",
          crypto.hkdf.expand_sha512(dot, sizeof(dot), prk_dot,
                                    info, sizeof(info)) == 0 &&
          neverc_hkdf_expand_sha512(direct, sizeof(direct), prk_direct,
                                    info, sizeof(info)) == 0 &&
          memcmp(dot, direct, sizeof(dot)) == 0);
}

/*
 * Keep one compile-time call for every family whose public dot method does
 * not mechanically match neverc_<marker>_<method>.  The branch is never
 * executed (some calls would perform I/O or expensive key generation), but
 * NeverC must still resolve and type-check every call.  This catches drift
 * between std/manifest.json and the compiler's generated dispatch table.
 */
static void test_manifest_dot_method_resolution(void) {
    if (0) {
        neverc_bigint_t big;
        neverc_sha3_ctx sha3;
        neverc_rsa_public_key_t rsa_public;
        neverc_net_interface_list_t interfaces;
        neverc_hpack_decoder_t *hpack;
        neverc_plan9_file_t plan9;
        neverc_mldsa44_sk_t mldsa;
        neverc_mlkem768_dk_t mlkem;
        neverc_netip_addr_t addr;
        neverc_image_rgba_t rgba;
        neverc_exec_exit_status_t status;
        neverc_exec_cmd_t *cmd;
        neverc_sse_t *sse;
        const char *syntax_error = NULL;
        unsigned char byte = 0;
        unsigned char triple_key[24] = {0};
        char text[64];
        char *owned;
        size_t out_len = 0;

        math.big.init(&big);
        math.big.free(&big);
        crypto.sha3.sha3_256_init(&sha3);
        (void)crypto.des.triple_is_weak_key(triple_key);
        (void)crypto.rsa.encrypt(&rsa_public, &byte, 1, &byte, 1,
                                 &out_len);
        (void)crypto.rand.read(&byte, 1);

        (void)image.pt(1, 2);
        image.draw.draw(&rgba, image.rect(0, 0, 1, 1), &rgba,
                        image.pt(0, 0), NEVERC_DRAW_SRC);
        owned = html.template_mod.escape("<");
        free(owned);

        owned = net.textproto.canonical_key("content-type");
        free(owned);
        (void)net.resolve.join_host_port("::1", "80", text,
                                         sizeof(text));
        (void)net.interface.interfaces(&interfaces);
        hpack = net.http2.hpack_decoder_create(0);
        net.http2.hpack_decoder_destroy(hpack);
        (void)debug.plan9obj.valid_magic(NEVERC_PLAN9_MAGIC386);

        sse = net.http.sse_start(NULL);
        (void)net.http.sse_retry(sse, 1000);
        net.http.sse_close(sse);
        (void)crypto.mldsa.generate_key44(&mldsa);
        (void)crypto.mlkem.generate_key768(&mlkem);
        (void)net.netip.is_loopback(&addr);
        (void)mime.quotedprintable.max_encoded_len(1);

        cmd = os.exec.command("never", NULL, 0);
        (void)os.exec.run(cmd, &status);
        os.exec.cmd_free(cmd);

        owned = regexp.syntax.string(
            regexp.syntax.parse("x", 0, &syntax_error));
        free(owned);
        (void)net.websocket.compute_accept(
            "dGhlIHNhbXBsZSBub25jZQ==", text, sizeof(text));
    }

    CHECK("manifest_irregular_dot_methods_compile", 1);
}

static void test_bytes_dot_syntax(void) {
    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {1, 2, 3};
    const uint8_t c[] = {1, 2, 4};
    CHECK("bytes.equal_same", bytes.equal(a, 3, b, 3));
    CHECK("bytes.equal_diff", !bytes.equal(a, 3, c, 3));
    CHECK("bytes.compare_eq", bytes.compare(a, 3, b, 3) == 0);
    CHECK("bytes.compare_lt", bytes.compare(a, 3, c, 3) < 0);
    CHECK("bytes.contains_byte_yes", bytes.contains_byte(a, 3, 2));
    CHECK("bytes.contains_byte_no", !bytes.contains_byte(a, 3, 5));

    const uint8_t hay[] = "hello world";
    const uint8_t needle[] = "world";
    CHECK("bytes.contains_yes", bytes.contains(hay, 11, needle, 5));
    CHECK("bytes.count", bytes.count(hay, 11, (const uint8_t *)"l", 1) == 3);

    size_t idx = bytes.index(hay, 11, needle, 5);
    CHECK("bytes.index_found", idx == 6);

    CHECK("bytes.has_prefix", bytes.has_prefix(hay, 11, (const uint8_t *)"hello", 5));
    CHECK("bytes.has_suffix", bytes.has_suffix(hay, 11, (const uint8_t *)"world", 5));
}

static void test_unicode_dot_syntax(void) {
    CHECK("unicode.is_letter_A", unicode.is_letter('A'));
    CHECK("unicode.is_letter_z", unicode.is_letter('z'));
    CHECK("unicode.is_letter_digit", !unicode.is_letter('5'));
    CHECK("unicode.is_digit_5", unicode.is_digit('5'));
    CHECK("unicode.is_digit_A", !unicode.is_digit('A'));
    CHECK("unicode.is_space_tab", unicode.is_space('\t'));
    CHECK("unicode.is_space_nl", unicode.is_space('\n'));
    CHECK("unicode.is_upper_A", unicode.is_upper('A'));
    CHECK("unicode.is_lower_a", unicode.is_lower('a'));
    CHECK("unicode.to_upper_a", unicode.to_upper('a') == 'A');
    CHECK("unicode.to_lower_A", unicode.to_lower('A') == 'a');
    CHECK("unicode.is_punct", unicode.is_punct('!'));
    CHECK("unicode.is_control", unicode.is_control('\0'));
}

static void test_html_dot_syntax(void) {
    size_t len;
    char *escaped = html.escape_string("<div>test & \"quote\"</div>", &len);
    CHECK("html.escape_string_not_null", escaped != NULL);
    CHECK("html.escape_string_content",
          strcmp(escaped, "&lt;div&gt;test &amp; &#34;quote&#34;&lt;/div&gt;") == 0);
    free(escaped);

    char *unescaped = html.unescape_string("&lt;b&gt;", &len);
    CHECK("html.unescape_string", strcmp(unescaped, "<b>") == 0);
    free(unescaped);
}

static void test_path_dot_syntax(void) {
    char buf[256];
    path.base("/foo/bar/baz.txt", buf, sizeof(buf));
    CHECK("path.base", strcmp(buf, "baz.txt") == 0);

    path.dir("/foo/bar/baz.txt", buf, sizeof(buf));
    CHECK("path.dir", strcmp(buf, "/foo/bar") == 0);

    const char *ext = path.ext("/foo/bar.tar.gz");
    CHECK("path.ext", strcmp(ext, ".gz") == 0);

    CHECK("path.isabs_yes", path.isabs("/foo/bar"));
    CHECK("path.isabs_no", !path.isabs("foo/bar"));
}

static void test_slices_dot_syntax(void) {
    int arr[] = {5, 3, 1, 4, 2};
    slices.sort_ints(arr, 5);
    CHECK("slices.sort_ints", arr[0] == 1 && arr[4] == 5);
    CHECK("slices.contains_int_yes", slices.contains_int(arr, 5, 3));
    CHECK("slices.contains_int_no", !slices.contains_int(arr, 5, 9));
    CHECK("slices.index_int_found", slices.index_int(arr, 5, 4) == 3);

    int arr2[] = {1, 2, 3, 4, 5};
    CHECK("slices.equal_ints_same", slices.equal_ints(arr, 5, arr2, 5));

    slices.reverse_ints(arr, 5);
    CHECK("slices.reverse_ints", arr[0] == 5 && arr[4] == 1);
}

static void test_maps_dot_syntax(void) {
    neverc_map_t *m = maps.new();
    CHECK("maps.new", m != NULL);
    CHECK("maps.len_empty", maps.len(m) == 0);

    maps.set(m, "key1", (void *)(size_t)42);
    CHECK("maps.len_one", maps.len(m) == 1);
    CHECK("maps.has_yes", maps.has(m, "key1"));
    CHECK("maps.has_no", !maps.has(m, "key2"));
    CHECK("maps.get", (size_t)maps.get(m, "key1") == 42);

    maps.set(m, "key2", (void *)(size_t)99);
    CHECK("maps.len_two", maps.len(m) == 2);

    maps.delete(m, "key1");
    CHECK("maps.delete", !maps.has(m, "key1"));
    CHECK("maps.len_after_delete", maps.len(m) == 1);

    maps.clear(m);
    CHECK("maps.clear", maps.len(m) == 0);

    maps.free(m);
}

static void test_regexp_dot_syntax(void) {
    const char *err = NULL;
    neverc_regexp_t *re = regexp.compile("^hello.*world$", &err);
    CHECK("regexp.compile_ok", re != NULL);
    CHECK("regexp.match_yes", regexp.match(re, "hello beautiful world"));
    CHECK("regexp.match_no", !regexp.match(re, "goodbye world"));

    char *quoted = regexp.quote_meta("a.b+c*d?e");
    CHECK("regexp.quote_meta", strcmp(quoted, "a\\.b\\+c\\*d\\?e") == 0);
    free(quoted);

    regexp.free(re);
}

static void test_uuid_dot_syntax(void) {
    neverc_uuid_t u1 = uuid.new();
    neverc_uuid_t u2 = uuid.new();
    char buf1[37], buf2[37];
    uuid.to_string(u1, buf1);
    uuid.to_string(u2, buf2);
    CHECK("uuid.new_format", buf1[8] == '-' && buf1[13] == '-' &&
                              buf1[18] == '-' && buf1[23] == '-');
    CHECK("uuid.new_version4", uuid.version(u1) == 4);
    CHECK("uuid.new_unique", !uuid.equal(u1, u2));
    CHECK("uuid.is_nil_no", !uuid.is_nil(u1));
    CHECK("uuid.is_nil_yes", uuid.is_nil(uuid.nil()));
}

static void test_hash_fnv_dot_syntax(void) {
    uint32_t h = hash.fnv.sum32a("hello", 5);
    CHECK("hash.fnv.sum32a_nonzero", h != 0);
    CHECK("hash.fnv.sum32a_known", h == 0x4f9f2cab);

    uint64_t h64 = hash.fnv.sum64a("hello", 5);
    CHECK("hash.fnv.sum64a_nonzero", h64 != 0);

    uint32_t h32_update =
        hash.fnv.update32(NEVERC_FNV32_OFFSET_BASIS, "he", 2);
    h32_update = hash.fnv.update32(h32_update, "llo", 3);
    CHECK("hash.fnv.update32",
          h32_update == hash.fnv.sum32("hello", 5));

    uint32_t h32a_update =
        hash.fnv.update32a(NEVERC_FNV32_OFFSET_BASIS, "he", 2);
    h32a_update = hash.fnv.update32a(h32a_update, "llo", 3);
    CHECK("hash.fnv.update32a", h32a_update == h);

    uint64_t h64_update =
        hash.fnv.update64(NEVERC_FNV64_OFFSET_BASIS, "he", 2);
    h64_update = hash.fnv.update64(h64_update, "llo", 3);
    CHECK("hash.fnv.update64",
          h64_update == hash.fnv.sum64("hello", 5));

    uint64_t h64a_update =
        hash.fnv.update64a(NEVERC_FNV64_OFFSET_BASIS, "he", 2);
    h64a_update = hash.fnv.update64a(h64a_update, "llo", 3);
    CHECK("hash.fnv.update64a", h64a_update == h64);

    neverc_fnv_128_t h128 = {
        NEVERC_FNV128_OFFSET_BASIS_HI,
        NEVERC_FNV128_OFFSET_BASIS_LO
    };
    h128 = hash.fnv.update128(h128, "he", 2);
    h128 = hash.fnv.update128(h128, "llo", 3);
    neverc_fnv_128_t full128 = hash.fnv.sum128("hello", 5);
    CHECK("hash.fnv.update128",
          h128.hi == full128.hi && h128.lo == full128.lo);

    neverc_fnv_128_t h128a = {
        NEVERC_FNV128_OFFSET_BASIS_HI,
        NEVERC_FNV128_OFFSET_BASIS_LO
    };
    h128a = hash.fnv.update128a(h128a, "he", 2);
    h128a = hash.fnv.update128a(h128a, "llo", 3);
    neverc_fnv_128_t full128a = hash.fnv.sum128a("hello", 5);
    CHECK("hash.fnv.update128a",
          h128a.hi == full128a.hi && h128a.lo == full128a.lo);
}

static void test_hash_adler32_dot_syntax(void) {
    uint32_t a = hash.adler32.checksum((const uint8_t *)"hello", 5);
    CHECK("hash.adler32.checksum_nonzero", a != 0);
    CHECK("hash.adler32.checksum_known", a == 0x062c0215);
}

static void test_encoding_base64_dot_syntax(void) {
    const uint8_t input[] = "Hello, World!";
    char encoded[64];
    size_t elen = encoding.base64.encode(encoded, input, 13);
    CHECK("encoding.base64.encode_ok", elen == 20);
    CHECK("encoding.base64.encode_val",
          memcmp(encoded, "SGVsbG8sIFdvcmxkIQ==", 20) == 0);

    uint8_t decoded[64];
    int dlen = encoding.base64.decode(decoded, encoded, elen);
    CHECK("encoding.base64.decode_ok", dlen == 13);
    CHECK("encoding.base64.roundtrip", memcmp(decoded, input, 13) == 0);
}

static void test_bits_dot_syntax(void) {
    CHECK("math.bits.leading_zeros64_0", math.bits.leading_zeros64(0) == 64);
    CHECK("math.bits.leading_zeros64_1", math.bits.leading_zeros64(1) == 63);
    CHECK("math.bits.trailing_zeros64_8", math.bits.trailing_zeros64(8) == 3);
    CHECK("math.bits.ones_count64", math.bits.ones_count64(0xFF) == 8);
    CHECK("math.bits.len64_256", math.bits.len64(256) == 9);
    CHECK("math.bits.reverse_bytes64",
          math.bits.reverse_bytes64(0x0102030405060708ULL) == 0x0807060504030201ULL);
    CHECK("math.bits.rotate_left64",
          math.bits.rotate_left64(1, 3) == 8);

    uint64_t s64, c64;
    math.bits.add64(0xFFFFFFFFFFFFFFFFULL, 1, 0, &s64, &c64);
    CHECK("math.bits.add64_overflow", s64 == 0 && c64 == 1);

    uint64_t d64, b64;
    math.bits.sub64(0, 1, 0, &d64, &b64);
    CHECK("math.bits.sub64_underflow", d64 == 0xFFFFFFFFFFFFFFFFULL && b64 == 1);

    uint64_t hi64, lo64;
    math.bits.mul64(0xFFFFFFFF, 0xFFFFFFFF, &hi64, &lo64);
    CHECK("math.bits.mul64_basic", hi64 == 0 && lo64 == 0xFFFFFFFE00000001ULL);

    uint32_t q32, r32;
    math.bits.div32(0, 100, 7, &q32, &r32);
    CHECK("math.bits.div32_basic", q32 == 14 && r32 == 2);

    CHECK("math.bits.rem64", math.bits.rem64(0, 100, 7) == 2);
}

static void test_fmt_dot_syntax(void) {
    /* fmt.sprintf conflicts with libc sprintf macro on some platforms.
       Test via the C function name directly + non-conflicting dot methods. */
    char *s = neverc_fmt_sprintf("hello %d world %s", 42, "!");
    CHECK("fmt.sprintf_direct", s != NULL && strcmp(s, "hello 42 world !") == 0);
    free(s);

    s = neverc_fmt_sprintf("%x", 255);
    CHECK("fmt.sprintf_hex", s != NULL && strcmp(s, "ff") == 0);
    free(s);

    char buf[64];
    buf[0] = '\0';
    int n = fmt.appendf(buf, sizeof(buf), "val=%d", 100);
    CHECK("fmt.appendf", n > 0 && strcmp(buf, "val=100") == 0);

    char *e = fmt.errorf("code %d: %s", 404, "not found");
    CHECK("fmt.errorf", e != NULL && strcmp(e, "code 404: not found") == 0);
    free(e);

    char *sp = fmt.sprint("hello");
    CHECK("fmt.sprint", sp != NULL && strcmp(sp, "hello") == 0);
    free(sp);

    char *spl = fmt.sprintln("world");
    CHECK("fmt.sprintln", spl != NULL);
    free(spl);
}

static void test_cstring_dot_syntax(void) {
    CHECK("cstring.contains_yes", cstring.contains("hello world", "world"));
    CHECK("cstring.contains_no", !cstring.contains("hello", "xyz"));
    CHECK("cstring.has_prefix_yes", cstring.has_prefix("hello", "hel"));
    CHECK("cstring.has_suffix_yes", cstring.has_suffix("hello", "llo"));
    CHECK("cstring.index_found", cstring.index("hello world", "world") == 6);
    CHECK("cstring.index_not_found", cstring.index("hello", "xyz") == -1);
    CHECK("cstring.count", cstring.count("aababab", "ab") == 3);
    CHECK("cstring.equal_fold", cstring.equal_fold("Hello", "hello"));

    char *upper = cstring.to_upper("hello");
    CHECK("cstring.to_upper", strcmp(upper, "HELLO") == 0);
    free(upper);

    char *lower = cstring.to_lower("WORLD");
    CHECK("cstring.to_lower", strcmp(lower, "world") == 0);
    free(lower);

    char *rep = cstring.repeat("ab", 3);
    CHECK("cstring.repeat", strcmp(rep, "ababab") == 0);
    free(rep);

    char *trimmed = cstring.trim_space("  hello  ");
    CHECK("cstring.trim_space", strcmp(trimmed, "hello") == 0);
    free(trimmed);

    const char *strs[] = {"a", "b", "c"};
    char *joined = cstring.join(strs, 3, "-");
    CHECK("cstring.join", strcmp(joined, "a-b-c") == 0);
    free(joined);

    char *replaced = cstring.replace_all("foo bar foo", "foo", "baz");
    CHECK("cstring.replace_all", strcmp(replaced, "baz bar baz") == 0);
    free(replaced);

    char *cloned = cstring.clone("test");
    CHECK("cstring.clone", strcmp(cloned, "test") == 0);
    free(cloned);

    CHECK("cstring.len", cstring.len("hello") == 5);
}

static void test_context_dot_syntax(void) {
    neverc_context_t *bg = context.background();
    CHECK("context.background", bg != NULL);
    CHECK("context.done_no", !context.done(bg));
    CHECK("context.err_null", context.err(bg) == NULL);
    CHECK("context.deadline_zero", context.deadline(bg) == 0);

    neverc_context_t *todo = context.todo();
    CHECK("context.todo", todo != NULL);

    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *child = context.with_cancel(bg, &cancel);
    CHECK("context.with_cancel", child != NULL);
    CHECK("context.child_not_done", !context.done(child));

    if (cancel) cancel();
    CHECK("context.child_cancelled", context.done(child));
    CHECK("context.child_err", context.err(child) != NULL);

    neverc_cancel_func_t cancel2 = NULL;
    neverc_context_t *cause_ctx = context.with_cancel_cause(bg, &cancel2, "test reason");
    CHECK("context.with_cancel_cause", cause_ctx != NULL);
    if (cancel2) cancel2();
    CHECK("context.cause_after_cancel", context.cause(cause_ctx) != NULL);
    CHECK("context.cause_value", strcmp(context.cause(cause_ctx), "test reason") == 0);

    neverc_context_t *timeout_ctx = context.with_timeout(bg, 10000, NULL);
    CHECK("context.with_timeout", timeout_ctx != NULL);
    CHECK("context.timeout_deadline_set", context.deadline(timeout_ctx) > 0);
    CHECK("context.timeout_not_done", !context.done(timeout_ctx));

    neverc_context_t *tc_ctx = context.with_timeout_cause(bg, 10000, NULL, "slow op");
    CHECK("context.with_timeout_cause", tc_ctx != NULL);

    neverc_context_t *dl_ctx = context.with_deadline(bg, 1, NULL);
    CHECK("context.with_deadline", dl_ctx != NULL);
    CHECK("context.deadline_expired", context.done(dl_ctx));

    neverc_context_t *dlc_ctx = context.with_deadline_cause(bg, 1, NULL, "past");
    CHECK("context.with_deadline_cause", dlc_ctx != NULL);
    CHECK("context.deadline_cause_expired", context.done(dlc_ctx));
    CHECK("context.deadline_cause_val", strcmp(context.cause(dlc_ctx), "past") == 0);

    neverc_context_t *detached = context.without_cancel(child);
    CHECK("context.without_cancel", detached != NULL);
    CHECK("context.detached_not_done", !context.done(detached));

    neverc_context_t *val_ctx = context.with_value(bg, "mykey", "myval");
    CHECK("context.with_value", val_ctx != NULL);
    const char *v = (const char *)context.value(val_ctx, "mykey");
    CHECK("context.value_found", v != NULL && strcmp(v, "myval") == 0);
    CHECK("context.value_missing", context.value(val_ctx, "nope") == NULL);

    context.free(detached);
    context.free(dlc_ctx);
    context.free(dl_ctx);
    context.free(tc_ctx);
    context.free(timeout_ctx);
    context.free(cause_ctx);
    context.free(val_ctx);
    context.free(child);
    context.free(todo);
    context.free(bg);
}

static void test_io_dot_syntax(void) {
    const uint8_t data[] = "Hello, IO!";
    neverc_io_mem_reader_t mr;
    io.mem_reader_init(&mr, data, 10);

    neverc_io_reader_t r;
    r.ctx = &mr;
    r.read = neverc_io_mem_reader_read;

    size_t outlen = 0;
    uint8_t *all = io.read_all(&r, &outlen);
    CHECK("io.read_all_len", outlen == 10);
    CHECK("io.read_all_content", all != NULL && memcmp(all, "Hello, IO!", 10) == 0);
    free(all);

    io.mem_reader_init(&mr, data, 10);
    r.ctx = &mr;
    uint8_t buf[10];
    int rc = io.read_full(&r, buf, 10);
    CHECK("io.read_full", rc == 0 && memcmp(buf, "Hello, IO!", 10) == 0);

    neverc_io_mem_writer_t mw;
    io.mem_writer_init(&mw);
    neverc_io_writer_t w;
    w.ctx = &mw;
    w.write = neverc_io_mem_writer_write;

    size_t written;
    io.write_string(&w, "test output", &written);
    CHECK("io.write_string", written == 11);
    CHECK("io.write_string_data", mw.len == 11 && memcmp(mw.data, "test output", 11) == 0);
    io.mem_writer_free(&mw);
}

static void test_time_dot_syntax(void) {
    neverc_time_t now = time_mod.now();
    CHECK("time.now_nonzero", now.sec > 0);

    int year = time_mod.year(now);
    CHECK("time.year_reasonable", year >= 2024 && year <= 2030);

    int month = time_mod.month(now);
    CHECK("time.month_range", month >= 1 && month <= 12);

    int day = time_mod.day(now);
    CHECK("time.day_range", day >= 1 && day <= 31);

    neverc_time_t t = time_mod.date(2024, 1, 15, 12, 30, 45, 0);
    CHECK("time.date_year", time_mod.year(t) == 2024);
    CHECK("time.date_month", time_mod.month(t) == 1);
    CHECK("time.date_day", time_mod.day(t) == 15);
    CHECK("time.date_hour", time_mod.hour(t) == 12);
    CHECK("time.date_minute", time_mod.minute(t) == 30);
    CHECK("time.date_second", time_mod.second(t) == 45);

    neverc_time_t t2 = time_mod.add(t, 5 * NEVERC_TIME_SECOND);
    CHECK("time.add_5s", time_mod.second(t2) == 50);

    neverc_duration_t diff = time_mod.sub(t2, t);
    CHECK("time.sub_5s", diff == 5 * NEVERC_TIME_SECOND);

    CHECK("time.before", time_mod.before(t, t2));
    CHECK("time.after", time_mod.after(t2, t));
    CHECK("time.equal_self", time_mod.equal(t, t));

    int64_t usec = time_mod.unix_sec(t);
    CHECK("time.unix_sec_positive", usec > 0);

    char *rfc = time_mod.format_rfc3339(t);
    CHECK("time.format_rfc3339", rfc != NULL && strlen(rfc) > 10);
    free(rfc);

    neverc_duration_t dur;
    int ok = time_mod.parse_duration("1h30m", &dur);
    CHECK("time.parse_duration", ok == 0);
    CHECK("time.parse_duration_val",
          dur == NEVERC_TIME_HOUR + 30 * NEVERC_TIME_MINUTE);

    char *ds = time_mod.format_duration(dur);
    CHECK("time.format_duration", ds != NULL);
    free(ds);
}

static void test_compress_dot_syntax(void) {
    const uint8_t input[] = "Hello Hello Hello Hello Hello Hello Hello Hello";
    size_t src_len = sizeof(input) - 1;

    uint8_t cbuf[256];
    size_t clen = sizeof(cbuf);
    int rc = compress.gzip.compress(input, src_len, cbuf, &clen, 6);
    CHECK("compress.gzip.compress", rc == 0 && clen > 0);
    CHECK("compress.gzip.compress_smaller", clen < src_len);

    uint8_t dbuf[256];
    size_t dlen = sizeof(dbuf);
    rc = compress.gzip.decompress(cbuf, clen, dbuf, &dlen);
    CHECK("compress.gzip.decompress", rc == 0);
    CHECK("compress.gzip.roundtrip", dlen == src_len &&
          memcmp(dbuf, input, dlen) == 0);

    clen = sizeof(cbuf);
    rc = compress.zlib.compress(input, src_len, cbuf, &clen, 6);
    CHECK("compress.zlib.compress", rc == 0 && clen > 0);

    dlen = sizeof(dbuf);
    rc = compress.zlib.decompress(cbuf, clen, dbuf, &dlen);
    CHECK("compress.zlib.decompress", rc == 0);
    CHECK("compress.zlib.roundtrip", dlen == src_len &&
          memcmp(dbuf, input, dlen) == 0);
}

static void test_sync_dot_syntax(void) {
    neverc_mutex_t mu;
    sync_mod.mutex_init(&mu);
    sync_mod.mutex_lock(&mu);
    sync_mod.mutex_unlock(&mu);
    int got = sync_mod.mutex_trylock(&mu);
    CHECK("sync.mutex_trylock", got == 1);
    sync_mod.mutex_unlock(&mu);
    sync_mod.mutex_destroy(&mu);
    CHECK("sync.mutex_roundtrip", 1);

    neverc_rwmutex_t rw;
    sync_mod.rwmutex_init(&rw);
    sync_mod.rwmutex_rlock(&rw);
    sync_mod.rwmutex_runlock(&rw);
    sync_mod.rwmutex_lock(&rw);
    sync_mod.rwmutex_unlock(&rw);
    sync_mod.rwmutex_destroy(&rw);
    CHECK("sync.rwmutex_roundtrip", 1);

    neverc_waitgroup_t wg;
    sync_mod.waitgroup_init(&wg);
    sync_mod.waitgroup_add(&wg, 1);
    sync_mod.waitgroup_done(&wg);
    CHECK("sync.waitgroup_checked_rejects_underflow",
          sync_mod.waitgroup_done_checked(&wg) == -1);
    CHECK("sync.waitgroup_checked_add",
          sync_mod.waitgroup_add_checked(&wg, 1) == 0);
    CHECK("sync.waitgroup_checked_done",
          sync_mod.waitgroup_done_checked(&wg) == 0);
    sync_mod.waitgroup_wait(&wg);
    sync_mod.waitgroup_destroy(&wg);
    CHECK("sync.waitgroup_roundtrip", 1);

    neverc_sync_map_t *sm = neverc_sync_map_new();
    int sv1 = 10, sv2 = 20;
    neverc_sync_map_store(sm, "dot_key", &sv1);
    int sok = 0;
    void *sgot = neverc_sync_map_load(sm, "dot_key", &sok);
    CHECK("sync.map_store+load", sok && *(int *)sgot == 10);
    int sloaded = 0;
    void *sprev = neverc_sync_map_swap(sm, "dot_key", &sv2, &sloaded);
    CHECK("sync.map_swap", sloaded && sprev == &sv1);
    int sswapped = neverc_sync_map_compare_and_swap(sm, "dot_key", &sv2, &sv1);
    CHECK("sync.map_compare_and_swap", sswapped);
    int sdeleted = neverc_sync_map_compare_and_delete(sm, "dot_key", &sv1);
    CHECK("sync.map_compare_and_delete", sdeleted);
    neverc_sync_map_load(sm, "dot_key", &sok);
    CHECK("sync.map_after_cad_gone", !sok);

    neverc_sync_map_store(sm, "range_k", &sv1);
    neverc_sync_map_clear(sm);
    neverc_sync_map_load(sm, "range_k", &sok);
    CHECK("sync.map_clear", !sok);

    int los = 0;
    void *lores = neverc_sync_map_load_or_store(sm, "new_k", &sv1, &los);
    CHECK("sync.map_load_or_store_new", !los && lores == &sv1);
    void *lores2 = neverc_sync_map_load_or_store(sm, "new_k", &sv2, &los);
    CHECK("sync.map_load_or_store_existing", los && lores2 == &sv1);

    int lad_ok = 0;
    void *lad_val = neverc_sync_map_load_and_delete(sm, "new_k", &lad_ok);
    CHECK("sync.map_load_and_delete", lad_ok && lad_val == &sv1);

    neverc_sync_map_free(sm);

    neverc_sync_pool_t *pool = neverc_sync_pool_new(NULL);
    CHECK("sync.pool_new", pool != NULL);
    int pval = 99;
    neverc_sync_pool_put(pool, &pval);
    void *pgot = neverc_sync_pool_get(pool);
    CHECK("sync.pool_put_get", pgot == &pval);
    void *pempty = neverc_sync_pool_get(pool);
    CHECK("sync.pool_get_empty", pempty == NULL);
    neverc_sync_pool_free(pool);
}

static void test_atomic_dot_syntax(void) {
    volatile int32_t v32 = 0;
    sync_mod.atomic.store_int32(&v32, 42);
    CHECK("sync.atomic.store_int32", sync_mod.atomic.load_int32(&v32) == 42);

    int32_t newval = sync_mod.atomic.add_int32(&v32, 8);
    CHECK("sync.atomic.add_int32_new", newval == 50);
    CHECK("sync.atomic.add_int32_verify", sync_mod.atomic.load_int32(&v32) == 50);

    volatile uint64_t v64 = 0;
    sync_mod.atomic.store_uint64(&v64, 100);
    CHECK("sync.atomic.load_uint64", sync_mod.atomic.load_uint64(&v64) == 100);
}

static void test_os_dot_syntax(void) {
    os.setenv("NEVERC_DOT_TEST", "hello");
    const char *val = os.getenv("NEVERC_DOT_TEST");
    CHECK("os.setenv+getenv", val != NULL && strcmp(val, "hello") == 0);

    char hostname[256];
    int rc = os.hostname(hostname, sizeof(hostname));
    CHECK("os.hostname", rc == 0 && strlen(hostname) > 0);

    char cwd[1024];
    rc = os.getwd(cwd, sizeof(cwd));
    CHECK("os.getwd", rc == 0 && strlen(cwd) > 0);

    os.unsetenv("NEVERC_DOT_TEST");
    CHECK("os.unsetenv", os.getenv("NEVERC_DOT_TEST") == NULL);
}

static void test_log_dot_syntax(void) {
    neverc_log_logger_t logger;
#if defined(_WIN32)
    FILE *devnull = fopen("NUL", "w");
#else
    FILE *devnull = fopen("/dev/null", "w");
#endif
    if (!devnull) devnull = stdout;
    log_mod.init(&logger, devnull, "[test] ", NEVERC_LOG_LDATE | NEVERC_LOG_LTIME);
    log_mod.print(&logger, "dot syntax test");
    log_mod.set_prefix(&logger, "[new] ");
    CHECK("log.init+print", 1);
    if (devnull != stdout) fclose(devnull);
}

static void test_json_dot_syntax(void) {
    const char *src = "{\"name\":\"NeverC\",\"version\":1}";
    neverc_json_value_t *v = encoding.json.parse(src, strlen(src));
    CHECK("encoding.json.parse", v != NULL);
    CHECK("encoding.json.type_object",
          encoding.json.type(v) == NEVERC_JSON_OBJECT);

    neverc_json_value_t *name = encoding.json.object_get(v, "name");
    CHECK("encoding.json.object_get",
          name != NULL && strcmp(encoding.json.string(name), "NeverC") == 0);

    neverc_json_value_t *ver = encoding.json.object_get(v, "version");
    CHECK("encoding.json.number", encoding.json.number(ver) == 1.0);

    char buf[256];
    int mlen = encoding.json.marshal(v, buf, sizeof(buf), NULL);
    CHECK("encoding.json.marshal", mlen > 0);
    encoding.json.free(v);

    const char raw_string[3] = {'a', '\0', 'b'};
    neverc_json_value_t *string_value =
        encoding.json.new_string_n(raw_string, sizeof(raw_string));
    CHECK("encoding.json.new_string_n", string_value != NULL);
    CHECK("encoding.json.string_len",
          encoding.json.string_len(string_value) == sizeof(raw_string));
    encoding.json.free(string_value);

    const char raw_key[3] = {'k', '\0', 'y'};
    neverc_json_value_t *object = encoding.json.new_object();
    neverc_json_value_t *number = encoding.json.new_number(7);
    int set_result = encoding.json.object_set_n(
        object, raw_key, sizeof(raw_key), number);
    CHECK("encoding.json.object_set_n",
          set_result == 0);
    CHECK("encoding.json.object_get_n",
          encoding.json.number(encoding.json.object_get_n(
              object, raw_key, sizeof(raw_key))) == 7.0);
    if (set_result != 0) encoding.json.free(number);
    encoding.json.free(object);
}

static void test_binary_dot_syntax(void) {
    uint8_t buf[8];
    encoding.binary.big_endian_put_uint16(buf, 0x1234);
    CHECK("encoding.binary.big_endian_put_uint16",
          buf[0] == 0x12 && buf[1] == 0x34);
    CHECK("encoding.binary.big_endian_uint16",
          encoding.binary.big_endian_uint16(buf) == 0x1234);

    encoding.binary.little_endian_put_uint32(buf, 0xDEADBEEF);
    CHECK("encoding.binary.little_endian_uint32",
          encoding.binary.little_endian_uint32(buf) == 0xDEADBEEF);

    encoding.binary.big_endian_put_uint64(buf, 0x0102030405060708ULL);
    CHECK("encoding.binary.big_endian_uint64",
          encoding.binary.big_endian_uint64(buf) == 0x0102030405060708ULL);
}

static void test_list_dot_syntax(void) {
    neverc_list_t *l = container.list.new();
    CHECK("container.list.new", l != NULL);
    CHECK("container.list.len_empty", container.list.len(l) == 0);

    int a = 10, b = 20, c = 30;
    container.list.push_back(l, &a);
    container.list.push_back(l, &b);
    container.list.push_front(l, &c);
    CHECK("container.list.len_3", container.list.len(l) == 3);

    neverc_list_element_t *front = container.list.front(l);
    CHECK("container.list.front_val", *(int *)front->value == 30);

    neverc_list_element_t *back = container.list.back(l);
    CHECK("container.list.back_val", *(int *)back->value == 20);

    container.list.free(l);
}

static void test_ring_dot_syntax(void) {
    neverc_ring_t *r = container.ring.new(5);
    CHECK("container.ring.new", r != NULL);
    CHECK("container.ring.len", container.ring.len(r) == 5);

    r->value = (void *)(intptr_t)42;
    neverc_ring_t *nx = container.ring.next(r);
    CHECK("container.ring.next", nx != NULL && nx != r);

    neverc_ring_t *pv = container.ring.prev(r);
    CHECK("container.ring.prev", pv != NULL && pv != r);

    container.ring.free(r);
}

static void test_filepath_dot_syntax(void) {
    char buf[256];
#ifdef _WIN32
    const char *b = path.filepath.base("C:\\usr\\local\\bin\\neverc", buf, sizeof(buf));
    CHECK("path.filepath.base", strcmp(b, "neverc") == 0);

    const char *d = path.filepath.dir("C:\\usr\\local\\bin\\neverc", buf, sizeof(buf));
    CHECK("path.filepath.dir", strcmp(d, "C:\\usr\\local\\bin") == 0);
#else
    const char *b = path.filepath.base("/usr/local/bin/neverc", buf, sizeof(buf));
    CHECK("path.filepath.base", strcmp(b, "neverc") == 0);

    const char *d = path.filepath.dir("/usr/local/bin/neverc", buf, sizeof(buf));
    CHECK("path.filepath.dir", strcmp(d, "/usr/local/bin") == 0);
#endif

    const char *e = path.filepath.ext("hello.tar.gz");
    CHECK("path.filepath.ext", strcmp(e, ".gz") == 0);

#ifdef _WIN32
    CHECK("path.filepath.isabs_true", path.filepath.isabs("C:\\Windows") == 1);
#else
    CHECK("path.filepath.isabs_true", path.filepath.isabs("/usr/bin") == 1);
#endif
    CHECK("path.filepath.isabs_false", path.filepath.isabs("relative/path") == 0);

#ifdef _WIN32
    const char *j = path.filepath.join("C:\\usr", "local", buf, sizeof(buf));
    CHECK("path.filepath.join", strcmp(j, "C:\\usr\\local") == 0);
#else
    const char *j = path.filepath.join("/usr", "local", buf, sizeof(buf));
    CHECK("path.filepath.join", strcmp(j, "/usr/local") == 0);
#endif
}

static void test_sha1_dot_syntax(void) {
    uint8_t digest[20];
    const char *data = "hello";
    crypto.sha1.sum((const uint8_t *)data, 5, digest);
    CHECK("crypto.sha1.sum_byte0", digest[0] == 0xaa);
    CHECK("crypto.sha1.sum_byte1", digest[1] == 0xf4);
    CHECK("crypto.sha1.sum_byte2", digest[2] == 0xc6);
}

static void test_md5_dot_syntax(void) {
    uint8_t digest[16];
    const char *data = "hello";
    crypto.md5.sum((const uint8_t *)data, 5, digest);
    CHECK("crypto.md5.sum_byte0", digest[0] == 0x5d);
    CHECK("crypto.md5.sum_byte1", digest[1] == 0x41);
}

static void test_crc64_dot_syntax(void) {
    neverc_crc64_table_t table;
    hash.crc64.make_table(NEVERC_CRC64_ECMA, table);
    uint64_t crc = hash.crc64.checksum(table,
        (const uint8_t *)"123456789", 9);
    CHECK("hash.crc64.checksum", crc != 0);
}

static void test_color_dot_syntax(void) {
    neverc_color_rgba_t c = image.color.rgba(255, 128, 0, 255);
    CHECK("image.color.rgba_r", c.r == 255);
    CHECK("image.color.rgba_g", c.g == 128);
    CHECK("image.color.rgba_b", c.b == 0);
    CHECK("image.color.rgba_a", c.a == 255);

    neverc_color_nrgba_t nc = image.color.nrgba(200, 100, 50, 128);
    neverc_color_rgba_t conv = image.color.nrgba_to_rgba(nc);
    CHECK("image.color.nrgba_to_rgba", conv.a == 128);
}

static void test_flag_dot_syntax(void) {
    flag.reset();
    const char *sval = NULL;
    flag.string("test_name", "default", "a name", &sval);
    CHECK("flag.string_default", sval != NULL && strcmp(sval, "default") == 0);
    CHECK("flag.parsed_false", flag.parsed() == 0);
    CHECK("flag.narg_zero", flag.narg() == 0);
    flag.reset();
}

static void test_arena_dot_syntax(void) {
    neverc_arena_t *a = arena.new();
    CHECK("arena.new", a != NULL);

    int *p = (int *)arena.alloc(a, sizeof(int));
    CHECK("arena.alloc", p != NULL);
    *p = 42;
    CHECK("arena.alloc_value", *p == 42);

    char *s = arena.strdup(a, "hello");
    CHECK("arena.strdup", s != NULL && strcmp(s, "hello") == 0);

    CHECK("arena.bytes_allocated", arena.bytes_allocated(a) > 0);
    CHECK("arena.num_chunks", arena.num_chunks(a) >= 1);

    arena.reset(a);
    CHECK("arena.reset", arena.bytes_allocated(a) == 0);

    arena.free(a);
}

static void test_unique_dot_syntax(void) {
    unique.init();
    neverc_unique_handle_t h1 = unique.make_string("test");
    neverc_unique_handle_t h2 = unique.make_string("test");
    CHECK("unique.handle_equal", unique.handle_equal(h1, h2));
    CHECK("unique.handle_valid", unique.handle_valid(h1));
    CHECK("unique.string_value", strcmp(unique.string_value(h1), "test") == 0);

    neverc_unique_handle_t h3 = unique.make_int64(42);
    CHECK("unique.int64_value", unique.int64_value(h3) == 42);

    CHECK("unique.count", unique.count() >= 2);
    unique.destroy();
}

static void test_weak_dot_syntax(void) {
    int data = 99;
    neverc_weak_strong_t s = weak.new(&data, sizeof(int));
    CHECK("weak.new", s.ptr != NULL);
    CHECK("weak.strong_count", weak.strong_count(s) == 1);

    neverc_weak_ref_t *w = weak.make(s);
    CHECK("weak.make", w != NULL);
    CHECK("weak.value", *(int *)weak.value(w) == 99);

    neverc_weak_strong_t s2 = weak.upgrade(w);
    CHECK("weak.upgrade", s2.ptr != NULL);
    CHECK("weak.strong_count_2", weak.strong_count(s) == 2);

    weak.strong_release(&s);
    weak.strong_release(&s2);
    CHECK("weak.expired", weak.value(w) == NULL);
    weak.ref_release(w);
}

static void test_net_dot_syntax(void) {
    CHECK("net.http.status_text_200",
          strcmp(net.http.status_text(200), "OK") == 0);
    CHECK("net.http.status_text_404",
          strcmp(net.http.status_text(404), "Not Found") == 0);

    char qbuf[64];
    const char *qv = net.http.query_get("name=John&age=30", "name",
                                         qbuf, sizeof(qbuf));
    CHECK("net.http.query_get", qv != NULL && strcmp(qv, "John") == 0);

    neverc_url_t u;
    CHECK("net.url.parse",
          net.url.parse(&u, "https://example.com/path?q=1") == 0);
    CHECK("net.url.is_abs", net.url.is_abs(&u) == 1);

    const char *err = NULL;
    neverc_tcp_listener_t *ln = net.tcp.listen("127.0.0.1:0", &err);
    CHECK("net.tcp.listen", ln != NULL);
    if (ln)
        net.tcp.listener_close(ln);
}

#else
static void test_math_dot_syntax(void) { CHECK("dot_syntax_unavailable_math", 1); }
static void test_strconv_dot_syntax(void) { CHECK("dot_syntax_unavailable_strconv", 1); }
static void test_encoding_dot_syntax(void) { CHECK("dot_syntax_unavailable_encoding", 1); }
static void test_hash_dot_syntax(void) { CHECK("dot_syntax_unavailable_hash", 1); }
static void test_container_vector_dot_syntax(void) { CHECK("dot_syntax_unavailable_container_vector", 1); }
static void test_sort_dot_syntax(void) { CHECK("dot_syntax_unavailable_sort", 1); }
static void test_rand_dot_syntax(void) { CHECK("dot_syntax_unavailable_rand", 1); }
static void test_cmp_dot_syntax(void) { CHECK("dot_syntax_unavailable_cmp", 1); }
static void test_errors_dot_syntax(void) { CHECK("dot_syntax_unavailable_errors", 1); }
static void test_crypto_sha256_dot_syntax(void) { CHECK("dot_syntax_unavailable_crypto", 1); }
static void test_crypto_hkdf_sha512_dot_syntax(void) { CHECK("dot_syntax_unavailable_hkdf_sha512", 1); }
static void test_manifest_dot_method_resolution(void) { CHECK("dot_syntax_unavailable_manifest_methods", 1); }
static void test_bytes_dot_syntax(void) { CHECK("dot_syntax_unavailable_bytes", 1); }
static void test_unicode_dot_syntax(void) { CHECK("dot_syntax_unavailable_unicode", 1); }
static void test_html_dot_syntax(void) { CHECK("dot_syntax_unavailable_html", 1); }
static void test_path_dot_syntax(void) { CHECK("dot_syntax_unavailable_path", 1); }
static void test_slices_dot_syntax(void) { CHECK("dot_syntax_unavailable_slices", 1); }
static void test_maps_dot_syntax(void) { CHECK("dot_syntax_unavailable_maps", 1); }
static void test_regexp_dot_syntax(void) { CHECK("dot_syntax_unavailable_regexp", 1); }
static void test_uuid_dot_syntax(void) { CHECK("dot_syntax_unavailable_uuid", 1); }
static void test_hash_fnv_dot_syntax(void) { CHECK("dot_syntax_unavailable_hash_fnv", 1); }
static void test_hash_adler32_dot_syntax(void) { CHECK("dot_syntax_unavailable_hash_adler32", 1); }
static void test_encoding_base64_dot_syntax(void) { CHECK("dot_syntax_unavailable_encoding_base64", 1); }
static void test_bits_dot_syntax(void) { CHECK("dot_syntax_unavailable_bits", 1); }
static void test_fmt_dot_syntax(void) { CHECK("dot_syntax_unavailable_fmt", 1); }
static void test_cstring_dot_syntax(void) { CHECK("dot_syntax_unavailable_cstring", 1); }
static void test_context_dot_syntax(void) { CHECK("dot_syntax_unavailable_context", 1); }
static void test_io_dot_syntax(void) { CHECK("dot_syntax_unavailable_io", 1); }
static void test_time_dot_syntax(void) { CHECK("dot_syntax_unavailable_time", 1); }
static void test_compress_dot_syntax(void) { CHECK("dot_syntax_unavailable_compress", 1); }
static void test_sync_dot_syntax(void) { CHECK("dot_syntax_unavailable_sync", 1); }
static void test_atomic_dot_syntax(void) { CHECK("dot_syntax_unavailable_atomic", 1); }
static void test_os_dot_syntax(void) { CHECK("dot_syntax_unavailable_os", 1); }
static void test_log_dot_syntax(void) { CHECK("dot_syntax_unavailable_log", 1); }
static void test_json_dot_syntax(void) { CHECK("dot_syntax_unavailable_json", 1); }
static void test_binary_dot_syntax(void) { CHECK("dot_syntax_unavailable_binary", 1); }
static void test_list_dot_syntax(void) { CHECK("dot_syntax_unavailable_list", 1); }
static void test_ring_dot_syntax(void) { CHECK("dot_syntax_unavailable_ring", 1); }
static void test_filepath_dot_syntax(void) { CHECK("dot_syntax_unavailable_filepath", 1); }
static void test_sha1_dot_syntax(void) { CHECK("dot_syntax_unavailable_sha1", 1); }
static void test_md5_dot_syntax(void) { CHECK("dot_syntax_unavailable_md5", 1); }
static void test_crc64_dot_syntax(void) { CHECK("dot_syntax_unavailable_crc64", 1); }
static void test_color_dot_syntax(void) { CHECK("dot_syntax_unavailable_color", 1); }
static void test_flag_dot_syntax(void) { CHECK("dot_syntax_unavailable_flag", 1); }
static void test_arena_dot_syntax(void) { CHECK("dot_syntax_unavailable_arena", 1); }
static void test_unique_dot_syntax(void) { CHECK("dot_syntax_unavailable_unique", 1); }
static void test_weak_dot_syntax(void) { CHECK("dot_syntax_unavailable_weak", 1); }
static void test_net_dot_syntax(void) { CHECK("dot_syntax_unavailable_net", 1); }
#endif /* __neverc__ */

int main(void) {
    test_math_dot_syntax();
    test_strconv_dot_syntax();
    test_encoding_dot_syntax();
    test_hash_dot_syntax();
    test_container_vector_dot_syntax();
    test_sort_dot_syntax();
    test_rand_dot_syntax();
    test_cmp_dot_syntax();
    test_errors_dot_syntax();
    test_crypto_sha256_dot_syntax();
    test_crypto_hkdf_sha512_dot_syntax();
    test_manifest_dot_method_resolution();
    test_bytes_dot_syntax();
    test_unicode_dot_syntax();
    test_html_dot_syntax();
    test_path_dot_syntax();
    test_slices_dot_syntax();
    test_maps_dot_syntax();
    test_regexp_dot_syntax();
    test_uuid_dot_syntax();
    test_hash_fnv_dot_syntax();
    test_hash_adler32_dot_syntax();
    test_encoding_base64_dot_syntax();
    test_bits_dot_syntax();
    test_fmt_dot_syntax();
    test_cstring_dot_syntax();
    test_context_dot_syntax();
    test_io_dot_syntax();
    test_time_dot_syntax();
    test_compress_dot_syntax();
    test_sync_dot_syntax();
    test_atomic_dot_syntax();
    test_os_dot_syntax();
    test_log_dot_syntax();
    test_json_dot_syntax();
    test_binary_dot_syntax();
    test_list_dot_syntax();
    test_ring_dot_syntax();
    test_filepath_dot_syntax();
    test_sha1_dot_syntax();
    test_md5_dot_syntax();
    test_crc64_dot_syntax();
    test_color_dot_syntax();
    test_flag_dot_syntax();
    test_arena_dot_syntax();
    test_unique_dot_syntax();
    test_weak_dot_syntax();
    test_net_dot_syntax();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0)
        printf("%d tests FAILED\n", tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
