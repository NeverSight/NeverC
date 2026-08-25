#include "neverc/std/net/url.h"
#include "neverc/std/os/user.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char scheme[64];
    char user[128];
    char password[128];
    char host[256];
    char port[16];
    char path[1024];
    char raw_query[2048];
    char fragment[256];
} v3389_url_t;

typedef struct {
    char uid[64];
    char gid[64];
    char username[256];
    char name[256];
    char home_dir[1024];
} v3389_user_t;

_Static_assert(sizeof(v3389_url_t) == 3920, "v3389 URL size");
_Static_assert(_Alignof(v3389_url_t) == 1, "v3389 URL alignment");
_Static_assert(sizeof(v3389_user_t) == 1664, "v3389 user size");
_Static_assert(_Alignof(v3389_user_t) == 1, "v3389 user alignment");

#define ABI_TYPE_EQ(current, legacy)                                      \
    _Static_assert(sizeof(current) == sizeof(legacy), "v3389 size ABI");  \
    _Static_assert(_Alignof(current) == _Alignof(legacy),                 \
                   "v3389 alignment ABI")
#define ABI_FIELD_EQ(current, legacy, field)                              \
    _Static_assert(offsetof(current, field) == offsetof(legacy, field),   \
                   "v3389 field offset ABI")

ABI_TYPE_EQ(neverc_url_t, v3389_url_t);
ABI_FIELD_EQ(neverc_url_t, v3389_url_t, scheme);
ABI_FIELD_EQ(neverc_url_t, v3389_url_t, user);
ABI_FIELD_EQ(neverc_url_t, v3389_url_t, password);
ABI_FIELD_EQ(neverc_url_t, v3389_url_t, host);
ABI_FIELD_EQ(neverc_url_t, v3389_url_t, port);
ABI_FIELD_EQ(neverc_url_t, v3389_url_t, path);
ABI_FIELD_EQ(neverc_url_t, v3389_url_t, raw_query);
ABI_FIELD_EQ(neverc_url_t, v3389_url_t, fragment);

ABI_TYPE_EQ(neverc_user_t, v3389_user_t);
ABI_FIELD_EQ(neverc_user_t, v3389_user_t, uid);
ABI_FIELD_EQ(neverc_user_t, v3389_user_t, gid);
ABI_FIELD_EQ(neverc_user_t, v3389_user_t, username);
ABI_FIELD_EQ(neverc_user_t, v3389_user_t, name);
ABI_FIELD_EQ(neverc_user_t, v3389_user_t, home_dir);

enum { CANARY_SIZE = 32 };

typedef struct {
    uint8_t before[CANARY_SIZE];
    union {
        neverc_url_t now;
        v3389_url_t old;
    } value;
    uint8_t after[CANARY_SIZE];
} guarded_url_t;

typedef struct {
    uint8_t before[CANARY_SIZE];
    union {
        neverc_user_t now;
        v3389_user_t old;
    } value;
    uint8_t after[CANARY_SIZE];
} guarded_user_t;

static void set_canaries(uint8_t before[CANARY_SIZE],
                         uint8_t after[CANARY_SIZE]) {
    memset(before, 0xa5, CANARY_SIZE);
    memset(after, 0x5a, CANARY_SIZE);
}

static int canaries_ok(const uint8_t before[CANARY_SIZE],
                       const uint8_t after[CANARY_SIZE]) {
    for (size_t i = 0; i < CANARY_SIZE; i++) {
        if (before[i] != 0xa5 || after[i] != 0x5a)
            return 0;
    }
    return 1;
}

static int memory_is_zero(const void *memory, size_t size) {
    const uint8_t *bytes = (const uint8_t *)memory;
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != 0)
            return 0;
    }
    return 1;
}

static int url_released_abi_and_empty_delimiters(void) {
    guarded_url_t guarded;
    char rendered[128];
    char full_password_url[160];
    char full_query_url[2050];
    set_canaries(guarded.before, guarded.after);
    memset(&guarded.value, 0xcc, sizeof(guarded.value));

    if (neverc_url_parse(&guarded.value.now,
                         "http://user:@host:/path?") != 0 ||
        !canaries_ok(guarded.before, guarded.after) ||
        !neverc_url_has_password(&guarded.value.now) ||
        !neverc_url_has_port(&guarded.value.now) ||
        !neverc_url_has_query(&guarded.value.now) ||
        guarded.value.now.password[0] != '\0' ||
        guarded.value.now.port[0] != '\0' ||
        guarded.value.now.raw_query[0] != '\0' ||
        neverc_url_string(&guarded.value.now, rendered, sizeof(rendered)) !=
            (int)strlen("http://user:@host:/path?") ||
        strcmp(rendered, "http://user:@host:/path?") != 0)
        return -1;

    neverc_url_t copied = guarded.value.now;
    if (!neverc_url_has_password(&copied) ||
        !neverc_url_has_port(&copied) ||
        !neverc_url_has_query(&copied))
        return -1;

    if (neverc_url_parse(&guarded.value.now, "http://user@host/path") != 0 ||
        neverc_url_has_password(&guarded.value.now) ||
        neverc_url_has_port(&guarded.value.now) ||
        neverc_url_has_query(&guarded.value.now) ||
        neverc_url_has_password(NULL) || neverc_url_has_port(NULL) ||
        neverc_url_has_query(NULL) ||
        !canaries_ok(guarded.before, guarded.after))
        return -1;

    memcpy(full_password_url, "http://u:", 9);
    memset(full_password_url + 9, 'p', 127);
    memcpy(full_password_url + 136, "@host/", 7);
    if (neverc_url_parse(&guarded.value.now, full_password_url) != 0 ||
        strlen(guarded.value.now.password) != 127 ||
        !neverc_url_has_password(&guarded.value.now))
        return -1;

    full_query_url[0] = '/';
    full_query_url[1] = '?';
    memset(full_query_url + 2, 'q', 2047);
    full_query_url[2049] = '\0';
    if (neverc_url_parse(&guarded.value.now, full_query_url) != 0 ||
        strlen(guarded.value.now.raw_query) != 2047 ||
        !neverc_url_has_query(&guarded.value.now) ||
        !canaries_ok(guarded.before, guarded.after))
        return -1;
    return 0;
}

static int user_released_abi_and_transactional_failure(void) {
    guarded_user_t guarded;
    int result;
    set_canaries(guarded.before, guarded.after);

    memset(&guarded.value, 0xcc, sizeof(guarded.value));
    result = neverc_user_current(&guarded.value.now);
    if (!canaries_ok(guarded.before, guarded.after) ||
        (result != 0 && !memory_is_zero(&guarded.value.now,
                                        sizeof(guarded.value.now))))
        return -1;

    memset(&guarded.value, 0xcc, sizeof(guarded.value));
    if (neverc_user_lookup("", &guarded.value.now) != -1 ||
        !memory_is_zero(&guarded.value.now, sizeof(guarded.value.now)) ||
        !canaries_ok(guarded.before, guarded.after))
        return -1;

    memset(&guarded.value, 0xcc, sizeof(guarded.value));
    if (neverc_user_lookup_id(-1, &guarded.value.now) != -1 ||
        !memory_is_zero(&guarded.value.now, sizeof(guarded.value.now)) ||
        !canaries_ok(guarded.before, guarded.after))
        return -1;
    return 0;
}

int main(void) {
    if (url_released_abi_and_empty_delimiters() != 0 ||
        user_released_abi_and_transactional_failure() != 0) {
        fputs("released URL/user ABI canary failed\n", stderr);
        return 1;
    }
    puts("passed");
    return 0;
}
