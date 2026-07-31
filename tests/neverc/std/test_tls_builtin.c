#include "neverc/std/crypto/tls.h"

#include <stdio.h>

int main(void) {
#ifdef __neverc__
    if (crypto.tls.verify_certificate_verify(
            NULL, 0, 0, NULL, 0, NULL, 0) != -1)
        return 1;
#else
    if (neverc_tls_verify_certificate_verify(
            NULL, 0, 0, NULL, 0, NULL, 0) != -1)
        return 1;
#endif
    puts("passed");
    return 0;
}
