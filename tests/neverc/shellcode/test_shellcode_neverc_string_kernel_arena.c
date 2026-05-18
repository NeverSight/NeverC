// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string arena behaviour stress in ring-0 shellcode mode.
 *
 * Compile-only mirror of `test_shellcode_neverc_string_arena.c` for the
 * smaller 4 KB kernel arena (`StringRuntimeABI::KernelArenaSize`).  The
 * pipeline path is identical -- StringRuntimePass installs the same
 * `__sc_string_alloc` / `__sc_string_free` rewrite, ZeroRelocPass moves
 * the arena and metadata globals into the entry function's stack frame,
 * KernelImportPass leaves the helpers alone via `BuiltinString::isRuntimeFunctionName`
 * -- but the arena budget is much tighter, so a regression that
 * silently disabled free-list reuse would surface here as an OOM-style
 * empty-sentinel return well before the user-mode mirror noticed.
 *
 * The body never reaches the loader; the test asserts that the
 * shellcode pipeline accepts every helper on the kernel arena.
 */
int shellcode_entry(int seed) {
    /* (1) Free-list reuse contract on the kernel arena. */
    {
        const char *freed = 0;
        {
            string s = "abc" + "def";
            freed = s.data;
        }
        string second = "uvw" + "xyz";
        if (second.data != freed)
            return seed + 1;
        if (!neverc_string_eq(second, "uvwxyz"))
            return seed + 2;
    }

    /* (2) OOM short-circuit on a request larger than both the kernel
       arena AND the prelude's MAX_LEN cap.  Prelude collapses to the
       empty sentinel without dereferencing a NULL allocation. */
    {
        string s = "tiny" + "";
        s = s.reserve((__SIZE_TYPE__)1024 * 1024 * 1024);
        if (!neverc_string_empty(s))
            return seed + 3;
        if (s.cap != 0)
            return seed + 4;
    }

    /* (3) Reserve cap >= n + 1 even on the smaller kernel arena, when
       the request still fits comfortably. */
    {
        string s = "ab" + "cd";
        s = s.reserve(48);
        if (s.cap < 49)
            return seed + 5;
        if (s.len != 4)
            return seed + 6;
    }

    /* (4) shrink_to_fit collapses cap to len + 1, even after a reserve
       that bumped cap higher. */
    {
        string s = "ab" + "cd";
        s = s.reserve(48);
        s = s.shrink_to_fit();
        if (s.cap != s.len + 1)
            return seed + 7;
    }

    /* (5) Borrowed view stays cap == 0; the arena should never see it. */
    {
        string view = neverc_string_view("kernel-borrowed", 15);
        if (view.cap != 0)
            return seed + 8;
    }

    /* (6) Tight loop: 256 iters * 8-byte payload requires 2 KB if
       reuse were broken -- which is half the 4 KB kernel arena.  With
       free-list reuse the working set stays a single block. */
    for (int i = 0; i < 256; ++i) {
        string loop = "iter" + "step";
        if (!neverc_string_eq(loop, "iterstep"))
            return seed + 9;
    }

    /* (7) Differently-sized owned strings interleaved on the kernel
       budget: bump path + free-list path must not corrupt each
       other's headers. */
    for (int i = 0; i < 32; ++i) {
        string small = "abc" + "def";
        string big = neverc_string_repeat("kk", 8);
        if (!neverc_string_eq(small, "abcdef"))
            return seed + 10;
        if (big.len != 16)
            return seed + 11;
    }

    /* (8) clear + append cycle on the kernel arena: empty sentinel
       transition stays leak-free. */
    {
        string s = "abc" + "def";
        s = neverc_string_clear(s);
        if (!neverc_string_empty(s) || s.cap != 0)
            return seed + 12;
        s = s.append("rebuilt");
        if (s.cap == 0)
            return seed + 13;
        if (!neverc_string_eq(s, "rebuilt"))
            return seed + 14;
    }

    /* (9) Self-assign keeps the buffer alive on the kernel arena too. */
    {
        string s = "self" + "alias";
        const char *before = s.data;
        s = s;
        if (s.data != before)
            return seed + 15;
        if (!neverc_string_eq(s, "selfalias"))
            return seed + 16;
    }

    /* (10) Reserve+shrink cycle on the kernel arena: cap follows the
       explicit growth/shrink sequence without leaking blocks. */
    {
        string s = "tag" + "";
        for (int i = 0; i < 24; ++i) {
            s = s.reserve(48);
            if (s.cap < 49)
                return seed + 17;
            s = s.shrink_to_fit();
            if (s.cap != s.len + 1)
                return seed + 18;
        }
        if (!neverc_string_eq(s, "tag"))
            return seed + 19;
    }

    return seed;
}
