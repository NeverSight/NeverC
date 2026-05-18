#include "neverc/Tree/Decl/DeclBase.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/AttrIterator.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "neverc/Tree/Decl/DeclContextInternals.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>
#include <utility>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Declaration base implementation
// ===----------------------------------------------------------------------===

#define DECL(DERIVED, BASE) static int n##DERIVED##s = 0;
#define ABSTRACT_DECL(DECL)
#include "neverc/Tree/DeclNodes.td.h"

#define DECL(DERIVED, BASE)                                                    \
  static_assert(alignof(Decl) >= alignof(DERIVED##Decl),                       \
                "Alignment sufficient after objects prepended to " #DERIVED);
#define ABSTRACT_DECL(DECL)
#include "neverc/Tree/DeclNodes.td.h"

void *Decl::operator new(std::size_t Size, const TreeContext &Ctx,
                         DeclContext *Parent, std::size_t Extra) {
  assert(!Parent || &Parent->getParentTreeContext() == &Ctx);
  return ::operator new(Size + Extra, Ctx);
}

const char *Decl::getDeclKindName() const {
  switch (DeclKind) {
  default:
    llvm_unreachable("Declaration not in DeclNodes.td.h!");
#define DECL(DERIVED, BASE)                                                    \
  case DERIVED:                                                                \
    return #DERIVED;
#define ABSTRACT_DECL(DECL)
#include "neverc/Tree/DeclNodes.td.h"
  }
}

void Decl::setInvalidDecl(bool Invalid) {
  InvalidDecl = Invalid;
  assert(!isa<TagDecl>(this) || !cast<TagDecl>(this)->isCompleteDefinition());
  if (!Invalid) {
    return;
  }

  if (!isa<ParmVarDecl>(this)) {
    // Defensive maneuver for ill-formed code: we're likely not to make it to
    // a point where we set the access specifier, so default it to "public"
    // to avoid triggering asserts elsewhere in the front end.
    setAccess(AS_public);
  }
}

bool DeclContext::hasValidDeclKind() const {
  switch (getDeclKind()) {
#define DECL(DERIVED, BASE)                                                    \
  case Decl::DERIVED:                                                          \
    return true;
#define ABSTRACT_DECL(DECL)
#include "neverc/Tree/DeclNodes.td.h"
  }
  return false;
}

const char *DeclContext::getDeclKindName() const {
  switch (getDeclKind()) {
#define DECL(DERIVED, BASE)                                                    \
  case Decl::DERIVED:                                                          \
    return #DERIVED;
#define ABSTRACT_DECL(DECL)
#include "neverc/Tree/DeclNodes.td.h"
  }
  llvm_unreachable("Declaration context not in DeclNodes.td.h!");
}

bool Decl::StatisticsEnabled = false;
void Decl::EnableStatistics() { StatisticsEnabled = true; }

void Decl::PrintStats() {
  llvm::errs() << "\n*** Decl Stats:\n";

  int totalDecls = 0;
#define DECL(DERIVED, BASE) totalDecls += n##DERIVED##s;
#define ABSTRACT_DECL(DECL)
#include "neverc/Tree/DeclNodes.td.h"
  llvm::errs() << "  " << totalDecls << " decls total.\n";

  int totalBytes = 0;
#define DECL(DERIVED, BASE)                                                    \
  if (n##DERIVED##s > 0) {                                                     \
    totalBytes += (int)(n##DERIVED##s * sizeof(DERIVED##Decl));                \
    llvm::errs() << "    " << n##DERIVED##s << " " #DERIVED " decls, "         \
                 << sizeof(DERIVED##Decl) << " each ("                         \
                 << n##DERIVED##s * sizeof(DERIVED##Decl) << " bytes)\n";      \
  }
#define ABSTRACT_DECL(DECL)
#include "neverc/Tree/DeclNodes.td.h"

  llvm::errs() << "Total bytes = " << totalBytes << "\n";
}

void Decl::add(Kind k) {
  switch (k) {
#define DECL(DERIVED, BASE)                                                    \
  case DERIVED:                                                                \
    ++n##DERIVED##s;                                                           \
    break;
#define ABSTRACT_DECL(DECL)
#include "neverc/Tree/DeclNodes.td.h"
  }
}

FunctionDecl *Decl::getAsFunction() { return dyn_cast<FunctionDecl>(this); }

const DeclContext *Decl::getParentFunctionOrMethod(bool LexicalParent) const {
  for (const DeclContext *DC = LexicalParent ? getLexicalDeclContext()
                                             : getDeclContext();
       DC && !DC->isFileContext(); DC = DC->getParent())
    if (DC->isFunctionOrMethod())
      return DC;

  return nullptr;
}

void PrettyStackTraceDecl::print(llvm::raw_ostream &OS) const {
  SourceLocation TheLoc = Loc;
  if (TheLoc.isInvalid() && TheDecl)
    TheLoc = TheDecl->getLocation();

  if (TheLoc.isValid()) {
    TheLoc.print(OS, SM);
    OS << ": ";
  }

  OS << Message;

  if (const auto *DN = dyn_cast_or_null<NamedDecl>(TheDecl)) {
    OS << " '";
    DN->printQualifiedName(OS);
    OS << '\'';
  }
  OS << '\n';
}

// Out-of-line virtual method providing a home for Decl.
Decl::~Decl() = default;

void Decl::setDeclContext(DeclContext *DC) { DeclCtx = DC; }

void Decl::setLexicalDeclContext(DeclContext *DC) {
  if (DC == getLexicalDeclContext())
    return;

  if (isInSemaDC()) {
    setDeclContextsImpl(getDeclContext(), DC, getTreeContext());
  } else {
    getMultipleDC()->LexicalDC = DC;
  }
}

void Decl::setDeclContextsImpl(DeclContext *SemaDC, DeclContext *LexicalDC,
                               TreeContext &Ctx) {
  if (SemaDC == LexicalDC) {
    DeclCtx = SemaDC;
  } else {
    auto *MDC = new (Ctx) Decl::MultipleDC();
    MDC->SemanticDC = SemaDC;
    MDC->LexicalDC = LexicalDC;
    DeclCtx = MDC;
  }
}

bool Decl::isFileContextDecl() const {
  const auto *DC = dyn_cast<DeclContext>(this);
  return DC && DC->isFileContext();
}

TranslationUnitDecl *Decl::getTranslationUnitDecl() {
  if (auto *TUD = dyn_cast<TranslationUnitDecl>(this))
    return TUD;

  DeclContext *DC = getDeclContext();
  assert(DC && "This decl is not contained in a translation unit!");

  while (!DC->isTranslationUnit()) {
    DC = DC->getParent();
    assert(DC && "This decl is not contained in a translation unit!");
  }

  return cast<TranslationUnitDecl>(DC);
}

TreeContext &Decl::getTreeContext() const {
  return getTranslationUnitDecl()->getTreeContext();
}

const LangOptions &Decl::getLangOpts() const {
  return getTreeContext().getLangOpts();
}

TreeMutationListener *Decl::getTreeMutationListener() const {
  return getTreeContext().getTreeMutationListener();
}

unsigned Decl::getMaxAlignment() const {
  if (!hasAttrs())
    return 0;

  unsigned Align = 0;
  const AttrVec &V = getAttrs();
  TreeContext &Ctx = getTreeContext();
  specific_attr_iterator<AlignedAttr> I(V.begin()), E(V.end());
  for (; I != E; ++I) {
    if (!I->isAlignmentErrorDependent())
      Align = std::max(Align, I->getAlignment(Ctx));
  }
  return Align;
}

__attribute__((hot)) bool Decl::isUsed(bool CheckUsedAttr) const {
  const Decl *CanonD = getCanonicalDecl();
  if (LLVM_LIKELY(CanonD->Used))
    return true;

  if (LLVM_UNLIKELY(CheckUsedAttr) && getMostRecentDecl()->hasAttr<UsedAttr>())
    return true;

  return getMostRecentDecl()->getCanonicalDecl()->Used;
}

void Decl::markUsed(TreeContext &C) {
  if (LLVM_LIKELY(isUsed(false)))
    return;

  if (LLVM_UNLIKELY(C.getTreeMutationListener() != nullptr))
    C.getTreeMutationListener()->DeclarationMarkedUsed(this);

  setIsUsed();
}

__attribute__((hot)) bool Decl::isReferenced() const {
  if (LLVM_LIKELY(Referenced))
    return true;

  for (const auto *I : redecls())
    if (I->Referenced)
      return true;

  return false;
}

bool Decl::hasDefiningAttr() const {
  return hasAttr<AliasAttr>() || hasAttr<IFuncAttr>() ||
         hasAttr<LoaderUninitializedAttr>();
}

const Attr *Decl::getDefiningAttr() const {
  if (auto *AA = getAttr<AliasAttr>())
    return AA;
  if (auto *IFA = getAttr<IFuncAttr>())
    return IFA;
  if (auto *NZA = getAttr<LoaderUninitializedAttr>())
    return NZA;
  return nullptr;
}

namespace {
llvm::StringRef getRealizedPlatform(const AvailabilityAttr *A,
                                    const TreeContext &Context) {
  return A->getPlatform()->getName();
}
} // namespace

namespace {
AvailabilityResult checkAvailability(TreeContext &Context,
                                     const AvailabilityAttr *A,
                                     std::string *Message,
                                     llvm::VersionTuple EnclosingVersion) {
  if (EnclosingVersion.empty())
    EnclosingVersion = Context.getTargetInfo().getPlatformMinVersion();

  if (EnclosingVersion.empty())
    return AR_Available;

  llvm::StringRef ActualPlatform = A->getPlatform()->getName();
  llvm::StringRef TargetPlatform = Context.getTargetInfo().getPlatformName();

  // Match the platform name.
  if (getRealizedPlatform(A, Context) != TargetPlatform)
    return AR_Available;

  llvm::StringRef PrettyPlatformName =
      AvailabilityAttr::getPrettyPlatformName(ActualPlatform);

  if (PrettyPlatformName.empty())
    PrettyPlatformName = ActualPlatform;

  std::string HintMessage;
  if (!A->getMessage().empty()) {
    HintMessage = " - ";
    HintMessage += A->getMessage();
  }

  // Make sure that this declaration has not been marked 'unavailable'.
  if (A->getUnavailable()) {
    if (Message) {
      Message->clear();
      llvm::raw_string_ostream Out(*Message);
      Out << "not available on " << PrettyPlatformName << HintMessage;
    }

    return AR_Unavailable;
  }

  // Make sure that this declaration has already been introduced.
  if (!A->getIntroduced().empty() && EnclosingVersion < A->getIntroduced()) {
    if (Message) {
      Message->clear();
      llvm::raw_string_ostream Out(*Message);
      llvm::VersionTuple VTI(A->getIntroduced());
      Out << "introduced in " << PrettyPlatformName << ' ' << VTI
          << HintMessage;
    }

    return A->getStrict() ? AR_Unavailable : AR_NotYetIntroduced;
  }

  // Make sure that this declaration hasn't been obsoleted.
  if (!A->getObsoleted().empty() && EnclosingVersion >= A->getObsoleted()) {
    if (Message) {
      Message->clear();
      llvm::raw_string_ostream Out(*Message);
      llvm::VersionTuple VTO(A->getObsoleted());
      Out << "obsoleted in " << PrettyPlatformName << ' ' << VTO << HintMessage;
    }

    return AR_Unavailable;
  }

  // Make sure that this declaration hasn't been deprecated.
  if (!A->getDeprecated().empty() && EnclosingVersion >= A->getDeprecated()) {
    if (Message) {
      Message->clear();
      llvm::raw_string_ostream Out(*Message);
      llvm::VersionTuple VTD(A->getDeprecated());
      Out << "first deprecated in " << PrettyPlatformName << ' ' << VTD
          << HintMessage;
    }

    return AR_Deprecated;
  }

  return AR_Available;
}
} // namespace

AvailabilityResult
Decl::getAvailability(std::string *Message, llvm::VersionTuple EnclosingVersion,
                      llvm::StringRef *RealizedPlatform) const {
  AvailabilityResult Result = AR_Available;
  std::string ResultMessage;

  for (const auto *A : attrs()) {
    if (const auto *Deprecated = dyn_cast<DeprecatedAttr>(A)) {
      if (Result >= AR_Deprecated)
        continue;

      if (Message)
        ResultMessage = std::string(Deprecated->getMessage());

      Result = AR_Deprecated;
      continue;
    }

    if (const auto *Unavailable = dyn_cast<UnavailableAttr>(A)) {
      if (Message)
        *Message = std::string(Unavailable->getMessage());
      return AR_Unavailable;
    }

    if (const auto *Availability = dyn_cast<AvailabilityAttr>(A)) {
      AvailabilityResult AR = checkAvailability(getTreeContext(), Availability,
                                                Message, EnclosingVersion);

      if (AR == AR_Unavailable) {
        if (RealizedPlatform)
          *RealizedPlatform = Availability->getPlatform()->getName();
        return AR_Unavailable;
      }

      if (AR > Result) {
        Result = AR;
        if (Message)
          ResultMessage.swap(*Message);
      }
      continue;
    }
  }

  if (Message)
    Message->swap(ResultMessage);
  return Result;
}

llvm::VersionTuple Decl::getVersionIntroduced() const {
  const TreeContext &Context = getTreeContext();
  llvm::StringRef TargetPlatform = Context.getTargetInfo().getPlatformName();
  for (const auto *A : attrs()) {
    if (const auto *Availability = dyn_cast<AvailabilityAttr>(A)) {
      if (getRealizedPlatform(Availability, Context) != TargetPlatform)
        continue;
      if (!Availability->getIntroduced().empty())
        return Availability->getIntroduced();
    }
  }
  return {};
}

bool Decl::canBeWeakImported(bool &IsDefinition) const {
  IsDefinition = false;

  // Variables, if they aren't definitions.
  if (const auto *Var = dyn_cast<VarDecl>(this)) {
    if (Var->isThisDeclarationADefinition()) {
      IsDefinition = true;
      return false;
    }
    return true;
  }
  // Functions, if they aren't definitions.
  if (const auto *FD = dyn_cast<FunctionDecl>(this)) {
    if (FD->hasBody()) {
      IsDefinition = true;
      return false;
    }
    return true;
  }
  return false;
}

bool Decl::isWeakImported() const {
  bool IsDefinition;
  if (!canBeWeakImported(IsDefinition))
    return false;

  for (const auto *A : getMostRecentDecl()->attrs()) {
    if (isa<WeakImportAttr>(A))
      return true;

    if (const auto *Availability = dyn_cast<AvailabilityAttr>(A)) {
      if (checkAvailability(getTreeContext(), Availability, nullptr,
                            llvm::VersionTuple()) == AR_NotYetIntroduced)
        return true;
    }
  }

  return false;
}

unsigned Decl::getIdentifierNamespaceForKind(Kind DeclKind) {
  switch (DeclKind) {
  case Function:
  case EnumConstant:
  case Var:
  case ImplicitParam:
  case ParmVar:
    return IDNS_Ordinary;
  case Label:
    return IDNS_Label;
  case IndirectField:
    return IDNS_Ordinary | IDNS_Member;

  case Typedef:
    return IDNS_Ordinary | IDNS_Type;

  case Field:
    return IDNS_Member;

  case Record:
  case Enum:
    return IDNS_Tag | IDNS_Type;

  case FileScopeAsm:
  case StaticAssert:
  case PragmaComment:
  case PragmaDetectMismatch:
  case TranslationUnit:
  case ExternCContext:
  case Empty:
    return 0;
  }

  llvm_unreachable("Invalid DeclKind!");
}

void Decl::setAttrsImpl(const AttrVec &attrs, TreeContext &Ctx) {
  assert(!HasAttrs && "Decl already contains attrs.");

  AttrVec &AttrBlank = Ctx.getDeclAttrs(this);
  assert(AttrBlank.empty() && "HasAttrs was wrong?");

  AttrBlank = attrs;
  HasAttrs = true;
}

void Decl::dropAttrs() {
  if (!HasAttrs)
    return;

  HasAttrs = false;
  getTreeContext().eraseDeclAttrs(this);
}

void Decl::addAttr(Attr *A) {
  if (!hasAttrs()) {
    setAttrs(AttrVec(1, A));
    return;
  }

  AttrVec &Attrs = getAttrs();
  if (!A->isInherited()) {
    Attrs.push_back(A);
    return;
  }

  // Attribute inheritance is processed after attribute parsing. To keep the
  // order as in the source code, add inherited attributes before non-inherited
  // ones.
  auto I = Attrs.begin(), E = Attrs.end();
  for (; I != E; ++I) {
    if (!(*I)->isInherited())
      break;
  }
  Attrs.insert(I, A);
}

const AttrVec &Decl::getAttrs() const {
  assert(HasAttrs && "No attrs to get!");
  return getTreeContext().getDeclAttrs(this);
}

Decl *Decl::castFromDeclContext(const DeclContext *D) {
  Decl::Kind DK = D->getDeclKind();
  switch (DK) {
  case Decl::ExternCContext:
    return static_cast<ExternCContextDecl *>(const_cast<DeclContext *>(D));
  case Decl::TranslationUnit:
    return static_cast<TranslationUnitDecl *>(const_cast<DeclContext *>(D));
  default:
    if (DK == Function)
      return static_cast<FunctionDecl *>(const_cast<DeclContext *>(D));
    if (DK >= firstTag && DK <= lastTag)
      return static_cast<TagDecl *>(const_cast<DeclContext *>(D));
    llvm_unreachable("a decl that inherits DeclContext isn't handled");
  }
}

DeclContext *Decl::castToDeclContext(const Decl *D) {
  Decl::Kind DK = D->getKind();
  switch (DK) {
  case Decl::ExternCContext:
    return static_cast<ExternCContextDecl *>(const_cast<Decl *>(D));
  case Decl::TranslationUnit:
    return static_cast<TranslationUnitDecl *>(const_cast<Decl *>(D));
  default:
    if (DK == Function)
      return static_cast<FunctionDecl *>(const_cast<Decl *>(D));
    if (DK >= firstTag && DK <= lastTag)
      return static_cast<TagDecl *>(const_cast<Decl *>(D));
    llvm_unreachable("a decl that inherits DeclContext isn't handled");
  }
}

SourceLocation Decl::getBodyRBrace() const {
  // FunctionDecl stores EndRangeLoc, use it directly instead of getBody().
  if (const auto *FD = dyn_cast<FunctionDecl>(this)) {
    const FunctionDecl *Definition;
    if (FD->hasBody(Definition))
      return Definition->getSourceRange().getEnd();
    return {};
  }

  if (Stmt *Body = getBody())
    return Body->getSourceRange().getEnd();

  return {};
}

int64_t Decl::getID() const {
  return getTreeContext().getAllocator().identifyKnownAlignedObject<Decl>(this);
}

const FunctionType *Decl::getFunctionType() const {
  QualType Ty;
  if (const auto *D = dyn_cast<ValueDecl>(this))
    Ty = D->getType();
  else if (const auto *D = dyn_cast<TypedefNameDecl>(this))
    Ty = D->getUnderlyingType();
  else
    return nullptr;

  if (Ty->isFunctionPointerType())
    Ty = Ty->castAs<PointerType>()->getPointeeType();

  return Ty->getAs<FunctionType>();
}

bool Decl::isFunctionPointerType() const {
  QualType Ty;
  if (const auto *D = dyn_cast<ValueDecl>(this))
    Ty = D->getType();
  else if (const auto *D = dyn_cast<TypedefNameDecl>(this))
    Ty = D->getUnderlyingType();
  else
    return false;

  return Ty.getCanonicalType()->isFunctionPointerType();
}

DeclContext *Decl::getNonTransparentDeclContext() {
  assert(getDeclContext());
  return getDeclContext()->getNonTransparentContext();
}

DeclContext::DeclContext(Decl::Kind K) {
  DeclContextBits.DeclKind = K;
  setHasLazyLocalLexicalLookups(false);
  setUseQualifiedLookup(false);
}

bool DeclContext::classof(const Decl *D) {
  switch (D->getKind()) {
#define DECL(NAME, BASE)
#define DECL_CONTEXT(NAME) case Decl::NAME:
#define DECL_CONTEXT_BASE(NAME)
#include "neverc/Tree/DeclNodes.td.h"
    return true;
  default:
#define DECL(NAME, BASE)
#define DECL_CONTEXT_BASE(NAME)                                                \
  if (D->getKind() >= Decl::first##NAME && D->getKind() <= Decl::last##NAME)   \
    return true;
#include "neverc/Tree/DeclNodes.td.h"
    return false;
  }
}

DeclContext::~DeclContext() = default;

bool DeclContext::isTransparentContext() const {
  return getDeclKind() == Decl::Enum;
}

bool DeclContext::Encloses(const DeclContext *DC) const {
  if (getPrimaryContext() != this)
    return getPrimaryContext()->Encloses(DC);

  for (; DC; DC = DC->getParent())
    if (DC->getPrimaryContext() == this)
      return true;
  return false;
}

DeclContext *DeclContext::getNonTransparentContext() {
  DeclContext *DC = this;
  while (DC->isTransparentContext()) {
    DC = DC->getParent();
    assert(DC && "All transparent contexts should have a parent!");
  }
  return DC;
}

DeclContext *DeclContext::getPrimaryContext() {
  switch (getDeclKind()) {
  case Decl::ExternCContext:
    // There is only one DeclContext for these entities.
    return this;

  case Decl::TranslationUnit:
    return static_cast<TranslationUnitDecl *>(this)->getFirstDecl();

  default:
    if (getDeclKind() >= Decl::firstTag && getDeclKind() <= Decl::lastTag) {
      // If this is a tag type that has a definition or is currently
      // being defined, that definition is our primary context.
      auto *Tag = cast<TagDecl>(this);

      if (TagDecl *Def = Tag->getDefinition())
        return Def;

      if (const auto *TagTy = dyn_cast<TagType>(Tag->getTypeForDecl())) {
        // Note, TagType::getDecl returns the (partial) definition one exists.
        TagDecl *PossiblePartialDef = TagTy->getDecl();
        if (PossiblePartialDef->isBeingDefined())
          return PossiblePartialDef;
      } else {
        llvm_unreachable("unexpected TagDecl type (expected TagType)");
      }

      return Tag;
    }

    assert(getDeclKind() >= Decl::firstFunction &&
           getDeclKind() <= Decl::lastFunction && "Unknown DeclContext kind");
    return this;
  }
}

template <typename T>
void collectAllContextsImpl(T *Self,
                            llvm::SmallVectorImpl<DeclContext *> &Contexts) {
  for (T *D = Self->getMostRecentDecl(); D; D = D->getPreviousDecl())
    Contexts.push_back(D);

  std::reverse(Contexts.begin(), Contexts.end());
}

void DeclContext::collectAllContexts(
    llvm::SmallVectorImpl<DeclContext *> &Contexts) {
  Contexts.clear();

  Decl::Kind Kind = getDeclKind();

  if (Kind == Decl::TranslationUnit)
    collectAllContextsImpl(static_cast<TranslationUnitDecl *>(this), Contexts);
  else
    Contexts.push_back(this);
}

std::pair<Decl *, Decl *>
DeclContext::FormDeclChain(llvm::ArrayRef<Decl *> Decls,
                           bool FieldsAlreadyLoaded) {
  // Build up a chain of declarations via the Decl::NextInContext field.
  Decl *FirstNewDecl = nullptr;
  Decl *PrevDecl = nullptr;
  for (auto *D : Decls) {
    if (FieldsAlreadyLoaded && isa<FieldDecl>(D))
      continue;

    if (PrevDecl)
      PrevDecl->NextInContext = D;
    else
      FirstNewDecl = D;

    PrevDecl = D;
  }

  return std::make_pair(FirstNewDecl, PrevDecl);
}

DeclContext::decl_iterator DeclContext::decls_begin() const {
  return decl_iterator(FirstDecl);
}

bool DeclContext::decls_empty() const { return !FirstDecl; }

bool DeclContext::containsDecl(Decl *D) const {
  return (D->getLexicalDeclContext() == this &&
          (D->NextInContext || D == LastDecl));
}

bool DeclContext::containsDeclAndLoad(Decl *D) const { return containsDecl(D); }

namespace {
bool shouldBeHidden(NamedDecl *D) {
  // Skip unnamed declarations.
  if (!D->getDeclName())
    return true;

  // Skip entities that can't be found by name lookup into a particular
  // context.
  if (D->getIdentifierNamespace() == 0)
    return true;

  // Skip local extern declarations unless they're the first
  // declaration of the entity.
  if (D->isLocalExternDecl() && D != D->getCanonicalDecl())
    return true;

  return false;
}
} // namespace

void DeclContext::removeDecl(Decl *D) {
  assert(D->getLexicalDeclContext() == this &&
         "decl being removed from non-lexical context");
  assert((D->NextInContext || D == LastDecl) && "decl is not in decls list");

  // Remove D from the decl chain.  This is O(n) but hopefully rare.
  if (D == FirstDecl) {
    if (D == LastDecl)
      FirstDecl = LastDecl = nullptr;
    else
      FirstDecl = D->NextInContext;
  } else {
    for (Decl *I = FirstDecl; true; I = I->NextInContext) {
      assert(I && "decl not found in linked list");
      if (I->NextInContext == D) {
        I->NextInContext = D->NextInContext;
        if (D == LastDecl)
          LastDecl = I;
        break;
      }
    }
  }

  // Mark that D is no longer in the decl chain.
  D->NextInContext = nullptr;

  // Remove D from the lookup table if necessary.
  if (isa<NamedDecl>(D)) {
    auto *ND = cast<NamedDecl>(D);

    // Do not try to remove the declaration if that is invisible to qualified
    // lookup.
    if (shouldBeHidden(ND))
      return;

    // Remove only decls that have a name
    if (!ND->getDeclName())
      return;

    auto *DC = D->getDeclContext();
    do {
      StoredDeclsMap *Map = DC->getPrimaryContext()->LookupPtr;
      if (Map) {
        StoredDeclsMap::iterator Pos = Map->find(ND->getDeclName());
        assert(Pos != Map->end() && "no lookup entry for decl");
        StoredDeclsList &List = Pos->second;
        List.remove(ND);
        // Clean up the entry if there are no more decls.
        if (List.isNull())
          Map->erase(Pos);
      }
    } while (DC->isTransparentContext() && (DC = DC->getParent()));
  }
}

__attribute__((hot)) void DeclContext::addHiddenDecl(Decl *D) {
  assert(D->getLexicalDeclContext() == this &&
         "Decl inserted into wrong lexical context");
  assert(!D->getNextDeclInContext() && D != LastDecl &&
         "Decl already inserted into a DeclContext");

  if (LLVM_LIKELY(FirstDecl != nullptr)) {
    LastDecl->NextInContext = D;
    LastDecl = D;
  } else {
    FirstDecl = LastDecl = D;
  }
}

__attribute__((hot)) void DeclContext::addDecl(Decl *D) {
  addHiddenDecl(D);

  if (auto *ND = dyn_cast<NamedDecl>(D))
    ND->getDeclContext()
        ->getPrimaryContext()
        ->makeDeclVisibleInContextWithFlags(ND, true);
}

StoredDeclsMap *DeclContext::buildLookup() {
  assert(this == getPrimaryContext() && "buildLookup called on non-primary DC");

  if (!hasLazyLocalLexicalLookups())
    return LookupPtr;

  llvm::SmallVector<DeclContext *, 2> Contexts;
  collectAllContexts(Contexts);

  for (auto *DC : Contexts)
    buildLookupImpl(DC);

  setHasLazyLocalLexicalLookups(false);
  return LookupPtr;
}

void DeclContext::buildLookupImpl(DeclContext *DCtx) {
  for (auto *D : DCtx->noload_decls()) {
    if (auto *ND = dyn_cast<NamedDecl>(D))
      if (ND->getDeclContext() == DCtx && !shouldBeHidden(ND))
        makeDeclVisibleInContextImpl(ND);

    if (auto *InnerCtx = dyn_cast<DeclContext>(D))
      if (InnerCtx->isTransparentContext())
        buildLookupImpl(InnerCtx);
  }
}

__attribute__((hot)) DeclContext::lookup_result
DeclContext::lookup(DeclarationName Name) const {
  const DeclContext *PrimaryContext = getPrimaryContext();
  if (LLVM_UNLIKELY(PrimaryContext != this))
    return PrimaryContext->lookup(Name);

  StoredDeclsMap *Map = LookupPtr;
  if (hasLazyLocalLexicalLookups())
    Map = const_cast<DeclContext *>(this)->buildLookup();

  if (!Map)
    return {};

  StoredDeclsMap::iterator I = Map->find(Name);
  if (I == Map->end())
    return {};

  return I->second.getLookupResult();
}

// If we have any lazy lexical declarations not in our lookup map, add them
// now. Don't import any external declarations, not even if we know we have
// some missing from the external visible lookups.
void DeclContext::loadLazyLocalLexicalLookups() {
  if (hasLazyLocalLexicalLookups()) {
    llvm::SmallVector<DeclContext *, 2> Contexts;
    collectAllContexts(Contexts);
    for (auto *Context : Contexts)
      buildLookupImpl(Context);
    setHasLazyLocalLexicalLookups(false);
  }
}

void DeclContext::localUncachedLookup(
    DeclarationName Name, llvm::SmallVectorImpl<NamedDecl *> &Results) {
  Results.clear();

  // If there's no external storage, just perform a normal lookup and copy
  // the results.
  if (Name) {
    lookup_result LookupResults = lookup(Name);
    Results.insert(Results.end(), LookupResults.begin(), LookupResults.end());
    if (!Results.empty())
      return;
  }

  // If we have a lookup table, check there first. Maybe we'll get lucky.
  if (Name && !hasLazyLocalLexicalLookups()) {
    if (StoredDeclsMap *Map = LookupPtr) {
      StoredDeclsMap::iterator Pos = Map->find(Name);
      if (Pos != Map->end()) {
        Results.insert(Results.end(), Pos->second.getLookupResult().begin(),
                       Pos->second.getLookupResult().end());
        return;
      }
    }
  }

  // Slow case: grovel through the declarations in our chain looking for
  // matches.
  for (Decl *D = FirstDecl; D; D = D->getNextDeclInContext()) {
    if (auto *ND = dyn_cast<NamedDecl>(D))
      if (ND->getDeclName() == Name)
        Results.push_back(ND);
  }
}

DeclContext *DeclContext::getRedeclContext() {
  DeclContext *Ctx = this;

  // In C, a record type is the redeclaration context for its fields only. If
  // we arrive at a record context after skipping anything else, we should skip
  // the record as well. Currently, this means skipping enumerations because
  // they're the only transparent context that can exist within a struct or
  // union.
  bool SkipRecords = getDeclKind() == Decl::Kind::Enum;

  // Skip through contexts to get to the redeclaration context. Transparent
  // contexts are always skipped.
  while ((SkipRecords && Ctx->isRecord()) || Ctx->isTransparentContext())
    Ctx = Ctx->getParent();
  return Ctx;
}

void DeclContext::makeDeclVisibleInContext(NamedDecl *D) {
  DeclContext *PrimaryDC = this->getPrimaryContext();
  DeclContext *DeclDC = D->getDeclContext()->getPrimaryContext();
  // If the decl is being added outside of its semantic decl context, we
  // need to ensure that we eagerly build the lookup information for it.
  PrimaryDC->makeDeclVisibleInContextWithFlags(D, PrimaryDC == DeclDC);
}

void DeclContext::makeDeclVisibleInContextWithFlags(NamedDecl *D,
                                                    bool Recoverable) {
  assert(this == getPrimaryContext() && "expected a primary DC");

  if (!isLookupContext()) {
    if (isTransparentContext())
      getParent()->getPrimaryContext()->makeDeclVisibleInContextWithFlags(
          D, Recoverable);
    return;
  }

  // Skip declarations which should be invisible to name lookup.
  if (shouldBeHidden(D))
    return;

  // If we already have a lookup data structure, perform the insertion into
  // it. If we might have externally-stored decls with this name, look them
  // up and perform the insertion. If this decl was declared outside its
  // semantic context, buildLookup won't add it, so add it now.
  //
  if (LookupPtr ||
      ((!Recoverable || D->getDeclContext() != D->getLexicalDeclContext()) &&
       !isTranslationUnit())) {
    // If we have lazily omitted any decls, they might have the same name as
    // the decl which we are adding, so build a full lookup table before adding
    // this decl.
    buildLookup();
    makeDeclVisibleInContextImpl(D);
  } else {
    setHasLazyLocalLexicalLookups(true);
  }

  // If we are a transparent context or inline namespace, insert into our
  // parent context, too. This operation is recursive.
  if (isTransparentContext())
    getParent()->getPrimaryContext()->makeDeclVisibleInContextWithFlags(
        D, Recoverable);

  auto *DCAsDecl = cast<Decl>(this);
  // Notify that a decl was made visible unless we are a Tag being defined.
  if (!(isa<TagDecl>(DCAsDecl) && cast<TagDecl>(DCAsDecl)->isBeingDefined()))
    if (TreeMutationListener *L = DCAsDecl->getTreeMutationListener())
      L->AddedVisibleDecl(this, D);
}

void DeclContext::makeDeclVisibleInContextImpl(NamedDecl *D) {
  StoredDeclsMap *Map = LookupPtr;
  if (!Map) {
    TreeContext *C = &getParentTreeContext();
    Map = CreateStoredDeclsMap(*C);
  }

  StoredDeclsList &DeclNameEntries = (*Map)[D->getDeclName()];
  DeclNameEntries.addOrReplaceDecl(D);
}

StoredDeclsMap *DeclContext::CreateStoredDeclsMap(TreeContext &C) const {
  assert(!LookupPtr && "context already has a decls map");
  assert(getPrimaryContext() == this &&
         "creating decls map on non-primary context");

  StoredDeclsMap *M = new StoredDeclsMap();
  M->Previous = C.LastSDM;
  C.LastSDM = llvm::PointerIntPair<StoredDeclsMap *, 1>(M, false);
  LookupPtr = M;
  return M;
}

void TreeContext::ReleaseDeclContextMaps() {
  // It's okay to delete DependentStoredDeclsMaps via a StoredDeclsMap
  // pointer because the subclass doesn't add anything that needs to
  // be deleted.
  StoredDeclsMap::DestroyAll(LastSDM.getPointer(), LastSDM.getInt());
  LastSDM.setPointer(nullptr);
}

void StoredDeclsMap::DestroyAll(StoredDeclsMap *Map, bool Dependent) {
  while (Map) {
    // Advance the iteration before we invalidate memory.
    llvm::PointerIntPair<StoredDeclsMap *, 1> Next = Map->Previous;

    if (Dependent)
      delete static_cast<DependentStoredDeclsMap *>(Map);
    else
      delete Map;

    Map = Next.getPointer();
    Dependent = Next.getInt();
  }
}
