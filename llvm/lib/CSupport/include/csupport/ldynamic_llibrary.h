#ifndef CSUPPORT_LDYNAMIC_LLIBRARY_H
#define CSUPPORT_LDYNAMIC_LLIBRARY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void *csupport_dlopen(const char *filename, char *errbuf, size_t errlen);
void *csupport_dlsym(void *handle, const char *symbol);
int csupport_dlclose(void *handle);
void *csupport_dlsym_default(const char *symbol);

/* Look up address info: returns 1 if found, 0 otherwise.
   Writes file name and symbol name to fname/sname buffers. */
int csupport_dladdr(const void *addr, char *fname, size_t fname_len,
                    char *sname, size_t sname_len);

/* Open library with RTLD_LOCAL instead of RTLD_GLOBAL */
void *csupport_dlopen_local(const char *filename, char *errbuf, size_t errlen);

int csupport_dl_is_valid(void *handle);
const char *csupport_dl_error_string(void);
void *csupport_dl_search_special(const char *symbol_name);

#ifdef __cplusplus
}
#endif
#endif
