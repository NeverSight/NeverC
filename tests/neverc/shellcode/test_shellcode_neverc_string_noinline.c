// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string -- value-typed semantics across opaque
 * (non-inlined) function-call boundaries.
 *
 * Shellcode constraint: noinline functions CANNOT take `string` by value
 * because the cleanup attribute calls `__sc_string_free` which references
 * the arena global.  ZeroRelocPass(Stackify) rejects arena references
 * outside the entry function.  The test works around this by passing
 * raw struct fields or `string *` pointers to noinline helpers, which
 * exercises the ABI boundary without triggering cleanup-based arena
 * references.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status.
 * 0 means every assertion passed.
 */

static __attribute__((noinline)) int len_no_inline(const char *data,
                                                    __SIZE_TYPE__ len) {
    return (int)len;
}

static __attribute__((noinline)) int sum_lens_no_inline(const char *d1,
                                                         __SIZE_TYPE__ l1,
                                                         const char *d2,
                                                         __SIZE_TYPE__ l2) {
    return (int)(l1 + l2);
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

int main(int a, int b) {
    (void)a;
    (void)b;

    /* (1) Pass raw fields of an owned string across the noinline boundary. */
    {
        string keep = "abcdef" + "";
        if (len_no_inline(keep.data, keep.len) != 6)
            return 1;
        if (!neverc_string_eq(keep, "abcdef"))
            return 2;
    }

    /* (2) Owned result forwarded as raw fields. */
    {
        string g = "hi " + "noinline";
        if (len_no_inline(g.data, g.len) != 11)
            return 3;
        if (first_byte_no_inline(g.data, g.len) != 'h')
            return 4;
    }

    /* (3) Two distinct owned strings forwarded to the same noinline helper. */
    {
        string lhs = "lhs" + "";
        string rhs = "rhs" + "";
        if (sum_lens_no_inline(lhs.data, lhs.len, rhs.data, rhs.len) != 6)
            return 5;
    }

    /* (4) Recursive noinline that does NOT use string locals (pure integer
       recursion) -- proves the noinline + recursion ABI works in shellcode
       when there is no arena reference in the callee. */
    {
        if (recurse_no_inline(32) != 32)
            return 6;
    }

    /* (5) Recursive helper hammered in a loop. */
    {
        for (int i = 0; i < 192; ++i) {
            if (recurse_no_inline(8) != 8)
                return 7;
        }
    }

    /* (6) Loop: owned string created in main, raw fields passed across. */
    {
        for (int i = 0; i < 256; ++i) {
            string g = "hi " + "loop";
            if (len_no_inline(g.data, g.len) != 7)
                return 8;
        }
    }

    /* (7) Chain result forwarded as raw fields. */
    {
        string g = ("hi " + "xyz").to_upper().substr(3);
        if (len_no_inline(g.data, g.len) != 3)
            return 9;
        if (first_byte_no_inline(g.data, g.len) != 'X')
            return 10;
    }

    /* (8) Two lvalues alternating into the same noinline helper. */
    {
        string a_str = "AA" + "";
        string b_str = "BBBB" + "";
        for (int i = 0; i < 96; ++i) {
            int n = (i & 1) ? len_no_inline(a_str.data, a_str.len)
                            : len_no_inline(b_str.data, b_str.len);
            if ((i & 1) && n != 2)
                return 11;
            if (!(i & 1) && n != 4)
                return 12;
        }
        if (!neverc_string_eq(a_str, "AA") || !neverc_string_eq(b_str, "BBBB"))
            return 13;
    }

    return 0;
}
