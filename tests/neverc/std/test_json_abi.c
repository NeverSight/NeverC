#include "neverc/std/encoding/json.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct v3389_json_value v3389_json_value_t;
typedef struct {
    char *key;
    v3389_json_value_t *value;
} v3389_json_pair_t;

struct v3389_json_value {
    neverc_json_type_t type;
    union {
        int bool_val;
        double num_val;
        char *str_val;
        struct {
            v3389_json_value_t **items;
            int len;
            int cap;
        } arr;
        struct {
            v3389_json_pair_t *pairs;
            int len;
            int cap;
        } obj;
    } u;
};

_Static_assert(sizeof(neverc_json_pair_t) == sizeof(v3389_json_pair_t),
               "v3389.1.4 JSON pair size ABI changed");
_Static_assert(_Alignof(neverc_json_pair_t) == _Alignof(v3389_json_pair_t),
               "v3389.1.4 JSON pair alignment ABI changed");
_Static_assert(offsetof(neverc_json_pair_t, key) ==
                   offsetof(v3389_json_pair_t, key),
               "JSON pair key offset changed");
_Static_assert(offsetof(neverc_json_pair_t, value) ==
                   offsetof(v3389_json_pair_t, value),
               "JSON pair value offset changed");
_Static_assert(sizeof(neverc_json_value_t) == sizeof(v3389_json_value_t),
               "v3389.1.4 JSON value size ABI changed");
_Static_assert(_Alignof(neverc_json_value_t) == _Alignof(v3389_json_value_t),
               "v3389.1.4 JSON value alignment ABI changed");
_Static_assert(offsetof(neverc_json_value_t, type) ==
                   offsetof(v3389_json_value_t, type),
               "JSON value type offset changed");
_Static_assert(offsetof(neverc_json_value_t, u) ==
                   offsetof(v3389_json_value_t, u),
               "JSON value union offset changed");

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(neverc_json_pair_t) == 16,
               "v3389.1.4 JSON pair ABI is two pointers");
_Static_assert(_Alignof(neverc_json_pair_t) == 8,
               "v3389.1.4 JSON pair alignment changed");
_Static_assert(offsetof(neverc_json_pair_t, key) == 0,
               "JSON pair key offset changed");
_Static_assert(offsetof(neverc_json_pair_t, value) == 8,
               "JSON pair value offset changed");
_Static_assert(sizeof(neverc_json_value_t) == 24,
               "v3389.1.4 JSON value ABI changed");
_Static_assert(_Alignof(neverc_json_value_t) == 8,
               "v3389.1.4 JSON value alignment changed");
_Static_assert(offsetof(neverc_json_value_t, type) == 0,
               "JSON value type offset changed");
_Static_assert(offsetof(neverc_json_value_t, u) == 8,
               "JSON value union offset changed");
#endif

static int failures;

#define CHECK(expr) do {                                                     \
    if (!(expr)) {                                                           \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);    \
        failures++;                                                          \
    }                                                                        \
} while (0)

static void test_external_old_layout_is_bounded(void) {
    struct {
        neverc_json_value_t value;
        unsigned char canary[32];
    } external;
    memset(&external, 0, sizeof(external));
    memset(external.canary, 0xa5, sizeof(external.canary));
    external.value.type = NEVERC_JSON_STRING;
    external.value.u.str_val = (char *)"legacy";

    CHECK(neverc_json_string_len(&external.value) == 6U);
    {
        char output[16];
        CHECK(neverc_json_marshal(
                  &external.value, output, sizeof(output), NULL) == 8);
        CHECK(memcmp(output, "\"legacy\"", 8U) == 0);
    }

    /* External public structs cannot safely transfer heap ownership. */
    {
        neverc_json_value_t *array = neverc_json_new_array();
        CHECK(array != NULL);
        if (array) {
            CHECK(neverc_json_array_append(array, &external.value) == -1);
            neverc_json_free(array);
        }
    }
    neverc_json_free(&external.value);
    for (size_t i = 0; i < sizeof(external.canary); i++)
        CHECK(external.canary[i] == 0xa5U);

    /* Old pair arrays retain a two-pointer stride and C-string key fallback. */
    {
        neverc_json_value_t number;
        neverc_json_pair_t pairs[2];
        struct {
            neverc_json_value_t value;
            unsigned char canary[32];
        } object;
        memset(&number, 0, sizeof(number));
        memset(&object, 0, sizeof(object));
        memset(object.canary, 0x3c, sizeof(object.canary));
        number.type = NEVERC_JSON_NUMBER;
        number.u.num_val = 7.0;
        pairs[0].key = (char *)"a";
        pairs[0].value = &number;
        pairs[1].key = (char *)"b";
        pairs[1].value = &number;
        object.value.type = NEVERC_JSON_OBJECT;
        object.value.u.obj.pairs = pairs;
        object.value.u.obj.len = 2;
        object.value.u.obj.cap = 2;
        CHECK(neverc_json_object_get(&object.value, "b") == &number);
        {
            char output[32];
            static const char expected[] = "{\"a\":7,\"b\":7}";
            int length = neverc_json_marshal(
                &object.value, output, sizeof(output), NULL);
            CHECK(length == (int)(sizeof(expected) - 1U));
            CHECK(length < 0 ||
                  memcmp(output, expected, sizeof(expected) - 1U) == 0);
        }
        neverc_json_free(&object.value);
        for (size_t i = 0; i < sizeof(object.canary); i++)
            CHECK(object.canary[i] == 0x3cU);
    }
}

static void test_private_lengths_and_ownership(void) {
    static const char string_bytes[] = {'a', '\0', 'b'};
    static const char key_bytes[] = {'k', '\0', 'y'};
    neverc_json_value_t *object = neverc_json_new_object();
    neverc_json_value_t *string =
        neverc_json_new_string_n(string_bytes, sizeof(string_bytes));
    CHECK(object != NULL);
    CHECK(string != NULL);
    if (!object || !string) {
        neverc_json_free(object);
        neverc_json_free(string);
        return;
    }

    CHECK(neverc_json_string_len(string) == sizeof(string_bytes));
    CHECK(neverc_json_object_set_n(
              object, key_bytes, sizeof(key_bytes), string) == 0);
    CHECK(neverc_json_object_get_n(
              object, key_bytes, sizeof(key_bytes)) == string);
    CHECK(neverc_json_object_set_n(
              object, "other", 5U, string) == -1);
    CHECK(neverc_json_object_set_n(
              object, "cycle", 5U, object) == -1);

    {
        char output[64];
        int length = neverc_json_marshal(
            object, output, sizeof(output), NULL);
        static const char expected[] = "{\"k\\u0000y\":\"a\\u0000b\"}";
        CHECK(length == (int)(sizeof(expected) - 1U));
        CHECK(length < 0 ||
              memcmp(output, expected, sizeof(expected) - 1U) == 0);
    }
    neverc_json_free(string); /* attached-child free remains a no-op */
    CHECK(neverc_json_object_get_n(
              object, key_bytes, sizeof(key_bytes)) == string);
    neverc_json_free(object);
}

int main(void) {
    test_external_old_layout_is_bounded();
    test_private_lengths_and_ownership();
    if (failures == 0) printf("JSON ABI tests passed\n");
    return failures == 0 ? 0 : 1;
}
