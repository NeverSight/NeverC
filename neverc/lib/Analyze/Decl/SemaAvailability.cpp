#include "neverc/Analyze/DelayedDiagnostic.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/RecursiveTreeVisitor.h"
#include <optional>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Availability checking
// ===----------------------------------------------------------------------===

namespace {
const AvailabilityAttr *getAttrForPlatform(TreeContext &Context,
                                           const Decl *D) {
  // Check each AvailabilityAttr to find the one for this platform.
  for (const auto *A : D->attrs()) {
    if (const auto *Avail = dyn_cast<AvailabilityAttr>(A)) {
      llvm::StringRef TargetPlatform =
          Context.getTargetInfo().getPlatformName();

      if (Avail->getPlatform()->getName() == TargetPlatform)
        return Avail;
    }
  }
  return nullptr;
}

std::pair<AvailabilityResult, const NamedDecl *>
ShouldDiagnoseAvailabilityOfDecl(const NamedDecl *D, std::string *Message) {
  AvailabilityResult Result = D->getAvailability(Message);

  // For typedefs, if the typedef declaration appears available look
  // to the underlying type to see if it is more restrictive.
  while (const auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    if (Result == AR_Available) {
      QualType UT = TD->getUnderlyingType();
      if (!UT.isNull()) {
        const Type *UTP = UT.getTypePtrOrNull();
        if (UTP && (UTP->getTypeClass() == Type::Record ||
                    UTP->getTypeClass() == Type::Enum)) {
          if (const auto *TT = dyn_cast<TagType>(UTP)) {
            D = TT->getDecl();
            Result = D->getAvailability(Message);
            continue;
          }
        }
      }
    }
    break;
  }

  if (const auto *ECD = dyn_cast<EnumConstantDecl>(D))
    if (Result == AR_Available) {
      const DeclContext *DC = ECD->getDeclContext();
      if (const auto *TheEnumDecl = dyn_cast<EnumDecl>(DC)) {
        Result = TheEnumDecl->getAvailability(Message);
        D = TheEnumDecl;
      }
    }

  return {Result, D};
}

bool shouldDiagnoseAvailabilityInContext(Sema &S, AvailabilityResult K,
                                         llvm::VersionTuple DeclVersion,
                                         Decl *Ctx,
                                         const NamedDecl *OffendingDecl) {
  assert(K != AR_Available && "Expected an unavailable declaration here!");

  // If this was defined using CF_OPTIONS, etc. then ignore the diagnostic.

  // Checks if we should emit the availability diagnostic in the context of C.
  auto CheckContext = [&](const Decl *C) {
    if (K == AR_NotYetIntroduced) {
      if (const AvailabilityAttr *AA = getAttrForPlatform(S.Context, C))
        if (AA->getIntroduced() >= DeclVersion)
          return true;
    } else if (K == AR_Deprecated) {
      if (C->isDeprecated())
        return true;
    }

    if (K == AR_Unavailable && C->isUnavailable())
      return true;
    return false;
  };

  do {
    if (CheckContext(Ctx))
      return false;

  } while ((Ctx = cast_or_null<Decl>(Ctx->getDeclContext())));

  return true;
}

bool shouldDiagnoseAvailabilityByDefault(
    const TreeContext &Context, const llvm::VersionTuple &DeploymentVersion,
    const llvm::VersionTuple &DeclVersion) {
  const auto &Triple = Context.getTargetInfo().getTriple();
  llvm::VersionTuple ForceAvailabilityFromVersion;
  switch (Triple.getOS()) {
  case llvm::Triple::IOS:
    ForceAvailabilityFromVersion = llvm::VersionTuple(/*Major=*/11);
    break;
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
    ForceAvailabilityFromVersion =
        llvm::VersionTuple(/*Major=*/10, /*Minor=*/13);
    break;
  default:
    // New targets should always warn about availability.
    return Triple.getVendor() == llvm::Triple::Apple;
  }
  return DeploymentVersion >= ForceAvailabilityFromVersion ||
         DeclVersion >= ForceAvailabilityFromVersion;
}

NamedDecl *findEnclosingDeclToAnnotate(Decl *OrigCtx) {
  for (Decl *Ctx = OrigCtx; Ctx;
       Ctx = cast_or_null<Decl>(Ctx->getDeclContext())) {
    if (isa<TagDecl>(Ctx) || isa<FunctionDecl>(Ctx))
      return cast<NamedDecl>(Ctx);
  }

  return dyn_cast<NamedDecl>(OrigCtx);
}

struct AttributeInsertion {
  llvm::StringRef Prefix;
  SourceLocation Loc;
  llvm::StringRef Suffix;

  static AttributeInsertion createInsertionAfter(const NamedDecl *D) {
    return {" ", D->getEndLoc(), ""};
  }
  static AttributeInsertion createInsertionAfter(SourceLocation Loc) {
    return {" ", Loc, ""};
  }
  static AttributeInsertion createInsertionBefore(const NamedDecl *D) {
    return {"", D->getBeginLoc(), "\n"};
  }
};

std::optional<AttributeInsertion>
createAttributeInsertion(const NamedDecl *D, const SourceManager &SM,
                         const LangOptions &LangOpts) {
  if (const auto *TD = dyn_cast<TagDecl>(D)) {
    SourceLocation Loc = SourceScanner::getLocForEndOfToken(
        TD->getInnerLocStart(), 0, SM, LangOpts);
    if (Loc.isInvalid())
      return std::nullopt;
    // Insert after the 'struct'/whatever keyword.
    return AttributeInsertion::createInsertionAfter(Loc);
  }
  return AttributeInsertion::createInsertionBefore(D);
}

void doEmitAvailabilityWarning(Sema &S, AvailabilityResult K, Decl *Ctx,
                               const NamedDecl *ReferringDecl,
                               const NamedDecl *OffendingDecl,
                               llvm::StringRef Message,
                               llvm::ArrayRef<SourceLocation> Locs) {
  // Diagnostics for deprecated or unavailable.
  unsigned diag, diag_message;
  unsigned diag_available_here = diag::note_availability_specified_here;
  SourceLocation NoteLocation = OffendingDecl->getLocation();

  // Matches diag::note_availability_specified_here.
  unsigned available_here_select_kind;

  llvm::VersionTuple DeclVersion;
  if (const AvailabilityAttr *AA = getAttrForPlatform(S.Context, OffendingDecl))
    DeclVersion = AA->getIntroduced();

  if (!shouldDiagnoseAvailabilityInContext(S, K, DeclVersion, Ctx,
                                           OffendingDecl))
    return;

  SourceLocation Loc = Locs.front();

  // The declaration can have multiple availability attributes, we are looking
  // at one of them.
  const AvailabilityAttr *A = getAttrForPlatform(S.Context, OffendingDecl);
  if (A && A->isInherited()) {
    for (const Decl *Redecl = OffendingDecl->getMostRecentDecl(); Redecl;
         Redecl = Redecl->getPreviousDecl()) {
      const AvailabilityAttr *AForRedecl =
          getAttrForPlatform(S.Context, Redecl);
      if (AForRedecl && !AForRedecl->isInherited()) {
        // If D is a declaration with inherited attributes, the note should
        // point to the declaration with actual attributes.
        NoteLocation = Redecl->getLocation();
        break;
      }
    }
  }

  switch (K) {
  case AR_NotYetIntroduced: {
    // We would like to emit the diagnostic even if -Wunguarded-availability is
    // not specified for deployment targets >= to iOS 11 or equivalent or
    // for declarations that were introduced in iOS 11 (macOS 10.13, ...) or
    // later.
    const AvailabilityAttr *AA =
        getAttrForPlatform(S.getTreeContext(), OffendingDecl);
    llvm::VersionTuple Introduced = AA->getIntroduced();

    bool UseNewWarning = shouldDiagnoseAvailabilityByDefault(
        S.Context, S.Context.getTargetInfo().getPlatformMinVersion(),
        Introduced);
    unsigned Warning = UseNewWarning ? diag::warn_unguarded_availability_new
                                     : diag::warn_unguarded_availability;

    std::string PlatformName(AvailabilityAttr::getPrettyPlatformName(
        S.getTreeContext().getTargetInfo().getPlatformName()));

    S.Diag(Loc, Warning) << OffendingDecl << PlatformName
                         << Introduced.getAsString();

    S.Diag(OffendingDecl->getLocation(),
           diag::note_partial_availability_specified_here)
        << OffendingDecl << PlatformName << Introduced.getAsString()
        << S.Context.getTargetInfo().getPlatformMinVersion().getAsString();

    if (const auto *Enclosing = findEnclosingDeclToAnnotate(Ctx)) {
      if (const auto *TD = dyn_cast<TagDecl>(Enclosing))
        if (TD->getDeclName().isEmpty()) {
          S.Diag(TD->getLocation(),
                 diag::note_decl_unguarded_availability_silence)
              << /*Anonymous*/ 1 << TD->getKindName();
          return;
        }
      auto FixitNoteDiag =
          S.Diag(Enclosing->getLocation(),
                 diag::note_decl_unguarded_availability_silence)
          << /*Named*/ 0 << Enclosing;
      // Don't offer a fixit for declarations with availability attributes.
      if (Enclosing->hasAttr<AvailabilityAttr>())
        return;
      if (!S.getPrepEngine().isMacroDefined("API_AVAILABLE"))
        return;
      std::optional<AttributeInsertion> Insertion = createAttributeInsertion(
          Enclosing, S.getSourceManager(), S.getLangOpts());
      if (!Insertion)
        return;
      auto PlatformName =
          AvailabilityAttr::getPlatformNameSourceSpelling(
              S.getTreeContext().getTargetInfo().getPlatformName())
              .lower();
      auto Introduced = OffendingDecl->getVersionIntroduced().getAsString();
      FixitNoteDiag << FixItHint::CreateInsertion(
          Insertion->Loc,
          (llvm::Twine(Insertion->Prefix) + "API_AVAILABLE(" + PlatformName +
           "(" + Introduced + "))" + Insertion->Suffix)
              .str());
    }
    return;
  }
  case AR_Deprecated:
    diag = diag::warn_deprecated;
    diag_message = diag::warn_deprecated_message;
    available_here_select_kind = /* deprecated */ 2;
    if (const auto *AL = OffendingDecl->getAttr<DeprecatedAttr>())
      NoteLocation = AL->getLocation();
    break;

  case AR_Unavailable:
    diag = diag::err_unavailable;
    diag_message = diag::err_unavailable_message;
    available_here_select_kind = /* unavailable */ 0;
    break;

  case AR_Available:
    return;
  }

  llvm::SmallVector<FixItHint, 12> FixIts;
  if (K == AR_Deprecated) {
    llvm::StringRef Replacement;
    if (auto AL = OffendingDecl->getAttr<DeprecatedAttr>())
      Replacement = AL->getReplacement();
    if (auto AL = getAttrForPlatform(S.Context, OffendingDecl))
      Replacement = AL->getReplacement();

    CharSourceRange UseRange;
    if (!Replacement.empty())
      UseRange = CharSourceRange::getCharRange(Loc, S.getLocForEndOfToken(Loc));
    if (UseRange.isValid()) {
      FixIts.push_back(FixItHint::CreateReplacement(UseRange, Replacement));
    }
  }

  // Deprecation notes: compare use site vs declaration location (availability
  // attributes on the decl may still warrant a note when the use is elsewhere).
  SourceLocation InstantiationLoc =
      ReferringDecl ? ReferringDecl->getLocation() : SourceLocation();
  bool ShouldAllowWarningInSystemHeader =
      InstantiationLoc != Loc &&
      !S.getSourceManager().isInSystemHeader(InstantiationLoc);
  struct AllowWarningInSystemHeaders {
    AllowWarningInSystemHeaders(DiagnosticsEngine &E,
                                bool AllowWarningInSystemHeaders)
        : Engine(E), Prev(E.getSuppressSystemWarnings()) {
      E.setSuppressSystemWarnings(!AllowWarningInSystemHeaders);
    }
    ~AllowWarningInSystemHeaders() { Engine.setSuppressSystemWarnings(Prev); }

  private:
    DiagnosticsEngine &Engine;
    bool Prev;
  } SystemWarningOverrideRAII(S.getDiagnostics(),
                              ShouldAllowWarningInSystemHeader);

  if (!Message.empty()) {
    S.Diag(Loc, diag_message) << ReferringDecl << Message << FixIts;
  } else {
    S.Diag(Loc, diag) << ReferringDecl << FixIts;
  }

  S.Diag(NoteLocation, diag_available_here)
      << OffendingDecl << available_here_select_kind;
}
} // namespace

void Sema::handleDelayedAvailabilityCheck(DelayedDiagnostic &DD, Decl *Ctx) {
  DD.Triggered = true;
  doEmitAvailabilityWarning(
      *this, DD.getAvailabilityResult(), Ctx, DD.getAvailabilityReferringDecl(),
      DD.getAvailabilityOffendingDecl(), DD.getAvailabilityMessage(),
      DD.getAvailabilitySelectorLocs());
}

namespace {
void genAvailabilityWarning(Sema &S, AvailabilityResult AR,
                            const NamedDecl *ReferringDecl,
                            const NamedDecl *OffendingDecl,
                            llvm::StringRef Message,
                            llvm::ArrayRef<SourceLocation> Locs) {
  // Delay if we're currently parsing a declaration.
  if (S.DelayedDiagnostics.shouldDelayDiagnostics()) {
    S.DelayedDiagnostics.add(DelayedDiagnostic::makeAvailability(
        AR, Locs, ReferringDecl, OffendingDecl, Message));
    return;
  }

  Decl *Ctx = cast<Decl>(S.getCurLexicalContext());
  doEmitAvailabilityWarning(S, AR, Ctx, ReferringDecl, OffendingDecl, Message,
                            Locs);
}

bool isBodyLikeChildStmt(const Stmt *S, const Stmt *Parent) {
  switch (Parent->getStmtClass()) {
  case Stmt::IfStmtClass:
    return cast<IfStmt>(Parent)->getThen() == S ||
           cast<IfStmt>(Parent)->getElse() == S;
  case Stmt::WhileStmtClass:
    return cast<WhileStmt>(Parent)->getBody() == S;
  case Stmt::DoStmtClass:
    return cast<DoStmt>(Parent)->getBody() == S;
  case Stmt::ForStmtClass:
    return cast<ForStmt>(Parent)->getBody() == S;
  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
    return cast<SwitchCase>(Parent)->getSubStmt() == S;
  default:
    return false;
  }
}

class StmtUSEFinder : public RecursiveTreeVisitor<StmtUSEFinder> {
  const Stmt *Target;

public:
  bool VisitStmt(Stmt *S) { return S != Target; }

  static bool isContained(const Stmt *Target, const Decl *D) {
    StmtUSEFinder Visitor;
    Visitor.Target = Target;
    return !Visitor.TraverseDecl(const_cast<Decl *>(D));
  }
};

class LastDeclUSEFinder : public RecursiveTreeVisitor<LastDeclUSEFinder> {
  const Decl *D;

public:
  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    if (DRE->getDecl() == D)
      return false;
    return true;
  }

  static const Stmt *findLastStmtThatUsesDecl(const Decl *D,
                                              const CompoundStmt *Scope) {
    LastDeclUSEFinder Visitor;
    Visitor.D = D;
    for (const Stmt *S : llvm::reverse(Scope->body())) {
      if (!Visitor.TraverseStmt(const_cast<Stmt *>(S)))
        return S;
    }
    return nullptr;
  }
};

class DiagnoseUnguardedAvailability
    : public RecursiveTreeVisitor<DiagnoseUnguardedAvailability> {
  typedef RecursiveTreeVisitor<DiagnoseUnguardedAvailability> Base;

  Sema &SemaRef;
  Decl *Ctx;

  llvm::SmallVector<llvm::VersionTuple, 8> AvailabilityStack;
  llvm::SmallVector<const Stmt *, 16> StmtStack;

  void DiagnoseDeclAvailability(NamedDecl *D, SourceRange Range);

public:
  DiagnoseUnguardedAvailability(Sema &SemaRef, Decl *Ctx)
      : SemaRef(SemaRef), Ctx(Ctx) {
    AvailabilityStack.push_back(
        SemaRef.Context.getTargetInfo().getPlatformMinVersion());
  }

  bool TraverseStmt(Stmt *S) {
    if (!S)
      return true;
    StmtStack.push_back(S);
    bool Result = Base::TraverseStmt(S);
    StmtStack.pop_back();
    return Result;
  }

  void IssueDiagnostics(Stmt *S) { TraverseStmt(S); }

  bool TraverseIfStmt(IfStmt *If);

  // for 'case X:' statements, don't bother looking at the 'X'; it can't lead
  // to any useful diagnostics.
  bool TraverseCaseStmt(CaseStmt *CS) { return TraverseStmt(CS->getSubStmt()); }

  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    DiagnoseDeclAvailability(DRE->getDecl(),
                             SourceRange(DRE->getBeginLoc(), DRE->getEndLoc()));
    return true;
  }

  bool VisitMemberExpr(MemberExpr *ME) {
    DiagnoseDeclAvailability(ME->getMemberDecl(),
                             SourceRange(ME->getBeginLoc(), ME->getEndLoc()));
    return true;
  }

  bool VisitTypeLoc(TypeLoc Ty);
};

void DiagnoseUnguardedAvailability::DiagnoseDeclAvailability(
    NamedDecl *D, SourceRange Range) {
  AvailabilityResult Result;
  const NamedDecl *OffendingDecl;
  std::tie(Result, OffendingDecl) =
      ShouldDiagnoseAvailabilityOfDecl(D, nullptr);
  if (Result != AR_Available) {
    // All other diagnostic kinds have already been handled in
    // DiagnoseAvailabilityOfDecl.
    if (Result != AR_NotYetIntroduced)
      return;

    const AvailabilityAttr *AA =
        getAttrForPlatform(SemaRef.getTreeContext(), OffendingDecl);
    llvm::VersionTuple Introduced = AA->getIntroduced();

    if (AvailabilityStack.back() >= Introduced)
      return;

    // If the context of this function is less available than D, we should not
    // emit a diagnostic.
    if (!shouldDiagnoseAvailabilityInContext(SemaRef, Result, Introduced, Ctx,
                                             OffendingDecl))
      return;

    // We would like to emit the diagnostic even if -Wunguarded-availability is
    // not specified for deployment targets >= to iOS 11 or equivalent or
    // for declarations that were introduced in iOS 11 (macOS 10.13, ...) or
    // later.
    unsigned DiagKind =
        shouldDiagnoseAvailabilityByDefault(
            SemaRef.Context,
            SemaRef.Context.getTargetInfo().getPlatformMinVersion(), Introduced)
            ? diag::warn_unguarded_availability_new
            : diag::warn_unguarded_availability;

    std::string PlatformName(AvailabilityAttr::getPrettyPlatformName(
        SemaRef.getTreeContext().getTargetInfo().getPlatformName()));

    SemaRef.Diag(Range.getBegin(), DiagKind)
        << Range << D << PlatformName << Introduced.getAsString();

    SemaRef.Diag(OffendingDecl->getLocation(),
                 diag::note_partial_availability_specified_here)
        << OffendingDecl << PlatformName << Introduced.getAsString()
        << SemaRef.Context.getTargetInfo()
               .getPlatformMinVersion()
               .getAsString();

    auto FixitDiag =
        SemaRef.Diag(Range.getBegin(), diag::note_unguarded_available_silence)
        << Range;

    // Find the statement which should be enclosed in the if __builtin_available
    // check.
    if (StmtStack.empty())
      return;
    const Stmt *StmtOfUse = StmtStack.back();
    const CompoundStmt *Scope = nullptr;
    for (const Stmt *S : llvm::reverse(StmtStack)) {
      if (const auto *CS = dyn_cast<CompoundStmt>(S)) {
        Scope = CS;
        break;
      }
      if (isBodyLikeChildStmt(StmtOfUse, S)) {
        // The declaration won't be seen outside of the statement, so we don't
        // have to wrap the uses of any declared variables in if
        // (__builtin_available). Therefore we can avoid setting Scope here.
        break;
      }
      StmtOfUse = S;
    }
    const Stmt *LastStmtOfUse = nullptr;
    if (isa<DeclStmt>(StmtOfUse) && Scope) {
      for (const Decl *D : cast<DeclStmt>(StmtOfUse)->decls()) {
        if (StmtUSEFinder::isContained(StmtStack.back(), D)) {
          LastStmtOfUse = LastDeclUSEFinder::findLastStmtThatUsesDecl(D, Scope);
          break;
        }
      }
    }

    const SourceManager &SM = SemaRef.getSourceManager();
    SourceLocation IfInsertionLoc =
        SM.getExpansionLoc(StmtOfUse->getBeginLoc());
    SourceLocation StmtEndLoc =
        SM.getExpansionRange(
              (LastStmtOfUse ? LastStmtOfUse : StmtOfUse)->getEndLoc())
            .getEnd();
    if (SM.getFileID(IfInsertionLoc) != SM.getFileID(StmtEndLoc))
      return;

    llvm::StringRef Indentation =
        SourceScanner::getIndentationForLine(IfInsertionLoc, SM);
    const char *ExtraIndentation = "    ";
    std::string FixItString;
    llvm::raw_string_ostream FixItOS(FixItString);
    FixItOS << "if (__builtin_available("
            << AvailabilityAttr::getPlatformNameSourceSpelling(
                   SemaRef.getTreeContext().getTargetInfo().getPlatformName())
            << " " << Introduced.getAsString() << ", *)) {\n"
            << Indentation << ExtraIndentation;
    FixitDiag << FixItHint::CreateInsertion(IfInsertionLoc, FixItOS.str());
    SourceLocation ElseInsertionLoc = SourceScanner::locateAfterToken(
        StmtEndLoc, tok::semi, SM, SemaRef.getLangOpts(),
        /*SkipTrailingWhitespaceAndNewLine=*/false);
    if (ElseInsertionLoc.isInvalid())
      ElseInsertionLoc = SourceScanner::getLocForEndOfToken(
          StmtEndLoc, 0, SM, SemaRef.getLangOpts());
    FixItOS.str().clear();
    FixItOS << "\n"
            << Indentation << "} else {\n"
            << Indentation << ExtraIndentation
            << "// Fallback on earlier versions\n"
            << Indentation << "}";
    FixitDiag << FixItHint::CreateInsertion(ElseInsertionLoc, FixItOS.str());
  }
}

bool DiagnoseUnguardedAvailability::VisitTypeLoc(TypeLoc Ty) {
  const Type *TyPtr = Ty.getTypePtr();
  SourceRange Range{Ty.getBeginLoc(), Ty.getEndLoc()};

  if (Range.isInvalid())
    return true;

  if (const auto *TT = dyn_cast<TagType>(TyPtr)) {
    TagDecl *TD = TT->getDecl();
    DiagnoseDeclAvailability(TD, Range);

  } else if (const auto *TD = dyn_cast<TypedefType>(TyPtr)) {
    TypedefNameDecl *D = TD->getDecl();
    DiagnoseDeclAvailability(D, Range);
  }

  return true;
}

bool DiagnoseUnguardedAvailability::TraverseIfStmt(IfStmt *If) {
  return Base::TraverseIfStmt(If);
}

} // end anonymous namespace

void Sema::DiagnoseUnguardedAvailabilityViolations(Decl *D) {
  Stmt *Body = nullptr;

  if (auto *FD = D->getAsFunction()) {
    Body = FD->getBody();
  }
  assert(Body && "Need a body here!");

  DiagnoseUnguardedAvailability(*this, D).IssueDiagnostics(Body);
}

FunctionScopeInfo *Sema::getCurFunctionAvailabilityContext() {
  if (FunctionScopes.empty())
    return nullptr;

  // Conservatively search the entire current function scope context for
  // availability violations. This ensures we always correctly analyze nested
  // scopes that may or may not be inside if(__builtin_available) checks
  // themselves.
  return FunctionScopes.front();
}

void Sema::DiagnoseAvailabilityOfDecl(NamedDecl *D,
                                      llvm::ArrayRef<SourceLocation> Locs) {
  std::string Message;
  AvailabilityResult Result;
  const NamedDecl *OffendingDecl;
  // See if this declaration is unavailable, deprecated, or partial.
  std::tie(Result, OffendingDecl) =
      ShouldDiagnoseAvailabilityOfDecl(D, &Message);
  if (Result == AR_Available)
    return;

  if (Result == AR_NotYetIntroduced) {
    // We need to know the __builtin_available context in the current function
    // to diagnose this use, let DiagnoseUnguardedAvailabilityViolations do that
    // when we're done parsing the current function.
    if (FunctionScopeInfo *Context = getCurFunctionAvailabilityContext()) {
      Context->HasPotentialAvailabilityViolations = true;
      return;
    }
  }

  genAvailabilityWarning(*this, Result, D, OffendingDecl, Message, Locs);
}
