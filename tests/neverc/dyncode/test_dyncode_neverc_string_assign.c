// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string `neverc_string_assign` public API + borrowed-clone tests.
 *
 * Sema rewrites every `dst = src;` for `string`-typed lvalues into
 * `__neverc_string_assign(&dst, src)`, but the prelude also exposes a
 * `neverc_string_assign(&dst, src)` spelling so explicit user calls keep parity
 * with std::string's `assign(...)` member.  Both paths must:
 *
 *   * release `*dst`'s previous owned buffer before installing `src`,
 *   * promote borrowed sources to owned so `*dst` always owns its bytes,
 *   * stay leak-free when `src` aliases `*dst`'s allocation,
 *   * stay leak-free in tight free-list reuse loops.
 *
 * Borrowed -> owned `neverc_string_clone` is the second under-covered path: every
 * other `clone()` test feeds an owned receiver, so we add explicit
 * borrowed-view -> independent-owned cases here so the
 * `__neverc_string_retain` short-circuit on `cap == 0` stays exercised.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status; 0 means
 * every assertion passed.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    /* (1) owned dst <- owned src: release-then-install, both buffers
       must end up independent so mutating one cannot affect the other. */
    {
        string dst = "owned" + "old";
        string src = "owned" + "new";
        neverc_string_assign(&dst, src);
        if (!neverc_string_eq(dst, "ownednew"))
            return 1;
        if (dst.cap == 0)
            return 2;
    }

    /* (2) owned dst <- borrowed literal: borrowed source must be promoted
       to an owned buffer attached to dst before the previous owned buffer
       is released.  Post-condition: dst is owned. */
    {
        string dst = "owned" + "buf";
        neverc_string_assign(&dst, "literal");
        if (!neverc_string_eq(dst, "literal"))
            return 3;
        if (dst.cap == 0)
            return 4;
    }

    /* (3) borrowed dst <- owned src: dst was a literal-backed view
       (cap == 0); after assign it must take ownership cleanly. */
    {
        string dst = neverc_string_view("hi", 2);
        if (dst.cap != 0)
            return 5;
        string src = "owned" + "src";
        neverc_string_assign(&dst, src);
        if (!neverc_string_eq(dst, "ownedsrc"))
            return 6;
        if (dst.cap == 0)
            return 7;
    }

    /* (4) src bytes alias dst's allocation: assign must copy the bytes
       out before dropping dst, otherwise the install would dangle. */
    {
        string dst = "alpha" + "beta";  /* owned "alphabeta" */
        neverc_string_assign(&dst, neverc_string_view(dst.data + 2, 4));
        if (!neverc_string_eq(dst, "phab"))
            return 8;
        if (dst.cap == 0)
            return 9;
    }

    /* (5) src bytes touch dst's slack but are out of range: assign drops
       src and keeps dst untouched (alias path that cannot promote). */
    {
        string dst = "abc" + "def";  /* owned "abcdef", cap == len + 1 == 7 */
        const char *slack = dst.data + dst.cap;
        neverc_string_assign(&dst, neverc_string_view(slack, 0));
        if (!neverc_string_eq(dst, "abcdef"))
            return 10;
    }

    /* (6) Compound assign hammered through the `=` rewrite: 256 owned
       sources reuse the arena's free list instead of leaking blocks. */
    {
        string dst = "init" + "";
        for (int i = 0; i < 256; ++i) {
            string src = "iter" + "step";
            dst = src;
            if (!neverc_string_eq(dst, "iterstep"))
                return 11;
            if (dst.cap == 0)
                return 12;
        }
    }

    /* (7) Public `neverc_string_assign` spelling in a tight loop: identical
       lifetime contract to the `=` rewrite, so the arena must stay
       bounded across hundreds of public-spelling assignments too. */
    {
        string dst = "seed" + "";
        for (int i = 0; i < 192; ++i) {
            neverc_string_assign(&dst, "literal");
            if (!neverc_string_eq(dst, "literal"))
                return 13;
            if (dst.cap == 0)
                return 14;
        }
    }

    /* (8) borrowed-view clone: the input has cap == 0, so retain takes
       the make-owned path.  Result must be an independent buffer with
       the literal's bytes. */
    {
        string borrowed = neverc_string_view("clone-me", 8);
        if (borrowed.cap != 0)
            return 15;
        string copy = neverc_string_clone(borrowed);
        if (!neverc_string_eq(copy, "clone-me"))
            return 16;
        if (copy.cap == 0)
            return 17;
        if (copy.data == borrowed.data)
            return 18;
    }

    /* (9) string-literal clone via the dotted form: same path, exercised
       through the methodised dispatcher. */
    {
        string copy = "literal-clone".clone();
        if (!neverc_string_eq(copy, "literal-clone"))
            return 19;
        if (copy.cap == 0)
            return 20;
    }

    /* (10) Self-assign through the public spelling: dst == src on the
       owned side must keep the buffer alive (matches the `=` rewrite). */
    {
        string s = "self" + "assign";
        neverc_string_assign(&s, s);
        if (!neverc_string_eq(s, "selfassign"))
            return 21;
        if (s.cap == 0)
            return 22;
    }

    /* (11) Forged handle through the by-value retain path: Sema wraps
       every lvalue string argument with `__neverc_string_retain`, which
       sanitises `{NULL, 5, 0}` to empty `{NULL, 0, 0}` via the
       `len && !data` guard in `__neverc_string_make_owned`.  The
       sanitised empty string then compares as shorter-than "real" (-1)
       and finds nothing (NPOS). */
    {
        string forged = {(const char *)0, 5, 0};  /* borrowed forged */
        if (neverc_string_compare(forged, "real") >= 0)
            return 23;  /* retain sanitises to empty -> empty < "real" */
        if (neverc_string_find(forged, "x") != NEVERC_STRING_NPOS)
            return 24;
    }

    /* (12) Mixed 192-iter loop: alternate `=` rewrite, public assign,
       and clone, all consuming the same dst.  Confirms the arena
       free-list reuses across multiple sinks without per-sink leaks. */
    {
        string dst = "rotate" + "";
        for (int i = 0; i < 192; ++i) {
            dst = neverc_string_clone(dst);
            neverc_string_assign(&dst, "halfway");
            dst = "rotate" + "";
            if (!neverc_string_eq(dst, "rotate"))
                return 25;
        }
    }

    return 0;
}
