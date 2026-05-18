#ifndef NEVERC_SHELLCODE_SHELLCODEEXTRACTOR_H
#define NEVERC_SHELLCODE_SHELLCODEEXTRACTOR_H

#include "neverc/Shellcode/Pipeline/ShellcodeOptions.h"
#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace shellcode {

int extractShellcode(llvm::StringRef InputObj, llvm::StringRef OutputBin,
                     const ShellcodeOptions &Opts);

}
}

#endif
