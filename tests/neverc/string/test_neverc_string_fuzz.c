// RUN: %neverc -std=c23 %s -o %t && %t
/* test_neverc_string_fuzz.c -- adversarial stress / hardening probe.
 *
 * Pins down the decoder strictness + arena lifetime contract in the
 * face of:
 *
 *   1. Large round-trip cycles (base64 / hex / url) on payloads of
 *      varying length.  Hammers the arena allocator with thousands
 *      of consecutive `string` temporaries; any leak / double-free
 *      / stale-pointer would surface as a crash, an `r = 1`
 *      mismatch, or a content drift after enough rotations.
 *
 *   2. Every malformed UTF-8 prefix shape (lone continuation,
 *      0xC0/0xC1 overlong start, truncated 2/3/4-byte sequence,
 *      surrogate-range encoding, codepoints above U+10FFFF,
 *      0xF5..0xFF prefixes).  Every shape MUST round-trip through
 *      `utf8_valid() == 0` without crashing and MUST NOT walk
 *      past the supplied length.
 *
 *   3. Strict-decode rejection on near-miss inputs that the lazy
 *      reader would otherwise silently consume:
 *        * base64: stray `=`, lone trailing `=`, non-multiple-of-4,
 *          three trailing `=`, leading `=`.
 *        * url / form: truncated `%`, half-truncated `%X`, non-hex
 *          escape body, trailing `%` in any position.
 *        * hex: odd length, non-hex digit anywhere in the buffer.
 *
 *   4. Forged / oversized handles.  `(len > 0, data == NULL)` is a
 *      shape only a corrupted caller could produce; every byte-level
 *      decoder MUST collapse it to the empty sentinel without
 *      dereferencing the NULL pointer.  `neverc_string_view(p, 0)`
 *      with non-NULL `p` is the legal "borrow nothing" handle and
 *      must round-trip to itself.
 *
 *   5. Saturation: `repeat` / `url_encode` of a payload that, after
 *      the worst-case 3x expansion, would exceed `MAX_LEN` MUST
 *      collapse to the empty sentinel rather than wrapping the
 *      length budget.  We probe near-max cases via repeat() because
 *      directly building a 2^63-byte input is not feasible at test
 *      time, but the saturating arithmetic guard inside the prelude
 *      uses the same `total > MAX_LEN - need` check so the smaller
 *      probe pins down the algorithm.
 *
 * Goal: every path completes without a crash AND produces the
 * documented sentinel/round-trip result.  Any silent corruption
 * (partial buffer, freed memory, stale pointer) would either
 * trip an `r = 1` match or be caught by the address-sanitizer
 * shim that the runtime arena pre-flights every free against.
 */

static unsigned __fuzz_lcg(unsigned *seed) {
    /* Numerical Recipes LCG -- deterministic, tiny, sufficient
       to walk a wide byte distribution without bringing in
       <stdlib.h>'s rand(). */
    *seed = (*seed) * 1664525u + 1013904223u;
    return *seed;
}

int main(void) {
    int r = 0;

    /* ===== (1) Arena hammer: 4096 base64 round-trips ===== */
    {
        unsigned seed = 0x11ABCDEFu;
        for (int iter = 0; iter < 4096; iter++) {
            unsigned len = __fuzz_lcg(&seed) % 64u;
            char buf[64];
            for (unsigned i = 0; i < len; i++)
                buf[i] = (char)__fuzz_lcg(&seed);
            string original = neverc_string_view(buf, len);
            string b64 = original.clone().to_base64();
            string back = b64.from_base64();
            if (back != original) r = 1;
        }
    }

    /* ===== (1b) Arena hammer: 2048 hex round-trips ===== */
    {
        unsigned seed = 0x22BEEFC0u;
        for (int iter = 0; iter < 2048; iter++) {
            unsigned len = __fuzz_lcg(&seed) % 48u;
            char buf[48];
            for (unsigned i = 0; i < len; i++)
                buf[i] = (char)__fuzz_lcg(&seed);
            string original = neverc_string_view(buf, len);
            string hex = original.clone().to_hex();
            if (hex.len != len * 2u) r = 1;
            string back = hex.from_hex();
            if (back != original) r = 1;
        }
    }

    /* ===== (1c) Arena hammer: 2048 url round-trips on UTF-8 ===== */
    {
        for (unsigned cp = 0; cp < 0x800u; cp += 1u) {
            string single = neverc_string_from_utf32_char(cp);
            if (single.len == 0) continue;
            string encoded = single.clone().url_encode();
            string back = encoded.url_decode();
            if (back != single) r = 1;
        }
    }

    /* ===== (2) Malformed UTF-8 shapes -- every category ===== */
    {
        { char b[] = { (char)0x80 };
          if (neverc_string_view(b, 1).utf8_valid()) r = 1; }
        { char b[] = { (char)0xC0, (char)0xAF };
          if (neverc_string_view(b, 2).utf8_valid()) r = 1; }
        { char b[] = { (char)0xC1, (char)0x80 };
          if (neverc_string_view(b, 2).utf8_valid()) r = 1; }
        { char b[] = { (char)0xC2 };
          if (neverc_string_view(b, 1).utf8_valid()) r = 1; }
        { char b[] = { (char)0xE2, (char)0x82 };
          if (neverc_string_view(b, 2).utf8_valid()) r = 1; }
        { char b[] = { (char)0xF0, (char)0x9F, (char)0x98 };
          if (neverc_string_view(b, 3).utf8_valid()) r = 1; }
        { char b[] = { (char)0xED, (char)0xA0, (char)0x80 };
          if (neverc_string_view(b, 3).utf8_valid()) r = 1; }
        { char b[] = { (char)0xF4, (char)0x90, (char)0x80, (char)0x80 };
          if (neverc_string_view(b, 4).utf8_valid()) r = 1; }
        { char b[] = { (char)0xF5 };
          if (neverc_string_view(b, 1).utf8_valid()) r = 1; }
        { char b[] = { (char)0xFF };
          if (neverc_string_view(b, 1).utf8_valid()) r = 1; }
        /* Mixed valid + invalid: validator MUST still report 0,
           tally counts every malformed byte as one codepoint. */
        { char b[] = { 'a', (char)0x80, 'b', (char)0xC0, 'c' };
          if (neverc_string_view(b, 5).utf8_valid()) r = 1;
          if (neverc_string_view(b, 5).utf8_count() != 5) r = 1; }
        /* 256 lone continuation bytes -- walks the validator past every
           one without overrunning len. */
        { char buf[256];
          for (int i = 0; i < 256; i++) buf[i] = (char)0x80;
          if (neverc_string_view(buf, 256).utf8_valid()) r = 1;
          if (neverc_string_view(buf, 256).utf8_count() != 256) r = 1; }
    }

    /* ===== (3) Strict decode rejection -- near-miss adversarial inputs ===== */
    {
        if ("Zm==Zg==".from_base64().len != 0) r = 1;
        if ("=AAA".from_base64().len != 0) r = 1;
        if ("Z===".from_base64().len != 0) r = 1;
        if ("ZmF".from_base64().len != 0) r = 1;
        if ("Zm9!".from_base64().len != 0) r = 1;
        if ("=Zg==".from_base64().len != 0) r = 1;

        if ("%".url_decode().len != 0) r = 1;
        if ("%2".url_decode().len != 0) r = 1;
        if ("%2g".url_decode().len != 0) r = 1;
        if ("%g2".url_decode().len != 0) r = 1;
        if ("foo%".url_decode().len != 0) r = 1;
        if ("foo%1".url_decode().len != 0) r = 1;
        if ("foo%1g".url_decode().len != 0) r = 1;
        if ("%".form_decode().len != 0) r = 1;
        if ("%g".form_decode().len != 0) r = 1;
        if ("%2g".form_decode().len != 0) r = 1;
        if ("ab%".form_decode().len != 0) r = 1;

        if ("a".from_hex().len != 0) r = 1;
        if ("g".from_hex().len != 0) r = 1;
        if ("ag".from_hex().len != 0) r = 1;
        if ("0g".from_hex().len != 0) r = 1;
        if ("0123456789abcdef0".from_hex().len != 0) r = 1;
    }

    /* ===== (4) Forged + borrow handles ===== */
    {
        /* Forged: (NULL, 99) is rejected by `neverc_string_view` and
           collapsed to the empty sentinel.  Every byte-level helper
           must short-circuit on the resulting empty handle without
           dereferencing the NULL.  An empty string is itself
           well-formed UTF-8 (RFC 3629 vacuously), so `utf8_valid()`
           returns 1 -- the safety property here is "we did not
           crash", not "we rejected the empty string". */
        string forged = neverc_string_view((const char *)0, 99);
        if (forged.len != 0) r = 1;
        if (forged.clone().to_base64().len != 0) r = 1;
        if (forged.clone().to_hex().len != 0) r = 1;
        if (forged.clone().url_encode().len != 0) r = 1;
        if (forged.clone().form_encode().len != 0) r = 1;
        if (forged.clone().from_base64().len != 0) r = 1;
        if (forged.clone().from_hex().len != 0) r = 1;
        if (forged.clone().url_decode().len != 0) r = 1;
        if (forged.clone().form_decode().len != 0) r = 1;
        if (forged.clone().utf8_count() != 0) r = 1;
        if (!forged.utf8_valid()) r = 1;

        /* Legal "borrow nothing": p != NULL, len == 0. */
        char tmp[1] = { 'x' };
        string empty_view = neverc_string_view(tmp, 0);
        if (empty_view.len != 0) r = 1;
        if (empty_view != "") r = 1;
        if (empty_view.utf8_count() != 0) r = 1;
        if (!empty_view.utf8_valid()) r = 1;
    }

    /* ===== (5) Saturation guard via repeat() + worst-case URL ===== */
    {
        /* repeat(npos) on a non-empty string MUST short-circuit
           through the empty sentinel rather than wrap the budget. */
        string r1 = "x".repeat(NEVERC_STRING_NPOS);
        if (r1.len != 0) r = 1;
        string r2 = "ab".repeat(100);
        if (r2.len != 200) r = 1;
        if (r2.substr(0, 2) != "ab") r = 1;
        if (r2.substr(198, 2) != "ab") r = 1;
        if ("ab".repeat(0).len != 0) r = 1;
        if ("".repeat(100).len != 0) r = 1;

        /* All-non-unreserved input forces 3x url_encode expansion.
           Hits the per-byte saturation check on every iteration. */
        char worst[256];
        for (int i = 0; i < 256; i++) worst[i] = (char)0xFF;
        string sat = neverc_string_view(worst, 256).url_encode();
        if (sat.len != 256u * 3u) r = 1;
        if (sat.url_decode() != neverc_string_view(worst, 256)) r = 1;
    }

    /* ===== (6) Cross-encoding chain on a tampered CJK payload =====
       Mix CJK + supplementary plane + ASCII + non-UTF-8 tamper bytes
       (0x80, 0xFF) and run base64 -> url_encode -> url_decode ->
       from_base64.  The base64 layer makes the bytes opaque so even
       a malformed UTF-8 substring round-trips cleanly. */
    {
        char src[] = u8"中文 mixed \x80 tamper \xFF end";
        unsigned src_len = 0;
        while (src[src_len] != '\0') src_len++;
        string original = neverc_string_view(src, src_len);
        string b64 = original.clone().to_base64();
        string urlsafe = b64.clone().url_encode();
        string back_b64 = urlsafe.url_decode();
        if (back_b64 != b64) r = 1;
        string back = back_b64.from_base64();
        if (back != original) r = 1;
    }

    /* ===== (7) Self-assignment + alias-aware lifetime =====
       Hammer every shape in `__neverc_string_assign`'s decision tree
       so any "release-then-install" inversion would dangle the bytes
       under leaks --atExit:
         * `s = s` on borrow + owned -- chained-self path.
         * `s = s.substr(...)` -- src bytes alias *dst's allocation,
           must promote BEFORE freeing dst.
         * `s += s` -- cat takes two retains so the second arg
           survives release of the first.
         * `s = s + sep` accumulator -- 32 iterations exercises the
           assign path repeatedly.
       Any byte-level lifetime regression here also gets flagged by
       the leaks --atExit gate further down. */
    {
        string sa = "hello";
        sa = sa;
        if (sa != "hello") r = 1;
        if (sa.len != 5u) r = 1;

        string sb = "x".repeat(5);
        sb = sb;
        if (sb != "xxxxx") r = 1;

        /* substr-into-self: classic alias trap.  prelude's assign
           helper detects the in-range view and copies bytes BEFORE
           releasing *dst's storage (see __neverc_string_assign_borrowed). */
        string sc = "abcdef".clone();
        sc = sc.substr(0, 3);
        if (sc != "abc") r = 1;

        /* `s += s`: cat(s, s) on the same handle.  The two operands
           are independent retains by the time __neverc_string_cat
           runs, so the second arg's bytes survive release of the
           first. */
        string sd = "ab".clone();
        sd += sd;
        if (sd != "abab") r = 1;
        sd += sd;
        if (sd != "abababab") r = 1;

        /* Accumulator loop: same `w` shows up on both sides 32 times. */
        string sw = "x";
        for (int i = 0; i < 32; i++)
            sw = sw + "y";
        if (sw.len != 33u) r = 1;
        if (sw[0] != 'x') r = 1;
        if (sw[32] != 'y') r = 1;

        /* `s = s.clone()`: src is fresh-owned (no aliasing), exercises
           the no-alias owned-takeover branch. */
        string se = "hello".clone();
        se = se.clone();
        if (se != "hello") r = 1;
    }

    /* ===== (8) Deep dotted-call chains on temporaries =====
       Each link is a fresh-owned temporary that the next dotted
       method consumes by value; if any helper drops its receiver
       without returning a fresh owned buffer, the chain would
       either crash on a freed pointer or leak under
       leaks --atExit. */
    {
        string a = "  HELLO  ".trim().to_lower().reverse();
        if (a != "olleh") r = 1;

        string b = "abc".repeat(3).substr(2, 4).to_upper();
        if (b != "CABC") r = 1;

        string c = "Zm9vYmFy".from_base64().to_hex();
        if (c != "666f6f626172") r = 1;

        /* 6-deep chain: trim -> lower -> reverse -> base64 -> base64 -> reverse. */
        string d = "  AbC  ".trim().to_lower().reverse()
                              .to_base64().from_base64().reverse();
        if (d != "abc") r = 1;

        /* Mutating dotted call on a literal returns a fresh owned buffer. */
        string e = "hello world".replace_all("world", "neverc");
        if (e != "hello neverc") r = 1;

        /* Encoding round-trip with intermediate concatenation -- every
           link transfers ownership to the next via the by-value
           consume contract. */
        string f = ("foo" + neverc_string_view("bar", 3))
                       .to_base64().from_base64();
        if (f != "foobar") r = 1;
    }

    /* ===== (9) Nested format =====
       `"%S"` consumes a `string` by value.  When the argument is
       itself the result of another `.format()` call, the outer
       format must take ownership exactly once -- a release-on-input
       AND release-on-pass-2 would double-free; a missing release
       would leak.  Pin both ends of that contract. */
    {
        /* Two-deep nest: outer `%S` consumes the inner format result. */
        string a = "[%S]".format("(%S)".format("hi".clone()));
        if (a != "[(hi)]") r = 1;

        /* Three-deep nest. */
        string b = "<%S>".format("[%S]".format("(%S)".format("xx".clone())));
        if (b != "<[(xx)]>") r = 1;

        /* Mixed nest: inner format result + plain literal in the same
           outer call. */
        string c = "%S=%d".format("answer".clone(), 42);
        if (c != "answer=42") r = 1;

        /* Inner format with width/precision modifiers also flows
           through cleanly. */
        string d = "[%S]".format("%5d".format(7));
        if (d != "[    7]") r = 1;

        /* Nested format inside a concat operator: the temporary
           must be released by the cat helper, not leaked through
           the operator rewrite. */
        string e = "head=" + "%d".format(99) + ";tail=" + "%S".format("ok".clone());
        if (e != "head=99;tail=ok") r = 1;
    }

    printf("test_neverc_string_fuzz: %s\n",
           r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}
