#include "neverc/std/crypto/x509.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define X509_POOL_MAX_CHAIN_DEPTH 16U
#define X509_POOL_MAX_SIGNATURE_CHECKS 100U

typedef struct {
    uint8_t *der;
    size_t der_len;
    neverc_x509_cert_t certificate;
} x509_pool_entry_t;

struct neverc_x509_cert_pool {
    x509_pool_entry_t *entries;
    size_t count;
    size_t capacity;
};

typedef struct {
    const neverc_x509_cert_pool_t *intermediates;
    const neverc_x509_cert_pool_t *roots;
    const neverc_x509_time_t *moment;
    const char *hostname;
    uint32_t required_ext_key_usage;
    const neverc_x509_cert_t *chain[X509_POOL_MAX_CHAIN_DEPTH];
    size_t chain_len;
    size_t signature_checks;
} x509_chain_builder_t;

static int x509_pool_raw_equal(const neverc_x509_cert_t *left,
                               const neverc_x509_cert_t *right) {
    return left && right && left->raw && right->raw &&
           left->raw_len == right->raw_len &&
           memcmp(left->raw, right->raw, left->raw_len) == 0;
}

static int x509_pool_name_equal(const uint8_t *left, size_t left_len,
                                const uint8_t *right, size_t right_len) {
    return left && right && left_len == right_len &&
           memcmp(left, right, left_len) == 0;
}

static int x509_pool_contains(
    const neverc_x509_cert_pool_t *pool,
    const neverc_x509_cert_t *certificate) {
    if (!pool || !certificate)
        return 0;
    for (size_t i = 0; i < pool->count; ++i) {
        if (x509_pool_raw_equal(
                &pool->entries[i].certificate, certificate))
            return 1;
    }
    return 0;
}

static int x509_builder_already_contains(
    const x509_chain_builder_t *builder,
    const neverc_x509_cert_t *certificate) {
    for (size_t i = 0; i < builder->chain_len; ++i) {
        if (x509_pool_raw_equal(builder->chain[i], certificate))
            return 1;
    }
    return 0;
}

static int x509_builder_verify_current(
    const x509_chain_builder_t *builder) {
    return neverc_x509_verify_chain(
        builder->chain, builder->chain_len, builder->moment,
        builder->hostname, builder->required_ext_key_usage);
}

static int x509_builder_search(x509_chain_builder_t *builder);

static int x509_builder_try_pool(
    x509_chain_builder_t *builder,
    const neverc_x509_cert_pool_t *pool,
    int candidates_are_roots) {
    if (!builder || !pool || builder->chain_len == 0 ||
        builder->chain_len >= X509_POOL_MAX_CHAIN_DEPTH)
        return 0;

    const neverc_x509_cert_t *child =
        builder->chain[builder->chain_len - 1];
    for (size_t i = 0; i < pool->count; ++i) {
        const neverc_x509_cert_t *candidate =
            &pool->entries[i].certificate;
        if (!x509_pool_name_equal(
                child->raw_issuer, child->raw_issuer_len,
                candidate->raw_subject, candidate->raw_subject_len) ||
            x509_builder_already_contains(builder, candidate))
            continue;

        if (builder->signature_checks >=
            X509_POOL_MAX_SIGNATURE_CHECKS)
            return 0;
        ++builder->signature_checks;
        if (neverc_x509_check_signature_from(
                child, candidate) != 0)
            continue;

        builder->chain[builder->chain_len++] = candidate;
        int verified;
        if (candidates_are_roots) {
            verified = x509_builder_verify_current(builder) == 0;
        } else {
            verified = x509_builder_search(builder);
        }
        --builder->chain_len;
        if (verified)
            return 1;
    }
    return 0;
}

static int x509_builder_search(x509_chain_builder_t *builder) {
    if (!builder || builder->chain_len == 0)
        return 0;

    const neverc_x509_cert_t *current =
        builder->chain[builder->chain_len - 1];
    if (x509_pool_contains(builder->roots, current))
        return x509_builder_verify_current(builder) == 0;
    if (builder->chain_len >= X509_POOL_MAX_CHAIN_DEPTH)
        return 0;

    /* Match Go crypto/x509's preference: a directly trusted parent wins over
     * an intermediate with the same subject. */
    if (x509_builder_try_pool(builder, builder->roots, 1))
        return 1;
    return x509_builder_try_pool(
        builder, builder->intermediates, 0);
}

neverc_x509_cert_pool_t *neverc_x509_cert_pool_new(void) {
    return (neverc_x509_cert_pool_t *)calloc(
        1, sizeof(neverc_x509_cert_pool_t));
}

void neverc_x509_cert_pool_free(neverc_x509_cert_pool_t *pool) {
    if (!pool)
        return;
    for (size_t i = 0; i < pool->count; ++i) {
        neverc_x509_cert_free(&pool->entries[i].certificate);
        free(pool->entries[i].der);
    }
    free(pool->entries);
    free(pool);
}

int neverc_x509_cert_pool_add_der(neverc_x509_cert_pool_t *pool,
                                  const uint8_t *der, size_t der_len) {
    if (!pool || !der || der_len == 0)
        return -1;
    for (size_t i = 0; i < pool->count; ++i) {
        if (pool->entries[i].der_len == der_len &&
            memcmp(pool->entries[i].der, der, der_len) == 0)
            return 0;
    }

    uint8_t *der_copy = (uint8_t *)malloc(der_len);
    if (!der_copy)
        return -1;
    memcpy(der_copy, der, der_len);

    neverc_x509_cert_t certificate;
    if (neverc_x509_parse_certificate(
            &certificate, der_copy, der_len) != 0) {
        neverc_x509_cert_free(&certificate);
        free(der_copy);
        return -1;
    }

    if (pool->count == pool->capacity) {
        size_t new_capacity =
            pool->capacity == 0 ? 8 : pool->capacity * 2;
        if (new_capacity < pool->capacity ||
            new_capacity > SIZE_MAX / sizeof(*pool->entries)) {
            neverc_x509_cert_free(&certificate);
            free(der_copy);
            return -1;
        }
        x509_pool_entry_t *entries =
            (x509_pool_entry_t *)realloc(
                pool->entries,
                new_capacity * sizeof(*pool->entries));
        if (!entries) {
            neverc_x509_cert_free(&certificate);
            free(der_copy);
            return -1;
        }
        pool->entries = entries;
        pool->capacity = new_capacity;
    }

    x509_pool_entry_t *entry = &pool->entries[pool->count++];
    entry->der = der_copy;
    entry->der_len = der_len;
    entry->certificate = certificate;
    return 0;
}

size_t neverc_x509_cert_pool_count(
    const neverc_x509_cert_pool_t *pool) {
    return pool ? pool->count : 0;
}

int neverc_x509_verify_with_pools(
    const neverc_x509_cert_t *leaf,
    const neverc_x509_cert_pool_t *intermediates,
    const neverc_x509_cert_pool_t *roots,
    const neverc_x509_time_t *moment,
    const char *hostname,
    uint32_t required_ext_key_usage) {
    if (!leaf || !moment || !roots || roots->count == 0)
        return -1;

    const neverc_x509_cert_t *leaf_only[] = {leaf};
    if (neverc_x509_verify_chain(
            leaf_only, 1, moment, hostname,
            required_ext_key_usage) != 0)
        return -1;

    x509_chain_builder_t builder;
    memset(&builder, 0, sizeof(builder));
    builder.intermediates = intermediates;
    builder.roots = roots;
    builder.moment = moment;
    builder.hostname = hostname;
    builder.required_ext_key_usage = required_ext_key_usage;
    builder.chain[0] = leaf;
    builder.chain_len = 1;
    return x509_builder_search(&builder) ? 0 : -1;
}
