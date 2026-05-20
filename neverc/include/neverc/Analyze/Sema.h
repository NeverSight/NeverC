#ifndef NEVERC_ANALYZE_SEMA_H
#define NEVERC_ANALYZE_SEMA_H

#include "neverc/Analyze/CleanupInfo.h"
#include "neverc/Analyze/DeclSpec.h"
#include "neverc/Analyze/IdentifierResolver.h"
#include "neverc/Analyze/Ownership.h"
#include "neverc/Analyze/Scope.h"
#include "neverc/Analyze/SemaNameLookupKinds.h"
#include "neverc/Analyze/TypoCorrection.h"
#include "neverc/Analyze/Weak.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/PragmaKinds.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Foundation/LangOpts/TypeTraits.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/PrettyPrinter.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Core/TreeFwd.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Tree/Type/LocInfoType.h"
#include "neverc/Tree/Type/TypeLoc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace llvm {
class APSInt;
template <typename ValueT, typename ValueInfoT> class DenseSet;
struct InlineAsmIdentifierInfo;
} // namespace llvm

namespace neverc {
class TreeConsumer;
class TreeContext;
class ArrayType;
class ParsedAttr;
class CallExpr;
class Decl;
class DeclContext;
class DeclRefExpr;
class DeclaratorDecl;
class Designation;
class EnumConstantDecl;
class Expr;
class FormatAttr;
class FunctionDecl;
class FunctionProtoType;
class ImplicitConversionSequence;
class InitializationSequence;
class InitializedEntity;
class LangOptions;
class LookupResult;
class NamedDecl;
class ParmVarDecl;
class PrepEngine;
class QualType;
class StandardConversionSequence;
class Stmt;
class StringLiteral;
class Token;
class TypedefDecl;
class TypedefNameDecl;
class TypeLoc;
class TypoCorrectionConsumer;
class UnqualifiedId;

class ValueDecl;
class VarDecl;
class VisibilityAttr;
class IndirectFieldDecl;

namespace sema {
class CompoundScopeInfo;
class DelayedDiagnostic;
class DelayedDiagnosticPool;
class FunctionScopeInfo;
class SemaPrepEngineObserver;
} // namespace sema

struct FileNullability {
  SourceLocation PointerLoc;

  SourceLocation PointerEndLoc;

  uint8_t PointerKind;

  bool SawTypeNullability = false;
};

class FileNullabilityMap {
  llvm::DenseMap<FileID, FileNullability> Map;

  struct {
    FileID File;
    FileNullability Nullability;
  } Cache;

public:
  FileNullability &operator[](FileID file) {
    // Check the single-element cache.
    if (file == Cache.File)
      return Cache.Nullability;

    // It's not in the single-element cache; flush the cache if we have one.
    if (!Cache.File.isInvalid()) {
      Map[Cache.File] = Cache.Nullability;
    }

    // Pull this entry into the cache.
    Cache.File = file;
    Cache.Nullability = Map[file];
    return Cache.Nullability;
  }
};

class Sema final {
  Sema(const Sema &) = delete;
  void operator=(const Sema &) = delete;

  static bool mightHaveNonExternalLinkage(const DeclaratorDecl *FD);

  bool shouldLinkPossiblyHiddenDecl(const NamedDecl *Old,
                                    const NamedDecl *New) {
    if (isVisible(Old))
      return true;
    // See comment in below overload for why it's safe to compute the linkage
    // of the new declaration here.
    if (New->isExternallyDeclarable()) {
      assert(Old->isExternallyDeclarable() &&
             "should not have found a non-externally-declarable previous decl");
      return true;
    }
    return false;
  }
  bool shouldLinkPossiblyHiddenDecl(LookupResult &Old, const NamedDecl *New);

public:
  using ResolveNameKind = ::neverc::ResolveNameKind;
  using RedeclarationKind = ::neverc::RedeclarationKind;

  static const unsigned MaxAlignmentExponent = 32;
  static const uint64_t MaximumAlignment = 1ull << MaxAlignmentExponent;

  typedef OpaquePtr<DeclGroupRef> DeclGroupPtrTy;

  FPOptions CurFPFeatures;

  const LangOptions &LangOpts;
  PrepEngine &PP;
  TreeContext &Context;
  TreeConsumer &Consumer;
  DiagnosticsEngine &Diags;
  SourceManager &SourceMgr;

  DeclContext *CurContext;

  DeclContext *OriginalLexicalContext;

  DeclarationName VAListTagName;

  bool MSStructPragmaOn; // True when \#pragma ms_struct on

  llvm::SmallVector<Scope *, 2> CurrentSEHFinally;

  llvm::SmallVector<TypoExpr *, 2> TypoExprs;

  enum PragmaSectionKind {
    PCSK_Invalid = 0,
    PCSK_BSS = 1,
    PCSK_Data = 2,
    PCSK_Rodata = 3,
    PCSK_Text = 4,
    PCSK_Relro = 5
  };

  enum PragmaSectionAction { PCSA_Set = 0, PCSA_Clear = 1 };

  struct PragmaSection {
    std::string SectionName;
    bool Valid = false;
    SourceLocation PragmaLocation;
  };

  PragmaSection PragmaBSSSection;
  PragmaSection PragmaDataSection;
  PragmaSection PragmaRodataSection;
  PragmaSection PragmaRelroSection;
  PragmaSection PragmaTextSection;

  enum PragmaMsStackAction {
    PSK_Reset = 0x0,                   // #pragma ()
    PSK_Set = 0x1,                     // #pragma (value)
    PSK_Push = 0x2,                    // #pragma (push[, id])
    PSK_Pop = 0x4,                     // #pragma (pop[, id])
    PSK_Show = 0x8,                    // #pragma (show) -- only for "pack"!
    PSK_Push_Set = PSK_Push | PSK_Set, // #pragma (push[, id], value)
    PSK_Pop_Set = PSK_Pop | PSK_Set,   // #pragma (pop[, id], value)
  };

  struct PragmaPackInfo {
    PragmaMsStackAction Action;
    llvm::StringRef SlotLabel;
    Token Alignment;
  };

  // #pragma pack and align.
  class AlignPackInfo {
  public:
    enum Mode : unsigned char { Native, Packed };

    AlignPackInfo(AlignPackInfo::Mode M, unsigned Num)
        : PackAttr(true), AlignMode(M), PackNumber(Num) {
      assert(Num == PackNumber && "The pack number has been truncated.");
    }

    AlignPackInfo(AlignPackInfo::Mode M)
        : PackAttr(false), AlignMode(M),
          PackNumber(M == Packed ? 1 : UninitPackVal) {}

    AlignPackInfo() : AlignPackInfo(Native) {}

    static uint32_t getRawEncoding(const AlignPackInfo &Info) {
      uint32_t Encoding = static_cast<uint32_t>(Info.getAlignMode());
      if (Info.IsPackAttr())
        Encoding |= PackAttrMask;
      Encoding |= static_cast<uint32_t>(Info.getPackNumber()) << 2;
      return Encoding;
    }

    static AlignPackInfo getFromRawEncoding(unsigned Encoding) {
      Mode M = static_cast<Mode>(Encoding & AlignModeMask);
      int PackNumber = (Encoding & PackNumMask) >> 2;
      if (Encoding & PackAttrMask)
        return AlignPackInfo(M, PackNumber);
      return AlignPackInfo(M);
    }

    bool IsPackAttr() const { return PackAttr; }

    Mode getAlignMode() const { return AlignMode; }

    unsigned getPackNumber() const { return PackNumber; }

    bool IsPackSet() const {
      return PackNumber != UninitPackVal && PackNumber != 0;
    }

    bool operator==(const AlignPackInfo &Info) const {
      return std::tie(AlignMode, PackNumber, PackAttr) ==
             std::tie(Info.AlignMode, Info.PackNumber, Info.PackAttr);
    }

    bool operator!=(const AlignPackInfo &Info) const {
      return !(*this == Info);
    }

  private:
    bool PackAttr;
    Mode AlignMode;
    unsigned char PackNumber;
    static constexpr unsigned char UninitPackVal = -1;

    static constexpr uint32_t AlignModeMask{0x01};
    static constexpr uint32_t PackAttrMask{0x02};
    static constexpr uint32_t PackNumMask{0x7C};
  };

  template <typename ValueType> struct PragmaStack {
    struct Slot {
      llvm::StringRef StackSlotLabel;
      ValueType Value;
      SourceLocation PragmaLocation;
      SourceLocation PragmaPushLocation;
      Slot(llvm::StringRef StackSlotLabel, ValueType Value,
           SourceLocation PragmaLocation, SourceLocation PragmaPushLocation)
          : StackSlotLabel(StackSlotLabel), Value(Value),
            PragmaLocation(PragmaLocation),
            PragmaPushLocation(PragmaPushLocation) {}
    };

    void Act(SourceLocation PragmaLocation, PragmaMsStackAction Action,
             llvm::StringRef StackSlotLabel, ValueType Value) {
      if (Action == PSK_Reset) {
        CurrentValue = DefaultValue;
        CurrentPragmaLocation = PragmaLocation;
        return;
      }
      if (Action & PSK_Push)
        Stack.emplace_back(StackSlotLabel, CurrentValue, CurrentPragmaLocation,
                           PragmaLocation);
      else if (Action & PSK_Pop) {
        if (!StackSlotLabel.empty()) {
          // If we've got a label, try to find it and jump there.
          auto I = llvm::find_if(llvm::reverse(Stack), [&](const Slot &x) {
            return x.StackSlotLabel == StackSlotLabel;
          });
          // If we found the label so pop from there.
          if (I != Stack.rend()) {
            CurrentValue = I->Value;
            CurrentPragmaLocation = I->PragmaLocation;
            Stack.erase(std::prev(I.base()), Stack.end());
          }
        } else if (!Stack.empty()) {
          // We do not have a label, just pop the last entry.
          CurrentValue = Stack.back().Value;
          CurrentPragmaLocation = Stack.back().PragmaLocation;
          Stack.pop_back();
        }
      }
      if (Action & PSK_Set) {
        CurrentValue = Value;
        CurrentPragmaLocation = PragmaLocation;
      }
    }

    // MSVC adds artificial slots to #pragma stacks when entering a function
    // body so stacks can be restored on exit, e.g.:
    //
    //   void f(void) {
    //     #pragma <name>(push, InternalPragmaSlot, <current_pragma_value>)
    //     ...
    //     #pragma <name>(pop, InternalPragmaSlot)
    //   }
    //
    // Push / pop a named sentinel slot.
    void SentinelAction(PragmaMsStackAction Action, llvm::StringRef Label) {
      assert((Action == PSK_Push || Action == PSK_Pop) &&
             "Can only push / pop #pragma stack sentinels!");
      Act(CurrentPragmaLocation, Action, Label, CurrentValue);
    }

    // Constructors.
    explicit PragmaStack(const ValueType &Default)
        : DefaultValue(Default), CurrentValue(Default) {}

    bool hasValue() const { return CurrentValue != DefaultValue; }

    llvm::SmallVector<Slot, 2> Stack;
    ValueType DefaultValue; // Value used for PSK_Reset action.
    ValueType CurrentValue;
    SourceLocation CurrentPragmaLocation;
  };
  PragmaStack<AlignPackInfo> AlignPackStack;
  // Segment #pragmas.
  PragmaStack<StringLiteral *> DataSegStack;
  PragmaStack<StringLiteral *> BSSSegStack;
  PragmaStack<StringLiteral *> ConstSegStack;
  PragmaStack<StringLiteral *> CodeSegStack;

  // #pragma strict_gs_check.
  PragmaStack<bool> StrictGuardStackCheckStack;

  // This stack tracks the current state of Sema.CurFPFeatures.
  PragmaStack<FPOptionsOverride> FpPragmaStack;
  FPOptionsOverride CurFPFeatureOverrides() {
    FPOptionsOverride result;
    if (!FpPragmaStack.hasValue()) {
      result = FPOptionsOverride();
    } else {
      result = FpPragmaStack.CurrentValue;
    }
    return result;
  }

  // Saves the current floating-point pragma stack and clear it in this Sema.
  class FpPragmaStackSaveRAII {
  public:
    FpPragmaStackSaveRAII(Sema &S)
        : S(S), SavedStack(std::move(S.FpPragmaStack)) {
      S.FpPragmaStack.Stack.clear();
    }
    ~FpPragmaStackSaveRAII() { S.FpPragmaStack = std::move(SavedStack); }

  private:
    Sema &S;
    PragmaStack<FPOptionsOverride> SavedStack;
  };

  void resetFPOptions(FPOptions FPO) {
    CurFPFeatures = FPO;
    FpPragmaStack.CurrentValue = FPO.getChangesFrom(FPOptions(LangOpts));
  }

  // RAII object to push / pop sentinel slots for all MS #pragma stacks when
  // entering or leaving a function body (MSVC-compatible stack discipline).
  class PragmaStackSentinelRAII {
  public:
    PragmaStackSentinelRAII(Sema &S, llvm::StringRef SlotLabel, bool ShouldAct);
    ~PragmaStackSentinelRAII();

  private:
    Sema &S;
    llvm::StringRef SlotLabel;
    bool ShouldAct;
  };

  FileNullabilityMap NullabilityMap;

  StringLiteral *CurInitSeg;
  SourceLocation CurInitSegLoc;

  llvm::StringMap<std::tuple<llvm::StringRef, SourceLocation>>
      FunctionToSectionMap;

  void *VisContext; // Really a "PragmaVisStack*"

  struct PragmaAttributeEntry {
    SourceLocation Loc;
    ParsedAttr *Attribute;
    llvm::SmallVector<attr::SubjectMatchRule, 4> MatchRules;
    bool IsUsed;
  };

  struct PragmaAttributeGroup {
    /// The location of the push attribute.
    SourceLocation Loc;
    /// The namespace of this push group.
    const IdentifierInfo *Namespace;
    llvm::SmallVector<PragmaAttributeEntry, 2> Entries;
  };

  llvm::SmallVector<PragmaAttributeGroup, 2> PragmaAttributeStack;

  const Decl *PragmaAttributeCurrentTargetDecl;

  SourceLocation OptimizeOffPragmaLocation;

  bool MSPragmaOptimizeIsOn = true;

  llvm::SmallSetVector<llvm::StringRef, 4> MSFunctionNoBuiltins;

  CleanupInfo Cleanup;

  llvm::SmallVector<ExprWithCleanups::CleanupObject, 8> ExprCleanupObjects;

  std::unique_ptr<sema::FunctionScopeInfo> CachedFunctionScope;

  llvm::SmallVector<sema::FunctionScopeInfo *, 4> FunctionScopes;

  unsigned FunctionScopesStart = 0;

  llvm::ArrayRef<sema::FunctionScopeInfo *> getFunctionScopes() const {
    return llvm::ArrayRef(FunctionScopes.begin() + FunctionScopesStart,
                          FunctionScopes.end());
  }

  typedef llvm::SmallVector<TypedefNameDecl *, 2> ExtVectorDeclsType;

  ExtVectorDeclsType ExtVectorDecls;

  using NamedDeclSetType = llvm::SmallSetVector<const NamedDecl *, 16>;

  NamedDeclSetType UnusedPrivateFields;

  llvm::SmallSetVector<const TypedefNameDecl *, 4>
      UnusedLocalTypedefNameCandidates;

  llvm::SmallPtrSet<const Decl *, 4> ParsingInitForAutoVars;

  NamedDecl *findLocallyScopedExternCDecl(DeclarationName Name);

  typedef llvm::SmallVector<VarDecl *, 2> TentativeDefinitionsType;

  TentativeDefinitionsType TentativeDefinitions;

  llvm::SmallVector<VarDecl *, 4> ExternalDeclarations;

  typedef llvm::SmallVector<const DeclaratorDecl *, 2>
      UnusedFileScopedDeclsType;

  UnusedFileScopedDeclsType UnusedFileScopedDecls;

  class DelayedDiagnostics;

  class DelayedDiagnosticsState {
    sema::DelayedDiagnosticPool *SavedPool = nullptr;
    friend class Sema::DelayedDiagnostics;
  };
  typedef DelayedDiagnosticsState ParsingDeclState;
  typedef DelayedDiagnosticsState ProcessingContextState;

  class DelayedDiagnostics {
    /// The current pool of diagnostics into which delayed
    /// diagnostics should go.
    sema::DelayedDiagnosticPool *CurPool = nullptr;

  public:
    DelayedDiagnostics() = default;

    /// Adds a delayed diagnostic.
    void add(const sema::DelayedDiagnostic &diag); // in DelayedDiagnostic.h

    /// Determines whether diagnostics should be delayed.
    bool shouldDelayDiagnostics() { return CurPool != nullptr; }

    /// Returns the current delayed-diagnostics pool.
    sema::DelayedDiagnosticPool *getCurrentPool() const { return CurPool; }

    /// Enter a new scope.  Access and deprecation diagnostics will be
    /// collected in this pool.
    DelayedDiagnosticsState push(sema::DelayedDiagnosticPool &pool) {
      DelayedDiagnosticsState state;
      state.SavedPool = CurPool;
      CurPool = &pool;
      return state;
    }

    /// Leave a delayed-diagnostic state that was previously pushed.
    /// Do not emit any of the diagnostics.  This is performed as part
    /// of the bookkeeping of popping a pool "properly".
    void popWithoutEmitting(DelayedDiagnosticsState state) {
      CurPool = state.SavedPool;
    }

    /// Enter a new scope where access and deprecation diagnostics are
    /// not delayed.
    DelayedDiagnosticsState pushUndelayed() {
      DelayedDiagnosticsState state;
      state.SavedPool = CurPool;
      CurPool = nullptr;
      return state;
    }

    /// Undo a previous pushUndelayed().
    void popUndelayed(DelayedDiagnosticsState state) {
      assert(CurPool == nullptr);
      CurPool = state.SavedPool;
    }
  } DelayedDiagnostics;

  class ContextRAII {
  private:
    Sema &S;
    DeclContext *SavedContext;
    ProcessingContextState SavedContextState;
    unsigned SavedFunctionScopesStart;

  public:
    ContextRAII(Sema &S, DeclContext *ContextToPush, bool NewThisContext = true)
        : S(S), SavedContext(S.CurContext),
          SavedContextState(S.DelayedDiagnostics.pushUndelayed()),
          SavedFunctionScopesStart(S.FunctionScopesStart) {
      assert(ContextToPush && "pushing null context");
      S.CurContext = ContextToPush;
      (void)NewThisContext;
      S.FunctionScopesStart = S.FunctionScopes.size();
    }

    void pop() {
      if (!SavedContext)
        return;
      S.CurContext = SavedContext;
      S.DelayedDiagnostics.popUndelayed(SavedContextState);
      S.FunctionScopesStart = SavedFunctionScopesStart;
      SavedContext = nullptr;
    }

    ~ContextRAII() { pop(); }
  };

  llvm::MapVector<
      IdentifierInfo *,
      llvm::SetVector<
          WeakInfo, llvm::SmallVector<WeakInfo, 1u>,
          llvm::SmallDenseSet<WeakInfo, 2u, WeakInfo::DenseMapInfoByAliasOnly>>>
      WeakUndeclaredIdentifiers;

  llvm::SmallVector<Decl *, 2> WeakTopLevelDecl;

  IdentifierResolver IdResolver;

  Scope *TUScope;

  enum class ExpressionEvaluationContext {
    /// Unevaluated operand (e.g. the operand of \c sizeof): type may matter,
    /// the value is not evaluated at run time.
    Unevaluated,

    /// The current expression occurs within an unevaluated
    /// operand that unconditionally permits abstract references to
    /// fields, such as a SIZE operator in MS-style inline assembly.
    UnevaluatedAbstract,

    /// Evaluated at translation time (e.g. case values in a \c switch).
    ConstantEvaluated,

    /// The current expression is potentially evaluated at run time,
    /// which means that code may be generated to evaluate the value of the
    /// expression at run time.
    PotentiallyEvaluated
  };

  struct ExpressionEvaluationContextRecord {
    /// The expression evaluation context.
    ExpressionEvaluationContext Context;

    /// Whether the enclosing context needed a cleanup.
    CleanupInfo ParentCleanup;

    /// The number of active cleanup objects when we entered
    /// this expression evaluation context.
    unsigned NumCleanupObjects;

    /// The number of typos encountered during this expression evaluation
    /// context (i.e. the number of TypoExprs created).
    unsigned NumTypos;

    llvm::SmallPtrSet<const Expr *, 8> PossibleDerefs;

    ExpressionEvaluationContextRecord(ExpressionEvaluationContext Context,
                                      unsigned NumCleanupObjects,
                                      CleanupInfo ParentCleanup)
        : Context(Context), ParentCleanup(ParentCleanup),
          NumCleanupObjects(NumCleanupObjects), NumTypos(0) {}

    bool isUnevaluated() const {
      return Context == ExpressionEvaluationContext::Unevaluated ||
             Context == ExpressionEvaluationContext::UnevaluatedAbstract;
    }

    bool isConstantEvaluated() const {
      return Context == ExpressionEvaluationContext::ConstantEvaluated;
    }
  };

  llvm::SmallVector<ExpressionEvaluationContextRecord, 8> ExprEvalContexts;

  void WarnOnPendingNoDerefs(ExpressionEvaluationContextRecord &Rec);

  mutable llvm::DenseMap<const EnumDecl *, llvm::APInt> FlagBitsCache;

  llvm::BumpPtrAllocator BumpAlloc;

  llvm::MapVector<NamedDecl *, SourceLocation> UndefinedButUsed;

  void getUndefinedButUsed(
      llvm::SmallVectorImpl<std::pair<NamedDecl *, SourceLocation>> &Undefined);

  llvm::SmallPtrSet<const NamedDecl *, 4> TypoCorrectedFunctionDefinitions;

  void GenCurrentDiagnostic();

  class FPFeaturesStateRAII {
  public:
    FPFeaturesStateRAII(Sema &S);
    ~FPFeaturesStateRAII();
    FPOptionsOverride getOverrides() { return OldOverrides; }

  private:
    Sema &S;
    FPOptions OldFPFeaturesState;
    FPOptionsOverride OldOverrides;
    LangOptions::FPEvalMethodKind OldEvalMethod;
    SourceLocation OldFPPragmaLocation;
  };

  void addImplicitTypedef(llvm::StringRef Name, QualType T);

  bool WarnedStackExhausted = false;

  bool TrackUnusedButSet = false;
  llvm::DenseMap<const VarDecl *, int> RefsMinusAssignments;

public:
  Sema(PrepEngine &pp, TreeContext &ctxt, TreeConsumer &consumer);
  ~Sema();

  void Initialize();

  virtual void anchor();

  const LangOptions &getLangOpts() const { return LangOpts; }
  FPOptions &getCurFPFeatures() { return CurFPFeatures; }

  DiagnosticsEngine &getDiagnostics() const { return Diags; }
  SourceManager &getSourceManager() const { return SourceMgr; }
  PrepEngine &getPrepEngine() const { return PP; }
  TreeContext &getTreeContext() const { return Context; }
  TreeConsumer &getTreeConsumer() const { return Consumer; }

  void warnStackExhausted(SourceLocation Loc);

  void runWithSufficientStackSpace(SourceLocation Loc,
                                   llvm::function_ref<void()> Fn);

  class ImmediateDiagBuilder : public DiagnosticBuilder {
    Sema &SemaRef;

  public:
    ImmediateDiagBuilder(DiagnosticBuilder &DB, Sema &SemaRef)
        : DiagnosticBuilder(DB), SemaRef(SemaRef) {}
    ImmediateDiagBuilder(DiagnosticBuilder &&DB, Sema &SemaRef)
        : DiagnosticBuilder(DB), SemaRef(SemaRef) {}

    // This is a cunning lie. DiagnosticBuilder actually performs move
    // construction in its copy constructor (but due to varied uses, it's not
    // possible to conveniently express this as actual move construction). So
    // the default copy ctor here is fine, because the base class disables the
    // source anyway, so the user-defined ~ImmediateDiagBuilder is a safe no-op
    // in that case anwyay.
    ImmediateDiagBuilder(const ImmediateDiagBuilder &) = default;

    ~ImmediateDiagBuilder() {
      // If we aren't active, there is nothing to do.
      if (!isActive())
        return;

      // Otherwise, we need to emit the diagnostic. First clear the diagnostic
      // builder itself so it won't emit the diagnostic in its own destructor.
      //
      // This seems wasteful, in that as written the DiagnosticBuilder dtor will
      // do its own needless checks to see if the diagnostic needs to be
      // emitted. However, because we take care to ensure that the builder
      // objects never escape, a sufficiently smart compiler will be able to
      // eliminate that code.
      Clear();

      // Dispatch to Sema to emit the diagnostic.
      SemaRef.GenCurrentDiagnostic();
    }

    /// Teach operator<< to produce an object of the correct type.
    template <typename T>
    friend const ImmediateDiagBuilder &
    operator<<(const ImmediateDiagBuilder &Diag, const T &Value) {
      const DiagnosticBuilder &BaseDiag = Diag;
      BaseDiag << Value;
      return Diag;
    }

    // It is necessary to limit this to rvalue reference to avoid calling this
    // function with a bitfield lvalue argument since non-const reference to
    // bitfield is not allowed.
    template <typename T,
              typename = std::enable_if_t<!std::is_lvalue_reference<T>::value>>
    const ImmediateDiagBuilder &operator<<(T &&V) const {
      const DiagnosticBuilder &BaseDiag = *this;
      BaseDiag << std::move(V);
      return *this;
    }
  };

  class SemaDiagnosticBuilder {
  public:
    SemaDiagnosticBuilder(SourceLocation Loc, unsigned DiagID,
                          const FunctionDecl *Fn, Sema &S);
    SemaDiagnosticBuilder(SemaDiagnosticBuilder &&D);
    SemaDiagnosticBuilder(const SemaDiagnosticBuilder &) = default;

    SemaDiagnosticBuilder &operator=(const SemaDiagnosticBuilder &) = delete;
    SemaDiagnosticBuilder &operator=(SemaDiagnosticBuilder &&) = delete;

    ~SemaDiagnosticBuilder();

    bool isImmediate() const { return ImmediateDiag.has_value(); }

    operator bool() const { return isImmediate(); }

    template <typename T>
    friend const SemaDiagnosticBuilder &
    operator<<(const SemaDiagnosticBuilder &Diag, const T &Value) {
      if (Diag.ImmediateDiag)
        *Diag.ImmediateDiag << Value;
      return Diag;
    }

    template <typename T,
              typename = std::enable_if_t<!std::is_lvalue_reference<T>::value>>
    const SemaDiagnosticBuilder &operator<<(T &&V) const {
      if (ImmediateDiag)
        *ImmediateDiag << std::move(V);
      return *this;
    }

    friend const SemaDiagnosticBuilder &
    operator<<(const SemaDiagnosticBuilder &Diag, const PartialDiagnostic &PD) {
      if (Diag.ImmediateDiag)
        PD.Emit(*Diag.ImmediateDiag);
      return Diag;
    }

    void AddFixItHint(const FixItHint &Hint) const {
      if (ImmediateDiag)
        ImmediateDiag->AddFixItHint(Hint);
    }

    friend ExprResult ExprError(const SemaDiagnosticBuilder &) {
      return ExprError();
    }
    friend StmtResult StmtError(const SemaDiagnosticBuilder &) {
      return StmtError();
    }
    operator ExprResult() const { return ExprError(); }
    operator StmtResult() const { return StmtError(); }
    operator TypeResult() const { return TypeError(); }
    operator DeclResult() const { return DeclResult(true); }

  private:
    Sema &S;
    SourceLocation Loc;
    unsigned DiagID;
    const FunctionDecl *Fn;
    std::optional<ImmediateDiagBuilder> ImmediateDiag;
  };

  bool IsLastErrorImmediate = true;

  SemaDiagnosticBuilder Diag(SourceLocation Loc, unsigned DiagID);

  SemaDiagnosticBuilder Diag(SourceLocation Loc, const PartialDiagnostic &PD);

  PartialDiagnostic PDiag(unsigned DiagID = 0); // in SemaInternal.h

  bool DeferDiags = false;

  class DeferDiagsRAII {
    Sema &S;
    bool SavedDeferDiags = false;

  public:
    DeferDiagsRAII(Sema &S, bool DeferDiags)
        : S(S), SavedDeferDiags(S.DeferDiags) {
      S.DeferDiags = DeferDiags;
    }
    ~DeferDiagsRAII() { S.DeferDiags = SavedDeferDiags; }
  };

  bool hasUncompilableErrorOccurred() const;

  bool locateMacroSpelling(SourceLocation &loc, llvm::StringRef name);

  std::string getFixItZeroInitializerForType(QualType T,
                                             SourceLocation Loc) const;
  std::string getFixItZeroLiteralForType(QualType T, SourceLocation Loc) const;

  SourceLocation getLocForEndOfToken(SourceLocation Loc, unsigned Offset = 0);

  void FlushUnusedTypedefWarnings();

public:
  void OnEndOfTranslationUnit();

  Scope *getScopeForContext(DeclContext *Ctx);

  void PushFunctionScope();

  class PoppedFunctionScopeDeleter {
    Sema *Self;

  public:
    explicit PoppedFunctionScopeDeleter(Sema *Self) : Self(Self) {}
    void operator()(sema::FunctionScopeInfo *Scope) const;
  };

  using PoppedFunctionScopePtr =
      std::unique_ptr<sema::FunctionScopeInfo, PoppedFunctionScopeDeleter>;

  PoppedFunctionScopePtr PopFunctionScopeInfo();

  sema::FunctionScopeInfo *getCurFunction() const {
    return FunctionScopes.empty() ? nullptr : FunctionScopes.back();
  }

  void setFunctionHasBranchIntoScope();
  void setFunctionHasBranchProtectedScope();
  void setFunctionHasIndirectGoto();
  void setFunctionHasMustTail();

  void PushCompoundScope(bool IsStmtExpr);
  void PopCompoundScope();

  sema::CompoundScopeInfo &getCurCompoundScope() const;

  bool hasAnyUnrecoverableErrorsInThisFunction() const;

  sema::FunctionScopeInfo *getCurFunctionAvailabilityContext();

  llvm::SmallVectorImpl<Decl *> &WeakTopLevelDecls() {
    return WeakTopLevelDecl;
  }

  //===--------------------------------------------------------------------===//
  // Type Analysis / Processing: SemaType.cpp.
  //

  QualType FormQualifiedType(QualType T, SourceLocation Loc, Qualifiers Qs,
                             const DeclSpec *DS = nullptr);
  QualType FormQualifiedType(QualType T, SourceLocation Loc, unsigned CVRA,
                             const DeclSpec *DS = nullptr);
  QualType FormPointerType(QualType T, SourceLocation Loc,
                           DeclarationName Entity);
  QualType FormArrayType(QualType T, ArraySizeModifier ASM, Expr *ArraySize,
                         unsigned Quals, SourceRange Brackets,
                         DeclarationName Entity);
  QualType FormVectorType(QualType T, Expr *VecSize, SourceLocation AttrLoc);
  QualType FormExtVectorType(QualType T, Expr *ArraySize,
                             SourceLocation AttrLoc);

  QualType FormAddressSpaceAttr(QualType &T, LangAS ASIdx, Expr *AddrSpace,
                                SourceLocation AttrLoc);

  QualType FormAddressSpaceAttr(QualType &T, Expr *AddrSpace,
                                SourceLocation AttrLoc);

  CodeAlignAttr *FormCodeAlignAttr(const AttributeCommonInfo &CI, Expr *E);
  bool CheckRebuiltStmtAttributes(llvm::ArrayRef<const Attr *> Attrs);

  bool CheckFunctionReturnType(QualType T, SourceLocation Loc);

  QualType FormFunctionType(QualType T,
                            llvm::MutableArrayRef<QualType> ParamTypes,
                            SourceLocation Loc, DeclarationName Entity,
                            const FunctionProtoType::ExtProtoInfo &EPI);

  QualType FormParenType(QualType T);
  QualType FormAtomicType(QualType T, SourceLocation Loc);
  QualType FormBitIntType(bool IsUnsigned, Expr *BitWidth, SourceLocation Loc);

  TypeSourceInfo *ResolveDeclaratorType(Declarator &D, Scope *S);
  TypeSourceInfo *ResolveDeclaratorTypeCast(Declarator &D, QualType FromTy);

  ParsedType CreateParsedType(QualType T, TypeSourceInfo *TInfo);
  DeclarationNameInfo GetNameForDeclarator(Declarator &D);
  DeclarationNameInfo GetNameFromUnqualifiedId(const UnqualifiedId &Name);
  static QualType GetTypeFromParser(ParsedType Ty,
                                    TypeSourceInfo **TInfo = nullptr);
  TypeResult OnTypeName(Scope *S, Declarator &D);

  struct TypeDiagnoser {
    TypeDiagnoser() {}

    virtual void diagnose(Sema &S, SourceLocation Loc, QualType T) = 0;
    virtual ~TypeDiagnoser() {}
  };

  static int getPrintable(int I) { return I; }
  static unsigned getPrintable(unsigned I) { return I; }
  static bool getPrintable(bool B) { return B; }
  static const char *getPrintable(const char *S) { return S; }
  static llvm::StringRef getPrintable(llvm::StringRef S) { return S; }
  static const std::string &getPrintable(const std::string &S) { return S; }
  static const IdentifierInfo *getPrintable(const IdentifierInfo *II) {
    return II;
  }
  static DeclarationName getPrintable(DeclarationName N) { return N; }
  static QualType getPrintable(QualType T) { return T; }
  static SourceRange getPrintable(SourceRange R) { return R; }
  static SourceRange getPrintable(SourceLocation L) { return L; }
  static SourceRange getPrintable(const Expr *E) { return E->getSourceRange(); }
  static SourceRange getPrintable(TypeLoc TL) { return TL.getSourceRange(); }

  template <typename... Ts> class BoundTypeDiagnoser : public TypeDiagnoser {
  protected:
    unsigned DiagID;
    std::tuple<const Ts &...> Args;

    template <std::size_t... Is>
    void emit(const SemaDiagnosticBuilder &DB,
              std::index_sequence<Is...>) const {
      // Apply all tuple elements to the builder in order.
      bool Dummy[] = {false, (DB << getPrintable(std::get<Is>(Args)))...};
      (void)Dummy;
    }

  public:
    BoundTypeDiagnoser(unsigned DiagID, const Ts &...Args)
        : TypeDiagnoser(), DiagID(DiagID), Args(Args...) {
      assert(DiagID != 0 && "no diagnostic for type diagnoser");
    }

    void diagnose(Sema &S, SourceLocation Loc, QualType T) override {
      const SemaDiagnosticBuilder &DB = S.Diag(Loc, DiagID);
      emit(DB, std::index_sequence_for<Ts...>());
      DB << T;
    }
  };

  template <typename... Ts>
  class SizelessTypeDiagnoser : public BoundTypeDiagnoser<Ts...> {
  public:
    SizelessTypeDiagnoser(unsigned DiagID, const Ts &...Args)
        : BoundTypeDiagnoser<Ts...>(DiagID, Args...) {}

    void diagnose(Sema &S, SourceLocation Loc, QualType T) override {
      const SemaDiagnosticBuilder &DB = S.Diag(Loc, this->DiagID);
      this->emit(DB, std::index_sequence_for<Ts...>());
      DB << T->isSizelessType() << T;
    }
  };

  enum class CompleteTypeKind {
    /// Apply the normal rules for complete types.  In particular,
    /// treat all sizeless types as incomplete.
    Normal,

    /// Relax the normal rules for complete types so that they include
    /// sizeless built-in types.
    AcceptSizeless,

    Default = AcceptSizeless
  };

private:
  void CheckSubscriptAccessOfNoDeref(const ArraySubscriptExpr *E);
  void CheckAddressOfNoDeref(const Expr *E);
  void CheckMemberAccessOfNoDeref(const MemberExpr *E);

  bool RequireCompleteTypeImpl(SourceLocation Loc, QualType T,
                               CompleteTypeKind Kind, TypeDiagnoser *Diagnoser);

public:
  bool isVisible(const NamedDecl *D) { return true; }

  // Check whether the size of array element of type \p EltTy is a multiple of
  // its alignment and return false if it isn't.
  bool checkArrayElementAlignment(QualType EltTy, SourceLocation Loc);

  bool isCompleteType(SourceLocation Loc, QualType T,
                      CompleteTypeKind Kind = CompleteTypeKind::Default) {
    return !RequireCompleteTypeImpl(Loc, T, Kind, nullptr);
  }
  bool RequireCompleteType(SourceLocation Loc, QualType T,
                           CompleteTypeKind Kind, TypeDiagnoser &Diagnoser);
  bool RequireCompleteType(SourceLocation Loc, QualType T,
                           CompleteTypeKind Kind, unsigned DiagID);

  bool RequireCompleteType(SourceLocation Loc, QualType T,
                           TypeDiagnoser &Diagnoser) {
    return RequireCompleteType(Loc, T, CompleteTypeKind::Default, Diagnoser);
  }
  bool RequireCompleteType(SourceLocation Loc, QualType T, unsigned DiagID) {
    return RequireCompleteType(Loc, T, CompleteTypeKind::Default, DiagID);
  }

  template <typename... Ts>
  bool RequireCompleteType(SourceLocation Loc, QualType T, unsigned DiagID,
                           const Ts &...Args) {
    BoundTypeDiagnoser<Ts...> Diagnoser(DiagID, Args...);
    return RequireCompleteType(Loc, T, Diagnoser);
  }

  template <typename... Ts>
  bool RequireCompleteSizedType(SourceLocation Loc, QualType T, unsigned DiagID,
                                const Ts &...Args) {
    SizelessTypeDiagnoser<Ts...> Diagnoser(DiagID, Args...);
    return RequireCompleteType(Loc, T, CompleteTypeKind::Normal, Diagnoser);
  }

  bool RequireCompleteExprType(Expr *E, CompleteTypeKind Kind,
                               TypeDiagnoser &Diagnoser);
  bool RequireCompleteExprType(Expr *E, unsigned DiagID);

  template <typename... Ts>
  bool RequireCompleteExprType(Expr *E, unsigned DiagID, const Ts &...Args) {
    BoundTypeDiagnoser<Ts...> Diagnoser(DiagID, Args...);
    return RequireCompleteExprType(E, CompleteTypeKind::Default, Diagnoser);
  }

  template <typename... Ts>
  bool RequireCompleteSizedExprType(Expr *E, unsigned DiagID,
                                    const Ts &...Args) {
    SizelessTypeDiagnoser<Ts...> Diagnoser(DiagID, Args...);
    return RequireCompleteExprType(E, CompleteTypeKind::Normal, Diagnoser);
  }

  bool RequireLiteralType(SourceLocation Loc, QualType T,
                          TypeDiagnoser &Diagnoser);
  bool RequireLiteralType(SourceLocation Loc, QualType T, unsigned DiagID);

  template <typename... Ts>
  bool RequireLiteralType(SourceLocation Loc, QualType T, unsigned DiagID,
                          const Ts &...Args) {
    BoundTypeDiagnoser<Ts...> Diagnoser(DiagID, Args...);
    return RequireLiteralType(Loc, T, Diagnoser);
  }

  QualType getElaboratedType(ElaboratedTypeKeyword Keyword, QualType T,
                             TagDecl *OwnedTagDecl = nullptr);

  QualType FormTypeofExprType(Expr *E, TypeOfKind Kind);

  //===--------------------------------------------------------------------===//
  // Symbol table / Decl tracking callbacks: SemaDecl.cpp.
  //

  struct SkipBodyInfo {
    SkipBodyInfo() = default;
    bool ShouldSkip = false;
    bool CheckSameAsPrevious = false;
    NamedDecl *Previous = nullptr;
    NamedDecl *New = nullptr;
  };

  DeclGroupPtrTy WrapDeclAsGroup(Decl *Ptr, Decl *OwnedType = nullptr);

  ParsedType getTypeName(const IdentifierInfo &II, SourceLocation NameLoc,
                         Scope *S, bool HasTrailingDot = false,
                         bool WantNontrivialTypeSourceInfo = false,
                         IdentifierInfo **CorrectedII = nullptr);
  TypeSpecifierType isTagName(IdentifierInfo &II, Scope *S);
  void DiagnoseUnknownTypeName(IdentifierInfo *&II, SourceLocation IILoc,
                               Scope *S, ParsedType &SuggestedType);

  enum NameClassificationKind {
    /// This name is not a type in this context, but might be something else.
    NC_Unknown,
    /// Classification failed; an error has been produced.
    NC_Error,
    /// The name has been typo-corrected to a keyword.
    NC_Keyword,
    /// The name was classified as a type.
    NC_Type,
    /// The name was classified as a specific non-type declaration.
    NC_NonType,
  };

  class NameClassification {
    NameClassificationKind Kind;
    union {
      NamedDecl *NonTypeDecl;
      ParsedType Type;
    };

    explicit NameClassification(NameClassificationKind Kind) : Kind(Kind) {}

  public:
    NameClassification(ParsedType Type) : Kind(NC_Type), Type(Type) {}

    NameClassification(const IdentifierInfo *Keyword) : Kind(NC_Keyword) {}

    static NameClassification Error() { return NameClassification(NC_Error); }

    static NameClassification Unknown() {
      return NameClassification(NC_Unknown);
    }

    static NameClassification NonType(NamedDecl *D) {
      NameClassification Result(NC_NonType);
      Result.NonTypeDecl = D;
      return Result;
    }

    NameClassificationKind getKind() const { return Kind; }

    ParsedType getType() const {
      assert(Kind == NC_Type);
      return Type;
    }

    NamedDecl *getNonTypeDecl() const {
      assert(Kind == NC_NonType);
      return NonTypeDecl;
    }
  };

  NameClassification ClassifyName(Scope *S, IdentifierInfo *&Name,
                                  SourceLocation NameLoc,
                                  const Token &NextToken,
                                  CorrectionCandidateCallback *CCC = nullptr);

  void warnOnReservedIdentifier(const NamedDecl *D);

  NamedDecl *OnDeclarator(Scope *S, Declarator &D);
  bool tryToFixVariablyModifiedVarType(TypeSourceInfo *&TInfo, QualType &T,
                                       SourceLocation Loc,
                                       unsigned FailedFoldDiagID);
  void RegisterLocallyScopedExternCDecl(NamedDecl *ND, Scope *S);
  void
  diagnoseIgnoredQualifiers(unsigned DiagID, unsigned Quals,
                            SourceLocation FallbackLoc,
                            SourceLocation ConstQualLoc = SourceLocation(),
                            SourceLocation VolatileQualLoc = SourceLocation(),
                            SourceLocation RestrictQualLoc = SourceLocation(),
                            SourceLocation AtomicQualLoc = SourceLocation(),
                            SourceLocation UnalignedQualLoc = SourceLocation());

  static bool adjustContextForLocalExternDecl(DeclContext *&DC);
  NamedDecl *getShadowedDeclaration(const TypedefNameDecl *D,
                                    const LookupResult &R);
  NamedDecl *getShadowedDeclaration(const VarDecl *D, const LookupResult &R);
  void CheckShadow(NamedDecl *D, NamedDecl *ShadowedDecl,
                   const LookupResult &R);
  void CheckShadow(Scope *S, VarDecl *D);

public:
  void CheckCastAlign(Expr *Op, QualType T, SourceRange TRange);
  void setTagNameForLinkagePurposes(TagDecl *TagFromDeclSpec,
                                    TypedefNameDecl *NewTD);
  void CheckTypedefForVariablyModifiedType(Scope *S, TypedefNameDecl *D);
  NamedDecl *OnTypedefDeclarator(Scope *S, Declarator &D, DeclContext *DC,
                                 TypeSourceInfo *TInfo, LookupResult &Previous);
  NamedDecl *OnTypedefNameDecl(Scope *S, DeclContext *DC, TypedefNameDecl *D,
                               LookupResult &Previous, bool &Redeclaration);
  NamedDecl *OnVariableDeclarator(Scope *S, Declarator &D, DeclContext *DC,
                                  TypeSourceInfo *TInfo, LookupResult &Previous,
                                  bool &AddToScope);
  // Returns true if the variable declaration is a redeclaration
  bool CheckVariableDeclaration(VarDecl *NewVD, LookupResult &Previous);
  void CheckVariableDeclarationType(VarDecl *NewVD);
  bool DeduceVariableDeclarationType(VarDecl *VDecl, bool DirectInit,
                                     Expr *Init);
  void CheckCompleteVariableDeclaration(VarDecl *VD);
  void MaybeSuggestAddingStaticToDecl(const FunctionDecl *D);

  NamedDecl *OnFunctionDeclarator(Scope *S, Declarator &D, DeclContext *DC,
                                  TypeSourceInfo *TInfo, LookupResult &Previous,
                                  bool &AddToScope);

  // Returns true if the function declaration is a redeclaration
  bool CheckFunctionDeclaration(Scope *S, FunctionDecl *NewFD,
                                LookupResult &Previous, bool DeclIsDefn);
  void CheckMain(FunctionDecl *FD, const DeclSpec &D);
  void CheckMSVCRTEntryPoint(FunctionDecl *FD);
  Attr *getImplicitCodeSegOrSectionAttrForFunction(const FunctionDecl *FD,
                                                   bool IsDefinition);
  Decl *OnParamDeclarator(Scope *S, Declarator &D);
  ParmVarDecl *FormParmVarDeclForTypedef(DeclContext *DC, SourceLocation Loc,
                                         QualType T);
  ParmVarDecl *CheckParameter(DeclContext *DC, SourceLocation StartLoc,
                              SourceLocation NameLoc, IdentifierInfo *Name,
                              QualType T, TypeSourceInfo *TSInfo,
                              StorageClass SC);
  void AttachInitializerToDecl(Decl *dcl, Expr *init, bool DirectInit);
  void OnUninitializedDecl(Decl *dcl);
  void OnInitializerError(Decl *Dcl);

  void CheckStaticLocalForDllExport(VarDecl *VD);
  void CheckThreadLocalForLargeAlignment(VarDecl *VD);
  void FinalizeDeclaration(Decl *D);
  DeclGroupPtrTy FinalizeDeclaratorGroup(Scope *S, const DeclSpec &DS,
                                         llvm::ArrayRef<Decl *> Group);
  DeclGroupPtrTy FormDeclaratorGroup(llvm::MutableArrayRef<Decl *> Group);

  void OnFinishKNRParamDeclarations(Scope *S, Declarator &D,
                                    SourceLocation LocAfterDecls);
  void CheckForFunctionRedefinition(
      FunctionDecl *FD, const FunctionDecl *EffectiveDefinition = nullptr,
      SkipBodyInfo *SkipBody = nullptr);
  Decl *OnStartOfFunctionDef(Scope *S, Declarator &D,
                             SkipBodyInfo *SkipBody = nullptr);
  Decl *OnStartOfFunctionDef(Scope *S, Decl *D,
                             SkipBodyInfo *SkipBody = nullptr);

  void computeNRVO(Stmt *Body, sema::FunctionScopeInfo *Scope);
  Decl *OnFinishFunctionBody(Decl *Decl, Stmt *Body);

  void OnFinishDelayedAttribute(Scope *S, Decl *D, ParsedAttributes &Attrs);

  void DiagnoseUnusedParameters(llvm::ArrayRef<ParmVarDecl *> Parameters);

  void DiagnoseSizeOfParametersAndReturnValue(
      llvm::ArrayRef<ParmVarDecl *> Parameters, QualType ReturnTy,
      NamedDecl *D);

  void DiagnoseInvalidJumps(Stmt *Body);
  Decl *OnFileScopeAsmDecl(Expr *expr, SourceLocation AsmLoc,
                           SourceLocation RParenLoc);

  Decl *OnEmptyDeclaration(Scope *S, const ParsedAttributesView &AttrList,
                           SourceLocation SemiLoc);

  enum class MissingImportKind { Declaration, Definition };

  PrintingPolicy getPrintingPolicy() const {
    return getPrintingPolicy(Context, PP);
  }

  const PrintingPolicy &getCachedPrintingPolicy() const {
    return Context.getPrintingPolicy();
  }

  static PrintingPolicy getPrintingPolicy(const TreeContext &Ctx,
                                          const PrepEngine &PP);

  void OnPopScope(SourceLocation Loc, Scope *S);
  void OnTranslationUnitScope(Scope *S);

  Decl *ParsedFreeStandingDeclSpec(Scope *S, AccessSpecifier AS, DeclSpec &DS,
                                   const ParsedAttributesView &DeclAttrs,
                                   RecordDecl *&AnonRecord);

  Decl *FormAnonymousStructOrUnion(Scope *S, DeclSpec &DS, AccessSpecifier AS,
                                   RecordDecl *Record,
                                   const PrintingPolicy &Policy);

  Decl *FormMicrosoftCAnonymousStruct(Scope *S, DeclSpec &DS,
                                      RecordDecl *Record);

  enum NonTagKind {
    NTK_NonStruct,
    NTK_NonUnion,
    NTK_NonEnum,
    NTK_Typedef,
  };

  NonTagKind getNonTagTypeDeclKind(const Decl *D, TagTypeKind TTK);

  bool isAcceptableTagRedeclaration(const TagDecl *Previous,
                                    TagTypeKind NewTag);

  enum TagUseKind {
    TUK_Reference,   // Reference to a tag:  'struct foo *X;'
    TUK_Declaration, // Fwd decl of a tag:   'struct foo;'
    TUK_Definition,  // Definition of a tag: 'struct foo { int X; } Y;'
  };

  enum OffsetOfKind {
    // Not parsing a type within __builtin_offsetof.
    OOK_Outside,
    // Parsing a type within __builtin_offsetof.
    OOK_Builtin,
    // Parsing a type within macro "offsetof", defined in __buitin_offsetof
    // To improve our diagnostic message.
    OOK_Macro,
  };

  DeclResult OnTag(Scope *S, unsigned TagSpec, TagUseKind TUK,
                   SourceLocation KWLoc, IdentifierInfo *Name,
                   SourceLocation NameLoc, const ParsedAttributesView &Attr,
                   AccessSpecifier AS, bool &OwnedDecl,
                   TypeResult UnderlyingType, OffsetOfKind OOK,
                   SkipBodyInfo *SkipBody = nullptr);

  Decl *OnField(Scope *S, Decl *TagD, SourceLocation DeclStart, Declarator &D,
                Expr *BitfieldWidth);

  FieldDecl *OnField(Scope *S, RecordDecl *TagD, SourceLocation DeclStart,
                     Declarator &D, Expr *BitfieldWidth, AccessSpecifier AS);
  FieldDecl *CheckFieldDecl(DeclarationName Name, QualType T,
                            TypeSourceInfo *TInfo, RecordDecl *Record,
                            SourceLocation Loc, Expr *BitfieldWidth,
                            SourceLocation TSSL, AccessSpecifier AS,
                            NamedDecl *PrevDecl, Declarator *D = nullptr);

  void OnFields(Scope *S, SourceLocation RecLoc, Decl *TagDecl,
                llvm::ArrayRef<Decl *> Fields, SourceLocation LBrac,
                SourceLocation RBrac, const ParsedAttributesView &AttrList);

  void OnTagStartDefinition(Scope *S, Decl *TagDecl);

  void OnTagFinishDefinition(Scope *S, Decl *TagDecl, SourceRange BraceRange);

  EnumConstantDecl *CheckEnumConstant(EnumDecl *Enum,
                                      EnumConstantDecl *LastEnumConst,
                                      SourceLocation IdLoc, IdentifierInfo *Id,
                                      Expr *val);
  bool CheckEnumUnderlyingType(TypeSourceInfo *TI);
  bool CheckEnumRedeclaration(SourceLocation EnumLoc, bool,
                              QualType EnumUnderlyingTy, bool IsFixed,
                              const EnumDecl *Prev);

  Decl *OnEnumConstant(Scope *S, Decl *EnumDecl, Decl *LastEnumConstant,
                       SourceLocation IdLoc, IdentifierInfo *Id,
                       const ParsedAttributesView &Attrs,
                       SourceLocation EqualLoc, Expr *Val);
  void OnEnumBody(SourceLocation EnumLoc, SourceRange BraceRange,
                  Decl *EnumDecl, llvm::ArrayRef<Decl *> Elements, Scope *S,
                  const ParsedAttributesView &Attr);

  void PushDeclContext(Scope *S, DeclContext *DC);
  void PopDeclContext();

  void EnterDeclaratorContext(Scope *S, DeclContext *DC);

  void OnReenterFunctionContext(Scope *S, Decl *D);
  void OnExitFunctionContext();

  DeclContext *getFunctionLevelDeclContext() const;

  FunctionDecl *getCurFunctionDecl() const;

  void PushOnScopeChains(NamedDecl *D, Scope *S, bool AddToContext = true);

  bool isDeclInScope(NamedDecl *D, DeclContext *Ctx, Scope *S = nullptr) const;

  static Scope *getScopeForDeclContext(Scope *S, DeclContext *DC);

  TypedefDecl *ParseTypedefDecl(Scope *S, Declarator &D, QualType T,
                                TypeSourceInfo *TInfo);
  bool isIncompatibleTypedef(TypeDecl *Old, TypedefNameDecl *New);

  enum AvailabilityMergeKind {
    /// Don't merge availability attributes at all.
    AMK_None,
    /// Merge availability attributes for a redeclaration, which requires
    /// an exact match.
    AMK_Redeclaration
  };

  enum AvailabilityPriority : int {
    /// The availability attribute was specified explicitly next to the
    /// declaration.
    AP_Explicit = 0,

    /// The availability attribute was applied using '#pragma neverc attribute'.
    AP_PragmaAttribute = 1,

    /// The availability attribute for a specific platform was inferred from
    /// an availability attribute for another platform.
    AP_InferredFromOtherPlatform = 2
  };

  AvailabilityAttr *mergeAvailabilityAttr(
      NamedDecl *D, const AttributeCommonInfo &CI, IdentifierInfo *Platform,
      bool Implicit, llvm::VersionTuple Introduced,
      llvm::VersionTuple Deprecated, llvm::VersionTuple Obsoleted,
      bool IsUnavailable, llvm::StringRef Message, bool IsStrict,
      llvm::StringRef Replacement, int Priority);
  TypeVisibilityAttr *
  mergeTypeVisibilityAttr(Decl *D, const AttributeCommonInfo &CI,
                          TypeVisibilityAttr::VisibilityType Vis);
  VisibilityAttr *mergeVisibilityAttr(Decl *D, const AttributeCommonInfo &CI,
                                      VisibilityAttr::VisibilityType Vis);
  DLLImportAttr *mergeDLLImportAttr(Decl *D, const AttributeCommonInfo &CI);
  DLLExportAttr *mergeDLLExportAttr(Decl *D, const AttributeCommonInfo &CI);
  ErrorAttr *mergeErrorAttr(Decl *D, const AttributeCommonInfo &CI,
                            llvm::StringRef NewUserDiagnostic);
  FormatAttr *mergeFormatAttr(Decl *D, const AttributeCommonInfo &CI,
                              IdentifierInfo *Format, int FormatIdx,
                              int FirstArg);
  SectionAttr *mergeSectionAttr(Decl *D, const AttributeCommonInfo &CI,
                                llvm::StringRef Name);
  CodeSegAttr *mergeCodeSegAttr(Decl *D, const AttributeCommonInfo &CI,
                                llvm::StringRef Name);
  AlwaysInlineAttr *mergeAlwaysInlineAttr(Decl *D,
                                          const AttributeCommonInfo &CI,
                                          const IdentifierInfo *Ident);
  MinSizeAttr *mergeMinSizeAttr(Decl *D, const AttributeCommonInfo &CI);
  OptimizeNoneAttr *mergeOptimizeNoneAttr(Decl *D,
                                          const AttributeCommonInfo &CI);
  InternalLinkageAttr *mergeInternalLinkageAttr(Decl *D, const ParsedAttr &AL);
  InternalLinkageAttr *mergeInternalLinkageAttr(Decl *D,
                                                const InternalLinkageAttr &AL);
  EnforceTCBAttr *mergeEnforceTCBAttr(Decl *D, const EnforceTCBAttr &AL);
  EnforceTCBLeafAttr *mergeEnforceTCBLeafAttr(Decl *D,
                                              const EnforceTCBLeafAttr &AL);
  BTFDeclTagAttr *mergeBTFDeclTagAttr(Decl *D, const BTFDeclTagAttr &AL);

  void mergeDeclAttributes(NamedDecl *New, Decl *Old,
                           AvailabilityMergeKind AMK = AMK_Redeclaration);
  void MergeTypedefNameDecl(Scope *S, TypedefNameDecl *New,
                            LookupResult &OldDecls);
  bool MergeFunctionDecl(FunctionDecl *New, NamedDecl *&Old, Scope *S,
                         bool MergeTypeWithOld, bool NewDeclIsDefn);
  bool MergeCompatibleFunctionDecls(FunctionDecl *New, FunctionDecl *Old,
                                    Scope *S, bool MergeTypeWithOld);
  bool MergeMSVCCompatibleFunctionDecls(FunctionDecl *New, FunctionDecl *Old,
                                        Scope *S, bool MergeTypeWithOld);
  void MergeVarDecl(VarDecl *New, LookupResult &Previous);
  void MergeVarDeclTypes(VarDecl *New, VarDecl *Old, bool MergeTypeWithOld);
  bool checkVarDeclRedefinition(VarDecl *OldDefn, VarDecl *NewDefn);
  void notePreviousDefinition(const NamedDecl *Old, SourceLocation New);
  // AssignmentAction - This is used by all the assignment diagnostic functions
  // to represent what is actually causing the operation
  enum AssignmentAction {
    AA_Assigning,
    AA_Passing,
    AA_Returning,
    AA_Converting,
    AA_Initializing,
    AA_Sending,
    AA_Casting
  };

  enum OverloadKind {
    /// Distinct overloadable signatures for the same name.
    Ovl_Overload,

    /// Same signature as an existing declaration (merge / redeclaration).
    Ovl_Match,

    /// Lookup contained a non-function.
    Ovl_NonFunction
  };
  OverloadKind CheckOverload(FunctionDecl *New, const LookupResult &OldDecls,
                             NamedDecl *&OldDecl);
  bool IsOverload(FunctionDecl *New, FunctionDecl *Old);

  bool IsIntegralPromotion(Expr *From, QualType FromType, QualType ToType);
  bool IsFloatingPointPromotion(QualType FromType, QualType ToType);
  bool IsComplexPromotion(QualType FromType, QualType ToType);
  bool IsPointerConversion(Expr *From, QualType FromType, QualType ToType,
                           QualType &ConvertedType);

  bool FunctionParamTypesAreEqual(llvm::ArrayRef<QualType> Old,
                                  llvm::ArrayRef<QualType> New,
                                  unsigned *ArgPos = nullptr,
                                  bool Reversed = false);

  bool FunctionParamTypesAreEqual(const FunctionProtoType *OldType,
                                  const FunctionProtoType *NewType,
                                  unsigned *ArgPos = nullptr,
                                  bool Reversed = false);

  void DiagnoseFunctionTypeMismatch(PartialDiagnostic &PDiag, QualType FromType,
                                    QualType ToType);

  bool CheckPointerConversion(Expr *From, QualType ToType, CastKind &Kind,
                              bool IgnoreBaseAccess, bool Diagnose = true);
  bool IsQualificationConversion(QualType FromType, QualType ToType,
                                 bool CStyle);
  bool IsFunctionConversion(QualType FromType, QualType ToType,
                            QualType &ResultTy);

  bool IsStringInit(Expr *Init, const ArrayType *AT);

  bool CanPerformCopyInitialization(const InitializedEntity &Entity,
                                    ExprResult Init);
  ExprResult PerformCopyInitialization(const InitializedEntity &Entity,
                                       SourceLocation EqualLoc, ExprResult Init,
                                       bool TopLevelOfInitList = false);

  void checkInitializerLifetime(const InitializedEntity &Entity, Expr *Init);

  ExprResult PerformContextuallyConvertToBool(Expr *From);

  // Members have to be TranslationUnitDecl*.

  bool diagnoseArgDependentDiagnoseIfAttrs(const FunctionDecl *Function,
                                           llvm::ArrayRef<const Expr *> Args,
                                           SourceLocation Loc);

  bool diagnoseArgIndependentDiagnoseIfAttrs(const NamedDecl *ND,
                                             SourceLocation Loc);

  bool checkAddressOfFunctionIsAvailable(const FunctionDecl *Function,
                                         bool Complain = false,
                                         SourceLocation Loc = SourceLocation());

  // [PossiblyAFunctionType]  -->   [Return]
  bool CheckCallReturnType(QualType ReturnType, SourceLocation Loc,
                           CallExpr *CE, FunctionDecl *FD);

  bool CheckParmsForFunctionDef(llvm::ArrayRef<ParmVarDecl *> Parameters,
                                bool CheckParameterNames);
  Scope *getNonFieldDeclScope(Scope *S);

  //@{

  RedeclarationKind forRedeclarationInCurContext() const {
    return ForExternalRedeclaration;
  }

  using TypoDiagnosticGenerator = std::function<void(const TypoCorrection &)>;
  using TypoRecoveryCallback =
      std::function<ExprResult(Sema &, TypoExpr *, TypoCorrection)>;

private:
  struct TypoExprState {
    std::unique_ptr<TypoCorrectionConsumer> Consumer;
    TypoDiagnosticGenerator DiagHandler;
    TypoRecoveryCallback RecoveryHandler;
    TypoExprState();
    TypoExprState(TypoExprState &&other) noexcept;
    TypoExprState &operator=(TypoExprState &&other) noexcept;
  };

  llvm::MapVector<TypoExpr *, TypoExprState> DelayedTypos;

  NamedDecl *CachedClassifyNameDecl = nullptr;
  SourceLocation CachedClassifyNameLoc;

  TypoExpr *createDelayedTypo(std::unique_ptr<TypoCorrectionConsumer> TCC,
                              TypoDiagnosticGenerator TDG,
                              TypoRecoveryCallback TRC, SourceLocation TypoLoc);

  std::unique_ptr<TypoCorrectionConsumer>
  makeTypoCorrectionConsumer(const DeclarationNameInfo &Typo,
                             ResolveNameKind LookupKind, Scope *S,
                             CorrectionCandidateCallback &CCC,
                             DeclContext *MemberContext, bool ErrorRecovery);

public:
  const TypoExprState &getTypoExprState(TypoExpr *TE) const;

  void clearDelayedTypo(TypoExpr *TE);

  NamedDecl *LookupSingleName(Scope *S, DeclarationName Name,
                              SourceLocation Loc, ResolveNameKind NameKind,
                              RedeclarationKind Redecl = NotForRedeclaration);
  bool LookupBuiltin(LookupResult &R);
  bool ResolveName(LookupResult &R, Scope *S,
                   bool AllowBuiltinCreation = false);
  bool LookupQualifiedName(LookupResult &R, DeclContext *LookupCtx);
  bool LookupParsedName(LookupResult &R, Scope *S,
                        bool AllowBuiltinCreation = false);

  LabelDecl *LookupOrCreateLabel(IdentifierInfo *II, SourceLocation IdentLoc,
                                 SourceLocation GnuLabelLoc = SourceLocation());

  enum CorrectTypoKind {
    CTK_NonError,     // CorrectTypo used in a non error recovery situation.
    CTK_ErrorRecovery // CorrectTypo used in normal error recovery.
  };

  TypoCorrection
  CorrectTypo(const DeclarationNameInfo &Typo, ResolveNameKind LookupKind,
              Scope *S, CorrectionCandidateCallback &CCC, CorrectTypoKind Mode,
              DeclContext *MemberContext = nullptr, bool RecordFailure = true);

  TypoExpr *CorrectTypoDelayed(const DeclarationNameInfo &Typo,
                               ResolveNameKind LookupKind, Scope *S,
                               CorrectionCandidateCallback &CCC,
                               TypoDiagnosticGenerator TDG,
                               TypoRecoveryCallback TRC, CorrectTypoKind Mode,
                               DeclContext *MemberContext = nullptr);

  ExprResult CorrectDelayedTyposInExpr(
      Expr *E, VarDecl * = nullptr, bool = false,
      llvm::function_ref<ExprResult(Expr *)> = [](Expr *E) -> ExprResult {
        return E;
      }) {
    return E;
  }

  ExprResult CorrectDelayedTyposInExpr(
      ExprResult ER, VarDecl * = nullptr, bool = false,
      llvm::function_ref<ExprResult(Expr *)> = [](Expr *E) -> ExprResult {
        return E;
      }) {
    return ER;
  }

  void diagnoseTypo(const TypoCorrection &Correction,
                    const PartialDiagnostic &TypoDiag,
                    bool ErrorRecovery = true);

  void diagnoseTypo(const TypoCorrection &Correction,
                    const PartialDiagnostic &TypoDiag,
                    const PartialDiagnostic &PrevNote,
                    bool ErrorRecovery = true);

  void FilterLookupForScope(LookupResult &R, DeclContext *Ctx, Scope *S,
                            bool ConsiderLinkage);

  void DiagnoseAmbiguousLookup(LookupResult &Result);
  //@}

  ExprResult CreateRecoveryExpr(SourceLocation Begin, SourceLocation End,
                                llvm::ArrayRef<Expr *> SubExprs,
                                QualType T = QualType());

  FunctionDecl *CreateBuiltin(IdentifierInfo *II, QualType Type, unsigned ID,
                              SourceLocation Loc);
  NamedDecl *LazilyCreateBuiltin(IdentifierInfo *II, unsigned ID, Scope *S,
                                 bool ForRedeclaration, SourceLocation Loc);
  NamedDecl *ImplicitlyDefineFunction(SourceLocation Loc, IdentifierInfo &II,
                                      Scope *S);
  void AddKnownFunctionAttributes(FunctionDecl *FD);

  // More parsing and symbol table subroutines.

  void ProcessPragmaWeak(Scope *S, Decl *D);
  // Decl attributes - this routine is the top level dispatcher.
  void ApplyDeclAttributes(Scope *S, Decl *D, const Declarator &PD);
  // Helper for delayed processing of attributes.
  void ProcessDeclAttributeDelayed(Decl *D,
                                   const ParsedAttributesView &AttrList);

  // Options for ProcessDeclAttributeList().
  struct ProcessDeclAttributeOptions {
    ProcessDeclAttributeOptions()
        : IncludeBracketAttributes(true), IgnoreTypeAttributes(false) {}

    ProcessDeclAttributeOptions WithIncludeBracketAttributes(bool Val) {
      ProcessDeclAttributeOptions Result = *this;
      Result.IncludeBracketAttributes = Val;
      return Result;
    }

    ProcessDeclAttributeOptions WithIgnoreTypeAttributes(bool Val) {
      ProcessDeclAttributeOptions Result = *this;
      Result.IgnoreTypeAttributes = Val;
      return Result;
    }

    // Whether \c [[...]] attributes (double square bracket syntax) are applied.
    bool IncludeBracketAttributes;

    // Should any type attributes encountered be ignored?
    // If this option is false, a diagnostic will be emitted for any type
    // attributes of a kind that does not "slide" from the declaration to
    // the decl-specifier-seq.
    bool IgnoreTypeAttributes;
  };

  void ProcessDeclAttributeList(Scope *S, Decl *D,
                                const ParsedAttributesView &AttrList,
                                const ProcessDeclAttributeOptions &Options =
                                    ProcessDeclAttributeOptions());

  void checkUnusedDeclAttributes(Declarator &D);

  bool checkCommonAttributeFeatures(const Decl *D, const ParsedAttr &A);
  bool checkCommonAttributeFeatures(const Stmt *S, const ParsedAttr &A);

  bool isValidPointerAttrType(QualType T);

  bool CheckRegparmAttr(const ParsedAttr &attr, unsigned &value);

  bool ValidateCallingConvAttr(const ParsedAttr &attr, CallingConv &CC,
                               const FunctionDecl *FD = nullptr);
  bool CheckAttrTarget(const ParsedAttr &CurrAttr);
  bool CheckAttrNoArgs(const ParsedAttr &CurrAttr);
  bool checkStringLiteralArgumentAttr(const AttributeCommonInfo &CI,
                                      const Expr *E, llvm::StringRef &Str,
                                      SourceLocation *ArgLocation = nullptr);
  bool checkStringLiteralArgumentAttr(const ParsedAttr &Attr, unsigned ArgNum,
                                      llvm::StringRef &Str,
                                      SourceLocation *ArgLocation = nullptr);
  llvm::Error isValidSectionSpecifier(llvm::StringRef Str);
  bool checkSectionName(SourceLocation LiteralLoc, llvm::StringRef Str);
  bool checkTargetAttr(SourceLocation LiteralLoc, llvm::StringRef Str);
  bool checkTargetVersionAttr(SourceLocation LiteralLoc, llvm::StringRef &Str,
                              bool &isDefault);
  bool checkTargetClonesAttrString(
      SourceLocation LiteralLoc, llvm::StringRef Str,
      const StringLiteral *Literal, bool &HasDefault, bool &HasCommas,
      bool &HasNotDefault,
      llvm::SmallVectorImpl<llvm::SmallString<64>> &StringsBuffer);
  void CheckAlignasUnderalignment(Decl *D);

  bool CheckNoInlineAttr(const Stmt *OrigSt, const Stmt *CurSt,
                         const AttributeCommonInfo &A);
  bool CheckAlwaysInlineAttr(const Stmt *OrigSt, const Stmt *CurSt,
                             const AttributeCommonInfo &A);

  // Check if there is an explicit attribute, but only look through parens.
  // The intent is to look for an attribute on the current declarator, but not
  // one that came from a typedef.
  bool hasExplicitCallingConv(QualType T);

  const AttributedType *getCallingConvAttributedType(QualType T) const;

  void ProcessStmtAttributes(Stmt *Stmt, const ParsedAttributes &InAttrs,
                             llvm::SmallVectorImpl<const Attr *> &OutAttrs);

  TypoCorrection FailedCorrection(IdentifierInfo *Typo, SourceLocation TypoLoc,
                                  bool RecordFailure = true) {
    if (RecordFailure)
      TypoCorrectionFailures[Typo].insert(TypoLoc);
    return TypoCorrection();
  }

  //===--------------------------------------------------------------------===//
  // Statement Parsing Callbacks: SemaStmt.cpp.
public:
  class FullExprArg {
  public:
    FullExprArg() : E(nullptr) {}

    ExprResult release() { return E; }

    Expr *get() const { return E; }

    Expr *operator->() { return E; }

  private:
    friend class Sema;

    explicit FullExprArg(Expr *expr) : E(expr) {}

    Expr *E;
  };

  FullExprArg MakeFullExpr(Expr *Arg) {
    return MakeFullExpr(Arg, Arg ? Arg->getExprLoc() : SourceLocation());
  }
  FullExprArg MakeFullExpr(Expr *Arg, SourceLocation CC) {
    return FullExprArg(
        OnFinishFullExpr(Arg, CC, /*DiscardedValue*/ false).get());
  }
  FullExprArg MakeFullDiscardedValueExpr(Expr *Arg) {
    ExprResult FE =
        OnFinishFullExpr(Arg, Arg ? Arg->getExprLoc() : SourceLocation(),
                         /*DiscardedValue*/ true);
    return FullExprArg(FE.get());
  }

  StmtResult OnExprStmt(ExprResult Arg, bool DiscardedValue = true);
  StmtResult OnExprStmtError();

  StmtResult OnNullStmt(SourceLocation SemiLoc,
                        bool HasLeadingEmptyMacro = false);

  void OnStartOfCompoundStmt(bool IsStmtExpr);
  void OnAfterCompoundStatementLeadingPragmas();
  void OnFinishOfCompoundStmt();
  StmtResult OnCompoundStmt(SourceLocation L, SourceLocation R,
                            llvm::ArrayRef<Stmt *> Elts, bool isStmtExpr);

  class CompoundScopeRAII {
  public:
    CompoundScopeRAII(Sema &S, bool IsStmtExpr = false) : S(S) {
      S.OnStartOfCompoundStmt(IsStmtExpr);
    }

    ~CompoundScopeRAII() { S.OnFinishOfCompoundStmt(); }

  private:
    Sema &S;
  };

  struct FunctionScopeRAII {
    Sema &S;
    bool Active;
    FunctionScopeRAII(Sema &S) : S(S), Active(true) {}
    ~FunctionScopeRAII() {
      if (Active)
        S.PopFunctionScopeInfo();
    }
    void disable() { Active = false; }
  };

  StmtResult OnDeclStmt(DeclGroupPtrTy Decl, SourceLocation StartLoc,
                        SourceLocation EndLoc);
  ExprResult OnCaseExpr(SourceLocation CaseLoc, ExprResult Val);
  StmtResult OnCaseStmt(SourceLocation CaseLoc, ExprResult LHS,
                        SourceLocation DotDotDotLoc, ExprResult RHS,
                        SourceLocation ColonLoc);
  void OnCaseStmtBody(Stmt *CaseStmt, Stmt *SubStmt);

  StmtResult OnDefaultStmt(SourceLocation DefaultLoc, SourceLocation ColonLoc,
                           Stmt *SubStmt, Scope *CurScope);
  StmtResult OnLabelStmt(SourceLocation IdentLoc, LabelDecl *TheDecl,
                         SourceLocation ColonLoc, Stmt *SubStmt);

  StmtResult FormAttributedStmt(SourceLocation AttrsLoc,
                                llvm::ArrayRef<const Attr *> Attrs,
                                Stmt *SubStmt);
  StmtResult OnAttributedStmt(const ParsedAttributes &AttrList, Stmt *SubStmt);

  class ConditionResult;

  StmtResult OnIfStmt(SourceLocation IfLoc, SourceLocation LParenLoc,
                      Stmt *InitStmt, ConditionResult Cond,
                      SourceLocation RParenLoc, Stmt *ThenVal,
                      SourceLocation ElseLoc, Stmt *ElseVal);
  StmtResult FormIfStmt(SourceLocation IfLoc, SourceLocation LParenLoc,
                        Stmt *InitStmt, ConditionResult Cond,
                        SourceLocation RParenLoc, Stmt *ThenVal,
                        SourceLocation ElseLoc, Stmt *ElseVal);
  StmtResult OnStartOfSwitchStmt(SourceLocation SwitchLoc,
                                 SourceLocation LParenLoc, Stmt *InitStmt,
                                 ConditionResult Cond,
                                 SourceLocation RParenLoc);
  StmtResult OnFinishSwitchStmt(SourceLocation SwitchLoc, Stmt *Switch,
                                Stmt *Body);
  StmtResult OnWhileStmt(SourceLocation WhileLoc, SourceLocation LParenLoc,
                         ConditionResult Cond, SourceLocation RParenLoc,
                         Stmt *Body);
  StmtResult OnDoStmt(SourceLocation DoLoc, Stmt *Body, SourceLocation WhileLoc,
                      SourceLocation CondLParen, Expr *Cond,
                      SourceLocation CondRParen);

  StmtResult OnForStmt(SourceLocation ForLoc, SourceLocation LParenLoc,
                       Stmt *First, ConditionResult Second, FullExprArg Third,
                       SourceLocation RParenLoc, Stmt *Body);

  StmtResult OnGotoStmt(SourceLocation GotoLoc, SourceLocation LabelLoc,
                        LabelDecl *TheDecl);
  StmtResult OnIndirectGotoStmt(SourceLocation GotoLoc, SourceLocation StarLoc,
                                Expr *DestExp);
  StmtResult OnContinueStmt(SourceLocation ContinueLoc, Scope *CurScope);
  StmtResult OnBreakStmt(SourceLocation BreakLoc, Scope *CurScope);

  struct NamedReturnInfo {
    const VarDecl *Candidate;

    enum Status : uint8_t { None, MoveEligible, MoveEligibleAndCopyElidable };
    Status S;

    bool isMoveEligible() const { return S != None; };
    bool isCopyElidable() const { return S == MoveEligibleAndCopyElidable; }
  };
  NamedReturnInfo getNamedReturnInfo(Expr *&E);
  NamedReturnInfo getNamedReturnInfo(const VarDecl *VD);
  const VarDecl *getCopyElisionCandidate(NamedReturnInfo &Info,
                                         QualType ReturnType);

  StmtResult OnReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp,
                          Scope *CurScope);
  StmtResult FormReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp,
                            bool AllowRecovery = false);
  StmtResult OnGCCAsmStmt(SourceLocation AsmLoc, bool IsSimple, bool IsVolatile,
                          unsigned NumOutputs, unsigned NumInputs,
                          IdentifierInfo **Names, MultiExprArg Constraints,
                          MultiExprArg Exprs, Expr *AsmString,
                          MultiExprArg Clobbers, unsigned NumLabels,
                          SourceLocation RParenLoc);

  void FillInlineAsmIdentifierInfo(Expr *Res,
                                   llvm::InlineAsmIdentifierInfo &Info);
  ExprResult LookupInlineAsmIdentifier(UnqualifiedId &Id,
                                       bool IsUnevaluatedContext);
  bool LookupInlineAsmField(llvm::StringRef Base, llvm::StringRef Member,
                            unsigned &Offset, SourceLocation AsmLoc);
  ExprResult LookupInlineAsmVarDeclField(Expr *RefExpr, llvm::StringRef Member,
                                         SourceLocation AsmLoc);
  StmtResult OnMSAsmStmt(SourceLocation AsmLoc, SourceLocation LBraceLoc,
                         llvm::ArrayRef<Token> AsmToks,
                         llvm::StringRef AsmString, unsigned NumOutputs,
                         unsigned NumInputs,
                         llvm::ArrayRef<llvm::StringRef> Constraints,
                         llvm::ArrayRef<llvm::StringRef> Clobbers,
                         llvm::ArrayRef<Expr *> Exprs, SourceLocation EndLoc);
  LabelDecl *GetOrCreateMSAsmLabel(llvm::StringRef ExternalLabelName,
                                   SourceLocation Location, bool AlwaysCreate);

  StmtResult OnSEHTryBlock(SourceLocation TryLoc, Stmt *TryBlock,
                           Stmt *Handler);
  StmtResult OnSEHExceptBlock(SourceLocation Loc, Expr *FilterExpr,
                              Stmt *Block);
  void OnStartSEHFinallyBlock();
  void OnAbortSEHFinallyBlock();
  StmtResult OnFinishSEHFinallyBlock(SourceLocation Loc, Stmt *Block);
  StmtResult OnSEHLeaveStmt(SourceLocation Loc, Scope *CurScope);

  bool ShouldWarnIfUnusedFileScopedDecl(const DeclaratorDecl *D) const;

  void MarkUnusedFileScopedDecl(const DeclaratorDecl *D);

  using DiagReceiverTy =
      llvm::function_ref<void(SourceLocation Loc, PartialDiagnostic PD)>;

  void DiagnoseUnusedExprResult(const Stmt *S, unsigned DiagID);
  void DiagnoseUnusedNestedTypedefs(const RecordDecl *D);
  void DiagnoseUnusedNestedTypedefs(const RecordDecl *D,
                                    DiagReceiverTy DiagReceiver);
  void DiagnoseUnusedDecl(const NamedDecl *ND);
  void DiagnoseUnusedDecl(const NamedDecl *ND, DiagReceiverTy DiagReceiver);

  void DiagnoseUnusedButSetDecl(const VarDecl *VD, DiagReceiverTy DiagReceiver);

  void DiagnoseEmptyStmtBody(SourceLocation StmtLoc, const Stmt *Body,
                             unsigned DiagID);

  void DiagnoseEmptyLoopBody(const Stmt *S, const Stmt *PossibleBody);

  void diagnoseNullableToNonnullConversion(QualType DstType, QualType SrcType,
                                           SourceLocation Loc);

  ParsingDeclState PushParsingDeclaration(sema::DelayedDiagnosticPool &pool) {
    return DelayedDiagnostics.push(pool);
  }
  void PopParsingDeclaration(ParsingDeclState state, Decl *decl);

  void redelayDiagnostics(sema::DelayedDiagnosticPool &pool);

  void DiagnoseAvailabilityOfDecl(NamedDecl *D,
                                  llvm::ArrayRef<SourceLocation> Locs);

  bool makeUnavailableInSystemHeader(SourceLocation loc,
                                     UnavailableAttr::ImplicitReason reason);

  void DiagnoseUnguardedAvailabilityViolations(Decl *FD);

  void handleDelayedAvailabilityCheck(sema::DelayedDiagnostic &DD, Decl *Ctx);

  //===--------------------------------------------------------------------===//
  // Expression Parsing Callbacks: SemaExpr.cpp.

  bool CanUseDecl(NamedDecl *D, bool TreatUnavailableAsInvalid);
  bool CheckDeclUsage(NamedDecl *D, llvm::ArrayRef<SourceLocation> Locs);
  void CheckSentinelArgs(const NamedDecl *D, SourceLocation Loc,
                         llvm::ArrayRef<Expr *> Args);

  void PushExpressionEvaluationContext(ExpressionEvaluationContext NewContext);
  void PopExpressionEvaluationContext();

  void DiscardCleanupsInEvaluationContext();

  ExprResult TransformToPotentiallyEvaluated(Expr *E);
  TypeSourceInfo *TransformToPotentiallyEvaluated(TypeSourceInfo *TInfo);
  ExprResult ResolveExprEvaluationContextForTypeof(Expr *E);

  ExprResult OnConstantExpression(ExprResult Res);

  // Mark declarations referenced (including linkage / odr-use tracking).
  //
  // MightBeOdrUse is usually true; set false only when the context proves the
  // use cannot be a linkage-relevant odr-use.
  void MarkAnyDeclReferenced(SourceLocation Loc, Decl *D, bool MightBeOdrUse);
  void MarkFunctionReferenced(SourceLocation Loc, FunctionDecl *Func,
                              bool MightBeOdrUse = true);
  void MarkVariableReferenced(SourceLocation Loc, VarDecl *Var);
  void MarkDeclRefReferenced(DeclRefExpr *E);
  void MarkMemberReferenced(MemberExpr *E);
  ExprResult CheckLValueToRValueConversionOperand(Expr *E);

  bool tryCaptureVariable(ValueDecl *Var, SourceLocation Loc);

  void MarkDeclarationsReferencedInExpr(
      Expr *E, bool SkipLocalVariables = false,
      llvm::ArrayRef<const Expr *> StopAt = std::nullopt);

  bool tryToRecoverWithCall(ExprResult &E, const PartialDiagnostic &PD,
                            bool ForceComplain = false,
                            bool (*IsPlausibleResult)(QualType) = nullptr);

  bool tryExprAsCall(Expr &E, QualType &ZeroArgCallReturnTy);

  ExprResult tryConvertExprToType(Expr *E, QualType Ty);

  bool DiagIfReachable(SourceLocation Loc, llvm::ArrayRef<const Stmt *> Stmts,
                       const PartialDiagnostic &PD);

  bool DiagRuntimeBehavior(SourceLocation Loc, const Stmt *Statement,
                           const PartialDiagnostic &PD);
  bool DiagRuntimeBehavior(SourceLocation Loc,
                           llvm::ArrayRef<const Stmt *> Stmts,
                           const PartialDiagnostic &PD);

  // Primary Expressions.
  SourceRange getExprRange(Expr *E) const;

  ExprResult OnIdExpression(Scope *S, UnqualifiedId &Id, bool HasTrailingLParen,
                            bool IsAddressOfOperand,
                            CorrectionCandidateCallback *CCC = nullptr,
                            bool IsInlineAsmIdentifier = false,
                            Token *KeywordReplacement = nullptr);

  bool DiagnoseEmptyLookup(Scope *S, LookupResult &R,
                           CorrectionCandidateCallback &CCC,
                           TypoExpr **Out = nullptr);

  NonOdrUseReason getNonOdrUseReasonInCurrentContext(ValueDecl *D);

  DeclRefExpr *MakeDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                               SourceLocation Loc);
  DeclRefExpr *MakeDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                               const DeclarationNameInfo &NameInfo,
                               NamedDecl *FoundD = nullptr);

  ExprResult FormAnonymousStructUnionMemberReference(
      SourceLocation nameLoc, IndirectFieldDecl *indirectField,
      NamedDecl *FoundDecl = nullptr, Expr *baseObjectExpr = nullptr,
      SourceLocation opLoc = SourceLocation());

  ExprResult FormDeclarationNameExpr(LookupResult &R);
  ExprResult FormDeclarationNameExpr(const DeclarationNameInfo &NameInfo,
                                     NamedDecl *D, NamedDecl *FoundD = nullptr);

  // ExpandFunctionLocalPredefinedMacros - Returns a new vector of Tokens,
  // where Tokens representing function local predefined macros (such as
  // __FUNCTION__) are replaced (expanded) with string-literal Tokens.
  std::vector<Token>
  ExpandFunctionLocalPredefinedMacros(llvm::ArrayRef<Token> Toks);

  ExprResult FormPredefinedExpr(SourceLocation Loc, PredefinedIdentKind IK);
  ExprResult OnPredefinedExpr(SourceLocation Loc, tok::TokenKind Kind);
  ExprResult OnIntegerConstant(SourceLocation Loc, uint64_t Val);

  ExprResult OnNumericConstant(const Token &Tok);
  ExprResult OnCharacterConstant(const Token &Tok);
  ExprResult OnParenExpr(SourceLocation L, SourceLocation R, Expr *E);
  ExprResult OnParenListExpr(SourceLocation L, SourceLocation R,
                             MultiExprArg Val);

  ExprResult OnStringLiteral(llvm::ArrayRef<Token> StringToks);

  ExprResult OnUnevaluatedStringLiteral(llvm::ArrayRef<Token> StringToks);

  ExprResult OnGenericSelectionExpr(SourceLocation KeyLoc,
                                    SourceLocation DefaultLoc,
                                    SourceLocation RParenLoc,
                                    bool PredicateIsExpr,
                                    void *ControllingExprOrType,
                                    llvm::ArrayRef<ParsedType> ArgTypes,
                                    llvm::ArrayRef<Expr *> ArgExprs);
  ExprResult CreateGenericSelectionExpr(SourceLocation KeyLoc,
                                        SourceLocation DefaultLoc,
                                        SourceLocation RParenLoc,
                                        bool PredicateIsExpr,
                                        void *ControllingExprOrType,
                                        llvm::ArrayRef<TypeSourceInfo *> Types,
                                        llvm::ArrayRef<Expr *> Exprs);

  // Binary/Unary Operators.  'Tok' is the token for the operator.
  ExprResult CreateBuiltinUnaryOp(SourceLocation OpLoc, UnaryOperatorKind Opc,
                                  Expr *InputExpr, bool IsAfterAmp = false);
  ExprResult FormUnaryOp(Scope *S, SourceLocation OpLoc, UnaryOperatorKind Opc,
                         Expr *Input, bool IsAfterAmp = false);
  ExprResult OnUnaryOp(Scope *S, SourceLocation OpLoc, tok::TokenKind Op,
                       Expr *Input, bool IsAfterAmp = false);

  QualType CheckAddressOfOperand(ExprResult &Operand, SourceLocation OpLoc);

  bool OnAlignasTypeArgument(llvm::StringRef KWName, ParsedType Ty,
                             SourceLocation OpLoc, SourceRange R);
  bool CheckAlignasTypeArgument(llvm::StringRef KWName, TypeSourceInfo *TInfo,
                                SourceLocation OpLoc, SourceRange R);

  ExprResult CreateUnaryExprOrTypeTraitExpr(TypeSourceInfo *TInfo,
                                            SourceLocation OpLoc,
                                            UnaryExprOrTypeTrait ExprKind,
                                            SourceRange R);
  ExprResult CreateUnaryExprOrTypeTraitExpr(Expr *E, SourceLocation OpLoc,
                                            UnaryExprOrTypeTrait ExprKind);
  ExprResult OnUnaryExprOrTypeTraitExpr(SourceLocation OpLoc,
                                        UnaryExprOrTypeTrait ExprKind,
                                        bool IsType, void *TyOrEx,
                                        SourceRange ArgRange);

  ExprResult CheckPlaceholderExpr(Expr *E);
  bool CheckUnaryExprOrTypeTraitOperand(Expr *E, UnaryExprOrTypeTrait ExprKind);
  bool CheckUnaryExprOrTypeTraitOperand(QualType ExprType, SourceLocation OpLoc,
                                        SourceRange ExprRange,
                                        UnaryExprOrTypeTrait ExprKind,
                                        llvm::StringRef KWName);
  ExprResult OnPostfixUnaryOp(Scope *S, SourceLocation OpLoc,
                              tok::TokenKind Kind, Expr *Input);

  ExprResult OnArraySubscriptExpr(Scope *S, Expr *Base, SourceLocation LLoc,
                                  MultiExprArg ArgExprs, SourceLocation RLoc);
  ExprResult CreateBuiltinArraySubscriptExpr(Expr *Base, SourceLocation LLoc,
                                             Expr *Idx, SourceLocation RLoc);

  ExprResult CreateBuiltinMatrixSubscriptExpr(Expr *Base, Expr *RowIdx,
                                              Expr *ColumnIdx,
                                              SourceLocation RBLoc);

  ExprResult FormMemberReferenceExpr(Expr *Base, QualType BaseType,
                                     SourceLocation OpLoc, bool IsArrow,
                                     const DeclarationNameInfo &NameInfo);

  ExprResult FormMemberReferenceExpr(Expr *Base, QualType BaseType,
                                     SourceLocation OpLoc, bool IsArrow,
                                     LookupResult &R);

  ExprResult FormFieldReferenceExpr(Expr *BaseExpr, bool IsArrow,
                                    SourceLocation OpLoc, FieldDecl *Field,
                                    NamedDecl *FoundDecl,
                                    const DeclarationNameInfo &MemberNameInfo);

  ExprResult PerformMemberExprBaseConversion(Expr *Base, bool IsArrow);

  ExprResult OnMemberAccessExpr(Scope *S, Expr *Base, SourceLocation OpLoc,
                                tok::TokenKind OpKind, UnqualifiedId &Member);
  bool isNeverCStringType(QualType T) const;

  mutable const RecordDecl *NeverCStringRDCache = nullptr;

  mutable const IdentifierInfo *NeverCStringIICache = nullptr;

  llvm::DenseMap<const IdentifierInfo *, FunctionDecl *> NeverCStringFDCache;

  /// Cached name-lookup for a NeverC builtin-string runtime function.
  /// Centralises the IdentifierInfo+LookupResult+ResolveName+cache dance
  /// shared by `attachNeverC{String,Wptr}Cleanup` and
  /// `buildNeverCStringRuntimeCall`.  Returns nullptr when the thin
  /// header / prelude does not declare `Name` (e.g. legacy TU with no
  /// `string` use, or downstream removed the helper).  `Sc` may be
  /// nullptr; the implementation falls back to `TUScope`.
  FunctionDecl *lookupNeverCStringFunctionDecl(llvm::StringRef Name, Scope *Sc,
                                               SourceLocation Loc);

  /// Bit flags for categorising NeverC builtin-string runtime functions.
  /// A single DenseMap lookup replaces three separate DenseSets and
  /// eliminates repeated const_cast / lazy-init boilerplate.
  enum NeverCStringFnKind : uint8_t {
    NCSFK_Runtime      = 1 << 0,
    NCSFK_BorrowedView = 1 << 1,
    NCSFK_WptrProducer = 1 << 2,
  };
  // `mutable` lets the lazy-init helper stay `const`-callable from the
  // `isNeverCString*` predicates without sprinkling `const_cast` at
  // every call site -- the map is logically part of the cache state,
  // not part of the Sema's observable contract.
  mutable llvm::DenseMap<const IdentifierInfo *, uint8_t> NeverCStringFnKinds;
  mutable bool NeverCStringFnKindsReady = false;
  void ensureNeverCStringFnKinds() const;

  bool hasNeverCStringFnKind(const FunctionDecl *FD, uint8_t Mask) const;

  bool isNeverCStringRuntimeFD(const FunctionDecl *FD) const;
  bool isInsideNeverCStringRuntime() const;
  bool isInsideNeverCStringRuntime(const Decl *D) const;
  bool isNeverCStringBorrowedViewFD(const FunctionDecl *FD) const;
  bool isNeverCStringWptrProducer(const FunctionDecl *FD) const;

  ExprResult
  OnBuiltinStringMethodCall(Scope *S, Expr *Base, SourceLocation OpLoc,
                            tok::TokenKind OpKind, UnqualifiedId &Member,
                            SourceLocation LParenLoc, MultiExprArg ArgExprs,
                            SourceLocation RParenLoc);

  MemberExpr *FormMemberExpr(Expr *Base, bool IsArrow, SourceLocation OpLoc,
                             ValueDecl *Member, NamedDecl *FoundDecl,
                             const DeclarationNameInfo &MemberNameInfo,
                             QualType Ty, ExprValueKind VK, ExprObjectKind OK);

  bool ConvertArgumentsForCall(CallExpr *Call, Expr *Fn, FunctionDecl *FDecl,
                               const FunctionProtoType *Proto,
                               llvm::ArrayRef<Expr *> Args,
                               SourceLocation RParenLoc);
  void CheckStaticArrayArgument(SourceLocation CallLoc, ParmVarDecl *Param,
                                const Expr *ArgExpr);

  ExprResult OnCallExpr(Scope *S, Expr *Fn, SourceLocation LParenLoc,
                        MultiExprArg ArgExprs, SourceLocation RParenLoc);
  ExprResult FormCallExpr(Scope *S, Expr *Fn, SourceLocation LParenLoc,
                          MultiExprArg ArgExprs, SourceLocation RParenLoc,
                          bool AllowRecovery = false);
  Expr *FormBuiltinCallExpr(SourceLocation Loc, Builtin::ID Id,
                            MultiExprArg CallArgs);
  enum class AtomicArgumentOrder { API, AST };
  ExprResult
  FormAtomicExpr(SourceRange CallRange, SourceRange ExprRange,
                 SourceLocation RParenLoc, MultiExprArg Args,
                 AtomicExpr::AtomicOp Op,
                 AtomicArgumentOrder ArgOrder = AtomicArgumentOrder::API);
  ExprResult MakeResolvedCallExpr(Expr *Fn, NamedDecl *NDecl,
                                  SourceLocation LParenLoc,
                                  llvm::ArrayRef<Expr *> Arg,
                                  SourceLocation RParenLoc);

  ExprResult OnCastExpr(Scope *S, SourceLocation LParenLoc, Declarator &D,
                        ParsedType &Ty, SourceLocation RParenLoc,
                        Expr *CastExpr);
  ExprResult FormCStyleCastExpr(SourceLocation LParenLoc, TypeSourceInfo *Ty,
                                SourceLocation RParenLoc, Expr *Op);
  CastKind PrepareScalarCast(ExprResult &src, QualType destType);

  ExprResult MaybeConvertParenListExprToParenExpr(Scope *S, Expr *ME);

  ExprResult OnCompoundLiteral(SourceLocation LParenLoc, ParsedType Ty,
                               SourceLocation RParenLoc, Expr *InitExpr);

  ExprResult FormCompoundLiteralExpr(SourceLocation LParenLoc,
                                     TypeSourceInfo *TInfo,
                                     SourceLocation RParenLoc,
                                     Expr *LiteralExpr);

  ExprResult OnInitList(SourceLocation LBraceLoc, MultiExprArg InitArgList,
                        SourceLocation RBraceLoc);

  ExprResult FormInitList(SourceLocation LBraceLoc, MultiExprArg InitArgList,
                          SourceLocation RBraceLoc);

  ExprResult OnDesignatedInitializer(Designation &Desig,
                                     SourceLocation EqualOrColonLoc,
                                     bool GNUSyntax, ExprResult Init);

private:
  static BinaryOperatorKind ConvertTokenKindToBinaryOpcode(tok::TokenKind Kind);

public:
  ExprResult OnBinOp(Scope *S, SourceLocation TokLoc, tok::TokenKind Kind,
                     Expr *LHSExpr, Expr *RHSExpr);
  ExprResult FormBinOp(Scope *S, SourceLocation OpLoc, BinaryOperatorKind Opc,
                       Expr *LHSExpr, Expr *RHSExpr);
  ExprResult CreateBuiltinBinOp(SourceLocation OpLoc, BinaryOperatorKind Opc,
                                Expr *LHSExpr, Expr *RHSExpr);
  void DiagnoseCommaOperator(const Expr *LHS, SourceLocation Loc);

  ExprResult OnConditionalOp(SourceLocation QuestionLoc,
                             SourceLocation ColonLoc, Expr *CondExpr,
                             Expr *LHSExpr, Expr *RHSExpr);

  ExprResult OnAddrLabel(SourceLocation OpLoc, SourceLocation LabLoc,
                         LabelDecl *TheDecl);

  void OnStartStmtExpr();
  ExprResult OnStmtExpr(Scope *S, SourceLocation LPLoc, Stmt *SubStmt,
                        SourceLocation RPLoc);
  ExprResult FormStmtExpr(SourceLocation LPLoc, Stmt *SubStmt,
                          SourceLocation RPLoc);
  // Handle the final expression in a statement expression.
  ExprResult OnStmtExprResult(ExprResult E);
  void OnStmtExprError();

  // __builtin_offsetof(type, identifier(.identifier|[expr])*)
  struct OffsetOfComponent {
    SourceLocation LocStart, LocEnd;
    bool isBrackets; // true if [expr], false if .ident
    union {
      IdentifierInfo *IdentInfo;
      Expr *E;
    } U;
  };

  ExprResult FormBuiltinOffsetOf(SourceLocation BuiltinLoc,
                                 TypeSourceInfo *TInfo,
                                 llvm::ArrayRef<OffsetOfComponent> Components,
                                 SourceLocation RParenLoc);
  ExprResult OnBuiltinOffsetOf(Scope *S, SourceLocation BuiltinLoc,
                               SourceLocation TypeLoc, ParsedType ParsedArgTy,
                               llvm::ArrayRef<OffsetOfComponent> Components,
                               SourceLocation RParenLoc);

  // __builtin_choose_expr(constExpr, expr1, expr2)
  ExprResult OnChooseExpr(SourceLocation BuiltinLoc, Expr *CondExpr,
                          Expr *LHSExpr, Expr *RHSExpr, SourceLocation RPLoc);

  // __builtin_va_arg(expr, type)
  ExprResult OnVAArg(SourceLocation BuiltinLoc, Expr *E, ParsedType Ty,
                     SourceLocation RPLoc);
  ExprResult FormVAArgExpr(SourceLocation BuiltinLoc, Expr *E,
                           TypeSourceInfo *TInfo, SourceLocation RPLoc);

  // __builtin_LINE(), __builtin_FUNCTION(), __builtin_FUNCSIG(),
  // __builtin_FILE(), __builtin_COLUMN()
  ExprResult OnSourceLocExpr(SourceLocIdentKind Kind, SourceLocation BuiltinLoc,
                             SourceLocation RPLoc);

  // Build a potentially resolved SourceLocExpr.
  ExprResult FormSourceLocExpr(SourceLocIdentKind Kind, QualType ResultTy,
                               SourceLocation BuiltinLoc, SourceLocation RPLoc,
                               DeclContext *ParentContext);

  bool CheckCaseExpression(Expr *E);

  enum IfExistsResult {
    /// The symbol exists.
    IER_Exists,

    /// The symbol does not exist.
    IER_DoesNotExist,

    /// An error occurred.
    IER_Error
  };

  IfExistsResult
  CheckMicrosoftIfExistsSymbol(Scope *S,
                               const DeclarationNameInfo &TargetNameInfo);

  IfExistsResult CheckMicrosoftIfExistsSymbol(Scope *S,
                                              SourceLocation KeywordLoc,
                                              bool IsIfExists,
                                              UnqualifiedId &Name);

  //===---------------------------- NeverC Extensions
  //----------------------===//

  ExprResult OnConvertVectorExpr(Expr *E, ParsedType ParsedDestTy,
                                 SourceLocation BuiltinLoc,
                                 SourceLocation RParenLoc);

  //===---- Temporaries, cleanups, unambiguous field lookup (C + shared AST)
  //---===//

  ValueDecl *tryLookupUnambiguousFieldDecl(RecordDecl *RD,
                                           const IdentifierInfo *MemberOrBase);

public:
  ExprResult MaybeBindToTemporary(Expr *E);

  ExprResult OnBoolLiteral(SourceLocation OpLoc, tok::TokenKind Kind);
  ExprResult OnNullPtrLiteral(SourceLocation Loc);

  Expr *MaybeCreateExprWithCleanups(Expr *SubExpr);
  ExprResult MaybeCreateExprWithCleanups(ExprResult SubExpr);

  ExprResult OnFinishFullExpr(Expr *Expr, bool DiscardedValue) {
    return OnFinishFullExpr(Expr, Expr ? Expr->getExprLoc() : SourceLocation(),
                            DiscardedValue);
  }
  ExprResult OnFinishFullExpr(Expr *Expr, SourceLocation CC,
                              bool DiscardedValue, bool IsConstexpr = false);

  enum class AArch64SMECallConversionKind {
    MatchExactly,
    MayAddPreservesZA,
    MayDropPreservesZA,
  };
  bool IsInvalidSMECallConversion(QualType FromType, QualType ToType,
                                  AArch64SMECallConversionKind C);

public:
  bool EvaluateStaticAssertMessageAsString(Expr *Message, std::string &Result,
                                           TreeContext &Ctx,
                                           bool ErrorOnInvalidMessage);
  Decl *OnStaticAssertDeclaration(SourceLocation StaticAssertLoc,
                                  Expr *AssertExpr, Expr *AssertMessageExpr,
                                  SourceLocation RParenLoc);
  Decl *FormStaticAssertDeclaration(SourceLocation StaticAssertLoc,
                                    Expr *AssertExpr, Expr *AssertMessageExpr,
                                    SourceLocation RParenLoc, bool Failed);

  bool SetMemberAccessSpecifier(NamedDecl *MemberDecl,
                                NamedDecl *PrevMemberDecl,
                                AccessSpecifier LexicalAS);

  enum AutoDeductionResult {
    ADK_Success = 0,
    ADK_Invalid,
    ADK_Inconsistent,
    ADK_AlreadyDiagnosed
  };

  QualType ReplaceAutoType(QualType TypeWithAuto, QualType Replacement);

  AutoDeductionResult DeduceAutoType(TypeLoc AutoTypeLoc, Expr *Initializer,
                                     QualType &Result);
  void DiagnoseAutoDeductionFailure(VarDecl *VDecl, Expr *Init);
  QualType deduceVarTypeFromInitializer(VarDecl *VDecl, QualType Type,
                                        TypeSourceInfo *TSI, SourceRange Range,
                                        bool DirectInit, Expr *Init);

  void PrintContextStack() {
    if (PragmaAttributeCurrentTargetDecl)
      PrintPragmaAttributeInstantiationPoint();
  }
  void PrintPragmaAttributeInstantiationPoint();

  bool isConstantEvaluatedOverride = false;

  const ExpressionEvaluationContextRecord &currentEvaluationContext() const {
    assert(!ExprEvalContexts.empty() &&
           "Must be in an expression evaluation context");
    return ExprEvalContexts.back();
  };

  bool isConstantEvaluatedContext() const {
    return currentEvaluationContext().isConstantEvaluated() ||
           isConstantEvaluatedOverride;
  }

  bool isUnevaluatedContext() const {
    return currentEvaluationContext().isUnevaluated();
  }

  bool DisableTypoCorrection;

  unsigned TyposCorrected;

  using SrcLocSet = llvm::SmallSet<SourceLocation, 2>;
  using IdentifierSourceLocations = llvm::DenseMap<IdentifierInfo *, SrcLocSet>;

  IdentifierSourceLocations TypoCorrectionFailures;

  class ExtParameterInfoBuilder {
    llvm::SmallVector<FunctionProtoType::ExtParameterInfo, 16> Infos;
    bool HasInteresting = false;

  public:
    /// Set the ExtParameterInfo for the parameter at the given index,
    void set(unsigned index, FunctionProtoType::ExtParameterInfo info) {
      assert(Infos.size() <= index);
      Infos.resize(index);
      Infos.push_back(info);

      if (!HasInteresting)
        HasInteresting = (info != FunctionProtoType::ExtParameterInfo());
    }

    /// Return a pointer (suitable for setting in an ExtProtoInfo) to the
    /// ExtParameterInfo array we've built up.
    const FunctionProtoType::ExtParameterInfo *
    getPointerOrNull(unsigned numParams) {
      if (!HasInteresting)
        return nullptr;
      Infos.resize(numParams);
      return Infos.data();
    }
  };

  enum PragmaOptionsAlignKind {
    POAK_Native, // #pragma options align=native / natural / power
    POAK_Packed, // #pragma options align=packed
    POAK_Reset   // #pragma options align=reset
  };

  void OnPragmaSection(SourceLocation PragmaLoc, PragmaSectionAction Action,
                       PragmaSectionKind SecKind, llvm::StringRef SecName);

  void OnPragmaOptionsAlign(PragmaOptionsAlignKind Kind,
                            SourceLocation PragmaLoc);

  void OnPragmaPack(SourceLocation PragmaLoc, PragmaMsStackAction Action,
                    llvm::StringRef SlotLabel, Expr *Alignment);

  void DiagnoseUnterminatedPragmaAlignPack();

  void OnPragmaMSStrictGuardStackCheck(SourceLocation PragmaLocation,
                                       PragmaMsStackAction Action, bool Value);

  void OnPragmaMSStruct(PragmaMSStructKind Kind);

  void OnPragmaMSComment(SourceLocation CommentLoc, PragmaMSCommentKind Kind,
                         llvm::StringRef Arg);

  // PragmaSectionKind defined above (PCSK_*)

  bool UnifySection(llvm::StringRef SectionName, int SectionFlags,
                    NamedDecl *TheDecl);
  bool UnifySection(llvm::StringRef SectionName, int SectionFlags,
                    SourceLocation PragmaSectionLocation);

  void OnPragmaMSSeg(SourceLocation PragmaLocation, PragmaMsStackAction Action,
                     llvm::StringRef StackSlotLabel, StringLiteral *SegmentName,
                     llvm::StringRef PragmaName);

  void OnPragmaMSSection(SourceLocation PragmaLocation, int SectionFlags,
                         StringLiteral *SegmentName);

  void OnPragmaMSInitSeg(SourceLocation PragmaLocation,
                         StringLiteral *SegmentName);

  void OnPragmaMSAllocText(
      SourceLocation PragmaLocation, llvm::StringRef Section,
      const llvm::SmallVector<std::tuple<IdentifierInfo *, SourceLocation>>
          &Functions);

  void OnPragmaDump(Scope *S, SourceLocation Loc, IdentifierInfo *II);

  void OnPragmaDump(Expr *E);

  void OnPragmaDetectMismatch(SourceLocation Loc, llvm::StringRef Name,
                              llvm::StringRef Value);

  bool isPreciseFPEnabled() {
    return !CurFPFeatures.getAllowFPReassociate() &&
           !CurFPFeatures.getNoSignedZero() &&
           !CurFPFeatures.getAllowReciprocal() &&
           !CurFPFeatures.getAllowApproxFunc();
  }

  void OnPragmaFPEvalMethod(SourceLocation Loc,
                            LangOptions::FPEvalMethodKind Value);

  void OnPragmaFloatControl(SourceLocation Loc, PragmaMsStackAction Action,
                            PragmaFloatControlKind Value);

  void OnPragmaUnused(const Token &Identifier, Scope *curScope,
                      SourceLocation PragmaLoc);

  void OnPragmaVisibility(const IdentifierInfo *VisType,
                          SourceLocation PragmaLoc);

  NamedDecl *DeclClonePragmaWeak(NamedDecl *ND, const IdentifierInfo *II,
                                 SourceLocation Loc);
  void DeclApplyPragmaWeak(Scope *S, NamedDecl *ND, const WeakInfo &W);

  void OnPragmaWeakID(IdentifierInfo *WeakName, SourceLocation PragmaLoc,
                      SourceLocation WeakNameLoc);

  void OnPragmaWeakAlias(IdentifierInfo *WeakName, IdentifierInfo *AliasName,
                         SourceLocation PragmaLoc, SourceLocation WeakNameLoc,
                         SourceLocation AliasNameLoc);

  void OnPragmaFPContract(SourceLocation Loc, LangOptions::FPModeKind FPC);

  void OnPragmaFPValueChangingOption(SourceLocation Loc, PragmaFPKind Kind,
                                     bool IsEnabled);

  void OnPragmaFEnvAccess(SourceLocation Loc, bool IsEnabled);

  void OnPragmaCXLimitedRange(SourceLocation Loc,
                              LangOptions::ComplexRangeKind Range);

  void OnPragmaFPExceptions(SourceLocation Loc,
                            LangOptions::FPExceptionModeKind);

  void OnPragmaFEnvRound(SourceLocation Loc, llvm::RoundingMode);

  void setExceptionMode(SourceLocation Loc, LangOptions::FPExceptionModeKind);

  void AddAlignmentAttributesForRecord(RecordDecl *RD);

  void AddMsStructLayoutForRecord(RecordDecl *RD);

  void AddPushedVisibilityAttribute(Decl *RD);

  void PopPragmaVisibility(SourceLocation EndLoc);

  void FreeVisContext();

  void OnPragmaAttributeAttribute(ParsedAttr &Attribute,
                                  SourceLocation PragmaLoc,
                                  attr::ParsedSubjectMatchRuleSet Rules);
  void OnPragmaAttributeEmptyPush(SourceLocation PragmaLoc,
                                  const IdentifierInfo *Namespace);

  void OnPragmaAttributePop(SourceLocation PragmaLoc,
                            const IdentifierInfo *Namespace);

  void AddPragmaAttributes(Scope *S, Decl *D);

  void DiagnoseUnterminatedPragmaAttribute();

  void OnPragmaOptimize(bool On, SourceLocation PragmaLoc);

  void OnPragmaMSOptimize(SourceLocation Loc, bool IsOn);

  void
  OnPragmaMSFunction(SourceLocation Loc,
                     const llvm::SmallVectorImpl<llvm::StringRef> &NoBuiltins);

  SourceLocation getOptimizeOffPragmaLocation() const {
    return OptimizeOffPragmaLocation;
  }

  void AddRangeBasedOptnone(FunctionDecl *FD);

  void AddSectionMSAllocText(FunctionDecl *FD);

  void AddOptnoneAttributeIfNoConflicts(FunctionDecl *FD, SourceLocation Loc);

  void ModifyFnAttributesMSPragmaOptimize(FunctionDecl *FD);

  void AddImplicitMSFunctionNoBuiltinAttr(FunctionDecl *FD);

  void AddAlignedAttr(Decl *D, const AttributeCommonInfo &CI, Expr *E);
  void AddAlignedAttr(Decl *D, const AttributeCommonInfo &CI,
                      TypeSourceInfo *T);

  void AddAssumeAlignedAttr(Decl *D, const AttributeCommonInfo &CI, Expr *E,
                            Expr *OE);

  void AddAllocAlignAttr(Decl *D, const AttributeCommonInfo &CI,
                         Expr *ParamExpr);

  void AddAlignValueAttr(Decl *D, const AttributeCommonInfo &CI, Expr *E);

  void AddAnnotationAttr(Decl *D, const AttributeCommonInfo &CI,
                         llvm::StringRef Annot,
                         llvm::MutableArrayRef<Expr *> Args);

  bool ConstantFoldAttrArgs(const AttributeCommonInfo &CI,
                            llvm::MutableArrayRef<Expr *> Args);

  void AddModeAttr(Decl *D, const AttributeCommonInfo &CI,
                   IdentifierInfo *Name);

  bool areMultiversionVariantFunctionsCompatible(
      const FunctionDecl *OldFD, const FunctionDecl *NewFD,
      const PartialDiagnostic &NoProtoDiagID,
      const PartialDiagnosticAt &NoteCausedDiagIDAt,
      const PartialDiagnosticAt &DiffDiagIDAt);

  enum CheckedConversionKind {
    /// An implicit conversion.
    CCK_ImplicitConversion,
    /// A C-style cast.
    CCK_CStyleCast,
    /// A cast other than a C-style cast.
    CCK_OtherCast
  };

  static bool isCast(CheckedConversionKind CCK) {
    return CCK == CCK_CStyleCast || CCK == CCK_OtherCast;
  }

  ExprResult
  ImpCastExprToType(Expr *E, QualType Type, CastKind CK,
                    ExprValueKind VK = VK_PRValue,
                    CheckedConversionKind CCK = CCK_ImplicitConversion);

  static CastKind ScalarTypeToBooleanCastKind(QualType ScalarTy);

  ExprResult IgnoredValueConversions(Expr *E);

  // UsualUnaryConversions - promotes integers (C99 6.3.1.1p2) and converts
  // functions and arrays to their respective pointers (C99 6.3.2.1).
  ExprResult UsualUnaryConversions(Expr *E);

  ExprResult CallExprUnaryConversions(Expr *E);

  // DefaultFunctionArrayConversion - converts functions and arrays
  // to their respective pointers (C99 6.3.2.1).
  ExprResult DefaultFunctionArrayConversion(Expr *E, bool Diagnose = true);

  // DefaultFunctionArrayLvalueConversion - converts functions and
  // arrays to their respective pointers and performs the
  // lvalue-to-rvalue conversion.
  ExprResult DefaultFunctionArrayLvalueConversion(Expr *E,
                                                  bool Diagnose = true);

  // DefaultLvalueConversion - performs lvalue-to-rvalue conversion on
  // the operand. This function is a no-op if the operand has a function type
  // or an array type.
  ExprResult DefaultLvalueConversion(Expr *E);

  // DefaultArgumentPromotion (C99 6.5.2.2p6). Used for function calls that
  // do not have a prototype. Integer promotions are performed on each
  // argument, and arguments that have type float are promoted to double.
  ExprResult DefaultArgumentPromotion(Expr *E);

  enum VariadicCallType { VariadicFunction, VariadicDoesNotApply };

  // Used for determining in which context a type is allowed to be passed to a
  // vararg function.
  enum VarArgKind { VAK_Valid, VAK_Undefined, VAK_MSVCUndefined, VAK_Invalid };

  // Determines which VarArgKind fits an expression.
  VarArgKind isValidVarArgType(const QualType &Ty);

  void checkVariadicArgument(const Expr *E);

  bool checkAndRewriteMustTailAttr(Stmt *St, const Attr &MTA);

private:
  bool checkMustTailAttr(const Stmt *St, const Attr &MTA);

public:
  bool GatherArgumentsForCall(SourceLocation CallLoc, FunctionDecl *FDecl,
                              const FunctionProtoType *Proto,
                              unsigned FirstParam, llvm::ArrayRef<Expr *> Args,
                              llvm::SmallVectorImpl<Expr *> &AllArgs,
                              VariadicCallType CallType = VariadicDoesNotApply);

  // DefaultVariadicArgumentPromotion - Like DefaultArgumentPromotion, but
  // will create a runtime trap if the resulting type is not a POD type.
  ExprResult DefaultVariadicArgumentPromotion(Expr *E);

  enum ArithConvKind {
    /// An arithmetic operation.
    ACK_Arithmetic,
    /// A bitwise operation.
    ACK_BitwiseOp,
    /// A comparison.
    ACK_Comparison,
    /// A conditional (?:) operator.
    ACK_Conditional,
    /// A compound assignment expression.
    ACK_CompAssign,
  };

  // UsualArithmeticConversions - performs the UsualUnaryConversions on it's
  // operands and then handles various conversions that are common to binary
  // operators (C99 6.3.1.8). If both operands aren't arithmetic, this
  // routine returns the first non-arithmetic type found. The client is
  // responsible for emitting appropriate error diagnostics.
  QualType UsualArithmeticConversions(ExprResult &LHS, ExprResult &RHS,
                                      SourceLocation Loc, ArithConvKind ACK);

  enum AssignConvertType {
    /// Compatible - the types are compatible according to the standard.
    Compatible,

    /// PointerToInt - The assignment converts a pointer to an int, which we
    /// accept as an extension.
    PointerToInt,

    /// IntToPointer - The assignment converts an int to a pointer, which we
    /// accept as an extension.
    IntToPointer,

    /// FunctionVoidPointer - The assignment is between a function pointer and
    /// void*, which the standard doesn't allow, but we accept as an extension.
    FunctionVoidPointer,

    /// IncompatiblePointer - The assignment is between two pointers types that
    /// are not compatible, but we accept them as an extension.
    IncompatiblePointer,

    /// IncompatibleFunctionPointer - The assignment is between two function
    /// pointers types that are not compatible, but we accept them as an
    /// extension.
    IncompatibleFunctionPointer,

    /// IncompatibleFunctionPointerStrict - The assignment is between two
    /// function pointer types that are not identical, but are compatible,
    /// unless compiled with -fsanitize=cfi, in which case the type mismatch
    /// may trip an indirect call runtime check.
    IncompatibleFunctionPointerStrict,

    /// IncompatiblePointerSign - The assignment is between two pointers types
    /// which point to integers which have a different sign, but are otherwise
    /// identical. This is a subset of the above, but broken out because it's by
    /// far the most common case of incompatible pointers.
    IncompatiblePointerSign,

    /// CompatiblePointerDiscardsQualifiers - The assignment discards
    /// c/v/r qualifiers, which we accept as an extension.
    CompatiblePointerDiscardsQualifiers,

    /// IncompatiblePointerDiscardsQualifiers - The assignment
    /// discards qualifiers that we don't permit to be discarded,
    /// like address spaces.
    IncompatiblePointerDiscardsQualifiers,

    /// IncompatibleNestedPointerAddressSpaceMismatch - The assignment
    /// changes address spaces in nested pointer types which is not allowed.
    /// For instance, converting __private int ** to __generic int ** is
    /// illegal even though __private could be converted to __generic.
    IncompatibleNestedPointerAddressSpaceMismatch,

    /// IncompatibleNestedPointerQualifiers - The assignment is between two
    /// nested pointer types, and the qualifiers other than the first two
    /// levels differ e.g. char ** -> const char **, but we accept them as an
    /// extension.
    IncompatibleNestedPointerQualifiers,

    /// IncompatibleVectors - The assignment is between two vector types that
    /// have the same size, which we accept as an extension.
    IncompatibleVectors,

    /// Incompatible - We reject this conversion outright, it is invalid to
    /// represent it in the AST.
    Incompatible
  };

  bool DiagnoseAssignmentResult(AssignConvertType ConvTy, SourceLocation Loc,
                                QualType DstType, QualType SrcType,
                                Expr *SrcExpr, AssignmentAction Action,
                                bool *Complained = nullptr);

  bool IsValueInFlagEnum(const EnumDecl *ED, const llvm::APInt &Val,
                         bool AllowMask) const;

  void DiagnoseAssignmentEnum(QualType DstType, QualType SrcType,
                              Expr *SrcExpr);

  AssignConvertType CheckAssignmentConstraints(SourceLocation Loc,
                                               QualType LHSType,
                                               QualType RHSType);

  AssignConvertType CheckAssignmentConstraints(QualType LHSType,
                                               ExprResult &RHS, CastKind &Kind,
                                               bool ConvertRHS = true);

  AssignConvertType CheckSingleAssignmentConstraints(QualType LHSType,
                                                     ExprResult &RHS,
                                                     bool Diagnose = true,
                                                     bool ConvertRHS = true);

  // If the lhs type is a transparent union, check whether we
  // can initialize the transparent union with the given expression.
  AssignConvertType CheckTransparentUnionArgumentConstraints(QualType ArgType,
                                                             ExprResult &RHS);

  bool IsStringLiteralToNonConstPointerConversion(Expr *From, QualType ToType);

  ExprResult PerformImplicitConversion(Expr *From, QualType ToType,
                                       AssignmentAction Action);
  ExprResult
  PerformImplicitConversion(Expr *From, QualType ToType,
                            const ImplicitConversionSequence &ICS,
                            AssignmentAction Action,
                            CheckedConversionKind CCK = CCK_ImplicitConversion);
  ExprResult PerformImplicitConversion(Expr *From, QualType ToType,
                                       const StandardConversionSequence &SCS,
                                       CheckedConversionKind CCK);

  QualType InvalidOperands(SourceLocation Loc, ExprResult &LHS,
                           ExprResult &RHS);
  QualType InvalidLogicalVectorOperands(SourceLocation Loc, ExprResult &LHS,
                                        ExprResult &RHS);
  QualType CheckMultiplyDivideOperands( // C99 6.5.5
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc, bool IsCompAssign,
      bool IsDivide);
  QualType CheckRemainderOperands( // C99 6.5.5
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      bool IsCompAssign = false);
  QualType CheckAdditionOperands( // C99 6.5.6
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      BinaryOperatorKind Opc, QualType *CompLHSTy = nullptr);
  QualType CheckSubtractionOperands( // C99 6.5.6
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      QualType *CompLHSTy = nullptr);
  QualType CheckShiftOperands( // C99 6.5.7
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      BinaryOperatorKind Opc, bool IsCompAssign = false);
  void CheckPtrComparisonWithNullChar(ExprResult &E, ExprResult &NullE);
  QualType CheckCompareOperands( // C99 6.5.8/9
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      BinaryOperatorKind Opc);
  QualType CheckBitwiseOperands( // C99 6.5.[10...12]
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      BinaryOperatorKind Opc);
  QualType CheckLogicalOperands( // C99 6.5.[13,14]
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      BinaryOperatorKind Opc);
  // CheckAssignmentOperands is used for both simple and compound assignment.
  // For simple assignment, pass both expressions and a null converted type.
  // For compound assignment, pass both expressions and the converted type.
  QualType CheckAssignmentOperands( // C99 6.5.16.[1,2]
      Expr *LHSExpr, ExprResult &RHS, SourceLocation Loc, QualType CompoundType,
      BinaryOperatorKind Opc);

  QualType CheckConditionalOperands( // C99 6.5.15
      ExprResult &Cond, ExprResult &LHS, ExprResult &RHS, ExprValueKind &VK,
      ExprObjectKind &OK, SourceLocation QuestionLoc);
  bool DiagnoseConditionalForNull(Expr *LHSExpr, Expr *RHSExpr,
                                  SourceLocation QuestionLoc);

  void DiagnoseAlwaysNonNullPointer(Expr *E,
                                    Expr::NullPointerConstantKind NullType,
                                    bool IsEqual, SourceRange Range);

  QualType CheckVectorOperands(ExprResult &LHS, ExprResult &RHS,
                               SourceLocation Loc, bool IsCompAssign,
                               bool AllowBothBool, bool AllowBoolConversion,
                               bool AllowBoolOperation, bool ReportInvalid);
  QualType GetSignedVectorType(QualType V);
  QualType GetSignedSizelessVectorType(QualType V);
  QualType CheckVectorCompareOperands(ExprResult &LHS, ExprResult &RHS,
                                      SourceLocation Loc,
                                      BinaryOperatorKind Opc);
  QualType CheckSizelessVectorCompareOperands(ExprResult &LHS, ExprResult &RHS,
                                              SourceLocation Loc,
                                              BinaryOperatorKind Opc);
  QualType CheckVectorLogicalOperands(ExprResult &LHS, ExprResult &RHS,
                                      SourceLocation Loc);

  // type checking for sizeless vector binary operators.
  QualType CheckSizelessVectorOperands(ExprResult &LHS, ExprResult &RHS,
                                       SourceLocation Loc, bool IsCompAssign,
                                       ArithConvKind OperationKind);

  QualType CheckMatrixElementwiseOperands(ExprResult &LHS, ExprResult &RHS,
                                          SourceLocation Loc,
                                          bool IsCompAssign);
  QualType CheckMatrixMultiplyOperands(ExprResult &LHS, ExprResult &RHS,
                                       SourceLocation Loc, bool IsCompAssign);

  bool isValidSveBitcast(QualType srcType, QualType destType);

  bool areMatrixTypesOfTheSameDimension(QualType srcTy, QualType destTy);

  bool areVectorTypesSameSize(QualType srcType, QualType destType);
  bool areLaxCompatibleVectorTypes(QualType srcType, QualType destType);
  bool isLaxVectorConversion(QualType srcType, QualType destType);

  bool CheckForConstantInitializer(Expr *e, QualType t);

  // CheckMatrixCast - Check type constraints for matrix casts.
  // We allow casting between matrixes of the same dimensions i.e. when they
  // have the same number of rows and column. Returns true if the cast is
  // invalid.
  bool CheckMatrixCast(SourceRange R, QualType DestTy, QualType SrcTy,
                       CastKind &Kind);

  // CheckVectorCast - check type constraints for vectors.
  // Since vectors are an extension, there are no C standard reference for this.
  // We allow casting between vectors and integer datatypes of the same size.
  // returns true if the cast is invalid
  bool CheckVectorCast(SourceRange R, QualType VectorTy, QualType Ty,
                       CastKind &Kind);

  ExprResult prepareVectorSplat(QualType VectorTy, Expr *SplattedExpr);

  // CheckExtVectorCast - check type constraints for extended vectors.
  // Since vectors are an extension, there are no C standard reference for this.
  // We allow casting between vectors and integer datatypes of the same size,
  // or vectors and the element type of that vector.
  // returns the cast expr
  ExprResult CheckExtVectorCast(SourceRange R, QualType DestTy, Expr *CastExpr,
                                CastKind &Kind);

  class ConditionResult {
    Decl *ConditionVar;
    FullExprArg Condition;
    bool Invalid;
    std::optional<bool> KnownValue;

    friend class Sema;
    ConditionResult(Sema &S, Decl *ConditionVar, FullExprArg Condition,
                    bool IsConstexpr)
        : ConditionVar(ConditionVar), Condition(Condition), Invalid(false) {
      if (IsConstexpr && Condition.get()) {
        if (std::optional<llvm::APSInt> Val =
                Condition.get()->getIntegerConstantExpr(S.Context)) {
          KnownValue = !!(*Val);
        }
      }
    }
    explicit ConditionResult(bool Invalid)
        : ConditionVar(nullptr), Condition(nullptr), Invalid(Invalid),
          KnownValue(std::nullopt) {}

  public:
    ConditionResult() : ConditionResult(false) {}
    bool isInvalid() const { return Invalid; }
    std::pair<VarDecl *, Expr *> get() const {
      return std::make_pair(cast_or_null<VarDecl>(ConditionVar),
                            Condition.get());
    }
    std::optional<bool> getKnownValue() const { return KnownValue; }
  };
  static ConditionResult ConditionError() { return ConditionResult(true); }

  enum class ConditionKind {
    Boolean, ///< A boolean condition, from 'if', 'while', 'for', or 'do'.
    Switch   ///< An integral condition for a 'switch' statement.
  };
  QualType PreferredConditionType(ConditionKind K) const {
    return K == ConditionKind::Switch ? Context.IntTy : Context.BoolTy;
  }

  ConditionResult OnCondition(Scope *S, SourceLocation Loc, Expr *SubExpr,
                              ConditionKind CK, bool MissingOK = false);

  ConditionResult OnConditionVariable(Decl *ConditionVar,
                                      SourceLocation StmtLoc, ConditionKind CK);

  ExprResult CheckConditionVariable(VarDecl *ConditionVar,
                                    SourceLocation StmtLoc, ConditionKind CK);
  ExprResult CheckSwitchCondition(SourceLocation SwitchLoc, Expr *Cond);

  ExprResult CheckBooleanCondition(SourceLocation Loc, Expr *E,
                                   bool IsConstexpr = false);

  void DiagnoseAssignmentAsCondition(Expr *E);

  void DiagnoseEqualityWithExtraParens(ParenExpr *ParenE);

  class VerifyICEDiagnoser {
  public:
    bool Suppress;

    VerifyICEDiagnoser(bool Suppress = false) : Suppress(Suppress) {}

    virtual SemaDiagnosticBuilder
    diagnoseNotICEType(Sema &S, SourceLocation Loc, QualType T);
    virtual SemaDiagnosticBuilder diagnoseNotICE(Sema &S,
                                                 SourceLocation Loc) = 0;
    virtual SemaDiagnosticBuilder diagnoseFold(Sema &S, SourceLocation Loc);
    virtual ~VerifyICEDiagnoser() {}
  };

  enum AllowFoldKind {
    NoFold,
    AllowFold,
  };

  ExprResult VerifyIntegerConstantExpression(Expr *E, llvm::APSInt *Result,
                                             VerifyICEDiagnoser &Diagnoser,
                                             AllowFoldKind CanFold = NoFold);
  ExprResult VerifyIntegerConstantExpression(Expr *E, llvm::APSInt *Result,
                                             unsigned DiagID,
                                             AllowFoldKind CanFold = NoFold);
  ExprResult VerifyIntegerConstantExpression(Expr *E,
                                             llvm::APSInt *Result = nullptr,
                                             AllowFoldKind CanFold = NoFold);
  ExprResult VerifyIntegerConstantExpression(Expr *E,
                                             AllowFoldKind CanFold = NoFold) {
    return VerifyIntegerConstantExpression(E, nullptr, CanFold);
  }

  ExprResult VerifyBitField(SourceLocation FieldLoc, IdentifierInfo *FieldName,
                            QualType FieldTy, bool IsMsStruct, Expr *BitWidth);

  SemaDiagnosticBuilder targetDiag(SourceLocation Loc, unsigned DiagID,
                                   const FunctionDecl *FD = nullptr);
  SemaDiagnosticBuilder targetDiag(SourceLocation Loc,
                                   const PartialDiagnostic &PD,
                                   const FunctionDecl *FD = nullptr) {
    return targetDiag(Loc, PD.getDiagID(), FD) << PD;
  }

  void checkTypeSupport(QualType Ty, SourceLocation Loc,
                        ValueDecl *D = nullptr);

  //===--------------------------------------------------------------------===//
  // Extra semantic analysis beyond the C type system

public:
  SourceLocation getLocationOfStringLiteralByte(const StringLiteral *SL,
                                                unsigned ByteNo) const;

  enum FormatArgumentPassingKind {
    FAPK_Fixed,    // values to format are fixed (no C-style variadic arguments)
    FAPK_Variadic, // values to format are passed as variadic arguments
    FAPK_VAList,   // values to format are passed in a va_list
  };

  // Used to grab the relevant information from a FormatAttr and a
  // FunctionDeclaration.
  struct FormatStringInfo {
    unsigned FormatIdx;
    unsigned FirstDataArg;
    FormatArgumentPassingKind ArgPassingKind;
  };

  static bool getFormatStringInfo(const FormatAttr *Format, bool IsVariadic,
                                  FormatStringInfo *FSI);

private:
  void CheckArrayAccess(const Expr *BaseExpr, const Expr *IndexExpr,
                        const ArraySubscriptExpr *ASE = nullptr,
                        bool AllowOnePastEnd = true, bool IndexNegated = false);
  void CheckArrayAccess(const Expr *E);

  bool CheckFunctionCall(FunctionDecl *FDecl, CallExpr *TheCall,
                         const FunctionProtoType *Proto);
  bool CheckPointerCall(NamedDecl *NDecl, CallExpr *TheCall,
                        const FunctionProtoType *Proto);
  bool CheckOtherCall(CallExpr *TheCall, const FunctionProtoType *Proto);
  void CheckArgAlignment(SourceLocation Loc, NamedDecl *FDecl,
                         llvm::StringRef ParamName, QualType ArgTy,
                         QualType ParamTy);

  void checkCall(NamedDecl *FDecl, const FunctionProtoType *Proto,
                 llvm::ArrayRef<const Expr *> Args, SourceLocation Loc,
                 SourceRange Range, VariadicCallType CallType);

  ExprResult CheckOSLogFormatStringArg(Expr *Arg);

  ExprResult CheckBuiltinFunctionCall(FunctionDecl *FDecl, unsigned BuiltinID,
                                      CallExpr *TheCall);

  bool CheckTSBuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                  CallExpr *TheCall);

  void checkFortifiedBuiltinMemoryFunction(FunctionDecl *FD, CallExpr *TheCall);

  bool CheckARMBuiltinExclusiveCall(unsigned BuiltinID, CallExpr *TheCall,
                                    unsigned MaxWidth);
  bool CheckNeonBuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                    CallExpr *TheCall);
  bool CheckSVEBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall);
  bool
  ParseSVEImmChecks(CallExpr *TheCall,
                    llvm::SmallVector<std::tuple<int, int, int>, 3> &ImmChecks);
  bool CheckSMEBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall);
  bool CheckAArch64BuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                       CallExpr *TheCall);
  bool CheckX86BuiltinRoundingOrSAE(unsigned BuiltinID, CallExpr *TheCall);
  bool CheckX86BuiltinGatherScatterScale(unsigned BuiltinID, CallExpr *TheCall);
  bool CheckX86BuiltinTileArguments(unsigned BuiltinID, CallExpr *TheCall);
  bool CheckX86BuiltinTileArgumentsRange(CallExpr *TheCall,
                                         llvm::ArrayRef<int> ArgNums);
  bool CheckX86BuiltinTileDuplicate(CallExpr *TheCall,
                                    llvm::ArrayRef<int> ArgNums);
  bool CheckX86BuiltinTileRangeAndDuplicate(CallExpr *TheCall,
                                            llvm::ArrayRef<int> ArgNums);
  bool CheckX86BuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                   CallExpr *TheCall);
  bool SemaBuiltinVAStart(unsigned BuiltinID, CallExpr *TheCall);
  bool SemaBuiltinVAStartARMMicrosoft(CallExpr *Call);
  bool SemaBuiltinUnorderedCompare(CallExpr *TheCall);
  bool SemaBuiltinFPClassification(CallExpr *TheCall, unsigned NumArgs);
  bool SemaBuiltinComplex(CallExpr *TheCall);
  bool SemaBuiltinOSLogFormat(CallExpr *TheCall);

public:
  ExprResult SemaBuiltinShuffleVector(CallExpr *TheCall);
  ExprResult SemaConvertVectorExpr(Expr *E, TypeSourceInfo *TInfo,
                                   SourceLocation BuiltinLoc,
                                   SourceLocation RParenLoc);

private:
  bool SemaBuiltinPrefetch(CallExpr *TheCall);
  bool SemaBuiltinAllocaWithAlign(CallExpr *TheCall);
  bool SemaBuiltinArithmeticFence(CallExpr *TheCall);
  bool SemaBuiltinAssume(CallExpr *TheCall);
  bool SemaBuiltinAssumeAligned(CallExpr *TheCall);
  bool SemaBuiltinLongjmp(CallExpr *TheCall);
  bool SemaBuiltinSetjmp(CallExpr *TheCall);
  ExprResult SemaBuiltinAtomicOverloaded(ExprResult TheCallResult);
  ExprResult SemaBuiltinNontemporalOverloaded(ExprResult TheCallResult);
  ExprResult SemaAtomicOpsOverloaded(ExprResult TheCallResult,
                                     AtomicExpr::AtomicOp Op);
  bool SemaBuiltinConstantArg(CallExpr *TheCall, int ArgNum,
                              llvm::APSInt &Result);
  bool SemaBuiltinConstantArgRange(CallExpr *TheCall, int ArgNum, int Low,
                                   int High, bool RangeIsError = true);
  bool SemaBuiltinConstantArgMultiple(CallExpr *TheCall, int ArgNum,
                                      unsigned Multiple);
  bool SemaBuiltinConstantArgPower2(CallExpr *TheCall, int ArgNum);
  bool SemaBuiltinARMSpecialReg(unsigned BuiltinID, CallExpr *TheCall,
                                int ArgNum, unsigned ExpectedFieldNum,
                                bool AllowName);
  bool SemaBuiltinARMMemoryTaggingCall(unsigned BuiltinID, CallExpr *TheCall);

  bool SemaBuiltinElementwiseMath(CallExpr *TheCall);
  bool SemaBuiltinElementwiseTernaryMath(CallExpr *TheCall);
  bool PrepareBuiltinElementwiseMathOneArgCall(CallExpr *TheCall);
  bool PrepareBuiltinReduceMathOneArgCall(CallExpr *TheCall);

  bool SemaBuiltinNonDeterministicValue(CallExpr *TheCall);

  // Matrix builtin handling.
  ExprResult SemaBuiltinMatrixTranspose(CallExpr *TheCall,
                                        ExprResult CallResult);
  ExprResult SemaBuiltinMatrixColumnMajorLoad(CallExpr *TheCall,
                                              ExprResult CallResult);
  ExprResult SemaBuiltinMatrixColumnMajorStore(CallExpr *TheCall,
                                               ExprResult CallResult);

public:
  enum FormatStringType {
    FST_Scanf,
    FST_Printf,
    FST_Strftime,
    FST_Strfmon,
    FST_OSTrace,
    FST_OSLog,
    FST_Unknown
  };
  static FormatStringType GetFormatStringType(const FormatAttr *Format);

private:
  bool CheckFormatArguments(const FormatAttr *Format,
                            llvm::ArrayRef<const Expr *> Args,
                            VariadicCallType CallType, SourceLocation Loc,
                            SourceRange Range,
                            llvm::SmallBitVector &CheckedVarArgs);
  bool CheckFormatArguments(llvm::ArrayRef<const Expr *> Args,
                            FormatArgumentPassingKind FAPK, unsigned format_idx,
                            unsigned firstDataArg, FormatStringType Type,
                            VariadicCallType CallType, SourceLocation Loc,
                            SourceRange range,
                            llvm::SmallBitVector &CheckedVarArgs);

  void CheckAbsoluteValueFunction(const CallExpr *Call,
                                  const FunctionDecl *FDecl);

  void CheckMemaccessArguments(const CallExpr *Call, unsigned BId,
                               IdentifierInfo *FnName);

  void CheckStrlcpycatArguments(const CallExpr *Call, IdentifierInfo *FnName);

  void CheckStrncatArguments(const CallExpr *Call, IdentifierInfo *FnName);

  void CheckFreeArguments(const CallExpr *E);

  void CheckReturnValExpr(Expr *RetValExp, QualType lhsType,
                          SourceLocation ReturnLoc,
                          const AttrVec *Attrs = nullptr);

public:
  void CheckFloatComparison(SourceLocation Loc, Expr *LHS, Expr *RHS,
                            BinaryOperatorKind Opcode);

private:
  void CheckImplicitConversions(Expr *E, SourceLocation CC = SourceLocation());
  void CheckBoolLikeConversion(Expr *E, SourceLocation CC);
  void CheckForIntOverflow(const Expr *E);
  void CheckUnsequencedOperations(const Expr *E);

  void CheckCompletedExpr(Expr *E, SourceLocation CheckLoc = SourceLocation(),
                          bool IsConstexpr = false);

  void CheckBitFieldInitialization(SourceLocation InitLoc, FieldDecl *Field,
                                   Expr *Init);

  void CheckBreakContinueBinding(Expr *E);

  void CheckTCBEnforcement(const SourceLocation CallExprLoc,
                           const NamedDecl *Callee);

public:
  void RegisterTypeTagForDatatype(const IdentifierInfo *ArgumentKind,
                                  uint64_t MagicValue, QualType Type,
                                  bool LayoutCompatible, bool MustBeNull);

  struct TypeTagData {
    TypeTagData() {}

    TypeTagData(QualType Type, bool LayoutCompatible, bool MustBeNull)
        : Type(Type), LayoutCompatible(LayoutCompatible),
          MustBeNull(MustBeNull) {}

    QualType Type;

    /// If true, \c Type should be compared with other expression's types for
    /// layout-compatibility.
    LLVM_PREFERRED_TYPE(bool)
    unsigned LayoutCompatible : 1;
    LLVM_PREFERRED_TYPE(bool)
    unsigned MustBeNull : 1;
  };

  using TypeTagMagicValue = std::pair<const IdentifierInfo *, uint64_t>;

private:
  std::unique_ptr<llvm::DenseMap<TypeTagMagicValue, TypeTagData>>
      TypeTagForDatatypeMagicValues;

  void CheckArgumentWithTypeTag(const ArgumentWithTypeTagAttr *Attr,
                                const llvm::ArrayRef<const Expr *> ExprArgs,
                                SourceLocation CallSiteLoc);

  void CheckAddressOfPackedMember(Expr *rhs);

  Scope *CurScope;

  IdentifierInfo *Ident__Nonnull = nullptr;
  IdentifierInfo *Ident__Nullable = nullptr;
  IdentifierInfo *Ident__Null_unspecified = nullptr;

  sema::SemaPrepEngineObserver *SemaObserverHandler;

protected:
  friend class Parser;
  friend class InitializationSequence;

public:
  IdentifierInfo *getNullabilityKeyword(NullabilityKind nullability);

  Scope *getCurScope() const { return CurScope; }

  void incrementMSManglingNumber() const {
    return CurScope->incrementMSManglingNumber();
  }

  DeclContext *getCurLexicalContext() const {
    return OriginalLexicalContext ? OriginalLexicalContext : CurContext;
  }

  static bool TooManyArguments(size_t NumParams, size_t NumArgs) {
    return NumArgs > NumParams;
  }

private:
  struct MisalignedMember {
    Expr *E;
    RecordDecl *RD;
    ValueDecl *MD;
    CharUnits Alignment;

    MisalignedMember() : E(), RD(), MD() {}
    MisalignedMember(Expr *E, RecordDecl *RD, ValueDecl *MD,
                     CharUnits Alignment)
        : E(E), RD(RD), MD(MD), Alignment(Alignment) {}
    explicit MisalignedMember(Expr *E)
        : MisalignedMember(E, nullptr, nullptr, CharUnits()) {}

    bool operator==(const MisalignedMember &m) { return this->E == m.E; }
  };
  llvm::SmallVector<MisalignedMember, 4> MisalignedMembers;

  void AddPotentialMisalignedMembers(Expr *E, RecordDecl *RD, ValueDecl *MD,
                                     CharUnits Alignment);

public:
  void DiagnoseMisalignedMembers();

  void DiscardMisalignedMemberAddress(const Type *T, Expr *E);

  void RefersToMemberWithReducedAlignment(
      Expr *E,
      llvm::function_ref<void(Expr *, RecordDecl *, FieldDecl *, CharUnits)>
          Action);

  enum class CallingConventionIgnoredReason {
    ForThisTarget = 0,
    VariadicFunction,
    BuiltinFunction
  };
};

} // end namespace neverc

#endif
