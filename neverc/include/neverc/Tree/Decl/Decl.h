#ifndef NEVERC_TREE_DECL_H
#define NEVERC_TREE_DECL_H

#include "neverc/Foundation/Core/AddressSpaces.h"
#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Core/Linkage.h"
#include "neverc/Foundation/Core/PragmaKinds.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Foundation/Core/Visibility.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/PartialDiagnostic.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/ExternalTreeSource.h"
#include "neverc/Tree/Core/TreeContextAlloc.h"
#include "neverc/Tree/Decl/DeclBase.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Tree/Decl/Redeclarable.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace neverc {

using llvm::cast;
using llvm::cast_if_present;
using llvm::cast_or_null;
using llvm::dyn_cast;
using llvm::dyn_cast_if_present;
using llvm::dyn_cast_or_null;
using llvm::isa;
using llvm::isa_and_nonnull;

class TreeContext;
class EnumDecl;
class Expr;
class FunctionTypeLoc;
class LabelStmt;
class ParmVarDecl;
class RecordDecl;
class Stmt;
class StringLiteral;
class TagDecl;

class TranslationUnitDecl : public Decl,
                            public DeclContext,
                            public Redeclarable<TranslationUnitDecl> {
  using redeclarable_base = Redeclarable<TranslationUnitDecl>;

  TranslationUnitDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  TranslationUnitDecl *getPreviousDeclImpl() override {
    return getPreviousDecl();
  }

  TranslationUnitDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

  TreeContext &Ctx;

  explicit TranslationUnitDecl(TreeContext &ctx);

  virtual void anchor();

public:
  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::isFirstDecl;
  using redeclarable_base::redecls;
  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;

  TreeContext &getTreeContext() const { return Ctx; }

  static TranslationUnitDecl *Create(TreeContext &C);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == TranslationUnit; }
  static DeclContext *castToDeclContext(const TranslationUnitDecl *D) {
    return static_cast<DeclContext *>(const_cast<TranslationUnitDecl *>(D));
  }
  static TranslationUnitDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<TranslationUnitDecl *>(const_cast<DeclContext *>(DC));
  }
};

class PragmaCommentDecl final
    : public Decl,
      private llvm::TrailingObjects<PragmaCommentDecl, char> {
  friend TrailingObjects;

  PragmaMSCommentKind CommentKind;

  PragmaCommentDecl(TranslationUnitDecl *TU, SourceLocation CommentLoc,
                    PragmaMSCommentKind CommentKind)
      : Decl(PragmaComment, TU, CommentLoc), CommentKind(CommentKind) {}

  virtual void anchor();

public:
  static PragmaCommentDecl *Create(const TreeContext &C,
                                   TranslationUnitDecl *DC,
                                   SourceLocation CommentLoc,
                                   PragmaMSCommentKind CommentKind,
                                   llvm::StringRef Arg);
  PragmaMSCommentKind getCommentKind() const { return CommentKind; }

  llvm::StringRef getArg() const { return getTrailingObjects<char>(); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == PragmaComment; }
};

class PragmaDetectMismatchDecl final
    : public Decl,
      private llvm::TrailingObjects<PragmaDetectMismatchDecl, char> {
  friend TrailingObjects;

  size_t ValueStart;

  PragmaDetectMismatchDecl(TranslationUnitDecl *TU, SourceLocation Loc,
                           size_t ValueStart)
      : Decl(PragmaDetectMismatch, TU, Loc), ValueStart(ValueStart) {}

  virtual void anchor();

public:
  static PragmaDetectMismatchDecl *
  Create(const TreeContext &C, TranslationUnitDecl *DC, SourceLocation Loc,
         llvm::StringRef Name, llvm::StringRef Value);
  llvm::StringRef getName() const { return getTrailingObjects<char>(); }
  llvm::StringRef getValue() const {
    return getTrailingObjects<char>() + ValueStart;
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == PragmaDetectMismatch; }
};

class ExternCContextDecl : public Decl, public DeclContext {
  explicit ExternCContextDecl(TranslationUnitDecl *TU)
      : Decl(ExternCContext, TU, SourceLocation()),
        DeclContext(ExternCContext) {}

  virtual void anchor();

public:
  static ExternCContextDecl *Create(const TreeContext &C,
                                    TranslationUnitDecl *TU);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ExternCContext; }
  static DeclContext *castToDeclContext(const ExternCContextDecl *D) {
    return static_cast<DeclContext *>(const_cast<ExternCContextDecl *>(D));
  }
  static ExternCContextDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<ExternCContextDecl *>(const_cast<DeclContext *>(DC));
  }
};

class NamedDecl : public Decl {
  DeclarationName Name;

  virtual void anchor();

protected:
  NamedDecl(Kind DK, DeclContext *DC, SourceLocation L, DeclarationName N)
      : Decl(DK, DC, L), Name(N) {}

public:
  IdentifierInfo *getIdentifier() const { return Name.getAsIdentifierInfo(); }

  llvm::StringRef getName() const {
    return getIdentifier() ? getIdentifier()->getName() : "";
  }

  //
  std::string getNameAsString() const { return Name.getAsString(); }

  virtual void printName(llvm::raw_ostream &OS,
                         const PrintingPolicy &Policy) const;
  void printName(llvm::raw_ostream &OS) const;

  DeclarationName getDeclName() const { return Name; }

  void setDeclName(DeclarationName N) { Name = N; }

  void printQualifiedName(llvm::raw_ostream &OS) const;
  void printQualifiedName(llvm::raw_ostream &OS,
                          const PrintingPolicy &Policy) const;

  void printScopePrefix(llvm::raw_ostream &OS) const;
  void printScopePrefix(llvm::raw_ostream &OS,
                        const PrintingPolicy &Policy) const;

  std::string getQualifiedNameAsString() const;

  virtual void getNameForDiagnostic(llvm::raw_ostream &OS,
                                    const PrintingPolicy &Policy,
                                    bool Qualified) const;

  bool declarationReplaces(const NamedDecl *OldD,
                           bool IsKnownNewer = true) const;

  bool hasLinkage() const;

  ReservedIdentifierStatus isReserved(const LangOptions &LangOpts) const;

  Linkage getLinkageInternal() const;

  Linkage getFormalLinkage() const;

  bool hasExternalFormalLinkage() const {
    return isExternalFormalLinkage(getLinkageInternal());
  }

  bool isExternallyVisible() const {
    return neverc::isExternallyVisible(getLinkageInternal());
  }

  bool isExternallyDeclarable() const { return isExternallyVisible(); }

  Visibility getVisibility() const {
    return getLinkageAndVisibility().getVisibility();
  }

  LinkageInfo getLinkageAndVisibility() const;

  enum ExplicitVisibilityKind {
    /// Do an LV computation for, ultimately, a type.
    /// Visibility may be restricted by type visibility settings and
    /// the visibility of template arguments.
    VisibilityForType,

    /// Do an LV computation for, ultimately, a non-type declaration.
    /// Visibility may be restricted by value visibility settings and
    /// the visibility of template arguments.
    VisibilityForValue
  };

  std::optional<Visibility>
  getExplicitVisibility(ExplicitVisibilityKind kind) const;

  bool isLinkageValid() const;

  bool hasLinkageBeenComputed() const { return hasCachedLinkage(); }

  NamedDecl *getUnderlyingDecl() { return this; }
  const NamedDecl *getUnderlyingDecl() const {
    return const_cast<NamedDecl *>(this)->getUnderlyingDecl();
  }

  NamedDecl *getMostRecentDecl() {
    return cast<NamedDecl>(static_cast<Decl *>(this)->getMostRecentDecl());
  }
  const NamedDecl *getMostRecentDecl() const {
    return const_cast<NamedDecl *>(this)->getMostRecentDecl();
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K >= firstNamed && K <= lastNamed; }
};

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                     const NamedDecl &ND) {
  ND.printName(OS);
  return OS;
}

class LabelDecl : public NamedDecl {
  LabelStmt *TheStmt;
  llvm::StringRef MSAsmName;
  bool MSAsmNameResolved = false;

  SourceLocation LocStart;

  LabelDecl(DeclContext *DC, SourceLocation IdentL, IdentifierInfo *II,
            LabelStmt *S, SourceLocation StartL)
      : NamedDecl(Label, DC, IdentL, II), TheStmt(S), LocStart(StartL) {}

  void anchor() override;

public:
  static LabelDecl *Create(TreeContext &C, DeclContext *DC,
                           SourceLocation IdentL, IdentifierInfo *II);
  static LabelDecl *Create(TreeContext &C, DeclContext *DC,
                           SourceLocation IdentL, IdentifierInfo *II,
                           SourceLocation GnuLabelL);
  LabelStmt *getStmt() const { return TheStmt; }
  void setStmt(LabelStmt *T) { TheStmt = T; }

  bool isGnuLocal() const { return LocStart != getLocation(); }
  void setLocStart(SourceLocation L) { LocStart = L; }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(LocStart, getLocation());
  }

  bool isMSAsmLabel() const { return !MSAsmName.empty(); }
  bool isResolvedMSAsmLabel() const {
    return isMSAsmLabel() && MSAsmNameResolved;
  }
  void setMSAsmLabel(llvm::StringRef Name);
  llvm::StringRef getMSAsmLabel() const { return MSAsmName; }
  void setMSAsmLabelResolved() { MSAsmNameResolved = true; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Label; }
};

class VarDecl;

class ValueDecl : public NamedDecl {
public:
  QualType DeclType;

private:
  void anchor() override;

protected:
  ValueDecl(Kind DK, DeclContext *DC, SourceLocation L, DeclarationName N,
            QualType T)
      : NamedDecl(DK, DC, L, N), DeclType(T) {}

public:
  QualType getType() const { return DeclType; }
  void setType(QualType newType) { DeclType = newType; }

  bool isWeak() const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K >= firstValue && K <= lastValue; }
};

struct alignas(8) QualifierInfo {
  QualifierInfo() = default;
  QualifierInfo(const QualifierInfo &) = delete;
  QualifierInfo &operator=(const QualifierInfo &) = delete;
};

class DeclaratorDecl : public ValueDecl {
  // A struct representing a TInfo and a syntactic qualifier, to be used for
  // the (uncommon) case of out-of-line declarations.
  struct ExtInfo : public QualifierInfo {
    TypeSourceInfo *TInfo;
  };

  llvm::PointerUnion<TypeSourceInfo *, ExtInfo *> DeclInfo;

  SourceLocation InnerLocStart;

  bool hasExtInfo() const { return DeclInfo.is<ExtInfo *>(); }
  ExtInfo *getExtInfo() { return DeclInfo.get<ExtInfo *>(); }
  const ExtInfo *getExtInfo() const { return DeclInfo.get<ExtInfo *>(); }

protected:
  DeclaratorDecl(Kind DK, DeclContext *DC, SourceLocation L, DeclarationName N,
                 QualType T, TypeSourceInfo *TInfo, SourceLocation StartL)
      : ValueDecl(DK, DC, L, N, T), DeclInfo(TInfo), InnerLocStart(StartL) {}

public:
  TypeSourceInfo *getTypeSourceInfo() const {
    return hasExtInfo() ? getExtInfo()->TInfo
                        : DeclInfo.get<TypeSourceInfo *>();
  }

  void setTypeSourceInfo(TypeSourceInfo *TI) {
    if (hasExtInfo())
      getExtInfo()->TInfo = TI;
    else
      DeclInfo = TI;
  }

  SourceLocation getInnerLocStart() const { return InnerLocStart; }
  void setInnerLocStart(SourceLocation L) { InnerLocStart = L; }

  SourceLocation getOuterLocStart() const;

  SourceRange getSourceRange() const override LLVM_READONLY;

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getOuterLocStart();
  }

  SourceLocation getTypeSpecStartLoc() const;
  SourceLocation getTypeSpecEndLoc() const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K >= firstDeclarator && K <= lastDeclarator;
  }
};

struct EvaluatedStmt {
  bool WasEvaluated : 1;

  bool IsEvaluating : 1;

  bool HasConstantInitialization : 1;

  bool HasICEInit : 1;
  bool CheckedForICEInit : 1;

  LazyDeclStmtPtr Value;
  APValue Evaluated;

  EvaluatedStmt()
      : WasEvaluated(false), IsEvaluating(false),
        HasConstantInitialization(false), HasICEInit(false),
        CheckedForICEInit(false) {}
};

class VarDecl : public DeclaratorDecl, public Redeclarable<VarDecl> {
public:
  enum InitializationStyle {
    /// C-style initialization with assignment
    CInit,

    /// Call-style initialization \c T x(1);
    CallInit,

    /// Direct list-initialization \c T x{1};
    ListInit
  };

  enum TLSKind {
    /// Not a TLS variable.
    TLS_None,

    /// TLS with a known-constant initializer.
    TLS_Static,

    /// TLS with a dynamic initializer.
    TLS_Dynamic
  };

  static const char *getStorageClassSpecifierString(StorageClass SC);

protected:
  // A pointer union of Stmt * and EvaluatedStmt *. When an EvaluatedStmt, we
  // have allocated the auxiliary struct of information there.
  //
  using InitType = llvm::PointerUnion<Stmt *, EvaluatedStmt *>;

  mutable InitType Init;

private:
  friend class StmtIteratorBase;

  class VarDeclBitfields {
    friend class VarDecl;

    LLVM_PREFERRED_TYPE(StorageClass)
    unsigned SClass : 3;
    LLVM_PREFERRED_TYPE(ThreadStorageClassSpecifier)
    unsigned TSCSpec : 2;
    LLVM_PREFERRED_TYPE(InitializationStyle)
    unsigned InitStyle : 2;
  };
  enum { NumVarDeclBits = 8 };

protected:
  enum { NumParameterIndexBits = 8 };

  enum DefaultArgKind { DAK_None, DAK_Normal };

  enum { NumScopeDepthBits = 7 };

  class ParmVarDeclBitfields {
    friend class ParmVarDecl;

    LLVM_PREFERRED_TYPE(VarDeclBitfields)
    unsigned : NumVarDeclBits;

    LLVM_PREFERRED_TYPE(DefaultArgKind)
    unsigned DefaultArgKind : 1;

    /// Whether this parameter undergoes K&R argument promotion.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsKNRPromoted : 1;

    /// Number of function parameter scopes enclosing
    /// the function parameter scope in which this parameter was declared.
    unsigned ScopeDepth : NumScopeDepthBits;

    /// The number of parameters preceding this parameter in the
    /// function parameter scope in which it was declared.
    unsigned ParameterIndex : NumParameterIndexBits;
  };

  class NonParmVarDeclBitfields {
    friend class ImplicitParamDecl;
    friend class VarDecl;

    LLVM_PREFERRED_TYPE(VarDeclBitfields)
    unsigned : NumVarDeclBits;

    /// Whether this variable is a definition which was demoted due to
    /// module merge.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsThisDeclarationADemotedDefinition : 1;

    /// Whether this local variable could be allocated in the return
    /// slot of its function, enabling the named return value optimization
    /// (NRVO).
    LLVM_PREFERRED_TYPE(bool)
    unsigned NRVOVariable : 1;

    /// Whether this variable is \c inline.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsInline : 1;

    /// Whether \c inline was written explicitly.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsInlineSpecified : 1;

    /// Whether this variable is \c constexpr.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsConstexpr : 1;

    /// Whether this local extern variable's previous declaration was
    /// declared in the same block scope. This controls whether we should merge
    /// the type of this declaration with its previous declaration.
    LLVM_PREFERRED_TYPE(bool)
    unsigned PreviousDeclInSameBlockScope : 1;
  };

  union {
    unsigned AllBits;
    VarDeclBitfields VarDeclBits;
    ParmVarDeclBitfields ParmVarDeclBits;
    NonParmVarDeclBitfields NonParmVarDeclBits;
  };

  VarDecl(Kind DK, TreeContext &C, DeclContext *DC, SourceLocation StartLoc,
          SourceLocation IdLoc, const IdentifierInfo *Id, QualType T,
          TypeSourceInfo *TInfo, StorageClass SC);

  using redeclarable_base = Redeclarable<VarDecl>;

  VarDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  VarDecl *getPreviousDeclImpl() override { return getPreviousDecl(); }

  VarDecl *getMostRecentDeclImpl() override { return getMostRecentDecl(); }

public:
  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::isFirstDecl;
  using redeclarable_base::redecls;
  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;

  static VarDecl *Create(TreeContext &C, DeclContext *DC,
                         SourceLocation StartLoc, SourceLocation IdLoc,
                         const IdentifierInfo *Id, QualType T,
                         TypeSourceInfo *TInfo, StorageClass S);

  SourceRange getSourceRange() const override LLVM_READONLY;

  StorageClass getStorageClass() const {
    return (StorageClass)VarDeclBits.SClass;
  }
  void setStorageClass(StorageClass SC);

  void setTSCSpec(ThreadStorageClassSpecifier TSC) {
    VarDeclBits.TSCSpec = TSC;
    assert(VarDeclBits.TSCSpec == TSC && "truncation");
  }
  ThreadStorageClassSpecifier getTSCSpec() const {
    return static_cast<ThreadStorageClassSpecifier>(VarDeclBits.TSCSpec);
  }
  TLSKind getTLSKind() const;

  bool hasLocalStorage() const {
    if (getStorageClass() == SC_None) {
      return !isFileVarDecl() && getTSCSpec() == TSCS_unspecified;
    }

    // Global Named Register (GNU extension)
    if (getStorageClass() == SC_Register && !isLocalVarDeclOrParm())
      return false;

    // Return true for:  Auto, Register.
    // Return false for: Extern, Static, PrivateExtern.

    return getStorageClass() >= SC_Auto;
  }

  bool isStaticLocal() const {
    return (getStorageClass() == SC_Static ||
            (getStorageClass() == SC_None &&
             getTSCSpec() == TSCS_thread_local)) &&
           !isFileVarDecl();
  }

  bool hasExternalStorage() const {
    return getStorageClass() == SC_Extern ||
           getStorageClass() == SC_PrivateExtern;
  }

  bool hasGlobalStorage() const { return !hasLocalStorage(); }

  StorageDuration getStorageDuration() const {
    return hasLocalStorage() ? SD_Automatic
           : getTSCSpec()    ? SD_Thread
                             : SD_Static;
  }

  LanguageLinkage getLanguageLinkage() const;

  bool isExternC() const;

  bool isLocalVarDecl() const {
    if (getKind() != Decl::Var)
      return false;
    if (const DeclContext *DC = getLexicalDeclContext())
      return DC->getRedeclContext()->isFunctionOrMethod();
    return false;
  }

  bool isLocalVarDeclOrParm() const {
    return isLocalVarDecl() || getKind() == Decl::ParmVar;
  }

  bool isFunctionOrMethodVarDecl() const {
    if (getKind() != Decl::Var)
      return false;
    const DeclContext *DC = getLexicalDeclContext()->getRedeclContext();
    return DC->isFunctionOrMethod();
  }

  VarDecl *getCanonicalDecl() override;
  const VarDecl *getCanonicalDecl() const {
    return const_cast<VarDecl *>(this)->getCanonicalDecl();
  }

  enum DefinitionKind {
    /// This declaration is only a declaration.
    DeclarationOnly,

    /// This declaration is a tentative definition.
    TentativeDefinition,

    /// This declaration is definitely a definition.
    Definition
  };

  DefinitionKind isThisDeclarationADefinition(TreeContext &) const;
  DefinitionKind isThisDeclarationADefinition() const {
    return isThisDeclarationADefinition(getTreeContext());
  }

  DefinitionKind hasDefinition(TreeContext &) const;
  DefinitionKind hasDefinition() const {
    return hasDefinition(getTreeContext());
  }

  VarDecl *getActingDefinition();
  const VarDecl *getActingDefinition() const {
    return const_cast<VarDecl *>(this)->getActingDefinition();
  }

  VarDecl *getDefinition(TreeContext &);
  const VarDecl *getDefinition(TreeContext &C) const {
    return const_cast<VarDecl *>(this)->getDefinition(C);
  }
  VarDecl *getDefinition() { return getDefinition(getTreeContext()); }
  const VarDecl *getDefinition() const {
    return const_cast<VarDecl *>(this)->getDefinition();
  }

  bool isFileVarDecl() const {
    Kind K = getKind();
    if (K == ParmVar || K == ImplicitParam)
      return false;

    return getLexicalDeclContext()->getRedeclContext()->isFileContext();
  }

  const Expr *getAnyInitializer() const {
    const VarDecl *D;
    return getAnyInitializer(D);
  }

  const Expr *getAnyInitializer(const VarDecl *&D) const;

  bool hasInit() const;
  const Expr *getInit() const { return const_cast<VarDecl *>(this)->getInit(); }
  Expr *getInit();

  Stmt **getInitAddress();

  void setInit(Expr *I);

  VarDecl *getInitializingDeclaration();
  const VarDecl *getInitializingDeclaration() const {
    return const_cast<VarDecl *>(this)->getInitializingDeclaration();
  }

  EvaluatedStmt *ensureEvaluatedStmt() const;
  EvaluatedStmt *getEvaluatedStmt() const;

  APValue *evaluateValue() const;

private:
  APValue *evaluateValueImpl(llvm::SmallVectorImpl<PartialDiagnosticAt> &Notes,
                             bool IsConstantInitialization) const;

public:
  APValue *getEvaluatedValue() const;

  bool hasConstantInitialization() const;

  bool hasICEInitializer(const TreeContext &Context) const;

  bool checkForConstantInitialization(
      llvm::SmallVectorImpl<PartialDiagnosticAt> &Notes) const;

  void setInitStyle(InitializationStyle Style) {
    VarDeclBits.InitStyle = Style;
  }

  InitializationStyle getInitStyle() const {
    return static_cast<InitializationStyle>(VarDeclBits.InitStyle);
  }

  bool isDirectInit() const { return getInitStyle() != CInit; }

  bool isThisDeclarationADemotedDefinition() const {
    return isa<ParmVarDecl>(this)
               ? false
               : NonParmVarDeclBits.IsThisDeclarationADemotedDefinition;
  }

  void demoteThisDefinitionToDeclaration() {
    assert(isThisDeclarationADefinition() && "Not a definition!");
    assert(!isa<ParmVarDecl>(this) && "Cannot demote ParmVarDecls!");
    NonParmVarDeclBits.IsThisDeclarationADemotedDefinition = 1;
  }

  bool isNRVOVariable() const {
    return isa<ParmVarDecl>(this) ? false : NonParmVarDeclBits.NRVOVariable;
  }
  void setNRVOVariable(bool NRVO) {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.NRVOVariable = NRVO;
  }

  bool isInline() const {
    return isa<ParmVarDecl>(this) ? false : NonParmVarDeclBits.IsInline;
  }
  bool isInlineSpecified() const {
    return isa<ParmVarDecl>(this) ? false
                                  : NonParmVarDeclBits.IsInlineSpecified;
  }
  void setInlineSpecified() {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.IsInline = true;
    NonParmVarDeclBits.IsInlineSpecified = true;
  }
  void setImplicitlyInline() {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.IsInline = true;
  }

  bool isConstexpr() const {
    return isa<ParmVarDecl>(this) ? false : NonParmVarDeclBits.IsConstexpr;
  }
  void setConstexpr(bool IC) {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.IsConstexpr = IC;
  }

  bool isPreviousDeclInSameBlockScope() const {
    return isa<ParmVarDecl>(this)
               ? false
               : NonParmVarDeclBits.PreviousDeclInSameBlockScope;
  }
  void setPreviousDeclInSameBlockScope(bool Same) {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.PreviousDeclInSameBlockScope = Same;
  }

  bool hasDependentAlignment() const;

  // Is this variable known to have a definition somewhere in the complete
  // program? This may be true even if the declaration has internal linkage and
  // has no definition within this source file.
  bool isKnownToBeDefined() const;

  bool isNoDestroy(const TreeContext &) const;

  QualType::DestructionKind needsDestruction(const TreeContext &Ctx) const;

  bool hasFlexibleArrayInit(const TreeContext &Ctx) const;

  CharUnits getFlexibleArrayInitChars(const TreeContext &Ctx) const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K >= firstVar && K <= lastVar; }
};

class ImplicitParamDecl : public VarDecl {
  void anchor() override;

public:
  static ImplicitParamDecl *Create(TreeContext &C, DeclContext *DC,
                                   SourceLocation IdLoc, IdentifierInfo *Id,
                                   QualType T);
  static ImplicitParamDecl *Create(TreeContext &C, QualType T);

  ImplicitParamDecl(TreeContext &C, DeclContext *DC, SourceLocation IdLoc,
                    IdentifierInfo *Id, QualType Type)
      : VarDecl(ImplicitParam, C, DC, IdLoc, IdLoc, Id, Type,
                /*TInfo=*/nullptr, SC_None) {
    setImplicit();
  }

  ImplicitParamDecl(TreeContext &C, QualType Type)
      : VarDecl(ImplicitParam, C, /*DC=*/nullptr, SourceLocation(),
                SourceLocation(), /*Id=*/nullptr, Type,
                /*TInfo=*/nullptr, SC_None) {
    setImplicit();
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ImplicitParam; }
};

class ParmVarDecl : public VarDecl {
public:
  enum { MaxFunctionScopeDepth = 255 };
  enum { MaxFunctionScopeIndex = 255 };

protected:
  ParmVarDecl(Kind DK, TreeContext &C, DeclContext *DC, SourceLocation StartLoc,
              SourceLocation IdLoc, IdentifierInfo *Id, QualType T,
              TypeSourceInfo *TInfo, StorageClass S, Expr *DefArg)
      : VarDecl(DK, C, DC, StartLoc, IdLoc, Id, T, TInfo, S) {
    assert(ParmVarDeclBits.DefaultArgKind == DAK_None);
    assert(ParmVarDeclBits.IsKNRPromoted == false);
    setDefaultArg(DefArg);
  }

public:
  static ParmVarDecl *Create(TreeContext &C, DeclContext *DC,
                             SourceLocation StartLoc, SourceLocation IdLoc,
                             IdentifierInfo *Id, QualType T,
                             TypeSourceInfo *TInfo, StorageClass S,
                             Expr *DefArg);

  SourceRange getSourceRange() const override LLVM_READONLY;

  void setScopeInfo(unsigned scopeDepth, unsigned parameterIndex) {
    ParmVarDeclBits.ScopeDepth = scopeDepth;
    assert(ParmVarDeclBits.ScopeDepth == scopeDepth && "truncation!");
    setParameterIndex(parameterIndex);
  }

  unsigned getFunctionScopeDepth() const { return ParmVarDeclBits.ScopeDepth; }

  static constexpr unsigned getMaxFunctionScopeDepth() {
    return (1u << NumScopeDepthBits) - 1;
  }

  unsigned getFunctionScopeIndex() const { return getParameterIndex(); }

  bool isKNRPromoted() const { return ParmVarDeclBits.IsKNRPromoted; }
  void setKNRPromoted(bool promoted) {
    ParmVarDeclBits.IsKNRPromoted = promoted;
  }

  Expr *getDefaultArg();
  const Expr *getDefaultArg() const {
    return const_cast<ParmVarDecl *>(this)->getDefaultArg();
  }

  void setDefaultArg(Expr *defarg);

  SourceRange getDefaultArgRange() const;
  bool hasDefaultArg() const;

  QualType getOriginalType() const;

  void setOwningFunction(DeclContext *FD) { setDeclContext(FD); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ParmVar; }

private:
  enum { ParameterIndexSentinel = (1 << NumParameterIndexBits) - 1 };

  void setParameterIndex(unsigned parameterIndex) {
    if (parameterIndex >= ParameterIndexSentinel) {
      setParameterIndexLarge(parameterIndex);
      return;
    }

    ParmVarDeclBits.ParameterIndex = parameterIndex;
    assert(ParmVarDeclBits.ParameterIndex == parameterIndex && "truncation!");
  }
  unsigned getParameterIndex() const {
    unsigned d = ParmVarDeclBits.ParameterIndex;
    return d == ParameterIndexSentinel ? getParameterIndexLarge() : d;
  }

  void setParameterIndexLarge(unsigned parameterIndex);
  unsigned getParameterIndexLarge() const;
};

enum class MultiVersionKind {
  None,
  Target,
  CPUSpecific,
  CPUDispatch,
  TargetClones,
  TargetVersion
};

class FunctionDecl : public DeclaratorDecl,
                     public DeclContext,
                     public Redeclarable<FunctionDecl> {
  // This class stores some data in DeclContext::FunctionDeclBits
  // to save some space. Use the provided accessors to access it.
public:
private:
  ParmVarDecl **ParamInfo = nullptr;

  LazyDeclStmtPtr Body;

  SourceLocation EndRangeLoc;

  void setParams(TreeContext &C, llvm::ArrayRef<ParmVarDecl *> NewParamInfo);

protected:
  FunctionDecl(Kind DK, TreeContext &C, DeclContext *DC,
               SourceLocation StartLoc, const DeclarationNameInfo &NameInfo,
               QualType T, TypeSourceInfo *TInfo, StorageClass S,
               bool UsesFPIntrin, bool isInlineSpecified,
               ConstexprSpecKind ConstexprKind);

  using redeclarable_base = Redeclarable<FunctionDecl>;

  FunctionDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  FunctionDecl *getPreviousDeclImpl() override { return getPreviousDecl(); }

  FunctionDecl *getMostRecentDeclImpl() override { return getMostRecentDecl(); }

public:
  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::isFirstDecl;
  using redeclarable_base::redecls;
  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;

  static FunctionDecl *
  Create(TreeContext &C, DeclContext *DC, SourceLocation StartLoc,
         SourceLocation NLoc, DeclarationName N, QualType T,
         TypeSourceInfo *TInfo, StorageClass SC, bool UsesFPIntrin = false,
         bool isInlineSpecified = false, bool hasWrittenPrototype = true,
         ConstexprSpecKind ConstexprKind = ConstexprSpecKind::Unspecified) {
    DeclarationNameInfo NameInfo(N, NLoc);
    return FunctionDecl::Create(C, DC, StartLoc, NameInfo, T, TInfo, SC,
                                UsesFPIntrin, isInlineSpecified,
                                hasWrittenPrototype, ConstexprKind);
  }

  static FunctionDecl *
  Create(TreeContext &C, DeclContext *DC, SourceLocation StartLoc,
         const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
         StorageClass SC, bool UsesFPIntrin, bool isInlineSpecified,
         bool hasWrittenPrototype, ConstexprSpecKind ConstexprKind);

  DeclarationNameInfo getNameInfo() const {
    return DeclarationNameInfo(getDeclName(), getLocation());
  }

  void getNameForDiagnostic(llvm::raw_ostream &OS, const PrintingPolicy &Policy,
                            bool Qualified) const override;

  void setRangeEnd(SourceLocation E) { EndRangeLoc = E; }

  SourceLocation getEllipsisLoc() const {
    const auto *FPT = getType()->getAs<FunctionProtoType>();
    if (FPT && FPT->isVariadic())
      return FPT->getEllipsisLoc();
    return SourceLocation();
  }

  SourceRange getSourceRange() const override LLVM_READONLY;

  // Function definitions.
  //
  // A function declaration may be:
  // - a non defining declaration,
  // - a definition. A function may be defined because:
  //   - it has a body, or will have it in the case of late parsing.
  //   - it has an uninstantiated body. The body does not exist because the
  //     function is not used yet, but the declaration is considered a
  //     definition and does not allow other definition of this function.
  //   - it does not have a user specified body, but it does not allow
  //     redefinition, because it is defined through some other mechanism
  //     (alias, ifunc).

  bool hasBody(const FunctionDecl *&Definition) const;

  bool hasBody() const override {
    const FunctionDecl *Definition;
    return hasBody(Definition);
  }

  bool isDefined(const FunctionDecl *&Definition) const;

  bool isDefined() const {
    const FunctionDecl *Definition;
    return isDefined(Definition);
  }

  FunctionDecl *getDefinition() {
    const FunctionDecl *Definition;
    if (isDefined(Definition))
      return const_cast<FunctionDecl *>(Definition);
    return nullptr;
  }
  const FunctionDecl *getDefinition() const {
    return const_cast<FunctionDecl *>(this)->getDefinition();
  }

  Stmt *getBody(const FunctionDecl *&Definition) const;

  Stmt *getBody() const override {
    const FunctionDecl *Definition;
    return getBody(Definition);
  }

  bool isThisDeclarationADefinition() const {
    return doesThisDeclarationHaveABody() || willHaveBody() ||
           hasDefiningAttr();
  }

  bool doesThisDeclarationHaveABody() const { return Body.isValid(); }

  void setBody(Stmt *B);

  bool isVariadic() const;

  bool hasImplicitReturnZero() const {
    return FunctionDeclBits.HasImplicitReturnZero;
  }

  void setHasImplicitReturnZero(bool IRZ) {
    FunctionDeclBits.HasImplicitReturnZero = IRZ;
  }

  bool hasPrototype() const {
    return hasWrittenPrototype() || hasInheritedPrototype();
  }

  bool hasWrittenPrototype() const {
    return FunctionDeclBits.HasWrittenPrototype;
  }

  void setHasWrittenPrototype(bool P = true) {
    FunctionDeclBits.HasWrittenPrototype = P;
  }

  bool hasInheritedPrototype() const {
    return FunctionDeclBits.HasInheritedPrototype;
  }

  void setHasInheritedPrototype(bool P = true) {
    FunctionDeclBits.HasInheritedPrototype = P;
  }

  bool isConstexpr() const {
    return getConstexprKind() != ConstexprSpecKind::Unspecified;
  }
  void setConstexprKind(ConstexprSpecKind CSK) {
    FunctionDeclBits.ConstexprKind = static_cast<uint64_t>(CSK);
  }
  ConstexprSpecKind getConstexprKind() const {
    return static_cast<ConstexprSpecKind>(FunctionDeclBits.ConstexprKind);
  }

  bool usesSEHTry() const { return FunctionDeclBits.UsesSEHTry; }
  void setUsesSEHTry(bool UST) { FunctionDeclBits.UsesSEHTry = UST; }

  bool isMain() const;

  bool isMSVCRTEntryPoint() const;

  bool isInlineBuiltinDeclaration() const;

  LanguageLinkage getLanguageLinkage() const;

  bool isExternC() const;

  bool isGlobal() const;

  bool isNoReturn() const;

  bool willHaveBody() const { return FunctionDeclBits.WillHaveBody; }
  void setWillHaveBody(bool V = true) { FunctionDeclBits.WillHaveBody = V; }

  bool isMultiVersion() const {
    return getCanonicalDecl()->FunctionDeclBits.IsMultiVersion;
  }

  void setIsMultiVersion(bool V = true) {
    getCanonicalDecl()->FunctionDeclBits.IsMultiVersion = V;
  }

  MultiVersionKind getMultiVersionKind() const;

  bool isCPUDispatchMultiVersion() const;
  bool isCPUSpecificMultiVersion() const;

  bool isTargetMultiVersion() const;

  bool isTargetClonesMultiVersion() const;

  void setPreviousDeclaration(FunctionDecl *PrevDecl);

  FunctionDecl *getCanonicalDecl() override;
  const FunctionDecl *getCanonicalDecl() const {
    return const_cast<FunctionDecl *>(this)->getCanonicalDecl();
  }

  unsigned getBuiltinID(bool ConsiderWrapperFunctions = false) const;

  // llvm::ArrayRef interface to parameters.
  llvm::ArrayRef<ParmVarDecl *> parameters() const {
    return {ParamInfo, getNumParams()};
  }
  llvm::MutableArrayRef<ParmVarDecl *> parameters() {
    return {ParamInfo, getNumParams()};
  }

  // Iterator access to formal parameters.
  using param_iterator = llvm::MutableArrayRef<ParmVarDecl *>::iterator;
  using param_const_iterator = llvm::ArrayRef<ParmVarDecl *>::const_iterator;

  bool param_empty() const { return parameters().empty(); }
  param_iterator param_begin() { return parameters().begin(); }
  param_iterator param_end() { return parameters().end(); }
  param_const_iterator param_begin() const { return parameters().begin(); }
  param_const_iterator param_end() const { return parameters().end(); }
  size_t param_size() const { return parameters().size(); }

  unsigned getNumParams() const;

  const ParmVarDecl *getParamDecl(unsigned i) const {
    assert(i < getNumParams() && "Illegal param #");
    return ParamInfo[i];
  }
  ParmVarDecl *getParamDecl(unsigned i) {
    assert(i < getNumParams() && "Illegal param #");
    return ParamInfo[i];
  }
  void setParams(llvm::ArrayRef<ParmVarDecl *> NewParamInfo) {
    setParams(getTreeContext(), NewParamInfo);
  }

  unsigned getMinRequiredArguments() const;

  FunctionTypeLoc getFunctionTypeLoc() const;

  QualType getReturnType() const {
    return getType()->castAs<FunctionType>()->getReturnType();
  }

  SourceRange getReturnTypeSourceRange() const;

  SourceRange getParametersSourceRange() const;

  QualType getDeclaredReturnType() const {
    auto *TSI = getTypeSourceInfo();
    QualType T = TSI ? TSI->getType() : getType();
    return T->castAs<FunctionType>()->getReturnType();
  }

  QualType getCallResultType() const {
    return getType()->castAs<FunctionType>()->getCallResultType(
        getTreeContext());
  }

  StorageClass getStorageClass() const {
    return static_cast<StorageClass>(FunctionDeclBits.SClass);
  }

  void setStorageClass(StorageClass SClass) {
    FunctionDeclBits.SClass = SClass;
  }

  bool isInlineSpecified() const { return FunctionDeclBits.IsInlineSpecified; }

  void setInlineSpecified(bool I) {
    FunctionDeclBits.IsInlineSpecified = I;
    FunctionDeclBits.IsInline = I;
  }

  bool UsesFPIntrin() const { return FunctionDeclBits.UsesFPIntrin; }

  void setUsesFPIntrin(bool I) { FunctionDeclBits.UsesFPIntrin = I; }

  void setImplicitlyInline(bool I = true) { FunctionDeclBits.IsInline = I; }

  bool isInlined() const { return FunctionDeclBits.IsInline; }

  bool isInlineDefinitionExternallyVisible() const;

  bool isMSExternInline() const;

  bool doesDeclarationForceExternallyVisibleDefinition() const;

  bool isStatic() const { return getStorageClass() == SC_Static; }

  unsigned getMemoryFunctionKind() const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Function; }
  static DeclContext *castToDeclContext(const FunctionDecl *D) {
    return static_cast<DeclContext *>(const_cast<FunctionDecl *>(D));
  }
  static FunctionDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<FunctionDecl *>(const_cast<DeclContext *>(DC));
  }
};

class FieldDecl : public DeclaratorDecl, public Mergeable<FieldDecl> {
  LLVM_PREFERRED_TYPE(bool)
  unsigned BitField : 1;
  mutable unsigned CachedFieldIndex : 28;

  union {
    LazyDeclStmtPtr Init;
    Expr *BitWidth;
  };

protected:
  FieldDecl(Kind DK, DeclContext *DC, SourceLocation StartLoc,
            SourceLocation IdLoc, IdentifierInfo *Id, QualType T,
            TypeSourceInfo *TInfo, Expr *BW)
      : DeclaratorDecl(DK, DC, IdLoc, Id, T, TInfo, StartLoc), BitField(false),
        CachedFieldIndex(0), Init() {
    if (BW)
      setBitWidth(BW);
  }

public:
  static FieldDecl *Create(const TreeContext &C, DeclContext *DC,
                           SourceLocation StartLoc, SourceLocation IdLoc,
                           IdentifierInfo *Id, QualType T,
                           TypeSourceInfo *TInfo, Expr *BW);

  unsigned getFieldIndex() const;

  bool isBitField() const { return BitField; }

  bool isUnnamedBitfield() const { return isBitField() && !getDeclName(); }

  bool isAnonymousStructOrUnion() const;

  Expr *getBitWidth() const { return BitField ? BitWidth : nullptr; }

  unsigned getBitWidthValue(const TreeContext &Ctx) const;

  // Note: used by some clients (i.e., do not remove it).
  void setBitWidth(Expr *Width) {
    assert(!BitField && "bit width already set");
    assert(Width && "no bit width specified");
    BitWidth = Width;
    BitField = true;
  }

  void removeBitWidth() {
    assert(isBitField() && "no bitfield width to remove");
    BitField = false;
  }

  bool isZeroLengthBitField(const TreeContext &Ctx) const;

  bool isZeroSize(const TreeContext &Ctx) const;

  const RecordDecl *getParent() const {
    return dyn_cast<RecordDecl>(getDeclContext());
  }

  RecordDecl *getParent() { return dyn_cast<RecordDecl>(getDeclContext()); }

  SourceRange getSourceRange() const override LLVM_READONLY;

  FieldDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const FieldDecl *getCanonicalDecl() const { return getFirstDecl(); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Field; }

  void printName(llvm::raw_ostream &OS,
                 const PrintingPolicy &Policy) const override;
};

class EnumConstantDecl : public ValueDecl, public Mergeable<EnumConstantDecl> {
  Stmt *Init;       // an integer constant expression
  llvm::APSInt Val; // The value.

protected:
  EnumConstantDecl(DeclContext *DC, SourceLocation L, IdentifierInfo *Id,
                   QualType T, Expr *E, const llvm::APSInt &V)
      : ValueDecl(EnumConstant, DC, L, Id, T), Init((Stmt *)E), Val(V) {}

public:
  friend class StmtIteratorBase;

  static EnumConstantDecl *Create(TreeContext &C, EnumDecl *DC,
                                  SourceLocation L, IdentifierInfo *Id,
                                  QualType T, Expr *E, const llvm::APSInt &V);
  const Expr *getInitExpr() const { return (const Expr *)Init; }
  Expr *getInitExpr() { return (Expr *)Init; }
  const llvm::APSInt &getInitVal() const { return Val; }

  void setInitExpr(Expr *E) { Init = (Stmt *)E; }
  void setInitVal(const llvm::APSInt &V) { Val = V; }

  SourceRange getSourceRange() const override LLVM_READONLY;

  EnumConstantDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const EnumConstantDecl *getCanonicalDecl() const { return getFirstDecl(); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == EnumConstant; }
};

class IndirectFieldDecl : public ValueDecl,
                          public Mergeable<IndirectFieldDecl> {
  NamedDecl **Chaining;
  unsigned ChainingSize;

  IndirectFieldDecl(TreeContext &C, DeclContext *DC, SourceLocation L,
                    DeclarationName N, QualType T,
                    llvm::MutableArrayRef<NamedDecl *> CH);

  void anchor() override;

public:
  static IndirectFieldDecl *Create(TreeContext &C, DeclContext *DC,
                                   SourceLocation L, IdentifierInfo *Id,
                                   QualType T,
                                   llvm::MutableArrayRef<NamedDecl *> CH);

  using chain_iterator = llvm::ArrayRef<NamedDecl *>::const_iterator;

  llvm::ArrayRef<NamedDecl *> chain() const {
    return llvm::ArrayRef(Chaining, ChainingSize);
  }
  chain_iterator chain_begin() const { return chain().begin(); }
  chain_iterator chain_end() const { return chain().end(); }

  unsigned getChainingSize() const { return ChainingSize; }

  FieldDecl *getAnonField() const {
    assert(chain().size() >= 2);
    return cast<FieldDecl>(chain().back());
  }

  VarDecl *getVarDecl() const {
    assert(chain().size() >= 2);
    return dyn_cast<VarDecl>(chain().front());
  }

  IndirectFieldDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const IndirectFieldDecl *getCanonicalDecl() const { return getFirstDecl(); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == IndirectField; }
};

class TypeDecl : public NamedDecl {
  friend class TreeContext;

  mutable const Type *TypeForDecl = nullptr;

  SourceLocation LocStart;

  void anchor() override;

protected:
  TypeDecl(Kind DK, DeclContext *DC, SourceLocation L, IdentifierInfo *Id,
           SourceLocation StartL = SourceLocation())
      : NamedDecl(DK, DC, L, Id), LocStart(StartL) {}

public:
  // Low-level accessor. If you just want the type defined by this node,
  // check out TreeContext::getTypeDeclType or one of
  // TreeContext::getTypedefType, TreeContext::getRecordType, etc. if you
  // already know the specific kind of node this is.
  const Type *getTypeForDecl() const { return TypeForDecl; }
  void setTypeForDecl(const Type *TD) { TypeForDecl = TD; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LocStart; }
  void setLocStart(SourceLocation L) { LocStart = L; }
  SourceRange getSourceRange() const override LLVM_READONLY {
    if (LocStart.isValid())
      return SourceRange(LocStart, getLocation());
    else
      return SourceRange(getLocation());
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K >= firstType && K <= lastType; }
};

class TypedefNameDecl : public TypeDecl, public Redeclarable<TypedefNameDecl> {
  struct alignas(8) ModedTInfo {
    TypeSourceInfo *first;
    QualType second;
  };

  mutable llvm::PointerIntPair<
      llvm::PointerUnion<TypeSourceInfo *, ModedTInfo *>, 2>
      MaybeModedTInfo;

  void anchor() override;

protected:
  TypedefNameDecl(Kind DK, TreeContext &C, DeclContext *DC,
                  SourceLocation StartLoc, SourceLocation IdLoc,
                  IdentifierInfo *Id, TypeSourceInfo *TInfo)
      : TypeDecl(DK, DC, IdLoc, Id, StartLoc), redeclarable_base(C),
        MaybeModedTInfo(TInfo, 0) {}

  using redeclarable_base = Redeclarable<TypedefNameDecl>;

  TypedefNameDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  TypedefNameDecl *getPreviousDeclImpl() override { return getPreviousDecl(); }

  TypedefNameDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

public:
  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::isFirstDecl;
  using redeclarable_base::redecls;
  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;

  bool isModed() const {
    return MaybeModedTInfo.getPointer().is<ModedTInfo *>();
  }

  TypeSourceInfo *getTypeSourceInfo() const {
    return isModed() ? MaybeModedTInfo.getPointer().get<ModedTInfo *>()->first
                     : MaybeModedTInfo.getPointer().get<TypeSourceInfo *>();
  }

  QualType getUnderlyingType() const {
    return isModed() ? MaybeModedTInfo.getPointer().get<ModedTInfo *>()->second
                     : MaybeModedTInfo.getPointer()
                           .get<TypeSourceInfo *>()
                           ->getType();
  }

  void setTypeSourceInfo(TypeSourceInfo *newType) {
    MaybeModedTInfo.setPointer(newType);
  }

  void setModedTypeSourceInfo(TypeSourceInfo *unmodedTSI, QualType modedTy) {
    MaybeModedTInfo.setPointer(new (getTreeContext(), 8)
                                   ModedTInfo({unmodedTSI, modedTy}));
  }

  TypedefNameDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const TypedefNameDecl *getCanonicalDecl() const { return getFirstDecl(); }

  TagDecl *getAnonDeclWithTypedefName(bool AnyRedecl = false) const;

  bool isTransparentTag() const {
    if (MaybeModedTInfo.getInt())
      return MaybeModedTInfo.getInt() & 0x2;
    return isTransparentTagSlow();
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K >= firstTypedefName && K <= lastTypedefName;
  }

private:
  bool isTransparentTagSlow() const;
};

class TypedefDecl : public TypedefNameDecl {
  TypedefDecl(TreeContext &C, DeclContext *DC, SourceLocation StartLoc,
              SourceLocation IdLoc, IdentifierInfo *Id, TypeSourceInfo *TInfo)
      : TypedefNameDecl(Typedef, C, DC, StartLoc, IdLoc, Id, TInfo) {}

public:
  static TypedefDecl *Create(TreeContext &C, DeclContext *DC,
                             SourceLocation StartLoc, SourceLocation IdLoc,
                             IdentifierInfo *Id, TypeSourceInfo *TInfo);
  SourceRange getSourceRange() const override LLVM_READONLY;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Typedef; }
};

class TagDecl : public TypeDecl,
                public DeclContext,
                public Redeclarable<TagDecl> {
  // This class stores some data in DeclContext::TagDeclBits
  // to save some space. Use the provided accessors to access it.
public:
  // This is really ugly.
  using TagKind = TagTypeKind;

private:
  SourceRange BraceRange;

  // A struct representing syntactic qualifier info,
  // to be used for the (uncommon) case of out-of-line declarations.
  using ExtInfo = QualifierInfo;

  llvm::PointerUnion<TypedefNameDecl *, ExtInfo *> TypedefNameDeclOrQualifier;

  bool hasExtInfo() const { return TypedefNameDeclOrQualifier.is<ExtInfo *>(); }
  ExtInfo *getExtInfo() { return TypedefNameDeclOrQualifier.get<ExtInfo *>(); }
  const ExtInfo *getExtInfo() const {
    return TypedefNameDeclOrQualifier.get<ExtInfo *>();
  }

protected:
  TagDecl(Kind DK, TagKind TK, const TreeContext &C, DeclContext *DC,
          SourceLocation L, IdentifierInfo *Id, TagDecl *PrevDecl,
          SourceLocation StartL);

  using redeclarable_base = Redeclarable<TagDecl>;

  TagDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  TagDecl *getPreviousDeclImpl() override { return getPreviousDecl(); }

  TagDecl *getMostRecentDeclImpl() override { return getMostRecentDecl(); }

  void completeDefinition();

  void setBeingDefined(bool V = true) { TagDeclBits.IsBeingDefined = V; }

public:
  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::isFirstDecl;
  using redeclarable_base::redecls;
  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;

  SourceRange getBraceRange() const { return BraceRange; }
  void setBraceRange(SourceRange R) { BraceRange = R; }

  SourceLocation getInnerLocStart() const { return getBeginLoc(); }
  SourceLocation getOuterLocStart() const;
  SourceRange getSourceRange() const override LLVM_READONLY;

  TagDecl *getCanonicalDecl() override;
  const TagDecl *getCanonicalDecl() const {
    return const_cast<TagDecl *>(this)->getCanonicalDecl();
  }

  bool isThisDeclarationADefinition() const { return isCompleteDefinition(); }

  bool isCompleteDefinition() const { return TagDeclBits.IsCompleteDefinition; }

  void setCompleteDefinition(bool V = true) {
    TagDeclBits.IsCompleteDefinition = V;
  }

  bool isCompleteDefinitionRequired() const {
    return TagDeclBits.IsCompleteDefinitionRequired;
  }

  void setCompleteDefinitionRequired(bool V = true) {
    TagDeclBits.IsCompleteDefinitionRequired = V;
  }

  bool isBeingDefined() const { return TagDeclBits.IsBeingDefined; }

  bool isEmbeddedInDeclarator() const {
    return TagDeclBits.IsEmbeddedInDeclarator;
  }

  void setEmbeddedInDeclarator(bool isInDeclarator) {
    TagDeclBits.IsEmbeddedInDeclarator = isInDeclarator;
  }

  bool isFreeStanding() const { return TagDeclBits.IsFreeStanding; }

  void setFreeStanding(bool isFreeStanding = true) {
    TagDeclBits.IsFreeStanding = isFreeStanding;
  }

  bool isThisDeclarationADemotedDefinition() const {
    return TagDeclBits.IsThisDeclarationADemotedDefinition;
  }

  void demoteThisDefinitionToDeclaration() {
    assert(isCompleteDefinition() &&
           "Should demote definitions only, not forward declarations");
    setCompleteDefinition(false);
    TagDeclBits.IsThisDeclarationADemotedDefinition = true;
  }

  void startDefinition();

  TagDecl *getDefinition() const;

  llvm::StringRef getKindName() const {
    return TypeWithKeyword::getTagTypeKindName(getTagKind());
  }

  TagKind getTagKind() const {
    return static_cast<TagKind>(TagDeclBits.TagDeclKind);
  }

  void setTagKind(TagKind TK) {
    TagDeclBits.TagDeclKind = llvm::to_underlying(TK);
  }

  bool isStruct() const { return getTagKind() == TagTypeKind::Struct; }
  bool isUnion() const { return getTagKind() == TagTypeKind::Union; }
  bool isEnum() const { return getTagKind() == TagTypeKind::Enum; }

  bool hasNameForLinkage() const {
    return (getDeclName() || getTypedefNameForAnonDecl());
  }

  TypedefNameDecl *getTypedefNameForAnonDecl() const {
    return hasExtInfo() ? nullptr
                        : TypedefNameDeclOrQualifier.get<TypedefNameDecl *>();
  }

  void setTypedefNameForAnonDecl(TypedefNameDecl *TDD);

  using TypeDecl::printName;
  void printName(llvm::raw_ostream &OS,
                 const PrintingPolicy &Policy) const override;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K >= firstTag && K <= lastTag; }

  static DeclContext *castToDeclContext(const TagDecl *D) {
    return static_cast<DeclContext *>(const_cast<TagDecl *>(D));
  }

  static TagDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<TagDecl *>(const_cast<DeclContext *>(DC));
  }
};

class EnumDecl : public TagDecl {
  // This class stores some data in DeclContext::EnumDeclBits
  // to save some space. Use the provided accessors to access it.

  llvm::PointerUnion<const Type *, TypeSourceInfo *> IntegerType;

  QualType PromotionType;

  EnumDecl(TreeContext &C, DeclContext *DC, SourceLocation StartLoc,
           SourceLocation IdLoc, IdentifierInfo *Id, EnumDecl *PrevDecl,
           bool Fixed);

  void anchor() override;

  void setNumPositiveBits(unsigned Num) {
    EnumDeclBits.NumPositiveBits = Num;
    assert(EnumDeclBits.NumPositiveBits == Num && "can't store this bitcount");
  }

  void setNumNegativeBits(unsigned Num) { EnumDeclBits.NumNegativeBits = Num; }

public:
  void setFixed(bool Fixed = true) { EnumDeclBits.IsFixed = Fixed; }

public:
  EnumDecl *getCanonicalDecl() override {
    return cast<EnumDecl>(TagDecl::getCanonicalDecl());
  }
  const EnumDecl *getCanonicalDecl() const {
    return const_cast<EnumDecl *>(this)->getCanonicalDecl();
  }

  EnumDecl *getPreviousDecl() {
    return cast_or_null<EnumDecl>(
        static_cast<TagDecl *>(this)->getPreviousDecl());
  }
  const EnumDecl *getPreviousDecl() const {
    return const_cast<EnumDecl *>(this)->getPreviousDecl();
  }

  EnumDecl *getMostRecentDecl() {
    return cast<EnumDecl>(static_cast<TagDecl *>(this)->getMostRecentDecl());
  }
  const EnumDecl *getMostRecentDecl() const {
    return const_cast<EnumDecl *>(this)->getMostRecentDecl();
  }

  EnumDecl *getDefinition() const {
    return cast_or_null<EnumDecl>(TagDecl::getDefinition());
  }

  static EnumDecl *Create(TreeContext &C, DeclContext *DC,
                          SourceLocation StartLoc, SourceLocation IdLoc,
                          IdentifierInfo *Id, EnumDecl *PrevDecl, bool IsFixed);
  SourceRange getSourceRange() const override LLVM_READONLY;

  void completeDefinition(QualType NewType, QualType PromotionType,
                          unsigned NumPositiveBits, unsigned NumNegativeBits);

  // Iterates through the enumerators of this enumeration.
  using enumerator_iterator = specific_decl_iterator<EnumConstantDecl>;
  using enumerator_range =
      llvm::iterator_range<specific_decl_iterator<EnumConstantDecl>>;

  enumerator_range enumerators() const {
    return enumerator_range(enumerator_begin(), enumerator_end());
  }

  enumerator_iterator enumerator_begin() const {
    const EnumDecl *E = getDefinition();
    if (!E)
      E = this;
    return enumerator_iterator(E->decls_begin());
  }

  enumerator_iterator enumerator_end() const {
    const EnumDecl *E = getDefinition();
    if (!E)
      E = this;
    return enumerator_iterator(E->decls_end());
  }

  QualType getPromotionType() const { return PromotionType; }

  void setPromotionType(QualType T) { PromotionType = T; }

  QualType getIntegerType() const {
    if (!IntegerType)
      return QualType();
    if (const Type *T = IntegerType.dyn_cast<const Type *>())
      return QualType(T, 0);
    return IntegerType.get<TypeSourceInfo *>()->getType().getUnqualifiedType();
  }

  void setIntegerType(QualType T) { IntegerType = T.getTypePtrOrNull(); }

  void setIntegerTypeSourceInfo(TypeSourceInfo *TInfo) { IntegerType = TInfo; }

  TypeSourceInfo *getIntegerTypeSourceInfo() const {
    return IntegerType.dyn_cast<TypeSourceInfo *>();
  }

  SourceRange getIntegerTypeRange() const LLVM_READONLY;

  unsigned getNumPositiveBits() const { return EnumDeclBits.NumPositiveBits; }

  unsigned getNumNegativeBits() const { return EnumDeclBits.NumNegativeBits; }

  void getValueRange(llvm::APInt &Max, llvm::APInt &Min) const;

  bool isFixed() const { return EnumDeclBits.IsFixed; }

  bool isComplete() const {
    // IntegerType is set for fixed type enums and non-fixed but implicitly
    // int-sized Microsoft enums.
    return isCompleteDefinition() || IntegerType;
  }

  bool isClosed() const;

  bool isClosedFlag() const;

  bool isClosedNonFlag() const;

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Enum; }
};

class RecordDecl : public TagDecl {
  // This class stores some data in DeclContext::RecordDeclBits
  // to save some space. Use the provided accessors to access it.
public:
  friend class DeclContext;

protected:
  RecordDecl(Kind DK, TagKind TK, const TreeContext &C, DeclContext *DC,
             SourceLocation StartLoc, SourceLocation IdLoc, IdentifierInfo *Id,
             RecordDecl *PrevDecl);

public:
  static RecordDecl *Create(const TreeContext &C, TagKind TK, DeclContext *DC,
                            SourceLocation StartLoc, SourceLocation IdLoc,
                            IdentifierInfo *Id, RecordDecl *PrevDecl = nullptr);
  RecordDecl *getPreviousDecl() {
    return cast_or_null<RecordDecl>(
        static_cast<TagDecl *>(this)->getPreviousDecl());
  }
  const RecordDecl *getPreviousDecl() const {
    return const_cast<RecordDecl *>(this)->getPreviousDecl();
  }

  RecordDecl *getMostRecentDecl() {
    return cast<RecordDecl>(static_cast<TagDecl *>(this)->getMostRecentDecl());
  }
  const RecordDecl *getMostRecentDecl() const {
    return const_cast<RecordDecl *>(this)->getMostRecentDecl();
  }

  bool hasFlexibleArrayMember() const {
    return RecordDeclBits.HasFlexibleArrayMember;
  }

  void setHasFlexibleArrayMember(bool V) {
    RecordDeclBits.HasFlexibleArrayMember = V;
  }

  bool isAnonymousStructOrUnion() const {
    return RecordDeclBits.AnonymousStructOrUnion;
  }

  void setAnonymousStructOrUnion(bool Anon) {
    RecordDeclBits.AnonymousStructOrUnion = Anon;
  }

  bool hasVolatileMember() const { return RecordDeclBits.HasVolatileMember; }

  void setHasVolatileMember(bool val) {
    RecordDeclBits.HasVolatileMember = val;
  }

  bool isNonTrivialToPrimitiveDefaultInitialize() const {
    return RecordDeclBits.NonTrivialToPrimitiveDefaultInitialize;
  }

  void setNonTrivialToPrimitiveDefaultInitialize(bool V) {
    RecordDeclBits.NonTrivialToPrimitiveDefaultInitialize = V;
  }

  bool isNonTrivialToPrimitiveCopy() const {
    return RecordDeclBits.NonTrivialToPrimitiveCopy;
  }

  void setNonTrivialToPrimitiveCopy(bool V) {
    RecordDeclBits.NonTrivialToPrimitiveCopy = V;
  }

  bool isNonTrivialToPrimitiveDestroy() const {
    return RecordDeclBits.NonTrivialToPrimitiveDestroy;
  }

  void setNonTrivialToPrimitiveDestroy(bool V) {
    RecordDeclBits.NonTrivialToPrimitiveDestroy = V;
  }

  bool hasNonTrivialToPrimitiveDefaultInitializeCUnion() const {
    return RecordDeclBits.HasNonTrivialToPrimitiveDefaultInitializeCUnion;
  }

  void setHasNonTrivialToPrimitiveDefaultInitializeCUnion(bool V) {
    RecordDeclBits.HasNonTrivialToPrimitiveDefaultInitializeCUnion = V;
  }

  bool hasNonTrivialToPrimitiveDestructCUnion() const {
    return RecordDeclBits.HasNonTrivialToPrimitiveDestructCUnion;
  }

  void setHasNonTrivialToPrimitiveDestructCUnion(bool V) {
    RecordDeclBits.HasNonTrivialToPrimitiveDestructCUnion = V;
  }

  bool hasNonTrivialToPrimitiveCopyCUnion() const {
    return RecordDeclBits.HasNonTrivialToPrimitiveCopyCUnion;
  }

  void setHasNonTrivialToPrimitiveCopyCUnion(bool V) {
    RecordDeclBits.HasNonTrivialToPrimitiveCopyCUnion = V;
  }

  bool isRandomized() const { return RecordDeclBits.IsRandomized; }

  void setIsRandomized(bool V) { RecordDeclBits.IsRandomized = V; }

  void reorderDecls(const llvm::SmallVectorImpl<Decl *> &Decls);

  RecordDecl *getDefinition() const {
    return cast_or_null<RecordDecl>(TagDecl::getDefinition());
  }

  bool isOrContainsUnion() const;

  // Iterator access to field members. The field iterator only visits
  // the fields of this record.
  using field_iterator = specific_decl_iterator<FieldDecl>;
  using field_range = llvm::iterator_range<specific_decl_iterator<FieldDecl>>;

  field_range fields() const { return field_range(field_begin(), field_end()); }
  field_iterator field_begin() const;

  field_iterator field_end() const { return field_iterator(decl_iterator()); }

  // Whether there are any fields in this record.
  bool field_empty() const { return field_begin() == field_end(); }

  virtual void completeDefinition();

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Record; }

  bool isMsStruct(const TreeContext &C) const;

  const FieldDecl *findFirstNamedDataMember() const;

private:
  void LoadFieldsFromExternalStorage() const;
};

class FileScopeAsmDecl : public Decl {
  StringLiteral *AsmString;
  SourceLocation RParenLoc;

  FileScopeAsmDecl(DeclContext *DC, StringLiteral *asmstring,
                   SourceLocation StartL, SourceLocation EndL)
      : Decl(FileScopeAsm, DC, StartL), AsmString(asmstring), RParenLoc(EndL) {}

  virtual void anchor();

public:
  static FileScopeAsmDecl *Create(TreeContext &C, DeclContext *DC,
                                  StringLiteral *Str, SourceLocation AsmLoc,
                                  SourceLocation RParenLoc);

  SourceLocation getAsmLoc() const { return getLocation(); }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }
  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getAsmLoc(), getRParenLoc());
  }

  const StringLiteral *getAsmString() const { return AsmString; }
  StringLiteral *getAsmString() { return AsmString; }
  void setAsmString(StringLiteral *Asm) { AsmString = Asm; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == FileScopeAsm; }
};

class EmptyDecl : public Decl {
  EmptyDecl(DeclContext *DC, SourceLocation L) : Decl(Empty, DC, L) {}

  virtual void anchor();

public:
  static EmptyDecl *Create(TreeContext &C, DeclContext *DC, SourceLocation L);
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Empty; }
};

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &PD,
                                             const NamedDecl *ND) {
  PD.AddTaggedVal(reinterpret_cast<uint64_t>(ND),
                  DiagnosticsEngine::ak_nameddecl);
  return PD;
}

template <typename decl_type>
void Redeclarable<decl_type>::setPreviousDecl(decl_type *PrevDecl) {
  // Note: This routine is implemented here because we need both NamedDecl
  // and Redeclarable to be defined.
  assert(RedeclLink.isFirst() &&
         "setPreviousDecl on a decl already in a redeclaration chain");

  if (PrevDecl) {
    // Point to previous. Make sure that this is actually the most recent
    // redeclaration, or we can build invalid chains. If the most recent
    // redeclaration is invalid, it won't be PrevDecl, but we want it anyway.
    First = PrevDecl->getFirstDecl();
    assert(First->RedeclLink.isFirst() && "Expected first");
    decl_type *MostRecent = First->getNextRedeclaration();
    RedeclLink = PreviousDeclLink(cast<decl_type>(MostRecent));

    // If the declaration was previously visible, a redeclaration of it remains
    // visible even if it wouldn't be visible by itself.
    static_cast<decl_type *>(this)->IdentifierNamespace |=
        MostRecent->getIdentifierNamespace() &
        (Decl::IDNS_Ordinary | Decl::IDNS_Tag | Decl::IDNS_Type);
  } else {
    // Make this first.
    First = static_cast<decl_type *>(this);
  }

  // First one will point to this one as latest.
  First->RedeclLink.setLatest(static_cast<decl_type *>(this));

  assert(!isa<NamedDecl>(static_cast<decl_type *>(this)) ||
         cast<NamedDecl>(static_cast<decl_type *>(this))->isLinkageValid());
}

// Inline function definitions.

inline bool IsEnumDeclComplete(EnumDecl *ED) { return ED->isComplete(); }

} // namespace neverc

#endif // NEVERC_TREE_DECL_H
