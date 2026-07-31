#include "neverc/std/crypto/x509.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#elif defined(__APPLE__)
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define X509_SYSTEM_MAX_BUNDLE_SIZE (64U * 1024U * 1024U)
#define X509_SYSTEM_MAX_DIRECTORY_ENTRIES 16384U
#define X509_SYSTEM_MAX_ENV_PATHS_SIZE (1024U * 1024U)

static FILE *x509_system_open_file(const char *path) {
#if defined(_WIN32)
    FILE *file = NULL;
    return fopen_s(&file, path, "rb") == 0 ? file : NULL;
#else
    return fopen(path, "rb");
#endif
}

static int x509_system_copy_environment_value(
    const char *name, char **out) {
    if (!name || !out)
        return -1;
    *out = NULL;
#if defined(_WIN32)
    size_t value_len = 0;
    return _dupenv_s(out, &value_len, name) == 0 ? 0 : -1;
#else
    const char *value = getenv(name);
    if (!value)
        return 0;
    size_t value_len = strlen(value);
    if (value_len == SIZE_MAX)
        return -1;
    *out = (char *)malloc(value_len + 1);
    if (!*out)
        return -1;
    memcpy(*out, value, value_len + 1);
    return 0;
#endif
}

static int x509_system_load_pem_file(
    neverc_x509_cert_pool_t *pool, const char *path) {
    if (!pool || !path || path[0] == '\0')
        return -1;

    FILE *file = x509_system_open_file(path);
    if (!file)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long file_size = ftell(file);
    if (file_size <= 0 ||
        (unsigned long)file_size > X509_SYSTEM_MAX_BUNDLE_SIZE ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    size_t size = (size_t)file_size;
    char *contents = (char *)malloc(size);
    if (!contents) {
        fclose(file);
        return -1;
    }
    size_t bytes_read = fread(contents, 1, size, file);
    int close_result = fclose(file);
    if (bytes_read != size || close_result != 0) {
        free(contents);
        return -1;
    }

    int added = neverc_x509_cert_pool_add_pem(
        pool, contents, size);
    free(contents);
    return added;
}

static char *x509_system_join_path(
    const char *directory, const char *name, char separator) {
    if (!directory || !name)
        return NULL;
    size_t directory_len = strlen(directory);
    size_t name_len = strlen(name);
    int needs_separator =
        directory_len != 0 &&
        directory[directory_len - 1] != '/' &&
        directory[directory_len - 1] != '\\';
    if (directory_len > SIZE_MAX - name_len - 2)
        return NULL;

    size_t path_len = directory_len + (size_t)needs_separator + name_len;
    char *path = (char *)malloc(path_len + 1);
    if (!path)
        return NULL;
    memcpy(path, directory, directory_len);
    size_t offset = directory_len;
    if (needs_separator)
        path[offset++] = separator;
    memcpy(path + offset, name, name_len);
    path[path_len] = '\0';
    return path;
}

#if defined(_WIN32)
static int x509_system_load_directory(
    neverc_x509_cert_pool_t *pool, const char *directory) {
    char *pattern = x509_system_join_path(directory, "*", '\\');
    if (!pattern)
        return -1;

    WIN32_FIND_DATAA entry;
    HANDLE search = FindFirstFileA(pattern, &entry);
    free(pattern);
    if (search == INVALID_HANDLE_VALUE)
        return 0;

    size_t entries_seen = 0;
    do {
        if (++entries_seen > X509_SYSTEM_MAX_DIRECTORY_ENTRIES)
            break;
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            continue;
        char *path = x509_system_join_path(
            directory, entry.cFileName, '\\');
        if (!path) {
            FindClose(search);
            return -1;
        }
        (void)x509_system_load_pem_file(pool, path);
        free(path);
    } while (FindNextFileA(search, &entry));

    FindClose(search);
    return 0;
}
#else
static int x509_system_load_directory(
    neverc_x509_cert_pool_t *pool, const char *directory) {
    DIR *stream = opendir(directory);
    if (!stream)
        return 0;

    size_t entries_seen = 0;
    struct dirent *entry;
    while ((entry = readdir(stream)) != NULL) {
        if (++entries_seen > X509_SYSTEM_MAX_DIRECTORY_ENTRIES)
            break;
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        char *path = x509_system_join_path(
            directory, entry->d_name, '/');
        if (!path) {
            closedir(stream);
            return -1;
        }
        struct stat status;
        if (stat(path, &status) == 0 && S_ISREG(status.st_mode))
            (void)x509_system_load_pem_file(pool, path);
        free(path);
    }

    closedir(stream);
    return 0;
}
#endif

static int x509_system_load_directory_list(
    neverc_x509_cert_pool_t *pool, const char *directories,
    char list_separator) {
    if (!directories || directories[0] == '\0')
        return 0;
    size_t directories_len = strlen(directories);
    if (directories_len > X509_SYSTEM_MAX_ENV_PATHS_SIZE)
        return -1;

    char *copy = (char *)malloc(directories_len + 1);
    if (!copy)
        return -1;
    memcpy(copy, directories, directories_len + 1);

    char *component = copy;
    for (char *cursor = copy;; ++cursor) {
        if (*cursor != list_separator && *cursor != '\0')
            continue;
        char saved = *cursor;
        *cursor = '\0';
        if (component[0] != '\0' &&
            x509_system_load_directory(pool, component) != 0) {
            free(copy);
            return -1;
        }
        if (saved == '\0')
            break;
        component = cursor + 1;
    }
    free(copy);
    return 0;
}

#if !defined(_WIN32) && !defined(__APPLE__)
static const char *const x509_system_cert_files[] = {
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/ca-bundle.pem",
    "/etc/pki/tls/cacert.pem",
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
    "/etc/ssl/cert.pem",
    "/usr/local/etc/ssl/cert.pem",
    "/usr/local/share/certs/ca-root-nss.crt",
    "/etc/openssl/certs/ca-certificates.crt",
    "/etc/certs/ca-certificates.crt",
    "/etc/ssl/cacert.pem",
    "/var/ssl/certs/ca-bundle.crt"
};

static const char *const x509_system_cert_directories[] = {
    "/etc/ssl/certs",
    "/etc/pki/tls/certs",
    "/usr/local/share/certs",
    "/etc/openssl/certs",
    "/etc/certs/CA",
    "/var/ssl/certs"
};

static void x509_system_load_unix_default_file(
    neverc_x509_cert_pool_t *pool) {
    for (size_t i = 0;
         i < sizeof(x509_system_cert_files) /
                 sizeof(x509_system_cert_files[0]);
         ++i) {
        if (x509_system_load_pem_file(
                pool, x509_system_cert_files[i]) >= 0)
            break;
    }
}

static void x509_system_load_unix_default_directories(
    neverc_x509_cert_pool_t *pool) {
    for (size_t i = 0;
         i < sizeof(x509_system_cert_directories) /
                 sizeof(x509_system_cert_directories[0]);
         ++i) {
        (void)x509_system_load_directory(
            pool, x509_system_cert_directories[i]);
    }
}
#endif

#if defined(_WIN32)
static void x509_system_load_windows_store(
    neverc_x509_cert_pool_t *pool, DWORD location) {
    HCERTSTORE store = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_A,
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        0,
        location | CERT_STORE_OPEN_EXISTING_FLAG |
            CERT_STORE_READONLY_FLAG,
        "ROOT");
    if (!store)
        return;

    PCCERT_CONTEXT certificate = NULL;
    while ((certificate = CertEnumCertificatesInStore(
                store, certificate)) != NULL) {
        if (certificate->pbCertEncoded &&
            certificate->cbCertEncoded != 0) {
            (void)neverc_x509_cert_pool_add_der(
                pool, certificate->pbCertEncoded,
                (size_t)certificate->cbCertEncoded);
        }
    }
    CertCloseStore(store, 0);
}
#endif

#if defined(__APPLE__)
typedef const void *x509_cf_ref_t;
typedef long x509_cf_index_t;
typedef int32_t x509_os_status_t;

typedef x509_os_status_t (*x509_copy_anchors_fn)(
    x509_cf_ref_t *anchors);
typedef x509_cf_ref_t (*x509_certificate_copy_data_fn)(
    x509_cf_ref_t certificate);
typedef x509_cf_index_t (*x509_array_get_count_fn)(
    x509_cf_ref_t array);
typedef x509_cf_ref_t (*x509_array_get_value_fn)(
    x509_cf_ref_t array, x509_cf_index_t index);
typedef const uint8_t *(*x509_data_get_bytes_fn)(
    x509_cf_ref_t data);
typedef x509_cf_index_t (*x509_data_get_length_fn)(
    x509_cf_ref_t data);
typedef void (*x509_cf_release_fn)(x509_cf_ref_t object);

static int x509_system_load_symbol(
    void *library, const char *name,
    void *function, size_t function_size) {
    void *symbol = dlsym(library, name);
    if (!symbol || function_size != sizeof(symbol))
        return -1;
    memcpy(function, &symbol, sizeof(symbol));
    return 0;
}

static int x509_system_load_apple_anchors(
    neverc_x509_cert_pool_t *pool) {
    void *security = dlopen(
        "/System/Library/Frameworks/Security.framework/Security",
        RTLD_LAZY | RTLD_LOCAL);
    void *core_foundation = dlopen(
        "/System/Library/Frameworks/CoreFoundation.framework/"
        "CoreFoundation",
        RTLD_LAZY | RTLD_LOCAL);
    if (!security || !core_foundation) {
        if (core_foundation)
            dlclose(core_foundation);
        if (security)
            dlclose(security);
        return -1;
    }

    x509_copy_anchors_fn copy_anchors = NULL;
    x509_certificate_copy_data_fn copy_data = NULL;
    x509_array_get_count_fn array_get_count = NULL;
    x509_array_get_value_fn array_get_value = NULL;
    x509_data_get_bytes_fn data_get_bytes = NULL;
    x509_data_get_length_fn data_get_length = NULL;
    x509_cf_release_fn release = NULL;
    if (x509_system_load_symbol(
            security, "SecTrustCopyAnchorCertificates",
            &copy_anchors, sizeof(copy_anchors)) != 0 ||
        x509_system_load_symbol(
            security, "SecCertificateCopyData",
            &copy_data, sizeof(copy_data)) != 0 ||
        x509_system_load_symbol(
            core_foundation, "CFArrayGetCount",
            &array_get_count, sizeof(array_get_count)) != 0 ||
        x509_system_load_symbol(
            core_foundation, "CFArrayGetValueAtIndex",
            &array_get_value, sizeof(array_get_value)) != 0 ||
        x509_system_load_symbol(
            core_foundation, "CFDataGetBytePtr",
            &data_get_bytes, sizeof(data_get_bytes)) != 0 ||
        x509_system_load_symbol(
            core_foundation, "CFDataGetLength",
            &data_get_length, sizeof(data_get_length)) != 0 ||
        x509_system_load_symbol(
            core_foundation, "CFRelease",
            &release, sizeof(release)) != 0) {
        dlclose(core_foundation);
        dlclose(security);
        return -1;
    }

    size_t old_count = neverc_x509_cert_pool_count(pool);
    x509_cf_ref_t anchors = NULL;
    if (copy_anchors(&anchors) == 0 && anchors) {
        x509_cf_index_t count = array_get_count(anchors);
        for (x509_cf_index_t i = 0; i < count; ++i) {
            x509_cf_ref_t certificate =
                array_get_value(anchors, i);
            x509_cf_ref_t data = copy_data(certificate);
            if (!data)
                continue;
            const uint8_t *bytes = data_get_bytes(data);
            x509_cf_index_t length = data_get_length(data);
            if (bytes && length > 0)
                (void)neverc_x509_cert_pool_add_der(
                    pool, bytes, (size_t)length);
            release(data);
        }
        release(anchors);
    }

    dlclose(core_foundation);
    dlclose(security);
    size_t new_count = neverc_x509_cert_pool_count(pool);
    if (new_count <= old_count)
        return -1;
    size_t added = new_count - old_count;
    return added > INT_MAX ? INT_MAX : (int)added;
}
#endif

neverc_x509_cert_pool_t *neverc_x509_system_cert_pool(void) {
    neverc_x509_cert_pool_t *pool =
        neverc_x509_cert_pool_new();
    if (!pool)
        return NULL;

    char *cert_file = NULL;
    char *cert_directories = NULL;
    if (x509_system_copy_environment_value(
            "SSL_CERT_FILE", &cert_file) != 0 ||
        x509_system_copy_environment_value(
            "SSL_CERT_DIR", &cert_directories) != 0) {
        free(cert_file);
        free(cert_directories);
        neverc_x509_cert_pool_free(pool);
        return NULL;
    }
    int has_file_override = cert_file && cert_file[0] != '\0';
    int has_directory_override =
        cert_directories && cert_directories[0] != '\0';

    if (has_file_override || has_directory_override) {
#if !defined(_WIN32) && !defined(__APPLE__)
        if (has_file_override)
            (void)x509_system_load_pem_file(pool, cert_file);
        else
            x509_system_load_unix_default_file(pool);
        if (has_directory_override) {
            (void)x509_system_load_directory_list(
                pool, cert_directories, ':');
        } else {
            x509_system_load_unix_default_directories(pool);
        }
#else
        if (has_file_override)
            (void)x509_system_load_pem_file(pool, cert_file);
        if (has_directory_override) {
#if defined(_WIN32)
            (void)x509_system_load_directory_list(
                pool, cert_directories, ';');
#else
            (void)x509_system_load_directory_list(
                pool, cert_directories, ':');
#endif
        }
#endif
    } else {
#if defined(_WIN32)
        x509_system_load_windows_store(
            pool, CERT_SYSTEM_STORE_CURRENT_USER);
        x509_system_load_windows_store(
            pool, CERT_SYSTEM_STORE_LOCAL_MACHINE);
#elif defined(__APPLE__)
        if (x509_system_load_apple_anchors(pool) < 0)
            (void)x509_system_load_pem_file(
                pool, "/etc/ssl/cert.pem");
#else
        x509_system_load_unix_default_file(pool);
        x509_system_load_unix_default_directories(pool);
#endif
    }

    free(cert_file);
    free(cert_directories);
    if (neverc_x509_cert_pool_count(pool) == 0) {
        neverc_x509_cert_pool_free(pool);
        return NULL;
    }
    return pool;
}
