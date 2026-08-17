/*
 * NeverC encoding/json tests.
 * Tests parse, marshal, query, constructors, edge cases.
 */
#include "neverc/std/encoding/json.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (expr); int _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL line %d: %s = %d, expected %d\n", __LINE__, #expr, _v, _e); } \
} while(0)

#define ASSERT_DBL_EQ(expr, expected, eps) do { \
    double _v = (expr); double _e = (expected); tests_run++; \
    double _d = _v - _e; if (_d < 0) _d = -_d; \
    if (_d < (eps)) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL line %d: %s = %g, expected %g\n", __LINE__, #expr, _v, _e); } \
} while(0)

#define ASSERT_STR_EQ(expr, expected) do { \
    const char *_v = (expr); const char *_e = (expected); tests_run++; \
    if (_v && _e && strcmp(_v, _e) == 0) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL line %d: %s = \"%s\", expected \"%s\"\n", __LINE__, #expr, _v?_v:"(null)", _e); } \
} while(0)

#define ASSERT_NOT_NULL(expr) do { tests_run++; \
    if ((expr) != NULL) tests_passed++; \
    else { tests_failed++; printf("  FAIL line %d: %s is NULL\n", __LINE__, #expr); } } while(0)

#define ASSERT_NULL(expr) do { tests_run++; \
    if ((expr) == NULL) tests_passed++; \
    else { tests_failed++; printf("  FAIL line %d: %s not NULL\n", __LINE__, #expr); } } while(0)

#define ASSERT_TRUE(expr) ASSERT_INT_EQ(!!(expr), 1)
#define ASSERT_FALSE(expr) ASSERT_INT_EQ(!!(expr), 0)

static void test_parse_null(void) {
    printf("[parse_null]\n");
    neverc_json_value_t *v = neverc_json_parse("null", 4);
    ASSERT_NOT_NULL(v);
    ASSERT_INT_EQ(neverc_json_type(v), NEVERC_JSON_NULL);
    neverc_json_free(v);
}

static void test_parse_bool(void) {
    printf("[parse_bool]\n");
    neverc_json_value_t *t = neverc_json_parse("true", 4);
    ASSERT_NOT_NULL(t);
    ASSERT_INT_EQ(neverc_json_type(t), NEVERC_JSON_BOOL);
    ASSERT_INT_EQ(neverc_json_bool(t), 1);
    neverc_json_free(t);

    neverc_json_value_t *f = neverc_json_parse("false", 5);
    ASSERT_NOT_NULL(f);
    ASSERT_INT_EQ(neverc_json_bool(f), 0);
    neverc_json_free(f);
}

static void test_parse_number(void) {
    printf("[parse_number]\n");
    neverc_json_value_t *v;

    v = neverc_json_parse("42", 2);
    ASSERT_NOT_NULL(v);
    ASSERT_DBL_EQ(neverc_json_number(v), 42.0, 1e-10);
    neverc_json_free(v);

    v = neverc_json_parse("-3.14", 5);
    ASSERT_NOT_NULL(v);
    ASSERT_DBL_EQ(neverc_json_number(v), -3.14, 1e-10);
    neverc_json_free(v);

    v = neverc_json_parse("1e10", 4);
    ASSERT_NOT_NULL(v);
    ASSERT_DBL_EQ(neverc_json_number(v), 1e10, 1.0);
    neverc_json_free(v);

    v = neverc_json_parse("0", 1);
    ASSERT_NOT_NULL(v);
    ASSERT_DBL_EQ(neverc_json_number(v), 0.0, 1e-10);
    neverc_json_free(v);

    v = neverc_json_parse("2.5e-3", 6);
    ASSERT_NOT_NULL(v);
    ASSERT_DBL_EQ(neverc_json_number(v), 0.0025, 1e-10);
    neverc_json_free(v);

    /* The DOM stores numbers as double, so values outside that finite range
     * must fail instead of silently becoming infinity and marshaling as null. */
    ASSERT_NULL(neverc_json_parse("1e9999", 6));
    ASSERT_NULL(neverc_json_parse("1e309", 5));
    ASSERT_NULL(neverc_json_parse("-1e309", 6));
    ASSERT_NULL(neverc_json_parse("1e+309", 6));
    v = neverc_json_parse("1e308", 5);
    ASSERT_NOT_NULL(v);
    neverc_json_free(v);
    /* Underflow is a legal JSON number and must become 0, not a parse error. */
    v = neverc_json_parse("1e-400", 6);
    ASSERT_NOT_NULL(v);
    ASSERT_DBL_EQ(neverc_json_number(v), 0.0, 1e-10);
    neverc_json_free(v);

    /* A 401-digit integer is a legal JSON number but exceeds finite double. */
    {
        char huge[402];
        huge[0] = '1';
        memset(huge + 1, '0', 400);
        ASSERT_NULL(neverc_json_parse(huge, 401));
    }
    /* 16 digits skip the integer fast path and still parse. */
    v = neverc_json_parse("1000000000000000", 16);
    ASSERT_NOT_NULL(v);
    ASSERT_DBL_EQ(neverc_json_number(v), 1e15, 1.0);
    neverc_json_free(v);
}

static void test_parse_string(void) {
    printf("[parse_string]\n");
    neverc_json_value_t *v;

    v = neverc_json_parse("\"hello\"", 7);
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(neverc_json_string(v), "hello");
    neverc_json_free(v);

    v = neverc_json_parse("\"he said \\\"hi\\\"\"", 16);
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(neverc_json_string(v), "he said \"hi\"");
    neverc_json_free(v);

    v = neverc_json_parse("\"tab\\there\"", 11);
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(neverc_json_string(v), "tab\there");
    neverc_json_free(v);

    v = neverc_json_parse("\"\\u0041\"", 8);
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(neverc_json_string(v), "A");
    neverc_json_free(v);

    /* empty string */
    v = neverc_json_parse("\"\"", 2);
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(neverc_json_string(v), "");
    neverc_json_free(v);

    static const char raw_newline[] = {'"', 'a', '\n', 'b', '"'};
    static const char raw_nul[] = {'"', 'a', '\0', 'b', '"'};
    static const char raw_tab[] = {'"', 'a', '\t', 'b', '"'};
    ASSERT_NULL(neverc_json_parse(raw_newline, sizeof(raw_newline)));
    ASSERT_NULL(neverc_json_parse(raw_nul, sizeof(raw_nul)));
    ASSERT_NULL(neverc_json_parse(raw_tab, sizeof(raw_tab)));
    ASSERT_NULL(neverc_json_parse("\"\\uDC00\"", 8));
    ASSERT_NULL(neverc_json_parse("\"\\uD800\"", 8));
    ASSERT_NULL(neverc_json_parse("\"\\uD800\\uD800\"", 14));

    static const char overlong[] = {'"', (char)0xC0, (char)0xAF, '"'};
    static const char bad_cont[] = {
        '"', (char)0xE2, (char)0x28, (char)0xA1, '"'
    };
    static const char utf8_surrogate[] = {
        '"', (char)0xED, (char)0xA0, (char)0x80, '"'
    };
    static const char too_large[] = {
        '"', (char)0xF4, (char)0x90, (char)0x80, (char)0x80, '"'
    };
    static const char truncated[] = {
        '"', (char)0xF0, (char)0x9F, (char)0x98, '"'
    };
    ASSERT_NULL(neverc_json_parse(overlong, sizeof(overlong)));
    ASSERT_NULL(neverc_json_parse(bad_cont, sizeof(bad_cont)));
    ASSERT_NULL(neverc_json_parse(utf8_surrogate, sizeof(utf8_surrogate)));
    ASSERT_NULL(neverc_json_parse(too_large, sizeof(too_large)));
    ASSERT_NULL(neverc_json_parse(truncated, sizeof(truncated)));

    /* RFC 8259 strings and object keys are Unicode (UTF-8). */
    {
        static const char cafe_str[] = {
            '"', 'c', 'a', 'f', (char)0xc3, (char)0xa9, '"'
        };
        v = neverc_json_parse(cafe_str, sizeof(cafe_str));
        ASSERT_NOT_NULL(v);
        if (v) {
            ASSERT_INT_EQ((int)neverc_json_string_len(v), 5);
            neverc_json_free(v);
        }

        static const char cafe_key[] = {
            '{', '"', 'c', 'a', 'f', (char)0xc3, (char)0xa9, '"', ':', '1', '}'
        };
        v = neverc_json_parse(cafe_key, sizeof(cafe_key));
        ASSERT_NOT_NULL(v);
        if (v) {
            static const char key[] = {
                'c', 'a', 'f', (char)0xc3, (char)0xa9, '\0'
            };
            neverc_json_value_t *got = neverc_json_object_get(v, key);
            ASSERT_NOT_NULL(got);
            if (got)
                ASSERT_DBL_EQ(neverc_json_number(got), 1.0, 1e-10);
            neverc_json_free(v);
        }

        static const char trunc_utf8[] = {
            '"', 'c', 'a', 'f', (char)0xc3, '"'
        };
        ASSERT_NULL(neverc_json_parse(trunc_utf8, sizeof(trunc_utf8)));
    }
}

static void test_embedded_nul(void) {
    printf("[embedded_nul]\n");
    const char *encoded = "\"a\\u0000b\"";
    neverc_json_value_t *value =
        neverc_json_parse(encoded, strlen(encoded));
    ASSERT_NOT_NULL(value);
    if (value) {
        const char *bytes = neverc_json_string(value);
        ASSERT_INT_EQ((int)neverc_json_string_len(value), 3);
        ASSERT_TRUE(bytes && bytes[0] == 'a' && bytes[1] == '\0' &&
                    bytes[2] == 'b');
        char out[32];
        int n = neverc_json_marshal(value, out, sizeof(out), NULL);
        ASSERT_INT_EQ(n, (int)strlen(encoded));
        ASSERT_TRUE(n > 0 &&
                    memcmp(out, encoded, (size_t)n) == 0);
        neverc_json_free(value);
    }

    static const char raw[] = {'x', '\0', 'y'};
    value = neverc_json_new_string_n(raw, sizeof(raw));
    ASSERT_NOT_NULL(value);
    if (value) {
        ASSERT_INT_EQ((int)neverc_json_string_len(value), 3);
        char out[32];
        int n = neverc_json_marshal(value, out, sizeof(out), NULL);
        ASSERT_TRUE(n > 0);
        neverc_json_value_t *roundtrip =
            n > 0 ? neverc_json_parse(out, (size_t)n) : NULL;
        ASSERT_NOT_NULL(roundtrip);
        if (roundtrip) {
            ASSERT_INT_EQ((int)neverc_json_string_len(roundtrip), 3);
            ASSERT_TRUE(memcmp(neverc_json_string(roundtrip),
                               raw, sizeof(raw)) == 0);
            neverc_json_free(roundtrip);
        }
        neverc_json_free(value);
    }

    const char *object_text = "{\"a\\u0000b\":1,\"a\":2}";
    neverc_json_value_t *object =
        neverc_json_parse(object_text, strlen(object_text));
    ASSERT_NOT_NULL(object);
    if (object) {
        static const char nul_key[] = {'a', '\0', 'b'};
        ASSERT_INT_EQ(neverc_json_object_len(object), 2);
        ASSERT_DBL_EQ(neverc_json_number(
                          neverc_json_object_get_n(
                              object, nul_key, sizeof(nul_key))),
                      1.0, 1e-10);
        ASSERT_DBL_EQ(neverc_json_number(
                          neverc_json_object_get(object, "a")),
                      2.0, 1e-10);
        char out[64];
        int n = neverc_json_marshal(object, out, sizeof(out), NULL);
        ASSERT_TRUE(n > 0);
        neverc_json_value_t *roundtrip =
            n > 0 ? neverc_json_parse(out, (size_t)n) : NULL;
        ASSERT_NOT_NULL(roundtrip);
        if (roundtrip) {
            ASSERT_DBL_EQ(neverc_json_number(
                              neverc_json_object_get_n(
                                  roundtrip, nul_key, sizeof(nul_key))),
                          1.0, 1e-10);
            neverc_json_free(roundtrip);
        }
        neverc_json_free(object);
    }
}

static void test_parse_array(void) {
    printf("[parse_array]\n");
    const char *json = "[1, 2, 3]";
    neverc_json_value_t *v = neverc_json_parse(json, strlen(json));
    ASSERT_NOT_NULL(v);
    ASSERT_INT_EQ(neverc_json_type(v), NEVERC_JSON_ARRAY);
    ASSERT_INT_EQ(neverc_json_array_len(v), 3);
    ASSERT_DBL_EQ(neverc_json_number(neverc_json_array_get(v, 0)), 1.0, 1e-10);
    ASSERT_DBL_EQ(neverc_json_number(neverc_json_array_get(v, 2)), 3.0, 1e-10);
    ASSERT_NULL(neverc_json_array_get(v, 3));
    neverc_json_free(v);

    /* empty array */
    v = neverc_json_parse("[]", 2);
    ASSERT_NOT_NULL(v);
    ASSERT_INT_EQ(neverc_json_array_len(v), 0);
    neverc_json_free(v);

    /* nested */
    v = neverc_json_parse("[[1,2],[3]]", 11);
    ASSERT_NOT_NULL(v);
    ASSERT_INT_EQ(neverc_json_array_len(v), 2);
    ASSERT_INT_EQ(neverc_json_array_len(neverc_json_array_get(v, 0)), 2);
    neverc_json_free(v);
}

static void test_parse_object(void) {
    printf("[parse_object]\n");
    const char *json = "{\"name\":\"Alice\",\"age\":30,\"active\":true}";
    neverc_json_value_t *v = neverc_json_parse(json, strlen(json));
    ASSERT_NOT_NULL(v);
    ASSERT_INT_EQ(neverc_json_type(v), NEVERC_JSON_OBJECT);
    ASSERT_INT_EQ(neverc_json_object_len(v), 3);
    ASSERT_STR_EQ(neverc_json_string(neverc_json_object_get(v, "name")), "Alice");
    ASSERT_DBL_EQ(neverc_json_number(neverc_json_object_get(v, "age")), 30.0, 1e-10);
    ASSERT_INT_EQ(neverc_json_bool(neverc_json_object_get(v, "active")), 1);
    ASSERT_NULL(neverc_json_object_get(v, "missing"));
    neverc_json_free(v);

    /* empty object */
    v = neverc_json_parse("{}", 2);
    ASSERT_NOT_NULL(v);
    ASSERT_INT_EQ(neverc_json_object_len(v), 0);
    neverc_json_free(v);
}

static void test_parse_complex(void) {
    printf("[parse_complex]\n");
    const char *json = "{\"users\":[{\"id\":1,\"name\":\"Bob\"},{\"id\":2,\"name\":\"Eve\"}],\"count\":2}";
    neverc_json_value_t *v = neverc_json_parse(json, strlen(json));
    ASSERT_NOT_NULL(v);

    neverc_json_value_t *users = neverc_json_object_get(v, "users");
    ASSERT_NOT_NULL(users);
    ASSERT_INT_EQ(neverc_json_array_len(users), 2);

    neverc_json_value_t *u0 = neverc_json_array_get(users, 0);
    ASSERT_STR_EQ(neverc_json_string(neverc_json_object_get(u0, "name")), "Bob");
    ASSERT_DBL_EQ(neverc_json_number(neverc_json_object_get(u0, "id")), 1.0, 1e-10);

    neverc_json_value_t *u1 = neverc_json_array_get(users, 1);
    ASSERT_STR_EQ(neverc_json_string(neverc_json_object_get(u1, "name")), "Eve");

    ASSERT_DBL_EQ(neverc_json_number(neverc_json_object_get(v, "count")), 2.0, 1e-10);

    neverc_json_free(v);
}

static void test_marshal(void) {
    printf("[marshal]\n");
    neverc_json_value_t *obj = neverc_json_new_object();
    neverc_json_object_set(obj, "x", neverc_json_new_number(42));
    neverc_json_object_set(obj, "y", neverc_json_new_string("hi"));
    neverc_json_object_set(obj, "z", neverc_json_new_bool(1));
    ASSERT_INT_EQ(neverc_json_object_set_n(obj, "abcdef", 3,
                                          neverc_json_new_number(9)), 0);
    ASSERT_DBL_EQ(neverc_json_number(neverc_json_object_get(obj, "abc")),
                  9.0, 1e-10);

    char buf[256];
    int n = neverc_json_marshal(obj, buf, sizeof(buf), NULL);
    ASSERT_TRUE(n > 0);
    buf[n] = '\0';

    /* verify roundtrip */
    neverc_json_value_t *parsed = neverc_json_parse(buf, (size_t)n);
    ASSERT_NOT_NULL(parsed);
    ASSERT_DBL_EQ(neverc_json_number(neverc_json_object_get(parsed, "x")), 42.0, 1e-10);
    ASSERT_STR_EQ(neverc_json_string(neverc_json_object_get(parsed, "y")), "hi");
    ASSERT_INT_EQ(neverc_json_bool(neverc_json_object_get(parsed, "z")), 1);

    neverc_json_free(parsed);
    neverc_json_free(obj);
}

static void test_marshal_escapes(void) {
    printf("[marshal_escapes]\n");
    char buf[256];

    /* Each special escape plus a control char and a bulk-copied run. */
    /* "\x01" "end": split literal so \x01 is not parsed as the hex escape \x01e. */
    neverc_json_value_t *s = neverc_json_new_string("a\"b\\c\n\t\r\b\f\x01" "end");
    int n = neverc_json_marshal(s, buf, sizeof(buf), NULL);
    ASSERT_TRUE(n > 0);
    if (n > 0) buf[n] = '\0';
    ASSERT_STR_EQ(buf, "\"a\\\"b\\\\c\\n\\t\\r\\b\\f\\u0001end\"");
    neverc_json_free(s);

    /* HTML/JS-safe marshal (Go encoding/json.Marshal default). */
    neverc_json_value_t *html = neverc_json_new_string("<script>&");
    n = neverc_json_marshal(html, buf, sizeof(buf), NULL);
    ASSERT_TRUE(n > 0);
    if (n > 0) buf[n] = '\0';
    ASSERT_STR_EQ(buf, "\"\\u003cscript\\u003e\\u0026\"");
    neverc_json_free(html);

    static const char line_sep[] = {'a', (char)0xE2, (char)0x80, (char)0xA8,
                                    'b', (char)0xE2, (char)0x80, (char)0xA9,
                                    'c'};
    neverc_json_value_t *ls = neverc_json_new_string_n(line_sep, sizeof(line_sep));
    n = neverc_json_marshal(ls, buf, sizeof(buf), NULL);
    ASSERT_TRUE(n > 0);
    if (n > 0) buf[n] = '\0';
    ASSERT_STR_EQ(buf, "\"a\\u2028b\\u2029c\"");
    neverc_json_free(ls);

    /* High (UTF-8) bytes pass through verbatim; only quote/backslash/HTML escape. */
    neverc_json_value_t *u = neverc_json_new_string("\xE4\xBD\xA0\xE5\xA5\xBD");
    n = neverc_json_marshal(u, buf, sizeof(buf), NULL);
    ASSERT_TRUE(n > 0);
    if (n > 0) buf[n] = '\0';
    ASSERT_STR_EQ(buf, "\"\xE4\xBD\xA0\xE5\xA5\xBD\"");
    neverc_json_free(u);

    static const char invalid_utf8[] = {(char)0xC2, 'x'};
    ASSERT_NULL(neverc_json_new_string_n(
        invalid_utf8, sizeof(invalid_utf8)));

    /* Roundtrip a string containing every escapeworthy + ordinary byte. */
    char all[256]; int k = 0;
    for (int c = 1; c < 128; c++) all[k++] = (char)c;   /* skip NUL (string terminator) */
    all[k] = '\0';
    neverc_json_value_t *a = neverc_json_new_string(all);
    char big[1024];
    n = neverc_json_marshal(a, big, sizeof(big), NULL);
    ASSERT_TRUE(n > 0);
    neverc_json_value_t *rt = neverc_json_parse(big, (size_t)n);
    ASSERT_NOT_NULL(rt);
    ASSERT_STR_EQ(neverc_json_string(rt), all);
    neverc_json_free(rt);
    neverc_json_free(a);
}

static void test_marshal_array(void) {
    printf("[marshal_array]\n");
    neverc_json_value_t *arr = neverc_json_new_array();
    neverc_json_array_append(arr, neverc_json_new_number(1));
    neverc_json_array_append(arr, neverc_json_new_number(2));
    neverc_json_array_append(arr, neverc_json_new_null());

    char buf[256];
    int n = neverc_json_marshal(arr, buf, sizeof(buf), NULL);
    ASSERT_TRUE(n > 0);
    buf[n] = '\0';
    ASSERT_STR_EQ(buf, "[1,2,null]");

    neverc_json_free(arr);
}

static void test_valid(void) {
    printf("[valid]\n");
    ASSERT_TRUE(neverc_json_valid("{}", 2));
    ASSERT_TRUE(neverc_json_valid("[]", 2));
    ASSERT_TRUE(neverc_json_valid("null", 4));
    ASSERT_TRUE(neverc_json_valid("42", 2));
    ASSERT_TRUE(neverc_json_valid("\"hello\"", 7));
    ASSERT_FALSE(neverc_json_valid("{", 1));
    ASSERT_FALSE(neverc_json_valid("[", 1));
    ASSERT_FALSE(neverc_json_valid("[1", 2));
    ASSERT_FALSE(neverc_json_valid("{\"a\":", 5));
    ASSERT_FALSE(neverc_json_valid("\"hello", 6));
    ASSERT_FALSE(neverc_json_valid("1e", 2));
    ASSERT_FALSE(neverc_json_valid("42x", 3));
    ASSERT_FALSE(neverc_json_valid("[1,]", 4));
    ASSERT_FALSE(neverc_json_valid("[1,2,]", 6));
    ASSERT_FALSE(neverc_json_valid("[,]", 3));
    ASSERT_FALSE(neverc_json_valid("{\"a\":1,}", 8));
    ASSERT_FALSE(neverc_json_valid("{,}", 3));
    ASSERT_FALSE(neverc_json_valid("", 0));
    ASSERT_FALSE(neverc_json_valid("nul", 3));
    ASSERT_FALSE(neverc_json_valid("[][]", 4));
    ASSERT_FALSE(neverc_json_valid("{}{}", 4));
    ASSERT_FALSE(neverc_json_valid("01", 2));
    ASSERT_FALSE(neverc_json_valid("+1", 2));
    ASSERT_FALSE(neverc_json_valid(".5", 2));
    ASSERT_FALSE(neverc_json_valid("1.", 2));
    ASSERT_FALSE(neverc_json_valid("nullx", 5));
    ASSERT_TRUE(neverc_json_valid("0e1", 3));
    ASSERT_TRUE(neverc_json_valid("1e+10", 5));
}

static void test_whitespace(void) {
    printf("[whitespace]\n");
    const char *json = "  {  \"a\"  :  1  ,  \"b\"  :  2  }  ";
    neverc_json_value_t *v = neverc_json_parse(json, strlen(json));
    ASSERT_NOT_NULL(v);
    ASSERT_DBL_EQ(neverc_json_number(neverc_json_object_get(v, "a")), 1.0, 1e-10);
    ASSERT_DBL_EQ(neverc_json_number(neverc_json_object_get(v, "b")), 2.0, 1e-10);
    neverc_json_free(v);

    /* RFC 8259 ws is only SP / HT / LF / CR — form feed is not whitespace. */
    static const char ff[] = {'[', '\f', ']'};
    ASSERT_NULL(neverc_json_parse(ff, sizeof(ff)));
}

static void test_escape_sequences(void) {
    printf("[escape_sequences]\n");
    const char *json = "\"line1\\nline2\\ttab\\\\backslash\\/slash\"";
    neverc_json_value_t *v = neverc_json_parse(json, strlen(json));
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(neverc_json_string(v), "line1\nline2\ttab\\backslash/slash");
    neverc_json_free(v);
}

static void test_unicode_escape(void) {
    printf("[unicode_escape]\n");
    /* surrogate pair for emoji 😀 U+1F600 */
    const char *json = "\"\\uD83D\\uDE00\"";
    neverc_json_value_t *v = neverc_json_parse(json, strlen(json));
    ASSERT_NOT_NULL(v);
    const char *s = neverc_json_string(v);
    /* UTF-8 for U+1F600 is F0 9F 98 80 */
    ASSERT_INT_EQ((unsigned char)s[0], 0xF0);
    ASSERT_INT_EQ((unsigned char)s[1], 0x9F);
    ASSERT_INT_EQ((unsigned char)s[2], 0x98);
    ASSERT_INT_EQ((unsigned char)s[3], 0x80);
    neverc_json_free(v);
}

/* Large objects (exercises the O(n) parse path) and duplicate-key semantics
 * (must collapse to one entry, last value wins). */
static void test_large_and_dup_objects(void) {
    printf("[large_and_dup_objects]\n");

    /* build {"k0":0,"k1":1,...,"k1999":1999} and verify every key parses back */
    const int N = 2000;
    size_t cap = (size_t)N * 24 + 4;
    char *buf = (char *)malloc(cap);
    size_t w = 0;
    buf[w++] = '{';
    for (int i = 0; i < N; i++) {
        if (i) buf[w++] = ',';
        w += (size_t)snprintf(buf + w, cap - w, "\"k%d\":%d", i, i);
    }
    buf[w++] = '}';
    buf[w] = '\0';

    neverc_json_value_t *v = neverc_json_parse(buf, w);
    ASSERT_NOT_NULL(v);
    if (v) {
        int ok = 1;
        for (int i = 0; i < N; i++) {
            char key[16];
            snprintf(key, sizeof(key), "k%d", i);
            neverc_json_value_t *e = neverc_json_object_get(v, key);
            if (!e || (int)neverc_json_number(e) != i) { ok = 0; break; }
        }
        ASSERT_TRUE(ok);
        neverc_json_free(v);
    }
    free(buf);

    /* duplicate keys: last value wins, single entry remains */
    neverc_json_value_t *d = neverc_json_parse("{\"a\":1,\"b\":2,\"a\":3,\"a\":4}", 25);
    ASSERT_NOT_NULL(d);
    if (d) {
        ASSERT_DBL_EQ(neverc_json_number(neverc_json_object_get(d, "a")), 4.0, 1e-10);
        ASSERT_DBL_EQ(neverc_json_number(neverc_json_object_get(d, "b")), 2.0, 1e-10);
        /* re-marshal should contain "a":4 exactly once */
        char out[128];
        int n = neverc_json_marshal(d, out, sizeof(out), NULL);
        ASSERT_TRUE(n > 0);
        if (n > 0) {
            int acount = 0;
            for (char *q = out; (q = strstr(q, "\"a\"")); q += 3) acount++;
            ASSERT_INT_EQ(acount, 1);
            ASSERT_NOT_NULL(strstr(out, "\"a\":4"));
        }
        neverc_json_free(d);
    }
}

static void test_large_strings(void) {
    printf("[large_strings]\n");

    /* A string far larger than the old fixed 64 KiB buffer must now parse. */
    const size_t N = 200000;
    size_t cap = N + 4;
    char *json = (char *)malloc(cap);
    size_t w = 0;
    json[w++] = '"';
    for (size_t i = 0; i < N; i++) json[w++] = (char)('a' + (i % 26));
    json[w++] = '"';
    neverc_json_value_t *v = neverc_json_parse(json, w);
    ASSERT_NOT_NULL(v);
    if (v) {
        const char *s = neverc_json_string(v);
        ASSERT_INT_EQ((int)strlen(s), (int)N);
        int ok = (strlen(s) == N);
        for (size_t i = 0; ok && i < N; i++)
            if (s[i] != (char)('a' + (i % 26))) ok = 0;
        ASSERT_TRUE(ok);
        neverc_json_free(v);
    }
    free(json);

    /* Escape-heavy large string: 50k repetitions of "a\n" -> 100k bytes out. */
    const int R = 50000;
    cap = (size_t)R * 4 + 4;
    char *je = (char *)malloc(cap);
    w = 0;
    je[w++] = '"';
    for (int i = 0; i < R; i++) { je[w++] = 'a'; je[w++] = '\\'; je[w++] = 'n'; }
    je[w++] = '"';
    v = neverc_json_parse(je, w);
    ASSERT_NOT_NULL(v);
    if (v) {
        const char *s = neverc_json_string(v);
        ASSERT_INT_EQ((int)strlen(s), R * 2);
        int ok = ((int)strlen(s) == R * 2);
        for (int i = 0; ok && i < R; i++)
            if (s[i*2] != 'a' || s[i*2+1] != '\n') ok = 0;
        ASSERT_TRUE(ok);
        neverc_json_free(v);
    }
    free(je);

    /* Large string as an object value round-trips through the keymap path. */
    char *jo = (char *)malloc(N + 32);
    w = 0;
    w += (size_t)snprintf(jo + w, N + 32 - w, "{\"big\":\"");
    for (size_t i = 0; i < N; i++) jo[w++] = 'x';
    jo[w++] = '"'; jo[w++] = '}';
    v = neverc_json_parse(jo, w);
    ASSERT_NOT_NULL(v);
    if (v) {
        neverc_json_value_t *e = neverc_json_object_get(v, "big");
        ASSERT_NOT_NULL(e);
        if (e) ASSERT_INT_EQ((int)strlen(neverc_json_string(e)), (int)N);
        neverc_json_free(v);
    }
    free(jo);
}

/* Append `"<klen bytes of c>":val,` to buf (klen==0 produces the empty key ""). */
static void jkey_put(char *buf, size_t *off, size_t cap, char c, int klen, int val) {
    size_t o = *off;
    buf[o++] = '"';
    for (int i = 0; i < klen; i++) buf[o++] = c;
    o += (size_t)snprintf(buf + o, cap - o, "\":%d,", val);
    *off = o;
}

/* Exercises every length branch of the object key index hash (the FNV-1a ->
 * wyhash upgrade): empty / 1-3 / 4-16 / 17-48 / >48-byte keys, duplicate
 * collapse at several lengths (last value wins), index growth past its initial
 * capacity, and a guaranteed miss. The 17-48 and >48 byte keys are not covered
 * by test_large_and_dup_objects, which only uses short "kN" keys. */
static void test_keymap_hash_lengths(void) {
    printf("[keymap_hash_lengths]\n");
    static char buf[8192];
    size_t off = 0;
    buf[off++] = '{';
    jkey_put(buf, &off, sizeof buf, 'a', 0,   1);    /* empty key (len 0)      */
    jkey_put(buf, &off, sizeof buf, 'b', 1,   2);    /* len 1   (1-3 path)     */
    jkey_put(buf, &off, sizeof buf, 'c', 2,   3);
    jkey_put(buf, &off, sizeof buf, 'd', 3,   4);
    jkey_put(buf, &off, sizeof buf, 'e', 4,   5);    /* len 4   (4-16 path)    */
    jkey_put(buf, &off, sizeof buf, 'f', 8,   6);
    jkey_put(buf, &off, sizeof buf, 'o', 9,  15);    /* len 9-11 under-read   */
    jkey_put(buf, &off, sizeof buf, 'p', 10, 16);
    jkey_put(buf, &off, sizeof buf, 'q', 11, 17);
    jkey_put(buf, &off, sizeof buf, 'g', 15,  7);
    jkey_put(buf, &off, sizeof buf, 'h', 16,  8);
    jkey_put(buf, &off, sizeof buf, 'i', 17,  9);    /* len 17  (17-48 path)   */
    jkey_put(buf, &off, sizeof buf, 'j', 32, 10);
    jkey_put(buf, &off, sizeof buf, 'k', 47, 11);
    jkey_put(buf, &off, sizeof buf, 'l', 48, 12);
    jkey_put(buf, &off, sizeof buf, 'm', 49, 13);    /* len 49  (>48 stride)   */
    jkey_put(buf, &off, sizeof buf, 'n', 100,14);    /* len 100 (>48 stride)   */
    jkey_put(buf, &off, sizeof buf, 'a', 0,   100);  /* duplicates: last wins  */
    jkey_put(buf, &off, sizeof buf, 'e', 4,   105);
    jkey_put(buf, &off, sizeof buf, 'm', 49, 113);
    jkey_put(buf, &off, sizeof buf, 'n', 100,114);
    if (off && buf[off-1] == ',') off--;
    buf[off++] = '}';

    neverc_json_value_t *v = neverc_json_parse(buf, off);
    ASSERT_NOT_NULL(v);
    if (!v) return;
    ASSERT_INT_EQ(neverc_json_object_len(v), 17);    /* duplicates collapsed   */

    struct { char c; int klen; int want; } exp[] = {
        {'a',0,100}, {'b',1,2}, {'c',2,3}, {'d',3,4}, {'e',4,105},
        {'f',8,6}, {'o',9,15}, {'p',10,16}, {'q',11,17},
        {'g',15,7}, {'h',16,8}, {'i',17,9}, {'j',32,10},
        {'k',47,11}, {'l',48,12}, {'m',49,113}, {'n',100,114},
    };
    char key[128];
    for (size_t i = 0; i < sizeof exp / sizeof exp[0]; i++) {
        memset(key, exp[i].c, (size_t)exp[i].klen);
        key[exp[i].klen] = '\0';
        neverc_json_value_t *e = neverc_json_object_get(v, key);
        ASSERT_NOT_NULL(e);
        if (e) ASSERT_INT_EQ((int)neverc_json_number(e), exp[i].want);
    }
    ASSERT_NULL(neverc_json_object_get(v, "definitely-absent-key"));
    neverc_json_free(v);
}

static void test_invalid_api_inputs(void) {
    printf("[invalid_api_inputs]\n");
    char out[16];
    ASSERT_NULL(neverc_json_parse(NULL, 1));
    ASSERT_NULL(neverc_json_new_string(NULL));
    ASSERT_INT_EQ(neverc_json_marshal(NULL, out, sizeof(out), NULL), -1);
    neverc_json_value_t *empty = neverc_json_new_null();
    ASSERT_NOT_NULL(empty);
    ASSERT_INT_EQ(neverc_json_marshal(
                      empty, out, sizeof(out), "not-whitespace"), -1);
    neverc_json_free(empty);

    neverc_json_value_t *nonfinite = neverc_json_new_number(INFINITY);
    ASSERT_NOT_NULL(nonfinite);
    ASSERT_INT_EQ(neverc_json_marshal(
                      nonfinite, out, sizeof(out), NULL), -1);
    neverc_json_free(nonfinite);

    neverc_json_value_t *array = neverc_json_new_array();
    ASSERT_NOT_NULL(array);
    ASSERT_INT_EQ(neverc_json_array_append(array, NULL), -1);
    neverc_json_free(array);

    neverc_json_value_t *object = neverc_json_new_object();
    neverc_json_value_t *value = neverc_json_new_number(7);
    ASSERT_NOT_NULL(object);
    ASSERT_NOT_NULL(value);
    ASSERT_INT_EQ(neverc_json_object_set(object, NULL, value), -1);
    ASSERT_INT_EQ(neverc_json_object_set(object, "key", NULL), -1);
    ASSERT_INT_EQ(neverc_json_object_set(object, "key", value), 0);
    ASSERT_INT_EQ(neverc_json_object_set(object, "key", value), 0);
    ASSERT_DBL_EQ(neverc_json_number(
                      neverc_json_object_get(object, "key")), 7.0, 0.01);
    neverc_json_free(object);
}

static void test_tree_ownership(void) {
    printf("[tree_ownership]\n");
    neverc_json_value_t *root = neverc_json_new_array();
    neverc_json_value_t *other = neverc_json_new_array();
    neverc_json_value_t *child = neverc_json_new_object();
    ASSERT_NOT_NULL(root);
    ASSERT_NOT_NULL(other);
    ASSERT_NOT_NULL(child);
    if (!root || !other || !child) {
        neverc_json_free(root);
        neverc_json_free(other);
        neverc_json_free(child);
        return;
    }

    ASSERT_INT_EQ(neverc_json_array_append(root, child), 0);
    ASSERT_INT_EQ(neverc_json_array_append(root, child), -1);
    ASSERT_INT_EQ(neverc_json_array_append(other, child), -1);
    ASSERT_INT_EQ(neverc_json_array_append(root, root), -1);
    ASSERT_INT_EQ(neverc_json_object_set(child, "cycle", root), -1);

    /* Freeing an owned child is a safe no-op; the parent still owns it. */
    neverc_json_free(child);
    ASSERT_INT_EQ(neverc_json_type(
                      neverc_json_array_get(root, 0)),
                  NEVERC_JSON_OBJECT);
    neverc_json_free(other);
    neverc_json_free(root);

    neverc_json_value_t *object = neverc_json_new_object();
    neverc_json_value_t *number = neverc_json_new_number(1.0);
    ASSERT_NOT_NULL(object);
    ASSERT_NOT_NULL(number);
    if (!object || !number) {
        neverc_json_free(object);
        neverc_json_free(number);
        return;
    }
    ASSERT_INT_EQ(neverc_json_object_set(object, "a", number), 0);
    ASSERT_INT_EQ(neverc_json_object_set(object, "a", number), 0);
    ASSERT_INT_EQ(neverc_json_object_set(object, "b", number), -1);
    neverc_json_free(object);
}

static void test_utf8_bom(void) {
    printf("[utf8_bom]\n");
    static const char bom_json[] = "\xEF\xBB\xBF{}";
    neverc_json_value_t *v = neverc_json_parse(bom_json, sizeof(bom_json) - 1);
    ASSERT_NOT_NULL(v);
    ASSERT_INT_EQ(v->type, NEVERC_JSON_OBJECT);
    neverc_json_free(v);
}

static void test_nesting_limit(void) {
    printf("[nesting_limit]\n");
    char buf[2010];
    int depth = 1000;
    for (int i = 0; i < depth; i++) buf[i] = '[';
    for (int i = 0; i < depth; i++) buf[depth + i] = ']';
    neverc_json_value_t *v = neverc_json_parse(buf, (size_t)(depth * 2));
    ASSERT_NOT_NULL(v);
    {
        char out[2010];
        int n = neverc_json_marshal(v, out, sizeof(out), NULL);
        ASSERT_INT_EQ(n, depth * 2);
        if (n == depth * 2)
            ASSERT_INT_EQ(memcmp(out, buf, (size_t)n) == 0, 1);
    }
    neverc_json_free(v);

    depth = 1001;
    for (int i = 0; i < depth; i++) buf[i] = '[';
    for (int i = 0; i < depth; i++) buf[depth + i] = ']';
    ASSERT_NULL(neverc_json_parse(buf, (size_t)(depth * 2)));

    /* Leaves must not consume a nesting slot: 1000-deep `[0]` is the same
     * container depth as empty `[[[]]]` and must round-trip. */
    {
        char leaf[2012];
        int d = 1000;
        for (int i = 0; i < d; i++) leaf[i] = '[';
        leaf[d] = '0';
        for (int i = 0; i < d; i++) leaf[d + 1 + i] = ']';
        size_t nlen = (size_t)d * 2U + 1U;
        neverc_json_value_t *v2 = neverc_json_parse(leaf, nlen);
        ASSERT_NOT_NULL(v2);
        if (v2) {
            char out[2012];
            int n = neverc_json_marshal(v2, out, sizeof(out), NULL);
            ASSERT_INT_EQ(n, (int)nlen);
            if (n == (int)nlen)
                ASSERT_INT_EQ(memcmp(out, leaf, nlen) == 0, 1);
            neverc_json_free(v2);
        }

        d = 1001;
        for (int i = 0; i < d; i++) leaf[i] = '[';
        leaf[d] = '0';
        for (int i = 0; i < d; i++) leaf[d + 1 + i] = ']';
        ASSERT_NULL(neverc_json_parse(leaf, (size_t)d * 2U + 1U));
    }

    /* Same container-depth rule for objects: 1000-deep `{"a":null}` parses. */
    {
        int d = 1000;
        size_t cap = (size_t)d * 6U + 8U;
        char *ob = (char *)malloc(cap);
        ASSERT_NOT_NULL(ob);
        if (ob) {
            size_t w = 0;
            for (int i = 0; i < d; i++) {
                memcpy(ob + w, "{\"a\":", 5);
                w += 5;
            }
            memcpy(ob + w, "null", 4);
            w += 4;
            for (int i = 0; i < d; i++) ob[w++] = '}';
            neverc_json_value_t *vo = neverc_json_parse(ob, w);
            ASSERT_NOT_NULL(vo);
            if (vo) {
                char *out = (char *)malloc(cap);
                ASSERT_NOT_NULL(out);
                if (out) {
                    int n = neverc_json_marshal(vo, out, cap, NULL);
                    ASSERT_INT_EQ(n, (int)w);
                    if (n == (int)w)
                        ASSERT_INT_EQ(memcmp(out, ob, w) == 0, 1);
                    free(out);
                }
                neverc_json_free(vo);
            }
            free(ob);
        }
    }
}

static void test_deep_constructor_free(void) {
    printf("[deep_constructor_free]\n");
    neverc_json_value_t *root = neverc_json_new_array();
    neverc_json_value_t *cur = root;
    ASSERT_NOT_NULL(root);
    int ok = root != NULL;
    for (int i = 0; ok && i < 5000; i++) {
        neverc_json_value_t *inner = neverc_json_new_array();
        if (!inner || neverc_json_array_append(cur, inner) != 0) {
            neverc_json_free(inner);
            ok = 0;
            break;
        }
        cur = inner;
    }
    ASSERT_TRUE(ok);
    neverc_json_free(root);
}

int main(void) {
    printf("=== NeverC encoding/json Tests ===\n");
    test_parse_null();
    test_parse_bool();
    test_parse_number();
    test_parse_string();
    test_embedded_nul();
    test_parse_array();
    test_parse_object();
    test_parse_complex();
    test_marshal();
    test_marshal_escapes();
    test_marshal_array();
    test_valid();
    test_whitespace();
    test_escape_sequences();
    test_unicode_escape();
    test_large_strings();
    test_large_and_dup_objects();
    test_keymap_hash_lengths();
    test_invalid_api_inputs();
    test_tree_ownership();
    test_utf8_bom();
    test_nesting_limit();
    test_deep_constructor_free();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
