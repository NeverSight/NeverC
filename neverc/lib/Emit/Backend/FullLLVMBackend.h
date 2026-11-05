//===- FullLLVMBackend.h - Full LLVM machine output -------------*- C++ -*-===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_EMIT_BACKEND_FULLLLVMBACKEND_H
#define NEVERC_EMIT_BACKEND_FULLLLVMBACKEND_H

#include "neverc/Emit/Backend/BackendUtil.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"

namespace neverc {

/// Run the explicitly selected full LLVM fork after the frontend IR pipeline.
/// No LLVM C++ objects cross the process boundary. Return only complete output.
llvm::Expected<llvm::SmallVector<char, 0>>
emitWithFullLLVM(llvm::Module &Module, BackendAction Action,
                 const CodeGenOptions &Options, const TargetOptions &Target);

} // namespace neverc

#endif // NEVERC_EMIT_BACKEND_FULLLLVMBACKEND_H
