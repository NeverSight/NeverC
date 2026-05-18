#ifndef NEVERC_SHELLCODE_MIRPREPPASS_H
#define NEVERC_SHELLCODE_MIRPREPPASS_H

#include "neverc/Shellcode/Pipeline/ShellcodeOptions.h"

namespace llvm {
class FunctionPass;
}

namespace neverc {
namespace shellcode {

llvm::FunctionPass *createShellcodeMIRPrepPass(const ShellcodeOptions &Opts);

}
}

#endif
