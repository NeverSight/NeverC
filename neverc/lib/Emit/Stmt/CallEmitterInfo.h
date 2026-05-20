#ifndef NEVERC_LIB_EMIT_STMT_CALLEMITTERINFO_H
#define NEVERC_LIB_EMIT_STMT_CALLEMITTERINFO_H

#include "Core/EmitterValue.h"
#include "Stmt/EHScopeStack.h"
#include "neverc/Tree/Core/TreeFwd.h"
#include "neverc/Tree/Decl/GlobalDecl.h"
#include "neverc/Tree/Type/CanonicalType.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/IR/Value.h"

namespace llvm {
class Type;
class Value;
} // namespace llvm

namespace neverc {
class Decl;
class FunctionDecl;
class TargetOptions;
class VarDecl;

namespace Emit {

class FnCalleeInfo {
  const FunctionProtoType *CalleeProtoTy;
  GlobalDecl CalleeDecl;

public:
  explicit FnCalleeInfo() : CalleeProtoTy(nullptr) {}
  FnCalleeInfo(const FunctionProtoType *calleeProtoTy, GlobalDecl calleeDecl)
      : CalleeProtoTy(calleeProtoTy), CalleeDecl(calleeDecl) {}
  FnCalleeInfo(const FunctionProtoType *calleeProtoTy)
      : CalleeProtoTy(calleeProtoTy) {}
  FnCalleeInfo(GlobalDecl calleeDecl)
      : CalleeProtoTy(nullptr), CalleeDecl(calleeDecl) {}

  const FunctionProtoType *getCalleeFunctionProtoType() const {
    return CalleeProtoTy;
  }
  const GlobalDecl getCalleeDecl() const { return CalleeDecl; }
};

class FnCallee {
  enum class SpecialKind : uintptr_t {
    Invalid,
    Builtin,

    Last = Builtin
  };

  struct BuiltinInfoStorage {
    const FunctionDecl *Decl;
    unsigned ID;
  };
  SpecialKind KindOrFunctionPointer;
  union {
    FnCalleeInfo AbstractInfo;
    BuiltinInfoStorage BuiltinInfo;
  };

  explicit FnCallee(SpecialKind kind) : KindOrFunctionPointer(kind) {}

  FnCallee(const FunctionDecl *builtinDecl, unsigned builtinID)
      : KindOrFunctionPointer(SpecialKind::Builtin) {
    BuiltinInfo.Decl = builtinDecl;
    BuiltinInfo.ID = builtinID;
  }

public:
  FnCallee() : KindOrFunctionPointer(SpecialKind::Invalid) {}

  FnCallee(const FnCalleeInfo &abstractInfo, llvm::Value *functionPtr)
      : KindOrFunctionPointer(
            SpecialKind(reinterpret_cast<uintptr_t>(functionPtr))) {
    AbstractInfo = abstractInfo;
    assert(functionPtr && "configuring callee without function pointer");
    assert(functionPtr->getType()->isPointerTy());
  }

  static FnCallee forBuiltin(unsigned builtinID,
                             const FunctionDecl *builtinDecl) {
    FnCallee result(SpecialKind::Builtin);
    result.BuiltinInfo.Decl = builtinDecl;
    result.BuiltinInfo.ID = builtinID;
    return result;
  }

  static FnCallee forDirect(llvm::Constant *functionPtr,
                            const FnCalleeInfo &abstractInfo = FnCalleeInfo()) {
    return FnCallee(abstractInfo, functionPtr);
  }

  static FnCallee forDirect(llvm::FunctionCallee functionPtr,
                            const FnCalleeInfo &abstractInfo = FnCalleeInfo()) {
    return FnCallee(abstractInfo, functionPtr.getCallee());
  }

  bool isBuiltin() const {
    return KindOrFunctionPointer == SpecialKind::Builtin;
  }
  const FunctionDecl *getBuiltinDecl() const {
    assert(isBuiltin());
    return BuiltinInfo.Decl;
  }
  unsigned getBuiltinID() const {
    assert(isBuiltin());
    return BuiltinInfo.ID;
  }

  bool isOrdinary() const {
    return uintptr_t(KindOrFunctionPointer) > uintptr_t(SpecialKind::Last);
  }
  FnCalleeInfo getAbstractInfo() const {
    assert(isOrdinary());
    return AbstractInfo;
  }
  llvm::Value *getFunctionPointer() const {
    assert(isOrdinary());
    return reinterpret_cast<llvm::Value *>(uintptr_t(KindOrFunctionPointer));
  }
  void setFunctionPointer(llvm::Value *functionPtr) {
    assert(isOrdinary());
    KindOrFunctionPointer =
        SpecialKind(reinterpret_cast<uintptr_t>(functionPtr));
  }

  FnCallee prepareConcreteCallee(FunctionEmitter &FE) const;
};

struct CallArg {
private:
  union {
    RValue RV;
    LValue LV; /// The argument is semantically a load from this l-value.
  };
  bool HasLV;

  mutable bool IsUsed;

public:
  QualType Ty;
  CallArg(RValue rv, QualType ty)
      : RV(rv), HasLV(false), IsUsed(false), Ty(ty) {}
  CallArg(LValue lv, QualType ty)
      : LV(lv), HasLV(true), IsUsed(false), Ty(ty) {}
  bool hasLValue() const { return HasLV; }
  QualType getType() const { return Ty; }

  RValue getRValue(FunctionEmitter &FE) const;

  LValue getKnownLValue() const {
    assert(HasLV && !IsUsed);
    return LV;
  }
  RValue getKnownRValue() const {
    assert(!HasLV && !IsUsed);
    return RV;
  }
  void setRValue(RValue _RV) {
    assert(!HasLV);
    RV = _RV;
  }

  bool isAggregate() const { return HasLV || RV.isAggregate(); }

  void copyInto(FunctionEmitter &FE, Address A) const;
};

class CallArgList : public llvm::SmallVector<CallArg, 8> {
public:
  CallArgList() = default;

  void add(RValue rvalue, QualType type) { push_back(CallArg(rvalue, type)); }

  void addUncopiedAggregate(LValue LV, QualType type) {
    push_back(CallArg(LV, type));
  }
};

class FunctionArgList : public llvm::SmallVector<const VarDecl *, 16> {};

class ReturnValueSlot {
  Address Addr = Address::invalid();

  // Return value slot flags
  unsigned IsVolatile : 1;
  unsigned IsUnused : 1;
  unsigned IsExternallyDestructed : 1;

public:
  ReturnValueSlot()
      : IsVolatile(false), IsUnused(false), IsExternallyDestructed(false) {}
  ReturnValueSlot(Address Addr, bool IsVolatile, bool IsUnused = false,
                  bool IsExternallyDestructed = false)
      : Addr(Addr), IsVolatile(IsVolatile), IsUnused(IsUnused),
        IsExternallyDestructed(IsExternallyDestructed) {}

  bool isNull() const { return !Addr.isValid(); }
  bool isVolatile() const { return IsVolatile; }
  Address getValue() const { return Addr; }
  bool isUnused() const { return IsUnused; }
  bool isExternallyDestructed() const { return IsExternallyDestructed; }
};

void mergeDefaultFunctionDefinitionAttributes(llvm::Function &F,
                                              const CodeGenOptions &CodeGenOpts,
                                              const LangOptions &LangOpts,
                                              const TargetOptions &TargetOpts,
                                              bool WillInternalize);

enum class FnInfoOpts {
  None = 0,
  IsInstanceMethod = 1 << 0,
  IsChainCall = 1 << 1,
  IsDelegateCall = 1 << 2,
};

inline FnInfoOpts operator|(FnInfoOpts A, FnInfoOpts B) {
  return static_cast<FnInfoOpts>(llvm::to_underlying(A) |
                                 llvm::to_underlying(B));
}

inline FnInfoOpts operator&(FnInfoOpts A, FnInfoOpts B) {
  return static_cast<FnInfoOpts>(llvm::to_underlying(A) &
                                 llvm::to_underlying(B));
}

inline FnInfoOpts operator|=(FnInfoOpts A, FnInfoOpts B) {
  A = A | B;
  return A;
}

inline FnInfoOpts operator&=(FnInfoOpts A, FnInfoOpts B) {
  A = A & B;
  return A;
}

} // end namespace Emit
} // end namespace neverc

#endif
