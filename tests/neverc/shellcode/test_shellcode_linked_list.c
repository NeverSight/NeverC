// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// Self-pointing const struct (classic compile-time linked list):
// each node holds a value and a pointer to the next const node.
// Data2TextPass must break the would-be infinite recursion inside
// getOrMaterialize: caching each (Function, GlobalValue) mapping
// *before* descending into its initializer lets the pass terminate
// even on reference chains like n0 -> n1 -> n2 -> NULL (and a
// hypothetical cyclic chain would likewise stop at the cached entry
// instead of blowing the stack).
//
// For args (i=1, _=0): sum the first two nodes' values starting at
// n0: n0.val + n1.val = 10 + 20 = 30.

struct node { int val; const struct node *next; };

static const struct node n2 = { 30, 0 };
static const struct node n1 = { 20, &n2 };
static const struct node n0 = { 10, &n1 };

int main(int i, int _) {
    (void)_;
    const struct node *p = &n0;
    int sum = 0;
    while (p && i-- >= 0) {
        sum += p->val;
        p = p->next;
    }
    return sum;
}
