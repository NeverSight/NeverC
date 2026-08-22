#include "neverc/std/bytes.h"
#include <stdint.h>
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
    check_bool("compare invalid vs nonempty",
               neverc_bytes_compare(NULL, 5, B("hello")) < 0, 1);
    check_bool("compare nonempty vs invalid",
               neverc_bytes_compare(B("hello"), NULL, 5) > 0, 1);

    check_bool("equalfold yes", neverc_bytes_equal_fold(B("Hello"), B("hELLO")), 1);
    check_bool("equalfold no", neverc_bytes_equal_fold(B("Hello"), B("world")), 0);
    check_bool("equalfold word path", neverc_bytes_equal_fold(
                   B("ABCDEFGH"), B("abcdefgh")), 1);
    check_bool("equalfold punctuation is not case", neverc_bytes_equal_fold(
                   B("@@@@@@@@"), B("````````")), 0);
    /* Go bytes.EqualFold: SimpleFold, lengths need not match. */
    check_bool("equalfold kelvin", neverc_bytes_equal_fold(
                   B("K"), (const uint8_t *)"\xe2\x84\xaa", 3), 1);
    check_bool("equalfold long s", neverc_bytes_equal_fold(
                   B("s"), (const uint8_t *)"\xc5\xbf", 2), 1);
    check_bool("equalfold sigma", neverc_bytes_equal_fold(
                   (const uint8_t *)"\xce\xa3", 2,
                   (const uint8_t *)"\xcf\x83", 2), 1);
    /* Go 1.23 SimpleFold has no ΐ/ΐ or ﬅ/ﬆ orbit. */
    check_bool("equalfold iota dialytika", neverc_bytes_equal_fold(
                   (const uint8_t *)"\xce\x90", 2,
                   (const uint8_t *)"\xe1\xbf\x93", 3), 0);
    check_bool("equalfold st ligatures", neverc_bytes_equal_fold(
                   (const uint8_t *)"\xef\xac\x85", 3,
                   (const uint8_t *)"\xef\xac\x86", 3), 0);
}

static void test_search(void) {
    printf("[search]\n");
    check_size("index found", neverc_bytes_index(B("hello world"), B("world")), 6);
    check_size("index not found", neverc_bytes_index(B("hello"), B("xyz")), NOT_FOUND);
    check_size("index empty sep", neverc_bytes_index(B("hello"), B("")), 0);
    check_size("index empty both", neverc_bytes_index(NULL, 0, NULL, 0), 0);
    check_size("index needle longer", neverc_bytes_index(B("ab"), B("abcd")), NOT_FOUND);
    check_size("index prefix at last byte", neverc_bytes_index(B("abc"), B("cX")), NOT_FOUND);
    check_size("index invalid haystack", neverc_bytes_index(NULL, 5, B("x")), NOT_FOUND);
    check_size("index invalid needle", neverc_bytes_index(B("hello"), NULL, 5), NOT_FOUND);
    check_size("index byte", neverc_bytes_index_byte(B("hello"), 'l'), 2);
    check_size("index byte none", neverc_bytes_index_byte(B("hello"), 'z'), NOT_FOUND);

    static const uint8_t with_nul[] = { 'a', 0, 'b', 0, 'c' };
    check_size("index byte NUL",
               neverc_bytes_index_byte(with_nul, sizeof(with_nul), 0), 1);
    check_size("last index byte NUL",
               neverc_bytes_last_index_byte(with_nul, sizeof(with_nul), 0), 3);
    check_size("index NUL needle",
               neverc_bytes_index(with_nul, sizeof(with_nul), with_nul + 1, 1),
               1);
    check_size("last index NUL needle",
               neverc_bytes_last_index(with_nul, sizeof(with_nul),
                                       with_nul + 1, 1),
               3);

    check_size("last index", neverc_bytes_last_index(B("go gopher"), B("go")), 3);
    check_size("last index empty sep", neverc_bytes_last_index(B("hello"), B("")), 5);
    check_size("last index needle longer",
               neverc_bytes_last_index(B("ab"), B("abcd")), NOT_FOUND);
    check_size("last index invalid haystack",
               neverc_bytes_last_index(NULL, 5, B("x")), NOT_FOUND);
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

    static const uint8_t unicode[] = {
        'a', 0xc3, 0xa9, 0xe4, 0xb8, 0x96, 0xe7, 0x95, 0x8c
    };
    check_size("index any Unicode rune",
               neverc_bytes_index_any(
                   unicode, sizeof(unicode), "\xe7\x95\x8c\xe4\xb8\x96"),
               3);
    check_size("last index any Unicode rune",
               neverc_bytes_last_index_any(
                   unicode, sizeof(unicode), "\xe7\x95\x8c\xe4\xb8\x96"),
               6);
    static const uint8_t shared_utf8_byte[] = {0xc4, 0x96};
    check_size("index any does not match UTF-8 fragments",
               neverc_bytes_index_any(
                   shared_utf8_byte, sizeof(shared_utf8_byte),
                   "\xe4\xb8\x96"),
               NOT_FOUND);
    static const uint8_t invalid_rune[] = {0xff};
    check_size("index any matches malformed UTF-8 as RuneError",
               neverc_bytes_index_any(
                   invalid_rune, sizeof(invalid_rune), "\xfe"),
               0);

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

    title = neverc_bytes_to_title(B("one-two.three"), &outlen);
    check_bytes("to_title separators", title, outlen, "One-Two.Three");
    free(title);

    /* Split hex escapes so `\xAD` is not parsed as `\xADb`. */
    title = neverc_bytes_to_title((const uint8_t *)"a" "\xE4\xB8\xAD" "b", 5, &outlen);
    check_bytes("to_title utf8 letter not sep", title, outlen,
                "A" "\xE4\xB8\xAD" "b");
    free(title);

    /* Go bytes.Title: unicode.ToTitle on the first rune after a separator. */
    title = neverc_bytes_to_title((const uint8_t *)"\xC3\xBC" "ber", 5, &outlen);
    check_bytes("to_title umlaut", title, outlen, "\xC3\x9C" "ber");
    free(title);
    title = neverc_bytes_to_title(
        (const uint8_t *)"\xCE\xB1" "\xCE\xB8" "\xCE\xAE" "\xCE\xBD" "\xCE\xB1",
        10, &outlen);
    check_bytes("to_title greek word", title, outlen,
                "\xCE\x91" "\xCE\xB8" "\xCE\xAE" "\xCE\xBD" "\xCE\xB1");
    free(title);
    /* U+017F long s titles to 'S' and shrinks from 2 UTF-8 bytes to 1. */
    title = neverc_bytes_to_title((const uint8_t *)"\xC5\xBF" "word", 6, &outlen);
    check_bytes("to_title long s width", title, outlen, "Sword");
    free(title);

    uint8_t *rep = neverc_bytes_repeat(B("ab"), 3, &outlen);
    check_bytes("repeat", rep, outlen, "ababab");
    free(rep);

    uint8_t *rep0 = neverc_bytes_repeat(B("ab"), 0, &outlen);
    check_size("repeat 0 len", outlen, 0);
    free(rep0);

    outlen = 99;
    uint8_t *rep_negative = neverc_bytes_repeat(B("ab"), -1, &outlen);
    check_bool("repeat negative rejected", rep_negative == NULL, 1);
    check_size("repeat negative length", outlen, 0);

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

    uint8_t *repl_excess = neverc_bytes_replace(B("a-b"), B("-"), B("::"),
                                                100, &outlen);
    check_bytes("replace count exceeds matches", repl_excess, outlen, "a::b");
    free(repl_excess);

    uint8_t *repl0 = neverc_bytes_replace(B("abc"), B("a"), B("x"), 0, &outlen);
    check_bytes("replace n=0", repl0, outlen, "abc");
    free(repl0);

    uint8_t *repl_empty = neverc_bytes_replace_all(B("ab"), B(""), B("-"),
                                                   &outlen);
    check_bytes("replace empty old", repl_empty, outlen, "-a-b-");
    free(repl_empty);

    uint8_t one = 1;
    outlen = 99;
    uint8_t *overflow = neverc_bytes_repeat(&one, SIZE_MAX / 2 + 1, 2,
                                            &outlen);
    check_bool("repeat overflow rejected", overflow == NULL, 1);
    check_size("repeat overflow length", outlen, 0);
    free(overflow);

    uint8_t dummy = 'x';
    uint8_t aa[] = {'a', 'a'};
    outlen = 99;
    overflow = neverc_bytes_replace(aa, sizeof(aa), aa, 1,
                                    &dummy, SIZE_MAX / 2 + 1, -1, &outlen);
    check_bool("replace grow overflow rejected", overflow == NULL, 1);
    check_size("replace grow overflow length", outlen, 0);
    free(overflow);

    outlen = 99;
    overflow = neverc_bytes_replace(aa, 1, NULL, 0,
                                    &dummy, SIZE_MAX, -1, &outlen);
    check_bool("replace empty-old overflow rejected", overflow == NULL, 1);
    check_size("replace empty-old overflow length", outlen, 0);
    free(overflow);

    static const uint8_t nul_hay[] = { 'a', 0, 'b', 0, 'c' };
    static const uint8_t nul_old[] = { 0 };
    uint8_t *repl_nul = neverc_bytes_replace(nul_hay, sizeof(nul_hay),
                                             nul_old, 1, B("X"), -1, &outlen);
    check_bytes("replace embedded NUL", repl_nul, outlen, "aXbXc");
    free(repl_nul);
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

    static const uint8_t unicode_trim[] = {
        0xe4, 0xb8, 0x96, 'h', 'e', 'l', 'l', 'o', 0xe4, 0xb8, 0x96
    };
    t = neverc_bytes_trim(
        unicode_trim, sizeof(unicode_trim), "\xe4\xb8\x96", &outlen);
    check_bytes("trim Unicode cutset", t, outlen, "hello");
    free(t);

    static const uint8_t shared_utf8_byte[] = {0xc4, 0x96};
    t = neverc_bytes_trim(
        shared_utf8_byte, sizeof(shared_utf8_byte), "\xe4\xb8\x96", &outlen);
    check_bool("trim does not split UTF-8 runes",
               outlen == sizeof(shared_utf8_byte) &&
                   memcmp(t, shared_utf8_byte, sizeof(shared_utf8_byte)) == 0,
               1);
    free(t);

    static const uint8_t unicode_space[] = {
        0xc2, 0xa0, 'h', 'e', 'l', 'l', 'o', 0xe3, 0x80, 0x80
    };
    t = neverc_bytes_trim_space(
        unicode_space, sizeof(unicode_space), &outlen);
    check_bytes("trim Unicode White_Space", t, outlen, "hello");
    free(t);

    static const uint8_t bom_wrapped[] = {
        0xef, 0xbb, 0xbf, 'x', 0xef, 0xbb, 0xbf
    };
    t = neverc_bytes_trim_space(bom_wrapped, sizeof(bom_wrapped), &outlen);
    check_bool("trim_space preserves non-White_Space BOM",
               outlen == sizeof(bom_wrapped) &&
                   memcmp(t, bom_wrapped, sizeof(bom_wrapped)) == 0,
               1);
    free(t);
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

    static const uint8_t unicode_fields[] = {
        'a', 0xc2, 0xa0, 'b', 0xe3, 0x80, 0x80, 'c'
    };
    neverc_bytes_slice_t *ufields = neverc_bytes_fields(
        unicode_fields, sizeof(unicode_fields), &count);
    check_size("fields Unicode White_Space count", count, 3);
    if (count >= 3) {
        check_bytes("fields Unicode[0]",
                    ufields[0].data, ufields[0].len, "a");
        check_bytes("fields Unicode[1]",
                    ufields[1].data, ufields[1].len, "b");
        check_bytes("fields Unicode[2]",
                    ufields[2].data, ufields[2].len, "c");
    }
    free(ufields);

    neverc_bytes_slice_t *empty_runes = neverc_bytes_split_n(
        NULL, 0, NULL, 0, 1, &count);
    check_size("split_n empty input and separator", count, 0);
    free(empty_runes);

    static const uint8_t utf8[] = {
        'A', 0xE4, 0xB8, 0x96, 0xF0, 0x9F, 0x98, 0x80, 0xFF, 'B'
    };
    neverc_bytes_slice_t *runes = neverc_bytes_split(
        utf8, sizeof(utf8), NULL, 0, &count);
    check_size("split empty separator UTF-8 count", count, 5);
    if (count >= 5) {
        check_bytes("split UTF-8[0]", runes[0].data, runes[0].len, "A");
        check_bool("split UTF-8[1]", runes[1].len == 3 &&
                   memcmp(runes[1].data, utf8 + 1, 3) == 0, 1);
        check_bool("split UTF-8[2]", runes[2].len == 4 &&
                   memcmp(runes[2].data, utf8 + 4, 4) == 0, 1);
        check_bool("split invalid UTF-8 byte", runes[3].len == 1 &&
                   runes[3].data[0] == 0xFF, 1);
        check_bytes("split UTF-8[4]", runes[4].data, runes[4].len, "B");
    }
    free(runes);

    runes = neverc_bytes_split_n(utf8, sizeof(utf8), NULL, 0, 3, &count);
    check_size("split_n empty separator UTF-8 count", count, 3);
    if (count >= 3) {
        check_bytes("split_n UTF-8[0]", runes[0].data, runes[0].len, "A");
        check_bool("split_n UTF-8[1]", runes[1].len == 3 &&
                   memcmp(runes[1].data, utf8 + 1, 3) == 0, 1);
        check_bool("split_n UTF-8 remainder", runes[2].len == 6 &&
                   memcmp(runes[2].data, utf8 + 4, 6) == 0, 1);
    }
    free(runes);
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

    uint8_t *j0 = neverc_bytes_join(NULL, NULL, 0, NULL, 5, &outlen);
    check_size("join empty ignores invalid sep", outlen, 0);
    free(j0);

    const uint8_t one = 1;
    const uint8_t *overflow_slices[] = {&one, &one};
    size_t overflow_lens[] = {SIZE_MAX, 1};
    outlen = 99;
    uint8_t *overflow = neverc_bytes_join(overflow_slices, overflow_lens, 2,
                                          NULL, 0, &outlen);
    check_bool("join overflow rejected", overflow == NULL, 1);
    check_size("join overflow length", outlen, 0);
    free(overflow);
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

    found = neverc_bytes_cut(NULL, 0, NULL, 0,
                            &before, &blen, &after, &alen);
    check_bool("cut empty nil span", found, 1);
    check_bool("cut empty nil before", before == NULL && blen == 0, 1);
    check_bool("cut empty nil after", after == NULL && alen == 0, 1);

    before = (const uint8_t *)(uintptr_t)0x1;
    after = (const uint8_t *)(uintptr_t)0x1;
    blen = 99;
    alen = 99;
    found = neverc_bytes_cut(NULL, 5, B("x"), &before, &blen, &after, &alen);
    check_bool("cut invalid span not found", found, 0);
    check_bool("cut invalid span writes outputs",
               before == NULL && blen == 0 && after == NULL && alen == 0, 1);
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

    parts = neverc_bytes_split_after(B("a,"), B(","), &count);
    check_size("split_after trailing separator count", count, 2);
    if (count >= 2) {
        check_bytes("split_after trailing[0]", parts[0].data,
                    parts[0].len, "a,");
        check_bytes("split_after trailing[1]", parts[1].data,
                    parts[1].len, "");
    }
    free(parts);

    parts = neverc_bytes_split_after(NULL, 0, B(","), &count);
    check_size("split_after empty input count", count, 1);
    if (count >= 1)
        check_size("split_after empty input length", parts[0].len, 0);
    free(parts);

    static const uint8_t utf8[] = {
        'A', 0xE4, 0xB8, 0x96, 0xF0, 0x9F, 0x98, 0x80, 'B'
    };
    parts = neverc_bytes_split_after(utf8, sizeof(utf8), NULL, 0, &count);
    check_size("split_after empty separator UTF-8 count", count, 4);
    if (count >= 4) {
        check_bytes("split_after UTF-8[0]", parts[0].data, parts[0].len, "A");
        check_bool("split_after UTF-8[1]", parts[1].len == 3 &&
                   memcmp(parts[1].data, utf8 + 1, 3) == 0, 1);
        check_bool("split_after UTF-8[2]", parts[2].len == 4 &&
                   memcmp(parts[2].data, utf8 + 4, 4) == 0, 1);
        check_bytes("split_after UTF-8[3]", parts[3].data, parts[3].len, "B");
    }
    free(parts);

    parts = neverc_bytes_split_after_n(
        utf8, sizeof(utf8), NULL, 0, 3, &count);
    check_size("split_after_n empty separator UTF-8 count", count, 3);
    if (count >= 3) {
        check_bytes("split_after_n UTF-8[0]", parts[0].data, parts[0].len, "A");
        check_bool("split_after_n UTF-8[1]", parts[1].len == 3 &&
                   memcmp(parts[1].data, utf8 + 1, 3) == 0, 1);
        check_bool("split_after_n UTF-8 remainder", parts[2].len == 5 &&
                   memcmp(parts[2].data, utf8 + 4, 5) == 0, 1);
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

static void test_cut_last(void) {
    printf("[cut_last]\n");
    const uint8_t *before, *after;
    size_t blen, alen;

    int found = neverc_bytes_cut_last(B("foo::bar::baz"), B("::"), &before, &blen, &after, &alen);
    check_bool("cut_last found", found, 1);
    check_bool("cut_last before", blen == 8 && memcmp(before, "foo::bar", 8) == 0, 1);
    check_bool("cut_last after", alen == 3 && memcmp(after, "baz", 3) == 0, 1);

    found = neverc_bytes_cut_last(B("hello"), B("::"), &before, &blen, &after, &alen);
    check_bool("cut_last not found", found, 0);
    check_bool("cut_last nf before", blen == 5 && memcmp(before, "hello", 5) == 0, 1);

    found = neverc_bytes_cut_last(B("a.b.c"), B("."), &before, &blen, &after, &alen);
    check_bool("cut_last dot found", found, 1);
    check_bool("cut_last dot before", blen == 3 && memcmp(before, "a.b", 3) == 0, 1);
    check_bool("cut_last dot after", alen == 1 && after[0] == 'c', 1);

    found = neverc_bytes_cut_last(NULL, 0, NULL, 0,
                                  &before, &blen, &after, &alen);
    check_bool("cut_last empty nil span", found, 1);
    check_bool("cut_last empty nil before", before == NULL && blen == 0, 1);
    check_bool("cut_last empty nil after", after == NULL && alen == 0, 1);
}

static void test_index_rune(void) {
    printf("[index_rune]\n");
    check_bool("index_rune 'a'", neverc_bytes_index_rune(B("hello"), 'l') == 2, 1);
    check_bool("index_rune not found", neverc_bytes_index_rune(B("hello"), 'z') == (size_t)-1, 1);

    /* UTF-8 multibyte: U+4E16 (世) is 0xE4 0xB8 0x96 */
    const uint8_t s[] = { 'h', 'i', 0xE4, 0xB8, 0x96, 0 };
    check_bool("index_rune utf8", neverc_bytes_index_rune(s, 5, 0x4E16) == 2, 1);
    check_bool("contains_rune utf8",
               neverc_bytes_contains_rune(s, 5, 0x4E16), 1);
    check_bool("contains_rune not found",
               neverc_bytes_contains_rune(s, 5, 0x754C), 0);

    static const uint8_t invalid[] = { 'x', 0xFF, 'y' };
    check_size("index_rune RuneError matches invalid byte",
               neverc_bytes_index_rune(invalid, sizeof(invalid), 0xFFFD), 1);
    static const uint8_t truncated[] = { 0xE2, 0x98 };
    check_size("index_rune RuneError matches truncated sequence",
               neverc_bytes_index_rune(truncated, sizeof(truncated), 0xFFFD), 0);
    static const uint8_t literal_error[] = { 0xEF, 0xBF, 0xBD };
    check_size("index_rune RuneError matches encoded rune",
               neverc_bytes_index_rune(
                   literal_error, sizeof(literal_error), 0xFFFD), 0);
}

static void test_runes(void) {
    printf("[runes]\n");
    size_t count;
    uint32_t *r = neverc_bytes_runes(B("ABC"), &count);
    check_bool("runes ASCII count", count == 3, 1);
    check_bool("runes A", r[0] == 'A', 1);
    check_bool("runes B", r[1] == 'B', 1);
    check_bool("runes C", r[2] == 'C', 1);
    free(r);

    /* "A世B" = 0x41 0xE4 0xB8 0x96 0x42 */
    const uint8_t utf[] = { 0x41, 0xE4, 0xB8, 0x96, 0x42 };
    r = neverc_bytes_runes(utf, 5, &count);
    check_bool("runes utf8 count", count == 3, 1);
    check_bool("runes utf8[0]", r[0] == 0x41, 1);
    check_bool("runes utf8[1]", r[1] == 0x4E16, 1);
    check_bool("runes utf8[2]", r[2] == 0x42, 1);
    free(r);
}

static void test_to_valid_utf8(void) {
    printf("[to_valid_utf8]\n");
    size_t outlen;
    /* Valid UTF-8 should pass through */
    uint8_t *r = neverc_bytes_to_valid_utf8(B("hello"), B("?"), &outlen);
    check_bool("valid passthrough", outlen == 5 && memcmp(r, "hello", 5) == 0, 1);
    free(r);

    /* Invalid byte 0xFF should be replaced */
    const uint8_t bad[] = { 'h', 0xFF, 'i' };
    r = neverc_bytes_to_valid_utf8(bad, 3, B("?"), &outlen);
    check_bool("invalid replaced len", outlen == 3, 1);
    check_bool("invalid replaced val", r[0] == 'h' && r[1] == '?' && r[2] == 'i', 1);
    free(r);

    /* Multiple replacement bytes */
    r = neverc_bytes_to_valid_utf8(bad, 3, (const uint8_t *)"\xEF\xBF\xBD", 3, &outlen);
    check_bool("replacement U+FFFD len", outlen == 5, 1);
    free(r);

    static const uint8_t invalid_runs[] = {
        'a', 0xFF, 0xC0, 0xAF, 'b', 0xE0, 0x80, 0xAF, 'c'
    };
    r = neverc_bytes_to_valid_utf8(
        invalid_runs, sizeof(invalid_runs), B("?"), &outlen);
    check_bytes("invalid UTF-8 runs replaced once", r, outlen, "a?b?c");
    free(r);

    r = neverc_bytes_to_valid_utf8(
        invalid_runs, sizeof(invalid_runs), NULL, 0, &outlen);
    check_bytes("invalid UTF-8 runs removed", r, outlen, "abc");
    free(r);
}

/* Brute-force references for cross-checking the Two-Way/BMH search engine. */
static size_t ref_index(const uint8_t *h, size_t hlen, const uint8_t *n, size_t nlen) {
    if (nlen == 0) return 0;
    if (nlen > hlen) return NOT_FOUND;
    for (size_t i = 0; i + nlen <= hlen; i++)
        if (memcmp(h + i, n, nlen) == 0) return i;
    return NOT_FOUND;
}
static size_t ref_last_index(const uint8_t *h, size_t hlen, const uint8_t *n, size_t nlen) {
    if (nlen == 0) return hlen;
    if (nlen > hlen) return NOT_FOUND;
    size_t i = hlen - nlen + 1;
    while (i > 0) { i--; if (memcmp(h + i, n, nlen) == 0) return i; }
    return NOT_FOUND;
}
static size_t ref_count(const uint8_t *h, size_t hlen, const uint8_t *n, size_t nlen) {
    if (nlen == 0) return hlen + 1;
    if (nlen > hlen) return 0;
    size_t c = 0, p = 0;
    while (p + nlen <= hlen) {
        if (memcmp(h + p, n, nlen) == 0) { c++; p += nlen; } else p++;
    }
    return c;
}

/* Adversarial / large-input correctness for the substring search engine:
 * periodic needles, all-same haystacks, rare/absent first bytes, and a
 * randomized cross-check against the brute-force references above. */
static void test_search_engine(void) {
    printf("[search_engine]\n");

    /* Worst case for naive/BMH search: a^(m-2)+b+a in an all-'a' haystack. */
    size_t hlen = 20000;
    uint8_t *hay = (uint8_t *)malloc(hlen);
    memset(hay, 'a', hlen);
    uint8_t needle[200];
    memset(needle, 'a', 200); needle[198] = 'b'; /* m=200, late mismatch */
    check_size("adversarial miss", neverc_bytes_index(hay, hlen, needle, 200), NOT_FOUND);
    /* Plant it so it matches near the end. */
    memcpy(hay + hlen - 200, needle, 200);
    check_size("adversarial found", neverc_bytes_index(hay, hlen, needle, 200), hlen - 200);
    check_size("adversarial last",  neverc_bytes_last_index(hay, hlen, needle, 200), hlen - 200);

    /* Periodic needle present many times. */
    memset(hay, 'a', hlen);
    uint8_t per[40]; memset(per, 'a', 40);
    check_size("periodic found@0", neverc_bytes_index(hay, hlen, per, 40), 0);
    check_size("periodic count", neverc_bytes_count(hay, hlen, per, 40), hlen / 40);

    /* Rare/absent first byte → memchr fast path. */
    check_size("absent first byte", neverc_bytes_index(hay, hlen, (const uint8_t *)"Zaaaaaaaaaaaaaaaaaaa", 20), NOT_FOUND);
    free(hay);

    /* Randomized cross-check against brute force over small alphabets (high
     * periodicity) and varied lengths that cross the engine's thresholds. */
    srand(12345);
    int mism = 0;
    uint8_t hb[600], nb[120];
    for (int it = 0; it < 40000 && mism == 0; it++) {
        int alpha = (it & 1) ? 2 : 4;
        size_t hl = (size_t)(rand() % 600);
        size_t nl = (size_t)(rand() % 120);
        for (size_t i = 0; i < hl; i++) hb[i] = (uint8_t)('a' + rand() % alpha);
        if (nl > 0 && hl >= nl && (rand() & 3)) {
            size_t st = (size_t)(rand() % (int)(hl - nl + 1));
            memcpy(nb, hb + st, nl);
            if ((rand() & 1) && nl) nb[rand() % (int)nl] = (uint8_t)('a' + rand() % alpha);
        } else {
            for (size_t i = 0; i < nl; i++) nb[i] = (uint8_t)('a' + rand() % alpha);
        }
        if (neverc_bytes_index(hb, hl, nb, nl) != ref_index(hb, hl, nb, nl)) mism++;
        if (neverc_bytes_last_index(hb, hl, nb, nl) != ref_last_index(hb, hl, nb, nl)) mism++;
        if (neverc_bytes_count(hb, hl, nb, nl) != ref_count(hb, hl, nb, nl)) mism++;
    }
    check_bool("randomized cross-check vs brute force", mism, 0);
}

/* Differential fuzz for index_any/last_index_any vs a brute-force oracle. The
 * single-byte cutset now delegates to memchr / last_index_byte, so this checks
 * both that fast path and the multi-byte ASCII-set path stay correct. */
static uint64_t ia_rng = 0xfeedface12345678ULL;
static uint32_t ia_rand(void) {
    ia_rng ^= ia_rng << 13; ia_rng ^= ia_rng >> 7; ia_rng ^= ia_rng << 17;
    return (uint32_t)(ia_rng >> 32);
}
static size_t ref_index_any(const uint8_t *s, size_t n, const char *cut) {
    for (size_t i = 0; i < n; i++)
        for (const char *c = cut; *c; c++)
            if ((uint8_t)*c == s[i]) return i;
    return (size_t)-1;
}
static size_t ref_last_index_any(const uint8_t *s, size_t n, const char *cut) {
    for (size_t i = n; i > 0; i--)
        for (const char *c = cut; *c; c++)
            if ((uint8_t)*c == s[i - 1]) return i - 1;
    return (size_t)-1;
}
static void test_index_any_fuzz(void) {
    printf("[index_any_fuzz]\n");
    ia_rng = 0xfeedface12345678ULL;
    static uint8_t s[300];
    char cut[9];
    int bad = 0;
    for (int it = 0; it < 40000 && !bad; it++) {
        size_t n = ia_rand() % 300;
        int alpha = 1 + (int)(ia_rand() % 6);              /* small alphabet => hits */
        for (size_t i = 0; i < n; i++) s[i] = (uint8_t)('a' + ia_rand() % alpha);
        size_t clen = 1 + ia_rand() % 4;                   /* 1..4 (covers single-byte) */
        for (size_t i = 0; i < clen; i++) cut[i] = (char)('a' + ia_rand() % 6);
        cut[clen] = '\0';
        if (neverc_bytes_index_any(s, n, cut) != ref_index_any(s, n, cut)) bad = 1;
        if (neverc_bytes_last_index_any(s, n, cut) != ref_last_index_any(s, n, cut)) bad = 1;
    }
    check_bool("index_any/last fuzz == oracle", bad, 0);
}

int main(void) {
    printf("=== NeverC Bytes Module Tests ===\n\n");
    test_compare();
    test_search();
    test_index_any_fuzz();
    test_search_engine();
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
    test_cut_last();
    test_index_rune();
    test_runes();
    test_to_valid_utf8();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
