// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string memory-safety / lifetime stress.
 *
 * Targets the scenarios most likely to leak or dangle in a value-type
 * string runtime built on top of the shellcode arena:
 *
 *   * self-assignment (owned->owned, borrowed literal->borrowed),
 *   * borrowed-view-into-owned (the `__neverc_string_view_in_range`
 *     branch in `__neverc_string_assign`),
 *   * deep method chains where every step consumes its base + returns
 *     a fresh owned,
 *   * free-list reuse pressure (256-iter loops),
 *   * `neverc_string_clear` -> `append` cycle, `neverc_string_swap`, and literal
 *     forwarding through helpers.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status.
 * 0 means every assertion passed.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    /* 1) Self-assign on an owned string: same-buffer alias must not free
       the buffer mid-flight. */
    {
        string s = "abc" + "def";
        s = s;
        if (!neverc_string_eq(s, "abcdef"))
            return 1;
    }

    /* 2) Self-assign on a borrowed literal view: assignment promotes to
       owned without leaking the literal pointer. */
    {
        string s = "literal";
        s = s;
        if (!neverc_string_eq(s, "literal"))
            return 2;
    }

    /* 3) Borrowed view pointing inside dst's own allocation: assignment
       must copy first, then free the source allocation. */
    {
        string s = "alpha" + "beta";  /* owned "alphabeta" */
        s = neverc_string_view(s.data + 2, 4);
        if (!neverc_string_eq(s, "phab"))
            return 3;
    }

    /* 4) substr produces a fresh owned; the original keeps living. */
    {
        string s = "hello" + "world";
        string t = s.substr(0, 5);
        if (!neverc_string_eq(t, "hello"))
            return 4;
        if (!neverc_string_eq(s, "helloworld"))
            return 5;
    }

    /* 5) Long += chain: arena free-list must reuse blocks instead of
       running out of room. */
    {
        string s = "x";
        for (int i = 0; i < 64; i++)
            s += "y";
        if (s.size() != 65)
            return 6;
        if (s.front() != 'x' || s.back() != 'y')
            return 7;
    }

    /* 6) clone (and method form) produces an independent buffer with the
       same content. */
    {
        string s = "alpha" + "";
        string c1 = neverc_string_clone(s);
        string c2 = s.clone();
        if (c1.data == s.data || c2.data == s.data)
            return 8;
        if (!neverc_string_eq(c1, "alpha") || !neverc_string_eq(c2, "alpha") ||
            !neverc_string_eq(s, "alpha"))
            return 9;
    }

    /* 7) Deep method chain — each step consumes its base and returns a
       fresh owned, so the final value must reflect every step. */
    {
        string s = "  hello world  " + "";
        s = s.trim().to_upper().replace(0, 5, "XXXXX");
        if (!neverc_string_eq(s, "XXXXX WORLD"))
            return 10;
    }

    /* 8) Free-list reuse stress in a tight loop: a short-lived owned
       string must roundtrip through the arena hundreds of times. */
    {
        for (int i = 0; i < 256; i++) {
            string loop = "warm" + "up";
            if (!neverc_string_eq(loop, "warmup"))
                return 11;
        }
    }

    /* 9) neverc_string_clear leaves an empty / owned-free string; appending
       must still produce a fresh owned buffer. */
    {
        string e = neverc_string_clear("data");
        if (!neverc_string_empty(e))
            return 12;
        e = e.append("rebuilt");
        if (!neverc_string_eq(e, "rebuilt"))
            return 13;
    }

    /* 10) neverc_string_swap transfers ownership both ways without leaking
       either side. */
    {
        string left = "left" + "";
        string right = "right" + "";
        neverc_string_swap(&left, &right);
        if (!neverc_string_eq(left, "right") || !neverc_string_eq(right, "left"))
            return 14;
    }

    /* 11) Argument forwarding: a borrowed literal flows through multiple
       runtime helpers without producing dangling views. */
    {
        for (int i = 0; i < 64; i++) {
            if (!neverc_string_eq(neverc_string_substr("forward", 0, 7), "forward"))
                return 15;
            if (neverc_string_find("forward", "rd") != 5)
                return 16;
            if (neverc_string_find_first_of("forward", "wd") != 3)
                return 17;
        }
    }

    /* 12) Resize then free-list reuse: collapse to empty, regrow, and
       confirm content survives — guards against the new resize helper
       leaking a half-initialised buffer. */
    {
        string r = "ab" + "cd";
        for (int i = 0; i < 96; i++) {
            r = r.resize(8, '+');
            if (!neverc_string_eq(r, "abcd++++"))
                return 18;
            r = r.resize(4, '?');
            if (!neverc_string_eq(r, "abcd"))
                return 19;
        }
    }

    /* 13) Owned string used as comparison RHS in a loop must not
       accumulate retained copies. */
    {
        string needle = "needle";
        for (int i = 0; i < 96; i++) {
            if (!neverc_string_eq(needle, "needle"))
                return 20;
            if (needle.compare("needle") != 0)
                return 21;
        }
    }

    /* 14) Self-swap through the dotted-call dispatcher: `s.swap(s)`
       lowers to `neverc_string_swap(&s, &s)` (no retain insertion because
       `MethodReceiverKind::All` wraps the second arg in `&` instead
       of by-value).  Both pointers alias, so the helper's
       `tmp = *a; *a = *b; *b = tmp;` body must end up storing the
       original handle back at `*a` -- any short-circuit that bails
       early on `a == b` would leak the only owned buffer the test
       allocated. */
    {
        string s = "alpha" + "beta";  /* owned "alphabeta" */
        s.swap(s);
        if (!neverc_string_eq(s, "alphabeta"))
            return 22;
        if (s.cap == 0)
            return 23;
    }

    /* 15) Self-assign through the dotted-call dispatcher: `s.assign(s)`
       lowers to `neverc_string_assign(&s, retain(s))` (the second arg is
       by-value, so Sema's retain pass materialises a fresh owned
       copy).  The retain breaks the alias before `neverc_string_assign`
       runs, so the helper takes the "owned src with no aliasing"
       fast path -- frees the old buffer, installs the retain copy.
       A regression that drops the retain (or the assign that
       freed the source before reading it) would dangle here. */
    {
        string s = "self" + "dotted";
        s.assign(s);
        if (!neverc_string_eq(s, "selfdotted"))
            return 24;
        if (s.cap == 0)
            return 25;
    }

    /* 16) `s.assign(count, ch)` edge cases: count == 0 and the
       saturating "count > MAX_LEN" guard both collapse to the empty
       sentinel without leaking the previous buffer.  Both branches
       share the same "assign empty" code path in
       `neverc_string_assign_char`, so a regression that dropped the
       previous-buffer release would show up as a leaked allocation
       in the arena pressure loop below. */
    {
        string s = "previous" + "owned";
        s.assign(0, '*');
        if (!s.empty())
            return 26;
        if (s.cap != 0)
            return 27;
    }
    {
        string s = "previous" + "owned";
        s.assign(NEVERC_STRING_NPOS, '*');  /* count > MAX_LEN -> empty */
        if (!s.empty())
            return 28;
    }

    /* 17) `s.assign(count, ch)` arena pressure: tight loop with a
       successful char-fill, then back-to-back to a different fill,
       confirms the assign path releases the previous buffer at every
       step (otherwise the kernel arena's smaller budget would OOM
       the parallel kernel-mode mirror). */
    {
        string s = "init" + "ial";
        for (int i = 0; i < 64; i++) {
            s.assign(4, 'A');
            if (!neverc_string_eq(s, "AAAA"))
                return 29;
            s.assign(2, 'B');
            if (!neverc_string_eq(s, "BB"))
                return 30;
        }
    }

    return 0;
}
