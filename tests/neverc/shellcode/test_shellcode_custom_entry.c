// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Custom entry point: the user names the exported function anything they
 * want and tells neverc about it via `-fshellcode-entry=<name>`.  The
 * pipeline still enforces internal linkage + always_inline on every
 * helper and the extractor still demands that <name> land at offset 0.
 *
 * Here we use `shellcode_main` and rely on the loader's int(int,int)
 * signature to pass 9 * 8 = 72 back through the exit code.
 */
static int doubled(int x) { return x * 2; }
static int tripled(int x) { return x * 3; }

int shellcode_main(int a, int b) {
    return doubled(a) + tripled(b);
}
