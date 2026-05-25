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

    typedef struct {
        string token;
        string tags[2];
    } auth_ctx;
    auth_ctx ctx = {
        .token = "kernel_token".encrypt(),
        .tags = {"k1".encrypt(), "k2".encrypt()}
    };
    if (ctx.token != "kernel_token".encrypt())
        return 8;
    if (ctx.tags[1] != "k2".encrypt())
        return 9;

    return 0;
}
