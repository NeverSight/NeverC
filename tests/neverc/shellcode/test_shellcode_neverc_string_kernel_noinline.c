// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string -- noinline boundary tests, ring-0 mirror.
 *
 * Compile-only kernel-context counterpart.  Same constraint as the
 * user-mode mirror: noinline functions cannot take `string` by value
 * (cleanup references the arena, which ZeroRelocPass rejects outside
 * the entry function).  Pass raw fields or use pure-integer noinline
 * helpers instead.
 */

static __attribute__((noinline)) int len_no_inline(const char *data,
                                                    __SIZE_TYPE__ len) {
    return (int)len;
}

static __attribute__((noinline)) int recurse_no_inline(int n) {
    if (n <= 0)
        return 0;
    return recurse_no_inline(n - 1) + 1;
}

static __attribute__((noinline)) char first_byte_no_inline(const char *data,
                                                            __SIZE_TYPE__ len) {
    return (len > 0 && data) ? data[0] : 0;
}

int shellcode_entry(int seed) {
    /* (1) Raw fields across noinline boundary. */
    {
        string keep = "ring0" + "";
        if (len_no_inline(keep.data, keep.len) != 5)
            return seed + 1;
        if (!neverc_string_eq(keep, "ring0"))
            return seed + 2;
    }

    /* (2) Owned result forwarded as raw fields. */
    {
        string g = "hi " + "kernel";
        if (len_no_inline(g.data, g.len) != 9)
            return seed + 3;
        if (first_byte_no_inline(g.data, g.len) != 'h')
            return seed + 4;
    }

    /* (3) Recursive noinline (pure integer). */
    {
        if (recurse_no_inline(16) != 16)
            return seed + 5;
    }

    /* (4) Recursive helper in a loop on the kernel arena. */
    {
        for (int i = 0; i < 64; ++i) {
            if (recurse_no_inline(8) != 8)
                return seed + 6;
        }
    }

    /* (5) Loop: owned string + raw fields across noinline. */
    {
        for (int i = 0; i < 96; ++i) {
            string g = "hi " + "loop";
            if (len_no_inline(g.data, g.len) != 7)
                return seed + 7;
        }
    }

    /* (6) Chain result forwarded as raw fields. */
    {
        string g = ("hi " + "ker").to_upper().substr(3);
        if (len_no_inline(g.data, g.len) != 3)
            return seed + 8;
        if (first_byte_no_inline(g.data, g.len) != 'K')
            return seed + 9;
    }

    /* (7) Two lvalues alternating. */
    {
        string a_str = "AA" + "";
        string b_str = "BBBB" + "";
        for (int i = 0; i < 64; ++i) {
            int n = (i & 1) ? len_no_inline(a_str.data, a_str.len)
                            : len_no_inline(b_str.data, b_str.len);
            if ((i & 1) && n != 2)
                return seed + 10;
            if (!(i & 1) && n != 4)
                return seed + 11;
        }
        if (!neverc_string_eq(a_str, "AA") || !neverc_string_eq(b_str, "BBBB"))
            return seed + 12;
    }

    return seed;
}
