#include "neverc/std/mime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_STR_EQ(expr, expected) do { \
    const char *_v = (expr); const char *_e = (expected); tests_run++; \
    if (_v && _e && strcmp(_v, _e) == 0) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = \"%s\", expected \"%s\" (line %d)\n", \
                  #expr, _v ? _v : "(null)", _e ? _e : "(null)", __LINE__); } \
} while(0)

#define ASSERT_NULL(expr) do { tests_run++; \
    if ((expr) == NULL) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: expected NULL (line %d)\n", __LINE__); } \
} while(0)

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (expr); int _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

static void free_params(char *keys[], char *vals[], int count) {
    for (int i = 0; i < count; i++) {
        free(keys[i]);
        free(vals[i]);
    }
}

static void test_type_by_extension(void) {
    printf("[type_by_extension]\n");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".html"), "text/html");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".htm"), "text/html");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".css"), "text/css");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".js"), "text/javascript");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".json"), "application/json");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".png"), "image/png");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".jpg"), "image/jpeg");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".gif"), "image/gif");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".pdf"), "application/pdf");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".xml"), "application/xml");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".zip"), "application/zip");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".tar"), "application/x-tar");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".gz"), "application/gzip");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".wasm"), "application/wasm");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".svg"), "image/svg+xml");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".mp4"), "video/mp4");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".mp3"), "audio/mpeg");
}

static void test_case_insensitive(void) {
    printf("[case_insensitive]\n");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".HTML"), "text/html");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".Png"), "image/png");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".JSON"), "application/json");
}

static void test_unknown_extension(void) {
    printf("[unknown_extension]\n");
    ASSERT_STR_EQ(neverc_mime_type_by_extension(".xyz123"), "application/octet-stream");
    ASSERT_NULL(neverc_mime_type_by_extension(NULL));
}

static void test_extension_by_type(void) {
    printf("[extension_by_type]\n");
    ASSERT_STR_EQ(neverc_mime_extension_by_type("text/html"), ".htm");
    ASSERT_STR_EQ(neverc_mime_extension_by_type("application/json"), ".json");
    ASSERT_STR_EQ(neverc_mime_extension_by_type("image/png"), ".png");
    ASSERT_STR_EQ(neverc_mime_extension_by_type(
                      "  Text/HTML ; charset=utf-8"), ".htm");
    ASSERT_NULL(neverc_mime_extension_by_type("application/x-unknown-thing"));
    ASSERT_NULL(neverc_mime_extension_by_type(NULL));
}

static void test_parse_media_type(void) {
    printf("[parse_media_type]\n");
    char mt[128];
    char *keys[8], *vals[8];
    int nparams;

    neverc_mime_parse_media_type("text/html; charset=utf-8",
                                 mt, sizeof(mt), keys, vals, 8, &nparams);
    ASSERT_STR_EQ(mt, "text/html");
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "charset");
    ASSERT_STR_EQ(vals[0], "utf-8");
    free(keys[0]); free(vals[0]);

    neverc_mime_parse_media_type("application/json", mt, sizeof(mt),
                                 keys, vals, 8, &nparams);
    ASSERT_STR_EQ(mt, "application/json");
    ASSERT_INT_EQ(nparams, 0);
}

static void test_parse_quoted_params(void) {
    printf("[parse_quoted_params]\n");
    char mt[128];
    char *keys[8], *vals[8];
    int nparams;

    neverc_mime_parse_media_type("multipart/form-data; boundary=\"----WebKit\"",
                                 mt, sizeof(mt), keys, vals, 8, &nparams);
    ASSERT_STR_EQ(mt, "multipart/form-data");
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "boundary");
    ASSERT_STR_EQ(vals[0], "----WebKit");
    free(keys[0]); free(vals[0]);

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "Text/Plain; boundary=\"a\\\";b\\\\c\"; CHARSET = utf-8",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_STR_EQ(mt, "text/plain");
    ASSERT_INT_EQ(nparams, 2);
    ASSERT_STR_EQ(keys[0], "boundary");
    ASSERT_STR_EQ(vals[0], "a\";b\\c");
    ASSERT_STR_EQ(keys[1], "charset");
    ASSERT_STR_EQ(vals[1], "utf-8");
    free_params(keys, vals, nparams);
}

static void test_parse_rejects_invalid_input(void) {
    printf("[parse_rejects_invalid_input]\n");
    char mt[32] = "unchanged";
    char *keys[2] = {NULL, NULL};
    char *vals[2] = {NULL, NULL};
    int nparams = 99;

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/html", mt, 0, keys, vals, 2, &nparams), -1);
    ASSERT_INT_EQ(nparams, 0);
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/html", mt, sizeof(mt), keys, vals, 2, NULL), -1);

    const char *invalid[] = {
        "text", "text/", "/plain", "text/plain garbage",
        "text/plain; charset", "text/plain; =utf-8",
        "text/plain; charset=", "text/plain; charset=\"unterminated",
        "text/plain; charset=utf-8 trailing",
        "text/plain; charset=utf-8; CHARSET=ascii"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        strcpy(mt, "unchanged");
        nparams = 99;
        ASSERT_INT_EQ(neverc_mime_parse_media_type(
                          invalid[i], mt, sizeof(mt), keys, vals, 2,
                          &nparams), -1);
        ASSERT_STR_EQ(mt, "");
        ASSERT_INT_EQ(nparams, 0);
    }

    strcpy(mt, "unchanged");
    nparams = 99;
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; a=1; b=2; c=3", mt, sizeof(mt),
                      keys, vals, 2, &nparams), -1);
    ASSERT_STR_EQ(mt, "");
    ASSERT_INT_EQ(nparams, 0);

    char tiny[4] = "old";
    nparams = 99;
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain", tiny, sizeof(tiny), keys, vals, 2,
                      &nparams), -1);
    ASSERT_STR_EQ(tiny, "");
    ASSERT_INT_EQ(nparams, 0);
}

static void test_format_media_type(void) {
    printf("[format_media_type]\n");
    char out[256];
    const char *keys[] = {"charset"};
    const char *vals[] = {"utf-8"};

    int len = neverc_mime_format_media_type("text/html", keys, vals, 1, out, sizeof(out));
    ASSERT_STR_EQ(out, "text/html; charset=utf-8");
    ASSERT_INT_EQ(len, (int)strlen("text/html; charset=utf-8"));

    const char *quoted_keys[] = {"boundary", "empty"};
    const char *quoted_vals[] = {"a\";b\\c", ""};
    len = neverc_mime_format_media_type("Text/Plain", quoted_keys,
                                        quoted_vals, 2, out, sizeof(out));
    ASSERT_STR_EQ(out,
                  "text/plain; boundary=\"a\\\";b\\\\c\"; empty=\"\"");
    ASSERT_INT_EQ(len, (int)strlen(out));
}

static void test_format_rejects_invalid_input(void) {
    printf("[format_rejects_invalid_input]\n");
    char out[64] = "unchanged";
    const char *keys[] = {"charset"};
    const char *vals[] = {"utf-8"};
    const char *bad_keys[] = {"bad key"};
    const char *bad_vals[] = {"ok\r\nInjected: yes"};
    const char *null_keys[] = {NULL};

    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "invalid", keys, vals, 1, out, sizeof(out)), -1);
    ASSERT_STR_EQ(out, "");
    strcpy(out, "unchanged");
    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "text/plain", bad_keys, vals, 1, out,
                      sizeof(out)), -1);
    ASSERT_STR_EQ(out, "");
    strcpy(out, "unchanged");
    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "text/plain", keys, bad_vals, 1, out,
                      sizeof(out)), -1);
    ASSERT_STR_EQ(out, "");
    strcpy(out, "unchanged");
    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "text/plain", null_keys, vals, 1, out,
                      sizeof(out)), -1);
    ASSERT_STR_EQ(out, "");
    strcpy(out, "unchanged");
    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "text/plain", keys, vals, -1, out,
                      sizeof(out)), -1);
    ASSERT_STR_EQ(out, "");

    char tiny[12] = "unchanged";
    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "text/plain", keys, vals, 1, tiny,
                      sizeof(tiny)), -1);
    ASSERT_STR_EQ(tiny, "");
}

static void test_qp_decode(void) {
    printf("[qp_decode]\n");
    char out[256];
    size_t out_len;

    neverc_mime_qp_decode("Hello=20World", 13, out, sizeof(out), &out_len);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "Hello World");

    neverc_mime_qp_decode("=E4=BD=A0=E5=A5=BD", 18, out, sizeof(out), &out_len);
    ASSERT_INT_EQ((int)out_len, 6);

    ASSERT_INT_EQ(neverc_mime_qp_decode("a=\nb", 4, out,
                                        sizeof(out), &out_len), 0);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "ab");

    out_len = 99;
    ASSERT_INT_EQ(neverc_mime_qp_decode("abcd", 4, out, 3, &out_len), -1);
    ASSERT_INT_EQ((int)out_len, 0);
    ASSERT_INT_EQ(neverc_mime_qp_decode(NULL, 1, out,
                                        sizeof(out), &out_len), -1);
    ASSERT_INT_EQ(neverc_mime_qp_decode("x", 1, NULL, 1, &out_len), -1);
}

static void test_qp_encode(void) {
    printf("[qp_encode]\n");
    char out[256];
    size_t out_len;

    neverc_mime_qp_encode("Hello World", 11, out, sizeof(out), &out_len);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "Hello World");

    neverc_mime_qp_encode("\x80\xFF", 2, out, sizeof(out), &out_len);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "=80=FF");

    out_len = 99;
    ASSERT_INT_EQ(neverc_mime_qp_encode("\x80", 1, out, 2, &out_len), -1);
    ASSERT_INT_EQ((int)out_len, 0);
    ASSERT_INT_EQ(neverc_mime_qp_encode(NULL, 1, out,
                                        sizeof(out), &out_len), -1);
    ASSERT_INT_EQ(neverc_mime_qp_encode("x", 1, NULL, 1, &out_len), -1);
}

int main(void) {
    printf("=== NeverC mime Tests ===\n");
    test_type_by_extension();
    test_case_insensitive();
    test_unknown_extension();
    test_extension_by_type();
    test_parse_media_type();
    test_parse_quoted_params();
    test_parse_rejects_invalid_input();
    test_format_media_type();
    test_format_rejects_invalid_input();
    test_qp_decode();
    test_qp_encode();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
