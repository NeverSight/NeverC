#include "Core/AndroidKernelEmitter.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>

using namespace neverc::Emit;

static llvm::GlobalVariable *
emitWeakPad(llvm::Module &M, llvm::StringRef Name, llvm::StringRef Section) {
  if (auto *GV = M.getGlobalVariable(Name))
    return GV;
  auto &Ctx = M.getContext();
  auto *I8 = llvm::Type::getInt8Ty(Ctx);
  auto *GV = new llvm::GlobalVariable(
      M, I8, true, llvm::GlobalValue::WeakAnyLinkage,
      llvm::ConstantInt::get(I8, 0), Name);
  GV->setSection(Section);
  GV->setAlignment(llvm::Align(1));
  GV->setDSOLocal(true);
  return GV;
}

static void emitPLTSections(llvm::Module &M) {
  emitWeakPad(M, "__nvk_plt", ".plt");
  emitWeakPad(M, "__nvk_init_plt", ".init.plt");
  emitWeakPad(M, "__nvk_ftrace", ".text.ftrace_trampoline");
}

static void emitEmptyVersionsSection(llvm::Module &M) {
  if (auto *GV = M.getGlobalVariable("__nvk_versions"))
    return;
  auto &Ctx = M.getContext();
  auto *Arr = llvm::ArrayType::get(llvm::Type::getInt8Ty(Ctx), 0);
  auto *GV = new llvm::GlobalVariable(
      M, Arr, true, llvm::GlobalValue::WeakAnyLinkage,
      llvm::ConstantAggregateZero::get(Arr), "__nvk_versions");
  GV->setSection("__versions");
  GV->setDSOLocal(true);
}

static void emitCFIStubFn(llvm::Module &M, llvm::StringRef Name,
                          llvm::GlobalValue::VisibilityTypes Vis,
                          unsigned Align) {
  if (M.getFunction(Name))
    return;
  auto &Ctx = M.getContext();
  auto *FTy = llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), false);
  auto *F = llvm::Function::Create(FTy, llvm::GlobalValue::WeakAnyLinkage,
                                   Name, &M);
  F->setVisibility(Vis);
  F->setDSOLocal(true);
  F->setAlignment(llvm::Align(Align));
  F->setSection(".text");
  F->addFnAttr(llvm::Attribute::Naked);
  F->addFnAttr(llvm::Attribute::NoUnwind);
  auto *BB = llvm::BasicBlock::Create(Ctx, "", F);
  auto *IAsmTy = llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), false);
  auto *Body = llvm::InlineAsm::get(IAsmTy, "hint #25\nhint #29\nret",
                                    "", true, false);
  auto *CI = llvm::CallInst::Create(IAsmTy, Body, "", BB);
  CI->setDoesNotThrow();
  new llvm::UnreachableInst(Ctx, BB);
}

static void emitCFICheckStubs(llvm::Module &M) {
  emitCFIStubFn(M, "__cfi_check",
                llvm::GlobalValue::DefaultVisibility, 4096);
  emitCFIStubFn(M, "__cfi_check_fail",
                llvm::GlobalValue::HiddenVisibility, 4);
}

static uint32_t consumeKCFITypeId(llvm::Module &M,
                                  llvm::StringRef MarkerName) {
  llvm::GlobalVariable *Marker = M.getNamedGlobal(MarkerName);
  if (!Marker)
    return 0;

  auto *Value =
      llvm::dyn_cast_or_null<llvm::ConstantInt>(Marker->getInitializer());
  if (!Value || Value->getBitWidth() != 32)
    llvm::report_fatal_error("invalid Android kernel KCFI type-id marker " +
                             MarkerName);
  if (!Marker->use_empty())
    llvm::report_fatal_error("referenced Android kernel KCFI type-id marker " +
                             MarkerName);

  const uint32_t TypeId = static_cast<uint32_t>(Value->getZExtValue());
  Marker->eraseFromParent();
  return TypeId;
}

static void emitKCFIEntryPrefix(llvm::Module &M, llvm::StringRef EntryName,
                                llvm::StringRef MarkerName) {
  const uint32_t TypeId = consumeKCFITypeId(M, MarkerName);
  if (TypeId == 0)
    return;

  llvm::GlobalValue *Entry = M.getNamedValue(EntryName);
  llvm::Function *Function =
      Entry
          ? llvm::dyn_cast_or_null<llvm::Function>(Entry->getAliaseeObject())
          : nullptr;
  if (!Function || Function->isDeclaration())
    llvm::report_fatal_error("Android kernel KCFI entry is not defined: " +
                             EntryName);

  auto *Prefix = llvm::ConstantInt::get(
      llvm::Type::getInt32Ty(M.getContext()), TypeId);
  if (Function->hasPrefixData()) {
    auto *Existing =
        llvm::dyn_cast<llvm::ConstantInt>(Function->getPrefixData());
    if (!Existing || Existing->getValue() != Prefix->getValue())
      llvm::report_fatal_error("conflicting prefix data on Android kernel "
                               "KCFI entry " +
                               EntryName);
    return;
  }
  Function->setPrefixData(Prefix);
}

static void emitKCFIEntryPrefixes(llvm::Module &M) {
  emitKCFIEntryPrefix(M, "init_module",
                      "__neverc_krt_kcfi_init_module_typeid");
  emitKCFIEntryPrefix(M, "cleanup_module",
                      "__neverc_krt_kcfi_cleanup_module_typeid");
}

// Apply per-function attributes that cannot be expressed via ToolChain flags:
//   ShadowCallStack  — -fsanitize=shadow-call-stack would pull in the
//                      sanitizer runtime which the kernel does not export
//   remove UWTable   — kernel modules do not use .eh_frame
static void applyKernelFunctionAttrs(llvm::Module &M) {
  for (llvm::Function &F : M) {
    if (F.isDeclaration())
      continue;
    F.addFnAttr(llvm::Attribute::ShadowCallStack);
    F.addFnAttr("branch-target-enforcement", "true");
    F.addFnAttr("sign-return-address", "all");
    F.addFnAttr("sign-return-address-key", "a_key");
    F.removeFnAttr(llvm::Attribute::UWTable);
    F.setUWTableKind(llvm::UWTableKind::None);
  }
}

llvm::PreservedAnalyses
AndroidKernel::KernelFunctionAttrsPass::run(llvm::Module &M,
                                            llvm::ModuleAnalysisManager &) {
  applyKernelFunctionAttrs(M);
  return llvm::PreservedAnalyses::none();
}

void AndroidKernel::emitFixups(llvm::Module &M, unsigned Arch) {
  if (Arch != llvm::Triple::aarch64)
    return;

  emitPLTSections(M);
  emitEmptyVersionsSection(M);
  emitCFICheckStubs(M);
  emitKCFIEntryPrefixes(M);
}
