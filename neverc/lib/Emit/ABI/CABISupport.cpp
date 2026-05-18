#include "ABI/EmitterABI.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"

using namespace neverc;
using namespace Emit;

CGABI::CGABI(ModuleEmitter &ME)
    : ME(ME), MangleCtx(ME.getContext().createMangleContext()) {}

CGABI::~CGABI() {}

namespace {
void emitGlobalDtorWithCXAAtExit(FunctionEmitter &FE, llvm::FunctionCallee dtor,
                                 llvm::Constant *addr, bool TLS) {
  assert((TLS || FE.getTypes().getCodeGenOpts().CXAAtExit));
  const char *Name = "__cxa_atexit";
  if (TLS) {
    const llvm::Triple &T = FE.getTarget().getTriple();
    Name = T.isOSDarwin() ? "_tlv_atexit" : "__cxa_thread_atexit";
  }

  auto AddrAS = addr ? addr->getType()->getPointerAddressSpace() : 0;
  auto AddrPtrTy = AddrAS ? llvm::PointerType::get(FE.getLLVMContext(), AddrAS)
                          : FE.Int8PtrTy;
  llvm::Constant *handle =
      FE.ME.createRuntimeVariable(FE.Int8Ty, "__dso_handle");
  cast<llvm::GlobalValue>(handle->stripPointerCasts())
      ->setVisibility(llvm::GlobalValue::HiddenVisibility);

  llvm::Type *paramTys[] = {FE.UnqualPtrTy, AddrPtrTy, handle->getType()};
  auto *atexitTy = llvm::FunctionType::get(FE.IntTy, paramTys, false);
  llvm::FunctionCallee atexit = FE.ME.createRuntimeFunction(atexitTy, Name);
  if (auto *fn = dyn_cast<llvm::Function>(atexit.getCallee()))
    fn->setDoesNotThrow();

  if (!addr)
    addr = llvm::Constant::getNullValue(FE.Int8PtrTy);
  llvm::Value *args[] = {dtor.getCallee(), addr, handle};
  FE.genNounwindRuntimeCall(atexit, args);
}

llvm::Function *createGlobalInitOrCleanupFn(ModuleEmitter &ME,
                                            llvm::StringRef FnName) {
  auto *FTy = llvm::FunctionType::get(ME.VoidTy, false);
  return ME.createGlobalInitOrCleanUpFunction(
      FTy, FnName, ME.getTypes().arrangeNullaryFunction());
}
} // namespace

void ModuleEmitter::registerGlobalDtorsWithAtExit() {
  for (const auto &I : DtorsUsingAtExit) {
    auto *GlobalInitFn = createGlobalInitOrCleanupFn(
        *this, std::string("__GLOBAL_init_") + std::to_string(I.first));
    FunctionEmitter FE(*this);
    FE.startFunction(GlobalDecl(), getContext().VoidTy, GlobalInitFn,
                     getTypes().arrangeNullaryFunction(), FunctionArgList(),
                     SourceLocation(), SourceLocation());
    auto AL = ApplyDebugLocation::CreateArtificial(FE);
    for (auto *Dtor : I.second) {
      if (getCodeGenOpts().CXAAtExit)
        emitGlobalDtorWithCXAAtExit(FE, Dtor, nullptr, false);
      else
        FE.registerGlobalDtorWithAtExit(Dtor);
    }
    FE.finishFunction();
    addGlobalCtor(GlobalInitFn, I.first);
  }
}
