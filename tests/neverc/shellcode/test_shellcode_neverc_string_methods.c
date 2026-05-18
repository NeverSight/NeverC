// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string std::string-parity dotted methods.
 *
 * Targets the dotted-call dispatcher's "receiver / string-arg passed
 * by pointer" path, where `s.assign(t)` lowers to
 * `neverc_string_assign(&s, t)` and `s.swap(t)` lowers to
 * `neverc_string_swap(&s, &t)`.  The "(method -> kind)" rows live in
 * `BuiltinStringMethodReceiverKinds.def`; the kind enum roster
 * lives in `BuiltinStringMethodReceiverKindsRoster.def`; the actual
 * `&` UnaryOp wrapping happens in `Sema::ActOnBuiltinStringMethodCall`.
 *
 * Loader runs `main(a, b)` and uses the return code as exit status;
 * 0 means every assertion passed.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    /* (1) Dotted assign with an owned src: release-then-install, dst
       must end up owning its own buffer independent of src. */
    {
        string dst = "owned" + "old";
        string src = "owned" + "new";
        dst.assign(src);
        if (!neverc_string_eq(dst, "ownednew"))
            return 1;
        if (dst.cap == 0)
            return 2;
    }

    /* (2) Dotted assign with a string literal: borrowed source must
       still flow through the runtime helper and end up owned at dst. */
    {
        string dst = "owned" + "buf";
        dst.assign("literal");
        if (!neverc_string_eq(dst, "literal"))
            return 3;
        if (dst.cap == 0)
            return 4;
    }

    /* (3) Dotted assign on a borrowed dst (cap == 0 going in): the
       previous borrowed view is dropped and dst takes ownership of
       the new src bytes. */
    {
        string dst = neverc_string_view("hi", 2);
        if (dst.cap != 0)
            return 5;
        string src = "owned" + "src";
        dst.assign(src);
        if (!neverc_string_eq(dst, "ownedsrc"))
            return 6;
        if (dst.cap == 0)
            return 7;
    }

    /* (4) Self dotted assign on an owned string: dst == src must keep
       the buffer alive (matches the `=` rewrite). */
    {
        string s = "self" + "owned";
        s.assign(s);
        if (!neverc_string_eq(s, "selfowned"))
            return 8;
        if (s.cap == 0)
            return 9;
    }

    /* (5) Dotted assign hammered in a tight loop: arena free-list must
       reuse blocks instead of leaking per-iteration sources. */
    {
        string dst = "init" + "";
        for (int i = 0; i < 192; ++i) {
            string src = "iter" + "step";
            dst.assign(src);
            if (!neverc_string_eq(dst, "iterstep"))
                return 10;
            if (dst.cap == 0)
                return 11;
        }
    }

    /* (6) Dotted swap on two owned strings: handles must end up
       exchanged with no buffer copy. */
    {
        string left = "left" + "side";
        string right = "right" + "side";
        const char *left_data_before = left.data;
        const char *right_data_before = right.data;
        left.swap(right);
        if (!neverc_string_eq(left, "rightside"))
            return 12;
        if (!neverc_string_eq(right, "leftside"))
            return 13;
        /* Pointer-handle swap is observable: each side now points
           at the buffer the other used to own. */
        if (left.data != right_data_before)
            return 14;
        if (right.data != left_data_before)
            return 15;
    }

    /* (7) Dotted swap mixing owned and borrowed: the borrowed view
       (`cap == 0`) must end up on the side that was previously owned
       and vice versa.  Confirms the swap helper does not accidentally
       free the borrowed pointer. */
    {
        string owned = "owned" + "buf";
        string borrowed = neverc_string_view("borrowed", 8);
        if (borrowed.cap != 0)
            return 16;
        owned.swap(borrowed);
        if (!neverc_string_eq(owned, "borrowed"))
            return 17;
        if (owned.cap != 0)
            return 18;
        if (!neverc_string_eq(borrowed, "ownedbuf"))
            return 19;
        if (borrowed.cap == 0)
            return 20;
    }

    /* (8) Self dotted swap: must be a no-op on the buffer (no double
       free), content stays intact. */
    {
        string s = "alias" + "";
        s.swap(s);
        if (!neverc_string_eq(s, "alias"))
            return 21;
        if (s.cap == 0)
            return 22;
    }

    /* (9) Dotted swap inside a tight loop: the arena's free-list must
       stay bounded across hundreds of paired allocations. */
    {
        for (int i = 0; i < 192; ++i) {
            string a_str = "iter" + "a";
            string b_str = "iter" + "b";
            a_str.swap(b_str);
            if (!neverc_string_eq(a_str, "iterb"))
                return 23;
            if (!neverc_string_eq(b_str, "itera"))
                return 24;
        }
    }

    /* (10) Dotted assign + dotted swap interleaved: confirms the two
       receiver-by-pointer paths cooperate (assign drops dst's owned
       buffer; the very next swap exchanges into the freshly-installed
       handle without revisiting the released bytes). */
    {
        string dst = "init" + "";
        string other = "swapper" + "";
        for (int i = 0; i < 96; ++i) {
            dst.assign("payload");
            dst.swap(other);
            if (!neverc_string_eq(dst, "swapper"))
                return 25;
            if (!neverc_string_eq(other, "payload"))
                return 26;
            /* Restore for the next iteration. */
            other.assign("swapper");
        }
    }

    /* (11) Dotted assign forwards an owned method-call result: the
       prvalue source is released by the runtime helper, dst ends up
       owning a fresh buffer with the joined bytes. */
    {
        string dst = "seed" + "";
        dst.assign("hello " + "world");
        if (!neverc_string_eq(dst, "hello world"))
            return 27;
        if (dst.cap == 0)
            return 28;
    }

    /* (12) Dotted assign of an empty literal: dst ends up empty but
       still owned-with-NUL (cap == 1 after the borrowed -> owned
       promote in the prelude). */
    {
        string dst = "owned" + "buf";
        dst.assign("");
        if (!neverc_string_empty(dst))
            return 29;
    }

    /* (13) Dotted assign with a borrowed slice that aliases dst's own
       buffer: assign must copy the bytes out before releasing dst,
       otherwise the install would dangle. */
    {
        string dst = "alpha" + "beta";  /* owned "alphabeta" */
        dst.assign(neverc_string_view(dst.data + 2, 4));
        if (!neverc_string_eq(dst, "phab"))
            return 30;
        if (dst.cap == 0)
            return 31;
    }

    /* (14) Dotted swap on a fresh borrowed view paired with itself:
       no-op swap on a self-aliasing handle keeps content intact. */
    {
        string s = neverc_string_view("borrowed", 8);
        s.swap(s);
        if (!neverc_string_eq(s, "borrowed"))
            return 32;
        if (s.cap != 0)
            return 33;
    }

    /* (15) Dotted swap then dotted assign in sequence: the swap moves
       the handle from `t` into `s`, and the subsequent assign drops
       that handle through the regular release-then-install path. */
    {
        string s = "init" + "ial";
        string t = "moved" + "in";
        s.swap(t);
        if (!neverc_string_eq(s, "movedin"))
            return 34;
        if (!neverc_string_eq(t, "initial"))
            return 35;
        s.assign("after");
        if (!neverc_string_eq(s, "after"))
            return 36;
        if (!neverc_string_eq(t, "initial"))
            return 37;
    }

    return 0;
}
