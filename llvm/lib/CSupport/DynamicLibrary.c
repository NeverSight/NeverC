/*===- DynamicLibrary.c - Dynamic loading (pure C) --------------*- C -*-===*/
#include "include/csupport/ldynamic_llibrary.h"
#include "llvm/Config/llvm-config.h"
#include <stdio.h>
#include <string.h>

#ifdef LLVM_ON_UNIX
#include <dlfcn.h>
#include <string.h>

void *csupport_dlopen(const char *filename, char *errbuf, size_t errlen) {
  void *h = dlopen(filename, RTLD_LAZY | RTLD_GLOBAL);
  if (!h && errbuf && errlen > 0) {
    const char *e = dlerror();
    if (e) {
      size_t len = strlen(e);
      if (len >= errlen) len = errlen - 1;
      memcpy(errbuf, e, len);
      errbuf[len] = '\0';
    }
  }
  return h;
}

void *csupport_dlsym(void *handle, const char *symbol) {
  return dlsym(handle, symbol);
}

int csupport_dlclose(void *handle) {
  if (!handle) return 0;
  return dlclose(handle);
}

void *csupport_dlsym_default(const char *symbol) {
  return dlsym(RTLD_DEFAULT, symbol);
}

int csupport_dladdr(const void *addr, char *fname, size_t fname_len,
                    char *sname, size_t sname_len) {
  Dl_info info;
  if (dladdr(addr, &info) == 0) return 0;
  if (fname && fname_len > 0 && info.dli_fname) {
    size_t len = strlen(info.dli_fname);
    if (len >= fname_len) len = fname_len - 1;
    memcpy(fname, info.dli_fname, len);
    fname[len] = '\0';
  }
  if (sname && sname_len > 0) {
    if (info.dli_sname) {
      size_t len = strlen(info.dli_sname);
      if (len >= sname_len) len = sname_len - 1;
      memcpy(sname, info.dli_sname, len);
      sname[len] = '\0';
    } else {
      sname[0] = '\0';
    }
  }
  return 1;
}

void *csupport_dlopen_local(const char *filename, char *errbuf, size_t errlen) {
  void *h = dlopen(filename, RTLD_LAZY | RTLD_LOCAL);
  if (!h && errbuf && errlen > 0) {
    const char *e = dlerror();
    if (e) {
      size_t len = strlen(e);
      if (len >= errlen) len = errlen - 1;
      memcpy(errbuf, e, len);
      errbuf[len] = '\0';
    }
  }
  return h;
}

#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void win32_format_error(char *errbuf, size_t errlen) {
  if (!errbuf || errlen == 0)
    return;
  DWORD err = GetLastError();
  DWORD n = FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errbuf, (DWORD)errlen, NULL);
  if (n == 0) {
    errbuf[0] = '\0';
    return;
  }
  /* FormatMessageA appends trailing "\r\n" — strip it for cleaner errors. */
  while (n > 0 && (errbuf[n - 1] == '\r' || errbuf[n - 1] == '\n' ||
                   errbuf[n - 1] == ' ' || errbuf[n - 1] == '.'))
    errbuf[--n] = '\0';
}

void *csupport_dlopen(const char *filename, char *errbuf, size_t errlen) {
  HMODULE h = LoadLibraryA(filename);
  if (!h)
    win32_format_error(errbuf, errlen);
  return (void *)h;
}

void *csupport_dlsym(void *handle, const char *symbol) {
  if (!handle)
    return NULL;
  return (void *)(uintptr_t)GetProcAddress((HMODULE)handle, symbol);
}

int csupport_dlclose(void *handle) {
  if (!handle)
    return 0;
  return FreeLibrary((HMODULE)handle) ? 0 : -1;
}

void *csupport_dlsym_default(const char *symbol) {
  HMODULE exe = GetModuleHandleA(NULL);
  if (!exe)
    return NULL;
  return (void *)(uintptr_t)GetProcAddress(exe, symbol);
}

int csupport_dladdr(const void *addr, char *fname, size_t fname_len,
                    char *sname, size_t sname_len) {
  HMODULE mod = NULL;
  if (!GetModuleHandleExA(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          (LPCSTR)addr, &mod))
    return 0;
  if (fname && fname_len > 0) {
    DWORD n = GetModuleFileNameA(mod, fname, (DWORD)fname_len);
    if (n == 0)
      fname[0] = '\0';
  }
  if (sname && sname_len > 0)
    sname[0] = '\0';
  return 1;
}

void *csupport_dlopen_local(const char *filename, char *errbuf, size_t errlen) {
  HMODULE h = LoadLibraryExA(filename, NULL,
                             LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!h)
    win32_format_error(errbuf, errlen);
  return (void *)h;
}

#else

void *csupport_dlopen(const char *f, char *e, size_t l) {
  (void)f; (void)e; (void)l;
  return 0;
}
void *csupport_dlsym(void *h, const char *s) {
  (void)h; (void)s;
  return 0;
}
int csupport_dlclose(void *h) {
  (void)h;
  return 0;
}
void *csupport_dlsym_default(const char *s) {
  (void)s;
  return 0;
}
int csupport_dladdr(const void *a, char *f, size_t fl, char *s, size_t sl) {
  (void)a; (void)f; (void)fl; (void)s; (void)sl;
  return 0;
}
void *csupport_dlopen_local(const char *f, char *e, size_t l) {
  (void)f; (void)e; (void)l;
  return 0;
}

#endif

int csupport_dl_is_valid(void *handle) {
  return handle != 0;
}

const char *csupport_dl_error_string(void) {
#ifdef LLVM_ON_UNIX
  const char *err = dlerror();
  return err ? err : "";
#else
  return "";
#endif
}

void *csupport_dl_search_special(const char *sym) {
#define EXPLICIT_SYMBOL(SYM) \
  extern void *SYM; \
  if (!strcmp(sym, #SYM)) return (void *)&SYM

#ifdef __CYGWIN__
  EXPLICIT_SYMBOL(_alloca);
  EXPLICIT_SYMBOL(__main);
#endif

#undef EXPLICIT_SYMBOL

#define EXPLICIT_SYMBOL(SYM) \
  if (!strcmp(sym, #SYM)) return &SYM

#if defined(__GLIBC__)
  EXPLICIT_SYMBOL(stderr);
  EXPLICIT_SYMBOL(stdout);
  EXPLICIT_SYMBOL(stdin);
#else
#ifndef stdin
  EXPLICIT_SYMBOL(stdin);
#endif
#ifndef stdout
  EXPLICIT_SYMBOL(stdout);
#endif
#ifndef stderr
  EXPLICIT_SYMBOL(stderr);
#endif
#endif
#undef EXPLICIT_SYMBOL

  return 0;
}
