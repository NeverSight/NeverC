// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string by-value parameter / return tests in ring-0
 * shellcode mode.
 *
 * Compile-only guard for `-mshellcode-context=kernel`: the builtin string
 * prelude + StringRuntimePass must keep ownership balanced across
 * user-defined helpers that pass `string` by value or return it by
 * value, even on the smaller kernel arena.  KernelImportPass would
 * otherwise pick up a leaked malloc/free reference and surface the
 * resolver-shim extern.
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

int shellcode_entry(int seed) {
    /* prvalue (literal-derived view) forwarded into a callee. */
    {
        string g = greet("world");
        if (!neverc_string_eq(g, "hello world"))
            return seed + 1;
    }

    /* lvalue argument: callee receives a retain copy. */
    {
        string name = "name" + "";
        string g = greet(name);
        if (!neverc_string_eq(g, "hello name"))
            return seed + 2;
    }

    /* Nested call round-trips through identity. */
    {
        string g = identity(greet("nested"));
        if (!neverc_string_eq(g, "hello nested"))
            return seed + 3;
    }

    /* Caller takes ownership of a fresh owned return value. */
    {
        string g = twice("ab");
        if (!neverc_string_eq(g, "abab") || g.cap == 0)
            return seed + 4;
    }

    /* string parameter consumed by an int-returning helper. */
    {
        string s = "lenme" + "";
        if (len_of(s) != 5)
            return seed + 5;
    }

    /* Two string parameters get independent retain copies. */
    {
        string lhs = "abc" + "";
        string rhs = "defg" + "";
        if (sum_lengths(lhs, rhs) != 7)
            return seed + 6;
    }

    /* Pressure: many calls reuse the kernel arena's free list. */
    {
        string base = "base" + "";
        for (int i = 0; i < 32; ++i) {
            string g = greet(base);
            if (!neverc_string_eq(g, "hello base"))
                return seed + 7;
        }
    }

    /* identity^3 chain through three independent helper frames. */
    {
        string g = identity(identity(identity("chain")));
        if (!neverc_string_eq(g, "chain"))
            return seed + 8;
    }

    /* Method call on a prvalue function result. */
    {
        string g = greet("abc").substr(6);
        if (!neverc_string_eq(g, "abc"))
            return seed + 9;
    }

    /* Pipeline of user helper + builtin runtime. */
    {
        string s = "shell" + "code";
        string upper = echo_uppercase(s);
        if (!neverc_string_eq(upper, "SHELLCODE"))
            return seed + 10;
        if (!upper.contains("CODE"))
            return seed + 11;
    }

    return seed;
}
