// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// Cross-referencing const struct nodes: several `const struct node`
// variables where multiple nodes point to the same shared target
// (not a simple chain, more like a small DAG).  This stress-tests
// the `(Function, GlobalValue)` cache in Data2TextPass:
//
//   c.prev -> b                b.prev -> a
//   c.next -> a                         (shared with c.next)
//
// When stackifying `&c`, the recursion visits `b` which recurses
// into `a`.  On the second path (`c.next -> a`) the cache hits,
// reusing the same alloca for `a` instead of rebuilding it.  Without
// the cache, a would be materialised twice per function.
//
// Args (i=2, _=ignored): walk `c -> c.prev (b) -> b.prev (a)`
// two steps, return a.val = 1.

struct node {
    int val;
    const struct node *prev;
    const struct node *next;
};

static const struct node a = {1, 0, 0};
static const struct node b = {2, &a, 0};
static const struct node c = {3, &b, &a};

int main(int i, int _) {
    (void)_;
    const struct node *p = &c;
    for (int k = 0; k < i && p; k++)
        p = p->prev;
    return p ? p->val : -1;
}
