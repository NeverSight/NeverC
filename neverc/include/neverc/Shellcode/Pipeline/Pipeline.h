#ifndef NEVERC_SHELLCODE_PIPELINE_H
#define NEVERC_SHELLCODE_PIPELINE_H

#include "neverc/Shellcode/Pipeline/ShellcodeOptions.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>

namespace llvm {
class PassBuilder;
class TargetPassConfig;
}

namespace neverc {
namespace shellcode {

void registerShellcodePasses(llvm::PassBuilder &PB,
                             const ShellcodeOptions &Opts);
void registerShellcodeMachinePasses(const ShellcodeOptions &Opts);
const ShellcodeOptions &getCurrentShellcodeOptions();

void applyPostExtractObfuscationHook(llvm::SmallVectorImpl<uint8_t> &Bytes);
void applyPostFinalizeObfuscationHook(llvm::SmallVectorImpl<uint8_t> &Bytes);

}
}

#endif
