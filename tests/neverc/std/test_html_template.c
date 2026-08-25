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

    e = neverc_html_escape("a+b");
    check_str("plus", e, "a&#43;b");
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

    e = neverc_html_css_escape("!");
    check_str("css escape terminates at end of value", e, "\\21 ");
    free(e);

    e = neverc_html_css_escape(NULL);
    check_str("css null", e, "");
    free(e);
}

static void test_url_escape(void) {
    printf("[url_escape]\n");
    char *e = neverc_html_url_query_escape("hello world&foo=bar");
    check_str("url", e, "hello+world%26foo%3Dbar");
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

    neverc_html_template_data_set(&data, "Show", "yes");
    out = neverc_html_template_render("{{if.Show}}shown{{end}}", &data);
    check_str("if without space before selector", out, "shown");
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
    check_str("unquoted attr no-space escaped", out,
              "<div class=x&#32;onmouseover&#61;alert(1)>");
    free(out);

    neverc_html_template_data_set(&data, "Name", "alert(1)");
    out = neverc_html_template_render("<img onclick={{.Name}}>", &data);
    check("unquoted onclick remains one attribute",
          out && strstr(out, "onclick=&#39;") != NULL);
    check("unquoted onclick is js string",
          out && strstr(out, "&#39;alert(1)&#39;") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "x y");
    out = neverc_html_template_render("<div class={{.X}}-world>", &data);
    check_str("unquoted action keeps static suffix in value", out,
              "<div class=x&#32;y-world>");
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

    neverc_html_template_data_set(&data, "X", "!");
    out = neverc_html_template_render("<style>p{x:{{.X}}fff}</style>", &data);
    check_str("css action does not absorb following hex",
              out, "<style>p{x:\\21 fff}</style>");
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

    /* Go html/template urlFilter runs on the URL after `url=`, not on
     * `0;url=`+value as one scheme (`0;url=https` is not https). */
    neverc_html_template_data_set(&data, "Link", "https://example.com/r");
    out = neverc_html_template_render(
        "<meta http-equiv=\"refresh\" content=\"0;url={{.Link}}\">", &data);
    check_str("meta refresh url= keeps https", out,
              "<meta http-equiv=\"refresh\" content=\"0;url=https://example.com/r\">");
    free(out);

    neverc_html_template_data_set(&data, "Link", "https://example.com/r");
    out = neverc_html_template_render(
        "<meta http-equiv=\"refresh\" content=\"0; url ={{.Link}}\">", &data);
    check("meta refresh url = keeps https",
          out && strstr(out, "url =https://example.com/r") != NULL);
    check("meta refresh url = https is not a hash",
          out && strstr(out, "url =#") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<meta http-equiv=\"refresh\" content=\"0; url ={{.Link}}\">", &data);
    check("meta refresh url = js neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("meta refresh url = js becomes hash",
          out && strstr(out, "url =#") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "https://example.com/r");
    out = neverc_html_template_render(
        "<meta http-equiv=\"refresh\" content=\"0; {{.Link}}\">", &data);
    check("meta refresh implicit semicolon keeps https",
          out && strstr(out, "0; https://example.com/r") != NULL);
    check("meta refresh implicit semicolon https is not a hash",
          out && strstr(out, "0; #") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<meta http-equiv=\"refresh\" content=\"0; {{.Link}}\">", &data);
    check("meta refresh implicit semicolon js neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("meta refresh implicit semicolon js becomes hash",
          out && strstr(out, "0; #") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "https://example.com/r");
    out = neverc_html_template_render(
        "<meta http-equiv=\"refresh\" content=\"0,{{.Link}}\">", &data);
    check("meta refresh implicit comma keeps https",
          out && strstr(out, "0,https://example.com/r") != NULL);
    check("meta refresh implicit comma https is not a hash",
          out && strstr(out, "0,#") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "javascript:alert(1)");
    out = neverc_html_template_render(
        "<meta http-equiv=\"refresh\" content=\"0,{{.Link}}\">", &data);
    check("meta refresh implicit comma js neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("meta refresh implicit comma js becomes hash",
          out && strstr(out, "0,#") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "https://example.com/r");
    out = neverc_html_template_render(
        "<meta http-equiv=\"refresh\" content=\"0;URL={{.Link}}\">", &data);
    check("meta refresh URL= keeps https",
          out && strstr(out, "URL=https://example.com/r") != NULL);
    check("meta refresh URL= https is not a hash",
          out && strstr(out, "URL=#") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "script:alert(1)");
    out = neverc_html_template_render(
        "<meta http-equiv=\"refresh\" content=\"0;url=java{{.Link}}\">",
        &data);
    check("meta refresh url= split scheme neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("meta refresh url= split scheme becomes hash",
          out && strstr(out, "url=java#") != NULL);
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

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<iframe srcdoc=\"<script>{{.X}}</script>\">", &data);
    check("nested srcdoc script is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("nested srcdoc script is not raw",
          out && strstr(out, "alert(1)") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "x onerror=alert(1)");
    out = neverc_html_template_render(
        "<iframe srcdoc=\"<img src={{.X}}\">", &data);
    check("nested srcdoc attr is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("nested srcdoc attr has no onerror",
          out && strstr(out, "onerror") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", " onload=alert(1)");
    out = neverc_html_template_render("<iframe srcdoc=Hello{{.X}}>", &data);
    check("unquoted srcdoc prefix breakout is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("unquoted srcdoc prefix does not inject onload",
          out && strstr(out, "onload") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render("<!-- {{.X}} -->", &data);
    check("comment interpolation is empty",
          out && strstr(out, "alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<!--><script>{{.X}}</script>", &data);
    check("html5 <!--> leaves comment",
          out && strstr(out, "<script>") != NULL);
    check("html5 <!--> script is js-escaped",
          out && strstr(out, "<script>\"alert(1)\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<!--x--!><script>{{.X}}</script>", &data);
    check("html5 --!> leaves comment",
          out && strstr(out, "<script>") != NULL);
    check("html5 --!> script is js-escaped",
          out && strstr(out, "<script>\"alert(1)\"") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<a href=\"javascript&colon;{{.X}}\">", &data);
    check("entity colon scheme is neutralized",
          out && strstr(out, "javascript:") == NULL &&
              strstr(out, "javascript&colon;alert") == NULL);
    free(out);

    out = neverc_html_template_render(
        "<a href=\"javascript&Colon;{{.X}}\">", &data);
    check("html5 Colon entity is not a colon scheme",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    /* Entity name / numeric reference split across the interpolation: the
     * browser HTML-decodes the assembled attribute, so javascript&colo +
     * n;alert(1) becomes javascript:alert(1). Checking only the prefix
     * leaves no ASCII colon. */
    neverc_html_template_data_set(&data, "X", "n;alert(1)");
    out = neverc_html_template_render(
        "<a href=\"javascript&colo{{.X}}\">", &data);
    check("split named colon entity is neutralized",
          out && strstr(out, "javascript:") == NULL &&
              strstr(out, "javascript&colon;alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "colon;alert(1)");
    out = neverc_html_template_render(
        "<a href=\"javascript&{{.X}}\">", &data);
    check("split amp-colon entity is neutralized",
          out && strstr(out, "javascript:") == NULL &&
              strstr(out, "javascript&colon;alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "58;alert(1)");
    out = neverc_html_template_render(
        "<a href=\"javascript&#{{.X}}\">", &data);
    check("split numeric colon is neutralized",
          out && strstr(out, "javascript:") == NULL &&
              strstr(out, "javascript&#58;alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "a;alert(1)");
    out = neverc_html_template_render(
        "<a href=\"javascript&#x3{{.X}}\">", &data);
    check("split hex colon is neutralized",
          out && strstr(out, "javascript:") == NULL &&
              strstr(out, "javascript&#x3a;alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "colon;text/html,alert(1)");
    out = neverc_html_template_render(
        "<a href=\"data&{{.X}}\">", &data);
    check("split data colon entity is neutralized",
          out && strstr(out, "data&colon;text") == NULL &&
              strstr(out, "data&colon;alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "colon;alert(1)");
    out = neverc_html_template_render(
        "<a href=\"vbscript&{{.X}}\">", &data);
    check("split vbscript colon entity is neutralized",
          out && strstr(out, "vbscript&colon;alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<iframe srcdoc=\"&lt;img src=x onerror={{.X}}&gt;\">", &data);
    check("entity srcdoc prefix is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("entity srcdoc prefix has no onerror",
          out && strstr(out, "onerror=alert") == NULL);
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

    neverc_html_template_data_set(&data, "Link", "script:alert");
    out = neverc_html_template_render(
        "<div style=\"background:url( 'java{{.Link}}')\">", &data);
    check("css url() spaced quote split scheme neutralized",
          out && strstr(out, "url( 'java#')") != NULL);
    check("css url() spaced quote split is not css-escaped javascript",
          out && strstr(out, "javascript") == NULL &&
              strstr(out, "script\\3A") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "https://example.com/a.css");
    out = neverc_html_template_render(
        "<div style=\"background:url( '{{.Link}}')\">", &data);
    check("css url() spaced quote https still interpolated",
          out && strstr(out, "https://example.com/a.css") != NULL);
    check("css url() spaced quote https is not replaced with hash",
          out && strstr(out, "url( '#')") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "script:alert");
    out = neverc_html_template_render(
        "<div style=\"background:url(/*x*/'java{{.Link}}')\">", &data);
    check("css url() comment-quote split scheme neutralized",
          out && strstr(out, "url(/*x*/'java#')") != NULL);
    check("css url() comment-quote split is not css-escaped javascript",
          out && strstr(out, "javascript") == NULL &&
              strstr(out, "script\\3A") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link", "script:alert");
    out = neverc_html_template_render(
        "<div style=\"background:url(&quot;java{{.Link}})\">", &data);
    check("css url() entity quote split scheme neutralized",
          out && strstr(out, "url(&quot;java#)") != NULL);
    check("css url() entity quote split is not css-escaped javascript",
          out && strstr(out, "javascript") == NULL &&
              strstr(out, "script\\3A") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert");
    out = neverc_html_template_render(
        "<div style=\"background:url(javascript&colon;{{.X}})\">", &data);
    check("css url() entity colon neutralized",
          out && strstr(out, "url(javascript&colon;#)") != NULL);
    check("css url() entity colon does not keep the payload",
          out && strstr(out, "javascript&colon;alert") == NULL);
    free(out);

    out = neverc_html_template_render(
        "<div style=\"background:url(javascript&Colon;{{.X}})\">", &data);
    check("css url() html5 Colon is not a colon scheme",
          out && strstr(out, "javascript:") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "n;alert(1)");
    out = neverc_html_template_render(
        "<div style=\"background:url(javascript&colo{{.X}})\">", &data);
    check("css url() split named colon neutralized",
          out && strstr(out, "javascript:") == NULL &&
              strstr(out, "javascript&colon;") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "colon;alert(1)");
    out = neverc_html_template_render(
        "<div style=\"background:url(javascript&{{.X}})\">", &data);
    check("css url() split amp-colon neutralized",
          out && strstr(out, "javascript:") == NULL &&
              strstr(out, "javascript&colon;") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "script:alert");
    out = neverc_html_template_render(
        "<style>@import 'java{{.X}}';</style>", &data);
    check("css import split scheme neutralized",
          out && strstr(out, "@import 'java#';") != NULL);
    check("css import split is not css-escaped javascript",
          out && strstr(out, "javascript") == NULL &&
              strstr(out, "script\\3A") == NULL);
    free(out);

    out = neverc_html_template_render(
        "<a href=\"java{{.Missing}}script:alert(1)\">x</a>", &data);
    check("missing split href scheme neutralized",
          out && strstr(out, "javascript:") == NULL);
    check("missing split href becomes hash",
          out && strstr(out, "href=\"java#script:alert(1)\"") != NULL);
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

    neverc_html_template_data_set(&data, "Link", "https://example.com/a)x");
    out = neverc_html_template_render(
        "<div style=\"background:url({{.Link}})\">", &data);
    check("css url() closes paren is percent-encoded",
          out && strstr(out, "%29") != NULL);
    check("css url() does not CSS-escape paren",
          out && strstr(out, "\\29") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "Link",
                                  "https://example.com/&#34;onclick=alert(1) x=&#34;");
    out = neverc_html_template_render(
        "<div style=\"background:url({{.Link}})\">", &data);
    check("css url() attr html-escapes ampersand",
          out && strstr(out, "&amp;#34;") != NULL);
    check("css url() attr does not emit raw entity quote",
          out && strstr(out, "url(https://example.com/&#34;") == NULL);
    free(out);

    out = neverc_html_template_render(
        "<script>if (/{{.MissingRe}}/.test(s)) {}</script>", &data);
    check("missing JS regexp interpolation is (?:)",
          out && strstr(out, "/(?:)/") != NULL);
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

    /* Go html/template attrType: type is contentTypeUnsafe (script/style). */
    neverc_html_template_data_set(&data, "T", "text/javascript");
    neverc_html_template_data_set(&data, "S", "https://evil.example/x.js");
    out = neverc_html_template_render(
        "<script type=\"{{.T}}\" src=\"{{.S}}\"></script>", &data);
    check("type interpolation is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("type text/javascript not passed through",
          out && strstr(out, "text/javascript") == NULL);
    free(out);
    neverc_html_template_data_set(&data, "T", "");
    out = neverc_html_template_render(
        "<script type=\"{{.T}}\"></script>", &data);
    check("empty type interpolation is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
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

    /* CVE-2023-39318 / Go html/template: #! and --> are JS line comments.
     * A quote on the comment line must not desync so the next line is a
     * raw JS expression. */
    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<script>#! \"\n{{.X}}\n</script>", &data);
    check("hashbang then action is a js string",
          out && strstr(out, "\"alert(1)\"") != NULL);
    check("hashbang does not emit a bare call",
          out && strstr(out, "\nalert(1)") == NULL);
    free(out);

    out = neverc_html_template_render(
        "<script>--> \"\n{{.X}}\n</script>", &data);
    check("html-close-comment then action is a js string",
          out && strstr(out, "\"alert(1)\"") != NULL);
    check("html-close-comment does not emit a bare call",
          out && strstr(out, "\nalert(1)") == NULL);
    free(out);

    /* Go html/template: Annex B.1.1 `<!--` is a line comment. A quote on
     * that line must not desync so the next line is a raw JS expression. */
    out = neverc_html_template_render(
        "<script><!-- --> \"\n{{.X}}\n</script>", &data);
    check("html-open-comment then action is a js string",
          out && strstr(out, "\"alert(1)\"") != NULL);
    check("html-open-comment does not emit a bare call",
          out && strstr(out, "\nalert(1)") == NULL);
    free(out);

    out = neverc_html_template_render("<script>#!{{.X}}</script>", &data);
    check("hashbang same-line interpolation is replaced",
          out && strstr(out, "ZgotmplZ") != NULL);
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
          out && (strstr(out, "\"alert(1)\"") != NULL ||
                  strstr(out, "alert\\(1\\)") != NULL));
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

    neverc_html_template_data_set(&data, "X", ";alert(1)//");
    out = neverc_html_template_render(
        "<script>var x=\"OK\\{{.X}}\"</script>", &data);
    check("js dangling backslash is fail-closed",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("js dangling backslash does not break out",
          out && strstr(out, "alert(1)") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", ";alert(1)//");
    out = neverc_html_template_render(
        "<script>var x=`OK\\{{.X}}`</script>", &data);
    check("js dangling template-literal backslash is fail-closed",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("js dangling template-literal backslash does not break out",
          out && strstr(out, "alert(1)") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", ";alert(1)//");
    out = neverc_html_template_render(
        "<script>var x='OK\\{{.X}}'</script>", &data);
    check("js dangling single-quote backslash is fail-closed",
          out && strstr(out, "ZgotmplZ") != NULL);
    check("js dangling single-quote backslash does not break out",
          out && strstr(out, "alert(1)") == NULL);
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
        "<script>if (/\"/.test(s)) { var msg = {{.X}}; }</script>", &data);
    check("js regexp quote does not desync into a string",
          out && strstr(out, "\"alert(1)\"") != NULL);
    check("js regexp quote is not a raw call",
          out && strstr(out, "msg = alert(1)") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<script>if (/'/.test(s)) { var msg = {{.X}}; }</script>", &data);
    check("js regexp single quote does not desync",
          out && strstr(out, "\"alert(1)\"") != NULL &&
              strstr(out, "msg = alert(1)") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<script>if (/[\"']/.test(s)) { var msg = {{.X}}; }</script>", &data);
    check("js regexp charset quotes do not desync",
          out && strstr(out, "\"alert(1)\"") != NULL &&
              strstr(out, "msg = alert(1)") == NULL);
    free(out);

    /* Go nextJSCtx: after an identifier, '/' is division. Treating it as a
     * regexp consumes the author's later quotes so html_js_expr wraps a
     * second literal and closes the surrounding JS string. */
    neverc_html_template_data_set(&data, "X", "\";alert(1)//");
    out = neverc_html_template_render(
        "<script>var x = a / b; var s = \"{{.X}}\"</script>", &data);
    check("js division slash does not desync into a regexp",
          out && strstr(out, "\\\";alert(1)") != NULL);
    check("js division slash does not wrap a second literal",
          out && strstr(out, "s = \"\"") == NULL &&
              strstr(out, "\"\";alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<script>var x = a / b; var y = {{.X}};</script>", &data);
    check("js after division still wraps an expression",
          out && strstr(out, "\"alert(1)\"") != NULL);
    check("js after division is not a raw call",
          out && strstr(out, "y = alert(1)") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<script>return /{{.X}}/;</script>", &data);
    check("js return slash stays a regexp opener",
          out && strstr(out, "return /") != NULL);
    check("js return slash is not rewritten as a string then comment",
          out && strstr(out, "return \"alert(1)\"/") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "a.*b");
    out = neverc_html_template_render(
        "<script>return /{{.X}}/;</script>", &data);
    check("js regexp interpolates as a literal",
          out && strstr(out, "return /a\\.\\*b/") != NULL);
    check("js regexp does not wrap a JS string",
          out && strstr(out, "/\"") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X",
                                 "https://example.com/a;color:red");
    out = neverc_html_template_render(
        "<style>p{background:myurl({{.X}})}</style>", &data);
    check("myurl is not css url() context",
          out && strstr(out, "color:red") == NULL);
    check_str("myurl uses css value filter",
              out, "<style>p{background:myurl(#)}</style>");
    free(out);

    neverc_html_template_data_set(&data, "X", "https://example.com/a.png");
    out = neverc_html_template_render(
        "<style>p{background:url({{.X}})}</style>", &data);
    check("real url() still interpolates",
          out && strstr(out, "https://example.com/a.png") != NULL);
    free(out);

    /* U+2000 EN QUAD is in Go jsWhitespace. Missing it treats '/' as
     * division and desyncs the next quote into a raw call. */
    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<script>return\xe2\x80\x80/\";{{.X}}</script>", &data);
    check("u2000 after return does not desync into a string",
          out && (strstr(out, "\"alert(1)\"") != NULL ||
                  strstr(out, "alert\\(1\\)") != NULL));
    check("u2000 after return is not a raw call",
          out && strstr(out, "/;alert(1)") == NULL);
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

    /* `${` is stateJS. A double-quoted action inside it must use
     * js_escape only. Wrapping a second "..." closes the author's
     * string and runs the payload (NeverC analogue of CVE-2026-32289). */
    neverc_html_template_data_set(&data, "X", ";alert(1)//");
    out = neverc_html_template_render(
        "<script>var a = `${\"hello {{.X}}\"}`</script>", &data);
    check("js tpl dq interp does not wrap a second literal",
          out && strstr(out, "hello \";alert") == NULL &&
              strstr(out, "hello \"\"") == NULL);
    check("js tpl dq interp keeps the author's string",
          out && strstr(out, "hello ;alert(1)\\/\\/") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "+alert(1)");
    out = neverc_html_template_render(
        "<script>var a = `${\"hello {{.X}}\"}`</script>", &data);
    check("js tpl dq interp plus is not wrap-concat",
          out && strstr(out, "\"\"\\u002balert") == NULL &&
              strstr(out, "hello \\u002balert(1)") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "\";alert(1)//");
    out = neverc_html_template_render(
        "<script>var a = `${\"{{.X}}\"}`</script>", &data);
    check("js tpl dq interp escapes a closer",
          out && strstr(out, "${\"\\\";alert(1)\\/\\/\"}") != NULL);
    check("js tpl dq interp closer does not wrap",
          out && strstr(out, "${\"\"\"") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", ";alert(1)//");
    out = neverc_html_template_render(
        "<script>var a = `${'{{.X}}'}`</script>", &data);
    check("js tpl sq interp is js-escaped not wrapped",
          out && strstr(out, "${';alert(1)\\/\\/'}") != NULL &&
              strstr(out, "\"\";alert") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "${alert(1)}");
    out = neverc_html_template_render(
        "<script>var a = `hello {{.X}}`</script>", &data);
    check("js tpl literal does not wrap a second literal",
          out && strstr(out, "`hello \"") == NULL);
    check("js tpl literal escapes dollar and braces (Go jsBq)",
          out && strstr(out, "hello \\u0024\\u007balert(1)\\u007d") != NULL);
    free(out);

    /* Go js.go jsBqStrReplacementTable / escape_test.go
     * "JS template lit special characters". `{{` eats the `{` of `${`,
     * so `hello${{.X}}` is static `$` plus the action (stateJSTmplLit). */
    neverc_html_template_data_set(&data, "X", "{alert(1)}");
    out = neverc_html_template_render(
        "<script>var a = `hello${{.X}}`</script>", &data);
    check("js tpl glued ${{ does not open interpolation",
          out && strstr(out, "${alert(1)}") == NULL &&
              strstr(out, "hello${") == NULL);
    check("js tpl glued ${{ escapes braces after static dollar",
          out && strstr(out, "hello$\\u007balert(1)\\u007d") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "{alert(1)}");
    out = neverc_html_template_render(
        "<script>var a = `hello\\\\${{.X}}`</script>", &data);
    check("js tpl \\\\${{ does not open interpolation",
          out && strstr(out, "${alert(1)}") == NULL);
    check("js tpl \\\\${{ escapes braces after escaped backslash",
          out && strstr(out, "hello\\\\$\\u007balert(1)\\u007d") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "I", "${ asd `` }");
    out = neverc_html_template_render(
        "<script>var a = `{{.I}}`</script>", &data);
    check("js tpl Go jsBq specials",
          out && strstr(out,
              "`\\u0024\\u007b asd \\u0060\\u0060 \\u007d`") != NULL);
    free(out);

    neverc_html_template_data_set(&data, "I", "${ asd `` }");
    out = neverc_html_template_render(
        "<script>var a = `${ `{{.I}}` }`</script>", &data);
    check("js tpl nested lit Go jsBq specials",
          out && strstr(out,
              "${ `\\u0024\\u007b asd \\u0060\\u0060 \\u007d` }") != NULL);
    check("js tpl nested lit still interpolates the inner literal",
          out && strstr(out, "${ \"") == NULL);
    free(out);

    /* A fixed-depth interpolation stack used to silently discard the 17th
     * nested template literal, then misclassify JS code after the unwind as
     * template text. Values such as alert(1) are unchanged by the template
     * text escaper and therefore became executable statements. */
    {
        char deep[256];
        size_t pos = 0;
        static const char prefix[] = "<script>let x=";
        static const char suffix[] = ";{{.X}}</script>";
        memcpy(deep + pos, prefix, sizeof(prefix) - 1U);
        pos += sizeof(prefix) - 1U;
        for (int i = 0; i < 18; i++) {
            memcpy(deep + pos, "`${", 3);
            pos += 3;
        }
        deep[pos++] = '0';
        for (int i = 0; i < 18; i++) {
            memcpy(deep + pos, "}`", 2);
            pos += 2;
        }
        memcpy(deep + pos, suffix, sizeof(suffix) - 1U);
        pos += sizeof(suffix) - 1U;
        deep[pos] = '\0';

        neverc_html_template_data_set(&data, "X", "alert(1)");
        out = neverc_html_template_render(deep, &data);
        check("deep js template nesting fails closed",
              out && strstr(out, "ZgotmplZ") != NULL);
        check("deep js template nesting does not emit a raw call",
              out && strstr(out, ";alert(1)</script>") == NULL);
        free(out);
    }

    /* Go CVE-2026-39826: empty / whitespace script type is still JavaScript.
     * HTML-escaping alert(1) is a no-op and would execute. */
    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<script type=\"\">{{.X}}</script>", &data);
    check("empty script type is js-wrapped",
          out && strstr(out, "\"alert(1)\"") != NULL);
    check("empty script type is not a raw call",
          out && strstr(out, "<script type=\"\">alert(1)</script>") == NULL);
    free(out);

    neverc_html_template_data_set(&data, "X", "alert(1)");
    out = neverc_html_template_render(
        "<script type=\" \">{{.X}}</script>", &data);
    check("whitespace script type is js-wrapped",
          out && strstr(out, "\"alert(1)\"") != NULL);
    check("whitespace script type is not a raw call",
          out && strstr(out, "type=\" \">alert(1)") == NULL);
    free(out);

    out = neverc_html_template_render("<script>{{.Missing}}</script>", &data);
    check("missing JS action is an empty string expr",
          out && strstr(out, "<script>\"\"</script>") != NULL);
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
        "{{if .Show extra}}yes{{end}}", "{{.A-B}}", "{{.Name-.Other}}"
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
