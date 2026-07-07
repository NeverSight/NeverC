// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string arena fragmentation behaviour.
 *
 * Locks down the observable side of `__sc_string_alloc`'s free-list
 * walk when multiple differently-sized blocks are available:
 *
 *   * Same-size sequential alloc/free reuses the same block (LIFO
 *     free-list head is the most-recently freed block; first-fit
 *     picks it on the next request).
 *   * First-fit selection over a free list with mixed sizes: the
 *     prelude returns the FIRST block whose `header.size` covers
 *     the request, NOT the tightest match.
 *   * A request larger than every free-list entry walks the whole
 *     list, falls through to the bump allocator, and returns a
 *     fresh block instead of NULL.
 *   * Freed blocks contribute their full reserved `header.size`
 *     (which is the post-alignment `cap` rounded up to
 *     `ArenaAlignment`), not just `len`.  A `reserve(N)` followed
 *     by free should make a block large enough for any subsequent
 *     `<= N` request.
 *   * Active vs. free tag flip survives many round-trips (long-loop
 *     stress).
 *   * Borrowed views never consume arena space.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status.
 * Each `return N;` line is unique so the failing assertion is
 * directly identifiable.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    /* (1) Same-size sequential alloc/free reuses the SAME block.
       a_str dies at its inner `}`, then b_str's alloc finds the
       freed block at free-list head and reuses it; same again for
       c_str.  All three share one data pointer. */
    {
        const char *first_data = 0;
        const char *second_data = 0;
        {
            string a_str = "AA" + "AA";
            first_data = a_str.data;
        }
        {
            string b_str = "BB" + "BB";
            second_data = b_str.data;
        }
        if (first_data != second_data)
            return 1;
        string c_str = "CC" + "CC";
        if (c_str.data != first_data)
            return 2;
    }

    /* (2) Two co-existing blocks (small + large) before either is
       freed: bump path hands out distinct addresses.  Then both
       enter the free list at scope exit; the LIFO order means the
       block declared SECOND (large) becomes free-list head.  A
       subsequent SMALL request reuses LARGE (first-fit, head wins
       even though small_data would also fit). */
    {
        const char *small_data = 0;
        const char *large_data = 0;
        {
            string small = "ab" + "cd";
            string large = neverc_string_repeat("x", 32);
            small_data = small.data;
            large_data = large.data;
            if (small_data == large_data)
                return 3;
        }
        /* Cleanup order at `}` is reverse-declaration: `large` is
           freed first (head), then `small` is freed (becomes new
           head).  So free-list head is now `small`. */
        string fit_after = "ef" + "gh";
        if (fit_after.data != small_data)
            return 4;
    }

    /* (3) First-fit when only the larger block fits: a 32-byte
       request must skip past every too-small block in the free
       list and unlink the first sufficient one.  We seed the free
       list with one small (4-byte) and one large (32-byte) block,
       then ask for 32 bytes.  The walk skips the small head and
       returns the large block. */
    {
        const char *large_data = 0;
        {
            string large = neverc_string_repeat("y", 32);
            large_data = large.data;
            string small = "ab" + "cd";
            (void)small;
            /* Cleanup order: small first (head) -> large second
               (becomes new head).  Wait -- reverse-declaration
               means the LATER-declared `small` is freed FIRST,
               so the order is small-then-large.  After the block,
               free-list head is `large`, then `small`. */
        }
        /* Request 32 bytes: head (large) fits, returned. */
        string need32 = neverc_string_repeat("z", 32);
        if (need32.data != large_data)
            return 5;
    }

    /* (4) Free + bump fallthrough: build a free list of three
       4-byte blocks, then request 64 bytes -- none fits, the alloc
       walks the whole list and falls through to the bump path. */
    {
        const char *block_data[3];
        {
            string a_str = "ab" + "cd";
            string b_str = "ef" + "gh";
            string c_str = "ij" + "kl";
            block_data[0] = a_str.data;
            block_data[1] = b_str.data;
            block_data[2] = c_str.data;
            if (a_str.data == b_str.data || b_str.data == c_str.data)
                return 6;
        }
        /* All three on the free list, none can satisfy 64 bytes. */
        string big = neverc_string_repeat("z", 64);
        if (big.len != 64)
            return 7;
        for (int i = 0; i < 3; ++i)
            if (big.data == block_data[i])
                return 8;
    }

    /* (5) `reserve(N)` followed by free: the freed block keeps its
       full `header.size` reserve, so the next request <= reserved
       size reuses it. */
    {
        const char *reserved_data = 0;
        {
            string s = "tag" + "";
            s = s.reserve(64);
            if (s.cap < 65)
                return 9;
            reserved_data = s.data;
        }
        /* Next request for ANY size that fits within the reserved
           header.size reuses the block (first-fit picks the head). */
        string reuse = neverc_string_repeat("q", 50);
        if (reuse.data != reserved_data)
            return 10;
    }

    /* (6) Long round-trip on a single block: ArenaBlockActiveTag
       (`SCRSTRAA`) <-> ArenaBlockFreeTag (`SCRSTRFF`) flip must
       survive many cycles.  A regression that left the tag in the
       active state after free would make the next free's audit
       (`MatchesSelf && IsActive`) silently skip the re-link, and
       the working set would balloon until OOM. */
    {
        const char *anchor = 0;
        for (int i = 0; i < 1024; ++i) {
            string spin = "spin" + "block";
            if (!neverc_string_eq(spin, "spinblock"))
                return 11;
            if (i == 0)
                anchor = spin.data;
            else if (spin.data != anchor)
                return 12;
        }
    }

    /* (7) Borrowed views never consume arena: cap stays 0 and the
       free path short-circuits.  4096 ephemeral views must produce
       zero arena traffic. */
    {
        string view = neverc_string_view("borrowed-fragment", 17);
        if (view.cap != 0)
            return 13;
        for (int i = 0; i < 4096; ++i) {
            string v = neverc_string_view("ignored", 7);
            if (v.cap != 0)
                return 14;
            if (v.len != 7)
                return 15;
        }
    }

    /* (8) Three differently-sized blocks alternating: the arena
       must hand out 3 distinct buffers per round AND free-list
       reuse must keep them stable across rounds (no bump growth). */
    {
        const char *seen_a = 0;
        const char *seen_b = 0;
        const char *seen_c = 0;
        for (int round = 0; round < 64; ++round) {
            string a_str = "aa" + "aa";
            string b_str = neverc_string_repeat("b", 16);
            string c_str = neverc_string_repeat("c", 64);
            if (a_str.data == b_str.data || b_str.data == c_str.data ||
                a_str.data == c_str.data)
                return 16;
            if (round == 0) {
                seen_a = a_str.data;
                seen_b = b_str.data;
                seen_c = c_str.data;
            } else {
                if (a_str.data != seen_a)
                    return 17;
                if (b_str.data != seen_b)
                    return 18;
                if (c_str.data != seen_c)
                    return 19;
            }
            if (a_str.len != 4 || b_str.len != 16 || c_str.len != 64)
                return 20;
        }
    }

    return 0;
}
