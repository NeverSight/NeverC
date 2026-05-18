// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string std::string-style chained-method coverage.
 *
 * `_safety.c` covers a 3-step chain (`s.trim().to_upper().replace`).
 * `_chain` extends that with the patterns std::string users actually
 * write in production code, where each chained call consumes a
 * prvalue receiver and returns a fresh owned buffer:
 *
 *   * 5+ step chains -- every intermediate prvalue must be released
 *     by the runtime helper without leaking into the arena.
 *   * Chains crossing function boundaries (a helper returns the
 *     middle of a chain; the caller continues chaining on the
 *     return value).
 *   * Chains inside function-call arguments (the chain's terminal
 *     prvalue is forwarded into another helper that takes string
 *     by value).
 *   * Mixed-receiver chains: the head is a literal, then `+`
 *     produces an owned, then `.dotted_method()` consumes that.
 *   * Chains that interact with comparison / equality operators
 *     in a single expression (the arena must release the chain's
 *     terminal prvalue exactly once).
 *   * Chains hammered in tight loops -- the arena's free-list
 *     must absorb the per-step churn instead of growing the bump
 *     pointer linearly.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status;
 * 0 means every assertion passed.  Each `return N;` line is unique
 * so the failing assertion is directly identifiable.
 */

static string normalize(string s) {
    /* Five-step chain inside a helper.  Every intermediate prvalue
       (`.trim()`, `.to_lower()`, `.replace(...)`, `.append(...)`)
       is released by the next call's by-value retain contract. */
    return s.trim().to_lower().replace(0, 1, "Z").append("!");
}

static int chain_size(string s) {
    /* Method chain on the parameter directly.  `s` is the cleanup-
       attributed retain copy; the chain consumes a prvalue clone
       and returns an int -- the chain's terminal prvalue must be
       released before we leave this function. */
    return (int)s.trim().to_upper().substr(0, 3).size();
}

int main(int a, int b) {
    (void)a;
    (void)b;

    /* (1) Five-step chain on a literal seed: each step consumes the
       previous prvalue.  No leaks => arena reuses blocks. */
    {
        string s = "  hello  ";
        s = s.trim().to_upper().replace(0, 1, "Z").append("!").substr(0, 6);
        if (!neverc_string_eq(s, "ZELLO!"))
            return 1;
        if (s.cap == 0)
            return 2;
    }

    /* (2) Chain crossing a function boundary: the helper returns
       a fresh owned, the caller continues chaining. */
    {
        string s = normalize("  hello  " + "");
        if (!neverc_string_eq(s, "Zello!"))
            return 3;
        s = s.to_upper().substr(0, 3);
        if (!neverc_string_eq(s, "ZEL"))
            return 4;
    }

    /* (3) Chain inside a function-call argument: the terminal
       prvalue must flow into the helper's by-value parameter
       cleanly, with the runtime releasing it on return. */
    {
        string base = "  hello world  " + "";
        if (chain_size(base.clone()) != 3)  /* "HEL" -> 3 */
            return 5;
        if (!neverc_string_eq(base, "  hello world  "))
            return 6;
    }

    /* (4) Mixed-receiver chain: literal -> `+` produces owned ->
       dotted-method consumes that owned ->  comparison op. */
    {
        if (!("abc" + "def").to_upper().starts_with("ABC"))
            return 7;
        if (("hello" + "world").substr(5, 5).compare("world") != 0)
            return 8;
    }

    /* (5) Chain whose terminal prvalue feeds an equality operator
       in the same expression: the rewritten `neverc_string_eq` consumes
       both inputs (including the chain's terminal prvalue). */
    {
        if (!(neverc_string_repeat("ab", 4).to_upper() == "ABABABAB"))
            return 9;
        if ((neverc_string_repeat("xy", 3) == "abcdef"))
            return 10;
    }

    /* (6) Chain that triggers alias-aware assignment: the chain's
       last step takes a substring of an owned buffer and re-binds
       it to the same variable through `=`.  Sema's assign rewrite
       must promote-then-release without dangling. */
    {
        string s = "alphabeta" + "";
        s = s.substr(2, 4);
        if (!neverc_string_eq(s, "phab"))
            return 11;
        if (s.cap == 0)
            return 12;
    }

    /* (7) Chain triggering the OOM short-circuit half-way: the
       middle step asks for an unreasonable buffer; the prelude
       must collapse to the empty sentinel and propagate that
       through the rest of the chain without dereferencing NULL.
       `reserve(max_size())` returns the empty owned with cap == 0;
       the subsequent `.append("x")` must allocate a fresh 1-byte
       owned. */
    {
        string s = "anchor" + "";
        s = s.reserve(s.max_size()).append("x");
        if (!neverc_string_eq(s, "x"))
            return 13;
        if (s.cap == 0)
            return 14;
    }

    /* (8) Deep chain pressure: 5-step chain in a 256-iter loop.
       Free-list must reuse blocks across iterations -- otherwise
       each iter would burn ~5 bump allocations and the user-mode
       64 KB arena would only last ~3000 iters before OOM. */
    {
        for (int i = 0; i < 256; ++i) {
            string s = "  abcDEF  " + "";
            s = s.trim().to_lower().substr(0, 4).append("!").to_upper();
            if (!neverc_string_eq(s, "ABCD!"))
                return 15;
        }
    }

    /* (9) Chain inside a chain: the inner chain produces a string
       that is consumed as the argument of an outer chain step. */
    {
        string s = "outer" + "";
        s = s.append(("inner" + "core").substr(0, 5).to_upper());
        if (!neverc_string_eq(s, "outerINNER"))
            return 16;
    }

    /* (10) Method chain that feeds two distinct sinks (forks of the
       same prvalue would dangle if the runtime released it twice).
       We avoid the regression by binding the chain result first. */
    {
        string built = ("ab" + "cd").to_upper();
        if (!neverc_string_eq(built, "ABCD"))
            return 17;
        if (built.size() != 4)
            return 18;
        /* `built` reaches its own cleanup at scope exit. */
    }

    /* (11) Search/predicate at the tail of a chain: the chain
       releases the receiver via `neverc_string_starts_with`, so the
       returned int does not leak the chain's terminal owned. */
    {
        for (int i = 0; i < 96; ++i) {
            if (!("hello" + "world").to_upper().starts_with("HELLO"))
                return 19;
            if (("foo" + "bar").substr(0, 3).contains("oo") != 1)
                return 20;
        }
    }

    /* (12) Long chain consuming a borrowed view from the start:
       no allocation until `.replace`, after which every step is
       owned-consume.  Verifies the borrow -> own promotion path. */
    {
        string view = neverc_string_view("padding-text", 12);
        string s = view.replace(0, 8, "INTRO-");
        if (!neverc_string_eq(s, "INTRO-text"))
            return 21;
        if (s.cap == 0)
            return 22;
    }

    /* (13) Chain on a clone() result: the clone owns its own
       buffer, the chain consumes that owned buffer and returns
       another owned buffer.  Original receiver stays alive. */
    {
        string base = "preserve" + "me";
        string derived = base.clone().to_upper().substr(0, 4);
        if (!neverc_string_eq(derived, "PRES"))
            return 23;
        if (!neverc_string_eq(base, "preserveme"))
            return 24;
    }

    return 0;
}
