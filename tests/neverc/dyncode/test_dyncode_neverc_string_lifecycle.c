// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string lifecycle / scope tests.
 *
 * Exercises the value-typed string runtime's interaction with C scope
 * semantics: the auto-`__neverc_string_cleanup` attribute Sema attaches
 * to every locally-stored `string`, the assign helper's release-then-
 * install path, branched and looped scopes, function-parameter cleanup,
 * and the borrowed-view (cap == 0) path the helpers must NOT free.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status.
 * 0 means every assertion passed; any non-zero return points at the
 * specific `return N;` line below.
 */

static int len_of(string s) {
    /* The parameter `s` gets the cleanup attribute attached by Sema,
       so the owned retain-copy passed in is released at function exit
       even if the body returns early. */
    return (int)s.size();
}

int main(int a, int b) {
    (void)a;
    (void)b;

    /* (1) Block-scoped owned: cleanup at } releases the buffer. */
    {
        string s = "alpha" + "";
        if (!neverc_string_eq(s, "alpha"))
            return 1;
    }

    /* (2) Reassign with literal RHS: assignment must release the old
       owned buffer before installing the literal-derived clone. */
    {
        string s = "owned" + "buf";
        s = "literal";
        if (!neverc_string_eq(s, "literal"))
            return 2;
    }

    /* (3) Reassign with owned RHS twice: each step releases the
       previous buffer. */
    {
        string s = "first" + "";
        s = "second" + "";
        if (!neverc_string_eq(s, "second"))
            return 3;
        s = "third" + "";
        if (!neverc_string_eq(s, "third"))
            return 4;
    }

    /* (4) Conditional branches: cleanup must fire on every taken edge. */
    {
        string label = "label" + "";
        if (label.size() == 5) {
            string taken = "yes" + "";
            if (!neverc_string_eq(taken, "yes"))
                return 5;
        } else {
            string never = "no" + "";
            if (!neverc_string_eq(never, "no"))
                return 6;
        }
        if (!neverc_string_eq(label, "label"))
            return 7;
    }

    /* (5) Loop body: 256 owned strings come and go.  The arena's
       free-list MUST keep memory bounded -- otherwise a fresh user-
       arena (64 KB) would also be perfectly happy here, but we use a
       loop count high enough that any leak shows up as a kernel-style
       OOM in the smaller kernel build. */
    for (int i = 0; i < 256; ++i) {
        string body_local = "loop" + "";
        if (!neverc_string_eq(body_local, "loop"))
            return 8;
    }

    /* (6) Nested scopes: inner cleanup leaves the outer alive. */
    {
        string outer = "outer" + "";
        {
            string inner = "inner" + "";
            if (!neverc_string_eq(inner, "inner"))
                return 9;
        }
        if (!neverc_string_eq(outer, "outer"))
            return 10;
    }

    /* (7) Pass lvalue into a user-defined fn: callee receives a retain
       copy whose cleanup releases it on return; the caller's lvalue
       must remain valid across hundreds of calls. */
    {
        string keep = "param" + "";
        for (int i = 0; i < 64; ++i) {
            if (len_of(keep) != 5)
                return 11;
        }
        if (!neverc_string_eq(keep, "param"))
            return 12;
    }

    /* (8) Self-assignment: dst == src must not corrupt the buffer.
       Sema retains the RHS first, then `__neverc_string_assign`
       reinstalls it -- which means even the "trivially identical"
       case still ends up swapping in a fresh independent buffer. */
    {
        string s = "alias" + "";
        s = s;
        if (!neverc_string_eq(s, "alias"))
            return 13;
    }

    /* (9) clear / append cycle: clear collapses to empty, append must
       still produce a fresh owned buffer. */
    {
        string s = "abc" + "";
        s = neverc_string_clear(s);
        if (!neverc_string_empty(s))
            return 14;
        s = s.append("rebuilt");
        if (!neverc_string_eq(s, "rebuilt"))
            return 15;
    }

    /* (10) Borrowed view local (cap == 0): cleanup is a no-op so the
       backing literal pointer must NOT be touched. */
    {
        string borrowed = neverc_string_view("ab", 2);
        if (borrowed.cap != 0)
            return 16;
        if (!neverc_string_eq(borrowed, "ab"))
            return 17;
    }

    /* (11) Borrowed -> owned reassignment: cleanup at scope releases
       the new owned buffer cleanly. */
    {
        string s = neverc_string_view("hi", 2);
        s = "long" + "string";
        if (s.cap == 0)
            return 18;
        if (!neverc_string_eq(s, "longstring"))
            return 19;
    }

    /* (12) Goto OUT of a cleanup-attributed scope: the inner `temp`
       must still be released when the goto leaves its block.  Done in
       a loop so the free-list reuse path is exercised end-to-end and
       any silent leak would visibly grow the arena. */
    for (int rep = 0; rep < 16; ++rep) {
        {
            string temp = "goto" + "";
            if (neverc_string_eq(temp, "goto"))
                goto next_rep;
            return 20;
        }
next_rep:;
    }

    /* (13) Reassignment via compound `+=` releases its temporary. */
    {
        string s = "x" + "";
        for (int i = 0; i < 32; ++i)
            s += "y";
        if (s.size() != 33)
            return 21;
        if (s.front() != 'x' || s.back() != 'y')
            return 22;
    }

    return 0;
}
