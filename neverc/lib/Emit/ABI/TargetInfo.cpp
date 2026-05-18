#include "ABI/TargetInfo.h"
#include "ABI/ABIInfo.h"
#include "ABI/ABIInfoImpl.h"
#include "Core/FunctionEmitter.h"
#include "neverc/Emit/ABI/ABIFunctionInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"

using namespace neverc;
using namespace Emit;
using namespace CodeGen;

LLVM_DUMP_METHOD void ABIArgInfo::dump() const {
  llvm::raw_ostream &OS = llvm::errs();
  OS << "(ABIArgInfo Kind=";
  switch (TheKind) {
  case Direct:
    OS << "Direct Type=";
    if (llvm::Type *Ty = getCoerceToType())
      Ty->print(OS);
    else
      OS << "null";
    break;
  case Extend:
    OS << "Extend";
    break;
  case Ignore:
    OS << "Ignore";
    break;
  case Indirect:
    OS << "Indirect Align=" << getIndirectAlign().getQuantity()
       << " ByVal=" << getIndirectByVal()
       << " Realign=" << getIndirectRealign();
    break;
  case IndirectAliased:
    OS << "Indirect Align=" << getIndirectAlign().getQuantity()
       << " AadrSpace=" << getIndirectAddrSpace()
       << " Realign=" << getIndirectRealign();
    break;
  case Expand:
    OS << "Expand";
    break;
  case CoerceAndExpand:
    OS << "CoerceAndExpand Type=";
    getCoerceAndExpandType()->print(OS);
    break;
  }
  OS << ")\n";
}

TargetCodeGenInfo::TargetCodeGenInfo(std::unique_ptr<ABIInfo> Info)
    : Info(std::move(Info)) {}

TargetCodeGenInfo::~TargetCodeGenInfo() = default;

bool TargetCodeGenInfo::isNoProtoCallVariadic(
    const CallArgList &args, const FunctionNoProtoType *fnType) const {
  return false;
}

void TargetCodeGenInfo::getDependentLibraryOption(
    llvm::StringRef Lib, llvm::SmallString<24> &Opt) const {
  // This assumes the user is passing a library name like "rt" instead of a
  // filename like "librt.a/so", and that they don't care whether it's static or
  // dynamic.
  Opt = "-l";
  Opt += Lib;
}

llvm::Constant *TargetCodeGenInfo::getNullPointer(const Emit::ModuleEmitter &ME,
                                                  llvm::PointerType *T,
                                                  QualType QT) const {
  return llvm::ConstantPointerNull::get(T);
}

LangAS TargetCodeGenInfo::getGlobalVarAddressSpace(ModuleEmitter &ME,
                                                   const VarDecl *D) const {
  assert("Address space agnostic languages only");
  return D ? D->getType().getAddressSpace() : LangAS::Default;
}

llvm::Value *TargetCodeGenInfo::performAddrSpaceCast(
    Emit::FunctionEmitter &FE, llvm::Value *Src, LangAS SrcAddr,
    LangAS DestAddr, llvm::Type *DestTy, bool isNonNull) const {
  // Since target may map different address spaces in AST to the same address
  // space, an address space conversion may end up as a bitcast.
  if (auto *C = dyn_cast<llvm::Constant>(Src))
    return performAddrSpaceCast(FE.ME, C, SrcAddr, DestAddr, DestTy);
  // Try to preserve the source's name to make IR more readable.
  return FE.Builder.CreateAddrSpaceCast(
      Src, DestTy, Src->hasName() ? Src->getName() + ".ascast" : "");
}

llvm::Constant *
TargetCodeGenInfo::performAddrSpaceCast(ModuleEmitter &ME, llvm::Constant *Src,
                                        LangAS SrcAddr, LangAS DestAddr,
                                        llvm::Type *DestTy) const {
  // Since target may map different address spaces in AST to the same address
  // space, an address space conversion may end up as a bitcast.
  return llvm::ConstantExpr::getPointerCast(Src, DestTy);
}

llvm::SyncScope::ID TargetCodeGenInfo::getLLVMSyncScopeID(
    const LangOptions &LangOpts, SyncScope Scope, llvm::AtomicOrdering Ordering,
    llvm::LLVMContext &Ctx) const {
  return Ctx.getOrInsertSyncScopeID(""); /* default sync scope */
}

void TargetCodeGenInfo::addStackProbeTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, Emit::ModuleEmitter &ME) const {
  if (llvm::Function *Fn = dyn_cast_or_null<llvm::Function>(GV)) {
    if (ME.getCodeGenOpts().StackProbeSize != 4096)
      Fn->addFnAttr("stack-probe-size",
                    llvm::utostr(ME.getCodeGenOpts().StackProbeSize));
    if (ME.getCodeGenOpts().NoStackArgProbe)
      Fn->addFnAttr("no-stack-arg-probe");
  }
}

namespace {
class DefaultTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  DefaultTargetCodeGenInfo(Emit::TypeEmitter &CGT)
      : TargetCodeGenInfo(std::make_unique<DefaultABIInfo>(CGT)) {}
};
} // namespace

std::unique_ptr<TargetCodeGenInfo>
Emit::createDefaultTargetCodeGenInfo(ModuleEmitter &ME) {
  return std::make_unique<DefaultTargetCodeGenInfo>(ME.getTypes());
}
