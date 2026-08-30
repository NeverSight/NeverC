//===- ELFInputWorkload.h - adaptive input accounting -----------*- C++ -*-===//

#ifndef NEVERC_LINKER_ELF_DRIVER_ELFINPUTWORKLOAD_H
#define NEVERC_LINKER_ELF_DRIVER_ELFINPUTWORKLOAD_H

#include "llvm/Support/MathExtras.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace linker::elf::detail {

inline std::optional<unsigned> selectProvisionalLinkPoolThreads(
    unsigned RequestedThreads, uint64_t BitcodeFiles,
    unsigned NativeCandidateThreads, unsigned MaximumAutoThreads) {
  if (RequestedThreads != 0)
    return RequestedThreads;
  if (BitcodeFiles == 0 || NativeCandidateThreads >= MaximumAutoThreads)
    return NativeCandidateThreads;
  return std::nullopt;
}

inline bool shouldConfigureProvisionalLinkPool(unsigned RequestedThreads,
                                               uint64_t BitcodeFiles,
                                               unsigned NativeCandidateThreads,
                                               unsigned MaximumAutoThreads) {
  // An explicit budget is authoritative. Automatic selection must wait for
  // native LTO outputs: configuring from compact source bitcode can create a
  // permanently undersized pool before that representation expands. The one
  // safe early automatic choice is the complete available budget, which
  // cannot require a later upgrade.
  return selectProvisionalLinkPoolThreads(
             RequestedThreads, BitcodeFiles, NativeCandidateThreads,
             MaximumAutoThreads)
      .has_value();
}

inline std::pair<uint64_t, uint64_t>
mergeMaterializedInputWorkload(uint64_t NativeBytes, uint64_t NativeFiles,
                               uint64_t BitcodeBytes, uint64_t BitcodeFiles,
                               uint64_t BinaryBytes, uint64_t BinaryFiles,
                               bool IncludeBitcode) {
  uint64_t InputBytes = NativeBytes;
  uint64_t InputFiles = NativeFiles;
  if (IncludeBitcode) {
    InputBytes = llvm::SaturatingAdd<uint64_t>(InputBytes, BitcodeBytes);
    InputFiles = llvm::SaturatingAdd<uint64_t>(InputFiles, BitcodeFiles);
  }
  InputBytes = llvm::SaturatingAdd<uint64_t>(InputBytes, BinaryBytes);
  InputFiles = llvm::SaturatingAdd<uint64_t>(InputFiles, BinaryFiles);
  return {InputBytes, InputFiles};
}

} // namespace linker::elf::detail

#endif // NEVERC_LINKER_ELF_DRIVER_ELFINPUTWORKLOAD_H
