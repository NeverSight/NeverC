typedef int(__cdecl *UnaryFunction)(int);

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllexport) int __cdecl GuardedTarget(int value) { return value + 7; }

__declspec(dllexport) int __cdecl GuardedIndirectCall(UnaryFunction target,
                                                      int value) {
  return target(value);
}

__declspec(dllexport) int __cdecl GuardedExercise(void) {
  UnaryFunction target = &GuardedTarget;
  return GuardedIndirectCall(target, 35);
}

#ifdef __cplusplus
}
#endif
