// NeverC string composite type cleanup tests
// Verifies that string members in arrays, structs, and nested combinations
// are automatically freed when their containing variable goes out of scope.

#include <stdio.h>

typedef struct {
    string name;
    string value;
} kv_pair;

typedef struct {
    string items[3];
} string_container;

typedef struct {
    string label;
    string tags[2];
} tagged_item;

static int test_struct_cleanup(void) {
    kv_pair p = {.name = "key".encrypt(), .value = "val".encrypt()};
    if (p.name != "key".encrypt()) return 1;
    if (p.value != "val".encrypt()) return 2;
    return 0;
}

static int test_array_1d(void) {
    string keys[] = {"admin".encrypt(), "root".encrypt(), "user".encrypt()};
    if (keys[0] != "admin".encrypt()) return 3;
    if (keys[1] != "root".encrypt()) return 4;
    if (keys[2] != "user".encrypt()) return 5;
    return 0;
}

static int test_array_2d(void) {
    string grid[2][2] = {
        {"a".encrypt(), "b".encrypt()},
        {"c".encrypt(), "d".encrypt()}
    };
    if (grid[0][0] != "a".encrypt()) return 6;
    if (grid[1][1] != "d".encrypt()) return 7;
    return 0;
}

static int test_struct_with_array(void) {
    string_container c = {.items = {"x".encrypt(), "y".encrypt(), "z".encrypt()}};
    if (c.items[0] != "x".encrypt()) return 8;
    if (c.items[1] != "y".encrypt()) return 9;
    if (c.items[2] != "z".encrypt()) return 10;
    return 0;
}

static int test_struct_mixed(void) {
    tagged_item item = {
        .label = "title".encrypt(),
        .tags = {"tag1".encrypt(), "tag2".encrypt()}
    };
    if (item.label != "title".encrypt()) return 11;
    if (item.tags[0] != "tag1".encrypt()) return 12;
    if (item.tags[1] != "tag2".encrypt()) return 13;
    return 0;
}

static int test_array_of_structs(void) {
    kv_pair pairs[] = {
        {.name = "host".encrypt(), .value = "localhost".encrypt()},
        {.name = "port".encrypt(), .value = "8080".encrypt()},
    };
    if (pairs[0].name != "host".encrypt()) return 14;
    if (pairs[1].value != "8080".encrypt()) return 15;
    return 0;
}

static int test_loop_with_array(void) {
    string keys[] = {"admin".encrypt(), "root".encrypt(), "user".encrypt()};
    int found = 0;
    for (int i = 0; i < 3; i++) {
        if (keys[i] == "root".encrypt()) {
            found = 1;
            break;
        }
    }
    if (!found) return 16;
    return 0;
}

static int test_early_return(void) {
    kv_pair p = {.name = "a".encrypt(), .value = "b".encrypt()};
    if (p.name == "a".encrypt()) return 0;
    return 17;
}

int main(void) {
    int r = 0;
    if (!r) r = test_struct_cleanup();
    if (!r) r = test_array_1d();
    if (!r) r = test_array_2d();
    if (!r) r = test_struct_with_array();
    if (!r) r = test_struct_mixed();
    if (!r) r = test_array_of_structs();
    if (!r) r = test_loop_with_array();
    if (!r) r = test_early_return();
    if (r != 0) printf("FAIL: r=%d\n", r);
    else printf("test_neverc_string_composite_cleanup: ALL PASSED\n");
    return r;
}
