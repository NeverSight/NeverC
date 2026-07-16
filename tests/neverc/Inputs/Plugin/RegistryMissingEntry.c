#include <stdint.h>

#if defined(_WIN32)
#define NEVERC_TEST_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define NEVERC_TEST_EXPORT __attribute__((visibility("default")))
#else
#define NEVERC_TEST_EXPORT
#endif

NEVERC_TEST_EXPORT uint32_t neverc_registry_fixture_without_entry(void) {
  return 1;
}
