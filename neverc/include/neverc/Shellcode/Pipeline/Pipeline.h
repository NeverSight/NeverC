#ifndef NEVERC_SHELLCODE_PIPELINE_H
#define NEVERC_SHELLCODE_PIPELINE_H

#include "neverc/Shellcode/Pipeline/ShellcodeOptions.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"
#include <cstdint>
#include <functional>

namespace llvm {
class PassBuilder;
class TargetPassConfig;
}

namespace neverc {
namespace shellcode {

using ObfuscationHook =
    std::function<void(llvm::ModulePassManager &, const ShellcodeOptions &)>;

using MachineObfuscationHook =
    std::function<void(llvm::TargetPassConfig &, const ShellcodeOptions &)>;

using BinaryObfuscationHook = std::function<void(
    llvm::SmallVectorImpl<uint8_t> &, const ShellcodeOptions &)>;

struct ObfuscationHooks {
  ObfuscationHook RunBeforePrep;
  ObfuscationHook RunAfterPrep;
  ObfuscationHook RunBeforeInlining;

  ObfuscationHook RunAfterInlining;
  ObfuscationHook RunAfterStackify;
  ObfuscationHook RunAfterFinalIR;

  MachineObfuscationHook RunBeforePreEmit;
  MachineObfuscationHook RunAfterPreEmit;
  MachineObfuscationHook RunAfterFinalMIR;

  BinaryObfuscationHook RunPostExtract;
  BinaryObfuscationHook RunPostFinalize;
};

void setShellcodeObfuscationHooks(ObfuscationHooks H);
const ObfuscationHooks &getShellcodeObfuscationHooks();

void registerShellcodePasses(llvm::PassBuilder &PB,
                             const ShellcodeOptions &Opts);
void registerShellcodeMachinePasses(const ShellcodeOptions &Opts);
const ShellcodeOptions &getCurrentShellcodeOptions();

void applyPostExtractObfuscationHook(llvm::SmallVectorImpl<uint8_t> &Bytes);
void applyPostFinalizeObfuscationHook(llvm::SmallVectorImpl<uint8_t> &Bytes);

}
}

#endif
