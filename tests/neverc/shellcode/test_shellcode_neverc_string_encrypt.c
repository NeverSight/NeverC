// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* NeverC builtin string .encrypt() in shellcode mode.
 *
 * Exercises compile-time string encryption with the shellcode pipeline:
 * basic encrypt/decrypt, zero-allocation comparisons, search, struct/array
 * patterns, and encrypted + unencrypted interop.
 */
int main(int a, int b) {
    (void)a;
    (void)b;

    string secret = "shellcode_secret".encrypt();
    if (secret != "shellcode_secret".encrypt())
        return 1;
    if (secret.len != 16)
        return 2;

    if (!secret.starts_with("shell".encrypt()))
        return 3;
    if (!secret.ends_with("secret".encrypt()))
        return 4;
    if (!secret.contains("code".encrypt()))
        return 5;
    if (secret.find("secret".encrypt()) != 10)
        return 6;

    string keys[] = {"admin".encrypt(), "root".encrypt(), "user".encrypt()};
    int found = 0;
    for (int i = 0; i < 3; i++) {
        if (keys[i] == "root".encrypt()) {
            found = 1;
            break;
        }
    }
    if (!found)
        return 7;

    string plain = "mixed";
    if (plain != "mixed")
        return 8;
    if (plain != "mixed".encrypt())
        return 9;

    string enc_a = "alpha".encrypt();
    string enc_b = "beta".encrypt();
    if (enc_a == enc_b)
        return 10;
    if (!(enc_a != enc_b))
        return 11;

    string empty = "".encrypt();
    if (!neverc_string_empty(empty))
        return 12;

    string single = "x".encrypt();
    if (single != "x".encrypt())
        return 13;
    if (single.len != 1)
        return 14;

    string chain = "HELLO".encrypt().to_lower();
    if (chain != "hello")
        return 15;

    typedef struct {
        string user;
        string pass;
    } creds;
    creds login = {.user = "admin".encrypt(), .pass = "s3cret".encrypt()};
    if (login.user != "admin".encrypt())
        return 16;
    if (login.pass != "s3cret".encrypt())
        return 17;

    string grid[2][2] = {
        {"a".encrypt(), "b".encrypt()},
        {"c".encrypt(), "d".encrypt()}
    };
    if (grid[0][1] != "b".encrypt())
        return 18;
    if (grid[1][0] != "c".encrypt())
        return 19;

    return 0;
}
