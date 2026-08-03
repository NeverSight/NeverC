// NeverC C++ ABI v1 — RTTI
#include <string.h>
#include <stdint.h>

namespace std {

class type_info {
public:
  virtual ~type_info();
  const char *name() const noexcept { return __name; }
  bool before(const type_info &rhs) const noexcept {
    return strcmp(__name, rhs.__name) < 0;
  }
  bool operator==(const type_info &rhs) const noexcept {
    return strcmp(__name, rhs.__name) == 0;
  }
  bool operator!=(const type_info &rhs) const noexcept {
    return !(*this == rhs);
  }
protected:
  const char *__name;
  explicit type_info(const char *Name) : __name(Name) {}
};

type_info::~type_info() {}

// NeverC fundamental type_info nodes (names match simplified _ZTI encodings).
class __neverc_fundamental_type_info : public type_info {
public:
  explicit __neverc_fundamental_type_info(const char *N) : type_info(N) {}
};

} // namespace std

// Emitted type_info-like globals use a two-pointer record: {vtable, name}.
// dynamic_cast scaffold — returns null (no cross-cast) until full hierarchy
// walk is wired to NeverCCXXABI vtables.
extern "C" void *__neverc_dynamic_cast(const void *src_ptr,
                                       const void *src_type,
                                       const void *dst_type,
                                       ptrdiff_t src2dst) {
  (void)src_type;
  (void)dst_type;
  (void)src2dst;
  return const_cast<void *>(src_ptr); // same-pointer optimistic path
}

// Itanium-compat symbol some runtimes expect.
extern "C" void *__dynamic_cast(const void *src_ptr, const void *src_type,
                                const void *dst_type, long src2dst) {
  return __neverc_dynamic_cast(src_ptr, src_type, dst_type,
                               (ptrdiff_t)src2dst);
}
