// NeverC C++ ABI v1 — exception personality / throw scaffold
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

extern "C" {

struct __neverc_exception {
  void *exception_object;
  void *type_info;
  void (*destructor)(void *);
  uintptr_t handler_count;
};

static __neverc_exception *g_uncaught = nullptr;

void *__cxa_allocate_exception(size_t thrown_size) {
  size_t total = sizeof(__neverc_exception) + thrown_size;
  void *mem = malloc(total ? total : 1);
  if (!mem)
    abort();
  memset(mem, 0, total);
  return (char *)mem + sizeof(__neverc_exception);
}

void __cxa_free_exception(void *thrown_exception) {
  if (!thrown_exception)
    return;
  void *base = (char *)thrown_exception - sizeof(__neverc_exception);
  free(base);
}

void __cxa_throw(void *thrown_exception, void *tinfo, void (*dest)(void *)) {
  __neverc_exception *header =
      (__neverc_exception *)((char *)thrown_exception -
                             sizeof(__neverc_exception));
  header->exception_object = thrown_exception;
  header->type_info = tinfo;
  header->destructor = dest;
  g_uncaught = header;
  // No landing pads yet — terminate.
  abort();
}

void __cxa_rethrow(void) {
  if (!g_uncaught)
    abort();
  abort();
}

void *__cxa_begin_catch(void *exceptionObject) { return exceptionObject; }

void __cxa_end_catch(void) {}

void *__cxa_get_exception_ptr(void *exceptionObject) {
  return exceptionObject;
}

int __cxa_uncaught_exception(void) { return g_uncaught != nullptr; }

// Personality stub (platform unwind integration later).
int __neverc_personality_v0(int version, unsigned long actions, uint64_t ex_class,
                            void *exception_object, void *context) {
  (void)version;
  (void)actions;
  (void)ex_class;
  (void)exception_object;
  (void)context;
  return 0;
}

} // extern "C"
