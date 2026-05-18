#ifndef NEVERC_LIB_DRIVER_TOOLCHAINS_ARCH_X86_H
#define NEVERC_LIB_DRIVER_TOOLCHAINS_ARCH_X86_H

#include "neverc/Invoke/Driver.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Option.h"
#include "llvm/TargetParser/Triple.h"
#include <string>
#include <vector>

namespace neverc {
namespace driver {
namespace tools {
namespace x86 {

std::string getX86TargetCPU(const Driver &D, const llvm::opt::ArgList &Args,
                            const llvm::Triple &Triple);

void getX86TargetFeatures(const Driver &D, const llvm::Triple &Triple,
                          const llvm::opt::ArgList &Args,
                          std::vector<llvm::StringRef> &Features);

} // end namespace x86
} // namespace tools
} // end namespace driver
} // end namespace neverc

#endif // NEVERC_LIB_DRIVER_TOOLCHAINS_ARCH_X86_H
