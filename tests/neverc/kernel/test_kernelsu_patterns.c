// RUN: %neverc -fsyntax-only %s
/*
 * NeverC Compiler Validation - KernelSU-specific C Patterns
 *
 * Exercises C language features found in the KernelSU driver source
 * (common/drivers/kernelsu/) that are not fully covered by other tests.
 *
 * Categories:
 *   1. Flexible array member in nested struct (apk_sign.c sdesc pattern)
 *   2. Mixed declarations after statements in blocks (kernel_compat.c)
 *   3. Multiple assignment in for-loop initializer (allowlist.c)
 *   4. __read_mostly + __aligned combined variable attributes
 *   5. Variable-length array from sizeof(local_array)
 *   6. extern declarations with empty parentheses (old-style)
 *   7. Complex #if version-gated code chains
 *   8. struct field initialization with nested macros (ATOMIC_INIT)
 *   9. __func__ in format strings
 *  10. Pointer cast patterns (__user simulation)
 *  11. DEFINE_MUTEX simulation (compound macro expanding to struct init)
 *  12. module_init / module_exit function registration macros
 *  13. Nested conditional preprocessing with version macros
 *  14. Task-lock/unlock pattern with struct save/restore
 *  15. Boolean flag patterns with static globals
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

/* ================================================================
 * Kernel-style attribute / section macros
 * ================================================================ */
#ifdef __MACH__
#define __init
#define __exit
#define __read_mostly
#else
#define __init     __attribute__((section(".init.text")))
#define __exit     __attribute__((section(".exit.text")))
#define __read_mostly __attribute__((section(".data..read_mostly")))
#endif

#define __aligned(x) __attribute__((aligned(x)))
#define __user
#define __force
#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x)   __builtin_expect(!!(x), 1)

#define EXPORT_SYMBOL(sym)
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_IMPORT_NS(x)

typedef int (*initcall_t)(void);
typedef void (*exitcall_t)(void);
#define module_init(fn) static initcall_t __initcall_##fn \
    __attribute__((unused)) = fn
#define module_exit(fn) static exitcall_t __exitcall_##fn \
    __attribute__((unused)) = fn

/* ================================================================
 * 1. Flexible array member in nested struct (apk_sign.c sdesc)
 * ================================================================ */
struct hash_desc {
    void *tfm;
    int flags;
};

struct sdesc {
    struct hash_desc shash;
    char ctx[];
};

static int test_flex_array_nested(void) {
    size_t ctx_size = 64;
    size_t total = sizeof(struct sdesc) + ctx_size;
    struct sdesc *sd = malloc(total);
    ASSERT(sd != NULL);

    sd->shash.tfm = NULL;
    sd->shash.flags = 0;
    memset(sd->ctx, 0xAB, ctx_size);
    ASSERT(sd->ctx[0] == (char)0xAB);
    ASSERT(sd->ctx[ctx_size - 1] == (char)0xAB);

    free(sd);
    return 0;
}

/* ================================================================
 * 2. Mixed declarations after statements (kernel_compat.c pattern)
 * C99 allows declarations anywhere in a block. Kernel code relies on
 * this heavily: `if (cond) { stmt; } type var = init;`
 * ================================================================ */
struct ns_fs_saved {
    void *ns;
    void *fs;
};

static int test_mixed_declarations(void) {
    int enabled = 1;
    int result = 0;

    if (enabled) {
        result = 42;
    }
    struct ns_fs_saved saved;
    saved.ns = NULL;
    saved.fs = NULL;

    if (enabled) {
        struct ns_fs_saved another;
        another.ns = &result;
        another.fs = &saved;
        result += (another.ns != NULL) ? 1 : 0;
    }
    int *fp = &result;
    if (enabled) {
        *fp += 1;
    }
    struct ns_fs_saved *ptr = &saved;
    ptr->ns = fp;

    ASSERT(result == 44);
    ASSERT(ptr->ns == fp);
    return 0;
}

/* ================================================================
 * 3. Multiple assignment in for-loop (allowlist.c pattern)
 * `for (i = j = 0; i < n; i++)`
 * ================================================================ */
static int test_multi_assign_for(void) {
    int arr[] = {10, 20, 0, 30, 0, 40};
    int filtered[6];
    int i, j;

    for (i = j = 0; i < 6; i++) {
        if (arr[i] != 0)
            filtered[j++] = arr[i];
    }
    ASSERT(j == 4);
    ASSERT(filtered[0] == 10);
    ASSERT(filtered[1] == 20);
    ASSERT(filtered[2] == 30);
    ASSERT(filtered[3] == 40);

    int a, b, c;
    a = b = c = 100;
    ASSERT(a == 100 && b == 100 && c == 100);
    return 0;
}

/* ================================================================
 * 4. __read_mostly + __aligned combined attributes on arrays
 * (allowlist.c: `static int arr[PAGE_SIZE/sizeof(int)] __read_mostly __aligned(PAGE_SIZE)`)
 * ================================================================ */
#define FAKE_PAGE_SIZE 4096

static int allow_list_arr[FAKE_PAGE_SIZE / sizeof(int)]
    __read_mostly __aligned(4096);
static int allow_list_pointer __read_mostly = 0;

static int test_aligned_array(void) {
    ASSERT(sizeof(allow_list_arr) == FAKE_PAGE_SIZE);
    ASSERT(((uintptr_t)allow_list_arr % 4096) == 0);
    allow_list_arr[0] = 1000;
    allow_list_arr[FAKE_PAGE_SIZE / sizeof(int) - 1] = 9999;
    ASSERT(allow_list_arr[0] == 1000);
    ASSERT(allow_list_arr[FAKE_PAGE_SIZE / sizeof(int) - 1] == 9999);
    (void)allow_list_pointer;
    return 0;
}

/* ================================================================
 * 5. Variable-length array from sizeof(local_array)
 * (sucompat.c: `char path[sizeof(su) + 1];`)
 * ================================================================ */
static int test_sizeof_vla(void) {
    const char su[] = "/system/bin/su";
    const char sh[] = "/system/bin/sh";
    char path[sizeof(su) + 1];

    memset(path, 0, sizeof(path));
    memcpy(path, su, sizeof(su));
    ASSERT(strcmp(path, "/system/bin/su") == 0);

    char path2[sizeof(sh)];
    memcpy(path2, sh, sizeof(sh));
    ASSERT(strcmp(path2, "/system/bin/sh") == 0);

    ASSERT(sizeof(path) == sizeof(su) + 1);
    return 0;
}

/* ================================================================
 * 6. extern declarations with empty parentheses (old-style C)
 * KernelSU uses `extern void func();` without `void` parameter.
 * ================================================================ */
extern void ksu_dummy_func();
extern int ksu_dummy_func2();

void ksu_dummy_func() { }
int ksu_dummy_func2() { return 42; }

static int test_extern_empty_parens(void) {
    ksu_dummy_func();
    ASSERT(ksu_dummy_func2() == 42);
    return 0;
}

/* ================================================================
 * 7. Complex version-gated #if chains (kernel_compat.c pattern)
 * ================================================================ */
#define FAKE_LINUX_VERSION 51000
#define FAKE_KERNEL_VERSION(a, b, c) ((a)*10000 + (b)*100 + (c))

#if FAKE_LINUX_VERSION >= FAKE_KERNEL_VERSION(5, 8, 0)
static long compat_strncpy(char *dst, const char *src, long count) {
    strncpy(dst, src, (size_t)count);
    return (long)strlen(dst);
}
#elif FAKE_LINUX_VERSION >= FAKE_KERNEL_VERSION(5, 3, 0)
static long compat_strncpy(char *dst, const char *src, long count) {
    strncpy(dst, src, (size_t)count);
    return (long)strlen(dst);
}
#else
static long compat_strncpy(char *dst, const char *src, long count) {
    if (unlikely(count <= 0)) return 0;
    strncpy(dst, src, (size_t)count);
    long ret = (long)strlen(dst);
    if (ret >= count) {
        ret = count;
        dst[ret - 1] = '\0';
    } else if (ret > 0) {
        ret++;
    }
    return ret;
}
#endif

static int test_version_gated_code(void) {
    char buf[32];
    long r = compat_strncpy(buf, "hello", 32);
    ASSERT(r == 5);
    ASSERT(strcmp(buf, "hello") == 0);
    return 0;
}

/* ================================================================
 * 8. Nested macro struct initialization (KernelSU patterns)
 * ================================================================ */
typedef struct { int counter; } ksu_atomic_t;
#define KSU_ATOMIC_INIT(i) { (i) }

typedef struct { volatile int locked; } ksu_mutex_t;
#define KSU_MUTEX_INIT { .locked = 0 }
#define DEFINE_KSU_MUTEX(name) static ksu_mutex_t name = KSU_MUTEX_INIT

struct root_profile {
    int uid;
    int gid;
    int groups_count;
    int groups[32];
    char selinux_domain[64];
};

struct group_info_sim {
    ksu_atomic_t usage;
    int ngroups;
};

static struct group_info_sim root_groups = { .usage = KSU_ATOMIC_INIT(2) };
DEFINE_KSU_MUTEX(allowlist_mutex);

static struct root_profile default_root_profile;

static int test_nested_macro_init(void) {
    ASSERT(root_groups.usage.counter == 2);
    ASSERT(allowlist_mutex.locked == 0);
    default_root_profile.uid = 0;
    default_root_profile.groups_count = 1;
    default_root_profile.groups[0] = 0;
    ASSERT(default_root_profile.uid == 0);
    return 0;
}

/* ================================================================
 * 9. __func__ in format strings (common KernelSU debug pattern)
 * ================================================================ */
#define pr_err(fmt, ...) fprintf(stderr, "ERR: " fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) fprintf(stderr, "INFO: " fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...) fprintf(stderr, "WARN: " fmt, ##__VA_ARGS__)
#define pr_alert(fmt, ...) fprintf(stderr, "ALERT: " fmt, ##__VA_ARGS__)

static int test_func_in_format(void) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: test message, uid=%d", __func__, 1000);
    ASSERT(strstr(buf, "test_func_in_format") != NULL);
    ASSERT(strstr(buf, "uid=1000") != NULL);
    return 0;
}

/* ================================================================
 * 10. Pointer cast patterns (__user simulation)
 * ================================================================ */
static void __user *userspace_stack_buffer(const void *d, size_t len) {
    static char stack_sim[256];
    if (len > sizeof(stack_sim)) return NULL;
    memcpy(stack_sim, d, len);
    return (void __user *)stack_sim;
}

static char __user *sh_user_path(void) {
    static const char sh_path[] = "/system/bin/sh";
    return (char __user *)userspace_stack_buffer(sh_path, sizeof(sh_path));
}

static int test_user_pointer_cast(void) {
    char __user *p = sh_user_path();
    ASSERT(p != NULL);
    ASSERT(strcmp((const char *)p, "/system/bin/sh") == 0);

    void __user *vp = userspace_stack_buffer("test", 5);
    ASSERT(vp != NULL);
    ASSERT(strcmp((const char *)(void __force *)vp, "test") == 0);
    return 0;
}

/* ================================================================
 * 11. module_init / module_exit macros
 * ================================================================ */
static int __init ksu_test_init(void) { return 0; }
static void __exit ksu_test_exit(void) { }

module_init(ksu_test_init);
module_exit(ksu_test_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("NeverC Test");
MODULE_DESCRIPTION("KernelSU pattern validation");

/* ================================================================
 * 12. Task-lock pattern with struct save/restore (kernel_compat.c)
 * ================================================================ */
struct task_sim {
    void *nsproxy;
    void *fs;
    int lock_count;
};

static struct task_sim current_task = { .nsproxy = NULL, .fs = NULL, .lock_count = 0 };

static void task_lock_sim(struct task_sim *t) { t->lock_count++; }
static void task_unlock_sim(struct task_sim *t) { t->lock_count--; }

struct saved_context {
    void *ns;
    void *fs;
};

static bool context_saved_checked = false;
static bool context_saved_enabled = false;
static struct saved_context android_context;

static void check_context(void) {
    if (context_saved_checked) return;
    context_saved_checked = true;
    task_lock_sim(&current_task);
    if (current_task.nsproxy != NULL) {
        context_saved_enabled = true;
        android_context.ns = current_task.nsproxy;
        android_context.fs = current_task.fs;
    }
    task_unlock_sim(&current_task);
}

static int test_task_lock_pattern(void) {
    int dummy_ns = 1, dummy_fs = 2;
    current_task.nsproxy = &dummy_ns;
    current_task.fs = &dummy_fs;

    check_context();
    ASSERT(context_saved_checked == true);
    ASSERT(context_saved_enabled == true);
    ASSERT(android_context.ns == &dummy_ns);
    ASSERT(android_context.fs == &dummy_fs);
    ASSERT(current_task.lock_count == 0);
    return 0;
}

/* ================================================================
 * 13. Complex struct with function pointer + conditional compilation
 * (KernelSU core_hook.c pattern: kprobe struct with handler)
 * ================================================================ */
struct kprobe_sim {
    const char *symbol_name;
    int (*pre_handler)(struct kprobe_sim *, void *regs);
    int (*post_handler)(struct kprobe_sim *, void *regs);
};

static int hook_execve(struct kprobe_sim *p, void *regs) {
    (void)p; (void)regs;
    return 0;
}

static struct kprobe_sim kp_execve = {
    .symbol_name = "do_execveat_common",
    .pre_handler = hook_execve,
    .post_handler = NULL,
};

#define REGISTER_KPROBE(kp) do { (void)(kp); } while (0)
#define UNREGISTER_KPROBE(kp) do { (void)(kp); } while (0)

static int test_kprobe_pattern(void) {
    REGISTER_KPROBE(&kp_execve);
    ASSERT(kp_execve.pre_handler != NULL);
    ASSERT(kp_execve.post_handler == NULL);
    ASSERT(strcmp(kp_execve.symbol_name, "do_execveat_common") == 0);

    int ret = kp_execve.pre_handler(&kp_execve, NULL);
    ASSERT(ret == 0);
    UNREGISTER_KPROBE(&kp_execve);
    return 0;
}

/* ================================================================
 * 14. FILE_MAGIC constant + bit manipulation (allowlist.c pattern)
 * ================================================================ */
#define FILE_MAGIC 0x7f4b5355
#define FILE_FORMAT_VERSION 3

struct ksu_file_header {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t reserved;
};

static int test_file_magic(void) {
    struct ksu_file_header hdr = {
        .magic = FILE_MAGIC,
        .version = FILE_FORMAT_VERSION,
        .flags = 0,
        .reserved = 0,
    };
    ASSERT(hdr.magic == 0x7f4b5355);
    ASSERT(hdr.version == 3);

    unsigned char *raw = (unsigned char *)&hdr;
    uint32_t read_magic;
    memcpy(&read_magic, raw, sizeof(uint32_t));
    ASSERT(read_magic == FILE_MAGIC);
    return 0;
}

/* ================================================================
 * 15. uid_t pattern + isolated UID range check (sucompat.c)
 * ================================================================ */
typedef uint32_t ksu_uid_t;

#define FIRST_ISOLATED_UID 99000
#define LAST_ISOLATED_UID 99999
#define FIRST_APP_ZYGOTE_ISOLATED_UID 90000
#define LAST_APP_ZYGOTE_ISOLATED_UID 98999

static inline bool is_isolated_uid(ksu_uid_t uid) {
    ksu_uid_t appid = uid % 100000;
    return (appid >= FIRST_ISOLATED_UID && appid <= LAST_ISOLATED_UID) ||
           (appid >= FIRST_APP_ZYGOTE_ISOLATED_UID &&
            appid <= LAST_APP_ZYGOTE_ISOLATED_UID);
}

static int test_uid_range(void) {
    ASSERT(is_isolated_uid(99000) == true);
    ASSERT(is_isolated_uid(99999) == true);
    ASSERT(is_isolated_uid(90000) == true);
    ASSERT(is_isolated_uid(98999) == true);
    ASSERT(is_isolated_uid(89999) == false);
    ASSERT(is_isolated_uid(0) == false);
    ASSERT(is_isolated_uid(1000) == false);
    ASSERT(is_isolated_uid(199000) == true);
    return 0;
}

/* ================================================================
 * Main
 * ================================================================ */
int main(void) {
    int failures = 0;

#define RUN(fn) do { \
    if (fn() != 0) { fprintf(stderr, "FAIL: " #fn "\n"); failures++; } \
} while (0)

    RUN(test_flex_array_nested);
    RUN(test_mixed_declarations);
    RUN(test_multi_assign_for);
    RUN(test_aligned_array);
    RUN(test_sizeof_vla);
    RUN(test_extern_empty_parens);
    RUN(test_version_gated_code);
    RUN(test_nested_macro_init);
    RUN(test_func_in_format);
    RUN(test_user_pointer_cast);
    RUN(test_task_lock_pattern);
    RUN(test_kprobe_pattern);
    RUN(test_file_magic);
    RUN(test_uid_range);

#undef RUN

    if (failures == 0) {
        printf("test_kernelsu_patterns: ALL PASSED\n");
    } else {
        printf("test_kernelsu_patterns: %d FAILED\n", failures);
    }
    return failures;
}
