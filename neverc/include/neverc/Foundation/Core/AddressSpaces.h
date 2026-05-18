#ifndef NEVERC_BASIC_ADDRESSSPACES_H
#define NEVERC_BASIC_ADDRESSSPACES_H

#include <cassert>

namespace neverc {

enum class LangAS : unsigned {
  Default = 0,

  // Pointer size and extension address spaces (Windows MSVC).
  ptr32_sptr,
  ptr32_uptr,
  ptr64,

  // This denotes the count of language-specific address spaces and also
  // the offset added to the target-specific address spaces, which are usually
  // specified by address space attributes __attribute__(address_space(n))).
  FirstTargetAddressSpace
};

using LangASMap = unsigned[(unsigned)LangAS::FirstTargetAddressSpace];

inline bool isTargetAddressSpace(LangAS AS) {
  return (unsigned)AS >= (unsigned)LangAS::FirstTargetAddressSpace;
}

inline unsigned toTargetAddressSpace(LangAS AS) {
  assert(isTargetAddressSpace(AS));
  return (unsigned)AS - (unsigned)LangAS::FirstTargetAddressSpace;
}

inline LangAS getLangASFromTargetAS(unsigned TargetAS) {
  return static_cast<LangAS>((TargetAS) +
                             (unsigned)LangAS::FirstTargetAddressSpace);
}

inline bool isPtrSizeAddressSpace(LangAS AS) {
  return (AS == LangAS::ptr32_sptr || AS == LangAS::ptr32_uptr ||
          AS == LangAS::ptr64);
}

} // namespace neverc

#endif // NEVERC_BASIC_ADDRESSSPACES_H
