#include "neverc/Analyze/Sema.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaConsumer.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/Stack.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeDiag.h"
#include "llvm/Support/TimeProfiler.h"
#include <optional>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Sema utilities
// ===----------------------------------------------------------------------===

SourceLocation Sema::getLocForEndOfToken(SourceLocation Loc, unsigned Offset) {
  return SourceScanner::getLocForEndOfToken(Loc, Offset, SourceMgr, LangOpts);
}

bool Sema::isNeverCStringType(QualType T) const {
  if (T.isNull())
    return false;

  const Type *TP = T.getTypePtr();
  const RecordType *RT;
  if (LLVM_LIKELY(TP->isRecordType())) {
    // Common path: direct RecordType -- skip the atomic-unwrap +
    // second getAs<> that getAtomicUnqualifiedType() would perform.
    RT = TP->getAs<RecordType>();
  } else {
    if (!TP->isAtomicType())
      return false;
    RT = T.getAtomicUnqualifiedType()->getAs<RecordType>();
  }
  if (!RT)
    return false;

  RecordDecl *RD = RT->getDecl();

  if (NeverCStringRDCache && RD == NeverCStringRDCache)
    return true;

  const IdentifierInfo *II = RD->getIdentifier();
  if (!II)
    return false;

  if (NeverCStringIICache) {
    if (II != NeverCStringIICache)
      return false;
    if (RecordDecl *Def = RD->getDefinition())
      RD = Def;
    NeverCStringRDCache = RD;
    return true;
  }

  if (!II->isStr(BuiltinStringNames::RecordName))
    return false;

  NeverCStringIICache = II;
  if (RecordDecl *Def = RD->getDefinition())
    RD = Def;
  NeverCStringRDCache = RD;
  return true;
}

void Sema::ensureNeverCStringFnKinds() const {
  if (NeverCStringFnKindsReady)
    return;
  NeverCStringFnKindsReady = true;

  auto Mark = [&](llvm::StringRef Name, uint8_t Kind) {
    NeverCStringFnKinds[&Context.Idents.get(Name)] |= Kind;
  };

#define NEVERC_BUILTIN_STRING_FN(NameId, Spelling, IsPublic)                   \
  Mark(Spelling, NCSFK_Runtime);
#include "neverc/Foundation/Builtin/BuiltinStringRoster.def"
#undef NEVERC_BUILTIN_STRING_FN

#define NEVERC_BUILTIN_STRING_BORROWED_VIEW_HELPER(NameId)                     \
  Mark(BuiltinStringNames::NameId##FunctionName, NCSFK_BorrowedView);
#include "neverc/Foundation/Builtin/BuiltinStringBorrowedViewHelpers.def"
#undef NEVERC_BUILTIN_STRING_BORROWED_VIEW_HELPER

#define NEVERC_BUILTIN_STRING_WPTR_PRODUCER(NameId)                            \
  Mark(BuiltinStringNames::NameId##FunctionName, NCSFK_WptrProducer);
#include "neverc/Foundation/Builtin/BuiltinStringWptrProducers.def"
#undef NEVERC_BUILTIN_STRING_WPTR_PRODUCER
}

bool Sema::hasNeverCStringFnKind(const FunctionDecl *FD, uint8_t Mask) const {
  if (!FD || !FD->getIdentifier())
    return false;
  ensureNeverCStringFnKinds();
  auto It = NeverCStringFnKinds.find(FD->getIdentifier());
  return It != NeverCStringFnKinds.end() && (It->second & Mask);
}

bool Sema::isNeverCStringRuntimeFD(const FunctionDecl *FD) const {
  if (hasNeverCStringFnKind(FD, NCSFK_Runtime))
    return true;
  if (!FD || !FD->getIdentifier())
    return false;
  return FD->getName().starts_with(
      BuiltinStringNames::InternalFunctionPrefix);
}

bool Sema::isInsideNeverCStringRuntime() const {
  return isNeverCStringRuntimeFD(
      llvm::dyn_cast_or_null<FunctionDecl>(CurContext));
}

bool Sema::isInsideNeverCStringRuntime(const Decl *D) const {
  for (const DeclContext *DC = D ? D->getDeclContext() : nullptr; DC;
       DC = DC->getParent()) {
    if (const auto *FD = llvm::dyn_cast<FunctionDecl>(DC))
      return isNeverCStringRuntimeFD(FD);
  }
  return false;
}

bool Sema::isNeverCStringBorrowedViewFD(const FunctionDecl *FD) const {
  return hasNeverCStringFnKind(FD, NCSFK_BorrowedView);
}

bool Sema::isNeverCStringWptrProducer(const FunctionDecl *FD) const {
  return hasNeverCStringFnKind(FD, NCSFK_WptrProducer);
}

FunctionDecl *Sema::lookupNeverCStringFunctionDecl(llvm::StringRef Name,
                                                   Scope *Sc,
                                                   SourceLocation Loc) {
  IdentifierInfo &II = Context.Idents.get(Name);
  auto CacheIt = NeverCStringFDCache.find(&II);
  if (CacheIt != NeverCStringFDCache.end())
    return CacheIt->second;
  LookupResult R(*this, &II, Loc, ResolveOrdinary);
  ResolveName(R, Sc ? Sc : TUScope, /*AllowBuiltinCreation=*/false);
  FunctionDecl *FD = R.getAsSingle<FunctionDecl>();
  if (FD)
    NeverCStringFDCache[&II] = FD;
  return FD;
}

PrintingPolicy Sema::getPrintingPolicy(const TreeContext &Context,
                                       const PrepEngine &PP) {
  PrintingPolicy Policy{Context.getPrintingPolicy()};
  // In diagnostics, we print _Bool as bool if the latter is defined as the
  // former.
  Policy.Bool = Context.getLangOpts().Bool;
  if (!Policy.Bool) {
    if (const MacroRecord *BoolMacro =
            PP.getMacroRecord(Context.getBoolName())) {
      Policy.Bool = BoolMacro->isObjectLike() &&
                    BoolMacro->getNumTokens() == 1 &&
                    BoolMacro->getReplacementToken(0).is(tok::kw__Bool);
    }
  }

  // Shorten the data output if needed
  Policy.EntireContentsOfLargeArray = false;

  return Policy;
}

void Sema::OnTranslationUnitScope(Scope *S) {
  TUScope = S;
  PushDeclContext(S, Context.getTranslationUnitDecl());
}

namespace neverc {
namespace sema {

class SemaPrepEngineObserver : public PrepObserver {
  Sema *S = nullptr;
  llvm::SmallVector<SourceLocation, 8> IncludeStack;

public:
  void set(Sema &S) { this->S = &S; }

  void reset() { S = nullptr; }

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override {
    if (!S)
      return;
    switch (Reason) {
    case EnterFile: {
      SourceManager &SM = S->getSourceManager();
      SourceLocation IncludeLoc = SM.getIncludeLoc(SM.getFileID(Loc));
      if (IncludeLoc.isValid()) {
        if (llvm::timeTraceProfilerEnabled()) {
          OptionalFileEntryRef FE = SM.getFileEntryRefForID(SM.getFileID(Loc));
          llvm::timeTraceProfilerBegin(
              "Source", FE ? FE->getName() : llvm::StringRef("<unknown>"));
        }

        IncludeStack.push_back(IncludeLoc);
      }
      break;
    }
    case ExitFile:
      if (!IncludeStack.empty()) {
        if (llvm::timeTraceProfilerEnabled())
          llvm::timeTraceProfilerEnd();

        IncludeStack.pop_back();
      }
      break;
    default:
      break;
    }
  }
};

} // end namespace sema
} // end namespace neverc

const unsigned Sema::MaxAlignmentExponent;
const uint64_t Sema::MaximumAlignment;

Sema::Sema(PrepEngine &pp, TreeContext &ctxt, TreeConsumer &consumer)
    : CurFPFeatures(pp.getLangOpts()), LangOpts(pp.getLangOpts()), PP(pp),
      Context(ctxt), Consumer(consumer), Diags(PP.getDiagnostics()),
      SourceMgr(PP.getSourceManager()), CurContext(nullptr),
      OriginalLexicalContext(nullptr), MSStructPragmaOn(false),
      AlignPackStack(AlignPackInfo()), DataSegStack(nullptr),
      BSSSegStack(nullptr), ConstSegStack(nullptr), CodeSegStack(nullptr),
      StrictGuardStackCheckStack(false), FpPragmaStack(FPOptionsOverride()),
      CurInitSeg(nullptr), VisContext(nullptr),
      PragmaAttributeCurrentTargetDecl(nullptr), IdResolver(pp),
      DisableTypoCorrection(false), TyposCorrected(0), CurScope(nullptr) {
  TUScope = nullptr;

  // Tell diagnostics how to render things from the AST library.
  Diags.SetArgToStringFn(&FormatASTNodeDiagnosticArgument, &Context);

  // This evaluation context exists to ensure that there's always at least one
  // valid evaluation context available. It is never removed from the
  // evaluation stack.
  ExprEvalContexts.emplace_back(
      ExpressionEvaluationContext::PotentiallyEvaluated, 0, CleanupInfo{});

  std::unique_ptr<sema::SemaPrepEngineObserver> Callbacks =
      std::make_unique<sema::SemaPrepEngineObserver>();
  SemaObserverHandler = Callbacks.get();
  PP.addObserver(std::move(Callbacks));
  SemaObserverHandler->set(*this);

  CurFPFeatures.setFPEvalMethod(PP.getCurrentFPEvalMethod());

  TrackUnusedButSet =
      !Diags.isIgnored(diag::warn_unused_but_set_variable, SourceLocation()) ||
      !Diags.isIgnored(diag::warn_unused_but_set_parameter, SourceLocation());
}

// Anchor Sema's type info to this TU.
void Sema::anchor() {}

void Sema::addImplicitTypedef(llvm::StringRef Name, QualType T) {
  DeclarationName DN = &Context.Idents.get(Name);
  if (IdResolver.begin(DN) == IdResolver.end())
    PushOnScopeChains(Context.buildImplicitTypedef(T, Name), TUScope);
}

void Sema::Initialize() {
  if (SemaConsumer *SC = dyn_cast<SemaConsumer>(&Consumer))
    SC->InitializeSema(*this);

  VAListTagName = PP.getIdentifierInfo("__va_list_tag");

  if (!TUScope)
    return;

  // Initialize predefined 128-bit integer types, if needed.
  if (Context.getTargetInfo().hasInt128Type()) {
    // If either of the 128-bit integer types are unavailable to name lookup,
    // define them now.
    DeclarationName Int128 = &Context.Idents.get("__int128_t");
    if (IdResolver.begin(Int128) == IdResolver.end())
      PushOnScopeChains(Context.getInt128Decl(), TUScope);

    DeclarationName UInt128 = &Context.Idents.get("__uint128_t");
    if (IdResolver.begin(UInt128) == IdResolver.end())
      PushOnScopeChains(Context.getUInt128Decl(), TUScope);
  }

  // Initialize predefined types.
  if (getLangOpts().MSVCCompat)
    addImplicitTypedef("size_t", Context.getSizeType());

  if (Context.getTargetInfo().hasAArch64SVETypes()) {
#define SVE_TYPE(Name, Id, SingletonId)                                        \
  addImplicitTypedef(Name, Context.SingletonId);
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
  }

  if (Context.getTargetInfo().hasBuiltinMSVaList()) {
    DeclarationName MSVaList = &Context.Idents.get("__builtin_ms_va_list");
    if (IdResolver.begin(MSVaList) == IdResolver.end())
      PushOnScopeChains(Context.getBuiltinMSVaListDecl(), TUScope);
  }

  DeclarationName BuiltinVaList = &Context.Idents.get("__builtin_va_list");
  if (IdResolver.begin(BuiltinVaList) == IdResolver.end())
    PushOnScopeChains(Context.getBuiltinVaListDecl(), TUScope);
}

Sema::~Sema() {
  if (VisContext)
    FreeVisContext();

  // Kill all the active scopes.
  for (sema::FunctionScopeInfo *FSI : FunctionScopes)
    delete FSI;

  // Tell the SemaConsumer to forget about us; we're going out of scope.
  if (SemaConsumer *SC = dyn_cast<SemaConsumer>(&Consumer))
    SC->ForgetSema();

  // Detach from the PP callback handler which outlives Sema since it's owned
  // by the preprocessor.
  SemaObserverHandler->reset();
}

void Sema::warnStackExhausted(SourceLocation Loc) {
  // Only warn about this once.
  if (!WarnedStackExhausted) {
    Diag(Loc, diag::warn_stack_exhausted);
    WarnedStackExhausted = true;
  }
}

void Sema::runWithSufficientStackSpace(SourceLocation Loc,
                                       llvm::function_ref<void()> Fn) {
  neverc::runWithSufficientStackSpace([&] { warnStackExhausted(Loc); }, Fn);
}

bool Sema::makeUnavailableInSystemHeader(
    SourceLocation loc, UnavailableAttr::ImplicitReason reason) {
  // If we're not in a function, it's an error.
  FunctionDecl *fn = dyn_cast<FunctionDecl>(CurContext);
  if (!fn)
    return false;

  // If that function's not in a system header, it's an error.
  if (!Context.getSourceManager().isInSystemHeader(loc))
    return false;

  // If the function is already unavailable, it's not an error.
  if (fn->hasAttr<UnavailableAttr>())
    return true;

  fn->addAttr(UnavailableAttr::CreateImplicit(Context, "", reason, loc));
  return true;
}

void Sema::diagnoseNullableToNonnullConversion(QualType DstType,
                                               QualType SrcType,
                                               SourceLocation Loc) {
  std::optional<NullabilityKind> ExprNullability = SrcType->getNullability();
  if (!ExprNullability || *ExprNullability != NullabilityKind::Nullable)
    return;

  std::optional<NullabilityKind> TypeNullability = DstType->getNullability();
  if (!TypeNullability || *TypeNullability != NullabilityKind::NonNull)
    return;

  Diag(Loc, diag::warn_nullability_lost) << SrcType << DstType;
}

ExprResult Sema::ImpCastExprToType(Expr *E, QualType Ty, CastKind Kind,
                                   ExprValueKind VK,
                                   CheckedConversionKind CCK) {
#ifndef NDEBUG
  if (VK == VK_PRValue && !E->isPRValue()) {
    switch (Kind) {
    default:
      llvm_unreachable(
          ("can't implicitly cast lvalue to prvalue with this cast "
           "kind: " +
           std::string(CastExpr::getCastKindName(Kind)))
              .c_str());
    case CK_Dependent:
    case CK_LValueToRValue:
    case CK_ArrayToPointerDecay:
    case CK_FunctionToPointerDecay:
    case CK_ToVoid:
    case CK_NonAtomicToAtomic:
      break;
    }
  }
  assert((VK == VK_PRValue || Kind == CK_Dependent || !E->isPRValue()) &&
         "can't cast prvalue to lvalue");
#endif

  QualType ETy = E->getType();
  if (LLVM_LIKELY(ETy == Ty))
    return E;

  QualType ExprTy = Context.getCanonicalType(ETy);
  QualType TypeTy = Context.getCanonicalType(Ty);

  if (LLVM_LIKELY(ExprTy == TypeTy))
    return E;

  diagnoseNullableToNonnullConversion(Ty, ETy, E->getBeginLoc());

  if (Kind == CK_ArrayToPointerDecay) {
    // C17 6.7.1p6 footnote 124: The implementation can treat any register
    // declaration simply as an auto declaration. However, whether or not
    // addressable storage is actually used, the address of any part of an
    // object declared with storage-class specifier register cannot be
    // computed, either explicitly(by use of the unary & operator as discussed
    // in 6.5.3.2) or implicitly(by converting an array name to a pointer as
    // discussed in 6.3.2.1).Thus, the only operator that can be applied to an
    // array declared with storage-class specifier register is sizeof.
    if (VK == VK_PRValue && !E->isPRValue()) {
      if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (VD->getStorageClass() == SC_Register) {
            Diag(E->getExprLoc(), diag::err_typecheck_address_of)
                << /*register variable*/ 3 << E->getSourceRange();
            return ExprError();
          }
        }
      }
    }
  }

  if (ImplicitCastExpr *ImpCast = dyn_cast<ImplicitCastExpr>(E)) {
    if (ImpCast->getCastKind() == Kind) {
      ImpCast->setType(Ty);
      ImpCast->setValueKind(VK);
      return E;
    }
  }

  return ImplicitCastExpr::Create(Context, Ty, Kind, E, VK,
                                  CurFPFeatureOverrides());
}

CastKind Sema::ScalarTypeToBooleanCastKind(QualType ScalarTy) {
  switch (ScalarTy->getScalarTypeKind()) {
  case Type::STK_Bool:
    return CK_NoOp;
  case Type::STK_CPointer:
    return CK_PointerToBoolean;
  case Type::STK_Integral:
    return CK_IntegralToBoolean;
  case Type::STK_Floating:
    return CK_FloatingToBoolean;
  case Type::STK_IntegralComplex:
    return CK_IntegralComplexToBoolean;
  case Type::STK_FloatingComplex:
    return CK_FloatingComplexToBoolean;
  case Type::STK_FixedPoint:
    return CK_FixedPointToBoolean;
  }
  llvm_unreachable("unknown scalar type kind");
}

namespace {
bool isNoLongerUnused(Sema *SemaRef, const DeclaratorDecl *D) {
  if (D->getMostRecentDecl()->isUsed())
    return true;

  if (D->isExternallyVisible())
    return true;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    // UnusedFileScopedDecls stores the first declaration.
    // The declaration may have become definition so check again.
    const FunctionDecl *DeclToCheck;
    if (FD->hasBody(DeclToCheck))
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);

    // Later redecls may add new information resulting in not having to warn,
    // so check again.
    DeclToCheck = FD->getMostRecentDecl();
    if (DeclToCheck != FD)
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);
  }

  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    // UnusedFileScopedDecls stores the first declaration.
    // The declaration may have become definition so check again.
    const VarDecl *DeclToCheck = VD->getDefinition();
    if (DeclToCheck)
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);

    // Later redecls may add new information resulting in not having to warn,
    // so check again.
    DeclToCheck = VD->getMostRecentDecl();
    if (DeclToCheck != VD)
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);
  }

  return false;
}
} // namespace

void Sema::getUndefinedButUsed(
    llvm::SmallVectorImpl<std::pair<NamedDecl *, SourceLocation>> &Undefined) {
  for (const auto &UndefinedUse : UndefinedButUsed) {
    NamedDecl *ND = UndefinedUse.first;

    // Ignore attributes that have become invalid.
    if (ND->isInvalidDecl())
      continue;

    // __attribute__((weakref)) is basically a definition.
    if (ND->hasAttr<WeakRefAttr>())
      continue;

    if (ND->hasAttr<DLLImportAttr>() || ND->hasAttr<DLLExportAttr>()) {
      // An exported function will always be emitted when defined, so even if
      // the function is inline, it doesn't have to be emitted in this TU. An
      // imported function implies that it has been exported somewhere else.
      continue;
    }

    if (const auto *FD = dyn_cast<FunctionDecl>(ND)) {
      if (FD->isDefined())
        continue;
      if (FD->isExternallyVisible() && !FD->getMostRecentDecl()->isInlined())
        continue;
      if (FD->getBuiltinID())
        continue;
    } else {
      const auto *VD = cast<VarDecl>(ND);
      if (VD->hasDefinition() != VarDecl::DeclarationOnly)
        continue;
      if (VD->isExternallyVisible() && !VD->getMostRecentDecl()->isInline())
        continue;

      // Skip VarDecls that lack formal definitions but which we know are in
      // fact defined somewhere.
      if (VD->isKnownToBeDefined())
        continue;
    }

    Undefined.push_back(std::make_pair(ND, UndefinedUse.second));
  }
}

namespace {
void diagnoseUndefinedUsedDecls(Sema &S) {
  if (S.UndefinedButUsed.empty())
    return;

  // Collect all the still-undefined entities with internal linkage.
  llvm::SmallVector<std::pair<NamedDecl *, SourceLocation>, 16> Undefined;
  S.getUndefinedButUsed(Undefined);
  S.UndefinedButUsed.clear();
  if (Undefined.empty())
    return;

  for (const auto &Undef : Undefined) {
    ValueDecl *VD = cast<ValueDecl>(Undef.first);
    SourceLocation UseLoc = Undef.second;

    if (!VD->isExternallyVisible()) {
      S.Diag(VD->getLocation(), diag::warn_undefined_internal)
          << isa<VarDecl>(VD) << VD;
    } else if ([[maybe_unused]] auto *FD = dyn_cast<FunctionDecl>(VD)) {
      assert(FD->getMostRecentDecl()->isInlined() &&
             "used object requires definition but isn't inline or internal?");
      S.Diag(VD->getLocation(), diag::warn_undefined_inline) << VD;
    } else {
      assert(cast<VarDecl>(VD)->getMostRecentDecl()->isInline() &&
             "used var requires definition but isn't inline or internal?");
      S.Diag(VD->getLocation(), diag::err_undefined_inline_var) << VD;
    }
    if (UseLoc.isValid())
      S.Diag(UseLoc, diag::note_used_here);
  }
}
} // namespace

void Sema::FlushUnusedTypedefWarnings() {
  for (const TypedefNameDecl *TD : UnusedLocalTypedefNameCandidates) {
    if (TD->isReferenced())
      continue;
    Diag(TD->getLocation(), diag::warn_unused_local_typedef)
        << 0 << TD->getDeclName();
  }
  UnusedLocalTypedefNameCandidates.clear();
}

void Sema::OnEndOfTranslationUnit() {
  assert(DelayedDiagnostics.getCurrentPool() == nullptr &&
         "reached end of translation unit with a pool attached?");

  for (const auto &Typo : DelayedTypos)
    Typo.second.DiagHandler(TypoCorrection());
  DelayedTypos.clear();

  DiagnoseUnterminatedPragmaAlignPack();
  DiagnoseUnterminatedPragmaAttribute();

  // Remove file scoped decls that turned out to be used.
  UnusedFileScopedDecls.erase(
      std::remove_if(UnusedFileScopedDecls.begin(), UnusedFileScopedDecls.end(),
                     [this](const DeclaratorDecl *DD) {
                       return isNoLongerUnused(this, DD);
                     }),
      UnusedFileScopedDecls.end());

  // Check for #pragma weak identifiers that were never declared
  for (const auto &WeakIDs : WeakUndeclaredIdentifiers) {
    if (WeakIDs.second.empty())
      continue;

    Decl *PrevDecl = LookupSingleName(TUScope, WeakIDs.first, SourceLocation(),
                                      ResolveOrdinary);
    if (PrevDecl != nullptr &&
        !(isa<FunctionDecl>(PrevDecl) || isa<VarDecl>(PrevDecl)))
      for (const auto &WI : WeakIDs.second)
        Diag(WI.getLocation(), diag::warn_attribute_wrong_decl_type)
            << "'weak'" << /*isRegularKeyword=*/0 << ExpectedVariableOrFunction;
    else
      for (const auto &WI : WeakIDs.second)
        Diag(WI.getLocation(), diag::warn_weak_identifier_undeclared)
            << WeakIDs.first;
  }

  if (!Diags.hasErrorOccurred()) {
    diagnoseUndefinedUsedDecls(*this);
  }

  // C99 6.9.2p2:
  //   A declaration of an identifier for an object that has file
  //   scope without an initializer, and without a storage-class
  //   specifier or with the storage-class specifier static,
  //   constitutes a tentative definition. If a translation unit
  //   contains one or more tentative definitions for an identifier,
  //   and the translation unit contains no external definition for
  //   that identifier, then the behavior is exactly as if the
  //   translation unit contains a file scope declaration of that
  //   identifier, with the composite type as of the end of the
  //   translation unit, with an initializer equal to 0.
  llvm::SmallSet<VarDecl *, 32> Seen;
  for (auto *TD : TentativeDefinitions) {
    VarDecl *VD = TD->getActingDefinition();

    // If the tentative definition was completed, getActingDefinition() returns
    // null. If we've already seen this variable before, insert()'s second
    // return value is false.
    if (!VD || VD->isInvalidDecl() || !Seen.insert(VD).second)
      continue;

    if (const IncompleteArrayType *ArrayT =
            Context.getAsIncompleteArrayType(VD->getType())) {
      // Set the length of the array to 1 (C99 6.9.2p5).
      Diag(VD->getLocation(), diag::warn_tentative_incomplete_array);
      llvm::APInt One(Context.getTypeSize(Context.getSizeType()), true);
      QualType T = Context.getConstantArrayType(
          ArrayT->getElementType(), One, nullptr, ArraySizeModifier::Normal, 0);
      VD->setType(T);
    } else if (RequireCompleteType(VD->getLocation(), VD->getType(),
                                   diag::err_tentative_def_incomplete_type))
      VD->setInvalidDecl();

    // No initialization is performed for a tentative definition.
    CheckCompleteVariableDeclaration(VD);

    // Notify the consumer that we've completed a tentative definition.
    if (!VD->isInvalidDecl())
      Consumer.FinalizeTentativeDefinition(VD);
  }

  for (auto *D : ExternalDeclarations) {
    if (!D || D->isInvalidDecl() || D->getPreviousDecl() || !D->isUsed())
      continue;

    Consumer.FinalizeExternalDeclaration(D);
  }

  // If there were errors, disable 'unused' warnings since they will mostly be
  // noise.
  if (!Diags.hasErrorOccurred()) {
    // Output warning for unused file scoped decls.
    for (const auto *D : UnusedFileScopedDecls) {
      if (isNoLongerUnused(this, D))
        continue;

      if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
        const FunctionDecl *DiagD;
        if (!FD->hasBody(DiagD))
          DiagD = FD;
        SourceRange DiagRange = DiagD->getLocation();
        if (DiagD->isReferenced()) {
          if (FD->getStorageClass() == SC_Static && !FD->isInlineSpecified() &&
              !SourceMgr.isInMainFile(
                  SourceMgr.getExpansionLoc(FD->getLocation())))
            Diag(DiagD->getLocation(), diag::warn_unneeded_static_internal_decl)
                << DiagD << DiagRange;
          else
            Diag(DiagD->getLocation(), diag::warn_unneeded_internal_decl)
                << /*function=*/0 << DiagD << DiagRange;
        } else {
          Diag(DiagD->getLocation(), diag::warn_unused_function)
              << DiagD << DiagRange;
        }
      } else {
        const VarDecl *DiagD = cast<VarDecl>(D)->getDefinition();
        if (!DiagD)
          DiagD = cast<VarDecl>(D);
        SourceRange DiagRange = DiagD->getLocation();
        if (DiagD->isReferenced()) {
          Diag(DiagD->getLocation(), diag::warn_unneeded_internal_decl)
              << /*variable=*/1 << DiagD << DiagRange;
        } else if (DiagD->getType().isConstQualified()) {
          const SourceManager &SM = SourceMgr;
          if (SM.getMainFileID() != SM.getFileID(DiagD->getLocation()) ||
              !PP.getLangOpts().IsHeaderFile)
            Diag(DiagD->getLocation(), diag::warn_unused_const_variable)
                << DiagD << DiagRange;
        } else {
          Diag(DiagD->getLocation(), diag::warn_unused_variable)
              << DiagD << DiagRange;
        }
      }
    }

    FlushUnusedTypedefWarnings();
  }

  // Check we've noticed that we're no longer parsing the initializer for every
  // variable. If we miss cases, then at best we have a performance issue and
  // at worst a rejects-valid bug.
  assert(ParsingInitForAutoVars.empty() &&
         "Didn't unmark var as having its initializer parsed");

  TUScope = nullptr;
}

DeclContext *Sema::getFunctionLevelDeclContext() const {
  DeclContext *DC = CurContext;

  while (isa<EnumDecl>(DC))
    DC = DC->getParent();

  return DC;
}

FunctionDecl *Sema::getCurFunctionDecl() const {
  DeclContext *DC = getFunctionLevelDeclContext();
  return dyn_cast<FunctionDecl>(DC);
}

void Sema::GenCurrentDiagnostic() {
  Context.setPrintingPolicy(getPrintingPolicy());
  if (!Diags.GenCurrentDiagnostic())
    return;
}

Sema::SemaDiagnosticBuilder Sema::Diag(SourceLocation Loc,
                                       const PartialDiagnostic &PD) {
  return Diag(Loc, PD.getDiagID()) << PD;
}

bool Sema::hasUncompilableErrorOccurred() const {
  return getDiagnostics().hasUncompilableErrorOccurred();
}

Sema::SemaDiagnosticBuilder::SemaDiagnosticBuilder(SourceLocation Loc,
                                                   unsigned DiagID,
                                                   const FunctionDecl *Fn,
                                                   Sema &S)
    : S(S), Loc(Loc), DiagID(DiagID), Fn(Fn) {
  ImmediateDiag.emplace(ImmediateDiagBuilder(S.Diags.Report(Loc, DiagID), S));
}

Sema::SemaDiagnosticBuilder::SemaDiagnosticBuilder(SemaDiagnosticBuilder &&D)
    : S(D.S), Loc(D.Loc), DiagID(D.DiagID), Fn(D.Fn),
      ImmediateDiag(D.ImmediateDiag) {
  D.ImmediateDiag.reset();
}

Sema::SemaDiagnosticBuilder::~SemaDiagnosticBuilder() { ImmediateDiag.reset(); }

Sema::SemaDiagnosticBuilder
Sema::targetDiag(SourceLocation Loc, unsigned DiagID, const FunctionDecl *FD) {
  FD = FD ? FD : getCurFunctionDecl();
  return SemaDiagnosticBuilder(Loc, DiagID, FD, *this);
}

Sema::SemaDiagnosticBuilder Sema::Diag(SourceLocation Loc, unsigned DiagID) {
  if (Diags.getDiagnosticIDs()->isDefaultMappingAsError(DiagID))
    IsLastErrorImmediate = true;
  return SemaDiagnosticBuilder(Loc, DiagID, getCurFunctionDecl(), *this);
}

void Sema::checkTypeSupport(QualType Ty, SourceLocation Loc, ValueDecl *D) {
  if (isUnevaluatedContext() || Ty.isNull())
    return;

  Decl *C = cast<Decl>(getCurLexicalContext());

  // Memcpy operations for structs containing a member with unsupported type
  // are ok, though.

  // Try to associate errors with the lexical context, if that is a function, or
  // the value declaration otherwise.
  const FunctionDecl *FD = isa<FunctionDecl>(C)
                               ? cast<FunctionDecl>(C)
                               : dyn_cast_or_null<FunctionDecl>(D);

  auto CheckType = [&](QualType Ty, bool IsRetTy = false) {
    QualType UnqualTy = Ty.getCanonicalType().getUnqualifiedType();
    const TargetInfo &TI = Context.getTargetInfo();
    if (!TI.hasLongDoubleType() && UnqualTy == Context.LongDoubleTy) {
      PartialDiagnostic PD = PDiag(diag::err_target_unsupported_type);
      if (D)
        PD << D;
      else
        PD << "expression";

      if (Diag(Loc, PD) << false /*show bit size*/ << 0 << Ty
                        << false /*return*/
                        << TI.getTriple().str()) {
        if (D)
          D->setInvalidDecl();
      }
      if (D)
        targetDiag(D->getLocation(), diag::note_defined_here, FD) << D;
    }

    bool IsDouble = UnqualTy == Context.DoubleTy;
    bool IsFloat = UnqualTy == Context.FloatTy;
    if (IsRetTy && !TI.hasFPReturn() && (IsDouble || IsFloat)) {
      PartialDiagnostic PD = PDiag(diag::err_target_unsupported_type);
      if (D)
        PD << D;
      else
        PD << "expression";

      if (Diag(Loc, PD) << false /*show bit size*/ << 0 << Ty << true /*return*/
                        << TI.getTriple().str()) {
        if (D)
          D->setInvalidDecl();
      }
      if (D)
        targetDiag(D->getLocation(), diag::note_defined_here, FD) << D;
    }

    // Don't allow SVE types in functions without a SVE target.
    if (Ty->isSVESizelessBuiltinType() && FD && FD->hasBody()) {
      llvm::StringMap<bool> CallerFeatureMap;
      Context.getFunctionFeatureMap(CallerFeatureMap, FD);
      if (!Builtin::evaluateRequiredTargetFeatures("sve", CallerFeatureMap) &&
          !Builtin::evaluateRequiredTargetFeatures("sme", CallerFeatureMap))
        Diag(D->getLocation(), diag::err_sve_vector_in_non_sve_target) << Ty;
    }
  };

  CheckType(Ty);
  if (const auto *FPTy = dyn_cast<FunctionProtoType>(Ty)) {
    for (const auto &ParamTy : FPTy->param_types())
      CheckType(ParamTy);
    CheckType(FPTy->getReturnType(), /*IsRetTy=*/true);
  }
  if (const auto *FNPTy = dyn_cast<FunctionNoProtoType>(Ty))
    CheckType(FNPTy->getReturnType(), /*IsRetTy=*/true);
}

bool Sema::locateMacroSpelling(SourceLocation &locref, llvm::StringRef name) {
  SourceLocation loc = locref;
  if (!loc.isMacroID())
    return false;

  // There's no good way right now to look at the intermediate
  // expansions, so just jump to the expansion location.
  loc = getSourceManager().getExpansionLoc(loc);

  // If that's written with the name, stop here.
  llvm::SmallString<16> buffer;
  if (getPrepEngine().getSpelling(loc, buffer) == name) {
    locref = loc;
    return true;
  }
  return false;
}

Scope *Sema::getScopeForContext(DeclContext *Ctx) {

  if (!Ctx)
    return nullptr;

  Ctx = Ctx->getPrimaryContext();
  for (Scope *S = getCurScope(); S; S = S->getParent()) {
    // Ignore scopes that cannot have declarations.
    if (S->getFlags() & Scope::DeclScope)
      if (DeclContext *Entity = S->getEntity())
        if (Ctx == Entity->getPrimaryContext())
          return S;
  }

  return nullptr;
}

void Sema::PushFunctionScope() {
  if (FunctionScopes.empty() && CachedFunctionScope) {
    // Use CachedFunctionScope to avoid allocating memory when possible.
    CachedFunctionScope->Clear();
    FunctionScopes.push_back(CachedFunctionScope.release());
  } else {
    FunctionScopes.push_back(new FunctionScopeInfo(getDiagnostics()));
  }
}

Sema::PoppedFunctionScopePtr Sema::PopFunctionScopeInfo() {
  assert(!FunctionScopes.empty() && "mismatched push/pop!");

  PoppedFunctionScopePtr Scope(FunctionScopes.pop_back_val(),
                               PoppedFunctionScopeDeleter(this));

  for (const auto &PUD : Scope->PossiblyUnreachableDiags)
    Diag(PUD.Loc, PUD.PD);

  return Scope;
}

void Sema::PoppedFunctionScopeDeleter::operator()(
    sema::FunctionScopeInfo *Scope) const {
  if (!Self->CachedFunctionScope)
    Self->CachedFunctionScope.reset(Scope);
  else
    delete Scope;
}

void Sema::PushCompoundScope(bool IsStmtExpr) {
  getCurFunction()->CompoundScopes.push_back(
      CompoundScopeInfo(IsStmtExpr, getCurFPFeatures()));
}

void Sema::PopCompoundScope() {
  FunctionScopeInfo *CurFunction = getCurFunction();
  assert(!CurFunction->CompoundScopes.empty() && "mismatched push/pop");

  CurFunction->CompoundScopes.pop_back();
}

bool Sema::hasAnyUnrecoverableErrorsInThisFunction() const {
  return getCurFunction()->hasUnrecoverableErrorOccurred();
}

void Sema::setFunctionHasBranchIntoScope() {
  if (!FunctionScopes.empty())
    FunctionScopes.back()->setHasBranchIntoScope();
}

void Sema::setFunctionHasBranchProtectedScope() {
  if (!FunctionScopes.empty())
    FunctionScopes.back()->setHasBranchProtectedScope();
}

void Sema::setFunctionHasIndirectGoto() {
  if (!FunctionScopes.empty())
    FunctionScopes.back()->setHasIndirectGoto();
}

void Sema::setFunctionHasMustTail() {
  if (!FunctionScopes.empty())
    FunctionScopes.back()->setHasMustTail();
}

bool Sema::tryExprAsCall(Expr &E, QualType &ZeroArgCallReturnTy) {
  ZeroArgCallReturnTy = QualType();

  if (const auto *DeclRef = dyn_cast<DeclRefExpr>(E.IgnoreParens())) {
    if (const auto *Fun = dyn_cast<FunctionDecl>(DeclRef->getDecl())) {
      if (Fun->getMinRequiredArguments() == 0)
        ZeroArgCallReturnTy = Fun->getReturnType();
      return true;
    }
  }

  // We don't have an expression that's convenient to get a FunctionDecl from,
  // but we can at least check if the type is "function of 0 arguments".
  QualType ExprTy = E.getType();
  const FunctionType *FunTy = nullptr;
  QualType PointeeTy = ExprTy->getPointeeType();
  if (!PointeeTy.isNull())
    FunTy = PointeeTy->getAs<FunctionType>();
  if (!FunTy)
    FunTy = ExprTy->getAs<FunctionType>();

  if (const auto *FPT = dyn_cast_if_present<FunctionProtoType>(FunTy)) {
    if (FPT->getNumParams() == 0)
      ZeroArgCallReturnTy = FunTy->getReturnType();
    return true;
  }
  return false;
}

bool Sema::tryToRecoverWithCall(ExprResult &E, const PartialDiagnostic &PD,
                                bool ForceComplain,
                                bool (*IsPlausibleResult)(QualType)) {
  SourceLocation Loc = E.get()->getExprLoc();
  SourceRange Range = E.get()->getSourceRange();

  QualType ZeroArgCallTy;
  if (tryExprAsCall(*E.get(), ZeroArgCallTy) && !ZeroArgCallTy.isNull() &&
      (!IsPlausibleResult || IsPlausibleResult(ZeroArgCallTy))) {
    SourceLocation ParenInsertionLoc = getLocForEndOfToken(Range.getEnd());
    const Expr *Callee = E.get()->IgnoreImplicit();
    const bool CanAppend = !isa<CStyleCastExpr>(Callee) &&
                           !isa<UnaryOperator>(Callee) &&
                           !isa<BinaryOperator>(Callee);
    Diag(Loc, PD) << /*zero-arg*/ 1 << Range
                  << (CanAppend
                          ? FixItHint::CreateInsertion(ParenInsertionLoc, "()")
                          : FixItHint());

    E = FormCallExpr(nullptr, E.get(), Range.getEnd(), std::nullopt,
                     Range.getEnd().getLocWithOffset(1));
    return true;
  }
  if (!ForceComplain)
    return false;

  Diag(Loc, PD) << /*not zero-arg*/ 0 << Range;
  E = ExprError();
  return true;
}

Sema::FPFeaturesStateRAII::FPFeaturesStateRAII(Sema &S)
    : S(S), OldFPFeaturesState(S.CurFPFeatures),
      OldOverrides(S.FpPragmaStack.CurrentValue),
      OldEvalMethod(S.PP.getCurrentFPEvalMethod()),
      OldFPPragmaLocation(S.PP.getLastFPEvalPragmaLocation()) {}

Sema::FPFeaturesStateRAII::~FPFeaturesStateRAII() {
  S.CurFPFeatures = OldFPFeaturesState;
  S.FpPragmaStack.CurrentValue = OldOverrides;
  S.PP.setCurrentFPEvalMethod(OldFPPragmaLocation, OldEvalMethod);
}

void SemaConsumer::anchor() {}
