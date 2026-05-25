// NeverC builtin string .encrypt() Unicode tests
// RUN: %neverc -std=c23 %s -o %t && %t

#include <stdio.h>

static int test_utf8(void) {
    string u8s = u8"héllo".encrypt();
    if (u8s != u8"héllo") return 1;
    return 0;
}

static int test_wide(void) {
    string ws = L"NeverC \u4E2D\u6587".encrypt();
    if (ws.find("\xe4\xb8\xad\xe6\x96\x87") == NEVERC_STRING_NPOS) return 3;
    return 0;
}

static int test_utf16(void) {
    string us = u"\u4E2D\u6587".encrypt();
    if (us != u8"\xe4\xb8\xad\xe6\x96\x87") return 5;
    return 0;
}

static int test_utf32(void) {
    string Us = U"\U0001F389party".encrypt();
    if (Us.find("party") == NEVERC_STRING_NPOS) return 6;
    if (Us.len != 9) return 9;
    return 0;
}

static int test_newline(void) {
    string nl = "line1\nline2".encrypt();
    if (!nl.contains("\n")) return 7;
    if (nl.find("line1") == NEVERC_STRING_NPOS) return 8;
    return 0;
}

int main(void) {
    int r = 0;
    if (!r) r = test_utf8();
    if (!r) r = test_wide();
    if (!r) r = test_utf16();
    if (!r) r = test_utf32();
    if (!r) r = test_newline();
    if (r != 0) printf("FAIL: r=%d\n", r);
    else printf("test_neverc_string_encrypt_unicode: ALL PASSED\n");
    return r;
}
