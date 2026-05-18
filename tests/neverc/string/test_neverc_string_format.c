// RUN: %neverc -std=c23 %s -o %t && %t
/* test_neverc_string_format.c -- variadic dotted-call format helper.
 *
 * Pins down `s.format(fmt, ...)` (and the explicit
 * `neverc_string_format(fmt, ...)` spelling) across the documented
 * printf-style spec subset:
 *
 *   %d / %i  -- int
 *   %u       -- unsigned int
 *   %ld %lu  -- long / unsigned long
 *   %lld %llu-- long long / unsigned long long
 *   %x %lx
 *   %llx     -- unsigned hex of the matching width
 *   %s       -- const char *  (NULL prints "(null)")
 *   %S       -- NeverC `string` (consumed)
 *   %c       -- int promoted to a single byte
 *   %p       -- void * (lowercase hex with `0x` prefix)
 *   %%       -- literal %
 *
 * Receiver `fmt` is consumed by the helper (by-value contract), so
 * `"name={}".format(x)` does not leak the format literal even on
 * chained calls.
 */

int main(void) {
    int r = 0;

    /* ===== Plain literals + no specifiers (round-trip) ===== */
    if ("hello".format() != "hello") r = 1;
    if ("".format() != "") r = 1;

    /* ===== %d / %i / %u ===== */
    if ("answer=%d".format(42) != "answer=42") r = 1;
    if ("neg=%d".format(-7) != "neg=-7") r = 1;
    if ("min=%d".format(-2147483648) != "min=-2147483648") r = 1;
    if ("hex-int=%i".format(0xFF) != "hex-int=255") r = 1;
    if ("u=%u".format(4000000000u) != "u=4000000000") r = 1;
    if ("zero=%d".format(0) != "zero=0") r = 1;

    /* ===== %lld / %llu / %lx / %llx ===== */
    if ("big=%lld".format(9223372036854775807LL) != "big=9223372036854775807") r = 1;
    /* INT64_MIN -- the off-by-one negation guard pins this case. */
    if ("min64=%lld".format(-9223372036854775807LL - 1) != "min64=-9223372036854775808") r = 1;
    if ("u64=%llu".format(18446744073709551615ULL) != "u64=18446744073709551615") r = 1;
    if ("hex32=%x".format(0xCAFEBABEu) != "hex32=cafebabe") r = 1;
    if ("hex64=%llx".format(0xDEADBEEFCAFEBABEULL) != "hex64=deadbeefcafebabe") r = 1;

    /* ===== %s (C string) + NULL ===== */
    if ("greet %s!".format("world") != "greet world!") r = 1;
    if ("v=[%s]".format("") != "v=[]") r = 1;
    if ("nil=[%s]".format((const char *)0) != "nil=[(null)]") r = 1;

    /* ===== %S (NeverC string consumed) ===== */
    {
        string name = "world";
        if ("hi %S".format(name.clone()) != "hi world") r = 1;
        /* Original `name` is still alive after the format call --
           only the clone was consumed. */
        if (name.len() != 5) r = 1;
    }
    /* Empty string arg */
    if ("[%S]".format("".clone()) != "[]") r = 1;
    /* CJK round-trip */
    if ("hi %S!".format(u8"世界".clone()) != u8"hi 世界!") r = 1;

    /* ===== %S with a bare lvalue (no manual clone) =====
     *
     * Sema inserts an implicit `__neverc_string_retain` copy for
     * every NeverC `string` lvalue handed to a runtime helper's
     * variadic tail.  This restores parity with the non-variadic
     * dotted call surface (`s.append(t)` / `s == t` etc. don't
     * require a manual clone either) and avoids the silent
     * double-free that bare lvalues would otherwise trigger
     * against the caller's scope cleanup.  Mirrors the contract
     * exercised by `arg.clone()` cases above. */
    {
        string greeting = "world";  /* borrowed view */
        if ("hi %S".format(greeting) != "hi world") r = 1;
        /* `greeting` is still readable after the call -- the
           retain copy sema inserted is what got consumed. */
        if (greeting.len() != 5) r = 1;
        if (greeting != "world") r = 1;
    }
    {
        string owned_msg = "hello".clone();
        if ("[%S]".format(owned_msg) != "[hello]") r = 1;
        /* The owned `owned_msg` survives the format call because
           sema cloned it before passing.  Releasing it twice (the
           variadic helper already freed the retain copy; cleanup
           releases the original) would have been a double-free
           prior to the implicit-retain change. */
        if (owned_msg.len() != 5) r = 1;
    }
    {
        string a = "alpha";
        string b = "bravo";
        if ("%S/%S".format(a, b) != "alpha/bravo") r = 1;
        if (a != "alpha" || b != "bravo") r = 1;
    }

    /* ===== %c ===== */
    if ("chr=%c".format('Q') != "chr=Q") r = 1;
    if ("nl=[%c]".format('\n') != "nl=[\n]") r = 1;

    /* ===== %p ===== */
    {
        int dummy = 42;
        string s = "ptr=%p".format(&dummy);
        /* `%p` always emits `0x` + lowercase hex; we don't pin the
           exact address but we do pin the prefix and that the rest
           is hex-ish.  Use starts_with + length guard. */
        if (!s.starts_with("ptr=0x")) r = 1;
        if (s.len() < 7) r = 1;
    }
    /* NULL pointer prints as `0x0` (zero -> single hex digit). */
    if ("nil=%p".format((void *)0) != "nil=0x0") r = 1;

    /* ===== %% literal ===== */
    if ("100%%".format() != "100%") r = 1;
    if ("a%%b%%c".format() != "a%b%c") r = 1;

    /* ===== Mixed multi-spec ===== */
    if ("name=%s, age=%d, pct=%u%%".format("Alice", 30, 95u)
        != "name=Alice, age=30, pct=95%") r = 1;

    /* ===== Unrecognised spec emits literally ===== */
    /* `%z` is not in the recognised set -- emit `%z` verbatim
       without consuming a va_arg.  We follow it with a real `%d`
       to verify the va_list cursor did not skip ahead. */
    if ("%z=%d".format(7) != "%z=7") r = 1;
    /* Truncated `%` at end of fmt -- emitted as literal. */
    if ("hello%".format() != "hello%") r = 1;
    /* Truncated `%l` at end -- emitted as literal. */
    if ("trunc=%l".format() != "trunc=%l") r = 1;

    /* ===== Explicit prefixed call ===== */
    {
        string s = neverc_string_format("explicit %d %s", 42, "ok");
        if (s != "explicit 42 ok") r = 1;
    }

    /* ===== Chained on a string variable (consumes receiver) ===== */
    {
        string fmt = "[%d]";
        /* `fmt` is consumed by the format call.  After the call the
           receiver should not be re-used. */
        string out = fmt.format(99);
        if (out != "[99]") r = 1;
    }

    /* ===== Receiver is consumed -- chained format on temporary ===== */
    if ("a=%d, b=%d".clone().format(1, 2) != "a=1, b=2") r = 1;

    /* ===== Adversarial: empty fmt with args ===== */
    if ("".format(42, "ignored") != "") r = 1;

    /* ===== Forged handle short-circuits to empty ===== */
    {
        string forged = neverc_string_view((const char *)0, 99);
        string out = forged.format(42);
        if (out.len != 0) r = 1;
    }

    /* ===== Long pure-literal payload (stress arena) ===== */
    {
        /* 256-byte payload, no specifiers -- exercises the
           "no spec" fast lane in the budget pass. */
        string hay = "abcdefghijklmnop".repeat(16);
        string out = hay.clone().format();
        if (out != hay) r = 1;
        if (out.len() != 256) r = 1;
    }

    /* ===== %S on the empty / forged input does not deref ===== */
    {
        string forged_arg = neverc_string_view((const char *)0, 5);
        if ("[%S]".format(forged_arg.clone()) != "[]") r = 1;
    }

    /* ===== Width: integer right-align (default) ===== */
    if ("[%5d]".format(42) != "[   42]") r = 1;
    if ("[%5d]".format(-42) != "[  -42]") r = 1;
    if ("[%5d]".format(123456) != "[123456]") r = 1;
    if ("[%5u]".format(123u) != "[  123]") r = 1;
    if ("[%5x]".format(0xabcu) != "[  abc]") r = 1;
    if ("[%5lld]".format(9223372036854775807LL) != "[9223372036854775807]") r = 1;

    /* ===== Width: left-align with `-` flag ===== */
    if ("[%-5d]".format(42) != "[42   ]") r = 1;
    if ("[%-5d]".format(-42) != "[-42  ]") r = 1;
    if ("[%-5d]".format(123456) != "[123456]") r = 1;
    if ("[%-5x]".format(0xfu) != "[f    ]") r = 1;

    /* ===== Width: zero-pad with `0` flag ===== */
    if ("[%05d]".format(42) != "[00042]") r = 1;
    /* Negative zero-pad: sign first, then zeros, then magnitude. */
    if ("[%05d]".format(-42) != "[-0042]") r = 1;
    if ("[%05d]".format(0) != "[00000]") r = 1;
    if ("[%08x]".format(0xCAFEu) != "[0000cafe]") r = 1;
    /* `-` flag overrides `0` flag (matches printf). */
    if ("[%-05d]".format(42) != "[42   ]") r = 1;

    /* ===== Width on %s: pad with space, `-` flips, `0` ignored ===== */
    if ("[%5s]".format("hi") != "[   hi]") r = 1;
    if ("[%-5s]".format("hi") != "[hi   ]") r = 1;
    /* `0` flag has no effect on string fields per printf. */
    if ("[%05s]".format("hi") != "[   hi]") r = 1;
    /* Width <= rendered length -- no padding. */
    if ("[%2s]".format("hello") != "[hello]") r = 1;

    /* ===== Precision on %s: byte-truncation + interaction with width ===== */
    if ("[%.3s]".format("hello") != "[hel]") r = 1;
    if ("[%.0s]".format("hello") != "[]") r = 1;
    if ("[%.10s]".format("hi") != "[hi]") r = 1;
    /* Width + precision: width applies AFTER precision truncation. */
    if ("[%10.3s]".format("hello") != "[       hel]") r = 1;
    if ("[%-10.3s]".format("hello") != "[hel       ]") r = 1;

    /* ===== Width / precision on %S (NeverC string) ===== */
    if ("[%5S]".format("hi".clone()) != "[   hi]") r = 1;
    if ("[%-5S]".format("hi".clone()) != "[hi   ]") r = 1;
    if ("[%.3S]".format("hello".clone()) != "[hel]") r = 1;
    if ("[%10.3S]".format("hello".clone()) != "[       hel]") r = 1;
    /* Bare lvalue with width -- sema's implicit retain copy still
       releases exactly once on the failure tail.  Pairs with the
       leaks --atExit gate. */
    {
        string name = "alice";
        if ("[%10S]".format(name) != "[     alice]") r = 1;
        if (name.len() != 5) r = 1;
    }

    /* ===== Width on %c ===== */
    if ("[%3c]".format('Q') != "[  Q]") r = 1;
    if ("[%-3c]".format('Q') != "[Q  ]") r = 1;

    /* ===== Width on %% literal ===== */
    if ("[%5%]".format() != "[    %]") r = 1;
    if ("[%-5%]".format() != "[%    ]") r = 1;

    /* ===== Width / zero-pad on %p ===== */
    {
        /* Use NULL so the rendered length is stable (`0x0` -> 3 bytes). */
        string s = "[%10p]".format((void *)0);
        if (s != "[       0x0]") r = 1;
    }
    {
        string s = "[%-10p]".format((void *)0);
        if (s != "[0x0       ]") r = 1;
    }
    {
        /* Zero-pad puts the fill BETWEEN `0x` and the digits. */
        string s = "[%010p]".format((void *)0);
        if (s != "[0x00000000]") r = 1;
    }

    /* ===== Multiple width/precision specs in one call ===== */
    if ("[%-5d|%05d|%.3s]".format(42, 42, "hello") != "[42   |00042|hel]") r = 1;
    if ("name=%-10s age=%3d".format("Bob", 7) != "name=Bob        age=  7") r = 1;

    /* ===== Long-form modifiers preserved with width ===== */
    if ("[%5lld]".format(123LL) != "[  123]") r = 1;
    if ("[%-10llu]".format(99ULL) != "[99        ]") r = 1;
    if ("[%08llx]".format(0xABULL) != "[000000ab]") r = 1;

    /* ===== Malformed flag/width run -- emitted literally,
            no va_arg consumed ===== */
    /* `%-` at end of fmt: parse_spec returns spec == 0, pass 1
       budgets 1 byte for the `%`, pass 2 emits `%`, then the next
       byte is `-` which is not part of any spec -> emit literally. */
    if ("[%-]".format() != "[%-]") r = 1;
    /* Parse stops at non-recognised conversion `z`, run is "%5z". */
    if ("[%5z=%d]".format(99) != "[%5z=99]") r = 1;

    /* ===== Stress: very wide field still respects MAX_LEN guard ===== */
    {
        /* `%999999999999999999d` would overflow the width parser
           guard; parse_spec marks it malformed, so we emit a stray
           `%` plus the rest as literal text and the trailing `%d`
           still consumes its arg. */
        string s = "%999999999999999999d=%d".format(7);
        if (!s.ends_with("=7")) r = 1;
    }

    printf("test_neverc_string_format: %s\n",
           r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}
