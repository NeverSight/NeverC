#include <stdio.h>
#include <stdlib.h>

static void *failing_malloc(size_t size) {
    (void)size;
    return NULL;
}

#define malloc failing_malloc
#include "../../../std/src/encoding/asn1/asn1.c"
#undef malloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    static const uint8_t encoded[] = {
        0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01
    };
    neverc_asn1_element_t element;
    CHECK(neverc_asn1_decode_element(
              encoded, sizeof(encoded), &element) == (int)sizeof(encoded));
    CHECK(neverc_asn1_decode_oid(&element) == NULL);
    puts("passed");
    return 0;
}
