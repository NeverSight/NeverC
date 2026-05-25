// NeverC builtin string .encrypt() tests
// RUN: %neverc -std=c23 %s -o %t && %t

#include <stdio.h>

static int test_basic(void) {
    string s = "Hello World".encrypt();
    if (s != "Hello World".encrypt()) return 1;
    if (s.len != 11) return 2;
    printf("basic: %s\n", s.c_str());
    return 0;
}

static int test_chain(void) {
    string upper = "neverc".encrypt().to_upper();
    if (upper != "NEVERC".encrypt()) return 3;
    return 0;
}

static int test_search(void) {
    string path = "/usr/local/bin".encrypt();
    if (path.find("local".encrypt()) == NEVERC_STRING_NPOS) return 4;
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
    if (multi1 != "alpha".encrypt()) return 6;
    if (multi2 != "beta".encrypt()) return 7;
    if (multi1 == multi2) return 8;
    return 0;
}

static int test_substr(void) {
    string sub = "hello world".encrypt();
    string s2 = sub.substr(6, 5);
    if (s2 != "world".encrypt()) return 9;
    return 0;
}

static int test_clone(void) {
    string clone = "cloned".encrypt().clone();
    if (clone != "cloned".encrypt()) return 10;
    return 0;
}

static int test_decrypt_equals(void) {
    string input = "secret".encrypt();
    if (input == "secret".encrypt()) {
    } else {
        return 11;
    }
    if (input == "wrong".encrypt()) return 12;
    return 0;
}

static int test_decrypt_not_equals(void) {
    string input = "admin".encrypt();
    if (input != "admin".encrypt()) return 13;
    if (!(input != "user".encrypt())) return 14;
    return 0;
}

static int test_decrypt_equals_empty(void) {
    string empty = "".encrypt();
    if (!(empty == "".encrypt())) return 15;
    if (empty == "nonempty".encrypt()) return 16;
    return 0;
}

static int test_decrypt_equals_reversed(void) {
    string val = "hello".encrypt();
    if ("hello".encrypt() == val) {
    } else {
        return 17;
    }
    if ("world".encrypt() == val) return 18;
    return 0;
}

static int test_decrypt_equals_owned(void) {
    string owned = "owned_string".encrypt().clone();
    if (owned != "owned_string".encrypt()) return 19;
    if (owned == "other".encrypt()) return 20;
    return 0;
}

static int test_decrypt_starts_with(void) {
    string path = "/usr/local/bin".encrypt();
    if (!path.starts_with("/usr".encrypt())) return 21;
    if (path.starts_with("/etc".encrypt())) return 22;
    if (!path.starts_with("".encrypt())) return 23;
    return 0;
}

static int test_decrypt_ends_with(void) {
    string path = "/usr/local/bin".encrypt();
    if (!path.ends_with("bin".encrypt())) return 24;
    if (path.ends_with("lib".encrypt())) return 25;
    if (!path.ends_with("".encrypt())) return 26;
    return 0;
}

static int test_decrypt_contains(void) {
    string s = "hello world".encrypt();
    if (!s.contains("world".encrypt())) return 27;
    if (!s.contains("hello".encrypt())) return 28;
    if (s.contains("xyz".encrypt())) return 29;
    if (!s.contains("".encrypt())) return 30;
    return 0;
}

static int test_decrypt_compare(void) {
    string s = "banana".encrypt();
    if (!(s < "cherry".encrypt())) return 31;
    if (!(s > "apple".encrypt())) return 32;
    if (s < "banana".encrypt()) return 33;
    if (s > "banana".encrypt()) return 34;
    return 0;
}

static int test_decrypt_eq_ic(void) {
    string s = "Hello".encrypt();
    if (!s.eq_ic("hello".encrypt())) return 35;
    if (!s.eq_ic("HELLO".encrypt())) return 36;
    if (s.eq_ic("world".encrypt())) return 37;
    return 0;
}

static int test_decrypt_starts_with_ic(void) {
    string s = "Hello World".encrypt();
    if (!s.starts_with_ic("hello".encrypt())) return 38;
    if (!s.starts_with_ic("HELLO".encrypt())) return 39;
    if (s.starts_with_ic("world".encrypt())) return 40;
    return 0;
}

static int test_decrypt_ends_with_ic(void) {
    string s = "Hello World".encrypt();
    if (!s.ends_with_ic("world".encrypt())) return 41;
    if (!s.ends_with_ic("WORLD".encrypt())) return 42;
    if (s.ends_with_ic("hello".encrypt())) return 43;
    return 0;
}

static int test_decrypt_contains_ic(void) {
    string s = "Hello World".encrypt();
    if (!s.contains_ic("WORLD".encrypt())) return 44;
    if (!s.contains_ic("hello".encrypt())) return 45;
    if (s.contains_ic("xyz".encrypt())) return 46;
    return 0;
}

static int test_decrypt_both_encrypted(void) {
    string s = "password".encrypt();
    if (s != "password".encrypt()) return 70;
    if (!(s == "password".encrypt())) return 71;
    if (s == "wrong".encrypt()) return 72;
    if (!(s != "wrong".encrypt())) return 73;
    return 0;
}

static int test_decrypt_encrypted_methods(void) {
    string s = "Hello World".encrypt();
    if (!s.starts_with("Hello".encrypt())) return 74;
    if (!s.ends_with("World".encrypt())) return 75;
    if (!s.contains("lo Wo".encrypt())) return 76;
    if (s.find("World".encrypt()) != 6) return 77;
    if (!s.eq_ic("hello world".encrypt())) return 78;
    if (!s.contains_ic("WORLD".encrypt())) return 79;
    return 0;
}

static int test_decrypt_encrypted_relational(void) {
    string s = "banana".encrypt();
    if (!(s < "cherry".encrypt())) return 80;
    if (!(s > "apple".encrypt())) return 81;
    if (s < "banana".encrypt()) return 82;
    if (s > "banana".encrypt()) return 83;
    return 0;
}

static int test_decrypt_find(void) {
    string s = "hello world hello".encrypt();
    __SIZE_TYPE__ pos = s.find("world".encrypt());
    if (pos != 6) return 47;
    if (s.find("xyz".encrypt()) != NEVERC_STRING_NPOS) return 48;
    if (s.find("hello".encrypt()) != 0) return 49;
    if (s.find("".encrypt()) != 0) return 50;
    return 0;
}

static int test_decrypt_find_from(void) {
    string s = "abcabc".encrypt();
    __SIZE_TYPE__ pos = s.find("abc".encrypt(), 1);
    if (pos != 3) return 51;
    if (s.find("abc".encrypt(), 0) != 0) return 52;
    if (s.find("abc".encrypt(), 4) != NEVERC_STRING_NPOS) return 53;
    return 0;
}

static int test_decrypt_rfind(void) {
    string s = "hello world hello".encrypt();
    __SIZE_TYPE__ pos = s.rfind("hello".encrypt());
    if (pos != 12) return 54;
    if (s.rfind("xyz".encrypt()) != NEVERC_STRING_NPOS) return 55;
    if (s.rfind("world".encrypt()) != 6) return 56;
    return 0;
}

static int test_decrypt_rfind_to(void) {
    string s = "abcabc".encrypt();
    if (s.rfind("abc".encrypt(), 2) != 0) return 57;
    if (s.rfind("abc".encrypt(), 5) != 3) return 58;
    if (s.rfind("abc".encrypt(), 0) != 0) return 59;
    return 0;
}

static int test_decrypt_find_ic(void) {
    string s = "Hello World".encrypt();
    __SIZE_TYPE__ pos = s.find_ic("WORLD".encrypt());
    if (pos != 6) return 60;
    if (s.find_ic("hello".encrypt()) != 0) return 61;
    if (s.find_ic("xyz".encrypt()) != NEVERC_STRING_NPOS) return 62;
    return 0;
}

static int test_decrypt_count(void) {
    string s = "abcabcabc".encrypt();
    if (s.count("abc".encrypt()) != 3) return 63;
    if (s.count("xyz".encrypt()) != 0) return 64;
    if (s.count("a".encrypt()) != 3) return 65;
    return 0;
}

static int test_decrypt_single_char(void) {
    string s = "x".encrypt();
    if (s != "x".encrypt()) return 84;
    if (s == "y".encrypt()) return 85;
    if (!s.starts_with("x".encrypt())) return 86;
    if (!s.ends_with("x".encrypt())) return 87;
    if (!s.contains("x".encrypt())) return 88;
    if (s.find("x".encrypt()) != 0) return 89;
    if (s.count("x".encrypt()) != 1) return 90;
    return 0;
}

static int test_decrypt_full_match(void) {
    string s = "exact".encrypt();
    if (!s.starts_with("exact".encrypt())) return 91;
    if (!s.ends_with("exact".encrypt())) return 92;
    if (s.find("exact".encrypt()) != 0) return 93;
    if (s.rfind("exact".encrypt()) != 0) return 94;
    if (s.count("exact".encrypt()) != 1) return 95;
    return 0;
}

static int test_decrypt_longer_than_haystack(void) {
    string s = "hi".encrypt();
    if (s == "hello".encrypt()) return 96;
    if (s.starts_with("hello".encrypt())) return 97;
    if (s.ends_with("hello".encrypt())) return 98;
    if (s.contains("hello".encrypt())) return 99;
    if (s.find("hello".encrypt()) != NEVERC_STRING_NPOS) return 100;
    if (s.rfind("hello".encrypt()) != NEVERC_STRING_NPOS) return 101;
    if (s.count("hello".encrypt()) != 0) return 102;
    return 0;
}

static int test_decrypt_empty_haystack(void) {
    string s = "".encrypt();
    if (s == "a".encrypt()) return 103;
    if (s != "".encrypt()) return 104;
    if (s.contains("a".encrypt())) return 105;
    if (s.find("a".encrypt()) != NEVERC_STRING_NPOS) return 106;
    if (!s.starts_with("".encrypt())) return 107;
    if (!s.ends_with("".encrypt())) return 108;
    return 0;
}

static int test_decrypt_count_overlapping(void) {
    string s = "aaa".encrypt();
    if (s.count("aa".encrypt()) != 1) return 109;
    if (s.count("a".encrypt()) != 3) return 110;
    return 0;
}

static int test_decrypt_rfind_edge(void) {
    string s = "abcabc".encrypt();
    if (s.rfind("abc".encrypt()) != 3) return 111;
    if (s.rfind("abc".encrypt(), 0) != 0) return 112;
    if (s.rfind("abc".encrypt(), 1) != 0) return 113;
    if (s.rfind("abc".encrypt(), 3) != 3) return 114;
    return 0;
}

static int test_decrypt_in_loop(void) {
    string keys[] = {"admin".encrypt(), "root".encrypt(), "user".encrypt()};
    int found = 0;
    for (int i = 0; i < 3; i++) {
        if (keys[i] == "root".encrypt()) {
            found = 1;
            break;
        }
    }
    if (!found) return 115;

    int admin_idx = -1;
    for (int i = 0; i < 3; i++) {
        if (keys[i].starts_with("adm".encrypt())) {
            admin_idx = i;
            break;
        }
    }
    if (admin_idx != 0) return 116;
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
    if (!r) r = test_decrypt_equals();
    if (!r) r = test_decrypt_not_equals();
    if (!r) r = test_decrypt_equals_empty();
    if (!r) r = test_decrypt_equals_reversed();
    if (!r) r = test_decrypt_equals_owned();
    if (!r) r = test_decrypt_starts_with();
    if (!r) r = test_decrypt_ends_with();
    if (!r) r = test_decrypt_contains();
    if (!r) r = test_decrypt_compare();
    if (!r) r = test_decrypt_eq_ic();
    if (!r) r = test_decrypt_starts_with_ic();
    if (!r) r = test_decrypt_ends_with_ic();
    if (!r) r = test_decrypt_contains_ic();
    if (!r) r = test_decrypt_both_encrypted();
    if (!r) r = test_decrypt_encrypted_methods();
    if (!r) r = test_decrypt_encrypted_relational();
    if (!r) r = test_decrypt_find();
    if (!r) r = test_decrypt_find_from();
    if (!r) r = test_decrypt_rfind();
    if (!r) r = test_decrypt_rfind_to();
    if (!r) r = test_decrypt_find_ic();
    if (!r) r = test_decrypt_count();
    if (!r) r = test_decrypt_single_char();
    if (!r) r = test_decrypt_full_match();
    if (!r) r = test_decrypt_longer_than_haystack();
    if (!r) r = test_decrypt_empty_haystack();
    if (!r) r = test_decrypt_count_overlapping();
    if (!r) r = test_decrypt_rfind_edge();
    if (!r) r = test_decrypt_in_loop();
    if (r != 0) printf("FAIL: r=%d\n", r);
    else printf("test_neverc_string_encrypt: ALL PASSED\n");
    return r;
}
