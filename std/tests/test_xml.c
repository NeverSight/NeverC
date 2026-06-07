#include "neverc/encoding/xml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}
static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else if (!got && !expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got?got:"(null)", expected?expected:"(null)"); }
}
static void check_bool(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void test_tokenizer(void) {
    printf("[tokenizer]\n");
    const char *xml = "<root><item id=\"1\">hello</item></root>";
    neverc_xml_decoder_t d;
    neverc_xml_decoder_init(&d, xml, strlen(xml));

    neverc_xml_token_t tok;

    neverc_xml_decode_token(&d, &tok);
    check_int("tok1 type", tok.type, NEVERC_XML_START_ELEMENT);
    check_str("tok1 name", tok.name, "root");
    neverc_xml_token_free(&tok);

    neverc_xml_decode_token(&d, &tok);
    check_int("tok2 type", tok.type, NEVERC_XML_START_ELEMENT);
    check_str("tok2 name", tok.name, "item");
    check_int("tok2 nattrs", tok.nattrs, 1);
    check_str("tok2 attr name", tok.attrs[0].name, "id");
    check_str("tok2 attr val", tok.attrs[0].value, "1");
    neverc_xml_token_free(&tok);

    neverc_xml_decode_token(&d, &tok);
    check_int("tok3 type", tok.type, NEVERC_XML_CHAR_DATA);
    check_str("tok3 data", tok.data, "hello");
    neverc_xml_token_free(&tok);

    neverc_xml_decode_token(&d, &tok);
    check_int("tok4 type", tok.type, NEVERC_XML_END_ELEMENT);
    check_str("tok4 name", tok.name, "item");
    neverc_xml_token_free(&tok);
}

static void test_dom(void) {
    printf("[dom]\n");
    const char *xml =
        "<config>"
        "  <server host=\"localhost\" port=\"8080\">main</server>"
        "  <db>postgres</db>"
        "</config>";

    neverc_xml_node_t *root = neverc_xml_parse(xml, strlen(xml));
    check_bool("root ok", root != NULL, 1);

    neverc_xml_node_t *config = neverc_xml_node_child(root, "config");
    check_bool("config found", config != NULL, 1);

    if (config) {
        neverc_xml_node_t *server = neverc_xml_node_child(config, "server");
        check_bool("server found", server != NULL, 1);
        if (server) {
            check_str("host attr", neverc_xml_node_attr(server, "host"), "localhost");
            check_str("port attr", neverc_xml_node_attr(server, "port"), "8080");
            check_str("server text", server->text, "main");
        }

        neverc_xml_node_t *db = neverc_xml_node_child(config, "db");
        check_bool("db found", db != NULL, 1);
        if (db) check_str("db text", db->text, "postgres");
    }

    neverc_xml_node_free(root);
}

static void test_comment(void) {
    printf("[comment]\n");
    const char *xml = "<!-- comment --><root>text</root>";
    neverc_xml_decoder_t d;
    neverc_xml_decoder_init(&d, xml, strlen(xml));

    neverc_xml_token_t tok;
    neverc_xml_decode_token(&d, &tok);
    check_int("comment type", tok.type, NEVERC_XML_COMMENT);
    check_str("comment data", tok.data, " comment ");
    neverc_xml_token_free(&tok);

    neverc_xml_decode_token(&d, &tok);
    check_int("after comment", tok.type, NEVERC_XML_START_ELEMENT);
    check_str("root tag", tok.name, "root");
    neverc_xml_token_free(&tok);
}

static void test_proc_inst(void) {
    printf("[proc inst]\n");
    const char *xml = "<?xml version=\"1.0\"?><root/>";
    neverc_xml_decoder_t d;
    neverc_xml_decoder_init(&d, xml, strlen(xml));

    neverc_xml_token_t tok;
    neverc_xml_decode_token(&d, &tok);
    check_int("pi type", tok.type, NEVERC_XML_PROC_INST);
    neverc_xml_token_free(&tok);
}

static void test_escape(void) {
    printf("[escape]\n");
    size_t outlen;
    char *r = neverc_xml_escape("a < b & c > d", &outlen);
    check_str("escape", r, "a &lt; b &amp; c &gt; d");
    free(r);
}

static void test_nested(void) {
    printf("[nested]\n");
    const char *xml =
        "<a><b><c>deep</c></b><d>shallow</d></a>";
    neverc_xml_node_t *root = neverc_xml_parse(xml, strlen(xml));
    neverc_xml_node_t *a = neverc_xml_node_child(root, "a");
    check_bool("a found", a != NULL, 1);
    if (a) {
        neverc_xml_node_t *b = neverc_xml_node_child(a, "b");
        check_bool("b found", b != NULL, 1);
        if (b) {
            neverc_xml_node_t *c = neverc_xml_node_child(b, "c");
            check_bool("c found", c != NULL, 1);
            if (c) check_str("c text", c->text, "deep");
        }
        neverc_xml_node_t *d = neverc_xml_node_child(a, "d");
        check_bool("d found", d != NULL, 1);
        if (d) check_str("d text", d->text, "shallow");
    }
    neverc_xml_node_free(root);
}

int main(void) {
    printf("=== NeverC Encoding/XML Module Tests ===\n\n");
    test_tokenizer();
    test_dom();
    test_comment();
    test_proc_inst();
    test_escape();
    test_nested();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
