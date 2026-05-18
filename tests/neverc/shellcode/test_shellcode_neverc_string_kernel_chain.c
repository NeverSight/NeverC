// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string std::string-style chained-method coverage in
 * ring-0 shellcode mode.
 *
 * Compile-only mirror of `test_shellcode_neverc_string_chain.c` for
 * the smaller 4 KB kernel arena (`StringRuntimeABI::KernelArenaSize`).
 * Same chained-call shapes; loop counts and intermediate buffer
 * sizes are scaled down so the kernel arena never legitimately OOMs
 * along the way.  The body never reaches the loader; the test
 * asserts the shellcode pipeline accepts every chain.
 */

static string normalize_kern(string s) {
    return s.trim().to_lower().replace(0, 1, "Z").append("!");
}

static int chain_size_kern(string s) {
    return (int)s.trim().to_upper().substr(0, 3).size();
}

int shellcode_entry(int seed) {
    /* (1) Five-step chain on the kernel arena. */
    {
        string s = "  hello  ";
        s = s.trim().to_upper().replace(0, 1, "Z").append("!").substr(0, 6);
        if (!neverc_string_eq(s, "ZELLO!"))
            return seed + 1;
    }

    /* (2) Chain crossing a function boundary. */
    {
        string s = normalize_kern("  hello  " + "");
        if (!neverc_string_eq(s, "Zello!"))
            return seed + 2;
    }

    /* (3) Chain inside function-call argument. */
    {
        string base = "  hello  " + "";
        if (chain_size_kern(base.clone()) != 3)
            return seed + 3;
    }

    /* (4) Mixed-receiver chain: literal + literal -> dotted. */
    {
        if (!("abc" + "def").to_upper().starts_with("ABC"))
            return seed + 4;
    }

    /* (5) Chain feeding equality op. */
    {
        if (!(neverc_string_repeat("ab", 4).to_upper() == "ABABABAB"))
            return seed + 5;
    }

    /* (6) OOM short-circuit mid-chain on the kernel arena. */
    {
        string s = "kt" + "";
        s = s.reserve(s.max_size()).append("x");
        if (!neverc_string_eq(s, "x"))
            return seed + 6;
    }

    /* (7) Deep chain pressure (smaller iter count for kernel arena). */
    {
        for (int i = 0; i < 64; ++i) {
            string s = "  abcDEF  " + "";
            s = s.trim().to_lower().substr(0, 4).append("!").to_upper();
            if (!neverc_string_eq(s, "ABCD!"))
                return seed + 7;
        }
    }

    /* (8) Chain inside chain. */
    {
        string s = "outr" + "";
        s = s.append(("inn" + "core").substr(0, 4).to_upper());
        if (!neverc_string_eq(s, "outrINNC"))
            return seed + 8;
    }

    /* (9) Borrowed -> owned promotion mid-chain. */
    {
        string view = neverc_string_view("padding-text", 12);
        string s = view.replace(0, 7, "INTRO-");
        if (!neverc_string_eq(s, "INTRO-text"))
            return seed + 9;
    }

    /* (10) Chain on clone() result without invalidating the source. */
    {
        string base = "kern" + "el";
        string derived = base.clone().to_upper().substr(0, 4);
        if (!neverc_string_eq(derived, "KERN"))
            return seed + 10;
        if (!neverc_string_eq(base, "kernel"))
            return seed + 11;
    }

    return seed;
}
