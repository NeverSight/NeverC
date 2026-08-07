#include "Core/AndroidKernelEmitter.h"
#include "neverc/Foundation/AndroidKernelProfileContract.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <cstdint>

using namespace neverc::Emit;

static_assert(neverc::AndroidKernelProfileContract::MaxKCFIMode ==
              static_cast<uint32_t>(AndroidKernel::KCFIMode::Normalized));

namespace {

constexpr llvm::StringLiteral KCFITypePairMetadata = "neverc.kcfi.typeids";
constexpr llvm::StringLiteral KCFIModeModuleFlag =
    "neverc.android.kernel.kcfi.mode";
constexpr llvm::StringLiteral ProfileModuleFlag =
    "neverc.android.kernel.profile";
constexpr llvm::StringLiteral KCFIModeSourceMarker =
    "__neverc_krt_kcfi_mode_marker";
constexpr llvm::StringLiteral ProfileSourceMarker =
    "__neverc_krt_profile_marker";
constexpr llvm::StringLiteral PCGOriginalLocalAttr =
    "neverc.pcg.original-local";
constexpr llvm::StringLiteral PCGOriginalAddressTakenAttr =
    "neverc.pcg.original-address-taken";

llvm::MDNode *makeKCFITypeNode(llvm::LLVMContext &Ctx, uint32_t TypeID) {
  auto *Value = llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), TypeID);
  return llvm::MDNode::get(Ctx, llvm::ConstantAsMetadata::get(Value));
}

uint32_t getKCFITypeOperand(const llvm::MDNode &Node, unsigned Index,
                            llvm::StringRef Description) {
  if (Node.getNumOperands() <= Index)
    llvm::report_fatal_error("invalid " + Description);
  const auto *Value =
      llvm::mdconst::dyn_extract<llvm::ConstantInt>(Node.getOperand(Index));
  if (!Value || Value->getBitWidth() != 32)
    llvm::report_fatal_error("invalid " + Description);
  return static_cast<uint32_t>(Value->getZExtValue());
}

bool getKCFITypePair(const llvm::Function &F, uint32_t &Standard,
                     uint32_t &Normalized) {
  llvm::SmallVector<llvm::MDNode *, 2> Pairs;
  F.getMetadata(KCFITypePairMetadata, Pairs);
  if (Pairs.empty())
    return false;

  Standard = getKCFITypeOperand(*Pairs.front(), 0,
                                "Android kernel KCFI standard type ID");
  Normalized = getKCFITypeOperand(*Pairs.front(), 1,
                                  "Android kernel KCFI normalized type ID");
  for (const llvm::MDNode *Pair : Pairs) {
    if (Pair->getNumOperands() != 2 ||
        getKCFITypeOperand(*Pair, 0, "Android kernel KCFI standard type ID") !=
            Standard ||
        getKCFITypeOperand(
            *Pair, 1, "Android kernel KCFI normalized type ID") != Normalized)
      llvm::report_fatal_error(
          "conflicting source-level Android kernel KCFI type IDs on " +
          F.getName());
  }
  return true;
}

std::optional<uint32_t> getSelectedKCFIType(const llvm::Function &F) {
  llvm::SmallVector<llvm::MDNode *, 2> Types;
  F.getMetadata(llvm::LLVMContext::MD_kcfi_type, Types);
  if (Types.empty())
    return std::nullopt;

  const uint32_t TypeID = getKCFITypeOperand(
      *Types.front(), 0, "selected Android kernel KCFI type ID");
  for (const llvm::MDNode *Type : Types) {
    if (Type->getNumOperands() != 1 ||
        getKCFITypeOperand(*Type, 0, "selected Android kernel KCFI type ID") !=
            TypeID)
      llvm::report_fatal_error(
          "conflicting selected Android kernel KCFI type ID on " + F.getName());
  }
  return TypeID;
}

void prepareKCFITypes(llvm::Module &M) {
  const std::optional<AndroidKernel::KCFIMode> ModuleMode =
      AndroidKernel::getKCFIMode(M);
  if (!ModuleMode)
    return;
  const AndroidKernel::KCFIMode Mode = *ModuleMode;
  for (llvm::Function &F : M) {
    // Profiles without KCFI must not validate dormant source carriers. This is
    // deliberately before parsing either attachment: a profile-neutral input
    // may contain IDs (or unsupported types) that only matter to KCFI-enabled
    // consumers.
    if (Mode == AndroidKernel::KCFIMode::Disabled) {
      F.setMetadata(llvm::LLVMContext::MD_kcfi_type, nullptr);
      F.setMetadata(KCFITypePairMetadata, nullptr);
      continue;
    }

    uint32_t Standard = 0;
    uint32_t Normalized = 0;
    const bool HasPair = getKCFITypePair(F, Standard, Normalized);
    const std::optional<uint32_t> Existing = getSelectedKCFIType(F);

    if (HasPair) {
      const uint32_t Selected =
          Mode == AndroidKernel::KCFIMode::Classic ? Standard : Normalized;
      if (Existing && *Existing != Selected)
        llvm::report_fatal_error(
            "conflicting selected Android kernel KCFI type ID on " +
            F.getName());
      // Also coalesce duplicate identical attachments introduced by an
      // in-memory IR link into the one attachment required by the verifier.
      F.setMetadata(llvm::LLVMContext::MD_kcfi_type,
                    makeKCFITypeNode(M.getContext(), Selected));
    } else if (Existing) {
      F.setMetadata(llvm::LLVMContext::MD_kcfi_type,
                    makeKCFITypeNode(M.getContext(), *Existing));
    }
    F.setMetadata(KCFITypePairMetadata, nullptr);

    // Match Clang's finalizeKCFITypes(): direct-only local functions do not
    // need a prefix because no indirect call can legally target them.
    // Parallel codegen promotes local functions to hidden external symbols and
    // partitions their uses.  Its private attributes retain the pre-split
    // facts, so a direct-only local remains exempt while an address-taken local
    // keeps its prefix even when the taking use lives in another partition.
    const bool IsLocal =
        F.hasLocalLinkage() || F.hasFnAttribute(PCGOriginalLocalAttr);
    const bool AddressTaken =
        F.hasAddressTaken() || F.hasFnAttribute(PCGOriginalAddressTakenAttr);
    if (!AddressTaken && IsLocal)
      F.eraseMetadata(llvm::LLVMContext::MD_kcfi_type);

    if (!F.isDeclaration() && !F.hasAvailableExternallyLinkage() &&
        (!IsLocal || AddressTaken) &&
        !F.getMetadata(llvm::LLVMContext::MD_kcfi_type))
      llvm::report_fatal_error(
          "Android kernel function lacks a source-level KCFI type ID: " +
          F.getName());
  }
}

std::optional<uint32_t> consumeSourceMarker(llvm::Module &M,
                                            llvm::StringRef MarkerName,
                                            llvm::StringRef Description) {
  llvm::GlobalVariable *Marker = M.getNamedGlobal(MarkerName);
  if (!Marker)
    return std::nullopt;
  const auto *Value =
      llvm::dyn_cast_or_null<llvm::ConstantInt>(Marker->getInitializer());
  if (!Value || Value->getBitWidth() > 32)
    llvm::report_fatal_error("invalid Android kernel source " + Description);
  if (!Marker->use_empty())
    llvm::report_fatal_error("referenced Android kernel source " + Description);
  const uint32_t Result = static_cast<uint32_t>(Value->getZExtValue());
  Marker->eraseFromParent();
  return Result;
}

void materializeNativeProfileContract(llvm::Module &M, uint32_t Profile,
                                      AndroidKernel::KCFIMode Mode) {
  namespace ProfileContract = neverc::AndroidKernelProfileContract;
  const uint64_t Serialized =
      ProfileContract::encode(Profile, static_cast<unsigned>(Mode));
  llvm::GlobalVariable *Record =
      M.getNamedGlobal(ProfileContract::NativeSymbol);
  if (Record) {
    const auto *Value =
        llvm::dyn_cast_or_null<llvm::ConstantInt>(Record->getInitializer());
    if (!Record->isConstant() || !Value || Value->getBitWidth() != 64 ||
        Value->getZExtValue() != Serialized ||
        Record->getSection() != ProfileContract::NativeSection)
      llvm::report_fatal_error(
          "conflicting native Android kernel profile contract");
  } else {
    llvm::LLVMContext &Ctx = M.getContext();
    llvm::Type *I64 = llvm::Type::getInt64Ty(Ctx);
    Record = new llvm::GlobalVariable(
        M, I64, true, llvm::GlobalValue::InternalLinkage,
        llvm::ConstantInt::get(I64, Serialized), ProfileContract::NativeSymbol);
  }

  // A provider may return a pre-existing record.  Normalize every emission
  // property instead of trusting a same-valued global that could otherwise be
  // available_externally, discarded, or omitted from the object.
  Record->setLinkage(llvm::GlobalValue::InternalLinkage);
  Record->setConstant(true);
  Record->setVisibility(llvm::GlobalValue::DefaultVisibility);
  Record->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  Record->setSection(ProfileContract::NativeSection);
  Record->setAlignment(llvm::Align(8));
  Record->setComdat(nullptr);

  llvm::SmallVector<llvm::GlobalValue *, 8> CompilerUsed;
  llvm::collectUsedGlobalVariables(M, CompilerUsed, true);
  bool IsCompilerUsed = false;
  for (const llvm::GlobalValue *Value : CompilerUsed)
    IsCompilerUsed |= Value == Record;
  if (!IsCompilerUsed)
    llvm::appendToCompilerUsed(M, {Record});
}

} // namespace

std::optional<AndroidKernel::KCFIMode>
AndroidKernel::getKCFIMode(const llvm::Module &M) {
  llvm::Metadata *Flag = M.getModuleFlag(KCFIModeModuleFlag);
  if (!Flag)
    return std::nullopt;
  const auto *Value = llvm::mdconst::dyn_extract<llvm::ConstantInt>(Flag);
  if (!Value || Value->getBitWidth() > 32 ||
      Value->getZExtValue() > static_cast<unsigned>(KCFIMode::Normalized))
    llvm::report_fatal_error("invalid Android kernel KCFI mode module flag");
  return static_cast<KCFIMode>(Value->getZExtValue());
}

std::optional<uint32_t> AndroidKernel::getProfile(const llvm::Module &M) {
  llvm::Metadata *Flag = M.getModuleFlag(ProfileModuleFlag);
  if (!Flag)
    return std::nullopt;
  const auto *Value = llvm::mdconst::dyn_extract<llvm::ConstantInt>(Flag);
  if (!Value || Value->getBitWidth() > 32 || Value->isZero())
    llvm::report_fatal_error("invalid Android kernel profile module flag");
  return static_cast<uint32_t>(Value->getZExtValue());
}

std::optional<AndroidKernel::Contract>
AndroidKernel::getContract(const llvm::Module &M) {
  const std::optional<KCFIMode> Mode = getKCFIMode(M);
  const std::optional<uint32_t> Profile = getProfile(M);
  if (!Mode || !Profile)
    return std::nullopt;
  return Contract{*Mode, *Profile};
}

void AndroidKernel::setKCFITypePair(llvm::Function &F, uint32_t Standard,
                                    uint32_t Normalized) {
  uint32_t ExistingStandard = 0;
  uint32_t ExistingNormalized = 0;
  if (getKCFITypePair(F, ExistingStandard, ExistingNormalized) &&
      (ExistingStandard != Standard || ExistingNormalized != Normalized))
    llvm::report_fatal_error(
        "conflicting source-level Android kernel KCFI type IDs on " +
        F.getName());

  llvm::LLVMContext &Ctx = F.getContext();
  llvm::Metadata *Values[] = {
      llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), Standard)),
      llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), Normalized)),
  };
  F.setMetadata(KCFITypePairMetadata, llvm::MDNode::get(Ctx, Values));
}

void AndroidKernel::setKCFIType(llvm::Function &F, uint32_t TypeID) {
  const std::optional<uint32_t> Existing = getSelectedKCFIType(F);
  if (Existing && *Existing != TypeID)
    llvm::report_fatal_error(
        "conflicting source-level Android kernel KCFI type IDs on " +
        F.getName());
  F.setMetadata(llvm::LLVMContext::MD_kcfi_type,
                makeKCFITypeNode(F.getContext(), TypeID));
}

void AndroidKernel::finalizeKCFIPrefixes(llvm::Module &M) {
  const std::optional<KCFIMode> Mode = getKCFIMode(M);
  if (!Mode)
    return;
  const std::optional<uint32_t> Profile = getProfile(M);
  if (!Profile)
    llvm::report_fatal_error(
        "Android kernel module is missing its native profile contract");
  materializeNativeProfileContract(M, *Profile, *Mode);

  // This is intentionally also the fail-safe selection point.  The normal
  // builtin pipeline prepares types earlier, but an IR optimization provider
  // may replace or pass through the module while suppressing that pipeline.
  prepareKCFITypes(M);
  if (*Mode == KCFIMode::Disabled)
    return;

  for (llvm::Function &F : M) {
    if (F.isDeclaration() || F.hasAvailableExternallyLinkage())
      continue;
    llvm::MDNode *Type = F.getMetadata(llvm::LLVMContext::MD_kcfi_type);
    if (!Type)
      continue;
    if (Type->getNumOperands() != 1)
      llvm::report_fatal_error(
          "invalid selected Android kernel KCFI type ID on " + F.getName());
    const uint32_t TypeID =
        getKCFITypeOperand(*Type, 0, "selected Android kernel KCFI type ID");
    auto *Prefix =
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(M.getContext()), TypeID);
    if (F.hasPrefixData()) {
      const auto *Existing =
          llvm::dyn_cast<llvm::ConstantInt>(F.getPrefixData());
      if (!Existing || Existing->getBitWidth() != 32 ||
          Existing->getZExtValue() != TypeID)
        llvm::report_fatal_error(
            "conflicting prefix data on Android kernel KCFI function " +
            F.getName());
      continue;
    }
    F.setPrefixData(Prefix);
  }
}

llvm::PreservedAnalyses
AndroidKernel::FinalizeKCFIPrefixesPass::run(llvm::Module &M,
                                             llvm::ModuleAnalysisManager &) {
  finalizeKCFIPrefixes(M);
  return llvm::PreservedAnalyses::none();
}

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
  // The source-equivalent stub type is void(void). Keep both profile hashes;
  // the runtime-link preparation pass selects the active one.
  AndroidKernel::setKCFITypePair(*F, 0xa540670cU, 0xe5c47d60U);
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
  }
}

llvm::PreservedAnalyses
AndroidKernel::KernelFunctionAttrsPass::run(llvm::Module &M,
                                            llvm::ModuleAnalysisManager &) {
  prepareKCFITypes(M);
  applyKernelFunctionAttrs(M);
  return llvm::PreservedAnalyses::none();
}

void AndroidKernel::emitFixups(llvm::Module &M, unsigned Arch, KCFIMode Mode) {
  if (Arch != llvm::Triple::aarch64)
    return;
  if (static_cast<unsigned>(Mode) >
      static_cast<unsigned>(KCFIMode::Unspecified))
    llvm::report_fatal_error("invalid Android kernel KCFI profile mode");

  const std::optional<uint32_t> SourceModeValue =
      consumeSourceMarker(M, KCFIModeSourceMarker, "KCFI mode marker");
  if (SourceModeValue) {
    if (*SourceModeValue > static_cast<unsigned>(KCFIMode::Normalized))
      llvm::report_fatal_error(
          "invalid Android kernel source KCFI mode marker");
    const KCFIMode SourceMode = static_cast<KCFIMode>(*SourceModeValue);
    if (isConcrete(Mode) && Mode != SourceMode)
      llvm::report_fatal_error(
          "conflicting command-line and source Android kernel KCFI modes");
    Mode = SourceMode;
  }
  if (!isConcrete(Mode))
    llvm::report_fatal_error(
        "Android kernel source is missing a KCFI mode marker");

  const std::optional<uint32_t> Profile =
      consumeSourceMarker(M, ProfileSourceMarker, "profile marker");
  if (!Profile)
    llvm::report_fatal_error(
        "Android kernel source is missing a profile marker");
  if (*Profile == 0)
    llvm::report_fatal_error("invalid Android kernel source profile marker");

  M.addModuleFlag(llvm::Module::Error, KCFIModeModuleFlag,
                  static_cast<unsigned>(Mode));
  M.addModuleFlag(llvm::Module::Error, ProfileModuleFlag, *Profile);

  emitPLTSections(M);
  emitEmptyVersionsSection(M);
  emitCFICheckStubs(M);
}
