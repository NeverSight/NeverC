#include "neverc/std/encoding/xml.h"
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
    outlen = 99;
    check_bool("escape rejects invalid XML characters",
               neverc_xml_escape("\x01", &outlen) == NULL, 1);
    check_int("invalid escape clears length", (int)outlen, 0);

    const char *escaped = "&lt;tag&gt; &amp; &#x1f600;";
    r = neverc_xml_unescape(escaped, strlen(escaped), &outlen);
    check_str("unescape", r, "<tag> & \xf0\x9f\x98\x80");
    check_int("unescape length", (int)outlen, 12);
    free(r);
    outlen = 99;
    check_bool("unescape rejects unknown entity",
               neverc_xml_unescape("&unknown;", 9, &outlen) == NULL, 1);
    check_int("invalid unescape clears length", (int)outlen, 0);
}

static void test_entities_cdata_and_well_formedness(void) {
    printf("[entities/cdata/well-formedness]\n");
    const char *xml =
        "<root value=\"a &amp; &#65;\">"
        "x &lt; y<![CDATA[<&]]>z"
        "</root>";
    neverc_xml_decoder_t decoder;
    neverc_xml_token_t token;
    neverc_xml_decoder_init(&decoder, xml, strlen(xml));
    check_int("entity start token",
              neverc_xml_decode_token(&decoder, &token), 1);
    check_bool("entity attribute present",
               token.nattrs == 1 && token.attrs != NULL, 1);
    if (token.nattrs == 1 && token.attrs)
        check_str("decoded attribute", token.attrs[0].value, "a & A");
    neverc_xml_token_free(&token);
    check_int("entity text token",
              neverc_xml_decode_token(&decoder, &token), 1);
    check_str("decoded text entity", token.data, "x < y");
    neverc_xml_token_free(&token);
    check_int("CDATA token",
              neverc_xml_decode_token(&decoder, &token), 1);
    check_int("CDATA is character data",
              token.type, NEVERC_XML_CHAR_DATA);
    check_str("CDATA preserves markup", token.data, "<&");
    neverc_xml_token_free(&token);

    neverc_xml_node_t *tree = neverc_xml_parse(xml, strlen(xml));
    neverc_xml_node_t *root =
        tree ? neverc_xml_node_child(tree, "root") : NULL;
    check_bool("entity/CDATA DOM parses", root != NULL, 1);
    if (root) {
        check_str("entity/CDATA text concatenates", root->text, "x < y<&z");
        check_str("DOM attribute is decoded",
                  neverc_xml_node_attr(root, "value"), "a & A");
    }
    neverc_xml_node_free(tree);

    static const char unicode_names[] =
        "\xef\xbb\xbf<r\xc2\xb7x><\xcd\xbf/></r\xc2\xb7x>";
    tree = neverc_xml_parse(unicode_names, sizeof(unicode_names) - 1);
    check_bool("BOM and XML Name ranges parse", tree != NULL, 1);
    neverc_xml_node_free(tree);

    static const char *invalid_documents[] = {
        "<root>&unknown;</root>",
        "<root>&#xD800;</root>",
        "<root>&#X41;</root>",
        "<root>&amp</root>",
        "<root a=\"<\"/>",
        "<root a=\"1\" a=\"2\"/>",
        "<root a=\"1\"b=\"2\"/>",
        "<1root/>",
        "<\xc2\xb7root/>",
        "<\xcd\xberoot/>",
        "<\xf3\xb0\x80\x80/>",
        "<root><![CDATA[unterminated</root>",
        "<root>bad]]></root>",
        "<!DOCTYPE root><root/>",
        "<first/><second/>",
        "text<root/>",
        "<root/><!-- bad--comment -->"
    };
    for (size_t i = 0;
         i < sizeof(invalid_documents) / sizeof(invalid_documents[0]);
         i++) {
        check_bool("reject malformed XML",
                   neverc_xml_parse(
                       invalid_documents[i],
                       strlen(invalid_documents[i])) == NULL,
                   1);
    }
    static const char invalid_utf8[] = "<root>\xc0\x80</root>";
    check_bool("reject invalid UTF-8",
               neverc_xml_parse(
                   invalid_utf8, sizeof(invalid_utf8) - 1) == NULL,
               1);
    static const char bom_document[] = "\xef\xbb\xbf<root/>";
    tree = neverc_xml_parse(
        bom_document, sizeof(bom_document) - 1);
    check_bool("accept UTF-8 BOM", tree != NULL, 1);
    neverc_xml_node_free(tree);
    check_bool("reject empty document",
               neverc_xml_parse("", 0) == NULL, 1);
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

static void test_self_closing_and_errors(void) {
    printf("[self closing/errors]\n");
    const char *xml = "<root><empty/><sibling>ok</sibling></root>";
    neverc_xml_node_t *tree = neverc_xml_parse(xml, strlen(xml));
    neverc_xml_node_t *root = tree ? neverc_xml_node_child(tree, "root") : NULL;
    neverc_xml_node_t *empty = root ? neverc_xml_node_child(root, "empty") : NULL;
    neverc_xml_node_t *sibling = root ? neverc_xml_node_child(root, "sibling") : NULL;
    check_bool("self closing parse", tree != NULL, 1);
    check_bool("empty is root child", empty != NULL, 1);
    check_bool("sibling is root child", sibling != NULL, 1);
    if (sibling) check_str("sibling text", sibling->text, "ok");
    neverc_xml_node_free(tree);

    check_bool("mismatched tags rejected",
               neverc_xml_parse("<a></b>", 7) == NULL, 1);
    check_bool("unclosed tags rejected",
               neverc_xml_parse("<a>", 3) == NULL, 1);
    check_bool("unclosed comment rejected",
               neverc_xml_parse("<!-- bad", 8) == NULL, 1);
}

/* Regression: an element whose text is split into many runs by interspersed
 * children (mixed content) must concatenate every run in order. The builder
 * previously did strlen + realloc-to-exact on each run -> O(n^2); the parallel
 * length/capacity stack makes it O(n) with geometric growth. Large N here both
 * checks correctness and would crawl under the old quadratic path. */
static void test_mixed_content(void) {
    printf("[mixed content]\n");
    const int N = 4000;

    /* Document: <p>t0<b></b>t1<b></b>...<b></b>t{N}</p>, segment i = "[i]".
     * Each <b></b> opens and closes, so every text run belongs to <p> and the
     * reused stack slot for <b> must reset its text accumulator each time. */
    size_t cap = (size_t)N * 32 + 64, len = 0;
    char *xml = (char *)malloc(cap);
    char *want = (char *)malloc(cap);
    size_t wlen = 0;
    len += (size_t)snprintf(xml + len, cap - len, "<p>");
    for (int i = 0; i <= N; i++) {
        len  += (size_t)snprintf(xml + len, cap - len, "[%d]", i);
        wlen += (size_t)snprintf(want + wlen, cap - wlen, "[%d]", i);
        if (i < N) len += (size_t)snprintf(xml + len, cap - len, "<b></b>");
    }
    len += (size_t)snprintf(xml + len, cap - len, "</p>");

    neverc_xml_node_t *root = neverc_xml_parse(xml, len);
    check_bool("mixed parse ok", root != NULL, 1);
    neverc_xml_node_t *p = root ? neverc_xml_node_child(root, "p") : NULL;
    check_bool("mixed p found", p != NULL, 1);
    if (p) {
        check_str("mixed concatenated text", p->text, want);
        check_int("mixed child count", p->nchildren, N);
    }
    neverc_xml_node_free(root);
    free(xml);
    free(want);

    /* Text before, between and after children all belong to the parent. */
    const char *mc = "<a>x<b></b>y<c></c>z</a>";
    neverc_xml_node_t *r2 = neverc_xml_parse(mc, strlen(mc));
    neverc_xml_node_t *a = r2 ? neverc_xml_node_child(r2, "a") : NULL;
    if (a) check_str("mixed around children", a->text, "xyz");
    else   check_bool("mixed around children", 0, 1);
    neverc_xml_node_free(r2);
}

int main(void) {
    printf("=== NeverC Encoding/XML Module Tests ===\n\n");
    test_tokenizer();
    test_dom();
    test_comment();
    test_proc_inst();
    test_escape();
    test_entities_cdata_and_well_formedness();
    test_nested();
    test_self_closing_and_errors();
    test_mixed_content();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
