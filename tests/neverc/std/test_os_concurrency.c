#include "neverc/std/os.h"
#include "neverc/std/_platform.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

enum { STDIO_THREAD_COUNT = 16 };

typedef struct {
    volatile int32_t *ready;
    volatile int32_t *go;
    int status;
} stdio_thread_arg_t;

#if defined(_WIN32)
static DWORD WINAPI stdio_first_use_worker(LPVOID opaque) {
#else
static void *stdio_first_use_worker(void *opaque) {
#endif
    stdio_thread_arg_t *arg = (stdio_thread_arg_t *)opaque;
    NEVERC_ATOMIC_ADD32(arg->ready, 1);
    while (!NEVERC_ATOMIC_LOAD32(arg->go)) {
    }
    char byte = 0;
    neverc_os_file_t *input = neverc_os_stdin();
    neverc_os_file_t *output = neverc_os_stdout();
    neverc_os_file_t *error = neverc_os_stderr();
    arg->status = !input || !output || !error ||
                  neverc_os_read(input, &byte, 0) != 0 ||
                  neverc_os_write(output, &byte, 0) != 0 ||
                  neverc_os_write(error, &byte, 0) != 0;
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int test_concurrent_stdio_first_use(void) {
    volatile int32_t ready = 0;
    volatile int32_t go = 0;
    stdio_thread_arg_t args[STDIO_THREAD_COUNT];
#if defined(_WIN32)
    HANDLE threads[STDIO_THREAD_COUNT];
#else
    pthread_t threads[STDIO_THREAD_COUNT];
#endif
    int started = 0;
    for (int i = 0; i < STDIO_THREAD_COUNT; i++) {
        args[i].ready = &ready;
        args[i].go = &go;
        args[i].status = 1;
#if defined(_WIN32)
        threads[i] = CreateThread(NULL, 0, stdio_first_use_worker,
                                  &args[i], 0, NULL);
        if (!threads[i])
            break;
#else
        if (pthread_create(&threads[i], NULL, stdio_first_use_worker,
                           &args[i]) != 0)
            break;
#endif
        started++;
    }
    while (NEVERC_ATOMIC_LOAD32(&ready) < started) {
    }
    NEVERC_ATOMIC_STORE32(&go, 1);
    int failed = started != STDIO_THREAD_COUNT;
    for (int i = 0; i < started; i++) {
#if defined(_WIN32)
        if (WaitForSingleObject(threads[i], INFINITE) != WAIT_OBJECT_0)
            failed = 1;
        CloseHandle(threads[i]);
#else
        if (pthread_join(threads[i], NULL) != 0)
            failed = 1;
#endif
        if (args[i].status != 0)
            failed = 1;
    }
    return failed ? -1 : 0;
}

#if defined(_WIN32)
typedef struct {
    volatile int32_t first_read;
    volatile int32_t second_read;
    int first_status;
    int second_status;
} getenv_test_t;

static const char getenv_first_key[] = "NEVERC_GETENV_THREAD_FIRST";
static const char getenv_first_value[] = "first-thread-value";
static const char getenv_second_key[] = "NEVERC_GETENV_THREAD_SECOND";
static const char getenv_second_value[] = "second-thread-value";

static DWORD WINAPI getenv_first_worker(LPVOID opaque) {
    getenv_test_t *test = (getenv_test_t *)opaque;
    const char *value = neverc_os_getenv(getenv_first_key);
    test->first_status = !value || strcmp(value, getenv_first_value) != 0;
    NEVERC_ATOMIC_STORE32(&test->first_read, 1);
    while (!NEVERC_ATOMIC_LOAD32(&test->second_read)) {
    }
    if (!value || strcmp(value, getenv_first_value) != 0)
        test->first_status = 1;
    return 0;
}

static DWORD WINAPI getenv_second_worker(LPVOID opaque) {
    getenv_test_t *test = (getenv_test_t *)opaque;
    while (!NEVERC_ATOMIC_LOAD32(&test->first_read)) {
    }
    const char *value = NULL;
    test->second_status =
        !neverc_os_lookup_env(getenv_second_key, &value) || !value ||
        strcmp(value, getenv_second_value) != 0;
    NEVERC_ATOMIC_STORE32(&test->second_read, 1);
    return 0;
}

static int test_windows_getenv_thread_lifetime(void) {
    if (neverc_os_setenv(getenv_first_key, getenv_first_value) != 0 ||
        neverc_os_setenv(getenv_second_key, getenv_second_value) != 0)
        return -1;
    getenv_test_t test;
    test.first_read = 0;
    test.second_read = 0;
    test.first_status = 1;
    test.second_status = 1;
    HANDLE first = CreateThread(NULL, 0, getenv_first_worker,
                                &test, 0, NULL);
    HANDLE second = CreateThread(NULL, 0, getenv_second_worker,
                                 &test, 0, NULL);
    int failed = !first || !second;
    if (!first)
        NEVERC_ATOMIC_STORE32(&test.first_read, 1);
    if (!second)
        NEVERC_ATOMIC_STORE32(&test.second_read, 1);
    if (first) {
        if (WaitForSingleObject(first, INFINITE) != WAIT_OBJECT_0)
            failed = 1;
        CloseHandle(first);
    }
    if (second) {
        if (WaitForSingleObject(second, INFINITE) != WAIT_OBJECT_0)
            failed = 1;
        CloseHandle(second);
    }
    if (test.first_status != 0 || test.second_status != 0)
        failed = 1;
    if (neverc_os_unsetenv(getenv_first_key) != 0 ||
        neverc_os_unsetenv(getenv_second_key) != 0)
        failed = 1;
    return failed ? -1 : 0;
}
#endif

int main(void) {
    if (test_concurrent_stdio_first_use() != 0) {
        fputs("concurrent stdio first use failed\n", stderr);
        return 1;
    }
#if defined(_WIN32)
    if (test_windows_getenv_thread_lifetime() != 0) {
        fputs("Windows getenv thread lifetime failed\n", stderr);
        return 1;
    }
#endif
    puts("passed");
    return 0;
}
