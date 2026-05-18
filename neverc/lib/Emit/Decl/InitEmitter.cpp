#include "ABI/EmitterABI.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "llvm/IR/Intrinsics.h"

using namespace neverc;
using namespace Emit;

void FunctionEmitter::genInvariantStart(llvm::Constant *Addr, CharUnits Size) {
  if (!ME.getCodeGenOpts().OptimizationLevel)
    return;
  llvm::Type *ObjectPtr[1] = {Int8PtrTy};
  auto *InvariantStart =
      ME.getIntrinsic(llvm::Intrinsic::invariant_start, ObjectPtr);
  llvm::Value *Args[2] = {
      llvm::ConstantInt::getSigned(Int64Ty, Size.getQuantity()), Addr};
  Builder.CreateCall(InvariantStart, Args);
}

llvm::Function *ModuleEmitter::createGlobalInitOrCleanUpFunction(
    llvm::FunctionType *FTy, const llvm::Twine &Name, const ABIFunctionInfo &FI,
    bool TLS, llvm::GlobalVariable::LinkageTypes Linkage) {
  auto *Fn = llvm::Function::Create(FTy, Linkage, Name, &getModule());
  if (!TLS)
    if (const char *Section = getTarget().getStaticInitSectionSpecifier())
      Fn->setSection(Section);
  if (Linkage == llvm::GlobalVariable::InternalLinkage)
    setInternalFunctionAttributes(GlobalDecl(), Fn, FI);
  Fn->setCallingConv(getRuntimeCC());
  if (!getLangOpts().Exceptions)
    Fn->setDoesNotThrow();
  return Fn;
}

void FunctionEmitter::registerGlobalDtorWithAtExit(llvm::Constant *dtorStub) {
  llvm::FunctionType *atexitTy =
      llvm::FunctionType::get(IntTy, {dtorStub->getType()}, false);
  llvm::FunctionCallee atexit =
      ME.createRuntimeFunction(atexitTy, "atexit", llvm::AttributeList(), true);
  if (auto *atexitFn = dyn_cast<llvm::Function>(atexit.getCallee()))
    atexitFn->setDoesNotThrow();
  genNounwindRuntimeCall(atexit, dtorStub);
}
