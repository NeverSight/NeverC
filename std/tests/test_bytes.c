#include "neverc/bytes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define B(s) ((const uint8_t *)(s)), (sizeof(s) - 1)
#define NOT_FOUND ((size_t)-1)

static void check_bool(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_size(const char *name, size_t got, size_t expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %zu, expected %zu\n", name, got, expected); }
}

static void check_bytes(const char *name, const uint8_t *got, size_t glen,
                         const char *expected) {
    tests_run++;
    size_t elen = strlen(expected);
    if (glen == elen && memcmp(got, expected, glen) == 0) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got \"", name);
        fwrite(got, 1, glen, stdout);
        printf("\" (len=%zu), expected \"%s\" (len=%zu)\n", glen, expected, elen);
    }
}

static void test_compare(void) {
    printf("[compare]\n");
    check_bool("equal same", neverc_bytes_equal(B("hello"), B("hello")), 1);
    check_bool("equal diff", neverc_bytes_equal(B("hello"), B("world")), 0);
    check_bool("equal difflen", neverc_bytes_equal(B("hi"), B("hii")), 0);
    check_bool("equal empty", neverc_bytes_equal(B(""), B("")), 1);

    check_bool("compare eq", neverc_bytes_compare(B("abc"), B("abc")), 0);
    check_bool("compare lt", neverc_bytes_compare(B("abc"), B("abd")) < 0, 1);
    check_bool("compare gt", neverc_bytes_compare(B("abd"), B("abc")) > 0, 1);
    check_bool("compare shorter", neverc_bytes_compare(B("ab"), B("abc")) < 0, 1);

    check_bool("equalfold yes", neverc_bytes_equal_fold(B("Hello"), B("hELLO")), 1);
    check_bool("equalfold no", neverc_bytes_equal_fold(B("Hello"), B("world")), 0);
}

static void test_search(void) {
    printf("[search]\n");
    check_size("index found", neverc_bytes_index(B("hello world"), B("world")), 6);
    check_size("index not found", neverc_bytes_index(B("hello"), B("xyz")), NOT_FOUND);
    check_size("index empty sep", neverc_bytes_index(B("hello"), B("")), 0);
    check_size("index byte", neverc_bytes_index_byte(B("hello"), 'l'), 2);
    check_size("index byte none", neverc_bytes_index_byte(B("hello"), 'z'), NOT_FOUND);

    check_size("last index", neverc_bytes_last_index(B("go gopher"), B("go")), 3);
    check_size("last index byte", neverc_bytes_last_index_byte(B("hello"), 'l'), 3);
    check_size("last index none", neverc_bytes_last_index(B("hello"), B("xyz")), NOT_FOUND);

    check_size("index any", neverc_bytes_index_any(B("chicken"), "aeiou"), 2);
    check_size("index any none", neverc_bytes_index_any(B("crwth"), "aeiou"), NOT_FOUND);
    check_size("last index any", neverc_bytes_last_index_any(B("chicken"), "aeiou"), 5);

    check_bool("contains yes", neverc_bytes_contains(B("seafood"), B("foo")), 1);
    check_bool("contains no", neverc_bytes_contains(B("seafood"), B("bar")), 0);
    check_bool("contains byte", neverc_bytes_contains_byte(B("hello"), 'e'), 1);
    check_bool("contains any", neverc_bytes_contains_any(B("hello"), "aeiou"), 1);
    check_bool("contains any no", neverc_bytes_contains_any(B("crwth"), "aeiou"), 0);

    check_size("count", neverc_bytes_count(B("cheese"), B("e")), 3);
    check_size("count none", neverc_bytes_count(B("five"), B("x")), 0);
    check_size("count empty", neverc_bytes_count(B("abc"), B("")), 4);
    check_size("count overlap", neverc_bytes_count(B("aaa"), B("a")), 3);
}

static void test_prefix_suffix(void) {
    printf("[prefix/suffix]\n");
    check_bool("has prefix yes", neverc_bytes_has_prefix(B("Gopher"), B("Go")), 1);
    check_bool("has prefix no", neverc_bytes_has_prefix(B("Gopher"), B("C")), 0);
    check_bool("has prefix empty", neverc_bytes_has_prefix(B("Gopher"), B("")), 1);
    check_bool("has suffix yes", neverc_bytes_has_suffix(B("Amigo"), B("go")), 1);
    check_bool("has suffix no", neverc_bytes_has_suffix(B("Amigo"), B("Ami")), 0);
}

static void test_transform(void) {
    printf("[transform]\n");
    size_t outlen;

    uint8_t *upper = neverc_bytes_to_upper(B("hello, World!"), &outlen);
    check_bytes("to_upper", upper, outlen, "HELLO, WORLD!");
    free(upper);

    uint8_t *lower = neverc_bytes_to_lower(B("Hello, WORLD!"), &outlen);
    check_bytes("to_lower", lower, outlen, "hello, world!");
    free(lower);

    uint8_t *title = neverc_bytes_to_title(B("hello world"), &outlen);
    check_bytes("to_title", title, outlen, "Hello World");
    free(title);

    uint8_t *rep = neverc_bytes_repeat(B("ab"), 3, &outlen);
    check_bytes("repeat", rep, outlen, "ababab");
    free(rep);

    uint8_t *rep0 = neverc_bytes_repeat(B("ab"), 0, &outlen);
    check_size("repeat 0 len", outlen, 0);
    free(rep0);

    uint8_t *repl = neverc_bytes_replace(B("oink oink oink"),
                                         B("oink"), B("moo"), 2, &outlen);
    check_bytes("replace 2", repl, outlen, "moo moo oink");
    free(repl);

    uint8_t *repla = neverc_bytes_replace_all(B("oink oink oink"),
                                              B("oink"), B("moo"), &outlen);
    check_bytes("replace all", repla, outlen, "moo moo moo");
    free(repla);

    uint8_t *repl_grow = neverc_bytes_replace_all(B("ab"), B("a"), B("xyz"), &outlen);
    check_bytes("replace grow", repl_grow, outlen, "xyzb");
    free(repl_grow);

    uint8_t *repl_shrink = neverc_bytes_replace_all(B("aabbcc"),
                                                     B("bb"), B(""), &outlen);
    check_bytes("replace shrink", repl_shrink, outlen, "aacc");
    free(repl_shrink);
}

static void test_trim(void) {
    printf("[trim]\n");
    size_t outlen;

    uint8_t *ts = neverc_bytes_trim_space(B("  hello  \n"), &outlen);
    check_bytes("trim_space", ts, outlen, "hello");
    free(ts);

    uint8_t *t = neverc_bytes_trim(B("!!!hello!!!"), "!", &outlen);
    check_bytes("trim", t, outlen, "hello");
    free(t);

    uint8_t *tl = neverc_bytes_trim_left(B("xxxhelloxx"), "x", &outlen);
    check_bytes("trim_left", tl, outlen, "helloxx");
    free(tl);

    uint8_t *tr = neverc_bytes_trim_right(B("xxhelloxxx"), "x", &outlen);
    check_bytes("trim_right", tr, outlen, "xxhello");
    free(tr);

    uint8_t *tp = neverc_bytes_trim_prefix(B("Hello, World"), B("Hello, "), &outlen);
    check_bytes("trim_prefix", tp, outlen, "World");
    free(tp);

    uint8_t *tp2 = neverc_bytes_trim_prefix(B("Hello"), B("xyz"), &outlen);
    check_bytes("trim_prefix nomatch", tp2, outlen, "Hello");
    free(tp2);

    uint8_t *tsuf = neverc_bytes_trim_suffix(B("Hello, World"), B(", World"), &outlen);
    check_bytes("trim_suffix", tsuf, outlen, "Hello");
    free(tsuf);
}

static void test_split(void) {
    printf("[split]\n");
    size_t count;

    neverc_bytes_slice_t *parts = neverc_bytes_split(B("a,b,c"), B(","), &count);
    check_size("split count", count, 3);
    if (count >= 3) {
        check_bytes("split[0]", parts[0].data, parts[0].len, "a");
        check_bytes("split[1]", parts[1].data, parts[1].len, "b");
        check_bytes("split[2]", parts[2].data, parts[2].len, "c");
    }
    free(parts);

    neverc_bytes_slice_t *parts2 = neverc_bytes_split_n(B("a,b,c"), B(","), 2, &count);
    check_size("split_n count", count, 2);
    if (count >= 2) {
        check_bytes("split_n[0]", parts2[0].data, parts2[0].len, "a");
        check_bytes("split_n[1]", parts2[1].data, parts2[1].len, "b,c");
    }
    free(parts2);

    neverc_bytes_slice_t *parts3 = neverc_bytes_split(B("a,,b"), B(","), &count);
    check_size("split empty middle", count, 3);
    if (count >= 3) {
        check_bytes("split empty[0]", parts3[0].data, parts3[0].len, "a");
        check_bytes("split empty[1]", parts3[1].data, parts3[1].len, "");
        check_bytes("split empty[2]", parts3[2].data, parts3[2].len, "b");
    }
    free(parts3);

    neverc_bytes_slice_t *fields = neverc_bytes_fields(B("  foo  bar  baz  "), &count);
    check_size("fields count", count, 3);
    if (count >= 3) {
        check_bytes("fields[0]", fields[0].data, fields[0].len, "foo");
        check_bytes("fields[1]", fields[1].data, fields[1].len, "bar");
        check_bytes("fields[2]", fields[2].data, fields[2].len, "baz");
    }
    free(fields);

    neverc_bytes_slice_t *fempty = neverc_bytes_fields(B("   "), &count);
    check_size("fields empty", count, 0);
    free(fempty);
}

static void test_join(void) {
    printf("[join]\n");
    size_t outlen;
    const uint8_t *slices[] = {(const uint8_t *)"foo",
                               (const uint8_t *)"bar",
                               (const uint8_t *)"baz"};
    size_t lens[] = {3, 3, 3};

    uint8_t *j = neverc_bytes_join(slices, lens, 3, B(", "), &outlen);
    check_bytes("join", j, outlen, "foo, bar, baz");
    free(j);

    uint8_t *j1 = neverc_bytes_join(slices, lens, 1, B(", "), &outlen);
    check_bytes("join single", j1, outlen, "foo");
    free(j1);
}

static void test_cut(void) {
    printf("[cut]\n");
    const uint8_t *before, *after;
    size_t blen, alen;

    int found = neverc_bytes_cut(B("Gopher"), B("ph"),
                                &before, &blen, &after, &alen);
    check_bool("cut found", found, 1);
    check_bytes("cut before", before, blen, "Go");
    check_bytes("cut after", after, alen, "er");

    found = neverc_bytes_cut(B("Gopher"), B("xyz"),
                            &before, &blen, &after, &alen);
    check_bool("cut not found", found, 0);
    check_bytes("cut before nf", before, blen, "Gopher");

    found = neverc_bytes_cut_prefix(B("Hello, World"), B("Hello, "),
                                   &after, &alen);
    check_bool("cut_prefix found", found, 1);
    check_bytes("cut_prefix after", after, alen, "World");

    found = neverc_bytes_cut_suffix(B("Hello, World"), B(", World"),
                                   &before, &blen);
    check_bool("cut_suffix found", found, 1);
    check_bytes("cut_suffix before", before, blen, "Hello");
}

static void test_clone(void) {
    printf("[clone]\n");
    uint8_t *cloned = neverc_bytes_clone(B("hello"));
    check_bytes("clone", cloned, 5, "hello");
    free(cloned);

    uint8_t *empty = neverc_bytes_clone(B(""));
    check_bool("clone empty is null", empty == NULL, 1);
}

static int is_space(uint8_t c) { return c == ' ' || c == '\t' || c == '\n'; }
static int is_upper(uint8_t c) { return c >= 'A' && c <= 'Z'; }
static uint8_t to_upper_map(uint8_t c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static void test_func_ops(void) {
    printf("[func operations]\n");

    check_bool("contains_func space in 'a b'", 
        neverc_bytes_contains_func(B("a b"), is_space), 1);
    check_bool("contains_func space in 'abc'", 
        neverc_bytes_contains_func(B("abc"), is_space), 0);

    size_t idx = neverc_bytes_index_func(B("hello World"), is_upper);
    check_bool("index_func uppercase", idx == 6, 1);

    idx = neverc_bytes_index_func(B("hello"), is_upper);
    check_bool("index_func no match", idx == (size_t)-1, 1);

    idx = neverc_bytes_last_index_func(B("Hello World"), is_upper);
    check_bool("last_index_func", idx == 6, 1);

    size_t olen;
    uint8_t *r = neverc_bytes_trim_func(B("  hello  "), is_space, &olen);
    check_bool("trim_func spaces", olen == 5 && memcmp(r, "hello", 5) == 0, 1);
    free(r);

    r = neverc_bytes_trim_left_func(B("  hello"), is_space, &olen);
    check_bool("trim_left_func", olen == 5 && memcmp(r, "hello", 5) == 0, 1);
    free(r);

    r = neverc_bytes_trim_right_func(B("hello  "), is_space, &olen);
    check_bool("trim_right_func", olen == 5 && memcmp(r, "hello", 5) == 0, 1);
    free(r);
}

static void test_map(void) {
    printf("[map]\n");
    size_t olen;
    uint8_t *r = neverc_bytes_map(to_upper_map, B("hello"), &olen);
    check_bool("map to_upper", olen == 5 && memcmp(r, "HELLO", 5) == 0, 1);
    free(r);
}

static void test_split_after(void) {
    printf("[split_after]\n");
    size_t count;
    neverc_bytes_slice_t *parts = neverc_bytes_split_after(
        B("a,b,c"), (const uint8_t *)",", 1, &count);
    check_bool("split_after count", count == 3, 1);
    if (count >= 3) {
        check_bool("split_after[0]", parts[0].len == 2 && memcmp(parts[0].data, "a,", 2) == 0, 1);
        check_bool("split_after[1]", parts[1].len == 2 && memcmp(parts[1].data, "b,", 2) == 0, 1);
        check_bool("split_after[2]", parts[2].len == 1 && parts[2].data[0] == 'c', 1);
    }
    free(parts);
}

static void test_fields_func(void) {
    printf("[fields_func]\n");
    size_t count;
    neverc_bytes_slice_t *parts = neverc_bytes_fields_func(
        B("  hello  world  "), is_space, &count);
    check_bool("fields_func count", count == 2, 1);
    if (count >= 2) {
        check_bool("fields_func[0]", parts[0].len == 5 && memcmp(parts[0].data, "hello", 5) == 0, 1);
        check_bool("fields_func[1]", parts[1].len == 5 && memcmp(parts[1].data, "world", 5) == 0, 1);
    }
    free(parts);
}

int main(void) {
    printf("=== NeverC Bytes Module Tests ===\n\n");
    test_compare();
    test_search();
    test_prefix_suffix();
    test_transform();
    test_trim();
    test_split();
    test_join();
    test_cut();
    test_clone();
    test_func_ops();
    test_map();
    test_split_after();
    test_fields_func();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
