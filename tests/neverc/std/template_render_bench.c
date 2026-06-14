/*
 * A/B benchmark + correctness check: text/template execute (render) path.
 *
 *  - old_* — the previous library engine, reproduced verbatim. Its executor
 *    re-derives every literal text node's length with strlen() on *every*
 *    render. A template is parsed once but executed many times, so for a page
 *    that is mostly static markup this re-scans the whole template body on each
 *    call (on top of the memcpy that actually emits it).
 *
 *  - neverc_template_* (library) — the new engine: the parser records each text
 *    node's byte length once, so execute() emits it with a single memcpy and no
 *    per-render strlen scan.
 *
 * Both engines share the public neverc_template_data_t lookup API, so the only
 * difference under test is the literal-text emission in the execute loop. Each
 * case asserts the new output is byte-for-byte identical to the old output
 * before timing the parse-once / execute-many hot loop.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/tpl_render_bench \
 *      tests/neverc/std/template_render_bench.c std/src/text/template/template.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/text/template.h"

/* ============================================================
 * OLD engine — verbatim reproduction of the previous library
 * (executor uses strlen() per text node, no cached length)
 * ============================================================ */
enum { O_TEXT, O_VAR, O_IF, O_RANGE };

typedef struct o_tnode {
    int             type;
    char           *text;
    char           *key;
    struct o_tnode *children;
    int             nchildren;
    int             cap;
    struct o_tnode *else_branch;
    int             nelse;
    int             else_cap;
    struct o_tnode *next;
} o_tnode_t;

typedef struct {
    o_tnode_t *nodes;
    int        nnodes;
    int        cap;
} o_template_t;

static void o_add_node(o_tnode_t **list, int *count, int *cap, o_tnode_t node) {
    if (*count >= *cap) {
        *cap = (*cap == 0) ? 8 : *cap * 2;
        *list = (o_tnode_t *)realloc(*list, *cap * sizeof(o_tnode_t));
    }
    (*list)[(*count)++] = node;
}

static char *o_dup_str(const char *s, size_t n) {
    char *r = (char *)malloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static char *o_trim_ws(const char *s, size_t len) {
    while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) len--;
    return o_dup_str(s, len);
}

static int o_parse_nodes(const char **p, const char *end,
                         o_tnode_t **nodes, int *count, int *cap,
                         int stop_on_end, int stop_on_else);

static int o_parse_action(const char **p, const char *end,
                          o_tnode_t **nodes, int *count, int *cap) {
    const char *start = *p;
    const char *close = strstr(start, "}}");
    if (!close) return -1;

    const char *inner = start + 2;
    size_t ilen = close - inner;
    char *action = o_trim_ws(inner, ilen);
    *p = close + 2;

    if (strncmp(action, "if ", 3) == 0) {
        o_tnode_t node;
        memset(&node, 0, sizeof(node));
        node.type = O_IF;
        node.key = o_trim_ws(action + 3, strlen(action + 3));
        if (node.key[0] == '.') {
            char *nk = o_dup_str(node.key + 1, strlen(node.key + 1));
            free(node.key);
            node.key = nk;
        }
        int child_cap = 0;
        o_parse_nodes(p, end, &node.children, &node.nchildren, &child_cap, 1, 1);
        if (*p + 2 < end && strncmp(*p, "{{", 2) == 0) {
            const char *check = *p + 2;
            while (*check == ' ') check++;
            if (strncmp(check, "else", 4) == 0) {
                const char *ec = strstr(*p, "}}");
                if (ec) *p = ec + 2;
                int else_cap = 0;
                o_parse_nodes(p, end, &node.else_branch, &node.nelse, &else_cap, 1, 0);
            }
        }
        free(action);
        o_add_node(nodes, count, cap, node);
        return 0;
    }

    if (strncmp(action, "range ", 6) == 0) {
        o_tnode_t node;
        memset(&node, 0, sizeof(node));
        node.type = O_RANGE;
        node.key = o_trim_ws(action + 6, strlen(action + 6));
        if (node.key[0] == '.') {
            char *nk = o_dup_str(node.key + 1, strlen(node.key + 1));
            free(node.key);
            node.key = nk;
        }
        int child_cap = 0;
        o_parse_nodes(p, end, &node.children, &node.nchildren, &child_cap, 1, 0);
        free(action);
        o_add_node(nodes, count, cap, node);
        return 0;
    }

    if (strcmp(action, "end") == 0) { free(action); return 1; }
    if (strcmp(action, "else") == 0) { free(action); return 2; }

    o_tnode_t node;
    memset(&node, 0, sizeof(node));
    node.type = O_VAR;
    if (action[0] == '.')
        node.key = o_dup_str(action + 1, strlen(action + 1));
    else
        node.key = o_dup_str(action, strlen(action));
    free(action);
    o_add_node(nodes, count, cap, node);
    return 0;
}

static int o_parse_nodes(const char **p, const char *end,
                         o_tnode_t **nodes, int *count, int *cap,
                         int stop_on_end, int stop_on_else) {
    while (*p < end) {
        const char *next = strstr(*p, "{{");
        if (!next) {
            if (*p < end) {
                o_tnode_t node;
                memset(&node, 0, sizeof(node));
                node.type = O_TEXT;
                node.text = o_dup_str(*p, end - *p);
                o_add_node(nodes, count, cap, node);
            }
            *p = end;
            return 0;
        }
        if (next > *p) {
            o_tnode_t node;
            memset(&node, 0, sizeof(node));
            node.type = O_TEXT;
            node.text = o_dup_str(*p, next - *p);
            o_add_node(nodes, count, cap, node);
        }
        *p = next;
        const char *close = strstr(*p + 2, "}}");
        if (!close) { *p = end; return 0; }
        const char *inner = *p + 2;
        while (*inner == ' ') inner++;
        if (stop_on_end && strncmp(inner, "end", 3) == 0) { *p = close + 2; return 1; }
        if (stop_on_else && strncmp(inner, "else", 4) == 0) return 2;
        int r = o_parse_action(p, end, nodes, count, cap);
        if (r == 1 && stop_on_end) return 1;
        if (r == 2 && stop_on_else) return 2;
    }
    return 0;
}

static o_template_t *o_parse(const char *text) {
    o_template_t *tmpl = (o_template_t *)calloc(1, sizeof(*tmpl));
    const char *p = text;
    const char *end = text + strlen(text);
    o_parse_nodes(&p, end, &tmpl->nodes, &tmpl->nnodes, &tmpl->cap, 0, 0);
    return tmpl;
}

typedef struct { char *data; size_t len, cap; } o_outbuf_t;
static void o_out_init(o_outbuf_t *b) { b->cap = 256; b->data = (char *)malloc(b->cap); b->len = 0; }
static void o_out_puts(o_outbuf_t *b, const char *s, size_t n) {
    while (b->len + n >= b->cap) { b->cap *= 2; b->data = (char *)realloc(b->data, b->cap); }
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void o_exec_nodes(o_outbuf_t *out, o_tnode_t *nodes, int count,
                         const neverc_template_data_t *data) {
    for (int i = 0; i < count; i++) {
        o_tnode_t *n = &nodes[i];
        switch (n->type) {
        case O_TEXT:
            o_out_puts(out, n->text, strlen(n->text)); /* the old per-render scan */
            break;
        case O_VAR: {
            const char *val = neverc_template_data_get(data, n->key);
            if (val) o_out_puts(out, val, strlen(val));
            break;
        }
        case O_IF: {
            const char *val = neverc_template_data_get(data, n->key);
            int truthy = val && val[0] != '\0' && strcmp(val, "0") != 0 &&
                         strcmp(val, "false") != 0;
            if (truthy) o_exec_nodes(out, n->children, n->nchildren, data);
            else if (n->else_branch) o_exec_nodes(out, n->else_branch, n->nelse, data);
            break;
        }
        case O_RANGE:
            if (neverc_template_data_get(data, n->key))
                o_exec_nodes(out, n->children, n->nchildren, data);
            break;
        }
    }
}

static char *o_execute(o_template_t *tmpl, const neverc_template_data_t *data, size_t *outlen) {
    o_outbuf_t out;
    o_out_init(&out);
    o_exec_nodes(&out, tmpl->nodes, tmpl->nnodes, data);
    out.data[out.len] = '\0';
    *outlen = out.len;
    return out.data;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile uint64_t sink;

static char *make_repeat(const char *pattern, size_t target_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    return s;
}

static neverc_template_data_t g_data;

static void bench_case(const char *label, const char *tmpl_text) {
    o_template_t *ot = o_parse(tmpl_text);
    neverc_template_t *nt = neverc_template_parse(tmpl_text, NULL);

    size_t ol = 0, nl = 0;
    char *o = o_execute(ot, &g_data, &ol);
    char *n = neverc_template_execute(nt, &g_data, &nl);
    if (!o || !n || ol != nl || memcmp(o, n, ol) != 0) {
        printf("%-22s  CORRECTNESS FAIL (old %zu B, new %zu B)\n", label, ol, nl);
        if (o && n) printf("    old=\"%.48s\"\n    new=\"%.48s\"\n", o, n);
        free(o); free(n);
        return;
    }
    size_t out_len = nl;
    free(o); free(n);

    int iters = (int)(400000000 / (strlen(tmpl_text) + 1)); if (iters < 500) iters = 500;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            size_t l; char *q = o_execute(ot, &g_data, &l); sink += (uint64_t)q[0]; free(q);
        }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            size_t l; char *q = neverc_template_execute(nt, &g_data, &l); sink += (uint64_t)q[0]; free(q);
        }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-22s  %8.1f ms  %8.1f ms  %6.2fx   (out %zu B)\n",
           label, t_old*1000, t_new*1000, t_old/t_new, out_len);

    /* o_template_t / neverc_template_t leak intentionally: process exits next. */
    (void)ot; (void)nt;
}

int main(void) {
    neverc_template_data_init(&g_data);
    neverc_template_data_set(&g_data, "title", "Quarterly Report");
    neverc_template_data_set(&g_data, "user", "Ada Lovelace");
    neverc_template_data_set(&g_data, "count", "42");
    neverc_template_data_set(&g_data, "active", "true");
    neverc_template_data_set(&g_data, "items", "yes");
    neverc_template_data_set(&g_data, "body", "the body");

    printf("=== text/template execute: cached text-len (new) vs strlen-per-render (old) ===\n");
    printf("%-22s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    /* Static-heavy page: large literal block, a handful of variables. */
    char *chunk = make_repeat(
        "<p>The quick brown fox jumps over the lazy dog and keeps going. </p>\n", 3072);
    size_t sh_cap = strlen(chunk) + 256;
    char *static_heavy = malloc(sh_cap);
    snprintf(static_heavy, sh_cap, "<h1>{{.title}}</h1>\n%s<footer>by {{.user}}</footer>", chunk);

    /* Balanced: medium text segments interleaved with variables. */
    char *balanced = make_repeat(
        "Hello {{.user}}, you have {{.count}} new messages today. ", 2048);

    /* Var-dense: many small literals between many variable substitutions. */
    char *var_dense = make_repeat("[{{.count}}] {{.title}} - ", 1536);

    /* Control flow: if/range wrapping a static body. */
    char *control = make_repeat(
        "{{if .active}}<li>{{.title}}: {{.body}}</li>{{end}}\n", 2048);

    bench_case("static_heavy", static_heavy);
    bench_case("balanced", balanced);
    bench_case("var_dense", var_dense);
    bench_case("control_flow", control);

    free(chunk); free(static_heavy); free(balanced); free(var_dense); free(control);

    /* Correctness sweep over assorted shapes. */
    printf("\n");
    const char *cases[] = {
        "",
        "no actions at all, just text",
        "{{.title}}",
        "prefix {{.user}} suffix",
        "{{if .active}}on{{else}}off{{end}}",
        "{{if .missing}}yes{{else}}no{{end}}",
        "{{range .items}}row {{.title}} {{end}}done",
        "a{{.count}}b{{.user}}c{{.title}}d",
        "<<>>&& {{.title}} \t\n trailing",
        "{{.title}}{{.user}}{{.count}}{{.active}}{{.body}}",
    };
    int ncases = (int)(sizeof(cases)/sizeof(cases[0]));
    int ok = 0;
    for (int i = 0; i < ncases; i++) {
        o_template_t *ot = o_parse(cases[i]);
        neverc_template_t *nt = neverc_template_parse(cases[i], NULL);
        size_t ol = 0, nl = 0;
        char *o = o_execute(ot, &g_data, &ol);
        char *n = neverc_template_execute(nt, &g_data, &nl);
        if (o && n && ol == nl && memcmp(o, n, ol) == 0) ok++;
        else printf("  case[%d] FAIL: old=\"%.40s\" new=\"%.40s\"\n", i, o?o:"(null)", n?n:"(null)");
        free(o); free(n);
        neverc_template_free(nt);
        free(ot); /* old nodes leak; fine for a short-lived bench */
    }
    printf("edge cases: %d/%d identical\n", ok, ncases);

    neverc_template_data_free(&g_data);
    printf("\n=== Done ===\n");
    return 0;
}
