#include "neverc/std/crypto/tls.h"

#include <stdio.h>

int main(void) {
#ifdef __neverc__
    if (crypto.tls.verify_certificate_verify(
            NULL, 0, 0, NULL, 0, NULL, 0) != -1)
        return 1;
    if (crypto.tls.sign_certificate_verify(
            NULL, 0, NULL, 0, NULL, NULL, 0, NULL) != -1)
        return 2;
#else
    if (neverc_tls_verify_certificate_verify(
            NULL, 0, 0, NULL, 0, NULL, 0) != -1)
        return 1;
    if (neverc_tls_sign_certificate_verify(
            NULL, 0, NULL, 0, NULL, NULL, 0, NULL) != -1)
        return 2;
#endif
    puts("passed");
    return 0;
}
