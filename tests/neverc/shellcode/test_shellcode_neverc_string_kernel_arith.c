// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string arithmetic / byte-range boundary coverage in
 * ring-0 shellcode mode.
 *
 * Compile-only mirror of `test_shellcode_neverc_string_arith.c` for
 * the smaller 4 KB kernel arena (`StringRuntimeABI::KernelArenaSize`).
 * The pipeline path is identical -- StringRuntimePass installs the
 * same `__sc_string_alloc` / `__sc_string_free` rewrite; the prelude's
 * saturating-arithmetic guards (max_size, repeat overflow,
 * reserve(max_size())) are arena-size-independent so they must compile
 * cleanly here too.  Loop / payload sizes are scaled down so the
 * kernel arena never legitimately OOMs along the way; what we care
 * about is that the arithmetic-edge prelude paths survive the
 * pipeline.
 */
int shellcode_entry(int seed) {
    /* (1) max_size constant under the kernel pipeline. */
    {
        if (neverc_string_max_size("k") != ((__SIZE_TYPE__)-2))
            return seed + 1;
    }

    /* (2) `repeat(SIZE_MAX/2)` overflow detection on the kernel arena. */
    {
        string s = "kk" + "";
        string r = s.repeat(((__SIZE_TYPE__)-1) / 2);
        if (!neverc_string_empty(r))
            return seed + 2;
    }

    /* (3) `reserve(max_size())` collapses to empty without dereferencing
       a NULL allocation. */
    {
        string s = "k" + "";
        s = s.reserve(s.max_size());
        if (!neverc_string_empty(s))
            return seed + 3;
    }

    /* (4) High-bit byte ordering (use char buffers to avoid the
       ambiguous `\x` greedy-hex parse). */
    {
        char lo_buf[1] = {(char)0x01};
        char hi_buf[1] = {(char)0xff};
        string lo = neverc_string_view(lo_buf, 1);
        string hi = neverc_string_view(hi_buf, 1);
        if (neverc_string_compare(lo, hi) >= 0)
            return seed + 4;
    }

    /* (5) Search across embedded NUL / high-bit bytes. */
    {
        char hay[4] = {'k', 0x00, (char)0x80, 'n'};
        char needle_high[1] = {(char)0x80};
        if (neverc_string_find(neverc_string_view(hay, 4),
                        neverc_string_view(needle_high, 1)) != 2)
            return seed + 5;
    }

    /* (6) Modest concat that fits the kernel arena (32 + 32 = 64 B
       payload + 17 B header + 16 B alignment slack ~= ~100 B; well
       under 4 KB). */
    {
        string lhs = neverc_string_repeat("k", 32);
        string rhs = neverc_string_repeat("n", 32);
        string both = lhs + rhs;
        if (both.len != 64)
            return seed + 6;
        if (both.cap != both.len + 1)
            return seed + 7;
        if (both.data[31] != 'k' || both.data[32] != 'n')
            return seed + 8;
    }

    /* (7) `from_int` / `from_uint` boundary on the kernel arena -- the
       int-conversion path uses a fixed `NEVERC_STRING_INT_BUF` scratch
       buffer and so does not balloon the kernel stack. */
    {
        if (!neverc_string_eq(neverc_string_from_int(-12345), "-12345"))
            return seed + 9;
        if (!neverc_string_eq(neverc_string_from_int(0), "0"))
            return seed + 10;
        if (!neverc_string_eq(neverc_string_from_uint(0), "0"))
            return seed + 11;
    }

    /* (8) Loop with high-bit + zero bytes interleaved: free-list reuse
       must keep the arena bounded across many iterations even on the
       smaller kernel budget. */
    {
        char base[4] = {0x00, (char)0xff, 0x00, (char)0xff};
        char tail[2] = {(char)0xff, 0x00};
        for (int i = 0; i < 32; ++i) {
            string s = neverc_string_view(base, 4);
            s = s.append(neverc_string_view(tail, 2));
            if (s.len != 6)
                return seed + 12;
            if (s.data[0] != 0 || s.data[5] != 0)
                return seed + 13;
        }
    }

    return seed;
}
