#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
static int events[32], count;
static void observe(int n) { if (count < 32) events[count++] = n; }
int main(int argc, char **argv) {
 if (argc != 2) return 1;
 for (int pass = 0; pass < 2; ++pass) {
#ifdef _WIN32
  HMODULE module = LoadLibraryA(argv[1]);
  if (!module) return 2;
  int (*start)(void (*)(int)) = (int (*)(void (*)(int)))GetProcAddress(module, "lifetime_start");
#else
  void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!module) return 2;
  int (*start)(void (*)(int)) = (int (*)(void (*)(int)))dlsym(module, "lifetime_start");
#endif
  if (!start || !start(observe) || count != 9 * pass) return 3;
#ifdef _WIN32
  if (!FreeLibrary(module)) return 4;
#else
  if (dlclose(module)) return 4;
#endif
  if (count != 9 * (pass + 1)) return 5;
  for (int i = 0; i < 9; ++i)
   if (events[9 * pass + i] != 29 - i) return 10 + i;
 }
 return 0;
}
