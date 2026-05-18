// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string by-value parameter / return-value tests.
 *
 * Validates that passing `string` by value into user-defined helpers
 * and returning `string` from them keeps every owned buffer balanced
 * across the retain / cleanup contract Sema layers on top of the
 * runtime.  Caller-side lvalues stay alive across calls, callee-side
 * parameters get retain copies that the cleanup attribute releases on
 * return, and prvalue / literal arguments forward through the runtime
 * helpers without leaking.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status;
 * 0 means every assertion passed.
 */

static string identity(string s) {
    return s;
}

static string greet(string name) {
    return "hello " + name;
}

static string twice(string s) {
    return s + s;
}

static int len_of(string s) {
    return (int)s.size();
}

static int sum_lengths(string a, string b) {
    return (int)(a.size() + b.size());
}

static string echo_uppercase(string s) {
    return s.to_upper();
}

int main(int a, int b) {
    (void)a;
    (void)b;

    /* (1) prvalue (literal-derived view) forwarded into a callee. */
    {
        string g = greet("world");
        if (!neverc_string_eq(g, "hello world"))
            return 1;
    }

    /* (2) lvalue argument: callee receives a retain copy, caller's
       lvalue is untouched. */
    {
        string name = "name" + "";
        string g = greet(name);
        if (!neverc_string_eq(g, "hello name"))
            return 2;
        if (!neverc_string_eq(name, "name"))
            return 3;
    }

    /* (3) Nested call: identity(greet(...)) round-trips ownership. */
    {
        string g = identity(greet("nested"));
        if (!neverc_string_eq(g, "hello nested"))
            return 4;
    }

    /* (4) Function returning a fresh owned buffer: caller takes
       ownership of an independent allocation. */
    {
        string g = twice("ab");
        if (!neverc_string_eq(g, "abab"))
            return 5;
        if (g.cap == 0)
            return 6;  /* must be owned, not borrowed */
    }

    /* (5) String parameter consumed by an int-returning helper:
       caller's lvalue persists, helper's retain copy is freed. */
    {
        string s = "lenme" + "";
        if (len_of(s) != 5)
            return 7;
        if (!neverc_string_eq(s, "lenme"))
            return 8;
    }

    /* (6) Two string parameters: each gets an independent retain
       copy; both caller-side lvalues remain valid. */
    {
        string lhs = "abc" + "";
        string rhs = "defg" + "";
        if (sum_lengths(lhs, rhs) != 7)
            return 9;
        if (!neverc_string_eq(lhs, "abc") || !neverc_string_eq(rhs, "defg"))
            return 10;
    }

    /* (7) Pressure: hundreds of calls reuse the arena's free list
       instead of leaking blocks of size sizeof(retain copy). */
    {
        string base = "base" + "";
        for (int i = 0; i < 192; ++i) {
            string g = greet(base);
            if (!neverc_string_eq(g, "hello base"))
                return 11;
        }
        if (!neverc_string_eq(base, "base"))
            return 12;
    }

    /* (8) Returning an owned string built from a literal source:
       caller receives owned (cap != 0) regardless of the input. */
    {
        string g = greet("literal");
        if (g.cap == 0)
            return 13;
        if (!neverc_string_eq(g, "hello literal"))
            return 14;
    }

    /* (9) Pass-through chain: identity^3 must keep the value but
       allocate / free three independent buffers along the way. */
    {
        string g = identity(identity(identity("chain")));
        if (!neverc_string_eq(g, "chain"))
            return 15;
    }

    /* (10) Method call on a prvalue function result: greet returns
       owned, .substr consumes it and returns a fresh owned slice. */
    {
        string g = greet("abc").substr(6);
        if (!neverc_string_eq(g, "abc"))
            return 16;
    }

    /* (11) Pipeline of user helper + builtin runtime: the upper-cased
       result must come back owned and survive a contains check. */
    {
        string s = "shell" + "code";
        string upper = echo_uppercase(s);
        if (!neverc_string_eq(upper, "SHELLCODE"))
            return 17;
        if (!upper.contains("CODE"))
            return 18;
        if (!neverc_string_eq(s, "shellcode"))
            return 19;
    }

    /* (12) Function params hold their retain copies through deep
       method chains inside the callee body. */
    {
        for (int i = 0; i < 96; ++i) {
            string g = echo_uppercase("mixed").substr(0, 3);
            if (!neverc_string_eq(g, "MIX"))
                return 20;
        }
    }

    /* (13) Multiple distinct string lvalues passed into one call,
       both retained independently with no cross-talk. */
    {
        string a_str = "AAA" + "";
        string b_str = "BBB" + "";
        for (int i = 0; i < 64; ++i) {
            if (sum_lengths(a_str, b_str) != 6)
                return 21;
        }
        if (!neverc_string_eq(a_str, "AAA"))
            return 22;
        if (!neverc_string_eq(b_str, "BBB"))
            return 23;
    }

    return 0;
}
