// RUN: %neverc -std=c23 -DNEVERC_STRING_ALLOC=__neverc_oom_alloc -DNEVERC_STRING_FREE=__neverc_oom_free %s -o %t && %t
/* test_neverc_string_format_oom.c -- pin the by-value consume contract
 * on the format helper's failure paths.
 *
 * Pass 1 of `neverc_string_format` deliberately reads each `%S` arg
 * without releasing it (pass 2 owns the single release so the same
 * struct copy can be re-read without dangling the heap buffer).  When
 * the budget overflows or the allocator returns NULL we never reach
 * pass 2, so the helper MUST drain the variadic list itself --
 * otherwise every owned-string the caller handed in would leak.
 *
 * Variadic-arg lifetime convention
 * --------------------------------
 *
 * Sema's `GatherArgumentsForCall` inserts an implicit
 * `__neverc_string_retain` wrapper for every NeverC `string` lvalue
 * handed to a runtime helper through the `...` tail (see the
 * `IsNeverCStringRuntime` block alongside DefaultVariadicArgumentPromotion).
 * That removes the "you MUST clone() variadic %S args" trap that
 * an earlier revision exposed.  This test still spells every `%S`
 * argument as `arg.clone()` because the explicit prvalue gives us
 * a precise allocation-size handle for the OOM threshold tuning
 * below: the size-keyed allocator returns NULL on the first request
 * `>= g_oom_min_size`, so we want every retain copy to be a fixed
 * known size (the prvalue's clone alloc) rather than relying on
 * sema's implicit-retain alloc shape.  Both spellings funnel
 * through the same release_args drain on the failure tail, so the
 * lifetime invariant the test pins (zero leaks under OOM) holds
 * either way.
 *
 * Allocator hook
 * --------------
 *
 * The arena allocator below is real malloc / real free with one
 * knob: any allocation request whose size is `>= g_oom_min_size`
 * returns NULL while `g_oom_armed != 0`, then disarms the flag.
 * We pick `g_oom_min_size` large enough that every retain copy
 * sema inserts on the non-variadic args (the format-receiver
 * itself, plus the `clone()` call's internal alloc) slips
 * through, but the format-internal output buffer alloc fails.
 * Each test case disarms after firing once so a stray subsequent
 * allocation cannot cascade into a different test case.
 *
 * Verified end-to-end through `leaks --atExit` -- every byte
 * allocated for a `%S` arg must be released.  An empty result
 * with the original args still alive on the heap would surface
 * as a leak report and trip the gate.
 */

/* ===== Scripted allocator with size-keyed OOM toggle ===== */

static __SIZE_TYPE__ g_oom_min_size = 0;
static int          g_oom_armed    = 0;

void *__neverc_oom_alloc(__SIZE_TYPE__ n) {
    if (g_oom_armed && n >= g_oom_min_size) {
        g_oom_armed = 0;
        return (void *)0;
    }
    return __builtin_malloc(n);
}

void __neverc_oom_free(void *p) {
    if (p) __builtin_free(p);
}

static void arm_oom(__SIZE_TYPE__ min) {
    g_oom_min_size = min;
    g_oom_armed    = 1;
}

int main(void) {
    int r = 0;

    /* ===== Case (1): single %S, OOM on output buffer =====
       arg.clone() materialises a 200-byte owned prvalue that the
       format helper consumes by value.  Caller does not also
       release it (no lvalue on the cleanup chain) so when format
       OOMs and drains the arg through the release_args helper
       we exit cleanly with zero leaks.  Threshold 200 keeps the
       arg's clone alloc (201) and the receiver's literal-retain
       (9) both well under the bar; format's output budget
       (literal "echo: " + 200 -> alloc 207) trips the OOM. */
    {
        string arg = ("0123456789" "0123456789" "0123456789" "0123456789"
                      "0123456789" "0123456789" "0123456789" "0123456789"
                      "0123456789" "0123456789" "0123456789" "0123456789"
                      "0123456789" "0123456789" "0123456789" "0123456789"
                      "0123456789" "0123456789" "0123456789"
                      "0123456789").clone();
        if (arg.len() != 200) r = 1;
        arm_oom(202);
        string out = "echo: %S".format(arg.clone());
        if (out.len != 0) r = 1;
        if (g_oom_armed) r = 1;
    }

    /* ===== Case (2): two %S args, both must drain ===== */
    {
        string a = ("alpha-padding-text-must-be-100-bytes-or-so-to-avoid-"
                    "tripping-the-tiny-retain-window-aaaaaaaaaaaa").clone();
        string b = ("bravo-padding-text-must-be-100-bytes-or-so-to-avoid-"
                    "tripping-the-tiny-retain-window-bbbbbbbbbbbb").clone();
        if (a.len() < 90 || b.len() < 90) r = 1;
        /* Each clone alloc is 100ish; format budget is fmt + 2*arg
           ~= 5 + 200 = 205, alloc 206.  Threshold 150 lets the
           per-arg clones through and trips the format alloc. */
        arm_oom(150);
        string out = "%S/%S".format(a.clone(), b.clone());
        if (out.len != 0) r = 1;
        if (g_oom_armed) r = 1;
    }

    /* ===== Case (3): mixed specs interleaved with %S ===== */
    {
        string s1 = "name-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".clone();
        string s2 = "value-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb".clone();
        if (s1.len() < 30 || s2.len() < 30) r = 1;
        /* Per-arg clones ~42, format budget ~ 80, alloc 81+.
           Threshold 60: both clones (43) under, format alloc trips. */
        arm_oom(60);
        string out = "key=%S, val=%S, idx=%d".format(
            s1.clone(), s2.clone(), 42);
        if (out.len != 0) r = 1;
        if (g_oom_armed) r = 1;
    }

    /* ===== Case (4): %S whose payload was allocated through the
                       same scripted allocator -- proves the drain
                       works regardless of where the inner buffer
                       came from. */
    {
        string big = "0123456789ABCDEF".repeat(8);
        if (big.len() != 128) r = 1;
        /* clone alloc 129, format budget = 4 + 128 = 132, alloc 133.
           Threshold 130: clone (129) under, format alloc trips. */
        arm_oom(130);
        string out = "big=%S".format(big.clone());
        if (out.len != 0) r = 1;
        if (g_oom_armed) r = 1;
    }

    /* ===== Case (5): no OOM -- normal path still works after the
                       previous cases armed and disarmed the toggle.
                       Catches a sticky-flag regression. */
    {
        if ("ok=%d".format(7) != "ok=7") r = 1;
        string greet = "world".clone();
        if ("hi %S".format(greet.clone()) != "hi world") r = 1;
    }

    /* ===== Case (6): forged invalid %S arg under OOM.
                       `forged.clone()` of a (NULL, 99) view goes
                       through `__neverc_string_retain` ->
                       `__neverc_string_make_owned(NULL, 99)`,
                       which short-circuits the forged shape and
                       returns the empty sentinel.  Pass 1 reads
                       the empty arg (skips length budget); pass 2
                       allocates the literal "[%S, %S]" -> "[, , ]"
                       output and OOMs.  Drain releases both args
                       (one empty + one big) without ever touching
                       NULL. */
    {
        char tail_pad[256];
        for (int i = 0; i < 256; i++) tail_pad[i] = (char)('a' + (i % 26));
        string padded = neverc_string_view(tail_pad, 256).clone();
        string forged = neverc_string_view((const char *)0, 99);
        if (padded.len() != 256) r = 1;
        /* Padded clone 257, format budget = 4 + 0 + 256 = 260.
           Threshold 258: padded clone (257) under, format alloc
           (261) trips. */
        arm_oom(258);
        string out = "[%S, %S]".format(forged.clone(), padded.clone());
        if (out.len != 0) r = 1;
        if (g_oom_armed) r = 1;
    }

    /* ===== Case (7): OOM with no %S args at all.  The drain path
                       walks the variadic list without touching
                       `neverc_string_free` (no `%S` slots).  Pins
                       the no-op-drain branch for `%d` / `%s` only.
                       Use a long `%s` cstring so format's output
                       budget is well above any retain copy. */
    {
        const char *long_cstr =
            "very-long-ascii-payload-that-makes-the-format-budget-"
            "comfortably-larger-than-any-retain-copy-so-the-only-"
            "alloc-that-trips-is-the-format-internal-output-buffer-"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
        /* fmt is short (literal borrow, no retain alloc); format
           output budget = ~2 + long_cstr.len ~= 230; alloc ~231.
           Threshold 200: short retains all under, format alloc
           trips. */
        arm_oom(200);
        string out = "%s".format(long_cstr);
        if (out.len != 0) r = 1;
        if (g_oom_armed) r = 1;
    }

    printf("test_neverc_string_format_oom: %s\n",
           r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}
