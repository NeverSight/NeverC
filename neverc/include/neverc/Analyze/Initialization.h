#ifndef NEVERC_SEMA_INITIALIZATION_H
#define NEVERC_SEMA_INITIALIZATION_H

#include "neverc/Analyze/Ownership.h"
#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstdint>
#include <string>

namespace neverc {

using llvm::isa;

class Sema;

class alignas(8) InitializedEntity {
public:
  enum EntityKind {
    /// The entity being initialized is a variable.
    EK_Variable,

    /// The entity being initialized is a function parameter.
    EK_Parameter,

    /// The entity being initialized is the result of a function call.
    EK_Result,

    /// The entity being initialized is the result of a statement expression.
    EK_StmtExprResult,

    /// The entity being initialized is a field subobject.
    EK_Member,

    /// The entity being initialized is an element of an array.
    EK_ArrayElement,

    /// The entity being initialized is a temporary object.
    EK_Temporary,

    /// The entity being initialized is an element of a vector.
    EK_VectorElement,

    /// The entity being initialized is the real or imaginary part of a
    /// complex number.
    EK_ComplexElement,

    /// The entity being initialized is the initializer for a compound
    /// literal.
    EK_CompoundLiteralInit,

    // Note: err_init_conversion_failed in DiagnosticSemaKinds.td uses this
    // enum as an index for its first %select.  When modifying this list,
    // that diagnostic text needs to be updated as well.
  };

private:
  EntityKind Kind;

  const InitializedEntity *Parent = nullptr;

  QualType Type;

  mutable unsigned ManglingNumber = 0;

  struct LN {
    /// When Kind == EK_Result, the location of the 'return' keyword.
    /// When Kind == EK_Temporary, the location where the temporary
    /// is being created.
    SourceLocation Location;

    /// Whether the entity being initialized may end up using the
    /// named return value optimization (NRVO).
    bool NRVO;
  };

  struct VD {
    /// The VarDecl or FieldDecl being initialized.
    ValueDecl *VariableOrMember;
  };

  union {
    /// When Kind == EK_Variable or EK_Member, the variable or member.
    VD Variable;

    /// When Kind == EK_Parameter, the ParmVarDecl, with the
    /// integer indicating whether the parameter is "consumed".
    llvm::PointerIntPair<ParmVarDecl *, 1> Parameter;

    /// When Kind == EK_Temporary or EK_CompoundLiteralInit, the type
    /// source information for the temporary.
    TypeSourceInfo *TypeInfo;

    struct LN LocAndNRVO;

    /// When Kind == EK_ArrayElement, EK_VectorElement, or
    /// EK_ComplexElement, the index of the array or vector element being
    /// initialized.
    unsigned Index;
  };

  InitializedEntity() {};

  InitializedEntity(VarDecl *Var, EntityKind EK = EK_Variable)
      : Kind(EK), Type(Var->getType()), Variable{Var} {}

  InitializedEntity(EntityKind Kind, SourceLocation Loc, QualType Type,
                    bool NRVO = false)
      : Kind(Kind), Type(Type) {
    new (&LocAndNRVO) LN;
    LocAndNRVO.Location = Loc;
    LocAndNRVO.NRVO = NRVO;
  }

  InitializedEntity(FieldDecl *Member, const InitializedEntity *Parent)
      : Kind(EK_Member), Parent(Parent), Type(Member->getType()),
        Variable{Member} {}

  InitializedEntity(TreeContext &Context, unsigned Index,
                    const InitializedEntity &Parent);

public:
  static InitializedEntity InitializeVariable(VarDecl *Var) {
    return InitializedEntity(Var);
  }

  static InitializedEntity InitializeParameter(TreeContext &Context,
                                               ParmVarDecl *Parm) {
    return InitializeParameter(Context, Parm, Parm->getType());
  }

  static InitializedEntity
  InitializeParameter(TreeContext &Context, ParmVarDecl *Parm, QualType Type) {
    bool Consumed = false;

    InitializedEntity Entity;
    Entity.Kind = EK_Parameter;
    Entity.Type =
        Context.getVariableArrayDecayedType(Type.getUnqualifiedType());
    Entity.Parent = nullptr;
    Entity.Parameter = {Parm, Consumed};
    return Entity;
  }

  static InitializedEntity InitializeParameter(TreeContext &Context,
                                               QualType Type, bool Consumed) {
    InitializedEntity Entity;
    Entity.Kind = EK_Parameter;
    Entity.Type = Context.getVariableArrayDecayedType(Type);
    Entity.Parent = nullptr;
    Entity.Parameter = {nullptr, Consumed};
    return Entity;
  }

  static InitializedEntity InitializeResult(SourceLocation ReturnLoc,
                                            QualType Type) {
    return InitializedEntity(EK_Result, ReturnLoc, Type);
  }

  static InitializedEntity InitializeStmtExprResult(SourceLocation ReturnLoc,
                                                    QualType Type) {
    return InitializedEntity(EK_StmtExprResult, ReturnLoc, Type);
  }

  static InitializedEntity InitializeTemporary(QualType Type) {
    return InitializeTemporary(nullptr, Type);
  }

  static InitializedEntity InitializeTemporary(TreeContext &Context,
                                               TypeSourceInfo *TypeInfo) {
    return InitializeTemporary(TypeInfo, TypeInfo->getType());
  }

  static InitializedEntity InitializeTemporary(TypeSourceInfo *TypeInfo,
                                               QualType Type) {
    InitializedEntity Result(EK_Temporary, SourceLocation(), Type);
    Result.TypeInfo = TypeInfo;
    return Result;
  }

  static InitializedEntity
  InitializeMember(FieldDecl *Member,
                   const InitializedEntity *Parent = nullptr) {
    return InitializedEntity(Member, Parent);
  }

  static InitializedEntity
  InitializeMember(IndirectFieldDecl *Member,
                   const InitializedEntity *Parent = nullptr) {
    return InitializedEntity(Member->getAnonField(), Parent);
  }

  static InitializedEntity InitializeElement(TreeContext &Context,
                                             unsigned Index,
                                             const InitializedEntity &Parent) {
    return InitializedEntity(Context, Index, Parent);
  }

  static InitializedEntity InitializeCompoundLiteralInit(TypeSourceInfo *TSI) {
    InitializedEntity Result(EK_CompoundLiteralInit, SourceLocation(),
                             TSI->getType());
    Result.TypeInfo = TSI;
    return Result;
  }

  EntityKind getKind() const { return Kind; }

  const InitializedEntity *getParent() const { return Parent; }

  QualType getType() const { return Type; }

  TypeSourceInfo *getTypeSourceInfo() const {
    if (Kind == EK_Temporary || Kind == EK_CompoundLiteralInit)
      return TypeInfo;

    return nullptr;
  }

  DeclarationName getName() const;

  ValueDecl *getDecl() const;

  bool allowsNRVO() const;

  bool isParameterKind() const { return getKind() == EK_Parameter; }

  SourceLocation getReturnLoc() const {
    assert(getKind() == EK_Result && "No 'return' location!");
    return LocAndNRVO.Location;
  }

  unsigned getElementIndex() const {
    assert(getKind() == EK_ArrayElement || getKind() == EK_VectorElement ||
           getKind() == EK_ComplexElement);
    return Index;
  }

  void setElementIndex(unsigned Index) {
    assert(getKind() == EK_ArrayElement || getKind() == EK_VectorElement ||
           getKind() == EK_ComplexElement);
    this->Index = Index;
  }

  unsigned allocateManglingNumber() const { return ++ManglingNumber; }

  void dump() const;

private:
  unsigned dumpImpl(llvm::raw_ostream &OS) const;
};

class InitializationKind {
public:
  enum InitKind {
    /// Direct initialization
    IK_Direct,

    /// Direct list-initialization
    IK_DirectList,

    /// Copy initialization
    IK_Copy,

    /// Default initialization
    IK_Default,

    /// Value initialization
    IK_Value
  };

private:
  enum InitContext {
    /// Normal context
    IC_Normal,

    /// Implicit context (value initialization)
    IC_Implicit,

    /// C-style cast context
    IC_CStyleCast,
  };

  InitKind Kind : 8;

  InitContext Context : 8;

  SourceLocation Locations[3];

  InitializationKind(InitKind Kind, InitContext Context, SourceLocation Loc1,
                     SourceLocation Loc2, SourceLocation Loc3)
      : Kind(Kind), Context(Context) {
    Locations[0] = Loc1;
    Locations[1] = Loc2;
    Locations[2] = Loc3;
  }

public:
  static InitializationKind CreateDirect(SourceLocation InitLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation RParenLoc) {
    return InitializationKind(IK_Direct, IC_Normal, InitLoc, LParenLoc,
                              RParenLoc);
  }

  static InitializationKind CreateDirectList(SourceLocation InitLoc) {
    return InitializationKind(IK_DirectList, IC_Normal, InitLoc, InitLoc,
                              InitLoc);
  }

  static InitializationKind CreateDirectList(SourceLocation InitLoc,
                                             SourceLocation LBraceLoc,
                                             SourceLocation RBraceLoc) {
    return InitializationKind(IK_DirectList, IC_Normal, InitLoc, LBraceLoc,
                              RBraceLoc);
  }

  static InitializationKind CreateCStyleCast(SourceLocation StartLoc,
                                             SourceRange TypeRange,
                                             bool InitList) {
    // Compound literals use list initialization; other casts use direct init.
    return InitializationKind(InitList ? IK_DirectList : IK_Direct,
                              IC_CStyleCast, StartLoc, TypeRange.getBegin(),
                              TypeRange.getEnd());
  }

  static InitializationKind CreateCopy(SourceLocation InitLoc,
                                       SourceLocation EqualLoc) {
    return InitializationKind(IK_Copy, IC_Normal, InitLoc, EqualLoc, EqualLoc);
  }

  static InitializationKind CreateDefault(SourceLocation InitLoc) {
    return InitializationKind(IK_Default, IC_Normal, InitLoc, InitLoc, InitLoc);
  }

  static InitializationKind CreateValue(SourceLocation InitLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation RParenLoc,
                                        bool isImplicit = false) {
    return InitializationKind(IK_Value, isImplicit ? IC_Implicit : IC_Normal,
                              InitLoc, LParenLoc, RParenLoc);
  }

  static InitializationKind CreateForInit(SourceLocation Loc, bool DirectInit,
                                          Expr *Init) {
    if (!Init)
      return CreateDefault(Loc);
    if (!DirectInit)
      return CreateCopy(Loc, Init->getBeginLoc());
    if (isa<InitListExpr>(Init))
      return CreateDirectList(Loc, Init->getBeginLoc(), Init->getEndLoc());
    return CreateDirect(Loc, Init->getBeginLoc(), Init->getEndLoc());
  }

  InitKind getKind() const { return Kind; }

  bool isExplicitCast() const { return Context == IC_CStyleCast; }

  bool isCStyleOrFunctionalCast() const { return Context == IC_CStyleCast; }

  bool isCStyleCast() const { return Context == IC_CStyleCast; }

  bool isImplicitValueInit() const { return Context == IC_Implicit; }

  SourceLocation getLocation() const { return Locations[0]; }

  SourceRange getRange() const {
    return SourceRange(Locations[0], Locations[2]);
  }

  SourceLocation getEqualLoc() const {
    assert(Kind == IK_Copy && "Only copy initialization has an '='");
    return Locations[1];
  }

  bool isCopyInit() const { return Kind == IK_Copy; }

  bool AllowExplicit() const { return !isCopyInit(); }

  bool hasParenOrBraceRange() const {
    return Kind == IK_Direct || Kind == IK_Value || Kind == IK_DirectList;
  }

  SourceRange getParenOrBraceRange() const {
    assert(hasParenOrBraceRange() && "Only direct, value, and direct-list "
                                     "initialization have parentheses or "
                                     "braces");
    return SourceRange(Locations[1], Locations[2]);
  }
};

class InitializationSequence {
public:
  enum SequenceKind {
    /// A failed initialization sequence. The failure kind tells what
    /// happened.
    FailedSequence = 0,

    /// A dependent initialization, which could not be
    /// type-checked due to the presence of dependent types or
    /// dependently-typed expressions.
    DependentSequence,

    /// A normal sequence.
    NormalSequence
  };

  enum StepKind {
    SK_ListInitialization,
    SK_ZeroInitialization,
    SK_CAssignment,
    SK_StringInit,
    SK_ArrayLoopIndex,
    SK_ArrayLoopInit,
    SK_ArrayInit,
    SK_GNUArrayInit
  };

  class Step {
  public:
    /// The kind of conversion or initialization step we are taking.
    StepKind Kind;

    // The type that results from this initialization.
    QualType Type;

    void Destroy();
  };

private:
  enum SequenceKind SequenceKind;

  llvm::SmallVector<Step, 4> Steps;

public:
  enum FailureKind {
    /// Array must be initialized with an initializer list.
    FK_ArrayNeedsInitList,

    /// Array must be initialized with an initializer list or a
    /// string literal.
    FK_ArrayNeedsInitListOrStringLiteral,

    /// Array must be initialized with an initializer list or a
    /// wide string literal.
    FK_ArrayNeedsInitListOrWideStringLiteral,

    /// Initializing a wide char array with narrow string literal.
    FK_NarrowStringIntoWideCharArray,

    /// Initializing char array with wide string literal.
    FK_WideStringIntoCharArray,

    /// Initializing wide char array with incompatible wide string
    /// literal.
    FK_IncompatWideStringIntoWideChar,

    /// Initializing char8_t array with plain string literal.
    FK_PlainStringIntoUTF8Char,

    /// Initializing char array with UTF-8 string literal.
    FK_UTF8StringIntoPlainChar,

    /// Array type mismatch.
    FK_ArrayTypeMismatch,

    /// Non-constant array initializer
    FK_NonConstantArrayInit,

    /// Implicit conversion failed.
    FK_ConversionFailed,

    /// Too many initializers for scalar
    FK_TooManyInitsForScalar,

    /// Initialization of some unused destination type with an
    /// initializer list.
    FK_InitListBadDestinationType,

    /// Default-initialization of a 'const' object.
    FK_DefaultInitOfConst,

    /// Initialization of an incomplete type.
    FK_Incomplete,

    /// Variable-length array must not have an initializer.
    FK_VariableLengthArrayHasInitializer,

    /// List initialization failed at some point.
    FK_ListInitializationFailed,

    /// Initializer has a placeholder type which cannot be
    /// resolved by initialization.
    FK_PlaceholderType,

    /// Trying to take the address of a function that doesn't support
    /// having its address taken.
    FK_AddressOfUnaddressableFunction,

    // A designated initializer was provided for a non-aggregate type.
    FK_DesignatedInitForNonAggregate,
  };

private:
  FailureKind Failure;

  QualType FailedIncompleteType;

  std::string ZeroInitializationFixit;
  SourceLocation ZeroInitializationFixitLoc;

public:
  void SetZeroInitializationFixit(const std::string &Fixit, SourceLocation L) {
    ZeroInitializationFixit = Fixit;
    ZeroInitializationFixitLoc = L;
  }

private:
  void PrintInitLocationNote(Sema &S, const InitializedEntity &Entity);

public:
  InitializationSequence(Sema &S, const InitializedEntity &Entity,
                         const InitializationKind &Kind, MultiExprArg Args,
                         bool TopLevelOfInitList = false,
                         bool TreatUnavailableAsInvalid = true);
  void InitializeFrom(Sema &S, const InitializedEntity &Entity,
                      const InitializationKind &Kind, MultiExprArg Args,
                      bool TopLevelOfInitList, bool TreatUnavailableAsInvalid);

  ~InitializationSequence();

  ExprResult Perform(Sema &S, const InitializedEntity &Entity,
                     const InitializationKind &Kind, MultiExprArg Args,
                     QualType *ResultType = nullptr);

  bool Diagnose(Sema &S, const InitializedEntity &Entity,
                const InitializationKind &Kind, llvm::ArrayRef<Expr *> Args);

  enum SequenceKind getKind() const { return SequenceKind; }

  void setSequenceKind(enum SequenceKind SK) { SequenceKind = SK; }

  explicit operator bool() const { return !Failed(); }

  bool Failed() const { return SequenceKind == FailedSequence; }

  using step_iterator = llvm::SmallVectorImpl<Step>::const_iterator;

  step_iterator step_begin() const { return Steps.begin(); }
  step_iterator step_end() const { return Steps.end(); }

  using step_range = llvm::iterator_range<step_iterator>;

  step_range steps() const { return {step_begin(), step_end()}; }

  bool isAmbiguous() const;

  void AddListInitializationStep(QualType T);

  void AddZeroInitializationStep(QualType T);

  void AddCAssignmentStep(QualType T);

  void AddStringInitStep(QualType T);

  void AddArrayInitLoopStep(QualType T, QualType EltTy);

  void AddArrayInitStep(QualType T, bool IsGNUExtension);

  void SetFailed(FailureKind Failure) {
    SequenceKind = FailedSequence;
    this->Failure = Failure;
    assert((Failure != FK_Incomplete || !FailedIncompleteType.isNull()) &&
           "Incomplete type failure requires a type!");
  }

  void setIncompleteTypeFailure(QualType IncompleteType) {
    FailedIncompleteType = IncompleteType;
    SetFailed(FK_Incomplete);
  }

  FailureKind getFailureKind() const {
    assert(Failed() && "Not an initialization failure!");
    return Failure;
  }

  void dump(llvm::raw_ostream &OS) const;

  void dump() const;
};

} // namespace neverc

#endif // NEVERC_SEMA_INITIALIZATION_H
