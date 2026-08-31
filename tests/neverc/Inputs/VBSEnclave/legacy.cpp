typedef int(__cdecl *UnaryFunction)(int);

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllexport) int __cdecl LegacyTarget(int value) {
  return value ^ 0x5a;
}

__declspec(dllexport) UnaryFunction LegacyAddressTaken = &LegacyTarget;

__declspec(dllexport) int __cdecl LegacyExercise(int value) {
  return LegacyAddressTaken(value);
}

#ifdef __cplusplus
}
#endif
