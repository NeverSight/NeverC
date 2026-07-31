#include "neverc/std/crypto/rsa.h"

#include <stdio.h>

int main(void) {
#ifdef __neverc__
    if (crypto.rsa.verify_pss_sha384(NULL, NULL, 0, NULL, 0) != -1)
        return 1;
    if (crypto.rsa.verify_pss_sha512(NULL, NULL, 0, NULL, 0) != -1)
        return 2;
#else
    if (neverc_rsa_verify_pss_sha384(NULL, NULL, 0, NULL, 0) != -1)
        return 1;
    if (neverc_rsa_verify_pss_sha512(NULL, NULL, 0, NULL, 0) != -1)
        return 2;
#endif
    puts("passed");
    return 0;
}
