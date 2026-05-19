// NeverC builtin string smoke tests
// RUN: %neverc -std=c23 %s -o %t && %t

string global_label = "global";

string suffix(void);
int accepts_string(string s);
int accepts_string_copy(string s);
int accepts_owned_parameter(string s);
string returns_string_copy(string s);
string returns_local_owned(void);
string returns_local_slice(void);
int scoped_cleanup_paths(void);
int value_parameter_is_copy(string s);
int pointer_parameter_mutates(string *s);
int pointer_parameter_reassigns(string *s);

int main(void) {
    int r = 0;

    if (!neverc_string_eq(global_label, "global")) r = 1;

    string hello = "hello";
    if (neverc_string_len(hello) != 5) r = 1;
    if (hello.len != 5 || hello.data[0] != 'h') r = 1;
    if (hello != "hello") r = 1;
    if (hello == "world") r = 1;
    if ("hello" != hello) r = 1;

    string c = "123";
    string b = c.substr(1);
    if (!neverc_string_eq(b, "23")) r = 1;
    if (!neverc_string_eq(c, "123")) r = 1;

    string suffix_value = suffix();
    if (!neverc_string_eq(suffix_value, "!")) r = 1;
    if (suffix_value != "!") r = 1;

    string assigned;
    assigned = "neverc";
    if (!neverc_string_eq(assigned, "neverc")) r = 1;

    string view = neverc_string_view("view-data", 4);
    if (!neverc_string_eq(view, "view")) r = 1;

    if (neverc_string_find(hello, "ll") != 2) r = 1;
    if (neverc_string_find(hello, "missing") != NEVERC_STRING_NPOS) r = 1;
    if (neverc_string_find(hello, "") != 0) r = 1;
    if (neverc_string_find("bananas", "na") != 2) r = 1;
    if (neverc_string_rfind("bananas", "na") != 4) r = 1;
    if (neverc_string_rfind(hello, "l") != 3) r = 1;
    if (neverc_string_rfind(hello, "xyz") != NEVERC_STRING_NPOS) r = 1;
    if (neverc_string_rfind(hello, "") != 5) r = 1;
    if (neverc_string_rfind("abcabc", "abc") != 3) r = 1;
    if (neverc_string_rfind("a", "ab") != NEVERC_STRING_NPOS) r = 1;
    if (hello.rfind("l") != 3) r = 1;
    if (hello.rfind("missing") != NEVERC_STRING_NPOS) r = 1;

    string slice = neverc_string_substr(hello, 1, 3);
    if (!neverc_string_eq(slice, "ell")) r = 1;
    string dotted = hello.substr(1);
    if (!neverc_string_eq(dotted, "ello")) r = 1;
    string dotted_limited = hello.substr(1, 2);
    if (!neverc_string_eq(dotted_limited, "el")) r = 1;
    string chained_slice = ("abc" + "def").substr(2, 3);
    if (!neverc_string_eq(chained_slice, "cde")) r = 1;
    string self_slice = "012345";
    self_slice = self_slice.substr(1, 3);
    if (!neverc_string_eq(self_slice, "123")) r = 1;
    string append_slice = "abc";
    append_slice += append_slice.substr(1);
    if (!neverc_string_eq(append_slice, "abcbc")) r = 1;
    if (hello.find("ll") != 2) r = 1;
    if (hello.find("missing") != NEVERC_STRING_NPOS) r = 1;
    if (!hello.contains("ell") || hello.contains("xyz")) r = 1;
    if (!hello.starts_with("he") || hello.starts_with("el")) r = 1;
    if (!hello.ends_with("lo") || hello.ends_with("hel")) r = 1;
    if (hello.compare("hello") != 0 || hello.compare("hellp") >= 0 ||
        hello.compare("hell") <= 0) r = 1;
    if (hello.len() != 5 || hello.length() != 5 || hello.size() != 5 ||
        hello.empty()) r = 1;
    if (hello.at(1) != 'e' || neverc_string_at(hello, 4) != 'o' ||
        hello.at(99) != 0 || hello.front() != 'h' || hello.back() != 'o') r = 1;
    string clipped = neverc_string_substr(hello, 4, 99);
    if (!neverc_string_eq(clipped, "o")) r = 1;
    string out_of_range = neverc_string_substr(hello, 99, 1);
    if (neverc_string_len(out_of_range) != 0 || out_of_range != "") r = 1;

    string joined = hello + ", neverc";
    if (!neverc_string_eq(joined, "hello, neverc")) r = 1;
    if (joined != "hello, neverc") r = 1;
    if (joined == "hello") r = 1;
    const char *joined_cstr = neverc_string_cstr(joined);
    if (joined_cstr[12] != 'c') r = 1;
    if (!neverc_string_eq(joined, "hello, neverc")) r = 1;
    if (joined.c_str()[7] != 'n' || joined.c_str()[12] != 'c') r = 1;
    /* `data()` now returns `void *`; the pointer aliases the same byte
       buffer `c_str()` exposes, so casting back to `const char *`
       round-trips to the same bytes. */
    if (((const char *)joined.data())[12] != 'c') r = 1;

    string prefixed = "say " + hello;
    if (!neverc_string_eq(prefixed, "say hello")) r = 1;

    string literal_pair = "never" + "c";
    if (!neverc_string_eq(literal_pair, "neverc")) r = 1;
    if (literal_pair != "neverc") r = 1;
    if ("neverc" != literal_pair) r = 1;

    if (!accepts_string("direct parameter")) r = 1;
    if (!accepts_string_copy("copy parameter")) r = 1;
    if (!accepts_owned_parameter(hello + "!")) r = 1;

    string owned = "copy " + "return";
    string copied = returns_string_copy(owned);
    if (!neverc_string_eq(copied, "copy return")) r = 1;
    if (!neverc_string_eq(owned, "copy return")) r = 1;
    string returned_local = returns_local_owned();
    if (!neverc_string_eq(returned_local, "local return")) r = 1;
    string returned_slice = returns_local_slice();
    if (!neverc_string_eq(returned_slice, "return")) r = 1;
    if (!scoped_cleanup_paths()) r = 1;

    string reassigned = "old " + "value";
    reassigned = "new " + "value";
    if (!neverc_string_eq(reassigned, "new value")) r = 1;
    reassigned += "!";
    if (!neverc_string_eq(reassigned, "new value!")) r = 1;
    reassigned += reassigned;
    if (!neverc_string_eq(reassigned, "new value!new value!")) r = 1;
    if (!neverc_string_eq((reassigned += "?"), "new value!new value!?")) r = 1;

    string self_assigned = "self " + "owned";
    self_assigned = self_assigned;
    if (!neverc_string_eq(self_assigned, "self owned")) r = 1;

    string alias_source = "alias " + "source";
    alias_source = neverc_string_view(alias_source.data + 6, 6);
    if (!neverc_string_eq(alias_source, "source")) r = 1;
    string guarded_alias = "guard " + "alias";
    guarded_alias = neverc_string_view(guarded_alias.data + 6, 99);
    if (!neverc_string_eq(guarded_alias, "guard alias")) r = 1;
    string end_empty_alias = "abc";
    end_empty_alias = neverc_string_view(end_empty_alias.data + end_empty_alias.len, 0);
    if (end_empty_alias != "") r = 1;
    string allocation_edge_alias = "edge " + "alias";
    allocation_edge_alias =
        neverc_string_view(allocation_edge_alias.data + allocation_edge_alias.cap, 0);
    if (!neverc_string_eq(allocation_edge_alias, "edge alias")) r = 1;

    string chain_left;
    string chain_right;
    chain_left = chain_right = "chain " + "value";
    if (!neverc_string_eq(chain_left, "chain value")) r = 1;
    if (!neverc_string_eq(chain_right, "chain value")) r = 1;
    if (!neverc_string_eq((chain_left = "expr " + "value"), "expr value")) r = 1;
    if (!neverc_string_eq(chain_left, "expr value")) r = 1;

    string copy_source = "copy " + "source";
    string init_copy = copy_source;
    if (!neverc_string_eq(init_copy, "copy source")) r = 1;
    if (init_copy.data == copy_source.data) r = 1;
    if (!neverc_string_eq(copy_source, "copy source")) r = 1;
    string assign_copy;
    assign_copy = copy_source;
    if (!neverc_string_eq(assign_copy, "copy source")) r = 1;
    if (assign_copy.data == copy_source.data) r = 1;
    if (!neverc_string_eq(copy_source, "copy source")) r = 1;
    string clone_api_copy = neverc_string_clone(copy_source);
    if (!neverc_string_eq(clone_api_copy, "copy source")) r = 1;
    if (clone_api_copy.data == copy_source.data) r = 1;
    string dotted_clone_api_copy = copy_source.clone();
    if (!neverc_string_eq(dotted_clone_api_copy, "copy source")) r = 1;
    if (dotted_clone_api_copy.data == copy_source.data) r = 1;
    "discard " + "concat";
    neverc_string_clone("discard " + "clone");
    ("discard " + "dotted clone").clone();

    string pass_subject = "copy " + "source";
    if (!value_parameter_is_copy(pass_subject)) r = 1;
    if (!neverc_string_eq(pass_subject, "copy source")) r = 1;
    if (!pointer_parameter_mutates(&pass_subject)) r = 1;
    if (!neverc_string_eq(pass_subject, "copy source!")) r = 1;
    if (!pointer_parameter_reassigns(&pass_subject)) r = 1;
    if (!neverc_string_eq(pass_subject, "pointer target")) r = 1;

    string empty = "";
    const char *empty_cstr = neverc_string_cstr(empty);
    if (neverc_string_len(empty) != 0 || empty_cstr[0] != 0) r = 1;
    if (neverc_string_cstr("literal cstr")[8] != 'c') r = 1;
    if (empty != "" || !empty.empty()) r = 1;
    if (empty.front() != 0 || empty.back() != 0 || empty.at(0) != 0) r = 1;
    if (!empty.contains("") || !empty.starts_with("") || !empty.ends_with(""))
        r = 1;
    if ("" == "nonempty") r = 1;
    if ("same" != "same") r = 1;
    if (!neverc_string_eq("MiXeD".to_lower(), "mixed")) r = 1;
    if (!neverc_string_eq("MiXeD".to_upper(), "MIXED")) r = 1;
    if (!neverc_string_eq(" \t trim me \n".trim(), "trim me")) r = 1;
    if (!neverc_string_eq(" \t left".ltrim(), "left")) r = 1;
    if (!neverc_string_eq("right \r\n".rtrim(), "right")) r = 1;
    if (!neverc_string_eq(neverc_string_view((const char *)0, 5), "")) r = 1;

    printf("test_neverc_string: %s\n", r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}

string suffix(void) {
    return "!";
}

int accepts_string(string s) {
    return neverc_string_eq(s, "direct parameter");
}

int accepts_string_copy(string s) {
    string local = s;
    return neverc_string_eq(local, "copy parameter");
}

int accepts_owned_parameter(string s) {
    string local = s;
    return neverc_string_eq(local, "hello!");
}

string returns_string_copy(string s) {
    string local = s;
    return local;
}

string returns_local_owned(void) {
    string local = "local " + "return";
    return local;
}

string returns_local_slice(void) {
    string local = "slice return";
    return local.substr(6);
}

int scoped_cleanup_paths(void) {
    int ok = 1;

    for (int i = 0; i < 3; i++) {
        string tmp = "loop " + "cleanup";
        if (!neverc_string_eq(tmp, "loop cleanup")) ok = 0;
        if (i == 0) continue;
        if (i == 1) break;
    }

    {
        string tmp = "goto " + "cleanup";
        if (!neverc_string_eq(tmp, "goto cleanup")) ok = 0;
        goto after_goto_scope;
    }

after_goto_scope:
    return ok;
}

int value_parameter_is_copy(string s) {
    s = "changed by value";
    return neverc_string_eq(s, "changed by value");
}

int pointer_parameter_mutates(string *s) {
    *s = *s + "!";
    return neverc_string_eq(*s, "copy source!");
}

int pointer_parameter_reassigns(string *s) {
    *s = "pointer target";
    return neverc_string_eq(*s, "pointer target");
}
