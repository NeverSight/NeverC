/*
 * NeverC encoding/csv tests.
 */
#include "neverc/std/encoding/csv.h"
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

#define ASSERT_STR_EQ(expr, expected) do { \
    const char *_v = (expr); const char *_e = (expected); tests_run++; \
    if (_v && _e && strcmp(_v, _e) == 0) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL line %d: %s = \"%s\", expected \"%s\"\n", __LINE__, #expr, _v?_v:"(null)", _e); } \
} while(0)

static void test_simple_line(void) {
    printf("[simple_line]\n");
    const char *line = "a,b,c";
    const char *fields[10];
    char work[256];
    int n = neverc_csv_read_line(line, strlen(line), fields, 10, work, sizeof(work), NULL);
    ASSERT_INT_EQ(n, 3);
    ASSERT_STR_EQ(fields[0], "a");
    ASSERT_STR_EQ(fields[1], "b");
    ASSERT_STR_EQ(fields[2], "c");
}

static void test_quoted_fields(void) {
    printf("[quoted_fields]\n");
    const char *line = "\"hello, world\",\"he said \"\"hi\"\"\",plain";
    const char *fields[10];
    char work[256];
    int n = neverc_csv_read_line(line, strlen(line), fields, 10, work, sizeof(work), NULL);
    ASSERT_INT_EQ(n, 3);
    ASSERT_STR_EQ(fields[0], "hello, world");
    ASSERT_STR_EQ(fields[1], "he said \"hi\"");
    ASSERT_STR_EQ(fields[2], "plain");
}

static void test_empty_fields(void) {
    printf("[empty_fields]\n");
    const char *line = "a,,c,";
    const char *fields[10];
    char work[256];
    int n = neverc_csv_read_line(line, strlen(line), fields, 10, work, sizeof(work), NULL);
    ASSERT_INT_EQ(n, 4);
    ASSERT_STR_EQ(fields[0], "a");
    ASSERT_STR_EQ(fields[1], "");
    ASSERT_STR_EQ(fields[2], "c");
    ASSERT_STR_EQ(fields[3], "");
}

static void test_custom_delimiter(void) {
    printf("[custom_delimiter]\n");
    const char *line = "a\tb\tc";
    const char *fields[10];
    char work[256];
    neverc_csv_reader_opts_t opts = { .delimiter = '\t' };
    int n = neverc_csv_read_line(line, strlen(line), fields, 10, work, sizeof(work), &opts);
    ASSERT_INT_EQ(n, 3);
    ASSERT_STR_EQ(fields[0], "a");
    ASSERT_STR_EQ(fields[1], "b");
    ASSERT_STR_EQ(fields[2], "c");
}

static void test_with_newline(void) {
    printf("[with_newline]\n");
    const char *line = "a,b,c\n";
    const char *fields[10];
    char work[256];
    int n = neverc_csv_read_line(line, strlen(line), fields, 10, work, sizeof(work), NULL);
    ASSERT_INT_EQ(n, 3);
    ASSERT_STR_EQ(fields[0], "a");
    ASSERT_STR_EQ(fields[2], "c");

    /* CRLF */
    const char *line2 = "x,y\r\n";
    n = neverc_csv_read_line(line2, strlen(line2), fields, 10, work, sizeof(work), NULL);
    ASSERT_INT_EQ(n, 2);
    ASSERT_STR_EQ(fields[1], "y");

    /* Extra CR before CRLF is field content (Go encoding/csv). */
    static const char cr_content[] = {'a', '\r', '\r', '\n'};
    n = neverc_csv_read_line(cr_content, sizeof(cr_content), fields, 10,
                             work, sizeof(work), NULL);
    ASSERT_INT_EQ(n, 1);
    ASSERT_STR_EQ(fields[0], "a\r");

    static const char eof_cr[] = {'a', '\r'};
    n = neverc_csv_read_line(eof_cr, sizeof(eof_cr), fields, 10,
                             work, sizeof(work), NULL);
    ASSERT_INT_EQ(n, 1);
    ASSERT_STR_EQ(fields[0], "a");
}

static void test_write_simple(void) {
    printf("[write_simple]\n");
    const char *fields[] = {"a", "b", "c"};
    char dst[256];
    int n = neverc_csv_write_record(fields, 3, dst, sizeof(dst), NULL);
    ASSERT_INT_EQ(n > 0, 1);
    dst[n] = '\0';
    ASSERT_STR_EQ(dst, "a,b,c\n");
}

static void test_write_quoting(void) {
    printf("[write_quoting]\n");
    const char *fields[] = {"hello, world", "he said \"hi\"", "plain"};
    char dst[256];
    int n = neverc_csv_write_record(fields, 3, dst, sizeof(dst), NULL);
    ASSERT_INT_EQ(n > 0, 1);
    dst[n] = '\0';
    ASSERT_STR_EQ(dst, "\"hello, world\",\"he said \"\"hi\"\"\",plain\n");

    const char *nl_fields[] = {"a\nb", "c\r"};
    n = neverc_csv_write_record(nl_fields, 2, dst, sizeof(dst), NULL);
    ASSERT_INT_EQ(n > 0, 1);
    dst[n] = '\0';
    ASSERT_STR_EQ(dst, "\"a\nb\",\"c\r\"\n");

    /* Go encoding/csv quotes leading unicode.IsSpace and the Postgres `\.`. */
    const char *lead_fields[] = {" a", "\tb", "\\."};
    n = neverc_csv_write_record(lead_fields, 3, dst, sizeof(dst), NULL);
    ASSERT_INT_EQ(n > 0, 1);
    dst[n] = '\0';
    ASSERT_STR_EQ(dst, "\" a\",\"\tb\",\"\\.\"\n");

    const char *nbsp_fields[] = {"\xc2\xa0x"};
    n = neverc_csv_write_record(nbsp_fields, 1, dst, sizeof(dst), NULL);
    ASSERT_INT_EQ(n > 0, 1);
    dst[n] = '\0';
    ASSERT_STR_EQ(dst, "\"\xc2\xa0x\"\n");

    /* Formula prefixes must be quoted; a leading minus is a number, not a formula. */
    const char *formula_fields[] = {"=1+1", "+cmd", "@SUM(A1)", "-1"};
    n = neverc_csv_write_record(formula_fields, 4, dst, sizeof(dst), NULL);
    ASSERT_INT_EQ(n > 0, 1);
    dst[n] = '\0';
    ASSERT_STR_EQ(dst, "\"=1+1\",\"+cmd\",\"@SUM(A1)\",-1\n");

    neverc_csv_reader_opts_t trim_opts = {
        .delimiter = ',', .trim_leading_space = 1
    };
    const char *round_fields[10];
    char work[256];
    int nf = neverc_csv_read_line(
        "\" a\",b\n", 7U, round_fields, 10, work, sizeof(work), &trim_opts);
    ASSERT_INT_EQ(nf, 2);
    ASSERT_STR_EQ(round_fields[0], " a");
    ASSERT_STR_EQ(round_fields[1], "b");
}

static void test_write_crlf(void) {
    printf("[write_crlf]\n");
    const char *fields[] = {"a", "b"};
    char dst[256];
    neverc_csv_writer_opts_t opts = { .delimiter = ',', .use_crlf = 1 };
    int n = neverc_csv_write_record(fields, 2, dst, sizeof(dst), &opts);
    ASSERT_INT_EQ(n > 0, 1);
    dst[n] = '\0';
    ASSERT_STR_EQ(dst, "a,b\r\n");
}

static void test_roundtrip(void) {
    printf("[roundtrip]\n");
    const char *fields[] = {"name", "age", "city"};
    char csv_buf[256];
    int n = neverc_csv_write_record(fields, 3, csv_buf, sizeof(csv_buf), NULL);
    ASSERT_INT_EQ(n > 0, 1);

    const char *read_fields[10];
    char work[256];
    int nf = neverc_csv_read_line(csv_buf, (size_t)n, read_fields, 10, work, sizeof(work), NULL);
    ASSERT_INT_EQ(nf, 3);
    ASSERT_STR_EQ(read_fields[0], "name");
    ASSERT_STR_EQ(read_fields[1], "age");
    ASSERT_STR_EQ(read_fields[2], "city");
}

static void test_trim_space(void) {
    printf("[trim_space]\n");
    const char *line = "  a ,  b  , c  ";
    const char *fields[10];
    char work[256];
    neverc_csv_reader_opts_t opts = { .delimiter = ',', .trim_leading_space = 1 };
    int n = neverc_csv_read_line(line, strlen(line), fields, 10, work, sizeof(work), &opts);
    ASSERT_INT_EQ(n, 3);
    ASSERT_STR_EQ(fields[0], "a ");
    ASSERT_STR_EQ(fields[1], "b  ");
    ASSERT_STR_EQ(fields[2], "c  ");
}

static void test_read_all(void) {
    printf("[read_all]\n");
    static const char data[] = "a,b\n\"multi\nline\",d\n";
    const char *row0[NEVERC_CSV_MAX_FIELDS];
    const char *row1[NEVERC_CSV_MAX_FIELDS];
    const char **records[] = {row0, row1};
    int field_counts[2] = {0, 0};
    char work[256];
    int n = neverc_csv_read_all(
        data, sizeof(data) - 1U, records, field_counts, 2,
        work, sizeof(work), NULL);
    ASSERT_INT_EQ(n, 2);
    ASSERT_INT_EQ(field_counts[0], 2);
    ASSERT_INT_EQ(field_counts[1], 2);
    ASSERT_STR_EQ(records[0][0], "a");
    ASSERT_STR_EQ(records[0][1], "b");
    ASSERT_STR_EQ(records[1][0], "multi\nline");
    ASSERT_STR_EQ(records[1][1], "d");

    char encoded[256];
    n = neverc_csv_write_all(
        records, field_counts, 2, encoded, sizeof(encoded), NULL);
    ASSERT_INT_EQ(n, (int)(sizeof(data) - 1U));
    if (n > 0) {
        encoded[n] = '\0';
        ASSERT_STR_EQ(encoded, data);
    }

    const char *limited_row[NEVERC_CSV_MAX_FIELDS];
    const char **limited_records[] = {limited_row};
    int limited_count[1] = {0};
    ASSERT_INT_EQ(neverc_csv_read_all(
                      "a\nb\n", 4U, limited_records, limited_count, 1,
                      work, sizeof(work), NULL),
                  -1);

    neverc_csv_reader_opts_t comment_opts = {.comment = '#'};
    ASSERT_INT_EQ(neverc_csv_read_all(
                      "#,\"ignored\nok,value\n", 20U,
                      limited_records, limited_count, 1,
                      work, sizeof(work), &comment_opts),
                  1);
    ASSERT_STR_EQ(limited_records[0][0], "ok");
    ASSERT_STR_EQ(limited_records[0][1], "value");

    neverc_csv_reader_opts_t lazy_opts = {.lazy_quotes = 1};
    ASSERT_INT_EQ(neverc_csv_read_all(
                      "\"a\"b\nc\",d\n", 10U,
                      limited_records, limited_count, 1,
                      work, sizeof(work), &lazy_opts),
                  1);
    ASSERT_STR_EQ(limited_records[0][0], "a\"b\nc");
    ASSERT_STR_EQ(limited_records[0][1], "d");

    /* Lone CR is a field byte (Go encoding/csv). Only LF / CRLF end records. */
    {
        const char *cr_row[NEVERC_CSV_MAX_FIELDS];
        const char **cr_records[] = {cr_row};
        int cr_count[1] = {0};
        static const char lone_cr[] = {'a', '\r', 'b'};
        ASSERT_INT_EQ(neverc_csv_read_all(
                          lone_cr, sizeof(lone_cr),
                          cr_records, cr_count, 1,
                          work, sizeof(work), NULL),
                      1);
        ASSERT_INT_EQ(cr_count[0], 1);
        ASSERT_STR_EQ(cr_records[0][0], "a\rb");
    }
    {
        const char *crlf0[NEVERC_CSV_MAX_FIELDS];
        const char *crlf1[NEVERC_CSV_MAX_FIELDS];
        const char **crlf_records[] = {crlf0, crlf1};
        int crlf_counts[2] = {0, 0};
        ASSERT_INT_EQ(neverc_csv_read_all(
                          "a\r\nb\n", 5U,
                          crlf_records, crlf_counts, 2,
                          work, sizeof(work), NULL),
                      2);
        ASSERT_STR_EQ(crlf_records[0][0], "a");
        ASSERT_STR_EQ(crlf_records[1][0], "b");
    }
    {
        const char *cmt_row[NEVERC_CSV_MAX_FIELDS];
        const char **cmt_records[] = {cmt_row};
        int cmt_count[1] = {0};
        neverc_csv_reader_opts_t cr_comment = {.comment = '#'};
        static const char comment_cr[] = {
            '#', 'x', '\r', 'k', 'e', 'p', 't'
        };
        ASSERT_INT_EQ(neverc_csv_read_all(
                          comment_cr, sizeof(comment_cr),
                          cmt_records, cmt_count, 1,
                          work, sizeof(work), &cr_comment),
                      0);
        ASSERT_INT_EQ(neverc_csv_read_all(
                          "#comment\r\nnext\n", 15U,
                          cmt_records, cmt_count, 1,
                          work, sizeof(work), &cr_comment),
                      1);
        ASSERT_STR_EQ(cmt_records[0][0], "next");
    }
    {
        const char *eof_row[NEVERC_CSV_MAX_FIELDS];
        const char **eof_records[] = {eof_row};
        int eof_count[1] = {0};
        static const char eof_cr[] = {'a', '\r'};
        ASSERT_INT_EQ(neverc_csv_read_all(
                          eof_cr, sizeof(eof_cr),
                          eof_records, eof_count, 1,
                          work, sizeof(work), NULL),
                      1);
        ASSERT_STR_EQ(eof_records[0][0], "a");
    }
    {
        const char *dbl0[NEVERC_CSV_MAX_FIELDS];
        const char *dbl1[NEVERC_CSV_MAX_FIELDS];
        const char **dbl_records[] = {dbl0, dbl1};
        int dbl_counts[2] = {0, 0};
        static const char cr_before_crlf[] = {'a', '\r', '\r', '\n', 'b'};
        ASSERT_INT_EQ(neverc_csv_read_all(
                          cr_before_crlf, sizeof(cr_before_crlf),
                          dbl_records, dbl_counts, 2,
                          work, sizeof(work), NULL),
                      2);
        ASSERT_STR_EQ(dbl_records[0][0], "a\r");
        ASSERT_STR_EQ(dbl_records[1][0], "b");
    }
    {
        const char *qrow[NEVERC_CSV_MAX_FIELDS];
        const char **qrecords[] = {qrow};
        int qcount[1] = {0};
        ASSERT_INT_EQ(neverc_csv_read_all(
                          "\"a\rb\"\n", 6U,
                          qrecords, qcount, 1,
                          work, sizeof(work), NULL),
                      1);
        ASSERT_STR_EQ(qrecords[0][0], "a\rb");
    }
    /* RFC 4180 quoted fields may contain CRLF; it is not a record break. */
    {
        const char *crlf_qrow[NEVERC_CSV_MAX_FIELDS];
        const char **crlf_qrecords[] = {crlf_qrow};
        int crlf_qcount[1] = {0};
        ASSERT_INT_EQ(neverc_csv_read_all(
                          "\"a\r\nb\",c\n", 9U,
                          crlf_qrecords, crlf_qcount, 1,
                          work, sizeof(work), NULL),
                      1);
        ASSERT_INT_EQ(crlf_qcount[0], 2);
        ASSERT_STR_EQ(crlf_qrecords[0][0], "a\r\nb");
        ASSERT_STR_EQ(crlf_qrecords[0][1], "c");
    }
    /* Quoted newline at EOF without a trailing record terminator. */
    {
        const char *eof_qrow[NEVERC_CSV_MAX_FIELDS];
        const char **eof_qrecords[] = {eof_qrow};
        int eof_qcount[1] = {0};
        static const char quoted_nl_eof[] = {'"', 'a', '\n', 'b', '"'};
        ASSERT_INT_EQ(neverc_csv_read_all(
                          quoted_nl_eof, sizeof(quoted_nl_eof),
                          eof_qrecords, eof_qcount, 1,
                          work, sizeof(work), NULL),
                      1);
        ASSERT_STR_EQ(eof_qrecords[0][0], "a\nb");
    }
}

static void test_invalid_inputs(void) {
    printf("[invalid_inputs]\n");
    const char *fields[2];
    char work[32];
    ASSERT_INT_EQ(neverc_csv_read_line(
                      "a,b,c", 5U, fields, 2, work, sizeof(work), NULL),
                  -1);
    ASSERT_INT_EQ(neverc_csv_read_line(
                      "a,b,", 4U, fields, 2, work, sizeof(work), NULL),
                  -1);
    ASSERT_INT_EQ(neverc_csv_read_line(
                      "\"unterminated", 13U, fields, 2,
                      work, sizeof(work), NULL),
                  -1);
    ASSERT_INT_EQ(neverc_csv_read_line(
                      "a\"b,c", 5U, fields, 2, work, sizeof(work), NULL),
                  -1);
    ASSERT_INT_EQ(neverc_csv_read_line(
                      "\"foo\"bar", 8U, fields, 2, work, sizeof(work), NULL),
                  -1);
    ASSERT_INT_EQ(neverc_csv_read_line(
                      "\"a\" ,b", 6U, fields, 2, work, sizeof(work), NULL),
                  -1);
    {
        static const char embedded_nul[] = {'a', '\0', 'b', ',', 'c'};
        ASSERT_INT_EQ(neverc_csv_read_line(
                          embedded_nul, sizeof(embedded_nul), fields, 2,
                          work, sizeof(work), NULL),
                      -1);
        const char *row[NEVERC_CSV_MAX_FIELDS];
        const char **records[] = {row};
        int field_count = 0;
        ASSERT_INT_EQ(neverc_csv_read_all(
                          embedded_nul, sizeof(embedded_nul),
                          records, &field_count, 1,
                          work, sizeof(work), NULL),
                      -1);
    }

    neverc_csv_reader_opts_t lazy = {.lazy_quotes = 1};
    ASSERT_INT_EQ(neverc_csv_read_line(
                      "\"unterminated", 13U, fields, 2,
                      work, sizeof(work), &lazy),
                  1);
    ASSERT_STR_EQ(fields[0], "unterminated");

    const char *null_field[] = {"a", NULL};
    char dst[32];
    ASSERT_INT_EQ(neverc_csv_write_record(
                      null_field, 2, dst, sizeof(dst), NULL),
                  -1);

    {
        size_t n = (size_t)NEVERC_CSV_MAX_FIELD_LEN + 1u;
        char *line = (char *)malloc(n);
        char *work = (char *)malloc(n + 8u);
        const char *big_fields[2];
        ASSERT_INT_EQ(line != NULL && work != NULL, 1);
        if (line && work) {
            memset(line, 'x', n);
            ASSERT_INT_EQ(neverc_csv_read_line(
                              line, n, big_fields, 2, work, n + 8u, NULL),
                          -1);
        }
        free(line);
        free(work);
    }
}

int main(void) {
    printf("=== NeverC encoding/csv Tests ===\n");
    test_simple_line();
    test_quoted_fields();
    test_empty_fields();
    test_custom_delimiter();
    test_with_newline();
    test_write_simple();
    test_write_quoting();
    test_write_crlf();
    test_roundtrip();
    test_trim_space();
    test_read_all();
    test_invalid_inputs();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
