#include "neverc/std/uuid.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}
static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got?got:"(null)", expected); }
}

static void test_new(void) {
    printf("[new]\n");
    neverc_uuid_t u = neverc_uuid_new();
    check_int("version 4", neverc_uuid_version(u), 4);
    check_int("variant RFC", neverc_uuid_variant(u), 1);
    check_int("not nil", neverc_uuid_is_nil(u), 0);

    neverc_uuid_t u2 = neverc_uuid_new();
    check_int("unique", neverc_uuid_equal(u, u2), 0);
}

static void test_string_roundtrip(void) {
    printf("[string roundtrip]\n");
    neverc_uuid_t u = neverc_uuid_new();
    char str[37];
    neverc_uuid_to_string(u, str);

    check_int("str len", (int)strlen(str), 36);
    check_int("dash 8", str[8], '-');
    check_int("dash 13", str[13], '-');
    check_int("dash 18", str[18], '-');
    check_int("dash 23", str[23], '-');

    neverc_uuid_t parsed;
    int err = neverc_uuid_parse(str, &parsed);
    check_int("parse ok", err, 0);
    check_int("roundtrip equal", neverc_uuid_equal(u, parsed), 1);
}

static void test_parse(void) {
    printf("[parse]\n");
    neverc_uuid_t u;

    int err = neverc_uuid_parse("550e8400-e29b-41d4-a716-446655440000", &u);
    check_int("parse valid", err, 0);
    check_int("version", neverc_uuid_version(u), 4);

    char out[37];
    neverc_uuid_to_string(u, out);
    check_str("format", out, "550e8400-e29b-41d4-a716-446655440000");

    err = neverc_uuid_parse("invalid-uuid", &u);
    check_int("parse invalid", err, -1);

    err = neverc_uuid_parse("550e8400e29b41d4a716446655440000", &u);
    check_int("parse no dashes", err, -1);

    memset(&u, 0xa5, sizeof(u));
    neverc_uuid_t unchanged = u;
    err = neverc_uuid_parse("550e8400-e29b-41d4-a716-44665544000g", &u);
    check_int("parse invalid hex", err, -1);
    check_int("invalid parse is atomic", neverc_uuid_equal(u, unchanged), 1);

    err = neverc_uuid_parse(NULL, &u);
    check_int("parse null input", err, -1);
    err = neverc_uuid_parse("550e8400-e29b-41d4-a716-446655440000", NULL);
    check_int("parse null output", err, -1);

    neverc_uuid_to_string(u, NULL);
    check_int("format null output is safe", 1, 1);
}

static void test_nil(void) {
    printf("[nil]\n");
    neverc_uuid_t nil = neverc_uuid_nil();
    check_int("is nil", neverc_uuid_is_nil(nil), 1);

    char str[37];
    neverc_uuid_to_string(nil, str);
    check_str("nil string", str, "00000000-0000-0000-0000-000000000000");
}

static void test_uniqueness(void) {
    printf("[uniqueness]\n");
    neverc_uuid_t uuids[100];
    for (int i = 0; i < 100; i++)
        uuids[i] = neverc_uuid_new();

    int all_unique = 1;
    for (int i = 0; i < 100 && all_unique; i++)
        for (int j = i + 1; j < 100 && all_unique; j++)
            if (neverc_uuid_equal(uuids[i], uuids[j]))
                all_unique = 0;

    check_int("100 unique", all_unique, 1);
}

int main(void) {
    printf("=== NeverC UUID Module Tests ===\n\n");
    test_new();
    test_string_roundtrip();
    test_parse();
    test_nil();
    test_uniqueness();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
