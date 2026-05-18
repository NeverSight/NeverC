// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string capacity helpers.
 *
 * Drives the std::string-parity capacity API:
 *
 *   * reserve(n)        -- post-condition cap >= max(n, len) + 1
 *   * shrink_to_fit()   -- collapse cap back to len + 1
 *   * max_size()        -- constant ceiling on payload length
 *   * capacity()        -- dispatched as `neverc_string_capacity(&s)` so the
 *                          retain copy Sema otherwise inserts cannot
 *                          flatten the reserved headroom to `len + 1`
 *                          before the helper observes it.
 *
 * Capacity transitions are observable through the `s.cap` field (the
 * runtime exposes the layout through Type.inc), so the test reads
 * `cap` and `len` directly to confirm post-conditions instead of
 * relying on a side-channel runtime helper.  Both dotted-method and
 * free-standing call forms must agree on the result.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status;
 * 0 means every assertion passed.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    /* (1) Owned RHS + reserve(64): cap must grow to at least 65 and
       the live payload must still match the source bytes. */
    {
        string s = "ab" + "cd";
        s = s.reserve(64);
        if (s.len != 4)
            return 1;
        if (s.cap < 65)
            return 2;
        if (!neverc_string_eq(s, "abcd"))
            return 3;
    }

    /* (2) reserve(n) when n < len: capacity never drops below len + 1. */
    {
        string s = "longer string" + "";  /* len == 13 */
        s = s.reserve(2);
        if (s.len != 13)
            return 4;
        if (s.cap < 14)
            return 5;
        if (!neverc_string_eq(s, "longer string"))
            return 6;
    }

    /* (3) shrink_to_fit() restores cap == len + 1. */
    {
        string s = "ab" + "cd";
        s = s.reserve(64);
        if (s.cap < 65)
            return 7;
        s = s.shrink_to_fit();
        if (s.cap != s.len + 1)
            return 8;
        if (!neverc_string_eq(s, "abcd"))
            return 9;
    }

    /* (4) max_size returns the implementation-defined ceiling. */
    {
        string s = "x" + "";
        if (s.max_size() < 1024)
            return 10;
    }

    /* (5) Free-standing call forms agree with the dotted spelling. */
    {
        string s = "ef" + "gh";
        s = neverc_string_reserve(s, 32);
        if (s.cap < 33)
            return 11;
        s = neverc_string_shrink_to_fit(s);
        if (s.cap != s.len + 1)
            return 12;
        if (!neverc_string_eq(s, "efgh"))
            return 13;
    }

    /* (6) reserve from a borrowed view materialises an owned buffer
       with the requested headroom; cap was 0 going in. */
    {
        string borrowed = neverc_string_view("ab", 2);
        if (borrowed.cap != 0)
            return 14;
        borrowed = borrowed.reserve(16);
        if (borrowed.cap < 17)
            return 15;
        if (!neverc_string_eq(borrowed, "ab"))
            return 16;
    }

    /* (7) Reserved capacity survives a shrink + reserve cycle. */
    {
        string s = "tag" + "";
        s = s.reserve(128);
        s = s.shrink_to_fit();
        s = s.reserve(64);
        if (s.cap < 65)
            return 17;
        if (!neverc_string_eq(s, "tag"))
            return 18;
    }

    /* (8) Pressure: alternating reserve / shrink in a tight loop must
       keep the arena bounded via free-list reuse. */
    for (int i = 0; i < 64; ++i) {
        string s = "loop" + "";
        s = s.reserve(128);
        s = s.shrink_to_fit();
        if (!neverc_string_eq(s, "loop"))
            return 19;
    }

    /* (9) Capacity reflects the make_owned path even after append:
       the result of `+` always owns a fresh buffer with cap == len+1. */
    {
        string s = "x" + "y";
        if (s.cap != s.len + 1)
            return 20;
        s += "z";
        if (s.cap != s.len + 1)
            return 21;
    }

    /* (10) max_size is a strict upper bound so reserve must reject
       a request beyond it: prelude returns the empty sentinel on
       too-large requests, the live payload then becomes "" with
       cap == 0. */
    {
        string s = "abc" + "";
        s = s.reserve(s.max_size());
        if (!neverc_string_empty(s))
            return 22;
        if (s.cap != 0)
            return 23;
    }

    /* (11) `s.capacity()` reports the true reserved storage even
       across a `reserve(n)` step: Sema dispatches the dotted call
       through `MethodReceiverKind::Receiver`, so the helper sees
       `&s` instead of a retain-flattened copy.  This is the whole
       point of the by-pointer signature -- a by-value helper would
       always answer `s.len` because `__neverc_string_retain`
       materialises a fresh `cap == len + 1` clone before dispatch. */
    {
        string s = "ab" + "cd";
        if (s.capacity() < s.len)
            return 24;
        s = s.reserve(64);
        if (s.capacity() < 64)
            return 25;
        if (s.capacity() != s.cap - 1)
            return 26;
    }

    /* (12) `s.capacity()` on a borrowed view: the helper falls back
       to `s.len` so std::string's `capacity() >= size()` contract is
       preserved on literal-derived views (`cap == 0`). */
    {
        string borrowed = neverc_string_view("hello", 5);
        if (borrowed.cap != 0)
            return 27;
        if (borrowed.capacity() != borrowed.len)
            return 28;
    }

    /* (13) `s.capacity()` after `shrink_to_fit()` collapses to
       `len`: post-shrink `cap == len + 1`, so the helper subtracts
       the trailing-NUL slot and returns the live payload length. */
    {
        string s = "abc" + "def";
        s = s.reserve(64);
        s = s.shrink_to_fit();
        if (s.capacity() != s.len)
            return 29;
        if (!neverc_string_eq(s, "abcdef"))
            return 30;
    }

    /* (14) Free-standing `neverc_string_capacity(&s)` agrees with the dotted
       form -- the helper signature is `(const string *)` and the
       method dispatch is the only thing that auto-wraps the receiver
       in a `&` UnaryOp; manual `&s` reaches the same helper. */
    {
        string s = "tag" + "";
        s = s.reserve(48);
        if (neverc_string_capacity(&s) != s.capacity())
            return 31;
        if (neverc_string_capacity(&s) < 48)
            return 32;
    }

    /* (15) `s.capacity()` reuse pressure: the helper itself must not
       allocate (no retain, no buffer churn), so a tight loop reading
       capacity should never grow the arena. */
    {
        string s = "loop" + "";
        s = s.reserve(96);
        for (int i = 0; i < 256; ++i) {
            if (s.capacity() < 96)
                return 33;
        }
    }

    return 0;
}
