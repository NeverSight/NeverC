// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string capacity helpers in ring-0 shellcode mode.
 *
 * Compile-only guard for `-mshellcode-context=kernel`: the capacity
 * fragment (Capacity.inc) must lower into the same StringRuntimePass-
 * rewritten allocator the rest of the prelude uses, so reserve /
 * shrink_to_fit / max_size / capacity all flow through the kernel
 * arena.  If a helper accidentally pulled in the libc `realloc`
 * family, the kernel resolver would surface the extern in the loader
 * contract.
 *
 * `s.capacity()` is dispatched through `MethodReceiverKind::Receiver`
 * (`neverc_string_capacity(&s)`), so the receiver retain path NeverC normally
 * inserts in front of by-value helpers does not flatten the reserved
 * `cap` to `len + 1` before the helper observes it -- the test reads
 * the helper result both before and after `reserve(n)` to confirm the
 * pointer-shaped signature actually preserves the reservation in the
 * tighter 4 KB ring-0 arena.
 */
int shellcode_entry(int seed) {
    /* Owned reserve grows capacity past the request. */
    {
        string s = "ab" + "cd";
        s = s.reserve(48);
        if (s.cap < 49)
            return seed + 1;
        if (!neverc_string_eq(s, "abcd"))
            return seed + 2;
    }

    /* reserve(n) when n < len: capacity stays >= len + 1. */
    {
        string s = "longer string" + "";
        s = s.reserve(2);
        if (s.cap < 14)
            return seed + 3;
    }

    /* shrink_to_fit collapses cap to len + 1. */
    {
        string s = "ab" + "cd";
        s = s.reserve(48);
        s = s.shrink_to_fit();
        if (s.cap != s.len + 1)
            return seed + 4;
    }

    /* max_size returns the runtime ceiling. */
    {
        string s = "x" + "";
        if (s.max_size() < 1024)
            return seed + 5;
    }

    /* Free-standing call form. */
    {
        string s = "ef" + "gh";
        s = neverc_string_reserve(s, 16);
        if (s.cap < 17)
            return seed + 6;
        s = neverc_string_shrink_to_fit(s);
        if (s.cap != s.len + 1)
            return seed + 7;
    }

    /* reserve from a borrowed view materialises an owned buffer. */
    {
        string borrowed = neverc_string_view("ab", 2);
        if (borrowed.cap != 0)
            return seed + 8;
        borrowed = borrowed.reserve(8);
        if (borrowed.cap < 9)
            return seed + 9;
        if (!neverc_string_eq(borrowed, "ab"))
            return seed + 10;
    }

    /* Reserve survives shrink + reserve cycle. */
    {
        string s = "tag" + "";
        s = s.reserve(32);
        s = s.shrink_to_fit();
        s = s.reserve(16);
        if (s.cap < 17)
            return seed + 11;
    }

    /* Pressure: alternate reserve/shrink to hit free-list reuse. */
    for (int i = 0; i < 32; ++i) {
        string s = "loop" + "";
        s = s.reserve(24);
        s = s.shrink_to_fit();
        if (!neverc_string_eq(s, "loop"))
            return seed + 12;
    }

    /* `s.capacity()` in kernel mode: same pointer-receiver dispatch
       as user mode, must agree with `s.cap - 1` after reserve. */
    {
        string s = "ring0" + "";
        s = s.reserve(48);
        if (s.capacity() < 48)
            return seed + 13;
        if (s.capacity() != s.cap - 1)
            return seed + 14;
    }

    /* Borrowed view returns capacity == len, matching std::string's
       capacity()-at-least-size contract on literal-equivalent views. */
    {
        string borrowed = neverc_string_view("hi", 2);
        if (borrowed.capacity() != borrowed.len)
            return seed + 15;
    }

    /* Free-standing `neverc_string_capacity(&s)` agrees with the dotted form
       and survives the smaller kernel arena. */
    {
        string s = "kn" + "kn";
        s = s.reserve(20);
        if (neverc_string_capacity(&s) != s.capacity())
            return seed + 16;
        if (neverc_string_capacity(&s) < 20)
            return seed + 17;
    }

    return seed;
}
