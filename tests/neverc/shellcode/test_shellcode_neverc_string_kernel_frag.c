// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string arena fragmentation behaviour in ring-0
 * shellcode mode.
 *
 * Compile-only mirror of `test_shellcode_neverc_string_frag.c` for
 * the smaller 4 KB kernel arena (`StringRuntimeABI::KernelArenaSize`).
 * Same first-fit / LIFO contracts; payload sizes stay small so the
 * arena's bump path never legitimately OOMs along the way.  The body
 * never reaches the loader -- the test asserts the shellcode
 * pipeline accepts every helper on the kernel arena.
 */
int shellcode_entry(int seed) {
    /* (1) Same-size sequential alloc/free reuses the same block. */
    {
        const char *first_data = 0;
        {
            string a_str = "kk" + "nn";
            first_data = a_str.data;
        }
        string b_str = "mm" + "pp";
        if (b_str.data != first_data)
            return seed + 1;
    }

    /* (2) Small + large coexisting then freed: LIFO head wins
       first-fit.  Reverse-declaration cleanup order means the
       LATER-declared block sits at free-list head. */
    {
        const char *small_data = 0;
        const char *large_data = 0;
        {
            string small = "ab" + "cd";
            string large = neverc_string_repeat("x", 24);
            small_data = small.data;
            large_data = large.data;
            if (small_data == large_data)
                return seed + 2;
        }
        /* free order: large then small.  Head = small. */
        string fit_after = "ef" + "gh";
        if (fit_after.data != small_data)
            return seed + 3;
        (void)large_data;
    }

    /* (3) `reserve(N)` followed by free: the freed block keeps its
       reserved header.size for subsequent reuse. */
    {
        const char *reserved_data = 0;
        {
            string s = "kt" + "";
            s = s.reserve(48);
            if (s.cap < 49)
                return seed + 4;
            reserved_data = s.data;
        }
        string reuse = neverc_string_repeat("q", 32);
        if (reuse.data != reserved_data)
            return seed + 5;
    }

    /* (4) Active <-> free tag flip survives a long round-trip on a
       single block.  Smaller iter count to fit the kernel test
       budget; same shape as the user-mode test. */
    {
        const char *anchor = 0;
        for (int i = 0; i < 256; ++i) {
            string spin = "spn" + "blk";
            if (!neverc_string_eq(spin, "spnblk"))
                return seed + 6;
            if (i == 0)
                anchor = spin.data;
            else if (spin.data != anchor)
                return seed + 7;
        }
    }

    /* (5) Borrowed views never consume arena -- 1024 ephemeral
       views must produce zero arena traffic. */
    {
        for (int i = 0; i < 1024; ++i) {
            string v = neverc_string_view("ignored", 7);
            if (v.cap != 0)
                return seed + 8;
            if (v.len != 7)
                return seed + 9;
        }
    }

    /* (6) Three differently-sized blocks alternating: arena hands
       out 3 distinct buffers per round AND keeps them stable across
       rounds.  All sizes deliberately small for the kernel arena. */
    {
        const char *seen_a = 0;
        const char *seen_b = 0;
        const char *seen_c = 0;
        for (int round = 0; round < 32; ++round) {
            string a_str = "aa" + "aa";
            string b_str = neverc_string_repeat("b", 8);
            string c_str = neverc_string_repeat("c", 24);
            if (a_str.data == b_str.data || b_str.data == c_str.data ||
                a_str.data == c_str.data)
                return seed + 10;
            if (round == 0) {
                seen_a = a_str.data;
                seen_b = b_str.data;
                seen_c = c_str.data;
            } else {
                if (a_str.data != seen_a)
                    return seed + 11;
                if (b_str.data != seen_b)
                    return seed + 12;
                if (c_str.data != seen_c)
                    return seed + 13;
            }
        }
    }

    return seed;
}
