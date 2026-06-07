/*
 * NeverC encoding/json tests.
 * Tests parse, marshal, query, constructors, edge cases.
 */
#include "neverc/encoding/json.h"
#include <stdio.h>
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
    ASSERT_FALSE(neverc_json_valid("[1,]", 4));
    ASSERT_FALSE(neverc_json_valid("", 0));
    ASSERT_FALSE(neverc_json_valid("nul", 3));
}

static void test_whitespace(void) {
    printf("[whitespace]\n");
    const char *json = "  {  \"a\"  :  1  ,  \"b\"  :  2  }  ";
    neverc_json_value_t *v = neverc_json_parse(json, strlen(json));
    ASSERT_NOT_NULL(v);
    ASSERT_DBL_EQ(neverc_json_number(neverc_json_object_get(v, "a")), 1.0, 1e-10);
    ASSERT_DBL_EQ(neverc_json_number(neverc_json_object_get(v, "b")), 2.0, 1e-10);
    neverc_json_free(v);
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

int main(void) {
    printf("=== NeverC encoding/json Tests ===\n");
    test_parse_null();
    test_parse_bool();
    test_parse_number();
    test_parse_string();
    test_parse_array();
    test_parse_object();
    test_parse_complex();
    test_marshal();
    test_marshal_array();
    test_valid();
    test_whitespace();
    test_escape_sequences();
    test_unicode_escape();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
