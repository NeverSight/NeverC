// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string .encrypt() in kernel-context shellcode mode.
 *
 * Compile-only guard for -mshellcode-context=kernel: string encryption
 * must use the shellcode-local arena and must not leave malloc/free externs.
 */
int shellcode_entry(int seed) {
    string secret = "kernel_secret".encrypt();
    if (secret != "kernel_secret".encrypt())
        return 1;
    if (secret.len != 13)
        return 2;

    if (!secret.starts_with("kernel".encrypt()))
        return 3;
    if (!secret.contains("secret".encrypt()))
        return 4;

    string keys[] = {"key1".encrypt(), "key2".encrypt()};
    if (keys[0] != "key1".encrypt())
        return 5;
    if (keys[1] != "key2".encrypt())
        return 6;

    string chain = "ENCRYPTED".encrypt().to_lower();
    if (chain != "encrypted")
        return 7;

    return 0;
}
