#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

/* Deliberately models a pre-release binary without depending on removed types. */
EXPORT int nevercGetPluginInfo(void) { return 0; }
