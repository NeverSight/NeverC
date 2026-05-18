#ifndef NEVERC_LIB_DRIVER_TOOLCHAINS_ARCH_AARCH64_H
#define NEVERC_LIB_DRIVER_TOOLCHAINS_ARCH_AARCH64_H

#include "neverc/Invoke/Driver.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Option.h"
#include <string>
#include <vector>

namespace neverc {
namespace driver {
namespace tools {
namespace aarch64 {

void getAArch64TargetFeatures(const Driver &D, const llvm::Triple &Triple,
                              const llvm::opt::ArgList &Args,
                              std::vector<llvm::StringRef> &Features,
                              bool ForAS);

std::string getAArch64TargetCPU(const llvm::opt::ArgList &Args,
                                const llvm::Triple &Triple, llvm::opt::Arg *&A);

} // end namespace aarch64
} // namespace tools
} // end namespace driver
} // end namespace neverc

#endif // NEVERC_LIB_DRIVER_TOOLCHAINS_ARCH_AARCH64_H
