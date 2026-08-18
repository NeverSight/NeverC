#include "neverc/std/html/template.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0;

static void check(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else printf("  FAIL: %s\n", name);
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && strcmp(got, expected) == 0) tests_passed++;
    else { printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got ? got : "(null)", expected); }
}

static void test_html_escape(void) {
    printf("[html_escape]\n");
    char *e = neverc_html_escape("<script>alert('xss')</script>");
    check_str("script", e, "&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;");
    free(e);

    e = neverc_html_escape("a & b < c > d \"e\"");
    check_str("mixed", e, "a &amp; b &lt; c &gt; d &#34;e&#34;");
    free(e);

    e = neverc_html_escape("safe text");
    check_str("safe", e, "safe text");
    free(e);

    e = neverc_html_escape("");
    check_str("empty", e, "");
    free(e);

    e = neverc_html_escape(NULL);
    check_str("null", e, "");
    free(e);
}

static void test_attr_escape(void) {
    printf("[attr_escape]\n");
    char *e = neverc_html_attr_escape("a & b < c > d \"e\"");
    check_str("attr mixed", e, "a &amp; b &lt; c &gt; d &#34;e&#34;");
    free(e);

    e = neverc_html_attr_escape("safe");
    check_str("attr safe", e, "safe");
    free(e);

    e = neverc_html_attr_escape(NULL);
    check_str("attr null", e, "");
    free(e);
}

static void test_js_escape(void) {
    printf("[js_escape]\n");
    char *e = neverc_html_js_escape("hello \"world\" <script>");
    check_str("js", e, "hello \\\"world\\\" \\u003cscript\\u003e");
    free(e);

    e = neverc_html_js_escape("line1\nline2");
    check_str("newline", e, "line1\\nline2");
    free(e);

    e = neverc_html_js_escape("`+$");
    check_str("template literal", e, "\\u0060\\u002b\\u0024");
    free(e);

    e = neverc_html_js_escape("a=b/c");
    check_str("equals and slash", e, "a\\u003db\\/c");
    free(e);

    e = neverc_html_js_escape("\x01");
    check_str("control", e, "\\u0001");
    free(e);

    e = neverc_html_js_escape("\xe2\x80\xa8" "x");
    check_str("line separator", e, "\\u2028x");
    free(e);

    e = neverc_html_js_escape(NULL);
    check_str("js null", e, "");
    free(e);
}

static void test_css_escape(void) {
    printf("[css_escape]\n");
    char *e = neverc_html_css_escape("!B");
    check_str("terminate before hex digit", e, "\\21 B");
    free(e);

    e = neverc_html_css_escape("#fff");
    check_str("selector escape does not absorb hex", e, "\\23 fff");
    free(e);

    e = neverc_html_css_escape("!G");
    check_str("non-hex continuation needs no separator", e, "\\21G");
    free(e);

    e = neverc_html_css_escape(NULL);
    check_str("css null", e, "");
    free(e);
}

static void test_url_escape(void) {
    printf("[url_escape]\n");
    char *e = neverc_html_url_query_escape("hello world&foo=bar");
    check_str("url", e, "hello%20world%26foo%3Dbar");
    free(e);

    e = neverc_html_url_query_escape(NULL);
    check_str("url null", e, "");
    free(e);
}

static void test_template_basic(void) {
    printf("[template_basic]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "Name", "Alice");
    neverc_html_template_data_set(&data, "Title", "Welcome");

    char *out = neverc_html_template_render("<h1>{{.Title}}</h1><p>Hello, {{.Name}}!</p>", &data);
    check_str("basic", out, "<h1>Welcome</h1><p>Hello, Alice!</p>");
    free(out);
    neverc_html_template_data_free(&data);
}

static void test_template_auto_escape(void) {
    printf("[template_auto_escape]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "Input", "<script>alert(1)</script>");

    char *out = neverc_html_template_render("User input: {{.Input}}", &data);
    check_str("auto_escape", out,
              "User input: &lt;script&gt;alert(1)&lt;/script&gt;");
    free(out);
    neverc_html_template_data_free(&data);
}

static void test_template_url_and_script(void) {
    printf("[template_url_and_script]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    char *out = neverc_html_template_render("<a href=\"{{.Link}}\">x</a>", &data);
    check("js url neutralized", out && strstr(out, "javascript:") == NULL);
    check("js url becomes hash", out && strstr(out, "href=\"#\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Name", "\";alert(1);//");
    out = neverc_html_template_render("<script>var x=\"{{.Name}}\";</script>", &data);
    check("script uses js escape", out && strstr(out, "\\\"") != NULL);
    check("script no html entity quote", out && strstr(out, "&#34;") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<a href={{.Link}}>x</a>", &data);
    check("unquoted js url neutralized", out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<form action=\"{{.Link}}\">", &data);
    check("form action js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("form action becomes hash", out && strstr(out, "action=\"#\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<button formaction=\"{{.Link}}\">go</button>", &data);
    check("formaction js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Name", "');alert(1);//");
    out = neverc_html_template_render("<img onclick=\"{{.Name}}\">", &data);
    check("onclick is js string", out && strstr(out, "&#39;") != NULL);
    check("onclick no raw quote breakout",
          out && strstr(out, "');alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<a href = \"{{.Link}}\">x</a>", &data);
    check("spaced href neutralized", out && strstr(out, "javascript:") == NULL);
    check("spaced href becomes hash", out && strstr(out, "href = \"#\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Name", "');alert(1);//");
    out = neverc_html_template_render("<img onclick = \"{{.Name}}\">", &data);
    check("spaced onclick is js string", out && strstr(out, "&#39;") != NULL);
    check("spaced onclick no raw quote breakout",
          out && strstr(out, "');alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<script src=\"{{.Link}}\"></script>", &data);
    check("script src js url neutralized", out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "data:text/javascript,alert(1)");
    out = neverc_html_template_render("<script src={{.Link}}></script>", &data);
    check("script src data url neutralized", out && strstr(out, "data:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Cls", "x onmouseover=alert(1)");
    out = neverc_html_template_render("<div class={{.Cls}}>", &data);
    check_str("unquoted attr wrapped", out,
              "<div class=\"x onmouseover=alert(1)\">");
    free(out);

    neverc_html_template_data_set(&data, "Name", "alert(1)");
    out = neverc_html_template_render("<img onclick={{.Name}}>", &data);
    check("unquoted onclick quoted", out && strstr(out, "onclick=\"") != NULL);
    check("unquoted onclick is js string",
          out && strstr(out, "&#39;alert(1)&#39;") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "P", "+alert(1)//");
    out = neverc_html_template_render("<img onclick=0{{.P}}>", &data);
    check("unquoted event prefix is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("unquoted event prefix no alert concat",
          out && strstr(out, "0'+alert") == NULL &&
              strstr(out, "0&#39;+alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Color", "!B");
    out = neverc_html_template_render("<div style=\"{{.Color}}\">", &data);
    check_str("style uses css escape", out, "<div style=\"\\21 B\">");
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<div style=\"background:url({{.Link}})\">", &data);
    check("css url() neutralized", out && strstr(out, "javascript:") == NULL);
    free(out);

    {
        char spaced[] = { 'j','a','v','a','\t','s','c','r','i','p','t',
                          ':','a','l','e','r','t','(','1',')',0 };
        neverc_html_template_data_set(&data, "Link", spaced);
        out = neverc_html_template_render("<a href=\"{{.Link}}\">x</a>", &data);
        check("tab in javascript scheme neutralized",
              out && strstr(out, "href=\"#\"") != NULL);
        check("tab scheme not passed through",
              out && strstr(out, "script:") == NULL);
        free(out);
    }
    {
        char ff[] = { '\x0c','j','a','v','a','s','c','r','i','p','t',
                      ':','a','l','e','r','t','(','1',')',0 };
        neverc_html_template_data_set(&data, "Link", ff);
        out = neverc_html_template_render("<a href=\"{{.Link}}\">x</a>", &data);
        check("formfeed before javascript neutralized",
              out && strstr(out, "javascript") == NULL);
        free(out);
    }

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<img dynsrc=\"{{.Link}}\">", &data);
    check("dynsrc js url neutralized", out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<img lowsrc=\"{{.Link}}\">", &data);
    check("lowsrc js url neutralized", out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<applet code=\"{{.Link}}\">", &data);
    check("applet code js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<embed pluginspage=\"{{.Link}}\">", &data);
    check("pluginspage js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<embed pluginurl=\"{{.Link}}\">", &data);
    check("pluginurl js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<svg><a><animate attributeName=\"href\" to=\"{{.Link}}\" /></a></svg>",
        &data);
    check("svg animate to js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<svg><a><set attributeName=\"href\" from=\"{{.Link}}\" /></a></svg>",
        &data);
    check("svg set from js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<svg><a><animate attributeName=\"href\" values=\"{{.Link}}\" /></a></svg>",
        &data);
    check("svg animate values js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<svg><a><animate attributeName=\"href\" by=\"{{.Link}}\" /></a></svg>",
        &data);
    check("svg animate by js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<img srcset=\"{{.Link}}\">", &data);
    check("srcset js url neutralized", out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<meta http-equiv=\"refresh\" content=\"0;url={{.Link}}\">", &data);
    check("meta refresh url= prefix neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("meta refresh url= prefix becomes hash",
          out && strstr(out, "url=#") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<meta http-equiv=\"refresh\" content=\"{{.Link}}\">", &data);
    check("meta refresh content neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("meta refresh content becomes hash",
          out && strstr(out, "content=\"#\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<meta content=\"{{.Link}}\" http-equiv=\"refresh\">", &data);
    check("meta refresh content-first neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<a href=\" {{.Link}}\">x</a>", &data);
    check("space after href quote neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("space after href quote becomes hash",
          out && strstr(out, "href=\" #\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "script:alert(1)");
    out = neverc_html_template_render("<a href=\"java{{.Link}}\">x</a>", &data);
    check("split javascript scheme neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("split javascript becomes hash",
          out && strstr(out, "href=\"java#\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Name", "alert(1)");
    out = neverc_html_template_render("<img onclick=\"{{.Name}}\">", &data);
    check("quoted onclick is js string",
          out && strstr(out, "&#39;alert(1)&#39;") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Color", "!B");
    out = neverc_html_template_render("<style>{{.Color}}</style>", &data);
    check_str("style tag uses css escape", out, "<style>\\21 B</style>");
    free(out);

    neverc_html_template_data_set(&data, "X", "<script>alert(1)</script>");
    out = neverc_html_template_render("<iframe srcdoc=\"{{.X}}\"></iframe>", &data);
    check("srcdoc is double-escaped",
          out && strstr(out, "&amp;lt;script") != NULL);
    check("srcdoc does not contain a raw script tag",
          out && strstr(out, "<script") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", " onload=alert(1)");
    out = neverc_html_template_render("<iframe srcdoc=Hello{{.X}}>", &data);
    check("unquoted srcdoc prefix breakout is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("unquoted srcdoc prefix does not inject onload",
          out && strstr(out, "onload") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "onclick=alert(1)");
    out = neverc_html_template_render("<div {{.X}}>", &data);
    check("attr name context is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("attr name context is not raw",
          out && strstr(out, "onclick=alert(1)") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", " x onclick=alert(1)");
    out = neverc_html_template_render("<div class=pre{{.X}}>", &data);
    check("unquoted prefix breakout is replaced",
          out && strstr(out, "onclick") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "1;alert(1)");
    out = neverc_html_template_render("<script>var x={{.X}}</script>", &data);
    check("unquoted js expr is quoted",
          out && strstr(out, "var x=\"") != NULL);
    check("unquoted js expr does not run extra statements",
          out && strstr(out, "var x=1;alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "+alert(1)//");
    out = neverc_html_template_render("<script>var x=0{{.X}}</script>", &data);
    check("js numeric prefix is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("js numeric prefix no concat",
          out && strstr(out, "0+alert") == NULL &&
              strstr(out, "0\"+alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "script:alert(1)");
    out = neverc_html_template_render(
        "<div style=\"background:url(java{{.Link}})\">", &data);
    check("css url() split scheme neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "\" onmouseover=alert(1) x=\"");
    out = neverc_html_template_render(
        "<div data-code=\"<script>\" class=\"{{.X}}\">", &data);
    check_str("script text in attr uses html escape", out,
              "<div data-code=\"<script>\" class=\"&#34; onmouseover=alert(1) x=&#34;\">");
    free(out);

    neverc_html_template_data_set(&data, "X", "onclick=alert(1)");
    out = neverc_html_template_render("<div title=\"a > b\" {{.X}}>", &data);
    check("quoted gt does not end the tag",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("quoted gt no raw onclick attr",
          out && strstr(out, "onclick=alert(1)") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "<img>");
    out = neverc_html_template_render("<!-- <script> -->{{.X}}", &data);
    check_str("comment does not start script context", out,
              "<!-- <script> -->&lt;img&gt;");
    free(out);

    neverc_html_template_data_set(&data, "Link", "https://example.com/a.js");
    out = neverc_html_template_render(
        "<script src=\"{{.Link}}\"></script>", &data);
    check_str("safe script src is not js-escaped", out,
              "<script src=\"https://example.com/a.js\"></script>");
    free(out);

    neverc_html_template_data_set(&data, "Color",
                                  "background:url(javascript:alert(1))");
    out = neverc_html_template_render("<div style=\"{{.Color}}\">", &data);
    check_str("style value js url neutralized", out, "<div style=\"#\">");
    free(out);

    neverc_html_template_data_set(&data, "Color",
                                  "background:url(javascript:alert(1))");
    out = neverc_html_template_render("<style>{{.Color}}</style>", &data);
    check_str("style tag js url neutralized", out, "<style>#</style>");
    free(out);

    neverc_html_template_data_set(&data, "X", "script");
    out = neverc_html_template_render("<{{.X}}>alert(1)</{{.X}}>", &data);
    check("tag name is replaced", out && strstr(out, "ZgotmplZ") != NULL);
    check("tag name is not a script element",
          out && strstr(out, "<script>") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "javascript");
    out = neverc_html_template_render("<a href=\"{{.X}}:alert(1)\">x</a>",
                                      &data);
    check("scheme then static colon neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("scheme then static colon becomes hash",
          out && strstr(out, "href=\"#") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "s");
    out = neverc_html_template_render(
        "<a href=\"java{{.X}}cript:alert(1)\">x</a>", &data);
    check("split scheme around interpolation neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "ftp://files.example.com/a");
    out = neverc_html_template_render("<a href=\"{{.Link}}\">x</a>", &data);
    check("unknown scheme neutralized",
          out && strstr(out, "ftp:") == NULL);
    check("unknown scheme becomes hash",
          out && strstr(out, "href=\"#\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "mailto:user@example.com");
    out = neverc_html_template_render("<a href=\"{{.Link}}\">x</a>", &data);
    check_str("mailto url allowed", out,
              "<a href=\"mailto:user@example.com\">x</a>");
    free(out);

    neverc_html_template_data_set(&data, "X", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<img srcset=\"https://example.com/a.jpg 1x, {{.X}}\">", &data);
    check("srcset later url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Q", "a&b=c");
    out = neverc_html_template_render(
        "<a href=\"/search?q={{.Q}}\">x</a>", &data);
    check_str("url query is percent-encoded", out,
              "<a href=\"/search?q=a%26b%3Dc\">x</a>");
    free(out);

    neverc_html_template_data_set(&data, "Color",
                                  "@import url(https://evil.example/x.css)");
    out = neverc_html_template_render("<style>{{.Color}}</style>", &data);
    check_str("style tag import neutralized", out, "<style>#</style>");
    free(out);

    neverc_html_template_data_set(&data, "Color", "expression(alert(1))");
    out = neverc_html_template_render("<div style=\"{{.Color}}\">", &data);
    check_str("style expression neutralized", out, "<div style=\"#\">");
    free(out);

    neverc_html_template_data_set(&data, "Color",
                                  "-moz-binding:url(http://evil.example/x.xml)");
    out = neverc_html_template_render("<div style=\"{{.Color}}\">", &data);
    check_str("style moz-binding neutralized", out, "<div style=\"#\">");
    free(out);

    neverc_html_template_data_set(&data, "Color",
                                  "\\6aavascript:alert(1)");
    out = neverc_html_template_render("<div style=\"{{.Color}}\">", &data);
    check("css escape bypass neutralized",
          out && strstr(out, "avascript") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "java/* */script:alert(1)");
    out = neverc_html_template_render(
        "<div style=\"background:url({{.Link}})\">", &data);
    check("css url() comment-hidden javascript neutralized",
          out && strstr(out, "javascript") == NULL &&
              strstr(out, "script:alert") == NULL);
    check("css url() comment-hidden becomes hash",
          out && strstr(out, "url(#)") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Color", "exp/* */ression(alert(1))");
    out = neverc_html_template_render("<div style=\"{{.Color}}\">", &data);
    check_str("style comment-hidden expression neutralized",
              out, "<div style=\"#\">");
    free(out);

    neverc_html_template_data_set(&data, "Color",
                                  "@im/* */port url(https://evil.example/x.css)");
    out = neverc_html_template_render("<style>{{.Color}}</style>", &data);
    check_str("style tag comment-hidden import neutralized",
              out, "<style>#</style>");
    free(out);

    neverc_html_template_data_set(&data, "Color", "*/body{color:blue}/*");
    out = neverc_html_template_render(
        "<style>/* {{.Color}} */ p{color:red}</style>", &data);
    check_str("style comment closer cannot break out",
              out, "<style>/* # */ p{color:red}</style>");
    free(out);

    neverc_html_template_data_set(&data, "Link", "https://example.com/a.css");
    out = neverc_html_template_render(
        "<div style=\"background:url({{.Link}})\">", &data);
    check("css url() https still interpolated",
          out && strstr(out, "https") != NULL);
    check("css url() https is not replaced with hash",
          out && strstr(out, "url(#)") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "</textarea><script>alert(1)</script>");
    out = neverc_html_template_render("<textarea>{{.X}}</textarea>", &data);
    check("textarea end tag is html-escaped",
          out && strstr(out, "&lt;/textarea&gt;") != NULL);
    check("textarea does not contain a raw script tag",
          out && strstr(out, "<script") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<svg xmlns=\"{{.Link}}\">", &data);
    check("xmlns js url neutralized", out && strstr(out, "javascript:") == NULL);
    check("xmlns js url becomes hash", out && strstr(out, "xmlns=\"#\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<svg xml:base=\"{{.Link}}\"><a href=\"x\">", &data);
    check("xml:base js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("xml:base js url becomes hash",
          out && strstr(out, "xml:base=\"#\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<a svg:href=\"{{.Link}}\">x</a>", &data);
    check("namespaced href js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<div data-href=\"{{.Link}}\">", &data);
    check("data-href js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render("<b datasrc=\"{{.Link}}\">", &data);
    check("datasrc js url neutralized",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "allow-scripts allow-same-origin");
    out = neverc_html_template_render(
        "<iframe sandbox=\"{{.X}}\" src=\"https://example.com\"></iframe>",
        &data);
    check("sandbox interpolation is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("sandbox flags are not passed through",
          out && strstr(out, "allow-scripts") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "refresh");
    out = neverc_html_template_render(
        "<meta http-equiv=\"{{.X}}\" content=\"0;url=javascript:alert(1)\">",
        &data);
    check("http-equiv interpolation is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("http-equiv cannot enable refresh",
          out && strstr(out, "http-equiv=\"refresh\"") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "*/alert(1)//");
    out = neverc_html_template_render("<script>/*{{.X}}*/</script>", &data);
    check("js block comment closer cannot break out",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("js block comment does not contain alert",
          out && strstr(out, "alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "-->alert(1)//");
    out = neverc_html_template_render(
        "<script><!--{{.X}}--></script>", &data);
    check("js html comment closer cannot break out",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("js html comment does not contain alert",
          out && strstr(out, "alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)//");
    out = neverc_html_template_render("<script>//{{.X}}</script>", &data);
    check("js line comment interpolation is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("js line comment does not contain alert",
          out && strstr(out, "alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "hello");
    out = neverc_html_template_render(
        "<script>/* done */var x = {{.X}};</script>", &data);
    check("js after closed comment still interpolates",
          out && strstr(out, "\"hello\"") != NULL);
    check("js after closed comment is not replaced",
          out && strstr(out, "ZgotmplZ") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<script></script-foo>{{.X}}</script>", &data);
    check("hyphenated script closer stays in js context",
          out && strstr(out, "\"alert(1)\"") != NULL);
    check("hyphenated script closer is not a raw call",
          out && strstr(out, "</script-foo>alert(1)") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X",
                                  "body{background:url(javascript:alert(1))}");
    out = neverc_html_template_render(
        "<style></style-foo>{{.X}}</style>", &data);
    check("hyphenated style closer stays in css context",
          out && strstr(out, "javascript") == NULL);
    check("hyphenated style closer is neutralized",
          out && strstr(out, "</style-foo>#</style>") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", ";alert(1)//");
    out = neverc_html_template_render(
        "<script>var x=\"ok\"{{.X}}</script>", &data);
    check("js after closed double quote is not statement concat",
          out && strstr(out, "\"ok\";alert") == NULL);
    check("js after closed double quote wraps as a string expr",
          out && strstr(out, "var x=\"ok\"") != NULL &&
              strstr(out, "\";alert(1)") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", ";alert(1)//");
    out = neverc_html_template_render(
        "<script>var x='ok'{{.X}}</script>", &data);
    check("js after closed single quote is not statement concat",
          out && strstr(out, "'ok';alert") == NULL);
    check("js after closed single quote wraps as a string expr",
          out && strstr(out, "var x='ok'") != NULL &&
              strstr(out, "\";alert(1)") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", ";alert(1)//");
    out = neverc_html_template_render(
        "<script>var x=\"ok\" {{.X}}</script>", &data);
    check("js after closed quote plus space is not statement concat",
          out && strstr(out, "\"ok\" ;alert") == NULL &&
              strstr(out, "\"ok\";alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "\";alert(1)//");
    out = neverc_html_template_render(
        "<script>var x=\"hello {{.X}}\"</script>", &data);
    check("js mid-string escapes a closer",
          out && strstr(out, "hello \\\";alert(1)") != NULL);
    check("js mid-string does not wrap a second literal",
          out && strstr(out, "hello \"\"") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", ");alert(1);//");
    out = neverc_html_template_render(
        "<img onclick=\"foo('{{.X}}')\">", &data);
    check("quoted event prefix is fail-closed",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("quoted event prefix is not a second js string",
          out && strstr(out, "alert(1)") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<script>x=`foo${ {{.X}} }`</script>", &data);
    check("js template interpolation wraps as a string expr",
          out && strstr(out, "\"alert(1)\"") != NULL);
    check("js template interpolation is not a raw call",
          out && strstr(out, "${ alert(1) }") == NULL &&
              strstr(out, "${alert(1)}") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X",
        "red;}body{background:url(https://evil.example/x)}");
    out = neverc_html_template_render("<style>p{color:{{.X}}}</style>", &data);
    check_str("css rule breakout neutralized", out, "<style>p{color:#}</style>");
    free(out);

    neverc_html_template_data_set(&data, "X",
        "red; background:url(https://evil.example/x)");
    out = neverc_html_template_render("<div style=\"color:{{.X}}\">", &data);
    check_str("css property breakout neutralized",
              out, "<div style=\"color:#\">");
    free(out);

    neverc_html_template_data_set(&data, "X", "red");
    out = neverc_html_template_render("<div style=\"color:{{.X}}\">", &data);
    check_str("safe css keyword still interpolates",
              out, "<div style=\"color:red\">");
    free(out);

    neverc_html_template_data_free(&data);
}

static void test_template_missing_var(void) {
    printf("[template_missing_var]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);

    char *out = neverc_html_template_render("Hello {{.Name}}!", &data);
    check_str("missing", out, "Hello !");
    free(out);
    neverc_html_template_data_free(&data);
}

static void test_template_if(void) {
    printf("[template_if]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "Show", "1");

    char *out = neverc_html_template_render("{{if .Show}}visible{{end}}", &data);
    check_str("if_true", out, "visible");
    free(out);

    neverc_html_template_data_set(&data, "Show", "0");
    out = neverc_html_template_render("{{if .Show}}visible{{end}}", &data);
    check_str("if_false", out, "");
    free(out);

    out = neverc_html_template_render(
        "{{if .Missing}}visible{{else}}hidden{{end}}", &data);
    check_str("if_missing", out, "hidden");
    free(out);

    neverc_html_template_data_free(&data);
}

static void test_template_if_else(void) {
    printf("[template_if_else]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "Show", "1");

    char *out = neverc_html_template_render(
        "{{if .Show}}yes{{else}}no{{end}}", &data);
    check_str("if_else_true", out, "yes");
    free(out);

    neverc_html_template_data_set(&data, "Show", "0");
    out = neverc_html_template_render(
        "{{if .Show}}yes{{else}}no{{end}}", &data);
    check_str("if_else_false", out, "no");
    free(out);

    neverc_html_template_data_set(&data, "A", "1");
    neverc_html_template_data_set(&data, "B", "0");
    out = neverc_html_template_render(
        "{{if .A}}{{if .B}}ab{{else}}a{{end}}{{else}}no{{end}}", &data);
    check_str("nested_if_else", out, "a");
    free(out);

    neverc_html_template_data_set(&data, "A", "0");
    out = neverc_html_template_render(
        "{{if .A}}{{if .B}}ab{{else}}a{{end}}{{else}}no{{end}}", &data);
    check_str("nested_if_outer_false", out, "no");
    free(out);

    neverc_html_template_data_free(&data);
}

static void test_template_range_subset(void) {
    printf("[template_range_subset]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);

    char *out = neverc_html_template_render(
        "A{{range .Items}}B{{end}}C", &data);
    check_str("range_absent", out, "AC");
    free(out);

    neverc_html_template_data_set(&data, "Items", "present");
    out = neverc_html_template_render(
        "A{{range .Items}}B{{end}}C", &data);
    check_str("range_present_once", out, "ABC");
    free(out);

    neverc_html_template_data_set(&data, "Show", "1");
    neverc_html_template_data_set(&data, "Items", "present");
    out = neverc_html_template_render(
        "{{if .Show}}A{{range .Items}}B{{end}}C{{else}}no{{end}}", &data);
    check_str("range_inside_if", out, "ABC");
    free(out);
    neverc_html_template_data_free(&data);
}

static void test_template_parse_errors(void) {
    printf("[template_parse_errors]\n");
    const char *bad[] = {
        "{{.Name", "{{if .Show}}open", "{{else}}", "{{end}}",
        "{{if}}", "{{range}}", "{{if .A}}{{if .B}}x{{end}}",
        "{{range .X}}{{else}}{{end}}", "{{if .A}}{{else}}{{else}}{{end}}",
        "{{if .A}}a{{else if .B}}b{{end}}", "{{.A | html}}",
        "{{if .Show extra}}yes{{end}}"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        neverc_html_template_t *t = neverc_html_template_parse(bad[i]);
        check("invalid template rejected", t == NULL);
        neverc_html_template_free(t);
    }
    check("NULL template rejected", neverc_html_template_parse(NULL) == NULL);
    check("NULL render rejected", neverc_html_template_render(NULL, NULL) == NULL);
    check("NULL execute rejected",
          neverc_html_template_execute(NULL, NULL) == NULL);

    neverc_html_template_t *ok = neverc_html_template_parse("Hi {{.Name}}");
    check("parse ok", ok != NULL);
    char *out = neverc_html_template_execute(ok, NULL);
    check_str("NULL data execute", out, "Hi ");
    free(out);
    neverc_html_template_free(ok);
}

static void test_data_operations(void) {
    printf("[data_operations]\n");
    neverc_html_template_data_t data;
    neverc_html_template_data_init(&data);
    neverc_html_template_data_set(&data, "key1", "val1");
    neverc_html_template_data_set(&data, "key2", "val2");
    check_str("get1", neverc_html_template_data_get(&data, "key1"), "val1");
    check_str("get2", neverc_html_template_data_get(&data, "key2"), "val2");
    check("get_missing", neverc_html_template_data_get(&data, "key3") == NULL);

    neverc_html_template_data_set(&data, "key1", "updated");
    check_str("update", neverc_html_template_data_get(&data, "key1"), "updated");
    neverc_html_template_data_free(&data);
}

int main(void) {
    test_html_escape();
    test_attr_escape();
    test_js_escape();
    test_css_escape();
    test_url_escape();
    test_template_basic();
    test_template_auto_escape();
    test_template_url_and_script();
    test_template_missing_var();
    test_template_if();
    test_template_if_else();
    test_template_range_subset();
    test_template_parse_errors();
    test_data_operations();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_passed == tests_run) puts("passed");
    return tests_passed == tests_run ? 0 : 1;
}
