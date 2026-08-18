#include "neverc/std/crypto/rsa.h"
#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/sha384.h"
#include "neverc/std/crypto/sha512.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

static int hex_nibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int decode_hex(const char *hex, unsigned char *out, size_t out_len) {
    if (!hex || !out || strlen(hex) != out_len * 2)
        return -1;
    for (size_t i = 0; i < out_len; ++i) {
        int high = hex_nibble(hex[i * 2]);
        int low = hex_nibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0)
            return -1;
        out[i] = (unsigned char)((high << 4) | low);
    }
    return 0;
}

static void test_keygen(void) {
    printf("[keygen]\n");
    neverc_rsa_private_key_t key;
    neverc_rsa_private_key_init(&key);
    ASSERT_INT_EQ(neverc_rsa_generate_key(&key, 512), 0);
    ASSERT_TRUE(!neverc_bigint_is_zero(&key.pub.n));
    ASSERT_TRUE(!neverc_bigint_is_zero(&key.d));
    ASSERT_TRUE(neverc_bigint_int64(&key.pub.e) == 65537);
    ASSERT_TRUE(neverc_bigint_bit_len(&key.pub.n) == 512);

    int ks = neverc_rsa_key_size(&key.pub);
    ASSERT_TRUE(ks == 64);

    neverc_rsa_private_key_free(&key);

    {
        neverc_rsa_public_key_t pub = {0};
        ASSERT_TRUE(neverc_rsa_key_size(&pub) == -1);
        pub.n.len = 513;
        ASSERT_TRUE(neverc_rsa_key_size(&pub) == -1);
        ASSERT_TRUE(neverc_rsa_key_size(NULL) == -1);
    }
}

static void test_encrypt_decrypt(void) {
    printf("[encrypt_decrypt]\n");
    neverc_rsa_private_key_t key;
    neverc_rsa_private_key_init(&key);
    ASSERT_INT_EQ(neverc_rsa_generate_key(&key, 512), 0);

    const char *msg = "Hello RSA!";
    size_t msg_len = strlen(msg);
    unsigned char ct[256], pt[256];
    size_t ct_len, pt_len;

    ASSERT_INT_EQ(neverc_rsa_encrypt_pkcs1v15(&key.pub,
        (const unsigned char *)msg, msg_len, ct, sizeof(ct), &ct_len), 0);
    ASSERT_TRUE(ct_len == 64);

    ASSERT_INT_EQ(neverc_rsa_decrypt_pkcs1v15(&key,
        ct, ct_len, pt, sizeof(pt), &pt_len), 0);
    ASSERT_TRUE(pt_len == msg_len);
    ASSERT_TRUE(memcmp(pt, msg, msg_len) == 0);

    memset(ct, 0, ct_len);
    memset(pt, 0x5a, sizeof(pt));
    pt_len = 99;
    ASSERT_INT_EQ(neverc_rsa_decrypt_pkcs1v15(&key,
        ct, ct_len, pt, sizeof(pt), &pt_len), -1);
    ASSERT_TRUE(pt_len == 0);
    ASSERT_TRUE(pt[0] == 0x5a);

    ASSERT_INT_EQ(neverc_rsa_encrypt_pkcs1v15(&key.pub,
        (const unsigned char *)msg, msg_len, ct, sizeof(ct), &ct_len), 0);
    memset(pt, 0x5a, sizeof(pt));
    pt_len = 99;
    ASSERT_INT_EQ(neverc_rsa_decrypt_pkcs1v15(&key,
        ct, ct_len, pt, msg_len - 1, &pt_len), -1);
    ASSERT_TRUE(pt_len == 0);
    ASSERT_TRUE(pt[0] == 0x5a);

    neverc_rsa_private_key_free(&key);
}

static void test_sign_verify(void) {
    printf("[sign_verify]\n");
    neverc_rsa_private_key_t key;
    neverc_rsa_private_key_init(&key);
    ASSERT_INT_EQ(neverc_rsa_generate_key(&key, 1024), 0);

    const char *msg = "Sign this message";
    unsigned char hash[32];
    neverc_sha256_sum((const unsigned char *)msg, strlen(msg), hash);

    unsigned char sig[256];
    size_t sig_len;
    ASSERT_INT_EQ(neverc_rsa_sign_pkcs1v15_sha256(&key, hash, 32,
        sig, sizeof(sig), &sig_len), 0);
    ASSERT_TRUE(sig_len == 128);

    ASSERT_INT_EQ(neverc_rsa_verify_pkcs1v15_sha256(&key.pub, hash, 32,
        sig, sig_len), 0);

    hash[0] ^= 0xFF;
    ASSERT_TRUE(neverc_rsa_verify_pkcs1v15_sha256(&key.pub, hash, 32,
        sig, sig_len) != 0);

    neverc_bigint_t saved_dp;
    neverc_bigint_init(&saved_dp);
    neverc_bigint_set(&saved_dp, &key.dp);
    neverc_bigint_set_int64(&key.dp, 0);
    sig_len = 99;
    ASSERT_INT_EQ(neverc_rsa_sign_pkcs1v15_sha256(&key, hash, 32,
        sig, sizeof(sig), &sig_len), -1);
    ASSERT_TRUE(sig_len == 0);
    neverc_bigint_set(&key.dp, &saved_dp);
    neverc_bigint_free(&saved_dp);

    neverc_rsa_private_key_free(&key);
}

static void test_verify_pkcs1v15_sha384_sha512(void) {
    printf("[verify_pkcs1v15_sha384_sha512]\n");
    /* Generated with cryptography/OpenSSL PKCS1v15 signing. */
    static const char modulus[] =
        "A18AB912DF6FDEF885B60C60AF97C246A7447422BF6B12CE2DCA02351413D797"
        "D17D64976A773EFFF2525AE0E723422D616C1DFB940979B1DFCFA106CB6B0560"
        "6DC5763ADA5905387E2346F77EE8AE856A23BAE8FB29429D058EC607125CA4AB"
        "D4CDA43CD4CD927C924B0743A7A038B61E47E84446C83E3745CD257B6FA7359"
        "C5005FA99178229AB4203C87CFF98C27420E7F428404EB8223FCFBFAE8386747"
        "66D727023E08A1321B63577DD6C857ADAF09C68DFCDB3972847A96641CEBF60C"
        "21467593F4FEE2E5F0FC8B04F3432371D70B34568243108EE1BCABF176B3BF8"
        "A863B3B869292EE988C826D6D205B95FC0A66528FFE91533372F138F0482C47"
        "C2F";
    static const char signature_sha384_hex[] =
        "3C1AE42629F2DB2AC53A95ABCC62E75F5D2C93E9D4ACFE86784D1F2D2C32A5"
        "AFF7C83A682629D84FF975E2E6C4A4F4CA586FEC1C71863F80CF894B049FF884"
        "FE344B283B99D284F9447EEDBC76AFAC0DAC15E9E1B1C9BAE4B454998200FBB"
        "2A1EFC78E8121F9751AAF450B81C4FA5A8538E81108DBFFF92EE5C37C3D3AF"
        "6FF51749B6C62A4EDC40543FC4ECD884E3604B3277F1FA56474EFE8D8EFBAF9"
        "FA9DE2C08B8105B7E835D948C5B7107AFA2A8F68FF97D736472B1CB1CF4BB3"
        "9E2FCDEAC1737283AC0FDD4CAE66D3C7EB52C6CFC7C640A9AFA9983B76E6FD"
        "43646C6B0ABF7D04D8D5CB97906EF24CD1299CEBD3D402073BC2418038BD123"
        "EF4C8DE00C9";
    static const char signature_sha512_hex[] =
        "672B21134C768FFF7549BB6F22A78B3B7BFBBA593FB62A75C886E4DFC08C288"
        "F256ACCD820D1BCC2377A67E86ECEA4E9DA3A36CBE9F4612133609FA6E77B19"
        "22592D37C12F1660F47527EACA2D6397884F2B9E6EB93B7A062289BB8AA0E9"
        "B76E41FF6A6F7C8192FBC6903E66D55E0931FA73039E17B1B58883FF9051E6B"
        "46906F2D928E91AE82371DD87F23386871430C247B50B28FA51198027D8855D"
        "E6C9647DA6905F62C734F76F8354D978E39637FBDCDD3CA70C92AAE5D052ED7"
        "8589D620C6EAC1BAF008722528ECF02B10F7D6BF9F554B30E02F1B75E100EF"
        "11813B9A1C0078546367954B23F14C43366A44B10B007D7C1C80A704AF5D826"
        "6836CBFDAE";
    static const unsigned char message[] =
        "NeverC RSA PKCS1 SHA-2 test vector";

    neverc_rsa_public_key_t public_key;
    neverc_rsa_public_key_init(&public_key);
    ASSERT_INT_EQ(
        neverc_bigint_set_string(&public_key.n, modulus, 16), 0);
    neverc_bigint_set_uint64(&public_key.e, 65537);

    unsigned char signature_sha384[256];
    unsigned char signature_sha512[256];
    ASSERT_INT_EQ(
        decode_hex(signature_sha384_hex, signature_sha384,
                   sizeof(signature_sha384)),
        0);
    ASSERT_INT_EQ(
        decode_hex(signature_sha512_hex, signature_sha512,
                   sizeof(signature_sha512)),
        0);

    unsigned char hash_sha384[NEVERC_SHA384_DIGEST_SIZE];
    neverc_sha384_sum(message, sizeof(message) - 1, hash_sha384);
    ASSERT_INT_EQ(
        neverc_rsa_verify_pkcs1v15_sha384(
            &public_key, hash_sha384, sizeof(hash_sha384),
            signature_sha384, sizeof(signature_sha384)),
        0);
    hash_sha384[0] ^= 1;
    ASSERT_TRUE(
        neverc_rsa_verify_pkcs1v15_sha384(
            &public_key, hash_sha384, sizeof(hash_sha384),
            signature_sha384, sizeof(signature_sha384)) != 0);

    unsigned char hash_sha512[NEVERC_SHA512_DIGEST_SIZE];
    neverc_sha512_sum(message, sizeof(message) - 1, hash_sha512);
    ASSERT_INT_EQ(
        neverc_rsa_verify_pkcs1v15_sha512(
            &public_key, hash_sha512, sizeof(hash_sha512),
            signature_sha512, sizeof(signature_sha512)),
        0);
    hash_sha512[0] ^= 1;
    ASSERT_TRUE(
        neverc_rsa_verify_pkcs1v15_sha512(
            &public_key, hash_sha512, sizeof(hash_sha512),
            signature_sha512, sizeof(signature_sha512)) != 0);

    neverc_rsa_public_key_free(&public_key);
}

static void test_verify_pss_sha256(void) {
    printf("[verify_pss_sha256]\n");
    static const unsigned char signature[128] = {
        0x57, 0x12, 0xed, 0xba, 0xdf, 0x6b, 0x26, 0x0b,
        0x78, 0x88, 0x3d, 0xd9, 0x6a, 0x04, 0x77, 0x79,
        0xab, 0x48, 0xf3, 0x0f, 0x83, 0xa9, 0x60, 0x89,
        0x81, 0x61, 0x92, 0x90, 0x4e, 0xc6, 0x73, 0x1e,
        0x92, 0xd6, 0x48, 0xfa, 0xc2, 0x9d, 0x16, 0xd7,
        0x9c, 0xb9, 0xeb, 0xf0, 0xbe, 0xa3, 0xb6, 0x1b,
        0x95, 0x3d, 0x2e, 0x02, 0xb8, 0xb2, 0xe9, 0xb5,
        0x0f, 0x82, 0xc1, 0xdf, 0x58, 0xca, 0xa6, 0x03,
        0x96, 0x41, 0xbe, 0x27, 0xd5, 0xe1, 0xa2, 0xee,
        0x63, 0xa9, 0x25, 0xc6, 0xf1, 0x6f, 0xdb, 0x70,
        0xaa, 0x74, 0xa6, 0x72, 0xa9, 0xcb, 0x04, 0x52,
        0x3e, 0x40, 0x7e, 0xc2, 0xb7, 0x3b, 0x05, 0xad,
        0x6d, 0x11, 0x9e, 0xd2, 0x2b, 0x53, 0x08, 0x98,
        0xf3, 0x76, 0x55, 0x47, 0x50, 0x3c, 0xce, 0x3f,
        0xa1, 0xe2, 0x3c, 0x63, 0xbc, 0xcf, 0x47, 0xf8,
        0x0c, 0xa5, 0x24, 0x31, 0xd4, 0x69, 0x71, 0x4e,
    };
    static const char modulus[] =
        "E46058153C0023B64C659D555395C044D1DDCA1D823FB43047305775E3C8E5F1"
        "A414F555E50EE5CD2DC1B21C5F3FBA992FBD4F6592981DFD2D08BE9E514496B"
        "B16D8A6C4841561D3027780119E392601D7E662EBE2B60E2A8663E7A70BE4AE1"
        "0DF3BF095200C86CD61BFF253627CC0A0EF20A91219A737A323FB3110B061BC4B";
    static const unsigned char message[] = "NeverC RSA-PSS test vector";

    neverc_rsa_public_key_t public_key;
    neverc_rsa_public_key_init(&public_key);
    ASSERT_INT_EQ(
        neverc_bigint_set_string(&public_key.n, modulus, 16), 0);
    neverc_bigint_set_uint64(&public_key.e, 65537);

    unsigned char hash[NEVERC_SHA256_DIGEST_SIZE];
    neverc_sha256_sum(message, sizeof(message) - 1, hash);
    ASSERT_INT_EQ(
        neverc_rsa_verify_pss_sha256(
            &public_key, hash, sizeof(hash),
            signature, sizeof(signature)),
        0);
    hash[0] ^= 1;
    ASSERT_TRUE(
        neverc_rsa_verify_pss_sha256(
            &public_key, hash, sizeof(hash),
            signature, sizeof(signature)) != 0);
    neverc_rsa_public_key_free(&public_key);
}

static void test_verify_pss_sha384_sha512(void) {
    printf("[verify_pss_sha384_sha512]\n");
    /*
     * Generated with cryptography 44.0.0 backed by OpenSSL. Both signatures
     * use MGF1 with the message hash and a salt equal to the digest length.
     */
    static const char modulus[] =
        "F5F29205C6250B1AD315ADCF8B1B96FE643262C289270C6A954FB0EF00AAB3E4"
        "11E823A7E260E43C457DD27704B39D17A9334A433EF36402CB1B48F2F610F45E"
        "EE29972DA84EA2D08E8DD2B511CF5C26E4790B09BBF38B24D550217B8EFAD7DF"
        "8305ECF7111FA9048838F624A3132CFC672C4586F427AA6C4991C5F8C6ADF348"
        "929C7AE741E5DDB2FDA2E3D3C2FFFF337B6FDF75FEBAD41E27757FC36FD7D8F9"
        "1D3C8972CD22DACFB3E015BE41F0AED49F675CDD5566B64FC07E69E3AD9FCDAD"
        "A4A75E91FF0E6A5C9D7D950F0F000AE3AB206C064E5F4FB96069671F46F5ABC4"
        "4864E98B4734B88F56B2420733B4A785C61724FC33290B961C49321883D4AE23";
    static const char signature_sha384_hex[] =
        "0621F65F1B1BD84F3799BB170779051B951A83B79FCB45476B8B3CFBA8D7735D"
        "6F0F4DA09EA0E7D8F8C39891A54E73D529C2C5A5ABBB2DCB20821D479F60C6C6"
        "439252D1C95CC103FBCE069947CC728BF1A7D38FC5AFD9E28D7F16B6D34B6ED4"
        "0DAA53DFA59F3C36E5428E9BCEB2F7EFC8471B68D55931950A25AF7A74E9D557"
        "6FAE28267BEE0F307E4176C6713232779E792715D2F1965933E1C069DE570E77"
        "D6326D394E634DC347212D0E5793EBD70E861E9064035DB268E8AFC02D502636"
        "83898493B6DCFC36A549E2A4D770E65BA6E6DEAB2CDEEDA1F701C2AF761570A1"
        "DA045930AB5DED253CE74658350F0B13F02D3539D469A4C2521429ADA02DACA7";
    static const char signature_sha512_hex[] =
        "E206A3788B002E2218591C4424B99333838A773ECF21B064A8620D4A3C3DB370"
        "203FA67DB8DD946DF4C670C9F95841BFE64ED053C264EC5E3E03DCA32D2D3D41"
        "86378EC2C0E57DF4D04D554053AD97F5FF9CA72B2296C43118D5067E53D52120"
        "92DBCDCBBF1EA8808C669075C5CF13BB1D5C17B057344B2D83A1F168870C5DF0"
        "150C981315DED866F47BEECAB4E0C7BAE5FFBED37DD9ACA4C06FD9EE326A1C56"
        "AEF13C90982409EE524BF4FFECC34978D045F1E7A58605B1508D57BBB778CCB5"
        "AA83F80325B9E3450F38545BF0DB57C304E3B79C8FA4D7B5AFB656C45E82E6D4"
        "89B0761D8F883A6DF62544BDB7589F862C65345F59D8EF8DD33103566ACF9429";
    static const unsigned char message[] =
        "NeverC RSA-PSS SHA-2 test vector";

    neverc_rsa_public_key_t public_key;
    neverc_rsa_public_key_init(&public_key);
    ASSERT_INT_EQ(
        neverc_bigint_set_string(&public_key.n, modulus, 16), 0);
    neverc_bigint_set_uint64(&public_key.e, 65537);

    unsigned char signature_sha384[256];
    unsigned char signature_sha512[256];
    ASSERT_INT_EQ(
        decode_hex(signature_sha384_hex, signature_sha384,
                   sizeof(signature_sha384)),
        0);
    ASSERT_INT_EQ(
        decode_hex(signature_sha512_hex, signature_sha512,
                   sizeof(signature_sha512)),
        0);

    unsigned char hash_sha384[NEVERC_SHA384_DIGEST_SIZE];
    neverc_sha384_sum(message, sizeof(message) - 1, hash_sha384);
    ASSERT_INT_EQ(
        neverc_rsa_verify_pss_sha384(
            &public_key, hash_sha384, sizeof(hash_sha384),
            signature_sha384, sizeof(signature_sha384)),
        0);
    hash_sha384[0] ^= 1;
    ASSERT_TRUE(
        neverc_rsa_verify_pss_sha384(
            &public_key, hash_sha384, sizeof(hash_sha384),
            signature_sha384, sizeof(signature_sha384)) != 0);
    hash_sha384[0] ^= 1;
    ASSERT_TRUE(
        neverc_rsa_verify_pss_sha384(
            &public_key, hash_sha384, sizeof(hash_sha384) - 1,
            signature_sha384, sizeof(signature_sha384)) != 0);

    unsigned char hash_sha512[NEVERC_SHA512_DIGEST_SIZE];
    neverc_sha512_sum(message, sizeof(message) - 1, hash_sha512);
    ASSERT_INT_EQ(
        neverc_rsa_verify_pss_sha512(
            &public_key, hash_sha512, sizeof(hash_sha512),
            signature_sha512, sizeof(signature_sha512)),
        0);
    hash_sha512[0] ^= 1;
    ASSERT_TRUE(
        neverc_rsa_verify_pss_sha512(
            &public_key, hash_sha512, sizeof(hash_sha512),
            signature_sha512, sizeof(signature_sha512)) != 0);
    hash_sha512[0] ^= 1;
    ASSERT_TRUE(
        neverc_rsa_verify_pss_sha512(
            &public_key, hash_sha512, sizeof(hash_sha512) - 1,
            signature_sha512, sizeof(signature_sha512)) != 0);

    neverc_rsa_public_key_free(&public_key);
}

static void test_reject_weak_public_exponent(void) {
    printf("[reject_weak_public_exponent]\n");
    /* 1024-bit n from the PSS vector; e=1 makes verify the identity map. */
    static const char modulus[] =
        "E46058153C0023B64C659D555395C044D1DDCA1D823FB43047305775E3C8E5F1"
        "A414F555E50EE5CD2DC1B21C5F3FBA992FBD4F6592981DFD2D08BE9E514496B"
        "B16D8A6C4841561D3027780119E392601D7E662EBE2B60E2A8663E7A70BE4AE1"
        "0DF3BF095200C86CD61BFF253627CC0A0EF20A91219A737A323FB3110B061BC4B";
    static const unsigned char digest_info[] = {
        0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
        0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
        0x00, 0x04, 0x20
    };
    neverc_rsa_public_key_t pub;
    neverc_rsa_public_key_init(&pub);
    ASSERT_INT_EQ(neverc_bigint_set_string(&pub.n, modulus, 16), 0);
    neverc_bigint_set_int64(&pub.e, 1);

    unsigned char hash[NEVERC_SHA256_DIGEST_SIZE];
    neverc_sha256_sum((const unsigned char *)"e=1", 3, hash);

    unsigned char em[128];
    em[0] = 0x00;
    em[1] = 0x01;
    memset(em + 2, 0xff, 74);
    em[76] = 0x00;
    memcpy(em + 77, digest_info, sizeof(digest_info));
    memcpy(em + 96, hash, sizeof(hash));

    ASSERT_TRUE(neverc_rsa_verify_pkcs1v15_sha256(
                    &pub, hash, sizeof(hash), em, sizeof(em)) != 0);
    ASSERT_TRUE(neverc_rsa_verify_pss_sha256(
                    &pub, hash, sizeof(hash), em, sizeof(em)) != 0);

    unsigned char out[128];
    size_t out_len = 99;
    ASSERT_INT_EQ(neverc_rsa_encrypt_pkcs1v15(
                      &pub, hash, 8, out, sizeof(out), &out_len), -1);
    ASSERT_TRUE(out_len == 0);

    neverc_bigint_set_int64(&pub.e, 2);
    ASSERT_TRUE(neverc_rsa_verify_pkcs1v15_sha256(
                    &pub, hash, sizeof(hash), em, sizeof(em)) != 0);

    neverc_rsa_public_key_free(&pub);
}

static void test_generate_key_rejects_oversized_bits(void) {
    printf("[generate_key_oversized_bits]\n");
    neverc_rsa_private_key_t key;
    neverc_rsa_private_key_init(&key);
    ASSERT_INT_EQ(neverc_rsa_generate_key(&key, 511), -1);
    ASSERT_INT_EQ(neverc_rsa_generate_key(&key, 16385), -1);
    ASSERT_TRUE(neverc_bigint_is_zero(&key.pub.n));
    neverc_rsa_private_key_free(&key);
}

static void test_reject_oversized_public_exponent(void) {
    printf("[reject_oversized_public_exponent]\n");
    /* 1024-bit n from the PSS vector; e = 2^256+1 is odd and < n but
     * exceeds the 256-bit FIPS cap that rsa_public_ok now enforces. */
    static const char modulus[] =
        "E46058153C0023B64C659D555395C044D1DDCA1D823FB43047305775E3C8E5F1"
        "A414F555E50EE5CD2DC1B21C5F3FBA992FBD4F6592981DFD2D08BE9E514496B"
        "B16D8A6C4841561D3027780119E392601D7E662EBE2B60E2A8663E7A70BE4AE1"
        "0DF3BF095200C86CD61BFF253627CC0A0EF20A91219A737A323FB3110B061BC4B";
    neverc_rsa_public_key_t pub;
    neverc_rsa_public_key_init(&pub);
    ASSERT_INT_EQ(neverc_bigint_set_string(&pub.n, modulus, 16), 0);
    ASSERT_INT_EQ(
        neverc_bigint_set_string(
            &pub.e, "1"
                    "00000000000000000000000000000000"
                    "00000000000000000000000000000001",
            16),
        0);
    ASSERT_TRUE(neverc_bigint_bit_len(&pub.e) == 257);

    unsigned char hash[NEVERC_SHA256_DIGEST_SIZE];
    neverc_sha256_sum((const unsigned char *)"e-too-wide", 10, hash);
    unsigned char sig[128];
    memset(sig, 0x03, sizeof(sig));
    ASSERT_TRUE(neverc_rsa_verify_pkcs1v15_sha256(
                    &pub, hash, sizeof(hash), sig, sizeof(sig)) != 0);
    ASSERT_TRUE(neverc_rsa_verify_pss_sha256(
                    &pub, hash, sizeof(hash), sig, sizeof(sig)) != 0);

    unsigned char out[128];
    size_t out_len = 99;
    ASSERT_INT_EQ(neverc_rsa_encrypt_pkcs1v15(
                      &pub, hash, 8, out, sizeof(out), &out_len), -1);
    ASSERT_TRUE(out_len == 0);

    neverc_rsa_public_key_free(&pub);
}

static void test_init_free(void) {
    printf("[init_free]\n");
    neverc_rsa_public_key_t pub;
    neverc_rsa_public_key_init(&pub);
    neverc_rsa_public_key_free(&pub);

    neverc_rsa_private_key_t priv;
    neverc_rsa_private_key_init(&priv);
    neverc_rsa_private_key_free(&priv);
    tests_run++; tests_passed++;
}

int main(void) {
    printf("=== NeverC crypto/rsa Tests ===\n");
    test_init_free();
    test_keygen();
    test_encrypt_decrypt();
    test_sign_verify();
    test_verify_pkcs1v15_sha384_sha512();
    test_verify_pss_sha256();
    test_verify_pss_sha384_sha512();
    test_reject_weak_public_exponent();
    test_generate_key_rejects_oversized_bits();
    test_reject_oversized_public_exponent();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
