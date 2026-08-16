#include "neverc/std/cstring.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_size(const char *name, size_t got, size_t expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %zu, expected %zu\n", name, got, expected); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got == NULL && expected == NULL) { tests_passed++; return; }
    if (got == NULL || expected == NULL) {
        tests_failed++;
        printf("  FAIL: %s: got %s, expected %s\n", name,
               got ? got : "NULL", expected ? expected : "NULL");
        return;
    }
    if (strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got, expected); }
}

/* ===== Compare ===== */

static void test_compare(void) {
    printf("[compare]\n");
    check_int("compare equal", neverc_cstring_compare("abc", "abc"), 0);
    check_int("compare lt", neverc_cstring_compare("abc", "abd") < 0, 1);
    check_int("compare gt", neverc_cstring_compare("abd", "abc") > 0, 1);
    check_int("compare shorter", neverc_cstring_compare("ab", "abc") < 0, 1);
    check_int("compare longer", neverc_cstring_compare("abc", "ab") > 0, 1);
    check_int("compare empty", neverc_cstring_compare("", ""), 0);
    check_int("compare a vs empty", neverc_cstring_compare("a", "") > 0, 1);
    check_int("compare empty vs a", neverc_cstring_compare("", "a") < 0, 1);
}

static void test_equal_fold(void) {
    printf("[equal_fold]\n");
    check_int("fold same case", neverc_cstring_equal_fold("Hello", "Hello"), 1);
    check_int("fold diff case", neverc_cstring_equal_fold("Hello", "hELLO"), 1);
    check_int("fold diff", neverc_cstring_equal_fold("Hello", "World"), 0);
    check_int("fold empty", neverc_cstring_equal_fold("", ""), 1);
    check_int("fold diff len", neverc_cstring_equal_fold("abc", "abcd"), 0);
    check_int("fold numbers", neverc_cstring_equal_fold("123", "123"), 1);

    srand(9173);
    int mismatches = 0;
    char left[129], right[129];
    for (int round = 0; round < 50000 && mismatches == 0; round++) {
        size_t len = (size_t)(rand() % 129);
        for (size_t i = 0; i < len; i++) {
            left[i] = (char)(1 + rand() % 127);
            right[i] = (char)(1 + rand() % 127);
        }
        left[len] = '\0';
        right[len] = '\0';
        int expected = 1;
        for (size_t i = 0; i < len; i++) {
            char l = left[i] >= 'A' && left[i] <= 'Z'
                         ? (char)(left[i] + ('a' - 'A')) : left[i];
            char r = right[i] >= 'A' && right[i] <= 'Z'
                         ? (char)(right[i] + ('a' - 'A')) : right[i];
            if (l != r) { expected = 0; break; }
        }
        if (neverc_cstring_equal_fold(left, right) != expected)
            mismatches++;
    }
    check_int("fold randomized ASCII differential", mismatches, 0);
}

/* ===== Search / Index ===== */

static void test_index(void) {
    printf("[index]\n");
    check_int("index found", neverc_cstring_index("hello world", "world"), 6);
    check_int("index not found", neverc_cstring_index("hello", "xyz"), -1);
    check_int("index empty", neverc_cstring_index("hello", ""), 0);
    check_int("index begin", neverc_cstring_index("hello", "hel"), 0);
    check_int("index end", neverc_cstring_index("hello", "llo"), 2);
    check_int("index substr longer", neverc_cstring_index("hi", "hello"), -1);
    check_int("index single char", neverc_cstring_index("hello", "l"), 2);
    check_int("index entire", neverc_cstring_index("hello", "hello"), 0);
}

static void test_last_index(void) {
    printf("[last_index]\n");
    check_int("last_index found", neverc_cstring_last_index("go gopher", "go"), 3);
    check_int("last_index begin", neverc_cstring_last_index("go go", "go"), 3);
    check_int("last_index not found", neverc_cstring_last_index("hello", "xyz"), -1);
    check_int("last_index empty", neverc_cstring_last_index("hello", ""), 5);
    check_int("last_index single", neverc_cstring_last_index("hello", "l"), 3);
}

static void test_index_byte(void) {
    printf("[index_byte]\n");
    check_int("index_byte found", neverc_cstring_index_byte("hello", 'l'), 2);
    check_int("index_byte not found", neverc_cstring_index_byte("hello", 'z'), -1);
    check_int("index_byte first", neverc_cstring_index_byte("hello", 'h'), 0);
    check_int("index_byte last", neverc_cstring_index_byte("hello", 'o'), 4);
    check_int("index_byte terminator", neverc_cstring_index_byte("hello", '\0'), -1);
}

static void test_last_index_byte(void) {
    printf("[last_index_byte]\n");
    check_int("last_index_byte dup", neverc_cstring_last_index_byte("hello", 'l'), 3);
    check_int("last_index_byte none", neverc_cstring_last_index_byte("hello", 'z'), -1);
    check_int("last_index_byte first", neverc_cstring_last_index_byte("a", 'a'), 0);
    check_int("last_index_byte terminator",
              neverc_cstring_last_index_byte("hello", '\0'), -1);
}

static void test_index_any(void) {
    printf("[index_any]\n");
    check_int("index_any vowels", neverc_cstring_index_any("chicken", "aeiou"), 2);
    check_int("index_any none", neverc_cstring_index_any("crwth", "aeiou"), -1);
    check_int("index_any empty chars", neverc_cstring_index_any("hello", ""), -1);
    check_int("index_any first", neverc_cstring_index_any("aardvark", "a"), 0);
    check_int("index_any UTF-8 rune",
              neverc_cstring_index_any("a\xe4\xb8\x96" "b", "\xe4\xb8\x96"), 1);
    check_int("index_any does not match UTF-8 fragments",
              neverc_cstring_index_any("\xc4\x96", "\xe4\xb8\x96"), -1);
}

static void test_last_index_any(void) {
    printf("[last_index_any]\n");
    check_int("last_index_any vowels", neverc_cstring_last_index_any("chicken", "aeiou"), 5);
    check_int("last_index_any none", neverc_cstring_last_index_any("crwth", "aeiou"), -1);
    check_int("last_index_any empty", neverc_cstring_last_index_any("hello", ""), -1);
    check_int("last_index_any UTF-8 rune",
              neverc_cstring_last_index_any("a\xe4\xb8\x96" "b\xe4\xb8\x96",
                                            "\xe4\xb8\x96"), 5);
}

static void test_contains(void) {
    printf("[contains]\n");
    check_int("contains yes", neverc_cstring_contains("seafood", "foo"), 1);
    check_int("contains no", neverc_cstring_contains("seafood", "bar"), 0);
    check_int("contains empty", neverc_cstring_contains("seafood", ""), 1);
    check_int("contains entire", neverc_cstring_contains("hello", "hello"), 1);
    check_int("contains any yes", neverc_cstring_contains_any("hello", "aeiou"), 1);
    check_int("contains any no", neverc_cstring_contains_any("crwth", "aeiou"), 0);
    check_int("contains char yes", neverc_cstring_contains_char("hello", 'e'), 1);
    check_int("contains char no", neverc_cstring_contains_char("hello", 'z'), 0);
    check_int("contains terminator", neverc_cstring_contains_char("hello", '\0'), 0);
}

static void test_count(void) {
    printf("[count]\n");
    check_int("count e in cheese", neverc_cstring_count("cheese", "e"), 3);
    check_int("count none", neverc_cstring_count("five", "x"), 0);
    check_int("count empty sep", neverc_cstring_count("abc", ""), 4);
    check_int("count empty both", neverc_cstring_count("", ""), 1);
    check_int("count overlap", neverc_cstring_count("aaa", "a"), 3);
    check_int("count multi", neverc_cstring_count("abababab", "ab"), 4);
    check_int("count non-overlap", neverc_cstring_count("aaaa", "aa"), 2);
}

/* ===== Prefix / Suffix ===== */

static void test_prefix_suffix(void) {
    printf("[prefix/suffix]\n");
    check_int("has_prefix yes", neverc_cstring_has_prefix("Gopher", "Go"), 1);
    check_int("has_prefix no", neverc_cstring_has_prefix("Gopher", "C"), 0);
    check_int("has_prefix empty", neverc_cstring_has_prefix("Gopher", ""), 1);
    check_int("has_prefix longer", neverc_cstring_has_prefix("Go", "Gopher"), 0);

    check_int("has_suffix yes", neverc_cstring_has_suffix("Amigo", "go"), 1);
    check_int("has_suffix no", neverc_cstring_has_suffix("Amigo", "Ami"), 0);
    check_int("has_suffix empty", neverc_cstring_has_suffix("Amigo", ""), 1);
    check_int("has_suffix longer", neverc_cstring_has_suffix("go", "Amigo"), 0);
}

/* ===== Transform ===== */

static void test_to_upper(void) {
    printf("[to_upper]\n");
    char *r;

    r = neverc_cstring_to_upper("hello, World!");
    check_str("to_upper mixed", r, "HELLO, WORLD!");
    free(r);

    r = neverc_cstring_to_upper("ALREADY UPPER");
    check_str("to_upper already", r, "ALREADY UPPER");
    free(r);

    r = neverc_cstring_to_upper("");
    check_str("to_upper empty", r, "");
    free(r);

    r = neverc_cstring_to_upper("123abc");
    check_str("to_upper digits", r, "123ABC");
    free(r);
}

static void test_to_lower(void) {
    printf("[to_lower]\n");
    char *r;

    r = neverc_cstring_to_lower("Hello, WORLD!");
    check_str("to_lower mixed", r, "hello, world!");
    free(r);

    r = neverc_cstring_to_lower("already lower");
    check_str("to_lower already", r, "already lower");
    free(r);

    r = neverc_cstring_to_lower("");
    check_str("to_lower empty", r, "");
    free(r);
}

static void test_to_title(void) {
    printf("[to_title]\n");
    char *r;

    r = neverc_cstring_to_title("hello world");
    check_str("to_title basic", r, "Hello World");
    free(r);

    r = neverc_cstring_to_title("HELLO WORLD");
    check_str("to_title caps", r, "HELLO WORLD");
    free(r);

    r = neverc_cstring_to_title("");
    check_str("to_title empty", r, "");
    free(r);

    r = neverc_cstring_to_title("one-two.three");
    check_str("to_title separators", r, "One-Two.Three");
    free(r);
}

static void test_repeat(void) {
    printf("[repeat]\n");
    char *r;

    r = neverc_cstring_repeat("ab", 3);
    check_str("repeat 3", r, "ababab");
    free(r);

    r = neverc_cstring_repeat("abc", 0);
    check_str("repeat 0", r, "");
    free(r);

    r = neverc_cstring_repeat("", 5);
    check_str("repeat empty", r, "");
    free(r);

    r = neverc_cstring_repeat("x", 1);
    check_str("repeat 1", r, "x");
    free(r);

    r = neverc_cstring_repeat("-", 10);
    check_str("repeat dash", r, "----------");
    free(r);
}

static void test_replace(void) {
    printf("[replace]\n");
    char *r;

    r = neverc_cstring_replace("oink oink oink", "oink", "moo", 2);
    check_str("replace 2", r, "moo moo oink");
    free(r);

    r = neverc_cstring_replace("oink oink oink", "oink", "moo", -1);
    check_str("replace all via -1", r, "moo moo moo");
    free(r);

    r = neverc_cstring_replace("hello", "x", "y", -1);
    check_str("replace no match", r, "hello");
    free(r);

    r = neverc_cstring_replace("hello", "l", "L", 1);
    check_str("replace first only", r, "heLlo");
    free(r);

    r = neverc_cstring_replace("hello", "l", "", -1);
    check_str("replace delete", r, "heo");
    free(r);
}

static void test_replace_all(void) {
    printf("[replace_all]\n");
    char *r;

    r = neverc_cstring_replace_all("oink oink oink", "oink", "moo");
    check_str("replace_all basic", r, "moo moo moo");
    free(r);

    r = neverc_cstring_replace_all("abc", "b", "BB");
    check_str("replace_all expand", r, "aBBc");
    free(r);
}

static char rot13(char c) {
    if (c >= 'a' && c <= 'z') return (char)('a' + (c - 'a' + 13) % 26);
    if (c >= 'A' && c <= 'Z') return (char)('A' + (c - 'A' + 13) % 26);
    return c;
}

static void test_map(void) {
    printf("[map]\n");
    char *r;

    r = neverc_cstring_map(rot13, "Hello");
    check_str("map rot13", r, "Uryyb");
    free(r);

    r = neverc_cstring_map(rot13, "");
    check_str("map empty", r, "");
    free(r);
}

/* ===== Join ===== */

static void test_join(void) {
    printf("[join]\n");
    char *r;
    const char *parts1[] = {"a", "b", "c"};
    r = neverc_cstring_join(parts1, 3, ",");
    check_str("join basic", r, "a,b,c");
    free(r);

    r = neverc_cstring_join(parts1, 3, "");
    check_str("join no sep", r, "abc");
    free(r);

    r = neverc_cstring_join(parts1, 3, " - ");
    check_str("join multi sep", r, "a - b - c");
    free(r);

    const char *single[] = {"only"};
    r = neverc_cstring_join(single, 1, ",");
    check_str("join single", r, "only");
    free(r);

    r = neverc_cstring_join(NULL, 0, ",");
    check_str("join empty", r, "");
    free(r);
}

/* ===== Trim ===== */

static void test_trim(void) {
    printf("[trim]\n");
    char *r;

    r = neverc_cstring_trim("  hello  ", " ");
    check_str("trim spaces", r, "hello");
    free(r);

    r = neverc_cstring_trim("***hello***", "*");
    check_str("trim stars", r, "hello");
    free(r);

    r = neverc_cstring_trim("abcba", "abc");
    check_str("trim all chars", r, "");
    free(r);

    r = neverc_cstring_trim("hello", "");
    check_str("trim empty cutset", r, "hello");
    free(r);

    r = neverc_cstring_trim("", "abc");
    check_str("trim empty str", r, "");
    free(r);

    r = neverc_cstring_trim("\xe4\xb8\x96hello\xe4\xb8\x96", "\xe4\xb8\x96");
    check_str("trim Unicode cutset", r, "hello");
    free(r);

    r = neverc_cstring_trim("\xc4\x96", "\xe4\xb8\x96");
    check_str("trim does not split UTF-8 runes", r, "\xc4\x96");
    free(r);
}

static void test_trim_left(void) {
    printf("[trim_left]\n");
    char *r;

    r = neverc_cstring_trim_left("  hello  ", " ");
    check_str("trim_left", r, "hello  ");
    free(r);

    r = neverc_cstring_trim_left("xxxhello", "x");
    check_str("trim_left x", r, "hello");
    free(r);
}

static void test_trim_right(void) {
    printf("[trim_right]\n");
    char *r;

    r = neverc_cstring_trim_right("  hello  ", " ");
    check_str("trim_right", r, "  hello");
    free(r);

    r = neverc_cstring_trim_right("helloyyy", "y");
    check_str("trim_right y", r, "hello");
    free(r);
}

static void test_trim_space(void) {
    printf("[trim_space]\n");
    char *r;

    r = neverc_cstring_trim_space("  hello  ");
    check_str("trim_space basic", r, "hello");
    free(r);

    r = neverc_cstring_trim_space("\t\nhello\r\n");
    check_str("trim_space tabs", r, "hello");
    free(r);

    r = neverc_cstring_trim_space("   ");
    check_str("trim_space all spaces", r, "");
    free(r);

    r = neverc_cstring_trim_space("hello");
    check_str("trim_space none", r, "hello");
    free(r);

    r = neverc_cstring_trim_space("");
    check_str("trim_space empty", r, "");
    free(r);

    r = neverc_cstring_trim_space("\xc2\xa0hello\xe3\x80\x80");
    check_str("trim_space Unicode White_Space", r, "hello");
    free(r);
}

static void test_trim_prefix(void) {
    printf("[trim_prefix]\n");
    char *r;

    r = neverc_cstring_trim_prefix("Hello, world!", "Hello, ");
    check_str("trim_prefix found", r, "world!");
    free(r);

    r = neverc_cstring_trim_prefix("Hello, world!", "Bye, ");
    check_str("trim_prefix not found", r, "Hello, world!");
    free(r);

    r = neverc_cstring_trim_prefix("Hello", "");
    check_str("trim_prefix empty", r, "Hello");
    free(r);
}

static void test_trim_suffix(void) {
    printf("[trim_suffix]\n");
    char *r;

    r = neverc_cstring_trim_suffix("Hello, world!", ", world!");
    check_str("trim_suffix found", r, "Hello");
    free(r);

    r = neverc_cstring_trim_suffix("Hello, world!", "xxx");
    check_str("trim_suffix not found", r, "Hello, world!");
    free(r);

    r = neverc_cstring_trim_suffix("Hello", "");
    check_str("trim_suffix empty", r, "Hello");
    free(r);
}

/* ===== Split ===== */

static void test_split(void) {
    printf("[split]\n");
    size_t count = 0;
    char **arr;

    arr = neverc_cstring_split("a,b,c", ",", &count);
    check_size("split count", count, 3);
    if (count == 3) {
        check_str("split[0]", arr[0], "a");
        check_str("split[1]", arr[1], "b");
        check_str("split[2]", arr[2], "c");
    }
    neverc_cstring_free_split(arr, count);

    arr = neverc_cstring_split("hello", ",", &count);
    check_size("split no match count", count, 1);
    if (count == 1) check_str("split no match[0]", arr[0], "hello");
    neverc_cstring_free_split(arr, count);

    arr = neverc_cstring_split(",a,,b,", ",", &count);
    check_size("split leading/trailing count", count, 5);
    if (count == 5) {
        check_str("split lt[0]", arr[0], "");
        check_str("split lt[1]", arr[1], "a");
        check_str("split lt[2]", arr[2], "");
        check_str("split lt[3]", arr[3], "b");
        check_str("split lt[4]", arr[4], "");
    }
    neverc_cstring_free_split(arr, count);

    arr = neverc_cstring_split("a::b::c", "::", &count);
    check_size("split multi sep", count, 3);
    if (count == 3) {
        check_str("split ms[0]", arr[0], "a");
        check_str("split ms[1]", arr[1], "b");
        check_str("split ms[2]", arr[2], "c");
    }
    neverc_cstring_free_split(arr, count);

    arr = neverc_cstring_split("", "", &count);
    check_size("split empty+empty count", count, 0);
    neverc_cstring_free_split(arr, count);
}

static void test_split_n(void) {
    printf("[split_n]\n");
    size_t count = 0;
    char **arr;

    arr = neverc_cstring_split_n("a,b,c,d", ",", 2, &count);
    check_size("split_n count", count, 2);
    if (count == 2) {
        check_str("split_n[0]", arr[0], "a");
        check_str("split_n[1]", arr[1], "b,c,d");
    }
    neverc_cstring_free_split(arr, count);

    arr = neverc_cstring_split_n("", "", 1, &count);
    check_size("split_n empty+empty count", count, 0);
    neverc_cstring_free_split(arr, count);

    arr = neverc_cstring_split_n("a,b,c", ",", 10, &count);
    check_size("split_n overcount", count, 3);
    neverc_cstring_free_split(arr, count);
}

static void test_split_after(void) {
    printf("[split_after]\n");
    size_t count = 0;
    char **arr;

    arr = neverc_cstring_split_after("a,b,c", ",", &count);
    check_size("split_after count", count, 3);
    if (count == 3) {
        check_str("split_after[0]", arr[0], "a,");
        check_str("split_after[1]", arr[1], "b,");
        check_str("split_after[2]", arr[2], "c");
    }
    neverc_cstring_free_split(arr, count);
}

static void test_fields(void) {
    printf("[fields]\n");
    size_t count = 0;
    char **arr;

    arr = neverc_cstring_fields("  foo   bar  baz   ", &count);
    check_size("fields count", count, 3);
    if (count == 3) {
        check_str("fields[0]", arr[0], "foo");
        check_str("fields[1]", arr[1], "bar");
        check_str("fields[2]", arr[2], "baz");
    }
    neverc_cstring_free_split(arr, count);

    arr = neverc_cstring_fields("", &count);
    check_size("fields empty count", count, 0);
    neverc_cstring_free_split(arr, count);

    arr = neverc_cstring_fields("   ", &count);
    check_size("fields whitespace count", count, 0);
    neverc_cstring_free_split(arr, count);

    arr = neverc_cstring_fields("one\ttwo\nthree", &count);
    check_size("fields tabs/newlines", count, 3);
    if (count == 3) {
        check_str("fields tn[0]", arr[0], "one");
        check_str("fields tn[1]", arr[1], "two");
        check_str("fields tn[2]", arr[2], "three");
    }
    neverc_cstring_free_split(arr, count);

    arr = neverc_cstring_fields("a\xc2\xa0" "b\xe3\x80\x80" "c", &count);
    check_size("fields Unicode White_Space", count, 3);
    if (count == 3) {
        check_str("fields unicode[0]", arr[0], "a");
        check_str("fields unicode[1]", arr[1], "b");
        check_str("fields unicode[2]", arr[2], "c");
    }
    neverc_cstring_free_split(arr, count);
}

/* ===== Cut ===== */

static void test_cut(void) {
    printf("[cut]\n");
    char *before = NULL, *after = NULL;
    int found;

    found = neverc_cstring_cut("Gopher", "Go", &before, &after);
    check_int("cut found", found, 1);
    check_str("cut before", before, "");
    check_str("cut after", after, "pher");
    free(before); free(after);

    found = neverc_cstring_cut("Gopher", "ph", &before, &after);
    check_int("cut mid found", found, 1);
    check_str("cut mid before", before, "Go");
    check_str("cut mid after", after, "er");
    free(before); free(after);

    found = neverc_cstring_cut("Gopher", "xxx", &before, &after);
    check_int("cut not found", found, 0);
    check_str("cut nf before", before, "Gopher");
    check_str("cut nf after", after, "");
    free(before); free(after);
}

static void test_cut_prefix(void) {
    printf("[cut_prefix]\n");
    char *after = NULL;
    int found;

    found = neverc_cstring_cut_prefix("Hello, world!", "Hello, ", &after);
    check_int("cut_prefix found", found, 1);
    check_str("cut_prefix after", after, "world!");
    free(after);

    found = neverc_cstring_cut_prefix("Hello", "Bye", &after);
    check_int("cut_prefix not found", found, 0);
    check_str("cut_prefix nf", after, "Hello");
    free(after);
}

static void test_cut_suffix(void) {
    printf("[cut_suffix]\n");
    char *before = NULL;
    int found;

    found = neverc_cstring_cut_suffix("Hello, world!", ", world!", &before);
    check_int("cut_suffix found", found, 1);
    check_str("cut_suffix before", before, "Hello");
    free(before);

    found = neverc_cstring_cut_suffix("Hello", "xxx", &before);
    check_int("cut_suffix not found", found, 0);
    check_str("cut_suffix nf", before, "Hello");
    free(before);
}

static void test_cut_last(void) {
    printf("[cut_last]\n");
    char *before = NULL, *after = NULL;
    int found;

    found = neverc_cstring_cut_last("a/b/c", "/", &before, &after);
    check_int("cut_last found", found, 1);
    check_str("cut_last before", before, "a/b");
    check_str("cut_last after", after, "c");
    free(before); free(after);

    found = neverc_cstring_cut_last("hello", "/", &before, &after);
    check_int("cut_last not found", found, 0);
    check_str("cut_last nf before", before, "hello");
    check_str("cut_last nf after", after, "");
    free(before); free(after);
}

/* ===== Clone / Utility ===== */

static void test_clone(void) {
    printf("[clone]\n");
    char *r;

    r = neverc_cstring_clone("hello");
    check_str("clone basic", r, "hello");
    free(r);

    r = neverc_cstring_clone("");
    check_str("clone empty", r, "");
    free(r);
}

static void test_len(void) {
    printf("[len]\n");
    check_size("len hello", neverc_cstring_len("hello"), 5);
    check_size("len empty", neverc_cstring_len(""), 0);
    check_size("len one", neverc_cstring_len("x"), 1);
}

/* ===== Edge cases / Cross-platform ===== */

static void test_edge_cases(void) {
    printf("[edge_cases]\n");
    char *r;

    /* Embedded special characters */
    r = neverc_cstring_trim_space(" \t\r\n\v\f hello \t\r\n\v\f ");
    check_str("trim all ws types", r, "hello");
    free(r);

    /* Long string */
    r = neverc_cstring_repeat("x", 1000);
    check_size("repeat long len", neverc_cstring_len(r), 1000);
    free(r);

    /* Replace with empty old string */
    r = neverc_cstring_replace("ab", "", "-", -1);
    check_str("replace empty old", r, "-a-b-");
    free(r);

    /* Self-contained search */
    check_int("index self", neverc_cstring_index("hello", "hello"), 0);
    check_int("contains self", neverc_cstring_contains("x", "x"), 1);

    /* Count with overlapping matches (non-overlapping per Go semantics) */
    check_int("count non-overlap aaa/aa", neverc_cstring_count("aaaa", "aa"), 2);

    /* CutLast with multiple occurrences */
    char *b = NULL, *a = NULL;
    int f = neverc_cstring_cut_last("go go go", "go", &b, &a);
    check_int("cut_last multi found", f, 1);
    check_str("cut_last multi before", b, "go go ");
    check_str("cut_last multi after", a, "");
    free(b); free(a);

    /* Split empty separator = per-UTF-8-sequence split */
    size_t cnt = 0;
    char **arr = neverc_cstring_split("abc", "", &cnt);
    check_size("split empty sep count", cnt, 3);
    if (cnt == 3) {
        check_str("split empty[0]", arr[0], "a");
        check_str("split empty[1]", arr[1], "b");
        check_str("split empty[2]", arr[2], "c");
    }
    neverc_cstring_free_split(arr, cnt);

    arr = neverc_cstring_split("A\xe4\xb8\x96" "B", "", &cnt);
    check_size("split empty sep UTF-8 count", cnt, 3);
    if (cnt == 3) {
        check_str("split empty utf8[0]", arr[0], "A");
        check_str("split empty utf8[1]", arr[1], "\xe4\xb8\x96");
        check_str("split empty utf8[2]", arr[2], "B");
    }
    neverc_cstring_free_split(arr, cnt);

    check_int("count empty sep UTF-8",
              neverc_cstring_count("A\xe4\xb8\x96" "B", ""), 4);

    r = neverc_cstring_replace("A\xe4\xb8\x96" "B", "", "-", -1);
    check_str("replace empty old UTF-8", r, "-A-\xe4\xb8\x96-B-");
    free(r);
}

/* Brute-force references (NUL-terminated). */
static int ref_cindex(const char *s, const char *sub) {
    size_t sl = strlen(s), nl = strlen(sub);
    if (nl == 0) return 0;
    if (nl > sl) return -1;
    for (size_t i = 0; i + nl <= sl; i++)
        if (memcmp(s + i, sub, nl) == 0) return (int)i;
    return -1;
}
static int ref_clast(const char *s, const char *sub) {
    size_t sl = strlen(s), nl = strlen(sub);
    if (nl == 0) return (int)sl;
    if (nl > sl) return -1;
    size_t i = sl - nl + 1;
    while (i > 0) { i--; if (memcmp(s + i, sub, nl) == 0) return (int)i; }
    return -1;
}
static int ref_ccount(const char *s, const char *sub) {
    size_t sl = strlen(s), nl = strlen(sub);
    if (nl == 0) return (int)sl + 1;
    if (nl > sl) return 0;
    int c = 0; size_t p = 0;
    while (p + nl <= sl) { if (memcmp(s + p, sub, nl) == 0) { c++; p += nl; } else p++; }
    return c;
}

/* Adversarial + randomized cross-check of the substring search engine. */
static void test_search_engine(void) {
    printf("[search_engine]\n");

    /* Worst case: a^(m-2)+b+a in all-'a' text. */
    char *hay = (char *)malloc(20001);
    memset(hay, 'a', 20000); hay[20000] = '\0';
    char needle[201]; memset(needle, 'a', 200); needle[198] = 'b'; needle[200] = '\0';
    check_int("adversarial miss", neverc_cstring_index(hay, needle), -1);
    memcpy(hay + 20000 - 200, needle, 200);
    check_int("adversarial found", neverc_cstring_index(hay, needle), 20000 - 200);
    memset(hay, 'a', 20000);
    check_int("periodic count", neverc_cstring_count(hay, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), 20000 / 32);
    check_int("absent first byte", neverc_cstring_index(hay, "Zaaaaaaaaaaaaaaaaaaa"), -1);
    free(hay);

    srand(2026);
    int mism = 0;
    char sb[513], nb[121];
    for (int it = 0; it < 40000 && mism == 0; it++) {
        int alpha = (it & 1) ? 2 : 4;
        size_t sl = (size_t)(rand() % 512);
        size_t nl = (size_t)(rand() % 120);
        for (size_t i = 0; i < sl; i++) sb[i] = (char)('a' + rand() % alpha);
        sb[sl] = '\0';
        if (nl > 0 && sl >= nl && (rand() & 3)) {
            size_t st = (size_t)(rand() % (int)(sl - nl + 1));
            memcpy(nb, sb + st, nl);
            if ((rand() & 1) && nl) nb[rand() % (int)nl] = (char)('a' + rand() % alpha);
        } else {
            for (size_t i = 0; i < nl; i++) nb[i] = (char)('a' + rand() % alpha);
        }
        nb[nl] = '\0';
        if (neverc_cstring_index(sb, nb) != ref_cindex(sb, nb)) mism++;
        if (neverc_cstring_last_index(sb, nb) != ref_clast(sb, nb)) mism++;
        if (neverc_cstring_count(sb, nb) != ref_ccount(sb, nb)) mism++;
    }
    check_int("randomized cross-check vs brute force", mism, 0);
}

/* ===== Main ===== */

int main(void) {
    test_compare();
    test_equal_fold();
    test_search_engine();
    test_index();
    test_last_index();
    test_index_byte();
    test_last_index_byte();
    test_index_any();
    test_last_index_any();
    test_contains();
    test_count();
    test_prefix_suffix();
    test_to_upper();
    test_to_lower();
    test_to_title();
    test_repeat();
    test_replace();
    test_replace_all();
    test_map();
    test_join();
    test_trim();
    test_trim_left();
    test_trim_right();
    test_trim_space();
    test_trim_prefix();
    test_trim_suffix();
    test_split();
    test_split_n();
    test_split_after();
    test_fields();
    test_cut();
    test_cut_prefix();
    test_cut_suffix();
    test_cut_last();
    test_clone();
    test_len();
    test_edge_cases();

    printf("\n--- cstring: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    return tests_failed > 0 ? 1 : 0;
}
