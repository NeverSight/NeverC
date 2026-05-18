// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string std::string-parity dotted methods in ring-0
 * shellcode mode.
 *
 * Compile-only mirror of `test_shellcode_neverc_string_methods.c`:
 * the dotted-call dispatcher's pointer-shaped receivers
 * (`s.assign(t)` -> `neverc_string_assign(&s, t)`, `s.swap(t)` ->
 * `neverc_string_swap(&s, &t)`) must lower into the same StringRuntimePass-
 * rewritten allocator path as the rest of the prelude on the smaller
 * 4 KB kernel arena.  KernelImportPass would otherwise surface
 * `neverc_string_assign` / `neverc_string_swap` as unresolved kernel resolver calls.
 *
 * The body never reaches the loader -- the test only asserts that the
 * shellcode pipeline accepts every helper.
 */
int shellcode_entry(int seed) {
    /* Dotted assign with an owned src. */
    {
        string dst = "owned" + "old";
        string src = "owned" + "new";
        dst.assign(src);
        if (!neverc_string_eq(dst, "ownednew"))
            return seed + 1;
        if (dst.cap == 0)
            return seed + 2;
    }

    /* Dotted assign with a string literal: dst takes ownership. */
    {
        string dst = "owned" + "buf";
        dst.assign("literal");
        if (!neverc_string_eq(dst, "literal"))
            return seed + 3;
        if (dst.cap == 0)
            return seed + 4;
    }

    /* Dotted assign on a borrowed dst transitions to owned cleanly. */
    {
        string dst = neverc_string_view("hi", 2);
        if (dst.cap != 0)
            return seed + 5;
        dst.assign("kernel" + "src");
        if (!neverc_string_eq(dst, "kernelsrc"))
            return seed + 6;
        if (dst.cap == 0)
            return seed + 7;
    }

    /* Dotted assign in a kernel-arena-tight loop: free-list must keep
       per-iter sources bounded. */
    {
        string dst = "init" + "";
        for (int i = 0; i < 64; ++i) {
            string src = "iter" + "step";
            dst.assign(src);
            if (!neverc_string_eq(dst, "iterstep"))
                return seed + 8;
        }
    }

    /* Dotted swap on two owned strings: handles end up exchanged. */
    {
        string left = "left" + "side";
        string right = "right" + "side";
        const char *left_data_before = left.data;
        const char *right_data_before = right.data;
        left.swap(right);
        if (!neverc_string_eq(left, "rightside"))
            return seed + 9;
        if (!neverc_string_eq(right, "leftside"))
            return seed + 10;
        if (left.data != right_data_before)
            return seed + 11;
        if (right.data != left_data_before)
            return seed + 12;
    }

    /* Dotted swap mixing owned and borrowed. */
    {
        string owned = "owned" + "buf";
        string borrowed = neverc_string_view("borrowed", 8);
        owned.swap(borrowed);
        if (!neverc_string_eq(owned, "borrowed") || owned.cap != 0)
            return seed + 13;
        if (!neverc_string_eq(borrowed, "ownedbuf") || borrowed.cap == 0)
            return seed + 14;
    }

    /* Self dotted swap: no double-free, content intact. */
    {
        string s = "alias" + "";
        s.swap(s);
        if (!neverc_string_eq(s, "alias"))
            return seed + 15;
    }

    /* Dotted swap pressure on the smaller kernel arena. */
    {
        for (int i = 0; i < 64; ++i) {
            string a_str = "iter" + "a";
            string b_str = "iter" + "b";
            a_str.swap(b_str);
            if (!neverc_string_eq(a_str, "iterb") || !neverc_string_eq(b_str, "itera"))
                return seed + 16;
        }
    }

    /* Dotted assign of a method-call prvalue source. */
    {
        string dst = "seed" + "";
        dst.assign("hello " + "world");
        if (!neverc_string_eq(dst, "hello world"))
            return seed + 17;
    }

    return seed;
}
