// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string memory-safety guard for kernel-context shellcode.
 *
 * Compile-only mirror of `test_shellcode_neverc_string_safety.c`: ensures
 * the lifetime / overlap / free-list paths the user-mode safety test
 * exercises also survive the tighter kernel arena (4 KB) and the
 * KernelImportPass reserved-helper list.  The body never reaches the
 * loader — the test only asserts that the shellcode pipeline accepts
 * every helper.
 */
int shellcode_entry(int seed) {
    /* Self-assign on an owned string. */
    {
        string s = "abc" + "def";
        s = s;
        if (!neverc_string_eq(s, "abcdef"))
            return seed + 1;
    }

    /* Self-assign on a borrowed literal view. */
    {
        string s = "kernel";
        s = s;
        if (!neverc_string_eq(s, "kernel"))
            return seed + 2;
    }

    /* Borrowed view pointing inside dst's owned allocation. */
    {
        string s = "alpha" + "beta";
        s = neverc_string_view(s.data + 2, 4);
        if (!neverc_string_eq(s, "phab"))
            return seed + 3;
    }

    /* substr produces an independent owned buffer. */
    {
        string s = "hello" + "world";
        string t = s.substr(5, 5);
        if (!neverc_string_eq(t, "world"))
            return seed + 4;
        if (!neverc_string_eq(s, "helloworld"))
            return seed + 5;
    }

    /* += chain — kernel arena must reuse blocks. */
    {
        string s = "k";
        for (int i = 0; i < 24; i++)
            s += "n";
        if (s.size() != 25)
            return seed + 6;
        if (s.front() != 'k' || s.back() != 'n')
            return seed + 7;
    }

    /* clone independence (and method form). */
    {
        string s = "kernel" + "";
        string c1 = neverc_string_clone(s);
        string c2 = s.clone();
        if (c1.data == s.data || c2.data == s.data)
            return seed + 8;
        if (!neverc_string_eq(c1, "kernel") || !neverc_string_eq(c2, "kernel") ||
            !neverc_string_eq(s, "kernel"))
            return seed + 9;
    }

    /* Deep method chain. */
    {
        string s = "  kernel arena  " + "";
        s = s.trim().to_upper().replace(0, 6, "DRIVER");
        if (!neverc_string_eq(s, "DRIVER ARENA"))
            return seed + 10;
    }

    /* Free-list reuse stress. */
    {
        for (int i = 0; i < 96; i++) {
            string loop = "k" + "n";
            if (!neverc_string_eq(loop, "kn"))
                return seed + 11;
        }
    }

    /* neverc_string_clear -> append cycle. */
    {
        string e = neverc_string_clear("data");
        if (!neverc_string_empty(e))
            return seed + 12;
        e = e.append("rebuilt");
        if (!neverc_string_eq(e, "rebuilt"))
            return seed + 13;
    }

    /* neverc_string_swap. */
    {
        string left = "left" + "";
        string right = "right" + "";
        neverc_string_swap(&left, &right);
        if (!neverc_string_eq(left, "right") || !neverc_string_eq(right, "left"))
            return seed + 14;
    }

    /* Resize cycle on the smaller kernel arena. */
    {
        string r = "kk";
        for (int i = 0; i < 24; i++) {
            r = r.resize(6, '+');
            if (!neverc_string_eq(r, "kk++++"))
                return seed + 15;
            r = r.resize(2, '?');
            if (!neverc_string_eq(r, "kk"))
                return seed + 16;
        }
    }

    /* Self-swap through the dotted-call dispatcher: `s.swap(s)` lowers
       to `neverc_string_swap(&s, &s)`.  Identity swap must keep the only
       owned buffer alive even on the smaller 4 KB kernel arena. */
    {
        string s = "ring" + "0";
        s.swap(s);
        if (!neverc_string_eq(s, "ring0"))
            return seed + 17;
        if (s.cap == 0)
            return seed + 18;
    }

    /* Self-assign through the dotted-call dispatcher: `s.assign(s)`
       lowers to `neverc_string_assign(&s, retain(s))` -- the retain
       materialises a fresh copy that the assign's "owned src with
       no aliasing" branch installs in place of the original. */
    {
        string s = "kn" + "self";
        s.assign(s);
        if (!neverc_string_eq(s, "knself"))
            return seed + 19;
        if (s.cap == 0)
            return seed + 20;
    }

    /* `s.assign(count, ch)` edge cases on the smaller arena: count == 0
       and count > MAX_LEN both funnel through the same `neverc_string_assign(
       dst, empty)` bail-out in `neverc_string_assign_char`, so neither branch
       can leak the previous owned buffer. */
    {
        string s = "kk" + "kk";
        s.assign(0, 'X');
        if (!s.empty())
            return seed + 21;
    }
    {
        string s = "kk" + "kk";
        s.assign(NEVERC_STRING_NPOS, 'X');
        if (!s.empty())
            return seed + 22;
    }

    /* `s.assign(count, ch)` arena pressure on the kernel arena: 32
       iterations in lockstep with the 4 KB budget guarantees the
       previous-buffer release fires at every step. */
    {
        string s = "kn" + "init";
        for (int i = 0; i < 32; i++) {
            s.assign(3, 'A');
            if (!neverc_string_eq(s, "AAA"))
                return seed + 23;
            s.assign(2, 'B');
            if (!neverc_string_eq(s, "BB"))
                return seed + 24;
        }
    }

    return seed;
}
