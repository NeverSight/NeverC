#ifndef NEVERC_LIB_CODEGEN_ABI_TARGETINFO_H
#define NEVERC_LIB_CODEGEN_ABI_TARGETINFO_H

#include "Core/EmitterBuilder.h"
#include "Core/EmitterValue.h"
#include "Core/ModuleEmitter.h"
#include "neverc/Foundation/Core/SyncScope.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
class Constant;
class GlobalValue;
class Type;
class Value;
} // namespace llvm

namespace neverc {
class Decl;

namespace Emit {
class ABIInfo;
class CallArgList;
class FunctionEmitter;

class TargetCodeGenInfo {
  std::unique_ptr<ABIInfo> Info;

protected:
  template <typename T> const T &getABIInfo() const {
    return static_cast<const T &>(*Info);
  }

public:
  TargetCodeGenInfo(std::unique_ptr<ABIInfo> Info);
  virtual ~TargetCodeGenInfo();

  const ABIInfo &getABIInfo() const { return *Info; }

  virtual void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                                   Emit::ModuleEmitter &M) const {}

  virtual void
  emitTargetMetadata(Emit::ModuleEmitter &ME,
                     const llvm::MapVector<GlobalDecl, llvm::StringRef>
                         &MangledDeclNames) const {}

  virtual void emitTargetGlobals(Emit::ModuleEmitter &ME) const {}

  virtual void checkFunctionCallABI(ModuleEmitter &ME, SourceLocation CallLoc,
                                    const FunctionDecl *Caller,
                                    const FunctionDecl *Callee,
                                    const CallArgList &Args) const {}

  virtual bool extendPointerWithSExt() const { return false; }

  virtual int getDwarfEHStackPointer(Emit::ModuleEmitter &M) const {
    return -1;
  }

  virtual bool initDwarfEHRegSizeTable(Emit::FunctionEmitter &FE,
                                       llvm::Value *Address) const {
    return true;
  }

  virtual llvm::Value *decodeReturnAddress(Emit::FunctionEmitter &FE,
                                           llvm::Value *Address) const {
    return Address;
  }

  virtual llvm::Value *encodeReturnAddress(Emit::FunctionEmitter &FE,
                                           llvm::Value *Address) const {
    return Address;
  }

  virtual llvm::Value *testFPKind(llvm::Value *V, unsigned BuiltinID,
                                  CGBuilderTy &Builder,
                                  ModuleEmitter &ME) const {
    assert(V->getType()->isFloatingPointTy() && "V should have an FP type.");
    return nullptr;
  }

  virtual llvm::Type *adjustInlineAsmType(Emit::FunctionEmitter &FE,
                                          llvm::StringRef Constraint,
                                          llvm::Type *Ty) const {
    return Ty;
  }

  virtual bool isScalarizableAsmOperand(Emit::FunctionEmitter &FE,
                                        llvm::Type *Ty) const {
    return false;
  }

  virtual void
  addReturnRegisterOutputs(Emit::FunctionEmitter &FE, Emit::LValue ReturnValue,
                           std::string &Constraints,
                           std::vector<llvm::Type *> &ResultRegTypes,
                           std::vector<llvm::Type *> &ResultTruncRegTypes,
                           std::vector<Emit::LValue> &ResultRegDests,
                           std::string &AsmString, unsigned NumOutputs) const {}

  virtual bool doesReturnSlotInterfereWithArgs() const { return true; }

  virtual bool isNoProtoCallVariadic(const Emit::CallArgList &args,
                                     const FunctionNoProtoType *fnType) const;

  virtual void getDependentLibraryOption(llvm::StringRef Lib,
                                         llvm::SmallString<24> &Opt) const;

  virtual void getDetectMismatchOption(llvm::StringRef Name,
                                       llvm::StringRef Value,
                                       llvm::SmallString<32> &Opt) const {}

  virtual llvm::Constant *getNullPointer(const Emit::ModuleEmitter &ME,
                                         llvm::PointerType *T,
                                         QualType QT) const;

  virtual LangAS getGlobalVarAddressSpace(ModuleEmitter &ME,
                                          const VarDecl *D) const;

  virtual LangAS getASTAllocaAddressSpace() const { return LangAS::Default; }

  virtual llvm::Value *performAddrSpaceCast(Emit::FunctionEmitter &FE,
                                            llvm::Value *V, LangAS SrcAddr,
                                            LangAS DestAddr, llvm::Type *DestTy,
                                            bool IsNonNull = false) const;

  virtual llvm::Constant *performAddrSpaceCast(ModuleEmitter &ME,
                                               llvm::Constant *V,
                                               LangAS SrcAddr, LangAS DestAddr,
                                               llvm::Type *DestTy) const;

  virtual llvm::SyncScope::ID getLLVMSyncScopeID(const LangOptions &LangOpts,
                                                 SyncScope Scope,
                                                 llvm::AtomicOrdering Ordering,
                                                 llvm::LLVMContext &Ctx) const;

  virtual bool shouldEmitStaticExternCAliases() const { return true; }

  virtual bool shouldEmitDWARFBitFieldSeparators() const { return false; }

protected:
  static std::string qualifyWindowsLibrary(llvm::StringRef Lib);

  void addStackProbeTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                                     Emit::ModuleEmitter &ME) const;
};

std::unique_ptr<TargetCodeGenInfo>
createDefaultTargetCodeGenInfo(ModuleEmitter &ME);

enum class AArch64ABIKind {
  AAPCS = 0,
  DarwinPCS,
};

std::unique_ptr<TargetCodeGenInfo>
createAArch64TargetCodeGenInfo(ModuleEmitter &ME, AArch64ABIKind Kind);

enum class X86AVXABILevel {
  None,
  AVX,
  AVX512,
};

std::unique_ptr<TargetCodeGenInfo>
createX86_64TargetCodeGenInfo(ModuleEmitter &ME, X86AVXABILevel AVXLevel);

std::unique_ptr<TargetCodeGenInfo>
createWinX86_64TargetCodeGenInfo(ModuleEmitter &ME, X86AVXABILevel AVXLevel);

} // namespace Emit
} // namespace neverc

#endif // NEVERC_LIB_CODEGEN_ABI_TARGETINFO_H
