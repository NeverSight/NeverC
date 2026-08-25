#ifndef NEVERC_X509_INTERNAL_H
#define NEVERC_X509_INTERNAL_H

#include "neverc/std/crypto/x509.h"

typedef struct {
    int present;
    char **permitted_dns_names;
    size_t permitted_dns_name_count;
    char **excluded_dns_names;
    size_t excluded_dns_name_count;
    neverc_x509_ip_network_t *permitted_ip_networks;
    size_t permitted_ip_network_count;
    neverc_x509_ip_network_t *excluded_ip_networks;
    size_t excluded_ip_network_count;
} neverc_x509_name_constraints_t;

/* Extract constraints from cert->raw. A missing raw value or absent extension
 * succeeds with present == 0. The caller always clears the result. */
int neverc_x509_extract_name_constraints(
    const neverc_x509_cert_t *cert,
    neverc_x509_name_constraints_t *constraints);
void neverc_x509_name_constraints_clear(
    neverc_x509_name_constraints_t *constraints);

#endif /* NEVERC_X509_INTERNAL_H */
