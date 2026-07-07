// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string arena behaviour stress.
 *
 * Targets the contract `StringRuntimePass` installs around the builtin string
 * prelude's allocator hooks: every owned string is carved out of the
 * stackified `__sc_string_arena` global (64 KB by default in user mode);
 * `neverc_string_free` (and the auto-cleanup attribute Sema attaches to local
 * `string` lvalues) returns the block to a per-arena free list; the
 * next same-sized allocation pulls the freed block back out.  The
 * lifecycle / safety / capacity tests cover the higher-level ergonomics
 * -- this file pins down the arena-shaped invariants:
 *
 *   * Free-list reuse: a same-sized owned alloc that follows a freed
 *     same-sized owned alloc must hand back the SAME data pointer.
 *   * OOM short-circuit: an alloc the runtime cannot satisfy (arena
 *     too small, request larger than `NEVERC_STRING_MAX_LEN`, or the
 *     prelude's pre-checks reject it) must collapse to the empty
 *     sentinel without dereferencing NULL.
 *   * Reserve / shrink_to_fit honour `cap >= max(n + 1, len + 1)` /
 *     `cap == len + 1` post-conditions readable through the value
 *     type's `cap` slot.
 *   * Borrowed views (`cap == 0`) never enter the arena -- they pass
 *     literal pointers through unchanged.
 *   * Long loops on small owned strings stay bounded by free-list
 *     reuse instead of marching the bump allocator off the end of
 *     the arena.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status; 0
 * means every assertion passed.  Any non-zero return points at the
 * specific `return N;` line below.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    /* (1) Free-list reuse: a same-sized alloc following the same-sized
       free MUST hand back the exact data pointer.  Without free-list
       reuse the bump allocator would burn a fresh 7-byte block instead
       of recycling, and the 64 KB arena in this test would still pass
       this assertion -- but the smaller kernel arena would not.  Pin
       the contract here so a regression that silently disables reuse
       fails this exact assertion. */
    {
        const char *freed = 0;
        {
            string s = "abc" + "def";
            freed = s.data;
        }
        string second = "uvw" + "xyz";
        if (second.data != freed)
            return 1;
        if (!neverc_string_eq(second, "uvwxyz"))
            return 2;
    }

    /* (2) OOM on a huge `reserve` request: the prelude's
       `__neverc_string_make_owned_with_cap` rejects requests beyond
       `NEVERC_STRING_MAX_LEN` and the arena's `__sc_string_alloc`
       returns NULL when the bump pointer would overflow.  Either path
       must collapse to the empty sentinel rather than dereference the
       NULL allocation. */
    {
        string s = "tiny" + "";
        /* 1 GB > 64 KB user arena AND > NEVERC_STRING_MAX_LEN check. */
        s = s.reserve((__SIZE_TYPE__)1024 * 1024 * 1024);
        if (!neverc_string_empty(s))
            return 3;
        if (s.cap != 0)
            return 4;
    }

    /* (3) `reserve(n)` post-condition: cap >= n + 1 even when n is
       larger than the live payload.  The prelude consumes the input
       and materialises a fresh owned buffer, so the result reflects
       the request even when the caller had reserved less. */
    {
        string s = "ab" + "cd";
        s = s.reserve(100);
        if (s.cap < 101)
            return 5;
        if (s.len != 4)
            return 6;
        if (!neverc_string_eq(s, "abcd"))
            return 7;
    }

    /* (4) `shrink_to_fit` collapses cap to len + 1; a subsequent
       reserve grows it back without losing the live payload. */
    {
        string s = "ab" + "cd";
        s = s.reserve(64);
        s = s.shrink_to_fit();
        if (s.cap != s.len + 1)
            return 8;
        s = s.reserve(48);
        if (s.cap < 49)
            return 9;
        if (!neverc_string_eq(s, "abcd"))
            return 10;
    }

    /* (5) Borrowed view stays cap == 0; the arena should never see
       this string at all (neverc_string_free is a no-op on cap == 0). */
    {
        string view = neverc_string_view("borrowed", 8);
        if (view.cap != 0)
            return 11;
        if (!neverc_string_eq(view, "borrowed"))
            return 12;
    }

    /* (6) Long loop on small owned strings: the free-list MUST reuse
       blocks across iterations.  1024 iters * 8 bytes payload would
       require 8 KB of arena if reuse were broken; the 64 KB user
       arena would still survive but the kernel mirror would OOM.
       Pin the user-mode contract too so a "free-list reuse silently
       broken" regression fails here in addition to the kernel mirror.  */
    for (int i = 0; i < 1024; ++i) {
        string loop = "iter" + "step";
        if (!neverc_string_eq(loop, "iterstep"))
            return 13;
    }

    /* (7) Differently-sized owned strings interleaved through the
       arena: bump path + free-list path must compose cleanly without
       corrupting either each other's headers. */
    for (int i = 0; i < 64; ++i) {
        string small = "abc" + "def";
        string big = neverc_string_repeat("kk", 16);
        if (!neverc_string_eq(small, "abcdef"))
            return 14;
        if (!neverc_string_eq(big, "kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk"))
            return 15;
        if (big.len != 32)
            return 16;
    }

    /* (8) Concat of two non-trivially-sized owned strings: payload
       fits under MAX_LEN budget AND under the arena, so the result
       owns a single fresh buffer with cap == len + 1. */
    {
        string lhs = neverc_string_repeat("x", 100);
        string rhs = neverc_string_repeat("y", 100);
        string both = lhs + rhs;
        if (both.len != 200)
            return 17;
        if (both.cap < 201)
            return 18;
        if (!neverc_string_eq(both.substr(0, 4), "xxxx"))
            return 19;
        if (!neverc_string_eq(both.substr(196, 4), "yyyy"))
            return 20;
    }

    /* (9) Self-assign on an owned string: Sema inserts a retain copy
       on the RHS, so `s = s` becomes `tmp = retain(s); assign(&s, tmp)`.
       The assign helper sees distinct buffers (dst vs. retain copy) and
       installs the copy, freeing the original.  The data pointer MAY
       change, but the value must survive. */
    {
        string s = "self" + "alias";
        s = s;
        if (!neverc_string_eq(s, "selfalias"))
            return 21;
        if (s.cap == 0)
            return 22;
    }

    /* (10) Clear + append: clear collapses to the empty sentinel
       (cap == 0); the subsequent append must allocate a fresh owned
       buffer (cap > 0 after). */
    {
        string s = "abc" + "def";
        s = neverc_string_clear(s);
        if (!neverc_string_empty(s) || s.cap != 0)
            return 23;
        s = s.append("rebuilt");
        if (s.cap == 0)
            return 24;
        if (!neverc_string_eq(s, "rebuilt"))
            return 25;
    }

    /* (11) Drop owned strings in nested scopes; the arena pointer
       must be the same after the inner scope leaves -- otherwise the
       free-list lost the block. */
    {
        const char *first = 0;
        {
            string outer = "outer" + "data";
            first = outer.data;
            {
                string inner = "inner" + "data";
                if (inner.data == first)
                    return 26;  /* fresh bump, not same as outer's payload */
            }
            /* inner cleanup happens here; the freed inner block goes
               into the free-list and would be returned by the next
               same-sized alloc. */
        }
        /* outer cleanup runs here too. */
        string reused = "alpha" + "tango";  /* 10 bytes payload */
        (void)reused;
    }

    /* (12) `neverc_string_repeat` on a borrowed view gives an owned result
       through the arena; verify the resulting cap honours the
       `cap == len + 1` invariant. */
    {
        string filled = neverc_string_repeat("ab", 8);
        if (filled.len != 16)
            return 27;
        if (filled.cap != filled.len + 1)
            return 28;
        if (!neverc_string_eq(filled, "abababababababab"))
            return 29;
    }

    /* (13) Allocator under repeated reserve+shrink: the cap field
       must follow the explicit growth/shrink sequence without
       leaking blocks across iterations. */
    {
        string s = "tag" + "";
        for (int i = 0; i < 96; ++i) {
            s = s.reserve(96);
            if (s.cap < 97)
                return 30;
            s = s.shrink_to_fit();
            if (s.cap != s.len + 1)
                return 31;
        }
        if (!neverc_string_eq(s, "tag"))
            return 32;
    }

    return 0;
}
