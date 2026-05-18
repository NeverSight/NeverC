#ifndef NEVERC_LIB_CODEGEN_CORE_FUNCTIONEMITTER_H
#define NEVERC_LIB_CODEGEN_CORE_FUNCTIONEMITTER_H

#include "Core/EmitterBuilder.h"
#include "Core/EmitterValue.h"
#include "Core/ModuleEmitter.h"
#include "Debug/DebugEmitterInfo.h"
#include "Stmt/EHScopeStack.h"
#include "Stmt/LoopEmitterInfo.h"
#include "Stmt/VarBypassDetector.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Expr/CurrentSourceLocExprScope.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Debug.h"
#include <optional>

namespace llvm {
class BasicBlock;
class LLVMContext;
class MDNode;
class SwitchInst;
class Twine;
class Value;
} // namespace llvm

namespace neverc {
class TreeContext;
class Decl;
class LabelDecl;
class FunctionDecl;
class FunctionProtoType;
class LabelStmt;
class TargetInfo;
class VarDecl;
class SVETypeFlags;

namespace analyze_os_log {
class OSLogBufferLayout;
}

namespace Emit {
class TypeEmitter;
class FnCallee;
class ABIFunctionInfo;
class CGABI;
class TargetCodeGenInfo;

enum TypeEvaluationKind { TEK_Scalar, TEK_Complex, TEK_Aggregate };

enum SanitizerHandler {
  AddOverflow,
  SubOverflow,
  MulOverflow,
};

struct DominatingLLVMValue {
  typedef llvm::PointerIntPair<llvm::Value *, 1, bool> saved_type;

  static bool needsSaving(llvm::Value *value) {
    // If it's not an instruction, we don't need to save.
    if (!isa<llvm::Instruction>(value))
      return false;

    // If it's an instruction in the entry block, we don't need to save.
    llvm::BasicBlock *block = cast<llvm::Instruction>(value)->getParent();
    return (block != &block->getParent()->getEntryBlock());
  }

  static saved_type save(FunctionEmitter &FE, llvm::Value *value);
  static llvm::Value *restore(FunctionEmitter &FE, saved_type value);
};

template <class T> struct DominatingPointer<T, true> : DominatingLLVMValue {
  typedef T *type;
  static type restore(FunctionEmitter &FE, saved_type value) {
    return static_cast<T *>(DominatingLLVMValue::restore(FE, value));
  }
};

template <> struct DominatingValue<Address> {
  typedef Address type;

  struct saved_type {
    DominatingLLVMValue::saved_type SavedValue;
    llvm::Type *ElementType;
    CharUnits Alignment;
  };

  static bool needsSaving(type value) {
    return DominatingLLVMValue::needsSaving(value.getPointer());
  }
  static saved_type save(FunctionEmitter &FE, type value) {
    return {DominatingLLVMValue::save(FE, value.getPointer()),
            value.getElementType(), value.getAlignment()};
  }
  static type restore(FunctionEmitter &FE, saved_type value) {
    return Address(DominatingLLVMValue::restore(FE, value.SavedValue),
                   value.ElementType, value.Alignment);
  }
};

template <> struct DominatingValue<RValue> {
  typedef RValue type;
  class saved_type {
    enum Kind {
      ScalarLiteral,
      ScalarAddress,
      AggregateLiteral,
      AggregateAddress,
      ComplexAddress
    };

    llvm::Value *Value;
    llvm::Type *ElementType;
    unsigned K : 3;
    unsigned Align : 29;
    saved_type(llvm::Value *v, llvm::Type *e, Kind k, unsigned a = 0)
        : Value(v), ElementType(e), K(k), Align(a) {}

  public:
    static bool needsSaving(RValue value);
    static saved_type save(FunctionEmitter &FE, RValue value);
    RValue restore(FunctionEmitter &FE);

    // implementations in CleanupEmitter.cpp
  };

  static bool needsSaving(type value) { return saved_type::needsSaving(value); }
  static saved_type save(FunctionEmitter &FE, type value) {
    return saved_type::save(FE, value);
  }
  static type restore(FunctionEmitter &FE, saved_type value) {
    return value.restore(FE);
  }
};

class FunctionEmitter : public TypeEmitterCache {
  FunctionEmitter(const FunctionEmitter &) = delete;
  void operator=(const FunctionEmitter &) = delete;

  friend class CGABI;

public:
  struct JumpDest {
    JumpDest() : Block(nullptr), Index(0) {}
    JumpDest(llvm::BasicBlock *Block, EHScopeStack::stable_iterator Depth,
             unsigned Index)
        : Block(Block), ScopeDepth(Depth), Index(Index) {}

    bool isValid() const { return Block != nullptr; }
    llvm::BasicBlock *getBlock() const { return Block; }
    EHScopeStack::stable_iterator getScopeDepth() const { return ScopeDepth; }
    unsigned getDestIndex() const { return Index; }

    // This should be used cautiously.
    void setScopeDepth(EHScopeStack::stable_iterator depth) {
      ScopeDepth = depth;
    }

  private:
    llvm::BasicBlock *Block;
    EHScopeStack::stable_iterator ScopeDepth;
    unsigned Index;
  };

  bool tryGetInnermostBreakDest(JumpDest &Out) const {
    if (BreakContinueStack.empty())
      return false;
    Out = BreakContinueStack.back().BreakBlock;
    return Out.isValid();
  }
  bool tryGetInnermostContinueDest(JumpDest &Out) const {
    if (BreakContinueStack.empty())
      return false;
    Out = BreakContinueStack.back().ContinueBlock;
    return Out.isValid();
  }

  bool tryGetInnermostSEHLeaveDest(JumpDest &Out) const {
    if (SEHTryEpilogueStack.empty() || !SEHTryEpilogueStack.back())
      return false;
    Out = *SEHTryEpilogueStack.back();
    return Out.isValid();
  }

  bool hasNormalCleanupDestSlot() const { return NormalCleanupDest.isValid(); }
  Address getNormalCleanupDestSlotIfExists() const { return NormalCleanupDest; }

  enum class SEHFinallyBailoutKind : uint8_t {
    None = 0,
    Break = 1,
    Continue = 2,
    Goto = 3,
    Leave = 4,
  };

  struct SEHFinallyBailoutInfo {
    Address KindSlot = Address::invalid();   // i8
    Address TargetSlot = Address::invalid(); // i32 (label code for goto)
    llvm::SmallVector<const LabelDecl *, 4> GotoLabels; // code: 1..N
  };

  llvm::DenseMap<llvm::Function *, SEHFinallyBailoutInfo> SEHFinallyBailouts;

  Address SEHFinallyBailoutKindParentAlloca = Address::invalid();
  Address SEHFinallyBailoutTargetParentAlloca = Address::invalid();

  Address SEHFinallyBailoutKindParent = Address::invalid();
  Address SEHFinallyBailoutTargetParent = Address::invalid();

  llvm::DenseMap<const LabelDecl *, unsigned> SEHFinallyGotoLabelToCode;

  ModuleEmitter &ME; // Per-module state.
  const TargetInfo &Target;

  // For EH/SEH outlined funclets, this field points to parent's FE
  FunctionEmitter *ParentFnEmitter = nullptr;

  typedef std::pair<llvm::Value *, llvm::Value *> ComplexPairTy;
  LoopInfoStack LoopStack;
  CGBuilderTy Builder;

  // Stores variables for which we can't generate correct lifetime markers
  // because of jumps.
  VarBypassDetector Bypasses;

  void InsertHelper(llvm::Instruction *I, const llvm::Twine &Name,
                    llvm::BasicBlock *BB,
                    llvm::BasicBlock::iterator InsertPt) const;

  const Decl *CurFuncDecl = nullptr;
  const Decl *CurCodeDecl = nullptr;
  const ABIFunctionInfo *CurFnInfo = nullptr;
  QualType FnRetTy;
  llvm::Function *CurFn = nullptr;

  GlobalDecl CurGD;

  EHScopeStack::stable_iterator PrologueCleanupDepth;

  JumpDest ReturnBlock;

  Address ReturnValue = Address::invalid();

  Address ReturnValuePointer = Address::invalid();

  const Expr *RetExpr = nullptr;

  bool hasLabelBeenSeenInCurrentScope() const {
    if (CurLexicalScope)
      return CurLexicalScope->hasLabels();
    return !LabelMap.empty();
  }

  llvm::AssertingVH<llvm::Instruction> AllocaInsertPt;

private:
  llvm::AssertingVH<llvm::Instruction> PostAllocaInsertPt = nullptr;

public:
  llvm::Instruction *getPostAllocaInsertPoint() {
    if (!PostAllocaInsertPt) {
      assert(AllocaInsertPt &&
             "Expected static alloca insertion point at function prologue");
      assert(AllocaInsertPt->getParent()->isEntryBlock() &&
             "EBB should be entry block of the current code gen function");
      PostAllocaInsertPt = AllocaInsertPt->clone();
      PostAllocaInsertPt->setName("postallocapt");
      PostAllocaInsertPt->insertAfter(AllocaInsertPt);
    }

    return PostAllocaInsertPt;
  }

  class AbstractCallee {
    const Decl *CalleeDecl;

  public:
    AbstractCallee() : CalleeDecl(nullptr) {}
    AbstractCallee(const FunctionDecl *FD) : CalleeDecl(FD) {}
    bool hasFunctionDecl() const {
      return isa_and_nonnull<FunctionDecl>(CalleeDecl);
    }
    const Decl *getDecl() const { return CalleeDecl; }
    unsigned getNumParams() const {
      return cast<FunctionDecl>(CalleeDecl)->getNumParams();
    }
    const ParmVarDecl *getParamDecl(unsigned I) const {
      return cast<FunctionDecl>(CalleeDecl)->getParamDecl(I);
    }
  };

  bool IsSanitizerScope = false;

  class SanitizerScope {
    FunctionEmitter *FE;

  public:
    SanitizerScope(FunctionEmitter *FE);
    ~SanitizerScope();
  };

  bool SawAsmBlock = false;

  GlobalDecl CurSEHParent;

  bool IsOutlinedSEHHelper = false;

  bool IsInPreservedAIRegion = false;

  bool InNoMergeAttributedStmt = false;

  bool InNoInlineAttributedStmt = false;

  bool InAlwaysInlineAttributedStmt = false;

  // The CallExpr within the current statement that the musttail attribute
  // applies to.  nullptr if there is no 'musttail' on the current statement.
  const CallExpr *MustTailCall = nullptr;

  bool checkIfFunctionMustProgress() {
    if (ME.getCodeGenOpts().getFiniteLoops() ==
        CodeGenOptions::FiniteLoopsKind::Never)
      return false;

    // The standard guarantees that a thread eventually will do one of the
    // following:
    // - terminate,
    //  - make a call to a library I/O function,
    //  - perform an access through a volatile lvalue, or
    //  - perform a synchronization operation or an atomic operation.
    //
    return false;
  }

  bool checkIfLoopMustProgress(bool HasConstantCond) {
    if (ME.getCodeGenOpts().getFiniteLoops() ==
        CodeGenOptions::FiniteLoopsKind::Always)
      return true;
    if (ME.getCodeGenOpts().getFiniteLoops() ==
        CodeGenOptions::FiniteLoopsKind::Never)
      return false;

    // If the containing function must make progress, loops also must make
    // progress.
    if (checkIfFunctionMustProgress())
      return true;

    // Now apply rules for plain C (see  6.8.5.6 in C11).
    // Loops with constant conditions do not have to make progress in any C
    // version.
    if (HasConstantCond)
      return false;

    // Loops with non-constant conditions must make progress in C11 and later.
    return getLangOpts().C11;
  }

  EHScopeStack EHStack;
  llvm::SmallVector<char, 256> LifetimeExtendedCleanupStack;
  llvm::SmallVector<const JumpDest *, 2> SEHTryEpilogueStack;

  llvm::Instruction *CurrentFuncletPad = nullptr;

  class CallLifetimeEnd final : public EHScopeStack::Cleanup {
    bool isRedundantBeforeReturn() override { return true; }

    llvm::Value *Addr;
    llvm::Value *Size;

  public:
    CallLifetimeEnd(Address addr, llvm::Value *size)
        : Addr(addr.getPointer()), Size(size) {}

    void Emit(FunctionEmitter &FE, Flags flags) override {
      FE.genLifetimeEnd(Size, Addr);
    }
  };

  struct LifetimeExtendedCleanupHeader {
    /// The size of the following cleanup object.
    unsigned Size;
    /// The kind of cleanup to push: a value from the CleanupKind enumeration.
    unsigned Kind : 31;
    /// Whether this is a conditional cleanup.
    unsigned IsConditional : 1;

    size_t getSize() const { return Size; }
    CleanupKind getKind() const { return (CleanupKind)Kind; }
    bool isConditional() const { return IsConditional; }
  };

  Address NormalCleanupDest = Address::invalid();

  unsigned NextCleanupDestIndex = 1;

  llvm::BasicBlock *EHResumeBlock = nullptr;

  llvm::Value *ExceptionSlot = nullptr;

  llvm::AllocaInst *EHSelectorSlot = nullptr;

  llvm::SmallVector<Address, 1> SEHCodeSlotStack;

  llvm::SmallVector<Address, 1> SEHRetNowStack;

  Address SEHRetNowParent = Address::invalid();

  Address SEHReturnValue = Address::invalid();

  llvm::Value *SEHInfo = nullptr;

  llvm::BasicBlock *genLandingPad();

  llvm::BasicBlock *getInvokeDestImpl();

  template <class T>
  typename DominatingValue<T>::saved_type saveValueInCond(T value) {
    return DominatingValue<T>::save(*this, value);
  }

  class FPOptionsRAII {
  public:
    FPOptionsRAII(FunctionEmitter &FE, FPOptions FPFeatures);
    FPOptionsRAII(FunctionEmitter &FE, const Expr *E);
    ~FPOptionsRAII();

  private:
    void ConstructorHelper(FPOptions FPFeatures);
    FunctionEmitter &FE;
    FPOptions OldFPFeatures;
    llvm::fp::ExceptionBehavior OldExcept;
    llvm::RoundingMode OldRounding;
    std::optional<CGBuilderTy::FastMathFlagGuard> FMFGuard;
  };
  FPOptions CurFPFeatures;

public:
  bool isSEHTryScope() const { return !SEHTryEpilogueStack.empty(); }

  template <class T, class... As>
  void pushFullExprCleanup(CleanupKind kind, As... A) {
    // If we're not in a conditional branch, or if none of the
    // arguments requires saving, then use the unconditional cleanup.
    if (!isInConditionalBranch())
      return EHStack.pushCleanup<T>(kind, A...);

    // Stash values in a tuple so we can guarantee the order of saves.
    typedef std::tuple<typename DominatingValue<As>::saved_type...> SavedTuple;
    SavedTuple Saved{saveValueInCond(A)...};

    typedef EHScopeStack::ConditionalCleanup<T, As...> CleanupType;
    EHStack.pushCleanupTuple<CleanupType>(kind, Saved);
    initFullExprCleanup();
  }

  template <class T, class... As>
  void pushCleanupAfterFullExpr(CleanupKind Kind, As... A) {
    if (!isInConditionalBranch())
      return pushCleanupAfterFullExprWithActiveFlag<T>(Kind, Address::invalid(),
                                                       A...);

    Address ActiveFlag = createCleanupActiveFlag();
    assert(!DominatingValue<Address>::needsSaving(ActiveFlag) &&
           "cleanup active flag should never need saving");

    typedef std::tuple<typename DominatingValue<As>::saved_type...> SavedTuple;
    SavedTuple Saved{saveValueInCond(A)...};

    typedef EHScopeStack::ConditionalCleanup<T, As...> CleanupType;
    pushCleanupAfterFullExprWithActiveFlag<CleanupType>(Kind, ActiveFlag,
                                                        Saved);
  }

  template <class T, class... As>
  void pushCleanupAfterFullExprWithActiveFlag(CleanupKind Kind,
                                              Address ActiveFlag, As... A) {
    LifetimeExtendedCleanupHeader Header = {sizeof(T), Kind,
                                            ActiveFlag.isValid()};

    size_t OldSize = LifetimeExtendedCleanupStack.size();
    LifetimeExtendedCleanupStack.resize(
        LifetimeExtendedCleanupStack.size() + sizeof(Header) + Header.Size +
        (Header.IsConditional ? sizeof(ActiveFlag) : 0));

    static_assert(sizeof(Header) % alignof(T) == 0,
                  "Cleanup will be allocated on misaligned address");
    char *Buffer = &LifetimeExtendedCleanupStack[OldSize];
    new (Buffer) LifetimeExtendedCleanupHeader(Header);
    new (Buffer + sizeof(Header)) T(A...);
    if (Header.IsConditional)
      new (Buffer + sizeof(Header) + sizeof(T)) Address(ActiveFlag);
  }

  void initFullExprCleanup() {
    initFullExprCleanupWithFlag(createCleanupActiveFlag());
  }

  void initFullExprCleanupWithFlag(Address ActiveFlag);
  Address createCleanupActiveFlag();

  void popCleanupBlock(bool FallThroughIsBranchThrough = false);

  void deactivateCleanupBlock(EHScopeStack::stable_iterator Cleanup,
                              llvm::Instruction *DominatingIP);

  void activateCleanupBlock(EHScopeStack::stable_iterator Cleanup,
                            llvm::Instruction *DominatingIP);

  class RunCleanupsScope {
    EHScopeStack::stable_iterator CleanupStackDepth, OldCleanupScopeDepth;
    size_t LifetimeExtendedCleanupStackSize;
    bool OldDidCallStackSave;

  protected:
    bool PerformCleanup;

  private:
    RunCleanupsScope(const RunCleanupsScope &) = delete;
    void operator=(const RunCleanupsScope &) = delete;

  protected:
    FunctionEmitter &FE;

  public:
    /// Enter a new cleanup scope.
    explicit RunCleanupsScope(FunctionEmitter &FE)
        : PerformCleanup(true), FE(FE) {
      CleanupStackDepth = FE.EHStack.stable_begin();
      LifetimeExtendedCleanupStackSize = FE.LifetimeExtendedCleanupStack.size();
      OldDidCallStackSave = FE.DidCallStackSave;
      FE.DidCallStackSave = false;
      OldCleanupScopeDepth = FE.CurrentCleanupScopeDepth;
      FE.CurrentCleanupScopeDepth = CleanupStackDepth;
    }

    /// Exit this cleanup scope, emitting any accumulated cleanups.
    ~RunCleanupsScope() {
      if (PerformCleanup)
        ForceCleanup();
    }

    /// Determine whether this scope requires any cleanups.
    bool requiresCleanups() const {
      return FE.EHStack.stable_begin() != CleanupStackDepth;
    }

    /// Force the emission of cleanups now, instead of waiting
    /// until this object is destroyed.
    /// \param ValuesToReload - A list of values that need to be available at
    /// the insertion point after cleanup emission. If cleanup emission created
    /// a shared cleanup block, these value pointers will be rewritten.
    /// Otherwise, they not will be modified.
    void
    ForceCleanup(std::initializer_list<llvm::Value **> ValuesToReload = {}) {
      assert(PerformCleanup && "Already forced cleanup");
      FE.DidCallStackSave = OldDidCallStackSave;
      FE.popCleanupBlocks(CleanupStackDepth, LifetimeExtendedCleanupStackSize,
                          ValuesToReload);
      PerformCleanup = false;
      FE.CurrentCleanupScopeDepth = OldCleanupScopeDepth;
    }
  };

  // Cleanup stack depth of the RunCleanupsScope that was pushed most recently.
  EHScopeStack::stable_iterator CurrentCleanupScopeDepth =
      EHScopeStack::stable_end();

  class LexicalScope : public RunCleanupsScope {
    SourceRange Range;
    llvm::SmallVector<const LabelDecl *, 4> Labels;
    LexicalScope *ParentScope;

    LexicalScope(const LexicalScope &) = delete;
    void operator=(const LexicalScope &) = delete;

  public:
    /// Enter a new cleanup scope.
    explicit LexicalScope(FunctionEmitter &FE, SourceRange Range)
        : RunCleanupsScope(FE), Range(Range), ParentScope(FE.CurLexicalScope) {
      FE.CurLexicalScope = this;
      if (DebugEmitter *DI = FE.getDebugInfo())
        DI->genLexicalBlockStart(FE.Builder, Range.getBegin());
    }

    void addLabel(const LabelDecl *label) {
      assert(PerformCleanup && "adding label to dead scope?");
      Labels.push_back(label);
    }

    /// Exit this cleanup scope, emitting any accumulated
    /// cleanups.
    ~LexicalScope() {
      if (DebugEmitter *DI = FE.getDebugInfo())
        DI->genLexicalBlockEnd(FE.Builder, Range.getEnd());

      // If we should perform a cleanup, force them now.  Note that
      // this ends the cleanup scope before rescoping any labels.
      if (PerformCleanup) {
        ApplyDebugLocation DL(FE, Range.getEnd());
        ForceCleanup();
      }
    }

    /// Force the emission of cleanups now, instead of waiting
    /// until this object is destroyed.
    void ForceCleanup() {
      FE.CurLexicalScope = ParentScope;
      RunCleanupsScope::ForceCleanup();

      if (!Labels.empty())
        rescopeLabels();
    }

    bool hasLabels() const { return !Labels.empty(); }

    void rescopeLabels();
  };

  typedef llvm::DenseMap<const Decl *, Address> DeclMapTy;

  void
  popCleanupBlocks(EHScopeStack::stable_iterator OldCleanupStackSize,
                   std::initializer_list<llvm::Value **> ValuesToReload = {});

  void
  popCleanupBlocks(EHScopeStack::stable_iterator OldCleanupStackSize,
                   size_t OldLifetimeExtendedStackSize,
                   std::initializer_list<llvm::Value **> ValuesToReload = {});

  void resolveBranchFixups(llvm::BasicBlock *Target);

  JumpDest getJumpDestInCurrentScope(llvm::BasicBlock *Target) {
    return JumpDest(Target, EHStack.getInnermostNormalCleanup(),
                    NextCleanupDestIndex++);
  }

  JumpDest getJumpDestInCurrentScope(llvm::StringRef Name = llvm::StringRef()) {
    return getJumpDestInCurrentScope(createBasicBlock(Name));
  }

  void genBranchThroughCleanup(JumpDest Dest);

  bool isObviouslyBranchWithoutCleanups(JumpDest Dest) const;

  llvm::BasicBlock *getEHResumeBlock();
  llvm::BasicBlock *getEHDispatchBlock(EHScopeStack::stable_iterator scope);
  llvm::BasicBlock *
  getFuncletEHDispatchBlock(EHScopeStack::stable_iterator scope);

  class ConditionalEvaluation {
    llvm::BasicBlock *StartBB;

  public:
    ConditionalEvaluation(FunctionEmitter &FE)
        : StartBB(FE.Builder.GetInsertBlock()) {}

    void begin(FunctionEmitter &FE) {
      assert(FE.OutermostConditional != this);
      if (!FE.OutermostConditional)
        FE.OutermostConditional = this;
    }

    void end(FunctionEmitter &FE) {
      assert(FE.OutermostConditional != nullptr);
      if (FE.OutermostConditional == this)
        FE.OutermostConditional = nullptr;
    }

    /// Returns a block which will be executed prior to each
    /// evaluation of the conditional code.
    llvm::BasicBlock *getStartingBlock() const { return StartBB; }
  };

  bool isInConditionalBranch() const { return OutermostConditional != nullptr; }

  void setBeforeOutermostConditional(llvm::Value *value, Address addr) {
    assert(isInConditionalBranch());
    llvm::BasicBlock *block = OutermostConditional->getStartingBlock();
    auto store = new llvm::StoreInst(value, addr.getPointer(), &block->back());
    store->setAlignment(addr.getAlignment().getAsAlign());
  }

  class StmtExprEvaluation {
    FunctionEmitter &FE;

    /// We have to save the outermost conditional: cleanups in a
    /// statement expression aren't conditional just because the
    /// StmtExpr is.
    ConditionalEvaluation *SavedOutermostConditional;

  public:
    StmtExprEvaluation(FunctionEmitter &FE)
        : FE(FE), SavedOutermostConditional(FE.OutermostConditional) {
      FE.OutermostConditional = nullptr;
    }

    ~StmtExprEvaluation() {
      FE.OutermostConditional = SavedOutermostConditional;
      FE.ensureInsertPoint();
    }
  };

  class PeepholeProtection {
    llvm::Instruction *Inst = nullptr;
    friend class FunctionEmitter;

  public:
    PeepholeProtection() = default;
  };

  class OpaqueValueMappingData {
    const OpaqueValueExpr *OpaqueValue;
    bool BoundLValue;
    FunctionEmitter::PeepholeProtection Protection;

    OpaqueValueMappingData(const OpaqueValueExpr *ov, bool boundLValue)
        : OpaqueValue(ov), BoundLValue(boundLValue) {}

  public:
    OpaqueValueMappingData() : OpaqueValue(nullptr) {}

    static bool shouldBindAsLValue(const Expr *expr) {
      // gl-values should be bound as l-values for obvious reasons.
      // Records should be bound as l-values because IR generation
      // always keeps them in memory.  Expressions of function type
      // act exactly like l-values but are formally required to be
      // r-values in C.
      return expr->isLValue() || expr->getType()->isFunctionType() ||
             hasAggregateEvaluationKind(expr->getType());
    }

    static OpaqueValueMappingData
    bind(FunctionEmitter &FE, const OpaqueValueExpr *ov, const Expr *e) {
      if (shouldBindAsLValue(ov))
        return bind(FE, ov, FE.genLValue(e));
      return bind(FE, ov, FE.genAnyExpr(e));
    }

    static OpaqueValueMappingData
    bind(FunctionEmitter &FE, const OpaqueValueExpr *ov, const LValue &lv) {
      assert(shouldBindAsLValue(ov));
      FE.OpaqueLValues.insert(std::make_pair(ov, lv));
      return OpaqueValueMappingData(ov, true);
    }

    static OpaqueValueMappingData
    bind(FunctionEmitter &FE, const OpaqueValueExpr *ov, const RValue &rv) {
      assert(!shouldBindAsLValue(ov));
      FE.OpaqueRValues.insert(std::make_pair(ov, rv));

      OpaqueValueMappingData data(ov, false);

      // Work around an extremely aggressive peephole optimization in
      // genScalarConversion which assumes that all other uses of a
      // value are extant.
      data.Protection = FE.protectFromPeepholes(rv);

      return data;
    }

    bool isValid() const { return OpaqueValue != nullptr; }
    void clear() { OpaqueValue = nullptr; }

    void unbind(FunctionEmitter &FE) {
      assert(OpaqueValue && "no data to unbind!");

      if (BoundLValue) {
        FE.OpaqueLValues.erase(OpaqueValue);
      } else {
        FE.OpaqueRValues.erase(OpaqueValue);
        FE.unprotectFromPeepholes(Protection);
      }
    }
  };

  class OpaqueValueMapping {
    FunctionEmitter &FE;
    OpaqueValueMappingData Data;

  public:
    static bool shouldBindAsLValue(const Expr *expr) {
      return OpaqueValueMappingData::shouldBindAsLValue(expr);
    }

    /// Build the opaque value mapping for the given conditional
    /// operator if it's the GNU ?: extension.  This is a common
    /// enough pattern that the convenience operator is really
    /// helpful.
    OpaqueValueMapping(FunctionEmitter &FE,
                       const AbstractConditionalOperator *op)
        : FE(FE) {
      if (isa<ConditionalOperator>(op))
        // Leave Data empty.
        return;

      const BinaryConditionalOperator *e = cast<BinaryConditionalOperator>(op);
      Data =
          OpaqueValueMappingData::bind(FE, e->getOpaqueValue(), e->getCommon());
    }

    /// Build the opaque value mapping for an OpaqueValueExpr whose source
    /// expression is set to the expression the OVE represents.
    OpaqueValueMapping(FunctionEmitter &FE, const OpaqueValueExpr *OV)
        : FE(FE) {
      if (OV) {
        assert(OV->getSourceExpr() && "wrong form of OpaqueValueMapping used "
                                      "for OVE with no source expression");
        Data = OpaqueValueMappingData::bind(FE, OV, OV->getSourceExpr());
      }
    }

    OpaqueValueMapping(FunctionEmitter &FE, const OpaqueValueExpr *opaqueValue,
                       LValue lvalue)
        : FE(FE), Data(OpaqueValueMappingData::bind(FE, opaqueValue, lvalue)) {}

    OpaqueValueMapping(FunctionEmitter &FE, const OpaqueValueExpr *opaqueValue,
                       RValue rvalue)
        : FE(FE), Data(OpaqueValueMappingData::bind(FE, opaqueValue, rvalue)) {}

    void pop() {
      Data.unbind(FE);
      Data.clear();
    }

    ~OpaqueValueMapping() {
      if (Data.isValid())
        Data.unbind(FE);
    }
  };

private:
  DebugEmitter *DebugInfo;
  unsigned VLAExprCounter = 0;
  bool DisableDebugInfo = false;

  bool DidCallStackSave = false;

  llvm::IndirectBrInst *IndirectBranch = nullptr;

  DeclMapTy LocalDeclMap;

  llvm::SmallDenseMap<const ParmVarDecl *, const ImplicitParamDecl *, 2>
      SizeArguments;

  llvm::DenseMap<llvm::AllocaInst *, int> EscapedLocals;

  llvm::DenseMap<const LabelDecl *, JumpDest> LabelMap;

  // BreakContinueStack - This keeps track of where break and continue
  // statements should jump to.
  struct BreakContinue {
    BreakContinue(JumpDest Break, JumpDest Continue)
        : BreakBlock(Break), ContinueBlock(Continue) {}

    JumpDest BreakBlock;
    JumpDest ContinueBlock;
  };
  llvm::SmallVector<BreakContinue, 8> BreakContinueStack;

  llvm::Value *emitCondLikelihoodViaExpectIntrinsic(llvm::Value *Cond,
                                                    Stmt::Likelihood LH);

private:
  llvm::SwitchInst *SwitchInsn = nullptr;

  llvm::BasicBlock *CaseRangeBlock = nullptr;

  llvm::DenseMap<const OpaqueValueExpr *, LValue> OpaqueLValues;
  llvm::DenseMap<const OpaqueValueExpr *, RValue> OpaqueRValues;

  // VLASizeMap - This keeps track of the associated size for each VLA type.
  // We track this by the size expression rather than the type itself because
  // in certain situations, like a const qualifier applied to an VLA typedef,
  // multiple VLA types can share the same size expression.
  llvm::DenseMap<const Expr *, llvm::Value *> VLASizeMap;

  llvm::BasicBlock *UnreachableBlock = nullptr;

  unsigned NumReturnExprs = 0;

  unsigned NumSimpleReturnExprs = 0;

  SourceLocation LastStopPoint;

public:
  CurrentSourceLocExprScope CurSourceLocExprScope;
  using SourceLocExprScopeGuard =
      CurrentSourceLocExprScope::SourceLocExprScopeGuard;

  class ArrayInitLoopExprScope {
  public:
    ArrayInitLoopExprScope(FunctionEmitter &FE, llvm::Value *Index)
        : FE(FE), OldArrayInitIndex(FE.ArrayInitIndex) {
      FE.ArrayInitIndex = Index;
    }
    ~ArrayInitLoopExprScope() { FE.ArrayInitIndex = OldArrayInitIndex; }

  private:
    FunctionEmitter &FE;
    llvm::Value *OldArrayInitIndex;
  };

private:
  llvm::Value *ArrayInitIndex = nullptr;

  ConditionalEvaluation *OutermostConditional = nullptr;

  LexicalScope *CurLexicalScope = nullptr;

  SourceLocation CurEHLocation;

  llvm::BasicBlock *TerminateLandingPad = nullptr;
  llvm::BasicBlock *TerminateHandler = nullptr;
  llvm::SmallVector<llvm::BasicBlock *, 2> TrapBBs;

  llvm::MapVector<llvm::Value *, llvm::BasicBlock *> TerminateFunclets;

  unsigned LargestVectorWidth = 0;

  bool ShouldEmitLifetimeMarkers;

public:
  FunctionEmitter(ModuleEmitter &cgm, bool suppressNewContext = false);
  ~FunctionEmitter();

  TypeEmitter &getTypes() const { return ME.getTypes(); }
  TreeContext &getContext() const { return ME.getContext(); }
  DebugEmitter *getDebugInfo() {
    if (DisableDebugInfo)
      return nullptr;
    return DebugInfo;
  }
  void disableDebugInfo() { DisableDebugInfo = true; }
  void enableDebugInfo() { DisableDebugInfo = false; }

  const LangOptions &getLangOpts() const { return ME.getLangOpts(); }

  Address getExceptionSlot();
  Address getEHSelectorSlot();

  llvm::Value *getExceptionFromSlot();
  llvm::Value *getSelectorFromSlot();

  Address getNormalCleanupDestSlot();

  llvm::BasicBlock *getUnreachableBlock() {
    if (!UnreachableBlock) {
      UnreachableBlock = createBasicBlock("unreachable");
      new llvm::UnreachableInst(getLLVMContext(), UnreachableBlock);
    }
    return UnreachableBlock;
  }

  llvm::BasicBlock *getInvokeDest() {
    if (!EHStack.requiresLandingPad())
      return nullptr;
    return getInvokeDestImpl();
  }

  bool currentFunctionUsesSEHTry() const { return !!CurSEHParent; }

  const TargetInfo &getTarget() const { return Target; }
  llvm::LLVMContext &getLLVMContext() { return ME.getLLVMContext(); }
  const TargetCodeGenInfo &getTargetHooks() const {
    return ME.getTargetCodeGenInfo();
  }

  //===--------------------------------------------------------------------===//
  //                                  Cleanups
  //===--------------------------------------------------------------------===//

  void pushStackRestore(CleanupKind kind, Address SPMem);

  bool needsEHCleanup(QualType::DestructionKind kind) {
    switch (kind) {
    case QualType::DK_none:
    case QualType::DK_nontrivial_c_struct:
      return false;
    }
    llvm_unreachable("bad destruction kind");
  }

  CleanupKind getCleanupKind(QualType::DestructionKind kind) {
    return (needsEHCleanup(kind) ? NormalAndEHCleanup : NormalCleanup);
  }

  class AutoVarEmission;

  QualType formFunctionArgList(GlobalDecl GD, FunctionArgList &Args);

  void generateCode(GlobalDecl GD, llvm::Function *Fn,
                    const ABIFunctionInfo &FnInfo);

  void startFunction(GlobalDecl GD, QualType RetTy, llvm::Function *Fn,
                     const ABIFunctionInfo &FnInfo, const FunctionArgList &Args,
                     SourceLocation Loc = SourceLocation(),
                     SourceLocation StartLoc = SourceLocation());

  void genFunctionBody(const Stmt *Body);

  llvm::DebugLoc genReturnBlock();

  void finishFunction(SourceLocation EndLoc = SourceLocation());

  void genFunctionProlog(const ABIFunctionInfo &FI, llvm::Function *Fn,
                         const FunctionArgList &Args);

  void genFunctionEpilog(const ABIFunctionInfo &FI, bool genRetDbgLoc,
                         SourceLocation EndLoc);

  llvm::BasicBlock *getTerminateLandingPad();

  llvm::BasicBlock *getTerminateFunclet();

  llvm::BasicBlock *getTerminateHandler();

  llvm::Type *convertTypeForMem(QualType T);
  llvm::Type *convertType(QualType T);
  llvm::Type *convertType(const TypeDecl *T) {
    return convertType(getContext().getTypeDeclType(T));
  }

  static TypeEvaluationKind getEvaluationKind(QualType T);

  static bool hasScalarEvaluationKind(QualType T) {
    return getEvaluationKind(T) == TEK_Scalar;
  }

  static bool hasAggregateEvaluationKind(QualType T) {
    return getEvaluationKind(T) == TEK_Aggregate;
  }

  llvm::BasicBlock *createBasicBlock(const llvm::Twine &name = "",
                                     llvm::Function *parent = nullptr,
                                     llvm::BasicBlock *before = nullptr) {
    return llvm::BasicBlock::Create(getLLVMContext(), name, parent, before);
  }

  JumpDest getJumpDestForLabel(const LabelDecl *S);

  void simplifyForwardingBlocks(llvm::BasicBlock *BB);

  void genBlock(llvm::BasicBlock *BB, bool IsFinished = false);

  void genBlockAfterUses(llvm::BasicBlock *BB);

  void genBranch(llvm::BasicBlock *Block);

  bool haveInsertPoint() const { return Builder.GetInsertBlock() != nullptr; }

  void ensureInsertPoint() {
    if (!haveInsertPoint())
      genBlock(createBasicBlock());
  }

  void errorUnsupported(const Stmt *S, const char *Type);

  //===--------------------------------------------------------------------===//
  //                                  Helpers
  //===--------------------------------------------------------------------===//

  LValue makeAddrLValue(Address Addr, QualType T,
                        AlignmentSource Source = AlignmentSource::Type) {
    return LValue::MakeAddr(Addr, T, getContext(), LValueBaseInfo(Source),
                            ME.getTBAAAccessInfo(T));
  }

  LValue makeAddrLValue(Address Addr, QualType T, LValueBaseInfo BaseInfo,
                        TBAAAccessInfo TBAAInfo) {
    return LValue::MakeAddr(Addr, T, getContext(), BaseInfo, TBAAInfo);
  }

  LValue makeAddrLValue(llvm::Value *V, QualType T, CharUnits Alignment,
                        AlignmentSource Source = AlignmentSource::Type) {
    Address Addr(V, convertTypeForMem(T), Alignment);
    return LValue::MakeAddr(Addr, T, getContext(), LValueBaseInfo(Source),
                            ME.getTBAAAccessInfo(T));
  }

  LValue
  makeAddrLValueWithoutTBAA(Address Addr, QualType T,
                            AlignmentSource Source = AlignmentSource::Type) {
    return LValue::MakeAddr(Addr, T, getContext(), LValueBaseInfo(Source),
                            TBAAAccessInfo());
  }

  LValue makeNaturalAlignPointeeAddrLValue(llvm::Value *V, QualType T);
  LValue makeNaturalAlignAddrLValue(llvm::Value *V, QualType T);

  Address genLoadOfPointer(Address Ptr, const PointerType *PtrTy,
                           LValueBaseInfo *BaseInfo = nullptr,
                           TBAAAccessInfo *TBAAInfo = nullptr);

  llvm::AllocaInst *createTempAlloca(llvm::Type *Ty,
                                     const llvm::Twine &Name = "tmp",
                                     llvm::Value *ArraySize = nullptr);
  Address createTempAlloca(llvm::Type *Ty, CharUnits align,
                           const llvm::Twine &Name = "tmp",
                           llvm::Value *ArraySize = nullptr,
                           Address *Alloca = nullptr);
  Address createTempAllocaWithoutCast(llvm::Type *Ty, CharUnits align,
                                      const llvm::Twine &Name = "tmp",
                                      llvm::Value *ArraySize = nullptr);

  Address createDefaultAlignTempAlloca(llvm::Type *Ty,
                                       const llvm::Twine &Name = "tmp");

  Address createIRTemp(QualType T, const llvm::Twine &Name = "tmp");

  Address createMemTemp(QualType T, const llvm::Twine &Name = "tmp",
                        Address *Alloca = nullptr);
  Address createMemTemp(QualType T, CharUnits Align,
                        const llvm::Twine &Name = "tmp",
                        Address *Alloca = nullptr);

  Address createMemTempWithoutCast(QualType T, const llvm::Twine &Name = "tmp");
  Address createMemTempWithoutCast(QualType T, CharUnits Align,
                                   const llvm::Twine &Name = "tmp");

  AggValueSlot createAggTemp(QualType T, const llvm::Twine &Name = "tmp",
                             Address *Alloca = nullptr) {
    return AggValueSlot::forAddr(
        createMemTemp(T, Name, Alloca), T.getQualifiers(),
        AggValueSlot::IsNotDestructed, AggValueSlot::IsNotAliased,
        AggValueSlot::DoesNotOverlap);
  }

  llvm::Value *evaluateExprAsBool(const Expr *E);

  void genIgnoredExpr(const Expr *E);

  RValue genAnyExpr(const Expr *E,
                    AggValueSlot aggSlot = AggValueSlot::ignored(),
                    bool ignoreResult = false);

  // genVAListRef - Emit a "reference" to a va_list; this is either the address
  // or the value of the expression, depending on how va_list is defined.
  Address genVAListRef(const Expr *E);

  Address genMSVAListRef(const Expr *E);

  RValue genAnyExprToTemp(const Expr *E);

  void genAnyExprToMem(const Expr *E, Address Location, Qualifiers Quals,
                       bool IsInitializer);

  void genExprAsInit(const Expr *init, const ValueDecl *D, LValue lvalue,
                     bool capturedByInit);

  bool hasVolatileMember(QualType T) {
    if (const RecordType *RT = T->getAs<RecordType>()) {
      const RecordDecl *RD = cast<RecordDecl>(RT->getDecl());
      return RD->hasVolatileMember();
    }
    return false;
  }

  AggValueSlot::Overlap_t getOverlapForReturnValue() {
    return AggValueSlot::DoesNotOverlap;
  }

  AggValueSlot::Overlap_t getOverlapForFieldInit(const FieldDecl *FD);

  void genAggregateCopy(LValue Dest, LValue Src, QualType EltTy,
                        AggValueSlot::Overlap_t MayOverlap,
                        bool isVolatile = false);

  Address addrOfLocalVar(const VarDecl *VD) {
    auto it = LocalDeclMap.find(VD);
    assert(it != LocalDeclMap.end() &&
           "Invalid argument to addrOfLocalVar(), no decl!");
    return it->second;
  }

  LValue getOrCreateOpaqueLValueMapping(const OpaqueValueExpr *e);

  RValue getOrCreateOpaqueRValueMapping(const OpaqueValueExpr *e);

  llvm::Value *getArrayInitIndex() { return ArrayInitIndex; }

  static unsigned getAccessedFieldNo(unsigned Idx, const llvm::Constant *Elts);

  llvm::BlockAddress *addrOfLabel(const LabelDecl *L);
  llvm::BasicBlock *getIndirectGotoBlock();

  void genNullInitialization(Address DestPtr, QualType Ty);

  llvm::Value *genVAStartEnd(llvm::Value *ArgValue, bool IsStart);

  Address genVAArg(VAArgExpr *VE, Address &VAListAddr);

  llvm::Value *emitArrayLength(const ArrayType *arrayType, QualType &baseType,
                               Address &addr);

  void genVariablyModifiedType(QualType Ty);

  struct VlaSizePair {
    llvm::Value *NumElts;
    QualType Type;

    VlaSizePair(llvm::Value *NE, QualType T) : NumElts(NE), Type(T) {}
  };

  VlaSizePair getVLAElements1D(const VariableArrayType *vla);
  VlaSizePair getVLAElements1D(QualType vla);

  VlaSizePair getVLASize(const VariableArrayType *vla);
  VlaSizePair getVLASize(QualType vla);

  llvm::InvokeInst *genSehTryScopeBegin();
  llvm::InvokeInst *genSehTryScopeEnd();

  llvm::Value *genLifetimeStart(llvm::TypeSize Size, llvm::Value *Addr);
  void genLifetimeEnd(llvm::Value *Size, llvm::Value *Addr);

  enum TypeCheckKind {
    /// Checking the operand of a load. Must be suitably sized and aligned.
    TCK_Load,
    /// Checking the destination of a store. Must be suitably sized and aligned.
    TCK_Store,
    /// Checking the object expression in a field access. Must
    /// be an object within its lifetime.
    TCK_MemberAccess,
    /// Checking the value assigned to a _Nonnull pointer. Must not be null.
    TCK_NonnullAssign
  };

  llvm::Value *genScalarPrePostIncDec(const UnaryOperator *E, LValue LV,
                                      bool isInc, bool isPre);
  ComplexPairTy genComplexPrePostIncDec(const UnaryOperator *E, LValue LV,
                                        bool isInc, bool isPre);

  llvm::DebugLoc sourceLocToDebugLoc(SourceLocation Location);

  unsigned getDebugInfoFIndex(const RecordDecl *Rec, unsigned FieldIndex);

  //===--------------------------------------------------------------------===//
  //                            Declaration Emission
  //===--------------------------------------------------------------------===//

  void genDecl(const Decl &D);

  void genVarDecl(const VarDecl &D);

  void genScalarInit(const Expr *init, const ValueDecl *D, LValue lvalue,
                     bool capturedByInit);

  typedef void SpecialInitFn(FunctionEmitter &Init, const VarDecl &D,
                             llvm::Value *Address);

  bool isTrivialInitializer(const Expr *Init);

  void genAutoVarDecl(const VarDecl &D);

  class AutoVarEmission {
    friend class FunctionEmitter;

    const VarDecl *Variable;

    /// The address of the alloca (or alloca cast to generic pointer for address
    /// space agnostic languages). Invalid if the variable was emitted as a
    /// global constant.
    Address Addr;

    /// True if the variable is of aggregate type and has a constant
    /// initializer.
    bool IsConstantAggregate;

    /// Non-null if we should use lifetime annotations.
    llvm::Value *SizeForLifetimeMarkers;

    /// Address with original alloca instruction. Invalid if the variable was
    /// emitted as a global constant.
    Address AllocaAddr;

    struct Invalid {};
    AutoVarEmission(Invalid)
        : Variable(nullptr), Addr(Address::invalid()),
          AllocaAddr(Address::invalid()) {}

    AutoVarEmission(const VarDecl &variable)
        : Variable(&variable), Addr(Address::invalid()),
          IsConstantAggregate(false), SizeForLifetimeMarkers(nullptr),
          AllocaAddr(Address::invalid()) {}

    bool wasEmittedAsGlobal() const { return !Addr.isValid(); }

  public:
    static AutoVarEmission invalid() { return AutoVarEmission(Invalid()); }

    bool useLifetimeMarkers() const {
      return SizeForLifetimeMarkers != nullptr;
    }
    llvm::Value *getSizeForLifetimeMarkers() const {
      assert(useLifetimeMarkers());
      return SizeForLifetimeMarkers;
    }

    /// Returns the raw, allocated address, which is not necessarily
    /// the address of the object itself. It is casted to default
    /// address space for address space agnostic languages.
    Address getAllocatedAddress() const { return Addr; }

    /// Returns the address for the original alloca instruction.
    Address getOriginalAllocatedAddress() const { return AllocaAddr; }

    Address getObjectAddress(FunctionEmitter &) const { return Addr; }
  };
  AutoVarEmission genAutoVarAlloca(const VarDecl &var);
  void genAutoVarInit(const AutoVarEmission &emission);
  void genAutoVarCleanups(const AutoVarEmission &emission);

  void genAndRegisterVariableArrayDimensions(DebugEmitter *DI, const VarDecl &D,
                                             bool genDebugInfo);

  void genStaticVarDecl(const VarDecl &D,
                        llvm::GlobalValue::LinkageTypes Linkage);

  class ParamValue {
    llvm::Value *Value;
    llvm::Type *ElementType;
    unsigned Alignment;
    ParamValue(llvm::Value *V, llvm::Type *T, unsigned A)
        : Value(V), ElementType(T), Alignment(A) {}

  public:
    static ParamValue forDirect(llvm::Value *value) {
      return ParamValue(value, nullptr, 0);
    }
    static ParamValue forIndirect(Address addr) {
      assert(!addr.getAlignment().isZero());
      return ParamValue(addr.getPointer(), addr.getElementType(),
                        addr.getAlignment().getQuantity());
    }

    bool isIndirect() const { return Alignment != 0; }
    llvm::Value *getAnyValue() const { return Value; }

    llvm::Value *getDirectValue() const {
      assert(!isIndirect());
      return Value;
    }

    Address getIndirectAddress() const {
      assert(isIndirect());
      return Address(Value, ElementType, CharUnits::fromQuantity(Alignment),
                     KnownNonNull);
    }
  };

  void genParmDecl(const VarDecl &D, ParamValue Arg, unsigned ArgNo);

  PeepholeProtection protectFromPeepholes(RValue rvalue);
  void unprotectFromPeepholes(PeepholeProtection protection);

  void emitAlignmentAssumption(llvm::Value *PtrValue, QualType Ty,
                               llvm::Value *Alignment,
                               llvm::Value *OffsetValue = nullptr);

  void emitAlignmentAssumption(llvm::Value *PtrValue, const Expr *E,
                               llvm::Value *Alignment,
                               llvm::Value *OffsetValue = nullptr);

  //===--------------------------------------------------------------------===//
  //                             Statement Emission
  //===--------------------------------------------------------------------===//

  void genStopPoint(const Stmt *S);

  void genStmt(const Stmt *S,
               llvm::ArrayRef<const Attr *> Attrs = std::nullopt);

  bool genSimpleStmt(const Stmt *S, llvm::ArrayRef<const Attr *> Attrs);

  Address genCompoundStmt(const CompoundStmt &S, bool GetLast = false,
                          AggValueSlot AVS = AggValueSlot::ignored());
  Address
  genCompoundStmtWithoutScope(const CompoundStmt &S, bool GetLast = false,
                              AggValueSlot AVS = AggValueSlot::ignored());

  void genLabel(const LabelDecl *D); // helper for genLabelStmt.

  void genLabelStmt(const LabelStmt &S);
  void genAttributedStmt(const AttributedStmt &S);
  void genGotoStmt(const GotoStmt &S);
  void genIndirectGotoStmt(const IndirectGotoStmt &S);
  void genIfStmt(const IfStmt &S);

  void genWhileStmt(const WhileStmt &S,
                    llvm::ArrayRef<const Attr *> Attrs = std::nullopt);
  void genDoStmt(const DoStmt &S,
                 llvm::ArrayRef<const Attr *> Attrs = std::nullopt);
  void genForStmt(const ForStmt &S,
                  llvm::ArrayRef<const Attr *> Attrs = std::nullopt);
  void genReturnStmt(const ReturnStmt &S);
  void genDeclStmt(const DeclStmt &S);
  void genBreakStmt(const BreakStmt &S);
  void genContinueStmt(const ContinueStmt &S);
  void genSwitchStmt(const SwitchStmt &S);
  void genDefaultStmt(const DefaultStmt &S, llvm::ArrayRef<const Attr *> Attrs);
  void genCaseStmt(const CaseStmt &S, llvm::ArrayRef<const Attr *> Attrs);
  void genCaseStmtRange(const CaseStmt &S, llvm::ArrayRef<const Attr *> Attrs);
  void genAsmStmt(const AsmStmt &S);

  void genSEHTryStmt(const SEHTryStmt &S);
  void fixSEHEnd(llvm::InvokeInst *InvokeIst);
  void genSEHLeaveStmt(const SEHLeaveStmt &S);
  void enterSEHTryStmt(const SEHTryStmt &S, bool &ContainsRetStmt);
  void exitSEHTryStmt(const SEHTryStmt &S, bool ContainsRetStmt);
  void volatilizeTryBlocks(llvm::BasicBlock *BB,
                           llvm::SmallPtrSet<llvm::BasicBlock *, 10> &V);
  void genSEHLocalUnwind();
  llvm::Function *generateSEHIsLocalUnwindFunction();

  void pushSEHCleanup(CleanupKind kind, llvm::Function *FinallyFunc);
  void startOutlinedSEHHelper(FunctionEmitter &ParentFnEmitter, bool IsFilter,
                              const Stmt *OutlinedStmt);

  llvm::Function *generateSEHFilterFunction(FunctionEmitter &ParentFnEmitter,
                                            const SEHExceptStmt &Except);

  llvm::Function *generateSEHFinallyFunction(FunctionEmitter &ParentFnEmitter,
                                             const SEHFinallyStmt &Finally);

  void genSEHExceptionCodeSave(FunctionEmitter &ParentFnEmitter,
                               llvm::Value *ParentFP, llvm::Value *EntryFP);
  llvm::Value *genSEHExceptionCode();
  llvm::Value *genSEHExceptionInfo();
  llvm::Value *genSEHAbnormalTermination();

  void genCapturedLocals(FunctionEmitter &ParentFnEmitter,
                         const Stmt *OutlinedStmt, bool IsFilter);

  Address recoverAddrOfEscapedLocal(FunctionEmitter &ParentFnEmitter,
                                    Address ParentVar, llvm::Value *ParentFP);

public:
  //===--------------------------------------------------------------------===//
  //                         LValue Expression Emission
  //===--------------------------------------------------------------------===//

  RValue getUndefRValue(QualType Ty);

  RValue genUnsupportedRValue(const Expr *E, const char *Name);

  LValue genUnsupportedLValue(const Expr *E, const char *Name);

  LValue genLValue(const Expr *E,
                   KnownNonNull_t IsKnownNonNull = NotKnownNonNull);

private:
  LValue genLValueHelper(const Expr *E, KnownNonNull_t IsKnownNonNull);

public:
  LValue genCheckedLValue(const Expr *E, TypeCheckKind TCK);

  RValue convertTempToRValue(Address addr, QualType type, SourceLocation Loc);

  void genAtomicInit(Expr *E, LValue lvalue);

  bool lValueIsSuitableForInlineAtomic(LValue Src);

  RValue genAtomicLoad(LValue LV, SourceLocation SL,
                       AggValueSlot Slot = AggValueSlot::ignored());

  RValue genAtomicLoad(LValue lvalue, SourceLocation loc,
                       llvm::AtomicOrdering AO, bool IsVolatile = false,
                       AggValueSlot slot = AggValueSlot::ignored());

  void genAtomicStore(RValue rvalue, LValue lvalue, bool isInit);

  void genAtomicStore(RValue rvalue, LValue lvalue, llvm::AtomicOrdering AO,
                      bool IsVolatile, bool isInit);

  std::pair<RValue, llvm::Value *> genAtomicCompareExchange(
      LValue Obj, RValue Expected, RValue Desired, SourceLocation Loc,
      llvm::AtomicOrdering Success =
          llvm::AtomicOrdering::SequentiallyConsistent,
      llvm::AtomicOrdering Failure =
          llvm::AtomicOrdering::SequentiallyConsistent,
      bool IsWeak = false, AggValueSlot Slot = AggValueSlot::ignored());

  void genAtomicUpdate(LValue LVal, llvm::AtomicOrdering AO,
                       const llvm::function_ref<RValue(RValue)> &UpdateOp,
                       bool IsVolatile);

  llvm::Value *genToMemory(llvm::Value *Value, QualType Ty);

  llvm::Value *genFromMemory(llvm::Value *Value, QualType Ty);

  llvm::Value *genLoadOfScalar(Address Addr, bool Volatile, QualType Ty,
                               SourceLocation Loc,
                               AlignmentSource Source = AlignmentSource::Type,
                               bool isNontemporal = false) {
    return genLoadOfScalar(Addr, Volatile, Ty, Loc, LValueBaseInfo(Source),
                           ME.getTBAAAccessInfo(Ty), isNontemporal);
  }

  llvm::Value *genLoadOfScalar(Address Addr, bool Volatile, QualType Ty,
                               SourceLocation Loc, LValueBaseInfo BaseInfo,
                               TBAAAccessInfo TBAAInfo,
                               bool isNontemporal = false);

  llvm::Value *genLoadOfScalar(LValue lvalue, SourceLocation Loc);

  void genStoreOfScalar(llvm::Value *Value, Address Addr, bool Volatile,
                        QualType Ty,
                        AlignmentSource Source = AlignmentSource::Type,
                        bool isInit = false, bool isNontemporal = false) {
    genStoreOfScalar(Value, Addr, Volatile, Ty, LValueBaseInfo(Source),
                     ME.getTBAAAccessInfo(Ty), isInit, isNontemporal);
  }

  void genStoreOfScalar(llvm::Value *Value, Address Addr, bool Volatile,
                        QualType Ty, LValueBaseInfo BaseInfo,
                        TBAAAccessInfo TBAAInfo, bool isInit = false,
                        bool isNontemporal = false);

  void genStoreOfScalar(llvm::Value *value, LValue lvalue, bool isInit = false);

  RValue genLoadOfLValue(LValue V, SourceLocation Loc);
  RValue genLoadOfExtVectorElementLValue(LValue V);
  RValue genLoadOfBitfieldLValue(LValue LV, SourceLocation Loc);
  RValue genLoadOfGlobalRegLValue(LValue LV);

  void genStoreThroughLValue(RValue Src, LValue Dst, bool isInit = false);
  void genStoreThroughExtVectorComponentLValue(RValue Src, LValue Dst);
  void genStoreThroughGlobalRegLValue(RValue Src, LValue Dst);

  void genStoreThroughBitfieldLValue(RValue Src, LValue Dst,
                                     llvm::Value **Result = nullptr);

  LValue genComplexAssignmentLValue(const BinaryOperator *E);
  LValue genComplexCompoundAssignmentLValue(const CompoundAssignOperator *E);
  LValue genScalarCompoundAssignWithComplex(const CompoundAssignOperator *E,
                                            llvm::Value *&Result);

  // Note: only available for agg return types
  LValue genBinaryOperatorLValue(const BinaryOperator *E);
  LValue genCompoundAssignmentLValue(const CompoundAssignOperator *E);
  // Note: only available for agg return types
  LValue genCallExprLValue(const CallExpr *E);
  // Note: only available for agg return types
  LValue genVAArgExprLValue(const VAArgExpr *E);
  LValue genDeclRefLValue(const DeclRefExpr *E);
  LValue genStringLiteralLValue(const StringLiteral *E);
  LValue genPredefinedLValue(const PredefinedExpr *E);
  LValue genUnaryOpLValue(const UnaryOperator *E);
  LValue genArraySubscriptExpr(const ArraySubscriptExpr *E,
                               bool Accessed = false);
  LValue genMatrixSubscriptExpr(const MatrixSubscriptExpr *E);
  LValue genExtVectorElementExpr(const ExtVectorElementExpr *E);
  LValue genMemberExpr(const MemberExpr *E);
  LValue genCompoundLiteralLValue(const CompoundLiteralExpr *E);
  LValue genInitListLValue(const InitListExpr *E);
  void genIgnoredConditionalOperator(const AbstractConditionalOperator *E);
  LValue genConditionalOperatorLValue(const AbstractConditionalOperator *E);
  LValue genCastLValue(const CastExpr *E);
  LValue genOpaqueValueLValue(const OpaqueValueExpr *e);

  Address genExtVectorElementLValue(LValue V);

  RValue genRValueForField(LValue LV, const FieldDecl *FD, SourceLocation Loc);

  Address genArrayToPointerDecay(const Expr *Array,
                                 LValueBaseInfo *BaseInfo = nullptr,
                                 TBAAAccessInfo *TBAAInfo = nullptr);

  class ConstantEmission {
    llvm::Constant *Value = nullptr;

  public:
    ConstantEmission() = default;
    explicit ConstantEmission(llvm::Constant *C) : Value(C) {}
    static ConstantEmission forValue(llvm::Constant *C) {
      return ConstantEmission(C);
    }

    explicit operator bool() const { return Value != nullptr; }

    llvm::Constant *getValue() const {
      assert(Value && "not a constant");
      return Value;
    }
  };

  ConstantEmission tryEmitAsConstant(DeclRefExpr *refExpr);
  ConstantEmission tryEmitAsConstant(const MemberExpr *ME);
  llvm::Value *emitScalarConstant(const ConstantEmission &Constant);

  RValue genPseudoObjectRValue(const PseudoObjectExpr *e,
                               AggValueSlot slot = AggValueSlot::ignored());
  LValue genPseudoObjectLValue(const PseudoObjectExpr *e);

  LValue genLValueForField(LValue Base, const FieldDecl *Field);

  LValue genLValueForFieldInitialization(LValue Base, const FieldDecl *Field);

  LValue genStmtExprLValue(const StmtExpr *E);
  void genDeclRefExprDbgValue(const DeclRefExpr *E, const APValue &Init);

  //===--------------------------------------------------------------------===//
  //                         Scalar Expression Emission
  //===--------------------------------------------------------------------===//

  RValue genCall(const ABIFunctionInfo &CallInfo, const FnCallee &Callee,
                 ReturnValueSlot ReturnValue, const CallArgList &Args,
                 llvm::CallBase **callOrInvoke, bool IsMustTail,
                 SourceLocation Loc);
  RValue genCall(const ABIFunctionInfo &CallInfo, const FnCallee &Callee,
                 ReturnValueSlot ReturnValue, const CallArgList &Args,
                 llvm::CallBase **callOrInvoke = nullptr,
                 bool IsMustTail = false) {
    return genCall(CallInfo, Callee, ReturnValue, Args, callOrInvoke,
                   IsMustTail, SourceLocation());
  }
  RValue genCall(QualType FnType, const FnCallee &Callee, const CallExpr *E,
                 ReturnValueSlot ReturnValue, llvm::Value *Chain = nullptr);
  RValue genCallExpr(const CallExpr *E,
                     ReturnValueSlot ReturnValue = ReturnValueSlot());
  FnCallee genCallee(const Expr *E);

  void checkTargetFeatures(const CallExpr *E, const FunctionDecl *TargetDecl);
  void checkTargetFeatures(SourceLocation Loc, const FunctionDecl *TargetDecl);

  llvm::CallInst *genRuntimeCall(llvm::FunctionCallee callee,
                                 const llvm::Twine &name = "");
  llvm::CallInst *genRuntimeCall(llvm::FunctionCallee callee,
                                 llvm::ArrayRef<llvm::Value *> args,
                                 const llvm::Twine &name = "");
  llvm::CallInst *genNounwindRuntimeCall(llvm::FunctionCallee callee,
                                         const llvm::Twine &name = "");
  llvm::CallInst *genNounwindRuntimeCall(llvm::FunctionCallee callee,
                                         llvm::ArrayRef<llvm::Value *> args,
                                         const llvm::Twine &name = "");

  llvm::SmallVector<llvm::OperandBundleDef, 1>
  getBundlesForFunclet(llvm::Value *Callee);

  llvm::CallBase *genCallOrInvoke(llvm::FunctionCallee Callee,
                                  llvm::ArrayRef<llvm::Value *> Args,
                                  const llvm::Twine &Name = "");
  llvm::CallBase *genRuntimeCallOrInvoke(llvm::FunctionCallee callee,
                                         llvm::ArrayRef<llvm::Value *> args,
                                         const llvm::Twine &name = "");
  llvm::CallBase *genRuntimeCallOrInvoke(llvm::FunctionCallee callee,
                                         const llvm::Twine &name = "");
  void genNoreturnRuntimeCallOrInvoke(llvm::FunctionCallee callee,
                                      llvm::ArrayRef<llvm::Value *> args);

  RValue genBuiltinExpr(const GlobalDecl GD, unsigned BuiltinID,
                        const CallExpr *E, ReturnValueSlot ReturnValue);

  RValue emitRotate(const CallExpr *E, bool IsRotateRight);

  RValue emitBuiltinOSLogFormat(const CallExpr &E);

  RValue genBuiltinIsAligned(const CallExpr *E);
  RValue genBuiltinAlignTo(const CallExpr *E, bool AlignUp);

  llvm::Function *generateBuiltinOSLogHelperFunction(
      const analyze_os_log::OSLogBufferLayout &Layout,
      CharUnits BufferAlignment);

  llvm::Value *genTargetBuiltinExpr(unsigned BuiltinID, const CallExpr *E,
                                    ReturnValueSlot ReturnValue);

  llvm::Value *genAArch64CompareBuiltinExpr(llvm::Value *Op, llvm::Type *Ty,
                                            const llvm::CmpInst::Predicate Fp,
                                            const llvm::CmpInst::Predicate Ip,
                                            const llvm::Twine &Name = "");
  llvm::Value *genCommonNeonBuiltinExpr(
      unsigned BuiltinID, unsigned LLVMIntrinsic, unsigned AltLLVMIntrinsic,
      const char *NameHint, unsigned Modifier, const CallExpr *E,
      llvm::SmallVectorImpl<llvm::Value *> &Ops, Address PtrOp0, Address PtrOp1,
      llvm::Triple::ArchType Arch);

  llvm::Function *lookupNeonLLVMIntrinsic(unsigned IntrinsicID,
                                          unsigned Modifier, llvm::Type *ArgTy,
                                          const CallExpr *E);
  llvm::Value *genNeonCall(llvm::Function *F,
                           llvm::SmallVectorImpl<llvm::Value *> &O,
                           const char *name, unsigned shift = 0,
                           bool rightshift = false);
  llvm::Value *genNeonSplat(llvm::Value *V, llvm::Constant *Idx,
                            const llvm::ElementCount &Count);
  llvm::Value *genNeonSplat(llvm::Value *V, llvm::Constant *Idx);
  llvm::Value *genNeonShiftVector(llvm::Value *V, llvm::Type *Ty,
                                  bool negateForRightShift);
  llvm::Value *genNeonRShiftImm(llvm::Value *Vec, llvm::Value *Amt,
                                llvm::Type *Ty, bool usgn, const char *name);
  llvm::Value *vectorWrapScalar16(llvm::Value *Op);
  llvm::Type *sveBuiltinMemEltTy(const SVETypeFlags &TypeFlags);

  llvm::SmallVector<llvm::Type *, 2>
  getSVEOverloadTypes(const SVETypeFlags &TypeFlags, llvm::Type *ReturnType,
                      llvm::ArrayRef<llvm::Value *> Ops);
  llvm::Type *getEltType(const SVETypeFlags &TypeFlags);
  llvm::ScalableVectorType *getSVEType(const SVETypeFlags &TypeFlags);
  llvm::ScalableVectorType *getSVEPredType(const SVETypeFlags &TypeFlags);
  llvm::Value *genSVETupleSetOrGet(const SVETypeFlags &TypeFlags,
                                   llvm::Type *ReturnType,
                                   llvm::ArrayRef<llvm::Value *> Ops);
  llvm::Value *genSVETupleCreate(const SVETypeFlags &TypeFlags,
                                 llvm::Type *ReturnType,
                                 llvm::ArrayRef<llvm::Value *> Ops);
  llvm::Value *genSVEAllTruePred(const SVETypeFlags &TypeFlags);
  llvm::Value *genSVEDupX(llvm::Value *Scalar);
  llvm::Value *genSVEDupX(llvm::Value *Scalar, llvm::Type *Ty);
  llvm::Value *genSVEReinterpret(llvm::Value *Val, llvm::Type *Ty);
  llvm::Value *genSVEPMull(const SVETypeFlags &TypeFlags,
                           llvm::SmallVectorImpl<llvm::Value *> &Ops,
                           unsigned BuiltinID);
  llvm::Value *genSVEMovl(const SVETypeFlags &TypeFlags,
                          llvm::ArrayRef<llvm::Value *> Ops,
                          unsigned BuiltinID);
  llvm::Value *genSVEPredicateCast(llvm::Value *Pred,
                                   llvm::ScalableVectorType *VTy);
  llvm::Value *genSVEGatherLoad(const SVETypeFlags &TypeFlags,
                                llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                unsigned IntID);
  llvm::Value *genSVEScatterStore(const SVETypeFlags &TypeFlags,
                                  llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                  unsigned IntID);
  llvm::Value *genSVEMaskedLoad(const CallExpr *, llvm::Type *ReturnTy,
                                llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                unsigned BuiltinID, bool IsZExtReturn);
  llvm::Value *genSVEMaskedStore(const CallExpr *,
                                 llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                 unsigned BuiltinID);
  llvm::Value *genSVEPrefetchLoad(const SVETypeFlags &TypeFlags,
                                  llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                  unsigned BuiltinID);
  llvm::Value *genSVEGatherPrefetch(const SVETypeFlags &TypeFlags,
                                    llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                    unsigned IntID);
  llvm::Value *genSVEStructLoad(const SVETypeFlags &TypeFlags,
                                llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                unsigned IntID);
  llvm::Value *genSVEStructStore(const SVETypeFlags &TypeFlags,
                                 llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                 unsigned IntID);
  llvm::Value *formSVEBuiltinResult(llvm::Value *Call);

  llvm::Value *genAArch64SVEBuiltinExpr(unsigned BuiltinID, const CallExpr *E);

  llvm::Value *genSMELd1St1(const SVETypeFlags &TypeFlags,
                            llvm::SmallVectorImpl<llvm::Value *> &Ops,
                            unsigned IntID);
  llvm::Value *genSMEReadWrite(const SVETypeFlags &TypeFlags,
                               llvm::SmallVectorImpl<llvm::Value *> &Ops,
                               unsigned IntID);
  llvm::Value *genSMEZero(const SVETypeFlags &TypeFlags,
                          llvm::SmallVectorImpl<llvm::Value *> &Ops,
                          unsigned IntID);
  llvm::Value *genSMELdrStr(const SVETypeFlags &TypeFlags,
                            llvm::SmallVectorImpl<llvm::Value *> &Ops,
                            unsigned IntID);

  void getAArch64SVEProcessedOperands(unsigned BuiltinID, const CallExpr *E,
                                      llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                      SVETypeFlags TypeFlags);

  llvm::Value *genAArch64SMEBuiltinExpr(unsigned BuiltinID, const CallExpr *E);

  llvm::Value *genAArch64BuiltinExpr(unsigned BuiltinID, const CallExpr *E,
                                     llvm::Triple::ArchType Arch);

  llvm::Value *formVector(llvm::ArrayRef<llvm::Value *> Ops);
  llvm::Value *genX86BuiltinExpr(unsigned BuiltinID, const CallExpr *E);
  llvm::Value *genScalarOrConstFoldImmArg(unsigned ICEArguments, unsigned Idx,
                                          const CallExpr *E);
  enum class MSVCIntrin;
  llvm::Value *genMSVCBuiltinExpr(MSVCIntrin BuiltinID, const CallExpr *E);

  //===--------------------------------------------------------------------===//
  //                           Expression Emission
  //===--------------------------------------------------------------------===//

  // Expressions are broken into three classes: scalar, complex, aggregate.

  llvm::Value *genScalarExpr(const Expr *E, bool IgnoreResultAssign = false);

  llvm::Value *genScalarConversion(llvm::Value *Src, QualType SrcTy,
                                   QualType DstTy, SourceLocation Loc);

  llvm::Value *genComplexToScalarConversion(ComplexPairTy Src, QualType SrcTy,
                                            QualType DstTy, SourceLocation Loc);

  void genAggExpr(const Expr *E, AggValueSlot AS);

  LValue genAggExprToLValue(const Expr *E);

  void genAggregateStore(llvm::Value *Val, Address Dest, bool DestIsVolatile);

  ComplexPairTy genComplexExpr(const Expr *E, bool IgnoreReal = false,
                               bool IgnoreImag = false);

  void genComplexExprIntoLValue(const Expr *E, LValue dest, bool isInit);

  void genStoreOfComplex(ComplexPairTy V, LValue dest, bool isInit);

  ComplexPairTy genLoadOfComplex(LValue src, SourceLocation loc);

  ComplexPairTy genPromotedComplexExpr(const Expr *E, QualType PromotionType);
  llvm::Value *genPromotedScalarExpr(const Expr *E, QualType PromotionType);
  ComplexPairTy genPromotedValue(ComplexPairTy result, QualType PromotionType);
  ComplexPairTy genUnPromotedValue(ComplexPairTy result,
                                   QualType PromotionType);

  Address emitAddrOfRealComponent(Address complex, QualType complexType);
  Address emitAddrOfImagComponent(Address complex, QualType complexType);

  llvm::GlobalVariable *addInitializerToStaticVarDecl(const VarDecl &D,
                                                      llvm::GlobalVariable *GV);

  // Emit an @llvm.invariant.start call for the given memory region.
  void genInvariantStart(llvm::Constant *Addr, CharUnits Size);

  void registerGlobalDtorWithAtExit(llvm::Constant *dtorStub);

  enum class GuardKind { VariableGuard, TlsGuard };

  RValue genAtomicExpr(AtomicExpr *E);

  //===--------------------------------------------------------------------===//
  //                         Annotations Emission
  //===--------------------------------------------------------------------===//

  llvm::Value *genAnnotationCall(llvm::Function *AnnotationFn,
                                 llvm::Value *AnnotatedVal,
                                 llvm::StringRef AnnotationStr,
                                 SourceLocation Location,
                                 const AnnotateAttr *Attr);

  void genVarAnnotations(const VarDecl *D, llvm::Value *V);

  Address genFieldAnnotations(const FieldDecl *D, Address V);

  //===--------------------------------------------------------------------===//
  //                             Internal Helpers
  //===--------------------------------------------------------------------===//

  static bool containsLabel(const Stmt *S, bool IgnoreCaseStmts = false);

  static bool containsBreak(const Stmt *S);

  static bool mightAddDeclToScope(const Stmt *S);

  bool constantFoldsToSimpleInteger(const Expr *Cond, bool &Result,
                                    bool AllowLabels = false);

  bool constantFoldsToSimpleInteger(const Expr *Cond, llvm::APSInt &Result,
                                    bool AllowLabels = false);

  void genBranchOnBoolExpr(const Expr *Cond, llvm::BasicBlock *TrueBlock,
                           llvm::BasicBlock *FalseBlock,
                           Stmt::Likelihood LH = Stmt::LH_None);

  enum { NotSubtraction = false, IsSubtraction = true };

  llvm::Value *genCheckedInBoundsGEP(llvm::Type *ElemTy, llvm::Value *Ptr,
                                     llvm::ArrayRef<llvm::Value *> IdxList,
                                     bool SignedIndices, bool IsSubtraction,
                                     SourceLocation Loc,
                                     const llvm::Twine &Name = "");

  enum BuiltinCheckKind {
    BCK_CTZPassedZero,
    BCK_CLZPassedZero,
  };

  llvm::Value *genCheckedArgForBuiltin(const Expr *E, BuiltinCheckKind Kind);

  void genUnreachable(SourceLocation Loc);

  void genTrapCheck(llvm::Value *Checked, SanitizerHandler CheckHandlerID);

  llvm::CallInst *genTrapCall(llvm::Intrinsic::ID IntrID);

  void genCallArg(CallArgList &args, const Expr *E, QualType ArgType);

  void setFPAccuracy(llvm::Value *Val, float Accuracy);

  void setFastMathFlags(FPOptions FPFeatures);

  // Truncate or extend a boolean vector to the requested number of elements.
  llvm::Value *emitBoolVecConversion(llvm::Value *SrcVec,
                                     unsigned NumElementsDst,
                                     const llvm::Twine &Name = "");

private:
  llvm::MDNode *getRangeForLoadFromType(QualType Ty);
  void genReturnOfRValue(RValue RV, QualType Ty);

  void deferPlaceholderReplacement(llvm::Instruction *Old, llvm::Value *New);

  llvm::SmallVector<std::pair<llvm::WeakTrackingVH, llvm::Value *>, 4>
      DeferredReplacements;

  void setAddrOfLocalVar(const VarDecl *VD, Address Addr) {
    assert(!LocalDeclMap.contains(VD) &&
           "Decl already exists in LocalDeclMap!");
    LocalDeclMap.insert({VD, Addr});
  }

  void expandTypeFromArgs(QualType Ty, LValue Dst,
                          llvm::Function::arg_iterator &AI);

  void expandTypeToArgs(QualType Ty, CallArg Arg, llvm::FunctionType *IRFuncTy,
                        llvm::SmallVectorImpl<llvm::Value *> &IRCallArgs,
                        unsigned &IRCallArgPos);

  std::pair<llvm::Value *, llvm::Type *>
  genAsmInput(const TargetInfo::ConstraintInfo &Info, const Expr *InputExpr,
              std::string &ConstraintStr);

  std::pair<llvm::Value *, llvm::Type *>
  genAsmInputLValue(const TargetInfo::ConstraintInfo &Info, LValue InputValue,
                    QualType InputType, std::string &ConstraintStr,
                    SourceLocation Loc);

  llvm::Value *evaluateOrEmitBuiltinObjectSize(const Expr *E, unsigned Type,
                                               llvm::IntegerType *ResType,
                                               llvm::Value *EmittedE,
                                               bool IsDynamic);

  llvm::Value *emitBuiltinObjectSize(const Expr *E, unsigned Type,
                                     llvm::IntegerType *ResType,
                                     llvm::Value *EmittedE, bool IsDynamic);

  void emitZeroOrPatternForAutoVarInit(QualType type, const VarDecl &D,
                                       Address Loc);

public:
  enum class EvaluationOrder {
    ///! No language constraints on evaluation order.
    Default,
    ///! Language semantics require left-to-right evaluation.
    ForceLeftToRight,
    ///! Language semantics require right-to-left evaluation.
    ForceRightToLeft
  };

  struct PrototypeWrapper {
    const FunctionProtoType *P;

    PrototypeWrapper(const FunctionProtoType *FT) : P(FT) {}
  };

  void genCallArgs(CallArgList &Args, PrototypeWrapper Prototype,
                   llvm::iterator_range<CallExpr::const_arg_iterator> ArgRange,
                   AbstractCallee AC = AbstractCallee(),
                   unsigned ParamsToSkip = 0,
                   EvaluationOrder Order = EvaluationOrder::Default);

  Address
  genPointerWithAlignment(const Expr *Addr, LValueBaseInfo *BaseInfo = nullptr,
                          TBAAAccessInfo *TBAAInfo = nullptr,
                          KnownNonNull_t IsKnownNonNull = NotKnownNonNull);

  llvm::Value *loadPassedObjectSize(const Expr *E, QualType EltTy);

  struct MultiVersionResolverOption {
    llvm::Function *Function;
    struct Conds {
      llvm::StringRef Architecture;
      llvm::SmallVector<llvm::StringRef, 8> Features;

      Conds(llvm::StringRef Arch, llvm::ArrayRef<llvm::StringRef> Feats)
          : Architecture(Arch), Features(Feats.begin(), Feats.end()) {}
    } Conditions;

    MultiVersionResolverOption(llvm::Function *F, llvm::StringRef Arch,
                               llvm::ArrayRef<llvm::StringRef> Feats)
        : Function(F), Conditions(Arch, Feats) {}
  };

  // Emits the body of a multiversion function's resolver. Assumes that the
  // options are already sorted in the proper order, with the 'default' option
  // last (if it exists).
  void
  genMultiVersionResolver(llvm::Function *Resolver,
                          llvm::ArrayRef<MultiVersionResolverOption> Options);
  void genX86MultiVersionResolver(
      llvm::Function *Resolver,
      llvm::ArrayRef<MultiVersionResolverOption> Options);
  void genAArch64MultiVersionResolver(
      llvm::Function *Resolver,
      llvm::ArrayRef<MultiVersionResolverOption> Options);

private:
  QualType getVarArgType(const Expr *Arg);

  llvm::Value *genX86CpuIs(const CallExpr *E);
  llvm::Value *genX86CpuIs(llvm::StringRef CPUStr);
  llvm::Value *genX86CpuSupports(const CallExpr *E);
  llvm::Value *genX86CpuSupports(llvm::ArrayRef<llvm::StringRef> FeatureStrs);
  llvm::Value *genX86CpuSupports(std::array<uint32_t, 4> FeatureMask);
  llvm::Value *genX86CpuInit();
  llvm::Value *formX86ResolverCondition(const MultiVersionResolverOption &RO);
  llvm::Value *genAArch64CpuInit();
  llvm::Value *
  formAArch64ResolverCondition(const MultiVersionResolverOption &RO);
  llvm::Value *
  genAArch64CpuSupports(llvm::ArrayRef<llvm::StringRef> FeatureStrs);
};

inline DominatingLLVMValue::saved_type
DominatingLLVMValue::save(FunctionEmitter &FE, llvm::Value *value) {
  if (!needsSaving(value))
    return saved_type(value, false);

  // Otherwise, we need an alloca.
  auto align = CharUnits::fromQuantity(
      FE.ME.getDataLayout().getPrefTypeAlign(value->getType()));
  Address alloca =
      FE.createTempAlloca(value->getType(), align, "cond-cleanup.save");
  FE.Builder.CreateStore(value, alloca);

  return saved_type(alloca.getPointer(), true);
}

inline llvm::Value *DominatingLLVMValue::restore(FunctionEmitter &FE,
                                                 saved_type value) {
  // If the value says it wasn't saved, trust that it's still dominating.
  if (!value.getInt())
    return value.getPointer();

  // Otherwise, it should be an alloca instruction, as set up in save().
  auto alloca = cast<llvm::AllocaInst>(value.getPointer());
  return FE.Builder.CreateAlignedLoad(alloca->getAllocatedType(), alloca,
                                      alloca->getAlign());
}

} // end namespace Emit

// Map the LangOption for floating point exception behavior into
// the corresponding enum in the IR.
llvm::fp::ExceptionBehavior
ToConstrainedExceptMD(LangOptions::FPExceptionModeKind Kind);
} // end namespace neverc

#endif
