// NeverC builtin string .encrypt() tests
// RUN: %neverc -std=c23 %s -o %t && %t

#include <stdio.h>

static int test_basic(void) {
    string s = "Hello World".encrypt();
    if (s != "Hello World") return 1;
    if (s.len != 11) return 2;
    printf("basic: %s\n", s.c_str());
    return 0;
}

static int test_chain(void) {
    string upper = "neverc".encrypt().to_upper();
    if (upper != "NEVERC") return 3;
    return 0;
}

static int test_search(void) {
    string path = "/usr/local/bin".encrypt();
    if (path.find("local") == NEVERC_STRING_NPOS) return 4;
    return 0;
}

static int test_empty(void) {
    string empty = "".encrypt();
    if (!empty.empty()) return 5;
    return 0;
}

static int test_multi(void) {
    string multi1 = "alpha".encrypt();
    string multi2 = "beta".encrypt();
    if (multi1 != "alpha") return 6;
    if (multi2 != "beta") return 7;
    if (multi1 == multi2) return 8;
    return 0;
}

static int test_substr(void) {
    string sub = "hello world".encrypt();
    string s2 = sub.substr(6, 5);
    if (s2 != "world") return 9;
    return 0;
}

static int test_clone(void) {
    string clone = "cloned".encrypt().clone();
    if (clone != "cloned") return 10;
    return 0;
}

int main(void) {
    int r = 0;
    if (!r) r = test_basic();
    if (!r) r = test_chain();
    if (!r) r = test_search();
    if (!r) r = test_empty();
    if (!r) r = test_multi();
    if (!r) r = test_substr();
    if (!r) r = test_clone();
    if (r != 0) printf("FAIL: r=%d\n", r);
    else printf("test_neverc_string_encrypt: ALL PASSED\n");
    return r;
}
