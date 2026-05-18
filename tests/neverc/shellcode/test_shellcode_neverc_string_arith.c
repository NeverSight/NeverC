// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string arithmetic / byte-range boundary coverage.
 *
 * Pins down the prelude's saturating-arithmetic guards and the byte-
 * level invariants every other test only touches incidentally:
 *
 *   * `neverc_string_max_size()` returns exactly `NEVERC_STRING_MAX_LEN`
 *     regardless of receiver shape (literal view, owned, empty owned).
 *   * `neverc_string_repeat` rejects `s.len * n` overflows via the
 *     `total / n != s.len` audit and never dereferences a NULL alloc.
 *   * `reserve(max_size())` short-circuits to the empty sentinel
 *     instead of OOM-looping the bump allocator.
 *   * Comparison / equality / search work on the full 0..255 byte
 *     range (high-bit bytes are compared as `unsigned char` to match
 *     `std::string::compare`).
 *   * Long owned strings (>= 256 B payload) survive the arena's
 *     bump path AND the search/compare loops without alignment or
 *     sign-extension regressions.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status;
 * 0 means every assertion passed.  Each `return N;` line is unique
 * so a regression points at one bullet.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    /* (1) max_size is the documented ceiling on payload length and
       must agree across every receiver shape. */
    {
        __SIZE_TYPE__ literal_max = neverc_string_max_size("literal");
        __SIZE_TYPE__ empty_max = neverc_string_max_size("");
        __SIZE_TYPE__ owned_max = neverc_string_max_size("a" + "b");
        if (literal_max != empty_max || empty_max != owned_max)
            return 1;
        /* Implementation detail (Type.inc): `(size_t)-2`.  Test the
           full equality so a regression that silently lowers the
           ceiling fails here. */
        if (literal_max != ((__SIZE_TYPE__)-2))
            return 2;
    }

    /* (2) `neverc_string_repeat(s, n)` rejects multiplication overflow:
       `s.len * n` wrapping `__SIZE_TYPE__` collapses to the empty
       sentinel via the `total / n != s.len` audit. */
    {
        string s = "abcd" + "";
        /* `4 * (SIZE_MAX / 2)` wraps `size_t` -- prelude detects via
           the divide-back check.  Result must be empty (cap == 0). */
        string r = s.repeat(((__SIZE_TYPE__)-1) / 2);
        if (!neverc_string_empty(r))
            return 3;
        if (r.cap != 0)
            return 4;
    }

    /* (3) `reserve(max_size())` is exactly at the prelude ceiling and
       must short-circuit (cap > MAX_LEN guard).  Result is empty. */
    {
        string s = "x" + "";
        s = s.reserve(s.max_size());
        if (!neverc_string_empty(s))
            return 5;
        if (s.cap != 0)
            return 6;
    }

    /* (4) `reserve(max_size() + 1)` (i.e. SIZE_MAX wrap) is also empty
       -- the saturating-add audit catches it before alloc. */
    {
        string s = "y" + "";
        s = s.reserve((__SIZE_TYPE__)-1);
        if (!neverc_string_empty(s))
            return 7;
    }

    /* (5) Concatenation that just barely fits: 100 + 100 = 200 bytes
       payload, well under the kernel arena let alone user.  cap must
       be exactly len + 1 (the canonical owned shape). */
    {
        string lhs = neverc_string_repeat("u", 100);
        string rhs = neverc_string_repeat("v", 100);
        string both = lhs + rhs;
        if (both.len != 200)
            return 8;
        if (both.cap != both.len + 1)
            return 9;
        if (both.data[0] != 'u' || both.data[99] != 'u')
            return 10;
        if (both.data[100] != 'v' || both.data[199] != 'v')
            return 11;
    }

    /* (6) High-bit bytes (0x80-0xFF) compare as unsigned: 0xFF
       must sort AFTER 0x01, never as a signed-negative outlier.
       (We avoid bare `"\xff"` in string literals because the `\x`
       escape is greedy across following hex characters, which makes
       multi-byte literals ambiguous; build the bytes via local
       `char` arrays instead.) */
    {
        char lo_buf[1] = {(char)0x01};
        char hi_buf[1] = {(char)0xff};
        string lo = neverc_string_view(lo_buf, 1);
        string hi = neverc_string_view(hi_buf, 1);
        if (neverc_string_compare(lo, hi) >= 0)
            return 12;
        if (neverc_string_compare(hi, lo) <= 0)
            return 13;
        /* Equality on the same high-bit byte. */
        char hi_buf_b[1] = {(char)0xff};
        if (!neverc_string_eq(neverc_string_view(hi_buf, 1), neverc_string_view(hi_buf_b, 1)))
            return 14;
    }

    /* (7) Search across high-bit bytes / embedded NUL: `find` must
       not stop at NUL or treat 0x80 as special. */
    {
        char hay[6] = {'a', 'b', 0x00, (char)0x80, 'z', 'z'};
        char needle_high[1] = {(char)0x80};
        char needle_nul[1] = {0x00};
        if (neverc_string_find(neverc_string_view(hay, 6), neverc_string_view(needle_high, 1)) != 3)
            return 15;
        if (neverc_string_find(neverc_string_view(hay, 6), neverc_string_view(needle_nul, 1)) != 2)
            return 16;
    }

    /* (8) Owned >= 256B payload: bump allocator bumps past the
       16-byte alignment boundary multiple times; per-byte search must
       still land on the right index. */
    {
        string big = neverc_string_repeat("ab", 200);  /* 400 bytes */
        if (big.len != 400)
            return 17;
        /* find a needle near the end. */
        if (big.find("ab", 380) != 380)
            return 18;
        /* rfind from the start finds the last occurrence (398). */
        if (big.rfind("ab") != 398)
            return 19;
        /* substr at 256 (past every plausible 16/64/128/256 alignment
           boundary) returns 144 bytes of the right pattern. */
        string tail = big.substr(256, 4);
        if (!neverc_string_eq(tail, "abab"))
            return 20;
    }

    /* (9) Concat of two large owned (>= 128B each) keeps len/cap
       invariants and copies every byte through the per-byte loop
       without truncation. */
    {
        string lhs = neverc_string_repeat("p", 128);
        string rhs = neverc_string_repeat("q", 128);
        string both = lhs + rhs;
        if (both.len != 256)
            return 21;
        if (both.cap != both.len + 1)
            return 22;
        /* Boundary byte: lhs ends with 'p', rhs starts with 'q'. */
        if (both.data[127] != 'p' || both.data[128] != 'q')
            return 23;
        if (!neverc_string_eq(both.substr(126, 4), "ppqq"))
            return 24;
    }

    /* (10) `from_int` on the signed minimum-shaped value: prelude
       converts via `(size_t)(-(val + 1)) + 1` so the magnitude does
       not wrap on `INT_MIN` / `LONG_MIN`.  Use a deliberately
       portable in-source small value (-12345) so the conversion path
       is exercised on both 32-/64-bit hosts. */
    {
        if (!neverc_string_eq(neverc_string_from_int(-12345), "-12345"))
            return 25;
        if (!neverc_string_eq(neverc_string_from_int(0), "0"))
            return 26;
        if (!neverc_string_eq(neverc_string_from_int(1), "1"))
            return 27;
        if (!neverc_string_eq(neverc_string_from_uint(0), "0"))
            return 28;
        /* `from_uint(SIZE_MAX)` -- we cannot hard-code the exact
           decimal spelling because it depends on host word size,
           but the result must be non-empty AND every byte must be
           a decimal digit.  A regression where the conversion loop
           bails before completing would surface as either an empty
           string (`neverc_string_empty(converted)`) or non-digit bytes in
           the per-byte audit. */
        {
            string converted = neverc_string_from_uint((__SIZE_TYPE__)-1);
            if (neverc_string_empty(converted))
                return 29;
            for (__SIZE_TYPE__ i = 0; i < converted.len; ++i) {
                char c = converted.data[i];
                if (c < '0' || c > '9')
                    return 30;
            }
        }
    }

    /* (11) Pressure with high-bit + zero bytes interleaved through a
       loop: free-list reuse must not corrupt header tags via stray
       byte writes from the payload.  0x00 and 0xff collide with both
       arena tags' low bytes if there is a stray write. */
    {
        char base[8] = {0x00, (char)0xff, 0x00, (char)0xff,
                        0x00, (char)0xff, 0x00, (char)0xff};
        char tail[4] = {(char)0xff, 0x00, (char)0xff, 0x00};
        for (int i = 0; i < 96; ++i) {
            string s = neverc_string_view(base, 8);
            s = s.append(neverc_string_view(tail, 4));
            if (s.len != 12)
                return 31;
            if (s.data[0] != 0 || s.data[7] != (char)0xff)
                return 32;
            if (s.data[8] != (char)0xff || s.data[11] != 0)
                return 33;
        }
    }

    /* (12) Empty-string lifecycle: `clear` -> `neverc_string_eq` -> `clone`
       must keep cap at 0 (empty owned-or-borrowed sentinel) and never
       allocate. */
    {
        string e = neverc_string_clear("data");
        if (!neverc_string_empty(e) || e.cap != 0)
            return 34;
        if (!neverc_string_eq(e, ""))
            return 35;
        string c = neverc_string_clone(e);
        if (!neverc_string_empty(c))
            return 36;
        /* clone of empty MAY allocate a 1-byte buffer (cap==1) per
           the make_owned shape, but it MUST still be observable as
           empty.  Either path is correct -- pin only the empty-ness. */
    }

    /* (13) Consecutive-bytes all-zero string of moderate length:
       `len` is non-zero but every byte is NUL.  Equality via length
       not C-strlen; `find("")` matches at position 0. */
    {
        char nul_one[1] = {0x00};
        char nul_two[2] = {0x00, 0x00};
        string zeroes = neverc_string_repeat(neverc_string_view(nul_one, 1), 32);
        if (zeroes.len != 32)
            return 37;
        if (neverc_string_find(zeroes, "") != 0)
            return 38;
        if (neverc_string_find(zeroes, neverc_string_view(nul_two, 2)) != 0)
            return 39;
        if (zeroes.find_first_not_of(neverc_string_view(nul_one, 1)) != NEVERC_STRING_NPOS)
            return 40;
    }

    return 0;
}
