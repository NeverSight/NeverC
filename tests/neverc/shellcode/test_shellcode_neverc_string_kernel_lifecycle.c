// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string lifecycle / scope tests in ring-0 shellcode mode.
 *
 * Compile-only guard for `-mshellcode-context=kernel`: the builtin string
 * prelude + StringRuntimePass must keep every owned buffer balanced
 * across the value-type contract Sema attaches to locally-stored
 * `string`s, even when the arena is the smaller 4 KB kernel variant.
 * If a leak survived the pipeline, KernelImportPass would surface a
 * malloc/free extern that the loader cannot resolve.
 *
 * The driver only runs codegen on this file; we deliberately exercise
 * the same control-flow shapes as the user-mode lifecycle test so a
 * pipeline regression that fires only in the kernel arena is caught.
 */

static int len_of(string s) {
    return (int)s.size();
}

int shellcode_entry(int seed) {
    /* Block-scoped owned: cleanup at } releases the buffer. */
    {
        string s = "alpha" + "";
        if (!neverc_string_eq(s, "alpha"))
            return seed + 1;
    }

    /* Reassign with literal RHS: assign helper releases the old owned. */
    {
        string s = "owned" + "buf";
        s = "literal";
        if (!neverc_string_eq(s, "literal"))
            return seed + 2;
    }

    /* Reassign with owned RHS: same release-then-install path. */
    {
        string s = "first" + "";
        s = "second" + "";
        if (!neverc_string_eq(s, "second"))
            return seed + 3;
    }

    /* Conditional branches: cleanup must fire on every taken edge. */
    {
        string label = "label" + "";
        if (label.size() == 5) {
            string only_taken = "yes" + "";
            if (!neverc_string_eq(only_taken, "yes"))
                return seed + 4;
        } else {
            string only_other = "no" + "";
            if (!neverc_string_eq(only_other, "no"))
                return seed + 5;
        }
    }

    /* Loop body: free-list reuse keeps the kernel arena bounded. */
    for (int i = 0; i < 64; ++i) {
        string body_local = "loop" + "";
        if (!neverc_string_eq(body_local, "loop"))
            return seed + 6;
    }

    /* Nested scopes: inner cleanup does not invalidate the outer. */
    {
        string outer = "outer" + "";
        {
            string inner = "inner" + "";
            if (!neverc_string_eq(inner, "inner"))
                return seed + 7;
        }
        if (!neverc_string_eq(outer, "outer"))
            return seed + 8;
    }

    /* Function parameter cleanup keeps the caller's lvalue alive. */
    {
        string keep = "param" + "";
        for (int i = 0; i < 32; ++i) {
            if (len_of(keep) != 5)
                return seed + 9;
        }
    }

    /* Self-assignment must not corrupt the buffer. */
    {
        string s = "alias" + "";
        s = s;
        if (!neverc_string_eq(s, "alias"))
            return seed + 10;
    }

    /* clear/append cycle through the kernel arena. */
    {
        string s = "abc" + "";
        s = neverc_string_clear(s);
        if (!neverc_string_empty(s))
            return seed + 11;
        s = s.append("rebuilt");
        if (!neverc_string_eq(s, "rebuilt"))
            return seed + 12;
    }

    /* Borrowed view local: cleanup is a no-op for cap == 0. */
    {
        string borrowed = neverc_string_view("ab", 2);
        if (borrowed.cap != 0)
            return seed + 13;
    }

    /* Borrowed -> owned reassignment: cleanup at scope releases owned. */
    {
        string s = neverc_string_view("hi", 2);
        s = "long" + "string";
        if (s.cap == 0)
            return seed + 14;
    }

    /* Goto OUT of a cleanup-attributed scope: temp must be released. */
    for (int rep = 0; rep < 8; ++rep) {
        {
            string temp = "goto" + "";
            if (neverc_string_eq(temp, "goto"))
                goto next_rep;
            return seed + 15;
        }
next_rep:;
    }

    return seed;
}
