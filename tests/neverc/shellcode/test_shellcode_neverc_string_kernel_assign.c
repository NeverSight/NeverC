// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string `neverc_string_assign` + borrowed-clone tests in ring-0
 * shellcode mode.
 *
 * Compile-only mirror of `test_shellcode_neverc_string_assign.c`: the
 * public `neverc_string_assign` API and the borrowed -> owned `neverc_string_clone`
 * path must lower into the same StringRuntimePass-rewritten allocator
 * as the rest of the prelude on the smaller 4 KB kernel arena.  If a
 * leak survived, KernelImportPass would surface a malloc/free extern
 * the loader cannot resolve.
 *
 * The body never reaches the loader -- the test only asserts that the
 * shellcode pipeline accepts every helper.
 */
int shellcode_entry(int seed) {
    /* owned dst <- owned src */
    {
        string dst = "owned" + "old";
        string src = "owned" + "new";
        neverc_string_assign(&dst, src);
        if (!neverc_string_eq(dst, "ownednew"))
            return seed + 1;
        if (dst.cap == 0)
            return seed + 2;
    }

    /* owned dst <- borrowed literal: post-assign dst must own its bytes. */
    {
        string dst = "owned" + "buf";
        neverc_string_assign(&dst, "literal");
        if (!neverc_string_eq(dst, "literal"))
            return seed + 3;
        if (dst.cap == 0)
            return seed + 4;
    }

    /* borrowed dst <- owned src: dst takes ownership cleanly. */
    {
        string dst = neverc_string_view("hi", 2);
        if (dst.cap != 0)
            return seed + 5;
        neverc_string_assign(&dst, "owned" + "src");
        if (!neverc_string_eq(dst, "ownedsrc"))
            return seed + 6;
    }

    /* src bytes alias dst's allocation: assign must copy first. */
    {
        string dst = "alpha" + "beta";
        neverc_string_assign(&dst, neverc_string_view(dst.data + 2, 4));
        if (!neverc_string_eq(dst, "phab"))
            return seed + 7;
    }

    /* Compound `=` rewrite hammered on the kernel arena. */
    {
        string dst = "init" + "";
        for (int i = 0; i < 64; ++i) {
            dst = "iter" + "step";
            if (!neverc_string_eq(dst, "iterstep"))
                return seed + 8;
        }
    }

    /* Public `neverc_string_assign` spelling in the same shape. */
    {
        string dst = "seed" + "";
        for (int i = 0; i < 64; ++i) {
            neverc_string_assign(&dst, "literal");
            if (!neverc_string_eq(dst, "literal"))
                return seed + 9;
        }
    }

    /* borrowed-view clone path. */
    {
        string borrowed = neverc_string_view("clone-me", 8);
        string copy = neverc_string_clone(borrowed);
        if (!neverc_string_eq(copy, "clone-me"))
            return seed + 10;
        if (copy.cap == 0)
            return seed + 11;
    }

    /* string literal clone via dotted spelling. */
    {
        string copy = "literal-clone".clone();
        if (!neverc_string_eq(copy, "literal-clone"))
            return seed + 12;
        if (copy.cap == 0)
            return seed + 13;
    }

    /* Self-assign through the public spelling: keep buffer alive. */
    {
        string s = "self" + "assign";
        neverc_string_assign(&s, s);
        if (!neverc_string_eq(s, "selfassign"))
            return seed + 14;
    }

    /* Forge handle short-circuits compare/find sinks instead of
       dereferencing the NULL pointer.  Same robustness contract as the
       user-mode test on the smaller kernel arena. */
    {
        string forged = {(const char *)0, 5, 0};
        if (neverc_string_compare(forged, "real") < 0)
            return seed + 15;
        if (neverc_string_find(forged, "x") != NEVERC_STRING_NPOS)
            return seed + 16;
    }

    return seed;
}
