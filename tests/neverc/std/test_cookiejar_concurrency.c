#include "neverc/std/net/http/cookiejar.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READER_COUNT 4
#define WRITER_ITERATIONS 20000

static neverc_cookiejar_t *shared_jar;
static int start_flag;
static int stop_flag;
static int failed_flag;

static void *reader(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&start_flag, __ATOMIC_ACQUIRE)) { }

    while (!__atomic_load_n(&stop_flag, __ATOMIC_ACQUIRE)) {
        char *header = neverc_cookiejar_cookie_header(
            shared_jar, "https://example.com/account");
        if (header && strcmp(header, "token=alpha") != 0 &&
            strcmp(header, "token=beta") != 0) {
            __atomic_store_n(&failed_flag, 1, __ATOMIC_RELEASE);
        }
        free(header);
    }
    return NULL;
}

static void *writer(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&start_flag, __ATOMIC_ACQUIRE)) { }

    for (int i = 0; i < WRITER_ITERATIONS; i++) {
        const char *header = (i & 1)
            ? "token=alpha; Path=/" : "token=beta; Path=/";
        neverc_cookiejar_set_cookie_header(
            shared_jar, "https://example.com/", header);
        if ((i & 7) == 0) neverc_cookiejar_clear_all(shared_jar);
    }
    __atomic_store_n(&stop_flag, 1, __ATOMIC_RELEASE);
    return NULL;
}

int main(void) {
    shared_jar = neverc_cookiejar_new();
    if (!shared_jar) return 1;

    pthread_t readers[READER_COUNT];
    pthread_t writer_thread;
    for (int i = 0; i < READER_COUNT; i++) {
        if (pthread_create(&readers[i], NULL, reader, NULL) != 0) return 1;
    }
    if (pthread_create(&writer_thread, NULL, writer, NULL) != 0) return 1;
    __atomic_store_n(&start_flag, 1, __ATOMIC_RELEASE);

    pthread_join(writer_thread, NULL);
    for (int i = 0; i < READER_COUNT; i++)
        pthread_join(readers[i], NULL);

    int failed = __atomic_load_n(&failed_flag, __ATOMIC_ACQUIRE);
    neverc_cookiejar_free(shared_jar);
    printf("cookiejar concurrency: %s\n", failed ? "FAILED" : "passed");
    return failed ? 1 : 0;
}
