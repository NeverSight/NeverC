// .nc extension auto-enables builtin string without -fbuiltin-string flag.
// RUN: %neverc -std=c23 %s -o %t && %t

#include <stdio.h>

int main(void) {
    int r = 0;

    string hello = "hello";
    if (hello.len != 5) r = 1;
    if (hello != "hello") r = 1;
    if (hello == "world") r = 1;

    string joined = hello + ", neverc";
    if (joined != "hello, neverc") r = 1;
    if (joined.len != 13) r = 1;

    string upper = "neverc".to_upper();
    if (upper != "NEVERC") r = 1;

    string trimmed = "  spaces  ".trim();
    if (trimmed != "spaces") r = 1;

    string sub = "hello world".substr(6);
    if (sub != "world") r = 1;

    if (!hello.starts_with("he")) r = 1;
    if (!hello.ends_with("lo")) r = 1;
    if (!hello.contains("ell")) r = 1;
    if (hello.find("ll") != 2) r = 1;

    string empty = "";
    if (!empty.empty()) r = 1;
    if (empty.len != 0) r = 1;

    string copy = hello;
    if (copy != "hello") r = 1;

    string concat = "never" + "c";
    if (concat != "neverc") r = 1;

    string reassigned = "old";
    reassigned = "new";
    if (reassigned != "new") r = 1;
    reassigned += "!";
    if (reassigned != "new!") r = 1;

    printf("test_nc_ext_string: %s\n", r == 0 ? "ALL PASSED" : "FAILED");
    return r;
}
