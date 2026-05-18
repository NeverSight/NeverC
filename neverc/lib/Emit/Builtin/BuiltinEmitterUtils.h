//===- BuiltinEmitterUtils.h - Shared helpers for split builtin TUs -------===//
//
// Internal header that exposes a small set of helpers originally defined as
// file-scope ``static`` functions inside BuiltinEmitter.cpp.  Splitting that
// single TU into per-target files (BuiltinEmitterAArch64.cpp,
// BuiltinEmitterX86.cpp) requires these helpers to be visible across the
// resulting TUs.
//
// Functions live in the global namespace to match their original definitions
// (only the ``static`` storage class was removed).

#ifndef NEVERC_LIB_CODEGEN_BUILTIN_CGBUILTINUTILS_H
#define NEVERC_LIB_CODEGEN_BUILTIN_CGBUILTINUTILS_H

#include "Core/Address.h"
#include "Core/FunctionEmitter.h"
#include "llvm/ADT/ArrayRef.h"
#include <optional>

namespace neverc {
class CallExpr;
}

namespace llvm {
class Type;
class Value;
} // namespace llvm

::neverc::Emit::Address
CheckAtomicAlignment(::neverc::Emit::FunctionEmitter &FE,
                     const ::neverc::CallExpr *E);

llvm::Value *emitCallMaybeConstrainedFPBuiltin(
    ::neverc::Emit::FunctionEmitter &FE, unsigned IntrinsicID,
    unsigned ConstrainedIntrinsicID, llvm::Type *Ty,
    llvm::ArrayRef<llvm::Value *> Args);

std::optional<::neverc::Emit::FunctionEmitter::MSVCIntrin>
translateAarch64ToMsvcIntrin(unsigned BuiltinID);

std::optional<::neverc::Emit::FunctionEmitter::MSVCIntrin>
translateX86ToMsvcIntrin(unsigned BuiltinID);

#endif // NEVERC_LIB_CODEGEN_BUILTIN_CGBUILTINUTILS_H
