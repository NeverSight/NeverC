//===- InputWorkload.h - COFF adaptive workload accounting ------*- C++ -*-===//

#ifndef NEVERC_LINKER_COFF_DRIVER_INPUTWORKLOAD_H
#define NEVERC_LINKER_COFF_DRIVER_INPUTWORKLOAD_H

#include "llvm/Support/MathExtras.h"

#include <cstdint>
#include <utility>

namespace linker::coff::detail {

inline std::pair<uint64_t, uint64_t>
mergeMaterializedInputWorkload(uint64_t NativeBytes, uint64_t NativeFiles,
                               uint64_t BitcodeBytes, uint64_t BitcodeFiles,
                               uint64_t ResourceBytes, uint64_t ResourceFiles,
                               bool IncludeBitcode) {
  uint64_t InputBytes = NativeBytes;
  uint64_t InputFiles = NativeFiles;
  if (IncludeBitcode) {
    InputBytes = llvm::SaturatingAdd<uint64_t>(InputBytes, BitcodeBytes);
    InputFiles = llvm::SaturatingAdd<uint64_t>(InputFiles, BitcodeFiles);
  }
  InputBytes = llvm::SaturatingAdd<uint64_t>(InputBytes, ResourceBytes);
  InputFiles = llvm::SaturatingAdd<uint64_t>(InputFiles, ResourceFiles);
  return {InputBytes, InputFiles};
}

} // namespace linker::coff::detail

#endif // NEVERC_LINKER_COFF_DRIVER_INPUTWORKLOAD_H
