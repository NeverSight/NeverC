#ifndef NEVERC_FOUNDATION_ANDROIDKERNELRUNTIMECONTRACT_H
#define NEVERC_FOUNDATION_ANDROIDKERNELRUNTIMECONTRACT_H

#include "llvm/ADT/StringRef.h"

namespace neverc::AndroidKernelRuntimeContract {

// File-local definitions from the embedded runtime are renamed into this
// reserved namespace before every consumer translation unit links the runtime.
// The LTO and native relocatable-link paths use the same predicate to coalesce
// those copies into one module-wide instance.
inline constexpr llvm::StringLiteral LocalSymbolPrefix =
    "__neverc_nvk_local.";

// Only 618 needs runtime selection between SP_EL0 (KMI5) and TPIDRRO_EL0
// (KMI6). Other families keep a non-sleeping SP_EL0 read without requiring
// kprobe exports. Keep frontend entry instrumentation and runtime retention
// in agreement, including when entry and current live in different TUs.
inline bool needsCurrentInitialization(unsigned Profile) {
  return Profile == 618;
}

// The frontend guards module entry with an optional weak hook. Loader-only
// modules do not pull in the runtime or acquire any kernel API dependencies.
inline constexpr llvm::StringLiteral PrepareCurrentSymbol =
    "neverc_krt_prepare_current";
inline constexpr llvm::StringLiteral CurrentTaskSymbol =
    "neverc_krt_current_task";

inline bool isLocalSymbol(llvm::StringRef Name) {
  return Name.starts_with(LocalSymbolPrefix);
}

// Ordinary Android kernel C code cannot use FP/SIMD/SVE/SME state and must
// reserve x18.  Keep this target contract in one place: the driver, linked
// runtime fixups, and final LTO backend all have to materialize the same set.
inline constexpr llvm::StringLiteral ReservedX18Feature = "+reserve-x18";
inline constexpr llvm::StringLiteral BaseArchitectureFeature = "+v8a";
inline constexpr llvm::StringLiteral GeneralRegisterOnlyFeatures[] = {
    "-fp-armv8", "-crypto", "-neon", "-sve",
    "-sve2",     "-sme",    "-sme2",
};

template <typename Consumer>
inline void forEachGeneralRegisterOnlyAArch64Feature(Consumer Consume) {
  for (llvm::StringRef Feature : GeneralRegisterOnlyFeatures)
    Consume(Feature);
}

template <typename Consumer>
inline void forEachRequiredAArch64Feature(Consumer Consume) {
  Consume(ReservedX18Feature);
  Consume(BaseArchitectureFeature);
  forEachGeneralRegisterOnlyAArch64Feature(Consume);
}

inline bool isRequiredAArch64FeatureName(llvm::StringRef Feature) {
  if (Feature.size() < 2 || (Feature.front() != '+' && Feature.front() != '-'))
    return false;
  const llvm::StringRef Name = Feature.drop_front();
  bool Matches = false;
  forEachRequiredAArch64Feature([&](llvm::StringRef Required) {
    Matches |= Required.drop_front() == Name;
  });
  return Matches;
}

} // namespace neverc::AndroidKernelRuntimeContract

#endif // NEVERC_FOUNDATION_ANDROIDKERNELRUNTIMECONTRACT_H
