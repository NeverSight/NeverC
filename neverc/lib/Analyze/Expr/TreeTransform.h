#ifndef NEVERC_LIB_SEMA_TREETRANSFORM_H
#define NEVERC_LIB_SEMA_TREETRANSFORM_H

#include "Type/TypeLocBuilder.h"
#include "neverc/Analyze/Designator.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Lookup.h"
#include "neverc/Analyze/Ownership.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaDiag.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Diagnostic/DiagnosticParse.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "llvm/ADT/ArrayRef.h"
#include <optional>

namespace neverc {
using namespace sema;

template <typename Derived> class TreeTransform {
protected:
  Sema &SemaRef;

  llvm::DenseMap<Decl *, Decl *> TransformedLocalDecls;

public:
  TreeTransform(Sema &SemaRef) : SemaRef(SemaRef) {}

  Derived &getDerived() { return static_cast<Derived &>(*this); }

  const Derived &getDerived() const {
    return static_cast<const Derived &>(*this);
  }

  static inline ExprResult Owned(Expr *E) { return E; }
  static inline StmtResult Owned(Stmt *S) { return S; }

  Sema &getSema() const { return SemaRef; }

  bool AlwaysRebuild() { return false; }

  SourceLocation getBaseLocation() { return SourceLocation(); }

  DeclarationName getBaseEntity() { return DeclarationName(); }

  void setBase(SourceLocation Loc, DeclarationName Entity) {}

  class TemporaryBase {
    TreeTransform &Self;
    SourceLocation OldLocation;
    DeclarationName OldEntity;

  public:
    TemporaryBase(TreeTransform &Self, SourceLocation Location,
                  DeclarationName Entity)
        : Self(Self) {
      OldLocation = Self.getDerived().getBaseLocation();
      OldEntity = Self.getDerived().getBaseEntity();

      if (Location.isValid())
        Self.getDerived().setBase(Location, Entity);
    }

    ~TemporaryBase() { Self.getDerived().setBase(OldLocation, OldEntity); }
  };

  bool AlreadyTransformed(QualType T) { return T.isNull(); }

  QualType TransformType(QualType T);

  TypeSourceInfo *TransformType(TypeSourceInfo *DI);

  QualType TransformType(TypeLocBuilder &TLB, TypeLoc TL);

  enum StmtDiscardKind {
    SDK_Discarded,
    SDK_NotDiscarded,
    SDK_StmtExprResult,
  };

  StmtResult TransformStmt(Stmt *S, StmtDiscardKind SDK = SDK_Discarded);

  const Attr *TransformAttr(const Attr *S);

  // Transform the given statement attribute.
  //
  // Delegates to the appropriate TransformXXXAttr function to transform a
  // specific kind of statement attribute. Unlike the non-statement taking
  // version of this, this implements all attributes, not just pragmas.
  const Attr *TransformStmtAttr(const Stmt *OrigS, const Stmt *InstS,
                                const Attr *A);

  // Transform the specified attribute.
  //
  // Subclasses should override the transformation of attributes with a pragma
  // spelling to transform expressions stored within the attribute.
  //
  // \returns the transformed attribute.
#define ATTR(X)                                                                \
  const X##Attr *Transform##X##Attr(const X##Attr *R) { return R; }
#include "neverc/Foundation/AttrList.td.h"

  // Transform the specified attribute.
  //
  // Subclasses should override the transformation of attributes to do
  // transformation and checking of statement attributes. By default, this
  // delegates to the non-statement taking version.
  //
  // \returns the transformed attribute.
#define ATTR(X)                                                                \
  const X##Attr *TransformStmt##X##Attr(const Stmt *, const Stmt *,            \
                                        const X##Attr *A) {                    \
    return getDerived().Transform##X##Attr(A);                                 \
  }
#include "neverc/Foundation/AttrList.td.h"

  ExprResult TransformExpr(Expr *E);

  ExprResult TransformInitializer(Expr *Init, bool NotCopyInit);

  bool TransformExprs(Expr *const *Inputs, unsigned NumInputs, bool IsCall,
                      llvm::SmallVectorImpl<Expr *> &Outputs,
                      bool *ArgChanged = nullptr);

  Decl *TransformDecl(SourceLocation Loc, Decl *D) {
    llvm::DenseMap<Decl *, Decl *>::iterator Known =
        TransformedLocalDecls.find(D);
    if (Known != TransformedLocalDecls.end())
      return Known->second;

    return D;
  }

  Sema::ConditionResult TransformCondition(SourceLocation Loc, VarDecl *Var,
                                           Expr *Expr,
                                           Sema::ConditionKind Kind);

  void transformedLocalDecl(Decl *Old, llvm::ArrayRef<Decl *> New) {
    assert(New.size() == 1 &&
           "must override transformedLocalDecl for multiple replacements");
    TransformedLocalDecls[Old] = New.front();
  }

  Decl *TransformDefinition(SourceLocation Loc, Decl *D) {
    return getDerived().TransformDecl(Loc, D);
  }

  DeclarationNameInfo
  TransformDeclarationNameInfo(const DeclarationNameInfo &NameInfo);

  TypeSourceInfo *InventTypeSourceInfo(QualType T) {
    return SemaRef.Context.getTrivialTypeSourceInfo(
        T, getDerived().getBaseLocation());
  }

#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  QualType Transform##CLASS##Type(TypeLocBuilder &TLB, CLASS##TypeLoc T);
#include "neverc/Tree/Type/TypeLocNodes.def"

  template <typename Fn>
  QualType TransformFunctionProtoType(TypeLocBuilder &TLB,
                                      FunctionProtoTypeLoc TL,
                                      Fn TransformExceptionSpec);

  StmtResult TransformSEHHandler(Stmt *Handler);

  bool TransformFunctionTypeParams(
      SourceLocation Loc, llvm::ArrayRef<ParmVarDecl *> Params,
      const QualType *ParamTypes,
      const FunctionProtoType::ExtParameterInfo *ParamInfos,
      llvm::SmallVectorImpl<QualType> &PTypes,
      llvm::SmallVectorImpl<ParmVarDecl *> *PVars,
      Sema::ExtParameterInfoBuilder &PInfos, unsigned *LastParamTransformed);

  bool TransformFunctionTypeParams(
      SourceLocation Loc, llvm::ArrayRef<ParmVarDecl *> Params,
      const QualType *ParamTypes,
      const FunctionProtoType::ExtParameterInfo *ParamInfos,
      llvm::SmallVectorImpl<QualType> &PTypes,
      llvm::SmallVectorImpl<ParmVarDecl *> *PVars,
      Sema::ExtParameterInfoBuilder &PInfos) {
    return getDerived().TransformFunctionTypeParams(
        Loc, Params, ParamTypes, ParamInfos, PTypes, PVars, PInfos, nullptr);
  }

  ParmVarDecl *TransformFunctionTypeParam(ParmVarDecl *OldParm,
                                          int indexAdjustment,
                                          std::optional<unsigned> NumExpansions,
                                          bool ExpectParameterPack);

  StmtResult TransformCompoundStmt(CompoundStmt *S, bool IsStmtExpr);

  ExprResult TransformAddressOfOperand(Expr *E);

// LLVM_ATTRIBUTE_NOINLINE because inlining causes excessive stack usage.
#define STMT(Node, Parent)                                                     \
  LLVM_ATTRIBUTE_NOINLINE                                                      \
  StmtResult Transform##Node(Node *S);
#define VALUESTMT(Node, Parent)                                                \
  LLVM_ATTRIBUTE_NOINLINE                                                      \
  StmtResult Transform##Node(Node *S, StmtDiscardKind SDK);
#define EXPR(Node, Parent)                                                     \
  LLVM_ATTRIBUTE_NOINLINE                                                      \
  ExprResult Transform##Node(Node *E);
#define ABSTRACT_STMT(Stmt)
#include "neverc/Tree/StmtNodes.td.h"

  QualType RebuildQualifiedType(QualType T, QualifiedTypeLoc TL);

  QualType RebuildPointerType(QualType PointeeType, SourceLocation Sigil);

  QualType RebuildArrayType(QualType ElementType, ArraySizeModifier SizeMod,
                            const llvm::APInt *Size, Expr *SizeExpr,
                            unsigned IndexTypeQuals, SourceRange BracketsRange);

  QualType RebuildConstantArrayType(QualType ElementType,
                                    ArraySizeModifier SizeMod,
                                    const llvm::APInt &Size, Expr *SizeExpr,
                                    unsigned IndexTypeQuals,
                                    SourceRange BracketsRange);

  QualType RebuildIncompleteArrayType(QualType ElementType,
                                      ArraySizeModifier SizeMod,
                                      unsigned IndexTypeQuals,
                                      SourceRange BracketsRange);

  QualType RebuildVariableArrayType(QualType ElementType,
                                    ArraySizeModifier SizeMod, Expr *SizeExpr,
                                    unsigned IndexTypeQuals,
                                    SourceRange BracketsRange);

  QualType RebuildVectorType(QualType ElementType, unsigned NumElements,
                             VectorKind VecKind);

  QualType RebuildExtVectorType(QualType ElementType, unsigned NumElements,
                                SourceLocation AttributeLoc);

  QualType RebuildConstantMatrixType(QualType ElementType, unsigned NumRows,
                                     unsigned NumColumns);

  QualType RebuildFunctionProtoType(QualType T,
                                    llvm::MutableArrayRef<QualType> ParamTypes,
                                    const FunctionProtoType::ExtProtoInfo &EPI);

  QualType RebuildFunctionNoProtoType(QualType T);

  QualType RebuildTypedefType(TypedefNameDecl *Typedef) {
    return SemaRef.Context.getTypeDeclType(Typedef);
  }

  QualType RebuildMacroQualifiedType(QualType T,
                                     const IdentifierInfo *MacroII) {
    return SemaRef.Context.getMacroQualifiedType(T, MacroII);
  }

  QualType RebuildRecordType(RecordDecl *Record) {
    return SemaRef.Context.getTypeDeclType(Record);
  }

  QualType RebuildEnumType(EnumDecl *Enum) {
    return SemaRef.Context.getTypeDeclType(Enum);
  }

  QualType RebuildTypeOfExprType(Expr *Underlying, SourceLocation Loc,
                                 TypeOfKind Kind);

  QualType RebuildTypeOfType(QualType Underlying, TypeOfKind Kind);

  QualType RebuildAutoType(QualType Deduced, AutoTypeKeyword Keyword) {
    return SemaRef.Context.getAutoType(Deduced, Keyword,
                                       /*IsDependent*/ false);
  }

  QualType RebuildParenType(QualType InnerType) {
    return SemaRef.FormParenType(InnerType);
  }

  QualType RebuildElaboratedType(SourceLocation KeywordLoc,
                                 ElaboratedTypeKeyword Keyword,
                                 QualType Named) {
    return SemaRef.Context.getElaboratedType(Keyword, Named);
  }

  QualType RebuildAtomicType(QualType ValueType, SourceLocation KWLoc);

  QualType RebuildBitIntType(bool IsUnsigned, unsigned NumBits,
                             SourceLocation Loc);

  StmtResult RebuildCompoundStmt(SourceLocation LBraceLoc,
                                 MultiStmtArg Statements,
                                 SourceLocation RBraceLoc, bool IsStmtExpr) {
    return getSema().OnCompoundStmt(LBraceLoc, RBraceLoc, Statements,
                                    IsStmtExpr);
  }

  StmtResult RebuildCaseStmt(SourceLocation CaseLoc, Expr *LHS,
                             SourceLocation EllipsisLoc, Expr *RHS,
                             SourceLocation ColonLoc) {
    return getSema().OnCaseStmt(CaseLoc, LHS, EllipsisLoc, RHS, ColonLoc);
  }

  StmtResult RebuildCaseStmtBody(Stmt *S, Stmt *Body) {
    getSema().OnCaseStmtBody(S, Body);
    return S;
  }

  StmtResult RebuildDefaultStmt(SourceLocation DefaultLoc,
                                SourceLocation ColonLoc, Stmt *SubStmt) {
    return getSema().OnDefaultStmt(DefaultLoc, ColonLoc, SubStmt,
                                   /*CurScope=*/nullptr);
  }

  StmtResult RebuildLabelStmt(SourceLocation IdentLoc, LabelDecl *L,
                              SourceLocation ColonLoc, Stmt *SubStmt) {
    return SemaRef.OnLabelStmt(IdentLoc, L, ColonLoc, SubStmt);
  }

  StmtResult RebuildAttributedStmt(SourceLocation AttrLoc,
                                   llvm::ArrayRef<const Attr *> Attrs,
                                   Stmt *SubStmt) {
    if (SemaRef.CheckRebuiltStmtAttributes(Attrs))
      return StmtError();
    return SemaRef.FormAttributedStmt(AttrLoc, Attrs, SubStmt);
  }

  StmtResult RebuildIfStmt(SourceLocation IfLoc, SourceLocation LParenLoc,
                           Sema::ConditionResult Cond, SourceLocation RParenLoc,
                           Stmt *Init, Stmt *Then, SourceLocation ElseLoc,
                           Stmt *Else) {
    return getSema().OnIfStmt(IfLoc, LParenLoc, Init, Cond, RParenLoc, Then,
                              ElseLoc, Else);
  }

  StmtResult RebuildSwitchStmtStart(SourceLocation SwitchLoc,
                                    SourceLocation LParenLoc, Stmt *Init,
                                    Sema::ConditionResult Cond,
                                    SourceLocation RParenLoc) {
    return getSema().OnStartOfSwitchStmt(SwitchLoc, LParenLoc, Init, Cond,
                                         RParenLoc);
  }

  StmtResult RebuildSwitchStmtBody(SourceLocation SwitchLoc, Stmt *Switch,
                                   Stmt *Body) {
    return getSema().OnFinishSwitchStmt(SwitchLoc, Switch, Body);
  }

  StmtResult RebuildWhileStmt(SourceLocation WhileLoc, SourceLocation LParenLoc,
                              Sema::ConditionResult Cond,
                              SourceLocation RParenLoc, Stmt *Body) {
    return getSema().OnWhileStmt(WhileLoc, LParenLoc, Cond, RParenLoc, Body);
  }

  StmtResult RebuildDoStmt(SourceLocation DoLoc, Stmt *Body,
                           SourceLocation WhileLoc, SourceLocation LParenLoc,
                           Expr *Cond, SourceLocation RParenLoc) {
    return getSema().OnDoStmt(DoLoc, Body, WhileLoc, LParenLoc, Cond,
                              RParenLoc);
  }

  StmtResult RebuildForStmt(SourceLocation ForLoc, SourceLocation LParenLoc,
                            Stmt *Init, Sema::ConditionResult Cond,
                            Sema::FullExprArg Inc, SourceLocation RParenLoc,
                            Stmt *Body) {
    return getSema().OnForStmt(ForLoc, LParenLoc, Init, Cond, Inc, RParenLoc,
                               Body);
  }

  StmtResult RebuildGotoStmt(SourceLocation GotoLoc, SourceLocation LabelLoc,
                             LabelDecl *Label) {
    return getSema().OnGotoStmt(GotoLoc, LabelLoc, Label);
  }

  StmtResult RebuildIndirectGotoStmt(SourceLocation GotoLoc,
                                     SourceLocation StarLoc, Expr *Target) {
    return getSema().OnIndirectGotoStmt(GotoLoc, StarLoc, Target);
  }

  StmtResult RebuildReturnStmt(SourceLocation ReturnLoc, Expr *Result) {
    return getSema().FormReturnStmt(ReturnLoc, Result);
  }

  StmtResult RebuildDeclStmt(llvm::MutableArrayRef<Decl *> Decls,
                             SourceLocation StartLoc, SourceLocation EndLoc) {
    Sema::DeclGroupPtrTy DG = getSema().FormDeclaratorGroup(Decls);
    return getSema().OnDeclStmt(DG, StartLoc, EndLoc);
  }

  StmtResult RebuildGCCAsmStmt(SourceLocation AsmLoc, bool IsSimple,
                               bool IsVolatile, unsigned NumOutputs,
                               unsigned NumInputs, IdentifierInfo **Names,
                               MultiExprArg Constraints, MultiExprArg Exprs,
                               Expr *AsmString, MultiExprArg Clobbers,
                               unsigned NumLabels, SourceLocation RParenLoc) {
    return getSema().OnGCCAsmStmt(AsmLoc, IsSimple, IsVolatile, NumOutputs,
                                  NumInputs, Names, Constraints, Exprs,
                                  AsmString, Clobbers, NumLabels, RParenLoc);
  }

  StmtResult RebuildMSAsmStmt(SourceLocation AsmLoc, SourceLocation LBraceLoc,
                              llvm::ArrayRef<Token> AsmToks,
                              llvm::StringRef AsmString, unsigned NumOutputs,
                              unsigned NumInputs,
                              llvm::ArrayRef<llvm::StringRef> Constraints,
                              llvm::ArrayRef<llvm::StringRef> Clobbers,
                              llvm::ArrayRef<Expr *> Exprs,
                              SourceLocation EndLoc) {
    return getSema().OnMSAsmStmt(AsmLoc, LBraceLoc, AsmToks, AsmString,
                                 NumOutputs, NumInputs, Constraints, Clobbers,
                                 Exprs, EndLoc);
  }

  StmtResult RebuildSEHTryStmt(SourceLocation TryLoc, Stmt *TryBlock,
                               Stmt *Handler) {
    return getSema().OnSEHTryBlock(TryLoc, TryBlock, Handler);
  }

  StmtResult RebuildSEHExceptStmt(SourceLocation Loc, Expr *FilterExpr,
                                  Stmt *Block) {
    return getSema().OnSEHExceptBlock(Loc, FilterExpr, Block);
  }

  StmtResult RebuildSEHFinallyStmt(SourceLocation Loc, Stmt *Block) {
    return SEHFinallyStmt::Create(getSema().getTreeContext(), Loc, Block);
  }

  ExprResult RebuildPredefinedExpr(SourceLocation Loc, PredefinedIdentKind IK) {
    return getSema().FormPredefinedExpr(Loc, IK);
  }

  ExprResult RebuildDeclRefExpr(ValueDecl *VD,
                                const DeclarationNameInfo &NameInfo,
                                NamedDecl *Found) {
    return getSema().FormDeclarationNameExpr(NameInfo, VD, Found);
  }

  ExprResult RebuildParenExpr(Expr *SubExpr, SourceLocation LParen,
                              SourceLocation RParen) {
    return getSema().OnParenExpr(LParen, RParen, SubExpr);
  }

  ExprResult RebuildUnaryOperator(SourceLocation OpLoc, UnaryOperatorKind Opc,
                                  Expr *SubExpr) {
    return getSema().FormUnaryOp(/*Scope=*/nullptr, OpLoc, Opc, SubExpr);
  }

  ExprResult
  RebuildOffsetOfExpr(SourceLocation OperatorLoc, TypeSourceInfo *Type,
                      llvm::ArrayRef<Sema::OffsetOfComponent> Components,
                      SourceLocation RParenLoc) {
    return getSema().FormBuiltinOffsetOf(OperatorLoc, Type, Components,
                                         RParenLoc);
  }

  ExprResult RebuildUnaryExprOrTypeTrait(TypeSourceInfo *TInfo,
                                         SourceLocation OpLoc,
                                         UnaryExprOrTypeTrait ExprKind,
                                         SourceRange R) {
    return getSema().CreateUnaryExprOrTypeTraitExpr(TInfo, OpLoc, ExprKind, R);
  }

  ExprResult RebuildUnaryExprOrTypeTrait(Expr *SubExpr, SourceLocation OpLoc,
                                         UnaryExprOrTypeTrait ExprKind,
                                         SourceRange R) {
    ExprResult Result =
        getSema().CreateUnaryExprOrTypeTraitExpr(SubExpr, OpLoc, ExprKind);
    if (Result.isInvalid())
      return ExprError();

    return Result;
  }

  ExprResult RebuildArraySubscriptExpr(Expr *LHS, SourceLocation LBracketLoc,
                                       Expr *RHS, SourceLocation RBracketLoc) {
    return getSema().OnArraySubscriptExpr(/*Scope=*/nullptr, LHS, LBracketLoc,
                                          RHS, RBracketLoc);
  }

  ExprResult RebuildMatrixSubscriptExpr(Expr *Base, Expr *RowIdx,
                                        Expr *ColumnIdx,
                                        SourceLocation RBracketLoc) {
    return getSema().CreateBuiltinMatrixSubscriptExpr(Base, RowIdx, ColumnIdx,
                                                      RBracketLoc);
  }

  ExprResult RebuildCallExpr(Expr *Callee, SourceLocation LParenLoc,
                             MultiExprArg Args, SourceLocation RParenLoc) {
    return getSema().OnCallExpr(/*Scope=*/nullptr, Callee, LParenLoc, Args,
                                RParenLoc);
  }

  ExprResult RebuildMemberExpr(Expr *Base, SourceLocation OpLoc, bool isArrow,
                               const DeclarationNameInfo &MemberNameInfo,
                               ValueDecl *Member, NamedDecl *FoundDecl) {
    ExprResult BaseResult =
        getSema().PerformMemberExprBaseConversion(Base, isArrow);
    if (!Member->getDeclName()) {
      assert(Member->getType()->isRecordType() &&
             "unnamed member not of record type?");

      Base = BaseResult.get();

      return getSema().FormFieldReferenceExpr(Base, isArrow, OpLoc,
                                              cast<FieldDecl>(Member),
                                              FoundDecl, MemberNameInfo);
    }

    Base = BaseResult.get();
    QualType BaseType = Base->getType();

    if (isArrow && !BaseType->isPointerType())
      return ExprError();

    LookupResult R(getSema(), MemberNameInfo, neverc::ResolveMember);
    R.addDecl(FoundDecl);
    R.resolveKind();

    return getSema().FormMemberReferenceExpr(Base, BaseType, OpLoc, isArrow, R);
  }

  ExprResult RebuildBinaryOperator(SourceLocation OpLoc, BinaryOperatorKind Opc,
                                   Expr *LHS, Expr *RHS) {
    return getSema().FormBinOp(/*Scope=*/nullptr, OpLoc, Opc, LHS, RHS);
  }

  ExprResult RebuildConditionalOperator(Expr *Cond, SourceLocation QuestionLoc,
                                        Expr *LHS, SourceLocation ColonLoc,
                                        Expr *RHS) {
    return getSema().OnConditionalOp(QuestionLoc, ColonLoc, Cond, LHS, RHS);
  }

  ExprResult RebuildCStyleCastExpr(SourceLocation LParenLoc,
                                   TypeSourceInfo *TInfo,
                                   SourceLocation RParenLoc, Expr *SubExpr) {
    return getSema().FormCStyleCastExpr(LParenLoc, TInfo, RParenLoc, SubExpr);
  }

  ExprResult RebuildCompoundLiteralExpr(SourceLocation LParenLoc,
                                        TypeSourceInfo *TInfo,
                                        SourceLocation RParenLoc, Expr *Init) {
    return getSema().FormCompoundLiteralExpr(LParenLoc, TInfo, RParenLoc, Init);
  }

  ExprResult RebuildExtVectorElementExpr(Expr *Base, SourceLocation OpLoc,
                                         bool IsArrow,
                                         SourceLocation AccessorLoc,
                                         IdentifierInfo &Accessor) {

    DeclarationNameInfo NameInfo(&Accessor, AccessorLoc);
    return getSema().FormMemberReferenceExpr(Base, Base->getType(), OpLoc,
                                             IsArrow, NameInfo);
  }

  ExprResult RebuildInitList(SourceLocation LBraceLoc, MultiExprArg Inits,
                             SourceLocation RBraceLoc) {
    return SemaRef.FormInitList(LBraceLoc, Inits, RBraceLoc);
  }

  ExprResult RebuildDesignatedInitExpr(Designation &Desig,
                                       MultiExprArg ArrayExprs,
                                       SourceLocation EqualOrColonLoc,
                                       bool GNUSyntax, Expr *Init) {
    ExprResult Result = SemaRef.OnDesignatedInitializer(Desig, EqualOrColonLoc,
                                                        GNUSyntax, Init);
    if (Result.isInvalid())
      return ExprError();

    return Result;
  }

  ExprResult RebuildImplicitValueInitExpr(QualType T) {
    return new (SemaRef.Context) ImplicitValueInitExpr(T);
  }

  ExprResult RebuildVAArgExpr(SourceLocation BuiltinLoc, Expr *SubExpr,
                              TypeSourceInfo *TInfo, SourceLocation RParenLoc) {
    return getSema().FormVAArgExpr(BuiltinLoc, SubExpr, TInfo, RParenLoc);
  }

  ExprResult RebuildParenListExpr(SourceLocation LParenLoc,
                                  MultiExprArg SubExprs,
                                  SourceLocation RParenLoc) {
    return getSema().OnParenListExpr(LParenLoc, RParenLoc, SubExprs);
  }

  ExprResult RebuildAddrLabelExpr(SourceLocation AmpAmpLoc,
                                  SourceLocation LabelLoc, LabelDecl *Label) {
    return getSema().OnAddrLabel(AmpAmpLoc, LabelLoc, Label);
  }

  ExprResult RebuildStmtExpr(SourceLocation LParenLoc, Stmt *SubStmt,
                             SourceLocation RParenLoc, unsigned = 0) {
    return getSema().FormStmtExpr(LParenLoc, SubStmt, RParenLoc);
  }

  ExprResult RebuildChooseExpr(SourceLocation BuiltinLoc, Expr *Cond, Expr *LHS,
                               Expr *RHS, SourceLocation RParenLoc) {
    return SemaRef.OnChooseExpr(BuiltinLoc, Cond, LHS, RHS, RParenLoc);
  }

  ExprResult RebuildGenericSelectionExpr(SourceLocation KeyLoc,
                                         SourceLocation DefaultLoc,
                                         SourceLocation RParenLoc,
                                         Expr *ControllingExpr,
                                         llvm::ArrayRef<TypeSourceInfo *> Types,
                                         llvm::ArrayRef<Expr *> Exprs) {
    return getSema().CreateGenericSelectionExpr(KeyLoc, DefaultLoc, RParenLoc,
                                                /*PredicateIsExpr=*/true,
                                                ControllingExpr, Types, Exprs);
  }

  ExprResult RebuildGenericSelectionExpr(SourceLocation KeyLoc,
                                         SourceLocation DefaultLoc,
                                         SourceLocation RParenLoc,
                                         TypeSourceInfo *ControllingType,
                                         llvm::ArrayRef<TypeSourceInfo *> Types,
                                         llvm::ArrayRef<Expr *> Exprs) {
    return getSema().CreateGenericSelectionExpr(KeyLoc, DefaultLoc, RParenLoc,
                                                /*PredicateIsExpr=*/false,
                                                ControllingType, Types, Exprs);
  }

  ExprResult RebuildSourceLocExpr(SourceLocIdentKind Kind, QualType ResultTy,
                                  SourceLocation BuiltinLoc,
                                  SourceLocation RPLoc,
                                  DeclContext *ParentContext) {
    return getSema().FormSourceLocExpr(Kind, ResultTy, BuiltinLoc, RPLoc,
                                       ParentContext);
  }

  ExprResult RebuildShuffleVectorExpr(SourceLocation BuiltinLoc,
                                      MultiExprArg SubExprs,
                                      SourceLocation RParenLoc) {
    const IdentifierInfo &Name =
        SemaRef.Context.Idents.get("__builtin_shufflevector");
    TranslationUnitDecl *TUDecl = SemaRef.Context.getTranslationUnitDecl();
    DeclContext::lookup_result Lookup = TUDecl->lookup(DeclarationName(&Name));
    assert(!Lookup.empty() && "No __builtin_shufflevector?");

    FunctionDecl *Builtin = cast<FunctionDecl>(Lookup.front());
    Expr *Callee = new (SemaRef.Context)
        DeclRefExpr(SemaRef.Context, Builtin, SemaRef.Context.BuiltinFnTy,
                    VK_PRValue, BuiltinLoc);
    QualType CalleePtrTy = SemaRef.Context.getPointerType(Builtin->getType());
    Callee = SemaRef.ImpCastExprToType(Callee, CalleePtrTy, CK_BuiltinFnToFnPtr)
                 .get();

    ExprResult TheCall = CallExpr::Create(
        SemaRef.Context, Callee, SubExprs, Builtin->getCallResultType(),
        VK_PRValue, RParenLoc, FPOptionsOverride());

    // Type-check the __builtin_shufflevector expression.
    return SemaRef.SemaBuiltinShuffleVector(cast<CallExpr>(TheCall.get()));
  }

  ExprResult RebuildConvertVectorExpr(SourceLocation BuiltinLoc, Expr *SrcExpr,
                                      TypeSourceInfo *DstTInfo,
                                      SourceLocation RParenLoc) {
    return SemaRef.SemaConvertVectorExpr(SrcExpr, DstTInfo, BuiltinLoc,
                                         RParenLoc);
  }

  ExprResult RebuildAtomicExpr(SourceLocation BuiltinLoc, MultiExprArg SubExprs,
                               AtomicExpr::AtomicOp Op,
                               SourceLocation RParenLoc) {
    // Use this for all of the locations, since we don't know the difference
    // between the call and the expr at this point.
    SourceRange Range{BuiltinLoc, RParenLoc};
    return getSema().FormAtomicExpr(Range, Range, RParenLoc, SubExprs, Op,
                                    Sema::AtomicArgumentOrder::AST);
  }

  ExprResult RebuildRecoveryExpr(SourceLocation BeginLoc, SourceLocation EndLoc,
                                 llvm::ArrayRef<Expr *> SubExprs,
                                 QualType Type) {
    return getSema().CreateRecoveryExpr(BeginLoc, EndLoc, SubExprs, Type);
  }

private:
  TypeLoc TransformTypeInObjectScope(TypeLoc TL, QualType ObjectType,
                                     NamedDecl *FirstQualifierInScope);

  TypeSourceInfo *TransformTypeInObjectScope(TypeSourceInfo *TSInfo,
                                             QualType ObjectType,
                                             NamedDecl *FirstQualifierInScope);

  TypeSourceInfo *TransformTSIInObjectScope(TypeLoc TL, QualType ObjectType,
                                            NamedDecl *FirstQualifierInScope);
};

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformStmt(Stmt *S, StmtDiscardKind SDK) {
  if (!S)
    return S;

  switch (S->getStmtClass()) {
  case Stmt::NoStmtClass:
    break;

    // Transform individual statement nodes
    // Pass SDK into statements that can produce a value
#define STMT(Node, Parent)                                                     \
  case Stmt::Node##Class:                                                      \
    return getDerived().Transform##Node(cast<Node>(S));
#define VALUESTMT(Node, Parent)                                                \
  case Stmt::Node##Class:                                                      \
    return getDerived().Transform##Node(cast<Node>(S), SDK);
#define ABSTRACT_STMT(Node)
#define EXPR(Node, Parent)
#include "neverc/Tree/StmtNodes.td.h"

    // Transform expressions by calling TransformExpr.
#define STMT(Node, Parent)
#define ABSTRACT_STMT(Stmt)
#define EXPR(Node, Parent) case Stmt::Node##Class:
#include "neverc/Tree/StmtNodes.td.h"
    {
      ExprResult E = getDerived().TransformExpr(cast<Expr>(S));

      if (SDK == SDK_StmtExprResult)
        E = getSema().OnStmtExprResult(E);
      return getSema().OnExprStmt(E, SDK == SDK_Discarded);
    }
  }

  return S;
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformExpr(Expr *E) {
  if (!E)
    return E;

  switch (E->getStmtClass()) {
  case Stmt::NoStmtClass:
    break;
#define STMT(Node, Parent)                                                     \
  case Stmt::Node##Class:                                                      \
    break;
#define ABSTRACT_STMT(Stmt)
#define EXPR(Node, Parent)                                                     \
  case Stmt::Node##Class:                                                      \
    return getDerived().Transform##Node(cast<Node>(E));
#include "neverc/Tree/StmtNodes.td.h"
  }

  return E;
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformInitializer(Expr *Init,
                                                        bool NotCopyInit) {
  // Initializers are instantiated like expressions, except that various outer
  // layers are stripped.
  if (!Init)
    return Init;

  if (auto *FE = dyn_cast<FullExpr>(Init))
    Init = FE->getSubExpr();

  if (auto *AIL = dyn_cast<ArrayInitLoopExpr>(Init)) {
    OpaqueValueExpr *OVE = AIL->getCommonExpr();
    Init = OVE->getSourceExpr();
  }

  if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Init))
    Init = ICE->getSubExprAsWritten();

  if (!NotCopyInit)
    return getDerived().TransformExpr(Init);

  if (isa<ImplicitValueInitExpr>(Init))
    return getDerived().RebuildParenListExpr(SourceLocation(), std::nullopt,
                                             SourceLocation());

  return getDerived().TransformExpr(Init);
}

template <typename Derived>
bool TreeTransform<Derived>::TransformExprs(
    Expr *const *Inputs, unsigned NumInputs, bool IsCall,
    llvm::SmallVectorImpl<Expr *> &Outputs, bool *ArgChanged) {
  for (unsigned I = 0; I != NumInputs; ++I) {
    ExprResult Result =
        IsCall
            ? getDerived().TransformInitializer(Inputs[I], /*DirectInit*/ false)
            : getDerived().TransformExpr(Inputs[I]);
    if (Result.isInvalid())
      return true;

    if (Result.get() != Inputs[I] && ArgChanged)
      *ArgChanged = true;

    Outputs.push_back(Result.get());
  }

  return false;
}

template <typename Derived>
Sema::ConditionResult TreeTransform<Derived>::TransformCondition(
    SourceLocation Loc, VarDecl *Var, Expr *Expr, Sema::ConditionKind Kind) {
  if (Var) {
    VarDecl *ConditionVar = cast_or_null<VarDecl>(
        getDerived().TransformDefinition(Var->getLocation(), Var));

    if (!ConditionVar)
      return Sema::ConditionError();

    return getSema().OnConditionVariable(ConditionVar, Loc, Kind);
  }

  if (Expr) {
    ExprResult CondExpr = getDerived().TransformExpr(Expr);

    if (CondExpr.isInvalid())
      return Sema::ConditionError();

    return getSema().OnCondition(nullptr, Loc, CondExpr.get(), Kind,
                                 /*MissingOK=*/true);
  }

  return Sema::ConditionResult();
}

template <typename Derived>
DeclarationNameInfo TreeTransform<Derived>::TransformDeclarationNameInfo(
    const DeclarationNameInfo &NameInfo) {
  DeclarationName Name = NameInfo.getName();
  if (!Name)
    return DeclarationNameInfo();

  return NameInfo;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformType(QualType T) {
  if (getDerived().AlreadyTransformed(T))
    return T;

  // Temporary workaround.  All of these transformations should
  // eventually turn into transformations on TypeLocs.
  TypeSourceInfo *DI = getSema().Context.getTrivialTypeSourceInfo(
      T, getDerived().getBaseLocation());

  TypeSourceInfo *NewDI = getDerived().TransformType(DI);

  if (!NewDI)
    return QualType();

  return NewDI->getType();
}

template <typename Derived>
TypeSourceInfo *TreeTransform<Derived>::TransformType(TypeSourceInfo *DI) {
  // Refine the base location to the type's location.
  TemporaryBase Rebase(*this, DI->getTypeLoc().getBeginLoc(),
                       getDerived().getBaseEntity());
  if (getDerived().AlreadyTransformed(DI->getType()))
    return DI;

  TypeLocBuilder TLB;

  TypeLoc TL = DI->getTypeLoc();
  TLB.reserve(TL.getFullDataSize());

  QualType Result = getDerived().TransformType(TLB, TL);
  if (Result.isNull())
    return nullptr;

  return TLB.getTypeSourceInfo(SemaRef.Context, Result);
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformType(TypeLocBuilder &TLB, TypeLoc T) {
  switch (T.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  case TypeLoc::CLASS:                                                         \
    return getDerived().Transform##CLASS##Type(TLB, T.castAs<CLASS##TypeLoc>());
#include "neverc/Tree/Type/TypeLocNodes.def"
  }

  return QualType();
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformQualifiedType(TypeLocBuilder &TLB,
                                                        QualifiedTypeLoc T) {
  QualType Result = getDerived().TransformType(TLB, T.getUnqualifiedLoc());

  if (Result.isNull())
    return QualType();

  Result = getDerived().RebuildQualifiedType(Result, T);

  if (Result.isNull())
    return QualType();

  // RebuildQualifiedType might have updated the type, but not in a way
  // that invalidates the TypeLoc. (There's no location information for
  // qualifiers.)
  TLB.TypeWasModifiedSafely(Result);

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildQualifiedType(QualType T,
                                                      QualifiedTypeLoc TL) {

  SourceLocation Loc = TL.getBeginLoc();
  Qualifiers Quals = TL.getType().getLocalQualifiers();

  if ((T.getAddressSpace() != LangAS::Default &&
       Quals.getAddressSpace() != LangAS::Default) &&
      T.getAddressSpace() != Quals.getAddressSpace()) {
    SemaRef.Diag(Loc, diag::err_address_space_mismatch_templ_inst)
        << TL.getType() << T;
    return QualType();
  }

  // [dcl.fct]p7:
  //   [When] adding cv-qualifications on top of the function type [...] the
  //   cv-qualifiers are ignored.
  if (T->isFunctionType()) {
    T = SemaRef.getTreeContext().getAddrSpaceQualType(T,
                                                      Quals.getAddressSpace());
    return T;
  }

  return SemaRef.FormQualifiedType(T, Loc, Quals);
}

template <typename Derived>
TypeLoc TreeTransform<Derived>::TransformTypeInObjectScope(
    TypeLoc TL, QualType ObjectType, NamedDecl *UnqualLookup) {
  if (getDerived().AlreadyTransformed(TL.getType()))
    return TL;

  TypeSourceInfo *TSI = TransformTSIInObjectScope(TL, ObjectType, UnqualLookup);
  if (TSI)
    return TSI->getTypeLoc();
  return TypeLoc();
}

template <typename Derived>
TypeSourceInfo *TreeTransform<Derived>::TransformTypeInObjectScope(
    TypeSourceInfo *TSInfo, QualType ObjectType, NamedDecl *UnqualLookup) {
  if (getDerived().AlreadyTransformed(TSInfo->getType()))
    return TSInfo;

  return TransformTSIInObjectScope(TSInfo->getTypeLoc(), ObjectType,
                                   UnqualLookup);
}

template <typename Derived>
TypeSourceInfo *TreeTransform<Derived>::TransformTSIInObjectScope(
    TypeLoc TL, QualType ObjectType, NamedDecl *UnqualLookup) {
  (void)ObjectType;
  (void)UnqualLookup;
  assert(!getDerived().AlreadyTransformed(TL.getType()));

  TypeLocBuilder TLB;
  QualType Result = getDerived().TransformType(TLB, TL);

  if (Result.isNull())
    return nullptr;

  return TLB.getTypeSourceInfo(SemaRef.Context, Result);
}

template <class TyLoc>
static inline QualType TransformTypeSpecType(TypeLocBuilder &TLB, TyLoc T) {
  TyLoc NewT = TLB.push<TyLoc>(T.getType());
  NewT.setNameLoc(T.getNameLoc());
  return T.getType();
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformBuiltinType(TypeLocBuilder &TLB,
                                                      BuiltinTypeLoc T) {
  BuiltinTypeLoc NewT = TLB.push<BuiltinTypeLoc>(T.getType());
  NewT.setBuiltinLoc(T.getBuiltinLoc());
  if (T.needsExtraLocalData())
    NewT.getWrittenBuiltinSpecs() = T.getWrittenBuiltinSpecs();
  return T.getType();
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformComplexType(TypeLocBuilder &TLB,
                                                      ComplexTypeLoc T) {
  return TransformTypeSpecType(TLB, T);
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformAdjustedType(TypeLocBuilder &TLB,
                                                       AdjustedTypeLoc TL) {
  // Adjustments applied during transformation are handled elsewhere.
  return getDerived().TransformType(TLB, TL.getOriginalLoc());
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformDecayedType(TypeLocBuilder &TLB,
                                                      DecayedTypeLoc TL) {
  QualType OriginalType = getDerived().TransformType(TLB, TL.getOriginalLoc());
  if (OriginalType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      OriginalType != TL.getOriginalLoc().getType())
    Result = SemaRef.Context.getDecayedType(OriginalType);
  TLB.push<DecayedTypeLoc>(Result);
  // Nothing to set for DecayedTypeLoc.
  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformPointerType(TypeLocBuilder &TLB,
                                                      PointerTypeLoc TL) {
  QualType PointeeType = getDerived().TransformType(TLB, TL.getPointeeLoc());
  if (PointeeType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() ||
      PointeeType != TL.getPointeeLoc().getType()) {
    Result = getDerived().RebuildPointerType(PointeeType, TL.getSigilLoc());
    if (Result.isNull())
      return QualType();
  }

  TLB.TypeWasModifiedSafely(Result->getPointeeType());

  PointerTypeLoc NewT = TLB.push<PointerTypeLoc>(Result);
  NewT.setSigilLoc(TL.getSigilLoc());
  return Result;
}

template <typename Derived>
QualType
TreeTransform<Derived>::TransformConstantArrayType(TypeLocBuilder &TLB,
                                                   ConstantArrayTypeLoc TL) {
  const ConstantArrayType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  // Prefer the expression from the TypeLoc;  the other may have been uniqued.
  Expr *OldSize = TL.getSizeExpr();
  if (!OldSize)
    OldSize = const_cast<Expr *>(T->getSizeExpr());
  Expr *NewSize = nullptr;
  if (OldSize) {
    EnterExpressionEvaluationContext Unevaluated(
        SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);
    NewSize = getDerived().TransformExpr(OldSize).template getAs<Expr>();
    NewSize = SemaRef.OnConstantExpression(NewSize).get();
  }

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ElementType != T->getElementType() ||
      (T->getSizeExpr() && NewSize != OldSize)) {
    Result = getDerived().RebuildConstantArrayType(
        ElementType, T->getSizeModifier(), T->getSize(), NewSize,
        T->getIndexTypeCVRQualifiers(), TL.getBracketsRange());
    if (Result.isNull())
      return QualType();
  }

  // We might have either a ConstantArrayType or a VariableArrayType now:
  // a ConstantArrayType is allowed to have an element type which is a
  // VariableArrayType if the type is dependent.  Fortunately, all array
  // types have the same location layout.
  ArrayTypeLoc NewTL = TLB.push<ArrayTypeLoc>(Result);
  NewTL.setLBracketLoc(TL.getLBracketLoc());
  NewTL.setRBracketLoc(TL.getRBracketLoc());
  NewTL.setSizeExpr(NewSize);

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformIncompleteArrayType(
    TypeLocBuilder &TLB, IncompleteArrayTypeLoc TL) {
  const IncompleteArrayType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ElementType != T->getElementType()) {
    Result = getDerived().RebuildIncompleteArrayType(
        ElementType, T->getSizeModifier(), T->getIndexTypeCVRQualifiers(),
        TL.getBracketsRange());
    if (Result.isNull())
      return QualType();
  }

  IncompleteArrayTypeLoc NewTL = TLB.push<IncompleteArrayTypeLoc>(Result);
  NewTL.setLBracketLoc(TL.getLBracketLoc());
  NewTL.setRBracketLoc(TL.getRBracketLoc());
  NewTL.setSizeExpr(nullptr);

  return Result;
}

template <typename Derived>
QualType
TreeTransform<Derived>::TransformVariableArrayType(TypeLocBuilder &TLB,
                                                   VariableArrayTypeLoc TL) {
  const VariableArrayType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  ExprResult SizeResult;
  {
    EnterExpressionEvaluationContext Context(
        SemaRef, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);
    SizeResult = getDerived().TransformExpr(T->getSizeExpr());
  }
  if (SizeResult.isInvalid())
    return QualType();
  SizeResult =
      SemaRef.OnFinishFullExpr(SizeResult.get(), /*DiscardedValue*/ false);
  if (SizeResult.isInvalid())
    return QualType();

  Expr *Size = SizeResult.get();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ElementType != T->getElementType() ||
      Size != T->getSizeExpr()) {
    Result = getDerived().RebuildVariableArrayType(
        ElementType, T->getSizeModifier(), Size, T->getIndexTypeCVRQualifiers(),
        TL.getBracketsRange());
    if (Result.isNull())
      return QualType();
  }

  // We might have constant size array now, but fortunately it has the same
  // location layout.
  ArrayTypeLoc NewTL = TLB.push<ArrayTypeLoc>(Result);
  NewTL.setLBracketLoc(TL.getLBracketLoc());
  NewTL.setRBracketLoc(TL.getRBracketLoc());
  NewTL.setSizeExpr(Size);

  return Result;
}

template <typename Derived>
QualType
TreeTransform<Derived>::TransformConstantMatrixType(TypeLocBuilder &TLB,
                                                    ConstantMatrixTypeLoc TL) {
  const ConstantMatrixType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(T->getElementType());
  if (ElementType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ElementType != T->getElementType()) {
    Result = getDerived().RebuildConstantMatrixType(
        ElementType, T->getNumRows(), T->getNumColumns());
    if (Result.isNull())
      return QualType();
  }

  ConstantMatrixTypeLoc NewTL = TLB.push<ConstantMatrixTypeLoc>(Result);
  NewTL.setAttrNameLoc(TL.getAttrNameLoc());
  NewTL.setAttrOperandParensRange(TL.getAttrOperandParensRange());
  NewTL.setAttrRowOperand(TL.getAttrRowOperand());
  NewTL.setAttrColumnOperand(TL.getAttrColumnOperand());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformVectorType(TypeLocBuilder &TLB,
                                                     VectorTypeLoc TL) {
  const VectorType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ElementType != T->getElementType()) {
    Result = getDerived().RebuildVectorType(ElementType, T->getNumElements(),
                                            T->getVectorKind());
    if (Result.isNull())
      return QualType();
  }

  VectorTypeLoc NewTL = TLB.push<VectorTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformExtVectorType(TypeLocBuilder &TLB,
                                                        ExtVectorTypeLoc TL) {
  const VectorType *T = TL.getTypePtr();
  QualType ElementType = getDerived().TransformType(TLB, TL.getElementLoc());
  if (ElementType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ElementType != T->getElementType()) {
    Result = getDerived().RebuildExtVectorType(ElementType, T->getNumElements(),
                                               SourceLocation());
    if (Result.isNull())
      return QualType();
  }

  ExtVectorTypeLoc NewTL = TLB.push<ExtVectorTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());

  return Result;
}

template <typename Derived>
ParmVarDecl *TreeTransform<Derived>::TransformFunctionTypeParam(
    ParmVarDecl *OldParm, int indexAdjustment,
    std::optional<unsigned> NumExpansions, bool ExpectParameterPack) {
  (void)NumExpansions;
  (void)ExpectParameterPack;
  TypeSourceInfo *OldDI = OldParm->getTypeSourceInfo();
  TypeSourceInfo *NewDI = getDerived().TransformType(OldDI);
  if (!NewDI)
    return nullptr;

  if (NewDI == OldDI && indexAdjustment == 0)
    return OldParm;

  ParmVarDecl *newParm = ParmVarDecl::Create(
      SemaRef.Context, OldParm->getDeclContext(), OldParm->getInnerLocStart(),
      OldParm->getLocation(), OldParm->getIdentifier(), NewDI->getType(), NewDI,
      OldParm->getStorageClass(),
      /* DefArg */ nullptr);
  newParm->setScopeInfo(OldParm->getFunctionScopeDepth(),
                        OldParm->getFunctionScopeIndex() + indexAdjustment);
  transformedLocalDecl(OldParm, {newParm});
  return newParm;
}

template <typename Derived>
bool TreeTransform<Derived>::TransformFunctionTypeParams(
    SourceLocation Loc, llvm::ArrayRef<ParmVarDecl *> Params,
    const QualType *ParamTypes,
    const FunctionProtoType::ExtParameterInfo *ParamInfos,
    llvm::SmallVectorImpl<QualType> &OutParamTypes,
    llvm::SmallVectorImpl<ParmVarDecl *> *PVars,
    Sema::ExtParameterInfoBuilder &PInfos, unsigned *LastParamTransformed) {
  int indexAdjustment = 0;

  unsigned NumParams = Params.size();
  for (unsigned i = 0; i != NumParams; ++i) {
    if (LastParamTransformed)
      *LastParamTransformed = i;
    if (ParmVarDecl *OldParm = Params[i]) {
      assert(OldParm->getFunctionScopeIndex() == i);

      ParmVarDecl *NewParm = getDerived().TransformFunctionTypeParam(
          OldParm, indexAdjustment, std::nullopt,
          /*ExpectParameterPack=*/false);

      if (!NewParm)
        return true;

      if (ParamInfos)
        PInfos.set(OutParamTypes.size(), ParamInfos[i]);
      OutParamTypes.push_back(NewParm->getType());
      if (PVars)
        PVars->push_back(NewParm);
      continue;
    }

    // Deal with the possibility that we don't have a parameter
    // declaration for this parameter.
    assert(ParamTypes);
    QualType OldType = ParamTypes[i];
    QualType NewType = getDerived().TransformType(OldType);

    if (NewType.isNull())
      return true;

    if (ParamInfos)
      PInfos.set(OutParamTypes.size(), ParamInfos[i]);
    OutParamTypes.push_back(NewType);
    if (PVars)
      PVars->push_back(nullptr);
  }

#ifndef NDEBUG
  if (PVars) {
    for (unsigned i = 0, e = PVars->size(); i != e; ++i)
      if (ParmVarDecl *parm = (*PVars)[i])
        assert(parm->getFunctionScopeIndex() == i);
  }
#endif

  return false;
}

template <typename Derived>
QualType
TreeTransform<Derived>::TransformFunctionProtoType(TypeLocBuilder &TLB,
                                                   FunctionProtoTypeLoc TL) {
  return getDerived().TransformFunctionProtoType(
      TLB, TL,
      [](FunctionProtoType::ExceptionSpecInfo &, bool &) { return false; });
}

template <typename Derived>
template <typename Fn>
QualType TreeTransform<Derived>::TransformFunctionProtoType(
    TypeLocBuilder &TLB, FunctionProtoTypeLoc TL, Fn TransformExceptionSpec) {

  // Transform the parameters and return type.
  llvm::SmallVector<QualType, 4> ParamTypes;
  llvm::SmallVector<ParmVarDecl *, 4> ParamDecls;
  Sema::ExtParameterInfoBuilder ExtParamInfos;
  const FunctionProtoType *T = TL.getTypePtr();

  QualType ResultType;

  ResultType = getDerived().TransformType(TLB, TL.getReturnLoc());
  if (ResultType.isNull())
    return QualType();

  if (getDerived().TransformFunctionTypeParams(
          TL.getBeginLoc(), TL.getParams(), TL.getTypePtr()->param_type_begin(),
          T->getExtParameterInfosOrNull(), ParamTypes, &ParamDecls,
          ExtParamInfos))
    return QualType();

  FunctionProtoType::ExtProtoInfo EPI = T->getExtProtoInfo();

  bool EPIChanged = false;
  if (TransformExceptionSpec(EPI.ExceptionSpec, EPIChanged))
    return QualType();

  // Handle extended parameter information.
  if (auto NewExtParamInfos =
          ExtParamInfos.getPointerOrNull(ParamTypes.size())) {
    if (!EPI.ExtParameterInfos ||
        llvm::ArrayRef(EPI.ExtParameterInfos, TL.getNumParams()) !=
            llvm::ArrayRef(NewExtParamInfos, ParamTypes.size())) {
      EPIChanged = true;
    }
    EPI.ExtParameterInfos = NewExtParamInfos;
  } else if (EPI.ExtParameterInfos) {
    EPIChanged = true;
    EPI.ExtParameterInfos = nullptr;
  }

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ResultType != T->getReturnType() ||
      T->getParamTypes() != llvm::ArrayRef(ParamTypes) || EPIChanged) {
    Result = getDerived().RebuildFunctionProtoType(ResultType, ParamTypes, EPI);
    if (Result.isNull())
      return QualType();
  }

  FunctionProtoTypeLoc NewTL = TLB.push<FunctionProtoTypeLoc>(Result);
  NewTL.setLocalRangeBegin(TL.getLocalRangeBegin());
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());
  NewTL.setExceptionSpecRange(TL.getExceptionSpecRange());
  NewTL.setLocalRangeEnd(TL.getLocalRangeEnd());
  for (unsigned i = 0, e = NewTL.getNumParams(); i != e; ++i)
    NewTL.setParam(i, ParamDecls[i]);

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformFunctionNoProtoType(
    TypeLocBuilder &TLB, FunctionNoProtoTypeLoc TL) {
  const FunctionNoProtoType *T = TL.getTypePtr();
  QualType ResultType = getDerived().TransformType(TLB, TL.getReturnLoc());
  if (ResultType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ResultType != T->getReturnType())
    Result = getDerived().RebuildFunctionNoProtoType(ResultType);

  FunctionNoProtoTypeLoc NewTL = TLB.push<FunctionNoProtoTypeLoc>(Result);
  NewTL.setLocalRangeBegin(TL.getLocalRangeBegin());
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());
  NewTL.setLocalRangeEnd(TL.getLocalRangeEnd());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformTypedefType(TypeLocBuilder &TLB,
                                                      TypedefTypeLoc TL) {
  const TypedefType *T = TL.getTypePtr();
  TypedefNameDecl *Typedef = cast_or_null<TypedefNameDecl>(
      getDerived().TransformDecl(TL.getNameLoc(), T->getDecl()));
  if (!Typedef)
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || Typedef != T->getDecl()) {
    Result = getDerived().RebuildTypedefType(Typedef);
    if (Result.isNull())
      return QualType();
  }

  TypedefTypeLoc NewTL = TLB.push<TypedefTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformTypeOfExprType(TypeLocBuilder &TLB,
                                                         TypeOfExprTypeLoc TL) {
  // typeof expressions are not potentially evaluated contexts
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::Unevaluated);

  ExprResult E = getDerived().TransformExpr(TL.getUnderlyingExpr());
  if (E.isInvalid())
    return QualType();

  E = SemaRef.ResolveExprEvaluationContextForTypeof(E.get());
  if (E.isInvalid())
    return QualType();

  QualType Result = TL.getType();
  TypeOfKind Kind = Result->getAs<TypeOfExprType>()->getKind();
  if (getDerived().AlwaysRebuild() || E.get() != TL.getUnderlyingExpr()) {
    Result =
        getDerived().RebuildTypeOfExprType(E.get(), TL.getTypeofLoc(), Kind);
    if (Result.isNull())
      return QualType();
  }

  TypeOfExprTypeLoc NewTL = TLB.push<TypeOfExprTypeLoc>(Result);
  NewTL.setTypeofLoc(TL.getTypeofLoc());
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformTypeOfType(TypeLocBuilder &TLB,
                                                     TypeOfTypeLoc TL) {
  TypeSourceInfo *Old_Under_TI = TL.getUnmodifiedTInfo();
  TypeSourceInfo *New_Under_TI = getDerived().TransformType(Old_Under_TI);
  if (!New_Under_TI)
    return QualType();

  QualType Result = TL.getType();
  TypeOfKind Kind = Result->getAs<TypeOfType>()->getKind();
  if (getDerived().AlwaysRebuild() || New_Under_TI != Old_Under_TI) {
    Result = getDerived().RebuildTypeOfType(New_Under_TI->getType(), Kind);
    if (Result.isNull())
      return QualType();
  }

  TypeOfTypeLoc NewTL = TLB.push<TypeOfTypeLoc>(Result);
  NewTL.setTypeofLoc(TL.getTypeofLoc());
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());
  NewTL.setUnmodifiedTInfo(New_Under_TI);

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformRecordType(TypeLocBuilder &TLB,
                                                     RecordTypeLoc TL) {
  const RecordType *T = TL.getTypePtr();
  RecordDecl *Record = cast_or_null<RecordDecl>(
      getDerived().TransformDecl(TL.getNameLoc(), T->getDecl()));
  if (!Record)
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || Record != T->getDecl()) {
    Result = getDerived().RebuildRecordType(Record);
    if (Result.isNull())
      return QualType();
  }

  RecordTypeLoc NewTL = TLB.push<RecordTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformEnumType(TypeLocBuilder &TLB,
                                                   EnumTypeLoc TL) {
  const EnumType *T = TL.getTypePtr();
  EnumDecl *Enum = cast_or_null<EnumDecl>(
      getDerived().TransformDecl(TL.getNameLoc(), T->getDecl()));
  if (!Enum)
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || Enum != T->getDecl()) {
    Result = getDerived().RebuildEnumType(Enum);
    if (Result.isNull())
      return QualType();
  }

  EnumTypeLoc NewTL = TLB.push<EnumTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformAtomicType(TypeLocBuilder &TLB,
                                                     AtomicTypeLoc TL) {
  QualType ValueType = getDerived().TransformType(TLB, TL.getValueLoc());
  if (ValueType.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || ValueType != TL.getValueLoc().getType()) {
    Result = getDerived().RebuildAtomicType(ValueType, TL.getKWLoc());
    if (Result.isNull())
      return QualType();
  }

  AtomicTypeLoc NewTL = TLB.push<AtomicTypeLoc>(Result);
  NewTL.setKWLoc(TL.getKWLoc());
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformBitIntType(TypeLocBuilder &TLB,
                                                     BitIntTypeLoc TL) {
  const BitIntType *EIT = TL.getTypePtr();
  QualType Result = TL.getType();

  if (getDerived().AlwaysRebuild()) {
    Result = getDerived().RebuildBitIntType(EIT->isUnsigned(),
                                            EIT->getNumBits(), TL.getNameLoc());
    if (Result.isNull())
      return QualType();
  }

  BitIntTypeLoc NewTL = TLB.push<BitIntTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());
  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformAutoType(TypeLocBuilder &TLB,
                                                   AutoTypeLoc TL) {
  const AutoType *T = TL.getTypePtr();
  QualType OldDeduced = T->getDeducedType();
  QualType NewDeduced;
  if (!OldDeduced.isNull()) {
    NewDeduced = getDerived().TransformType(OldDeduced);
    if (NewDeduced.isNull())
      return QualType();
  }

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || NewDeduced != OldDeduced ||
      T->isDependentType()) {
    Result = getDerived().RebuildAutoType(NewDeduced, T->getKeyword());
    if (Result.isNull())
      return QualType();
  }

  AutoTypeLoc NewTL = TLB.push<AutoTypeLoc>(Result);
  NewTL.setNameLoc(TL.getNameLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());

  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformElaboratedType(TypeLocBuilder &TLB,
                                                         ElaboratedTypeLoc TL) {
  const ElaboratedType *T = TL.getTypePtr();

  QualType NamedT = getDerived().TransformType(TLB, TL.getNamedTypeLoc());
  if (NamedT.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || NamedT != T->getNamedType()) {
    Result = getDerived().RebuildElaboratedType(TL.getElaboratedKeywordLoc(),
                                                T->getKeyword(), NamedT);
    if (Result.isNull())
      return QualType();
  }

  ElaboratedTypeLoc NewTL = TLB.push<ElaboratedTypeLoc>(Result);
  NewTL.setElaboratedKeywordLoc(TL.getElaboratedKeywordLoc());
  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformAttributedType(TypeLocBuilder &TLB,
                                                         AttributedTypeLoc TL) {
  const AttributedType *oldType = TL.getTypePtr();
  QualType modifiedType = getDerived().TransformType(TLB, TL.getModifiedLoc());
  if (modifiedType.isNull())
    return QualType();

  // oldAttr can be null if we started with a QualType rather than a TypeLoc.
  const Attr *oldAttr = TL.getAttr();
  const Attr *newAttr = oldAttr ? getDerived().TransformAttr(oldAttr) : nullptr;
  if (oldAttr && !newAttr)
    return QualType();

  QualType result = TL.getType();

  if (getDerived().AlwaysRebuild() ||
      modifiedType != oldType->getModifiedType()) {
    QualType equivalentType =
        getDerived().TransformType(oldType->getEquivalentType());
    if (equivalentType.isNull())
      return QualType();

    // Check whether we can add nullability; it is only represented as
    // type sugar, and therefore cannot be diagnosed in any other way.
    if (auto nullability = oldType->getImmediateNullability()) {
      if (!modifiedType->canHaveNullability()) {
        SemaRef.Diag((TL.getAttr() ? TL.getAttr()->getLocation()
                                   : TL.getModifiedLoc().getBeginLoc()),
                     diag::err_nullability_nonpointer)
            << DiagNullabilityKind(*nullability, false) << modifiedType;
        return QualType();
      }
    }

    result = SemaRef.Context.getAttributedType(TL.getAttrKind(), modifiedType,
                                               equivalentType);
  }

  AttributedTypeLoc newTL = TLB.push<AttributedTypeLoc>(result);
  newTL.setAttr(newAttr);
  return result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformBTFTagAttributedType(
    TypeLocBuilder &TLB, BTFTagAttributedTypeLoc TL) {
  const BTFTagAttributedType *T = TL.getTypePtr();
  QualType Inner = getDerived().TransformType(TLB, TL.getWrappedLoc());
  if (Inner.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || Inner != T->getWrappedType()) {
    Result = SemaRef.Context.getBTFTagAttributedType(T->getAttr(), Inner);
    if (Result.isNull())
      return QualType();
  }

  TLB.push<BTFTagAttributedTypeLoc>(Result);
  return Result;
}

template <typename Derived>
QualType TreeTransform<Derived>::TransformParenType(TypeLocBuilder &TLB,
                                                    ParenTypeLoc TL) {
  QualType Inner = getDerived().TransformType(TLB, TL.getInnerLoc());
  if (Inner.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || Inner != TL.getInnerLoc().getType()) {
    Result = getDerived().RebuildParenType(Inner);
    if (Result.isNull())
      return QualType();
  }

  ParenTypeLoc NewTL = TLB.push<ParenTypeLoc>(Result);
  NewTL.setLParenLoc(TL.getLParenLoc());
  NewTL.setRParenLoc(TL.getRParenLoc());
  return Result;
}

template <typename Derived>
QualType
TreeTransform<Derived>::TransformMacroQualifiedType(TypeLocBuilder &TLB,
                                                    MacroQualifiedTypeLoc TL) {
  QualType Inner = getDerived().TransformType(TLB, TL.getInnerLoc());
  if (Inner.isNull())
    return QualType();

  QualType Result = TL.getType();
  if (getDerived().AlwaysRebuild() || Inner != TL.getInnerLoc().getType()) {
    Result =
        getDerived().RebuildMacroQualifiedType(Inner, TL.getMacroIdentifier());
    if (Result.isNull())
      return QualType();
  }

  MacroQualifiedTypeLoc NewTL = TLB.push<MacroQualifiedTypeLoc>(Result);
  NewTL.setExpansionLoc(TL.getExpansionLoc());
  return Result;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformNullStmt(NullStmt *S) {
  return S;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformCompoundStmt(CompoundStmt *S) {
  return getDerived().TransformCompoundStmt(S, false);
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformCompoundStmt(CompoundStmt *S,
                                                         bool IsStmtExpr) {
  Sema::CompoundScopeRAII CompoundScope(getSema());
  Sema::FPFeaturesStateRAII FPSave(getSema());
  if (S->hasStoredFPFeatures())
    getSema().resetFPOptions(
        S->getStoredFPFeatures().applyOverrides(getSema().getLangOpts()));

  const Stmt *ExprResult = S->getStmtExprResult();
  bool SubStmtInvalid = false;
  bool SubStmtChanged = false;
  llvm::SmallVector<Stmt *, 8> Statements;
  for (auto *B : S->body()) {
    StmtResult Result = getDerived().TransformStmt(
        B, IsStmtExpr && B == ExprResult ? SDK_StmtExprResult : SDK_Discarded);

    if (Result.isInvalid()) {
      // Immediately fail if this was a DeclStmt, since it's very
      // likely that this will cause problems for future statements.
      if (isa<DeclStmt>(B))
        return StmtError();

      // Otherwise, just keep processing substatements and fail later.
      SubStmtInvalid = true;
      continue;
    }

    SubStmtChanged = SubStmtChanged || Result.get() != B;
    Statements.push_back(Result.getAs<Stmt>());
  }

  if (SubStmtInvalid)
    return StmtError();

  if (!getDerived().AlwaysRebuild() && !SubStmtChanged)
    return S;

  return getDerived().RebuildCompoundStmt(S->getLBracLoc(), Statements,
                                          S->getRBracLoc(), IsStmtExpr);
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformCaseStmt(CaseStmt *S) {
  ExprResult LHS, RHS;
  {
    EnterExpressionEvaluationContext Unevaluated(
        SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);

    // Transform the left-hand case value.
    LHS = getDerived().TransformExpr(S->getLHS());
    LHS = SemaRef.OnCaseExpr(S->getCaseLoc(), LHS);
    if (LHS.isInvalid())
      return StmtError();

    // Transform the right-hand case value (for the GNU case-range extension).
    RHS = getDerived().TransformExpr(S->getRHS());
    RHS = SemaRef.OnCaseExpr(S->getCaseLoc(), RHS);
    if (RHS.isInvalid())
      return StmtError();
  }

  // Build the case statement.
  // Case statements are always rebuilt so that they will attached to their
  // transformed switch statement.
  StmtResult Case = getDerived().RebuildCaseStmt(S->getCaseLoc(), LHS.get(),
                                                 S->getEllipsisLoc(), RHS.get(),
                                                 S->getColonLoc());
  if (Case.isInvalid())
    return StmtError();

  // Transform the statement following the case
  StmtResult SubStmt = getDerived().TransformStmt(S->getSubStmt());
  if (SubStmt.isInvalid())
    return StmtError();

  // Attach the body to the case statement
  return getDerived().RebuildCaseStmtBody(Case.get(), SubStmt.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformDefaultStmt(DefaultStmt *S) {
  // Transform the statement following the default case
  StmtResult SubStmt = getDerived().TransformStmt(S->getSubStmt());
  if (SubStmt.isInvalid())
    return StmtError();

  // Default statements are always rebuilt
  return getDerived().RebuildDefaultStmt(S->getDefaultLoc(), S->getColonLoc(),
                                         SubStmt.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformLabelStmt(LabelStmt *S,
                                                      StmtDiscardKind SDK) {
  StmtResult SubStmt = getDerived().TransformStmt(S->getSubStmt(), SDK);
  if (SubStmt.isInvalid())
    return StmtError();

  Decl *LD =
      getDerived().TransformDecl(S->getDecl()->getLocation(), S->getDecl());
  if (!LD)
    return StmtError();

  // If we're transforming "in-place" (we're not creating new local
  // declarations), assume we're replacing the old label statement
  // and clear out the reference to it.
  if (LD == S->getDecl())
    S->getDecl()->setStmt(nullptr);

  return getDerived().RebuildLabelStmt(S->getIdentLoc(), cast<LabelDecl>(LD),
                                       SourceLocation(), SubStmt.get());
}

template <typename Derived>
const Attr *TreeTransform<Derived>::TransformAttr(const Attr *R) {
  if (!R)
    return R;

  switch (R->getKind()) {
// Transform attributes by calling TransformXXXAttr.
#define ATTR(X)                                                                \
  case attr::X:                                                                \
    return getDerived().Transform##X##Attr(cast<X##Attr>(R));
#include "neverc/Foundation/AttrList.td.h"
  }
  return R;
}

template <typename Derived>
const Attr *TreeTransform<Derived>::TransformStmtAttr(const Stmt *OrigS,
                                                      const Stmt *InstS,
                                                      const Attr *R) {
  if (!R)
    return R;

  switch (R->getKind()) {
// Transform attributes by calling TransformStmtXXXAttr.
#define ATTR(X)                                                                \
  case attr::X:                                                                \
    return getDerived().TransformStmt##X##Attr(OrigS, InstS, cast<X##Attr>(R));
#include "neverc/Foundation/AttrList.td.h"
  }
  return TransformAttr(R);
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformAttributedStmt(AttributedStmt *S,
                                                StmtDiscardKind SDK) {
  StmtResult SubStmt = getDerived().TransformStmt(S->getSubStmt(), SDK);
  if (SubStmt.isInvalid())
    return StmtError();

  bool AttrsChanged = false;
  llvm::SmallVector<const Attr *, 1> Attrs;

  // Visit attributes and keep track if any are transformed.
  for (const auto *I : S->getAttrs()) {
    const Attr *R =
        getDerived().TransformStmtAttr(S->getSubStmt(), SubStmt.get(), I);
    AttrsChanged |= (I != R);
    if (R)
      Attrs.push_back(R);
  }

  if (SubStmt.get() == S->getSubStmt() && !AttrsChanged)
    return S;

  // If transforming the attributes failed for all of the attributes in the
  // statement, don't make an AttributedStmt without attributes.
  if (Attrs.empty())
    return SubStmt;

  return getDerived().RebuildAttributedStmt(S->getAttrLoc(), Attrs,
                                            SubStmt.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformIfStmt(IfStmt *S) {
  StmtResult Init = getDerived().TransformStmt(S->getInit());
  if (Init.isInvalid())
    return StmtError();

  Sema::ConditionResult Cond = getDerived().TransformCondition(
      S->getIfLoc(), S->getConditionVariable(), S->getCond(),
      Sema::ConditionKind::Boolean);
  if (Cond.isInvalid())
    return StmtError();

  StmtResult Then = getDerived().TransformStmt(S->getThen());
  if (Then.isInvalid())
    return StmtError();

  StmtResult Else = getDerived().TransformStmt(S->getElse());
  if (Else.isInvalid())
    return StmtError();

  if (!getDerived().AlwaysRebuild() && Init.get() == S->getInit() &&
      Cond.get() == std::make_pair(S->getConditionVariable(), S->getCond()) &&
      Then.get() == S->getThen() && Else.get() == S->getElse())
    return S;

  return getDerived().RebuildIfStmt(S->getIfLoc(), S->getLParenLoc(), Cond,
                                    S->getRParenLoc(), Init.get(), Then.get(),
                                    S->getElseLoc(), Else.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformSwitchStmt(SwitchStmt *S) {
  // Transform the initialization statement
  StmtResult Init = getDerived().TransformStmt(S->getInit());
  if (Init.isInvalid())
    return StmtError();

  // Transform the condition.
  Sema::ConditionResult Cond = getDerived().TransformCondition(
      S->getSwitchLoc(), S->getConditionVariable(), S->getCond(),
      Sema::ConditionKind::Switch);
  if (Cond.isInvalid())
    return StmtError();

  // Rebuild the switch statement.
  StmtResult Switch =
      getDerived().RebuildSwitchStmtStart(S->getSwitchLoc(), S->getLParenLoc(),
                                          Init.get(), Cond, S->getRParenLoc());
  if (Switch.isInvalid())
    return StmtError();

  // Transform the body of the switch statement.
  StmtResult Body = getDerived().TransformStmt(S->getBody());
  if (Body.isInvalid())
    return StmtError();

  // Complete the switch statement.
  return getDerived().RebuildSwitchStmtBody(S->getSwitchLoc(), Switch.get(),
                                            Body.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformWhileStmt(WhileStmt *S) {
  // Transform the condition
  Sema::ConditionResult Cond = getDerived().TransformCondition(
      S->getWhileLoc(), S->getConditionVariable(), S->getCond(),
      Sema::ConditionKind::Boolean);
  if (Cond.isInvalid())
    return StmtError();

  // Transform the body
  StmtResult Body = getDerived().TransformStmt(S->getBody());
  if (Body.isInvalid())
    return StmtError();

  if (!getDerived().AlwaysRebuild() &&
      Cond.get() == std::make_pair(S->getConditionVariable(), S->getCond()) &&
      Body.get() == S->getBody())
    return Owned(S);

  return getDerived().RebuildWhileStmt(S->getWhileLoc(), S->getLParenLoc(),
                                       Cond, S->getRParenLoc(), Body.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformDoStmt(DoStmt *S) {
  // Transform the body
  StmtResult Body = getDerived().TransformStmt(S->getBody());
  if (Body.isInvalid())
    return StmtError();

  // Transform the condition
  ExprResult Cond = getDerived().TransformExpr(S->getCond());
  if (Cond.isInvalid())
    return StmtError();

  if (!getDerived().AlwaysRebuild() && Cond.get() == S->getCond() &&
      Body.get() == S->getBody())
    return S;

  return getDerived().RebuildDoStmt(S->getDoLoc(), Body.get(), S->getWhileLoc(),
                                    S->getWhileLoc(), Cond.get(),
                                    S->getRParenLoc());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformForStmt(ForStmt *S) {
  // Transform the initialization statement
  StmtResult Init = getDerived().TransformStmt(S->getInit());
  if (Init.isInvalid())
    return StmtError();

  // Transform the condition
  Sema::ConditionResult Cond = getDerived().TransformCondition(
      S->getForLoc(), S->getConditionVariable(), S->getCond(),
      Sema::ConditionKind::Boolean);
  if (Cond.isInvalid())
    return StmtError();

  // Transform the increment
  ExprResult Inc = getDerived().TransformExpr(S->getInc());
  if (Inc.isInvalid())
    return StmtError();

  Sema::FullExprArg FullInc(getSema().MakeFullDiscardedValueExpr(Inc.get()));
  if (S->getInc() && !FullInc.get())
    return StmtError();

  // Transform the body
  StmtResult Body = getDerived().TransformStmt(S->getBody());
  if (Body.isInvalid())
    return StmtError();

  if (!getDerived().AlwaysRebuild() && Init.get() == S->getInit() &&
      Cond.get() == std::make_pair(S->getConditionVariable(), S->getCond()) &&
      Inc.get() == S->getInc() && Body.get() == S->getBody())
    return S;

  return getDerived().RebuildForStmt(S->getForLoc(), S->getLParenLoc(),
                                     Init.get(), Cond, FullInc,
                                     S->getRParenLoc(), Body.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformGotoStmt(GotoStmt *S) {
  Decl *LD =
      getDerived().TransformDecl(S->getLabel()->getLocation(), S->getLabel());
  if (!LD)
    return StmtError();

  // Goto statements must always be rebuilt, to resolve the label.
  return getDerived().RebuildGotoStmt(S->getGotoLoc(), S->getLabelLoc(),
                                      cast<LabelDecl>(LD));
}

template <typename Derived>
StmtResult
TreeTransform<Derived>::TransformIndirectGotoStmt(IndirectGotoStmt *S) {
  ExprResult Target = getDerived().TransformExpr(S->getTarget());
  if (Target.isInvalid())
    return StmtError();
  Target = SemaRef.MaybeCreateExprWithCleanups(Target.get());

  if (!getDerived().AlwaysRebuild() && Target.get() == S->getTarget())
    return S;

  return getDerived().RebuildIndirectGotoStmt(S->getGotoLoc(), S->getStarLoc(),
                                              Target.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformContinueStmt(ContinueStmt *S) {
  return S;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformBreakStmt(BreakStmt *S) {
  return S;
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformReturnStmt(ReturnStmt *S) {
  ExprResult Result = getDerived().TransformInitializer(S->getRetValue(),
                                                        /*NotCopyInit*/ false);
  if (Result.isInvalid())
    return StmtError();

  // We always rebuild the return statement because there is no way
  // to tell whether the return type of the function has changed.
  return getDerived().RebuildReturnStmt(S->getReturnLoc(), Result.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformDeclStmt(DeclStmt *S) {
  bool DeclChanged = false;
  llvm::SmallVector<Decl *, 4> Decls;
  for (auto *D : S->decls()) {
    Decl *Transformed = getDerived().TransformDefinition(D->getLocation(), D);
    if (!Transformed)
      return StmtError();

    if (Transformed != D)
      DeclChanged = true;

    Decls.push_back(Transformed);
  }

  if (!getDerived().AlwaysRebuild() && !DeclChanged)
    return S;

  return getDerived().RebuildDeclStmt(Decls, S->getBeginLoc(), S->getEndLoc());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformGCCAsmStmt(GCCAsmStmt *S) {

  llvm::SmallVector<Expr *, 8> Constraints;
  llvm::SmallVector<Expr *, 8> Exprs;
  llvm::SmallVector<IdentifierInfo *, 4> Names;

  ExprResult AsmString;
  llvm::SmallVector<Expr *, 8> Clobbers;

  bool ExprsChanged = false;

  // Go through the outputs.
  for (unsigned I = 0, E = S->getNumOutputs(); I != E; ++I) {
    Names.push_back(S->getOutputIdentifier(I));

    // No need to transform the constraint literal.
    Constraints.push_back(S->getOutputConstraintLiteral(I));

    // Transform the output expr.
    Expr *OutputExpr = S->getOutputExpr(I);
    ExprResult Result = getDerived().TransformExpr(OutputExpr);
    if (Result.isInvalid())
      return StmtError();

    ExprsChanged |= Result.get() != OutputExpr;

    Exprs.push_back(Result.get());
  }

  // Go through the inputs.
  for (unsigned I = 0, E = S->getNumInputs(); I != E; ++I) {
    Names.push_back(S->getInputIdentifier(I));

    // No need to transform the constraint literal.
    Constraints.push_back(S->getInputConstraintLiteral(I));

    // Transform the input expr.
    Expr *InputExpr = S->getInputExpr(I);
    ExprResult Result = getDerived().TransformExpr(InputExpr);
    if (Result.isInvalid())
      return StmtError();

    ExprsChanged |= Result.get() != InputExpr;

    Exprs.push_back(Result.get());
  }

  // Go through the Labels.
  for (unsigned I = 0, E = S->getNumLabels(); I != E; ++I) {
    Names.push_back(S->getLabelIdentifier(I));

    ExprResult Result = getDerived().TransformExpr(S->getLabelExpr(I));
    if (Result.isInvalid())
      return StmtError();
    ExprsChanged |= Result.get() != S->getLabelExpr(I);
    Exprs.push_back(Result.get());
  }
  if (!getDerived().AlwaysRebuild() && !ExprsChanged)
    return S;

  // Go through the clobbers.
  for (unsigned I = 0, E = S->getNumClobbers(); I != E; ++I)
    Clobbers.push_back(S->getClobberStringLiteral(I));

  // No need to transform the asm string literal.
  AsmString = S->getAsmString();
  return getDerived().RebuildGCCAsmStmt(
      S->getAsmLoc(), S->isSimple(), S->isVolatile(), S->getNumOutputs(),
      S->getNumInputs(), Names.data(), Constraints, Exprs, AsmString.get(),
      Clobbers, S->getNumLabels(), S->getRParenLoc());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformMSAsmStmt(MSAsmStmt *S) {
  llvm::ArrayRef<Token> AsmToks =
      llvm::ArrayRef(S->getAsmToks(), S->getNumAsmToks());

  bool HadError = false, HadChange = false;

  llvm::ArrayRef<Expr *> SrcExprs = S->getAllExprs();
  llvm::SmallVector<Expr *, 8> TransformedExprs;
  TransformedExprs.reserve(SrcExprs.size());
  for (unsigned i = 0, e = SrcExprs.size(); i != e; ++i) {
    ExprResult Result = getDerived().TransformExpr(SrcExprs[i]);
    if (!Result.isUsable()) {
      HadError = true;
    } else {
      HadChange |= (Result.get() != SrcExprs[i]);
      TransformedExprs.push_back(Result.get());
    }
  }

  if (HadError)
    return StmtError();
  if (!HadChange && !getDerived().AlwaysRebuild())
    return Owned(S);

  return getDerived().RebuildMSAsmStmt(
      S->getAsmLoc(), S->getLBraceLoc(), AsmToks, S->getAsmString(),
      S->getNumOutputs(), S->getNumInputs(), S->getAllConstraints(),
      S->getClobbers(), TransformedExprs, S->getEndLoc());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformSEHTryStmt(SEHTryStmt *S) {
  StmtResult TryBlock = getDerived().TransformCompoundStmt(S->getTryBlock());
  if (TryBlock.isInvalid())
    return StmtError();

  StmtResult Handler = getDerived().TransformSEHHandler(S->getHandler());
  if (Handler.isInvalid())
    return StmtError();

  if (!getDerived().AlwaysRebuild() && TryBlock.get() == S->getTryBlock() &&
      Handler.get() == S->getHandler())
    return S;

  return getDerived().RebuildSEHTryStmt(S->getTryLoc(), TryBlock.get(),
                                        Handler.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformSEHFinallyStmt(SEHFinallyStmt *S) {
  StmtResult Block = getDerived().TransformCompoundStmt(S->getBlock());
  if (Block.isInvalid())
    return StmtError();

  return getDerived().RebuildSEHFinallyStmt(S->getFinallyLoc(), Block.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformSEHExceptStmt(SEHExceptStmt *S) {
  ExprResult FilterExpr = getDerived().TransformExpr(S->getFilterExpr());
  if (FilterExpr.isInvalid())
    return StmtError();

  StmtResult Block = getDerived().TransformCompoundStmt(S->getBlock());
  if (Block.isInvalid())
    return StmtError();

  return getDerived().RebuildSEHExceptStmt(S->getExceptLoc(), FilterExpr.get(),
                                           Block.get());
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformSEHHandler(Stmt *Handler) {
  if (isa<SEHFinallyStmt>(Handler))
    return getDerived().TransformSEHFinallyStmt(cast<SEHFinallyStmt>(Handler));
  else
    return getDerived().TransformSEHExceptStmt(cast<SEHExceptStmt>(Handler));
}

template <typename Derived>
StmtResult TreeTransform<Derived>::TransformSEHLeaveStmt(SEHLeaveStmt *S) {
  return S;
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformConstantExpr(ConstantExpr *E) {
  return TransformExpr(E->getSubExpr());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformPredefinedExpr(PredefinedExpr *E) {
  return E;

  return getDerived().RebuildPredefinedExpr(E->getLocation(),
                                            E->getIdentKind());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformDeclRefExpr(DeclRefExpr *E) {
  ValueDecl *ND = cast_or_null<ValueDecl>(
      getDerived().TransformDecl(E->getLocation(), E->getDecl()));
  if (!ND)
    return ExprError();

  NamedDecl *Found = ND;
  if (E->getFoundDecl() != E->getDecl()) {
    Found = cast_or_null<NamedDecl>(
        getDerived().TransformDecl(E->getLocation(), E->getFoundDecl()));
    if (!Found)
      return ExprError();
  }

  DeclarationNameInfo NameInfo = E->getNameInfo();
  if (NameInfo.getName()) {
    NameInfo = getDerived().TransformDeclarationNameInfo(NameInfo);
    if (!NameInfo.getName())
      return ExprError();
  }

  if (!getDerived().AlwaysRebuild() && ND == E->getDecl() &&
      Found == E->getFoundDecl() &&
      NameInfo.getName() == E->getDecl()->getDeclName()) {
    SemaRef.MarkDeclRefReferenced(E);
    return E;
  }

  return getDerived().RebuildDeclRefExpr(ND, NameInfo, Found);
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformIntegerLiteral(IntegerLiteral *E) {
  return E;
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformFixedPointLiteral(FixedPointLiteral *E) {
  return E;
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformFloatingLiteral(FloatingLiteral *E) {
  return E;
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformImaginaryLiteral(ImaginaryLiteral *E) {
  return E;
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformStringLiteral(StringLiteral *E) {
  return E;
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformCharacterLiteral(CharacterLiteral *E) {
  return E;
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformGenericSelectionExpr(GenericSelectionExpr *E) {
  ExprResult ControllingExpr;
  TypeSourceInfo *ControllingType = nullptr;
  if (E->isExprPredicate())
    ControllingExpr = getDerived().TransformExpr(E->getControllingExpr());
  else
    ControllingType = getDerived().TransformType(E->getControllingType());

  if (ControllingExpr.isInvalid() && !ControllingType)
    return ExprError();

  llvm::SmallVector<Expr *, 4> AssocExprs;
  llvm::SmallVector<TypeSourceInfo *, 4> AssocTypes;
  for (const GenericSelectionExpr::Association Assoc : E->associations()) {
    TypeSourceInfo *TSI = Assoc.getTypeSourceInfo();
    if (TSI) {
      TypeSourceInfo *AssocType = getDerived().TransformType(TSI);
      if (!AssocType)
        return ExprError();
      AssocTypes.push_back(AssocType);
    } else {
      AssocTypes.push_back(nullptr);
    }

    ExprResult AssocExpr =
        getDerived().TransformExpr(Assoc.getAssociationExpr());
    if (AssocExpr.isInvalid())
      return ExprError();
    AssocExprs.push_back(AssocExpr.get());
  }

  if (!ControllingType)
    return getDerived().RebuildGenericSelectionExpr(
        E->getGenericLoc(), E->getDefaultLoc(), E->getRParenLoc(),
        ControllingExpr.get(), AssocTypes, AssocExprs);
  return getDerived().RebuildGenericSelectionExpr(
      E->getGenericLoc(), E->getDefaultLoc(), E->getRParenLoc(),
      ControllingType, AssocTypes, AssocExprs);
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformParenExpr(ParenExpr *E) {
  ExprResult SubExpr = getDerived().TransformExpr(E->getSubExpr());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && SubExpr.get() == E->getSubExpr())
    return E;

  return getDerived().RebuildParenExpr(SubExpr.get(), E->getLParen(),
                                       E->getRParen());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformAddressOfOperand(Expr *E) {
  return getDerived().TransformExpr(E);
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformUnaryOperator(UnaryOperator *E) {
  ExprResult SubExpr;
  if (E->getOpcode() == UO_AddrOf)
    SubExpr = TransformAddressOfOperand(E->getSubExpr());
  else
    SubExpr = TransformExpr(E->getSubExpr());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && SubExpr.get() == E->getSubExpr())
    return E;

  return getDerived().RebuildUnaryOperator(E->getOperatorLoc(), E->getOpcode(),
                                           SubExpr.get());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformOffsetOfExpr(OffsetOfExpr *E) {
  // Transform the type.
  TypeSourceInfo *Type = getDerived().TransformType(E->getTypeSourceInfo());
  if (!Type)
    return ExprError();

  // Transform all of the components into components similar to what the
  // parser uses.
  // However, __builtin_offsetof is rare enough that we don't care.
  bool ExprChanged = false;
  typedef Sema::OffsetOfComponent Component;
  llvm::SmallVector<Component, 4> Components;
  for (unsigned I = 0, N = E->getNumComponents(); I != N; ++I) {
    const OffsetOfNode &ON = E->getComponent(I);
    Component Comp;
    Comp.isBrackets = true;
    Comp.LocStart = ON.getSourceRange().getBegin();
    Comp.LocEnd = ON.getSourceRange().getEnd();
    switch (ON.getKind()) {
    case OffsetOfNode::Array: {
      Expr *FromIndex = E->getIndexExpr(ON.getArrayExprIndex());
      ExprResult Index = getDerived().TransformExpr(FromIndex);
      if (Index.isInvalid())
        return ExprError();

      ExprChanged = ExprChanged || Index.get() != FromIndex;
      Comp.isBrackets = true;
      Comp.U.E = Index.get();
      break;
    }

    case OffsetOfNode::Field:
    case OffsetOfNode::Identifier:
      Comp.isBrackets = false;
      Comp.U.IdentInfo = ON.getFieldName();
      if (!Comp.U.IdentInfo)
        continue;

      break;
    }

    Components.push_back(Comp);
  }

  // If nothing changed, retain the existing expression.
  if (!getDerived().AlwaysRebuild() && Type == E->getTypeSourceInfo() &&
      !ExprChanged)
    return E;

  return getDerived().RebuildOffsetOfExpr(E->getOperatorLoc(), Type, Components,
                                          E->getRParenLoc());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformOpaqueValueExpr(OpaqueValueExpr *E) {
  assert(
      (!E->getSourceExpr() || getDerived().AlreadyTransformed(E->getType())) &&
      "opaque value expression requires transformation");
  return E;
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformTypoExpr(TypoExpr *E) {
  return E;
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformRecoveryExpr(RecoveryExpr *E) {
  llvm::SmallVector<Expr *, 8> Children;
  bool Changed = false;
  for (Expr *C : E->subExpressions()) {
    ExprResult NewC = getDerived().TransformExpr(C);
    if (NewC.isInvalid())
      return ExprError();
    Children.push_back(NewC.get());

    Changed |= NewC.get() != C;
  }
  if (!getDerived().AlwaysRebuild() && !Changed)
    return E;
  return getDerived().RebuildRecoveryExpr(E->getBeginLoc(), E->getEndLoc(),
                                          Children, E->getType());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformPseudoObjectExpr(PseudoObjectExpr *) {
  return ExprError();
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformUnaryExprOrTypeTraitExpr(
    UnaryExprOrTypeTraitExpr *E) {
  if (E->isArgumentType()) {
    TypeSourceInfo *OldT = E->getArgumentTypeInfo();

    TypeSourceInfo *NewT = getDerived().TransformType(OldT);
    if (!NewT)
      return ExprError();

    if (!getDerived().AlwaysRebuild() && OldT == NewT)
      return E;

    return getDerived().RebuildUnaryExprOrTypeTrait(
        NewT, E->getOperatorLoc(), E->getKind(), E->getSourceRange());
  }

  // [expr.sizeof]p1:
  //   The operand is either an expression, which is an unevaluated operand
  //   [...]
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::Unevaluated);

  ExprResult SubExpr = getDerived().TransformExpr(E->getArgumentExpr());

  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && SubExpr.get() == E->getArgumentExpr())
    return E;

  return getDerived().RebuildUnaryExprOrTypeTrait(
      SubExpr.get(), E->getOperatorLoc(), E->getKind(), E->getSourceRange());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformArraySubscriptExpr(ArraySubscriptExpr *E) {
  ExprResult LHS = getDerived().TransformExpr(E->getLHS());
  if (LHS.isInvalid())
    return ExprError();

  ExprResult RHS = getDerived().TransformExpr(E->getRHS());
  if (RHS.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && LHS.get() == E->getLHS() &&
      RHS.get() == E->getRHS())
    return E;

  return getDerived().RebuildArraySubscriptExpr(
      LHS.get(), E->getLHS()->getBeginLoc(), RHS.get(), E->getRBracketLoc());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformMatrixSubscriptExpr(MatrixSubscriptExpr *E) {
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  ExprResult RowIdx = getDerived().TransformExpr(E->getRowIdx());
  if (RowIdx.isInvalid())
    return ExprError();

  ExprResult ColumnIdx = getDerived().TransformExpr(E->getColumnIdx());
  if (ColumnIdx.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && Base.get() == E->getBase() &&
      RowIdx.get() == E->getRowIdx() && ColumnIdx.get() == E->getColumnIdx())
    return E;

  return getDerived().RebuildMatrixSubscriptExpr(
      Base.get(), RowIdx.get(), ColumnIdx.get(), E->getRBracketLoc());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformCallExpr(CallExpr *E) {
  // Transform the callee.
  ExprResult Callee = getDerived().TransformExpr(E->getCallee());
  if (Callee.isInvalid())
    return ExprError();

  // Transform arguments.
  bool ArgChanged = false;
  llvm::SmallVector<Expr *, 8> Args;
  if (getDerived().TransformExprs(E->getArgs(), E->getNumArgs(), true, Args,
                                  &ArgChanged))
    return ExprError();

  if (!getDerived().AlwaysRebuild() && Callee.get() == E->getCallee() &&
      !ArgChanged)
    return SemaRef.MaybeBindToTemporary(E);

  SourceLocation FakeLParenLoc =
      ((Expr *)Callee.get())->getSourceRange().getBegin();

  Sema::FPFeaturesStateRAII FPFeaturesState(getSema());
  if (E->hasStoredFPFeatures()) {
    FPOptionsOverride NewOverrides = E->getFPFeatures();
    getSema().CurFPFeatures =
        NewOverrides.applyOverrides(getSema().getLangOpts());
    getSema().FpPragmaStack.CurrentValue = NewOverrides;
  }

  return getDerived().RebuildCallExpr(Callee.get(), FakeLParenLoc, Args,
                                      E->getRParenLoc());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformMemberExpr(MemberExpr *E) {
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  ValueDecl *Member = cast_or_null<ValueDecl>(
      getDerived().TransformDecl(E->getMemberLoc(), E->getMemberDecl()));
  if (!Member)
    return ExprError();

  NamedDecl *FoundDecl = E->getFoundDecl();
  if (FoundDecl == E->getMemberDecl()) {
    FoundDecl = Member;
  } else {
    FoundDecl = cast_or_null<NamedDecl>(
        getDerived().TransformDecl(E->getMemberLoc(), FoundDecl));
    if (!FoundDecl)
      return ExprError();
  }

  if (!getDerived().AlwaysRebuild() && Base.get() == E->getBase() &&
      Member == E->getMemberDecl() && FoundDecl == E->getFoundDecl()) {
    SemaRef.MarkMemberReferenced(E);
    return E;
  }

  SourceLocation FakeOperatorLoc =
      SemaRef.getLocForEndOfToken(E->getBase()->getSourceRange().getEnd());

  DeclarationNameInfo MemberNameInfo = E->getMemberNameInfo();
  if (MemberNameInfo.getName()) {
    MemberNameInfo = getDerived().TransformDeclarationNameInfo(MemberNameInfo);
    if (!MemberNameInfo.getName())
      return ExprError();
  }

  return getDerived().RebuildMemberExpr(Base.get(), FakeOperatorLoc,
                                        E->isArrow(), MemberNameInfo, Member,
                                        FoundDecl);
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformBinaryOperator(BinaryOperator *E) {
  ExprResult LHS = getDerived().TransformExpr(E->getLHS());
  if (LHS.isInvalid())
    return ExprError();

  ExprResult RHS =
      getDerived().TransformInitializer(E->getRHS(), /*NotCopyInit=*/false);
  if (RHS.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && LHS.get() == E->getLHS() &&
      RHS.get() == E->getRHS())
    return E;

  if (E->isCompoundAssignmentOp())
    // FPFeatures has already been established from trailing storage
    return getDerived().RebuildBinaryOperator(
        E->getOperatorLoc(), E->getOpcode(), LHS.get(), RHS.get());
  Sema::FPFeaturesStateRAII FPFeaturesState(getSema());
  FPOptionsOverride NewOverrides(E->getFPFeatures());
  getSema().CurFPFeatures =
      NewOverrides.applyOverrides(getSema().getLangOpts());
  getSema().FpPragmaStack.CurrentValue = NewOverrides;
  return getDerived().RebuildBinaryOperator(E->getOperatorLoc(), E->getOpcode(),
                                            LHS.get(), RHS.get());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformCompoundAssignOperator(
    CompoundAssignOperator *E) {
  Sema::FPFeaturesStateRAII FPFeaturesState(getSema());
  FPOptionsOverride NewOverrides(E->getFPFeatures());
  getSema().CurFPFeatures =
      NewOverrides.applyOverrides(getSema().getLangOpts());
  getSema().FpPragmaStack.CurrentValue = NewOverrides;
  return getDerived().TransformBinaryOperator(E);
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformBinaryConditionalOperator(
    BinaryConditionalOperator *e) {
  // Just rebuild the common and RHS expressions and see whether we
  // get any changes.

  ExprResult commonExpr = getDerived().TransformExpr(e->getCommon());
  if (commonExpr.isInvalid())
    return ExprError();

  ExprResult rhs = getDerived().TransformExpr(e->getFalseExpr());
  if (rhs.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && commonExpr.get() == e->getCommon() &&
      rhs.get() == e->getFalseExpr())
    return e;

  return getDerived().RebuildConditionalOperator(commonExpr.get(),
                                                 e->getQuestionLoc(), nullptr,
                                                 e->getColonLoc(), rhs.get());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformConditionalOperator(ConditionalOperator *E) {
  ExprResult Cond = getDerived().TransformExpr(E->getCond());
  if (Cond.isInvalid())
    return ExprError();

  ExprResult LHS = getDerived().TransformExpr(E->getLHS());
  if (LHS.isInvalid())
    return ExprError();

  ExprResult RHS = getDerived().TransformExpr(E->getRHS());
  if (RHS.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && Cond.get() == E->getCond() &&
      LHS.get() == E->getLHS() && RHS.get() == E->getRHS())
    return E;

  return getDerived().RebuildConditionalOperator(
      Cond.get(), E->getQuestionLoc(), LHS.get(), E->getColonLoc(), RHS.get());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformImplicitCastExpr(ImplicitCastExpr *E) {
  // Implicit casts are eliminated during transformation, since they
  // will be recomputed by semantic analysis after transformation.
  return getDerived().TransformExpr(E->getSubExprAsWritten());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformCStyleCastExpr(CStyleCastExpr *E) {
  TypeSourceInfo *Type = getDerived().TransformType(E->getTypeInfoAsWritten());
  if (!Type)
    return ExprError();

  ExprResult SubExpr = getDerived().TransformExpr(E->getSubExprAsWritten());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && Type == E->getTypeInfoAsWritten() &&
      SubExpr.get() == E->getSubExpr())
    return E;

  return getDerived().RebuildCStyleCastExpr(E->getLParenLoc(), Type,
                                            E->getRParenLoc(), SubExpr.get());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformCompoundLiteralExpr(CompoundLiteralExpr *E) {
  TypeSourceInfo *OldT = E->getTypeSourceInfo();
  TypeSourceInfo *NewT = getDerived().TransformType(OldT);
  if (!NewT)
    return ExprError();

  ExprResult Init = getDerived().TransformExpr(E->getInitializer());
  if (Init.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && OldT == NewT &&
      Init.get() == E->getInitializer())
    return SemaRef.MaybeBindToTemporary(E);

  // Note: the expression type doesn't necessarily match the
  // type-as-written, but that's okay, because it should always be
  // derivable from the initializer.

  return getDerived().RebuildCompoundLiteralExpr(
      E->getLParenLoc(), NewT, E->getInitializer()->getEndLoc(), Init.get());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformExtVectorElementExpr(ExtVectorElementExpr *E) {
  ExprResult Base = getDerived().TransformExpr(E->getBase());
  if (Base.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && Base.get() == E->getBase())
    return E;

  SourceLocation FakeOperatorLoc =
      SemaRef.getLocForEndOfToken(E->getBase()->getEndLoc());
  return getDerived().RebuildExtVectorElementExpr(
      Base.get(), FakeOperatorLoc, E->isArrow(), E->getAccessorLoc(),
      E->getAccessor());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformInitListExpr(InitListExpr *E) {
  if (InitListExpr *Syntactic = E->getSyntacticForm())
    E = Syntactic;

  bool InitChanged = false;

  llvm::SmallVector<Expr *, 4> Inits;
  if (getDerived().TransformExprs(E->getInits(), E->getNumInits(), false, Inits,
                                  &InitChanged))
    return ExprError();

  if (!getDerived().AlwaysRebuild() && !InitChanged) {
    // Attempt to reuse the existing syntactic form of the InitListExpr
    // in some cases. We can't reuse it in general, because the syntactic and
    // semantic forms are linked, and we can't know that semantic form will
    // match even if the syntactic form does.
  }

  return getDerived().RebuildInitList(E->getLBraceLoc(), Inits,
                                      E->getRBraceLoc());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformDesignatedInitExpr(DesignatedInitExpr *E) {
  Designation Desig;

  // transform the initializer value
  ExprResult Init = getDerived().TransformExpr(E->getInit());
  if (Init.isInvalid())
    return ExprError();

  // transform the designators.
  llvm::SmallVector<Expr *, 4> ArrayExprs;
  bool ExprChanged = false;
  for (const DesignatedInitExpr::Designator &D : E->designators()) {
    if (D.isFieldDesignator()) {
      if (D.getFieldDecl()) {
        FieldDecl *Field = cast_or_null<FieldDecl>(
            getDerived().TransformDecl(D.getFieldLoc(), D.getFieldDecl()));
        if (Field != D.getFieldDecl())
          // Rebuild the expression when the transformed FieldDecl is
          // different to the already assigned FieldDecl.
          ExprChanged = true;
        if (Field->isAnonymousStructOrUnion())
          continue;
      } else {
        // Ensure that the designator expression is rebuilt when there isn't
        // a resolved FieldDecl in the designator as we don't want to assign
        // a FieldDecl to a pattern designator that will be instantiated again.
        ExprChanged = true;
      }
      Desig.AddDesignator(Designator::CreateFieldDesignator(
          D.getFieldName(), D.getDotLoc(), D.getFieldLoc()));
      continue;
    }

    if (D.isArrayDesignator()) {
      ExprResult Index = getDerived().TransformExpr(E->getArrayIndex(D));
      if (Index.isInvalid())
        return ExprError();

      Desig.AddDesignator(
          Designator::CreateArrayDesignator(Index.get(), D.getLBracketLoc()));

      ExprChanged = ExprChanged || Init.get() != E->getArrayIndex(D);
      ArrayExprs.push_back(Index.get());
      continue;
    }

    assert(D.isArrayRangeDesignator() && "New kind of designator?");
    ExprResult Start = getDerived().TransformExpr(E->getArrayRangeStart(D));
    if (Start.isInvalid())
      return ExprError();

    ExprResult End = getDerived().TransformExpr(E->getArrayRangeEnd(D));
    if (End.isInvalid())
      return ExprError();

    Desig.AddDesignator(Designator::CreateArrayRangeDesignator(
        Start.get(), End.get(), D.getLBracketLoc(), D.getEllipsisLoc()));

    ExprChanged = ExprChanged || Start.get() != E->getArrayRangeStart(D) ||
                  End.get() != E->getArrayRangeEnd(D);

    ArrayExprs.push_back(Start.get());
    ArrayExprs.push_back(End.get());
  }

  if (!getDerived().AlwaysRebuild() && Init.get() == E->getInit() &&
      !ExprChanged)
    return E;

  return getDerived().RebuildDesignatedInitExpr(Desig, ArrayExprs,
                                                E->getEqualOrColonLoc(),
                                                E->usesGNUSyntax(), Init.get());
}

// Seems that if TransformInitListExpr() only works on the syntactic form of an
// InitListExpr, then a DesignatedInitUpdateExpr is not encountered.
template <typename Derived>
ExprResult TreeTransform<Derived>::TransformDesignatedInitUpdateExpr(
    DesignatedInitUpdateExpr *E) {
  return ExprError();
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformNoInitExpr(NoInitExpr *E) {
  return ExprError();
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformArrayInitLoopExpr(ArrayInitLoopExpr *E) {
  return ExprError();
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformArrayInitIndexExpr(ArrayInitIndexExpr *E) {
  return ExprError();
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformImplicitValueInitExpr(
    ImplicitValueInitExpr *E) {
  TemporaryBase Rebase(*this, E->getBeginLoc(), DeclarationName());

  QualType T = getDerived().TransformType(E->getType());
  if (T.isNull())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && T == E->getType())
    return E;

  return getDerived().RebuildImplicitValueInitExpr(T);
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformVAArgExpr(VAArgExpr *E) {
  TypeSourceInfo *TInfo = getDerived().TransformType(E->getWrittenTypeInfo());
  if (!TInfo)
    return ExprError();

  ExprResult SubExpr = getDerived().TransformExpr(E->getSubExpr());
  if (SubExpr.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && TInfo == E->getWrittenTypeInfo() &&
      SubExpr.get() == E->getSubExpr())
    return E;

  return getDerived().RebuildVAArgExpr(E->getBuiltinLoc(), SubExpr.get(), TInfo,
                                       E->getRParenLoc());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformParenListExpr(ParenListExpr *E) {
  bool ArgumentChanged = false;
  llvm::SmallVector<Expr *, 4> Inits;
  if (TransformExprs(E->getExprs(), E->getNumExprs(), true, Inits,
                     &ArgumentChanged))
    return ExprError();

  return getDerived().RebuildParenListExpr(E->getLParenLoc(), Inits,
                                           E->getRParenLoc());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformAddrLabelExpr(AddrLabelExpr *E) {
  Decl *LD =
      getDerived().TransformDecl(E->getLabel()->getLocation(), E->getLabel());
  if (!LD)
    return ExprError();

  return getDerived().RebuildAddrLabelExpr(E->getAmpAmpLoc(), E->getLabelLoc(),
                                           cast<LabelDecl>(LD));
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformStmtExpr(StmtExpr *E) {
  SemaRef.OnStartStmtExpr();
  StmtResult SubStmt =
      getDerived().TransformCompoundStmt(E->getSubStmt(), true);
  if (SubStmt.isInvalid()) {
    SemaRef.OnStmtExprError();
    return ExprError();
  }

  if (!getDerived().AlwaysRebuild() && SubStmt.get() == E->getSubStmt()) {
    SemaRef.OnStmtExprError();
    return SemaRef.MaybeBindToTemporary(E);
  }

  return getDerived().RebuildStmtExpr(E->getLParenLoc(), SubStmt.get(),
                                      E->getRParenLoc(), 0);
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformChooseExpr(ChooseExpr *E) {
  ExprResult Cond = getDerived().TransformExpr(E->getCond());
  if (Cond.isInvalid())
    return ExprError();

  ExprResult LHS = getDerived().TransformExpr(E->getLHS());
  if (LHS.isInvalid())
    return ExprError();

  ExprResult RHS = getDerived().TransformExpr(E->getRHS());
  if (RHS.isInvalid())
    return ExprError();

  if (!getDerived().AlwaysRebuild() && Cond.get() == E->getCond() &&
      LHS.get() == E->getLHS() && RHS.get() == E->getRHS())
    return E;

  return getDerived().RebuildChooseExpr(
      E->getBuiltinLoc(), Cond.get(), LHS.get(), RHS.get(), E->getRParenLoc());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformSourceLocExpr(SourceLocExpr *E) {
  bool NeedRebuildFunc = E->getIdentKind() == SourceLocIdentKind::Function &&
                         getSema().CurContext != E->getParentContext();

  if (!getDerived().AlwaysRebuild() && !NeedRebuildFunc)
    return E;

  return getDerived().RebuildSourceLocExpr(E->getIdentKind(), E->getType(),
                                           E->getBeginLoc(), E->getEndLoc(),
                                           getSema().CurContext);
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformNullPtrLiteralExpr(NullPtrLiteralExpr *E) {
  return E;
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformExprWithCleanups(ExprWithCleanups *E) {
  return getDerived().TransformExpr(E->getSubExpr());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformShuffleVectorExpr(ShuffleVectorExpr *E) {
  bool ArgumentChanged = false;
  llvm::SmallVector<Expr *, 8> SubExprs;
  SubExprs.reserve(E->getNumSubExprs());
  if (getDerived().TransformExprs(E->getSubExprs(), E->getNumSubExprs(), false,
                                  SubExprs, &ArgumentChanged))
    return ExprError();

  if (!getDerived().AlwaysRebuild() && !ArgumentChanged)
    return E;

  return getDerived().RebuildShuffleVectorExpr(E->getBuiltinLoc(), SubExprs,
                                               E->getRParenLoc());
}

template <typename Derived>
ExprResult
TreeTransform<Derived>::TransformConvertVectorExpr(ConvertVectorExpr *E) {
  ExprResult SrcExpr = getDerived().TransformExpr(E->getSrcExpr());
  if (SrcExpr.isInvalid())
    return ExprError();

  TypeSourceInfo *Type = getDerived().TransformType(E->getTypeSourceInfo());
  if (!Type)
    return ExprError();

  if (!getDerived().AlwaysRebuild() && Type == E->getTypeSourceInfo() &&
      SrcExpr.get() == E->getSrcExpr())
    return E;

  return getDerived().RebuildConvertVectorExpr(
      E->getBuiltinLoc(), SrcExpr.get(), Type, E->getRParenLoc());
}

template <typename Derived>
ExprResult TreeTransform<Derived>::TransformAtomicExpr(AtomicExpr *E) {
  bool ArgumentChanged = false;
  llvm::SmallVector<Expr *, 8> SubExprs;
  SubExprs.reserve(E->getNumSubExprs());
  if (getDerived().TransformExprs(E->getSubExprs(), E->getNumSubExprs(), false,
                                  SubExprs, &ArgumentChanged))
    return ExprError();

  if (!getDerived().AlwaysRebuild() && !ArgumentChanged)
    return E;

  return getDerived().RebuildAtomicExpr(E->getBuiltinLoc(), SubExprs,
                                        E->getOp(), E->getRParenLoc());
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildPointerType(QualType PointeeType,
                                                    SourceLocation Star) {
  return SemaRef.FormPointerType(PointeeType, Star,
                                 getDerived().getBaseEntity());
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildArrayType(
    QualType ElementType, ArraySizeModifier SizeMod, const llvm::APInt *Size,
    Expr *SizeExpr, unsigned IndexTypeQuals, SourceRange BracketsRange) {
  if (SizeExpr || !Size)
    return SemaRef.FormArrayType(ElementType, SizeMod, SizeExpr, IndexTypeQuals,
                                 BracketsRange, getDerived().getBaseEntity());

  QualType Types[] = {
      SemaRef.Context.UnsignedCharTy,     SemaRef.Context.UnsignedShortTy,
      SemaRef.Context.UnsignedIntTy,      SemaRef.Context.UnsignedLongTy,
      SemaRef.Context.UnsignedLongLongTy, SemaRef.Context.UnsignedInt128Ty};
  QualType SizeType;
  for (const auto &T : Types)
    if (Size->getBitWidth() == SemaRef.Context.getIntWidth(T)) {
      SizeType = T;
      break;
    }

  // Note that we can return a VariableArrayType here in the case where
  // the element type was a dependent VariableArrayType.
  IntegerLiteral *ArraySize = IntegerLiteral::Create(
      SemaRef.Context, *Size, SizeType, BracketsRange.getBegin());
  return SemaRef.FormArrayType(ElementType, SizeMod, ArraySize, IndexTypeQuals,
                               BracketsRange, getDerived().getBaseEntity());
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildConstantArrayType(
    QualType ElementType, ArraySizeModifier SizeMod, const llvm::APInt &Size,
    Expr *SizeExpr, unsigned IndexTypeQuals, SourceRange BracketsRange) {
  return getDerived().RebuildArrayType(ElementType, SizeMod, &Size, SizeExpr,
                                       IndexTypeQuals, BracketsRange);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildIncompleteArrayType(
    QualType ElementType, ArraySizeModifier SizeMod, unsigned IndexTypeQuals,
    SourceRange BracketsRange) {
  return getDerived().RebuildArrayType(ElementType, SizeMod, nullptr, nullptr,
                                       IndexTypeQuals, BracketsRange);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildVariableArrayType(
    QualType ElementType, ArraySizeModifier SizeMod, Expr *SizeExpr,
    unsigned IndexTypeQuals, SourceRange BracketsRange) {
  return getDerived().RebuildArrayType(ElementType, SizeMod, nullptr, SizeExpr,
                                       IndexTypeQuals, BracketsRange);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildVectorType(QualType ElementType,
                                                   unsigned NumElements,
                                                   VectorKind VecKind) {
  return SemaRef.Context.getVectorType(ElementType, NumElements, VecKind);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildExtVectorType(
    QualType ElementType, unsigned NumElements, SourceLocation AttributeLoc) {
  llvm::APInt numElements(SemaRef.Context.getIntWidth(SemaRef.Context.IntTy),
                          NumElements, true);
  IntegerLiteral *VectorSize = IntegerLiteral::Create(
      SemaRef.Context, numElements, SemaRef.Context.IntTy, AttributeLoc);
  return SemaRef.FormExtVectorType(ElementType, VectorSize, AttributeLoc);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildConstantMatrixType(
    QualType ElementType, unsigned NumRows, unsigned NumColumns) {
  return SemaRef.Context.getConstantMatrixType(ElementType, NumRows,
                                               NumColumns);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildFunctionProtoType(
    QualType T, llvm::MutableArrayRef<QualType> ParamTypes,
    const FunctionProtoType::ExtProtoInfo &EPI) {
  return SemaRef.FormFunctionType(T, ParamTypes, getDerived().getBaseLocation(),
                                  getDerived().getBaseEntity(), EPI);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildFunctionNoProtoType(QualType T) {
  return SemaRef.Context.getFunctionNoProtoType(T);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildTypeOfExprType(Expr *E, SourceLocation,
                                                       TypeOfKind Kind) {
  return SemaRef.FormTypeofExprType(E, Kind);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildTypeOfType(QualType Underlying,
                                                   TypeOfKind Kind) {
  return SemaRef.Context.getTypeOfType(Underlying, Kind);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildAtomicType(QualType ValueType,
                                                   SourceLocation KWLoc) {
  return SemaRef.FormAtomicType(ValueType, KWLoc);
}

template <typename Derived>
QualType TreeTransform<Derived>::RebuildBitIntType(bool IsUnsigned,
                                                   unsigned NumBits,
                                                   SourceLocation Loc) {
  llvm::APInt NumBitsAP(SemaRef.Context.getIntWidth(SemaRef.Context.IntTy),
                        NumBits, true);
  IntegerLiteral *Bits = IntegerLiteral::Create(SemaRef.Context, NumBitsAP,
                                                SemaRef.Context.IntTy, Loc);
  return SemaRef.FormBitIntType(IsUnsigned, Bits, Loc);
}

} // end namespace neverc

#endif // NEVERC_LIB_SEMA_TREETRANSFORM_H
