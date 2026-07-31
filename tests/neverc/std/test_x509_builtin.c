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
    if (crypto.x509.cert_pool_count(NULL) != 0)
        return 2;
    if (crypto.x509.verify_with_pools(
            NULL, NULL, NULL, NULL, NULL, 0) != -1)
        return 3;
#else
    if (neverc_x509_check_signature_from(NULL, NULL) != -1)
        return 1;
    if (neverc_x509_verify_signature(
            NULL, 0, NULL, 0, NULL, 0) != -1)
        return 1;
    if (neverc_x509_verify_chain(NULL, 0, NULL, NULL, 0) != -1)
        return 1;
    if (neverc_x509_cert_pool_count(NULL) != 0)
        return 2;
    if (neverc_x509_verify_with_pools(
            NULL, NULL, NULL, NULL, NULL, 0) != -1)
        return 3;
#endif
    puts("passed");
    return 0;
}
