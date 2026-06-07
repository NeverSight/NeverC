#ifndef NEVERC_HASH_H
#define NEVERC_HASH_H

#ifdef __neverc__

struct __neverc_std_crc32_t { char __tag; };
struct __neverc_std_crc64_t { char __tag; };
struct __neverc_std_fnv_t { char __tag; };
struct __neverc_std_adler32_t { char __tag; };
struct __neverc_std_maphash_t { char __tag; };

struct __neverc_std_hash_t {
    struct __neverc_std_crc32_t crc32;
    struct __neverc_std_crc64_t crc64;
    struct __neverc_std_fnv_t fnv;
    struct __neverc_std_adler32_t adler32;
    struct __neverc_std_maphash_t maphash;
};

extern struct __neverc_std_hash_t __neverc_mod_hash;
extern struct __neverc_std_hash_t hash;
#endif /* __neverc__ */

#endif /* NEVERC_HASH_H */
