// kernel/freestanding friendly string smoke test
// RUN: %neverc -std=c23 -DNEVERC_STRING_ALLOC=kernel_alloc -DNEVERC_STRING_FREE=kernel_free %s -o %t && %t

/* The bump allocator below never recycles bytes (kernel_free is a no-op),
   so pool size grows monotonically over the whole test.  The original
   1024-byte arena was sized to fit the test as it stood when the harness
   was written; subsequent growth in the surface coverage (multiple
   substr / concat / += / clone chains) pushed the cumulative footprint
   well past that.  4 KiB stays small enough to still exercise the
   freestanding allocator hookup without making the late `+= "?"` step
   silently fall back to the empty sentinel because alloc returned NULL. */
static unsigned char kernel_pool[4096];
static __SIZE_TYPE__ kernel_off;
static int kernel_alloc_count;
static int kernel_free_count;

void *kernel_alloc(__SIZE_TYPE__ n) {
    if (n == 0 || n > sizeof(kernel_pool) - kernel_off)
        return (void *)0;
    void *p = (void *)(kernel_pool + kernel_off);
    kernel_off += n;
    kernel_alloc_count++;
    return p;
}

void kernel_free(void *p) {
    (void)p;
    kernel_free_count++;
}

int kernel_accept_copy(string s);
string kernel_returns_local_owned(void);
string kernel_returns_local_slice(void);
int kernel_scope_cleanup_paths(void);
int kernel_value_copy(string s);
int kernel_pointer_append(string *s);
int kernel_pointer_reassign(string *s);

int main(void) {
    {
        char kernel_word[] = {'k', 'e', 'r', 'n', 'e', 'l'};
        string a = neverc_string_view(kernel_word, sizeof(kernel_word));
        string b = " mode";
        string c = a + b;

        if (neverc_string_len(a) != 6)
            return 1;
        if (!neverc_string_eq(c, "kernel mode"))
            return 2;
        if (c != "kernel mode")
            return 4;
        if (c == "kernel")
            return 5;
        if ("kernel mode" != c)
            return 6;
        const char *c_data = neverc_string_cstr(c);
        if (c_data[10] != 'e')
            return 14;
        if (!neverc_string_eq(c, "kernel mode"))
            return 21;
        if (c.c_str()[7] != 'm' || c.c_str()[10] != 'e')
            return 34;
        /* `data()` returns `void *`; cast back through `const char *`
           to verify it points at the same byte buffer as `c_str`. */
        if (((const char *)c.data())[10] != 'e')
            return 68;
        if (neverc_string_find(c, "mode") != 7)
            return 7;
        if (neverc_string_find(c, "user") != NEVERC_STRING_NPOS)
            return 8;
        if (c.find("mode") != 7)
            return 22;
        if (neverc_string_rfind(c, "e") != 10)
            return 63;
        if (neverc_string_rfind(c, "user") != NEVERC_STRING_NPOS)
            return 64;
        if (neverc_string_rfind(c, "") != c.len)
            return 65;
        if (c.rfind("e") != 10)
            return 66;
        if (c.rfind("missing") != NEVERC_STRING_NPOS)
            return 67;
        if (!c.contains("nel") || c.contains("user"))
            return 47;
        if (!c.starts_with("kernel") || c.starts_with("mode"))
            return 48;
        if (!c.ends_with("mode") || c.ends_with("kernel"))
            return 49;
        if (c.compare("kernel mode") != 0 || c.compare("kernel node") >= 0 ||
            c.compare("kernel mod") <= 0)
            return 50;
        if (c.len() != 11 || c.length() != 11 || c.size() != 11 || c.empty())
            return 23;
        if (c.at(7) != 'm' || neverc_string_at(c, 10) != 'e' || c.at(99) != 0)
            return 46;
        if (c.front() != 'k' || c.back() != 'e')
            return 51;

        string digits = "123";
        string digits_tail = digits.substr(1);
        if (!neverc_string_eq(digits_tail, "23"))
            return 32;
        if (!neverc_string_eq(digits, "123"))
            return 33;

        string kernel_slice = neverc_string_substr(c, 0, 6);
        if (!neverc_string_eq(kernel_slice, "kernel"))
            return 9;
        string dotted_slice = c.substr(7);
        if (!neverc_string_eq(dotted_slice, "mode"))
            return 24;
        string dotted_limited = c.substr(0, 6);
        if (!neverc_string_eq(dotted_limited, "kernel"))
            return 25;
        string chained_slice = ("ker" + "nel-mode").substr(7, 4);
        if (!neverc_string_eq(chained_slice, "mode"))
            return 43;
        string self_slice = "012345";
        self_slice = self_slice.substr(1, 3);
        if (!neverc_string_eq(self_slice, "123"))
            return 44;
        string append_slice = "abc";
        append_slice += append_slice.substr(1);
        if (!neverc_string_eq(append_slice, "abcbc"))
            return 45;
        string mode_slice = neverc_string_substr(c, neverc_string_find(c, "mode"), 16);
        if (!neverc_string_eq(mode_slice, "mode"))
            return 10;
        if (!kernel_accept_copy(a + b))
            return 11;
        string returned_local = kernel_returns_local_owned();
        if (!neverc_string_eq(returned_local, "kernel return"))
            return 54;
        string returned_slice = kernel_returns_local_slice();
        if (!neverc_string_eq(returned_slice, "return"))
            return 55;
        if (!kernel_scope_cleanup_paths())
            return 56;

        string reassigned = "old " + "kernel";
        reassigned = "new " + "kernel";
        if (!neverc_string_eq(reassigned, "new kernel"))
            return 12;
        reassigned += "!";
        if (!neverc_string_eq(reassigned, "new kernel!"))
            return 26;
        reassigned += reassigned;
        if (!neverc_string_eq(reassigned, "new kernel!new kernel!"))
            return 27;
        if (!neverc_string_eq((reassigned += "?"), "new kernel!new kernel!?"))
            return 39;

        string self_assigned = "self " + "kernel";
        self_assigned = self_assigned;
        if (!neverc_string_eq(self_assigned, "self kernel"))
            return 28;

        string alias_source = "kernel " + "alias";
        alias_source = neverc_string_view(alias_source.data + 7, 5);
        if (!neverc_string_eq(alias_source, "alias"))
            return 40;
        string guarded_alias = "guard " + "kernel";
        guarded_alias = neverc_string_view(guarded_alias.data + 6, 99);
        if (!neverc_string_eq(guarded_alias, "guard kernel"))
            return 57;
        string end_empty_alias = "abc";
        end_empty_alias =
            neverc_string_view(end_empty_alias.data + end_empty_alias.len, 0);
        if (end_empty_alias != "")
            return 58;
        string allocation_edge_alias = "edge " + "kernel";
        allocation_edge_alias = neverc_string_view(
            allocation_edge_alias.data + allocation_edge_alias.cap, 0);
        if (!neverc_string_eq(allocation_edge_alias, "edge kernel"))
            return 59;

        string chain_left;
        string chain_right;
        chain_left = chain_right = "chain " + "kernel";
        if (!neverc_string_eq(chain_left, "chain kernel"))
            return 35;
        if (!neverc_string_eq(chain_right, "chain kernel"))
            return 36;
        if (!neverc_string_eq((chain_left = "expr " + "kernel"), "expr kernel"))
            return 37;
        if (!neverc_string_eq(chain_left, "expr kernel"))
            return 38;

        string copy_source = "copy " + "kernel";
        string init_copy = copy_source;
        if (!neverc_string_eq(init_copy, "copy kernel"))
            return 29;
        if (init_copy.data == copy_source.data)
            return 43;
        if (!neverc_string_eq(copy_source, "copy kernel"))
            return 30;
        string assign_copy;
        assign_copy = copy_source;
        if (!neverc_string_eq(assign_copy, "copy kernel"))
            return 31;
        if (assign_copy.data == copy_source.data)
            return 44;
        if (!neverc_string_eq(copy_source, "copy kernel"))
            return 32;
        string clone_api_copy = neverc_string_clone(copy_source);
        if (!neverc_string_eq(clone_api_copy, "copy kernel"))
            return 41;
        if (clone_api_copy.data == copy_source.data)
            return 45;
        string dotted_clone_api_copy = copy_source.clone();
        if (!neverc_string_eq(dotted_clone_api_copy, "copy kernel"))
            return 42;
        if (dotted_clone_api_copy.data == copy_source.data)
            return 46;
        "discard " + "concat";
        neverc_string_clone("discard " + "clone");
        ("discard " + "dotted clone").clone();

        string ptr_subject = "ptr " + "copy";
        if (!kernel_value_copy(ptr_subject))
            return 15;
        if (!neverc_string_eq(ptr_subject, "ptr copy"))
            return 16;
        if (!kernel_pointer_append(&ptr_subject))
            return 17;
        if (!neverc_string_eq(ptr_subject, "ptr copy!"))
            return 18;
        if (!kernel_pointer_reassign(&ptr_subject))
            return 19;
        if (!neverc_string_eq(ptr_subject, "kernel pointer"))
            return 20;

        string empty = "";
        if (empty.front() != 0 || empty.back() != 0 || empty.at(0) != 0)
            return 52;
        if (!empty.contains("") || !empty.starts_with("") ||
            !empty.ends_with(""))
            return 53;
        int alloc_before_cstr_literal = kernel_alloc_count;
        int free_before_cstr_literal = kernel_free_count;
        const char *literal_cstr = neverc_string_cstr("literal cstr");
        if (literal_cstr[8] != 'c')
            return 54;
        if (kernel_alloc_count != alloc_before_cstr_literal ||
            kernel_free_count != free_before_cstr_literal)
            return 55;
        if (neverc_string_len("literal len") != 11)
            return 56;
        if (!neverc_string_eq("literal eq", "literal eq"))
            return 57;
        if (neverc_string_find("literal find", "find") != 8)
            return 58;
        if (!neverc_string_contains("literal contains", "contain"))
            return 59;
        if (!neverc_string_starts_with("literal prefix", "literal"))
            return 60;
        if (!neverc_string_ends_with("literal suffix", "suffix"))
            return 61;
        if (kernel_alloc_count != alloc_before_cstr_literal ||
            kernel_free_count != free_before_cstr_literal)
            return 62;
    }
    if (kernel_free_count != kernel_alloc_count)
        return 13;

    string huge = {(const char *)1, ((__SIZE_TYPE__)-1), 0};
    string failed = huge + "x";
    /* NeverC's `+` lowering retains both operands before reaching
       __neverc_string_cat, and __neverc_string_retain already collapses
       any oversized/forged handle to the empty sentinel via
       __neverc_string_make_owned's MAX_LEN guard.  So cat sees
       (empty, "x") instead of (forged, "x") and yields "x".  We just
       care that the forged handle does not crash, walk wild memory, or
       leak the 1-byte data pointer through; checking len <= 1 captures
       that contract without over-specifying the post-retain shape. */
    if (neverc_string_len(failed) > 1)
        return 3;

    return 0;
}

int kernel_accept_copy(string s) {
    string local = s;
    return neverc_string_eq(local, "kernel mode");
}

string kernel_returns_local_owned(void) {
    string local = "kernel " + "return";
    return local;
}

string kernel_returns_local_slice(void) {
    string local = "kernel return";
    return local.substr(7);
}

int kernel_scope_cleanup_paths(void) {
    int alloc_before = kernel_alloc_count;
    int free_before = kernel_free_count;

    for (int i = 0; i < 3; i++) {
        string tmp = "loop " + "cleanup";
        if (!neverc_string_eq(tmp, "loop cleanup"))
            return 0;
        if (i == 0)
            continue;
        if (i == 1)
            break;
    }

    {
        string tmp = "goto " + "cleanup";
        if (!neverc_string_eq(tmp, "goto cleanup"))
            return 0;
        goto after_goto_scope;
    }

after_goto_scope:
    return kernel_alloc_count - alloc_before == kernel_free_count - free_before;
}

int kernel_value_copy(string s) {
    s = "value only";
    return neverc_string_eq(s, "value only");
}

int kernel_pointer_append(string *s) {
    *s = *s + "!";
    return neverc_string_eq(*s, "ptr copy!");
}

int kernel_pointer_reassign(string *s) {
    *s = "kernel pointer";
    return neverc_string_eq(*s, "kernel pointer");
}
