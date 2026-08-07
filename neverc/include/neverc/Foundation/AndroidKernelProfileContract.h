#ifndef NEVERC_FOUNDATION_ANDROIDKERNELPROFILECONTRACT_H
#define NEVERC_FOUNDATION_ANDROIDKERNELPROFILECONTRACT_H

#include <cstdint>

namespace neverc::AndroidKernelProfileContract {

// Native Android-kernel objects carry the same opaque profile/KCFI pair as
// their LLVM IR.  The section intentionally does not use a conventional
// `.rodata.*` prefix: relocatable Android links fold those implementation
// sections, while this link contract must survive unchanged for later links.
inline constexpr char NativeSection[] = ".neverc.android.kernel.profile";
inline constexpr char NativeSymbol[] =
    "__neverc_android_kernel_profile_contract";

inline constexpr uint32_t MaxKCFIMode = 2;

constexpr uint64_t encode(uint32_t Profile, uint32_t KCFIMode) {
  return (static_cast<uint64_t>(Profile) << 32) | KCFIMode;
}

constexpr uint32_t profile(uint64_t Contract) {
  return static_cast<uint32_t>(Contract >> 32);
}

constexpr uint32_t kcfiMode(uint64_t Contract) {
  return static_cast<uint32_t>(Contract);
}

constexpr bool isValid(uint64_t Contract) {
  return profile(Contract) != 0 && kcfiMode(Contract) <= MaxKCFIMode;
}

} // namespace neverc::AndroidKernelProfileContract

#endif // NEVERC_FOUNDATION_ANDROIDKERNELPROFILECONTRACT_H
