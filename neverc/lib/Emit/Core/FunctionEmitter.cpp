#include "Core/FunctionEmitter.h"
#include "ABI/EmitterABI.h"
#include "ABI/TargetInfo.h"
#include "Core/ModuleEmitter.h"
#include "Debug/DebugEmitterInfo.h"
#include "Stmt/CleanupEmitterInfo.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Emit/ABI/ABIFunctionInfo.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/FPEnv.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Support/CRC.h"
#include <optional>

using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// Internal helpers
// ===----------------------------------------------------------------------===

namespace {

bool needsLifetimeMarkers(const CodeGenOptions &CGOpts,
                          const LangOptions &LangOpts,
                          const llvm::Triple &Triple) {
  if (Triple.isOSWindows() || Triple.isOSBinFormatCOFF()) {
    // Windows/COFF targets don't benefit from lifetime markers and they
    // can interfere with SEH — force them off.
    auto &MutableCGOpts = const_cast<CodeGenOptions &>(CGOpts);
    MutableCGOpts.DisableLifetimeMarkers = true;
    return false;
  }

  if (CGOpts.DisableLifetimeMarkers)
    return false;

  return CGOpts.OptimizationLevel != 0;
}

} // namespace

// ===----------------------------------------------------------------------===
// Construction & FP configuration
// ===----------------------------------------------------------------------===

FunctionEmitter::FunctionEmitter(ModuleEmitter &cgm, bool suppressNewContext)
    : TypeEmitterCache(cgm), ME(cgm), Target(cgm.getTarget()),
      Builder(cgm, cgm.getModule().getContext(), llvm::ConstantFolder(),
              CGBuilderInserterTy(this)),
      CurFPFeatures(ME.getLangOpts()), DebugInfo(ME.getModuleDebugInfo()),
      ShouldEmitLifetimeMarkers(needsLifetimeMarkers(
          ME.getCodeGenOpts(), ME.getLangOpts(), ME.getTarget().getTriple())) {
  if (!suppressNewContext)
    ME.getCGABI().getMangleContext().startNewFunction();
  EHStack.setFunctionEmitter(this);

  setFastMathFlags(CurFPFeatures);
}

FunctionEmitter::~FunctionEmitter() {
  assert(LifetimeExtendedCleanupStack.empty() && "failed to emit a cleanup");
}

llvm::fp::ExceptionBehavior
neverc::ToConstrainedExceptMD(LangOptions::FPExceptionModeKind Kind) {

  switch (Kind) {
  case LangOptions::FPE_Ignore:
    return llvm::fp::ebIgnore;
  case LangOptions::FPE_MayTrap:
    return llvm::fp::ebMayTrap;
  case LangOptions::FPE_Strict:
    return llvm::fp::ebStrict;
  default:
    llvm_unreachable("Unsupported FP Exception Behavior");
  }
}

void FunctionEmitter::setFastMathFlags(FPOptions FPFeatures) {
  llvm::FastMathFlags FMF;
  FMF.setAllowReassoc(FPFeatures.getAllowFPReassociate());
  FMF.setNoNaNs(FPFeatures.getNoHonorNaNs());
  FMF.setNoInfs(FPFeatures.getNoHonorInfs());
  FMF.setNoSignedZeros(FPFeatures.getNoSignedZero());
  FMF.setAllowReciprocal(FPFeatures.getAllowReciprocal());
  FMF.setApproxFunc(FPFeatures.getAllowApproxFunc());
  FMF.setAllowContract(FPFeatures.allowFPContractAcrossStatement());
  Builder.setFastMathFlags(FMF);
}

FunctionEmitter::FPOptionsRAII::FPOptionsRAII(FunctionEmitter &FE,
                                              const Expr *E)
    : FE(FE) {
  ConstructorHelper(E->getFPFeaturesInEffect(FE.getLangOpts()));
}

FunctionEmitter::FPOptionsRAII::FPOptionsRAII(FunctionEmitter &FE,
                                              FPOptions FPFeatures)
    : FE(FE) {
  ConstructorHelper(FPFeatures);
}

void FunctionEmitter::FPOptionsRAII::ConstructorHelper(FPOptions FPFeatures) {
  OldFPFeatures = FE.CurFPFeatures;
  FE.CurFPFeatures = FPFeatures;

  OldExcept = FE.Builder.getDefaultConstrainedExcept();
  OldRounding = FE.Builder.getDefaultConstrainedRounding();

  if (OldFPFeatures == FPFeatures)
    return;

  FMFGuard.emplace(FE.Builder);

  llvm::RoundingMode NewRoundingBehavior = FPFeatures.getRoundingMode();
  FE.Builder.setDefaultConstrainedRounding(NewRoundingBehavior);
  auto NewExceptionBehavior =
      ToConstrainedExceptMD(static_cast<LangOptions::FPExceptionModeKind>(
          FPFeatures.getExceptionMode()));
  FE.Builder.setDefaultConstrainedExcept(NewExceptionBehavior);

  FE.setFastMathFlags(FPFeatures);

  assert((FE.CurFuncDecl == nullptr || FE.Builder.getIsFPConstrained() ||
          (NewExceptionBehavior == llvm::fp::ebIgnore &&
           NewRoundingBehavior == llvm::RoundingMode::NearestTiesToEven)) &&
         "FPConstrained should be enabled on entire function");

  auto mergeFnAttrValue = [&](llvm::StringRef Name, bool Value) {
    auto OldValue = FE.CurFn->getFnAttribute(Name).getValueAsBool();
    auto NewValue = OldValue & Value;
    if (OldValue != NewValue)
      FE.CurFn->addFnAttr(Name, llvm::toStringRef(NewValue));
  };
  mergeFnAttrValue("no-infs-fp-math", FPFeatures.getNoHonorInfs());
  mergeFnAttrValue("no-nans-fp-math", FPFeatures.getNoHonorNaNs());
  mergeFnAttrValue("no-signed-zeros-fp-math", FPFeatures.getNoSignedZero());
  mergeFnAttrValue(
      "unsafe-fp-math",
      FPFeatures.getAllowFPReassociate() && FPFeatures.getAllowReciprocal() &&
          FPFeatures.getAllowApproxFunc() && FPFeatures.getNoSignedZero() &&
          FPFeatures.allowFPContractAcrossStatement());
}

FunctionEmitter::FPOptionsRAII::~FPOptionsRAII() {
  FE.CurFPFeatures = OldFPFeatures;
  FE.Builder.setDefaultConstrainedExcept(OldExcept);
  FE.Builder.setDefaultConstrainedRounding(OldRounding);
}

// ===----------------------------------------------------------------------===
// Type conversion utilities
// ===----------------------------------------------------------------------===

LValue FunctionEmitter::makeNaturalAlignAddrLValue(llvm::Value *V, QualType T) {
  LValueBaseInfo BaseInfo;
  TBAAAccessInfo TBAAInfo;
  CharUnits Alignment = ME.getNaturalTypeAlignment(T, &BaseInfo, &TBAAInfo);
  Address Addr(V, convertTypeForMem(T), Alignment);
  return LValue::MakeAddr(Addr, T, getContext(), BaseInfo, TBAAInfo);
}

LValue FunctionEmitter::makeNaturalAlignPointeeAddrLValue(llvm::Value *V,
                                                          QualType T) {
  LValueBaseInfo BaseInfo;
  TBAAAccessInfo TBAAInfo;
  CharUnits Align = ME.getNaturalTypeAlignment(T, &BaseInfo, &TBAAInfo,
                                               /* forPointeeType= */ true);
  Address Addr(V, convertTypeForMem(T), Align);
  return makeAddrLValue(Addr, T, BaseInfo, TBAAInfo);
}

llvm::Type *FunctionEmitter::convertTypeForMem(QualType T) {
  return ME.getTypes().convertTypeForMem(T);
}

llvm::Type *FunctionEmitter::convertType(QualType T) {
  return ME.getTypes().convertType(T);
}

TypeEvaluationKind FunctionEmitter::getEvaluationKind(QualType type) {
  type = type.getCanonicalType();
  while (true) {
    switch (type->getTypeClass()) {
#define TYPE(name, parent)
#define ABSTRACT_TYPE(name, parent)
#define NON_CANONICAL_TYPE(name, parent) case Type::name:
#define DEPENDENT_TYPE(name, parent) case Type::name:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(name, parent) case Type::name:
#include "neverc/Tree/TypeNodes.td.h"
      llvm_unreachable("non-canonical or dependent type in IR-generation");

    case Type::Auto:
      llvm_unreachable("undeduced type in IR-generation");

    // Various scalar types.
    case Type::Builtin:
    case Type::Pointer:
    case Type::Vector:
    case Type::ExtVector:
    case Type::ConstantMatrix:
    case Type::FunctionProto:
    case Type::FunctionNoProto:
    case Type::Enum:
    case Type::BitInt:
      return TEK_Scalar;

    // Complexes.
    case Type::Complex:
      return TEK_Complex;

    // Arrays and records.
    case Type::ConstantArray:
    case Type::IncompleteArray:
    case Type::VariableArray:
    case Type::Record:
      return TEK_Aggregate;

    // We operate on atomic values according to their underlying type.
    case Type::Atomic:
      type = cast<AtomicType>(type)->getValueType();
      continue;
    }
    llvm_unreachable("unknown type kind!");
  }
}

// ===----------------------------------------------------------------------===
// Function lifecycle (Start / Generate / Finish)
// ===----------------------------------------------------------------------===

llvm::DebugLoc FunctionEmitter::genReturnBlock() {
  llvm::BasicBlock *CurBB = Builder.GetInsertBlock();

  if (CurBB) {
    assert(!CurBB->getTerminator() && "Unexpected terminated block.");

    if (CurBB->empty() || ReturnBlock.getBlock()->use_empty()) {
      ReturnBlock.getBlock()->replaceAllUsesWith(CurBB);
      delete ReturnBlock.getBlock();
      ReturnBlock = JumpDest();
    } else
      genBlock(ReturnBlock.getBlock());
    return llvm::DebugLoc();
  }

  if (ReturnBlock.getBlock()->hasOneUse()) {
    llvm::BranchInst *BI =
        dyn_cast<llvm::BranchInst>(*ReturnBlock.getBlock()->user_begin());
    if (BI && BI->isUnconditional() &&
        BI->getSuccessor(0) == ReturnBlock.getBlock()) {
      llvm::DebugLoc Loc = BI->getDebugLoc();
      Builder.SetInsertPoint(BI->getParent());
      BI->eraseFromParent();
      delete ReturnBlock.getBlock();
      ReturnBlock = JumpDest();
      return Loc;
    }
  }

  genBlock(ReturnBlock.getBlock());
  return llvm::DebugLoc();
}

namespace {

void emitPendingBlock(FunctionEmitter &FE, llvm::BasicBlock *BB) {
  if (!BB)
    return;
  if (!BB->use_empty()) {
    FE.CurFn->insert(FE.CurFn->end(), BB);
    return;
  }
  delete BB;
}

} // namespace

__attribute__((hot)) void
FunctionEmitter::finishFunction(SourceLocation EndLoc) {
  assert(BreakContinueStack.empty() &&
         "mismatched push/pop in break/continue stack!");

  bool OnlySimpleReturnStmts = NumSimpleReturnExprs > 0 &&
                               NumSimpleReturnExprs == NumReturnExprs &&
                               ReturnBlock.getBlock()->use_empty();

  if (DebugEmitter *DI = getDebugInfo()) {
    if (OnlySimpleReturnStmts)
      DI->genLocation(Builder, LastStopPoint);
    else
      DI->genLocation(Builder, EndLoc);
  }

  // Pop any cleanups that might have been associated with the
  // parameters.  Do this in whatever block we're currently in; it's
  // important to do this before we enter the return block or return
  // edges will be *really* confused.
  bool HasCleanups = EHStack.stable_begin() != PrologueCleanupDepth;
  bool HasOnlyLifetimeMarkers =
      HasCleanups && EHStack.containsOnlyLifetimeMarkers(PrologueCleanupDepth);
  bool genRetDbgLoc = !HasCleanups || HasOnlyLifetimeMarkers;

  std::optional<ApplyDebugLocation> OAL;
  if (HasCleanups) {
    // Make sure the line table doesn't jump back into the body for
    // the ret after it's been at EndLoc.
    if (DebugEmitter *DI = getDebugInfo()) {
      if (OnlySimpleReturnStmts)
        DI->genLocation(Builder, EndLoc);
      else
        // We may not have a valid end location. Try to apply it anyway, and
        // fall back to an artificial location if needed.
        OAL = ApplyDebugLocation::CreateDefaultArtificial(*this, EndLoc);
    }

    popCleanupBlocks(PrologueCleanupDepth);
  }

  llvm::DebugLoc Loc = genReturnBlock();

  if (DebugEmitter *DI = getDebugInfo())
    DI->genFunctionEnd(Builder, CurFn);

  if (CurCodeDecl && CurCodeDecl->hasAttr<VolatileAttr>())
    CurFn->setVolatileAndAppendToUsed();

  // Reset the debug location to that of the simple 'return' expression, if any
  // rather than that of the end of the function's scope '}'.
  ApplyDebugLocation AL(*this, Loc);
  genFunctionEpilog(*CurFnInfo, genRetDbgLoc, EndLoc);

  assert(EHStack.empty() && "did not remove all scopes from cleanup stack!");

  // If someone did an indirect goto, emit the indirect goto block at the end of
  // the function.
  if (IndirectBranch) {
    genBlock(IndirectBranch->getParent());
    Builder.ClearInsertionPoint();
  }

  // If some of our locals escaped, insert a call to llvm.localescape in the
  // entry block.
  if (!EscapedLocals.empty()) {
    // Invert the map from local to index into a simple vector. There should be
    // no holes.
    llvm::SmallVector<llvm::Value *, 4> EscapeArgs;
    EscapeArgs.resize(EscapedLocals.size());
    for (auto &Pair : EscapedLocals)
      EscapeArgs[Pair.second] = Pair.first;
    llvm::Function *FrameEscapeFn = llvm::Intrinsic::getDeclaration(
        &ME.getModule(), llvm::Intrinsic::localescape);
    CGBuilderTy(*this, AllocaInsertPt).CreateCall(FrameEscapeFn, EscapeArgs);
  }

  // Remove the AllocaInsertPt instruction, which is just a convenience for us.
  llvm::Instruction *Ptr = AllocaInsertPt;
  AllocaInsertPt = nullptr;
  Ptr->eraseFromParent();

  if (PostAllocaInsertPt) {
    llvm::Instruction *PostPtr = PostAllocaInsertPt;
    PostAllocaInsertPt = nullptr;
    PostPtr->eraseFromParent();
  }

  // If someone took the address of a label but never did an indirect goto, we
  // made a zero entry PHI node, which is illegal, zap it now.
  if (IndirectBranch) {
    llvm::PHINode *PN = cast<llvm::PHINode>(IndirectBranch->getAddress());
    if (PN->getNumIncomingValues() == 0) {
      PN->replaceAllUsesWith(llvm::UndefValue::get(PN->getType()));
      PN->eraseFromParent();
    }
  }

  emitPendingBlock(*this, EHResumeBlock);
  emitPendingBlock(*this, TerminateLandingPad);
  emitPendingBlock(*this, TerminateHandler);
  emitPendingBlock(*this, UnreachableBlock);

  for (const auto &FuncletAndParent : TerminateFunclets)
    emitPendingBlock(*this, FuncletAndParent.second);

  for (const auto &R : DeferredReplacements) {
    if (llvm::Value *Old = R.first) {
      Old->replaceAllUsesWith(R.second);
      cast<llvm::Instruction>(Old)->eraseFromParent();
    }
  }
  DeferredReplacements.clear();

  for (llvm::Argument &A : CurFn->args())
    if (auto *VT = dyn_cast<llvm::VectorType>(A.getType()))
      LargestVectorWidth =
          std::max((uint64_t)LargestVectorWidth,
                   VT->getPrimitiveSizeInBits().getKnownMinValue());

  if (auto *VT = dyn_cast<llvm::VectorType>(CurFn->getReturnType()))
    LargestVectorWidth =
        std::max((uint64_t)LargestVectorWidth,
                 VT->getPrimitiveSizeInBits().getKnownMinValue());

  if (CurFnInfo->getMaxVectorWidth() > LargestVectorWidth)
    LargestVectorWidth = CurFnInfo->getMaxVectorWidth();

  // Add the min-legal-vector-width attribute. This contains the max width from:
  // 1. min-vector-width attribute used in the source program.
  // 2. Any builtins used that have a vector width specified.
  // 3. Values passed in and out of inline assembly.
  // 4. Width of vector arguments and return types for this function.
  // 5. Width of vector arguments and return types for functions called by this
  //    function.
  if (getContext().getTargetInfo().getTriple().isX86())
    CurFn->addFnAttr("min-legal-vector-width",
                     llvm::utostr(LargestVectorWidth));

  std::optional<std::pair<unsigned, unsigned>> VScaleRange =
      getContext().getTargetInfo().getVScaleRange(getLangOpts());
  if (VScaleRange) {
    CurFn->addFnAttr(llvm::Attribute::getWithVScaleRangeArgs(
        getLLVMContext(), VScaleRange->first, VScaleRange->second));
  }

  // If we generated an unreachable return block, delete it now.
  if (ReturnBlock.isValid() && ReturnBlock.getBlock()->use_empty()) {
    Builder.ClearInsertionPoint();
    ReturnBlock.getBlock()->eraseFromParent();
  }
  if (ReturnValue.isValid()) {
    auto *RetAlloca = dyn_cast<llvm::AllocaInst>(ReturnValue.getPointer());
    if (RetAlloca && RetAlloca->use_empty()) {
      RetAlloca->eraseFromParent();
      ReturnValue = Address::invalid();
    }
  }
}

namespace {

bool hasReturnTerminator(const Decl *F) {
  const Stmt *Body = nullptr;
  if (auto *FD = dyn_cast_or_null<FunctionDecl>(F))
    Body = FD->getBody();

  if (auto *CS = dyn_cast_or_null<CompoundStmt>(Body)) {
    auto LastStmt = CS->body_rbegin();
    if (LastStmt != CS->body_rend())
      return isa<ReturnStmt>(*LastStmt);
  }
  return false;
}

} // namespace

__attribute__((hot)) void FunctionEmitter::startFunction(
    GlobalDecl GD, QualType RetTy, llvm::Function *Fn,
    const ABIFunctionInfo &FnInfo, const FunctionArgList &Args,
    SourceLocation Loc, SourceLocation StartLoc) {
  assert(!CurFn &&
         "Do not use a FunctionEmitter object for more than one function");

  const Decl *D = GD.getDecl();

  DidCallStackSave = false;
  CurCodeDecl = D;
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (FD && FD->usesSEHTry())
    CurSEHParent = GD;
  CurFuncDecl = (D ? dyn_cast<FunctionDecl>(D) : nullptr);
  FnRetTy = RetTy;
  CurFn = Fn;
  CurFnInfo = &FnInfo;
  assert(CurFn->isDeclaration() && "Function already has body?");

  unsigned Count, Offset;
  if (const auto *Attr =
          D ? D->getAttr<PatchableFunctionEntryAttr>() : nullptr) {
    Count = Attr->getCount();
    Offset = Attr->getOffset();
  } else {
    Count = ME.getCodeGenOpts().PatchableFunctionEntryCount;
    Offset = ME.getCodeGenOpts().PatchableFunctionEntryOffset;
  }
  if (Count && Offset <= Count) {
    Fn->addFnAttr("patchable-function-entry", std::to_string(Count - Offset));
    if (Offset)
      Fn->addFnAttr("patchable-function-prefix", std::to_string(Offset));
  }
  // MSVC /hotpatch-style patchable entry (LLVM "patchable-function"), Windows
  // x86_64 PE/COFF.
  if (ME.getCodeGenOpts().HotPatch &&
      getContext().getTargetInfo().getTriple().isX86() &&
      getContext().getTargetInfo().getTriple().getEnvironment() !=
          llvm::Triple::CODE16)
    Fn->addFnAttr("patchable-function", "prologue-short-redirect");

  if (ME.getCodeGenOpts().NoUseJumpTables)
    Fn->addFnAttr("no-jump-tables", "true");

  if (ME.getCodeGenOpts().NoInlineLineTables)
    Fn->addFnAttr("no-inline-line-tables");

  if (D) {
    if (auto *A = D->getAttr<FunctionReturnThunksAttr>()) {
      switch (A->getThunkType()) {
      case FunctionReturnThunksAttr::Kind::Keep:
        break;
      case FunctionReturnThunksAttr::Kind::Extern:
        Fn->addFnAttr(llvm::Attribute::FnRetThunkExtern);
        break;
      }
    } else if (ME.getCodeGenOpts().FunctionReturnThunks)
      Fn->addFnAttr(llvm::Attribute::FnRetThunkExtern);
  }

  llvm::RoundingMode RM = getLangOpts().getDefaultRoundingMode();
  llvm::fp::ExceptionBehavior FPExceptionBehavior =
      ToConstrainedExceptMD(getLangOpts().getDefaultExceptionMode());
  Builder.setDefaultConstrainedRounding(RM);
  Builder.setDefaultConstrainedExcept(FPExceptionBehavior);
  if ((FD && (FD->UsesFPIntrin() || FD->hasAttr<StrictFPAttr>())) ||
      (!FD && (FPExceptionBehavior != llvm::fp::ebIgnore ||
               RM != llvm::RoundingMode::NearestTiesToEven))) {
    Builder.setIsFPConstrained(true);
    Fn->addFnAttr(llvm::Attribute::StrictFP);
  }

  // If a custom alignment is used, force realigning to this alignment on
  // any main function which certainly will need it.
  if (FD && ((FD->isMain() || FD->isMSVCRTEntryPoint()) &&
             ME.getCodeGenOpts().StackAlignment))
    Fn->addFnAttr("stackrealign");

  // "main" doesn't need to zero out call-used registers.
  if (FD && FD->isMain())
    Fn->removeFnAttr("zero-call-used-regs");

  llvm::BasicBlock *EntryBB = createBasicBlock("entry", CurFn);

  // Use raw `new` instead of Builder so the marker isn't folded.
  llvm::Value *Undef = llvm::UndefValue::get(Int32Ty);
  AllocaInsertPt = new llvm::BitCastInst(Undef, Int32Ty, "allocapt", EntryBB);

  ReturnBlock = getJumpDestInCurrentScope("return");

  Builder.SetInsertPoint(EntryBB);

  if (DebugEmitter *DI = getDebugInfo()) {
    // Reconstruct the type from the argument list so that implicit parameters,
    // such as 'this' and 'vtt', show up in the debug info. Preserve the calling
    // convention.
    DI->emitFunctionStart(GD, Loc, StartLoc,
                          DI->getFunctionType(FD, RetTy, Args), CurFn);
  }

  if (ME.getCodeGenOpts().WarnStackSize != UINT_MAX &&
      !ME.getDiags().isIgnored(diag::warn_fe_backend_frame_larger_than, Loc))
    Fn->addFnAttr("warn-stack-size",
                  std::to_string(ME.getCodeGenOpts().WarnStackSize));

  if (RetTy->isVoidType()) {
    // Void type; nothing to return.
    ReturnValue = Address::invalid();

    if (!hasReturnTerminator(D))
      ++NumReturnExprs;
  } else if (CurFnInfo->getReturnInfo().getKind() == ABIArgInfo::Indirect) {
    // Indirect return; emit returned value directly into sret slot.
    // This reduces code size and matches the indirect-return ABI.
    auto AI = CurFn->arg_begin();
    if (CurFnInfo->getReturnInfo().isSRetAfterThis())
      ++AI;
    ReturnValue =
        Address(&*AI, convertType(RetTy),
                CurFnInfo->getReturnInfo().getIndirectAlign(), KnownNonNull);
    if (!CurFnInfo->getReturnInfo().getIndirectByVal()) {
      ReturnValuePointer = createDefaultAlignTempAlloca(
          ReturnValue.getPointer()->getType(), "result.ptr");
      Builder.CreateStore(ReturnValue.getPointer(), ReturnValuePointer);
    }
  } else {
    ReturnValue = createIRTemp(RetTy, "retval");
  }

  PrologueCleanupDepth = EHStack.stable_begin();

  genFunctionProlog(*CurFnInfo, CurFn, Args);

  // If any of the arguments have a variably modified type, make sure to
  // emit the type size, but only if the function is not naked. Naked functions
  // have no prolog to run this evaluation.
  if (!FD || !FD->hasAttr<NakedAttr>()) {
    for (const VarDecl *VD : Args) {
      // Use the original type from ParmVarDecl (matches GCC behavior).
      QualType Ty;
      if (const ParmVarDecl *PVD = dyn_cast<ParmVarDecl>(VD))
        Ty = PVD->getOriginalType();
      else
        Ty = VD->getType();

      if (Ty->isVariablyModifiedType())
        genVariablyModifiedType(Ty);
    }
  }
  if (DebugEmitter *DI = getDebugInfo())
    DI->genLocation(Builder, StartLoc);
  if (CurFuncDecl)
    if (const auto *VecWidth = CurFuncDecl->getAttr<MinVectorWidthAttr>())
      LargestVectorWidth = VecWidth->getVectorWidth();
}

__attribute__((hot)) void FunctionEmitter::genFunctionBody(const Stmt *Body) {
  if (const CompoundStmt *S = dyn_cast<CompoundStmt>(Body))
    genCompoundStmtWithoutScope(*S);
  else
    genStmt(Body);
}

namespace {

/// Conservatively mark a non-interposable function as nounwind if no
/// instruction in it may throw.
void markNoThrowIfSafe(llvm::Function *F) {
  if (F->isInterposable())
    return;

  for (llvm::BasicBlock &BB : *F)
    for (llvm::Instruction &I : BB)
      if (I.mayThrow())
        return;

  F->setDoesNotThrow();
}

} // namespace

QualType FunctionEmitter::formFunctionArgList(GlobalDecl GD,
                                              FunctionArgList &Args) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  QualType ResTy = FD->getReturnType();

  {
    for (auto *Param : FD->parameters()) {
      Args.push_back(Param);
      if (!Param->hasAttr<PassObjectSizeAttr>())
        continue;

      auto *Implicit = ImplicitParamDecl::Create(
          getContext(), Param->getDeclContext(), Param->getLocation(),
          /*Id=*/nullptr, getContext().getSizeType());
      SizeArguments[Param] = Implicit;
      Args.push_back(Implicit);
    }
  }

  return ResTy;
}

__attribute__((hot)) void
FunctionEmitter::generateCode(GlobalDecl GD, llvm::Function *Fn,
                              const ABIFunctionInfo &FnInfo) {
  assert(Fn && "generating code for null Function");
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  CurGD = GD;

  FunctionArgList Args;
  QualType ResTy = formFunctionArgList(GD, Args);

  if (FD->isInlineBuiltinDeclaration()) {
    // When generating code for a builtin with an inline declaration, use a
    // mangled name to hold the actual body, while keeping an external
    // definition in case the function pointer is referenced somewhere.
    std::string FDInlineName = (Fn->getName() + ".inline").str();
    llvm::Module *M = Fn->getParent();
    llvm::Function *Clone = M->getFunction(FDInlineName);
    if (!Clone) {
      Clone = llvm::Function::Create(Fn->getFunctionType(),
                                     llvm::GlobalValue::InternalLinkage,
                                     Fn->getAddressSpace(), FDInlineName, M);
      Clone->addFnAttr(llvm::Attribute::AlwaysInline);
    }
    Fn->setLinkage(llvm::GlobalValue::ExternalLinkage);
    Fn = Clone;
  } else {
    // Detect the unusual situation where an inline version is shadowed by a
    // non-inline version. In that case we should pick the external one
    // everywhere. That's GCC behavior too. Unfortunately, I cannot find a way
    // to detect that situation before we reach codegen, so do some late
    // replacement.
    for (const FunctionDecl *PD = FD->getPreviousDecl(); PD;
         PD = PD->getPreviousDecl()) {
      if (LLVM_UNLIKELY(PD->isInlineBuiltinDeclaration())) {
        std::string FDInlineName = (Fn->getName() + ".inline").str();
        llvm::Module *M = Fn->getParent();
        if (llvm::Function *Clone = M->getFunction(FDInlineName)) {
          Clone->replaceAllUsesWith(Fn);
          Clone->eraseFromParent();
        }
        break;
      }
    }
  }

  if (FD->hasAttr<NoDebugAttr>()) {
    // Clear non-distinct debug info that was possibly attached to the function
    // due to an earlier declaration without the nodebug attribute
    Fn->setSubprogram(nullptr);
    // Disable debug info indefinitely for this function
    DebugInfo = nullptr;
  }

  // The function might not have a body if we're generating thunks for a
  // function declaration.
  SourceRange BodyRange;
  if (Stmt *Body = FD->getBody())
    BodyRange = Body->getSourceRange();
  else
    BodyRange = FD->getLocation();
  CurEHLocation = BodyRange.getEnd();

  SourceLocation Loc = FD->getLocation();

  Stmt *Body = FD->getBody();

  if (Body) {
    if (ShouldEmitLifetimeMarkers)
      Bypasses.Init(Body);
  }

  startFunction(GD, ResTy, Fn, FnInfo, Args, Loc, BodyRange.getBegin());

  // Ensure that the function adheres to the forward progress guarantee, which
  // is required by certain optimizations.
  if (checkIfFunctionMustProgress())
    CurFn->addFnAttr(llvm::Attribute::MustProgress);

  if (Body) {
    genFunctionBody(Body);
  } else
    llvm_unreachable("no definition for emitted function");

  finishFunction(BodyRange.getEnd());

  // If we haven't marked the function nothrow through other means, do
  // a quick pass now to see if we can.  Skip at -O0 since the attribute
  // won't drive any optimization and the full-function scan is expensive.
  if (ME.getCodeGenOpts().OptimizationLevel != 0 && !CurFn->doesNotThrow())
    markNoThrowIfSafe(CurFn);
}

// ===----------------------------------------------------------------------===
// Control flow analysis & branching
// ===----------------------------------------------------------------------===

bool FunctionEmitter::containsLabel(const Stmt *S, bool IgnoreCaseStmts) {
  // Null statement, not a label!
  if (!S)
    return false;

  // If this is a label, we have to emit the code, consider something like:
  // if (0) {  ...  foo:  bar(); }  goto foo;
  //
  if (isa<LabelStmt>(S))
    return true;

  // If this is a case/default statement, and we haven't seen a switch, we have
  // to emit the code.
  if (isa<SwitchCase>(S) && !IgnoreCaseStmts)
    return true;

  // If this is a switch statement, we want to ignore cases below it.
  if (isa<SwitchStmt>(S))
    IgnoreCaseStmts = true;

  // Scan subexpressions for verboten labels.
  for (const Stmt *SubStmt : S->children())
    if (containsLabel(SubStmt, IgnoreCaseStmts))
      return true;

  return false;
}

bool FunctionEmitter::containsBreak(const Stmt *S) {
  // Null statement, not a label!
  if (!S)
    return false;

  // If this is a switch or loop that defines its own break scope, then we can
  // include it and anything inside of it.
  if (isa<SwitchStmt>(S) || isa<WhileStmt>(S) || isa<DoStmt>(S) ||
      isa<ForStmt>(S))
    return false;

  if (isa<BreakStmt>(S))
    return true;

  // Scan subexpressions for verboten breaks.
  for (const Stmt *SubStmt : S->children())
    if (containsBreak(SubStmt))
      return true;

  return false;
}

bool FunctionEmitter::mightAddDeclToScope(const Stmt *S) {
  if (!S)
    return false;

  // Some statement kinds add a scope and thus never add a decl to the current
  // scope. Note, this list is longer than the list of statements that might
  // have an unscoped decl nested within them, but this way is conservatively
  // correct even if more statement kinds are added.
  if (isa<IfStmt>(S) || isa<SwitchStmt>(S) || isa<WhileStmt>(S) ||
      isa<DoStmt>(S) || isa<ForStmt>(S) || isa<CompoundStmt>(S))
    return false;

  if (isa<DeclStmt>(S))
    return true;

  for (const Stmt *SubStmt : S->children())
    if (mightAddDeclToScope(SubStmt))
      return true;

  return false;
}

bool FunctionEmitter::constantFoldsToSimpleInteger(const Expr *Cond,
                                                   bool &ResultBool,
                                                   bool AllowLabels) {
  llvm::APSInt ResultInt;
  if (!constantFoldsToSimpleInteger(Cond, ResultInt, AllowLabels))
    return false;

  ResultBool = ResultInt.getBoolValue();
  return true;
}

bool FunctionEmitter::constantFoldsToSimpleInteger(const Expr *Cond,
                                                   llvm::APSInt &ResultInt,
                                                   bool AllowLabels) {
  Expr::EvalResult Result;
  if (!Cond->EvaluateAsInt(Result, getContext()))
    return false; // Not foldable, not integer or not fully evaluatable.

  llvm::APSInt Int = Result.Val.getInt();
  if (!AllowLabels && FunctionEmitter::containsLabel(Cond))
    return false; // Contains a label.

  ResultInt = Int;
  return true;
}

__attribute__((hot)) void FunctionEmitter::genBranchOnBoolExpr(
    const Expr *Cond, llvm::BasicBlock *TrueBlock, llvm::BasicBlock *FalseBlock,
    Stmt::Likelihood LH) {
  Cond = Cond->IgnoreParens();

  if (const BinaryOperator *CondBOp = dyn_cast<BinaryOperator>(Cond)) {

    // Handle X && Y in a condition.
    if (CondBOp->getOpcode() == BO_LAnd) {
      // If we have "1 && X", simplify the code.  "0 && X" would have constant
      // folded if the case was simple enough.
      bool ConstantBool = false;
      if (constantFoldsToSimpleInteger(CondBOp->getLHS(), ConstantBool) &&
          ConstantBool) {
        // br(1 && X) -> br(X).
        return genBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock,
                                   LH);
      }

      // If we have "X && 1", simplify the code to use an uncond branch.
      // "X && 0" would have been constant folded to 0.
      if (constantFoldsToSimpleInteger(CondBOp->getRHS(), ConstantBool) &&
          ConstantBool) {
        // br(X && 1) -> br(X).
        return genBranchOnBoolExpr(CondBOp->getLHS(), TrueBlock, FalseBlock,
                                   LH);
      }

      // If LHS is false, short-circuit to FalseBlock.
      llvm::BasicBlock *LHSTrue = createBasicBlock("land.lhs.true");

      ConditionalEvaluation eval(*this);
      {
        ApplyDebugLocation DL(*this, Cond);
        // Propagate the likelihood attribute like __builtin_expect
        // __builtin_expect(X && Y, 1) -> X and Y are likely
        // __builtin_expect(X && Y, 0) -> only Y is unlikely
        genBranchOnBoolExpr(CondBOp->getLHS(), LHSTrue, FalseBlock,
                            LH == Stmt::LH_Unlikely ? Stmt::LH_None : LH);
        genBlock(LHSTrue);
      }

      // Any temporaries created here are conditional.
      eval.begin(*this);
      genBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock, LH);
      eval.end(*this);

      return;
    }

    if (CondBOp->getOpcode() == BO_LOr) {
      // If we have "0 || X", simplify the code.  "1 || X" would have constant
      // folded if the case was simple enough.
      bool ConstantBool = false;
      if (constantFoldsToSimpleInteger(CondBOp->getLHS(), ConstantBool) &&
          !ConstantBool) {
        // br(0 || X) -> br(X).
        return genBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock,
                                   LH);
      }

      // If we have "X || 0", simplify the code to use an uncond branch.
      // "X || 1" would have been constant folded to 1.
      if (constantFoldsToSimpleInteger(CondBOp->getRHS(), ConstantBool) &&
          !ConstantBool) {
        // br(X || 0) -> br(X).
        return genBranchOnBoolExpr(CondBOp->getLHS(), TrueBlock, FalseBlock,
                                   LH);
      }

      // If LHS is true, short-circuit to TrueBlock.
      llvm::BasicBlock *LHSFalse = createBasicBlock("lor.lhs.false");

      ConditionalEvaluation eval(*this);
      {
        // Propagate the likelihood attribute like __builtin_expect
        // __builtin_expect(X || Y, 1) -> only Y is likely
        // __builtin_expect(X || Y, 0) -> both X and Y are unlikely
        ApplyDebugLocation DL(*this, Cond);
        genBranchOnBoolExpr(CondBOp->getLHS(), TrueBlock, LHSFalse,
                            LH == Stmt::LH_Likely ? Stmt::LH_None : LH);
        genBlock(LHSFalse);
      }

      // Any temporaries created here are conditional.
      eval.begin(*this);
      genBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock, LH);
      eval.end(*this);

      return;
    }
  }

  if (const UnaryOperator *CondUOp = dyn_cast<UnaryOperator>(Cond)) {
    // br(!x, t, f) -> br(x, f, t)
    if (CondUOp->getOpcode() == UO_LNot) {
      // The values of the enum are chosen to make this negation possible.
      LH = static_cast<Stmt::Likelihood>(-LH);
      // Negate the condition and swap the destination blocks.
      return genBranchOnBoolExpr(CondUOp->getSubExpr(), FalseBlock, TrueBlock,
                                 LH);
    }
  }

  if (const ConditionalOperator *CondOp = dyn_cast<ConditionalOperator>(Cond)) {
    // br(c ? x : y, t, f) -> br(c, br(x, t, f), br(y, t, f))
    llvm::BasicBlock *LHSBlock = createBasicBlock("cond.true");
    llvm::BasicBlock *RHSBlock = createBasicBlock("cond.false");

    // The ConditionalOperator itself has no likelihood information for its
    // true and false branches. This matches the behavior of __builtin_expect.
    ConditionalEvaluation cond(*this);
    genBranchOnBoolExpr(CondOp->getCond(), LHSBlock, RHSBlock, Stmt::LH_None);

    cond.begin(*this);
    genBlock(LHSBlock);
    {
      ApplyDebugLocation DL(*this, Cond);
      genBranchOnBoolExpr(CondOp->getLHS(), TrueBlock, FalseBlock, LH);
    }
    cond.end(*this);

    cond.begin(*this);
    genBlock(RHSBlock);
    genBranchOnBoolExpr(CondOp->getRHS(), TrueBlock, FalseBlock, LH);
    cond.end(*this);

    return;
  }

  llvm::Value *CondV;
  {
    ApplyDebugLocation DL(*this, Cond);
    CondV = evaluateExprAsBool(Cond);
  }

  llvm::MDNode *Unpredictable = nullptr;

  // If the branch has a condition wrapped by __builtin_unpredictable,
  // create metadata that specifies that the branch is unpredictable.
  // Don't bother if not optimizing because that metadata would not be used.
  auto *Call = dyn_cast<CallExpr>(Cond->IgnoreImpCasts());
  if (Call && ME.getCodeGenOpts().OptimizationLevel != 0) {
    auto *FD = dyn_cast_or_null<FunctionDecl>(Call->getCalleeDecl());
    if (FD && FD->getBuiltinID() == Builtin::BI__builtin_unpredictable) {
      llvm::MDBuilder MDHelper(getLLVMContext());
      Unpredictable = MDHelper.createUnpredictable();
    }
  }

  // If there is a Likelihood knowledge for the cond, lower it via
  // __builtin_expect intrinsic. Note that this only emits anything when
  // optimizing.
  CondV = emitCondLikelihoodViaExpectIntrinsic(CondV, LH);

  Builder.CreateCondBr(CondV, TrueBlock, FalseBlock, /*Weights=*/nullptr,
                       Unpredictable);
}

// ===----------------------------------------------------------------------===
// Memory initialization & VLA
// ===----------------------------------------------------------------------===

void FunctionEmitter::errorUnsupported(const Stmt *S, const char *Type) {
  ME.errorUnsupported(S, Type);
}

namespace {

/// Splat-copy a single element over a VLA whose element count is known nonzero.
void emitNonZeroVLAInit(FunctionEmitter &FE, QualType baseType, Address dest,
                        Address src, llvm::Value *sizeInChars) {
  CGBuilderTy &Builder = FE.Builder;

  CharUnits baseSize = FE.getContext().getTypeSizeInChars(baseType);
  llvm::Value *baseSizeInChars =
      llvm::ConstantInt::get(FE.IntPtrTy, baseSize.getQuantity());

  Address begin = dest.withElementType(FE.Int8Ty);
  llvm::Value *end = Builder.CreateInBoundsGEP(
      begin.getElementType(), begin.getPointer(), sizeInChars, "vla.end");

  llvm::BasicBlock *originBB = FE.Builder.GetInsertBlock();
  llvm::BasicBlock *loopBB = FE.createBasicBlock("vla-init.loop");
  llvm::BasicBlock *contBB = FE.createBasicBlock("vla-init.cont");

  // Make a loop over the VLA.  C99 guarantees that the VLA element
  // count must be nonzero.
  FE.genBlock(loopBB);

  llvm::PHINode *cur = Builder.CreatePHI(begin.getType(), 2, "vla.cur");
  cur->addIncoming(begin.getPointer(), originBB);

  CharUnits curAlign = dest.getAlignment().alignmentOfArrayElement(baseSize);

  // memcpy the individual element bit-pattern.
  Builder.CreateMemCpy(Address(cur, FE.Int8Ty, curAlign), src, baseSizeInChars,
                       /*volatile*/ false);

  // Go to the next element.
  llvm::Value *next =
      Builder.CreateInBoundsGEP(FE.Int8Ty, cur, baseSizeInChars, "vla.next");

  // Leave if that's the end of the VLA.
  llvm::Value *done = Builder.CreateICmpEQ(next, end, "vla-init.isdone");
  Builder.CreateCondBr(done, contBB, loopBB);
  cur->addIncoming(next, loopBB);

  FE.genBlock(contBB);
}

} // namespace

void FunctionEmitter::genNullInitialization(Address DestPtr, QualType Ty) {
  // Default-initialize storage: memset to zero, or copy a null constant when
  // the type is not bitwise-zeroable (e.g. some member-pointer layouts).

  if (DestPtr.getElementType() != Int8Ty)
    DestPtr = DestPtr.withElementType(Int8Ty);

  CharUnits size = getContext().getTypeSizeInChars(Ty);

  llvm::Value *SizeVal;
  const VariableArrayType *vla;

  // Don't bother emitting a zero-byte memset.
  if (size.isZero()) {
    // But note that getTypeInfo returns 0 for a VLA.
    if (const VariableArrayType *vlaType = dyn_cast_or_null<VariableArrayType>(
            getContext().getAsArrayType(Ty))) {
      auto VlaSize = getVLASize(vlaType);
      SizeVal = VlaSize.NumElts;
      CharUnits eltSize = getContext().getTypeSizeInChars(VlaSize.Type);
      if (!eltSize.isOne())
        SizeVal = Builder.CreateNUWMul(SizeVal, ME.getSize(eltSize));
      vla = vlaType;
    } else {
      return;
    }
  } else {
    SizeVal = ME.getSize(size);
    vla = nullptr;
  }

  // If the type contains a pointer to data member we can't memset it to zero.
  // Instead, create a null constant and copy it to the destination.
  if (!ME.getTypes().isZeroInitializable(Ty)) {
    // For a VLA, emit a single element, then splat that over the VLA.
    if (vla)
      Ty = getContext().getBaseElementType(vla);

    llvm::Constant *NullConstant = ME.genNullConstant(Ty);

    llvm::GlobalVariable *NullVariable = new llvm::GlobalVariable(
        ME.getModule(), NullConstant->getType(),
        /*isConstant=*/true, llvm::GlobalVariable::PrivateLinkage, NullConstant,
        llvm::Twine());
    CharUnits NullAlign = DestPtr.getAlignment();
    NullVariable->setAlignment(NullAlign.getAsAlign());
    Address SrcPtr(NullVariable, Builder.getInt8Ty(), NullAlign);

    if (vla)
      return emitNonZeroVLAInit(*this, Ty, DestPtr, SrcPtr, SizeVal);

    Builder.CreateMemCpy(DestPtr, SrcPtr, SizeVal, false);
    return;
  }

  // Otherwise, just memset the whole thing to zero.  This is legal
  // because in LLVM, all default initializers (other than the ones we just
  // handled above) are guaranteed to have a bit pattern of all zeros.
  Builder.CreateMemSet(DestPtr, Builder.getInt8(0), SizeVal, false);
}

llvm::BlockAddress *FunctionEmitter::addrOfLabel(const LabelDecl *L) {
  // Make sure that there is a block for the indirect goto.
  if (!IndirectBranch)
    getIndirectGotoBlock();

  llvm::BasicBlock *BB = getJumpDestForLabel(L).getBlock();

  // Make sure the indirect branch includes all of the address-taken blocks.
  IndirectBranch->addDestination(BB);
  return llvm::BlockAddress::get(CurFn, BB);
}

llvm::BasicBlock *FunctionEmitter::getIndirectGotoBlock() {
  // If we already made the indirect branch for indirect goto, return its block.
  if (IndirectBranch)
    return IndirectBranch->getParent();

  CGBuilderTy TmpBuilder(*this, createBasicBlock("indirectgoto"));

  llvm::Value *DestVal =
      TmpBuilder.CreatePHI(Int8PtrTy, 0, "indirect.goto.dest");

  IndirectBranch = TmpBuilder.CreateIndirectBr(DestVal);
  return IndirectBranch->getParent();
}

llvm::Value *FunctionEmitter::emitArrayLength(const ArrayType *origArrayType,
                                              QualType &baseType,
                                              Address &addr) {
  const ArrayType *arrayType = origArrayType;

  // If it's a VLA, we have to load the stored size.  Note that
  // this is the size of the VLA in bytes, not its size in elements.
  llvm::Value *numVLAElements = nullptr;
  if (isa<VariableArrayType>(arrayType)) {
    numVLAElements = getVLASize(cast<VariableArrayType>(arrayType)).NumElts;

    // Walk into all VLAs.  This doesn't require changes to addr,
    // which has type T* where T is the first non-VLA element type.
    do {
      QualType elementType = arrayType->getElementType();
      arrayType = getContext().getAsArrayType(elementType);

      // If we only have VLA components, 'addr' requires no adjustment.
      if (!arrayType) {
        baseType = elementType;
        return numVLAElements;
      }
    } while (isa<VariableArrayType>(arrayType));

    // We get out here only if we find a constant array type
    // inside the VLA.
  }

  // We have some number of constant-length arrays, so addr should
  // have LLVM type [M x [N x [...]]]*.  Build a GEP that walks
  // down to the first element of addr.
  llvm::SmallVector<llvm::Value *, 8> gepIndices;

  // GEP down to the array type.
  llvm::ConstantInt *zero = Builder.getInt32(0);
  gepIndices.push_back(zero);

  uint64_t countFromCLAs = 1;
  QualType eltType;

  llvm::ArrayType *llvmArrayType =
      dyn_cast<llvm::ArrayType>(addr.getElementType());
  while (llvmArrayType) {
    assert(isa<ConstantArrayType>(arrayType));
    assert(cast<ConstantArrayType>(arrayType)->getSize().getZExtValue() ==
           llvmArrayType->getNumElements());

    gepIndices.push_back(zero);
    countFromCLAs *= llvmArrayType->getNumElements();
    eltType = arrayType->getElementType();

    llvmArrayType = dyn_cast<llvm::ArrayType>(llvmArrayType->getElementType());
    arrayType = getContext().getAsArrayType(arrayType->getElementType());
    assert((!llvmArrayType || arrayType) &&
           "LLVM and NeverC types are out-of-synch");
  }

  if (arrayType) {
    // From this point onwards, the NeverC array type has been emitted
    // as some other type (probably a packed struct). Compute the array
    // size, and just emit the 'begin' expression as a bitcast.
    while (arrayType) {
      countFromCLAs *=
          cast<ConstantArrayType>(arrayType)->getSize().getZExtValue();
      eltType = arrayType->getElementType();
      arrayType = getContext().getAsArrayType(eltType);
    }

    llvm::Type *baseType = convertType(eltType);
    addr = addr.withElementType(baseType);
  } else {
    addr = Address(Builder.CreateInBoundsGEP(addr.getElementType(),
                                             addr.getPointer(), gepIndices,
                                             "array.begin"),
                   convertTypeForMem(eltType), addr.getAlignment());
  }

  baseType = eltType;

  llvm::Value *numElements = llvm::ConstantInt::get(SizeTy, countFromCLAs);

  // If we had any VLA dimensions, factor them in.
  if (numVLAElements)
    numElements = Builder.CreateNUWMul(numVLAElements, numElements);

  return numElements;
}

FunctionEmitter::VlaSizePair FunctionEmitter::getVLASize(QualType type) {
  const VariableArrayType *vla = getContext().getAsVariableArrayType(type);
  assert(vla && "type was not a variable array type!");
  return getVLASize(vla);
}

FunctionEmitter::VlaSizePair
FunctionEmitter::getVLASize(const VariableArrayType *type) {
  // The number of elements so far; always size_t.
  llvm::Value *numElements = nullptr;

  QualType elementType;
  do {
    elementType = type->getElementType();
    llvm::Value *vlaSize = VLASizeMap[type->getSizeExpr()];
    assert(vlaSize && "no size for VLA!");
    assert(vlaSize->getType() == SizeTy);

    if (!numElements) {
      numElements = vlaSize;
    } else {
      // It's undefined behavior if this wraps around, so mark it that way.
      numElements = Builder.CreateNUWMul(numElements, vlaSize);
    }
  } while ((type = getContext().getAsVariableArrayType(elementType)));

  return {numElements, elementType};
}

FunctionEmitter::VlaSizePair FunctionEmitter::getVLAElements1D(QualType type) {
  const VariableArrayType *vla = getContext().getAsVariableArrayType(type);
  assert(vla && "type was not a variable array type!");
  return getVLAElements1D(vla);
}

FunctionEmitter::VlaSizePair
FunctionEmitter::getVLAElements1D(const VariableArrayType *Vla) {
  llvm::Value *VlaSize = VLASizeMap[Vla->getSizeExpr()];
  assert(VlaSize && "no size for VLA!");
  assert(VlaSize->getType() == SizeTy);
  return {VlaSize, Vla->getElementType()};
}

void FunctionEmitter::genVariablyModifiedType(QualType type) {
  assert(type->isVariablyModifiedType() &&
         "Must pass variably modified type to genVLASizes!");

  ensureInsertPoint();

  // We're going to walk down into the type and look for VLA
  // expressions.
  do {
    assert(type->isVariablyModifiedType());

    const Type *ty = type.getTypePtr();
    switch (ty->getTypeClass()) {

#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base)
#include "neverc/Tree/TypeNodes.td.h"
      llvm_unreachable("unexpected dependent type!");

    // These types are never variably-modified.
    case Type::Builtin:
    case Type::Complex:
    case Type::Vector:
    case Type::ExtVector:
    case Type::ConstantMatrix:
    case Type::Record:
    case Type::Enum:
    case Type::BitInt:
      llvm_unreachable("type class is never variably-modified!");

    case Type::Elaborated:
      type = cast<ElaboratedType>(ty)->getNamedType();
      break;

    case Type::Adjusted:
      type = cast<AdjustedType>(ty)->getAdjustedType();
      break;

    case Type::Decayed:
      type = cast<DecayedType>(ty)->getPointeeType();
      break;

    case Type::Pointer:
      type = cast<PointerType>(ty)->getPointeeType();
      break;

    case Type::ConstantArray:
    case Type::IncompleteArray:
      // Losing element qualification here is fine.
      type = cast<ArrayType>(ty)->getElementType();
      break;

    case Type::VariableArray: {
      // Losing element qualification here is fine.
      const VariableArrayType *vat = cast<VariableArrayType>(ty);

      // Unknown size indication requires no size computation.
      // Otherwise, evaluate and record it.
      if (const Expr *sizeExpr = vat->getSizeExpr()) {
        // It's possible that we might have emitted this already,
        // e.g. with a typedef and a pointer to it.
        if (!VLASizeMap.lookup(sizeExpr)) {
          llvm::Value *size = genScalarExpr(sizeExpr);

          // VLA size must be > 0; negative is UB so zero-extend is safe.
          VLASizeMap[sizeExpr] =
              Builder.CreateIntCast(size, SizeTy, /*signed*/ false);
        }
      }
      type = vat->getElementType();
      break;
    }

    case Type::FunctionProto:
    case Type::FunctionNoProto:
      type = cast<FunctionType>(ty)->getReturnType();
      break;

    case Type::Paren:
    case Type::TypeOf:
    case Type::Attributed:
    case Type::BTFTagAttributed:
    case Type::MacroQualified:
      // Keep walking after single level desugaring.
      type = type.getSingleStepDesugaredType(getContext());
      break;

    case Type::Typedef:
    case Type::Auto:
      // Stop walking: nothing to do.
      return;

    case Type::TypeOfExpr:
      // Stop walking: emit typeof expression.
      genIgnoredExpr(cast<TypeOfExprType>(ty)->getUnderlyingExpr());
      return;

    case Type::Atomic:
      type = cast<AtomicType>(ty)->getValueType();
      break;
    }
  } while (type->isVariablyModifiedType());
}

// ===----------------------------------------------------------------------===
// Code generation utilities
// ===----------------------------------------------------------------------===

Address FunctionEmitter::genVAListRef(const Expr *E) {
  if (getContext().getBuiltinVaListType()->isArrayType())
    return genPointerWithAlignment(E);
  return genLValue(E).getAddress(*this);
}

Address FunctionEmitter::genMSVAListRef(const Expr *E) {
  return genLValue(E).getAddress(*this);
}

void FunctionEmitter::genDeclRefExprDbgValue(const DeclRefExpr *E,
                                             const APValue &Init) {
  assert(Init.hasValue() && "Invalid DeclRefExpr initializer!");
  if (DebugEmitter *Dbg = getDebugInfo())
    if (ME.getCodeGenOpts().hasReducedDebugInfo())
      Dbg->genGlobalVariable(E->getDecl(), Init);
}

FunctionEmitter::PeepholeProtection
FunctionEmitter::protectFromPeepholes(RValue rvalue) {
  // At the moment, the only aggressive peephole we do in IR gen
  // is trunc(zext) folding, but if we add more, we can easily
  // extend this protection.

  if (!rvalue.isScalar())
    return PeepholeProtection();
  llvm::Value *value = rvalue.getScalarVal();
  if (!isa<llvm::ZExtInst>(value))
    return PeepholeProtection();

  // Just make an extra bitcast.
  assert(haveInsertPoint());
  llvm::Instruction *inst = new llvm::BitCastInst(value, value->getType(), "",
                                                  Builder.GetInsertBlock());

  PeepholeProtection protection;
  protection.Inst = inst;
  return protection;
}

void FunctionEmitter::unprotectFromPeepholes(PeepholeProtection protection) {
  if (!protection.Inst)
    return;

  // In theory, we could try to duplicate the peepholes now, but whatever.
  protection.Inst->eraseFromParent();
}

void FunctionEmitter::emitAlignmentAssumption(llvm::Value *PtrValue, QualType,
                                              llvm::Value *Alignment,
                                              llvm::Value *OffsetValue) {
  if (Alignment->getType() != IntPtrTy)
    Alignment =
        Builder.CreateIntCast(Alignment, IntPtrTy, false, "casted.align");
  if (OffsetValue && OffsetValue->getType() != IntPtrTy)
    OffsetValue =
        Builder.CreateIntCast(OffsetValue, IntPtrTy, true, "casted.offset");
  Builder.CreateAlignmentAssumption(ME.getDataLayout(), PtrValue, Alignment,
                                    OffsetValue);
}

void FunctionEmitter::emitAlignmentAssumption(llvm::Value *PtrValue,
                                              const Expr *E,
                                              llvm::Value *Alignment,
                                              llvm::Value *OffsetValue) {
  emitAlignmentAssumption(PtrValue, E->getType(), Alignment, OffsetValue);
}

llvm::Value *FunctionEmitter::genAnnotationCall(llvm::Function *AnnotationFn,
                                                llvm::Value *AnnotatedVal,
                                                llvm::StringRef AnnotationStr,
                                                SourceLocation Location,
                                                const AnnotateAttr *Attr) {
  llvm::SmallVector<llvm::Value *, 5> Args = {
      AnnotatedVal,
      ME.genAnnotationString(AnnotationStr),
      ME.genAnnotationUnit(Location),
      ME.genAnnotationLineNo(Location),
  };
  if (Attr)
    Args.push_back(ME.genAnnotationArgs(Attr));
  return Builder.CreateCall(AnnotationFn, Args);
}

void FunctionEmitter::genVarAnnotations(const VarDecl *D, llvm::Value *V) {
  assert(D->hasAttr<AnnotateAttr>() && "no annotate attribute");
  for (const auto *I : D->specific_attrs<AnnotateAttr>())
    genAnnotationCall(ME.getIntrinsic(llvm::Intrinsic::var_annotation,
                                      {V->getType(), ME.ConstGlobalsPtrTy}),
                      V, I->getAnnotation(), D->getLocation(), I);
}

Address FunctionEmitter::genFieldAnnotations(const FieldDecl *D, Address Addr) {
  assert(D->hasAttr<AnnotateAttr>() && "no annotate attribute");
  llvm::Value *V = Addr.getPointer();
  llvm::Type *VTy = V->getType();
  auto *PTy = dyn_cast<llvm::PointerType>(VTy);
  unsigned AS = PTy ? PTy->getAddressSpace() : 0;
  llvm::PointerType *IntrinTy = llvm::PointerType::get(ME.getLLVMContext(), AS);
  llvm::Function *F = ME.getIntrinsic(llvm::Intrinsic::ptr_annotation,
                                      {IntrinTy, ME.ConstGlobalsPtrTy});

  for (const auto *I : D->specific_attrs<AnnotateAttr>()) {
    if (VTy != IntrinTy)
      V = Builder.CreateBitCast(V, IntrinTy);
    V = genAnnotationCall(F, V, I->getAnnotation(), D->getLocation(), I);
    V = Builder.CreateBitCast(V, VTy);
  }

  return Address(V, Addr.getElementType(), Addr.getAlignment());
}

// ===----------------------------------------------------------------------===
// Sanitizer & IR inserter hooks
// ===----------------------------------------------------------------------===

FunctionEmitter::SanitizerScope::SanitizerScope(FunctionEmitter *FE) : FE(FE) {
  assert(!FE->IsSanitizerScope);
  FE->IsSanitizerScope = true;
}

FunctionEmitter::SanitizerScope::~SanitizerScope() {
  FE->IsSanitizerScope = false;
}

void FunctionEmitter::InsertHelper(llvm::Instruction *I,
                                   const llvm::Twine &Name,
                                   llvm::BasicBlock *BB,
                                   llvm::BasicBlock::iterator InsertPt) const {
  if (LLVM_LIKELY(!IsSanitizerScope))
    return LoopStack.InsertHelper(I);
  LoopStack.InsertHelper(I);
  I->setNoSanitizeMetadata();
}

void CGBuilderInserter::InsertHelper(
    llvm::Instruction *I, const llvm::Twine &Name, llvm::BasicBlock *BB,
    llvm::BasicBlock::iterator InsertPt) const {
  llvm::IRBuilderDefaultInserter::InsertHelper(I, Name, BB, InsertPt);
  if (FE)
    FE->InsertHelper(I, Name, BB, InsertPt);
}

// ===----------------------------------------------------------------------===
// Target features & multiversion dispatch
// ===----------------------------------------------------------------------===

void FunctionEmitter::checkTargetFeatures(const CallExpr *E,
                                          const FunctionDecl *TargetDecl) {
  return checkTargetFeatures(E->getBeginLoc(), TargetDecl);
}

/// Verify that the caller has the target features required by the callee.
void FunctionEmitter::checkTargetFeatures(SourceLocation Loc,
                                          const FunctionDecl *TargetDecl) {
  // Early exit if this is an indirect call.
  if (!TargetDecl)
    return;

  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(CurCodeDecl);
  if (!FD)
    return;

  // Grab the required features for the call. For a builtin this is listed in
  // the td file with the default cpu, for an always_inline function this is any
  // listed cpu and any listed features.
  unsigned BuiltinID = TargetDecl->getBuiltinID();
  std::string MissingFeature;
  llvm::StringMap<bool> CallerFeatureMap;
  ME.getContext().getFunctionFeatureMap(CallerFeatureMap, FD);
  if (BuiltinID) {
    llvm::StringRef FeatureList(
        ME.getContext().BuiltinInfo.getRequiredFeatures(BuiltinID));
    if (!Builtin::evaluateRequiredTargetFeatures(FeatureList,
                                                 CallerFeatureMap)) {
      ME.getDiags().Report(Loc, diag::err_builtin_needs_feature)
          << TargetDecl->getDeclName() << FeatureList;
    }
  } else if (!TargetDecl->isMultiVersion() &&
             TargetDecl->hasAttr<TargetAttr>()) {
    const TargetAttr *TD = TargetDecl->getAttr<TargetAttr>();
    ParsedTargetAttr ParsedAttr = ME.getContext().filterFunctionTargetAttrs(TD);

    llvm::SmallVector<llvm::StringRef, 1> ReqFeatures;
    llvm::StringMap<bool> CalleeFeatureMap;
    ME.getContext().getFunctionFeatureMap(CalleeFeatureMap, TargetDecl);

    for (const auto &F : ParsedAttr.Features) {
      if (F[0] == '+' && CalleeFeatureMap.lookup(F.substr(1)))
        ReqFeatures.push_back(llvm::StringRef(F).substr(1));
    }

    for (const auto &F : CalleeFeatureMap) {
      // Only positive features are "required".
      if (F.getValue())
        ReqFeatures.push_back(F.getKey());
    }
    if (!llvm::all_of(ReqFeatures, [&](llvm::StringRef Feature) {
          if (!CallerFeatureMap.lookup(Feature)) {
            MissingFeature = Feature.str();
            return false;
          }
          return true;
        }))
      ME.getDiags().Report(Loc, diag::err_function_needs_feature)
          << FD->getDeclName() << TargetDecl->getDeclName() << MissingFeature;
  } else if (!FD->isMultiVersion() && FD->hasAttr<TargetAttr>()) {
    llvm::StringMap<bool> CalleeFeatureMap;
    ME.getContext().getFunctionFeatureMap(CalleeFeatureMap, TargetDecl);

    for (const auto &F : CalleeFeatureMap) {
      if (F.getValue() && (!CallerFeatureMap.lookup(F.getKey()) ||
                           !CallerFeatureMap.find(F.getKey())->getValue()))
        ME.getDiags().Report(Loc, diag::err_function_needs_feature)
            << FD->getDeclName() << TargetDecl->getDeclName() << F.getKey();
    }
  }
}

llvm::Value *FunctionEmitter::formAArch64ResolverCondition(
    const MultiVersionResolverOption &RO) {
  llvm::SmallVector<llvm::StringRef, 8> CondFeatures;
  for (const llvm::StringRef &Feature : RO.Conditions.Features) {
    // Form condition for features which are not yet enabled in target
    if (!getContext().getTargetInfo().hasFeature(Feature))
      CondFeatures.push_back(Feature);
  }
  if (!CondFeatures.empty()) {
    return genAArch64CpuSupports(CondFeatures);
  }
  return nullptr;
}

llvm::Value *FunctionEmitter::formX86ResolverCondition(
    const MultiVersionResolverOption &RO) {
  llvm::Value *Condition = nullptr;

  if (!RO.Conditions.Architecture.empty()) {
    llvm::StringRef Arch = RO.Conditions.Architecture;
    // If arch= specifies an x86-64 micro-architecture level, test the feature
    // with __builtin_cpu_supports, otherwise use __builtin_cpu_is.
    if (Arch.starts_with("x86-64"))
      Condition = genX86CpuSupports({Arch});
    else
      Condition = genX86CpuIs(Arch);
  }

  if (!RO.Conditions.Features.empty()) {
    llvm::Value *FeatureCond = genX86CpuSupports(RO.Conditions.Features);
    Condition =
        Condition ? Builder.CreateAnd(Condition, FeatureCond) : FeatureCond;
  }
  return Condition;
}

namespace {

void generateMultiVersionDispatchReturn(ModuleEmitter &ME,
                                        llvm::Function *Resolver,
                                        CGBuilderTy &Builder,
                                        llvm::Function *FuncToReturn,
                                        bool SupportsIFunc) {
  if (SupportsIFunc) {
    Builder.CreateRet(FuncToReturn);
    return;
  }

  llvm::SmallVector<llvm::Value *, 10> Args(
      llvm::make_pointer_range(Resolver->args()));

  llvm::CallInst *Result = Builder.CreateCall(FuncToReturn, Args);
  Result->setTailCallKind(llvm::CallInst::TCK_MustTail);

  if (Resolver->getReturnType()->isVoidTy())
    Builder.CreateRetVoid();
  else
    Builder.CreateRet(Result);
}

} // namespace

void FunctionEmitter::genMultiVersionResolver(
    llvm::Function *Resolver,
    llvm::ArrayRef<MultiVersionResolverOption> Options) {

  llvm::Triple::ArchType ArchType =
      getContext().getTargetInfo().getTriple().getArch();

  switch (ArchType) {
  case llvm::Triple::x86_64:
    genX86MultiVersionResolver(Resolver, Options);
    return;
  case llvm::Triple::aarch64:
    genAArch64MultiVersionResolver(Resolver, Options);
    return;

  default:
    assert(false && "Only implemented for x86_64 and AArch64 targets");
  }
}

void FunctionEmitter::genAArch64MultiVersionResolver(
    llvm::Function *Resolver,
    llvm::ArrayRef<MultiVersionResolverOption> Options) {
  assert(!Options.empty() && "No multiversion resolver options found");
  assert(Options.back().Conditions.Features.size() == 0 &&
         "Default case must be last");
  bool SupportsIFunc = getContext().getTargetInfo().supportsIFunc();
  assert(SupportsIFunc &&
         "Multiversion resolver requires target IFUNC support");
  bool AArch64CpuInitialized = false;
  llvm::BasicBlock *CurBlock = createBasicBlock("resolver_entry", Resolver);

  for (const MultiVersionResolverOption &RO : Options) {
    Builder.SetInsertPoint(CurBlock);
    llvm::Value *Condition = formAArch64ResolverCondition(RO);

    // The 'default' or 'all features enabled' case.
    if (!Condition) {
      generateMultiVersionDispatchReturn(ME, Resolver, Builder, RO.Function,
                                         SupportsIFunc);
      return;
    }

    if (!AArch64CpuInitialized) {
      Builder.SetInsertPoint(CurBlock, CurBlock->begin());
      genAArch64CpuInit();
      AArch64CpuInitialized = true;
      Builder.SetInsertPoint(CurBlock);
    }

    llvm::BasicBlock *RetBlock = createBasicBlock("resolver_return", Resolver);
    CGBuilderTy RetBuilder(*this, RetBlock);
    generateMultiVersionDispatchReturn(ME, Resolver, RetBuilder, RO.Function,
                                       SupportsIFunc);
    CurBlock = createBasicBlock("resolver_else", Resolver);
    Builder.CreateCondBr(Condition, RetBlock, CurBlock);
  }

  // If no default, emit an unreachable.
  Builder.SetInsertPoint(CurBlock);
  llvm::CallInst *TrapCall = genTrapCall(llvm::Intrinsic::trap);
  TrapCall->setDoesNotReturn();
  TrapCall->setDoesNotThrow();
  Builder.CreateUnreachable();
  Builder.ClearInsertionPoint();
}

void FunctionEmitter::genX86MultiVersionResolver(
    llvm::Function *Resolver,
    llvm::ArrayRef<MultiVersionResolverOption> Options) {

  bool SupportsIFunc = getContext().getTargetInfo().supportsIFunc();

  // Main function's basic block.
  llvm::BasicBlock *CurBlock = createBasicBlock("resolver_entry", Resolver);
  Builder.SetInsertPoint(CurBlock);
  genX86CpuInit();

  for (const MultiVersionResolverOption &RO : Options) {
    Builder.SetInsertPoint(CurBlock);
    llvm::Value *Condition = formX86ResolverCondition(RO);

    // The 'default' or 'generic' case.
    if (!Condition) {
      assert(&RO == Options.end() - 1 &&
             "Default or Generic case must be last");
      generateMultiVersionDispatchReturn(ME, Resolver, Builder, RO.Function,
                                         SupportsIFunc);
      return;
    }

    llvm::BasicBlock *RetBlock = createBasicBlock("resolver_return", Resolver);
    CGBuilderTy RetBuilder(*this, RetBlock);
    generateMultiVersionDispatchReturn(ME, Resolver, RetBuilder, RO.Function,
                                       SupportsIFunc);
    CurBlock = createBasicBlock("resolver_else", Resolver);
    Builder.CreateCondBr(Condition, RetBlock, CurBlock);
  }

  // If no generic/default, emit an unreachable.
  Builder.SetInsertPoint(CurBlock);
  llvm::CallInst *TrapCall = genTrapCall(llvm::Intrinsic::trap);
  TrapCall->setDoesNotReturn();
  TrapCall->setDoesNotThrow();
  Builder.CreateUnreachable();
  Builder.ClearInsertionPoint();
}

llvm::DebugLoc FunctionEmitter::sourceLocToDebugLoc(SourceLocation Location) {
  if (DebugEmitter *DI = getDebugInfo())
    return DI->sourceLocToDebugLoc(Location);

  return llvm::DebugLoc();
}

llvm::Value *
FunctionEmitter::emitCondLikelihoodViaExpectIntrinsic(llvm::Value *Cond,
                                                      Stmt::Likelihood LH) {
  switch (LH) {
  case Stmt::LH_None:
    return Cond;
  case Stmt::LH_Likely:
  case Stmt::LH_Unlikely:
    // Don't generate llvm.expect on -O0 as the backend won't use it for
    // anything.
    if (ME.getCodeGenOpts().OptimizationLevel == 0)
      return Cond;
    llvm::Type *CondTy = Cond->getType();
    assert(CondTy->isIntegerTy(1) && "expecting condition to be a boolean");
    llvm::Function *FnExpect = ME.getIntrinsic(llvm::Intrinsic::expect, CondTy);
    llvm::Value *ExpectedValueOfCond =
        llvm::ConstantInt::getBool(CondTy, LH == Stmt::LH_Likely);
    return Builder.CreateCall(FnExpect, {Cond, ExpectedValueOfCond},
                              Cond->getName() + ".expval");
  }
  llvm_unreachable("Unknown Likelihood");
}

llvm::Value *FunctionEmitter::emitBoolVecConversion(llvm::Value *SrcVec,
                                                    unsigned NumElementsDst,
                                                    const llvm::Twine &Name) {
  auto *SrcTy = cast<llvm::FixedVectorType>(SrcVec->getType());
  unsigned NumElementsSrc = SrcTy->getNumElements();
  if (NumElementsSrc == NumElementsDst)
    return SrcVec;

  std::vector<int> ShuffleMask(NumElementsDst, -1);
  for (unsigned MaskIdx = 0;
       MaskIdx < std::min<>(NumElementsDst, NumElementsSrc); ++MaskIdx)
    ShuffleMask[MaskIdx] = MaskIdx;

  return Builder.CreateShuffleVector(SrcVec, ShuffleMask, Name);
}
