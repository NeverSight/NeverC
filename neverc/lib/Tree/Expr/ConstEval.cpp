#include "Interp/Frame.h"
#include "Interp/State.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/OptionalDiagnostic.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Core/TreeDiag.h"
#include "neverc/Tree/Expr/CurrentSourceLocExprScope.h"
#include "neverc/Tree/Format/OSLog.h"
#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "neverc/Tree/Type/TypeLoc.h"
#include "llvm/ADT/APFixedPoint.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <optional>

#define DEBUG_TYPE "exprconstant"

using namespace neverc;
using llvm::APFixedPoint;
using llvm::APFloat;
using llvm::APInt;
using llvm::APSInt;
using llvm::FixedPointSemantics;

// ===----------------------------------------------------------------------===
// Evaluation infrastructure (EvalInfo, CallStackFrame, SubobjectDesignator)
// ===----------------------------------------------------------------------===

enum class GCCTypeClass {
  None = -1,
  Void = 0,
  Integer = 1,
  // GCC reserves 2 for character types, but classifies them as integers.
  Enum = 3,
  Bool = 4,
  Pointer = 5,
  // 6..7: reserved (not used in C)
  RealFloat = 8,
  Complex = 9,
  // 10: reserved (functions; since GCC 6, function/array ids decay)
  ClassOrStruct = 12,
  Union = 13,
  // 14..17: reserved
  BitInt = 18,
  Vector = 19
};

namespace {
struct LValue;
class CallStackFrame;
class EvalInfo;

using SourceLocExprScopeGuard =
    CurrentSourceLocExprScope::SourceLocExprScopeGuard;

QualType getType(APValue::LValueBase B) { return B.getType(); }

const FieldDecl *getAsField(APValue::LValuePathEntry E) {
  return dyn_cast_or_null<FieldDecl>(E.getAsField());
}

QualType getStorageType(const TreeContext &Ctx, const Expr *E) {
  return E->getType();
}

const AllocSizeAttr *getAllocSizeAttr(const CallExpr *CE) {
  if (const FunctionDecl *DirectCallee = CE->getDirectCallee())
    return DirectCallee->getAttr<AllocSizeAttr>();
  if (const Decl *IndirectCallee = CE->getCalleeDecl())
    return IndirectCallee->getAttr<AllocSizeAttr>();
  return nullptr;
}

const CallExpr *tryUnwrapAllocSizeCall(const Expr *E) {
  if (!E->getType()->isPointerType())
    return nullptr;

  E = E->IgnoreParens();
  // If we're doing a variable assignment from e.g. malloc(N), there will
  // probably be a cast of some kind. In exotic cases, we might also see a
  // top-level ExprWithCleanups. Ignore them either way.
  if (const auto *FE = dyn_cast<FullExpr>(E))
    E = FE->getSubExpr()->IgnoreParens();

  if (const auto *Cast = dyn_cast<CastExpr>(E))
    E = Cast->getSubExpr()->IgnoreParens();

  if (const auto *CE = dyn_cast<CallExpr>(E))
    return getAllocSizeAttr(CE) ? CE : nullptr;
  return nullptr;
}

bool isBaseAnAllocSizeCall(APValue::LValueBase Base) {
  const auto *E = Base.dyn_cast<const Expr *>();
  return E && E->getType()->isPointerType() && tryUnwrapAllocSizeCall(E);
}

static const uint64_t AssumedSizeForUnsizedArray =
    std::numeric_limits<uint64_t>::max() / 2;

unsigned findMostDerivedSubobject(TreeContext &Ctx, APValue::LValueBase Base,
                                  llvm::ArrayRef<APValue::LValuePathEntry> Path,
                                  uint64_t &ArraySize, QualType &Type,
                                  bool &IsArray,
                                  bool &FirstEntryIsUnsizedArray) {
  // This only accepts LValueBases from APValues, and APValues don't support
  // arrays that lack size info.
  assert(!isBaseAnAllocSizeCall(Base) &&
         "Unsized arrays shouldn't appear here");
  unsigned MostDerivedLength = 0;
  Type = getType(Base);

  for (unsigned I = 0, N = Path.size(); I != N; ++I) {
    if (Type->isArrayType()) {
      const ArrayType *AT = Ctx.getAsArrayType(Type);
      Type = AT->getElementType();
      MostDerivedLength = I + 1;
      IsArray = true;

      if (auto *CAT = dyn_cast<ConstantArrayType>(AT)) {
        ArraySize = CAT->getSize().getZExtValue();
      } else {
        assert(I == 0 && "unexpected unsized array designator");
        FirstEntryIsUnsizedArray = true;
        ArraySize = AssumedSizeForUnsizedArray;
      }
    } else if (Type->isAnyComplexType()) {
      const ComplexType *CT = Type->castAs<ComplexType>();
      Type = CT->getElementType();
      ArraySize = 2;
      MostDerivedLength = I + 1;
      IsArray = true;
    } else {
      const FieldDecl *FD = getAsField(Path[I]);
      assert(FD && "LValue path steps must be struct fields");
      Type = FD->getType();
      ArraySize = 0;
      MostDerivedLength = I + 1;
      IsArray = false;
    }
  }
  return MostDerivedLength;
}

struct SubobjectDesignator {
  unsigned Invalid : 1;

  unsigned IsOnePastTheEnd : 1;

  unsigned FirstEntryIsAnUnsizedArray : 1;

  unsigned MostDerivedIsArrayElement : 1;

  unsigned MostDerivedPathLength : 28;

  uint64_t MostDerivedArraySize;

  QualType MostDerivedType;

  typedef APValue::LValuePathEntry PathEntry;

  llvm::SmallVector<PathEntry, 8> Entries;

  SubobjectDesignator() : Invalid(true) {}

  explicit SubobjectDesignator(QualType T)
      : Invalid(false), IsOnePastTheEnd(false),
        FirstEntryIsAnUnsizedArray(false), MostDerivedIsArrayElement(false),
        MostDerivedPathLength(0), MostDerivedArraySize(0), MostDerivedType(T) {}

  SubobjectDesignator(TreeContext &Ctx, const APValue &V)
      : Invalid(!V.isLValue() || !V.hasLValuePath()), IsOnePastTheEnd(false),
        FirstEntryIsAnUnsizedArray(false), MostDerivedIsArrayElement(false),
        MostDerivedPathLength(0), MostDerivedArraySize(0) {
    assert(V.isLValue() && "Non-LValue used to make an LValue designator?");
    if (!Invalid) {
      IsOnePastTheEnd = V.isLValueOnePastTheEnd();
      llvm::ArrayRef<PathEntry> VEntries = V.getLValuePath();
      Entries.insert(Entries.end(), VEntries.begin(), VEntries.end());
      if (V.getLValueBase()) {
        bool IsArray = false;
        bool FirstIsUnsizedArray = false;
        MostDerivedPathLength = findMostDerivedSubobject(
            Ctx, V.getLValueBase(), V.getLValuePath(), MostDerivedArraySize,
            MostDerivedType, IsArray, FirstIsUnsizedArray);
        MostDerivedIsArrayElement = IsArray;
        FirstEntryIsAnUnsizedArray = FirstIsUnsizedArray;
      }
    }
  }

  void truncate(TreeContext &Ctx, APValue::LValueBase Base,
                unsigned NewLength) {
    if (Invalid)
      return;

    assert(Base && "cannot truncate path for null pointer");
    assert(NewLength <= Entries.size() && "not a truncation");

    if (NewLength == Entries.size())
      return;
    Entries.resize(NewLength);

    bool IsArray = false;
    bool FirstIsUnsizedArray = false;
    MostDerivedPathLength =
        findMostDerivedSubobject(Ctx, Base, Entries, MostDerivedArraySize,
                                 MostDerivedType, IsArray, FirstIsUnsizedArray);
    MostDerivedIsArrayElement = IsArray;
    FirstEntryIsAnUnsizedArray = FirstIsUnsizedArray;
  }

  void setInvalid() {
    Invalid = true;
    Entries.clear();
  }

  bool isMostDerivedAnUnsizedArray() const {
    assert(!Invalid && "Calling this makes no sense on invalid designators");
    return Entries.size() == 1 && FirstEntryIsAnUnsizedArray;
  }

  uint64_t getMostDerivedArraySize() const {
    assert(!isMostDerivedAnUnsizedArray() && "Unsized array has no size");
    return MostDerivedArraySize;
  }

  bool isOnePastTheEnd() const {
    assert(!Invalid);
    if (IsOnePastTheEnd)
      return true;
    if (!isMostDerivedAnUnsizedArray() && MostDerivedIsArrayElement &&
        Entries[MostDerivedPathLength - 1].getAsArrayIndex() ==
            MostDerivedArraySize)
      return true;
    return false;
  }

  std::pair<uint64_t, uint64_t> validIndexAdjustments() {
    if (Invalid || isMostDerivedAnUnsizedArray())
      return {0, 0};

    // Non-array objects are modeled as a one-element array for bounds.
    bool IsArray =
        MostDerivedPathLength == Entries.size() && MostDerivedIsArrayElement;
    uint64_t ArrayIndex =
        IsArray ? Entries.back().getAsArrayIndex() : (uint64_t)IsOnePastTheEnd;
    uint64_t ArraySize = IsArray ? getMostDerivedArraySize() : (uint64_t)1;
    return {ArrayIndex, ArraySize - ArrayIndex};
  }

  bool isValidSubobject() const {
    if (Invalid)
      return false;
    return !isOnePastTheEnd();
  }
  bool checkSubobject(EvalInfo &Info, const Expr *E, CheckSubobjectKind CSK);

  QualType getType(TreeContext &Ctx) const {
    assert(!Invalid && "invalid designator has no subobject type");
    (void)Ctx;
    assert(MostDerivedPathLength == Entries.size() &&
           "designators path length mismatch");
    return MostDerivedType;
  }

  void addArrayUnchecked(const ConstantArrayType *CAT) {
    Entries.push_back(PathEntry::ArrayIndex(0));

    // This is a most-derived object.
    MostDerivedType = CAT->getElementType();
    MostDerivedIsArrayElement = true;
    MostDerivedArraySize = CAT->getSize().getZExtValue();
    MostDerivedPathLength = Entries.size();
  }
  void addUnsizedArrayUnchecked(QualType ElemTy) {
    Entries.push_back(PathEntry::ArrayIndex(0));

    MostDerivedType = ElemTy;
    MostDerivedIsArrayElement = true;
    // The value in MostDerivedArraySize is undefined in this case. So, set it
    // to an arbitrary value that's likely to loudly break things if it's
    // used.
    MostDerivedArraySize = AssumedSizeForUnsizedArray;
    MostDerivedPathLength = Entries.size();
  }
  void addDeclUnchecked(const FieldDecl *FD) {
    Entries.push_back(APValue::LValuePathEntry(FD));
    MostDerivedType = FD->getType();
    MostDerivedIsArrayElement = false;
    MostDerivedArraySize = 0;
    MostDerivedPathLength = Entries.size();
  }
  void addComplexUnchecked(QualType EltTy, bool Imag) {
    Entries.push_back(PathEntry::ArrayIndex(Imag));

    // This is technically a most-derived object, though in practice this
    // is unlikely to matter.
    MostDerivedType = EltTy;
    MostDerivedIsArrayElement = true;
    MostDerivedArraySize = 2;
    MostDerivedPathLength = Entries.size();
  }
  void diagnoseUnsizedArrayPointerArithmetic(EvalInfo &Info, const Expr *E);
  void diagnosePointerArithmetic(EvalInfo &Info, const Expr *E,
                                 const APSInt &N);
  void adjustIndex(EvalInfo &Info, const Expr *E, APSInt N) {
    if (Invalid || !N)
      return;
    uint64_t TruncatedN = N.extOrTrunc(64).getZExtValue();
    if (isMostDerivedAnUnsizedArray()) {
      diagnoseUnsizedArrayPointerArithmetic(Info, E);
      // Can't verify -- trust that the user is doing the right thing (or if
      // not, trust that the caller will catch the bad behavior).
      Entries.back() =
          PathEntry::ArrayIndex(Entries.back().getAsArrayIndex() + TruncatedN);
      return;
    }

    // Non-array objects are modeled as a one-element array for bounds.
    bool IsArray =
        MostDerivedPathLength == Entries.size() && MostDerivedIsArrayElement;
    uint64_t ArrayIndex =
        IsArray ? Entries.back().getAsArrayIndex() : (uint64_t)IsOnePastTheEnd;
    uint64_t ArraySize = IsArray ? getMostDerivedArraySize() : (uint64_t)1;

    if (N < -(int64_t)ArrayIndex || N > ArraySize - ArrayIndex) {
      // Calculate the actual index in a wide enough type, so we can include
      // it in the note.
      N = N.extend(std::max<unsigned>(N.getBitWidth() + 1, 65));
      (llvm::APInt &)N += ArrayIndex;
      assert(N.ugt(ArraySize) && "bounds check failed for in-bounds index");
      diagnosePointerArithmetic(Info, E, N);
      setInvalid();
      return;
    }

    ArrayIndex += TruncatedN;
    assert(ArrayIndex <= ArraySize &&
           "bounds check succeeded for out-of-bounds index");

    if (IsArray)
      Entries.back() = PathEntry::ArrayIndex(ArrayIndex);
    else
      IsOnePastTheEnd = (ArrayIndex != 0);
  }
};

enum class ScopeKind { Block, FullExpression, Call };

struct CallRef {
  CallRef() : OrigCallee(), CallIndex(0), Version() {}
  CallRef(const FunctionDecl *Callee, unsigned CallIndex, unsigned Version)
      : OrigCallee(Callee), CallIndex(CallIndex), Version(Version) {}

  explicit operator bool() const { return OrigCallee; }

  const ParmVarDecl *getOrigParam(const ParmVarDecl *PVD) const {
    return OrigCallee ? OrigCallee->getParamDecl(PVD->getFunctionScopeIndex())
                      : PVD;
  }

  const FunctionDecl *OrigCallee;
  unsigned CallIndex;
  unsigned Version;
};

class CallStackFrame : public interp::Frame {
public:
  EvalInfo &Info;

  CallStackFrame *Caller;

  const FunctionDecl *Callee;

  const Expr *CallExpr;

  CallRef Arguments;

  CurrentSourceLocExprScope CurSourceLocExprScope;

  // Note that we intentionally use std::map here so that references to
  // values are stable.
  typedef std::pair<const void *, unsigned> MapKeyTy;
  typedef std::map<MapKeyTy, APValue> MapTy;
  MapTy Temporaries;

  SourceRange CallRange;

  unsigned Index;

  llvm::SmallVector<unsigned, 2> TempVersionStack = {1};
  unsigned CurTempVersion = TempVersionStack.back();

  unsigned getTempVersion() const { return TempVersionStack.back(); }

  void pushTempVersion() { TempVersionStack.push_back(++CurTempVersion); }

  void popTempVersion() { TempVersionStack.pop_back(); }

  CallRef createCall(const FunctionDecl *Callee) {
    return {Callee, Index, ++CurTempVersion};
  }

  CallStackFrame(EvalInfo &Info, SourceRange CallRange,
                 const FunctionDecl *Callee, const Expr *CallExpr,
                 CallRef Arguments);
  ~CallStackFrame();

  // Return the temporary for Key whose version number is Version.
  APValue *getTemporary(const void *Key, unsigned Version) {
    MapKeyTy KV(Key, Version);
    auto LB = Temporaries.lower_bound(KV);
    if (LB != Temporaries.end() && LB->first == KV)
      return &LB->second;
    return nullptr;
  }

  APValue *getCurrentTemporary(const void *Key) {
    auto UB = Temporaries.upper_bound(MapKeyTy(Key, UINT_MAX));
    if (UB != Temporaries.begin() && std::prev(UB)->first.first == Key)
      return &std::prev(UB)->second;
    return nullptr;
  }

  unsigned getCurrentTemporaryVersion(const void *Key) const {
    auto UB = Temporaries.upper_bound(MapKeyTy(Key, UINT_MAX));
    if (UB != Temporaries.begin() && std::prev(UB)->first.first == Key)
      return std::prev(UB)->first.second;
    return 0;
  }

  template <typename KeyT>
  APValue &createTemporary(const KeyT *Key, QualType T, ScopeKind Scope,
                           LValue &LV);

  APValue &createParam(CallRef Args, const ParmVarDecl *PVD, LValue &LV);

  void describe(llvm::raw_ostream &OS) const override;

  Frame *getCaller() const override { return Caller; }
  SourceRange getCallRange() const override { return CallRange; }
  const FunctionDecl *getCallee() const override { return Callee; }

private:
  APValue &createLocal(APValue::LValueBase Base, const void *Key, QualType T,
                       ScopeKind Scope);
};

// A shorthand time trace scope struct, prints source range, for example
// {"name":"evaluateAsRValue","args":{"detail":"<test.cc:8:21, col:25>"}}}
class ExprTimeTraceScope {
public:
  ExprTimeTraceScope(const Expr *E, const TreeContext &Ctx,
                     llvm::StringRef Name)
      : TimeScope(Name, [E, &Ctx]() -> llvm::SmallString<64> {
          return llvm::SmallString<64>(
              E->getSourceRange().printToString(Ctx.getSourceManager()));
        }) {}

private:
  llvm::TimeTraceScope TimeScope;
};

} // namespace

namespace {
bool handleDestruction(EvalInfo &Info, SourceLocation Loc,
                       APValue::LValueBase LVBase, APValue &Value, QualType T);
} // namespace

namespace {
class Cleanup {
  llvm::PointerIntPair<APValue *, 2, ScopeKind> Value;
  APValue::LValueBase Base;
  QualType T;

public:
  Cleanup(APValue *Val, APValue::LValueBase Base, QualType T, ScopeKind Scope)
      : Value(Val, Scope), Base(Base), T(T) {}

  bool isDestroyedAtEndOf(ScopeKind K) const {
    return (int)Value.getInt() >= (int)K;
  }
  bool endLifetime(EvalInfo &Info, bool RunDestructors) {
    if (RunDestructors) {
      SourceLocation Loc;
      if (const ValueDecl *VD = Base.dyn_cast<const ValueDecl *>())
        Loc = VD->getLocation();
      else if (const Expr *E = Base.dyn_cast<const Expr *>())
        Loc = E->getExprLoc();
      return handleDestruction(Info, Loc, Base, *Value.getPointer(), T);
    }
    *Value.getPointer() = APValue();
    return true;
  }

  bool hasSideEffect() { return T.isDestructedType(); }
};

} // namespace

namespace {
class EvalInfo : public interp::State {
public:
  TreeContext &Ctx;

  Expr::EvalStatus &EvalStatus;

  CallStackFrame *CurrentCall;

  unsigned CallStackDepth;

  unsigned NextCallIndex;

  unsigned StepsLeft;

  CallStackFrame BottomFrame;

  llvm::SmallVector<Cleanup, 16> CleanupStack;

  APValue::LValueBase EvaluatingDecl;

  APValue *EvaluatingDeclValue;

  unsigned SpeculativeEvaluationDepth = 0;

  uint64_t ArrayInitIndex = -1;

  bool HasActiveDiagnostic;

  bool HasFoldFailureDiagnostic;

  bool CheckingPotentialConstantExpression = false;

  bool CheckingForUndefinedBehavior = false;

  enum EvaluationMode {
    /// evaluate as a constant expression. Stop if we find that the expression
    /// is not a constant expression.
    EM_ConstantExpression,

    /// evaluate as a constant expression. Stop if we find that the expression
    /// is not a constant expression. Some expressions can be retried in the
    /// optimizer if we don't constant fold them here, but in an unevaluated
    /// context we try to fold them immediately since the optimizer never
    /// gets a chance to look at it.
    EM_ConstantExpressionUnevaluated,

    /// Fold the expression to a constant. Stop if we hit a side-effect that
    /// we can't model.
    EM_ConstantFold,

    /// evaluate in any way we know how. Don't worry about side-effects that
    /// can't be modeled.
    EM_IgnoreSideEffects,
  } EvalMode;

  bool checkingPotentialConstantExpression() const override {
    return CheckingPotentialConstantExpression;
  }

  bool checkingForUndefinedBehavior() const override {
    return CheckingForUndefinedBehavior;
  }

  EvalInfo(const TreeContext &C, Expr::EvalStatus &S, EvaluationMode Mode)
      : Ctx(const_cast<TreeContext &>(C)), EvalStatus(S), CurrentCall(nullptr),
        CallStackDepth(0), NextCallIndex(1),
        StepsLeft(C.getLangOpts().ConstexprStepLimit),
        BottomFrame(*this, SourceLocation(), /*Callee=*/nullptr,
                    /*CallExpr=*/nullptr, CallRef()),
        EvaluatingDecl((const ValueDecl *)nullptr),
        EvaluatingDeclValue(nullptr), HasActiveDiagnostic(false),
        HasFoldFailureDiagnostic(false), EvalMode(Mode) {}

  ~EvalInfo() { discardCleanups(); }

  TreeContext &getCtx() const override { return Ctx; }

  void setEvaluatingDecl(APValue::LValueBase Base, APValue &Value) {
    EvaluatingDecl = Base;
    EvaluatingDeclValue = &Value;
  }

  bool CheckCallLimit(SourceLocation Loc) {
    // Don't perform any constexpr calls (other than the call we're checking)
    // when checking a potential constant expression.
    if (checkingPotentialConstantExpression() && CallStackDepth > 1)
      return false;
    if (NextCallIndex == 0) {
      // NextCallIndex has wrapped around.
      FFDiag(Loc, diag::note_constexpr_call_limit_exceeded);
      return false;
    }
    if (CallStackDepth <= getLangOpts().ConstexprCallDepth)
      return true;
    FFDiag(Loc, diag::note_constexpr_depth_limit_exceeded)
        << getLangOpts().ConstexprCallDepth;
    return false;
  }

  bool checkArraySize(SourceLocation Loc, unsigned BitWidth, uint64_t ElemCount,
                      bool Diag) {
    if (BitWidth > ConstantArrayType::getMaxSizeBits(Ctx) ||
        ElemCount > uint64_t(std::numeric_limits<unsigned>::max())) {
      if (Diag)
        FFDiag(Loc, diag::note_constexpr_new_too_large) << ElemCount;
      return false;
    }

    uint64_t Limit = Ctx.getLangOpts().ConstexprStepLimit;
    if (ElemCount > Limit) {
      if (Diag)
        FFDiag(Loc, diag::note_constexpr_new_exceeds_limits)
            << ElemCount << Limit;
      return false;
    }
    return true;
  }

  std::pair<CallStackFrame *, unsigned>
  getCallFrameAndDepth(unsigned CallIndex) {
    assert(CallIndex && "no call index in getCallFrameAndDepth");
    // We will eventually hit BottomFrame, which has Index 1, so Frame can't
    // be null in this loop.
    unsigned Depth = CallStackDepth;
    CallStackFrame *Frame = CurrentCall;
    while (Frame->Index > CallIndex) {
      Frame = Frame->Caller;
      --Depth;
    }
    if (Frame->Index == CallIndex)
      return {Frame, Depth};
    return {nullptr, 0};
  }

  bool nextStep(const Stmt *S) {
    if (!StepsLeft) {
      FFDiag(S->getBeginLoc(), diag::note_constexpr_step_limit_exceeded);
      return false;
    }
    --StepsLeft;
    return true;
  }

  APValue *getParamSlot(CallRef Call, const ParmVarDecl *PVD) {
    CallStackFrame *Frame = getCallFrameAndDepth(Call.CallIndex).first;
    return Frame ? Frame->getTemporary(Call.getOrigParam(PVD), Call.Version)
                 : nullptr;
  }

  void performLifetimeExtension() {
    // Disable the cleanups for lifetime-extended temporaries.
    llvm::erase_if(CleanupStack, [](Cleanup &C) {
      return !C.isDestroyedAtEndOf(ScopeKind::FullExpression);
    });
  }

  bool discardCleanups() {
    for (Cleanup &C : CleanupStack) {
      if (C.hasSideEffect() && !noteSideEffect()) {
        CleanupStack.clear();
        return false;
      }
    }
    CleanupStack.clear();
    return true;
  }

private:
  interp::Frame *getCurrentFrame() override { return CurrentCall; }
  const interp::Frame *getBottomFrame() const override { return &BottomFrame; }

  bool hasActiveDiagnostic() override { return HasActiveDiagnostic; }
  void setActiveDiagnostic(bool Flag) override { HasActiveDiagnostic = Flag; }

  void setFoldFailureDiagnostic(bool Flag) override {
    HasFoldFailureDiagnostic = Flag;
  }

  Expr::EvalStatus &getEvalStatus() const override { return EvalStatus; }

  // If we have a prior diagnostic, it will be noting that the expression
  // isn't a constant expression. This diagnostic is more important,
  // unless we require this evaluation to produce a constant expression.
  //
  bool hasPriorDiagnostic() override {
    if (!EvalStatus.Diag->empty()) {
      switch (EvalMode) {
      case EM_ConstantFold:
      case EM_IgnoreSideEffects:
        if (!HasFoldFailureDiagnostic)
          break;
        // We've already failed to fold something. Keep that diagnostic.
        [[fallthrough]];
      case EM_ConstantExpression:
      case EM_ConstantExpressionUnevaluated:
        setActiveDiagnostic(false);
        return true;
      }
    }
    return false;
  }

  unsigned getCallStackDepth() override { return CallStackDepth; }

public:
  bool keepEvaluatingAfterSideEffect() {
    switch (EvalMode) {
    case EM_IgnoreSideEffects:
      return true;

    case EM_ConstantExpression:
    case EM_ConstantExpressionUnevaluated:
    case EM_ConstantFold:
      // By default, assume any side effect might be valid in some other
      // evaluation of this expression from a different context.
      return checkingPotentialConstantExpression() ||
             checkingForUndefinedBehavior();
    }
    llvm_unreachable("Missed EvalMode case");
  }

  bool noteSideEffect() {
    EvalStatus.HasSideEffects = true;
    return keepEvaluatingAfterSideEffect();
  }

  bool keepEvaluatingAfterUndefinedBehavior() {
    switch (EvalMode) {
    case EM_IgnoreSideEffects:
    case EM_ConstantFold:
      return true;

    case EM_ConstantExpression:
    case EM_ConstantExpressionUnevaluated:
      return checkingForUndefinedBehavior();
    }
    llvm_unreachable("Missed EvalMode case");
  }

  bool noteUndefinedBehavior() override {
    EvalStatus.HasUndefinedBehavior = true;
    return keepEvaluatingAfterUndefinedBehavior();
  }

  bool keepEvaluatingAfterFailure() const override {
    if (!StepsLeft)
      return false;

    switch (EvalMode) {
    case EM_ConstantExpression:
    case EM_ConstantExpressionUnevaluated:
    case EM_ConstantFold:
    case EM_IgnoreSideEffects:
      return checkingPotentialConstantExpression() ||
             checkingForUndefinedBehavior();
    }
    llvm_unreachable("Missed EvalMode case");
  }

  [[nodiscard]] bool noteFailure() {
    // Failure when evaluating some expression often means there is some
    // subexpression whose evaluation was skipped. Therefore, (because we
    // don't track whether we skipped an expression when unwinding after an
    // evaluation failure) every evaluation failure that bubbles up from a
    // subexpression implies that a side-effect has potentially happened. We
    // skip setting the HasSideEffects flag to true until we decide to
    // continue evaluating after that point, which happens here.
    bool KeepGoing = keepEvaluatingAfterFailure();
    EvalStatus.HasSideEffects |= KeepGoing;
    return KeepGoing;
  }

  class ArrayInitLoopIndex {
    EvalInfo &Info;
    uint64_t OuterIndex;

  public:
    ArrayInitLoopIndex(EvalInfo &Info)
        : Info(Info), OuterIndex(Info.ArrayInitIndex) {
      Info.ArrayInitIndex = 0;
    }
    ~ArrayInitLoopIndex() { Info.ArrayInitIndex = OuterIndex; }

    operator uint64_t &() { return Info.ArrayInitIndex; }
  };
};

struct FoldConstant {
  EvalInfo &Info;
  bool Enabled;
  bool HadNoPriorDiags;
  EvalInfo::EvaluationMode OldMode;

  explicit FoldConstant(EvalInfo &Info, bool Enabled)
      : Info(Info), Enabled(Enabled),
        HadNoPriorDiags(Info.EvalStatus.Diag && Info.EvalStatus.Diag->empty() &&
                        !Info.EvalStatus.HasSideEffects),
        OldMode(Info.EvalMode) {
    if (Enabled)
      Info.EvalMode = EvalInfo::EM_ConstantFold;
  }
  void keepDiagnostics() { Enabled = false; }
  ~FoldConstant() {
    if (Enabled && HadNoPriorDiags && !Info.EvalStatus.Diag->empty() &&
        !Info.EvalStatus.HasSideEffects)
      Info.EvalStatus.Diag->clear();
    Info.EvalMode = OldMode;
  }
};

struct IgnoreSideEffectsRAII {
  EvalInfo &Info;
  EvalInfo::EvaluationMode OldMode;
  explicit IgnoreSideEffectsRAII(EvalInfo &Info)
      : Info(Info), OldMode(Info.EvalMode) {
    Info.EvalMode = EvalInfo::EM_IgnoreSideEffects;
  }

  ~IgnoreSideEffectsRAII() { Info.EvalMode = OldMode; }
};

class SpeculativeEvaluationRAII {
  EvalInfo *Info = nullptr;
  Expr::EvalStatus OldStatus;
  unsigned OldSpeculativeEvaluationDepth = 0;

  void moveFromAndCancel(SpeculativeEvaluationRAII &&Other) {
    Info = Other.Info;
    OldStatus = Other.OldStatus;
    OldSpeculativeEvaluationDepth = Other.OldSpeculativeEvaluationDepth;
    Other.Info = nullptr;
  }

  void maybeRestoreState() {
    if (!Info)
      return;

    Info->EvalStatus = OldStatus;
    Info->SpeculativeEvaluationDepth = OldSpeculativeEvaluationDepth;
  }

public:
  SpeculativeEvaluationRAII() = default;

  SpeculativeEvaluationRAII(
      EvalInfo &Info,
      llvm::SmallVectorImpl<PartialDiagnosticAt> *NewDiag = nullptr)
      : Info(&Info), OldStatus(Info.EvalStatus),
        OldSpeculativeEvaluationDepth(Info.SpeculativeEvaluationDepth) {
    Info.EvalStatus.Diag = NewDiag;
    Info.SpeculativeEvaluationDepth = Info.CallStackDepth + 1;
  }

  SpeculativeEvaluationRAII(const SpeculativeEvaluationRAII &Other) = delete;
  SpeculativeEvaluationRAII(SpeculativeEvaluationRAII &&Other) {
    moveFromAndCancel(std::move(Other));
  }

  SpeculativeEvaluationRAII &operator=(SpeculativeEvaluationRAII &&Other) {
    maybeRestoreState();
    moveFromAndCancel(std::move(Other));
    return *this;
  }

  ~SpeculativeEvaluationRAII() { maybeRestoreState(); }
};

template <ScopeKind Kind> class ScopeRAII {
  EvalInfo &Info;
  unsigned OldStackSize;

public:
  ScopeRAII(EvalInfo &Info)
      : Info(Info), OldStackSize(Info.CleanupStack.size()) {
    // Push a new temporary version. This is needed to distinguish between
    // temporaries created in different iterations of a loop.
    Info.CurrentCall->pushTempVersion();
  }
  bool destroy(bool RunDestructors = true) {
    bool OK = cleanup(Info, RunDestructors, OldStackSize);
    OldStackSize = -1U;
    return OK;
  }
  ~ScopeRAII() {
    if (OldStackSize != -1U)
      destroy(false);
    // Body moved to a static method to encourage the compiler to inline away
    // instances of this class.
    Info.CurrentCall->popTempVersion();
  }

private:
  static bool cleanup(EvalInfo &Info, bool RunDestructors,
                      unsigned OldStackSize) {
    assert(OldStackSize <= Info.CleanupStack.size() &&
           "running cleanups out of order?");

    // Run all cleanups for a block scope, and non-lifetime-extended cleanups
    // for a full-expression scope.
    bool Success = true;
    for (unsigned I = Info.CleanupStack.size(); I > OldStackSize; --I) {
      if (Info.CleanupStack[I - 1].isDestroyedAtEndOf(Kind)) {
        if (!Info.CleanupStack[I - 1].endLifetime(Info, RunDestructors)) {
          Success = false;
          break;
        }
      }
    }

    // Compact any retained cleanups.
    auto NewEnd = Info.CleanupStack.begin() + OldStackSize;
    if (Kind != ScopeKind::Block)
      NewEnd = std::remove_if(NewEnd, Info.CleanupStack.end(), [](Cleanup &C) {
        return C.isDestroyedAtEndOf(Kind);
      });
    Info.CleanupStack.erase(NewEnd, Info.CleanupStack.end());
    return Success;
  }
};
typedef ScopeRAII<ScopeKind::Block> BlockScopeRAII;
typedef ScopeRAII<ScopeKind::FullExpression> FullExpressionRAII;
typedef ScopeRAII<ScopeKind::Call> CallScopeRAII;
} // namespace

bool SubobjectDesignator::checkSubobject(EvalInfo &Info, const Expr *E,
                                         CheckSubobjectKind CSK) {
  if (Invalid)
    return false;
  if (isOnePastTheEnd()) {
    Info.CCEDiag(E, diag::note_constexpr_past_end_subobject) << CSK;
    setInvalid();
    return false;
  }
  // Note, we do not diagnose if isMostDerivedAnUnsizedArray(), because there
  // must actually be at least one array element; even a VLA cannot have a
  // bound of zero. And if our index is nonzero, we already had a CCEDiag.
  return true;
}

void SubobjectDesignator::diagnoseUnsizedArrayPointerArithmetic(EvalInfo &Info,
                                                                const Expr *E) {
  Info.CCEDiag(E, diag::note_constexpr_unsized_array_indexed);
  // Do not set the designator as invalid: we can represent this situation,
  // and correct handling of __builtin_object_size requires us to do so.
}

void SubobjectDesignator::diagnosePointerArithmetic(EvalInfo &Info,
                                                    const Expr *E,
                                                    const APSInt &N) {
  // If we're complaining, we must be able to statically determine the size of
  // the most derived array.
  if (MostDerivedPathLength == Entries.size() && MostDerivedIsArrayElement)
    Info.CCEDiag(E, diag::note_constexpr_array_index)
        << N << /*array*/ 0 << static_cast<unsigned>(getMostDerivedArraySize());
  else
    Info.CCEDiag(E, diag::note_constexpr_array_index) << N << /*non-array*/ 1;
  setInvalid();
}

CallStackFrame::CallStackFrame(EvalInfo &Info, SourceRange CallRange,
                               const FunctionDecl *Callee, const Expr *CallExpr,
                               CallRef Call)
    : Info(Info), Caller(Info.CurrentCall), Callee(Callee), CallExpr(CallExpr),
      Arguments(Call), CallRange(CallRange), Index(Info.NextCallIndex++) {
  Info.CurrentCall = this;
  ++Info.CallStackDepth;
}

CallStackFrame::~CallStackFrame() {
  assert(Info.CurrentCall == this && "calls retired out of order");
  --Info.CallStackDepth;
  Info.CurrentCall = Caller;
}

namespace {
bool isRead(AccessKinds AK) {
  return AK == AK_Read || AK == AK_ReadObjectRepresentation;
}
} // namespace

namespace {
bool isModification(AccessKinds AK) {
  switch (AK) {
  case AK_Read:
  case AK_ReadObjectRepresentation:
    return false;
  case AK_Assign:
  case AK_Increment:
  case AK_Decrement:
    return true;
  }
  llvm_unreachable("unknown access kind");
}
} // namespace

namespace {
bool isAnyAccess(AccessKinds AK) { return isRead(AK) || isModification(AK); }
} // namespace

namespace {
bool isValidIndeterminateAccess(AccessKinds AK) {
  switch (AK) {
  case AK_Read:
  case AK_Increment:
  case AK_Decrement:
    return false;
  case AK_ReadObjectRepresentation:
  case AK_Assign:
    return true;
  }
  llvm_unreachable("unknown access kind");
}
} // namespace

// ===----------------------------------------------------------------------===
// Value representation types & forward declarations
// ===----------------------------------------------------------------------===

namespace {
struct ComplexValue {
private:
  bool IsInt;

public:
  APSInt IntReal, IntImag;
  APFloat FloatReal, FloatImag;

  ComplexValue() : FloatReal(APFloat::Bogus()), FloatImag(APFloat::Bogus()) {}

  void makeComplexFloat() { IsInt = false; }
  bool isComplexFloat() const { return !IsInt; }
  APFloat &getComplexFloatReal() { return FloatReal; }
  APFloat &getComplexFloatImag() { return FloatImag; }

  void makeComplexInt() { IsInt = true; }
  bool isComplexInt() const { return IsInt; }
  APSInt &getComplexIntReal() { return IntReal; }
  APSInt &getComplexIntImag() { return IntImag; }

  void moveInto(APValue &v) const {
    if (isComplexFloat())
      v = APValue(FloatReal, FloatImag);
    else
      v = APValue(IntReal, IntImag);
  }
  void setFrom(const APValue &v) {
    assert(v.isComplexFloat() || v.isComplexInt());
    if (v.isComplexFloat()) {
      makeComplexFloat();
      FloatReal = v.getComplexFloatReal();
      FloatImag = v.getComplexFloatImag();
    } else {
      makeComplexInt();
      IntReal = v.getComplexIntReal();
      IntImag = v.getComplexIntImag();
    }
  }
};

struct LValue {
  APValue::LValueBase Base;
  CharUnits Offset;
  SubobjectDesignator Designator;
  bool IsNullPtr : 1;
  bool InvalidBase : 1;

  const APValue::LValueBase getLValueBase() const { return Base; }
  CharUnits &getLValueOffset() { return Offset; }
  const CharUnits &getLValueOffset() const { return Offset; }
  SubobjectDesignator &getLValueDesignator() { return Designator; }
  const SubobjectDesignator &getLValueDesignator() const { return Designator; }
  bool isNullPointer() const { return IsNullPtr; }

  unsigned getLValueCallIndex() const { return Base.getCallIndex(); }
  unsigned getLValueVersion() const { return Base.getVersion(); }

  void moveInto(APValue &V) const {
    if (Designator.Invalid)
      V = APValue(Base, Offset, APValue::NoLValuePath(), IsNullPtr);
    else {
      assert(!InvalidBase && "APValues can't handle invalid LValue bases");
      V = APValue(Base, Offset, Designator.Entries, Designator.IsOnePastTheEnd,
                  IsNullPtr);
    }
  }
  void setFrom(TreeContext &Ctx, const APValue &V) {
    assert(V.isLValue() && "Setting LValue from a non-LValue?");
    Base = V.getLValueBase();
    Offset = V.getLValueOffset();
    InvalidBase = false;
    Designator = SubobjectDesignator(Ctx, V);
    IsNullPtr = V.isNullPointer();
  }

  void set(APValue::LValueBase B, bool BInvalid = false) {
#ifndef NDEBUG
    // We only allow a few types of invalid bases. Enforce that here.
    if (BInvalid) {
      const auto *E = B.get<const Expr *>();
      assert((isa<MemberExpr>(E) || tryUnwrapAllocSizeCall(E)) &&
             "Unexpected type of invalid base");
    }
#endif

    Base = B;
    Offset = CharUnits::fromQuantity(0);
    InvalidBase = BInvalid;
    Designator = SubobjectDesignator(getType(B));
    IsNullPtr = false;
  }

  void setNull(TreeContext &Ctx, QualType PointerTy) {
    Base = (const ValueDecl *)nullptr;
    Offset = CharUnits::fromQuantity(Ctx.getTargetNullPointerValue(PointerTy));
    InvalidBase = false;
    Designator = SubobjectDesignator(PointerTy->getPointeeType());
    IsNullPtr = true;
  }

  void setInvalid(APValue::LValueBase B, unsigned I = 0) { set(B, true); }

  std::string toString(TreeContext &Ctx, QualType T) const {
    APValue Printable;
    moveInto(Printable);
    return Printable.getAsString(Ctx, T);
  }

private:
  // Check that this LValue is not based on a null pointer. If it is, produce
  // a diagnostic and mark the designator as invalid.
  template <typename GenDiagType>
  bool checkNullPointerDiagnosingWith(const GenDiagType &GenDiag) {
    if (Designator.Invalid)
      return false;
    if (IsNullPtr) {
      GenDiag();
      Designator.setInvalid();
      return false;
    }
    return true;
  }

public:
  bool checkNullPointer(EvalInfo &Info, const Expr *E, CheckSubobjectKind CSK) {
    return checkNullPointerDiagnosingWith([&Info, E, CSK] {
      Info.CCEDiag(E, diag::note_constexpr_null_subobject) << CSK;
    });
  }

  bool checkNullPointerForFoldAccess(EvalInfo &Info, const Expr *E,
                                     AccessKinds AK) {
    return checkNullPointerDiagnosingWith([&Info, E, AK] {
      Info.FFDiag(E, diag::note_constexpr_access_null) << AK;
    });
  }

  // Check this LValue refers to an object. If not, set the designator to be
  // invalid and emit a diagnostic.
  bool checkSubobject(EvalInfo &Info, const Expr *E, CheckSubobjectKind CSK) {
    return (CSK == CSK_ArrayToPointer || checkNullPointer(Info, E, CSK)) &&
           Designator.checkSubobject(Info, E, CSK);
  }

  void addDecl(EvalInfo &Info, const Expr *E, const FieldDecl *FD) {
    if (checkSubobject(Info, E, CSK_Field))
      Designator.addDeclUnchecked(FD);
  }
  void addUnsizedArray(EvalInfo &Info, const Expr *E, QualType ElemTy) {
    if (!Designator.Entries.empty()) {
      Info.CCEDiag(E, diag::note_constexpr_unsupported_unsized_array);
      Designator.setInvalid();
      return;
    }
    if (checkSubobject(Info, E, CSK_ArrayToPointer)) {
      assert(getType(Base)->isPointerType() || getType(Base)->isArrayType());
      Designator.FirstEntryIsAnUnsizedArray = true;
      Designator.addUnsizedArrayUnchecked(ElemTy);
    }
  }
  void addArray(EvalInfo &Info, const Expr *E, const ConstantArrayType *CAT) {
    if (checkSubobject(Info, E, CSK_ArrayToPointer))
      Designator.addArrayUnchecked(CAT);
  }
  void addComplex(EvalInfo &Info, const Expr *E, QualType EltTy, bool Imag) {
    if (checkSubobject(Info, E, Imag ? CSK_Imag : CSK_Real))
      Designator.addComplexUnchecked(EltTy, Imag);
  }
  void clearIsNullPointer() { IsNullPtr = false; }
  void adjustOffsetAndIndex(EvalInfo &Info, const Expr *E, const APSInt &Index,
                            CharUnits ElementSize) {
    // Zero offset: no change to the designated address.
    if (!Index)
      return;

    // Compute the new offset in the appropriate width, wrapping at 64 bits.
    uint64_t Offset64 = Offset.getQuantity();
    uint64_t ElemSize64 = ElementSize.getQuantity();
    uint64_t Index64 = Index.extOrTrunc(64).getZExtValue();
    Offset = CharUnits::fromQuantity(Offset64 + ElemSize64 * Index64);

    if (checkNullPointer(Info, E, CSK_ArrayIndex))
      Designator.adjustIndex(Info, E, Index);
    clearIsNullPointer();
  }
  void adjustOffset(CharUnits N) {
    Offset += N;
    if (N.getQuantity())
      clearIsNullPointer();
  }
};

} // namespace

namespace {
bool evaluate(APValue &Result, EvalInfo &Info, const Expr *E);
} // namespace
namespace {
bool evaluateInPlace(APValue &Result, EvalInfo &Info, const LValue &This,
                     const Expr *E, bool AllowNonLiteralTypes = false);
} // namespace
namespace {
bool evaluateLValue(const Expr *E, LValue &Result, EvalInfo &Info,
                    bool InvalidBaseOK = false);
} // namespace
namespace {
bool evaluatePointer(const Expr *E, LValue &Result, EvalInfo &Info,
                     bool InvalidBaseOK = false);
} // namespace
namespace {
bool evaluateTemporary(const Expr *E, LValue &Result, EvalInfo &Info);
} // namespace
namespace {
bool evaluateInteger(const Expr *E, APSInt &Result, EvalInfo &Info);
} // namespace
namespace {
bool evaluateIntegerOrLValue(const Expr *E, APValue &Result, EvalInfo &Info);
} // namespace
namespace {
bool evaluateFloat(const Expr *E, APFloat &Result, EvalInfo &Info);
} // namespace
namespace {
bool evaluateComplex(const Expr *E, ComplexValue &Res, EvalInfo &Info);
} // namespace
namespace {
bool evaluateAtomic(const Expr *E, const LValue *This, APValue &Result,
                    EvalInfo &Info);
} // namespace
namespace {
bool evaluateAsRValue(EvalInfo &Info, const Expr *E, APValue &Result);
} // namespace
namespace {
bool evaluateBuiltinStrLen(const Expr *E, uint64_t &Result, EvalInfo &Info);
} // namespace

namespace {
bool evaluateFixedPointOrInteger(const Expr *E, APFixedPoint &Result,
                                 EvalInfo &Info);
} // namespace

namespace {
bool evaluateFixedPoint(const Expr *E, APFixedPoint &Result, EvalInfo &Info);
} // namespace

namespace {
void negateAsSigned(APSInt &Int) {
  if (Int.isUnsigned() || Int.isMinSignedValue()) {
    Int = Int.extend(Int.getBitWidth() + 1);
    Int.setIsSigned(true);
  }
  Int = -Int;
}
} // namespace

template <typename KeyT>
APValue &CallStackFrame::createTemporary(const KeyT *Key, QualType T,
                                         ScopeKind Scope, LValue &LV) {
  unsigned Version = getTempVersion();
  APValue::LValueBase Base(Key, Index, Version);
  LV.set(Base);
  return createLocal(Base, Key, T, Scope);
}

APValue &CallStackFrame::createParam(CallRef Args, const ParmVarDecl *PVD,
                                     LValue &LV) {
  assert(Args.CallIndex == Index && "creating parameter in wrong frame");
  APValue::LValueBase Base(PVD, Index, Args.Version);
  LV.set(Base);
  // We always destroy parameters at the end of the call, even if we'd allow
  // them to live to the end of the full-expression at runtime, in order to
  // give portable results and match other compilers.
  return createLocal(Base, PVD, PVD->getType(), ScopeKind::Call);
}

APValue &CallStackFrame::createLocal(APValue::LValueBase Base, const void *Key,
                                     QualType T, ScopeKind Scope) {
  assert(Base.getCallIndex() == Index && "lvalue for wrong frame");
  unsigned Version = Base.getVersion();
  APValue &Result = Temporaries[MapKeyTy(Key, Version)];
  assert(Result.isAbsent() && "local created multiple times");

  // If we're creating a local immediately in the operand of a speculative
  // evaluation, don't register a cleanup to be run outside the speculative
  // evaluation context, since we won't actually be able to initialize this
  // object.
  if (Index <= Info.SpeculativeEvaluationDepth) {
    if (T.isDestructedType())
      Info.noteSideEffect();
  } else {
    Info.CleanupStack.push_back(Cleanup(&Result, Base, T, Scope));
  }
  return Result;
}

void CallStackFrame::describe(llvm::raw_ostream &Out) const {
  unsigned ArgIndex = 0;

  Callee->getNameForDiagnostic(Out, Info.Ctx.getPrintingPolicy(),
                               /*Qualified=*/false);

  Out << '(';

  for (FunctionDecl::param_const_iterator I = Callee->param_begin(),
                                          E = Callee->param_end();
       I != E; ++I, ++ArgIndex) {
    if (ArgIndex > 0)
      Out << ", ";

    const ParmVarDecl *Param = *I;
    APValue *V = Info.getParamSlot(Arguments, Param);
    if (V)
      V->printPretty(Out, Info.Ctx, Param->getType());
    else
      Out << "<...>";
  }

  Out << ')';
}

namespace {
bool evaluateIgnoredValue(EvalInfo &Info, const Expr *E) {

  APValue Scratch;
  if (!evaluate(Scratch, Info, E))
    // We don't need the value, but we might have skipped a side effect here.
    return Info.noteSideEffect();
  return true;
}
} // namespace

namespace {
bool isNoOpCall(const CallExpr *E) {
  unsigned Builtin = E->getBuiltinCallee();
  return Builtin == Builtin::BI__builtin_function_start;
}
} // namespace

namespace {
bool isGlobalLValue(APValue::LValueBase B) {
  // Address constants: null pointer / nullptr_t, or addresses with static or
  // suitably global storage (see cases below).
  if (!B)
    return true;

  if (const ValueDecl *D = B.dyn_cast<const ValueDecl *>()) {
    // ... the address of an object with static storage duration,
    if (const VarDecl *VD = dyn_cast<VarDecl>(D))
      return VD->hasGlobalStorage();
    return isa<FunctionDecl>(D);
  }

  const Expr *E = B.get<const Expr *>();
  switch (E->getStmtClass()) {
  default:
    return false;
  case Expr::CompoundLiteralExprClass: {
    const CompoundLiteralExpr *CLE = cast<CompoundLiteralExpr>(E);
    return CLE->isFileScope() && CLE->isLValue();
  }
  // A string literal has static storage duration.
  case Expr::StringLiteralClass:
  case Expr::PredefinedExprClass:
    return true;
  case Expr::CallExprClass:
    return isNoOpCall(cast<CallExpr>(E));
  // For GCC compatibility, &&label has static storage duration.
  case Expr::AddrLabelExprClass:
    return true;
  case Expr::SourceLocExprClass:
    return true;
  case Expr::ImplicitValueInitExprClass:
    return true;
  }
}
} // namespace

namespace {
const ValueDecl *getLValueBaseDecl(const LValue &LVal) {
  return LVal.Base.dyn_cast<const ValueDecl *>();
}
} // namespace

namespace {
bool isLiteralLValue(const LValue &Value) {
  if (Value.getLValueCallIndex())
    return false;
  const Expr *E = Value.Base.dyn_cast<const Expr *>();
  return E != nullptr;
}
} // namespace

namespace {
bool isWeakLValue(const LValue &Value) {
  const ValueDecl *Decl = getLValueBaseDecl(Value);
  return Decl && Decl->isWeak();
}
} // namespace

namespace {
bool isZeroSized(const LValue &Value) {
  const ValueDecl *Decl = getLValueBaseDecl(Value);
  if (Decl && isa<VarDecl>(Decl)) {
    QualType Ty = Decl->getType();
    if (Ty->isArrayType())
      return Ty->isIncompleteType() ||
             Decl->getTreeContext().getTypeSize(Ty) == 0;
  }
  return false;
}
} // namespace

namespace {
bool hasSameBase(const LValue &A, const LValue &B) {
  if (!A.getLValueBase())
    return !B.getLValueBase();
  if (!B.getLValueBase())
    return false;

  if (A.getLValueBase().getOpaqueValue() != B.getLValueBase().getOpaqueValue())
    return false;

  return A.getLValueCallIndex() == B.getLValueCallIndex() &&
         A.getLValueVersion() == B.getLValueVersion();
}
} // namespace

namespace {
void noteLValueLocation(EvalInfo &Info, APValue::LValueBase Base) {
  assert(Base && "no location for a null lvalue");
  const ValueDecl *VD = Base.dyn_cast<const ValueDecl *>();

  // For a parameter, find the corresponding call stack frame (if it still
  // exists), and point at the parameter of the function definition we actually
  // invoked.
  if (auto *PVD = dyn_cast_or_null<ParmVarDecl>(VD)) {
    unsigned Idx = PVD->getFunctionScopeIndex();
    for (CallStackFrame *F = Info.CurrentCall; F; F = F->Caller) {
      if (F->Arguments.CallIndex == Base.getCallIndex() &&
          F->Arguments.Version == Base.getVersion() && F->Callee &&
          Idx < F->Callee->getNumParams()) {
        VD = F->Callee->getParamDecl(Idx);
        break;
      }
    }
  }

  if (VD)
    Info.Note(VD->getLocation(), diag::note_declared_at);
  else if (const Expr *E = Base.dyn_cast<const Expr *>())
    Info.Note(E->getExprLoc(), diag::note_constexpr_temporary_here);
}
} // namespace

// ===----------------------------------------------------------------------===
// Evaluation result checking & object handling
// ===----------------------------------------------------------------------===

enum class checkEvaluationResultKind {
  ConstantExpression,
  FullyInitialized,
};

using CheckedTemporaries = llvm::SmallPtrSet<const Expr *, 8>;

namespace {
bool checkEvaluationResult(checkEvaluationResultKind CERK, EvalInfo &Info,
                           SourceLocation DiagLoc, QualType Type,
                           const APValue &Value, const FieldDecl *SubobjectDecl,
                           CheckedTemporaries &CheckedTemps);
} // namespace

namespace {
bool checkLValueConstantExpression(EvalInfo &Info, SourceLocation Loc,
                                   QualType Type, const LValue &LVal,
                                   CheckedTemporaries &CheckedTemps) {
  APValue::LValueBase Base = LVal.getLValueBase();
  const ValueDecl *BaseVD = Base.dyn_cast<const ValueDecl *>();

  // Check that the object is a global. Note that the fake 'this' object we
  // manufacture when checking potential constant expressions is conservatively
  // assumed to be global here.
  if (!isGlobalLValue(Base)) {
    Info.FFDiag(Loc);
    return false;
  }
  assert((Info.checkingPotentialConstantExpression() ||
          LVal.getLValueCallIndex() == 0) &&
         "have call index for global lvalue");

  if (BaseVD) {
    if (const VarDecl *Var = dyn_cast<const VarDecl>(BaseVD)) {
      // Check if this is a thread-local variable.
      if (Var->getTLSKind())
        return false;

      // A dllimport variable never acts like a constant.
      if (Var->hasAttr<DLLImportAttr>())
        return false;
    }
    if (isa<FunctionDecl>(BaseVD)) {
      // __declspec(dllimport): C has no ODR or dynamic initialization, so
      // initialization with the address of the thunk is permitted.
    }
  }

  return true;
}
} // namespace

namespace {
bool checkLiteralType(EvalInfo &Info, const Expr *E,
                      const LValue *This = nullptr) {
  if (!E->isPRValue() || E->getType()->isLiteralType(Info.Ctx))
    return true;

  // Initializer for `EvaluatingDecl`: allow non-literal struct subobjects
  // (aggregate constexpr rules).
  if (This && Info.EvaluatingDecl == This->getLValueBase())
    return true;

  // Prvalue constant expressions must be of literal types.
  Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
  return false;
}
} // namespace

namespace {
bool checkEvaluationResult(checkEvaluationResultKind CERK, EvalInfo &Info,
                           SourceLocation DiagLoc, QualType Type,
                           const APValue &Value, const FieldDecl *SubobjectDecl,
                           CheckedTemporaries &CheckedTemps) {
  if (!Value.hasValue()) {
    if (SubobjectDecl) {
      Info.FFDiag(DiagLoc, diag::note_constexpr_uninitialized)
          << /*(name)*/ 1 << SubobjectDecl;
      Info.Note(SubobjectDecl->getLocation(),
                diag::note_constexpr_subobject_declared_here);
    } else {
      Info.FFDiag(DiagLoc, diag::note_constexpr_uninitialized)
          << /*of type*/ 0 << Type;
    }
    return false;
  }

  // We allow _Atomic(T) to be initialized from anything that T can be
  // initialized from.
  if (const AtomicType *AT = Type->getAs<AtomicType>())
    Type = AT->getValueType();

  // For a literal constant expression of array or record type, each subobject
  // of its value shall have been initialized by a constant expression.
  if (Value.isArray()) {
    QualType EltTy = Type->castAsArrayTypeUnsafe()->getElementType();
    for (unsigned I = 0, N = Value.getArrayInitializedElts(); I != N; ++I) {
      if (!checkEvaluationResult(CERK, Info, DiagLoc, EltTy,
                                 Value.getArrayInitializedElt(I), SubobjectDecl,
                                 CheckedTemps))
        return false;
    }
    if (!Value.hasArrayFiller())
      return true;
    return checkEvaluationResult(CERK, Info, DiagLoc, EltTy,
                                 Value.getArrayFiller(), SubobjectDecl,
                                 CheckedTemps);
  }
  if (Value.isUnion() && Value.getUnionField()) {
    return checkEvaluationResult(
        CERK, Info, DiagLoc, Value.getUnionField()->getType(),
        Value.getUnionValue(), Value.getUnionField(), CheckedTemps);
  }
  if (Value.isStruct()) {
    RecordDecl *RD = Type->castAs<RecordType>()->getDecl();
    for (const auto *I : RD->fields()) {
      if (I->isUnnamedBitfield())
        continue;

      if (!checkEvaluationResult(CERK, Info, DiagLoc, I->getType(),
                                 Value.getStructField(I->getFieldIndex()), I,
                                 CheckedTemps))
        return false;
    }
  }

  if (Value.isLValue() &&
      CERK == checkEvaluationResultKind::ConstantExpression) {
    LValue LVal;
    LVal.setFrom(Info.Ctx, Value);
    return checkLValueConstantExpression(Info, DiagLoc, Type, LVal,
                                         CheckedTemps);
  }

  // Everything else is fine.
  return true;
}
} // namespace

namespace {
bool checkConstantExpression(EvalInfo &Info, SourceLocation DiagLoc,
                             QualType Type, const APValue &Value) {
  // Nothing to check for a constant expression of type 'cv void'.
  if (Type->isVoidType())
    return true;

  CheckedTemporaries CheckedTemps;
  return checkEvaluationResult(checkEvaluationResultKind::ConstantExpression,
                               Info, DiagLoc, Type, Value,
                               /*SubobjectDecl=*/nullptr, CheckedTemps);
}
} // namespace

namespace {
bool checkFullyInitialized(EvalInfo &Info, SourceLocation DiagLoc,
                           QualType Type, const APValue &Value) {
  CheckedTemporaries CheckedTemps;
  return checkEvaluationResult(checkEvaluationResultKind::FullyInitialized,
                               Info, DiagLoc, Type, Value,
                               /*SubobjectDecl=*/nullptr, CheckedTemps);
}
} // namespace

namespace {
bool evalPointerValueAsBool(const APValue &Value, bool &Result) {
  // A null base expression indicates a null pointer.  These are always
  // evaluatable, and they are false unless the offset is zero.
  if (!Value.getLValueBase()) {
    Result = !Value.getLValueOffset().isZero();
    return true;
  }

  // We have a non-null base.  These are generally known to be true, but if it's
  // a weak declaration it can be null at runtime.
  Result = true;
  const ValueDecl *Decl = Value.getLValueBase().dyn_cast<const ValueDecl *>();
  return !Decl || !Decl->isWeak();
}
} // namespace

namespace {
bool handleConversionToBool(const APValue &Val, bool &Result) {
  switch (Val.getKind()) {
  case APValue::None:
  case APValue::Indeterminate:
    return false;
  case APValue::Int:
    Result = Val.getInt().getBoolValue();
    return true;
  case APValue::FixedPoint:
    Result = Val.getFixedPoint().getBoolValue();
    return true;
  case APValue::Float:
    Result = !Val.getFloat().isZero();
    return true;
  case APValue::ComplexInt:
    Result = Val.getComplexIntReal().getBoolValue() ||
             Val.getComplexIntImag().getBoolValue();
    return true;
  case APValue::ComplexFloat:
    Result = !Val.getComplexFloatReal().isZero() ||
             !Val.getComplexFloatImag().isZero();
    return true;
  case APValue::LValue:
    return evalPointerValueAsBool(Val, Result);
  case APValue::Vector:
  case APValue::Array:
  case APValue::Struct:
  case APValue::Union:
  case APValue::AddrLabelDiff:
    return false;
  }

  llvm_unreachable("unknown APValue kind");
}
} // namespace

namespace {
bool evaluateAsBooleanCondition(const Expr *E, bool &Result, EvalInfo &Info) {

  assert(E->isPRValue() && "missing lvalue-to-rvalue conv in bool condition");
  APValue Val;
  if (!evaluate(Val, Info, E))
    return false;
  return handleConversionToBool(Val, Result);
}
} // namespace

namespace {
template <typename T>
bool handleOverflow(EvalInfo &Info, const Expr *E, const T &SrcValue,
                    QualType DestType) {
  Info.CCEDiag(E, diag::note_constexpr_overflow) << SrcValue << DestType;
  return Info.noteUndefinedBehavior();
}
} // namespace

namespace {
bool handleFloatToIntCast(EvalInfo &Info, const Expr *E, QualType SrcType,
                          const APFloat &Value, QualType DestType,
                          APSInt &Result) {
  unsigned DestWidth = Info.Ctx.getIntWidth(DestType);
  bool DestSigned = DestType->isSignedIntegerOrEnumerationType();

  Result = APSInt(DestWidth, !DestSigned);
  bool ignored;
  if (Value.convertToInteger(Result, llvm::APFloat::rmTowardZero, &ignored) &
      APFloat::opInvalidOp)
    return handleOverflow(Info, E, Value, DestType);
  return true;
}
} // namespace

namespace {
llvm::RoundingMode getActiveRoundingMode(EvalInfo &Info, const Expr *E) {
  llvm::RoundingMode RM =
      E->getFPFeaturesInEffect(Info.Ctx.getLangOpts()).getRoundingMode();
  if (RM == llvm::RoundingMode::Dynamic)
    RM = llvm::RoundingMode::NearestTiesToEven;
  return RM;
}
} // namespace

namespace {
bool checkFloatingPointResult(EvalInfo &Info, const Expr *E,
                              APFloat::opStatus St) {
  // In a constant context, assume that any dynamic rounding mode or FP
  // exception state matches the default floating-point environment.
  if (Info.InConstantContext)
    return true;

  FPOptions FPO = E->getFPFeaturesInEffect(Info.Ctx.getLangOpts());
  if ((St & APFloat::opInexact) &&
      FPO.getRoundingMode() == llvm::RoundingMode::Dynamic) {
    // Inexact result means that it depends on rounding mode. If the requested
    // mode is dynamic, the evaluation cannot be made in compile time.
    Info.FFDiag(E, diag::note_constexpr_dynamic_rounding);
    return false;
  }

  if ((St != APFloat::opOK) &&
      (FPO.getRoundingMode() == llvm::RoundingMode::Dynamic ||
       FPO.getExceptionMode() != LangOptions::FPE_Ignore ||
       FPO.getAllowFEnvAccess())) {
    Info.FFDiag(E, diag::note_constexpr_float_arithmetic_strict);
    return false;
  }

  if ((St & APFloat::opStatus::opInvalidOp) &&
      FPO.getExceptionMode() != LangOptions::FPE_Ignore) {
    // There is no usefully definable result.
    Info.FFDiag(E);
    return false;
  }

  return true;
}
} // namespace

namespace {
bool handleFloatToFloatCast(EvalInfo &Info, const Expr *E, QualType SrcType,
                            QualType DestType, APFloat &Result) {
  assert(isa<CastExpr>(E) || isa<CompoundAssignOperator>(E));
  llvm::RoundingMode RM = getActiveRoundingMode(Info, E);
  APFloat::opStatus St;
  APFloat Value = Result;
  bool ignored;
  St = Result.convert(Info.Ctx.getFloatTypeSemantics(DestType), RM, &ignored);
  return checkFloatingPointResult(Info, E, St);
}
} // namespace

namespace {
APSInt handleIntToIntCast(EvalInfo &Info, const Expr *E, QualType DestType,
                          QualType SrcType, const APSInt &Value) {
  unsigned DestWidth = Info.Ctx.getIntWidth(DestType);
  // Figure out if this is a truncate, extend or noop cast.
  // If the input is signed, do a sign extend, noop, or truncate.
  APSInt Result = Value.extOrTrunc(DestWidth);
  Result.setIsUnsigned(DestType->isUnsignedIntegerOrEnumerationType());
  if (DestType->isBooleanType())
    Result = Value.getBoolValue();
  return Result;
}
} // namespace

namespace {
bool handleIntToFloatCast(EvalInfo &Info, const Expr *E, const FPOptions FPO,
                          QualType SrcType, const APSInt &Value,
                          QualType DestType, APFloat &Result) {
  Result = APFloat(Info.Ctx.getFloatTypeSemantics(DestType), 1);
  llvm::RoundingMode RM = getActiveRoundingMode(Info, E);
  APFloat::opStatus St = Result.convertFromAPInt(Value, Value.isSigned(), RM);
  return checkFloatingPointResult(Info, E, St);
}
} // namespace

namespace {
bool truncateBitfieldValue(EvalInfo &Info, const Expr *E, APValue &Value,
                           const FieldDecl *FD) {
  assert(FD->isBitField() && "truncateBitfieldValue on non-bitfield");

  if (!Value.isInt()) {
    // Trying to store a pointer-cast-to-integer into a bitfield.
    assert(Value.isLValue() && "integral value neither int nor lvalue?");
    Info.FFDiag(E);
    return false;
  }

  APSInt &Int = Value.getInt();
  unsigned OldBitWidth = Int.getBitWidth();
  unsigned NewBitWidth = FD->getBitWidthValue(Info.Ctx);
  if (NewBitWidth < OldBitWidth)
    Int = Int.trunc(NewBitWidth).extend(OldBitWidth);
  return true;
}
} // namespace

namespace {
template <typename Operation>
bool checkedIntArithmetic(EvalInfo &Info, const Expr *E, const APSInt &LHS,
                          const APSInt &RHS, unsigned BitWidth, Operation Op,
                          APSInt &Result) {
  if (LHS.isUnsigned()) {
    Result = Op(LHS, RHS);
    return true;
  }

  APSInt Value(Op(LHS.extend(BitWidth), RHS.extend(BitWidth)), false);
  Result = Value.trunc(LHS.getBitWidth());
  if (Result.extend(BitWidth) != Value) {
    if (Info.checkingForUndefinedBehavior())
      Info.Ctx.getDiagnostics().Report(E->getExprLoc(),
                                       diag::warn_integer_constant_overflow)
          << toString(Result, 10) << E->getType() << E->getSourceRange();
    return handleOverflow(Info, E, Value, E->getType());
  }
  return true;
}
} // namespace

namespace {
bool handleIntIntBinOp(EvalInfo &Info, const BinaryOperator *E,
                       const APSInt &LHS, BinaryOperatorKind Opcode, APSInt RHS,
                       APSInt &Result) {
  bool handleOverflowResult = true;
  switch (Opcode) {
  default:
    Info.FFDiag(E);
    return false;
  case BO_Mul:
    return checkedIntArithmetic(Info, E, LHS, RHS, LHS.getBitWidth() * 2,
                                std::multiplies<APSInt>(), Result);
  case BO_Add:
    return checkedIntArithmetic(Info, E, LHS, RHS, LHS.getBitWidth() + 1,
                                std::plus<APSInt>(), Result);
  case BO_Sub:
    return checkedIntArithmetic(Info, E, LHS, RHS, LHS.getBitWidth() + 1,
                                std::minus<APSInt>(), Result);
  case BO_And:
    Result = LHS & RHS;
    return true;
  case BO_Xor:
    Result = LHS ^ RHS;
    return true;
  case BO_Or:
    Result = LHS | RHS;
    return true;
  case BO_Div:
  case BO_Rem:
    if (RHS == 0) {
      Info.FFDiag(E, diag::note_expr_divide_by_zero)
          << E->getRHS()->getSourceRange();
      return false;
    }
    // Check for overflow case: INT_MIN / -1 or INT_MIN % -1. APSInt supports
    // this operation and gives the two's complement result.
    if (RHS.isNegative() && RHS.isAllOnes() && LHS.isSigned() &&
        LHS.isMinSignedValue())
      handleOverflowResult = handleOverflow(
          Info, E, -LHS.extend(LHS.getBitWidth() + 1), E->getType());
    Result = (Opcode == BO_Rem ? LHS % RHS : LHS / RHS);
    return handleOverflowResult;
  case BO_Shl: {
    if (RHS.isSigned() && RHS.isNegative()) {
      // During constant-folding, a negative shift is an opposite shift. Such
      // a shift is not a constant expression.
      Info.CCEDiag(E, diag::note_constexpr_negative_shift) << RHS;
      RHS = -RHS;
      goto shift_right;
    }
  shift_left:
    // Shift amount must be less than the width of the left-hand type.
    unsigned SA = (unsigned)RHS.getLimitedValue(LHS.getBitWidth() - 1);
    if (SA != RHS) {
      Info.CCEDiag(E, diag::note_constexpr_large_shift)
          << RHS << E->getType() << LHS.getBitWidth();
    } else if (LHS.isSigned()) {
      if (LHS.isNegative())
        Info.CCEDiag(E, diag::note_constexpr_lshift_of_negative) << LHS;
      else if (LHS.countl_zero() < SA)
        Info.CCEDiag(E, diag::note_constexpr_lshift_discards);
    }
    Result = LHS << SA;
    return true;
  }
  case BO_Shr: {
    if (RHS.isSigned() && RHS.isNegative()) {
      // During constant-folding, a negative shift is an opposite shift. Such a
      // shift is not a constant expression.
      Info.CCEDiag(E, diag::note_constexpr_negative_shift) << RHS;
      RHS = -RHS;
      goto shift_left;
    }
  shift_right:
    // Same width rule as left shift.
    unsigned SA = (unsigned)RHS.getLimitedValue(LHS.getBitWidth() - 1);
    if (SA != RHS)
      Info.CCEDiag(E, diag::note_constexpr_large_shift)
          << RHS << E->getType() << LHS.getBitWidth();
    Result = LHS >> SA;
    return true;
  }

  case BO_LT:
    Result = LHS < RHS;
    return true;
  case BO_GT:
    Result = LHS > RHS;
    return true;
  case BO_LE:
    Result = LHS <= RHS;
    return true;
  case BO_GE:
    Result = LHS >= RHS;
    return true;
  case BO_EQ:
    Result = LHS == RHS;
    return true;
  case BO_NE:
    Result = LHS != RHS;
    return true;
  }
}
} // namespace

namespace {
bool handleFloatFloatBinOp(EvalInfo &Info, const BinaryOperator *E,
                           APFloat &LHS, BinaryOperatorKind Opcode,
                           const APFloat &RHS) {
  llvm::RoundingMode RM = getActiveRoundingMode(Info, E);
  APFloat::opStatus St;
  switch (Opcode) {
  default:
    Info.FFDiag(E);
    return false;
  case BO_Mul:
    St = LHS.multiply(RHS, RM);
    break;
  case BO_Add:
    St = LHS.add(RHS, RM);
    break;
  case BO_Sub:
    St = LHS.subtract(RHS, RM);
    break;
  case BO_Div:
    // Division (and remainder) by zero: undefined.
    if (RHS.isZero())
      Info.CCEDiag(E, diag::note_expr_divide_by_zero);
    St = LHS.divide(RHS, RM);
    break;
  }

  // NaN results are treated as undefined behavior for constexpr folding (not
  // full IEEE 754 semantics).
  if (LHS.isNaN()) {
    Info.CCEDiag(E, diag::note_constexpr_float_arithmetic) << LHS.isNaN();
    return Info.noteUndefinedBehavior();
  }

  return checkFloatingPointResult(Info, E, St);
}
} // namespace

namespace {
bool handleLogicalOpForVector(const APInt &LHSValue, BinaryOperatorKind Opcode,
                              const APInt &RHSValue, APInt &Result) {
  bool LHS = (LHSValue != 0);
  bool RHS = (RHSValue != 0);

  if (Opcode == BO_LAnd)
    Result = LHS && RHS;
  else
    Result = LHS || RHS;
  return true;
}
} // namespace
namespace {
bool handleLogicalOpForVector(const APFloat &LHSValue,
                              BinaryOperatorKind Opcode,
                              const APFloat &RHSValue, APInt &Result) {
  bool LHS = !LHSValue.isZero();
  bool RHS = !RHSValue.isZero();

  if (Opcode == BO_LAnd)
    Result = LHS && RHS;
  else
    Result = LHS || RHS;
  return true;
}
} // namespace

namespace {
bool handleLogicalOpForVector(const APValue &LHSValue,
                              BinaryOperatorKind Opcode,
                              const APValue &RHSValue, APInt &Result) {
  // The result is always an int type, however operands match the first.
  if (LHSValue.getKind() == APValue::Int)
    return handleLogicalOpForVector(LHSValue.getInt(), Opcode,
                                    RHSValue.getInt(), Result);
  assert(LHSValue.getKind() == APValue::Float && "Should be no other options");
  return handleLogicalOpForVector(LHSValue.getFloat(), Opcode,
                                  RHSValue.getFloat(), Result);
}
} // namespace

namespace {
template <typename APTy>
bool handleCompareOpForVectorHelper(const APTy &LHSValue,
                                    BinaryOperatorKind Opcode,
                                    const APTy &RHSValue, APInt &Result) {
  switch (Opcode) {
  default:
    llvm_unreachable("unsupported binary operator");
  case BO_EQ:
    Result = (LHSValue == RHSValue);
    break;
  case BO_NE:
    Result = (LHSValue != RHSValue);
    break;
  case BO_LT:
    Result = (LHSValue < RHSValue);
    break;
  case BO_GT:
    Result = (LHSValue > RHSValue);
    break;
  case BO_LE:
    Result = (LHSValue <= RHSValue);
    break;
  case BO_GE:
    Result = (LHSValue >= RHSValue);
    break;
  }

  // The boolean operations on these vector types use an instruction that
  // results in a mask of '-1' for the 'truth' value.  Ensure that we negate 1
  // to -1 to make sure that we produce the correct value.
  Result.negate();

  return true;
}
} // namespace

namespace {
bool handleCompareOpForVector(const APValue &LHSValue,
                              BinaryOperatorKind Opcode,
                              const APValue &RHSValue, APInt &Result) {
  // The result is always an int type, however operands match the first.
  if (LHSValue.getKind() == APValue::Int)
    return handleCompareOpForVectorHelper(LHSValue.getInt(), Opcode,
                                          RHSValue.getInt(), Result);
  assert(LHSValue.getKind() == APValue::Float && "Should be no other options");
  return handleCompareOpForVectorHelper(LHSValue.getFloat(), Opcode,
                                        RHSValue.getFloat(), Result);
}
} // namespace

// Perform binary operations for vector types, in place on the LHS.
namespace {
bool handleVectorVectorBinOp(EvalInfo &Info, const BinaryOperator *E,
                             BinaryOperatorKind Opcode, APValue &LHSValue,
                             const APValue &RHSValue) {
  const auto *VT = E->getType()->castAs<VectorType>();
  unsigned NumElements = VT->getNumElements();
  QualType EltTy = VT->getElementType();

  // In the cases (typically C as I've observed) where we aren't evaluating
  // constexpr but are checking for cases where the LHS isn't yet evaluatable,
  // just give up.
  if (!LHSValue.isVector()) {
    assert(LHSValue.isLValue() &&
           "A vector result that isn't a vector OR uncalculated LValue");
    Info.FFDiag(E);
    return false;
  }

  assert(LHSValue.getVectorLength() == NumElements &&
         RHSValue.getVectorLength() == NumElements && "Different vector sizes");

  llvm::SmallVector<APValue, 4> ResultElements;

  for (unsigned EltNum = 0; EltNum < NumElements; ++EltNum) {
    APValue LHSElt = LHSValue.getVectorElt(EltNum);
    APValue RHSElt = RHSValue.getVectorElt(EltNum);

    if (EltTy->isIntegerType()) {
      APSInt EltResult{Info.Ctx.getIntWidth(EltTy),
                       EltTy->isUnsignedIntegerType()};
      bool Success = true;

      if (BinaryOperator::isLogicalOp(Opcode))
        Success = handleLogicalOpForVector(LHSElt, Opcode, RHSElt, EltResult);
      else if (BinaryOperator::isComparisonOp(Opcode))
        Success = handleCompareOpForVector(LHSElt, Opcode, RHSElt, EltResult);
      else
        Success = handleIntIntBinOp(Info, E, LHSElt.getInt(), Opcode,
                                    RHSElt.getInt(), EltResult);

      if (!Success) {
        Info.FFDiag(E);
        return false;
      }
      ResultElements.emplace_back(EltResult);

    } else if (EltTy->isFloatingType()) {
      assert(LHSElt.getKind() == APValue::Float &&
             RHSElt.getKind() == APValue::Float &&
             "Mismatched LHS/RHS/Result Type");
      APFloat LHSFloat = LHSElt.getFloat();

      if (!handleFloatFloatBinOp(Info, E, LHSFloat, Opcode,
                                 RHSElt.getFloat())) {
        Info.FFDiag(E);
        return false;
      }

      ResultElements.emplace_back(LHSFloat);
    }
  }

  LHSValue = APValue(ResultElements.data(), ResultElements.size());
  return true;
}
} // namespace

namespace {
bool handleLValueMember(EvalInfo &Info, const Expr *E, LValue &LVal,
                        const FieldDecl *FD,
                        const StructRecordLayout *RL = nullptr) {
  if (!RL) {
    if (FD->getParent()->isInvalidDecl())
      return false;
    RL = &Info.Ctx.getStructRecordLayout(FD->getParent());
  }

  unsigned I = FD->getFieldIndex();
  LVal.adjustOffset(Info.Ctx.toCharUnitsFromBits(RL->getFieldOffset(I)));
  LVal.addDecl(Info, E, FD);
  return true;
}
} // namespace

namespace {
bool handleLValueIndirectMember(EvalInfo &Info, const Expr *E, LValue &LVal,
                                const IndirectFieldDecl *IFD) {
  for (const auto *C : IFD->chain())
    if (!handleLValueMember(Info, E, LVal, cast<FieldDecl>(C)))
      return false;
  return true;
}
} // namespace

namespace {
bool handleSizeof(EvalInfo &Info, SourceLocation Loc, QualType Type,
                  CharUnits &Size) {
  // sizeof(void), __alignof__(void), sizeof(function) = 1 as a gcc
  // extension.
  if (Type->isVoidType() || Type->isFunctionType()) {
    Size = CharUnits::One();
    return true;
  }

  if (!Type->isConstantSizeType()) {
    // sizeof(VLA) is not a constant expression.
    Info.FFDiag(Loc);
    return false;
  }

  Size = Info.Ctx.getTypeSizeInChars(Type);
  return true;
}
} // namespace

namespace {
bool handleLValueArrayAdjustment(EvalInfo &Info, const Expr *E, LValue &LVal,
                                 QualType EltTy, APSInt Adjustment) {
  CharUnits SizeOfPointee;
  if (!handleSizeof(Info, E->getExprLoc(), EltTy, SizeOfPointee))
    return false;

  LVal.adjustOffsetAndIndex(Info, E, Adjustment, SizeOfPointee);
  return true;
}
} // namespace

namespace {
bool handleLValueArrayAdjustment(EvalInfo &Info, const Expr *E, LValue &LVal,
                                 QualType EltTy, int64_t Adjustment) {
  return handleLValueArrayAdjustment(Info, E, LVal, EltTy,
                                     APSInt::get(Adjustment));
}
} // namespace

namespace {
bool handleLValueComplexElement(EvalInfo &Info, const Expr *E, LValue &LVal,
                                QualType EltTy, bool Imag) {
  if (Imag) {
    CharUnits SizeOfComponent;
    if (!handleSizeof(Info, E->getExprLoc(), EltTy, SizeOfComponent))
      return false;
    LVal.Offset += SizeOfComponent;
  }
  LVal.addComplex(Info, E, EltTy, Imag);
  return true;
}
} // namespace

namespace {
bool evaluateVarDeclInit(EvalInfo &Info, const Expr *E, const VarDecl *VD,
                         CallStackFrame *Frame, unsigned Version,
                         APValue *&Result) {
  APValue::LValueBase Base(VD, Frame ? Frame->Index : 0, Version);

  // If this is a local variable, dig out its value.
  if (Frame) {
    Result = Frame->getTemporary(VD, Version);
    if (Result)
      return true;

    if (!isa<ParmVarDecl>(VD)) {
      return false;
    }
  }

  // If we're currently evaluating the initializer of this declaration, use that
  // in-flight value.
  if (Info.EvaluatingDecl == Base) {
    Result = Info.EvaluatingDeclValue;
    return true;
  }

  if (isa<ParmVarDecl>(VD)) {
    // Assume parameters of a potential constant expression are usable in
    // constant expressions.
    if (!Info.checkingPotentialConstantExpression() ||
        !Info.CurrentCall->Callee ||
        !Info.CurrentCall->Callee->Equals(VD->getDeclContext())) {
      Info.FFDiag(E);
    }
    return false;
  }

  // Dig out the initializer, and use the declaration which it's attached to.
  const Expr *Init = VD->getAnyInitializer(VD);
  if (!Init) {
    // Don't diagnose during potential constant expression checking; an
    // initializer might be added later.
    if (!Info.checkingPotentialConstantExpression()) {
      Info.FFDiag(E, diag::note_constexpr_var_init_unknown, 1) << VD;
      noteLValueLocation(Info, Base);
    }
    return false;
  }

  // Check that we can fold the initializer (already checked elsewhere when
  // required for language conformance).
  if (!VD->evaluateValue()) {
    Info.FFDiag(E, diag::note_constexpr_var_init_non_constant, 1) << VD;
    noteLValueLocation(Info, Base);
    return false;
  }

  // Usable-in-constexpr: a const integral (or similar) may still be rejected
  // if it is not actually usable in constant expressions (non-constant init,
  // etc.); older dialects also require ICE syntax where applicable.
  //
  if (VD->isWeak()) {
    Info.FFDiag(E, diag::note_constexpr_var_init_weak) << VD;
    noteLValueLocation(Info, Base);
    return false;
  }

  Result = VD->getEvaluatedValue();
  return true;
}
} // namespace

namespace {
APSInt extractStringLiteralCharacter(EvalInfo &Info, const Expr *Lit,
                                     uint64_t Index) {
  assert(!isa<SourceLocExpr>(Lit) &&
         "SourceLocExpr should have already been converted to a StringLiteral");

  if (auto PE = dyn_cast<PredefinedExpr>(Lit))
    Lit = PE->getFunctionName();
  const StringLiteral *S = cast<StringLiteral>(Lit);
  const ConstantArrayType *CAT = Info.Ctx.getAsConstantArrayType(S->getType());
  assert(CAT && "string literal isn't an array");
  QualType CharType = CAT->getElementType();
  assert(CharType->isIntegerType() && "unexpected character type");
  APSInt Value(Info.Ctx.getTypeSize(CharType),
               CharType->isUnsignedIntegerType());
  if (Index < S->getLength())
    Value = S->getCodeUnit(Index);
  return Value;
}
} // namespace

// Expand a string literal into an array of characters.
//
namespace {
void expandStringLiteral(EvalInfo &Info, const StringLiteral *S,
                         APValue &Result, QualType AllocType = QualType()) {
  const ConstantArrayType *CAT = Info.Ctx.getAsConstantArrayType(
      AllocType.isNull() ? S->getType() : AllocType);
  assert(CAT && "string literal isn't an array");
  QualType CharType = CAT->getElementType();
  assert(CharType->isIntegerType() && "unexpected character type");

  unsigned Elts = CAT->getSize().getZExtValue();
  Result =
      APValue(APValue::UninitArray(), std::min(S->getLength(), Elts), Elts);
  APSInt Value(Info.Ctx.getTypeSize(CharType),
               CharType->isUnsignedIntegerType());
  if (Result.hasArrayFiller())
    Result.getArrayFiller() = APValue(Value);
  for (unsigned I = 0, N = Result.getArrayInitializedElts(); I != N; ++I) {
    Value = S->getCodeUnit(I);
    Result.getArrayInitializedElt(I) = APValue(Value);
  }
}
} // namespace

// Expand an array so that it has more than Index filled elements.
namespace {
void expandArray(APValue &Array, unsigned Index) {
  unsigned Size = Array.getArraySize();
  assert(Index < Size);

  // Always at least double the number of elements for which we store a value.
  unsigned OldElts = Array.getArrayInitializedElts();
  unsigned NewElts = std::max(Index + 1, OldElts * 2);
  NewElts = std::min(Size, std::max(NewElts, 8u));

  // Copy the data across.
  APValue NewValue(APValue::UninitArray(), NewElts, Size);
  for (unsigned I = 0; I != OldElts; ++I)
    NewValue.getArrayInitializedElt(I).swap(Array.getArrayInitializedElt(I));
  for (unsigned I = OldElts; I != NewElts; ++I)
    NewValue.getArrayInitializedElt(I) = Array.getArrayFiller();
  if (NewValue.hasArrayFiller())
    NewValue.getArrayFiller() = Array.getArrayFiller();
  Array.swap(NewValue);
}
} // namespace

namespace {
bool isReadByLvalueToRvalueConversion(const RecordDecl *RD);
} // namespace
namespace {
bool isReadByLvalueToRvalueConversion(QualType T) {
  RecordDecl *RD = T->getBaseElementTypeUnsafe()->getAsRecordDecl();
  return !RD || isReadByLvalueToRvalueConversion(RD);
}
} // namespace
namespace {
bool isReadByLvalueToRvalueConversion(const RecordDecl *RD) {
  if (RD->isUnion())
    return !RD->field_empty();
  for (auto *Field : RD->fields())
    if (!Field->isUnnamedBitfield() &&
        isReadByLvalueToRvalueConversion(Field->getType()))
      return true;

  return false;
}
} // namespace

namespace {
bool checkArraySize(EvalInfo &Info, const ConstantArrayType *CAT,
                    SourceLocation CallLoc = {}) {
  return Info.checkArraySize(
      CAT->getSizeExpr() ? CAT->getSizeExpr()->getBeginLoc() : CallLoc,
      CAT->getNumAddressingBits(Info.Ctx), CAT->getSize().getZExtValue(),
      /*Diag=*/true);
}
} // namespace

namespace {
struct CompleteObject {
  APValue::LValueBase Base;
  APValue *Value;
  QualType Type;

  CompleteObject() : Value(nullptr) {}
  CompleteObject(APValue::LValueBase Base, APValue *Value, QualType Type)
      : Base(Base), Value(Value), Type(Type) {}

  explicit operator bool() const { return !Type.isNull(); }
};
} // end anonymous namespace

namespace {
QualType getSubobjectType(QualType ObjType, QualType SubobjType, bool = false) {
  if (ObjType.isConstQualified())
    SubobjType.addConst();
  if (ObjType.isVolatileQualified())
    SubobjType.addVolatile();
  return SubobjType;
}
} // namespace

template <typename SubobjectHandler>
typename SubobjectHandler::result_type
findSubobject(EvalInfo &Info, const Expr *E, const CompleteObject &Obj,
              const SubobjectDesignator &Sub, SubobjectHandler &handler) {
  if (Sub.Invalid)
    // A diagnostic will have already been produced.
    return handler.failed();
  if (Sub.isOnePastTheEnd() || Sub.isMostDerivedAnUnsizedArray()) {
    Info.FFDiag(E);
    return handler.failed();
  }

  APValue *O = Obj.Value;
  QualType ObjType = Obj.Type;
  const FieldDecl *LastField = nullptr;

  // Walk the designator's path to find the subobject.
  for (unsigned I = 0, N = Sub.Entries.size(); /**/; ++I) {
    // Reading an indeterminate value is undefined, but assigning over one is
    // OK.
    if (O->isAbsent() || (O->isIndeterminate() &&
                          !isValidIndeterminateAccess(handler.AccessKind))) {
      if (!Info.checkingPotentialConstantExpression())
        Info.FFDiag(E, diag::note_constexpr_access_uninit)
            << handler.AccessKind << O->isIndeterminate()
            << E->getSourceRange();
      return handler.failed();
    }

    // If this is our last pass, check that the final object type is OK.
    if (I == N || (I == N - 1 && ObjType->isAnyComplexType())) {
      // Accesses to volatile objects are prohibited.
      if (ObjType.isVolatileQualified() && isAnyAccess(handler.AccessKind)) {
        Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
        return handler.failed();
      }
    }

    if (I == N) {
      if (!handler.found(*O, ObjType))
        return false;

      // If we modified a bit-field, truncate it to the right width.
      if (isModification(handler.AccessKind) && LastField &&
          LastField->isBitField() &&
          !truncateBitfieldValue(Info, E, *O, LastField))
        return false;

      return true;
    }

    LastField = nullptr;
    if (ObjType->isArrayType()) {
      // Next subobject is an array element.
      const ConstantArrayType *CAT = Info.Ctx.getAsConstantArrayType(ObjType);
      assert(CAT && "vla in literal type?");
      uint64_t Index = Sub.Entries[I].getAsArrayIndex();
      if (CAT->getSize().ule(Index)) {
        Info.FFDiag(E);
        return handler.failed();
      }

      ObjType = CAT->getElementType();

      if (O->getArrayInitializedElts() > Index)
        O = &O->getArrayInitializedElt(Index);
      else if (!isRead(handler.AccessKind)) {
        if (!checkArraySize(Info, CAT, E->getExprLoc()))
          return handler.failed();

        expandArray(*O, Index);
        O = &O->getArrayInitializedElt(Index);
      } else
        O = &O->getArrayFiller();
    } else if (ObjType->isAnyComplexType()) {
      // Next subobject is a complex number.
      uint64_t Index = Sub.Entries[I].getAsArrayIndex();
      if (Index > 1) {
        Info.FFDiag(E);
        return handler.failed();
      }

      ObjType = getSubobjectType(
          ObjType, ObjType->castAs<ComplexType>()->getElementType());

      assert(I == N - 1 && "extracting subobject of scalar?");
      if (O->isComplexInt()) {
        return handler.found(
            Index ? O->getComplexIntImag() : O->getComplexIntReal(), ObjType);
      } else {
        assert(O->isComplexFloat());
        return handler.found(Index ? O->getComplexFloatImag()
                                   : O->getComplexFloatReal(),
                             ObjType);
      }
    } else {
      const FieldDecl *Field = getAsField(Sub.Entries[I]);
      assert(Field && "subobject path steps must be struct fields");

      // Next subobject is a struct or union field.
      RecordDecl *RD = ObjType->castAs<RecordType>()->getDecl();
      if (RD->isUnion()) {
        const FieldDecl *UnionField = O->getUnionField();
        if (!UnionField ||
            UnionField->getCanonicalDecl() != Field->getCanonicalDecl()) {
          Info.FFDiag(E, diag::note_constexpr_access_inactive_union_member)
              << handler.AccessKind << Field << !UnionField << UnionField;
          return handler.failed();
        }
        O = &O->getUnionValue();
      } else
        O = &O->getStructField(Field->getFieldIndex());

      ObjType = getSubobjectType(ObjType, Field->getType(), false);
      LastField = Field;
    }
  }
}

namespace {
struct ExtractSubobjectHandler {
  EvalInfo &Info;
  const Expr *E;
  APValue &Result;
  const AccessKinds AccessKind;

  typedef bool result_type;
  bool failed() { return false; }
  bool found(APValue &Subobj, QualType SubobjType) {
    Result = Subobj;
    if (AccessKind == AK_ReadObjectRepresentation)
      return true;
    return checkFullyInitialized(Info, E->getExprLoc(), SubobjType, Result);
  }
  bool found(APSInt &Value, QualType SubobjType) {
    Result = APValue(Value);
    return true;
  }
  bool found(APFloat &Value, QualType SubobjType) {
    Result = APValue(Value);
    return true;
  }
};
} // end anonymous namespace

namespace {
bool extractSubobject(EvalInfo &Info, const Expr *E, const CompleteObject &Obj,
                      const SubobjectDesignator &Sub, APValue &Result,
                      AccessKinds AK = AK_Read) {
  assert(AK == AK_Read || AK == AK_ReadObjectRepresentation);
  ExtractSubobjectHandler Handler = {Info, E, Result, AK};
  return findSubobject(Info, E, Obj, Sub, Handler);
}
} // namespace

namespace {
unsigned findDesignatorMismatch(QualType ObjType, const SubobjectDesignator &A,
                                const SubobjectDesignator &B,
                                bool &WasArrayIndex) {
  unsigned I = 0, N = std::min(A.Entries.size(), B.Entries.size());
  for (/**/; I != N; ++I) {
    if (!ObjType.isNull() &&
        (ObjType->isArrayType() || ObjType->isAnyComplexType())) {
      // Next subobject is an array element.
      if (A.Entries[I].getAsArrayIndex() != B.Entries[I].getAsArrayIndex()) {
        WasArrayIndex = true;
        return I;
      }
      if (ObjType->isAnyComplexType())
        ObjType = ObjType->castAs<ComplexType>()->getElementType();
      else
        ObjType = ObjType->castAsArrayTypeUnsafe()->getElementType();
    } else {
      if (A.Entries[I].getAsField() != B.Entries[I].getAsField()) {
        WasArrayIndex = false;
        return I;
      }
      const FieldDecl *FD = getAsField(A.Entries[I]);
      assert(FD && "designator path must use struct fields");
      ObjType = FD->getType();
    }
  }
  WasArrayIndex = false;
  return I;
}
} // namespace

namespace {
bool areElementsOfSameArray(QualType ObjType, const SubobjectDesignator &A,
                            const SubobjectDesignator &B) {
  if (A.Entries.size() != B.Entries.size())
    return false;

  bool IsArray = A.MostDerivedIsArrayElement;
  if (IsArray && A.MostDerivedPathLength != A.Entries.size())
    // A is a subobject of the array element.
    return false;

  // If A (and B) designates an array element, the last entry will be the array
  // index. That doesn't have to match. Otherwise, we're in the 'implicit array
  // of length 1' case, and the entire path must match.
  bool WasArrayIndex;
  unsigned CommonLength = findDesignatorMismatch(ObjType, A, B, WasArrayIndex);
  return CommonLength >= A.Entries.size() - IsArray;
}
} // namespace

namespace {
CompleteObject findCompleteObject(EvalInfo &Info, const Expr *E, AccessKinds AK,
                                  const LValue &LVal, QualType LValType) {
  if (LVal.InvalidBase) {
    Info.FFDiag(E);
    return CompleteObject();
  }

  if (!LVal.Base) {
    Info.FFDiag(E, diag::note_constexpr_access_null) << AK;
    return CompleteObject();
  }

  CallStackFrame *Frame = nullptr;
  unsigned Depth = 0;
  if (LVal.getLValueCallIndex()) {
    std::tie(Frame, Depth) =
        Info.getCallFrameAndDepth(LVal.getLValueCallIndex());
    if (!Frame) {
      Info.FFDiag(E, diag::note_constexpr_lifetime_ended, 1)
          << AK << LVal.Base.is<const ValueDecl *>();
      noteLValueLocation(Info, LVal.Base);
      return CompleteObject();
    }
  }

  bool IsAccess = isAnyAccess(AK);

  // Reading a volatile-qualified expression (even if the stored object is not
  // volatile) is not a core constant expression.
  if (isAnyAccess(AK) && LValType.isVolatileQualified()) {
    Info.FFDiag(E);
    return CompleteObject();
  }

  // Compute value storage location and type of base object.
  APValue *BaseVal = nullptr;
  QualType BaseType = getType(LVal.Base);

  if (const ValueDecl *D = LVal.Base.dyn_cast<const ValueDecl *>()) {
    // ICE/constexpr rules differ by language; locals with a stack Frame are
    // handled below. In C, some cases fold without being ICEs.
    const VarDecl *VD = dyn_cast<VarDecl>(D);
    if (VD) {
      if (const VarDecl *VDef = VD->getDefinition(Info.Ctx))
        VD = VDef;
    }
    if (!VD || VD->isInvalidDecl()) {
      Info.FFDiag(E);
      return CompleteObject();
    }

    bool IsConstant = BaseType.isConstant(Info.Ctx);

    // Unless we're looking at a local variable or argument in a constexpr call,
    // the variable we're reading must be const.
    if (!Frame) {
      if (IsAccess && isa<ParmVarDecl>(VD)) {
        // Access of a parameter that's not associated with a frame isn't going
        // to work out, but we can leave it to evaluateVarDeclInit to provide a
        // suitable diagnostic.
      } else if (isModification(AK)) {
        // All the remaining cases do not permit modification of the object.
        Info.FFDiag(E, diag::note_constexpr_modify_global);
        return CompleteObject();
      } else if (VD->isConstexpr()) {
        // OK, we can read this variable.
      } else if (BaseType->isIntegralOrEnumerationType()) {
        if (!IsConstant) {
          if (!IsAccess)
            return CompleteObject(LVal.getLValueBase(), nullptr, BaseType);
          Info.FFDiag(E);
          return CompleteObject();
        }
      } else if (!IsAccess) {
        return CompleteObject(LVal.getLValueBase(), nullptr, BaseType);
      } else if (IsConstant && Info.checkingPotentialConstantExpression() &&
                 BaseType->isLiteralType(Info.Ctx) && !VD->hasDefinition()) {
      } else if (IsConstant) {
        Info.CCEDiag(E);
      } else {
        Info.FFDiag(E);
        return CompleteObject();
      }
    }

    if (!evaluateVarDeclInit(Info, E, VD, Frame, LVal.getLValueVersion(),
                             BaseVal))
      return CompleteObject();
  } else {
    const Expr *Base = LVal.Base.dyn_cast<const Expr *>();

    if (!Frame) {
      if (!IsAccess)
        return CompleteObject(LVal.getLValueBase(), nullptr, BaseType);
      APValue Val;
      LVal.moveInto(Val);
      Info.FFDiag(E, diag::note_constexpr_access_unreadable_object)
          << AK << Val.getAsString(Info.Ctx, LValType);
      noteLValueLocation(Info, LVal.Base);
      return CompleteObject();
    } else {
      BaseVal = Frame->getTemporary(Base, LVal.Base.getVersion());
      assert(BaseVal && "missing value for temporary");
    }
  }

  // After a speculative call, outer mutable state may be unmodeled; block
  // modifications that would observe it. Parameters are caller state and
  // remain visible across speculated calls.
  //
  unsigned VisibleDepth = Depth;
  if (llvm::isa_and_nonnull<ParmVarDecl>(
          LVal.Base.dyn_cast<const ValueDecl *>()))
    ++VisibleDepth;
  if (isModification(AK) && VisibleDepth < Info.SpeculativeEvaluationDepth)
    return CompleteObject();

  return CompleteObject(LVal.getLValueBase(), BaseVal, BaseType);
}
} // namespace

namespace {
bool handleLValueToRValueConversion(EvalInfo &Info, const Expr *Conv,
                                    QualType Type, const LValue &LVal,
                                    APValue &RVal,
                                    bool WantObjectRepresentation = false) {
  if (LVal.Designator.Invalid)
    return false;

  // Check for special cases where there is no existing APValue to look at.
  const Expr *Base = LVal.Base.dyn_cast<const Expr *>();

  AccessKinds AK =
      WantObjectRepresentation ? AK_ReadObjectRepresentation : AK_Read;

  if (Base && !LVal.getLValueCallIndex() && !Type.isVolatileQualified()) {
    if (const CompoundLiteralExpr *CLE = dyn_cast<CompoundLiteralExpr>(Base)) {
      // In C99, a CompoundLiteralExpr is an lvalue, and we defer evaluating the
      // initializer until now for such expressions. Such an expression can't be
      // an ICE in C, so this only matters for fold.
      if (Type.isVolatileQualified()) {
        Info.FFDiag(Conv);
        return false;
      }

      APValue Lit;
      if (!evaluate(Lit, Info, CLE->getInitializer()))
        return false;

      // Match GCC extended lifetime rules for array compound literals (see GCC
      // manual "Compound Literals"); require const where GCC gives static
      // storage when converting to a scalar.

      QualType CLETy = CLE->getType();
      if (CLETy->isArrayType() && !Type->isArrayType()) {
        if (!CLETy.isConstant(Info.Ctx)) {
          Info.FFDiag(Conv);
          Info.Note(CLE->getExprLoc(), diag::note_declared_at);
          return false;
        }
      }

      CompleteObject LitObj(LVal.Base, &Lit, Base->getType());
      return extractSubobject(Info, Conv, LitObj, LVal.Designator, RVal, AK);
    } else if (isa<StringLiteral>(Base) || isa<PredefinedExpr>(Base)) {
      // Special-case character extraction so we don't have to construct an
      // APValue for the whole string.
      assert(LVal.Designator.Entries.size() <= 1 &&
             "Can only read characters from string literals");
      if (LVal.Designator.Entries.empty()) {
        // Fail for now for LValue-to-RValue conversion of a whole array without
        // an element designator (unusual API/tooling use).
        Info.FFDiag(Conv);
        return false;
      }
      if (LVal.Designator.isOnePastTheEnd()) {
        Info.FFDiag(Conv);
        return false;
      }
      uint64_t CharIndex = LVal.Designator.Entries[0].getAsArrayIndex();
      RVal = APValue(extractStringLiteralCharacter(Info, Base, CharIndex));
      return true;
    }
  }

  CompleteObject Obj = findCompleteObject(Info, Conv, AK, LVal, Type);
  return Obj && extractSubobject(Info, Conv, Obj, LVal.Designator, RVal, AK);
}
} // namespace

namespace {
bool handleAssignment(EvalInfo &Info, const Expr *E, const LValue &LVal,
                      QualType LValType, APValue &Val) {
  if (LVal.Designator.Invalid)
    return false;
  Info.FFDiag(E);
  return false;
}
} // namespace

namespace {
bool handleCompoundAssignment(EvalInfo &Info, const CompoundAssignOperator *E,
                              const LValue &LVal, QualType LValType,
                              QualType PromotedLValType,
                              BinaryOperatorKind Opcode, const APValue &RVal) {
  if (LVal.Designator.Invalid)
    return false;
  Info.FFDiag(E);
  return false;
}
} // namespace

namespace {
struct IncDecSubobjectHandler {
  EvalInfo &Info;
  const UnaryOperator *E;
  AccessKinds AccessKind;
  APValue *Old;

  typedef bool result_type;

  bool checkConst(QualType QT) {
    // Assigning to a const object has undefined behavior.
    if (QT.isConstQualified()) {
      Info.FFDiag(E, diag::note_constexpr_modify_const_type) << QT;
      return false;
    }
    return true;
  }

  bool failed() { return false; }
  bool found(APValue &Subobj, QualType SubobjType) {
    // Stash the old value. Also clear Old, so we don't clobber it later
    // if we're post-incrementing a complex.
    if (Old) {
      *Old = Subobj;
      Old = nullptr;
    }

    switch (Subobj.getKind()) {
    case APValue::Int:
      return found(Subobj.getInt(), SubobjType);
    case APValue::Float:
      return found(Subobj.getFloat(), SubobjType);
    case APValue::ComplexInt:
      return found(
          Subobj.getComplexIntReal(),
          SubobjType->castAs<ComplexType>()->getElementType().withCVRQualifiers(
              SubobjType.getCVRQualifiers()));
    case APValue::ComplexFloat:
      return found(
          Subobj.getComplexFloatReal(),
          SubobjType->castAs<ComplexType>()->getElementType().withCVRQualifiers(
              SubobjType.getCVRQualifiers()));
    case APValue::LValue:
      return foundPointer(Subobj, SubobjType);
    default:
      Info.FFDiag(E);
      return false;
    }
  }
  bool found(APSInt &Value, QualType SubobjType) {
    if (!checkConst(SubobjType))
      return false;

    if (!SubobjType->isIntegerType()) {
      // We don't support increment / decrement on integer-cast-to-pointer
      // values.
      Info.FFDiag(E);
      return false;
    }

    if (Old)
      *Old = APValue(Value);

    // bool arithmetic promotes to int, and the conversion back to bool
    // doesn't reduce mod 2^n, so special-case it.
    if (SubobjType->isBooleanType()) {
      if (AccessKind == AK_Increment)
        Value = 1;
      else
        Value = !Value;
      return true;
    }

    bool WasNegative = Value.isNegative();
    if (AccessKind == AK_Increment) {
      ++Value;

      if (!WasNegative && Value.isNegative() && E->canOverflow()) {
        APSInt ActualValue(Value, /*IsUnsigned*/ true);
        return handleOverflow(Info, E, ActualValue, SubobjType);
      }
    } else {
      --Value;

      if (WasNegative && !Value.isNegative() && E->canOverflow()) {
        unsigned BitWidth = Value.getBitWidth();
        APSInt ActualValue(Value.sext(BitWidth + 1), /*IsUnsigned*/ false);
        ActualValue.setBit(BitWidth);
        return handleOverflow(Info, E, ActualValue, SubobjType);
      }
    }
    return true;
  }
  bool found(APFloat &Value, QualType SubobjType) {
    if (!checkConst(SubobjType))
      return false;

    if (Old)
      *Old = APValue(Value);

    APFloat One(Value.getSemantics(), 1);
    llvm::RoundingMode RM = getActiveRoundingMode(Info, E);
    APFloat::opStatus St;
    if (AccessKind == AK_Increment)
      St = Value.add(One, RM);
    else
      St = Value.subtract(One, RM);
    return checkFloatingPointResult(Info, E, St);
  }
  bool foundPointer(APValue &Subobj, QualType SubobjType) {
    if (!checkConst(SubobjType))
      return false;

    QualType PointeeType;
    if (const PointerType *PT = SubobjType->getAs<PointerType>())
      PointeeType = PT->getPointeeType();
    else {
      Info.FFDiag(E);
      return false;
    }

    LValue LVal;
    LVal.setFrom(Info.Ctx, Subobj);
    if (!handleLValueArrayAdjustment(Info, E, LVal, PointeeType,
                                     AccessKind == AK_Increment ? 1 : -1))
      return false;
    LVal.moveInto(Subobj);
    return true;
  }
};
} // end anonymous namespace

namespace {
bool handleIncDec(EvalInfo &Info, const Expr *E, const LValue &LVal,
                  QualType LValType) {
  if (LVal.Designator.Invalid)
    return false;
  Info.FFDiag(E);
  return false;
}
} // namespace

namespace {
bool handleDefaultInitValue(QualType T, APValue &Result) {
  bool Success = true;

  // If there is already a value present don't overwrite it.
  if (!Result.isAbsent())
    return true;

  if (auto *RD = T->getAsRecordDecl()) {
    if (RD->isInvalidDecl()) {
      Result = APValue();
      return false;
    }
    if (RD->isUnion()) {
      Result = APValue((const FieldDecl *)nullptr);
      return true;
    }
    Result = APValue(APValue::UninitStruct(),
                     std::distance(RD->field_begin(), RD->field_end()));

    for (const auto *I : RD->fields()) {
      if (I->isUnnamedBitfield())
        continue;
      Success &= handleDefaultInitValue(
          I->getType(), Result.getStructField(I->getFieldIndex()));
    }
    return Success;
  }

  if (auto *AT =
          dyn_cast_or_null<ConstantArrayType>(T->getAsArrayTypeUnsafe())) {
    Result = APValue(APValue::UninitArray(), 0, AT->getSize().getZExtValue());
    if (Result.hasArrayFiller())
      Success &=
          handleDefaultInitValue(AT->getElementType(), Result.getArrayFiller());

    return Success;
  }

  Result = APValue::IndeterminateValue();
  return true;
}
} // namespace

namespace {
enum EvalStmtResult {
  ESR_Failed,
  ESR_Returned,
  ESR_Succeeded,
  ESR_Continue,
  ESR_Break,
  ESR_CaseNotFound
};
}

namespace {
bool evaluateVarDecl(EvalInfo &Info, const VarDecl *VD) {
  if (VD->isInvalidDecl())
    return false;
  // We don't need to evaluate the initializer for a static local.
  if (!VD->hasLocalStorage())
    return true;

  LValue Result;
  APValue &Val = Info.CurrentCall->createTemporary(VD, VD->getType(),
                                                   ScopeKind::Block, Result);

  const Expr *InitE = VD->getInit();
  if (!InitE)
    return handleDefaultInitValue(VD->getType(), Val);

  if (!evaluateInPlace(Val, Info, Result, InitE)) {
    // Wipe out any partially-computed value, to allow tracking that this
    // evaluation failed.
    Val = APValue();
    return false;
  }

  return true;
}
} // namespace

namespace {
bool evaluateDecl(EvalInfo &Info, const Decl *D) {
  bool OK = true;

  if (const VarDecl *VD = dyn_cast<VarDecl>(D))
    OK &= evaluateVarDecl(Info, VD);

  return OK;
}
} // namespace

namespace {
bool evaluateCond(EvalInfo &Info, const VarDecl *CondDecl, const Expr *Cond,
                  bool &Result) {
  FullExpressionRAII Scope(Info);
  if (CondDecl && !evaluateDecl(Info, CondDecl))
    return false;
  if (!evaluateAsBooleanCondition(Cond, Result, Info))
    return false;
  return Scope.destroy();
}
} // namespace

namespace {
struct StmtResult {
  APValue &Value;
  const LValue *Slot;
};

struct TempVersionRAII {
  CallStackFrame &Frame;

  TempVersionRAII(CallStackFrame &Frame) : Frame(Frame) {
    Frame.pushTempVersion();
  }

  ~TempVersionRAII() { Frame.popTempVersion(); }
};

} // namespace

namespace {
EvalStmtResult evaluateStmt(StmtResult &Result, EvalInfo &Info, const Stmt *S,
                            const SwitchCase *SC = nullptr);
} // namespace

namespace {
EvalStmtResult evaluateLoopBody(StmtResult &Result, EvalInfo &Info,
                                const Stmt *Body,
                                const SwitchCase *Case = nullptr) {
  BlockScopeRAII Scope(Info);

  EvalStmtResult ESR = evaluateStmt(Result, Info, Body, Case);
  if (ESR != ESR_Failed && ESR != ESR_CaseNotFound && !Scope.destroy())
    ESR = ESR_Failed;

  switch (ESR) {
  case ESR_Break:
    return ESR_Succeeded;
  case ESR_Succeeded:
  case ESR_Continue:
    return ESR_Continue;
  case ESR_Failed:
  case ESR_Returned:
  case ESR_CaseNotFound:
    return ESR;
  }
  llvm_unreachable("Invalid EvalStmtResult!");
}
} // namespace

namespace {
EvalStmtResult evaluateSwitch(StmtResult &Result, EvalInfo &Info,
                              const SwitchStmt *SS) {
  BlockScopeRAII Scope(Info);

  // evaluate the switch condition.
  APSInt Value;
  {
    if (const Stmt *Init = SS->getInit()) {
      EvalStmtResult ESR = evaluateStmt(Result, Info, Init);
      if (ESR != ESR_Succeeded) {
        if (ESR != ESR_Failed && !Scope.destroy())
          ESR = ESR_Failed;
        return ESR;
      }
    }

    FullExpressionRAII CondScope(Info);
    if (SS->getConditionVariable() &&
        !evaluateDecl(Info, SS->getConditionVariable()))
      return ESR_Failed;
    if (!evaluateInteger(SS->getCond(), Value, Info))
      return ESR_Failed;

    if (!CondScope.destroy())
      return ESR_Failed;
  }

  const SwitchCase *Found = nullptr;
  for (const SwitchCase *SC = SS->getSwitchCaseList(); SC;
       SC = SC->getNextSwitchCase()) {
    if (isa<DefaultStmt>(SC)) {
      Found = SC;
      continue;
    }

    const CaseStmt *CS = cast<CaseStmt>(SC);
    APSInt LHS = CS->getLHS()->EvaluateKnownConstInt(Info.Ctx);
    APSInt RHS =
        CS->getRHS() ? CS->getRHS()->EvaluateKnownConstInt(Info.Ctx) : LHS;
    if (LHS <= Value && Value <= RHS) {
      Found = SC;
      break;
    }
  }

  if (!Found)
    return Scope.destroy() ? ESR_Succeeded : ESR_Failed;

  // Search the switch body for the switch case and evaluate it from there.
  EvalStmtResult ESR = evaluateStmt(Result, Info, SS->getBody(), Found);
  if (ESR != ESR_Failed && ESR != ESR_CaseNotFound && !Scope.destroy())
    return ESR_Failed;

  switch (ESR) {
  case ESR_Break:
    return ESR_Succeeded;
  case ESR_Succeeded:
  case ESR_Continue:
  case ESR_Failed:
  case ESR_Returned:
    return ESR;
  case ESR_CaseNotFound:
    // This can only happen if the switch case is nested within a statement
    // expression. We have no intention of supporting that.
    Info.FFDiag(Found->getBeginLoc(),
                diag::note_constexpr_stmt_expr_unsupported);
    return ESR_Failed;
  }
  llvm_unreachable("Invalid EvalStmtResult!");
}
} // namespace

namespace {
bool checkLocalVariableDeclaration(EvalInfo &Info, const VarDecl *VD) {
  // Reject control flow that passes a static/thread-local local unless it is
  // constexpr-usable.
  if (VD->isLocalVarDecl() && VD->isStaticLocal()) {
    Info.CCEDiag(VD->getLocation(), diag::note_constexpr_static_local)
        << (VD->getTSCSpec() == TSCS_unspecified ? 0 : 1) << VD;
    return false;
  }
  return true;
}
} // namespace

// evaluate a statement.
namespace {
EvalStmtResult evaluateStmt(StmtResult &Result, EvalInfo &Info, const Stmt *S,
                            const SwitchCase *Case) {
  if (!Info.nextStep(S))
    return ESR_Failed;

  // If we're hunting down a 'case' or 'default' label, recurse through
  // substatements until we hit the label.
  if (Case) {
    switch (S->getStmtClass()) {
    case Stmt::CompoundStmtClass:
    case Stmt::LabelStmtClass:
    case Stmt::AttributedStmtClass:
    case Stmt::DoStmtClass:
      break;

    case Stmt::CaseStmtClass:
    case Stmt::DefaultStmtClass:
      if (Case == S)
        Case = nullptr;
      break;

    case Stmt::IfStmtClass: {
      const IfStmt *IS = cast<IfStmt>(S);

      // Wrap the evaluation in a block scope, in case it's a DeclStmt
      // preceded by our switch label.
      BlockScopeRAII Scope(Info);

      // Step into the init statement in case it brings an (uninitialized)
      // variable into scope.
      if (const Stmt *Init = IS->getInit()) {
        EvalStmtResult ESR = evaluateStmt(Result, Info, Init, Case);
        if (ESR != ESR_CaseNotFound) {
          assert(ESR != ESR_Succeeded);
          return ESR;
        }
      }

      // Condition variable must be initialized if it exists.

      EvalStmtResult ESR = evaluateStmt(Result, Info, IS->getThen(), Case);
      if (ESR == ESR_Failed)
        return ESR;
      if (ESR != ESR_CaseNotFound)
        return Scope.destroy() ? ESR : ESR_Failed;
      if (!IS->getElse())
        return ESR_CaseNotFound;

      ESR = evaluateStmt(Result, Info, IS->getElse(), Case);
      if (ESR == ESR_Failed)
        return ESR;
      if (ESR != ESR_CaseNotFound)
        return Scope.destroy() ? ESR : ESR_Failed;
      return ESR_CaseNotFound;
    }

    case Stmt::WhileStmtClass: {
      EvalStmtResult ESR =
          evaluateLoopBody(Result, Info, cast<WhileStmt>(S)->getBody(), Case);
      if (ESR != ESR_Continue)
        return ESR;
      break;
    }

    case Stmt::ForStmtClass: {
      const ForStmt *FS = cast<ForStmt>(S);
      BlockScopeRAII Scope(Info);

      // Step into the init statement in case it brings an (uninitialized)
      // variable into scope.
      if (const Stmt *Init = FS->getInit()) {
        EvalStmtResult ESR = evaluateStmt(Result, Info, Init, Case);
        if (ESR != ESR_CaseNotFound) {
          assert(ESR != ESR_Succeeded);
          return ESR;
        }
      }

      EvalStmtResult ESR = evaluateLoopBody(Result, Info, FS->getBody(), Case);
      if (ESR != ESR_Continue)
        return ESR;
      if (const auto *Inc = FS->getInc()) {
        FullExpressionRAII IncScope(Info);
        if (!evaluateIgnoredValue(Info, Inc) || !IncScope.destroy())
          return ESR_Failed;
      }
      break;
    }

    case Stmt::DeclStmtClass: {
      // might be used by the selected branch of the switch.
      const DeclStmt *DS = cast<DeclStmt>(S);
      for (const auto *D : DS->decls()) {
        if (const auto *VD = dyn_cast<VarDecl>(D)) {
          if (!checkLocalVariableDeclaration(Info, VD))
            return ESR_Failed;
          if (VD->hasLocalStorage() && !VD->getInit())
            if (!evaluateVarDecl(Info, VD))
              return ESR_Failed;
        }
      }
      return ESR_CaseNotFound;
    }

    default:
      return ESR_CaseNotFound;
    }
  }

  switch (S->getStmtClass()) {
  default:
    if (const Expr *E = dyn_cast<Expr>(S)) {
      FullExpressionRAII Scope(Info);
      if (!evaluateIgnoredValue(Info, E) || !Scope.destroy())
        return ESR_Failed;
      return ESR_Succeeded;
    }

    Info.FFDiag(S->getBeginLoc()) << S->getSourceRange();
    return ESR_Failed;

  case Stmt::NullStmtClass:
    return ESR_Succeeded;

  case Stmt::DeclStmtClass: {
    const DeclStmt *DS = cast<DeclStmt>(S);
    for (const auto *D : DS->decls()) {
      const VarDecl *VD = dyn_cast_or_null<VarDecl>(D);
      if (VD && !checkLocalVariableDeclaration(Info, VD))
        return ESR_Failed;
      // Each declaration initialization is its own full-expression.
      FullExpressionRAII Scope(Info);
      if (!evaluateDecl(Info, D) && !Info.noteFailure())
        return ESR_Failed;
      if (!Scope.destroy())
        return ESR_Failed;
    }
    return ESR_Succeeded;
  }

  case Stmt::ReturnStmtClass: {
    const Expr *RetExpr = cast<ReturnStmt>(S)->getRetValue();
    FullExpressionRAII Scope(Info);
    if (RetExpr && !(Result.Slot ? evaluateInPlace(Result.Value, Info,
                                                   *Result.Slot, RetExpr)
                                 : evaluate(Result.Value, Info, RetExpr)))
      return ESR_Failed;
    return Scope.destroy() ? ESR_Returned : ESR_Failed;
  }

  case Stmt::CompoundStmtClass: {
    BlockScopeRAII Scope(Info);

    const CompoundStmt *CS = cast<CompoundStmt>(S);
    for (const auto *BI : CS->body()) {
      EvalStmtResult ESR = evaluateStmt(Result, Info, BI, Case);
      if (ESR == ESR_Succeeded)
        Case = nullptr;
      else if (ESR != ESR_CaseNotFound) {
        if (ESR != ESR_Failed && !Scope.destroy())
          return ESR_Failed;
        return ESR;
      }
    }
    if (Case)
      return ESR_CaseNotFound;
    return Scope.destroy() ? ESR_Succeeded : ESR_Failed;
  }

  case Stmt::IfStmtClass: {
    const IfStmt *IS = cast<IfStmt>(S);

    // evaluate the condition, as either a var decl or as an expression.
    BlockScopeRAII Scope(Info);
    if (const Stmt *Init = IS->getInit()) {
      EvalStmtResult ESR = evaluateStmt(Result, Info, Init);
      if (ESR != ESR_Succeeded) {
        if (ESR != ESR_Failed && !Scope.destroy())
          return ESR_Failed;
        return ESR;
      }
    }
    bool Cond;
    if (!evaluateCond(Info, IS->getConditionVariable(), IS->getCond(), Cond))
      return ESR_Failed;

    if (const Stmt *SubStmt = Cond ? IS->getThen() : IS->getElse()) {
      EvalStmtResult ESR = evaluateStmt(Result, Info, SubStmt);
      if (ESR != ESR_Succeeded) {
        if (ESR != ESR_Failed && !Scope.destroy())
          return ESR_Failed;
        return ESR;
      }
    }
    return Scope.destroy() ? ESR_Succeeded : ESR_Failed;
  }

  case Stmt::WhileStmtClass: {
    const WhileStmt *WS = cast<WhileStmt>(S);
    while (true) {
      BlockScopeRAII Scope(Info);
      bool Continue;
      if (!evaluateCond(Info, WS->getConditionVariable(), WS->getCond(),
                        Continue))
        return ESR_Failed;
      if (!Continue)
        break;

      EvalStmtResult ESR = evaluateLoopBody(Result, Info, WS->getBody());
      if (ESR != ESR_Continue) {
        if (ESR != ESR_Failed && !Scope.destroy())
          return ESR_Failed;
        return ESR;
      }
      if (!Scope.destroy())
        return ESR_Failed;
    }
    return ESR_Succeeded;
  }

  case Stmt::DoStmtClass: {
    const DoStmt *DS = cast<DoStmt>(S);
    bool Continue;
    do {
      EvalStmtResult ESR = evaluateLoopBody(Result, Info, DS->getBody(), Case);
      if (ESR != ESR_Continue)
        return ESR;
      Case = nullptr;

      FullExpressionRAII CondScope(Info);
      if (!evaluateAsBooleanCondition(DS->getCond(), Continue, Info) ||
          !CondScope.destroy())
        return ESR_Failed;
    } while (Continue);
    return ESR_Succeeded;
  }

  case Stmt::ForStmtClass: {
    const ForStmt *FS = cast<ForStmt>(S);
    BlockScopeRAII ForScope(Info);
    if (FS->getInit()) {
      EvalStmtResult ESR = evaluateStmt(Result, Info, FS->getInit());
      if (ESR != ESR_Succeeded) {
        if (ESR != ESR_Failed && !ForScope.destroy())
          return ESR_Failed;
        return ESR;
      }
    }
    while (true) {
      BlockScopeRAII IterScope(Info);
      bool Continue = true;
      if (FS->getCond() && !evaluateCond(Info, FS->getConditionVariable(),
                                         FS->getCond(), Continue))
        return ESR_Failed;
      if (!Continue)
        break;

      EvalStmtResult ESR = evaluateLoopBody(Result, Info, FS->getBody());
      if (ESR != ESR_Continue) {
        if (ESR != ESR_Failed && (!IterScope.destroy() || !ForScope.destroy()))
          return ESR_Failed;
        return ESR;
      }

      if (const auto *Inc = FS->getInc()) {
        FullExpressionRAII IncScope(Info);
        if (!evaluateIgnoredValue(Info, Inc) || !IncScope.destroy())
          return ESR_Failed;
      }

      if (!IterScope.destroy())
        return ESR_Failed;
    }
    return ForScope.destroy() ? ESR_Succeeded : ESR_Failed;
  }

  case Stmt::SwitchStmtClass:
    return evaluateSwitch(Result, Info, cast<SwitchStmt>(S));

  case Stmt::ContinueStmtClass:
    return ESR_Continue;

  case Stmt::BreakStmtClass:
    return ESR_Break;

  case Stmt::LabelStmtClass:
    return evaluateStmt(Result, Info, cast<LabelStmt>(S)->getSubStmt(), Case);

  case Stmt::AttributedStmtClass: {
    const auto *AS = cast<AttributedStmt>(S);
    return evaluateStmt(Result, Info, AS->getSubStmt(), Case);
  }

  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
    return evaluateStmt(Result, Info, cast<SwitchCase>(S)->getSubStmt(), Case);
  }
}
} // namespace

namespace {
bool checkConstexprFunction(EvalInfo &Info, SourceLocation CallLoc,
                            const FunctionDecl *Declaration,
                            const FunctionDecl *Definition, const Stmt *Body) {
  // Potential constant expressions can contain calls to declared, but not yet
  // defined, constexpr functions.
  if (Info.checkingPotentialConstantExpression() && !Definition &&
      Declaration->isConstexpr())
    return false;

  // Bail out if the function declaration itself is invalid.  We will
  // have produced a relevant diagnostic while parsing it, so just
  // note the problematic sub-expression.
  if (Declaration->isInvalidDecl()) {
    Info.FFDiag(CallLoc, diag::note_invalid_subexpr_in_const_expr);
    return false;
  }

  if (Definition && Definition->isInvalidDecl()) {
    Info.FFDiag(CallLoc, diag::note_invalid_subexpr_in_const_expr);
    return false;
  }

  // Can we evaluate this function call?
  if (Definition && Body && Definition->isConstexpr())
    return true;

  Info.FFDiag(CallLoc, diag::note_invalid_subexpr_in_const_expr);
  return false;
}
} // namespace

namespace {
bool evaluateCallArg(const ParmVarDecl *PVD, const Expr *Arg, CallRef Call,
                     EvalInfo &Info, bool NonNull = false) {
  LValue LV;
  // Create the parameter slot and register its destruction. For a vararg
  // argument, create a temporary.
  APValue &V = PVD ? Info.CurrentCall->createParam(Call, PVD, LV)
                   : Info.CurrentCall->createTemporary(Arg, Arg->getType(),
                                                       ScopeKind::Call, LV);
  if (!evaluateInPlace(V, Info, LV, Arg))
    return false;

  // Passing a null pointer to an __attribute__((nonnull)) parameter results in
  // undefined behavior, so is non-constant.
  if (NonNull && V.isLValue() && V.isNullPointer()) {
    Info.CCEDiag(Arg, diag::note_non_null_attribute_failed);
    return false;
  }

  return true;
}
} // namespace

namespace {
bool evaluateArgs(llvm::ArrayRef<const Expr *> Args, CallRef Call,
                  EvalInfo &Info, const FunctionDecl *Callee,
                  bool RightToLeft = false) {
  bool Success = true;
  llvm::SmallBitVector ForbiddenNullArgs;
  if (Callee->hasAttr<NonNullAttr>()) {
    ForbiddenNullArgs.resize(Args.size());
    for (const auto *Attr : Callee->specific_attrs<NonNullAttr>()) {
      if (!Attr->args_size()) {
        ForbiddenNullArgs.set();
        break;
      } else
        for (auto Idx : Attr->args()) {
          unsigned ASTIdx = Idx.getASTIndex();
          if (ASTIdx >= Args.size())
            continue;
          ForbiddenNullArgs[ASTIdx] = true;
        }
    }
  }
  for (unsigned I = 0; I < Args.size(); I++) {
    unsigned Idx = RightToLeft ? Args.size() - I - 1 : I;
    const ParmVarDecl *PVD =
        Idx < Callee->getNumParams() ? Callee->getParamDecl(Idx) : nullptr;
    bool NonNull = !ForbiddenNullArgs.empty() && ForbiddenNullArgs[Idx];
    if (!evaluateCallArg(PVD, Args[Idx], Call, Info, NonNull)) {
      // If we're checking for a potential constant expression, evaluate all
      // initializers even if some of them fail.
      if (!Info.noteFailure())
        return false;
      Success = false;
    }
  }
  return Success;
}
} // namespace

namespace {
bool handleFunctionCall(SourceLocation CallLoc, const FunctionDecl *Callee,
                        const Expr *E, llvm::ArrayRef<const Expr *> Args,
                        CallRef Call, const Stmt *Body, EvalInfo &Info,
                        APValue &Result, const LValue *ResultSlot) {
  if (!Info.CheckCallLimit(CallLoc))
    return false;

  CallStackFrame Frame(Info, E->getSourceRange(), Callee, E, Call);

  StmtResult Ret = {Result, ResultSlot};
  EvalStmtResult ESR = evaluateStmt(Ret, Info, Body);
  if (ESR == ESR_Succeeded) {
    if (Callee->getReturnType()->isVoidType())
      return true;
    Info.FFDiag(Callee->getEndLoc(), diag::note_constexpr_no_return);
  }
  return ESR == ESR_Returned;
}
} // namespace

namespace {
bool handleDestructionImpl(EvalInfo &Info, SourceRange CallRange,
                           const LValue &This, APValue &Value, QualType T) {
  // Objects can only be destroyed while they're within their lifetimes.
  if (Value.isAbsent() && !T->isNullPtrType()) {
    APValue Printable;
    This.moveInto(Printable);
    Info.FFDiag(CallRange.getBegin(),
                diag::note_constexpr_destroy_out_of_lifetime)
        << Printable.getAsString(Info.Ctx, T);
    return false;
  }

  // Invent an expression for location purposes.
  OpaqueValueExpr LocE(CallRange.getBegin(), Info.Ctx.IntTy, VK_PRValue);

  // For arrays, destroy elements right-to-left.
  if (const ConstantArrayType *CAT = Info.Ctx.getAsConstantArrayType(T)) {
    uint64_t Size = CAT->getSize().getZExtValue();
    QualType ElemT = CAT->getElementType();

    if (!checkArraySize(Info, CAT, CallRange.getBegin()))
      return false;

    LValue ElemLV = This;
    ElemLV.addArray(Info, &LocE, CAT);
    if (!handleLValueArrayAdjustment(Info, &LocE, ElemLV, ElemT, Size))
      return false;

    // Ensure that we have actual array elements available; the cleanup
    // might mutate the value, so we can't run them on the array filler.
    if (Size && Size > Value.getArrayInitializedElts())
      expandArray(Value, Value.getArraySize() - 1);

    for (; Size != 0; --Size) {
      APValue &Elem = Value.getArrayInitializedElt(Size - 1);
      if (!handleLValueArrayAdjustment(Info, &LocE, ElemLV, ElemT, -1) ||
          !handleDestructionImpl(Info, CallRange, ElemLV, Elem, ElemT))
        return false;
    }

    // End the lifetime of this array now.
    Value = APValue();
    return true;
  }

  if (T.isDestructedType()) {
    Info.FFDiag(CallRange.getBegin(),
                diag::note_constexpr_unsupported_destruction)
        << T;
    return false;
  }

  Value = APValue();
  return true;
}
} // namespace

namespace {
bool handleDestruction(EvalInfo &Info, SourceLocation Loc,
                       APValue::LValueBase LVBase, APValue &Value, QualType T) {
  // If we've had an unmodeled side-effect, we can't rely on mutable state
  // (such as the object we're about to destroy) being correct.
  if (Info.EvalStatus.HasSideEffects)
    return false;

  LValue LV;
  LV.set({LVBase});
  return handleDestructionImpl(Info, Loc, LV, Value, T);
}
} // namespace

namespace {

class BitCastBuffer {
  llvm::SmallVector<std::optional<unsigned char>, 32> Bytes;

  static_assert(std::numeric_limits<unsigned char>::digits >= 8,
                "Need at least 8 bit unsigned char");

  bool TargetIsLittleEndian;

public:
  BitCastBuffer(CharUnits Width, bool TargetIsLittleEndian)
      : Bytes(Width.getQuantity()), TargetIsLittleEndian(TargetIsLittleEndian) {
  }

  [[nodiscard]] bool
  readObject(CharUnits Offset, CharUnits Width,
             llvm::SmallVectorImpl<unsigned char> &Output) const {
    for (CharUnits I = Offset, E = Offset + Width; I != E; ++I) {
      // If a byte of an integer is uninitialized, then the whole integer is
      // uninitialized.
      if (!Bytes[I.getQuantity()])
        return false;
      Output.push_back(*Bytes[I.getQuantity()]);
    }
    if (llvm::sys::IsLittleEndianHost != TargetIsLittleEndian)
      std::reverse(Output.begin(), Output.end());
    return true;
  }

  void writeObject(CharUnits Offset,
                   llvm::SmallVectorImpl<unsigned char> &Input) {
    if (llvm::sys::IsLittleEndianHost != TargetIsLittleEndian)
      std::reverse(Input.begin(), Input.end());

    size_t Index = 0;
    for (unsigned char Byte : Input) {
      assert(!Bytes[Offset.getQuantity() + Index] && "overwriting a byte?");
      Bytes[Offset.getQuantity() + Index] = Byte;
      ++Index;
    }
  }

  size_t size() { return Bytes.size(); }
};

class APValueToBufferConverter {
  EvalInfo &Info;
  BitCastBuffer Buffer;
  const CastExpr *BCE;

  APValueToBufferConverter(EvalInfo &Info, CharUnits ObjectWidth,
                           const CastExpr *BCE)
      : Info(Info),
        Buffer(ObjectWidth, Info.Ctx.getTargetInfo().isLittleEndian()),
        BCE(BCE) {}

  bool visit(const APValue &Val, QualType Ty) {
    return visit(Val, Ty, CharUnits::fromQuantity(0));
  }

  // Write out Val with type Ty into Buffer starting at Offset.
  bool visit(const APValue &Val, QualType Ty, CharUnits Offset) {
    assert((size_t)Offset.getQuantity() <= Buffer.size());

    // As a special case, nullptr_t has an indeterminate value.
    if (Ty->isNullPtrType())
      return true;

    // Dig through Src to find the byte at SrcOffset.
    switch (Val.getKind()) {
    case APValue::Indeterminate:
    case APValue::None:
      return true;

    case APValue::Int:
      return visitInt(Val.getInt(), Ty, Offset);
    case APValue::Float:
      return visitFloat(Val.getFloat(), Ty, Offset);
    case APValue::Array:
      return visitArray(Val, Ty, Offset);
    case APValue::Struct:
      return visitRecord(Val, Ty, Offset);
    case APValue::Vector:
      return visitVector(Val, Ty, Offset);

    case APValue::ComplexInt:
    case APValue::ComplexFloat:
    case APValue::FixedPoint:

    case APValue::Union:
    case APValue::AddrLabelDiff: {
      Info.FFDiag(BCE->getBeginLoc(),
                  diag::note_constexpr_bit_cast_unsupported_type)
          << Ty;
      return false;
    }

    case APValue::LValue:
      llvm_unreachable("LValue subobject in bit_cast?");
    }
    llvm_unreachable("Unhandled APValue::ValueKind");
  }

  bool visitRecord(const APValue &Val, QualType Ty, CharUnits Offset) {
    const RecordDecl *RD = Ty->getAsRecordDecl();
    const StructRecordLayout &Layout = Info.Ctx.getStructRecordLayout(RD);

    unsigned FieldIdx = 0;
    for (FieldDecl *FD : RD->fields()) {
      if (FD->isBitField()) {
        Info.FFDiag(BCE->getBeginLoc(),
                    diag::note_constexpr_bit_cast_unsupported_bitfield);
        return false;
      }

      uint64_t FieldOffsetBits = Layout.getFieldOffset(FieldIdx);

      assert(FieldOffsetBits % Info.Ctx.getCharWidth() == 0 &&
             "only bit-fields can have sub-char alignment");
      CharUnits FieldOffset =
          Info.Ctx.toCharUnitsFromBits(FieldOffsetBits) + Offset;
      QualType FieldTy = FD->getType();
      if (!visit(Val.getStructField(FieldIdx), FieldTy, FieldOffset))
        return false;
      ++FieldIdx;
    }

    return true;
  }

  bool visitArray(const APValue &Val, QualType Ty, CharUnits Offset) {
    const auto *CAT =
        dyn_cast_or_null<ConstantArrayType>(Ty->getAsArrayTypeUnsafe());
    if (!CAT)
      return false;

    CharUnits ElemWidth = Info.Ctx.getTypeSizeInChars(CAT->getElementType());
    unsigned NumInitializedElts = Val.getArrayInitializedElts();
    unsigned ArraySize = Val.getArraySize();
    // First, initialize the initialized elements.
    for (unsigned I = 0; I != NumInitializedElts; ++I) {
      const APValue &SubObj = Val.getArrayInitializedElt(I);
      if (!visit(SubObj, CAT->getElementType(), Offset + I * ElemWidth))
        return false;
    }

    // Next, initialize the rest of the array using the filler.
    if (Val.hasArrayFiller()) {
      const APValue &Filler = Val.getArrayFiller();
      for (unsigned I = NumInitializedElts; I != ArraySize; ++I) {
        if (!visit(Filler, CAT->getElementType(), Offset + I * ElemWidth))
          return false;
      }
    }

    return true;
  }

  bool visitVector(const APValue &Val, QualType Ty, CharUnits Offset) {
    const VectorType *VTy = Ty->castAs<VectorType>();
    QualType EltTy = VTy->getElementType();
    unsigned NElts = VTy->getNumElements();
    unsigned EltSize =
        VTy->isExtVectorBoolType() ? 1 : Info.Ctx.getTypeSize(EltTy);

    if ((NElts * EltSize) % Info.Ctx.getCharWidth() != 0) {
      // The vector's size in bits is not a multiple of the target's byte size,
      // so its layout is unspecified. For now, we'll simply treat these cases
      // as unsupported (e.g. ext vector bool types whose element count isn't
      // a multiple of the byte size).
      Info.FFDiag(BCE->getBeginLoc(),
                  diag::note_constexpr_bit_cast_invalid_vector)
          << Ty.getCanonicalType() << EltSize << NElts
          << Info.Ctx.getCharWidth();
      return false;
    }

    if (EltTy->isRealFloatingType() && &Info.Ctx.getFloatTypeSemantics(EltTy) ==
                                           &APFloat::x87DoubleExtended()) {
      // The layout for x86_fp80 vectors seems to be handled very inconsistently
      // by both NeverC and LLVM, so for now we won't allow bit_casts involving
      // it in a constexpr context.
      Info.FFDiag(BCE->getBeginLoc(),
                  diag::note_constexpr_bit_cast_unsupported_type)
          << EltTy;
      return false;
    }

    if (VTy->isExtVectorBoolType()) {
      // Special handling for packed bool vectors:
      // Since these vectors are stored as packed bits, but we can't write
      // individual bits to the BitCastBuffer, we'll buffer all of the elements
      // together into an appropriately sized APInt and write them all out at
      // once. Because we don't accept vectors where NElts * EltSize isn't a
      // multiple of the char size, there will be no padding space, so we don't
      // have to worry about writing data which should have been left
      // uninitialized.
      llvm::APInt Res = llvm::APInt::getZero(NElts);
      for (unsigned I = 0; I < NElts; ++I) {
        const llvm::APSInt &EltAsInt = Val.getVectorElt(I).getInt();
        assert(EltAsInt.isUnsigned() && EltAsInt.getBitWidth() == 1 &&
               "bool vector element must be 1-bit unsigned integer!");

        Res.insertBits(EltAsInt, I);
      }

      llvm::SmallVector<uint8_t, 8> Bytes(NElts / 8);
      llvm::StoreIntToMemory(Res, &*Bytes.begin(), NElts / 8);
      Buffer.writeObject(Offset, Bytes);
    } else {
      CharUnits EltSizeChars = Info.Ctx.getTypeSizeInChars(EltTy);
      for (unsigned I = 0; I < NElts; ++I) {
        if (!visit(Val.getVectorElt(I), EltTy, Offset + I * EltSizeChars))
          return false;
      }
    }

    return true;
  }

  bool visitInt(const APSInt &Val, QualType Ty, CharUnits Offset) {
    APSInt AdjustedVal = Val;
    unsigned Width = AdjustedVal.getBitWidth();
    if (Ty->isBooleanType()) {
      Width = Info.Ctx.getTypeSize(Ty);
      AdjustedVal = AdjustedVal.extend(Width);
    }

    llvm::SmallVector<uint8_t, 8> Bytes(Width / 8);
    llvm::StoreIntToMemory(AdjustedVal, &*Bytes.begin(), Width / 8);
    Buffer.writeObject(Offset, Bytes);
    return true;
  }

  bool visitFloat(const APFloat &Val, QualType Ty, CharUnits Offset) {
    APSInt AsInt(Val.bitcastToAPInt());
    return visitInt(AsInt, Ty, Offset);
  }

public:
  static std::optional<BitCastBuffer>
  convert(EvalInfo &Info, const APValue &Src, const CastExpr *BCE) {
    CharUnits DstSize = Info.Ctx.getTypeSizeInChars(BCE->getType());
    APValueToBufferConverter Converter(Info, DstSize, BCE);
    if (!Converter.visit(Src, BCE->getSubExpr()->getType()))
      return std::nullopt;
    return Converter.Buffer;
  }
};

class BufferToAPValueConverter {
  EvalInfo &Info;
  const BitCastBuffer &Buffer;
  const CastExpr *BCE;

  BufferToAPValueConverter(EvalInfo &Info, const BitCastBuffer &Buffer,
                           const CastExpr *BCE)
      : Info(Info), Buffer(Buffer), BCE(BCE) {}

  // Emit an unsupported bit_cast type error. Sema refuses to build a bit_cast
  // with an invalid type, so anything left should ideally be unreachable.
  std::nullopt_t unsupportedType(QualType Ty) {
    Info.FFDiag(BCE->getBeginLoc(),
                diag::note_constexpr_bit_cast_unsupported_type)
        << Ty;
    return std::nullopt;
  }

  std::nullopt_t unrepresentableValue(QualType Ty, const APSInt &Val) {
    Info.FFDiag(BCE->getBeginLoc(),
                diag::note_constexpr_bit_cast_unrepresentable_value)
        << Ty << toString(Val, /*Radix=*/10);
    return std::nullopt;
  }

  std::optional<APValue> visit(const BuiltinType *T, CharUnits Offset,
                               const EnumType *EnumSugar = nullptr) {
    if (T->isNullPtrType()) {
      uint64_t NullValue = Info.Ctx.getTargetNullPointerValue(QualType(T, 0));
      return APValue((Expr *)nullptr,
                     /*Offset=*/CharUnits::fromQuantity(NullValue),
                     APValue::NoLValuePath{}, /*IsNullPtr=*/true);
    }

    CharUnits SizeOf = Info.Ctx.getTypeSizeInChars(T);

    // Work around floating point types that contain unused padding bytes. This
    // is really just `long double` on x86, which is the only fundamental type
    // with padding bytes.
    if (T->isRealFloatingType()) {
      const llvm::fltSemantics &Semantics =
          Info.Ctx.getFloatTypeSemantics(QualType(T, 0));
      unsigned NumBits = llvm::APFloatBase::getSizeInBits(Semantics);
      assert(NumBits % 8 == 0);
      CharUnits NumBytes = CharUnits::fromQuantity(NumBits / 8);
      if (NumBytes != SizeOf)
        SizeOf = NumBytes;
    }

    llvm::SmallVector<uint8_t, 8> Bytes;
    if (!Buffer.readObject(Offset, SizeOf, Bytes)) {
      bool IsUChar =
          !EnumSugar && (T->isSpecificBuiltinType(BuiltinType::UChar) ||
                         T->isSpecificBuiltinType(BuiltinType::Char_U));
      if (!IsUChar) {
        QualType DisplayType(EnumSugar ? (const Type *)EnumSugar : T, 0);
        Info.FFDiag(BCE->getExprLoc(), diag::note_constexpr_bit_cast_indet_dest)
            << DisplayType << Info.Ctx.getLangOpts().CharIsSigned;
        return std::nullopt;
      }

      return APValue::IndeterminateValue();
    }

    APSInt Val(SizeOf.getQuantity() * Info.Ctx.getCharWidth(), true);
    llvm::LoadIntFromMemory(Val, &*Bytes.begin(), Bytes.size());

    if (T->isIntegralOrEnumerationType()) {
      Val.setIsSigned(T->isSignedIntegerOrEnumerationType());

      unsigned IntWidth = Info.Ctx.getIntWidth(QualType(T, 0));
      if (IntWidth != Val.getBitWidth()) {
        APSInt Truncated = Val.trunc(IntWidth);
        if (Truncated.extend(Val.getBitWidth()) != Val)
          return unrepresentableValue(QualType(T, 0), Val);
        Val = Truncated;
      }

      return APValue(Val);
    }

    if (T->isRealFloatingType()) {
      const llvm::fltSemantics &Semantics =
          Info.Ctx.getFloatTypeSemantics(QualType(T, 0));
      return APValue(APFloat(Semantics, Val));
    }

    return unsupportedType(QualType(T, 0));
  }

  std::optional<APValue> visit(const RecordType *RTy, CharUnits Offset) {
    const RecordDecl *RD = RTy->getAsRecordDecl();
    const StructRecordLayout &Layout = Info.Ctx.getStructRecordLayout(RD);

    APValue ResultVal(APValue::UninitStruct(),
                      std::distance(RD->field_begin(), RD->field_end()));

    // Visit the fields.
    unsigned FieldIdx = 0;
    for (FieldDecl *FD : RD->fields()) {
      if (FD->isBitField()) {
        Info.FFDiag(BCE->getBeginLoc(),
                    diag::note_constexpr_bit_cast_unsupported_bitfield);
        return std::nullopt;
      }

      uint64_t FieldOffsetBits = Layout.getFieldOffset(FieldIdx);
      assert(FieldOffsetBits % Info.Ctx.getCharWidth() == 0);

      CharUnits FieldOffset =
          CharUnits::fromQuantity(FieldOffsetBits / Info.Ctx.getCharWidth()) +
          Offset;
      QualType FieldTy = FD->getType();
      std::optional<APValue> SubObj = visitType(FieldTy, FieldOffset);
      if (!SubObj)
        return std::nullopt;
      ResultVal.getStructField(FieldIdx) = *SubObj;
      ++FieldIdx;
    }

    return ResultVal;
  }

  std::optional<APValue> visit(const EnumType *Ty, CharUnits Offset) {
    QualType RepresentationType = Ty->getDecl()->getIntegerType();
    assert(!RepresentationType.isNull() &&
           "enum forward decl should be caught by Sema");
    const auto *AsBuiltin =
        RepresentationType.getCanonicalType()->castAs<BuiltinType>();
    // Recurse into the underlying type. Treat std::byte transparently as
    // unsigned char.
    return visit(AsBuiltin, Offset, /*EnumTy=*/Ty);
  }

  std::optional<APValue> visit(const ConstantArrayType *Ty, CharUnits Offset) {
    size_t Size = Ty->getSize().getLimitedValue();
    CharUnits ElementWidth = Info.Ctx.getTypeSizeInChars(Ty->getElementType());

    APValue ArrayValue(APValue::UninitArray(), Size, Size);
    for (size_t I = 0; I != Size; ++I) {
      std::optional<APValue> ElementValue =
          visitType(Ty->getElementType(), Offset + I * ElementWidth);
      if (!ElementValue)
        return std::nullopt;
      ArrayValue.getArrayInitializedElt(I) = std::move(*ElementValue);
    }

    return ArrayValue;
  }

  std::optional<APValue> visit(const VectorType *VTy, CharUnits Offset) {
    QualType EltTy = VTy->getElementType();
    unsigned NElts = VTy->getNumElements();
    unsigned EltSize =
        VTy->isExtVectorBoolType() ? 1 : Info.Ctx.getTypeSize(EltTy);

    if ((NElts * EltSize) % Info.Ctx.getCharWidth() != 0) {
      // The vector's size in bits is not a multiple of the target's byte size,
      // so its layout is unspecified. For now, we'll simply treat these cases
      // as unsupported (e.g. ext vector bool types whose element count isn't
      // a multiple of the byte size).
      Info.FFDiag(BCE->getBeginLoc(),
                  diag::note_constexpr_bit_cast_invalid_vector)
          << QualType(VTy, 0) << EltSize << NElts << Info.Ctx.getCharWidth();
      return std::nullopt;
    }

    if (EltTy->isRealFloatingType() && &Info.Ctx.getFloatTypeSemantics(EltTy) ==
                                           &APFloat::x87DoubleExtended()) {
      // The layout for x86_fp80 vectors seems to be handled very inconsistently
      // by both NeverC and LLVM, so for now we won't allow bit_casts involving
      // it in a constexpr context.
      Info.FFDiag(BCE->getBeginLoc(),
                  diag::note_constexpr_bit_cast_unsupported_type)
          << EltTy;
      return std::nullopt;
    }

    llvm::SmallVector<APValue, 4> Elts;
    Elts.reserve(NElts);
    if (VTy->isExtVectorBoolType()) {
      // Special handling for packed bool vectors:
      // Since these vectors are stored as packed bits, but we can't read
      // individual bits from the BitCastBuffer, we'll buffer all of the
      // elements together into an appropriately sized APInt and write them all
      // out at once. Because we don't accept vectors where NElts * EltSize
      // isn't a multiple of the char size, there will be no padding space, so
      // we don't have to worry about reading any padding data which didn't
      // actually need to be accessed.
      llvm::SmallVector<uint8_t, 8> Bytes;
      Bytes.reserve(NElts / 8);
      if (!Buffer.readObject(Offset, CharUnits::fromQuantity(NElts / 8), Bytes))
        return std::nullopt;

      APSInt SValInt(NElts, true);
      llvm::LoadIntFromMemory(SValInt, &*Bytes.begin(), Bytes.size());

      for (unsigned I = 0; I < NElts; ++I) {
        llvm::APInt Elt = SValInt.extractBits(1, I * EltSize);
        Elts.emplace_back(
            APSInt(std::move(Elt), !EltTy->isSignedIntegerType()));
      }
    } else {
      CharUnits EltSizeChars = Info.Ctx.getTypeSizeInChars(EltTy);
      for (unsigned I = 0; I < NElts; ++I) {
        std::optional<APValue> EltValue =
            visitType(EltTy, Offset + I * EltSizeChars);
        if (!EltValue)
          return std::nullopt;
        Elts.push_back(std::move(*EltValue));
      }
    }

    return APValue(Elts.data(), Elts.size());
  }

  std::optional<APValue> visit(const Type *Ty, CharUnits Offset) {
    return unsupportedType(QualType(Ty, 0));
  }

  std::optional<APValue> visitType(QualType Ty, CharUnits Offset) {
    QualType Can = Ty.getCanonicalType();

    switch (Can->getTypeClass()) {
#define TYPE(Class, Base)                                                      \
  case Type::Class:                                                            \
    return visit(cast<Class##Type>(Can.getTypePtr()), Offset);
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base)                                        \
  case Type::Class:                                                            \
    llvm_unreachable("non-canonical type should be impossible!");
#define DEPENDENT_TYPE(Class, Base)                                            \
  case Type::Class:                                                            \
    llvm_unreachable(                                                          \
        "dependent types aren't supported in the constant evaluator!");
#define NON_CANONICAL_UNLESS_DEPENDENT(Class, Base)                            \
  case Type::Class:                                                            \
    llvm_unreachable("either dependent or not canonical!");
#include "neverc/Tree/TypeNodes.td.h"
    }
    llvm_unreachable("Unhandled Type::TypeClass");
  }

public:
  // Pull out a full value of type DstType.
  static std::optional<APValue> convert(EvalInfo &Info, BitCastBuffer &Buffer,
                                        const CastExpr *BCE) {
    BufferToAPValueConverter Converter(Info, Buffer, BCE);
    return Converter.visitType(BCE->getType(), CharUnits::fromQuantity(0));
  }
};

bool checkBitCastConstexprEligibilityType(SourceLocation Loc, QualType Ty,
                                          EvalInfo *Info,
                                          const TreeContext &Ctx,
                                          bool CheckingDest) {
  Ty = Ty.getCanonicalType();

  auto diag = [&](int Reason) {
    if (Info)
      Info->FFDiag(Loc, diag::note_constexpr_bit_cast_invalid_type)
          << CheckingDest << false << Reason;
    return false;
  };
  auto note = [&](int Construct, QualType NoteTy, SourceLocation NoteLoc) {
    if (Info)
      Info->Note(NoteLoc, diag::note_constexpr_bit_cast_invalid_subtype)
          << NoteTy << Ty;
    return false;
  };

  if (Ty->isUnionType())
    return diag(0);
  if (Ty->isPointerType())
    return diag(1);
  if (Ty.isVolatileQualified())
    return diag(2);

  if (RecordDecl *Record = Ty->getAsRecordDecl()) {
    for (FieldDecl *FD : Record->fields()) {
      if (!checkBitCastConstexprEligibilityType(Loc, FD->getType(), Info, Ctx,
                                                CheckingDest))
        return note(0, FD->getType(), FD->getBeginLoc());
    }
  }

  if (Ty->isArrayType() &&
      !checkBitCastConstexprEligibilityType(Loc, Ctx.getBaseElementType(Ty),
                                            Info, Ctx, CheckingDest))
    return false;

  return true;
}

bool checkBitCastConstexprEligibility(EvalInfo *Info, const TreeContext &Ctx,
                                      const CastExpr *BCE) {
  bool DestOK = checkBitCastConstexprEligibilityType(
      BCE->getBeginLoc(), BCE->getType(), Info, Ctx, true);
  bool SourceOK = DestOK && checkBitCastConstexprEligibilityType(
                                BCE->getBeginLoc(),
                                BCE->getSubExpr()->getType(), Info, Ctx, false);
  return SourceOK;
}

bool handleRValueToRValueBitCast(EvalInfo &Info, APValue &DestValue,
                                 const APValue &SourceRValue,
                                 const CastExpr *BCE) {
  assert(CHAR_BIT == 8 && Info.Ctx.getTargetInfo().getCharWidth() == 8 &&
         "no host or target supports non 8-bit chars");

  if (!checkBitCastConstexprEligibility(&Info, Info.Ctx, BCE))
    return false;

  // Read out SourceValue into a char buffer.
  std::optional<BitCastBuffer> Buffer =
      APValueToBufferConverter::convert(Info, SourceRValue, BCE);
  if (!Buffer)
    return false;

  // Write out the buffer into a new APValue.
  std::optional<APValue> MaybeDestValue =
      BufferToAPValueConverter::convert(Info, *Buffer, BCE);
  if (!MaybeDestValue)
    return false;

  DestValue = std::move(*MaybeDestValue);
  return true;
}

bool handleLValueToRValueBitCast(EvalInfo &Info, APValue &DestValue,
                                 APValue &SourceValue, const CastExpr *BCE) {
  assert(CHAR_BIT == 8 && Info.Ctx.getTargetInfo().getCharWidth() == 8 &&
         "no host or target supports non 8-bit chars");
  assert(SourceValue.isLValue() &&
         "LValueToRValueBitcast requires an lvalue operand!");

  LValue SourceLValue;
  APValue SourceRValue;
  SourceLValue.setFrom(Info.Ctx, SourceValue);
  if (!handleLValueToRValueConversion(
          Info, BCE, BCE->getSubExpr()->getType().withConst(), SourceLValue,
          SourceRValue, /*WantObjectRepresentation=*/true))
    return false;

  return handleRValueToRValueBitCast(Info, DestValue, SourceRValue, BCE);
}

// ===----------------------------------------------------------------------===
// Expression evaluators
// ===----------------------------------------------------------------------===

template <class Derived>
class ExprEvaluatorBase : public ConstStmtVisitor<Derived, bool> {
private:
  Derived &getDerived() { return static_cast<Derived &>(*this); }
  bool DerivedSuccess(const APValue &V, const Expr *E) {
    return getDerived().Success(V, E);
  }
  bool DerivedZeroInitialization(const Expr *E) {
    return getDerived().ZeroInitialization(E);
  }

  // Check whether a conditional operator with a non-constant condition is a
  // potential constant expression. If neither arm is a potential constant
  // expression, then the conditional operator is not either.
  template <typename ConditionalOperator>
  void CheckPotentialConstantConditional(const ConditionalOperator *E) {
    assert(Info.checkingPotentialConstantExpression());

    // Speculatively evaluate both arms.
    llvm::SmallVector<PartialDiagnosticAt, 8> Diag;
    {
      SpeculativeEvaluationRAII Speculate(Info, &Diag);
      StmtVisitorTy::Visit(E->getFalseExpr());
      if (Diag.empty())
        return;
    }

    {
      SpeculativeEvaluationRAII Speculate(Info, &Diag);
      Diag.clear();
      StmtVisitorTy::Visit(E->getTrueExpr());
      if (Diag.empty())
        return;
    }

    Error(E, diag::note_constexpr_conditional_never_const);
  }

  template <typename ConditionalOperator>
  bool HandleConditionalOperator(const ConditionalOperator *E) {
    bool BoolResult;
    if (!evaluateAsBooleanCondition(E->getCond(), BoolResult, Info)) {
      if (Info.checkingPotentialConstantExpression() && Info.noteFailure()) {
        CheckPotentialConstantConditional(E);
        return false;
      }
      if (Info.noteFailure()) {
        StmtVisitorTy::Visit(E->getTrueExpr());
        StmtVisitorTy::Visit(E->getFalseExpr());
      }
      return false;
    }

    Expr *EvalExpr = BoolResult ? E->getTrueExpr() : E->getFalseExpr();
    return StmtVisitorTy::Visit(EvalExpr);
  }

protected:
  EvalInfo &Info;
  typedef ConstStmtVisitor<Derived, bool> StmtVisitorTy;
  typedef ExprEvaluatorBase ExprEvaluatorBaseTy;

  OptionalDiagnostic CCEDiag(const Expr *E, diag::kind D) {
    return Info.CCEDiag(E, D);
  }

  bool ZeroInitialization(const Expr *E) { return Error(E); }

  bool IsConstantevaluatedBuiltinCall(const CallExpr *E) {
    unsigned BuiltinOp = E->getBuiltinCallee();
    return BuiltinOp != 0 &&
           Info.Ctx.BuiltinInfo.isConstantEvaluated(BuiltinOp);
  }

public:
  ExprEvaluatorBase(EvalInfo &Info) : Info(Info) {}

  EvalInfo &getEvalInfo() { return Info; }

  bool Error(const Expr *E, diag::kind D) {
    Info.FFDiag(E, D) << E->getSourceRange();
    return false;
  }
  bool Error(const Expr *E) {
    return Error(E, diag::note_invalid_subexpr_in_const_expr);
  }

  bool VisitStmt(const Stmt *) {
    llvm_unreachable("Expression evaluator should not be called on stmts");
  }
  bool VisitExpr(const Expr *E) { return Error(E); }

  bool VisitPredefinedExpr(const PredefinedExpr *E) {
    return StmtVisitorTy::Visit(E->getFunctionName());
  }
  bool VisitConstantExpr(const ConstantExpr *E) {
    if (E->hasAPValueResult())
      return DerivedSuccess(E->getAPValueResult(), E);

    return StmtVisitorTy::Visit(E->getSubExpr());
  }

  bool VisitParenExpr(const ParenExpr *E) {
    return StmtVisitorTy::Visit(E->getSubExpr());
  }
  bool VisitUnaryExtension(const UnaryOperator *E) {
    return StmtVisitorTy::Visit(E->getSubExpr());
  }
  bool VisitUnaryPlus(const UnaryOperator *E) {
    return StmtVisitorTy::Visit(E->getSubExpr());
  }
  bool VisitChooseExpr(const ChooseExpr *E) {
    return StmtVisitorTy::Visit(E->getChosenSubExpr());
  }
  bool VisitGenericSelectionExpr(const GenericSelectionExpr *E) {
    return StmtVisitorTy::Visit(E->getResultExpr());
  }
  bool VisitExprWithCleanups(const ExprWithCleanups *E) {
    FullExpressionRAII Scope(Info);
    return StmtVisitorTy::Visit(E->getSubExpr()) && Scope.destroy();
  }

  bool VisitBinaryOperator(const BinaryOperator *E) {
    switch (E->getOpcode()) {
    default:
      return Error(E);

    case BO_Comma:
      VisitIgnoredValue(E->getLHS());
      return StmtVisitorTy::Visit(E->getRHS());
    }
  }

  bool VisitBinaryConditionalOperator(const BinaryConditionalOperator *E) {
    // evaluate and cache the common expression. We treat it as a temporary,
    // even though it's not quite the same thing.
    LValue CommonLV;
    if (!evaluate(Info.CurrentCall->createTemporary(
                      E->getOpaqueValue(),
                      getStorageType(Info.Ctx, E->getOpaqueValue()),
                      ScopeKind::FullExpression, CommonLV),
                  Info, E->getCommon()))
      return false;

    return HandleConditionalOperator(E);
  }

  bool VisitConditionalOperator(const ConditionalOperator *E) {
    bool IsBcpCall = false;
    // If the condition (ignoring parens) is a __builtin_constant_p call,
    // the result is a constant expression if it can be folded without
    // side-effects. This is an important GNU extension. See GCC PR38377
    // for discussion.
    if (const CallExpr *CallCE =
            dyn_cast<CallExpr>(E->getCond()->IgnoreParenCasts()))
      if (CallCE->getBuiltinCallee() == Builtin::BI__builtin_constant_p)
        IsBcpCall = true;

    // Always assume __builtin_constant_p(...) ? ... : ... is a potential
    // constant expression; we can't check whether it's potentially foldable.
    if (Info.checkingPotentialConstantExpression() && IsBcpCall)
      return false;

    FoldConstant Fold(Info, IsBcpCall);
    if (!HandleConditionalOperator(E)) {
      Fold.keepDiagnostics();
      return false;
    }

    return true;
  }

  bool VisitOpaqueValueExpr(const OpaqueValueExpr *E) {
    if (APValue *Value = Info.CurrentCall->getCurrentTemporary(E);
        Value && !Value->isAbsent())
      return DerivedSuccess(*Value, E);

    const Expr *Source = E->getSourceExpr();
    if (!Source)
      return Error(E);
    if (Source == E) {
      assert(0 && "OpaqueValueExpr recursively refers to itself");
      return Error(E);
    }
    return StmtVisitorTy::Visit(Source);
  }

  bool VisitPseudoObjectExpr(const PseudoObjectExpr *E) {
    for (const Expr *SemE : E->semantics()) {
      if (auto *OVE = dyn_cast<OpaqueValueExpr>(SemE)) {
        if (SemE == E->getResultExpr())
          return Error(E);

        // Unique OVEs get evaluated if and when we encounter them when
        // emitting the rest of the semantic form, rather than eagerly.
        if (OVE->isUnique())
          continue;

        LValue LV;
        if (!evaluate(Info.CurrentCall->createTemporary(
                          OVE, getStorageType(Info.Ctx, OVE),
                          ScopeKind::FullExpression, LV),
                      Info, OVE->getSourceExpr()))
          return false;
      } else if (SemE == E->getResultExpr()) {
        if (!StmtVisitorTy::Visit(SemE))
          return false;
      } else {
        if (!evaluateIgnoredValue(Info, SemE))
          return false;
      }
    }
    return true;
  }

  bool VisitCallExpr(const CallExpr *E) {
    APValue Result;
    if (!handleCallExpr(E, Result, nullptr))
      return false;
    return DerivedSuccess(Result, E);
  }

  bool handleCallExpr(const CallExpr *E, APValue &Result,
                      const LValue *ResultSlot) {
    CallScopeRAII CallScope(Info);

    const Expr *Callee = E->getCallee()->IgnoreParens();
    QualType CalleeType = Callee->getType();

    const FunctionDecl *FD = nullptr;
    auto Args = llvm::ArrayRef(E->getArgs(), E->getNumArgs());

    CallRef Call;

    if (CalleeType->isFunctionPointerType()) {
      LValue CalleeLV;
      if (!evaluatePointer(Callee, CalleeLV, Info))
        return false;

      if (!CalleeLV.getLValueOffset().isZero())
        return Error(Callee);
      if (CalleeLV.isNullPointer()) {
        Info.FFDiag(Callee, diag::note_constexpr_null_callee)
            << Callee->getType();
        return false;
      }
      FD = dyn_cast_or_null<FunctionDecl>(
          CalleeLV.getLValueBase().dyn_cast<const ValueDecl *>());
      if (!FD)
        return Error(Callee);
      // Don't call function pointers which have been cast to some other type.
      if (!Info.Ctx.hasSameType(CalleeType->getPointeeType(), FD->getType())) {
        return Error(E);
      }

    } else
      return Error(E);

    // evaluate the arguments now if we've not already done so.
    if (!Call) {
      Call = Info.CurrentCall->createCall(FD);
      if (!evaluateArgs(Args, Call, Info, FD))
        return false;
    }

    const FunctionDecl *Definition = nullptr;
    Stmt *Body = FD->getBody(Definition);

    if (!checkConstexprFunction(Info, E->getExprLoc(), FD, Definition, Body) ||
        !handleFunctionCall(E->getExprLoc(), Definition, E, Args, Call, Body,
                            Info, Result, ResultSlot))
      return false;

    return CallScope.destroy();
  }

  bool VisitCompoundLiteralExpr(const CompoundLiteralExpr *E) {
    return StmtVisitorTy::Visit(E->getInitializer());
  }
  bool VisitInitListExpr(const InitListExpr *E) {
    if (E->getNumInits() == 0)
      return DerivedZeroInitialization(E);
    if (E->getNumInits() == 1)
      return StmtVisitorTy::Visit(E->getInit(0));
    return Error(E);
  }
  bool VisitImplicitValueInitExpr(const ImplicitValueInitExpr *E) {
    return DerivedZeroInitialization(E);
  }
  bool VisitNullPtrLiteralExpr(const NullPtrLiteralExpr *E) {
    return DerivedZeroInitialization(E);
  }

  bool VisitMemberExpr(const MemberExpr *E) {
    assert(!E->isArrow() && "arrow on prvalue?");

    APValue Val;
    if (!evaluate(Val, Info, E->getBase()))
      return false;

    QualType BaseTy = E->getBase()->getType();

    const FieldDecl *FD = dyn_cast<FieldDecl>(E->getMemberDecl());
    if (!FD)
      return Error(E);
    assert(BaseTy->castAs<RecordType>()->getDecl()->getCanonicalDecl() ==
               FD->getParent()->getCanonicalDecl() &&
           "record / field mismatch");

    // No lvalue base: only arises where constexpr construction is impossible,
    // so the missing base does not matter.
    CompleteObject Obj(APValue::LValueBase(), &Val, BaseTy);
    SubobjectDesignator Designator(BaseTy);
    Designator.addDeclUnchecked(FD);

    APValue Result;
    return extractSubobject(Info, E, Obj, Designator, Result) &&
           DerivedSuccess(Result, E);
  }

  bool VisitExtVectorElementExpr(const ExtVectorElementExpr *E) {
    APValue Val;
    if (!evaluate(Val, Info, E->getBase()))
      return false;

    if (Val.isVector()) {
      llvm::SmallVector<uint32_t, 4> Indices;
      E->getEncodedElementAccess(Indices);
      if (Indices.size() == 1) {
        // Return scalar.
        return DerivedSuccess(Val.getVectorElt(Indices[0]), E);
      } else {
        // Construct new APValue vector.
        llvm::SmallVector<APValue, 4> Elts;
        for (unsigned I = 0; I < Indices.size(); ++I) {
          Elts.push_back(Val.getVectorElt(Indices[I]));
        }
        APValue VecResult(Elts.data(), Indices.size());
        return DerivedSuccess(VecResult, E);
      }
    }

    return false;
  }

  bool VisitCastExpr(const CastExpr *E) {
    switch (E->getCastKind()) {
    default:
      break;

    case CK_AtomicToNonAtomic: {
      APValue AtomicVal;
      // This does not need to be done in place even for class/array types:
      // atomic-to-non-atomic conversion implies copying the object
      // representation.
      if (!evaluate(AtomicVal, Info, E->getSubExpr()))
        return false;
      return DerivedSuccess(AtomicVal, E);
    }

    case CK_NoOp:
      return StmtVisitorTy::Visit(E->getSubExpr());

    case CK_LValueToRValue: {
      LValue LVal;
      if (!evaluateLValue(E->getSubExpr(), LVal, Info))
        return false;
      APValue RVal;
      // Note, we use the subexpression's type in order to retain cv-qualifiers.
      if (!handleLValueToRValueConversion(Info, E, E->getSubExpr()->getType(),
                                          LVal, RVal))
        return false;
      return DerivedSuccess(RVal, E);
    }
    case CK_LValueToRValueBitCast: {
      APValue DestValue, SourceValue;
      if (!evaluate(SourceValue, Info, E->getSubExpr()))
        return false;
      if (!handleLValueToRValueBitCast(Info, DestValue, SourceValue, E))
        return false;
      return DerivedSuccess(DestValue, E);
    }

    case CK_AddressSpaceConversion: {
      APValue Value;
      if (!evaluate(Value, Info, E->getSubExpr()))
        return false;
      return DerivedSuccess(Value, E);
    }
    }

    return Error(E);
  }

  bool VisitUnaryPostInc(const UnaryOperator *UO) {
    return VisitUnaryPostIncDec(UO);
  }
  bool VisitUnaryPostDec(const UnaryOperator *UO) {
    return VisitUnaryPostIncDec(UO);
  }
  bool VisitUnaryPostIncDec(const UnaryOperator *UO) {
    if (!Info.keepEvaluatingAfterFailure())
      return Error(UO);

    LValue LVal;
    if (!evaluateLValue(UO->getSubExpr(), LVal, Info))
      return false;
    handleIncDec(this->Info, UO, LVal, UO->getSubExpr()->getType());
    return false;
  }

  bool VisitStmtExpr(const StmtExpr *E) {
    // We will have checked the full-expressions inside the statement expression
    // when they were completed, and don't need to check them again now.
    llvm::SaveAndRestore NotCheckingForUB(Info.CheckingForUndefinedBehavior,
                                          false);

    const CompoundStmt *CS = E->getSubStmt();
    if (CS->body_empty())
      return true;

    BlockScopeRAII Scope(Info);
    for (CompoundStmt::const_body_iterator BI = CS->body_begin(),
                                           BE = CS->body_end();
         /**/; ++BI) {
      if (BI + 1 == BE) {
        const Expr *FinalExpr = dyn_cast<Expr>(*BI);
        if (!FinalExpr) {
          Info.FFDiag((*BI)->getBeginLoc(),
                      diag::note_constexpr_stmt_expr_unsupported);
          return false;
        }
        return this->Visit(FinalExpr) && Scope.destroy();
      }

      APValue ReturnValue;
      StmtResult Result = {ReturnValue, nullptr};
      EvalStmtResult ESR = evaluateStmt(Result, Info, *BI);
      if (ESR != ESR_Succeeded) {
        if (ESR != ESR_Failed)
          Info.FFDiag((*BI)->getBeginLoc(),
                      diag::note_constexpr_stmt_expr_unsupported);
        return false;
      }
    }

    llvm_unreachable("Return from function from the loop above.");
  }

  void VisitIgnoredValue(const Expr *E) { evaluateIgnoredValue(Info, E); }

  void VisitIgnoredBaseExpression(const Expr *E) {
    // While MSVC doesn't evaluate the base expression, it does diagnose the
    // presence of side-effecting behavior.
    if (Info.getLangOpts().MSVCCompat && !E->HasSideEffects(Info.Ctx))
      return;
    VisitIgnoredValue(E);
  }
};

} // namespace

namespace {
template <class Derived>
class LValueExprEvaluatorBase : public ExprEvaluatorBase<Derived> {
protected:
  LValue &Result;
  bool InvalidBaseOK;
  typedef LValueExprEvaluatorBase LValueExprEvaluatorBaseTy;
  typedef ExprEvaluatorBase<Derived> ExprEvaluatorBaseTy;

  bool Success(APValue::LValueBase B) {
    Result.set(B);
    return true;
  }

  bool evaluatePointer(const Expr *E, LValue &Result) {
    return ::evaluatePointer(E, Result, this->Info, InvalidBaseOK);
  }

public:
  LValueExprEvaluatorBase(EvalInfo &Info, LValue &Result, bool InvalidBaseOK)
      : ExprEvaluatorBaseTy(Info), Result(Result),
        InvalidBaseOK(InvalidBaseOK) {}

  bool Success(const APValue &V, const Expr *E) {
    Result.setFrom(this->Info.Ctx, V);
    return true;
  }

  bool VisitMemberExpr(const MemberExpr *E) {
    // Handle fields.
    QualType BaseTy;
    bool EvalOK;
    if (E->isArrow()) {
      EvalOK = evaluatePointer(E->getBase(), Result);
      BaseTy = E->getBase()->getType()->castAs<PointerType>()->getPointeeType();
    } else if (E->getBase()->isPRValue()) {
      assert(E->getBase()->getType()->isRecordType());
      EvalOK = evaluateTemporary(E->getBase(), Result, this->Info);
      BaseTy = E->getBase()->getType();
    } else {
      EvalOK = this->Visit(E->getBase());
      BaseTy = E->getBase()->getType();
    }
    if (!EvalOK) {
      if (!InvalidBaseOK)
        return false;
      Result.setInvalid(E);
      return true;
    }

    const ValueDecl *MD = E->getMemberDecl();
    if (const FieldDecl *FD = dyn_cast<FieldDecl>(E->getMemberDecl())) {
      assert(BaseTy->castAs<RecordType>()->getDecl()->getCanonicalDecl() ==
                 FD->getParent()->getCanonicalDecl() &&
             "record / field mismatch");
      (void)BaseTy;
      if (!handleLValueMember(this->Info, E, Result, FD))
        return false;
    } else if (const IndirectFieldDecl *IFD = dyn_cast<IndirectFieldDecl>(MD)) {
      if (!handleLValueIndirectMember(this->Info, E, Result, IFD))
        return false;
    } else
      return this->Error(E);

    return true;
  }

  bool VisitCastExpr(const CastExpr *E) {
    return ExprEvaluatorBaseTy::VisitCastExpr(E);
  }
};
} // namespace

// LValue Evaluation
//
// This is used for evaluating lvalues, function designators,
// decl references to void objects, and
// temporaries (if building with -Wno-address-of-temporary).
//
// LValue evaluation produces values comprising a base expression of one of the
// following types:
// - Declarations
//  * VarDecl
//  * FunctionDecl
// - Literals
//  * CompoundLiteralExpr in C (and in file scope in languages that allow it)
//  * StringLiteral
//  * PredefinedExpr
//  * AddrLabelExpr
//  * CallExpr for a MakeStringConstant builtin
// - Locals and temporaries
//  * Any Expr, with a CallIndex indicating the function in which the temporary
//    was evaluated.
//  * The ConstantExpr that is currently being evaluated during evaluation of an
//    immediate invocation.
// plus an offset in bytes.
namespace {
class LValueExprEvaluator
    : public LValueExprEvaluatorBase<LValueExprEvaluator> {
public:
  LValueExprEvaluator(EvalInfo &Info, LValue &Result, bool InvalidBaseOK)
      : LValueExprEvaluatorBaseTy(Info, Result, InvalidBaseOK) {}

  bool VisitVarDecl(const Expr *E, const VarDecl *VD);
  bool VisitUnaryPreIncDec(const UnaryOperator *UO);

  bool VisitCallExpr(const CallExpr *E);
  bool VisitDeclRefExpr(const DeclRefExpr *E);
  bool VisitPredefinedExpr(const PredefinedExpr *E) { return Success(E); }
  bool VisitCompoundLiteralExpr(const CompoundLiteralExpr *E);
  bool VisitMemberExpr(const MemberExpr *E);
  bool VisitStringLiteral(const StringLiteral *E) { return Success(E); }
  bool VisitArraySubscriptExpr(const ArraySubscriptExpr *E);
  bool VisitUnaryDeref(const UnaryOperator *E);
  bool VisitUnaryReal(const UnaryOperator *E);
  bool VisitUnaryImag(const UnaryOperator *E);
  bool VisitUnaryPreInc(const UnaryOperator *UO) {
    return VisitUnaryPreIncDec(UO);
  }
  bool VisitUnaryPreDec(const UnaryOperator *UO) {
    return VisitUnaryPreIncDec(UO);
  }
  bool VisitBinAssign(const BinaryOperator *BO);
  bool VisitCompoundAssignOperator(const CompoundAssignOperator *CAO);

  bool VisitCastExpr(const CastExpr *E) {
    switch (E->getCastKind()) {
    default:
      return LValueExprEvaluatorBaseTy::VisitCastExpr(E);

    case CK_LValueBitCast:
      this->CCEDiag(E, diag::note_constexpr_invalid_cast) << 0;
      if (!Visit(E->getSubExpr()))
        return false;
      Result.Designator.setInvalid();
      return true;
    }
  }
};
} // end anonymous namespace

namespace {
bool evaluateLValue(const Expr *E, LValue &Result, EvalInfo &Info,
                    bool InvalidBaseOK) {

  assert(E->isLValue() || E->getType()->isFunctionType() ||
         E->getType()->isVoidType());
  return LValueExprEvaluator(Info, Result, InvalidBaseOK).Visit(E);
}
} // namespace

bool LValueExprEvaluator::VisitDeclRefExpr(const DeclRefExpr *E) {
  const NamedDecl *D = E->getDecl();
  if (isa<FunctionDecl>(D))
    return Success(cast<ValueDecl>(D));
  if (const VarDecl *VD = dyn_cast<VarDecl>(D))
    return VisitVarDecl(E, VD);
  return Error(E);
}

bool LValueExprEvaluator::VisitVarDecl(const Expr *E, const VarDecl *VD) {

  CallStackFrame *Frame = nullptr;
  unsigned Version = 0;
  if (VD->hasLocalStorage()) {
    // Only if a local variable was declared in the function currently being
    // evaluated, do we expect to be able to find its value in the current
    // frame. (Otherwise it was likely declared in an enclosing context and
    // could either have a valid evaluatable value (for e.g. a constexpr
    // variable) or be ill-formed (and trigger an appropriate evaluation
    // diagnostic)).
    CallStackFrame *CurrFrame = Info.CurrentCall;
    if (CurrFrame->Callee && CurrFrame->Callee->Equals(VD->getDeclContext())) {
      // Function parameters are stored in some caller's frame.
      if (auto *PVD = dyn_cast<ParmVarDecl>(VD)) {
        if (CurrFrame->Arguments) {
          VD = CurrFrame->Arguments.getOrigParam(PVD);
          Frame =
              Info.getCallFrameAndDepth(CurrFrame->Arguments.CallIndex).first;
          Version = CurrFrame->Arguments.Version;
        }
      } else {
        Frame = CurrFrame;
        Version = CurrFrame->getCurrentTemporaryVersion(VD);
      }
    }
  }

  if (Frame) {
    Result.set({VD, Frame->Index, Version});
    return true;
  }
  return Success(VD);
}

bool LValueExprEvaluator::VisitCallExpr(const CallExpr *E) {
  if (!IsConstantevaluatedBuiltinCall(E))
    return ExprEvaluatorBaseTy::VisitCallExpr(E);
  return false;
}

bool LValueExprEvaluator::VisitCompoundLiteralExpr(
    const CompoundLiteralExpr *E) {
  return Success(E);
}

bool LValueExprEvaluator::VisitMemberExpr(const MemberExpr *E) {
  if (const VarDecl *VD = dyn_cast<VarDecl>(E->getMemberDecl())) {
    VisitIgnoredBaseExpression(E->getBase());
    return VisitVarDecl(E, VD);
  }

  // Handle fields.
  return LValueExprEvaluatorBaseTy::VisitMemberExpr(E);
}

bool LValueExprEvaluator::VisitArraySubscriptExpr(const ArraySubscriptExpr *E) {
  if (E->getBase()->getType()->isVectorType() ||
      E->getBase()->getType()->isSveVLSBuiltinType())
    return Error(E);

  APSInt Index;
  bool Success = true;

  // evaluate base and index in source order.
  for (const Expr *SubExpr : {E->getLHS(), E->getRHS()}) {
    if (SubExpr == E->getBase() ? !evaluatePointer(SubExpr, Result)
                                : !evaluateInteger(SubExpr, Index, Info)) {
      if (!Info.noteFailure())
        return false;
      Success = false;
    }
  }

  return Success &&
         handleLValueArrayAdjustment(Info, E, Result, E->getType(), Index);
}

bool LValueExprEvaluator::VisitUnaryDeref(const UnaryOperator *E) {
  return evaluatePointer(E->getSubExpr(), Result);
}

bool LValueExprEvaluator::VisitUnaryReal(const UnaryOperator *E) {
  if (!Visit(E->getSubExpr()))
    return false;
  // __real is a no-op on scalar lvalues.
  if (E->getSubExpr()->getType()->isAnyComplexType())
    handleLValueComplexElement(Info, E, Result, E->getType(), false);
  return true;
}

bool LValueExprEvaluator::VisitUnaryImag(const UnaryOperator *E) {
  assert(E->getSubExpr()->getType()->isAnyComplexType() &&
         "lvalue __imag__ on scalar?");
  if (!Visit(E->getSubExpr()))
    return false;
  handleLValueComplexElement(Info, E, Result, E->getType(), true);
  return true;
}

bool LValueExprEvaluator::VisitUnaryPreIncDec(const UnaryOperator *UO) {
  if (!Info.keepEvaluatingAfterFailure())
    return Error(UO);

  if (!this->Visit(UO->getSubExpr()))
    return false;

  return handleIncDec(this->Info, UO, Result, UO->getSubExpr()->getType());
}

bool LValueExprEvaluator::VisitCompoundAssignOperator(
    const CompoundAssignOperator *CAO) {
  if (!Info.keepEvaluatingAfterFailure())
    return Error(CAO);

  bool Success = true;

  // evaluate the RHS value before the LHS lvalue.
  APValue RHS;
  if (!evaluate(RHS, this->Info, CAO->getRHS())) {
    if (!Info.noteFailure())
      return false;
    Success = false;
  }

  // The overall lvalue result is the result of evaluating the LHS.
  if (!this->Visit(CAO->getLHS()) || !Success)
    return false;

  return handleCompoundAssignment(
      this->Info, CAO, Result, CAO->getLHS()->getType(),
      CAO->getComputationLHSType(),
      CAO->getOpForCompoundAssignment(CAO->getOpcode()), RHS);
}

bool LValueExprEvaluator::VisitBinAssign(const BinaryOperator *E) {
  if (!Info.keepEvaluatingAfterFailure())
    return Error(E);

  bool Success = true;

  APValue NewVal;
  if (!evaluate(NewVal, this->Info, E->getRHS())) {
    if (!Info.noteFailure())
      return false;
    Success = false;
  }

  if (!this->Visit(E->getLHS()) || !Success)
    return false;

  return handleAssignment(this->Info, E, Result, E->getLHS()->getType(),
                          NewVal);
}

namespace {
bool getBytesReturnedByAllocSizeCall(const TreeContext &Ctx,
                                     const CallExpr *Call,
                                     llvm::APInt &Result) {
  const AllocSizeAttr *AllocSize = getAllocSizeAttr(Call);

  assert(AllocSize && AllocSize->getElemSizeParam().isValid());
  unsigned SizeArgNo = AllocSize->getElemSizeParam().getASTIndex();
  unsigned BitsInSizeT = Ctx.getTypeSize(Ctx.getSizeType());
  if (Call->getNumArgs() <= SizeArgNo)
    return false;

  auto evaluateAsSizeT = [&](const Expr *E, APSInt &Into) {
    Expr::EvalResult ExprResult;
    if (!E->EvaluateAsInt(ExprResult, Ctx, Expr::SE_AllowSideEffects))
      return false;
    Into = ExprResult.Val.getInt();
    if (Into.isNegative() || !Into.isIntN(BitsInSizeT))
      return false;
    Into = Into.zext(BitsInSizeT);
    return true;
  };

  APSInt SizeOfElem;
  if (!evaluateAsSizeT(Call->getArg(SizeArgNo), SizeOfElem))
    return false;

  if (!AllocSize->getNumElemsParam().isValid()) {
    Result = std::move(SizeOfElem);
    return true;
  }

  APSInt NumberOfElems;
  unsigned NumArgNo = AllocSize->getNumElemsParam().getASTIndex();
  if (!evaluateAsSizeT(Call->getArg(NumArgNo), NumberOfElems))
    return false;

  bool Overflow;
  llvm::APInt BytesAvailable = SizeOfElem.umul_ov(NumberOfElems, Overflow);
  if (Overflow)
    return false;

  Result = std::move(BytesAvailable);
  return true;
}
} // namespace

namespace {
bool getBytesReturnedByAllocSizeCall(const TreeContext &Ctx, const LValue &LVal,
                                     llvm::APInt &Result) {
  assert(isBaseAnAllocSizeCall(LVal.getLValueBase()) &&
         "Can't get the size of a non alloc_size function");
  const auto *Base = LVal.getLValueBase().get<const Expr *>();
  const CallExpr *CE = tryUnwrapAllocSizeCall(Base);
  return getBytesReturnedByAllocSizeCall(Ctx, CE, Result);
}
} // namespace

namespace {
bool evaluateLValueAsAllocSize(EvalInfo &Info, APValue::LValueBase Base,
                               LValue &Result) {
  if (Base.isNull())
    return false;

  // Because we do no form of static analysis, we only support const variables.
  //
  // Additionally, we can't support parameters, nor can we support static
  // variables (in the latter case, use-before-assign isn't UB; in the former,
  // we have no clue what they'll be assigned to).
  const auto *VD =
      dyn_cast_or_null<VarDecl>(Base.dyn_cast<const ValueDecl *>());
  if (!VD || !VD->isLocalVarDecl() || !VD->getType().isConstQualified())
    return false;

  const Expr *Init = VD->getAnyInitializer();
  if (!Init || Init->getType().isNull())
    return false;

  const Expr *E = Init->IgnoreParens();
  if (!tryUnwrapAllocSizeCall(E))
    return false;

  // Store E instead of E unwrapped so that the type of the LValue's base is
  // what the user wanted.
  Result.setInvalid(E);

  QualType Pointee = E->getType()->castAs<PointerType>()->getPointeeType();
  Result.addUnsizedArray(Info, E, Pointee);
  return true;
}
} // namespace

namespace {
class PointerExprEvaluator : public ExprEvaluatorBase<PointerExprEvaluator> {
  LValue &Result;
  bool InvalidBaseOK;

  bool Success(const Expr *E) {
    Result.set(E);
    return true;
  }

  bool evaluateLValue(const Expr *E, LValue &Result) {
    return ::evaluateLValue(E, Result, Info, InvalidBaseOK);
  }

  bool evaluatePointer(const Expr *E, LValue &Result) {
    return ::evaluatePointer(E, Result, Info, InvalidBaseOK);
  }

  bool visitNonBuiltinCallExpr(const CallExpr *E);

public:
  PointerExprEvaluator(EvalInfo &info, LValue &Result, bool InvalidBaseOK)
      : ExprEvaluatorBaseTy(info), Result(Result),
        InvalidBaseOK(InvalidBaseOK) {}

  bool Success(const APValue &V, const Expr *E) {
    Result.setFrom(Info.Ctx, V);
    return true;
  }
  bool ZeroInitialization(const Expr *E) {
    Result.setNull(Info.Ctx, E->getType());
    return true;
  }

  bool VisitBinaryOperator(const BinaryOperator *E);
  bool VisitCastExpr(const CastExpr *E);
  bool VisitUnaryAddrOf(const UnaryOperator *E);
  bool VisitAddrLabelExpr(const AddrLabelExpr *E) { return Success(E); }
  bool VisitCallExpr(const CallExpr *E);
  bool VisitBuiltinCallExpr(const CallExpr *E, unsigned BuiltinOp);

  bool VisitSourceLocExpr(const SourceLocExpr *E) {
    assert(!E->isIntType() && "SourceLocExpr isn't a pointer type?");
    APValue LValResult = E->EvaluateInContext(
        Info.Ctx, Info.CurrentCall->CurSourceLocExprScope.getDefaultExpr());
    Result.setFrom(Info.Ctx, LValResult);
    return true;
  }
};
} // end anonymous namespace

namespace {
bool evaluatePointer(const Expr *E, LValue &Result, EvalInfo &Info,
                     bool InvalidBaseOK) {

  assert(E->isPRValue() && E->getType()->hasPointerRepresentation());
  return PointerExprEvaluator(Info, Result, InvalidBaseOK).Visit(E);
}
} // namespace

bool PointerExprEvaluator::VisitBinaryOperator(const BinaryOperator *E) {
  if (E->getOpcode() != BO_Add && E->getOpcode() != BO_Sub)
    return ExprEvaluatorBaseTy::VisitBinaryOperator(E);

  const Expr *PExp = E->getLHS();
  const Expr *IExp = E->getRHS();
  if (IExp->getType()->isPointerType())
    std::swap(PExp, IExp);

  bool EvalPtrOK = evaluatePointer(PExp, Result);
  if (!EvalPtrOK && !Info.noteFailure())
    return false;

  llvm::APSInt Offset;
  if (!evaluateInteger(IExp, Offset, Info) || !EvalPtrOK)
    return false;

  if (E->getOpcode() == BO_Sub)
    negateAsSigned(Offset);

  QualType Pointee = PExp->getType()->castAs<PointerType>()->getPointeeType();
  return handleLValueArrayAdjustment(Info, E, Result, Pointee, Offset);
}

bool PointerExprEvaluator::VisitUnaryAddrOf(const UnaryOperator *E) {
  return evaluateLValue(E->getSubExpr(), Result);
}

bool PointerExprEvaluator::VisitCastExpr(const CastExpr *E) {
  const Expr *SubExpr = E->getSubExpr();

  switch (E->getCastKind()) {
  default:
    break;
  case CK_BitCast:
  case CK_AddressSpaceConversion:
    if (!Visit(SubExpr))
      return false;
    // Bitcast to non-void* is ok; from void* is not a constant pointer cast.
    if (!E->getType()->isVoidPointerType()) {
      if (SubExpr->getType()->isVoidPointerType()) {
        bool HasValidResult = !Result.InvalidBase &&
                              !Result.Designator.Invalid && !Result.IsNullPtr;
        if (HasValidResult)
          CCEDiag(E, diag::note_constexpr_invalid_void_star_cast)
              << SubExpr->getType();
        else
          CCEDiag(E, diag::note_constexpr_invalid_cast)
              << 1 << SubExpr->getType();
      } else
        CCEDiag(E, diag::note_constexpr_invalid_cast) << 0;
      Result.Designator.setInvalid();
    }
    if (E->getCastKind() == CK_AddressSpaceConversion && Result.IsNullPtr)
      ZeroInitialization(E);
    return true;

  case CK_NullToPointer:
    VisitIgnoredValue(E->getSubExpr());
    return ZeroInitialization(E);

  case CK_IntegralToPointer: {
    CCEDiag(E, diag::note_constexpr_invalid_cast) << 0;

    APValue Value;
    if (!evaluateIntegerOrLValue(SubExpr, Value, Info))
      break;

    if (Value.isInt()) {
      unsigned Size = Info.Ctx.getTypeSize(E->getType());
      uint64_t N = Value.getInt().extOrTrunc(Size).getZExtValue();
      Result.Base = (Expr *)nullptr;
      Result.InvalidBase = false;
      Result.Offset = CharUnits::fromQuantity(N);
      Result.Designator.setInvalid();
      Result.IsNullPtr = false;
      return true;
    } else {
      // Cast is of an lvalue, no need to change value.
      Result.setFrom(Info.Ctx, Value);
      return true;
    }
  }

  case CK_ArrayToPointerDecay: {
    if (SubExpr->isLValue()) {
      if (!evaluateLValue(SubExpr, Result))
        return false;
    } else {
      APValue &Value = Info.CurrentCall->createTemporary(
          SubExpr, SubExpr->getType(), ScopeKind::FullExpression, Result);
      if (!evaluateInPlace(Value, Info, Result, SubExpr))
        return false;
    }
    // The result is a pointer to the first element of the array.
    auto *AT = Info.Ctx.getAsArrayType(SubExpr->getType());
    if (auto *CAT = dyn_cast<ConstantArrayType>(AT))
      Result.addArray(Info, E, CAT);
    else
      Result.addUnsizedArray(Info, E, AT->getElementType());
    return true;
  }

  case CK_FunctionToPointerDecay:
    return evaluateLValue(SubExpr, Result);

  case CK_LValueToRValue: {
    LValue LVal;
    if (!evaluateLValue(E->getSubExpr(), LVal))
      return false;

    APValue RVal;
    // Note, we use the subexpression's type in order to retain cv-qualifiers.
    if (!handleLValueToRValueConversion(Info, E, E->getSubExpr()->getType(),
                                        LVal, RVal))
      return InvalidBaseOK &&
             evaluateLValueAsAllocSize(Info, LVal.Base, Result);
    return Success(RVal, E);
  }
  }

  return ExprEvaluatorBaseTy::VisitCastExpr(E);
}

namespace {
CharUnits getAlignOfType(EvalInfo &Info, QualType T,
                         UnaryExprOrTypeTrait ExprKind) {
  if (T.getQualifiers().hasUnaligned())
    return CharUnits::One();

  const bool AlignOfReturnsPreferred =
      Info.Ctx.getLangOpts().getNeverCABICompat() <=
      LangOptions::NeverCABI::Ver7;

  // __alignof is defined to return the preferred alignment.
  // Before 8, NeverC returned the preferred alignment for alignof and _Alignof
  // as well.
  if (ExprKind == UETT_PreferredAlignOf || AlignOfReturnsPreferred)
    return Info.Ctx.toCharUnitsFromBits(
        Info.Ctx.getPreferredTypeAlign(T.getTypePtr()));
  // alignof and _Alignof are defined to return the ABI alignment.
  else if (ExprKind == UETT_AlignOf)
    return Info.Ctx.getTypeAlignInChars(T.getTypePtr());
  else
    llvm_unreachable("getAlignOfType on a non-alignment ExprKind");
}
} // namespace

namespace {
CharUnits getAlignOfExpr(EvalInfo &Info, const Expr *E,
                         UnaryExprOrTypeTrait ExprKind) {
  E = E->IgnoreParens();

  // The kinds of expressions that we have special-case logic here for
  // should be kept up to date with the special checks for those
  // expressions in Sema.

  // alignof decl is always accepted, even if it doesn't make sense: we default
  // to 1 in those cases.
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    return Info.Ctx.getDeclAlign(DRE->getDecl(),
                                 /*RefAsPointee*/ true);

  if (const MemberExpr *ME = dyn_cast<MemberExpr>(E))
    return Info.Ctx.getDeclAlign(ME->getMemberDecl(),
                                 /*RefAsPointee*/ true);

  return getAlignOfType(Info, E->getType(), ExprKind);
}
} // namespace

namespace {
CharUnits getBaseAlignment(EvalInfo &Info, const LValue &Value) {
  if (const auto *VD = Value.Base.dyn_cast<const ValueDecl *>())
    return Info.Ctx.getDeclAlign(VD);
  if (const auto *E = Value.Base.dyn_cast<const Expr *>())
    return getAlignOfExpr(Info, E, UETT_AlignOf);
  return CharUnits::One();
}
} // namespace

namespace {
bool getAlignmentArgument(const Expr *E, QualType ForType, EvalInfo &Info,
                          APSInt &Alignment) {
  if (!evaluateInteger(E, Alignment, Info))
    return false;
  if (Alignment < 0 || !Alignment.isPowerOf2()) {
    Info.FFDiag(E, diag::note_constexpr_invalid_alignment) << Alignment;
    return false;
  }
  unsigned SrcWidth = Info.Ctx.getIntWidth(ForType);
  APSInt MaxValue(APInt::getOneBitSet(SrcWidth, SrcWidth - 1));
  if (APSInt::compareValues(Alignment, MaxValue) > 0) {
    Info.FFDiag(E, diag::note_constexpr_alignment_too_big)
        << MaxValue << ForType << Alignment;
    return false;
  }
  // Ensure both alignment and source value have the same bit width so that we
  // don't assert when computing the resulting value.
  APSInt ExtAlignment =
      APSInt(Alignment.zextOrTrunc(SrcWidth), /*isUnsigned=*/true);
  assert(APSInt::compareValues(Alignment, ExtAlignment) == 0 &&
         "Alignment should not be changed by ext/trunc");
  Alignment = ExtAlignment;
  assert(Alignment.getBitWidth() == SrcWidth);
  return true;
}
} // namespace

// To be clear: this happily visits unsupported builtins. Better name welcomed.
bool PointerExprEvaluator::visitNonBuiltinCallExpr(const CallExpr *E) {
  if (ExprEvaluatorBaseTy::VisitCallExpr(E))
    return true;

  if (!(InvalidBaseOK && getAllocSizeAttr(E)))
    return false;

  Result.setInvalid(E);
  QualType PointeeTy = E->getType()->castAs<PointerType>()->getPointeeType();
  Result.addUnsizedArray(Info, E, PointeeTy);
  return true;
}

bool PointerExprEvaluator::VisitCallExpr(const CallExpr *E) {
  if (!IsConstantevaluatedBuiltinCall(E))
    return visitNonBuiltinCallExpr(E);
  return VisitBuiltinCallExpr(E, E->getBuiltinCallee());
}

// Determine if T is a character type for which we guarantee that
// sizeof(T) == 1.
namespace {
bool isOneByteCharacterType(QualType T) {
  return T->isCharType() || T->isChar8Type();
}
} // namespace

bool PointerExprEvaluator::VisitBuiltinCallExpr(const CallExpr *E,
                                                unsigned BuiltinOp) {
  if (isNoOpCall(E))
    return Success(E);

  switch (BuiltinOp) {
  case Builtin::BI__builtin_assume_aligned: {
    // We need to be very careful here because: if the pointer does not have the
    // asserted alignment, then the behavior is undefined, and undefined
    // behavior is non-constant.
    if (!evaluatePointer(E->getArg(0), Result))
      return false;

    LValue OffsetResult(Result);
    APSInt Alignment;
    if (!getAlignmentArgument(E->getArg(1), E->getArg(0)->getType(), Info,
                              Alignment))
      return false;
    CharUnits Align = CharUnits::fromQuantity(Alignment.getZExtValue());

    if (E->getNumArgs() > 2) {
      APSInt Offset;
      if (!evaluateInteger(E->getArg(2), Offset, Info))
        return false;

      int64_t AdditionalOffset = -Offset.getZExtValue();
      OffsetResult.Offset += CharUnits::fromQuantity(AdditionalOffset);
    }

    // If there is a base object, then it must have the correct alignment.
    if (OffsetResult.Base) {
      CharUnits BaseAlignment = getBaseAlignment(Info, OffsetResult);

      if (BaseAlignment < Align) {
        Result.Designator.setInvalid();
        CCEDiag(E->getArg(0), diag::note_constexpr_baa_insufficient_alignment)
            << 0 << (unsigned)BaseAlignment.getQuantity()
            << (unsigned)Align.getQuantity();
        return false;
      }
    }

    // The offset must also have the correct alignment.
    if (OffsetResult.Offset.alignTo(Align) != OffsetResult.Offset) {
      Result.Designator.setInvalid();

      (OffsetResult.Base
           ? CCEDiag(E->getArg(0),
                     diag::note_constexpr_baa_insufficient_alignment)
                 << 1
           : CCEDiag(E->getArg(0),
                     diag::note_constexpr_baa_value_insufficient_alignment))
          << (int)OffsetResult.Offset.getQuantity()
          << (unsigned)Align.getQuantity();
      return false;
    }

    return true;
  }
  case Builtin::BI__builtin_align_up:
  case Builtin::BI__builtin_align_down: {
    if (!evaluatePointer(E->getArg(0), Result))
      return false;
    APSInt Alignment;
    if (!getAlignmentArgument(E->getArg(1), E->getArg(0)->getType(), Info,
                              Alignment))
      return false;
    CharUnits BaseAlignment = getBaseAlignment(Info, Result);
    CharUnits PtrAlign = BaseAlignment.alignmentAtOffset(Result.Offset);
    // For align_up/align_down, we can return the same value if the alignment
    // is known to be greater or equal to the requested value.
    if (PtrAlign.getQuantity() >= Alignment)
      return true;

    // The alignment could be greater than the minimum at run-time, so we cannot
    // infer much about the resulting pointer value. One case is possible:
    // For `_Alignas(32) char buf[N]; __builtin_align_down(&buf[idx], 32)` we
    // can infer the correct index if the requested alignment is smaller than
    // the base alignment so we can perform the computation on the offset.
    if (BaseAlignment.getQuantity() >= Alignment) {
      assert(Alignment.getBitWidth() <= 64 &&
             "Cannot handle > 64-bit address-space");
      uint64_t Alignment64 = Alignment.getZExtValue();
      CharUnits NewOffset = CharUnits::fromQuantity(
          BuiltinOp == Builtin::BI__builtin_align_down
              ? llvm::alignDown(Result.Offset.getQuantity(), Alignment64)
              : llvm::alignTo(Result.Offset.getQuantity(), Alignment64));
      Result.adjustOffset(NewOffset - Result.Offset);
      return true;
    }
    // Otherwise, we cannot constant-evaluate the result.
    Info.FFDiag(E->getArg(0), diag::note_constexpr_alignment_adjust)
        << Alignment;
    return false;
  }
  case Builtin::BIstrchr:
  case Builtin::BIwcschr:
  case Builtin::BImemchr:
  case Builtin::BIwmemchr:
    Info.CCEDiag(E, diag::note_invalid_subexpr_in_const_expr);
    [[fallthrough]];
  case Builtin::BI__builtin_strchr:
  case Builtin::BI__builtin_wcschr:
  case Builtin::BI__builtin_memchr:
  case Builtin::BI__builtin_char_memchr:
  case Builtin::BI__builtin_wmemchr: {
    if (!Visit(E->getArg(0)))
      return false;
    APSInt Desired;
    if (!evaluateInteger(E->getArg(1), Desired, Info))
      return false;
    uint64_t MaxLength = uint64_t(-1);
    if (BuiltinOp != Builtin::BIstrchr && BuiltinOp != Builtin::BIwcschr &&
        BuiltinOp != Builtin::BI__builtin_strchr &&
        BuiltinOp != Builtin::BI__builtin_wcschr) {
      APSInt N;
      if (!evaluateInteger(E->getArg(2), N, Info))
        return false;
      MaxLength = N.getZExtValue();
    }
    // We cannot find the value if there are no candidates to match against.
    if (MaxLength == 0u)
      return ZeroInitialization(E);
    if (!Result.checkNullPointerForFoldAccess(Info, E, AK_Read) ||
        Result.Designator.Invalid)
      return false;
    QualType CharTy = Result.Designator.getType(Info.Ctx);
    bool IsRawByte = BuiltinOp == Builtin::BImemchr ||
                     BuiltinOp == Builtin::BI__builtin_memchr;
    assert(IsRawByte || Info.Ctx.hasSameUnqualifiedType(
                            CharTy, E->getArg(0)->getType()->getPointeeType()));
    // Pointers to const void may point to objects of incomplete type.
    if (IsRawByte && CharTy->isIncompleteType()) {
      Info.FFDiag(E, diag::note_constexpr_ltor_incomplete_type) << CharTy;
      return false;
    }
    // Give up on byte-oriented matching against multibyte elements.
    if (IsRawByte && !isOneByteCharacterType(CharTy)) {
      Info.FFDiag(E, diag::note_constexpr_memchr_unsupported)
          << ("'" + Info.Ctx.BuiltinInfo.getName(BuiltinOp) + "'").str()
          << CharTy;
      return false;
    }
    // Figure out what value we're actually looking for (after converting to
    // the corresponding unsigned type if necessary).
    uint64_t DesiredVal;
    bool StopAtNull = false;
    switch (BuiltinOp) {
    case Builtin::BIstrchr:
    case Builtin::BI__builtin_strchr:
      // strchr compares directly to the passed integer, and therefore
      // always fails if given an int that is not a char.
      if (!APSInt::isSameValue(handleIntToIntCast(Info, E, CharTy,
                                                  E->getArg(1)->getType(),
                                                  Desired),
                               Desired))
        return ZeroInitialization(E);
      StopAtNull = true;
      [[fallthrough]];
    case Builtin::BImemchr:
    case Builtin::BI__builtin_memchr:
    case Builtin::BI__builtin_char_memchr:
      // memchr compares by converting both sides to unsigned char. That's also
      // correct for strchr if we get this far (to cope with plain char being
      // unsigned in the strchr case).
      DesiredVal = Desired.trunc(Info.Ctx.getCharWidth()).getZExtValue();
      break;

    case Builtin::BIwcschr:
    case Builtin::BI__builtin_wcschr:
      StopAtNull = true;
      [[fallthrough]];
    case Builtin::BIwmemchr:
    case Builtin::BI__builtin_wmemchr:
      // wcschr and wmemchr are given a wchar_t to look for. Just use it.
      DesiredVal = Desired.getZExtValue();
      break;
    }

    for (; MaxLength; --MaxLength) {
      APValue Char;
      if (!handleLValueToRValueConversion(Info, E, CharTy, Result, Char) ||
          !Char.isInt())
        return false;
      if (Char.getInt().getZExtValue() == DesiredVal)
        return true;
      if (StopAtNull && !Char.getInt())
        break;
      if (!handleLValueArrayAdjustment(Info, E, Result, CharTy, 1))
        return false;
    }
    // Not found: return nullptr.
    return ZeroInitialization(E);
  }

  case Builtin::BImemcpy:
  case Builtin::BImemmove:
  case Builtin::BIwmemcpy:
  case Builtin::BIwmemmove:
    Info.CCEDiag(E, diag::note_invalid_subexpr_in_const_expr);
    [[fallthrough]];
  case Builtin::BI__builtin_memcpy:
  case Builtin::BI__builtin_memmove:
  case Builtin::BI__builtin_wmemcpy:
  case Builtin::BI__builtin_wmemmove: {
    bool WChar = BuiltinOp == Builtin::BIwmemcpy ||
                 BuiltinOp == Builtin::BIwmemmove ||
                 BuiltinOp == Builtin::BI__builtin_wmemcpy ||
                 BuiltinOp == Builtin::BI__builtin_wmemmove;
    bool Move = BuiltinOp == Builtin::BImemmove ||
                BuiltinOp == Builtin::BIwmemmove ||
                BuiltinOp == Builtin::BI__builtin_memmove ||
                BuiltinOp == Builtin::BI__builtin_wmemmove;

    // The result of mem* is the first argument.
    if (!Visit(E->getArg(0)))
      return false;
    LValue Dest = Result;

    LValue Src;
    if (!::evaluatePointer(E->getArg(1), Src, Info))
      return false;

    APSInt N;
    if (!evaluateInteger(E->getArg(2), N, Info))
      return false;
    assert(!N.isSigned() && "memcpy and friends take an unsigned size");

    // If the size is zero, we treat this as always being a valid no-op.
    // (Even if one of the src and dest pointers is null.)
    if (!N)
      return true;

    // Otherwise, if either of the operands is null, we can't proceed. Don't
    // try to determine the type of the copied objects, because there aren't
    // any.
    if (!Src.Base || !Dest.Base) {
      APValue Val;
      (!Src.Base ? Src : Dest).moveInto(Val);
      Info.FFDiag(E, diag::note_constexpr_memcpy_null)
          << Move << WChar << !!Src.Base
          << Val.getAsString(Info.Ctx, E->getArg(0)->getType());
      return false;
    }
    if (Src.Designator.Invalid || Dest.Designator.Invalid)
      return false;

    // We require that Src and Dest are both pointers to arrays of
    // trivially-copyable type. (For the wide version, the designator will be
    // invalid if the designated object is not a wchar_t.)
    QualType T = Dest.Designator.getType(Info.Ctx);
    QualType SrcT = Src.Designator.getType(Info.Ctx);
    if (!Info.Ctx.hasSameUnqualifiedType(T, SrcT)) {
      Info.FFDiag(E, diag::note_constexpr_memcpy_type_pun) << Move << SrcT << T;
      return false;
    }
    if (T->isIncompleteType()) {
      Info.FFDiag(E, diag::note_constexpr_memcpy_incomplete_type) << Move << T;
      return false;
    }
    if (!T.isTriviallyCopyableType(Info.Ctx)) {
      Info.FFDiag(E, diag::note_constexpr_memcpy_nontrivial) << Move << T;
      return false;
    }

    // Figure out how many T's we're copying.
    uint64_t TSize = Info.Ctx.getTypeSizeInChars(T).getQuantity();
    if (TSize == 0)
      return false;
    if (!WChar) {
      uint64_t Remainder;
      llvm::APInt OrigN = N;
      llvm::APInt::udivrem(OrigN, TSize, N, Remainder);
      if (Remainder) {
        Info.FFDiag(E, diag::note_constexpr_memcpy_unsupported)
            << Move << WChar << 0 << T << toString(OrigN, 10, /*Signed*/ false)
            << (unsigned)TSize;
        return false;
      }
    }

    // Check that the copying will remain within the arrays, just so that we
    // can give a more meaningful diagnostic. This implicitly also checks that
    // N fits into 64 bits.
    uint64_t RemainingSrcSize = Src.Designator.validIndexAdjustments().second;
    uint64_t RemainingDestSize = Dest.Designator.validIndexAdjustments().second;
    if (N.ugt(RemainingSrcSize) || N.ugt(RemainingDestSize)) {
      Info.FFDiag(E, diag::note_constexpr_memcpy_unsupported)
          << Move << WChar << (N.ugt(RemainingSrcSize) ? 1 : 2) << T
          << toString(N, 10, /*Signed*/ false);
      return false;
    }
    uint64_t NElems = N.getZExtValue();
    uint64_t NBytes = NElems * TSize;

    // Check for overlap.
    int Direction = 1;
    if (hasSameBase(Src, Dest)) {
      uint64_t SrcOffset = Src.getLValueOffset().getQuantity();
      uint64_t DestOffset = Dest.getLValueOffset().getQuantity();
      if (DestOffset >= SrcOffset && DestOffset - SrcOffset < NBytes) {
        // Dest is inside the source region.
        if (!Move) {
          Info.FFDiag(E, diag::note_constexpr_memcpy_overlap) << WChar;
          return false;
        }
        // For memmove and friends, copy backwards.
        if (!handleLValueArrayAdjustment(Info, E, Src, T, NElems - 1) ||
            !handleLValueArrayAdjustment(Info, E, Dest, T, NElems - 1))
          return false;
        Direction = -1;
      } else if (!Move && SrcOffset >= DestOffset &&
                 SrcOffset - DestOffset < NBytes) {
        // Src is inside the destination region for memcpy: invalid.
        Info.FFDiag(E, diag::note_constexpr_memcpy_overlap) << WChar;
        return false;
      }
    }

    while (true) {
      APValue Val;
      if (!handleLValueToRValueConversion(Info, E, T, Src, Val) ||
          !handleAssignment(Info, E, Dest, T, Val))
        return false;
      // Do not iterate past the last element; if we're copying backwards, that
      // might take us off the start of the array.
      if (--NElems == 0)
        return true;
      if (!handleLValueArrayAdjustment(Info, E, Src, T, Direction) ||
          !handleLValueArrayAdjustment(Info, E, Dest, T, Direction))
        return false;
    }
  }

  default:
    return false;
  }
}

namespace {
class RecordExprEvaluator : public ExprEvaluatorBase<RecordExprEvaluator> {
  const LValue &This;
  APValue &Result;

public:
  RecordExprEvaluator(EvalInfo &info, const LValue &This, APValue &Result)
      : ExprEvaluatorBaseTy(info), This(This), Result(Result) {}

  bool Success(const APValue &V, const Expr *E) {
    Result = V;
    return true;
  }
  bool ZeroInitialization(const Expr *E) {
    return ZeroInitialization(E, E->getType());
  }
  bool ZeroInitialization(const Expr *E, QualType T);

  bool VisitCallExpr(const CallExpr *E) {
    return handleCallExpr(E, Result, &This);
  }
  bool VisitInitListExpr(const InitListExpr *E);
  bool VisitParenListOrInitListExpr(const Expr *ExprToVisit,
                                    llvm::ArrayRef<Expr *> Args);
};
} // namespace

namespace {
bool handleRecordZeroInitialization(EvalInfo &Info, const Expr *E,
                                    const RecordDecl *RD, const LValue &This,
                                    APValue &Result) {
  assert(!RD->isUnion() && "Expected non-union record type");
  Result = APValue(APValue::UninitStruct(),
                   std::distance(RD->field_begin(), RD->field_end()));

  if (RD->isInvalidDecl())
    return false;
  const StructRecordLayout &Layout = Info.Ctx.getStructRecordLayout(RD);

  for (const auto *I : RD->fields()) {
    if (I->isUnnamedBitfield())
      continue;

    LValue Subobject = This;
    if (!handleLValueMember(Info, E, Subobject, I, &Layout))
      return false;

    ImplicitValueInitExpr VIE(I->getType());
    if (!evaluateInPlace(Result.getStructField(I->getFieldIndex()), Info,
                         Subobject, &VIE))
      return false;
  }

  return true;
}
} // namespace

bool RecordExprEvaluator::ZeroInitialization(const Expr *E, QualType T) {
  const RecordDecl *RD = T->castAs<RecordType>()->getDecl();
  if (RD->isInvalidDecl())
    return false;
  if (RD->isUnion()) {
    // Union: zero the first ordinary named member.
    RecordDecl::field_iterator I = RD->field_begin();
    while (I != RD->field_end() && (*I)->isUnnamedBitfield())
      ++I;
    if (I == RD->field_end()) {
      Result = APValue((const FieldDecl *)nullptr);
      return true;
    }

    LValue Subobject = This;
    if (!handleLValueMember(Info, E, Subobject, *I))
      return false;
    Result = APValue(*I);
    ImplicitValueInitExpr VIE(I->getType());
    return evaluateInPlace(Result.getUnionValue(), Info, Subobject, &VIE);
  }

  return handleRecordZeroInitialization(Info, E, RD, This, Result);
}

bool RecordExprEvaluator::VisitInitListExpr(const InitListExpr *E) {
  if (E->isTransparent())
    return Visit(E->getInit(0));
  return VisitParenListOrInitListExpr(E, E->inits());
}

bool RecordExprEvaluator::VisitParenListOrInitListExpr(
    const Expr *ExprToVisit, llvm::ArrayRef<Expr *> Args) {
  const RecordDecl *RD =
      ExprToVisit->getType()->castAs<RecordType>()->getDecl();
  if (RD->isInvalidDecl())
    return false;
  const StructRecordLayout &Layout = Info.Ctx.getStructRecordLayout(RD);

  if (RD->isUnion()) {
    const FieldDecl *Field;
    if (auto *ILE = dyn_cast<InitListExpr>(ExprToVisit)) {
      Field = ILE->getInitializedFieldInUnion();
    } else {
      llvm_unreachable("union initializer is not InitListExpr");
    }

    Result = APValue(Field);
    if (!Field)
      return true;

    // If the initializer list for a union does not contain any elements, the
    // first element of the union is value-initialized.
    ImplicitValueInitExpr VIE(Field->getType());
    const Expr *InitExpr = Args.empty() ? &VIE : Args[0];

    LValue Subobject = This;
    if (!handleLValueMember(Info, InitExpr, Subobject, Field, &Layout))
      return false;

    if (evaluateInPlace(Result.getUnionValue(), Info, Subobject, InitExpr)) {
      if (Field->isBitField())
        return truncateBitfieldValue(Info, InitExpr, Result.getUnionValue(),
                                     Field);
      return true;
    }

    return false;
  }

  if (!Result.hasValue())
    Result = APValue(APValue::UninitStruct(),
                     std::distance(RD->field_begin(), RD->field_end()));
  unsigned ElementNo = 0;
  bool Success = true;

  // Initialize members.
  for (const auto *Field : RD->fields()) {
    // Anonymous bit-fields are not considered members of the struct for
    // purposes of aggregate initialization.
    if (Field->isUnnamedBitfield())
      continue;

    LValue Subobject = This;

    bool HaveInit = ElementNo < Args.size();

    if (!handleLValueMember(Info, HaveInit ? Args[ElementNo] : ExprToVisit,
                            Subobject, Field, &Layout))
      return false;

    // Perform an implicit value-initialization for members beyond the end of
    // the initializer list.
    ImplicitValueInitExpr VIE(HaveInit ? Info.Ctx.IntTy : Field->getType());
    const Expr *Init = HaveInit ? Args[ElementNo++] : &VIE;

    if (Field->getType()->isIncompleteArrayType()) {
      if (auto *CAT = Info.Ctx.getAsConstantArrayType(Init->getType())) {
        if (!CAT->getSize().isZero()) {
          // Bail out for now. This might sort of "work", but the rest of the
          // code isn't really prepared to handle it.
          Info.FFDiag(Init, diag::note_constexpr_unsupported_flexible_array);
          return false;
        }
      }
    }

    APValue &FieldVal = Result.getStructField(Field->getFieldIndex());
    if (!evaluateInPlace(FieldVal, Info, Subobject, Init) ||
        (Field->isBitField() &&
         !truncateBitfieldValue(Info, Init, FieldVal, Field))) {
      if (!Info.noteFailure())
        return false;
      Success = false;
    }
  }

  return Success;
}

namespace {
bool evaluateRecord(const Expr *E, const LValue &This, APValue &Result,
                    EvalInfo &Info) {

  assert(E->isPRValue() && E->getType()->isRecordType() &&
         "can't evaluate expression as a record rvalue");
  return RecordExprEvaluator(Info, This, Result).Visit(E);
}
} // namespace

// Temporary Evaluation
//
// Temporaries are represented in the AST as rvalues, but generally behave like
// lvalues. The full-object of which the temporary is a subobject is implicitly
// materialized so that a reference can bind to it.
namespace {
class TemporaryExprEvaluator
    : public LValueExprEvaluatorBase<TemporaryExprEvaluator> {
public:
  TemporaryExprEvaluator(EvalInfo &Info, LValue &Result)
      : LValueExprEvaluatorBaseTy(Info, Result, false) {}

  bool VisitInitExpr(const Expr *E) {
    APValue &Value = Info.CurrentCall->createTemporary(
        E, E->getType(), ScopeKind::FullExpression, Result);
    return evaluateInPlace(Value, Info, Result, E);
  }

  bool VisitInitListExpr(const InitListExpr *E) { return VisitInitExpr(E); }
  bool VisitCallExpr(const CallExpr *E) { return VisitInitExpr(E); }
};
} // end anonymous namespace

namespace {
bool evaluateTemporary(const Expr *E, LValue &Result, EvalInfo &Info) {

  assert(E->isPRValue() && E->getType()->isRecordType());
  return TemporaryExprEvaluator(Info, Result).Visit(E);
}
} // namespace

namespace {
class VectorExprEvaluator : public ExprEvaluatorBase<VectorExprEvaluator> {
  APValue &Result;

public:
  VectorExprEvaluator(EvalInfo &info, APValue &Result)
      : ExprEvaluatorBaseTy(info), Result(Result) {}

  bool Success(llvm::ArrayRef<APValue> V, const Expr *E) {
    assert(V.size() == E->getType()->castAs<VectorType>()->getNumElements());
    Result = APValue(V.data(), V.size());
    return true;
  }
  bool Success(const APValue &V, const Expr *E) {
    assert(V.isVector());
    Result = V;
    return true;
  }
  bool ZeroInitialization(const Expr *E);

  bool VisitUnaryReal(const UnaryOperator *E) { return Visit(E->getSubExpr()); }
  bool VisitCastExpr(const CastExpr *E);
  bool VisitInitListExpr(const InitListExpr *E);
  bool VisitUnaryImag(const UnaryOperator *E);
  bool VisitBinaryOperator(const BinaryOperator *E);
  bool VisitUnaryOperator(const UnaryOperator *E);
};
} // end anonymous namespace

namespace {
bool evaluateVector(const Expr *E, APValue &Result, EvalInfo &Info) {
  assert(E->isPRValue() && E->getType()->isVectorType() &&
         "not a vector prvalue");
  return VectorExprEvaluator(Info, Result).Visit(E);
}
} // namespace

bool VectorExprEvaluator::VisitCastExpr(const CastExpr *E) {
  const VectorType *VTy = E->getType()->castAs<VectorType>();
  unsigned NElts = VTy->getNumElements();

  const Expr *SE = E->getSubExpr();
  QualType SETy = SE->getType();

  switch (E->getCastKind()) {
  case CK_VectorSplat: {
    APValue Val = APValue();
    if (SETy->isIntegerType()) {
      APSInt IntResult;
      if (!evaluateInteger(SE, IntResult, Info))
        return false;
      Val = APValue(std::move(IntResult));
    } else if (SETy->isRealFloatingType()) {
      APFloat FloatResult(0.0);
      if (!evaluateFloat(SE, FloatResult, Info))
        return false;
      Val = APValue(std::move(FloatResult));
    } else {
      return Error(E);
    }

    // Splat and create vector APValue.
    llvm::SmallVector<APValue, 4> Elts(NElts, Val);
    return Success(Elts, E);
  }
  case CK_BitCast: {
    APValue SVal;
    if (!evaluate(SVal, Info, SE))
      return false;

    if (!SVal.isInt() && !SVal.isFloat() && !SVal.isVector()) {
      // Give up if the input isn't an int, float, or vector.  For example, we
      // reject "(v4i16)(intptr_t)&a".
      Info.FFDiag(E, diag::note_constexpr_invalid_cast) << 0;
      return false;
    }

    if (!handleRValueToRValueBitCast(Info, Result, SVal, E))
      return false;

    return true;
  }
  default:
    return ExprEvaluatorBaseTy::VisitCastExpr(E);
  }
}

bool VectorExprEvaluator::VisitInitListExpr(const InitListExpr *E) {
  const VectorType *VT = E->getType()->castAs<VectorType>();
  unsigned NumInits = E->getNumInits();
  unsigned NumElements = VT->getNumElements();

  QualType EltTy = VT->getElementType();
  llvm::SmallVector<APValue, 4> Elements;

  // The number of initializers can be less than the number of
  // vector elements. This can be due to nested vector initialization.
  // For GCC compatibility, missing trailing elements
  // should be initialized with zeroes.
  unsigned CountInits = 0, CountElts = 0;
  while (CountElts < NumElements) {
    // Handle nested vector initialization.
    if (CountInits < NumInits &&
        E->getInit(CountInits)->getType()->isVectorType()) {
      APValue v;
      if (!evaluateVector(E->getInit(CountInits), v, Info))
        return Error(E);
      unsigned vlen = v.getVectorLength();
      for (unsigned j = 0; j < vlen; j++)
        Elements.push_back(v.getVectorElt(j));
      CountElts += vlen;
    } else if (EltTy->isIntegerType()) {
      llvm::APSInt sInt(32);
      if (CountInits < NumInits) {
        if (!evaluateInteger(E->getInit(CountInits), sInt, Info))
          return false;
      } else // trailing integer zero.
        sInt = Info.Ctx.MakeIntValue(0, EltTy);
      Elements.push_back(APValue(sInt));
      CountElts++;
    } else {
      llvm::APFloat f(0.0);
      if (CountInits < NumInits) {
        if (!evaluateFloat(E->getInit(CountInits), f, Info))
          return false;
      } else // trailing float zero.
        f = APFloat::getZero(Info.Ctx.getFloatTypeSemantics(EltTy));
      Elements.push_back(APValue(f));
      CountElts++;
    }
    CountInits++;
  }
  return Success(Elements, E);
}

bool VectorExprEvaluator::ZeroInitialization(const Expr *E) {
  const auto *VT = E->getType()->castAs<VectorType>();
  QualType EltTy = VT->getElementType();
  APValue ZeroElement;
  if (EltTy->isIntegerType())
    ZeroElement = APValue(Info.Ctx.MakeIntValue(0, EltTy));
  else
    ZeroElement =
        APValue(APFloat::getZero(Info.Ctx.getFloatTypeSemantics(EltTy)));

  llvm::SmallVector<APValue, 4> Elements(VT->getNumElements(), ZeroElement);
  return Success(Elements, E);
}

bool VectorExprEvaluator::VisitUnaryImag(const UnaryOperator *E) {
  VisitIgnoredValue(E->getSubExpr());
  return ZeroInitialization(E);
}

bool VectorExprEvaluator::VisitBinaryOperator(const BinaryOperator *E) {
  BinaryOperatorKind Op = E->getOpcode();

  if (Op == BO_Comma)
    return ExprEvaluatorBaseTy::VisitBinaryOperator(E);

  Expr *LHS = E->getLHS();
  Expr *RHS = E->getRHS();

  assert(LHS->getType()->isVectorType() && RHS->getType()->isVectorType() &&
         "Must both be vector types");
  // Checking JUST the types are the same would be fine, except shifts don't
  // need to have their types be the same (since you always shift by an int).
  assert(LHS->getType()->castAs<VectorType>()->getNumElements() ==
             E->getType()->castAs<VectorType>()->getNumElements() &&
         RHS->getType()->castAs<VectorType>()->getNumElements() ==
             E->getType()->castAs<VectorType>()->getNumElements() &&
         "All operands must be the same size.");

  APValue LHSValue;
  APValue RHSValue;
  bool LHSOK = evaluate(LHSValue, Info, LHS);
  if (!LHSOK && !Info.noteFailure())
    return false;
  if (!evaluate(RHSValue, Info, RHS) || !LHSOK)
    return false;

  if (!handleVectorVectorBinOp(Info, E, Op, LHSValue, RHSValue))
    return false;

  return Success(LHSValue, E);
}

namespace {
std::optional<APValue> handleVectorUnaryOperator(TreeContext &Ctx,
                                                 QualType ResultTy,
                                                 UnaryOperatorKind Op,
                                                 APValue Elt) {
  switch (Op) {
  case UO_Plus:
    // Nothing to do here.
    return Elt;
  case UO_Minus:
    if (Elt.getKind() == APValue::Int) {
      Elt.getInt().negate();
    } else {
      assert(Elt.getKind() == APValue::Float &&
             "Vector can only be int or float type");
      Elt.getFloat().changeSign();
    }
    return Elt;
  case UO_Not:
    // This is only valid for integral types anyway, so we don't have to handle
    // float here.
    assert(Elt.getKind() == APValue::Int &&
           "Vector operator ~ can only be int");
    Elt.getInt().flipAllBits();
    return Elt;
  case UO_LNot: {
    if (Elt.getKind() == APValue::Int) {
      Elt.getInt() = !Elt.getInt();
      // operator ! on vectors returns -1 for 'truth', so negate it.
      Elt.getInt().negate();
      return Elt;
    }
    assert(Elt.getKind() == APValue::Float &&
           "Vector can only be int or float type");
    // Float types result in an int of the same size, but -1 for true, or 0 for
    // false.
    APSInt EltResult{Ctx.getIntWidth(ResultTy),
                     ResultTy->isUnsignedIntegerType()};
    if (Elt.getFloat().isZero())
      EltResult.setAllBits();
    else
      EltResult.clearAllBits();

    return APValue{EltResult};
  }
  default:
    return std::nullopt;
  }
}
} // namespace

bool VectorExprEvaluator::VisitUnaryOperator(const UnaryOperator *E) {
  Expr *SubExpr = E->getSubExpr();
  const auto *VD = SubExpr->getType()->castAs<VectorType>();
  // This result element type differs in the case of negating a floating point
  // vector, since the result type is the a vector of the equivilant sized
  // integer.
  const QualType ResultEltTy = VD->getElementType();
  UnaryOperatorKind Op = E->getOpcode();

  APValue SubExprValue;
  if (!evaluate(SubExprValue, Info, SubExpr))
    return false;

  if (SubExprValue.isLValue())
    return false;

  assert(SubExprValue.getVectorLength() == VD->getNumElements() &&
         "Vector length doesn't match type?");

  llvm::SmallVector<APValue, 4> ResultElements;
  for (unsigned EltNum = 0; EltNum < VD->getNumElements(); ++EltNum) {
    std::optional<APValue> Elt = handleVectorUnaryOperator(
        Info.Ctx, ResultEltTy, Op, SubExprValue.getVectorElt(EltNum));
    if (!Elt)
      return false;
    ResultElements.push_back(*Elt);
  }
  return Success(APValue(ResultElements.data(), ResultElements.size()), E);
}

namespace {
class ArrayExprEvaluator : public ExprEvaluatorBase<ArrayExprEvaluator> {
  const LValue &This;
  APValue &Result;

public:
  ArrayExprEvaluator(EvalInfo &Info, const LValue &This, APValue &Result)
      : ExprEvaluatorBaseTy(Info), This(This), Result(Result) {}

  bool Success(const APValue &V, const Expr *E) {
    assert(V.isArray() && "expected array");
    Result = V;
    return true;
  }

  bool ZeroInitialization(const Expr *E) {
    const ConstantArrayType *CAT =
        Info.Ctx.getAsConstantArrayType(E->getType());
    if (!CAT) {
      if (E->getType()->isIncompleteArrayType()) {
        // We can be asked to zero-initialize a flexible array member; this
        // is represented as an ImplicitValueInitExpr of incomplete array
        // type. In this case, the array has zero elements.
        Result = APValue(APValue::UninitArray(), 0, 0);
        return true;
      }
      return Error(E);
    }

    Result = APValue(APValue::UninitArray(), 0, CAT->getSize().getZExtValue());
    if (!Result.hasArrayFiller())
      return true;

    // Zero-initialize all elements.
    LValue Subobject = This;
    Subobject.addArray(Info, E, CAT);
    ImplicitValueInitExpr VIE(CAT->getElementType());
    return evaluateInPlace(Result.getArrayFiller(), Info, Subobject, &VIE);
  }

  bool VisitCallExpr(const CallExpr *E) {
    return handleCallExpr(E, Result, &This);
  }
  bool VisitInitListExpr(const InitListExpr *E,
                         QualType AllocType = QualType());
  bool VisitArrayInitLoopExpr(const ArrayInitLoopExpr *E);
  bool VisitStringLiteral(const StringLiteral *E,
                          QualType AllocType = QualType()) {
    expandStringLiteral(Info, E, Result, AllocType);
    return true;
  }
  bool VisitParenListOrInitListExpr(const Expr *ExprToVisit,
                                    llvm::ArrayRef<Expr *> Args,
                                    const Expr *ArrayFiller,
                                    QualType AllocType = QualType());
};
} // end anonymous namespace

namespace {
bool evaluateArray(const Expr *E, const LValue &This, APValue &Result,
                   EvalInfo &Info) {

  assert(E->isPRValue() && E->getType()->isArrayType() &&
         "not an array prvalue");
  return ArrayExprEvaluator(Info, This, Result).Visit(E);
}
} // namespace

// Return true iff the given array filler may depend on the element index.
namespace {
bool maybeElementDependentArrayFiller(const Expr *FillerExpr) {
  // For now, just allow value-initialization and initialization lists
  // comprised of them.
  if (isa<ImplicitValueInitExpr>(FillerExpr))
    return false;
  if (const InitListExpr *ILE = dyn_cast<InitListExpr>(FillerExpr)) {
    for (unsigned I = 0, E = ILE->getNumInits(); I != E; ++I) {
      if (maybeElementDependentArrayFiller(ILE->getInit(I)))
        return true;
    }

    if (ILE->hasArrayFiller() &&
        maybeElementDependentArrayFiller(ILE->getArrayFiller()))
      return true;

    return false;
  }
  return true;
}
} // namespace

bool ArrayExprEvaluator::VisitInitListExpr(const InitListExpr *E,
                                           QualType AllocType) {
  const ConstantArrayType *CAT = Info.Ctx.getAsConstantArrayType(
      AllocType.isNull() ? E->getType() : AllocType);
  if (!CAT)
    return Error(E);

  // Brace-wrapped string literal initializing a char/wchar array.
  if (E->isStringLiteralInit()) {
    auto *SL = dyn_cast<StringLiteral>(E->getInit(0)->IgnoreParenImpCasts());
    if (!SL)
      return Error(E);
    return VisitStringLiteral(SL, AllocType);
  }
  // Any other transparent list init will need proper handling of the
  // AllocType; we can't just recurse to the inner initializer.
  assert(!E->isTransparent() &&
         "transparent array list initialization is not string literal init?");

  return VisitParenListOrInitListExpr(E, E->inits(), E->getArrayFiller(),
                                      AllocType);
}

bool ArrayExprEvaluator::VisitParenListOrInitListExpr(
    const Expr *ExprToVisit, llvm::ArrayRef<Expr *> Args,
    const Expr *ArrayFiller, QualType AllocType) {
  const ConstantArrayType *CAT = Info.Ctx.getAsConstantArrayType(
      AllocType.isNull() ? ExprToVisit->getType() : AllocType);

  bool Success = true;

  assert((!Result.isArray() || Result.getArrayInitializedElts() == 0) &&
         "zero-initialized array shouldn't have any initialized elts");
  APValue Filler;
  if (Result.isArray() && Result.hasArrayFiller())
    Filler = Result.getArrayFiller();

  unsigned NumEltsToInit = Args.size();
  unsigned NumElts = CAT->getSize().getZExtValue();

  // If the initializer might depend on the array index, run it for each
  // array element.
  if (NumEltsToInit != NumElts && maybeElementDependentArrayFiller(ArrayFiller))
    NumEltsToInit = NumElts;

  LLVM_DEBUG(llvm::dbgs() << "The number of elements to initialize: "
                          << NumEltsToInit << ".\n");

  Result = APValue(APValue::UninitArray(), NumEltsToInit, NumElts);

  // If the array was previously zero-initialized, preserve the
  // zero-initialized values.
  if (Filler.hasValue()) {
    for (unsigned I = 0, E = Result.getArrayInitializedElts(); I != E; ++I)
      Result.getArrayInitializedElt(I) = Filler;
    if (Result.hasArrayFiller())
      Result.getArrayFiller() = Filler;
  }

  LValue Subobject = This;
  Subobject.addArray(Info, ExprToVisit, CAT);
  for (unsigned Index = 0; Index != NumEltsToInit; ++Index) {
    const Expr *Init = Index < Args.size() ? Args[Index] : ArrayFiller;
    if (!evaluateInPlace(Result.getArrayInitializedElt(Index), Info, Subobject,
                         Init) ||
        !handleLValueArrayAdjustment(Info, Init, Subobject,
                                     CAT->getElementType(), 1)) {
      if (!Info.noteFailure())
        return false;
      Success = false;
    }
  }

  if (!Result.hasArrayFiller())
    return Success;

  // If we get here, we have a trivial filler, which we can just evaluate
  // once and splat over the rest of the array elements.
  assert(ArrayFiller && "no array filler for incomplete init list");
  return evaluateInPlace(Result.getArrayFiller(), Info, Subobject,
                         ArrayFiller) &&
         Success;
}

bool ArrayExprEvaluator::VisitArrayInitLoopExpr(const ArrayInitLoopExpr *E) {
  LValue CommonLV;
  if (E->getCommonExpr() &&
      !evaluate(Info.CurrentCall->createTemporary(
                    E->getCommonExpr(),
                    getStorageType(Info.Ctx, E->getCommonExpr()),
                    ScopeKind::FullExpression, CommonLV),
                Info, E->getCommonExpr()->getSourceExpr()))
    return false;

  auto *CAT = cast<ConstantArrayType>(E->getType()->castAsArrayTypeUnsafe());

  uint64_t Elements = CAT->getSize().getZExtValue();
  Result = APValue(APValue::UninitArray(), Elements, Elements);

  LValue Subobject = This;
  Subobject.addArray(Info, E, CAT);

  bool Success = true;
  for (EvalInfo::ArrayInitLoopIndex Index(Info); Index != Elements; ++Index) {
    // Scope array-element evaluations so temporaries in default arguments
    // end before the next element (array copy / init rules).
    FullExpressionRAII Scope(Info);

    if (!evaluateInPlace(Result.getArrayInitializedElt(Index), Info, Subobject,
                         E->getSubExpr()) ||
        !handleLValueArrayAdjustment(Info, E, Subobject, CAT->getElementType(),
                                     1)) {
      if (!Info.noteFailure())
        return false;
      Success = false;
    }

    // Make sure we run end-of-scope cleanups.
    Scope.destroy();
  }

  return Success;
}

// Integer Evaluation
//
// As a GNU extension, we support casting pointers to sufficiently-wide integer
// types and back in constant folding. Integer values are thus represented
// either as an integer-valued APValue, or as an lvalue-valued APValue.

namespace {
class IntExprEvaluator : public ExprEvaluatorBase<IntExprEvaluator> {
  APValue &Result;

public:
  IntExprEvaluator(EvalInfo &info, APValue &result)
      : ExprEvaluatorBaseTy(info), Result(result) {}

  bool Success(const llvm::APSInt &SI, const Expr *E, APValue &Result) {
    assert(E->getType()->isIntegralOrEnumerationType() &&
           "Invalid evaluation result.");
    assert(SI.isSigned() == E->getType()->isSignedIntegerOrEnumerationType() &&
           "Invalid evaluation result.");
    assert(SI.getBitWidth() == Info.Ctx.getIntWidth(E->getType()) &&
           "Invalid evaluation result.");
    Result = APValue(SI);
    return true;
  }
  bool Success(const llvm::APSInt &SI, const Expr *E) {
    return Success(SI, E, Result);
  }

  bool Success(const llvm::APInt &I, const Expr *E, APValue &Result) {
    assert(E->getType()->isIntegralOrEnumerationType() &&
           "Invalid evaluation result.");
    assert(I.getBitWidth() == Info.Ctx.getIntWidth(E->getType()) &&
           "Invalid evaluation result.");
    Result = APValue(APSInt(I));
    Result.getInt().setIsUnsigned(
        E->getType()->isUnsignedIntegerOrEnumerationType());
    return true;
  }
  bool Success(const llvm::APInt &I, const Expr *E) {
    return Success(I, E, Result);
  }

  bool Success(uint64_t Value, const Expr *E, APValue &Result) {
    assert(E->getType()->isIntegralOrEnumerationType() &&
           "Invalid evaluation result.");
    Result = APValue(Info.Ctx.MakeIntValue(Value, E->getType()));
    return true;
  }
  bool Success(uint64_t Value, const Expr *E) {
    return Success(Value, E, Result);
  }

  bool Success(CharUnits Size, const Expr *E) {
    return Success(Size.getQuantity(), E);
  }

  bool Success(const APValue &V, const Expr *E) {
    if (V.isLValue() || V.isAddrLabelDiff() || V.isIndeterminate()) {
      Result = V;
      return true;
    }
    return Success(V.getInt(), E);
  }

  bool ZeroInitialization(const Expr *E) { return Success(0, E); }

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  bool VisitIntegerLiteral(const IntegerLiteral *E) {
    return Success(E->getValue(), E);
  }
  bool VisitCharacterLiteral(const CharacterLiteral *E) {
    return Success(E->getValue(), E);
  }

  bool CheckReferencedDecl(const Expr *E, const Decl *D);
  bool VisitDeclRefExpr(const DeclRefExpr *E) {
    if (CheckReferencedDecl(E, E->getDecl()))
      return true;

    return ExprEvaluatorBaseTy::VisitDeclRefExpr(E);
  }
  bool VisitMemberExpr(const MemberExpr *E) {
    if (CheckReferencedDecl(E, E->getMemberDecl())) {
      VisitIgnoredBaseExpression(E->getBase());
      return true;
    }

    return ExprEvaluatorBaseTy::VisitMemberExpr(E);
  }

  bool VisitCallExpr(const CallExpr *E);
  bool VisitBuiltinCallExpr(const CallExpr *E, unsigned BuiltinOp);
  bool VisitBinaryOperator(const BinaryOperator *E);
  bool VisitOffsetOfExpr(const OffsetOfExpr *E);
  bool VisitUnaryOperator(const UnaryOperator *E);

  bool VisitCastExpr(const CastExpr *E);
  bool VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *E);

  bool VisitArrayInitIndexExpr(const ArrayInitIndexExpr *E) {
    if (Info.ArrayInitIndex == uint64_t(-1)) {
      // We were asked to evaluate this subexpression independent of the
      // enclosing ArrayInitLoopExpr. We can't do that.
      Info.FFDiag(E);
      return false;
    }
    return Success(Info.ArrayInitIndex, E);
  }

  bool VisitUnaryReal(const UnaryOperator *E);
  bool VisitUnaryImag(const UnaryOperator *E);

  bool VisitSourceLocExpr(const SourceLocExpr *E);
};

class FixedPointExprEvaluator
    : public ExprEvaluatorBase<FixedPointExprEvaluator> {
  APValue &Result;

public:
  FixedPointExprEvaluator(EvalInfo &info, APValue &result)
      : ExprEvaluatorBaseTy(info), Result(result) {}

  bool Success(const llvm::APInt &I, const Expr *E) {
    return Success(
        APFixedPoint(I, Info.Ctx.getFixedPointSemantics(E->getType())), E);
  }

  bool Success(uint64_t Value, const Expr *E) {
    return Success(
        APFixedPoint(Value, Info.Ctx.getFixedPointSemantics(E->getType())), E);
  }

  bool Success(const APValue &V, const Expr *E) {
    return Success(V.getFixedPoint(), E);
  }

  bool Success(const APFixedPoint &V, const Expr *E) {
    assert(E->getType()->isFixedPointType() && "Invalid evaluation result.");
    assert(V.getWidth() == Info.Ctx.getIntWidth(E->getType()) &&
           "Invalid evaluation result.");
    Result = APValue(V);
    return true;
  }

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  bool VisitFixedPointLiteral(const FixedPointLiteral *E) {
    return Success(E->getValue(), E);
  }

  bool VisitCastExpr(const CastExpr *E);
  bool VisitUnaryOperator(const UnaryOperator *E);
  bool VisitBinaryOperator(const BinaryOperator *E);
};
} // end anonymous namespace

namespace {
bool evaluateIntegerOrLValue(const Expr *E, APValue &Result, EvalInfo &Info) {

  assert(E->isPRValue() && E->getType()->isIntegralOrEnumerationType());
  return IntExprEvaluator(Info, Result).Visit(E);
}
} // namespace

namespace {
bool evaluateInteger(const Expr *E, APSInt &Result, EvalInfo &Info) {

  APValue Val;
  if (!evaluateIntegerOrLValue(E, Val, Info))
    return false;
  if (!Val.isInt()) {
    Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
    return false;
  }
  Result = Val.getInt();
  return true;
}
} // namespace

bool IntExprEvaluator::VisitSourceLocExpr(const SourceLocExpr *E) {
  APValue evaluated = E->EvaluateInContext(
      Info.Ctx, Info.CurrentCall->CurSourceLocExprScope.getDefaultExpr());
  return Success(evaluated, E);
}

namespace {
bool evaluateFixedPoint(const Expr *E, APFixedPoint &Result, EvalInfo &Info) {

  if (E->getType()->isFixedPointType()) {
    APValue Val;
    if (!FixedPointExprEvaluator(Info, Val).Visit(E))
      return false;
    if (!Val.isFixedPoint())
      return false;

    Result = Val.getFixedPoint();
    return true;
  }
  return false;
}
} // namespace

namespace {
bool evaluateFixedPointOrInteger(const Expr *E, APFixedPoint &Result,
                                 EvalInfo &Info) {

  if (E->getType()->isIntegerType()) {
    auto FXSema = Info.Ctx.getFixedPointSemantics(E->getType());
    APSInt Val;
    if (!evaluateInteger(E, Val, Info))
      return false;
    Result = APFixedPoint(Val, FXSema);
    return true;
  } else if (E->getType()->isFixedPointType()) {
    return evaluateFixedPoint(E, Result, Info);
  }
  return false;
}
} // namespace

bool IntExprEvaluator::CheckReferencedDecl(const Expr *E, const Decl *D) {
  // Enums are integer constant exprs.
  if (const EnumConstantDecl *ECD = dyn_cast<EnumConstantDecl>(D)) {
    // Check for signedness/width mismatches between E type and ECD value.
    bool SameSign = (ECD->getInitVal().isSigned() ==
                     E->getType()->isSignedIntegerOrEnumerationType());
    bool SameWidth =
        (ECD->getInitVal().getBitWidth() == Info.Ctx.getIntWidth(E->getType()));
    if (SameSign && SameWidth)
      return Success(ECD->getInitVal(), E);
    else {
      // Get rid of mismatch (otherwise Success assertions will fail)
      // by computing a new value matching the type of E.
      llvm::APSInt Val = ECD->getInitVal();
      if (!SameSign)
        Val.setIsSigned(!ECD->getInitVal().isSigned());
      if (!SameWidth)
        Val = Val.extOrTrunc(Info.Ctx.getIntWidth(E->getType()));
      return Success(Val, E);
    }
  }
  return false;
}

namespace {
GCCTypeClass evaluateBuiltinClassifyType(QualType T,
                                         const LangOptions &LangOpts) {

  QualType CanTy = T.getCanonicalType();

  switch (CanTy->getTypeClass()) {
#define TYPE(ID, BASE)
#define DEPENDENT_TYPE(ID, BASE) case Type::ID:
#define NON_CANONICAL_TYPE(ID, BASE) case Type::ID:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(ID, BASE) case Type::ID:
#include "neverc/Tree/TypeNodes.td.h"
  case Type::Auto:
    llvm_unreachable("unexpected non-canonical or dependent type");

  case Type::Builtin:
    switch (cast<BuiltinType>(CanTy)->getKind()) {
#define BUILTIN_TYPE(ID, SINGLETON_ID)
#define SIGNED_TYPE(ID, SINGLETON_ID)                                          \
  case BuiltinType::ID:                                                        \
    return GCCTypeClass::Integer;
#define FLOATING_TYPE(ID, SINGLETON_ID)                                        \
  case BuiltinType::ID:                                                        \
    return GCCTypeClass::RealFloat;
#define PLACEHOLDER_TYPE(ID, SINGLETON_ID)                                     \
  case BuiltinType::ID:                                                        \
    break;
#include "neverc/Tree/Type/BuiltinTypes.def"
    case BuiltinType::Void:
      return GCCTypeClass::Void;

    case BuiltinType::Bool:
      return GCCTypeClass::Bool;

    case BuiltinType::Char_U:
    case BuiltinType::UChar:
    case BuiltinType::WChar_U:
    case BuiltinType::Char8:
    case BuiltinType::Char16:
    case BuiltinType::Char32:
    case BuiltinType::UShort:
    case BuiltinType::UInt:
    case BuiltinType::ULong:
    case BuiltinType::ULongLong:
    case BuiltinType::UInt128:
      return GCCTypeClass::Integer;

    case BuiltinType::UShortAccum:
    case BuiltinType::UAccum:
    case BuiltinType::ULongAccum:
    case BuiltinType::UShortFract:
    case BuiltinType::UFract:
    case BuiltinType::ULongFract:
    case BuiltinType::SatUShortAccum:
    case BuiltinType::SatUAccum:
    case BuiltinType::SatULongAccum:
    case BuiltinType::SatUShortFract:
    case BuiltinType::SatUFract:
    case BuiltinType::SatULongFract:
      return GCCTypeClass::None;

    case BuiltinType::NullPtr:
#define SVE_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
      return GCCTypeClass::None;

    case BuiltinType::Dependent:
      llvm_unreachable("unexpected dependent type");
    };
    llvm_unreachable("unexpected placeholder type");

  case Type::Enum:
    return GCCTypeClass::Integer;

  case Type::Pointer:
  case Type::ConstantArray:
  case Type::VariableArray:
  case Type::IncompleteArray:
  case Type::FunctionNoProto:
  case Type::FunctionProto:
    return GCCTypeClass::Pointer;

  case Type::Complex:
    return GCCTypeClass::Complex;

  case Type::Record:
    return CanTy->isUnionType() ? GCCTypeClass::Union
                                : GCCTypeClass::ClassOrStruct;

  case Type::Atomic:
    // GCC classifies _Atomic T the same as T.
    return evaluateBuiltinClassifyType(
        CanTy->castAs<AtomicType>()->getValueType(), LangOpts);

  case Type::Vector:
  case Type::ExtVector:
    return GCCTypeClass::Vector;

  case Type::ConstantMatrix:
    // Classify all other types that don't fit into the regular
    // classification the same way.
    return GCCTypeClass::None;

  case Type::BitInt:
    return GCCTypeClass::BitInt;
  }

  llvm_unreachable("unexpected type class");
}

GCCTypeClass evaluateBuiltinClassifyType(const CallExpr *E,
                                         const LangOptions &LangOpts) {
  // If no argument was supplied, default to None. This isn't
  // ideal, however it is what gcc does.
  if (E->getNumArgs() == 0)
    return GCCTypeClass::None;

  return evaluateBuiltinClassifyType(E->getArg(0)->getType(), LangOpts);
}
} // namespace

namespace {
bool evaluateBuiltinConstantPForLValue(const APValue &LV) {
  APValue::LValueBase Base = LV.getLValueBase();
  if (Base.isNull()) {
    // A null base is acceptable.
    return true;
  } else if (const Expr *E = Base.dyn_cast<const Expr *>()) {
    if (!isa<StringLiteral>(E))
      return false;
    return LV.getLValueOffset().isZero();
  } else {
    // Any other base is not constant enough for GCC.
    return false;
  }
}
} // namespace

namespace {
bool evaluateBuiltinConstantP(EvalInfo &Info, const Expr *Arg) {
  // This evaluation is not permitted to have side-effects, so evaluate it in
  // a speculative evaluation context.
  SpeculativeEvaluationRAII SpeculativeEval(Info);

  // Constant-folding is always enabled for the operand of __builtin_constant_p
  // (even when the enclosing evaluation context otherwise requires a strict
  // language-specific constant expression).
  FoldConstant Fold(Info, true);

  QualType ArgType = Arg->getType();

  // __builtin_constant_p always has one operand. The rules which gcc follows
  // are not precisely documented, but are as follows:
  //
  //  - If the operand is of integral, floating, complex or enumeration type,
  //    and can be folded to a known value of that type, it returns 1.
  //  - If the operand can be folded to a pointer to the first character
  //    of a string literal (or such a pointer cast to an integral type)
  //    or to a null pointer or an integer cast to a pointer, it returns 1.
  //
  // Otherwise, it returns 0.
  //
  if (ArgType->isIntegralOrEnumerationType() || ArgType->isFloatingType() ||
      ArgType->isAnyComplexType() || ArgType->isPointerType() ||
      ArgType->isNullPtrType()) {
    APValue V;
    if (!::evaluateAsRValue(Info, Arg, V) || Info.EvalStatus.HasSideEffects) {
      Fold.keepDiagnostics();
      return false;
    }

    // For a pointer (possibly cast to integer), there are special rules.
    if (V.getKind() == APValue::LValue)
      return evaluateBuiltinConstantPForLValue(V);

    // Otherwise, any constant value is good enough.
    return V.hasValue();
  }

  // Anything else isn't considered to be sufficiently constant.
  return false;
}
} // namespace

namespace {
QualType getObjectType(APValue::LValueBase B) {
  if (const ValueDecl *D = B.dyn_cast<const ValueDecl *>()) {
    if (const VarDecl *VD = dyn_cast<VarDecl>(D))
      return VD->getType();
  } else if (const Expr *E = B.dyn_cast<const Expr *>()) {
    if (isa<CompoundLiteralExpr>(E))
      return E->getType();
  }

  return QualType();
}
} // namespace

namespace {
const Expr *ignorePointerCastsAndParens(const Expr *E) {
  assert(E->isPRValue() && E->getType()->hasPointerRepresentation());

  auto *NoParens = E->IgnoreParens();
  auto *Cast = dyn_cast<CastExpr>(NoParens);
  if (Cast == nullptr)
    return NoParens;

  // We only conservatively allow a few kinds of casts, because this code is
  // inherently a simple solution that seeks to support the common case.
  auto CastKind = Cast->getCastKind();
  if (CastKind != CK_NoOp && CastKind != CK_BitCast &&
      CastKind != CK_AddressSpaceConversion)
    return NoParens;

  auto *SubExpr = Cast->getSubExpr();
  if (!SubExpr->getType()->hasPointerRepresentation() || !SubExpr->isPRValue())
    return NoParens;
  return ignorePointerCastsAndParens(SubExpr);
}
} // namespace

namespace {
bool isDesignatorAtObjectEnd(const TreeContext &Ctx, const LValue &LVal) {
  assert(!LVal.Designator.Invalid);

  auto IsLastOrInvalidFieldDecl = [&Ctx](const FieldDecl *FD, bool &Invalid) {
    const RecordDecl *Parent = FD->getParent();
    Invalid = Parent->isInvalidDecl();
    if (Invalid || Parent->isUnion())
      return true;
    const StructRecordLayout &Layout = Ctx.getStructRecordLayout(Parent);
    return FD->getFieldIndex() + 1 == Layout.getFieldCount();
  };

  auto &Base = LVal.getLValueBase();
  if (auto *ME = dyn_cast_or_null<MemberExpr>(Base.dyn_cast<const Expr *>())) {
    if (auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
      bool Invalid;
      if (!IsLastOrInvalidFieldDecl(FD, Invalid))
        return Invalid;
    } else if (auto *IFD = dyn_cast<IndirectFieldDecl>(ME->getMemberDecl())) {
      for (auto *FD : IFD->chain()) {
        bool Invalid;
        if (!IsLastOrInvalidFieldDecl(cast<FieldDecl>(FD), Invalid))
          return Invalid;
      }
    }
  }

  unsigned I = 0;
  QualType BaseType = getType(Base);
  if (LVal.Designator.FirstEntryIsAnUnsizedArray) {
    // If we don't know the array bound, conservatively assume we're looking at
    // the final array element.
    ++I;
    if (BaseType->isIncompleteArrayType())
      BaseType = Ctx.getAsArrayType(BaseType)->getElementType();
    else
      BaseType = BaseType->castAs<PointerType>()->getPointeeType();
  }

  for (unsigned E = LVal.Designator.Entries.size(); I != E; ++I) {
    const auto &Entry = LVal.Designator.Entries[I];
    if (BaseType->isArrayType()) {
      // Because __builtin_object_size treats arrays as objects, we can ignore
      // the index iff this is the last array in the Designator.
      if (I + 1 == E)
        return true;
      const auto *CAT = cast<ConstantArrayType>(Ctx.getAsArrayType(BaseType));
      uint64_t Index = Entry.getAsArrayIndex();
      if (Index + 1 != CAT->getSize())
        return false;
      BaseType = CAT->getElementType();
    } else if (BaseType->isAnyComplexType()) {
      const auto *CT = BaseType->castAs<ComplexType>();
      uint64_t Index = Entry.getAsArrayIndex();
      if (Index != 1)
        return false;
      BaseType = CT->getElementType();
    } else if (auto *FD = getAsField(Entry)) {
      bool Invalid;
      if (!IsLastOrInvalidFieldDecl(FD, Invalid))
        return Invalid;
      BaseType = FD->getType();
    } else {
      return false;
    }
  }
  return true;
}
} // namespace

namespace {
bool refersToCompleteObject(const LValue &LVal) {
  if (LVal.Designator.Invalid)
    return false;

  if (!LVal.Designator.Entries.empty())
    return LVal.Designator.isMostDerivedAnUnsizedArray();

  if (!LVal.InvalidBase)
    return true;

  // If `E` is a MemberExpr, then the first part of the designator is hiding in
  // the LValueBase.
  const auto *E = LVal.Base.dyn_cast<const Expr *>();
  return !E || !isa<MemberExpr>(E);
}
} // namespace

namespace {
bool isUserWritingOffTheEnd(const TreeContext &Ctx, const LValue &LVal) {
  const SubobjectDesignator &Designator = LVal.Designator;
  // Notes:
  // - Users can only write off of the end when we have an invalid base. Invalid
  //   bases imply we don't know where the memory came from.
  // - We used to be a bit more aggressive here; we'd only be conservative if
  //   the array at the end was flexible, or if it had 0 or 1 elements. This
  //   broke some common standard library extensions (PR30346), but was
  //   otherwise seemingly fine. It may be useful to reintroduce this behavior
  //   with some sort of list. OTOH, it seems that GCC is always
  //   conservative with the last element in structs (if it's an array), so our
  //   current behavior is more compatible than an explicit list approach would
  //   be.
  auto isFlexibleArrayMember = [&] {
    using FAMKind = LangOptions::StrictFlexArraysLevelKind;
    FAMKind StrictFlexArraysLevel =
        Ctx.getLangOpts().getStrictFlexArraysLevel();

    if (Designator.isMostDerivedAnUnsizedArray())
      return true;

    if (StrictFlexArraysLevel == FAMKind::Default)
      return true;

    if (Designator.getMostDerivedArraySize() == 0 &&
        StrictFlexArraysLevel != FAMKind::IncompleteOnly)
      return true;

    if (Designator.getMostDerivedArraySize() == 1 &&
        StrictFlexArraysLevel == FAMKind::OneZeroOrIncomplete)
      return true;

    return false;
  };

  return LVal.InvalidBase &&
         Designator.Entries.size() == Designator.MostDerivedPathLength &&
         Designator.MostDerivedIsArrayElement && isFlexibleArrayMember() &&
         isDesignatorAtObjectEnd(Ctx, LVal);
}
} // namespace

namespace {
bool convertUnsignedAPIntToCharUnits(const llvm::APInt &Int,
                                     CharUnits &Result) {
  auto CharUnitsMax = std::numeric_limits<CharUnits::QuantityType>::max();
  if (Int.ugt(CharUnitsMax))
    return false;
  Result = CharUnits::fromQuantity(Int.getZExtValue());
  return true;
}
} // namespace

namespace {
void addFlexibleArrayMemberInitSize(EvalInfo &Info, const QualType &T,
                                    const LValue &LV, CharUnits &Size) {
  if (!T.isNull() && T->isStructureType() &&
      T->getAsStructureType()->getDecl()->hasFlexibleArrayMember())
    if (const auto *V = LV.getLValueBase().dyn_cast<const ValueDecl *>())
      if (const auto *VD = dyn_cast<VarDecl>(V))
        if (VD->hasInit())
          Size += VD->getFlexibleArrayInitChars(Info.Ctx);
}
} // namespace

namespace {
bool determineEndOffset(EvalInfo &Info, SourceLocation ExprLoc, unsigned Type,
                        const LValue &LVal, CharUnits &EndOffset) {
  bool DetermineForCompleteObject = refersToCompleteObject(LVal);

  auto CheckedhandleSizeof = [&](QualType Ty, CharUnits &Result) {
    if (Ty.isNull() || Ty->isIncompleteType() || Ty->isFunctionType())
      return false;
    return handleSizeof(Info, ExprLoc, Ty, Result);
  };

  // We want to evaluate the size of the entire object. This is a valid fallback
  // for when Type=1 and the designator is invalid, because we're asked for an
  // upper-bound.
  if (!(Type & 1) || LVal.Designator.Invalid || DetermineForCompleteObject) {
    // Type=3 wants a lower bound, so we can't fall back to this.
    if (Type == 3 && !DetermineForCompleteObject)
      return false;

    llvm::APInt APEndOffset;
    if (isBaseAnAllocSizeCall(LVal.getLValueBase()) &&
        getBytesReturnedByAllocSizeCall(Info.Ctx, LVal, APEndOffset))
      return convertUnsignedAPIntToCharUnits(APEndOffset, EndOffset);

    if (LVal.InvalidBase)
      return false;

    QualType BaseTy = getObjectType(LVal.getLValueBase());
    const bool Ret = CheckedhandleSizeof(BaseTy, EndOffset);
    addFlexibleArrayMemberInitSize(Info, BaseTy, LVal, EndOffset);
    return Ret;
  }

  // We want to evaluate the size of a subobject.
  const SubobjectDesignator &Designator = LVal.Designator;

  // The following is a moderately common idiom in C:
  //
  // struct Foo { int a; char c[1]; };
  // struct Foo *F = (struct Foo *)malloc(sizeof(struct Foo) + strlen(Bar));
  // strcpy(&F->c[0], Bar);
  //
  // In order to not break too much legacy code, we need to support it.
  if (isUserWritingOffTheEnd(Info.Ctx, LVal)) {
    // If we can resolve this to an alloc_size call, we can hand that back,
    // because we know for certain how many bytes there are to write to.
    llvm::APInt APEndOffset;
    if (isBaseAnAllocSizeCall(LVal.getLValueBase()) &&
        getBytesReturnedByAllocSizeCall(Info.Ctx, LVal, APEndOffset))
      return convertUnsignedAPIntToCharUnits(APEndOffset, EndOffset);

    // If we cannot determine the size of the initial allocation, then we can't
    // given an accurate upper-bound. However, we are still able to give
    // conservative lower-bounds for Type=3.
    if (Type == 1)
      return false;
  }

  CharUnits BytesPerElem;
  if (!CheckedhandleSizeof(Designator.MostDerivedType, BytesPerElem))
    return false;

  // According to the GCC documentation, we want the size of the subobject
  // denoted by the pointer. But that's not quite right -- what we actually
  // want is the size of the immediately-enclosing array, if there is one.
  int64_t ElemsRemaining;
  if (Designator.MostDerivedIsArrayElement &&
      Designator.Entries.size() == Designator.MostDerivedPathLength) {
    uint64_t ArraySize = Designator.getMostDerivedArraySize();
    uint64_t ArrayIndex = Designator.Entries.back().getAsArrayIndex();
    ElemsRemaining = ArraySize <= ArrayIndex ? 0 : ArraySize - ArrayIndex;
  } else {
    ElemsRemaining = Designator.isOnePastTheEnd() ? 0 : 1;
  }

  EndOffset = LVal.getLValueOffset() + BytesPerElem * ElemsRemaining;
  return true;
}
} // namespace

namespace {
bool tryEvaluateBuiltinObjectSize(const Expr *E, unsigned Type, EvalInfo &Info,
                                  uint64_t &Size) {
  LValue LVal;
  {
    // The operand of __builtin_object_size is never evaluated for side-effects.
    // If there are any, but we can determine the pointed-to object anyway, then
    // ignore the side-effects.
    SpeculativeEvaluationRAII SpeculativeEval(Info);
    IgnoreSideEffectsRAII Fold(Info);

    if (E->isLValue()) {
      // It's possible for us to be given lvalues if we're called via
      // Expr::tryEvaluateObjectSize.
      APValue RVal;
      if (!evaluateAsRValue(Info, E, RVal))
        return false;
      LVal.setFrom(Info.Ctx, RVal);
    } else if (!evaluatePointer(ignorePointerCastsAndParens(E), LVal, Info,
                                /*InvalidBaseOK=*/true))
      return false;
  }

  // If we point to before the start of the object, there are no accessible
  // bytes.
  if (LVal.getLValueOffset().isNegative()) {
    Size = 0;
    return true;
  }

  CharUnits EndOffset;
  if (!determineEndOffset(Info, E->getExprLoc(), Type, LVal, EndOffset))
    return false;

  // If we've fallen outside of the end offset, just pretend there's nothing to
  // write to/read from.
  if (EndOffset <= LVal.getLValueOffset())
    Size = 0;
  else
    Size = (EndOffset - LVal.getLValueOffset()).getQuantity();
  return true;
}
} // namespace

bool IntExprEvaluator::VisitCallExpr(const CallExpr *E) {
  if (!IsConstantevaluatedBuiltinCall(E))
    return ExprEvaluatorBaseTy::VisitCallExpr(E);
  return VisitBuiltinCallExpr(E, E->getBuiltinCallee());
}

namespace {
bool getBuiltinAlignArguments(const CallExpr *E, EvalInfo &Info, APValue &Val,
                              APSInt &Alignment) {
  QualType SrcTy = E->getArg(0)->getType();
  if (!getAlignmentArgument(E->getArg(1), SrcTy, Info, Alignment))
    return false;
  // Even though we are evaluating integer expressions we could get a pointer
  // argument for the __builtin_is_aligned() case.
  if (SrcTy->isPointerType()) {
    LValue Ptr;
    if (!evaluatePointer(E->getArg(0), Ptr, Info))
      return false;
    Ptr.moveInto(Val);
  } else if (!SrcTy->isIntegralOrEnumerationType()) {
    Info.FFDiag(E->getArg(0));
    return false;
  } else {
    APSInt SrcInt;
    if (!evaluateInteger(E->getArg(0), SrcInt, Info))
      return false;
    assert(SrcInt.getBitWidth() >= Alignment.getBitWidth() &&
           "Bit widths must be the same");
    Val = APValue(SrcInt);
  }
  assert(Val.hasValue());
  return true;
}
} // namespace

bool IntExprEvaluator::VisitBuiltinCallExpr(const CallExpr *E,
                                            unsigned BuiltinOp) {
  switch (BuiltinOp) {
  default:
    return false;

  case Builtin::BI__builtin_dynamic_object_size:
  case Builtin::BI__builtin_object_size: {
    // The type was checked when we built the expression.
    unsigned Type =
        E->getArg(1)->EvaluateKnownConstInt(Info.Ctx).getZExtValue();
    assert(Type <= 3 && "unexpected type");

    uint64_t Size;
    if (tryEvaluateBuiltinObjectSize(E->getArg(0), Type, Info, Size))
      return Success(Size, E);

    if (E->getArg(0)->HasSideEffects(Info.Ctx))
      return Success((Type & 2) ? 0 : -1, E);

    // Expression had no side effects, but we couldn't statically determine the
    // size of the referenced object.
    switch (Info.EvalMode) {
    case EvalInfo::EM_ConstantExpression:
    case EvalInfo::EM_ConstantFold:
    case EvalInfo::EM_IgnoreSideEffects:
      // Leave it to IR generation.
      return Error(E);
    case EvalInfo::EM_ConstantExpressionUnevaluated:
      // Reduce it to a constant now.
      return Success((Type & 2) ? 0 : -1, E);
    }

    llvm_unreachable("unexpected EvalMode");
  }

  case Builtin::BI__builtin_os_log_format_buffer_size: {
    analyze_os_log::OSLogBufferLayout Layout;
    analyze_os_log::computeOSLogBufferLayout(Info.Ctx, E, Layout);
    return Success(Layout.size().getQuantity(), E);
  }

  case Builtin::BI__builtin_is_aligned: {
    APValue Src;
    APSInt Alignment;
    if (!getBuiltinAlignArguments(E, Info, Src, Alignment))
      return false;
    if (Src.isLValue()) {
      // If we evaluated a pointer, check the minimum known alignment.
      LValue Ptr;
      Ptr.setFrom(Info.Ctx, Src);
      CharUnits BaseAlignment = getBaseAlignment(Info, Ptr);
      CharUnits PtrAlign = BaseAlignment.alignmentAtOffset(Ptr.Offset);
      // We can return true if the known alignment at the computed offset is
      // greater than the requested alignment.
      assert(PtrAlign.isPowerOfTwo());
      assert(Alignment.isPowerOf2());
      if (PtrAlign.getQuantity() >= Alignment)
        return Success(1, E);
      // If the alignment is not known to be sufficient, some cases could still
      // be aligned at run time. However, if the requested alignment is less or
      // equal to the base alignment and the offset is not aligned, we know that
      // the run-time value can never be aligned.
      if (BaseAlignment.getQuantity() >= Alignment &&
          PtrAlign.getQuantity() < Alignment)
        return Success(0, E);
      // Otherwise we can't infer whether the value is sufficiently aligned.
      Info.FFDiag(E->getArg(0), diag::note_constexpr_alignment_compute)
          << Alignment;
      return false;
    }
    assert(Src.isInt());
    return Success((Src.getInt() & (Alignment - 1)) == 0 ? 1 : 0, E);
  }
  case Builtin::BI__builtin_align_up: {
    APValue Src;
    APSInt Alignment;
    if (!getBuiltinAlignArguments(E, Info, Src, Alignment))
      return false;
    if (!Src.isInt())
      return Error(E);
    APSInt AlignedVal =
        APSInt((Src.getInt() + (Alignment - 1)) & ~(Alignment - 1),
               Src.getInt().isUnsigned());
    assert(AlignedVal.getBitWidth() == Src.getInt().getBitWidth());
    return Success(AlignedVal, E);
  }
  case Builtin::BI__builtin_align_down: {
    APValue Src;
    APSInt Alignment;
    if (!getBuiltinAlignArguments(E, Info, Src, Alignment))
      return false;
    if (!Src.isInt())
      return Error(E);
    APSInt AlignedVal =
        APSInt(Src.getInt() & ~(Alignment - 1), Src.getInt().isUnsigned());
    assert(AlignedVal.getBitWidth() == Src.getInt().getBitWidth());
    return Success(AlignedVal, E);
  }

  case Builtin::BI__builtin_bitreverse8:
  case Builtin::BI__builtin_bitreverse16:
  case Builtin::BI__builtin_bitreverse32:
  case Builtin::BI__builtin_bitreverse64: {
    APSInt Val;
    if (!evaluateInteger(E->getArg(0), Val, Info))
      return false;

    return Success(Val.reverseBits(), E);
  }

  case Builtin::BI__builtin_bswap16:
  case Builtin::BI__builtin_bswap32:
  case Builtin::BI__builtin_bswap64: {
    APSInt Val;
    if (!evaluateInteger(E->getArg(0), Val, Info))
      return false;

    return Success(Val.byteSwap(), E);
  }

  case Builtin::BI__builtin_classify_type:
    return Success((int)evaluateBuiltinClassifyType(E, Info.getLangOpts()), E);

  case Builtin::BI__builtin_clrsb:
  case Builtin::BI__builtin_clrsbl:
  case Builtin::BI__builtin_clrsbll: {
    APSInt Val;
    if (!evaluateInteger(E->getArg(0), Val, Info))
      return false;

    return Success(Val.getBitWidth() - Val.getSignificantBits(), E);
  }

  case Builtin::BI__builtin_clz:
  case Builtin::BI__builtin_clzl:
  case Builtin::BI__builtin_clzll:
  case Builtin::BI__builtin_clzs:
  case Builtin::BI__lzcnt16: // Microsoft variants of count leading-zeroes
  case Builtin::BI__lzcnt:
  case Builtin::BI__lzcnt64: {
    APSInt Val;
    if (!evaluateInteger(E->getArg(0), Val, Info))
      return false;

    // When the argument is 0, the result of GCC builtins is undefined, whereas
    // for Microsoft intrinsics, the result is the bit-width of the argument.
    bool ZeroIsUndefined = BuiltinOp != Builtin::BI__lzcnt16 &&
                           BuiltinOp != Builtin::BI__lzcnt &&
                           BuiltinOp != Builtin::BI__lzcnt64;

    if (ZeroIsUndefined && !Val)
      return Error(E);

    return Success(Val.countl_zero(), E);
  }

  case Builtin::BI__builtin_constant_p: {
    const Expr *Arg = E->getArg(0);
    if (evaluateBuiltinConstantP(Info, Arg))
      return Success(true, E);
    if (Info.InConstantContext || Arg->HasSideEffects(Info.Ctx)) {
      // Outside a constant context, eagerly evaluate to false in the presence
      // of side-effects in order to avoid -Wunsequenced false-positives in
      // a branch on __builtin_constant_p(expr).
      return Success(false, E);
    }
    Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
    return false;
  }

  case Builtin::BI__builtin_ctz:
  case Builtin::BI__builtin_ctzl:
  case Builtin::BI__builtin_ctzll:
  case Builtin::BI__builtin_ctzs: {
    APSInt Val;
    if (!evaluateInteger(E->getArg(0), Val, Info))
      return false;
    if (!Val)
      return Error(E);

    return Success(Val.countr_zero(), E);
  }

  case Builtin::BI__builtin_eh_return_data_regno: {
    int Operand = E->getArg(0)->EvaluateKnownConstInt(Info.Ctx).getZExtValue();
    Operand = Info.Ctx.getTargetInfo().getEHDataRegisterNumber(Operand);
    return Success(Operand, E);
  }

  case Builtin::BI__builtin_expect:
  case Builtin::BI__builtin_expect_with_probability:
    return Visit(E->getArg(0));

  case Builtin::BI__builtin_ffs:
  case Builtin::BI__builtin_ffsl:
  case Builtin::BI__builtin_ffsll: {
    APSInt Val;
    if (!evaluateInteger(E->getArg(0), Val, Info))
      return false;

    unsigned N = Val.countr_zero();
    return Success(N == Val.getBitWidth() ? 0 : N + 1, E);
  }

  case Builtin::BI__builtin_fpclassify: {
    APFloat Val(0.0);
    if (!evaluateFloat(E->getArg(5), Val, Info))
      return false;
    unsigned Arg;
    switch (Val.getCategory()) {
    case APFloat::fcNaN:
      Arg = 0;
      break;
    case APFloat::fcInfinity:
      Arg = 1;
      break;
    case APFloat::fcNormal:
      Arg = Val.isDenormal() ? 3 : 2;
      break;
    case APFloat::fcZero:
      Arg = 4;
      break;
    }
    return Visit(E->getArg(Arg));
  }

  case Builtin::BI__builtin_isinf_sign: {
    APFloat Val(0.0);
    return evaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isInfinity() ? (Val.isNegative() ? -1 : 1) : 0, E);
  }

  case Builtin::BI__builtin_isinf: {
    APFloat Val(0.0);
    return evaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isInfinity() ? 1 : 0, E);
  }

  case Builtin::BI__builtin_isfinite: {
    APFloat Val(0.0);
    return evaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isFinite() ? 1 : 0, E);
  }

  case Builtin::BI__builtin_isnan: {
    APFloat Val(0.0);
    return evaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isNaN() ? 1 : 0, E);
  }

  case Builtin::BI__builtin_isnormal: {
    APFloat Val(0.0);
    return evaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isNormal() ? 1 : 0, E);
  }

  case Builtin::BI__builtin_issubnormal: {
    APFloat Val(0.0);
    return evaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isDenormal() ? 1 : 0, E);
  }

  case Builtin::BI__builtin_iszero: {
    APFloat Val(0.0);
    return evaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isZero() ? 1 : 0, E);
  }

  case Builtin::BI__builtin_issignaling: {
    APFloat Val(0.0);
    return evaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isSignaling() ? 1 : 0, E);
  }

  case Builtin::BI__builtin_isfpclass: {
    APSInt MaskVal;
    if (!evaluateInteger(E->getArg(1), MaskVal, Info))
      return false;
    unsigned Test = static_cast<llvm::FPClassTest>(MaskVal.getZExtValue());
    APFloat Val(0.0);
    return evaluateFloat(E->getArg(0), Val, Info) &&
           Success((Val.classify() & Test) ? 1 : 0, E);
  }

  case Builtin::BI__builtin_parity:
  case Builtin::BI__builtin_parityl:
  case Builtin::BI__builtin_parityll: {
    APSInt Val;
    if (!evaluateInteger(E->getArg(0), Val, Info))
      return false;

    return Success(Val.popcount() % 2, E);
  }

  case Builtin::BI__builtin_popcount:
  case Builtin::BI__builtin_popcountl:
  case Builtin::BI__builtin_popcountll:
  case Builtin::BI__popcnt16: // Microsoft variants of popcount
  case Builtin::BI__popcnt:
  case Builtin::BI__popcnt64: {
    APSInt Val;
    if (!evaluateInteger(E->getArg(0), Val, Info))
      return false;

    return Success(Val.popcount(), E);
  }

  case Builtin::BI__builtin_rotateleft8:
  case Builtin::BI__builtin_rotateleft16:
  case Builtin::BI__builtin_rotateleft32:
  case Builtin::BI__builtin_rotateleft64:
  case Builtin::BI_rotl8: // Microsoft variants of rotate right
  case Builtin::BI_rotl16:
  case Builtin::BI_rotl:
  case Builtin::BI_lrotl:
  case Builtin::BI_rotl64: {
    APSInt Val, Amt;
    if (!evaluateInteger(E->getArg(0), Val, Info) ||
        !evaluateInteger(E->getArg(1), Amt, Info))
      return false;

    return Success(Val.rotl(Amt.urem(Val.getBitWidth())), E);
  }

  case Builtin::BI__builtin_rotateright8:
  case Builtin::BI__builtin_rotateright16:
  case Builtin::BI__builtin_rotateright32:
  case Builtin::BI__builtin_rotateright64:
  case Builtin::BI_rotr8: // Microsoft variants of rotate right
  case Builtin::BI_rotr16:
  case Builtin::BI_rotr:
  case Builtin::BI_lrotr:
  case Builtin::BI_rotr64: {
    APSInt Val, Amt;
    if (!evaluateInteger(E->getArg(0), Val, Info) ||
        !evaluateInteger(E->getArg(1), Amt, Info))
      return false;

    return Success(Val.rotr(Amt.urem(Val.getBitWidth())), E);
  }

  case Builtin::BIstrlen:
  case Builtin::BIwcslen:
    Info.CCEDiag(E, diag::note_invalid_subexpr_in_const_expr);
    [[fallthrough]];
  case Builtin::BI__builtin_strlen:
  case Builtin::BI__builtin_wcslen: {
    // As an extension, we support __builtin_strlen() as a constant expression,
    // and support folding strlen() to a constant.
    uint64_t StrLen;
    if (evaluateBuiltinStrLen(E->getArg(0), StrLen, Info))
      return Success(StrLen, E);
    return false;
  }

  case Builtin::BIstrcmp:
  case Builtin::BIwcscmp:
  case Builtin::BIstrncmp:
  case Builtin::BIwcsncmp:
  case Builtin::BImemcmp:
  case Builtin::BIbcmp:
  case Builtin::BIwmemcmp:
    Info.CCEDiag(E, diag::note_invalid_subexpr_in_const_expr);
    [[fallthrough]];
  case Builtin::BI__builtin_strcmp:
  case Builtin::BI__builtin_wcscmp:
  case Builtin::BI__builtin_strncmp:
  case Builtin::BI__builtin_wcsncmp:
  case Builtin::BI__builtin_memcmp:
  case Builtin::BI__builtin_bcmp:
  case Builtin::BI__builtin_wmemcmp: {
    LValue String1, String2;
    if (!evaluatePointer(E->getArg(0), String1, Info) ||
        !evaluatePointer(E->getArg(1), String2, Info))
      return false;

    uint64_t MaxLength = uint64_t(-1);
    if (BuiltinOp != Builtin::BIstrcmp && BuiltinOp != Builtin::BIwcscmp &&
        BuiltinOp != Builtin::BI__builtin_strcmp &&
        BuiltinOp != Builtin::BI__builtin_wcscmp) {
      APSInt N;
      if (!evaluateInteger(E->getArg(2), N, Info))
        return false;
      MaxLength = N.getZExtValue();
    }

    // Empty substrings compare equal by definition.
    if (MaxLength == 0u)
      return Success(0, E);

    if (!String1.checkNullPointerForFoldAccess(Info, E, AK_Read) ||
        !String2.checkNullPointerForFoldAccess(Info, E, AK_Read) ||
        String1.Designator.Invalid || String2.Designator.Invalid)
      return false;

    QualType CharTy1 = String1.Designator.getType(Info.Ctx);
    QualType CharTy2 = String2.Designator.getType(Info.Ctx);

    bool IsRawByte = BuiltinOp == Builtin::BImemcmp ||
                     BuiltinOp == Builtin::BIbcmp ||
                     BuiltinOp == Builtin::BI__builtin_memcmp ||
                     BuiltinOp == Builtin::BI__builtin_bcmp;

    assert(IsRawByte ||
           (Info.Ctx.hasSameUnqualifiedType(
                CharTy1, E->getArg(0)->getType()->getPointeeType()) &&
            Info.Ctx.hasSameUnqualifiedType(CharTy1, CharTy2)));

    // For memcmp, allow comparing any arrays of '[[un]signed] char' or
    // 'char8_t', but no other types.
    if (IsRawByte &&
        !(isOneByteCharacterType(CharTy1) && isOneByteCharacterType(CharTy2))) {
      Info.FFDiag(E, diag::note_constexpr_memcmp_unsupported)
          << ("'" + Info.Ctx.BuiltinInfo.getName(BuiltinOp) + "'").str()
          << CharTy1 << CharTy2;
      return false;
    }

    const auto &ReadCurElems = [&](APValue &Char1, APValue &Char2) {
      return handleLValueToRValueConversion(Info, E, CharTy1, String1, Char1) &&
             handleLValueToRValueConversion(Info, E, CharTy2, String2, Char2) &&
             Char1.isInt() && Char2.isInt();
    };
    const auto &AdvanceElems = [&] {
      return handleLValueArrayAdjustment(Info, E, String1, CharTy1, 1) &&
             handleLValueArrayAdjustment(Info, E, String2, CharTy2, 1);
    };

    bool StopAtNull =
        (BuiltinOp != Builtin::BImemcmp && BuiltinOp != Builtin::BIbcmp &&
         BuiltinOp != Builtin::BIwmemcmp &&
         BuiltinOp != Builtin::BI__builtin_memcmp &&
         BuiltinOp != Builtin::BI__builtin_bcmp &&
         BuiltinOp != Builtin::BI__builtin_wmemcmp);
    bool IsWide = BuiltinOp == Builtin::BIwcscmp ||
                  BuiltinOp == Builtin::BIwcsncmp ||
                  BuiltinOp == Builtin::BIwmemcmp ||
                  BuiltinOp == Builtin::BI__builtin_wcscmp ||
                  BuiltinOp == Builtin::BI__builtin_wcsncmp ||
                  BuiltinOp == Builtin::BI__builtin_wmemcmp;

    for (; MaxLength; --MaxLength) {
      APValue Char1, Char2;
      if (!ReadCurElems(Char1, Char2))
        return false;
      if (Char1.getInt().ne(Char2.getInt())) {
        if (IsWide) // wmemcmp compares with wchar_t signedness.
          return Success(Char1.getInt() < Char2.getInt() ? -1 : 1, E);
        // memcmp always compares unsigned chars.
        return Success(Char1.getInt().ult(Char2.getInt()) ? -1 : 1, E);
      }
      if (StopAtNull && !Char1.getInt())
        return Success(0, E);
      assert(!(StopAtNull && !Char2.getInt()));
      if (!AdvanceElems())
        return false;
    }
    // We hit the strncmp / memcmp limit.
    return Success(0, E);
  }

  case Builtin::BI__atomic_always_lock_free:
  case Builtin::BI__atomic_is_lock_free:
  case Builtin::BI__c11_atomic_is_lock_free: {
    APSInt SizeVal;
    if (!evaluateInteger(E->getArg(0), SizeVal, Info))
      return false;

    // For __atomic_is_lock_free(sizeof(_Atomic(T))), if the size is a power
    // of two less than or equal to the maximum inline atomic width, we know it
    // is lock-free.  If the size isn't a power of two, or greater than the
    // maximum alignment where we promote atomics, we know it is not lock-free
    // (at least not in the sense of atomic_is_lock_free).  Otherwise,
    // the answer can only be determined at runtime; for example, 16-byte
    // atomics have lock-free implementations on some, but not all,
    // x86-64 processors.

    // Check power-of-two.
    CharUnits Size = CharUnits::fromQuantity(SizeVal.getZExtValue());
    if (Size.isPowerOfTwo()) {
      // Check against inlining width.
      unsigned InlineWidthBits =
          Info.Ctx.getTargetInfo().getMaxAtomicInlineWidth();
      if (Size <= Info.Ctx.toCharUnitsFromBits(InlineWidthBits)) {
        if (BuiltinOp == Builtin::BI__c11_atomic_is_lock_free ||
            Size == CharUnits::One() ||
            E->getArg(1)->isNullPointerConstant(Info.Ctx,
                                                Expr::NPC_NeverValueDependent))
          // OK, we will inline appropriately-aligned operations of this size,
          // and _Atomic(T) is appropriately-aligned.
          return Success(1, E);

        QualType PointeeType = E->getArg(1)
                                   ->IgnoreImpCasts()
                                   ->getType()
                                   ->castAs<PointerType>()
                                   ->getPointeeType();
        if (!PointeeType->isIncompleteType() &&
            Info.Ctx.getTypeAlignInChars(PointeeType) >= Size) {
          // OK, we will inline operations on this object.
          return Success(1, E);
        }
      }
    }

    return BuiltinOp == Builtin::BI__atomic_always_lock_free ? Success(0, E)
                                                             : Error(E);
  }
  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_mul_overflow:
  case Builtin::BI__builtin_sadd_overflow:
  case Builtin::BI__builtin_uadd_overflow:
  case Builtin::BI__builtin_uaddl_overflow:
  case Builtin::BI__builtin_uaddll_overflow:
  case Builtin::BI__builtin_usub_overflow:
  case Builtin::BI__builtin_usubl_overflow:
  case Builtin::BI__builtin_usubll_overflow:
  case Builtin::BI__builtin_umul_overflow:
  case Builtin::BI__builtin_umull_overflow:
  case Builtin::BI__builtin_umulll_overflow:
  case Builtin::BI__builtin_saddl_overflow:
  case Builtin::BI__builtin_saddll_overflow:
  case Builtin::BI__builtin_ssub_overflow:
  case Builtin::BI__builtin_ssubl_overflow:
  case Builtin::BI__builtin_ssubll_overflow:
  case Builtin::BI__builtin_smul_overflow:
  case Builtin::BI__builtin_smull_overflow:
  case Builtin::BI__builtin_smulll_overflow: {
    LValue ResultLValue;
    APSInt LHS, RHS;

    QualType ResultType = E->getArg(2)->getType()->getPointeeType();
    if (!evaluateInteger(E->getArg(0), LHS, Info) ||
        !evaluateInteger(E->getArg(1), RHS, Info) ||
        !evaluatePointer(E->getArg(2), ResultLValue, Info))
      return false;

    APSInt Result;
    bool DidOverflow = false;

    // If the types don't have to match, enlarge all 3 to the largest of them.
    if (BuiltinOp == Builtin::BI__builtin_add_overflow ||
        BuiltinOp == Builtin::BI__builtin_sub_overflow ||
        BuiltinOp == Builtin::BI__builtin_mul_overflow) {
      bool IsSigned = LHS.isSigned() || RHS.isSigned() ||
                      ResultType->isSignedIntegerOrEnumerationType();
      bool AllSigned = LHS.isSigned() && RHS.isSigned() &&
                       ResultType->isSignedIntegerOrEnumerationType();
      uint64_t LHSSize = LHS.getBitWidth();
      uint64_t RHSSize = RHS.getBitWidth();
      uint64_t ResultSize = Info.Ctx.getTypeSize(ResultType);
      uint64_t MaxBits = std::max(std::max(LHSSize, RHSSize), ResultSize);

      // Add an additional bit if the signedness isn't uniformly agreed to. We
      // could do this ONLY if there is a signed and an unsigned that both have
      // MaxBits, but the code to check that is pretty nasty.  The issue will be
      // caught in the shrink-to-result later anyway.
      if (IsSigned && !AllSigned)
        ++MaxBits;

      LHS = APSInt(LHS.extOrTrunc(MaxBits), !IsSigned);
      RHS = APSInt(RHS.extOrTrunc(MaxBits), !IsSigned);
      Result = APSInt(MaxBits, !IsSigned);
    }

    // Find largest int.
    switch (BuiltinOp) {
    default:
      llvm_unreachable("Invalid value for BuiltinOp");
    case Builtin::BI__builtin_add_overflow:
    case Builtin::BI__builtin_sadd_overflow:
    case Builtin::BI__builtin_saddl_overflow:
    case Builtin::BI__builtin_saddll_overflow:
    case Builtin::BI__builtin_uadd_overflow:
    case Builtin::BI__builtin_uaddl_overflow:
    case Builtin::BI__builtin_uaddll_overflow:
      Result = LHS.isSigned() ? LHS.sadd_ov(RHS, DidOverflow)
                              : LHS.uadd_ov(RHS, DidOverflow);
      break;
    case Builtin::BI__builtin_sub_overflow:
    case Builtin::BI__builtin_ssub_overflow:
    case Builtin::BI__builtin_ssubl_overflow:
    case Builtin::BI__builtin_ssubll_overflow:
    case Builtin::BI__builtin_usub_overflow:
    case Builtin::BI__builtin_usubl_overflow:
    case Builtin::BI__builtin_usubll_overflow:
      Result = LHS.isSigned() ? LHS.ssub_ov(RHS, DidOverflow)
                              : LHS.usub_ov(RHS, DidOverflow);
      break;
    case Builtin::BI__builtin_mul_overflow:
    case Builtin::BI__builtin_smul_overflow:
    case Builtin::BI__builtin_smull_overflow:
    case Builtin::BI__builtin_smulll_overflow:
    case Builtin::BI__builtin_umul_overflow:
    case Builtin::BI__builtin_umull_overflow:
    case Builtin::BI__builtin_umulll_overflow:
      Result = LHS.isSigned() ? LHS.smul_ov(RHS, DidOverflow)
                              : LHS.umul_ov(RHS, DidOverflow);
      break;
    }

    // In the case where multiple sizes are allowed, truncate and see if
    // the values are the same.
    if (BuiltinOp == Builtin::BI__builtin_add_overflow ||
        BuiltinOp == Builtin::BI__builtin_sub_overflow ||
        BuiltinOp == Builtin::BI__builtin_mul_overflow) {
      // APSInt doesn't have a TruncOrSelf, so we use extOrTrunc instead,
      // since it will give us the behavior of a TruncOrSelf in the case where
      // its parameter <= its size.  We previously set Result to be at least the
      // type-size of the result, so getTypeSize(ResultType) <= Result.BitWidth
      // will work exactly like TruncOrSelf.
      APSInt Temp = Result.extOrTrunc(Info.Ctx.getTypeSize(ResultType));
      Temp.setIsSigned(ResultType->isSignedIntegerOrEnumerationType());

      if (!APSInt::isSameValue(Temp, Result))
        DidOverflow = true;
      Result = Temp;
    }

    APValue APV{Result};
    if (!handleAssignment(Info, E, ResultLValue, ResultType, APV))
      return false;
    return Success(DidOverflow, E);
  }
  }
}

namespace {
bool isOnePastTheEndOfCompleteObject(const TreeContext &Ctx, const LValue &LV) {
  // A null pointer can be viewed as being "past the end" but we don't
  // choose to look at it that way here.
  if (!LV.getLValueBase())
    return false;

  // If the designator is valid and refers to a subobject, we're not pointing
  // past the end.
  if (!LV.getLValueDesignator().Invalid &&
      !LV.getLValueDesignator().isOnePastTheEnd())
    return false;

  // A pointer to an incomplete type might be past-the-end if the type's size is
  // zero.  We cannot tell because the type is incomplete.
  QualType Ty = getType(LV.getLValueBase());
  if (Ty->isIncompleteType())
    return true;

  // We're a past-the-end pointer if we point to the byte after the object,
  // no matter what our type or path is.
  auto Size = Ctx.getTypeSizeInChars(Ty);
  return LV.getLValueOffset() == Size;
}
} // namespace

namespace {

class DataRecursiveIntBinOpEvaluator {
  struct EvalResult {
    APValue Val;
    bool Failed = false;

    EvalResult() = default;

    void swap(EvalResult &RHS) {
      Val.swap(RHS.Val);
      Failed = RHS.Failed;
      RHS.Failed = false;
    }
  };

  struct Job {
    const Expr *E;
    EvalResult LHSResult; // meaningful only for binary operator expression.
    enum { AnyExprKind, BinOpKind, BinOpVisitedLHSKind } Kind;

    Job() = default;
    Job(Job &&) = default;

    void startSpeculativeEval(EvalInfo &Info) {
      SpecEvalRAII = SpeculativeEvaluationRAII(Info);
    }

  private:
    SpeculativeEvaluationRAII SpecEvalRAII;
  };

  llvm::SmallVector<Job, 16> Queue;

  IntExprEvaluator &IntEval;
  EvalInfo &Info;
  APValue &FinalResult;

public:
  DataRecursiveIntBinOpEvaluator(IntExprEvaluator &IntEval, APValue &Result)
      : IntEval(IntEval), Info(IntEval.getEvalInfo()), FinalResult(Result) {}

  static bool shouldEnqueue(const BinaryOperator *E) {
    return E->getOpcode() == BO_Comma || E->isLogicalOp() ||
           (E->isPRValue() && E->getType()->isIntegralOrEnumerationType() &&
            E->getLHS()->getType()->isIntegralOrEnumerationType() &&
            E->getRHS()->getType()->isIntegralOrEnumerationType());
  }

  bool Traverse(const BinaryOperator *E) {
    enqueue(E);
    EvalResult PrevResult;
    while (!Queue.empty())
      process(PrevResult);

    if (PrevResult.Failed)
      return false;

    FinalResult.swap(PrevResult.Val);
    return true;
  }

private:
  bool Success(uint64_t Value, const Expr *E, APValue &Result) {
    return IntEval.Success(Value, E, Result);
  }
  bool Success(const APSInt &Value, const Expr *E, APValue &Result) {
    return IntEval.Success(Value, E, Result);
  }
  bool Error(const Expr *E) { return IntEval.Error(E); }
  bool Error(const Expr *E, diag::kind D) { return IntEval.Error(E, D); }

  OptionalDiagnostic CCEDiag(const Expr *E, diag::kind D) {
    return Info.CCEDiag(E, D);
  }

  // Returns true if visiting the RHS is necessary, false otherwise.
  bool VisitBinOpLHSOnly(EvalResult &LHSResult, const BinaryOperator *E,
                         bool &SuppressRHSDiags);

  bool VisitBinOp(const EvalResult &LHSResult, const EvalResult &RHSResult,
                  const BinaryOperator *E, APValue &Result);

  void evaluateExpr(const Expr *E, EvalResult &Result) {
    Result.Failed = !evaluate(Result.Val, Info, E);
    if (Result.Failed)
      Result.Val = APValue();
  }

  void process(EvalResult &Result);

  void enqueue(const Expr *E) {
    E = E->IgnoreParens();
    Queue.resize(Queue.size() + 1);
    Queue.back().E = E;
    Queue.back().Kind = Job::AnyExprKind;
  }
};

} // namespace

bool DataRecursiveIntBinOpEvaluator::VisitBinOpLHSOnly(EvalResult &LHSResult,
                                                       const BinaryOperator *E,
                                                       bool &SuppressRHSDiags) {
  if (E->getOpcode() == BO_Comma) {
    // Ignore LHS but note if we could not evaluate it.
    if (LHSResult.Failed)
      return Info.noteSideEffect();
    return true;
  }

  if (E->isLogicalOp()) {
    bool LHSAsBool;
    if (!LHSResult.Failed && handleConversionToBool(LHSResult.Val, LHSAsBool)) {
      // We were able to evaluate the LHS, see if we can get away with not
      // evaluating the RHS: 0 && X -> 0, 1 || X -> 1
      if (LHSAsBool == (E->getOpcode() == BO_LOr)) {
        Success(LHSAsBool, E, LHSResult.Val);
        return false; // Ignore RHS
      }
    } else {
      LHSResult.Failed = true;

      // Since we weren't able to evaluate the left hand side, it
      // might have had side effects.
      if (!Info.noteSideEffect())
        return false;

      // We can't evaluate the LHS; however, sometimes the result
      // is determined by the RHS: X && 0 -> 0, X || 1 -> 1.
      // Don't ignore RHS and suppress diagnostics from this arm.
      SuppressRHSDiags = true;
    }

    return true;
  }

  assert(E->getLHS()->getType()->isIntegralOrEnumerationType() &&
         E->getRHS()->getType()->isIntegralOrEnumerationType());

  if (LHSResult.Failed && !Info.noteFailure())
    return false; // Ignore RHS;

  return true;
}

namespace {
void addOrSubLValueAsInteger(APValue &LVal, const APSInt &Index, bool IsSub) {
  // Compute the new offset in the appropriate width, wrapping at 64 bits.
  assert(!LVal.hasLValuePath() && "have designator for integer lvalue");
  CharUnits &Offset = LVal.getLValueOffset();
  uint64_t Offset64 = Offset.getQuantity();
  uint64_t Index64 = Index.extOrTrunc(64).getZExtValue();
  Offset =
      CharUnits::fromQuantity(IsSub ? Offset64 - Index64 : Offset64 + Index64);
}
} // namespace

bool DataRecursiveIntBinOpEvaluator::VisitBinOp(const EvalResult &LHSResult,
                                                const EvalResult &RHSResult,
                                                const BinaryOperator *E,
                                                APValue &Result) {
  if (E->getOpcode() == BO_Comma) {
    if (RHSResult.Failed)
      return false;
    Result = RHSResult.Val;
    return true;
  }

  if (E->isLogicalOp()) {
    bool lhsResult, rhsResult;
    bool LHSIsOK = handleConversionToBool(LHSResult.Val, lhsResult);
    bool RHSIsOK = handleConversionToBool(RHSResult.Val, rhsResult);

    if (LHSIsOK) {
      if (RHSIsOK) {
        if (E->getOpcode() == BO_LOr)
          return Success(lhsResult || rhsResult, E, Result);
        else
          return Success(lhsResult && rhsResult, E, Result);
      }
    } else {
      if (RHSIsOK) {
        // We can't evaluate the LHS; however, sometimes the result
        // is determined by the RHS: X && 0 -> 0, X || 1 -> 1.
        if (rhsResult == (E->getOpcode() == BO_LOr))
          return Success(rhsResult, E, Result);
      }
    }

    return false;
  }

  assert(E->getLHS()->getType()->isIntegralOrEnumerationType() &&
         E->getRHS()->getType()->isIntegralOrEnumerationType());

  if (LHSResult.Failed || RHSResult.Failed)
    return false;

  const APValue &LHSVal = LHSResult.Val;
  const APValue &RHSVal = RHSResult.Val;

  // Handle cases like (unsigned long)&a + 4.
  if (E->isAdditiveOp() && LHSVal.isLValue() && RHSVal.isInt()) {
    Result = LHSVal;
    addOrSubLValueAsInteger(Result, RHSVal.getInt(), E->getOpcode() == BO_Sub);
    return true;
  }

  // Handle cases like 4 + (unsigned long)&a
  if (E->getOpcode() == BO_Add && RHSVal.isLValue() && LHSVal.isInt()) {
    Result = RHSVal;
    addOrSubLValueAsInteger(Result, LHSVal.getInt(), /*IsSub*/ false);
    return true;
  }

  if (E->getOpcode() == BO_Sub && LHSVal.isLValue() && RHSVal.isLValue()) {
    // Handle (intptr_t)&&A - (intptr_t)&&B.
    if (!LHSVal.getLValueOffset().isZero() ||
        !RHSVal.getLValueOffset().isZero())
      return false;
    const Expr *LHSExpr = LHSVal.getLValueBase().dyn_cast<const Expr *>();
    const Expr *RHSExpr = RHSVal.getLValueBase().dyn_cast<const Expr *>();
    if (!LHSExpr || !RHSExpr)
      return false;
    const AddrLabelExpr *LHSAddrExpr = dyn_cast<AddrLabelExpr>(LHSExpr);
    const AddrLabelExpr *RHSAddrExpr = dyn_cast<AddrLabelExpr>(RHSExpr);
    if (!LHSAddrExpr || !RHSAddrExpr)
      return false;
    // Make sure both labels come from the same function.
    if (LHSAddrExpr->getLabel()->getDeclContext() !=
        RHSAddrExpr->getLabel()->getDeclContext())
      return false;
    Result = APValue(LHSAddrExpr, RHSAddrExpr);
    return true;
  }

  // All the remaining cases expect both operands to be an integer
  if (!LHSVal.isInt() || !RHSVal.isInt())
    return Error(E);

  // Set up the width and signedness manually, in case it can't be deduced
  // from the operation we're performing.
  APSInt Value(Info.Ctx.getIntWidth(E->getType()),
               E->getType()->isUnsignedIntegerOrEnumerationType());
  if (!handleIntIntBinOp(Info, E, LHSVal.getInt(), E->getOpcode(),
                         RHSVal.getInt(), Value))
    return false;
  return Success(Value, E, Result);
}

void DataRecursiveIntBinOpEvaluator::process(EvalResult &Result) {
  Job &job = Queue.back();

  switch (job.Kind) {
  case Job::AnyExprKind: {
    if (const BinaryOperator *Bop = dyn_cast<BinaryOperator>(job.E)) {
      if (shouldEnqueue(Bop)) {
        job.Kind = Job::BinOpKind;
        enqueue(Bop->getLHS());
        return;
      }
    }

    evaluateExpr(job.E, Result);
    Queue.pop_back();
    return;
  }

  case Job::BinOpKind: {
    const BinaryOperator *Bop = cast<BinaryOperator>(job.E);
    bool SuppressRHSDiags = false;
    if (!VisitBinOpLHSOnly(Result, Bop, SuppressRHSDiags)) {
      Queue.pop_back();
      return;
    }
    if (SuppressRHSDiags)
      job.startSpeculativeEval(Info);
    job.LHSResult.swap(Result);
    job.Kind = Job::BinOpVisitedLHSKind;
    enqueue(Bop->getRHS());
    return;
  }

  case Job::BinOpVisitedLHSKind: {
    const BinaryOperator *Bop = cast<BinaryOperator>(job.E);
    EvalResult RHS;
    RHS.swap(Result);
    Result.Failed = !VisitBinOp(job.LHSResult, RHS, Bop, Result.Val);
    Queue.pop_back();
    return;
  }
  }

  llvm_unreachable("Invalid Job::Kind!");
}

namespace {
enum class CmpResult {
  Unequal,
  Less,
  Equal,
  Greater,
  Unordered,
};
}

namespace {
template <class SuccessCB, class AfterCB>
bool evaluateComparisonBinaryOperator(EvalInfo &Info, const BinaryOperator *E,
                                      SuccessCB &&Success, AfterCB &&DoAfter) {

  assert(E->isComparisonOp() && "expected comparison operator");
  assert(E->getType()->isIntegralOrEnumerationType() &&
         "unsupported binary expression evaluation");
  auto Error = [&](const Expr *E) {
    Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
    return false;
  };

  bool IsRelational = E->isRelationalOp();
  bool IsEquality = E->isEqualityOp();

  QualType LHSTy = E->getLHS()->getType();
  QualType RHSTy = E->getRHS()->getType();

  if (LHSTy->isIntegralOrEnumerationType() &&
      RHSTy->isIntegralOrEnumerationType()) {
    APSInt LHS, RHS;
    bool LHSOK = evaluateInteger(E->getLHS(), LHS, Info);
    if (!LHSOK && !Info.noteFailure())
      return false;
    if (!evaluateInteger(E->getRHS(), RHS, Info) || !LHSOK)
      return false;
    if (LHS < RHS)
      return Success(CmpResult::Less, E);
    if (LHS > RHS)
      return Success(CmpResult::Greater, E);
    return Success(CmpResult::Equal, E);
  }

  if (LHSTy->isFixedPointType() || RHSTy->isFixedPointType()) {
    APFixedPoint LHSFX(Info.Ctx.getFixedPointSemantics(LHSTy));
    APFixedPoint RHSFX(Info.Ctx.getFixedPointSemantics(RHSTy));

    bool LHSOK = evaluateFixedPointOrInteger(E->getLHS(), LHSFX, Info);
    if (!LHSOK && !Info.noteFailure())
      return false;
    if (!evaluateFixedPointOrInteger(E->getRHS(), RHSFX, Info) || !LHSOK)
      return false;
    if (LHSFX < RHSFX)
      return Success(CmpResult::Less, E);
    if (LHSFX > RHSFX)
      return Success(CmpResult::Greater, E);
    return Success(CmpResult::Equal, E);
  }

  if (LHSTy->isAnyComplexType() || RHSTy->isAnyComplexType()) {
    ComplexValue LHS, RHS;
    bool LHSOK;
    if (E->isAssignmentOp()) {
      LValue LV;
      evaluateLValue(E->getLHS(), LV, Info);
      LHSOK = false;
    } else if (LHSTy->isRealFloatingType()) {
      LHSOK = evaluateFloat(E->getLHS(), LHS.FloatReal, Info);
      if (LHSOK) {
        LHS.makeComplexFloat();
        LHS.FloatImag = APFloat::getZero(LHS.FloatReal.getSemantics(),
                                         /*negative=*/false);
      }
    } else {
      LHSOK = evaluateComplex(E->getLHS(), LHS, Info);
    }
    if (!LHSOK && !Info.noteFailure())
      return false;

    if (E->getRHS()->getType()->isRealFloatingType()) {
      if (!evaluateFloat(E->getRHS(), RHS.FloatReal, Info) || !LHSOK)
        return false;
      RHS.makeComplexFloat();
      RHS.FloatImag = APFloat::getZero(RHS.FloatReal.getSemantics(),
                                       /*negative=*/false);
    } else if (!evaluateComplex(E->getRHS(), RHS, Info) || !LHSOK)
      return false;

    if (LHS.isComplexFloat()) {
      APFloat::cmpResult CR_r =
          LHS.getComplexFloatReal().compare(RHS.getComplexFloatReal());
      APFloat::cmpResult CR_i =
          LHS.getComplexFloatImag().compare(RHS.getComplexFloatImag());
      bool IsEqual = CR_r == APFloat::cmpEqual && CR_i == APFloat::cmpEqual;
      return Success(IsEqual ? CmpResult::Equal : CmpResult::Unequal, E);
    } else {
      assert(IsEquality && "invalid complex comparison");
      bool IsEqual = LHS.getComplexIntReal() == RHS.getComplexIntReal() &&
                     LHS.getComplexIntImag() == RHS.getComplexIntImag();
      return Success(IsEqual ? CmpResult::Equal : CmpResult::Unequal, E);
    }
  }

  if (LHSTy->isRealFloatingType() && RHSTy->isRealFloatingType()) {
    APFloat RHS(0.0), LHS(0.0);

    bool LHSOK = evaluateFloat(E->getRHS(), RHS, Info);
    if (!LHSOK && !Info.noteFailure())
      return false;

    if (!evaluateFloat(E->getLHS(), LHS, Info) || !LHSOK)
      return false;

    assert(E->isComparisonOp() && "Invalid binary operator!");
    llvm::APFloatBase::cmpResult APFloatCmpResult = LHS.compare(RHS);
    if (!Info.InConstantContext && APFloatCmpResult == APFloat::cmpUnordered &&
        E->getFPFeaturesInEffect(Info.Ctx.getLangOpts()).isFPConstrained()) {
      // Note: Compares may raise invalid in some cases involving NaN or sNaN.
      Info.FFDiag(E, diag::note_constexpr_float_arithmetic_strict);
      return false;
    }
    auto GetCmpRes = [&]() {
      switch (APFloatCmpResult) {
      case APFloat::cmpEqual:
        return CmpResult::Equal;
      case APFloat::cmpLessThan:
        return CmpResult::Less;
      case APFloat::cmpGreaterThan:
        return CmpResult::Greater;
      case APFloat::cmpUnordered:
        return CmpResult::Unordered;
      }
      llvm_unreachable("Unrecognised APFloat::cmpResult enum");
    };
    return Success(GetCmpRes(), E);
  }

  if (LHSTy->isPointerType() && RHSTy->isPointerType()) {
    LValue LHSValue, RHSValue;

    bool LHSOK = evaluatePointer(E->getLHS(), LHSValue, Info);
    if (!LHSOK && !Info.noteFailure())
      return false;

    if (!evaluatePointer(E->getRHS(), RHSValue, Info) || !LHSOK)
      return false;

    // Reject differing bases from the normal codepath; we special-case
    // comparisons to null.
    if (!hasSameBase(LHSValue, RHSValue)) {
      auto DiagComparison = [&](unsigned DiagID, bool Reversed = false) {
        std::string LHS = LHSValue.toString(Info.Ctx, E->getLHS()->getType());
        std::string RHS = RHSValue.toString(Info.Ctx, E->getRHS()->getType());
        Info.FFDiag(E, DiagID)
            << (Reversed ? RHS : LHS) << (Reversed ? LHS : RHS);
        return false;
      };
      // Inequalities and subtractions between unrelated pointers have
      // unspecified or undefined behavior.
      if (!IsEquality)
        return DiagComparison(
            diag::note_constexpr_pointer_comparison_unspecified);
      // A constant address may compare equal to the address of a symbol.
      // The one exception is that address of an object cannot compare equal
      // to a null pointer constant.
      if ((!LHSValue.Base && !LHSValue.Offset.isZero()) ||
          (!RHSValue.Base && !RHSValue.Offset.isZero()))
        return DiagComparison(diag::note_constexpr_pointer_constant_comparison,
                              !RHSValue.Base);
      // It's implementation-defined whether distinct literals will have
      // distinct addresses. In NeverC, the result of such a comparison is
      // unspecified, so it is not a constant expression. However, we do know
      // that the address of a literal will be non-null.
      if ((isLiteralLValue(LHSValue) || isLiteralLValue(RHSValue)) &&
          LHSValue.Base && RHSValue.Base)
        return DiagComparison(diag::note_constexpr_literal_comparison);
      // We can't tell whether weak symbols will end up pointing to the same
      // object.
      if (isWeakLValue(LHSValue) || isWeakLValue(RHSValue))
        return DiagComparison(diag::note_constexpr_pointer_weak_comparison,
                              !isWeakLValue(LHSValue));
      // Reject start-of-object vs one-past-end-of-other-object comparisons.
      if (LHSValue.Base && LHSValue.Offset.isZero() &&
          isOnePastTheEndOfCompleteObject(Info.Ctx, RHSValue))
        return DiagComparison(diag::note_constexpr_pointer_comparison_past_end,
                              true);
      if (RHSValue.Base && RHSValue.Offset.isZero() &&
          isOnePastTheEndOfCompleteObject(Info.Ctx, LHSValue))
        return DiagComparison(diag::note_constexpr_pointer_comparison_past_end,
                              false);
      // We can't tell whether an object is at the same address as another
      // zero sized object.
      if ((RHSValue.Base && isZeroSized(LHSValue)) ||
          (LHSValue.Base && isZeroSized(RHSValue)))
        return DiagComparison(
            diag::note_constexpr_pointer_comparison_zero_sized);
      return Success(CmpResult::Unequal, E);
    }

    const CharUnits &LHSOffset = LHSValue.getLValueOffset();
    const CharUnits &RHSOffset = RHSValue.getLValueOffset();

    // Comparing void pointers (including cv void*): relational comparison is
    // only defined for same address / both null; otherwise unspecified.
    if (LHSTy->isVoidPointerType() && LHSOffset != RHSOffset && IsRelational)
      Info.CCEDiag(E, diag::note_constexpr_void_comparison);

    // The comparison here must be unsigned, and performed with the same
    // width as the pointer.
    unsigned PtrSize = Info.Ctx.getTypeSize(LHSTy);
    uint64_t CompareLHS = LHSOffset.getQuantity();
    uint64_t CompareRHS = RHSOffset.getQuantity();
    assert(PtrSize <= 64 && "Unexpected pointer width");
    uint64_t Mask = ~0ULL >> (64 - PtrSize);
    CompareLHS &= Mask;
    CompareRHS &= Mask;

    // If there is a base and this is a relational operator, we can only
    // compare pointers within the object in question; otherwise, the result
    // depends on where the object is located in memory.
    if (!LHSValue.Base.isNull() && IsRelational) {
      QualType BaseTy = getType(LHSValue.Base);
      if (BaseTy->isIncompleteType())
        return Error(E);
      CharUnits Size = Info.Ctx.getTypeSizeInChars(BaseTy);
      uint64_t OffsetLimit = Size.getQuantity();
      if (CompareLHS > OffsetLimit || CompareRHS > OffsetLimit)
        return Error(E);
    }

    if (CompareLHS < CompareRHS)
      return Success(CmpResult::Less, E);
    if (CompareLHS > CompareRHS)
      return Success(CmpResult::Greater, E);
    return Success(CmpResult::Equal, E);
  }

  if (LHSTy->isNullPtrType()) {
    assert(E->isComparisonOp() && "unexpected nullptr operation");
    assert(RHSTy->isNullPtrType() && "missing pointer conversion");
    // nullptr_t comparisons: == / <= / >= are true for the equality case.
    LValue Res;
    if (!evaluatePointer(E->getLHS(), Res, Info) ||
        !evaluatePointer(E->getRHS(), Res, Info))
      return false;
    return Success(CmpResult::Equal, E);
  }

  return DoAfter();
}
} // namespace

bool IntExprEvaluator::VisitBinaryOperator(const BinaryOperator *E) {
  // Integral constant evaluation does not model `=` / compound assignment.
  if (E->isAssignmentOp()) {
    Error(E);
    if (!Info.noteFailure())
      return false;
  }

  if (DataRecursiveIntBinOpEvaluator::shouldEnqueue(E))
    return DataRecursiveIntBinOpEvaluator(*this, Result).Traverse(E);

  assert((!E->getLHS()->getType()->isIntegralOrEnumerationType() ||
          !E->getRHS()->getType()->isIntegralOrEnumerationType()) &&
         "DataRecursiveIntBinOpEvaluator should have handled integral types");

  if (E->isComparisonOp()) {
    // evaluate builtin binary comparisons by evaluating them as three-way
    // comparisons and then translating the result.
    auto OnSuccess = [&](CmpResult CR, const BinaryOperator *E) {
      assert((CR != CmpResult::Unequal || E->isEqualityOp()) &&
             "should only produce Unequal for equality comparisons");
      bool IsEqual = CR == CmpResult::Equal, IsLess = CR == CmpResult::Less,
           IsGreater = CR == CmpResult::Greater;
      auto Op = E->getOpcode();
      switch (Op) {
      default:
        llvm_unreachable("unsupported binary operator");
      case BO_EQ:
      case BO_NE:
        return Success(IsEqual == (Op == BO_EQ), E);
      case BO_LT:
        return Success(IsLess, E);
      case BO_GT:
        return Success(IsGreater, E);
      case BO_LE:
        return Success(IsEqual || IsLess, E);
      case BO_GE:
        return Success(IsEqual || IsGreater, E);
      }
    };
    return evaluateComparisonBinaryOperator(Info, E, OnSuccess, [&]() {
      return ExprEvaluatorBaseTy::VisitBinaryOperator(E);
    });
  }

  QualType LHSTy = E->getLHS()->getType();
  QualType RHSTy = E->getRHS()->getType();

  if (LHSTy->isPointerType() && RHSTy->isPointerType() &&
      E->getOpcode() == BO_Sub) {
    LValue LHSValue, RHSValue;

    bool LHSOK = evaluatePointer(E->getLHS(), LHSValue, Info);
    if (!LHSOK && !Info.noteFailure())
      return false;

    if (!evaluatePointer(E->getRHS(), RHSValue, Info) || !LHSOK)
      return false;

    // Reject differing bases from the normal codepath; we special-case
    // comparisons to null.
    if (!hasSameBase(LHSValue, RHSValue)) {
      // Handle &&A - &&B.
      if (!LHSValue.Offset.isZero() || !RHSValue.Offset.isZero())
        return Error(E);
      const Expr *LHSExpr = LHSValue.Base.dyn_cast<const Expr *>();
      const Expr *RHSExpr = RHSValue.Base.dyn_cast<const Expr *>();
      if (!LHSExpr || !RHSExpr)
        return Error(E);
      const AddrLabelExpr *LHSAddrExpr = dyn_cast<AddrLabelExpr>(LHSExpr);
      const AddrLabelExpr *RHSAddrExpr = dyn_cast<AddrLabelExpr>(RHSExpr);
      if (!LHSAddrExpr || !RHSAddrExpr)
        return Error(E);
      // Make sure both labels come from the same function.
      if (LHSAddrExpr->getLabel()->getDeclContext() !=
          RHSAddrExpr->getLabel()->getDeclContext())
        return Error(E);
      return Success(APValue(LHSAddrExpr, RHSAddrExpr), E);
    }
    const CharUnits &LHSOffset = LHSValue.getLValueOffset();
    const CharUnits &RHSOffset = RHSValue.getLValueOffset();

    SubobjectDesignator &LHSDesignator = LHSValue.getLValueDesignator();
    SubobjectDesignator &RHSDesignator = RHSValue.getLValueDesignator();

    // Pointer difference only within the same array (or one-past).
    if (!LHSDesignator.Invalid && !RHSDesignator.Invalid &&
        !areElementsOfSameArray(getType(LHSValue.Base), LHSDesignator,
                                RHSDesignator))
      Info.CCEDiag(E, diag::note_constexpr_pointer_subtraction_not_same_array);

    QualType Type = E->getLHS()->getType();
    QualType ElementType = Type->castAs<PointerType>()->getPointeeType();

    CharUnits ElementSize;
    if (!handleSizeof(Info, E->getExprLoc(), ElementType, ElementSize))
      return false;

    // As an extension, a type may have zero size (empty struct or union in
    // C, array of zero length). Pointer subtraction in such cases has
    // undefined behavior, so is not constant.
    if (ElementSize.isZero()) {
      Info.FFDiag(E, diag::note_constexpr_pointer_subtraction_zero_size)
          << ElementType;
      return false;
    }

    // Compute (LHSOffset - RHSOffset) / Size carefully, checking for
    // overflow in the final conversion to ptrdiff_t.
    APSInt LHS(llvm::APInt(65, (int64_t)LHSOffset.getQuantity(), true), false);
    APSInt RHS(llvm::APInt(65, (int64_t)RHSOffset.getQuantity(), true), false);
    APSInt ElemSize(llvm::APInt(65, (int64_t)ElementSize.getQuantity(), true),
                    false);
    APSInt TrueResult = (LHS - RHS) / ElemSize;
    APSInt Result = TrueResult.trunc(Info.Ctx.getIntWidth(E->getType()));

    if (Result.extend(65) != TrueResult &&
        !handleOverflow(Info, E, TrueResult, E->getType()))
      return false;
    return Success(Result, E);
  }

  return ExprEvaluatorBaseTy::VisitBinaryOperator(E);
}

bool IntExprEvaluator::VisitUnaryExprOrTypeTraitExpr(
    const UnaryExprOrTypeTraitExpr *E) {
  switch (E->getKind()) {
  case UETT_PreferredAlignOf:
  case UETT_AlignOf: {
    if (E->isArgumentType())
      return Success(getAlignOfType(Info, E->getArgumentType(), E->getKind()),
                     E);
    else
      return Success(getAlignOfExpr(Info, E->getArgumentExpr(), E->getKind()),
                     E);
  }

  case UETT_SizeOf: {
    QualType SrcTy = E->getTypeOfArgument();
    CharUnits Sizeof;
    if (!handleSizeof(Info, E->getExprLoc(), SrcTy, Sizeof))
      return false;
    return Success(Sizeof, E);
  }
  case UETT_VectorElements: {
    QualType Ty = E->getTypeOfArgument();
    // If the vector has a fixed size, we can determine the number of elements
    // at compile time.
    if (Ty->isVectorType())
      return Success(Ty->castAs<VectorType>()->getNumElements(), E);

    assert(Ty->isSizelessVectorType());
    if (Info.InConstantContext)
      Info.CCEDiag(E, diag::note_constexpr_non_const_vectorelements)
          << E->getSourceRange();

    return false;
  }
  }

  llvm_unreachable("unknown expr/type trait");
}

bool IntExprEvaluator::VisitOffsetOfExpr(const OffsetOfExpr *OOE) {
  CharUnits Result;
  unsigned n = OOE->getNumComponents();
  if (n == 0)
    return Error(OOE);
  QualType CurrentType = OOE->getTypeSourceInfo()->getType();
  for (unsigned i = 0; i != n; ++i) {
    OffsetOfNode ON = OOE->getComponent(i);
    switch (ON.getKind()) {
    case OffsetOfNode::Array: {
      const Expr *Idx = OOE->getIndexExpr(ON.getArrayExprIndex());
      APSInt IdxResult;
      if (!evaluateInteger(Idx, IdxResult, Info))
        return false;
      const ArrayType *AT = Info.Ctx.getAsArrayType(CurrentType);
      if (!AT)
        return Error(OOE);
      CurrentType = AT->getElementType();
      CharUnits ElementSize = Info.Ctx.getTypeSizeInChars(CurrentType);
      Result += IdxResult.getSExtValue() * ElementSize;
      break;
    }

    case OffsetOfNode::Field: {
      FieldDecl *MemberDecl = ON.getField();
      const RecordType *RT = CurrentType->getAs<RecordType>();
      if (!RT)
        return Error(OOE);
      RecordDecl *RD = RT->getDecl();
      if (RD->isInvalidDecl())
        return false;
      const StructRecordLayout &RL = Info.Ctx.getStructRecordLayout(RD);
      unsigned i = MemberDecl->getFieldIndex();
      assert(i < RL.getFieldCount() && "offsetof field in wrong type");
      Result += Info.Ctx.toCharUnitsFromBits(RL.getFieldOffset(i));
      CurrentType = MemberDecl->getType();
      break;
    }

    case OffsetOfNode::Identifier:
      llvm_unreachable("dependent __builtin_offsetof");
    }
  }
  return Success(Result, OOE);
}

bool IntExprEvaluator::VisitUnaryOperator(const UnaryOperator *E) {
  switch (E->getOpcode()) {
  default:
    // Address, indirect, pre/post inc/dec, etc are not valid constant exprs.
    // Not valid in constant expressions.
    return Error(E);
  case UO_Extension:
    return Visit(E->getSubExpr());
  case UO_Plus:
    // The result is just the value.
    return Visit(E->getSubExpr());
  case UO_Minus: {
    if (!Visit(E->getSubExpr()))
      return false;
    if (!Result.isInt())
      return Error(E);
    const APSInt &Value = Result.getInt();
    if (Value.isSigned() && Value.isMinSignedValue() && E->canOverflow()) {
      if (Info.checkingForUndefinedBehavior())
        Info.Ctx.getDiagnostics().Report(E->getExprLoc(),
                                         diag::warn_integer_constant_overflow)
            << toString(Value, 10) << E->getType() << E->getSourceRange();

      if (!handleOverflow(Info, E, -Value.extend(Value.getBitWidth() + 1),
                          E->getType()))
        return false;
    }
    return Success(-Value, E);
  }
  case UO_Not: {
    if (!Visit(E->getSubExpr()))
      return false;
    if (!Result.isInt())
      return Error(E);
    return Success(~Result.getInt(), E);
  }
  case UO_LNot: {
    bool bres;
    if (!evaluateAsBooleanCondition(E->getSubExpr(), bres, Info))
      return false;
    return Success(!bres, E);
  }
  }
}

bool IntExprEvaluator::VisitCastExpr(const CastExpr *E) {
  const Expr *SubExpr = E->getSubExpr();
  QualType DestType = E->getType();
  QualType SrcType = SubExpr->getType();

  switch (E->getCastKind()) {
  case CK_ToUnion:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_NullToPointer:
  case CK_IntegralToPointer:
  case CK_ToVoid:
  case CK_VectorSplat:
  case CK_IntegralToFloating:
  case CK_FloatingCast:
  case CK_FloatingRealToComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_BuiltinFnToFnPtr:
  case CK_NonAtomicToAtomic:
  case CK_AddressSpaceConversion:
  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_IntegralToFixedPoint:
  case CK_MatrixCast:
    llvm_unreachable("invalid cast kind for integral value");

  case CK_BitCast:
  case CK_Dependent:
  case CK_LValueBitCast:
    llvm_unreachable("unsupported cast kind");

  case CK_LValueToRValue:
  case CK_AtomicToNonAtomic:
  case CK_NoOp:
  case CK_LValueToRValueBitCast:
    return ExprEvaluatorBaseTy::VisitCastExpr(E);

  case CK_PointerToBoolean:
  case CK_IntegralToBoolean:
  case CK_FloatingToBoolean:
  case CK_BooleanToSignedIntegral:
  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToBoolean: {
    bool BoolResult;
    if (!evaluateAsBooleanCondition(SubExpr, BoolResult, Info))
      return false;
    uint64_t IntResult = BoolResult;
    if (BoolResult && E->getCastKind() == CK_BooleanToSignedIntegral)
      IntResult = (uint64_t)-1;
    return Success(IntResult, E);
  }

  case CK_FixedPointToIntegral: {
    APFixedPoint Src(Info.Ctx.getFixedPointSemantics(SrcType));
    if (!evaluateFixedPoint(SubExpr, Src, Info))
      return false;
    bool Overflowed;
    llvm::APSInt Result = Src.convertToInt(
        Info.Ctx.getIntWidth(DestType),
        DestType->isSignedIntegerOrEnumerationType(), &Overflowed);
    if (Overflowed && !handleOverflow(Info, E, Result, DestType))
      return false;
    return Success(Result, E);
  }

  case CK_FixedPointToBoolean: {
    // Unsigned padding does not affect this.
    APValue Val;
    if (!evaluate(Val, Info, SubExpr))
      return false;
    return Success(Val.getFixedPoint().getBoolValue(), E);
  }

  case CK_IntegralCast: {
    if (!Visit(SubExpr))
      return false;

    if (!Result.isInt()) {
      // Allow casts of address-of-label differences if they are no-ops
      // or narrowing.  (The narrowing case isn't actually guaranteed to
      // be constant-evaluatable except in some narrow cases which are hard
      // to detect here.  We let it through on the assumption the user knows
      // what they are doing.)
      if (Result.isAddrLabelDiff())
        return Info.Ctx.getTypeSize(DestType) <= Info.Ctx.getTypeSize(SrcType);
      // Only allow casts of lvalues if they are lossless.
      return Info.Ctx.getTypeSize(DestType) == Info.Ctx.getTypeSize(SrcType);
    }

    return Success(
        handleIntToIntCast(Info, E, DestType, SrcType, Result.getInt()), E);
  }

  case CK_PointerToIntegral: {
    CCEDiag(E, diag::note_constexpr_invalid_cast) << 0 << E->getSourceRange();

    LValue LV;
    if (!evaluatePointer(SubExpr, LV, Info))
      return false;

    if (LV.getLValueBase()) {
      // Only allow based lvalue casts if they are lossless.
      if (Info.Ctx.getTypeSize(DestType) != Info.Ctx.getTypeSize(SrcType))
        return Error(E);

      LV.Designator.setInvalid();
      LV.moveInto(Result);
      return true;
    }

    APSInt AsInt;
    APValue V;
    LV.moveInto(V);
    if (!V.toIntegralConstant(AsInt, SrcType, Info.Ctx))
      llvm_unreachable("Can't cast this!");

    return Success(handleIntToIntCast(Info, E, DestType, SrcType, AsInt), E);
  }

  case CK_IntegralComplexToReal: {
    ComplexValue C;
    if (!evaluateComplex(SubExpr, C, Info))
      return false;
    return Success(C.getComplexIntReal(), E);
  }

  case CK_FloatingToIntegral: {
    APFloat F(0.0);
    if (!evaluateFloat(SubExpr, F, Info))
      return false;

    APSInt Value;
    if (!handleFloatToIntCast(Info, E, SrcType, F, DestType, Value))
      return false;
    return Success(Value, E);
  }
  }

  llvm_unreachable("unknown cast resulting in integral value");
}

bool IntExprEvaluator::VisitUnaryReal(const UnaryOperator *E) {
  if (E->getSubExpr()->getType()->isAnyComplexType()) {
    ComplexValue LV;
    if (!evaluateComplex(E->getSubExpr(), LV, Info))
      return false;
    if (!LV.isComplexInt())
      return Error(E);
    return Success(LV.getComplexIntReal(), E);
  }

  return Visit(E->getSubExpr());
}

bool IntExprEvaluator::VisitUnaryImag(const UnaryOperator *E) {
  if (E->getSubExpr()->getType()->isComplexIntegerType()) {
    ComplexValue LV;
    if (!evaluateComplex(E->getSubExpr(), LV, Info))
      return false;
    if (!LV.isComplexInt())
      return Error(E);
    return Success(LV.getComplexIntImag(), E);
  }

  VisitIgnoredValue(E->getSubExpr());
  return Success(0, E);
}

bool FixedPointExprEvaluator::VisitUnaryOperator(const UnaryOperator *E) {
  switch (E->getOpcode()) {
  default:
    // Invalid unary operators
    return Error(E);
  case UO_Plus:
    // The result is just the value.
    return Visit(E->getSubExpr());
  case UO_Minus: {
    if (!Visit(E->getSubExpr()))
      return false;
    if (!Result.isFixedPoint())
      return Error(E);
    bool Overflowed;
    APFixedPoint Negated = Result.getFixedPoint().negate(&Overflowed);
    if (Overflowed && !handleOverflow(Info, E, Negated, E->getType()))
      return false;
    return Success(Negated, E);
  }
  case UO_LNot: {
    bool bres;
    if (!evaluateAsBooleanCondition(E->getSubExpr(), bres, Info))
      return false;
    return Success(!bres, E);
  }
  }
}

bool FixedPointExprEvaluator::VisitCastExpr(const CastExpr *E) {
  const Expr *SubExpr = E->getSubExpr();
  QualType DestType = E->getType();
  assert(DestType->isFixedPointType() &&
         "Expected destination type to be a fixed point type");
  auto DestFXSema = Info.Ctx.getFixedPointSemantics(DestType);

  switch (E->getCastKind()) {
  case CK_FixedPointCast: {
    APFixedPoint Src(Info.Ctx.getFixedPointSemantics(SubExpr->getType()));
    if (!evaluateFixedPoint(SubExpr, Src, Info))
      return false;
    bool Overflowed;
    APFixedPoint Result = Src.convert(DestFXSema, &Overflowed);
    if (Overflowed) {
      if (Info.checkingForUndefinedBehavior())
        Info.Ctx.getDiagnostics().Report(
            E->getExprLoc(), diag::warn_fixedpoint_constant_overflow)
            << Result.toString() << E->getType();
      if (!handleOverflow(Info, E, Result, E->getType()))
        return false;
    }
    return Success(Result, E);
  }
  case CK_IntegralToFixedPoint: {
    APSInt Src;
    if (!evaluateInteger(SubExpr, Src, Info))
      return false;

    bool Overflowed;
    APFixedPoint IntResult = APFixedPoint::getFromIntValue(
        Src, Info.Ctx.getFixedPointSemantics(DestType), &Overflowed);

    if (Overflowed) {
      if (Info.checkingForUndefinedBehavior())
        Info.Ctx.getDiagnostics().Report(
            E->getExprLoc(), diag::warn_fixedpoint_constant_overflow)
            << IntResult.toString() << E->getType();
      if (!handleOverflow(Info, E, IntResult, E->getType()))
        return false;
    }

    return Success(IntResult, E);
  }
  case CK_FloatingToFixedPoint: {
    APFloat Src(0.0);
    if (!evaluateFloat(SubExpr, Src, Info))
      return false;

    bool Overflowed;
    APFixedPoint Result = APFixedPoint::getFromFloatValue(
        Src, Info.Ctx.getFixedPointSemantics(DestType), &Overflowed);

    if (Overflowed) {
      if (Info.checkingForUndefinedBehavior())
        Info.Ctx.getDiagnostics().Report(
            E->getExprLoc(), diag::warn_fixedpoint_constant_overflow)
            << Result.toString() << E->getType();
      if (!handleOverflow(Info, E, Result, E->getType()))
        return false;
    }

    return Success(Result, E);
  }
  case CK_NoOp:
  case CK_LValueToRValue:
    return ExprEvaluatorBaseTy::VisitCastExpr(E);
  default:
    return Error(E);
  }
}

bool FixedPointExprEvaluator::VisitBinaryOperator(const BinaryOperator *E) {
  if (E->isAssignmentOp() || E->getOpcode() == BO_Comma)
    return ExprEvaluatorBaseTy::VisitBinaryOperator(E);

  const Expr *LHS = E->getLHS();
  const Expr *RHS = E->getRHS();
  FixedPointSemantics ResultFXSema =
      Info.Ctx.getFixedPointSemantics(E->getType());

  APFixedPoint LHSFX(Info.Ctx.getFixedPointSemantics(LHS->getType()));
  if (!evaluateFixedPointOrInteger(LHS, LHSFX, Info))
    return false;
  APFixedPoint RHSFX(Info.Ctx.getFixedPointSemantics(RHS->getType()));
  if (!evaluateFixedPointOrInteger(RHS, RHSFX, Info))
    return false;

  bool OpOverflow = false, ConversionOverflow = false;
  APFixedPoint Result(LHSFX.getSemantics());
  switch (E->getOpcode()) {
  case BO_Add: {
    Result = LHSFX.add(RHSFX, &OpOverflow)
                 .convert(ResultFXSema, &ConversionOverflow);
    break;
  }
  case BO_Sub: {
    Result = LHSFX.sub(RHSFX, &OpOverflow)
                 .convert(ResultFXSema, &ConversionOverflow);
    break;
  }
  case BO_Mul: {
    Result = LHSFX.mul(RHSFX, &OpOverflow)
                 .convert(ResultFXSema, &ConversionOverflow);
    break;
  }
  case BO_Div: {
    if (RHSFX.getValue() == 0) {
      Info.FFDiag(E, diag::note_expr_divide_by_zero);
      return false;
    }
    Result = LHSFX.div(RHSFX, &OpOverflow)
                 .convert(ResultFXSema, &ConversionOverflow);
    break;
  }
  case BO_Shl:
  case BO_Shr: {
    FixedPointSemantics LHSSema = LHSFX.getSemantics();
    llvm::APSInt RHSVal = RHSFX.getValue();

    unsigned ShiftBW =
        LHSSema.getWidth() - (unsigned)LHSSema.hasUnsignedPadding();
    unsigned Amt = RHSVal.getLimitedValue(ShiftBW - 1);
    // Embedded-C 4.1.6.2.2:
    //   The right operand must be nonnegative and less than the total number
    //   of (nonpadding) bits of the fixed-point operand ...
    if (RHSVal.isNegative())
      Info.CCEDiag(E, diag::note_constexpr_negative_shift) << RHSVal;
    else if (Amt != RHSVal)
      Info.CCEDiag(E, diag::note_constexpr_large_shift)
          << RHSVal << E->getType() << ShiftBW;

    if (E->getOpcode() == BO_Shl)
      Result = LHSFX.shl(Amt, &OpOverflow);
    else
      Result = LHSFX.shr(Amt, &OpOverflow);
    break;
  }
  default:
    return false;
  }
  if (OpOverflow || ConversionOverflow) {
    if (Info.checkingForUndefinedBehavior())
      Info.Ctx.getDiagnostics().Report(E->getExprLoc(),
                                       diag::warn_fixedpoint_constant_overflow)
          << Result.toString() << E->getType();
    if (!handleOverflow(Info, E, Result, E->getType()))
      return false;
  }
  return Success(Result, E);
}

namespace {
class FloatExprEvaluator : public ExprEvaluatorBase<FloatExprEvaluator> {
  APFloat &Result;

public:
  FloatExprEvaluator(EvalInfo &info, APFloat &result)
      : ExprEvaluatorBaseTy(info), Result(result) {}

  bool Success(const APValue &V, const Expr *e) {
    Result = V.getFloat();
    return true;
  }

  bool ZeroInitialization(const Expr *E) {
    Result = APFloat::getZero(Info.Ctx.getFloatTypeSemantics(E->getType()));
    return true;
  }

  bool VisitCallExpr(const CallExpr *E);

  bool VisitUnaryOperator(const UnaryOperator *E);
  bool VisitBinaryOperator(const BinaryOperator *E);
  bool VisitFloatingLiteral(const FloatingLiteral *E);
  bool VisitCastExpr(const CastExpr *E);

  bool VisitUnaryReal(const UnaryOperator *E);
  bool VisitUnaryImag(const UnaryOperator *E);
};
} // end anonymous namespace

namespace {
bool evaluateFloat(const Expr *E, APFloat &Result, EvalInfo &Info) {

  assert(E->isPRValue() && E->getType()->isRealFloatingType());
  return FloatExprEvaluator(Info, Result).Visit(E);
}
} // namespace

namespace {
bool tryEvaluateBuiltinNaN(const TreeContext &Context, QualType ResultTy,
                           const Expr *Arg, bool SNaN, llvm::APFloat &Result) {
  const StringLiteral *S = dyn_cast<StringLiteral>(Arg->IgnoreParenCasts());
  if (!S)
    return false;

  const llvm::fltSemantics &Sem = Context.getFloatTypeSemantics(ResultTy);

  llvm::APInt fill;

  // Treat empty strings as if they were zero.
  if (S->getString().empty())
    fill = llvm::APInt(32, 0);
  else if (S->getString().getAsInteger(0, fill))
    return false;

  if (Context.getTargetInfo().isNan2008()) {
    if (SNaN)
      Result = llvm::APFloat::getSNaN(Sem, false, &fill);
    else
      Result = llvm::APFloat::getQNaN(Sem, false, &fill);
  } else {
    // Legacy NaN encoding (pre-IEEE 754-2008).
    if (SNaN)
      Result = llvm::APFloat::getQNaN(Sem, false, &fill);
    else
      Result = llvm::APFloat::getSNaN(Sem, false, &fill);
  }

  return true;
}
} // namespace

bool FloatExprEvaluator::VisitCallExpr(const CallExpr *E) {
  if (!IsConstantevaluatedBuiltinCall(E))
    return ExprEvaluatorBaseTy::VisitCallExpr(E);

  switch (E->getBuiltinCallee()) {
  default:
    return false;

  case Builtin::BI__builtin_huge_val:
  case Builtin::BI__builtin_huge_valf:
  case Builtin::BI__builtin_huge_vall:
  case Builtin::BI__builtin_huge_valf16:
  case Builtin::BI__builtin_huge_valf128:
  case Builtin::BI__builtin_inf:
  case Builtin::BI__builtin_inff:
  case Builtin::BI__builtin_infl:
  case Builtin::BI__builtin_inff16:
  case Builtin::BI__builtin_inff128: {
    const llvm::fltSemantics &Sem =
        Info.Ctx.getFloatTypeSemantics(E->getType());
    Result = llvm::APFloat::getInf(Sem);
    return true;
  }

  case Builtin::BI__builtin_nans:
  case Builtin::BI__builtin_nansf:
  case Builtin::BI__builtin_nansl:
  case Builtin::BI__builtin_nansf16:
  case Builtin::BI__builtin_nansf128:
    if (!tryEvaluateBuiltinNaN(Info.Ctx, E->getType(), E->getArg(0), true,
                               Result))
      return Error(E);
    return true;

  case Builtin::BI__builtin_nan:
  case Builtin::BI__builtin_nanf:
  case Builtin::BI__builtin_nanl:
  case Builtin::BI__builtin_nanf16:
  case Builtin::BI__builtin_nanf128:
    // If this is __builtin_nan() turn this into a nan, otherwise we
    // can't constant fold it.
    if (!tryEvaluateBuiltinNaN(Info.Ctx, E->getType(), E->getArg(0), false,
                               Result))
      return Error(E);
    return true;

  case Builtin::BI__builtin_fabs:
  case Builtin::BI__builtin_fabsf:
  case Builtin::BI__builtin_fabsl:
  case Builtin::BI__builtin_fabsf128:
    // The C standard says "fabs raises no floating-point exceptions,
    // even if x is a signaling NaN. The returned value is independent of
    // the current rounding direction mode."  Therefore constant folding can
    // proceed without regard to the floating point settings.
    // Reference, WG14 N2478 F.10.4.3
    if (!evaluateFloat(E->getArg(0), Result, Info))
      return false;

    if (Result.isNegative())
      Result.changeSign();
    return true;

  case Builtin::BI__arithmetic_fence:
    return evaluateFloat(E->getArg(0), Result, Info);

  case Builtin::BI__builtin_copysign:
  case Builtin::BI__builtin_copysignf:
  case Builtin::BI__builtin_copysignl:
  case Builtin::BI__builtin_copysignf128: {
    APFloat RHS(0.);
    if (!evaluateFloat(E->getArg(0), Result, Info) ||
        !evaluateFloat(E->getArg(1), RHS, Info))
      return false;
    Result.copySign(RHS);
    return true;
  }

  case Builtin::BI__builtin_fmax:
  case Builtin::BI__builtin_fmaxf:
  case Builtin::BI__builtin_fmaxl:
  case Builtin::BI__builtin_fmaxf16:
  case Builtin::BI__builtin_fmaxf128: {
    APFloat RHS(0.);
    if (!evaluateFloat(E->getArg(0), Result, Info) ||
        !evaluateFloat(E->getArg(1), RHS, Info))
      return false;
    // When comparing zeroes, return +0.0 if one of the zeroes is positive.
    if (Result.isZero() && RHS.isZero() && Result.isNegative())
      Result = RHS;
    else if (Result.isNaN() || RHS > Result)
      Result = RHS;
    return true;
  }

  case Builtin::BI__builtin_fmin:
  case Builtin::BI__builtin_fminf:
  case Builtin::BI__builtin_fminl:
  case Builtin::BI__builtin_fminf16:
  case Builtin::BI__builtin_fminf128: {
    APFloat RHS(0.);
    if (!evaluateFloat(E->getArg(0), Result, Info) ||
        !evaluateFloat(E->getArg(1), RHS, Info))
      return false;
    // When comparing zeroes, return -0.0 if one of the zeroes is negative.
    if (Result.isZero() && RHS.isZero() && RHS.isNegative())
      Result = RHS;
    else if (Result.isNaN() || RHS < Result)
      Result = RHS;
    return true;
  }
  }
}

bool FloatExprEvaluator::VisitUnaryReal(const UnaryOperator *E) {
  if (E->getSubExpr()->getType()->isAnyComplexType()) {
    ComplexValue CV;
    if (!evaluateComplex(E->getSubExpr(), CV, Info))
      return false;
    Result = CV.FloatReal;
    return true;
  }

  return Visit(E->getSubExpr());
}

bool FloatExprEvaluator::VisitUnaryImag(const UnaryOperator *E) {
  if (E->getSubExpr()->getType()->isAnyComplexType()) {
    ComplexValue CV;
    if (!evaluateComplex(E->getSubExpr(), CV, Info))
      return false;
    Result = CV.FloatImag;
    return true;
  }

  VisitIgnoredValue(E->getSubExpr());
  const llvm::fltSemantics &Sem = Info.Ctx.getFloatTypeSemantics(E->getType());
  Result = llvm::APFloat::getZero(Sem);
  return true;
}

bool FloatExprEvaluator::VisitUnaryOperator(const UnaryOperator *E) {
  switch (E->getOpcode()) {
  default:
    return Error(E);
  case UO_Plus:
    return evaluateFloat(E->getSubExpr(), Result, Info);
  case UO_Minus:
    // In C standard, WG14 N2478 F.3 p4
    // "the unary - raises no floating point exceptions,
    // even if the operand is signalling."
    if (!evaluateFloat(E->getSubExpr(), Result, Info))
      return false;
    Result.changeSign();
    return true;
  }
}

bool FloatExprEvaluator::VisitBinaryOperator(const BinaryOperator *E) {
  if (E->isAssignmentOp() || E->getOpcode() == BO_Comma)
    return ExprEvaluatorBaseTy::VisitBinaryOperator(E);

  APFloat RHS(0.0);
  bool LHSOK = evaluateFloat(E->getLHS(), Result, Info);
  if (!LHSOK && !Info.noteFailure())
    return false;
  return evaluateFloat(E->getRHS(), RHS, Info) && LHSOK &&
         handleFloatFloatBinOp(Info, E, Result, E->getOpcode(), RHS);
}

bool FloatExprEvaluator::VisitFloatingLiteral(const FloatingLiteral *E) {
  Result = E->getValue();
  return true;
}

bool FloatExprEvaluator::VisitCastExpr(const CastExpr *E) {
  const Expr *SubExpr = E->getSubExpr();

  switch (E->getCastKind()) {
  default:
    return ExprEvaluatorBaseTy::VisitCastExpr(E);

  case CK_IntegralToFloating: {
    APSInt IntResult;
    const FPOptions FPO = E->getFPFeaturesInEffect(Info.Ctx.getLangOpts());
    return evaluateInteger(SubExpr, IntResult, Info) &&
           handleIntToFloatCast(Info, E, FPO, SubExpr->getType(), IntResult,
                                E->getType(), Result);
  }

  case CK_FixedPointToFloating: {
    APFixedPoint FixResult(Info.Ctx.getFixedPointSemantics(SubExpr->getType()));
    if (!evaluateFixedPoint(SubExpr, FixResult, Info))
      return false;
    Result =
        FixResult.convertToFloat(Info.Ctx.getFloatTypeSemantics(E->getType()));
    return true;
  }

  case CK_FloatingCast: {
    if (!Visit(SubExpr))
      return false;
    return handleFloatToFloatCast(Info, E, SubExpr->getType(), E->getType(),
                                  Result);
  }

  case CK_FloatingComplexToReal: {
    ComplexValue V;
    if (!evaluateComplex(SubExpr, V, Info))
      return false;
    Result = V.getComplexFloatReal();
    return true;
  }
  }
}

namespace {
class ComplexExprEvaluator : public ExprEvaluatorBase<ComplexExprEvaluator> {
  ComplexValue &Result;

public:
  ComplexExprEvaluator(EvalInfo &info, ComplexValue &Result)
      : ExprEvaluatorBaseTy(info), Result(Result) {}

  bool Success(const APValue &V, const Expr *e) {
    Result.setFrom(V);
    return true;
  }

  bool ZeroInitialization(const Expr *E);

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  bool VisitImaginaryLiteral(const ImaginaryLiteral *E);
  bool VisitCastExpr(const CastExpr *E);
  bool VisitBinaryOperator(const BinaryOperator *E);
  bool VisitUnaryOperator(const UnaryOperator *E);
  bool VisitInitListExpr(const InitListExpr *E);
  bool VisitCallExpr(const CallExpr *E);
};
} // end anonymous namespace

namespace {
bool evaluateComplex(const Expr *E, ComplexValue &Result, EvalInfo &Info) {

  assert(E->isPRValue() && E->getType()->isAnyComplexType());
  return ComplexExprEvaluator(Info, Result).Visit(E);
}
} // namespace

bool ComplexExprEvaluator::ZeroInitialization(const Expr *E) {
  QualType ElemTy = E->getType()->castAs<ComplexType>()->getElementType();
  if (ElemTy->isRealFloatingType()) {
    Result.makeComplexFloat();
    APFloat Zero = APFloat::getZero(Info.Ctx.getFloatTypeSemantics(ElemTy));
    Result.FloatReal = Zero;
    Result.FloatImag = Zero;
  } else {
    Result.makeComplexInt();
    APSInt Zero = Info.Ctx.MakeIntValue(0, ElemTy);
    Result.IntReal = Zero;
    Result.IntImag = Zero;
  }
  return true;
}

bool ComplexExprEvaluator::VisitImaginaryLiteral(const ImaginaryLiteral *E) {
  const Expr *SubExpr = E->getSubExpr();

  if (SubExpr->getType()->isRealFloatingType()) {
    Result.makeComplexFloat();
    APFloat &Imag = Result.FloatImag;
    if (!evaluateFloat(SubExpr, Imag, Info))
      return false;

    Result.FloatReal =
        APFloat::getZero(Imag.getSemantics(), /*negative=*/false);
    return true;
  } else {
    assert(SubExpr->getType()->isIntegerType() &&
           "Unexpected imaginary literal.");

    Result.makeComplexInt();
    APSInt &Imag = Result.IntImag;
    if (!evaluateInteger(SubExpr, Imag, Info))
      return false;

    Result.IntReal = APSInt(Imag.getBitWidth(), !Imag.isSigned());
    return true;
  }
}

bool ComplexExprEvaluator::VisitCastExpr(const CastExpr *E) {

  switch (E->getCastKind()) {
  case CK_BitCast:
  case CK_ToUnion:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_NullToPointer:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_PointerToBoolean:
  case CK_ToVoid:
  case CK_VectorSplat:
  case CK_IntegralCast:
  case CK_BooleanToSignedIntegral:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingToBoolean:
  case CK_FloatingCast:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexToBoolean:
    llvm_unreachable("unsupported cast kind");
  case CK_BuiltinFnToFnPtr:
  case CK_NonAtomicToAtomic:
  case CK_AddressSpaceConversion:
  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
  case CK_MatrixCast:
    llvm_unreachable("invalid cast kind for complex value");

  case CK_LValueToRValue:
  case CK_AtomicToNonAtomic:
  case CK_NoOp:
  case CK_LValueToRValueBitCast:
    return ExprEvaluatorBaseTy::VisitCastExpr(E);

  case CK_Dependent:
  case CK_LValueBitCast:
    return Error(E);

  case CK_FloatingRealToComplex: {
    APFloat &Real = Result.FloatReal;
    if (!evaluateFloat(E->getSubExpr(), Real, Info))
      return false;

    Result.makeComplexFloat();
    Result.FloatImag =
        APFloat::getZero(Real.getSemantics(), /*negative=*/false);
    return true;
  }

  case CK_FloatingComplexCast: {
    if (!Visit(E->getSubExpr()))
      return false;

    QualType To = E->getType()->castAs<ComplexType>()->getElementType();
    QualType From =
        E->getSubExpr()->getType()->castAs<ComplexType>()->getElementType();

    return handleFloatToFloatCast(Info, E, From, To, Result.FloatReal) &&
           handleFloatToFloatCast(Info, E, From, To, Result.FloatImag);
  }

  case CK_FloatingComplexToIntegralComplex: {
    if (!Visit(E->getSubExpr()))
      return false;

    QualType To = E->getType()->castAs<ComplexType>()->getElementType();
    QualType From =
        E->getSubExpr()->getType()->castAs<ComplexType>()->getElementType();
    Result.makeComplexInt();
    return handleFloatToIntCast(Info, E, From, Result.FloatReal, To,
                                Result.IntReal) &&
           handleFloatToIntCast(Info, E, From, Result.FloatImag, To,
                                Result.IntImag);
  }

  case CK_IntegralRealToComplex: {
    APSInt &Real = Result.IntReal;
    if (!evaluateInteger(E->getSubExpr(), Real, Info))
      return false;

    Result.makeComplexInt();
    Result.IntImag = APSInt(Real.getBitWidth(), !Real.isSigned());
    return true;
  }

  case CK_IntegralComplexCast: {
    if (!Visit(E->getSubExpr()))
      return false;

    QualType To = E->getType()->castAs<ComplexType>()->getElementType();
    QualType From =
        E->getSubExpr()->getType()->castAs<ComplexType>()->getElementType();

    Result.IntReal = handleIntToIntCast(Info, E, To, From, Result.IntReal);
    Result.IntImag = handleIntToIntCast(Info, E, To, From, Result.IntImag);
    return true;
  }

  case CK_IntegralComplexToFloatingComplex: {
    if (!Visit(E->getSubExpr()))
      return false;

    const FPOptions FPO = E->getFPFeaturesInEffect(Info.Ctx.getLangOpts());
    QualType To = E->getType()->castAs<ComplexType>()->getElementType();
    QualType From =
        E->getSubExpr()->getType()->castAs<ComplexType>()->getElementType();
    Result.makeComplexFloat();
    return handleIntToFloatCast(Info, E, FPO, From, Result.IntReal, To,
                                Result.FloatReal) &&
           handleIntToFloatCast(Info, E, FPO, From, Result.IntImag, To,
                                Result.FloatImag);
  }
  }

  llvm_unreachable("unknown cast resulting in complex value");
}

bool ComplexExprEvaluator::VisitBinaryOperator(const BinaryOperator *E) {
  if (E->isAssignmentOp() || E->getOpcode() == BO_Comma)
    return ExprEvaluatorBaseTy::VisitBinaryOperator(E);

  // Track whether the LHS or RHS is real at the type system level. When this is
  // the case we can simplify our evaluation strategy.
  bool LHSReal = false, RHSReal = false;

  bool LHSOK;
  if (E->getLHS()->getType()->isRealFloatingType()) {
    LHSReal = true;
    APFloat &Real = Result.FloatReal;
    LHSOK = evaluateFloat(E->getLHS(), Real, Info);
    if (LHSOK) {
      Result.makeComplexFloat();
      Result.FloatImag =
          APFloat::getZero(Real.getSemantics(), /*negative=*/false);
    }
  } else {
    LHSOK = Visit(E->getLHS());
  }
  if (!LHSOK && !Info.noteFailure())
    return false;

  ComplexValue RHS;
  if (E->getRHS()->getType()->isRealFloatingType()) {
    RHSReal = true;
    APFloat &Real = RHS.FloatReal;
    if (!evaluateFloat(E->getRHS(), Real, Info) || !LHSOK)
      return false;
    RHS.makeComplexFloat();
    RHS.FloatImag = APFloat::getZero(Real.getSemantics(), /*negative=*/false);
  } else if (!evaluateComplex(E->getRHS(), RHS, Info) || !LHSOK)
    return false;

  // Integer complex + floating complex was using getComplexIntReal() on a
  // ComplexValue that held floats (union), producing wrong results / NaNs.
  if (Result.isComplexInt() && RHS.isComplexFloat()) {
    QualType To = E->getType()->castAs<ComplexType>()->getElementType();
    QualType From =
        E->getLHS()->getType()->castAs<ComplexType>()->getElementType();
    const FPOptions FPO = E->getFPFeaturesInEffect(Info.Ctx.getLangOpts());
    APSInt IR = Result.IntReal, II = Result.IntImag;
    Result.makeComplexFloat();
    if (!handleIntToFloatCast(Info, E, FPO, From, IR, To, Result.FloatReal) ||
        !handleIntToFloatCast(Info, E, FPO, From, II, To, Result.FloatImag))
      return false;
  } else if (Result.isComplexFloat() && RHS.isComplexInt()) {
    QualType To = E->getType()->castAs<ComplexType>()->getElementType();
    QualType From =
        E->getRHS()->getType()->castAs<ComplexType>()->getElementType();
    const FPOptions FPO = E->getFPFeaturesInEffect(Info.Ctx.getLangOpts());
    APSInt IR = RHS.IntReal, II = RHS.IntImag;
    RHS.makeComplexFloat();
    if (!handleIntToFloatCast(Info, E, FPO, From, IR, To, RHS.FloatReal) ||
        !handleIntToFloatCast(Info, E, FPO, From, II, To, RHS.FloatImag))
      return false;
  }

  assert(!(LHSReal && RHSReal) &&
         "Cannot have both operands of a complex operation be real.");
  switch (E->getOpcode()) {
  default:
    return Error(E);
  case BO_Add:
    if (Result.isComplexFloat()) {
      Result.getComplexFloatReal().add(RHS.getComplexFloatReal(),
                                       APFloat::rmNearestTiesToEven);
      if (LHSReal)
        Result.getComplexFloatImag() = RHS.getComplexFloatImag();
      else if (!RHSReal)
        Result.getComplexFloatImag().add(RHS.getComplexFloatImag(),
                                         APFloat::rmNearestTiesToEven);
    } else {
      Result.getComplexIntReal() += RHS.getComplexIntReal();
      Result.getComplexIntImag() += RHS.getComplexIntImag();
    }
    break;
  case BO_Sub:
    if (Result.isComplexFloat()) {
      Result.getComplexFloatReal().subtract(RHS.getComplexFloatReal(),
                                            APFloat::rmNearestTiesToEven);
      if (LHSReal) {
        Result.getComplexFloatImag() = RHS.getComplexFloatImag();
        Result.getComplexFloatImag().changeSign();
      } else if (!RHSReal) {
        Result.getComplexFloatImag().subtract(RHS.getComplexFloatImag(),
                                              APFloat::rmNearestTiesToEven);
      }
    } else {
      Result.getComplexIntReal() -= RHS.getComplexIntReal();
      Result.getComplexIntImag() -= RHS.getComplexIntImag();
    }
    break;
  case BO_Mul:
    if (Result.isComplexFloat()) {
      // This is an implementation of complex multiplication according to the
      // constraints laid out in C11 Annex G. The implementation uses the
      // following naming scheme:
      //   (a + ib) * (c + id)
      ComplexValue LHS = Result;
      APFloat &A = LHS.getComplexFloatReal();
      APFloat &B = LHS.getComplexFloatImag();
      APFloat &C = RHS.getComplexFloatReal();
      APFloat &D = RHS.getComplexFloatImag();
      APFloat &ResR = Result.getComplexFloatReal();
      APFloat &ResI = Result.getComplexFloatImag();
      if (LHSReal) {
        assert(!RHSReal && "Cannot have two real operands for a complex op!");
        ResR = A * C;
        ResI = A * D;
      } else if (RHSReal) {
        ResR = C * A;
        ResI = C * B;
      } else {
        // In the fully general case, we need to handle NaNs and infinities
        // robustly.
        APFloat AC = A * C;
        APFloat BD = B * D;
        APFloat AD = A * D;
        APFloat BC = B * C;
        ResR = AC - BD;
        ResI = AD + BC;
        if (ResR.isNaN() && ResI.isNaN()) {
          bool Recalc = false;
          if (A.isInfinity() || B.isInfinity()) {
            A = APFloat::copySign(
                APFloat(A.getSemantics(), A.isInfinity() ? 1 : 0), A);
            B = APFloat::copySign(
                APFloat(B.getSemantics(), B.isInfinity() ? 1 : 0), B);
            if (C.isNaN())
              C = APFloat::copySign(APFloat(C.getSemantics()), C);
            if (D.isNaN())
              D = APFloat::copySign(APFloat(D.getSemantics()), D);
            Recalc = true;
          }
          if (C.isInfinity() || D.isInfinity()) {
            C = APFloat::copySign(
                APFloat(C.getSemantics(), C.isInfinity() ? 1 : 0), C);
            D = APFloat::copySign(
                APFloat(D.getSemantics(), D.isInfinity() ? 1 : 0), D);
            if (A.isNaN())
              A = APFloat::copySign(APFloat(A.getSemantics()), A);
            if (B.isNaN())
              B = APFloat::copySign(APFloat(B.getSemantics()), B);
            Recalc = true;
          }
          if (!Recalc && (AC.isInfinity() || BD.isInfinity() ||
                          AD.isInfinity() || BC.isInfinity())) {
            if (A.isNaN())
              A = APFloat::copySign(APFloat(A.getSemantics()), A);
            if (B.isNaN())
              B = APFloat::copySign(APFloat(B.getSemantics()), B);
            if (C.isNaN())
              C = APFloat::copySign(APFloat(C.getSemantics()), C);
            if (D.isNaN())
              D = APFloat::copySign(APFloat(D.getSemantics()), D);
            Recalc = true;
          }
          if (Recalc) {
            ResR = APFloat::getInf(A.getSemantics()) * (A * C - B * D);
            ResI = APFloat::getInf(A.getSemantics()) * (A * D + B * C);
          }
        }
      }
    } else {
      ComplexValue LHS = Result;
      Result.getComplexIntReal() =
          (LHS.getComplexIntReal() * RHS.getComplexIntReal() -
           LHS.getComplexIntImag() * RHS.getComplexIntImag());
      Result.getComplexIntImag() =
          (LHS.getComplexIntReal() * RHS.getComplexIntImag() +
           LHS.getComplexIntImag() * RHS.getComplexIntReal());
    }
    break;
  case BO_Div:
    if (Result.isComplexFloat()) {
      // This is an implementation of complex division according to the
      // constraints laid out in C11 Annex G. The implementation uses the
      // following naming scheme:
      //   (a + ib) / (c + id)
      ComplexValue LHS = Result;
      APFloat &A = LHS.getComplexFloatReal();
      APFloat &B = LHS.getComplexFloatImag();
      APFloat &C = RHS.getComplexFloatReal();
      APFloat &D = RHS.getComplexFloatImag();
      APFloat &ResR = Result.getComplexFloatReal();
      APFloat &ResI = Result.getComplexFloatImag();
      if (RHSReal) {
        ResR = A / C;
        ResI = B / C;
      } else {
        if (LHSReal) {
          // No real optimizations we can do here, stub out with zero.
          B = APFloat::getZero(A.getSemantics());
        }
        int DenomLogB = 0;
        APFloat MaxCD = maxnum(abs(C), abs(D));
        if (MaxCD.isFinite()) {
          DenomLogB = ilogb(MaxCD);
          C = scalbn(C, -DenomLogB, APFloat::rmNearestTiesToEven);
          D = scalbn(D, -DenomLogB, APFloat::rmNearestTiesToEven);
        }
        APFloat Denom = C * C + D * D;
        ResR = scalbn((A * C + B * D) / Denom, -DenomLogB,
                      APFloat::rmNearestTiesToEven);
        ResI = scalbn((B * C - A * D) / Denom, -DenomLogB,
                      APFloat::rmNearestTiesToEven);
        if (ResR.isNaN() && ResI.isNaN()) {
          if (Denom.isPosZero() && (!A.isNaN() || !B.isNaN())) {
            ResR = APFloat::getInf(ResR.getSemantics(), C.isNegative()) * A;
            ResI = APFloat::getInf(ResR.getSemantics(), C.isNegative()) * B;
          } else if ((A.isInfinity() || B.isInfinity()) && C.isFinite() &&
                     D.isFinite()) {
            A = APFloat::copySign(
                APFloat(A.getSemantics(), A.isInfinity() ? 1 : 0), A);
            B = APFloat::copySign(
                APFloat(B.getSemantics(), B.isInfinity() ? 1 : 0), B);
            ResR = APFloat::getInf(ResR.getSemantics()) * (A * C + B * D);
            ResI = APFloat::getInf(ResI.getSemantics()) * (B * C - A * D);
          } else if (MaxCD.isInfinity() && A.isFinite() && B.isFinite()) {
            C = APFloat::copySign(
                APFloat(C.getSemantics(), C.isInfinity() ? 1 : 0), C);
            D = APFloat::copySign(
                APFloat(D.getSemantics(), D.isInfinity() ? 1 : 0), D);
            ResR = APFloat::getZero(ResR.getSemantics()) * (A * C + B * D);
            ResI = APFloat::getZero(ResI.getSemantics()) * (B * C - A * D);
          }
        }
      }
    } else {
      if (RHS.getComplexIntReal() == 0 && RHS.getComplexIntImag() == 0)
        return Error(E, diag::note_expr_divide_by_zero);

      ComplexValue LHS = Result;
      APSInt Den = RHS.getComplexIntReal() * RHS.getComplexIntReal() +
                   RHS.getComplexIntImag() * RHS.getComplexIntImag();
      Result.getComplexIntReal() =
          (LHS.getComplexIntReal() * RHS.getComplexIntReal() +
           LHS.getComplexIntImag() * RHS.getComplexIntImag()) /
          Den;
      Result.getComplexIntImag() =
          (LHS.getComplexIntImag() * RHS.getComplexIntReal() -
           LHS.getComplexIntReal() * RHS.getComplexIntImag()) /
          Den;
    }
    break;
  }

  return true;
}

bool ComplexExprEvaluator::VisitUnaryOperator(const UnaryOperator *E) {
  if (!Visit(E->getSubExpr()))
    return false;

  switch (E->getOpcode()) {
  default:
    return Error(E);
  case UO_Extension:
    return true;
  case UO_Plus:
    // The result is always just the subexpr.
    return true;
  case UO_Minus:
    if (Result.isComplexFloat()) {
      Result.getComplexFloatReal().changeSign();
      Result.getComplexFloatImag().changeSign();
    } else {
      Result.getComplexIntReal() = -Result.getComplexIntReal();
      Result.getComplexIntImag() = -Result.getComplexIntImag();
    }
    return true;
  case UO_Not:
    if (Result.isComplexFloat())
      Result.getComplexFloatImag().changeSign();
    else
      Result.getComplexIntImag() = -Result.getComplexIntImag();
    return true;
  }
}

bool ComplexExprEvaluator::VisitInitListExpr(const InitListExpr *E) {
  if (E->getNumInits() == 2) {
    if (E->getType()->isComplexType()) {
      Result.makeComplexFloat();
      if (!evaluateFloat(E->getInit(0), Result.FloatReal, Info))
        return false;
      if (!evaluateFloat(E->getInit(1), Result.FloatImag, Info))
        return false;
    } else {
      Result.makeComplexInt();
      if (!evaluateInteger(E->getInit(0), Result.IntReal, Info))
        return false;
      if (!evaluateInteger(E->getInit(1), Result.IntImag, Info))
        return false;
    }
    return true;
  }
  return ExprEvaluatorBaseTy::VisitInitListExpr(E);
}

bool ComplexExprEvaluator::VisitCallExpr(const CallExpr *E) {
  if (!IsConstantevaluatedBuiltinCall(E))
    return ExprEvaluatorBaseTy::VisitCallExpr(E);

  switch (E->getBuiltinCallee()) {
  case Builtin::BI__builtin_complex:
    Result.makeComplexFloat();
    if (!evaluateFloat(E->getArg(0), Result.FloatReal, Info))
      return false;
    if (!evaluateFloat(E->getArg(1), Result.FloatImag, Info))
      return false;
    return true;

  default:
    return false;
  }
}

// Atomic expression evaluation, essentially just handling the NonAtomicToAtomic
// implicit conversion.

namespace {
class AtomicExprEvaluator : public ExprEvaluatorBase<AtomicExprEvaluator> {
  const LValue *This;
  APValue &Result;

public:
  AtomicExprEvaluator(EvalInfo &Info, const LValue *This, APValue &Result)
      : ExprEvaluatorBaseTy(Info), This(This), Result(Result) {}

  bool Success(const APValue &V, const Expr *E) {
    Result = V;
    return true;
  }

  bool ZeroInitialization(const Expr *E) {
    ImplicitValueInitExpr VIE(
        E->getType()->castAs<AtomicType>()->getValueType());
    // For atomic-qualified class/array types, initialize the unwrapped value
    // in-place.
    return This ? evaluateInPlace(Result, Info, *This, &VIE)
                : evaluate(Result, Info, &VIE);
  }

  bool VisitCastExpr(const CastExpr *E) {
    switch (E->getCastKind()) {
    default:
      return ExprEvaluatorBaseTy::VisitCastExpr(E);
    case CK_NullToPointer:
      VisitIgnoredValue(E->getSubExpr());
      return ZeroInitialization(E);
    case CK_NonAtomicToAtomic:
      return This ? evaluateInPlace(Result, Info, *This, E->getSubExpr())
                  : evaluate(Result, Info, E->getSubExpr());
    }
  }
};
} // end anonymous namespace

namespace {
bool evaluateAtomic(const Expr *E, const LValue *This, APValue &Result,
                    EvalInfo &Info) {

  assert(E->isPRValue() && E->getType()->isAtomicType());
  return AtomicExprEvaluator(Info, This, Result).Visit(E);
}
} // namespace

// Void expression evaluation, primarily for a cast to void on the LHS of a
// comma operator

namespace {
class VoidExprEvaluator : public ExprEvaluatorBase<VoidExprEvaluator> {
public:
  VoidExprEvaluator(EvalInfo &Info) : ExprEvaluatorBaseTy(Info) {}

  bool Success(const APValue &V, const Expr *e) { return true; }

  bool ZeroInitialization(const Expr *E) { return true; }

  bool VisitCastExpr(const CastExpr *E) {
    switch (E->getCastKind()) {
    default:
      return ExprEvaluatorBaseTy::VisitCastExpr(E);
    case CK_ToVoid:
      VisitIgnoredValue(E->getSubExpr());
      return true;
    }
  }

  bool VisitCallExpr(const CallExpr *E) {
    if (!IsConstantevaluatedBuiltinCall(E))
      return ExprEvaluatorBaseTy::VisitCallExpr(E);

    switch (E->getBuiltinCallee()) {
    case Builtin::BI__assume:
    case Builtin::BI__builtin_assume:
      return true;

    default:
      return false;
    }
  }
};
} // end anonymous namespace

namespace {
bool evaluateVoid(const Expr *E, EvalInfo &Info) {

  assert(E->isPRValue() && E->getType()->isVoidType());
  return VoidExprEvaluator(Info).Visit(E);
}
} // namespace

namespace {
__attribute__((hot, flatten)) bool evaluate(APValue &Result, EvalInfo &Info,
                                            const Expr *E) {
  QualType T = E->getType();
  if (E->isLValue() || T->isFunctionType()) {
    LValue LV;
    if (!evaluateLValue(E, LV, Info))
      return false;
    LV.moveInto(Result);
  } else if (LLVM_LIKELY(T->isIntegralOrEnumerationType())) {
    return IntExprEvaluator(Info, Result).Visit(E);
  } else if (T->isVectorType()) {
    if (!evaluateVector(E, Result, Info))
      return false;
  } else if (T->hasPointerRepresentation()) {
    LValue LV;
    if (!evaluatePointer(E, LV, Info))
      return false;
    LV.moveInto(Result);
  } else if (T->isRealFloatingType()) {
    llvm::APFloat F(0.0);
    if (!evaluateFloat(E, F, Info))
      return false;
    Result = APValue(F);
  } else if (T->isAnyComplexType()) {
    ComplexValue C;
    if (!evaluateComplex(E, C, Info))
      return false;
    C.moveInto(Result);
  } else if (T->isFixedPointType()) {
    if (!FixedPointExprEvaluator(Info, Result).Visit(E))
      return false;
  } else if (T->isArrayType()) {
    LValue LV;
    APValue &Value =
        Info.CurrentCall->createTemporary(E, T, ScopeKind::FullExpression, LV);
    if (!evaluateArray(E, LV, Value, Info))
      return false;
    Result = Value;
  } else if (T->isRecordType()) {
    LValue LV;
    APValue &Value =
        Info.CurrentCall->createTemporary(E, T, ScopeKind::FullExpression, LV);
    if (!evaluateRecord(E, LV, Value, Info))
      return false;
    Result = Value;
  } else if (T->isVoidType()) {
    Info.CCEDiag(E, diag::note_constexpr_nonliteral) << E->getType();
    if (!evaluateVoid(E, Info))
      return false;
  } else if (T->isAtomicType()) {
    QualType Unqual = T.getAtomicUnqualifiedType();
    if (Unqual->isArrayType() || Unqual->isRecordType()) {
      LValue LV;
      APValue &Value = Info.CurrentCall->createTemporary(
          E, Unqual, ScopeKind::FullExpression, LV);
      if (!evaluateAtomic(E, &LV, Value, Info))
        return false;
      Result = Value;
    } else {
      if (!evaluateAtomic(E, nullptr, Result, Info))
        return false;
    }
  } else {
    Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
    return false;
  }

  return true;
}
} // namespace

namespace {
bool evaluateInPlace(APValue &Result, EvalInfo &Info, const LValue &This,
                     const Expr *E, bool AllowNonLiteralTypes) {

  if (!AllowNonLiteralTypes && !checkLiteralType(Info, E, &This))
    return false;

  if (E->isPRValue()) {
    // evaluate arrays and record types in-place, so that later initializers can
    // refer to earlier-initialized members of the object.
    QualType T = E->getType();
    if (T->isArrayType())
      return evaluateArray(E, This, Result, Info);
    else if (T->isRecordType())
      return evaluateRecord(E, This, Result, Info);
    else if (T->isAtomicType()) {
      QualType Unqual = T.getAtomicUnqualifiedType();
      if (Unqual->isArrayType() || Unqual->isRecordType())
        return evaluateAtomic(E, &This, Result, Info);
    }
  }

  // For any other type, in-place evaluation is unimportant.
  return evaluate(Result, Info, E);
}
} // namespace

namespace {
bool evaluateAsRValue(EvalInfo &Info, const Expr *E, APValue &Result) {

  if (E->getType().isNull())
    return false;

  if (!checkLiteralType(Info, E))
    return false;

  {
    if (!::evaluate(Result, Info, E))
      return false;
  }

  // Implicit lvalue-to-rvalue cast.
  if (E->isLValue()) {
    LValue LV;
    LV.setFrom(Info.Ctx, Result);
    if (!handleLValueToRValueConversion(Info, E, E->getType(), LV, Result))
      return false;
  }

  // Check this core constant expression is a constant expression.
  return checkConstantExpression(Info, E->getExprLoc(), E->getType(), Result);
}
} // namespace

namespace {
bool fastEvaluateAsRValue(const Expr *Exp, Expr::EvalResult &Result,
                          const TreeContext &Ctx, bool &IsConst) {
  // Fast-path evaluations of integer literals, since we sometimes see files
  // containing vast quantities of these.
  if (const IntegerLiteral *L = dyn_cast<IntegerLiteral>(Exp)) {
    Result.Val =
        APValue(APSInt(L->getValue(), L->getType()->isUnsignedIntegerType()));
    IsConst = true;
    return true;
  }

  if (const auto *CE = dyn_cast<ConstantExpr>(Exp)) {
    if (CE->hasAPValueResult()) {
      Result.Val = CE->getAPValueResult();
      IsConst = true;
      return true;
    }

    // The SubExpr is usually just an IntegerLiteral.
    return fastEvaluateAsRValue(CE->getSubExpr(), Result, Ctx, IsConst);
  }

  // This case should be rare, but we need to check it before we check on
  // the type below.
  if (Exp->getType().isNull()) {
    IsConst = false;
    return true;
  }

  return false;
}
} // namespace

namespace {
bool hasUnacceptableSideEffect(Expr::EvalStatus &Result,
                               Expr::SideEffectsKind SEK) {
  return (SEK < Expr::SE_AllowSideEffects && Result.HasSideEffects) ||
         (SEK < Expr::SE_AllowUndefinedBehavior && Result.HasUndefinedBehavior);
}
} // namespace

namespace {
bool evaluateAsRValue(const Expr *E, Expr::EvalResult &Result,
                      const TreeContext &Ctx, EvalInfo &Info) {

  bool IsConst;
  if (fastEvaluateAsRValue(E, Result, Ctx, IsConst))
    return IsConst;

  return evaluateAsRValue(Info, E, Result.Val);
}
} // namespace

namespace {
bool evaluateAsInt(const Expr *E, Expr::EvalResult &ExprResult,
                   const TreeContext &Ctx,
                   Expr::SideEffectsKind AllowSideEffects, EvalInfo &Info) {

  if (!E->getType()->isIntegralOrEnumerationType())
    return false;

  if (!::evaluateAsRValue(E, ExprResult, Ctx, Info) ||
      !ExprResult.Val.isInt() ||
      hasUnacceptableSideEffect(ExprResult, AllowSideEffects))
    return false;

  return true;
}
} // namespace

namespace {
bool evaluateAsFixedPoint(const Expr *E, Expr::EvalResult &ExprResult,
                          const TreeContext &Ctx,
                          Expr::SideEffectsKind AllowSideEffects,
                          EvalInfo &Info) {

  if (!E->getType()->isFixedPointType())
    return false;

  if (!::evaluateAsRValue(E, ExprResult, Ctx, Info))
    return false;

  if (!ExprResult.Val.isFixedPoint() ||
      hasUnacceptableSideEffect(ExprResult, AllowSideEffects))
    return false;

  return true;
}
} // namespace

// ===----------------------------------------------------------------------===
// Public API (Expr::Evaluate*)
// ===----------------------------------------------------------------------===

__attribute__((hot)) bool Expr::EvaluateAsRValue(EvalResult &Result,
                                                 const TreeContext &Ctx,
                                                 bool InConstantContext) const {

  ExprTimeTraceScope TimeScope(this, Ctx, "evaluateAsRValue");
  EvalInfo Info(Ctx, Result, EvalInfo::EM_IgnoreSideEffects);
  Info.InConstantContext = InConstantContext;
  return ::evaluateAsRValue(this, Result, Ctx, Info);
}

bool Expr::EvaluateAsBooleanCondition(bool &Result, const TreeContext &Ctx,
                                      bool InConstantContext) const {

  ExprTimeTraceScope TimeScope(this, Ctx, "evaluateAsBooleanCondition");
  EvalResult Scratch;
  return EvaluateAsRValue(Scratch, Ctx, InConstantContext) &&
         handleConversionToBool(Scratch.Val, Result);
}

__attribute__((hot)) bool Expr::EvaluateAsInt(EvalResult &Result,
                                              const TreeContext &Ctx,
                                              SideEffectsKind AllowSideEffects,
                                              bool InConstantContext) const {

  ExprTimeTraceScope TimeScope(this, Ctx, "evaluateAsInt");
  EvalInfo Info(Ctx, Result, EvalInfo::EM_IgnoreSideEffects);
  Info.InConstantContext = InConstantContext;
  return ::evaluateAsInt(this, Result, Ctx, AllowSideEffects, Info);
}

bool Expr::EvaluateAsFixedPoint(EvalResult &Result, const TreeContext &Ctx,
                                SideEffectsKind AllowSideEffects,
                                bool InConstantContext) const {

  ExprTimeTraceScope TimeScope(this, Ctx, "evaluateAsFixedPoint");
  EvalInfo Info(Ctx, Result, EvalInfo::EM_IgnoreSideEffects);
  Info.InConstantContext = InConstantContext;
  return ::evaluateAsFixedPoint(this, Result, Ctx, AllowSideEffects, Info);
}

bool Expr::EvaluateAsFloat(APFloat &Result, const TreeContext &Ctx,
                           SideEffectsKind AllowSideEffects,
                           bool InConstantContext) const {

  if (!getType()->isRealFloatingType())
    return false;

  ExprTimeTraceScope TimeScope(this, Ctx, "evaluateAsFloat");
  EvalResult ExprResult;
  if (!EvaluateAsRValue(ExprResult, Ctx, InConstantContext) ||
      !ExprResult.Val.isFloat() ||
      hasUnacceptableSideEffect(ExprResult, AllowSideEffects))
    return false;

  Result = ExprResult.Val.getFloat();
  return true;
}

bool Expr::EvaluateAsLValue(EvalResult &Result, const TreeContext &Ctx,
                            bool InConstantContext) const {

  ExprTimeTraceScope TimeScope(this, Ctx, "evaluateAsLValue");
  EvalInfo Info(Ctx, Result, EvalInfo::EM_ConstantFold);
  Info.InConstantContext = InConstantContext;
  LValue LV;
  CheckedTemporaries CheckedTemps;
  if (!evaluateLValue(this, LV, Info) || !Info.discardCleanups() ||
      Result.HasSideEffects ||
      !checkLValueConstantExpression(Info, getExprLoc(), getType(), LV,
                                     CheckedTemps))
    return false;

  LV.moveInto(Result.Val);
  return true;
}

bool Expr::EvaluateAsConstantExpr(EvalResult &Result,
                                  const TreeContext &Ctx) const {

  bool IsConst;
  if (fastEvaluateAsRValue(this, Result, Ctx, IsConst) && Result.Val.hasValue())
    return true;

  ExprTimeTraceScope TimeScope(this, Ctx, "evaluateAsConstantExpr");
  EvalInfo::EvaluationMode EM = EvalInfo::EM_ConstantExpression;
  EvalInfo Info(Ctx, Result, EM);
  Info.InConstantContext = true;

  APValue::LValueBase Base(this);
  Info.setEvaluatingDecl(Base, Result.Val);

  {
    LValue LVal;
    LVal.set(Base);
    FullExpressionRAII Scope(Info);
    if (!::evaluateInPlace(Result.Val, Info, LVal, this) ||
        Result.HasSideEffects || !Scope.destroy())
      return false;

    if (!Info.discardCleanups())
      llvm_unreachable("Unhandled cleanup; missing full expression marker?");
  }

  return checkConstantExpression(Info, getExprLoc(), getStorageType(Ctx, this),
                                 Result.Val);
}

bool Expr::EvaluateAsInitializer(
    APValue &Value, const TreeContext &Ctx, const VarDecl *VD,
    llvm::SmallVectorImpl<PartialDiagnosticAt> &Notes,
    bool IsConstantInitialization) const {

  llvm::TimeTraceScope TimeScope("evaluateAsInitializer",
                                 [&]() -> llvm::SmallString<64> {
                                   llvm::SmallString<64> Name;
                                   llvm::raw_svector_ostream OS(Name);
                                   VD->printQualifiedName(OS);
                                   return Name;
                                 });

  Expr::EvalStatus EStatus;
  EStatus.Diag = &Notes;

  EvalInfo Info(Ctx, EStatus, EvalInfo::EM_ConstantFold);
  Info.setEvaluatingDecl(VD, Value);
  Info.InConstantContext = IsConstantInitialization;

  {
    LValue LVal;
    LVal.set(VD);

    if (!evaluateInPlace(Value, Info, LVal, this,
                         /*AllowNonLiteralTypes=*/true) ||
        EStatus.HasSideEffects)
      return false;

    Info.performLifetimeExtension();

    if (!Info.discardCleanups())
      llvm_unreachable("Unhandled cleanup; missing full expression marker?");
  }

  SourceLocation DeclLoc = VD->getLocation();
  QualType DeclTy = VD->getType();
  return checkConstantExpression(Info, DeclLoc, DeclTy, Value);
}

bool Expr::isEvaluatable(const TreeContext &Ctx, SideEffectsKind SEK) const {

  EvalResult Result;
  return EvaluateAsRValue(Result, Ctx, /* in constant context */ true) &&
         !hasUnacceptableSideEffect(Result, SEK);
}

APSInt Expr::EvaluateKnownConstInt(
    const TreeContext &Ctx,
    llvm::SmallVectorImpl<PartialDiagnosticAt> *Diag) const {

  ExprTimeTraceScope TimeScope(this, Ctx, "EvaluateKnownConstInt");
  EvalResult EVResult;
  EVResult.Diag = Diag;
  EvalInfo Info(Ctx, EVResult, EvalInfo::EM_IgnoreSideEffects);
  Info.InConstantContext = true;

  bool Result = ::evaluateAsRValue(this, EVResult, Ctx, Info);
  (void)Result;
  assert(Result && "Could not evaluate expression");
  assert(EVResult.Val.isInt() && "Expression did not evaluate to integer");

  return EVResult.Val.getInt();
}

APSInt Expr::EvaluateKnownConstIntCheckOverflow(
    const TreeContext &Ctx,
    llvm::SmallVectorImpl<PartialDiagnosticAt> *Diag) const {

  ExprTimeTraceScope TimeScope(this, Ctx, "EvaluateKnownConstIntCheckOverflow");
  EvalResult EVResult;
  EVResult.Diag = Diag;
  EvalInfo Info(Ctx, EVResult, EvalInfo::EM_IgnoreSideEffects);
  Info.InConstantContext = true;
  Info.CheckingForUndefinedBehavior = true;

  bool Result = ::evaluateAsRValue(Info, this, EVResult.Val);
  (void)Result;
  assert(Result && "Could not evaluate expression");
  assert(EVResult.Val.isInt() && "Expression did not evaluate to integer");

  return EVResult.Val.getInt();
}

void Expr::EvaluateForOverflow(const TreeContext &Ctx) const {

  ExprTimeTraceScope TimeScope(this, Ctx, "evaluateForOverflow");
  bool IsConst;
  EvalResult EVResult;
  if (!fastEvaluateAsRValue(this, EVResult, Ctx, IsConst)) {
    EvalInfo Info(Ctx, EVResult, EvalInfo::EM_IgnoreSideEffects);
    Info.CheckingForUndefinedBehavior = true;
    (void)::evaluateAsRValue(Info, this, EVResult.Val);
  }
}

bool Expr::EvalResult::isGlobalLValue() const {
  assert(Val.isLValue());
  return ::isGlobalLValue(Val.getLValueBase());
}

// checkICE - This function does the fundamental ICE checking: the returned
// ICEDiag contains an ICEKind indicating whether the expression is an ICE,
// and a (possibly null) SourceLocation indicating the location of the problem.
//
// Note that to reduce code duplication, this helper does no evaluation
// itself; the caller checks whether the expression is evaluatable, and
// in the rare cases where checkICE actually cares about the evaluated
// value, it calls into evaluate.

namespace {

enum ICEKind { IK_ICE, IK_ICEIfUnevaluated, IK_NotICE };

struct ICEDiag {
  ICEKind Kind;
  SourceLocation Loc;

  ICEDiag(ICEKind IK, SourceLocation l) : Kind(IK), Loc(l) {}
};

} // namespace

namespace {
ICEDiag noDiag() { return ICEDiag(IK_ICE, SourceLocation()); }
} // namespace

namespace {
ICEDiag worst(ICEDiag A, ICEDiag B) { return A.Kind >= B.Kind ? A : B; }
} // namespace

namespace {
ICEDiag checkEvalInICE(const Expr *E, const TreeContext &Ctx) {
  Expr::EvalResult EVResult;
  Expr::EvalStatus Status;
  EvalInfo Info(Ctx, Status, EvalInfo::EM_ConstantExpression);

  Info.InConstantContext = true;
  if (!::evaluateAsRValue(E, EVResult, Ctx, Info) || EVResult.HasSideEffects ||
      !EVResult.Val.isInt())
    return ICEDiag(IK_NotICE, E->getBeginLoc());

  return noDiag();
}
} // namespace

namespace {
ICEDiag checkICE(const Expr *E, const TreeContext &Ctx) {
  if (!E->getType()->isIntegralOrEnumerationType())
    return ICEDiag(IK_NotICE, E->getBeginLoc());

  switch (E->getStmtClass()) {
#define ABSTRACT_STMT(Node)
#define STMT(Node, Base) case Expr::Node##Class:
#define EXPR(Node, Base)
#include "neverc/Tree/StmtNodes.td.h"
  case Expr::PredefinedExprClass:
  case Expr::FloatingLiteralClass:
  case Expr::ImaginaryLiteralClass:
  case Expr::StringLiteralClass:
  case Expr::ArraySubscriptExprClass:
  case Expr::MatrixSubscriptExprClass:
  case Expr::MemberExprClass:
  case Expr::CompoundAssignOperatorClass:
  case Expr::CompoundLiteralExprClass:
  case Expr::ExtVectorElementExprClass:
  case Expr::DesignatedInitExprClass:
  case Expr::ArrayInitLoopExprClass:
  case Expr::ArrayInitIndexExprClass:
  case Expr::NoInitExprClass:
  case Expr::DesignatedInitUpdateExprClass:
  case Expr::ImplicitValueInitExprClass:
  case Expr::ParenListExprClass:
  case Expr::VAArgExprClass:
  case Expr::AddrLabelExprClass:
  case Expr::StmtExprClass:
  case Expr::NullPtrLiteralExprClass:
  case Expr::TypoExprClass:
  case Expr::RecoveryExprClass:
  case Expr::ExprWithCleanupsClass:
  case Expr::ShuffleVectorExprClass:
  case Expr::ConvertVectorExprClass:
  case Expr::NoStmtClass:
  case Expr::OpaqueValueExprClass:
  case Expr::PseudoObjectExprClass:
  case Expr::AtomicExprClass:
    return ICEDiag(IK_NotICE, E->getBeginLoc());

  case Expr::InitListExprClass: {
    // Scalar initializer list with one element is equivalent to a single init.
    if (E->isPRValue())
      if (cast<InitListExpr>(E)->getNumInits() == 1)
        return checkICE(cast<InitListExpr>(E)->getInit(0), Ctx);
    return ICEDiag(IK_NotICE, E->getBeginLoc());
  }

  case Expr::SourceLocExprClass:
    return noDiag();

  case Expr::ConstantExprClass:
    return checkICE(cast<ConstantExpr>(E)->getSubExpr(), Ctx);

  case Expr::ParenExprClass:
    return checkICE(cast<ParenExpr>(E)->getSubExpr(), Ctx);
  case Expr::GenericSelectionExprClass:
    return checkICE(cast<GenericSelectionExpr>(E)->getResultExpr(), Ctx);
  case Expr::IntegerLiteralClass:
  case Expr::FixedPointLiteralClass:
  case Expr::CharacterLiteralClass:
    return noDiag();
  case Expr::CallExprClass: {
    // Function calls in unevaluated subexpressions of
    // constant expressions, but they can never be ICEs because an ICE cannot
    // contain an operand of (pointer to) function type.
    const CallExpr *CE = cast<CallExpr>(E);
    if (CE->getBuiltinCallee())
      return checkEvalInICE(E, Ctx);
    return ICEDiag(IK_NotICE, E->getBeginLoc());
  }
  case Expr::DeclRefExprClass: {
    const NamedDecl *D = cast<DeclRefExpr>(E)->getDecl();
    if (isa<EnumConstantDecl>(D))
      return noDiag();

    return ICEDiag(IK_NotICE, E->getBeginLoc());
  }
  case Expr::UnaryOperatorClass: {
    const UnaryOperator *Exp = cast<UnaryOperator>(E);
    switch (Exp->getOpcode()) {
    case UO_PostInc:
    case UO_PostDec:
    case UO_PreInc:
    case UO_PreDec:
    case UO_AddrOf:
    case UO_Deref:
      // Increment/decrement in unevaluated
      // subexpressions of constant expressions, but they can never be ICEs
      // because an ICE cannot contain an lvalue operand.
      return ICEDiag(IK_NotICE, E->getBeginLoc());
    case UO_Extension:
    case UO_LNot:
    case UO_Plus:
    case UO_Minus:
    case UO_Not:
    case UO_Real:
    case UO_Imag:
      return checkICE(Exp->getSubExpr(), Ctx);
    }
    llvm_unreachable("invalid unary operator class");
  }
  case Expr::OffsetOfExprClass: {
    // Note that per C99, offsetof must be an ICE. And AFAIK, using
    // evaluateAsRValue matches the proposed gcc behavior for cases like
    // "offsetof(struct s{int x[4];}, x[1.0])".  This doesn't affect
    // compliance: we should warn earlier for offsetof expressions with
    // array subscripts that aren't ICEs, and if the array subscripts
    // are ICEs, the value of the offsetof must be an integer constant.
    return checkEvalInICE(E, Ctx);
  }
  case Expr::UnaryExprOrTypeTraitExprClass: {
    const UnaryExprOrTypeTraitExpr *Exp = cast<UnaryExprOrTypeTraitExpr>(E);
    if ((Exp->getKind() == UETT_SizeOf) &&
        Exp->getTypeOfArgument()->isVariableArrayType())
      return ICEDiag(IK_NotICE, E->getBeginLoc());
    return noDiag();
  }
  case Expr::BinaryOperatorClass: {
    const BinaryOperator *Exp = cast<BinaryOperator>(E);
    switch (Exp->getOpcode()) {
    case BO_Assign:
    case BO_MulAssign:
    case BO_DivAssign:
    case BO_RemAssign:
    case BO_AddAssign:
    case BO_SubAssign:
    case BO_ShlAssign:
    case BO_ShrAssign:
    case BO_AndAssign:
    case BO_XorAssign:
    case BO_OrAssign:
      // Assignments in unevaluated subexpressions of
      // constant expressions, but they can never be ICEs because an ICE cannot
      // contain an lvalue operand.
      return ICEDiag(IK_NotICE, E->getBeginLoc());

    case BO_Mul:
    case BO_Div:
    case BO_Rem:
    case BO_Add:
    case BO_Sub:
    case BO_Shl:
    case BO_Shr:
    case BO_LT:
    case BO_GT:
    case BO_LE:
    case BO_GE:
    case BO_EQ:
    case BO_NE:
    case BO_And:
    case BO_Xor:
    case BO_Or:
    case BO_Comma: {
      ICEDiag LHSResult = checkICE(Exp->getLHS(), Ctx);
      ICEDiag RHSResult = checkICE(Exp->getRHS(), Ctx);
      if (Exp->getOpcode() == BO_Div || Exp->getOpcode() == BO_Rem) {
        // evaluateAsRValue gives an error for undefined Div/Rem, so make sure
        // we don't evaluate one.
        if (LHSResult.Kind == IK_ICE && RHSResult.Kind == IK_ICE) {
          llvm::APSInt REval = Exp->getRHS()->EvaluateKnownConstInt(Ctx);
          if (REval == 0)
            return ICEDiag(IK_ICEIfUnevaluated, E->getBeginLoc());
          if (REval.isSigned() && REval.isAllOnes()) {
            llvm::APSInt LEval = Exp->getLHS()->EvaluateKnownConstInt(Ctx);
            if (LEval.isMinSignedValue())
              return ICEDiag(IK_ICEIfUnevaluated, E->getBeginLoc());
          }
        }
      }
      if (Exp->getOpcode() == BO_Comma) {
        if (Ctx.getLangOpts().C99) {
          // Edge case: comma can be in an ICE
          // if it isn't evaluated.
          if (LHSResult.Kind == IK_ICE && RHSResult.Kind == IK_ICE)
            return ICEDiag(IK_ICEIfUnevaluated, E->getBeginLoc());
        } else {
          // Pre-C99: comma cannot appear in an ICE.
          return ICEDiag(IK_NotICE, E->getBeginLoc());
        }
      }
      return worst(LHSResult, RHSResult);
    }
    case BO_LAnd:
    case BO_LOr: {
      ICEDiag LHSResult = checkICE(Exp->getLHS(), Ctx);
      ICEDiag RHSResult = checkICE(Exp->getRHS(), Ctx);
      if (LHSResult.Kind == IK_ICE && RHSResult.Kind == IK_ICEIfUnevaluated) {
        // Rare case where the RHS has a comma "side-effect"; we need
        // to actually check the condition to see whether the side
        // with the comma is evaluated.
        if ((Exp->getOpcode() == BO_LAnd) !=
            (Exp->getLHS()->EvaluateKnownConstInt(Ctx) == 0))
          return RHSResult;
        return noDiag();
      }

      return worst(LHSResult, RHSResult);
    }
    }
    llvm_unreachable("invalid binary operator kind");
  }
  case Expr::ImplicitCastExprClass:
  case Expr::CStyleCastExprClass: {
    const Expr *SubExpr = cast<CastExpr>(E)->getSubExpr();
    if (isa<ExplicitCastExpr>(E)) {
      if (const FloatingLiteral *FL =
              dyn_cast<FloatingLiteral>(SubExpr->IgnoreParenImpCasts())) {
        unsigned DestWidth = Ctx.getIntWidth(E->getType());
        bool DestSigned = E->getType()->isSignedIntegerOrEnumerationType();
        APSInt IgnoredVal(DestWidth, !DestSigned);
        bool Ignored;
        // If the value does not fit in the destination type, the behavior is
        // undefined, so we are not required to treat it as a constant
        // expression.
        if (FL->getValue().convertToInteger(
                IgnoredVal, llvm::APFloat::rmTowardZero, &Ignored) &
            APFloat::opInvalidOp)
          return ICEDiag(IK_NotICE, E->getBeginLoc());
        return noDiag();
      }
    }
    switch (cast<CastExpr>(E)->getCastKind()) {
    case CK_LValueToRValue:
    case CK_AtomicToNonAtomic:
    case CK_NonAtomicToAtomic:
    case CK_NoOp:
    case CK_IntegralToBoolean:
    case CK_IntegralCast:
      return checkICE(SubExpr, Ctx);
    default:
      return ICEDiag(IK_NotICE, E->getBeginLoc());
    }
  }
  case Expr::BinaryConditionalOperatorClass: {
    const BinaryConditionalOperator *Exp = cast<BinaryConditionalOperator>(E);
    ICEDiag CommonResult = checkICE(Exp->getCommon(), Ctx);
    if (CommonResult.Kind == IK_NotICE)
      return CommonResult;
    ICEDiag FalseResult = checkICE(Exp->getFalseExpr(), Ctx);
    if (FalseResult.Kind == IK_NotICE)
      return FalseResult;
    if (CommonResult.Kind == IK_ICEIfUnevaluated)
      return CommonResult;
    if (FalseResult.Kind == IK_ICEIfUnevaluated &&
        Exp->getCommon()->EvaluateKnownConstInt(Ctx) != 0)
      return noDiag();
    return FalseResult;
  }
  case Expr::ConditionalOperatorClass: {
    const ConditionalOperator *Exp = cast<ConditionalOperator>(E);
    // If the condition (ignoring parens) is a __builtin_constant_p call,
    // then only the true side is actually considered in an integer constant
    // expression, and it is fully evaluated.  This is an important GNU
    // extension.  See GCC PR38377 for discussion.
    if (const CallExpr *CallCE =
            dyn_cast<CallExpr>(Exp->getCond()->IgnoreParenCasts()))
      if (CallCE->getBuiltinCallee() == Builtin::BI__builtin_constant_p)
        return checkEvalInICE(E, Ctx);
    ICEDiag CondResult = checkICE(Exp->getCond(), Ctx);
    if (CondResult.Kind == IK_NotICE)
      return CondResult;

    ICEDiag TrueResult = checkICE(Exp->getTrueExpr(), Ctx);
    ICEDiag FalseResult = checkICE(Exp->getFalseExpr(), Ctx);

    if (TrueResult.Kind == IK_NotICE)
      return TrueResult;
    if (FalseResult.Kind == IK_NotICE)
      return FalseResult;
    if (CondResult.Kind == IK_ICEIfUnevaluated)
      return CondResult;
    if (TrueResult.Kind == IK_ICE && FalseResult.Kind == IK_ICE)
      return noDiag();
    // Rare case where the diagnostics depend on which side is evaluated
    // Note that if we get here, CondResult is 0, and at least one of
    // TrueResult and FalseResult is non-zero.
    if (Exp->getCond()->EvaluateKnownConstInt(Ctx) == 0)
      return FalseResult;
    return TrueResult;
  }
  case Expr::ChooseExprClass: {
    return checkICE(cast<ChooseExpr>(E)->getChosenSubExpr(), Ctx);
  }
  }

  llvm_unreachable("Invalid StmtClass!");
}
} // namespace

bool Expr::isIntegerConstantExpr(const TreeContext &Ctx,
                                 SourceLocation *Loc) const {

  ExprTimeTraceScope TimeScope(this, Ctx, "isIntegerConstantExpr");

  ICEDiag D = checkICE(this, Ctx);
  if (D.Kind != IK_ICE) {
    if (Loc)
      *Loc = D.Loc;
    return false;
  }
  return true;
}

std::optional<llvm::APSInt>
Expr::getIntegerConstantExpr(const TreeContext &Ctx,
                             SourceLocation *Loc) const {
  APSInt Value;

  if (!isIntegerConstantExpr(Ctx, Loc))
    return std::nullopt;

  // The only possible side-effects here are due to UB discovered in the
  // evaluation (for instance, INT_MAX + 1). In such a case, we are still
  // required to treat the expression as an ICE, so we produce the folded
  // value.
  EvalResult ExprResult;
  Expr::EvalStatus Status;
  EvalInfo Info(Ctx, Status, EvalInfo::EM_IgnoreSideEffects);
  Info.InConstantContext = true;

  if (!::evaluateAsInt(this, ExprResult, Ctx, SE_AllowSideEffects, Info))
    llvm_unreachable("ICE cannot be evaluated!");

  return ExprResult.Val.getInt();
}

bool Expr::EvaluateWithSubstitution(APValue &Value, TreeContext &Ctx,
                                    const FunctionDecl *Callee,
                                    llvm::ArrayRef<const Expr *> Args) const {

  llvm::TimeTraceScope TimeScope(
      "evaluateWithSubstitution", [&]() -> llvm::SmallString<64> {
        llvm::SmallString<64> Name;
        llvm::raw_svector_ostream OS(Name);
        Callee->getNameForDiagnostic(OS, Ctx.getPrintingPolicy(),
                                     /*Qualified=*/true);
        return Name;
      });

  Expr::EvalStatus Status;
  EvalInfo Info(Ctx, Status, EvalInfo::EM_ConstantExpressionUnevaluated);
  Info.InConstantContext = true;

  CallRef Call = Info.CurrentCall->createCall(Callee);
  unsigned NumParams = Callee->getNumParams();
  for (unsigned Idx = 0, N = Args.size(); Idx != N; ++Idx) {
    if (Idx >= NumParams)
      break;
    const ParmVarDecl *PVD = Callee->getParamDecl(Idx);
    if (!evaluateCallArg(PVD, Args[Idx], Call, Info) ||
        Info.EvalStatus.HasSideEffects) {
      if (APValue *Slot = Info.getParamSlot(Call, PVD))
        *Slot = APValue();
    }

    // Ignore any side-effects from a failed evaluation. This is safe because
    // they can't interfere with any other argument evaluation.
    Info.EvalStatus.HasSideEffects = false;
  }

  // Parameter cleanups happen in the caller and are not part of this
  // evaluation.
  Info.discardCleanups();
  Info.EvalStatus.HasSideEffects = false;

  // Build fake call to Callee.
  CallStackFrame Frame(Info, Callee->getLocation(), Callee,
                       /*CallExpr=*/nullptr, Call);
  FullExpressionRAII Scope(Info);
  return evaluate(Value, Info, this) && Scope.destroy() &&
         !Info.EvalStatus.HasSideEffects;
}

bool Expr::isPotentialConstantExprUnevaluated(
    Expr *E, const FunctionDecl *FD,
    llvm::SmallVectorImpl<PartialDiagnosticAt> &Diags) {
  Expr::EvalStatus Status;
  Status.Diag = &Diags;

  EvalInfo Info(FD->getTreeContext(), Status,
                EvalInfo::EM_ConstantExpressionUnevaluated);
  Info.InConstantContext = true;
  Info.CheckingPotentialConstantExpression = true;

  // Fabricate a call stack frame to give the arguments a plausible cover story.
  CallStackFrame Frame(Info, SourceLocation(), FD,
                       /*CallExpr=*/nullptr, CallRef());

  APValue ResultScratch;
  evaluate(ResultScratch, Info, E);
  return Diags.empty();
}

bool Expr::tryEvaluateObjectSize(uint64_t &Result, TreeContext &Ctx,
                                 unsigned Type) const {
  if (!getType()->isPointerType())
    return false;

  Expr::EvalStatus Status;
  EvalInfo Info(Ctx, Status, EvalInfo::EM_ConstantFold);
  return tryEvaluateBuiltinObjectSize(this, Type, Info, Result);
}

namespace {
bool evaluateBuiltinStrLen(const Expr *E, uint64_t &Result, EvalInfo &Info) {
  if (!E->getType()->hasPointerRepresentation() || !E->isPRValue())
    return false;

  LValue String;

  if (!evaluatePointer(E, String, Info))
    return false;

  QualType CharTy = E->getType()->getPointeeType();

  // Fast path: if it's a string literal, search the string value.
  if (const StringLiteral *S = dyn_cast_or_null<StringLiteral>(
          String.getLValueBase().dyn_cast<const Expr *>())) {
    llvm::StringRef Str = S->getBytes();
    int64_t Off = String.Offset.getQuantity();
    if (Off >= 0 && (uint64_t)Off <= (uint64_t)Str.size() &&
        S->getCharByteWidth() == 1 &&
        Info.Ctx.hasSameUnqualifiedType(CharTy, Info.Ctx.CharTy)) {
      Str = Str.substr(Off);

      llvm::StringRef::size_type Pos = Str.find(0);
      if (Pos != llvm::StringRef::npos)
        Str = Str.substr(0, Pos);

      Result = Str.size();
      return true;
    }

    // Fall through to slow path.
  }

  // Slow path: scan the bytes of the string looking for the terminating 0.
  for (uint64_t Strlen = 0; /**/; ++Strlen) {
    APValue Char;
    if (!handleLValueToRValueConversion(Info, E, CharTy, String, Char) ||
        !Char.isInt())
      return false;
    if (!Char.getInt()) {
      Result = Strlen;
      return true;
    }
    if (!handleLValueArrayAdjustment(Info, E, String, CharTy, 1))
      return false;
  }
}
} // namespace

bool Expr::EvaluateCharRangeAsString(std::string &Result,
                                     const Expr *SizeExpression,
                                     const Expr *PtrExpression,
                                     TreeContext &Ctx,
                                     EvalResult &Status) const {
  LValue String;
  EvalInfo Info(Ctx, Status, EvalInfo::EM_ConstantExpression);
  Info.InConstantContext = true;

  FullExpressionRAII Scope(Info);
  APSInt SizeValue;
  if (!::evaluateInteger(SizeExpression, SizeValue, Info))
    return false;

  int64_t Size = SizeValue.getExtValue();

  if (!::evaluatePointer(PtrExpression, String, Info))
    return false;

  QualType CharTy = PtrExpression->getType()->getPointeeType();
  for (int64_t I = 0; I < Size; ++I) {
    APValue Char;
    if (!handleLValueToRValueConversion(Info, PtrExpression, CharTy, String,
                                        Char))
      return false;

    APSInt C = Char.getInt();
    Result.push_back(static_cast<char>(C.getExtValue()));
    if (!handleLValueArrayAdjustment(Info, PtrExpression, String, CharTy, 1))
      return false;
  }
  return Scope.destroy();
}

bool Expr::tryEvaluateStrLen(uint64_t &Result, TreeContext &Ctx) const {
  Expr::EvalStatus Status;
  EvalInfo Info(Ctx, Status, EvalInfo::EM_ConstantFold);
  return evaluateBuiltinStrLen(this, Result, Info);
}
