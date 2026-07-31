#include "neverc/std/crypto/x509.h"

#include <stdio.h>

int main(void) {
#ifdef __neverc__
    if (crypto.x509.check_signature_from(NULL, NULL) != -1)
        return 1;
    if (crypto.x509.verify_signature(
            NULL, 0, NULL, 0, NULL, 0) != -1)
        return 1;
    if (crypto.x509.verify_chain(NULL, 0, NULL, NULL, 0) != -1)
        return 1;
#else
    if (neverc_x509_check_signature_from(NULL, NULL) != -1)
        return 1;
    if (neverc_x509_verify_signature(
            NULL, 0, NULL, 0, NULL, 0) != -1)
        return 1;
    if (neverc_x509_verify_chain(NULL, 0, NULL, NULL, 0) != -1)
        return 1;
#endif
    puts("passed");
    return 0;
}
