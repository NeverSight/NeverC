#include <windows.h>

typedef int(__cdecl *UnaryFunction)(int);

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllexport) int __cdecl GuardedTarget(int value) { return value + 7; }

__declspec(dllexport) __declspec(noinline) int __cdecl
GuardedIndirectCall(UnaryFunction target, int value) {
  return target(value);
}

__declspec(dllexport) int __cdecl GuardedExercise(void) {
  UnaryFunction target = &GuardedTarget;
  return GuardedIndirectCall(target, 35);
}

int __cdecl LegacyTarget(int value);
int __cdecl LegacyExercise(int value);

// Keep context scalar-only: the probe never accesses untrusted host memory.
__declspec(dllexport) void *CALLBACK VbsEnclaveExercise(void *context) {
  const int value = (int)((ULONG_PTR)context & 0xffff);
  const int result = GuardedIndirectCall(&LegacyTarget, value) +
                     LegacyExercise(value) + GuardedExercise() +
                     GuardedIndirectCall(&GuardedTarget, value);
  return (void *)(ULONG_PTR)result;
}

#ifdef __cplusplus
}
#endif
