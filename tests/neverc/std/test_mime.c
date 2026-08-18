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

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
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

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; charset=utf-8;",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_STR_EQ(mt, "text/plain");
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "charset");
    ASSERT_STR_EQ(vals[0], "utf-8");
    free_params(keys, vals, nparams);

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/html;", mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_STR_EQ(mt, "text/html");
    ASSERT_INT_EQ(nparams, 0);

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; charset=utf-8; CHARSET=utf-8",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(vals[0], "utf-8");
    free_params(keys, vals, nparams);

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "application/octet-stream; filename*=utf-8''na%C3%AFve.txt",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_STR_EQ(mt, "application/octet-stream");
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "filename");
    ASSERT_STR_EQ(vals[0], "na\xC3\xAFve.txt");
    free_params(keys, vals, nparams);

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; filename=plain.txt; filename*=utf-8''star.txt",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "filename");
    ASSERT_STR_EQ(vals[0], "star.txt");
    free_params(keys, vals, nparams);

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "application/octet-stream; filename*=utf-8''x%0d%0a.txt",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 0);

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "application/octet-stream; filename*=utf-8''%c0%8d.txt",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 0);

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; filename=safe.txt; "
                      "filename*=utf-8''evil%0d%0a.txt",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "filename");
    ASSERT_STR_EQ(vals[0], "safe.txt");
    free_params(keys, vals, nparams);

    /* Empty 2231 charset is US-ASCII (RFC 2231). */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "application/octet-stream; filename*=''hello.txt",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "filename");
    ASSERT_STR_EQ(vals[0], "hello.txt");
    free_params(keys, vals, nparams);

    /* RFC 2231 continuations (Go parseMediaTypeTests / RFC 2231 p.3). */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "message/external-body; access-type=URL; "
                      "URL*0=\"ftp://\";"
                      "URL*1=\"cs.utk.edu/pub/moore/bulk-mailer/"
                      "bulk-mailer.tar\"",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_STR_EQ(mt, "message/external-body");
    ASSERT_INT_EQ(nparams, 2);
    ASSERT_STR_EQ(keys[0], "access-type");
    ASSERT_STR_EQ(vals[0], "URL");
    ASSERT_STR_EQ(keys[1], "url");
    ASSERT_STR_EQ(vals[1],
                  "ftp://cs.utk.edu/pub/moore/bulk-mailer/bulk-mailer.tar");
    free_params(keys, vals, nparams);

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "application/x-stuff; "
                      "title*0*=us-ascii'en'This%20is%20even%20more%20; "
                      "title*1*=%2A%2A%2Afun%2A%2A%2A%20; "
                      "title*2=\"isn't it!\"",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "title");
    ASSERT_STR_EQ(vals[0], "This is even more ***fun*** isn't it!");
    free_params(keys, vals, nparams);

    /* name* single wins; leftover name*0 is not stitched. */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; filename*=utf-8''star.txt; "
                      "filename*0=ignored",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "filename");
    ASSERT_STR_EQ(vals[0], "star.txt");
    free_params(keys, vals, nparams);

    /* name*1 without name*0 is not a filename (Go #attfnconts1). */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; filename*1=\"foo.\"; filename*2=\"html\"",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 0);

    /* Continuation overwrites a plain sibling. */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; filename=plain.txt; "
                      "filename*0=\"foo.\"; filename*1=\"html\"",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "filename");
    ASSERT_STR_EQ(vals[0], "foo.html");
    free_params(keys, vals, nparams);

    /* filename*01 is not filename*1 (Go #attfncontlz). */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; filename*0=\"foo\"; filename*01=\"bar\"",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "filename");
    ASSERT_STR_EQ(vals[0], "foo");
    free_params(keys, vals, nparams);

    /* Unsupported 2231 charset on a continuation must not invent "". */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "application/octet-stream; "
                      "filename*0*=iso-8859-1''hello.txt",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 0);

    /* Failed first piece must not keep later pieces as a truncated name. */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "application/octet-stream; "
                      "filename*0*=iso-8859-1''caf%E9; filename*1=\".txt\"",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 0);

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; filename=\"=?utf-8?q?=0D=0ABcc:_x?=\"",
                      mt, sizeof(mt), keys, vals, 8, &nparams), -1);
    ASSERT_INT_EQ(nparams, 0);

    /* CTL in a 2231 continuation is dropped, not stitched to "". */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "application/octet-stream; "
                      "filename*0*=utf-8''x%0d%0a.txt",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 0);

    /* Invalid percent in a later encoded piece drops the whole stitch. */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "application/octet-stream; "
                      "filename*0*=utf-8''ok; filename*1*=%ZZ",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 0);

    /* Failed continuation does not clobber a plain sibling. */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; filename=safe.txt; "
                      "filename*0*=iso-8859-1''evil.txt",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "filename");
    ASSERT_STR_EQ(vals[0], "safe.txt");
    free_params(keys, vals, nparams);
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

    /* MSIE intranet / Windows paths: `\` is literal unless the next
     * byte is a tspecial (Go consumeValue). */
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "application/octet-stream; "
                      "filename=\"C:\\dev\\go\\robots.txt\"",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(keys[0], "filename");
    ASSERT_STR_EQ(vals[0], "C:\\dev\\go\\robots.txt");
    free_params(keys, vals, nparams);

    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "application/octet-stream; filename=\"f\\oo.html\"",
                      mt, sizeof(mt), keys, vals, 8, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(vals[0], "f\\oo.html");
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

    const char *quoted_keys[] = {"name", "empty"};
    const char *quoted_vals[] = {"a\";b\\c", ""};
    len = neverc_mime_format_media_type("Text/Plain", quoted_keys,
                                        quoted_vals, 2, out, sizeof(out));
    ASSERT_STR_EQ(out,
                  "text/plain; name=\"a\\\";b\\\\c\"; empty=\"\"");
    ASSERT_INT_EQ(len, (int)strlen(out));

    const char *bnd_keys[] = {"boundary"};
    const char *bnd_ok[] = {"simple boundary"};
    len = neverc_mime_format_media_type("multipart/mixed", bnd_keys, bnd_ok,
                                        1, out, sizeof(out));
    ASSERT_STR_EQ(out, "multipart/mixed; boundary=\"simple boundary\"");
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

    const char *bnd_keys[] = {"boundary"};
    const char *at_vals[] = {"foo@bar"};
    strcpy(out, "unchanged");
    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "multipart/mixed", bnd_keys, at_vals, 1, out,
                      sizeof(out)), -1);
    ASSERT_STR_EQ(out, "");

    char long_b[72];
    memset(long_b, 'x', 71);
    long_b[71] = '\0';
    const char *long_vals[] = {long_b};
    strcpy(out, "unchanged");
    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "multipart/mixed", bnd_keys, long_vals, 1, out,
                      sizeof(out)), -1);
    ASSERT_STR_EQ(out, "");

    const char *trail_vals[] = {"abc "};
    strcpy(out, "unchanged");
    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "multipart/mixed", bnd_keys, trail_vals, 1, out,
                      sizeof(out)), -1);
    ASSERT_STR_EQ(out, "");

    const char *ew_keys[] = {"filename"};
    const char *ew_vals[] = {"=?utf-8?q?=0D=0ABcc:_hidden?="};
    strcpy(out, "unchanged");
    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "text/plain", ew_keys, ew_vals, 1, out, sizeof(out)),
                  -1);
    ASSERT_STR_EQ(out, "");

    const char *ew_hidden[] = {"=?=?utf-8?q?=0D=0ABcc:_hidden?="};
    strcpy(out, "unchanged");
    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "text/plain", ew_keys, ew_hidden, 1, out, sizeof(out)),
                  -1);
    ASSERT_STR_EQ(out, "");

    const char *ew_ok[] = {"=?utf-8?q?foo?="};
    ASSERT_INT_EQ(neverc_mime_format_media_type(
                      "text/plain", ew_keys, ew_ok, 1, out, sizeof(out)),
                  (int)strlen("text/plain; filename=\"=?utf-8?q?foo?=\""));
    ASSERT_STR_EQ(out, "text/plain; filename=\"=?utf-8?q?foo?=\"");
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
    ASSERT_INT_EQ(neverc_mime_qp_decode("a=ZZ", 4, out, sizeof(out), &out_len), -1);
    ASSERT_INT_EQ(neverc_mime_qp_decode("=A", 2, out, sizeof(out), &out_len), -1);
    ASSERT_INT_EQ(neverc_mime_qp_decode("hello=", 6, out, sizeof(out), &out_len), 0);
    ASSERT_INT_EQ((int)out_len, 5);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "hello");
    ASSERT_INT_EQ(neverc_mime_qp_decode("hello=\n", 7, out, sizeof(out), &out_len), 0);
    ASSERT_INT_EQ((int)out_len, 5);

    ASSERT_INT_EQ(neverc_mime_qp_decode("Hello= \r\nWorld", 14, out,
                                        sizeof(out), &out_len), 0);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "HelloWorld");

    ASSERT_INT_EQ(neverc_mime_qp_decode("line1  \r\nline2", 14, out,
                                        sizeof(out), &out_len), 0);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "line1\r\nline2");

    ASSERT_INT_EQ(neverc_mime_qp_decode("hello=20  \n", 11, out,
                                        sizeof(out), &out_len), 0);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "hello \n");

    out_len = 99;
    ASSERT_INT_EQ(neverc_mime_qp_decode("= \r\n", 4, out, 0, &out_len), 0);
    ASSERT_INT_EQ((int)out_len, 0);

    out_len = 99;
    ASSERT_INT_EQ(neverc_mime_qp_decode("ab=\r cd", 7, out, sizeof(out),
                                        &out_len), -1);
    ASSERT_INT_EQ((int)out_len, 0);

    ASSERT_INT_EQ(neverc_mime_qp_decode("hello=\r", 7, out, sizeof(out),
                                        &out_len), 0);
    ASSERT_INT_EQ((int)out_len, 5);

    ASSERT_INT_EQ(neverc_mime_qp_decode("hello  \rworld", 13, out,
                                        sizeof(out), &out_len), 0);
    ASSERT_INT_EQ((int)out_len, 13);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "hello  \rworld");

    out_len = 99;
    ASSERT_INT_EQ(neverc_mime_qp_decode("foo\x00" "bar", 7, out, sizeof(out),
                                        &out_len), -1);
}

static void test_qp_encode(void) {
    printf("[qp_encode]\n");
    char out[256];
    size_t out_len;

    neverc_mime_qp_encode("Hello World", 11, out, sizeof(out), &out_len);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "Hello World");

    neverc_mime_qp_encode("hello \nworld", 12, out, sizeof(out), &out_len);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "hello=20=0Aworld");
    neverc_mime_qp_encode("hello\r\nworld", 12, out, sizeof(out), &out_len);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "hello\r\nworld");

    neverc_mime_qp_encode("\x80\xFF", 2, out, sizeof(out), &out_len);
    out[out_len] = '\0';
    ASSERT_STR_EQ(out, "=80=FF");

    out_len = 99;
    ASSERT_INT_EQ(neverc_mime_qp_encode("\x80", 1, out, 2, &out_len), -1);
    ASSERT_INT_EQ((int)out_len, 0);
    ASSERT_INT_EQ(neverc_mime_qp_encode(NULL, 1, out,
                                        sizeof(out), &out_len), -1);
    ASSERT_INT_EQ(neverc_mime_qp_encode("x", 1, NULL, 1, &out_len), -1);

    char longsrc[80];
    memset(longsrc, 'A', sizeof(longsrc));
    char wrap[256];
    ASSERT_INT_EQ(neverc_mime_qp_encode(longsrc, sizeof(longsrc), wrap,
                                        sizeof(wrap), &out_len), 0);
    wrap[out_len] = '\0';
    ASSERT_TRUE(strstr(wrap, "=\r\n") != NULL);
    ASSERT_TRUE(out_len > sizeof(longsrc));

    char decoded[128];
    size_t dlen = 0;
    ASSERT_INT_EQ(neverc_mime_qp_decode(wrap, out_len, decoded,
                                        sizeof(decoded), &dlen), 0);
    ASSERT_INT_EQ((int)dlen, (int)sizeof(longsrc));
    ASSERT_TRUE(memcmp(decoded, longsrc, sizeof(longsrc)) == 0);

    {
        char newlines[26];
        memset(newlines, '\n', sizeof(newlines));
        char qp[256];
        size_t qlen = 0;
        ASSERT_INT_EQ(neverc_mime_qp_encode(newlines, sizeof(newlines), qp,
                                            sizeof(qp), &qlen), 0);
        qp[qlen] = '\0';
        ASSERT_TRUE(strstr(qp, "=\r\n") != NULL);
        size_t line = 0;
        int lines_ok = 1;
        for (size_t i = 0; i < qlen; i++) {
            if (i + 1 < qlen && qp[i] == '\r' && qp[i + 1] == '\n') {
                if (line > 76) lines_ok = 0;
                line = 0;
                i++;
                continue;
            }
            line++;
        }
        if (line > 76) lines_ok = 0;
        ASSERT_INT_EQ(lines_ok, 1);
    }
}

static void test_rfc2047_decode_header(void) {
    printf("[rfc2047_decode_header]\n");
    char out[256];
    size_t n = 0;
    const char *qword = "=?utf-8?q?hello_world?=";
    const char *bword = "=?utf-8?b?aGVsbG8=?=";
    const char *adj = "=?utf-8?q?foo?= =?utf-8?q?bar?=";
    const char *mixed = "=?utf-8?q?foo?= hello =?utf-8?q?bar?=";
    const char *utf8q = "=?utf-8?q?caf=C3=A9?=";
    const char *latin1 = "=?iso-8859-1?q?caf=E9?=";
    const char *plain = "Subject: Hello";
    const char *inject = "=?utf-8?q?=0D=0ABcc:_hidden?=";
    const char *raw_crlf = "Hello\r\nBcc: hidden";
    const char *bad_cs = "=?koi8-r?q?foo?=";
    const char *folded = "=?utf-8?q?foo?=\r\n =?utf-8?q?bar?=";

    ASSERT_INT_EQ(neverc_mime_decode_header(
                      qword, strlen(qword), out, sizeof(out), &n), 0);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "hello world");

    ASSERT_INT_EQ(neverc_mime_decode_header(
                      bword, strlen(bword), out, sizeof(out), &n), 0);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "hello");

    ASSERT_INT_EQ(neverc_mime_decode_header(
                      adj, strlen(adj), out, sizeof(out), &n), 0);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "foobar");

    ASSERT_INT_EQ(neverc_mime_decode_header(
                      mixed, strlen(mixed), out, sizeof(out), &n), 0);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "foo hello bar");

    ASSERT_INT_EQ(neverc_mime_decode_header(
                      utf8q, strlen(utf8q), out, sizeof(out), &n), 0);
    ASSERT_INT_EQ((int)n, 5);
    ASSERT_TRUE(memcmp(out, "caf\xC3\xA9", 5) == 0);

    ASSERT_INT_EQ(neverc_mime_decode_header(
                      latin1, strlen(latin1), out, sizeof(out), &n), 0);
    ASSERT_INT_EQ((int)n, 5);
    ASSERT_TRUE(memcmp(out, "caf\xC3\xA9", 5) == 0);

    ASSERT_INT_EQ(neverc_mime_decode_header(
                      plain, strlen(plain), out, sizeof(out), &n), 0);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "Subject: Hello");

    n = 99;
    ASSERT_INT_EQ(neverc_mime_decode_header(
                      inject, strlen(inject), out, sizeof(out), &n), -1);
    ASSERT_INT_EQ((int)n, 0);

    const char *hidden = "=?=?utf-8?q?=0D=0ABcc:_hidden?=";
    n = 99;
    ASSERT_INT_EQ(neverc_mime_decode_header(
                      hidden, strlen(hidden), out, sizeof(out), &n), -1);
    ASSERT_INT_EQ((int)n, 0);

    n = 99;
    ASSERT_INT_EQ(neverc_mime_decode_header(
                      raw_crlf, strlen(raw_crlf), out, sizeof(out), &n), -1);
    ASSERT_INT_EQ((int)n, 0);

    n = 99;
    ASSERT_INT_EQ(neverc_mime_decode_header(
                      bad_cs, strlen(bad_cs), out, sizeof(out), &n), -1);
    ASSERT_INT_EQ((int)n, 0);

    ASSERT_INT_EQ(neverc_mime_decode_header(
                      folded, strlen(folded), out, sizeof(out), &n), 0);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "foobar");

    /* ParseMediaType leaves encoded-words in parameter values. */
    char mt[64];
    char *keys[2], *vals[2];
    int nparams;
    ASSERT_INT_EQ(neverc_mime_parse_media_type(
                      "text/plain; filename=\"=?utf-8?q?foo?=\"",
                      mt, sizeof(mt), keys, vals, 2, &nparams), 0);
    ASSERT_INT_EQ(nparams, 1);
    ASSERT_STR_EQ(vals[0], "=?utf-8?q?foo?=");
    ASSERT_INT_EQ(neverc_mime_decode_header(
                      vals[0], strlen(vals[0]), out, sizeof(out), &n), 0);
    out[n] = '\0';
    ASSERT_STR_EQ(out, "foo");
    free_params(keys, vals, nparams);

    /* A well-formed word longer than the old 128-byte stack buffer that
     * decodes to CR/LF must not be copied through as a literal. */
    {
        char long_q[256];
        size_t lp = 0;
        memcpy(long_q, "=?utf-8?q?=0D=0A", 16);
        lp = 16;
        memset(long_q + lp, 'A', 127);
        lp += 127;
        memcpy(long_q + lp, "?=", 2);
        lp += 2;
        long_q[lp] = '\0';
        n = 99;
        ASSERT_INT_EQ(neverc_mime_decode_header(
                          long_q, lp, out, sizeof(out), &n), -1);
        ASSERT_INT_EQ((int)n, 0);
    }

    /* Same length, no CTL: must decode, not fail-open-as-literal. */
    {
        char long_ok[256];
        size_t lp = 0;
        memcpy(long_ok, "=?utf-8?q?", 10);
        lp = 10;
        memset(long_ok + lp, 'A', 130);
        lp += 130;
        memcpy(long_ok + lp, "?=", 2);
        lp += 2;
        char big[256];
        n = 0;
        ASSERT_INT_EQ(neverc_mime_decode_header(
                          long_ok, lp, big, sizeof(big), &n), 0);
        ASSERT_INT_EQ((int)n, 130);
        {
            char expect[130];
            memset(expect, 'A', 130);
            ASSERT_TRUE(memcmp(big, expect, 130) == 0);
        }
    }

    /* utf-8 encoded-word must not accept overlong LF or a surrogate half. */
    const char *overlong_lf = "=?utf-8?q?=C0=8D?=";
    const char *surr_word = "=?utf-8?q?=ED=A0=80?=";
    n = 99;
    ASSERT_INT_EQ(neverc_mime_decode_header(
                      overlong_lf, strlen(overlong_lf), out, sizeof(out),
                      &n), -1);
    ASSERT_INT_EQ((int)n, 0);
    n = 99;
    ASSERT_INT_EQ(neverc_mime_decode_header(
                      surr_word, strlen(surr_word), out, sizeof(out), &n),
                  -1);
    ASSERT_INT_EQ((int)n, 0);

    const char *b64_crlf = "=?utf-8?b?DQo=?=";
    n = 99;
    ASSERT_INT_EQ(neverc_mime_decode_header(
                      b64_crlf, strlen(b64_crlf), out, sizeof(out), &n), -1);
    ASSERT_INT_EQ((int)n, 0);

    const char *u2028 = "=?utf-8?q?=E2=80=A8?=";
    const char *u2029 = "=?utf-8?q?=E2=80=A9?=";
    const char *nel = "=?iso-8859-1?q?=85?=";
    n = 99;
    ASSERT_INT_EQ(neverc_mime_decode_header(
                      u2028, strlen(u2028), out, sizeof(out), &n), -1);
    n = 99;
    ASSERT_INT_EQ(neverc_mime_decode_header(
                      u2029, strlen(u2029), out, sizeof(out), &n), -1);
    n = 99;
    ASSERT_INT_EQ(neverc_mime_decode_header(
                      nel, strlen(nel), out, sizeof(out), &n), -1);
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
    test_rfc2047_decode_header();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
