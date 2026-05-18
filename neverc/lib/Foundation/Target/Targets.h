#ifndef NEVERC_LIB_BASIC_TARGET_TARGETS_H
#define NEVERC_LIB_BASIC_TARGET_TARGETS_H

#include "neverc/Foundation/Core/MacroBuilder.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace targets {

LLVM_LIBRARY_VISIBILITY
std::unique_ptr<neverc::TargetInfo>
AllocateTarget(const llvm::Triple &Triple, const neverc::TargetOptions &Opts);

LLVM_LIBRARY_VISIBILITY
void DefineStd(neverc::MacroBuilder &Builder, llvm::StringRef MacroName,
               const neverc::LangOptions &Opts);

LLVM_LIBRARY_VISIBILITY
void defineCPUMacros(neverc::MacroBuilder &Builder, llvm::StringRef CPUName,
                     bool Tuning = true);

} // namespace targets
} // namespace neverc
#endif // NEVERC_LIB_BASIC_TARGET_TARGETS_H
