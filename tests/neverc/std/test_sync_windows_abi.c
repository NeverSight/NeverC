#include "neverc/std/sync.h"

#if !defined(_WIN32)

#include <stdio.h>

int main(void) {
    puts("passed");
    return 0;
}

#else

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef struct { SRWLOCK srw; } v3389_mutex_t;
typedef struct {
    volatile int32_t done;
    CRITICAL_SECTION mu;
} v3389_once_t;
typedef struct {
    CONDITION_VARIABLE cond;
    SRWLOCK *srw;
} v3389_cond_t;

#define ABI_TYPE_EQ(current, legacy)                                      \
    _Static_assert(sizeof(current) == sizeof(legacy), "v3389 size ABI"); \
    _Static_assert(_Alignof(current) == _Alignof(legacy),                 \
                   "v3389 alignment ABI")
#define ABI_FIELD_EQ(current, legacy, field)                              \
    _Static_assert(offsetof(current, field) == offsetof(legacy, field),   \
                   "v3389 field offset ABI")

ABI_TYPE_EQ(neverc_mutex_t, v3389_mutex_t);
ABI_FIELD_EQ(neverc_mutex_t, v3389_mutex_t, srw);
ABI_TYPE_EQ(neverc_once_t, v3389_once_t);
ABI_FIELD_EQ(neverc_once_t, v3389_once_t, done);
ABI_FIELD_EQ(neverc_once_t, v3389_once_t, mu);
ABI_TYPE_EQ(neverc_cond_t, v3389_cond_t);
ABI_FIELD_EQ(neverc_cond_t, v3389_cond_t, cond);
ABI_FIELD_EQ(neverc_cond_t, v3389_cond_t, srw);

#if defined(_WIN64)
_Static_assert(sizeof(neverc_mutex_t) == 8, "v3389 Win64 mutex size");
_Static_assert(sizeof(neverc_once_t) == 48, "v3389 Win64 once size");
_Static_assert(offsetof(neverc_once_t, mu) == 8,
               "v3389 Win64 once.mu offset");
_Static_assert(sizeof(neverc_cond_t) == 16, "v3389 Win64 cond size");
_Static_assert(offsetof(neverc_cond_t, srw) == 8,
               "v3389 Win64 cond.srw offset");
#else
_Static_assert(sizeof(neverc_mutex_t) == 4, "v3389 Win32 mutex size");
_Static_assert(sizeof(neverc_once_t) == 28, "v3389 Win32 once size");
_Static_assert(offsetof(neverc_once_t, mu) == 4,
               "v3389 Win32 once.mu offset");
_Static_assert(sizeof(neverc_cond_t) == 8, "v3389 Win32 cond size");
_Static_assert(offsetof(neverc_cond_t, srw) == 4,
               "v3389 Win32 cond.srw offset");
#endif

#undef ABI_FIELD_EQ
#undef ABI_TYPE_EQ

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "check failed at line %d: %s\n",            \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while (0)

#define CANARY_SIZE 32
#define CANARY_BYTE 0xa5

typedef struct {
    unsigned char before[CANARY_SIZE];
    neverc_mutex_t value;
    unsigned char after[CANARY_SIZE];
} guarded_mutex_t;

typedef struct {
    unsigned char before[CANARY_SIZE];
    neverc_once_t value;
    unsigned char after[CANARY_SIZE];
} guarded_once_t;

typedef struct {
    unsigned char before[CANARY_SIZE];
    neverc_cond_t value;
    unsigned char after[CANARY_SIZE];
} guarded_cond_t;

static int canary_ok(const unsigned char *bytes, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (bytes[i] != CANARY_BYTE)
            return 0;
    }
    return 1;
}

static int mutex_canaries_ok(const guarded_mutex_t *guarded) {
    return canary_ok(guarded->before, sizeof(guarded->before)) &&
           canary_ok(guarded->after, sizeof(guarded->after));
}

static int once_canaries_ok(const guarded_once_t *guarded) {
    return canary_ok(guarded->before, sizeof(guarded->before)) &&
           canary_ok(guarded->after, sizeof(guarded->after));
}

static int cond_canaries_ok(const guarded_cond_t *guarded) {
    return canary_ok(guarded->before, sizeof(guarded->before)) &&
           canary_ok(guarded->after, sizeof(guarded->after));
}

static neverc_mutex_t *wrong_owner_mutex;

static DWORD WINAPI wrong_owner_unlock(LPVOID unused) {
    (void)unused;
    neverc_mutex_unlock(wrong_owner_mutex);
    return 0;
}

static guarded_mutex_t cond_mutex;
static guarded_cond_t cond_value;

static DWORD WINAPI signal_waiter(LPVOID unused) {
    (void)unused;
    neverc_mutex_lock(&cond_mutex.value);
    neverc_cond_signal(&cond_value.value);
    neverc_mutex_unlock(&cond_mutex.value);
    return 0;
}

static int once_runs;

static void run_once(void) {
    ++once_runs;
}

int main(void) {
    guarded_mutex_t mutex;
    memset(&mutex, CANARY_BYTE, sizeof(mutex));
    CHECK(neverc_mutex_init(&mutex.value) == 0);
    CHECK(mutex_canaries_ok(&mutex));

    neverc_mutex_lock(&mutex.value);
    wrong_owner_mutex = &mutex.value;
    HANDLE wrong_owner =
        CreateThread(NULL, 0, wrong_owner_unlock, NULL, 0, NULL);
    CHECK(wrong_owner != NULL);
    CHECK(WaitForSingleObject(wrong_owner, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(wrong_owner);
    CHECK(!neverc_mutex_trylock(&mutex.value));
    neverc_mutex_unlock(&mutex.value);
    CHECK(neverc_mutex_trylock(&mutex.value));
    neverc_mutex_unlock(&mutex.value);
    neverc_mutex_destroy(&mutex.value);
    CHECK(mutex_canaries_ok(&mutex));

    /* The same public address gets a fresh private owner record. */
    CHECK(neverc_mutex_init(&mutex.value) == 0);
    neverc_mutex_unlock(&mutex.value);
    CHECK(neverc_mutex_trylock(&mutex.value));
    neverc_mutex_unlock(&mutex.value);
    neverc_mutex_destroy(&mutex.value);
    CHECK(mutex_canaries_ok(&mutex));

    guarded_once_t once;
    memset(&once, CANARY_BYTE, sizeof(once));
    once_runs = 0;
    CHECK(neverc_once_init(&once.value) == 0);
    CHECK(once_canaries_ok(&once));
    neverc_once_do(&once.value, run_once);
    neverc_once_do(&once.value, run_once);
    CHECK(once_runs == 1);
    neverc_once_destroy(&once.value);
    CHECK(once_canaries_ok(&once));

    CHECK(neverc_once_init(&once.value) == 0);
    CHECK(once.value.done == 0);
    neverc_once_do(&once.value, run_once);
    CHECK(once_runs == 2);
    neverc_once_destroy(&once.value);
    CHECK(once_canaries_ok(&once));

    memset(&cond_mutex, CANARY_BYTE, sizeof(cond_mutex));
    memset(&cond_value, CANARY_BYTE, sizeof(cond_value));
    CHECK(neverc_mutex_init(&cond_mutex.value) == 0);
    CHECK(neverc_cond_init(&cond_value.value, &cond_mutex.value) == 0);
    CHECK(cond_value.value.srw == &cond_mutex.value.srw);
    CHECK(mutex_canaries_ok(&cond_mutex));
    CHECK(cond_canaries_ok(&cond_value));

    neverc_mutex_lock(&cond_mutex.value);
    HANDLE signaler = CreateThread(NULL, 0, signal_waiter, NULL, 0, NULL);
    CHECK(signaler != NULL);
    neverc_cond_wait(&cond_value.value);
    /* cond_wait must restore the private owner before returning. */
    CHECK(!neverc_mutex_trylock(&cond_mutex.value));
    neverc_mutex_unlock(&cond_mutex.value);
    CHECK(WaitForSingleObject(signaler, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(signaler);
    CHECK(neverc_mutex_trylock(&cond_mutex.value));
    neverc_mutex_unlock(&cond_mutex.value);

    neverc_cond_destroy(&cond_value.value);
    neverc_mutex_destroy(&cond_mutex.value);
    CHECK(mutex_canaries_ok(&cond_mutex));
    CHECK(cond_canaries_ok(&cond_value));
    puts("passed");
    return 0;
}

#endif
