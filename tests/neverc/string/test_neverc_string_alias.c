// RUN: %neverc -std=c23 %s -o %t && %t
/* test_neverc_string_alias.c -- self-aliased ownership pin-down.
 *
 * Hammers every shape where the same `string` value reaches a builtin
 * runtime helper twice -- as both arguments, as receiver-and-argument,
 * as src/dst of an assign, etc.  These are the paths where a missing
 * retain copy would either double-free the underlying buffer or
 * dangle one of the two consumers.
 *
 * Sema's contract is that every NeverC `string` lvalue handed to a
 * runtime helper goes through `PerformCopyInitialization`, which
 * lowers to an implicit `__neverc_string_retain` -- producing an
 * INDEPENDENT owned buffer per argument slot.  The whitelist in
 * `BuiltinStringLValueDirectHelpers.def` skips that retain only for
 * helpers that explicitly do NOT consume the input (`neverc_string_cstr`,
 * `__neverc_string_retain`, and `neverc_string_free` inside the runtime).
 *
 * If that retain ever regressed, the leaks --atExit gate downstream
 * would fire on the missing release; the byte-level assertions in
 * this file would fire on a use-after-free reading random heap bytes.
 *
 * Coverage:
 *
 *   (1) `s = s` -- self-assign of the same lvalue.  The runtime detects
 *       the dst==src fast path and keeps the buffer alive.
 *
 *   (2) `s = s + s` -- self-cat through `__neverc_string_cat` then
 *       `__neverc_string_assign`.  Both retain copies feed the
 *       allocator, the cat result becomes the new `s`, the old `s`
 *       is released exactly once.
 *
 *   (3) `s.append(s)` / `neverc_string_append(s, s)` -- dotted call and
 *       direct prefixed call with the same lvalue twice.  Each
 *       argument slot must receive its own retained owned buffer.
 *
 *   (4) `s.replace_all(s, s)` -- the most adversarial shape: the
 *       same lvalue is the receiver, the `from` pattern, AND the `to`
 *       replacement at once.  Tests that retain works for three
 *       consumer slots in a row.
 *
 *   (5) `s.assign(s.substr(0, k))` -- assign FROM a slice of self.
 *       `s.substr(...)` returns an OWNED slice, so the assign helper
 *       sees `(dst=&s, src=owned_slice)` where src is a fresh buffer
 *       but the bytes it copied came from `s`.
 *
 *   (6) IC family with self-aliased args -- `s.eq_ic(s)`, `s.find_ic(s)`,
 *       etc.  Same retain contract; the helper must release both
 *       owned copies on every exit path.
 *
 *   (7) Loop hammer: 4096 iterations of self-aliased mutation so
 *       arena pressure surfaces any latent leak.
 */

extern int printf(const char *, ...);

int main(void) {
    int r = 0;

    /* ===== (1) Self-assign ===== */
    {
        string s = "Hello".clone();
        s = s;
        if (s != "Hello") r = 1;
        if (s.len != 5) r = 1;
        /* `s` going out of scope releases the (still single) buffer. */
    }

    /* ===== (2) Self-cat through assign ===== */
    {
        string s = "abc".clone();
        s = s + s;
        if (s != "abcabc") r = 1;
        if (s.len != 6) r = 1;
    }
    {
        string s = "Hello".clone();
        s += s;
        if (s != "HelloHello") r = 1;
    }
    /* Triple cat with mixed self-alias: `s + s + s`. */
    {
        string s = "x".clone();
        s = s + s + s;
        if (s != "xxx") r = 1;
    }

    /* ===== (3) Self-append via dotted + prefixed ===== */
    {
        string s = "ab".clone();
        s = s.append(s);
        if (s != "abab") r = 1;
    }
    {
        string s = "ab".clone();
        s = neverc_string_append(s, s);
        if (s != "abab") r = 1;
    }

    /* ===== (4) replace_all with self-aliased pattern + replacement ===== */
    {
        /* `s.replace_all(s, s)` with s = "ab" should replace every
           occurrence of "ab" with "ab", i.e. yield s unchanged. */
        string s = "ab".clone();
        s = s.replace_all(s, s);
        if (s != "ab") r = 1;
    }
    {
        /* `s.replace_all(s, s)` with s = "abab" should replace every
           occurrence of "abab" with "abab" -> still "abab". */
        string s = "abab".clone();
        s = s.replace_all(s, s);
        if (s != "abab") r = 1;
    }

    /* ===== (5) assign from owned slice of self ===== */
    {
        /* `s.substr(0, 3)` returns an OWNED slice (fresh allocation
           with copied bytes).  Assigning it back to `s` must not
           double-free or dangle.  The `__neverc_string_assign` alias
           branches `view_in_range` / `view_touches_allocation` only
           fire for borrowed views; an owned substr result is owned
           and takes the "owned src with no aliasing" fast path. */
        string s = "Hello, World".clone();
        s = s.substr(0, 5);
        if (s != "Hello") r = 1;
        if (s.len != 5) r = 1;
    }
    {
        /* substr -> assign chain on an owned buffer. */
        string s = "abcdef".clone();
        string t = s.substr(2, 3);
        if (t != "cde") r = 1;
        s = t;
        if (s != "cde") r = 1;
    }

    /* ===== (6) IC family with self-aliased args ===== */
    {
        string s = "Hello".clone();
        if (!s.eq_ic(s)) r = 1;
    }
    {
        string s = "Hello".clone();
        if (s.compare_ic(s) != 0) r = 1;
    }
    {
        string s = "Hello".clone();
        if (s.find_ic(s) != 0) r = 1;
    }
    {
        string s = "Hello".clone();
        if (!s.contains_ic(s)) r = 1;
    }
    {
        string s = "Hello".clone();
        if (!s.starts_with_ic(s)) r = 1;
        if (!s.ends_with_ic(s)) r = 1;
    }
    /* Direct prefixed self-alias paths */
    {
        string s = "Hello".clone();
        if (neverc_string_eq_ic(s, s) != 1) r = 1;
    }
    {
        string s = "abc".clone();
        if (neverc_string_compare_ic(s, s) != 0) r = 1;
    }

    /* ===== (7) Loop hammer: 4096 self-aliased mutations ===== */
    {
        string s = "X".clone();
        for (int i = 0; i < 12; i++) {
            /* s := s + s -- doubles the length each iteration.
               After 12 doublings, len == 4096.  Each iteration goes
               through `__neverc_string_cat(retain(s), retain(s))`
               followed by `__neverc_string_assign(&s, cat_result)`
               which releases the old `s`.  Any missed retain or
               missed release would either dangle the old buffer
               (heap-use-after-free on the next read) or leak the
               cat result (caught by leaks --atExit). */
            s = s + s;
        }
        if (s.len != 4096) r = 1;
        if (s.data[0] != 'X' || s.data[4095] != 'X') r = 1;
    }
    {
        string s = "ab".clone();
        for (int i = 0; i < 4096; i++) {
            /* s := s.replace_all(s, s) -- s replaced by self under
               self-as-needle => s unchanged each iteration, but
               every iteration touches the alias-handling paths in
               `__neverc_string_assign` and `neverc_string_replace_all`.
               If any path drops a retain, the buffer is reused-after-
               free and the next read crashes / drifts. */
            s = s.replace_all(s, s);
        }
        if (s != "ab") r = 1;
    }
    {
        /* Self-eq_ic in a tight loop: each iteration retains s twice,
           equates them, releases both retain copies.  Any missed
           release would leak 8 KB per 1000 iters; leaks --atExit
           catches it. */
        string s = "Header-Name".clone();
        int matches = 0;
        for (int i = 0; i < 4096; i++) {
            if (s.eq_ic(s)) matches++;
        }
        if (matches != 4096) r = 1;
    }

    /* ===== (8) Cross-aliased: receiver + argument from sibling locals
       of the same content.  Sema must NOT collapse the two retains
       into a single shared buffer just because the bytes are
       identical -- each lvalue feeds its own retain slot. ===== */
    {
        string s = "ABC".clone();
        string t = "ABC".clone();
        /* s and t are independent owned buffers with the same bytes. */
        if (!s.eq_ic(t)) r = 1;
        /* After eq_ic the two retain copies were released; s and t
           themselves are still alive and need to be cleaned up at
           scope exit.  No double-free even though bytes happen to
           match. */
        if (s != "ABC") r = 1;
        if (t != "ABC") r = 1;
    }

    printf("test_neverc_string_alias: %s\n",
           r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}
