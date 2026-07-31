#include "neverc/std/crypto/rsa.h"

#include <stdio.h>

int main(void) {
#ifdef __neverc__
    if (crypto.rsa.verify_pkcs1v15_sha384(
            NULL, NULL, 0, NULL, 0) != -1)
        return 1;
    if (crypto.rsa.verify_pkcs1v15_sha512(
            NULL, NULL, 0, NULL, 0) != -1)
        return 2;
    if (crypto.rsa.verify_pss_sha384(NULL, NULL, 0, NULL, 0) != -1)
        return 3;
    if (crypto.rsa.verify_pss_sha512(NULL, NULL, 0, NULL, 0) != -1)
        return 4;
#else
    if (neverc_rsa_verify_pkcs1v15_sha384(
            NULL, NULL, 0, NULL, 0) != -1)
        return 1;
    if (neverc_rsa_verify_pkcs1v15_sha512(
            NULL, NULL, 0, NULL, 0) != -1)
        return 2;
    if (neverc_rsa_verify_pss_sha384(NULL, NULL, 0, NULL, 0) != -1)
        return 3;
    if (neverc_rsa_verify_pss_sha512(NULL, NULL, 0, NULL, 0) != -1)
        return 4;
#endif
    puts("passed");
    return 0;
}
