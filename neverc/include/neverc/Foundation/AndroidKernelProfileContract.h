#ifndef NEVERC_FOUNDATION_ANDROIDKERNELPROFILECONTRACT_H
#define NEVERC_FOUNDATION_ANDROIDKERNELPROFILECONTRACT_H

#include <cstdint>

namespace neverc::AndroidKernelProfileContract {

// Compiler-produced Android-kernel objects carry the same profile and
// control-flow ABI contract as their LLVM IR.  The section intentionally does
// not use a conventional `.rodata.*` prefix: relocatable Android links fold
// those implementation sections, while this intermediate-object contract must
// survive until the final `.ko` merge.  That final Android-module merge verifies
// the inputs and then drops the section so delivered modules do not retain a
// NeverC fingerprint.
inline constexpr char NativeSection[] = ".neverc.android.kernel.profile";
inline constexpr char NativeSymbol[] =
    "__neverc_android_kernel_profile_contract";

inline constexpr uint32_t MaxKCFIMode = 2;
inline constexpr uint32_t MinShadowCallStackMode = 1;
inline constexpr uint32_t MaxShadowCallStackMode = 2;
inline constexpr uint32_t FormatVersion = 1;

constexpr uint64_t encode(uint32_t Profile, uint32_t KCFIMode,
                          uint32_t ShadowCallStackMode) {
  return (static_cast<uint64_t>(Profile) << 32) |
         (static_cast<uint64_t>(FormatVersion) << 16) |
         (static_cast<uint64_t>(ShadowCallStackMode) << 8) | KCFIMode;
}

constexpr uint32_t profile(uint64_t Contract) {
  return static_cast<uint32_t>(Contract >> 32);
}

constexpr uint32_t kcfiMode(uint64_t Contract) {
  return static_cast<uint32_t>(Contract & 0xffU);
}

constexpr uint32_t shadowCallStackMode(uint64_t Contract) {
  return static_cast<uint32_t>((Contract >> 8) & 0xffU);
}

constexpr uint32_t formatVersion(uint64_t Contract) {
  return static_cast<uint32_t>((Contract >> 16) & 0xffffU);
}

constexpr bool isValid(uint64_t Contract) {
  return profile(Contract) != 0 && formatVersion(Contract) == FormatVersion &&
         kcfiMode(Contract) <= MaxKCFIMode &&
         shadowCallStackMode(Contract) >= MinShadowCallStackMode &&
         shadowCallStackMode(Contract) <= MaxShadowCallStackMode;
}

} // namespace neverc::AndroidKernelProfileContract

#endif // NEVERC_FOUNDATION_ANDROIDKERNELPROFILECONTRACT_H
