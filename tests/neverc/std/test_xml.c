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
    r = neverc_xml_escape("a\tb\nc\rd", &outlen);
    check_str("escape tab lf cr", r, "a&#x9;b&#xA;c&#xD;d");
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
    outlen = 99;
    r = neverc_xml_unescape("&amp;lt;", 8, &outlen);
    check_str("no recursive entity expansion", r, "&lt;");
    free(r);

    /* Decimal/hex numeric entities that decode to markup must still go
     * through escape() before interpolation (XSS: &#60;script&#62;). */
    r = neverc_xml_unescape("&#60;script&#62;", 16, &outlen);
    check_str("decimal entity markup", r, "<script>");
    {
        char *esc = neverc_xml_escape(r, &outlen);
        check_str("escape decoded markup", esc, "&lt;script&gt;");
        free(esc);
    }
    free(r);
    r = neverc_xml_unescape("&#x3C;script&#x3E;", strlen("&#x3C;script&#x3E;"),
                            &outlen);
    check_str("hex entity markup", r, "<script>");
    free(r);
    r = neverc_xml_unescape("&#x22;onclick=alert(1)&#x22;",
                            strlen("&#x22;onclick=alert(1)&#x22;"), &outlen);
    check_str("hex entity quotes", r, "\"onclick=alert(1)\"");
    {
        char *esc = neverc_xml_escape(r, &outlen);
        check_str("escape decoded quotes", esc,
                  "&quot;onclick=alert(1)&quot;");
        free(esc);
    }
    free(r);
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

    {
        const char *ws_attr = "<r a=\"x&#10;y&#9;z\"/>";
        neverc_xml_decoder_t d;
        neverc_xml_token_t t;
        neverc_xml_decoder_init(&d, ws_attr, strlen(ws_attr));
        check_int("ws entity start token",
                  neverc_xml_decode_token(&d, &t), 1);
        check_bool("ws entity attr present",
                   t.nattrs == 1 && t.attrs != NULL, 1);
        if (t.nattrs == 1 && t.attrs)
            check_str("attr entity whitespace kept",
                      t.attrs[0].value, "x\ny\tz");
        neverc_xml_token_free(&t);
    }
    {
        const char *lit_ws = "<r a=\"x\ny\tz\"/>";
        neverc_xml_decoder_t d;
        neverc_xml_token_t t;
        neverc_xml_decoder_init(&d, lit_ws, strlen(lit_ws));
        check_int("lit ws start token",
                  neverc_xml_decode_token(&d, &t), 1);
        check_bool("lit ws attr present",
                   t.nattrs == 1 && t.attrs != NULL, 1);
        if (t.nattrs == 1 && t.attrs)
            check_str("attr literal whitespace normalized",
                      t.attrs[0].value, "x y z");
        neverc_xml_token_free(&t);
    }
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
        "\xef\xbb\xbf<r\xc2\xb7" "x><\xcd\xbf/></r\xc2\xb7" "x>";
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
        "<![CDATA[ ]]><root/>",
        "<root/><![CDATA[\n]]>",
        "<root/><!-- bad--comment -->",
        "<?xml version=\"1.1\"?><root/>",
        "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><root/>",
        "<?xml version=\"1.0\" encoding=\"UTF-16\"?><root/>",
        "<?xml version=\"1.0\" encoding=\"UTF-7\"?><root/>",
        "<?xml version=\"1.0\" encoding=\"windows-1252\"?><root/>",
        "<?xml version=\"1.0\" encoding = \"ISO-8859-1\"?><root/>",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" encoding=\"ISO-8859-1\"?><root/>",
        "<?xml?><root/>",
        "<?xml encoding=\"UTF-8\"?><root/>",
        "<?xml version=\"1.0\" encoding=\"\"?><root/>",
        "<?xml version=\"1.0\"encoding=\"ISO-8859-1\"?><root/>",
        "<root>&#0;</root>",
        "<root>&#x0;</root>",
        "<root>\v</root>"
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
    static const char overlong3[] = "<root>\xe0\x80\x80</root>";
    check_bool("reject overlong 3-byte UTF-8",
               neverc_xml_parse(overlong3, sizeof(overlong3) - 1) == NULL,
               1);
    static const char utf8_surrogate[] = "<root>\xed\xa0\x80</root>";
    check_bool("reject UTF-8 surrogate",
               neverc_xml_parse(utf8_surrogate, sizeof(utf8_surrogate) - 1)
                   == NULL,
               1);
    static const char too_large[] = "<root>\xf4\x90\x80\x80</root>";
    check_bool("reject UTF-8 above U+10FFFF",
               neverc_xml_parse(too_large, sizeof(too_large) - 1) == NULL,
               1);
    static const char noncharacter[] = "<root>\xef\xb7\x90</root>"; /* U+FDD0 */
    check_bool("reject XML noncharacter U+FDD0",
               neverc_xml_parse(
                   noncharacter, sizeof(noncharacter) - 1) == NULL,
               1);
    static const char bom_document[] = "\xef\xbb\xbf<root/>";
    tree = neverc_xml_parse(
        bom_document, sizeof(bom_document) - 1);
    check_bool("accept UTF-8 BOM", tree != NULL, 1);
    neverc_xml_node_free(tree);
    check_bool("reject empty document",
               neverc_xml_parse("", 0) == NULL, 1);
    check_bool("reject trailing non-whitespace",
               neverc_xml_parse("<root/>extra", 12) == NULL, 1);

    {
        static const char *utf8_decls[] = {
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?><root/>",
            "<?xml version=\"1.0\" encoding=\"utf-8\"?><root/>",
            "<?xml version='1.0' encoding='Utf-8'?><root/>",
            "<?xml version = \"1.0\" encoding = \"UTF-8\"?><root/>",
            "<?xml version=\"1.0\" standalone=\"yes\"?><root/>",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?><root/>"
        };
        for (size_t i = 0;
             i < sizeof(utf8_decls) / sizeof(utf8_decls[0]);
             i++) {
            tree = neverc_xml_parse(utf8_decls[i], strlen(utf8_decls[i]));
            check_bool("accept UTF-8 XML declaration", tree != NULL, 1);
            neverc_xml_node_free(tree);
        }
        neverc_xml_decoder_t d;
        neverc_xml_token_t t;
        const char *bad_enc =
            "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><root/>";
        neverc_xml_decoder_init(&d, bad_enc, strlen(bad_enc));
        check_int("tokenizer rejects non-UTF-8 encoding",
                  neverc_xml_decode_token(&d, &t), -1);
        check_int("non-UTF-8 encoding is an error token",
                  t.type, NEVERC_XML_ERROR);
        neverc_xml_token_free(&t);
    }

    {
        const int depth = 1001;
        size_t cap = (size_t)depth * 8U + 8U;
        char *nested = (char *)malloc(cap);
        size_t len = 0;
        check_bool("depth buffer", nested != NULL, 1);
        if (nested) {
            for (int i = 0; i < depth; i++)
                len += (size_t)snprintf(nested + len, cap - len, "<a>");
            for (int i = 0; i < depth; i++)
                len += (size_t)snprintf(nested + len, cap - len, "</a>");
            check_bool("reject over-deep nesting",
                       neverc_xml_parse(nested, len) == NULL, 1);
            /* 1000 is the cap: parse must succeed and free without overflowing
             * the C stack (node_free is iterative). */
            len = 0;
            for (int i = 0; i < 1000; i++)
                len += (size_t)snprintf(nested + len, cap - len, "<a>");
            for (int i = 0; i < 1000; i++)
                len += (size_t)snprintf(nested + len, cap - len, "</a>");
            tree = neverc_xml_parse(nested, len);
            check_bool("accept depth-1000 nesting", tree != NULL, 1);
            neverc_xml_node_free(tree);

            /* Parse permits a self-closing leaf on the innermost element.
             * neverc_xml_node_free used to skip that child when the C
             * stack already held root + 1000 open elements (off-by-one
             * vs the NCI_XML_MAX_DEPTH+2 array). */
            len = 0;
            for (int i = 0; i < 1000; i++)
                len += (size_t)snprintf(nested + len, cap - len, "<a>");
            len += (size_t)snprintf(nested + len, cap - len, "<b/>");
            for (int i = 0; i < 1000; i++)
                len += (size_t)snprintf(nested + len, cap - len, "</a>");
            tree = neverc_xml_parse(nested, len);
            check_bool("accept depth-1000 with self-closing leaf",
                       tree != NULL, 1);
            {
                neverc_xml_node_t *n = tree;
                int i;
                for (i = 0; n && i < 1000; i++)
                    n = neverc_xml_node_child(n, "a");
                check_bool("innermost a at depth 1000", n != NULL, 1);
                check_bool("self-closing leaf attached",
                           n && neverc_xml_node_child(n, "b") != NULL, 1);
            }
            neverc_xml_node_free(tree);
            free(nested);
        }
    }
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
