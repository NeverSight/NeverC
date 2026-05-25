#ifndef NEVERC_EMIT_DECL_STRING_CLEANUP_H
#define NEVERC_EMIT_DECL_STRING_CLEANUP_H

#include "Core/FunctionEmitter.h"
#include "Stmt/CleanupEmitterInfo.h"
#include "neverc/Foundation/Builtin/BuiltinStringNames.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/IR/IRBuilder.h"

namespace neverc {
namespace Emit {

inline bool isNeverCStringRecord(QualType T) {
  if (auto *RT = T->getAs<RecordType>()) {
    if (auto *II = RT->getDecl()->getIdentifier())
      return II->getName() == BuiltinStringNames::RecordName;
  }
  return false;
}

inline bool typeContainsNeverCString(const TreeContext &Ctx, QualType T) {
  if (isNeverCStringRecord(T))
    return true;
  if (auto *CAT = Ctx.getAsConstantArrayType(T))
    return typeContainsNeverCString(Ctx, CAT->getElementType());
  if (auto *RT = T->getAs<RecordType>()) {
    if (auto *RD = RT->getDecl()->getDefinition()) {
      for (auto *FD : RD->fields())
        if (typeContainsNeverCString(Ctx, FD->getType()))
          return true;
    }
  }
  return false;
}

llvm::Function *getOrCreateStringFreeHelper(llvm::Module &M);

void emitStringFieldCleanups(FunctionEmitter &FE, llvm::Value *BaseAddr,
                             QualType T);

struct NeverCStringCompositeCleanup final : EHScopeStack::Cleanup {
  const VarDecl &Var;

  NeverCStringCompositeCleanup(const VarDecl *V) : Var(*V) {}

  void Emit(FunctionEmitter &FE, Flags flags) override {
    DeclRefExpr DRE(FE.getContext(), const_cast<VarDecl *>(&Var),
                    Var.getType(), VK_LValue, SourceLocation());
    llvm::Value *Addr = FE.genDeclRefLValue(&DRE).getPointer(FE);
    emitStringFieldCleanups(FE, Addr, Var.getType());
  }
};

} // namespace Emit
} // namespace neverc

#endif
