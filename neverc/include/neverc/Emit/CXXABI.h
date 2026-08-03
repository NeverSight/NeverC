//===--- CXXABI.h - NeverC C++ ABI interface --------------------*- C++ -*-===//
#ifndef NEVERC_EMIT_CXXABI_H
#define NEVERC_EMIT_CXXABI_H

#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Value.h"

namespace neverc {
namespace Emit {

class ModuleEmitter;
class FunctionEmitter;

/// NeverC-only C++ ABI v1 interface.
/// Not compatible with system libstdc++ / libc++ ABIs.
class CXXABI {
public:
  virtual ~CXXABI() = default;

  virtual llvm::Value *emitCXXNew(FunctionEmitter &FE, const CXXNewExpr *E) = 0;
  virtual void emitCXXDelete(FunctionEmitter &FE, const CXXDeleteExpr *E) = 0;
  virtual llvm::Value *emitDynamicCast(FunctionEmitter &FE,
                                       llvm::Value *Value,
                                       QualType SrcTy, QualType DestTy) = 0;
  virtual void emitRethrow(FunctionEmitter &FE, bool isNoReturn) = 0;
  virtual void emitThrow(FunctionEmitter &FE, const CXXThrowExpr *E) = 0;

  /// Emit vtable group for a dynamic class (NeverC ABI v1 layout).
  virtual void emitVTable(ModuleEmitter &ME, const CXXRecordDecl *RD) = 0;

  /// Emit VTT (virtual table table) for classes with virtual bases.
  /// NeverC ABI v1: array of vtable pointers [primary, vbases...].
  virtual void emitVTT(ModuleEmitter &ME, const CXXRecordDecl *RD) {
    (void)ME;
    (void)RD;
  }

  /// Load virtual method pointer from object's vptr (NeverC ABI v1).
  /// Slot index is into the virtual method list (0-based after RTTI slot).
  virtual llvm::Value *getVirtualFunctionPointer(FunctionEmitter &FE,
                                                 llvm::Value *This,
                                                 const CXXMethodDecl *MD) {
    (void)FE;
    (void)This;
    (void)MD;
    return nullptr;
  }

  /// RTTI type_info global for a type.
  virtual llvm::Constant *getAddrOfRTTIDescriptor(QualType Ty) = 0;
};

CXXABI *CreateNeverCCXXABI(ModuleEmitter &ME);

} // namespace Emit
} // namespace neverc

#endif
