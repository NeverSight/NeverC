#ifndef NEVERC_TREE_EXPR_H
#define NEVERC_TREE_EXPR_H

#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/SyncScope.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/LangOpts/TypeTraits.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/DependCalc.h"
#include "neverc/Tree/Core/TreeVector.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/OperationKinds.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "neverc/Tree/Type/DependenceFlags.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TrailingObjects.h"
#include <optional>

namespace llvm {
template <> struct PointerLikeTypeTraits<neverc::Expr *> {
  static inline void *getAsVoidPointer(neverc::Expr *P) { return P; }
  static inline neverc::Expr *getFromVoidPointer(void *P) {
    return static_cast<neverc::Expr *>(P);
  }
  static constexpr int NumLowBitsAvailable = 2;
};
} // namespace llvm

namespace neverc {
class APValue;
class TreeContext;
class CastExpr;
class Decl;
class IdentifierInfo;
class NamedDecl;
class OpaqueValueExpr;
class StringLiteral;
class TargetInfo;
class ValueDecl;

struct SubobjectAdjustment {
  FieldDecl *Field;

  explicit SubobjectAdjustment(FieldDecl *Field) : Field(Field) {}
};

class Expr : public ValueStmt {
  QualType TR;

public:
  Expr() = delete;
  Expr(const Expr &) = delete;
  Expr(Expr &&) = delete;
  Expr &operator=(const Expr &) = delete;
  Expr &operator=(Expr &&) = delete;

protected:
  Expr(StmtClass SC, QualType T, ExprValueKind VK, ExprObjectKind OK)
      : ValueStmt(SC) {
    ExprBits.Dependent = 0;
    ExprBits.ValueKind = VK;
    ExprBits.ObjectKind = OK;
    assert(ExprBits.ObjectKind == OK && "truncated kind");
    setType(T);
  }

  explicit Expr(StmtClass SC, EmptyShell) : ValueStmt(SC) {}

  void setDependence(ExprDependence Deps) {
    ExprBits.Dependent = static_cast<unsigned>(Deps);
  }

public:
  QualType getType() const { return TR; }
  void setType(QualType t) { TR = t; }

  ExprDependence getDependence() const {
    return static_cast<ExprDependence>(ExprBits.Dependent);
  }

  bool isValueDependent() const {
    return static_cast<bool>(getDependence() & ExprDependence::Value);
  }

  bool isTypeDependent() const {
    return static_cast<bool>(getDependence() & ExprDependence::Type);
  }

  bool containsErrors() const {
    return static_cast<bool>(getDependence() & ExprDependence::Error);
  }

  SourceLocation getExprLoc() const LLVM_READONLY;

  bool isUnusedResultAWarning(const Expr *&WarnExpr, SourceLocation &Loc,
                              SourceRange &R1, SourceRange &R2,
                              TreeContext &Ctx) const;

  bool isLValue() const { return getValueKind() == VK_LValue; }
  bool isPRValue() const { return getValueKind() == VK_PRValue; }

  enum LValueClassification {
    LV_Valid,
    LV_NotObjectType,
    LV_IncompleteVoidType,
    LV_DuplicateVectorComponents,
    LV_InvalidExpression,
    LV_RecordTemporary,
    LV_ArrayTemporary
  };
  LValueClassification ClassifyLValue(TreeContext &Ctx) const;

  enum isModifiableLvalueResult {
    MLV_Valid,
    MLV_NotObjectType,
    MLV_IncompleteVoidType,
    MLV_DuplicateVectorComponents,
    MLV_InvalidExpression,
    MLV_LValueCast, // Specialized form of MLV_InvalidExpression.
    MLV_IncompleteType,
    MLV_ConstQualified,
    MLV_ConstQualifiedField,
    MLV_ArrayType,
    MLV_RecordTemporary,
    MLV_ArrayTemporary
  };
  isModifiableLvalueResult
  isModifiableLvalue(TreeContext &Ctx, SourceLocation *Loc = nullptr) const;

  class Classification {
  public:
    /// The various classification results. Most of these mean prvalue.
    enum Kinds {
      CL_LValue,
      CL_Function,        // Functions cannot be lvalues in C.
      CL_Void,            // Void cannot be an lvalue in C.
      CL_AddressableVoid, // Void expression whose address can be taken in C.
      CL_DuplicateVectorComponents, // A vector shuffle with dupes.
      CL_RecordTemporary, // A temporary of record type, or subobject thereof.
      CL_ArrayTemporary,  // A temporary of array type.
      CL_PRValue          // A prvalue for any other reason, of any other type
    };
    /// The results of modification testing.
    enum ModifiableType {
      CM_Untested, // testModifiable was false.
      CM_Modifiable,
      CM_RValue,     // Not modifiable because it's an rvalue
      CM_LValueCast, // Same as CM_RValue, but indicates GCC cast-as-lvalue ext
      CM_ConstQualified,
      CM_ConstQualifiedField,
      CM_ArrayType,
      CM_IncompleteType
    };

  private:
    friend class Expr;

    unsigned short Kind;
    unsigned short Modifiable;

    explicit Classification(Kinds k, ModifiableType m)
        : Kind(k), Modifiable(m) {}

  public:
    Classification() {}

    Kinds getKind() const { return static_cast<Kinds>(Kind); }
    ModifiableType getModifiable() const {
      assert(Modifiable != CM_Untested && "Did not test for modifiability.");
      return static_cast<ModifiableType>(Modifiable);
    }
    bool isLValue() const { return Kind == CL_LValue; }
    bool isPRValue() const { return Kind >= CL_Function; }
    bool isModifiable() const { return getModifiable() == CM_Modifiable; }

    /// Create a simple, modifiably lvalue
    static Classification makeSimpleLValue() {
      return Classification(CL_LValue, CM_Modifiable);
    }
  };
  Classification Classify(TreeContext &Ctx) const {
    return ClassifyImpl(Ctx, nullptr);
  }

  Classification ClassifyModifiable(TreeContext &Ctx,
                                    SourceLocation &Loc) const {
    return ClassifyImpl(Ctx, &Loc);
  }

  FPOptions getFPFeaturesInEffect(const LangOptions &LO) const;

  ExprValueKind getValueKind() const {
    return static_cast<ExprValueKind>(ExprBits.ValueKind);
  }

  ExprObjectKind getObjectKind() const {
    return static_cast<ExprObjectKind>(ExprBits.ObjectKind);
  }

  bool isOrdinaryOrBitFieldObject() const {
    ExprObjectKind OK = getObjectKind();
    return (OK == OK_Ordinary || OK == OK_BitField);
  }

  void setValueKind(ExprValueKind Cat) { ExprBits.ValueKind = Cat; }

  void setObjectKind(ExprObjectKind Cat) { ExprBits.ObjectKind = Cat; }

private:
  Classification ClassifyImpl(TreeContext &Ctx, SourceLocation *Loc) const;

public:
  bool refersToBitField() const { return getObjectKind() == OK_BitField; }

  FieldDecl *getSourceBitField();

  const FieldDecl *getSourceBitField() const {
    return const_cast<Expr *>(this)->getSourceBitField();
  }

  Decl *getReferencedDeclOfCallee();
  const Decl *getReferencedDeclOfCallee() const {
    return const_cast<Expr *>(this)->getReferencedDeclOfCallee();
  }

  bool refersToVectorElement() const;

  bool refersToMatrixElement() const {
    return getObjectKind() == OK_MatrixComponent;
  }

  bool refersToGlobalRegisterVar() const;

  bool hasPlaceholderType() const { return getType()->isPlaceholderType(); }

  bool hasPlaceholderType(BuiltinType::Kind K) const {
    assert(BuiltinType::isPlaceholderTypeKind(K));
    if (const BuiltinType *BT = dyn_cast<BuiltinType>(getType()))
      return BT->getKind() == K;
    return false;
  }

  bool isKnownToHaveBooleanValue(bool Semantic = true) const;

  bool isFlexibleArrayMemberLike(
      TreeContext &Context,
      LangOptions::StrictFlexArraysLevelKind StrictFlexArraysLevel,
      bool IgnoreMacroSubstitution = false) const;

  std::optional<llvm::APSInt>
  getIntegerConstantExpr(const TreeContext &Ctx,
                         SourceLocation *Loc = nullptr) const;
  bool isIntegerConstantExpr(const TreeContext &Ctx,
                             SourceLocation *Loc = nullptr) const;

  static bool isPotentialConstantExprUnevaluated(
      Expr *E, const FunctionDecl *FD,
      llvm::SmallVectorImpl<PartialDiagnosticAt> &Diags);

  bool isConstantInitializer(TreeContext &Ctx, bool ForRef,
                             const Expr **Culprit = nullptr) const;

  const ValueDecl *
  getAsBuiltinConstantDeclRef(const TreeContext &Context) const;

  struct EvalStatus {
    /// Whether the evaluated expression has side effects.
    /// For example, (f() && 0) can be folded, but it still has side effects.
    bool HasSideEffects = false;

    /// Whether the evaluation hit undefined behavior.
    /// For example, 1.0 / 0.0 can be folded to Inf, but has undefined behavior.
    /// Likewise, INT_MAX + 1 can be folded to INT_MIN, but has UB.
    bool HasUndefinedBehavior = false;

    /// Diag - If this is non-null, it will be filled in with a stack of notes
    /// indicating why evaluation failed (or why it failed to produce a constant
    /// expression).
    /// If the expression is unfoldable, the notes will indicate why it's not
    /// foldable. If the expression is foldable, but not a constant expression,
    /// the notes will describes why it isn't a constant expression. If the
    /// expression *is* a constant expression, no notes will be produced.
    ///
    /// refactored at some point. Not all evaluations of the constant
    /// expression interpreter will display the given diagnostics, this means
    /// those kinds of uses are paying the expense of generating a diagnostic
    /// (which may include expensive operations like converting APValue objects
    /// to a string representation).
    llvm::SmallVectorImpl<PartialDiagnosticAt> *Diag = nullptr;

    EvalStatus() = default;

    // hasSideEffects - Return true if the evaluated expression has
    // side effects.
    bool hasSideEffects() const { return HasSideEffects; }
  };

  struct EvalResult : EvalStatus {
    /// Val - This is the value the expression can be folded to.
    APValue Val;

    // isGlobalLValue - Return true if the evaluated lvalue expression
    // is global.
    bool isGlobalLValue() const;
  };

  bool EvaluateAsRValue(EvalResult &Result, const TreeContext &Ctx,
                        bool InConstantContext = false) const;

  bool EvaluateAsBooleanCondition(bool &Result, const TreeContext &Ctx,
                                  bool InConstantContext = false) const;

  enum SideEffectsKind {
    SE_NoSideEffects,          ///< Strictly evaluate the expression.
    SE_AllowUndefinedBehavior, ///< Allow UB that we can give a value, but not
                               ///< arbitrary unmodeled side effects.
    SE_AllowSideEffects        ///< Allow any unmodeled side effect.
  };

  bool EvaluateAsInt(EvalResult &Result, const TreeContext &Ctx,
                     SideEffectsKind AllowSideEffects = SE_NoSideEffects,
                     bool InConstantContext = false) const;

  bool EvaluateAsFloat(llvm::APFloat &Result, const TreeContext &Ctx,
                       SideEffectsKind AllowSideEffects = SE_NoSideEffects,
                       bool InConstantContext = false) const;

  bool EvaluateAsFixedPoint(EvalResult &Result, const TreeContext &Ctx,
                            SideEffectsKind AllowSideEffects = SE_NoSideEffects,
                            bool InConstantContext = false) const;

  bool isEvaluatable(const TreeContext &Ctx,
                     SideEffectsKind AllowSideEffects = SE_NoSideEffects) const;

  bool HasSideEffects(const TreeContext &Ctx,
                      bool IncludePossibleEffects = true) const;

  llvm::APSInt EvaluateKnownConstInt(
      const TreeContext &Ctx,
      llvm::SmallVectorImpl<PartialDiagnosticAt> *Diag = nullptr) const;

  llvm::APSInt EvaluateKnownConstIntCheckOverflow(
      const TreeContext &Ctx,
      llvm::SmallVectorImpl<PartialDiagnosticAt> *Diag = nullptr) const;

  void EvaluateForOverflow(const TreeContext &Ctx) const;

  bool EvaluateAsLValue(EvalResult &Result, const TreeContext &Ctx,
                        bool InConstantContext = false) const;

  bool EvaluateAsInitializer(APValue &Result, const TreeContext &Ctx,
                             const VarDecl *VD,
                             llvm::SmallVectorImpl<PartialDiagnosticAt> &Notes,
                             bool IsConstantInitializer) const;

  bool EvaluateWithSubstitution(APValue &Value, TreeContext &Ctx,
                                const FunctionDecl *Callee,
                                llvm::ArrayRef<const Expr *> Args) const;

  bool EvaluateAsConstantExpr(EvalResult &Result, const TreeContext &Ctx) const;

  bool tryEvaluateObjectSize(uint64_t &Result, TreeContext &Ctx,
                             unsigned Type) const;

  bool tryEvaluateStrLen(uint64_t &Result, TreeContext &Ctx) const;

  bool EvaluateCharRangeAsString(std::string &Result,
                                 const Expr *SizeExpression,
                                 const Expr *PtrExpression, TreeContext &Ctx,
                                 EvalResult &Status) const;

  enum NullPointerConstantKind {
    /// Expression is not a Null pointer constant.
    NPCK_NotNull = 0,

    /// Expression is a Null pointer constant built from a zero integer
    /// expression that is not a simple, possibly parenthesized, zero literal.
    /// WG21 core issue 903 proposes classifying these expressions as "not
    /// pointers"
    /// once it is adopted.
    /// http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_active.html#903
    NPCK_ZeroExpression,

    /// Expression is a Null pointer constant built from a literal zero.
    NPCK_ZeroLiteral,

    /// Expression is a \c nullptr literal (C23).
    NPCK_nullptr
  };

  enum NullPointerConstantValueDependence {
    /// Specifies that the expression should never be value-dependent.
    NPC_NeverValueDependent = 0,

    /// Specifies that a value-dependent expression of integral or
    /// dependent type should be considered a null pointer constant.
    NPC_ValueDependentIsNull,

    /// Specifies that a value-dependent expression should be considered
    /// to never be a null pointer constant.
    NPC_ValueDependentIsNotNull
  };

  NullPointerConstantKind
  isNullPointerConstant(TreeContext &Ctx,
                        NullPointerConstantValueDependence NPC) const;

  Expr *IgnoreUnlessSpelledInSource();
  const Expr *IgnoreUnlessSpelledInSource() const {
    return const_cast<Expr *>(this)->IgnoreUnlessSpelledInSource();
  }

  Expr *IgnoreImpCasts() LLVM_READONLY;
  const Expr *IgnoreImpCasts() const {
    return const_cast<Expr *>(this)->IgnoreImpCasts();
  }

  Expr *IgnoreCasts() LLVM_READONLY;
  const Expr *IgnoreCasts() const {
    return const_cast<Expr *>(this)->IgnoreCasts();
  }

  Expr *IgnoreImplicit() LLVM_READONLY;
  const Expr *IgnoreImplicit() const {
    return const_cast<Expr *>(this)->IgnoreImplicit();
  }

  Expr *IgnoreImplicitAsWritten() LLVM_READONLY;
  const Expr *IgnoreImplicitAsWritten() const {
    return const_cast<Expr *>(this)->IgnoreImplicitAsWritten();
  }

  Expr *IgnoreParens() LLVM_READONLY;
  const Expr *IgnoreParens() const {
    return const_cast<Expr *>(this)->IgnoreParens();
  }

  Expr *IgnoreParenImpCasts() LLVM_READONLY;
  const Expr *IgnoreParenImpCasts() const {
    return const_cast<Expr *>(this)->IgnoreParenImpCasts();
  }

  Expr *IgnoreParenCasts() LLVM_READONLY;
  const Expr *IgnoreParenCasts() const {
    return const_cast<Expr *>(this)->IgnoreParenCasts();
  }

  Expr *IgnoreParenLValueCasts() LLVM_READONLY;
  const Expr *IgnoreParenLValueCasts() const {
    return const_cast<Expr *>(this)->IgnoreParenLValueCasts();
  }

  Expr *IgnoreParenNoopCasts(const TreeContext &Ctx) LLVM_READONLY;
  const Expr *IgnoreParenNoopCasts(const TreeContext &Ctx) const {
    return const_cast<Expr *>(this)->IgnoreParenNoopCasts(Ctx);
  }

  Expr *IgnoreParenBaseCasts() LLVM_READONLY;
  const Expr *IgnoreParenBaseCasts() const {
    return const_cast<Expr *>(this)->IgnoreParenBaseCasts();
  }

  const Expr *skipRValueSubobjectAdjustments(
      llvm::SmallVectorImpl<const Expr *> &CommaLHS,
      llvm::SmallVectorImpl<SubobjectAdjustment> &Adjustments) const;
  const Expr *skipRValueSubobjectAdjustments() const {
    llvm::SmallVector<const Expr *, 8> CommaLHSs;
    llvm::SmallVector<SubobjectAdjustment, 8> Adjustments;
    return skipRValueSubobjectAdjustments(CommaLHSs, Adjustments);
  }

  static bool isSameComparisonOperand(const Expr *E1, const Expr *E2);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstExprConstant &&
           T->getStmtClass() <= lastExprConstant;
  }
};
// PointerLikeTypeTraits is specialized so it can be used with a forward-decl of
// Expr. Verify that we got it right.
static_assert(llvm::PointerLikeTypeTraits<Expr *>::NumLowBitsAvailable <=
                  llvm::detail::ConstantLog2<alignof(Expr)>::value,
              "PointerLikeTypeTraits<Expr*> assumes too much alignment.");

//===----------------------------------------------------------------------===//
// Wrapper Expressions.
//===----------------------------------------------------------------------===//

class FullExpr : public Expr {
protected:
  Stmt *SubExpr;

  FullExpr(StmtClass SC, Expr *subexpr)
      : Expr(SC, subexpr->getType(), subexpr->getValueKind(),
             subexpr->getObjectKind()),
        SubExpr(subexpr) {
    setDependence(computeDependence(this));
  }
  FullExpr(StmtClass SC, EmptyShell Empty) : Expr(SC, Empty) {}

public:
  const Expr *getSubExpr() const { return cast<Expr>(SubExpr); }
  Expr *getSubExpr() { return cast<Expr>(SubExpr); }

  void setSubExpr(Expr *E) { SubExpr = E; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstFullExprConstant &&
           T->getStmtClass() <= lastFullExprConstant;
  }
};

enum class ConstantResultStorageKind { None, Int64, APValue };

class ConstantExpr final
    : public FullExpr,
      private llvm::TrailingObjects<ConstantExpr, APValue, uint64_t> {
  static_assert(std::is_same<uint64_t, llvm::APInt::WordType>::value,
                "ConstantExpr assumes that llvm::APInt::WordType is uint64_t "
                "for tail-allocated storage");
  friend TrailingObjects;

  size_t numTrailingObjects(OverloadToken<APValue>) const {
    return getResultStorageKind() == ConstantResultStorageKind::APValue;
  }
  size_t numTrailingObjects(OverloadToken<uint64_t>) const {
    return getResultStorageKind() == ConstantResultStorageKind::Int64;
  }

  uint64_t &Int64Result() {
    assert(getResultStorageKind() == ConstantResultStorageKind::Int64 &&
           "invalid accessor");
    return *getTrailingObjects<uint64_t>();
  }
  const uint64_t &Int64Result() const {
    return const_cast<ConstantExpr *>(this)->Int64Result();
  }
  APValue &APValueResult() {
    assert(getResultStorageKind() == ConstantResultStorageKind::APValue &&
           "invalid accessor");
    return *getTrailingObjects<APValue>();
  }
  APValue &APValueResult() const {
    return const_cast<ConstantExpr *>(this)->APValueResult();
  }

  ConstantExpr(Expr *SubExpr, ConstantResultStorageKind StorageKind,
               bool IsImmediateInvocation);
  ConstantExpr(EmptyShell Empty, ConstantResultStorageKind StorageKind);

public:
  static ConstantExpr *Create(const TreeContext &Context, Expr *E,
                              const APValue &Result);
  static ConstantExpr *
  Create(const TreeContext &Context, Expr *E,
         ConstantResultStorageKind Storage = ConstantResultStorageKind::None,
         bool IsImmediateInvocation = false);
  static ConstantExpr *CreateEmpty(const TreeContext &Context,
                                   ConstantResultStorageKind StorageKind);

  static ConstantResultStorageKind getStorageKind(const APValue &Value);
  static ConstantResultStorageKind getStorageKind(const Type *T,
                                                  const TreeContext &Context);

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return SubExpr->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return SubExpr->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ConstantExprClass;
  }

  void SetResult(APValue Value, const TreeContext &Context) {
    MoveIntoResult(Value, Context);
  }
  void MoveIntoResult(APValue &Value, const TreeContext &Context);

  APValue::ValueKind getResultAPValueKind() const {
    return static_cast<APValue::ValueKind>(ConstantExprBits.APValueKind);
  }
  ConstantResultStorageKind getResultStorageKind() const {
    return static_cast<ConstantResultStorageKind>(ConstantExprBits.ResultKind);
  }
  bool isImmediateInvocation() const {
    return ConstantExprBits.IsImmediateInvocation;
  }
  bool hasAPValueResult() const {
    return ConstantExprBits.APValueKind != APValue::None;
  }
  APValue getAPValueResult() const;
  APValue &getResultAsAPValue() const { return APValueResult(); }
  llvm::APSInt getResultAsAPSInt() const;
  // Iterators
  child_range children() { return child_range(&SubExpr, &SubExpr + 1); }
  const_child_range children() const {
    return const_child_range(&SubExpr, &SubExpr + 1);
  }
};

class ExprWithCleanups final
    : public FullExpr,
      private llvm::TrailingObjects<ExprWithCleanups, CompoundLiteralExpr *> {
public:
  using CleanupObject = CompoundLiteralExpr *;

private:
  friend TrailingObjects;

  ExprWithCleanups(EmptyShell, unsigned NumObjects);
  ExprWithCleanups(Expr *SubExpr, bool CleanupsHaveSideEffects,
                   llvm::ArrayRef<CleanupObject> Objects);

public:
  static ExprWithCleanups *Create(const TreeContext &C, EmptyShell empty,
                                  unsigned numObjects);

  static ExprWithCleanups *Create(const TreeContext &C, Expr *subexpr,
                                  bool CleanupsHaveSideEffects,
                                  llvm::ArrayRef<CleanupObject> objects);

  llvm::ArrayRef<CleanupObject> getObjects() const {
    return llvm::ArrayRef(getTrailingObjects<CleanupObject>(), getNumObjects());
  }

  unsigned getNumObjects() const { return ExprWithCleanupsBits.NumObjects; }

  CleanupObject getObject(unsigned i) const {
    assert(i < getNumObjects() && "Index out of range");
    return getObjects()[i];
  }

  bool cleanupsHaveSideEffects() const {
    return ExprWithCleanupsBits.CleanupsHaveSideEffects;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return SubExpr->getBeginLoc();
  }

  SourceLocation getEndLoc() const LLVM_READONLY {
    return SubExpr->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ExprWithCleanupsClass;
  }

  child_range children() { return child_range(&SubExpr, &SubExpr + 1); }

  const_child_range children() const {
    return const_child_range(&SubExpr, &SubExpr + 1);
  }
};

//===----------------------------------------------------------------------===//
// Primary Expressions.
//===----------------------------------------------------------------------===//

class OpaqueValueExpr : public Expr {

  Expr *SourceExpr;

public:
  OpaqueValueExpr(SourceLocation Loc, QualType T, ExprValueKind VK,
                  ExprObjectKind OK = OK_Ordinary, Expr *SourceExpr = nullptr)
      : Expr(OpaqueValueExprClass, T, VK, OK), SourceExpr(SourceExpr) {
    setIsUnique(false);
    OpaqueValueExprBits.Loc = Loc;
    setDependence(computeDependence(this));
  }

  explicit OpaqueValueExpr(EmptyShell Empty)
      : Expr(OpaqueValueExprClass, Empty) {}

  SourceLocation getLocation() const { return OpaqueValueExprBits.Loc; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return SourceExpr ? SourceExpr->getBeginLoc() : getLocation();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return SourceExpr ? SourceExpr->getEndLoc() : getLocation();
  }
  SourceLocation getExprLoc() const LLVM_READONLY {
    return SourceExpr ? SourceExpr->getExprLoc() : getLocation();
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  Expr *getSourceExpr() const { return SourceExpr; }

  void setIsUnique(bool V) {
    assert((!V || SourceExpr) &&
           "unique OVEs are expected to have source expressions");
    OpaqueValueExprBits.IsUnique = V;
  }

  bool isUnique() const { return OpaqueValueExprBits.IsUnique; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OpaqueValueExprClass;
  }
};

class DeclRefExpr final
    : public Expr,
      private llvm::TrailingObjects<DeclRefExpr, NamedDecl *> {

  friend TrailingObjects;

  ValueDecl *D;

  size_t numTrailingObjects(OverloadToken<NamedDecl *>) const {
    return hasFoundDecl();
  }

  bool hasFoundDecl() const { return DeclRefExprBits.HasFoundDecl; }

  DeclRefExpr(const TreeContext &Ctx, ValueDecl *D,
              const DeclarationNameInfo &NameInfo, NamedDecl *FoundD,
              QualType T, ExprValueKind VK, NonOdrUseReason NOUR);

  explicit DeclRefExpr(EmptyShell Empty) : Expr(DeclRefExprClass, Empty) {}

public:
  DeclRefExpr(const TreeContext &Ctx, ValueDecl *D, QualType T,
              ExprValueKind VK, SourceLocation L,
              NonOdrUseReason NOUR = NOUR_None);

  static DeclRefExpr *Create(const TreeContext &Context, ValueDecl *D,
                             SourceLocation NameLoc, QualType T,
                             ExprValueKind VK, NamedDecl *FoundD = nullptr,
                             NonOdrUseReason NOUR = NOUR_None);

  static DeclRefExpr *Create(const TreeContext &Context, ValueDecl *D,
                             const DeclarationNameInfo &NameInfo, QualType T,
                             ExprValueKind VK, NamedDecl *FoundD = nullptr,
                             NonOdrUseReason NOUR = NOUR_None);

  static DeclRefExpr *CreateEmpty(const TreeContext &Context,
                                  bool HasFoundDecl);

  ValueDecl *getDecl() { return D; }
  const ValueDecl *getDecl() const { return D; }
  void setDecl(ValueDecl *NewD);

  DeclarationNameInfo getNameInfo() const {
    return DeclarationNameInfo(getDecl()->getDeclName(), getLocation());
  }

  SourceLocation getLocation() const { return DeclRefExprBits.Loc; }
  void setLocation(SourceLocation L) { DeclRefExprBits.Loc = L; }
  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  NamedDecl *getFoundDecl() {
    return hasFoundDecl() ? *getTrailingObjects<NamedDecl *>() : D;
  }
  const NamedDecl *getFoundDecl() const {
    return hasFoundDecl() ? *getTrailingObjects<NamedDecl *>() : D;
  }

  bool hadMultipleCandidates() const {
    return DeclRefExprBits.HadMultipleCandidates;
  }
  void setHadMultipleCandidates(bool V = true) {
    DeclRefExprBits.HadMultipleCandidates = V;
  }

  NonOdrUseReason isNonOdrUse() const {
    return static_cast<NonOdrUseReason>(DeclRefExprBits.NonOdrUseReason);
  }

  bool isImmediateEscalating() const {
    return DeclRefExprBits.IsImmediateEscalating;
  }

  void setIsImmediateEscalating(bool Set) {
    DeclRefExprBits.IsImmediateEscalating = Set;
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DeclRefExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class APNumericStorage {
  union {
    uint64_t VAL;   ///< Used to store the <= 64 bits integer value.
    uint64_t *pVal; ///< Used to store the >64 bits integer value.
  };
  unsigned BitWidth;

  bool hasAllocation() const { return llvm::APInt::getNumWords(BitWidth) > 1; }

  APNumericStorage(const APNumericStorage &) = delete;
  void operator=(const APNumericStorage &) = delete;

protected:
  APNumericStorage() : VAL(0), BitWidth(0) {}

  llvm::APInt getIntValue() const {
    unsigned NumWords = llvm::APInt::getNumWords(BitWidth);
    if (NumWords > 1)
      return llvm::APInt(BitWidth, NumWords, pVal);
    else
      return llvm::APInt(BitWidth, VAL);
  }
  void setIntValue(const TreeContext &C, const llvm::APInt &Val);
};

class APIntStorage : private APNumericStorage {
public:
  llvm::APInt getValue() const { return getIntValue(); }
  void setValue(const TreeContext &C, const llvm::APInt &Val) {
    setIntValue(C, Val);
  }
};

class APFloatStorage : private APNumericStorage {
public:
  llvm::APFloat getValue(const llvm::fltSemantics &Semantics) const {
    return llvm::APFloat(Semantics, getIntValue());
  }
  void setValue(const TreeContext &C, const llvm::APFloat &Val) {
    setIntValue(C, Val.bitcastToAPInt());
  }
};

class IntegerLiteral : public Expr, public APIntStorage {
  SourceLocation Loc;

  explicit IntegerLiteral(EmptyShell Empty)
      : Expr(IntegerLiteralClass, Empty) {}

public:
  // type should be IntTy, LongTy, LongLongTy, UnsignedIntTy, UnsignedLongTy,
  // or UnsignedLongLongTy
  IntegerLiteral(const TreeContext &C, const llvm::APInt &V, QualType type,
                 SourceLocation l);

  static IntegerLiteral *Create(const TreeContext &C, const llvm::APInt &V,
                                QualType type, SourceLocation l);
  static IntegerLiteral *Create(const TreeContext &C, EmptyShell Empty);

  SourceLocation getBeginLoc() const LLVM_READONLY { return Loc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return Loc; }

  SourceLocation getLocation() const { return Loc; }

  void setLocation(SourceLocation Location) { Loc = Location; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == IntegerLiteralClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class FixedPointLiteral : public Expr, public APIntStorage {
  SourceLocation Loc;
  unsigned Scale;

  explicit FixedPointLiteral(EmptyShell Empty)
      : Expr(FixedPointLiteralClass, Empty) {}

public:
  FixedPointLiteral(const TreeContext &C, const llvm::APInt &V, QualType type,
                    SourceLocation l, unsigned Scale);

  // Store the int as is without any bit shifting.
  static FixedPointLiteral *CreateFromRawInt(const TreeContext &C,
                                             const llvm::APInt &V,
                                             QualType type, SourceLocation l,
                                             unsigned Scale);

  static FixedPointLiteral *Create(const TreeContext &C, EmptyShell Empty);

  SourceLocation getBeginLoc() const LLVM_READONLY { return Loc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return Loc; }

  SourceLocation getLocation() const { return Loc; }

  void setLocation(SourceLocation Location) { Loc = Location; }

  unsigned getScale() const { return Scale; }
  void setScale(unsigned S) { Scale = S; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == FixedPointLiteralClass;
  }

  std::string getValueAsString(unsigned Radix) const;

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

enum class CharacterLiteralKind { Ascii, Wide, UTF8, UTF16, UTF32 };

class CharacterLiteral : public Expr {
  unsigned Value;
  SourceLocation Loc;

public:
  // type should be IntTy
  CharacterLiteral(unsigned value, CharacterLiteralKind kind, QualType type,
                   SourceLocation l)
      : Expr(CharacterLiteralClass, type, VK_PRValue, OK_Ordinary),
        Value(value), Loc(l) {
    CharacterLiteralBits.Kind = llvm::to_underlying(kind);
    setDependence(ExprDependence::None);
  }

  CharacterLiteral(EmptyShell Empty) : Expr(CharacterLiteralClass, Empty) {}

  SourceLocation getLocation() const { return Loc; }
  CharacterLiteralKind getKind() const {
    return static_cast<CharacterLiteralKind>(CharacterLiteralBits.Kind);
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return Loc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return Loc; }

  unsigned getValue() const { return Value; }

  void setLocation(SourceLocation Location) { Loc = Location; }
  void setKind(CharacterLiteralKind kind) {
    CharacterLiteralBits.Kind = llvm::to_underlying(kind);
  }
  void setValue(unsigned Val) { Value = Val; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CharacterLiteralClass;
  }

  static void print(unsigned val, CharacterLiteralKind Kind,
                    llvm::raw_ostream &OS);

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class FloatingLiteral : public Expr, private APFloatStorage {
  SourceLocation Loc;

  FloatingLiteral(const TreeContext &C, const llvm::APFloat &V, bool isexact,
                  QualType Type, SourceLocation L);

  explicit FloatingLiteral(const TreeContext &C, EmptyShell Empty);

public:
  static FloatingLiteral *Create(const TreeContext &C, const llvm::APFloat &V,
                                 bool isexact, QualType Type, SourceLocation L);
  static FloatingLiteral *Create(const TreeContext &C, EmptyShell Empty);

  llvm::APFloat getValue() const {
    return APFloatStorage::getValue(getSemantics());
  }
  void setValue(const TreeContext &C, const llvm::APFloat &Val) {
    assert(&getSemantics() == &Val.getSemantics() && "Inconsistent semantics");
    APFloatStorage::setValue(C, Val);
  }

  llvm::APFloatBase::Semantics getRawSemantics() const {
    return static_cast<llvm::APFloatBase::Semantics>(
        FloatingLiteralBits.Semantics);
  }

  void setRawSemantics(llvm::APFloatBase::Semantics Sem) {
    FloatingLiteralBits.Semantics = Sem;
  }

  const llvm::fltSemantics &getSemantics() const {
    return llvm::APFloatBase::EnumToSemantics(
        static_cast<llvm::APFloatBase::Semantics>(
            FloatingLiteralBits.Semantics));
  }

  void setSemantics(const llvm::fltSemantics &Sem) {
    FloatingLiteralBits.Semantics = llvm::APFloatBase::SemanticsToEnum(Sem);
  }

  bool isExact() const { return FloatingLiteralBits.IsExact; }
  void setExact(bool E) { FloatingLiteralBits.IsExact = E; }

  double getValueAsApproximateDouble() const;

  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return Loc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return Loc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == FloatingLiteralClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class ImaginaryLiteral : public Expr {
  Stmt *Val;

public:
  ImaginaryLiteral(Expr *val, QualType Ty)
      : Expr(ImaginaryLiteralClass, Ty, VK_PRValue, OK_Ordinary), Val(val) {
    setDependence(ExprDependence::None);
  }

  explicit ImaginaryLiteral(EmptyShell Empty)
      : Expr(ImaginaryLiteralClass, Empty) {}

  const Expr *getSubExpr() const { return cast<Expr>(Val); }
  Expr *getSubExpr() { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return Val->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY { return Val->getEndLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ImaginaryLiteralClass;
  }

  // Iterators
  child_range children() { return child_range(&Val, &Val + 1); }
  const_child_range children() const {
    return const_child_range(&Val, &Val + 1);
  }
};

enum class StringLiteralKind {
  Ordinary,
  Wide,
  UTF8,
  UTF16,
  UTF32,
  Unevaluated
};

class StringLiteral final
    : public Expr,
      private llvm::TrailingObjects<StringLiteral, unsigned, SourceLocation,
                                    char> {

  friend TrailingObjects;

  unsigned numTrailingObjects(OverloadToken<unsigned>) const { return 1; }
  unsigned numTrailingObjects(OverloadToken<SourceLocation>) const {
    return getNumConcatenated();
  }

  unsigned numTrailingObjects(OverloadToken<char>) const {
    return getByteLength();
  }

  char *getStrDataAsChar() { return getTrailingObjects<char>(); }
  const char *getStrDataAsChar() const { return getTrailingObjects<char>(); }

  const uint16_t *getStrDataAsUInt16() const {
    return reinterpret_cast<const uint16_t *>(getTrailingObjects<char>());
  }

  const uint32_t *getStrDataAsUInt32() const {
    return reinterpret_cast<const uint32_t *>(getTrailingObjects<char>());
  }

  StringLiteral(const TreeContext &Ctx, llvm::StringRef Str,
                StringLiteralKind Kind, QualType Ty, const SourceLocation *Loc,
                unsigned NumConcatenated);

  StringLiteral(EmptyShell Empty, unsigned NumConcatenated, unsigned Length,
                unsigned CharByteWidth);

  static unsigned mapCharByteWidth(TargetInfo const &Target,
                                   StringLiteralKind SK);

  void setStrTokenLoc(unsigned TokNum, SourceLocation L) {
    assert(TokNum < getNumConcatenated() && "Invalid tok number");
    getTrailingObjects<SourceLocation>()[TokNum] = L;
  }

public:
  static StringLiteral *Create(const TreeContext &Ctx, llvm::StringRef Str,
                               StringLiteralKind Kind, QualType Ty,
                               const SourceLocation *Loc,
                               unsigned NumConcatenated);

  static StringLiteral *Create(const TreeContext &Ctx, llvm::StringRef Str,
                               StringLiteralKind Kind, QualType Ty,
                               SourceLocation Loc) {
    return Create(Ctx, Str, Kind, Ty, &Loc, 1);
  }

  static StringLiteral *CreateEmpty(const TreeContext &Ctx,
                                    unsigned NumConcatenated, unsigned Length,
                                    unsigned CharByteWidth);

  llvm::StringRef getString() const {
    assert((isUnevaluated() || getCharByteWidth() == 1) &&
           "This function is used in places that assume strings use char");
    return llvm::StringRef(getStrDataAsChar(), getByteLength());
  }

  llvm::StringRef getBytes() const {
    return llvm::StringRef(getStrDataAsChar(), getByteLength());
  }

  void outputString(llvm::raw_ostream &OS) const;

  uint32_t getCodeUnit(size_t i) const {
    assert(i < getLength() && "out of bounds access");
    switch (getCharByteWidth()) {
    case 1:
      return static_cast<unsigned char>(getStrDataAsChar()[i]);
    case 2:
      return getStrDataAsUInt16()[i];
    case 4:
      return getStrDataAsUInt32()[i];
    }
    llvm_unreachable("Unsupported character width!");
  }

  unsigned getByteLength() const { return getCharByteWidth() * getLength(); }
  unsigned getLength() const { return *getTrailingObjects<unsigned>(); }
  unsigned getCharByteWidth() const { return StringLiteralBits.CharByteWidth; }

  StringLiteralKind getKind() const {
    return static_cast<StringLiteralKind>(StringLiteralBits.Kind);
  }

  bool isOrdinary() const { return getKind() == StringLiteralKind::Ordinary; }
  bool isWide() const { return getKind() == StringLiteralKind::Wide; }
  bool isUTF8() const { return getKind() == StringLiteralKind::UTF8; }
  bool isUTF16() const { return getKind() == StringLiteralKind::UTF16; }
  bool isUTF32() const { return getKind() == StringLiteralKind::UTF32; }
  bool isUnevaluated() const {
    return getKind() == StringLiteralKind::Unevaluated;
  }
  bool isPascal() const { return StringLiteralBits.IsPascal; }

  bool containsNonAscii() const {
    for (auto c : getString())
      if (!isASCII(c))
        return true;
    return false;
  }

  bool containsNonAsciiOrNull() const {
    for (auto c : getString())
      if (!isASCII(c) || !c)
        return true;
    return false;
  }

  unsigned getNumConcatenated() const {
    return StringLiteralBits.NumConcatenated;
  }

  SourceLocation getStrTokenLoc(unsigned TokNum) const {
    assert(TokNum < getNumConcatenated() && "Invalid tok number");
    return getTrailingObjects<SourceLocation>()[TokNum];
  }

  SourceLocation
  getLocationOfByte(unsigned ByteNo, const SourceManager &SM,
                    const LangOptions &Features, const TargetInfo &Target,
                    unsigned *StartToken = nullptr,
                    unsigned *StartTokenByteOffset = nullptr) const;

  typedef const SourceLocation *tokloc_iterator;

  tokloc_iterator tokloc_begin() const {
    return getTrailingObjects<SourceLocation>();
  }

  tokloc_iterator tokloc_end() const {
    return getTrailingObjects<SourceLocation>() + getNumConcatenated();
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return *tokloc_begin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return *(tokloc_end() - 1); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == StringLiteralClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

enum class PredefinedIdentKind {
  Func,
  Function,
  LFunction, // Same as Function, but as wide string.
  FuncDName,
  FuncSig,
  LFuncSig, // Same as FuncSig, but as wide string
  PrettyFunction,
};

class PredefinedExpr final
    : public Expr,
      private llvm::TrailingObjects<PredefinedExpr, Stmt *> {

  friend TrailingObjects;

  // PredefinedExpr is optionally followed by a single trailing
  // "Stmt *" for the predefined identifier. It is present if and only if
  // hasFunctionName() is true and is always a "StringLiteral *".

  PredefinedExpr(SourceLocation L, QualType FNTy, PredefinedIdentKind IK,
                 bool IsTransparent, StringLiteral *SL);

  explicit PredefinedExpr(EmptyShell Empty, bool HasFunctionName);

  bool hasFunctionName() const { return PredefinedExprBits.HasFunctionName; }

  void setFunctionName(StringLiteral *SL) {
    assert(hasFunctionName() &&
           "This PredefinedExpr has no storage for a function name!");
    *getTrailingObjects<Stmt *>() = SL;
  }

public:
  static PredefinedExpr *Create(const TreeContext &Ctx, SourceLocation L,
                                QualType FNTy, PredefinedIdentKind IK,
                                bool IsTransparent, StringLiteral *SL);

  static PredefinedExpr *CreateEmpty(const TreeContext &Ctx,
                                     bool HasFunctionName);

  PredefinedIdentKind getIdentKind() const {
    return static_cast<PredefinedIdentKind>(PredefinedExprBits.Kind);
  }

  bool isTransparent() const { return PredefinedExprBits.IsTransparent; }

  SourceLocation getLocation() const { return PredefinedExprBits.Loc; }
  void setLocation(SourceLocation L) { PredefinedExprBits.Loc = L; }

  StringLiteral *getFunctionName() {
    return hasFunctionName()
               ? static_cast<StringLiteral *>(*getTrailingObjects<Stmt *>())
               : nullptr;
  }

  const StringLiteral *getFunctionName() const {
    return hasFunctionName()
               ? static_cast<StringLiteral *>(*getTrailingObjects<Stmt *>())
               : nullptr;
  }

  static llvm::StringRef getIdentKindName(PredefinedIdentKind IK);
  llvm::StringRef getIdentKindName() const {
    return getIdentKindName(getIdentKind());
  }

  static std::string ComputeName(PredefinedIdentKind IK,
                                 const Decl *CurrentDecl);

  SourceLocation getBeginLoc() const { return getLocation(); }
  SourceLocation getEndLoc() const { return getLocation(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == PredefinedExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(getTrailingObjects<Stmt *>(),
                       getTrailingObjects<Stmt *>() + hasFunctionName());
  }

  const_child_range children() const {
    return const_child_range(getTrailingObjects<Stmt *>(),
                             getTrailingObjects<Stmt *>() + hasFunctionName());
  }
};

class ParenExpr : public Expr {
  SourceLocation L, R;
  Stmt *Val;

public:
  ParenExpr(SourceLocation l, SourceLocation r, Expr *val)
      : Expr(ParenExprClass, val->getType(), val->getValueKind(),
             val->getObjectKind()),
        L(l), R(r), Val(val) {
    setDependence(computeDependence(this));
  }

  explicit ParenExpr(EmptyShell Empty) : Expr(ParenExprClass, Empty) {}

  const Expr *getSubExpr() const { return cast<Expr>(Val); }
  Expr *getSubExpr() { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return L; }
  SourceLocation getEndLoc() const LLVM_READONLY { return R; }

  SourceLocation getLParen() const { return L; }
  void setLParen(SourceLocation Loc) { L = Loc; }

  SourceLocation getRParen() const { return R; }
  void setRParen(SourceLocation Loc) { R = Loc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ParenExprClass;
  }

  // Iterators
  child_range children() { return child_range(&Val, &Val + 1); }
  const_child_range children() const {
    return const_child_range(&Val, &Val + 1);
  }
};

class UnaryOperator final
    : public Expr,
      private llvm::TrailingObjects<UnaryOperator, FPOptionsOverride> {
  Stmt *Val;

  size_t numTrailingObjects(OverloadToken<FPOptionsOverride>) const {
    return UnaryOperatorBits.HasFPFeatures ? 1 : 0;
  }

  FPOptionsOverride &getTrailingFPFeatures() {
    assert(UnaryOperatorBits.HasFPFeatures);
    return *getTrailingObjects<FPOptionsOverride>();
  }

  const FPOptionsOverride &getTrailingFPFeatures() const {
    assert(UnaryOperatorBits.HasFPFeatures);
    return *getTrailingObjects<FPOptionsOverride>();
  }

public:
  typedef UnaryOperatorKind Opcode;

protected:
  UnaryOperator(const TreeContext &Ctx, Expr *input, Opcode opc, QualType type,
                ExprValueKind VK, ExprObjectKind OK, SourceLocation l,
                bool CanOverflow, FPOptionsOverride FPFeatures);

  explicit UnaryOperator(bool HasFPFeatures, EmptyShell Empty)
      : Expr(UnaryOperatorClass, Empty) {
    UnaryOperatorBits.Opc = UO_AddrOf;
    UnaryOperatorBits.HasFPFeatures = HasFPFeatures;
  }

public:
  static UnaryOperator *CreateEmpty(const TreeContext &C, bool hasFPFeatures);

  static UnaryOperator *Create(const TreeContext &C, Expr *input, Opcode opc,
                               QualType type, ExprValueKind VK,
                               ExprObjectKind OK, SourceLocation l,
                               bool CanOverflow, FPOptionsOverride FPFeatures);

  Opcode getOpcode() const {
    return static_cast<Opcode>(UnaryOperatorBits.Opc);
  }
  void setOpcode(Opcode Opc) { UnaryOperatorBits.Opc = Opc; }

  Expr *getSubExpr() const { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  SourceLocation getOperatorLoc() const { return UnaryOperatorBits.Loc; }
  void setOperatorLoc(SourceLocation L) { UnaryOperatorBits.Loc = L; }

  bool canOverflow() const { return UnaryOperatorBits.CanOverflow; }
  void setCanOverflow(bool C) { UnaryOperatorBits.CanOverflow = C; }

  bool isFPContractableWithinStatement(const LangOptions &LO) const {
    return getFPFeaturesInEffect(LO).allowFPContractWithinStatement();
  }

  bool isFEnvAccessOn(const LangOptions &LO) const {
    return getFPFeaturesInEffect(LO).getAllowFEnvAccess();
  }

  static bool isPostfix(Opcode Op) {
    return Op == UO_PostInc || Op == UO_PostDec;
  }

  static bool isPrefix(Opcode Op) { return Op == UO_PreInc || Op == UO_PreDec; }

  bool isPrefix() const { return isPrefix(getOpcode()); }
  bool isPostfix() const { return isPostfix(getOpcode()); }

  static bool isIncrementOp(Opcode Op) {
    return Op == UO_PreInc || Op == UO_PostInc;
  }
  bool isIncrementOp() const { return isIncrementOp(getOpcode()); }

  static bool isDecrementOp(Opcode Op) {
    return Op == UO_PreDec || Op == UO_PostDec;
  }
  bool isDecrementOp() const { return isDecrementOp(getOpcode()); }

  static bool isIncrementDecrementOp(Opcode Op) { return Op <= UO_PreDec; }
  bool isIncrementDecrementOp() const {
    return isIncrementDecrementOp(getOpcode());
  }

  static bool isArithmeticOp(Opcode Op) {
    return Op >= UO_Plus && Op <= UO_LNot;
  }
  bool isArithmeticOp() const { return isArithmeticOp(getOpcode()); }

  static llvm::StringRef getOpcodeStr(Opcode Op);

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return isPostfix() ? Val->getBeginLoc() : getOperatorLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return isPostfix() ? getOperatorLoc() : Val->getEndLoc();
  }
  SourceLocation getExprLoc() const { return getOperatorLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == UnaryOperatorClass;
  }

  // Iterators
  child_range children() { return child_range(&Val, &Val + 1); }
  const_child_range children() const {
    return const_child_range(&Val, &Val + 1);
  }

  bool hasStoredFPFeatures() const { return UnaryOperatorBits.HasFPFeatures; }

  FPOptionsOverride getStoredFPFeatures() const {
    return getTrailingFPFeatures();
  }

protected:
  void setStoredFPFeatures(FPOptionsOverride F) { getTrailingFPFeatures() = F; }

public:
  FPOptions getFPFeaturesInEffect(const LangOptions &LO) const {
    if (UnaryOperatorBits.HasFPFeatures)
      return getStoredFPFeatures().applyOverrides(LO);
    return FPOptions::defaultWithoutTrailingStorage(LO);
  }
  FPOptionsOverride getFPOptionsOverride() const {
    if (UnaryOperatorBits.HasFPFeatures)
      return getStoredFPFeatures();
    return FPOptionsOverride();
  }

  friend TrailingObjects;
};

// __builtin_offsetof(type, identifier(.identifier|[expr])*)
class OffsetOfNode {
public:
  enum Kind {
    /// An index into an array.
    Array = 0x00,
    /// A field.
    Field = 0x01,
    /// A field in a dependent type, known only by its name.
    Identifier = 0x02,
  };

private:
  enum { MaskBits = 2, Mask = 0x03 };

  SourceRange Range;

  uintptr_t Data;

public:
  OffsetOfNode(SourceLocation LBracketLoc, unsigned Index,
               SourceLocation RBracketLoc)
      : Range(LBracketLoc, RBracketLoc), Data((Index << 2) | Array) {}

  OffsetOfNode(SourceLocation DotLoc, FieldDecl *Field, SourceLocation NameLoc)
      : Range(DotLoc.isValid() ? DotLoc : NameLoc, NameLoc),
        Data(reinterpret_cast<uintptr_t>(Field) | OffsetOfNode::Field) {}

  OffsetOfNode(SourceLocation DotLoc, IdentifierInfo *Name,
               SourceLocation NameLoc)
      : Range(DotLoc.isValid() ? DotLoc : NameLoc, NameLoc),
        Data(reinterpret_cast<uintptr_t>(Name) | Identifier) {}

  Kind getKind() const { return static_cast<Kind>(Data & Mask); }

  unsigned getArrayExprIndex() const {
    assert(getKind() == Array);
    return Data >> 2;
  }

  FieldDecl *getField() const {
    assert(getKind() == Field);
    return reinterpret_cast<FieldDecl *>(Data & ~(uintptr_t)Mask);
  }

  IdentifierInfo *getFieldName() const;

  SourceRange getSourceRange() const LLVM_READONLY { return Range; }
  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }
};

class OffsetOfExpr final
    : public Expr,
      private llvm::TrailingObjects<OffsetOfExpr, OffsetOfNode, Expr *> {
  SourceLocation OperatorLoc, RParenLoc;
  // Base type;
  TypeSourceInfo *TSInfo;
  // Number of sub-components (i.e. instances of OffsetOfNode).
  unsigned NumComps;
  // Number of sub-expressions (i.e. array subscript expressions).
  unsigned NumExprs;

  size_t numTrailingObjects(OverloadToken<OffsetOfNode>) const {
    return NumComps;
  }

  OffsetOfExpr(const TreeContext &C, QualType type, SourceLocation OperatorLoc,
               TypeSourceInfo *tsi, llvm::ArrayRef<OffsetOfNode> comps,
               llvm::ArrayRef<Expr *> exprs, SourceLocation RParenLoc);

  explicit OffsetOfExpr(unsigned numComps, unsigned numExprs)
      : Expr(OffsetOfExprClass, EmptyShell()), TSInfo(nullptr),
        NumComps(numComps), NumExprs(numExprs) {}

public:
  static OffsetOfExpr *Create(const TreeContext &C, QualType type,
                              SourceLocation OperatorLoc, TypeSourceInfo *tsi,
                              llvm::ArrayRef<OffsetOfNode> comps,
                              llvm::ArrayRef<Expr *> exprs,
                              SourceLocation RParenLoc);

  static OffsetOfExpr *CreateEmpty(const TreeContext &C, unsigned NumComps,
                                   unsigned NumExprs);

  SourceLocation getOperatorLoc() const { return OperatorLoc; }
  void setOperatorLoc(SourceLocation L) { OperatorLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation R) { RParenLoc = R; }

  TypeSourceInfo *getTypeSourceInfo() const { return TSInfo; }
  void setTypeSourceInfo(TypeSourceInfo *tsi) { TSInfo = tsi; }

  const OffsetOfNode &getComponent(unsigned Idx) const {
    assert(Idx < NumComps && "Subscript out of range");
    return getTrailingObjects<OffsetOfNode>()[Idx];
  }

  void setComponent(unsigned Idx, OffsetOfNode ON) {
    assert(Idx < NumComps && "Subscript out of range");
    getTrailingObjects<OffsetOfNode>()[Idx] = ON;
  }

  unsigned getNumComponents() const { return NumComps; }

  Expr *getIndexExpr(unsigned Idx) {
    assert(Idx < NumExprs && "Subscript out of range");
    return getTrailingObjects<Expr *>()[Idx];
  }

  const Expr *getIndexExpr(unsigned Idx) const {
    assert(Idx < NumExprs && "Subscript out of range");
    return getTrailingObjects<Expr *>()[Idx];
  }

  void setIndexExpr(unsigned Idx, Expr *E) {
    assert(Idx < NumComps && "Subscript out of range");
    getTrailingObjects<Expr *>()[Idx] = E;
  }

  unsigned getNumExpressions() const { return NumExprs; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return OperatorLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OffsetOfExprClass;
  }

  // Iterators
  child_range children() {
    Stmt **begin = reinterpret_cast<Stmt **>(getTrailingObjects<Expr *>());
    return child_range(begin, begin + NumExprs);
  }
  const_child_range children() const {
    Stmt *const *begin =
        reinterpret_cast<Stmt *const *>(getTrailingObjects<Expr *>());
    return const_child_range(begin, begin + NumExprs);
  }
  friend TrailingObjects;
};

class UnaryExprOrTypeTraitExpr : public Expr {
  union {
    TypeSourceInfo *Ty;
    Stmt *Ex;
  } Argument;
  SourceLocation OpLoc, RParenLoc;

public:
  UnaryExprOrTypeTraitExpr(UnaryExprOrTypeTrait ExprKind, TypeSourceInfo *TInfo,
                           QualType resultType, SourceLocation op,
                           SourceLocation rp)
      : Expr(UnaryExprOrTypeTraitExprClass, resultType, VK_PRValue,
             OK_Ordinary),
        OpLoc(op), RParenLoc(rp) {
    assert(ExprKind <= UETT_Last && "invalid enum value!");
    UnaryExprOrTypeTraitExprBits.Kind = ExprKind;
    assert(static_cast<unsigned>(ExprKind) ==
               UnaryExprOrTypeTraitExprBits.Kind &&
           "UnaryExprOrTypeTraitExprBits.Kind overflow!");
    UnaryExprOrTypeTraitExprBits.IsType = true;
    Argument.Ty = TInfo;
    setDependence(computeDependence(this));
  }

  UnaryExprOrTypeTraitExpr(UnaryExprOrTypeTrait ExprKind, Expr *E,
                           QualType resultType, SourceLocation op,
                           SourceLocation rp);

  explicit UnaryExprOrTypeTraitExpr(EmptyShell Empty)
      : Expr(UnaryExprOrTypeTraitExprClass, Empty) {}

  UnaryExprOrTypeTrait getKind() const {
    return static_cast<UnaryExprOrTypeTrait>(UnaryExprOrTypeTraitExprBits.Kind);
  }
  void setKind(UnaryExprOrTypeTrait K) {
    assert(K <= UETT_Last && "invalid enum value!");
    UnaryExprOrTypeTraitExprBits.Kind = K;
    assert(static_cast<unsigned>(K) == UnaryExprOrTypeTraitExprBits.Kind &&
           "UnaryExprOrTypeTraitExprBits.Kind overflow!");
  }

  bool isArgumentType() const { return UnaryExprOrTypeTraitExprBits.IsType; }
  QualType getArgumentType() const { return getArgumentTypeInfo()->getType(); }
  TypeSourceInfo *getArgumentTypeInfo() const {
    assert(isArgumentType() && "calling getArgumentType() when arg is expr");
    return Argument.Ty;
  }
  Expr *getArgumentExpr() {
    assert(!isArgumentType() && "calling getArgumentExpr() when arg is type");
    return static_cast<Expr *>(Argument.Ex);
  }
  const Expr *getArgumentExpr() const {
    return const_cast<UnaryExprOrTypeTraitExpr *>(this)->getArgumentExpr();
  }

  void setArgument(Expr *E) {
    Argument.Ex = E;
    UnaryExprOrTypeTraitExprBits.IsType = false;
  }
  void setArgument(TypeSourceInfo *TInfo) {
    Argument.Ty = TInfo;
    UnaryExprOrTypeTraitExprBits.IsType = true;
  }

  QualType getTypeOfArgument() const {
    return isArgumentType() ? getArgumentType() : getArgumentExpr()->getType();
  }

  SourceLocation getOperatorLoc() const { return OpLoc; }
  void setOperatorLoc(SourceLocation L) { OpLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return OpLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == UnaryExprOrTypeTraitExprClass;
  }

  // Iterators
  child_range children();
  const_child_range children() const;
};

//===----------------------------------------------------------------------===//
// Postfix Operators.
//===----------------------------------------------------------------------===//

class ArraySubscriptExpr : public Expr {
  enum { LHS, RHS, END_EXPR };
  Stmt *SubExprs[END_EXPR];

  bool lhsIsBase() const { return getRHS()->getType()->isIntegerType(); }

public:
  ArraySubscriptExpr(Expr *lhs, Expr *rhs, QualType t, ExprValueKind VK,
                     ExprObjectKind OK, SourceLocation rbracketloc)
      : Expr(ArraySubscriptExprClass, t, VK, OK) {
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
    ArrayOrMatrixSubscriptExprBits.RBracketLoc = rbracketloc;
    setDependence(computeDependence(this));
  }

  explicit ArraySubscriptExpr(EmptyShell Shell)
      : Expr(ArraySubscriptExprClass, Shell) {}

  Expr *getLHS() { return cast<Expr>(SubExprs[LHS]); }
  const Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  void setLHS(Expr *E) { SubExprs[LHS] = E; }

  Expr *getRHS() { return cast<Expr>(SubExprs[RHS]); }
  const Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }
  void setRHS(Expr *E) { SubExprs[RHS] = E; }

  Expr *getBase() { return lhsIsBase() ? getLHS() : getRHS(); }
  const Expr *getBase() const { return lhsIsBase() ? getLHS() : getRHS(); }

  Expr *getIdx() { return lhsIsBase() ? getRHS() : getLHS(); }
  const Expr *getIdx() const { return lhsIsBase() ? getRHS() : getLHS(); }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getLHS()->getBeginLoc();
  }
  SourceLocation getEndLoc() const { return getRBracketLoc(); }

  SourceLocation getRBracketLoc() const {
    return ArrayOrMatrixSubscriptExprBits.RBracketLoc;
  }
  void setRBracketLoc(SourceLocation L) {
    ArrayOrMatrixSubscriptExprBits.RBracketLoc = L;
  }

  SourceLocation getExprLoc() const LLVM_READONLY {
    return getBase()->getExprLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ArraySubscriptExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
};

class MatrixSubscriptExpr : public Expr {
  enum { BASE, ROW_IDX, COLUMN_IDX, END_EXPR };
  Stmt *SubExprs[END_EXPR];

public:
  MatrixSubscriptExpr(Expr *Base, Expr *RowIdx, Expr *ColumnIdx, QualType T,
                      SourceLocation RBracketLoc)
      : Expr(MatrixSubscriptExprClass, T, Base->getValueKind(),
             OK_MatrixComponent) {
    SubExprs[BASE] = Base;
    SubExprs[ROW_IDX] = RowIdx;
    SubExprs[COLUMN_IDX] = ColumnIdx;
    ArrayOrMatrixSubscriptExprBits.RBracketLoc = RBracketLoc;
    setDependence(computeDependence(this));
  }

  explicit MatrixSubscriptExpr(EmptyShell Shell)
      : Expr(MatrixSubscriptExprClass, Shell) {}

  bool isIncomplete() const {
    bool IsIncomplete = hasPlaceholderType(BuiltinType::IncompleteMatrixIdx);
    assert((SubExprs[COLUMN_IDX] || IsIncomplete) &&
           "expressions without column index must be marked as incomplete");
    return IsIncomplete;
  }
  Expr *getBase() { return cast<Expr>(SubExprs[BASE]); }
  const Expr *getBase() const { return cast<Expr>(SubExprs[BASE]); }
  void setBase(Expr *E) { SubExprs[BASE] = E; }

  Expr *getRowIdx() { return cast<Expr>(SubExprs[ROW_IDX]); }
  const Expr *getRowIdx() const { return cast<Expr>(SubExprs[ROW_IDX]); }
  void setRowIdx(Expr *E) { SubExprs[ROW_IDX] = E; }

  Expr *getColumnIdx() { return cast_or_null<Expr>(SubExprs[COLUMN_IDX]); }
  const Expr *getColumnIdx() const {
    assert(!isIncomplete() &&
           "cannot get the column index of an incomplete expression");
    return cast<Expr>(SubExprs[COLUMN_IDX]);
  }
  void setColumnIdx(Expr *E) { SubExprs[COLUMN_IDX] = E; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getBase()->getBeginLoc();
  }

  SourceLocation getEndLoc() const { return getRBracketLoc(); }

  SourceLocation getExprLoc() const LLVM_READONLY {
    return getBase()->getExprLoc();
  }

  SourceLocation getRBracketLoc() const {
    return ArrayOrMatrixSubscriptExprBits.RBracketLoc;
  }
  void setRBracketLoc(SourceLocation L) {
    ArrayOrMatrixSubscriptExprBits.RBracketLoc = L;
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == MatrixSubscriptExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
};

class CallExpr : public Expr {
  enum { FN = 0, PREARGS_START = 1 };

  unsigned NumArgs;

  SourceLocation RParenLoc;

  // CallExpr store some data in trailing objects. However since CallExpr
  // is used a base of other expression classes we cannot use
  // llvm::TrailingObjects. Instead we manually perform the pointer arithmetic
  // and casts.
  //
  // The trailing objects are in order:
  //
  // * A single "Stmt *" for the callee expression.
  //
  // * An array of getNumPreArgs() "Stmt *" for the pre-argument expressions.
  //
  // * An array of getNumArgs() "Stmt *" for the argument expressions.
  //
  // * An optional of type FPOptionsOverride.
  //
  // Note that we store the offset in bytes from the this pointer to the start
  // of the trailing objects. It would be perfectly possible to compute it
  // based on the dynamic kind of the CallExpr. However 1.) we have plenty of
  // space in the bit-fields of Stmt. 2.) It was benchmarked to be faster to
  // compute this once and then load the offset from the bit-fields of Stmt,
  // instead of re-computing the offset each time the trailing objects are
  // accessed.

  Stmt **getTrailingStmts() {
    return reinterpret_cast<Stmt **>(reinterpret_cast<char *>(this) +
                                     CallExprBits.OffsetToTrailingObjects);
  }
  Stmt *const *getTrailingStmts() const {
    return const_cast<CallExpr *>(this)->getTrailingStmts();
  }

  static unsigned offsetToTrailingObjects(StmtClass SC);

  unsigned getSizeOfTrailingStmts() const {
    return (1 + getNumPreArgs() + getNumArgs()) * sizeof(Stmt *);
  }

  size_t getOffsetOfTrailingFPFeatures() const {
    assert(hasStoredFPFeatures());
    return CallExprBits.OffsetToTrailingObjects + getSizeOfTrailingStmts();
  }

protected:
  CallExpr(StmtClass SC, Expr *Fn, llvm::ArrayRef<Expr *> PreArgs,
           llvm::ArrayRef<Expr *> Args, QualType Ty, ExprValueKind VK,
           SourceLocation RParenLoc, FPOptionsOverride FPFeatures,
           unsigned MinNumArgs);

  CallExpr(StmtClass SC, unsigned NumPreArgs, unsigned NumArgs,
           bool hasFPFeatures, EmptyShell Empty);

  static unsigned sizeOfTrailingObjects(unsigned NumPreArgs, unsigned NumArgs,
                                        bool HasFPFeatures) {
    return (1 + NumPreArgs + NumArgs) * sizeof(Stmt *) +
           HasFPFeatures * sizeof(FPOptionsOverride);
  }

  Stmt *getPreArg(unsigned I) {
    assert(I < getNumPreArgs() && "Prearg access out of range!");
    return getTrailingStmts()[PREARGS_START + I];
  }
  const Stmt *getPreArg(unsigned I) const {
    assert(I < getNumPreArgs() && "Prearg access out of range!");
    return getTrailingStmts()[PREARGS_START + I];
  }
  void setPreArg(unsigned I, Stmt *PreArg) {
    assert(I < getNumPreArgs() && "Prearg access out of range!");
    getTrailingStmts()[PREARGS_START + I] = PreArg;
  }

  unsigned getNumPreArgs() const { return CallExprBits.NumPreArgs; }

  FPOptionsOverride *getTrailingFPFeatures() {
    assert(hasStoredFPFeatures());
    return reinterpret_cast<FPOptionsOverride *>(
        reinterpret_cast<char *>(this) + CallExprBits.OffsetToTrailingObjects +
        getSizeOfTrailingStmts());
  }
  const FPOptionsOverride *getTrailingFPFeatures() const {
    assert(hasStoredFPFeatures());
    return reinterpret_cast<const FPOptionsOverride *>(
        reinterpret_cast<const char *>(this) +
        CallExprBits.OffsetToTrailingObjects + getSizeOfTrailingStmts());
  }

public:
  static CallExpr *Create(const TreeContext &Ctx, Expr *Fn,
                          llvm::ArrayRef<Expr *> Args, QualType Ty,
                          ExprValueKind VK, SourceLocation RParenLoc,
                          FPOptionsOverride FPFeatures,
                          unsigned MinNumArgs = 0);

  static CallExpr *CreateTemporary(void *Mem, Expr *Fn, QualType Ty,
                                   ExprValueKind VK, SourceLocation RParenLoc);

  static CallExpr *CreateEmpty(const TreeContext &Ctx, unsigned NumArgs,
                               bool HasFPFeatures, EmptyShell Empty);

  Expr *getCallee() { return cast<Expr>(getTrailingStmts()[FN]); }
  const Expr *getCallee() const { return cast<Expr>(getTrailingStmts()[FN]); }
  void setCallee(Expr *F) { getTrailingStmts()[FN] = F; }

  bool hasStoredFPFeatures() const { return CallExprBits.HasFPFeatures; }

  Decl *getCalleeDecl() { return getCallee()->getReferencedDeclOfCallee(); }
  const Decl *getCalleeDecl() const {
    return getCallee()->getReferencedDeclOfCallee();
  }

  FunctionDecl *getDirectCallee() {
    return dyn_cast_or_null<FunctionDecl>(getCalleeDecl());
  }
  const FunctionDecl *getDirectCallee() const {
    return dyn_cast_or_null<FunctionDecl>(getCalleeDecl());
  }

  unsigned getNumArgs() const { return NumArgs; }

  Expr **getArgs() {
    return reinterpret_cast<Expr **>(getTrailingStmts() + PREARGS_START +
                                     getNumPreArgs());
  }
  const Expr *const *getArgs() const {
    return reinterpret_cast<const Expr *const *>(
        getTrailingStmts() + PREARGS_START + getNumPreArgs());
  }

  Expr *getArg(unsigned Arg) {
    assert(Arg < getNumArgs() && "Arg access out of range!");
    return getArgs()[Arg];
  }
  const Expr *getArg(unsigned Arg) const {
    assert(Arg < getNumArgs() && "Arg access out of range!");
    return getArgs()[Arg];
  }

  void setArg(unsigned Arg, Expr *ArgExpr) {
    assert(Arg < getNumArgs() && "Arg access out of range!");
    getArgs()[Arg] = ArgExpr;
  }

  void computeDependence() {
    setDependence(neverc::computeDependence(
        this, llvm::ArrayRef(
                  reinterpret_cast<Expr **>(getTrailingStmts() + PREARGS_START),
                  getNumPreArgs())));
  }

  void shrinkNumArgs(unsigned NewNumArgs) {
    assert((NewNumArgs <= getNumArgs()) &&
           "shrinkNumArgs cannot increase the number of arguments!");
    NumArgs = NewNumArgs;
  }

  void setNumArgsUnsafe(unsigned NewNumArgs) { NumArgs = NewNumArgs; }

  typedef ExprIterator arg_iterator;
  typedef ConstExprIterator const_arg_iterator;
  typedef llvm::iterator_range<arg_iterator> arg_range;
  typedef llvm::iterator_range<const_arg_iterator> const_arg_range;

  arg_range arguments() { return arg_range(arg_begin(), arg_end()); }
  const_arg_range arguments() const {
    return const_arg_range(arg_begin(), arg_end());
  }

  arg_iterator arg_begin() {
    return getTrailingStmts() + PREARGS_START + getNumPreArgs();
  }
  arg_iterator arg_end() { return arg_begin() + getNumArgs(); }

  const_arg_iterator arg_begin() const {
    return getTrailingStmts() + PREARGS_START + getNumPreArgs();
  }
  const_arg_iterator arg_end() const { return arg_begin() + getNumArgs(); }

  llvm::ArrayRef<Stmt *> getRawSubExprs() {
    return llvm::ArrayRef(getTrailingStmts(),
                          PREARGS_START + getNumPreArgs() + getNumArgs());
  }

  FPOptionsOverride getStoredFPFeatures() const {
    assert(hasStoredFPFeatures());
    return *getTrailingFPFeatures();
  }
  void setStoredFPFeatures(FPOptionsOverride F) {
    assert(hasStoredFPFeatures());
    *getTrailingFPFeatures() = F;
  }

  FPOptions getFPFeaturesInEffect(const LangOptions &LO) const {
    if (hasStoredFPFeatures())
      return getStoredFPFeatures().applyOverrides(LO);
    return FPOptions::defaultWithoutTrailingStorage(LO);
  }

  FPOptionsOverride getFPFeatures() const {
    if (hasStoredFPFeatures())
      return getStoredFPFeatures();
    return FPOptionsOverride();
  }

  unsigned getBuiltinCallee() const;

  bool isUnevaluatedBuiltinCall(const TreeContext &Ctx) const;

  QualType getCallReturnType(const TreeContext &Ctx) const;

  const Attr *getUnusedResultAttr(const TreeContext &Ctx) const;

  bool hasUnusedResultAttr(const TreeContext &Ctx) const {
    return getUnusedResultAttr(Ctx) != nullptr;
  }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  bool isBuiltinAssumeFalse(const TreeContext &Ctx) const;

  void markDependentForPostponedNameLookup() {
    setDependence(getDependence() | ExprDependence::TypeValueInstantiation);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CallExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(getTrailingStmts(), getTrailingStmts() + PREARGS_START +
                                               getNumPreArgs() + getNumArgs());
  }

  const_child_range children() const {
    return const_child_range(getTrailingStmts(),
                             getTrailingStmts() + PREARGS_START +
                                 getNumPreArgs() + getNumArgs());
  }
};

struct MemberExprNameQualifier {
  NamedDecl *FoundDecl;
};

class MemberExpr final
    : public Expr,
      private llvm::TrailingObjects<MemberExpr, MemberExprNameQualifier> {

  friend TrailingObjects;

  Stmt *Base;
  ValueDecl *MemberDecl;
  SourceLocation MemberLoc;

  size_t numTrailingObjects(OverloadToken<MemberExprNameQualifier>) const {
    return hasFoundDecl();
  }

  bool hasFoundDecl() const { return MemberExprBits.HasQualifierOrFoundDecl; }

  MemberExpr(Expr *Base, bool IsArrow, SourceLocation OperatorLoc,
             ValueDecl *MemberDecl, const DeclarationNameInfo &NameInfo,
             QualType T, ExprValueKind VK, ExprObjectKind OK,
             NonOdrUseReason NOUR);
  MemberExpr(EmptyShell Empty)
      : Expr(MemberExprClass, Empty), Base(), MemberDecl() {}

public:
  static MemberExpr *Create(const TreeContext &C, Expr *Base, bool IsArrow,
                            SourceLocation OperatorLoc, ValueDecl *MemberDecl,
                            NamedDecl *FoundDecl,
                            DeclarationNameInfo MemberNameInfo, QualType T,
                            ExprValueKind VK, ExprObjectKind OK,
                            NonOdrUseReason NOUR);

  static MemberExpr *CreateImplicit(const TreeContext &C, Expr *Base,
                                    bool IsArrow, ValueDecl *MemberDecl,
                                    QualType T, ExprValueKind VK,
                                    ExprObjectKind OK) {
    return Create(C, Base, IsArrow, SourceLocation(), MemberDecl, MemberDecl,
                  DeclarationNameInfo(), T, VK, OK, NOUR_None);
  }

  static MemberExpr *CreateEmpty(const TreeContext &Context, bool HasFoundDecl);

  void setBase(Expr *E) { Base = E; }
  Expr *getBase() const { return cast<Expr>(Base); }

  ValueDecl *getMemberDecl() const { return MemberDecl; }
  void setMemberDecl(ValueDecl *D);

  NamedDecl *getFoundDecl() const {
    if (!hasFoundDecl())
      return getMemberDecl();
    return getTrailingObjects<MemberExprNameQualifier>()->FoundDecl;
  }

  DeclarationNameInfo getMemberNameInfo() const {
    return DeclarationNameInfo(MemberDecl->getDeclName(), MemberLoc);
  }

  SourceLocation getOperatorLoc() const { return MemberExprBits.OperatorLoc; }

  bool isArrow() const { return MemberExprBits.IsArrow; }
  void setArrow(bool A) { MemberExprBits.IsArrow = A; }

  SourceLocation getMemberLoc() const { return MemberLoc; }
  void setMemberLoc(SourceLocation L) { MemberLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  SourceLocation getExprLoc() const LLVM_READONLY { return MemberLoc; }

  bool hadMultipleCandidates() const {
    return MemberExprBits.HadMultipleCandidates;
  }
  void setHadMultipleCandidates(bool V = true) {
    MemberExprBits.HadMultipleCandidates = V;
  }

  NonOdrUseReason isNonOdrUse() const {
    return static_cast<NonOdrUseReason>(MemberExprBits.NonOdrUseReason);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == MemberExprClass;
  }

  // Iterators
  child_range children() { return child_range(&Base, &Base + 1); }
  const_child_range children() const {
    return const_child_range(&Base, &Base + 1);
  }
};

class CompoundLiteralExpr : public Expr {
  SourceLocation LParenLoc;

  llvm::PointerIntPair<TypeSourceInfo *, 1, bool> TInfoAndScope;
  Stmt *Init;

public:
  CompoundLiteralExpr(SourceLocation lparenloc, TypeSourceInfo *tinfo,
                      QualType T, ExprValueKind VK, Expr *init, bool fileScope)
      : Expr(CompoundLiteralExprClass, T, VK, OK_Ordinary),
        LParenLoc(lparenloc), TInfoAndScope(tinfo, fileScope), Init(init) {
    setDependence(computeDependence(this));
  }

  explicit CompoundLiteralExpr(EmptyShell Empty)
      : Expr(CompoundLiteralExprClass, Empty) {}

  const Expr *getInitializer() const { return cast<Expr>(Init); }
  Expr *getInitializer() { return cast<Expr>(Init); }
  void setInitializer(Expr *E) { Init = E; }

  bool isFileScope() const { return TInfoAndScope.getInt(); }
  void setFileScope(bool FS) { TInfoAndScope.setInt(FS); }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation L) { LParenLoc = L; }

  TypeSourceInfo *getTypeSourceInfo() const {
    return TInfoAndScope.getPointer();
  }
  void setTypeSourceInfo(TypeSourceInfo *tinfo) {
    TInfoAndScope.setPointer(tinfo);
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    if (!Init)
      return SourceLocation();
    if (LParenLoc.isInvalid())
      return Init->getBeginLoc();
    return LParenLoc;
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    if (!Init)
      return SourceLocation();
    return Init->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CompoundLiteralExprClass;
  }

  // Iterators
  child_range children() { return child_range(&Init, &Init + 1); }
  const_child_range children() const {
    return const_child_range(&Init, &Init + 1);
  }
};

class CastExpr : public Expr {
  Stmt *Op;

  bool CastConsistency() const;

protected:
  CastExpr(StmtClass SC, QualType ty, ExprValueKind VK, const CastKind kind,
           Expr *op, bool HasFPFeatures)
      : Expr(SC, ty, VK, OK_Ordinary), Op(op) {
    CastExprBits.Kind = kind;
    CastExprBits.PartOfExplicitCast = false;
    assert(CastConsistency());
    CastExprBits.HasFPFeatures = HasFPFeatures;
  }

  CastExpr(StmtClass SC, EmptyShell Empty, bool HasFPFeatures)
      : Expr(SC, Empty) {
    CastExprBits.PartOfExplicitCast = false;
    CastExprBits.HasFPFeatures = HasFPFeatures;
  }

  FPOptionsOverride *getTrailingFPFeatures();
  const FPOptionsOverride *getTrailingFPFeatures() const {
    return const_cast<CastExpr *>(this)->getTrailingFPFeatures();
  }

public:
  CastKind getCastKind() const { return (CastKind)CastExprBits.Kind; }
  void setCastKind(CastKind K) { CastExprBits.Kind = K; }

  static const char *getCastKindName(CastKind CK);
  const char *getCastKindName() const { return getCastKindName(getCastKind()); }

  Expr *getSubExpr() { return cast<Expr>(Op); }
  const Expr *getSubExpr() const { return cast<Expr>(Op); }
  void setSubExpr(Expr *E) { Op = E; }

  Expr *getSubExprAsWritten();
  const Expr *getSubExprAsWritten() const {
    return const_cast<CastExpr *>(this)->getSubExprAsWritten();
  }

  const FieldDecl *getTargetUnionField() const {
    assert(getCastKind() == CK_ToUnion);
    return getTargetFieldForToUnionCast(getType(), getSubExpr()->getType());
  }

  bool hasStoredFPFeatures() const { return CastExprBits.HasFPFeatures; }

  FPOptionsOverride getStoredFPFeatures() const {
    assert(hasStoredFPFeatures());
    return *getTrailingFPFeatures();
  }

  FPOptions getFPFeaturesInEffect(const LangOptions &LO) const {
    if (hasStoredFPFeatures())
      return getStoredFPFeatures().applyOverrides(LO);
    return FPOptions::defaultWithoutTrailingStorage(LO);
  }

  FPOptionsOverride getFPFeatures() const {
    if (hasStoredFPFeatures())
      return getStoredFPFeatures();
    return FPOptionsOverride();
  }

  //  True : if this conversion changes the volatile-ness of an lvalue.
  //         Qualification conversions on lvalues currently use CK_NoOp, but
  //         it's important to recognize volatile-changing conversions in
  //         clients code generation that normally eagerly peephole loads. Note
  //         that the query is answering for this specific node; Sema may
  //         produce multiple cast nodes for any particular conversion sequence.
  //  False : Otherwise.
  bool changesVolatileQualification() const {
    return (isLValue() && (getType().isVolatileQualified() !=
                           getSubExpr()->getType().isVolatileQualified()));
  }

  static const FieldDecl *getTargetFieldForToUnionCast(QualType unionType,
                                                       QualType opType);
  static const FieldDecl *getTargetFieldForToUnionCast(const RecordDecl *RD,
                                                       QualType opType);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstCastExprConstant &&
           T->getStmtClass() <= lastCastExprConstant;
  }

  // Iterators
  child_range children() { return child_range(&Op, &Op + 1); }
  const_child_range children() const { return const_child_range(&Op, &Op + 1); }
};

class ImplicitCastExpr final
    : public CastExpr,
      private llvm::TrailingObjects<ImplicitCastExpr, FPOptionsOverride> {

  ImplicitCastExpr(QualType ty, CastKind kind, Expr *op, FPOptionsOverride FPO,
                   ExprValueKind VK)
      : CastExpr(ImplicitCastExprClass, ty, VK, kind, op,
                 FPO.requiresTrailingStorage()) {
    setDependence(computeDependence(this));
    if (hasStoredFPFeatures())
      *getTrailingFPFeatures() = FPO;
  }

  explicit ImplicitCastExpr(EmptyShell Shell, bool HasFPFeatures)
      : CastExpr(ImplicitCastExprClass, Shell, HasFPFeatures) {}

public:
  enum OnStack_t { OnStack };
  ImplicitCastExpr(OnStack_t _, QualType ty, CastKind kind, Expr *op,
                   ExprValueKind VK, FPOptionsOverride FPO)
      : CastExpr(ImplicitCastExprClass, ty, VK, kind, op,
                 FPO.requiresTrailingStorage()) {
    if (hasStoredFPFeatures())
      *getTrailingFPFeatures() = FPO;
  }

  bool isPartOfExplicitCast() const { return CastExprBits.PartOfExplicitCast; }
  void setIsPartOfExplicitCast(bool PartOfExplicitCast) {
    CastExprBits.PartOfExplicitCast = PartOfExplicitCast;
  }

  static ImplicitCastExpr *Create(const TreeContext &Context, QualType T,
                                  CastKind Kind, Expr *Operand,
                                  ExprValueKind Cat, FPOptionsOverride FPO);

  static ImplicitCastExpr *CreateEmpty(const TreeContext &Context,
                                       bool HasFPFeatures);

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getSubExpr()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getSubExpr()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ImplicitCastExprClass;
  }

  friend TrailingObjects;
  friend class CastExpr;
};

class ExplicitCastExpr : public CastExpr {
  TypeSourceInfo *TInfo;

protected:
  ExplicitCastExpr(StmtClass SC, QualType exprTy, ExprValueKind VK,
                   CastKind kind, Expr *op, bool HasFPFeatures,
                   TypeSourceInfo *writtenTy)
      : CastExpr(SC, exprTy, VK, kind, op, HasFPFeatures), TInfo(writtenTy) {
    setDependence(computeDependence(this));
  }

  ExplicitCastExpr(StmtClass SC, EmptyShell Shell, bool HasFPFeatures)
      : CastExpr(SC, Shell, HasFPFeatures) {}

public:
  TypeSourceInfo *getTypeInfoAsWritten() const { return TInfo; }
  void setTypeInfoAsWritten(TypeSourceInfo *writtenTy) { TInfo = writtenTy; }

  QualType getTypeAsWritten() const { return TInfo->getType(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstExplicitCastExprConstant &&
           T->getStmtClass() <= lastExplicitCastExprConstant;
  }
};

class CStyleCastExpr final
    : public ExplicitCastExpr,
      private llvm::TrailingObjects<CStyleCastExpr, FPOptionsOverride> {
  SourceLocation LPLoc;
  SourceLocation RPLoc;

  CStyleCastExpr(QualType exprTy, ExprValueKind vk, CastKind kind, Expr *op,
                 FPOptionsOverride FPO, TypeSourceInfo *writtenTy,
                 SourceLocation l, SourceLocation r)
      : ExplicitCastExpr(CStyleCastExprClass, exprTy, vk, kind, op,
                         FPO.requiresTrailingStorage(), writtenTy),
        LPLoc(l), RPLoc(r) {
    if (hasStoredFPFeatures())
      *getTrailingFPFeatures() = FPO;
  }

  explicit CStyleCastExpr(EmptyShell Shell, bool HasFPFeatures)
      : ExplicitCastExpr(CStyleCastExprClass, Shell, HasFPFeatures) {}

public:
  static CStyleCastExpr *Create(const TreeContext &Context, QualType T,
                                ExprValueKind VK, CastKind K, Expr *Op,
                                FPOptionsOverride FPO,
                                TypeSourceInfo *WrittenTy, SourceLocation L,
                                SourceLocation R);

  static CStyleCastExpr *CreateEmpty(const TreeContext &Context,
                                     bool HasFPFeatures);

  SourceLocation getLParenLoc() const { return LPLoc; }
  void setLParenLoc(SourceLocation L) { LPLoc = L; }

  SourceLocation getRParenLoc() const { return RPLoc; }
  void setRParenLoc(SourceLocation L) { RPLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LPLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getSubExpr()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CStyleCastExprClass;
  }

  friend TrailingObjects;
  friend class CastExpr;
};

class BinaryOperator : public Expr {
  enum { LHS, RHS, END_EXPR };
  Stmt *SubExprs[END_EXPR];

public:
  typedef BinaryOperatorKind Opcode;

protected:
  size_t offsetOfTrailingStorage() const;

  FPOptionsOverride *getTrailingFPFeatures() {
    assert(BinaryOperatorBits.HasFPFeatures);
    return reinterpret_cast<FPOptionsOverride *>(
        reinterpret_cast<char *>(this) + offsetOfTrailingStorage());
  }
  const FPOptionsOverride *getTrailingFPFeatures() const {
    assert(BinaryOperatorBits.HasFPFeatures);
    return reinterpret_cast<const FPOptionsOverride *>(
        reinterpret_cast<const char *>(this) + offsetOfTrailingStorage());
  }

  BinaryOperator(const TreeContext &Ctx, Expr *lhs, Expr *rhs, Opcode opc,
                 QualType ResTy, ExprValueKind VK, ExprObjectKind OK,
                 SourceLocation opLoc, FPOptionsOverride FPFeatures);

  explicit BinaryOperator(EmptyShell Empty) : Expr(BinaryOperatorClass, Empty) {
    BinaryOperatorBits.Opc = BO_Comma;
  }

public:
  static BinaryOperator *CreateEmpty(const TreeContext &C, bool hasFPFeatures);

  static BinaryOperator *Create(const TreeContext &C, Expr *lhs, Expr *rhs,
                                Opcode opc, QualType ResTy, ExprValueKind VK,
                                ExprObjectKind OK, SourceLocation opLoc,
                                FPOptionsOverride FPFeatures);
  SourceLocation getExprLoc() const { return getOperatorLoc(); }
  SourceLocation getOperatorLoc() const { return BinaryOperatorBits.OpLoc; }
  void setOperatorLoc(SourceLocation L) { BinaryOperatorBits.OpLoc = L; }

  Opcode getOpcode() const {
    return static_cast<Opcode>(BinaryOperatorBits.Opc);
  }
  void setOpcode(Opcode Opc) { BinaryOperatorBits.Opc = Opc; }

  Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  void setLHS(Expr *E) { SubExprs[LHS] = E; }
  Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }
  void setRHS(Expr *E) { SubExprs[RHS] = E; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getLHS()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getRHS()->getEndLoc();
  }

  static llvm::StringRef getOpcodeStr(Opcode Op);

  llvm::StringRef getOpcodeStr() const { return getOpcodeStr(getOpcode()); }

  static bool isMultiplicativeOp(Opcode Opc) {
    return Opc >= BO_Mul && Opc <= BO_Rem;
  }
  bool isMultiplicativeOp() const { return isMultiplicativeOp(getOpcode()); }
  static bool isAdditiveOp(Opcode Opc) {
    return Opc == BO_Add || Opc == BO_Sub;
  }
  bool isAdditiveOp() const { return isAdditiveOp(getOpcode()); }
  static bool isShiftOp(Opcode Opc) { return Opc == BO_Shl || Opc == BO_Shr; }
  bool isShiftOp() const { return isShiftOp(getOpcode()); }

  static bool isBitwiseOp(Opcode Opc) { return Opc >= BO_And && Opc <= BO_Or; }
  bool isBitwiseOp() const { return isBitwiseOp(getOpcode()); }

  static bool isRelationalOp(Opcode Opc) {
    return Opc >= BO_LT && Opc <= BO_GE;
  }
  bool isRelationalOp() const { return isRelationalOp(getOpcode()); }

  static bool isEqualityOp(Opcode Opc) { return Opc == BO_EQ || Opc == BO_NE; }
  bool isEqualityOp() const { return isEqualityOp(getOpcode()); }

  static bool isComparisonOp(Opcode Opc) {
    return Opc >= BO_LT && Opc <= BO_NE;
  }
  bool isComparisonOp() const { return isComparisonOp(getOpcode()); }

  static bool isCommaOp(Opcode Opc) { return Opc == BO_Comma; }
  bool isCommaOp() const { return isCommaOp(getOpcode()); }

  static Opcode negateComparisonOp(Opcode Opc) {
    switch (Opc) {
    default:
      llvm_unreachable("Not a comparison operator.");
    case BO_LT:
      return BO_GE;
    case BO_GT:
      return BO_LE;
    case BO_LE:
      return BO_GT;
    case BO_GE:
      return BO_LT;
    case BO_EQ:
      return BO_NE;
    case BO_NE:
      return BO_EQ;
    }
  }

  static Opcode reverseComparisonOp(Opcode Opc) {
    switch (Opc) {
    default:
      llvm_unreachable("Not a comparison operator.");
    case BO_LT:
      return BO_GT;
    case BO_GT:
      return BO_LT;
    case BO_LE:
      return BO_GE;
    case BO_GE:
      return BO_LE;
    case BO_EQ:
    case BO_NE:
      return Opc;
    }
  }

  static bool isLogicalOp(Opcode Opc) {
    return Opc == BO_LAnd || Opc == BO_LOr;
  }
  bool isLogicalOp() const { return isLogicalOp(getOpcode()); }

  static bool isAssignmentOp(Opcode Opc) {
    return Opc >= BO_Assign && Opc <= BO_OrAssign;
  }
  bool isAssignmentOp() const { return isAssignmentOp(getOpcode()); }

  static bool isCompoundAssignmentOp(Opcode Opc) {
    return Opc > BO_Assign && Opc <= BO_OrAssign;
  }
  bool isCompoundAssignmentOp() const {
    return isCompoundAssignmentOp(getOpcode());
  }
  static Opcode getOpForCompoundAssignment(Opcode Opc) {
    assert(isCompoundAssignmentOp(Opc));
    if (Opc >= BO_AndAssign)
      return Opcode(unsigned(Opc) - BO_AndAssign + BO_And);
    else
      return Opcode(unsigned(Opc) - BO_MulAssign + BO_Mul);
  }

  static bool isShiftAssignOp(Opcode Opc) {
    return Opc == BO_ShlAssign || Opc == BO_ShrAssign;
  }
  bool isShiftAssignOp() const { return isShiftAssignOp(getOpcode()); }

  static bool isNullPointerArithmeticExtension(TreeContext &Ctx, Opcode Opc,
                                               const Expr *LHS,
                                               const Expr *RHS);

  static bool classof(const Stmt *S) {
    return S->getStmtClass() >= firstBinaryOperatorConstant &&
           S->getStmtClass() <= lastBinaryOperatorConstant;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }

  void setHasStoredFPFeatures(bool B) { BinaryOperatorBits.HasFPFeatures = B; }
  bool hasStoredFPFeatures() const { return BinaryOperatorBits.HasFPFeatures; }

  FPOptionsOverride getStoredFPFeatures() const {
    assert(hasStoredFPFeatures());
    return *getTrailingFPFeatures();
  }
  void setStoredFPFeatures(FPOptionsOverride F) {
    assert(BinaryOperatorBits.HasFPFeatures);
    *getTrailingFPFeatures() = F;
  }

  FPOptions getFPFeaturesInEffect(const LangOptions &LO) const {
    if (BinaryOperatorBits.HasFPFeatures)
      return getStoredFPFeatures().applyOverrides(LO);
    return FPOptions::defaultWithoutTrailingStorage(LO);
  }

  FPOptionsOverride getFPFeatures() const {
    if (BinaryOperatorBits.HasFPFeatures)
      return getStoredFPFeatures();
    return FPOptionsOverride();
  }

  bool isFPContractableWithinStatement(const LangOptions &LO) const {
    return getFPFeaturesInEffect(LO).allowFPContractWithinStatement();
  }

  bool isFEnvAccessOn(const LangOptions &LO) const {
    return getFPFeaturesInEffect(LO).getAllowFEnvAccess();
  }

protected:
  BinaryOperator(const TreeContext &Ctx, Expr *lhs, Expr *rhs, Opcode opc,
                 QualType ResTy, ExprValueKind VK, ExprObjectKind OK,
                 SourceLocation opLoc, FPOptionsOverride FPFeatures,
                 bool IsCompoundAssign);

  BinaryOperator(StmtClass SC, EmptyShell Empty) : Expr(SC, Empty) {
    BinaryOperatorBits.Opc = BO_MulAssign;
  }

  static unsigned sizeOfTrailingObjects(bool HasFPFeatures) {
    return HasFPFeatures * sizeof(FPOptionsOverride);
  }
};

class CompoundAssignOperator : public BinaryOperator {
  QualType ComputationLHSType;
  QualType ComputationResultType;

  explicit CompoundAssignOperator(const TreeContext &C, EmptyShell Empty,
                                  bool hasFPFeatures)
      : BinaryOperator(CompoundAssignOperatorClass, Empty) {}

protected:
  CompoundAssignOperator(const TreeContext &C, Expr *lhs, Expr *rhs, Opcode opc,
                         QualType ResType, ExprValueKind VK, ExprObjectKind OK,
                         SourceLocation OpLoc, FPOptionsOverride FPFeatures,
                         QualType CompLHSType, QualType CompResultType)
      : BinaryOperator(C, lhs, rhs, opc, ResType, VK, OK, OpLoc, FPFeatures,
                       true),
        ComputationLHSType(CompLHSType), ComputationResultType(CompResultType) {
    assert(isCompoundAssignmentOp() &&
           "Only should be used for compound assignments");
  }

public:
  static CompoundAssignOperator *CreateEmpty(const TreeContext &C,
                                             bool hasFPFeatures);

  static CompoundAssignOperator *
  Create(const TreeContext &C, Expr *lhs, Expr *rhs, Opcode opc, QualType ResTy,
         ExprValueKind VK, ExprObjectKind OK, SourceLocation opLoc,
         FPOptionsOverride FPFeatures, QualType CompLHSType = QualType(),
         QualType CompResultType = QualType());

  // The two computation types are the type the LHS is converted
  // to for the computation and the type of the result; the two are
  // distinct in a few cases (specifically, int+=ptr and ptr-=ptr).
  QualType getComputationLHSType() const { return ComputationLHSType; }
  void setComputationLHSType(QualType T) { ComputationLHSType = T; }

  QualType getComputationResultType() const { return ComputationResultType; }
  void setComputationResultType(QualType T) { ComputationResultType = T; }

  static bool classof(const Stmt *S) {
    return S->getStmtClass() == CompoundAssignOperatorClass;
  }
};

inline size_t BinaryOperator::offsetOfTrailingStorage() const {
  assert(BinaryOperatorBits.HasFPFeatures);
  return isa<CompoundAssignOperator>(this) ? sizeof(CompoundAssignOperator)
                                           : sizeof(BinaryOperator);
}

class AbstractConditionalOperator : public Expr {
  SourceLocation QuestionLoc, ColonLoc;

protected:
  AbstractConditionalOperator(StmtClass SC, QualType T, ExprValueKind VK,
                              ExprObjectKind OK, SourceLocation qloc,
                              SourceLocation cloc)
      : Expr(SC, T, VK, OK), QuestionLoc(qloc), ColonLoc(cloc) {}

  AbstractConditionalOperator(StmtClass SC, EmptyShell Empty)
      : Expr(SC, Empty) {}

public:
  Expr *getCond() const;

  Expr *getTrueExpr() const;

  Expr *getFalseExpr() const;

  SourceLocation getQuestionLoc() const { return QuestionLoc; }
  SourceLocation getColonLoc() const { return ColonLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ConditionalOperatorClass ||
           T->getStmtClass() == BinaryConditionalOperatorClass;
  }
};

class ConditionalOperator : public AbstractConditionalOperator {
  enum { COND, LHS, RHS, END_EXPR };
  Stmt *SubExprs[END_EXPR]; // Left/Middle/Right hand sides.

public:
  ConditionalOperator(Expr *cond, SourceLocation QLoc, Expr *lhs,
                      SourceLocation CLoc, Expr *rhs, QualType t,
                      ExprValueKind VK, ExprObjectKind OK)
      : AbstractConditionalOperator(ConditionalOperatorClass, t, VK, OK, QLoc,
                                    CLoc) {
    SubExprs[COND] = cond;
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
    setDependence(computeDependence(this));
  }

  explicit ConditionalOperator(EmptyShell Empty)
      : AbstractConditionalOperator(ConditionalOperatorClass, Empty) {}

  Expr *getCond() const { return cast<Expr>(SubExprs[COND]); }

  Expr *getTrueExpr() const { return cast<Expr>(SubExprs[LHS]); }

  Expr *getFalseExpr() const { return cast<Expr>(SubExprs[RHS]); }

  Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getCond()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getRHS()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ConditionalOperatorClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
};

class BinaryConditionalOperator : public AbstractConditionalOperator {
  enum { COMMON, COND, LHS, RHS, NUM_SUBEXPRS };

  Stmt *SubExprs[NUM_SUBEXPRS];
  OpaqueValueExpr *OpaqueValue;

public:
  BinaryConditionalOperator(Expr *common, OpaqueValueExpr *opaqueValue,
                            Expr *cond, Expr *lhs, Expr *rhs,
                            SourceLocation qloc, SourceLocation cloc,
                            QualType t, ExprValueKind VK, ExprObjectKind OK)
      : AbstractConditionalOperator(BinaryConditionalOperatorClass, t, VK, OK,
                                    qloc, cloc),
        OpaqueValue(opaqueValue) {
    SubExprs[COMMON] = common;
    SubExprs[COND] = cond;
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
    assert(OpaqueValue->getSourceExpr() == common && "Wrong opaque value");
    setDependence(computeDependence(this));
  }

  explicit BinaryConditionalOperator(EmptyShell Empty)
      : AbstractConditionalOperator(BinaryConditionalOperatorClass, Empty) {}

  Expr *getCommon() const { return cast<Expr>(SubExprs[COMMON]); }

  OpaqueValueExpr *getOpaqueValue() const { return OpaqueValue; }

  Expr *getCond() const { return cast<Expr>(SubExprs[COND]); }

  Expr *getTrueExpr() const { return cast<Expr>(SubExprs[LHS]); }

  Expr *getFalseExpr() const { return cast<Expr>(SubExprs[RHS]); }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getCommon()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getFalseExpr()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == BinaryConditionalOperatorClass;
  }

  // Iterators
  child_range children() {
    return child_range(SubExprs, SubExprs + NUM_SUBEXPRS);
  }
  const_child_range children() const {
    return const_child_range(SubExprs, SubExprs + NUM_SUBEXPRS);
  }
};

inline Expr *AbstractConditionalOperator::getCond() const {
  if (const ConditionalOperator *co = dyn_cast<ConditionalOperator>(this))
    return co->getCond();
  return cast<BinaryConditionalOperator>(this)->getCond();
}

inline Expr *AbstractConditionalOperator::getTrueExpr() const {
  if (const ConditionalOperator *co = dyn_cast<ConditionalOperator>(this))
    return co->getTrueExpr();
  return cast<BinaryConditionalOperator>(this)->getTrueExpr();
}

inline Expr *AbstractConditionalOperator::getFalseExpr() const {
  if (const ConditionalOperator *co = dyn_cast<ConditionalOperator>(this))
    return co->getFalseExpr();
  return cast<BinaryConditionalOperator>(this)->getFalseExpr();
}

class AddrLabelExpr : public Expr {
  SourceLocation AmpAmpLoc, LabelLoc;
  LabelDecl *Label;

public:
  AddrLabelExpr(SourceLocation AALoc, SourceLocation LLoc, LabelDecl *L,
                QualType t)
      : Expr(AddrLabelExprClass, t, VK_PRValue, OK_Ordinary), AmpAmpLoc(AALoc),
        LabelLoc(LLoc), Label(L) {
    setDependence(ExprDependence::None);
  }

  explicit AddrLabelExpr(EmptyShell Empty) : Expr(AddrLabelExprClass, Empty) {}

  SourceLocation getAmpAmpLoc() const { return AmpAmpLoc; }
  void setAmpAmpLoc(SourceLocation L) { AmpAmpLoc = L; }
  SourceLocation getLabelLoc() const { return LabelLoc; }
  void setLabelLoc(SourceLocation L) { LabelLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return AmpAmpLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return LabelLoc; }

  LabelDecl *getLabel() const { return Label; }
  void setLabel(LabelDecl *L) { Label = L; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == AddrLabelExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class StmtExpr : public Expr {
  Stmt *SubStmt;
  SourceLocation LParenLoc, RParenLoc;

public:
  StmtExpr(CompoundStmt *SubStmt, QualType T, SourceLocation LParenLoc,
           SourceLocation RParenLoc)
      : Expr(StmtExprClass, T, VK_PRValue, OK_Ordinary), SubStmt(SubStmt),
        LParenLoc(LParenLoc), RParenLoc(RParenLoc) {
    setDependence(computeDependence(this));
  }

  explicit StmtExpr(EmptyShell Empty) : Expr(StmtExprClass, Empty) {}

  CompoundStmt *getSubStmt() { return cast<CompoundStmt>(SubStmt); }
  const CompoundStmt *getSubStmt() const { return cast<CompoundStmt>(SubStmt); }
  void setSubStmt(CompoundStmt *S) { SubStmt = S; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LParenLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation L) { LParenLoc = L; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == StmtExprClass;
  }

  // Iterators
  child_range children() { return child_range(&SubStmt, &SubStmt + 1); }
  const_child_range children() const {
    return const_child_range(&SubStmt, &SubStmt + 1);
  }
};

class ShuffleVectorExpr : public Expr {
  SourceLocation BuiltinLoc, RParenLoc;

  // SubExprs - the list of values passed to the __builtin_shufflevector
  // function. The first two are vectors, and the rest are constant
  // indices.  The number of values in this list is always
  // 2+the number of indices in the vector type.
  Stmt **SubExprs;
  unsigned NumExprs;

public:
  ShuffleVectorExpr(const TreeContext &C, llvm::ArrayRef<Expr *> args,
                    QualType Type, SourceLocation BLoc, SourceLocation RP);

  explicit ShuffleVectorExpr(EmptyShell Empty)
      : Expr(ShuffleVectorExprClass, Empty), SubExprs(nullptr) {}

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  void setBuiltinLoc(SourceLocation L) { BuiltinLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return BuiltinLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ShuffleVectorExprClass;
  }

  unsigned getNumSubExprs() const { return NumExprs; }

  Expr **getSubExprs() { return reinterpret_cast<Expr **>(SubExprs); }

  Expr *getExpr(unsigned Index) {
    assert((Index < NumExprs) && "Arg access out of range!");
    return cast<Expr>(SubExprs[Index]);
  }
  const Expr *getExpr(unsigned Index) const {
    assert((Index < NumExprs) && "Arg access out of range!");
    return cast<Expr>(SubExprs[Index]);
  }

  void setExprs(const TreeContext &C, llvm::ArrayRef<Expr *> Exprs);

  llvm::APSInt getShuffleMaskIdx(const TreeContext &Ctx, unsigned N) const {
    assert((N < NumExprs - 2) && "Shuffle idx out of range!");
    return getExpr(N + 2)->EvaluateKnownConstInt(Ctx);
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0] + NumExprs);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + NumExprs);
  }
};

class ConvertVectorExpr : public Expr {
private:
  Stmt *SrcExpr;
  TypeSourceInfo *TInfo;
  SourceLocation BuiltinLoc, RParenLoc;

  explicit ConvertVectorExpr(EmptyShell Empty)
      : Expr(ConvertVectorExprClass, Empty) {}

public:
  ConvertVectorExpr(Expr *SrcExpr, TypeSourceInfo *TI, QualType DstType,
                    ExprValueKind VK, ExprObjectKind OK,
                    SourceLocation BuiltinLoc, SourceLocation RParenLoc)
      : Expr(ConvertVectorExprClass, DstType, VK, OK), SrcExpr(SrcExpr),
        TInfo(TI), BuiltinLoc(BuiltinLoc), RParenLoc(RParenLoc) {
    setDependence(computeDependence(this));
  }

  Expr *getSrcExpr() const { return cast<Expr>(SrcExpr); }

  TypeSourceInfo *getTypeSourceInfo() const { return TInfo; }
  void setTypeSourceInfo(TypeSourceInfo *ti) { TInfo = ti; }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }

  SourceLocation getRParenLoc() const { return RParenLoc; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return BuiltinLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ConvertVectorExprClass;
  }

  // Iterators
  child_range children() { return child_range(&SrcExpr, &SrcExpr + 1); }
  const_child_range children() const {
    return const_child_range(&SrcExpr, &SrcExpr + 1);
  }
};

class ChooseExpr : public Expr {
  enum { COND, LHS, RHS, END_EXPR };
  Stmt *SubExprs[END_EXPR]; // Left/Middle/Right hand sides.
  SourceLocation BuiltinLoc, RParenLoc;
  bool CondIsTrue;

public:
  ChooseExpr(SourceLocation BLoc, Expr *cond, Expr *lhs, Expr *rhs, QualType t,
             ExprValueKind VK, ExprObjectKind OK, SourceLocation RP,
             bool condIsTrue)
      : Expr(ChooseExprClass, t, VK, OK), BuiltinLoc(BLoc), RParenLoc(RP),
        CondIsTrue(condIsTrue) {
    SubExprs[COND] = cond;
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;

    setDependence(computeDependence(this));
  }

  explicit ChooseExpr(EmptyShell Empty) : Expr(ChooseExprClass, Empty) {}

  bool isConditionTrue() const {
    assert(!isConditionDependent() &&
           "Dependent condition isn't true or false");
    return CondIsTrue;
  }
  void setIsConditionTrue(bool isTrue) { CondIsTrue = isTrue; }

  bool isConditionDependent() const {
    return getCond()->isTypeDependent() || getCond()->isValueDependent();
  }

  Expr *getChosenSubExpr() const {
    return isConditionTrue() ? getLHS() : getRHS();
  }

  Expr *getCond() const { return cast<Expr>(SubExprs[COND]); }
  void setCond(Expr *E) { SubExprs[COND] = E; }
  Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  void setLHS(Expr *E) { SubExprs[LHS] = E; }
  Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }
  void setRHS(Expr *E) { SubExprs[RHS] = E; }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  void setBuiltinLoc(SourceLocation L) { BuiltinLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return BuiltinLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ChooseExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
};

class NullPtrLiteralExpr : public Expr {
public:
  NullPtrLiteralExpr(QualType Ty, SourceLocation Loc)
      : Expr(NullPtrLiteralExprClass, Ty, VK_PRValue, OK_Ordinary) {
    NullPtrLiteralExprBits.Loc = Loc;
    setDependence(ExprDependence::None);
  }

  explicit NullPtrLiteralExpr(EmptyShell Empty)
      : Expr(NullPtrLiteralExprClass, Empty) {}

  SourceLocation getBeginLoc() const { return getLocation(); }
  SourceLocation getEndLoc() const { return getLocation(); }

  SourceLocation getLocation() const { return NullPtrLiteralExprBits.Loc; }
  void setLocation(SourceLocation L) { NullPtrLiteralExprBits.Loc = L; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == NullPtrLiteralExprClass;
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class VAArgExpr : public Expr {
  Stmt *Val;
  llvm::PointerIntPair<TypeSourceInfo *, 1, bool> TInfo;
  SourceLocation BuiltinLoc, RParenLoc;

public:
  VAArgExpr(SourceLocation BLoc, Expr *e, TypeSourceInfo *TInfo,
            SourceLocation RPLoc, QualType t, bool IsMS)
      : Expr(VAArgExprClass, t, VK_PRValue, OK_Ordinary), Val(e),
        TInfo(TInfo, IsMS), BuiltinLoc(BLoc), RParenLoc(RPLoc) {
    setDependence(computeDependence(this));
  }

  explicit VAArgExpr(EmptyShell Empty)
      : Expr(VAArgExprClass, Empty), Val(nullptr), TInfo(nullptr, false) {}

  const Expr *getSubExpr() const { return cast<Expr>(Val); }
  Expr *getSubExpr() { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  bool isMicrosoftABI() const { return TInfo.getInt(); }
  void setIsMicrosoftABI(bool IsMS) { TInfo.setInt(IsMS); }

  TypeSourceInfo *getWrittenTypeInfo() const { return TInfo.getPointer(); }
  void setWrittenTypeInfo(TypeSourceInfo *TI) { TInfo.setPointer(TI); }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  void setBuiltinLoc(SourceLocation L) { BuiltinLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return BuiltinLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == VAArgExprClass;
  }

  // Iterators
  child_range children() { return child_range(&Val, &Val + 1); }
  const_child_range children() const {
    return const_child_range(&Val, &Val + 1);
  }
};

enum class SourceLocIdentKind {
  Function,
  FuncSig,
  File,
  FileName,
  Line,
  Column
};

class SourceLocExpr final : public Expr {
  SourceLocation BuiltinLoc, RParenLoc;
  DeclContext *ParentContext;

public:
  SourceLocExpr(const TreeContext &Ctx, SourceLocIdentKind Type,
                QualType ResultTy, SourceLocation BLoc,
                SourceLocation RParenLoc, DeclContext *Context);

  explicit SourceLocExpr(EmptyShell Empty) : Expr(SourceLocExprClass, Empty) {}

  APValue EvaluateInContext(const TreeContext &Ctx,
                            const Expr *DefaultExpr) const;

  llvm::StringRef getBuiltinStr() const;

  SourceLocIdentKind getIdentKind() const {
    return static_cast<SourceLocIdentKind>(SourceLocExprBits.Kind);
  }

  bool isIntType() const {
    switch (getIdentKind()) {
    case SourceLocIdentKind::File:
    case SourceLocIdentKind::FileName:
    case SourceLocIdentKind::Function:
    case SourceLocIdentKind::FuncSig:
      return false;
    case SourceLocIdentKind::Line:
    case SourceLocIdentKind::Column:
      return true;
    }
    llvm_unreachable("unknown source location expression kind");
  }

  const DeclContext *getParentContext() const { return ParentContext; }
  DeclContext *getParentContext() { return ParentContext; }

  SourceLocation getLocation() const { return BuiltinLoc; }
  SourceLocation getBeginLoc() const { return BuiltinLoc; }
  SourceLocation getEndLoc() const { return RParenLoc; }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(child_iterator(), child_iterator());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SourceLocExprClass;
  }

private:
};

class InitListExpr : public Expr {
  typedef TreeVector<Stmt *> InitExprsTy;
  InitExprsTy InitExprs;
  SourceLocation LBraceLoc, RBraceLoc;

  llvm::PointerIntPair<InitListExpr *, 1, bool> AltForm;

  llvm::PointerUnion<Expr *, FieldDecl *> ArrayFillerOrUnionFieldInit;

public:
  InitListExpr(const TreeContext &C, SourceLocation lbraceloc,
               llvm::ArrayRef<Expr *> initExprs, SourceLocation rbraceloc);

  explicit InitListExpr(EmptyShell Empty)
      : Expr(InitListExprClass, Empty), AltForm(nullptr, true) {}

  unsigned getNumInits() const { return InitExprs.size(); }

  Expr **getInits() { return reinterpret_cast<Expr **>(InitExprs.data()); }

  Expr *const *getInits() const {
    return reinterpret_cast<Expr *const *>(InitExprs.data());
  }

  llvm::ArrayRef<Expr *> inits() {
    return llvm::ArrayRef(getInits(), getNumInits());
  }

  llvm::ArrayRef<Expr *> inits() const {
    return llvm::ArrayRef(getInits(), getNumInits());
  }

  const Expr *getInit(unsigned Init) const {
    assert(Init < getNumInits() && "Initializer access out of range!");
    return cast_or_null<Expr>(InitExprs[Init]);
  }

  Expr *getInit(unsigned Init) {
    assert(Init < getNumInits() && "Initializer access out of range!");
    return cast_or_null<Expr>(InitExprs[Init]);
  }

  void setInit(unsigned Init, Expr *expr) {
    assert(Init < getNumInits() && "Initializer access out of range!");
    InitExprs[Init] = expr;

    if (expr)
      setDependence(getDependence() | expr->getDependence());
  }

  void markError() {
    assert(isSemanticForm());
    setDependence(getDependence() | ExprDependence::ErrorDependent);
  }

  void reserveInits(const TreeContext &C, unsigned NumInits);

  void resizeInits(const TreeContext &Context, unsigned NumInits);

  Expr *updateInit(const TreeContext &C, unsigned Init, Expr *expr);

  Expr *getArrayFiller() {
    return ArrayFillerOrUnionFieldInit.dyn_cast<Expr *>();
  }
  const Expr *getArrayFiller() const {
    return const_cast<InitListExpr *>(this)->getArrayFiller();
  }
  void setArrayFiller(Expr *filler);

  bool hasArrayFiller() const { return getArrayFiller(); }

  bool hasDesignatedInit() const {
    return std::any_of(begin(), end(), [](const Stmt *S) {
      return isa<DesignatedInitExpr>(S);
    });
  }

  FieldDecl *getInitializedFieldInUnion() {
    return ArrayFillerOrUnionFieldInit.dyn_cast<FieldDecl *>();
  }
  const FieldDecl *getInitializedFieldInUnion() const {
    return const_cast<InitListExpr *>(this)->getInitializedFieldInUnion();
  }
  void setInitializedFieldInUnion(FieldDecl *FD) {
    assert((FD == nullptr || getInitializedFieldInUnion() == nullptr ||
            getInitializedFieldInUnion() == FD) &&
           "Only one field of a union may be initialized at a time!");
    ArrayFillerOrUnionFieldInit = FD;
  }

  // Explicit InitListExpr's originate from source code (and have valid source
  // locations). Implicit InitListExpr's are created by the semantic analyzer.
  bool isExplicit() const { return LBraceLoc.isValid() && RBraceLoc.isValid(); }

  bool isStringLiteralInit() const;

  bool isTransparent() const;

  bool isIdiomaticZeroInitializer(const LangOptions &LangOpts) const;

  SourceLocation getLBraceLoc() const { return LBraceLoc; }
  void setLBraceLoc(SourceLocation Loc) { LBraceLoc = Loc; }
  SourceLocation getRBraceLoc() const { return RBraceLoc; }
  void setRBraceLoc(SourceLocation Loc) { RBraceLoc = Loc; }

  bool isSemanticForm() const { return AltForm.getInt(); }
  InitListExpr *getSemanticForm() const {
    return isSemanticForm() ? nullptr : AltForm.getPointer();
  }
  bool isSyntacticForm() const {
    return !AltForm.getInt() || !AltForm.getPointer();
  }
  InitListExpr *getSyntacticForm() const {
    return isSemanticForm() ? AltForm.getPointer() : nullptr;
  }

  void setSyntacticForm(InitListExpr *Init) {
    AltForm.setPointer(Init);
    AltForm.setInt(true);
    Init->AltForm.setPointer(this);
    Init->AltForm.setInt(false);
  }

  bool hadArrayRangeDesignator() const {
    return InitListExprBits.HadArrayRangeDesignator != 0;
  }
  void sawArrayRangeDesignator(bool ARD = true) {
    InitListExprBits.HadArrayRangeDesignator = ARD;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == InitListExprClass;
  }

  // Iterators
  child_range children() {
    const_child_range CCR = const_cast<const InitListExpr *>(this)->children();
    return child_range(cast_away_const(CCR.begin()),
                       cast_away_const(CCR.end()));
  }

  const_child_range children() const {
    if (InitExprs.empty())
      return const_child_range(const_child_iterator(), const_child_iterator());
    return const_child_range(&InitExprs[0], &InitExprs[0] + InitExprs.size());
  }

  typedef InitExprsTy::iterator iterator;
  typedef InitExprsTy::const_iterator const_iterator;
  typedef InitExprsTy::reverse_iterator reverse_iterator;
  typedef InitExprsTy::const_reverse_iterator const_reverse_iterator;

  iterator begin() { return InitExprs.begin(); }
  const_iterator begin() const { return InitExprs.begin(); }
  iterator end() { return InitExprs.end(); }
  const_iterator end() const { return InitExprs.end(); }
  reverse_iterator rbegin() { return InitExprs.rbegin(); }
  const_reverse_iterator rbegin() const { return InitExprs.rbegin(); }
  reverse_iterator rend() { return InitExprs.rend(); }
  const_reverse_iterator rend() const { return InitExprs.rend(); }
};

class DesignatedInitExpr final
    : public Expr,
      private llvm::TrailingObjects<DesignatedInitExpr, Stmt *> {
public:
  class Designator;

private:
  SourceLocation EqualOrColonLoc;

  LLVM_PREFERRED_TYPE(bool)
  unsigned GNUSyntax : 1;

  unsigned NumDesignators : 15;

  unsigned NumSubExprs : 16;

  Designator *Designators;

  DesignatedInitExpr(const TreeContext &C, QualType Ty,
                     llvm::ArrayRef<Designator> Designators,
                     SourceLocation EqualOrColonLoc, bool GNUSyntax,
                     llvm::ArrayRef<Expr *> IndexExprs, Expr *Init);

  explicit DesignatedInitExpr(unsigned NumSubExprs)
      : Expr(DesignatedInitExprClass, EmptyShell()), NumDesignators(0),
        NumSubExprs(NumSubExprs), Designators(nullptr) {}

public:
  class Designator {
    /// A field designator, e.g., ".x".
    struct FieldDesignatorInfo {
      /// Refers to the field that is being initialized. The low bit
      /// of this field determines whether this is actually a pointer
      /// to an IdentifierInfo (if 1) or a FieldDecl (if 0). When
      /// initially constructed, a field designator will store an
      /// IdentifierInfo*. After semantic analysis has resolved that
      /// name, the field designator will instead store a FieldDecl*.
      uintptr_t NameOrField;

      /// The location of the '.' in the designated initializer.
      SourceLocation DotLoc;

      /// The location of the field name in the designated initializer.
      SourceLocation FieldLoc;

      FieldDesignatorInfo(const IdentifierInfo *II, SourceLocation DotLoc,
                          SourceLocation FieldLoc)
          : NameOrField(reinterpret_cast<uintptr_t>(II) | 0x1), DotLoc(DotLoc),
            FieldLoc(FieldLoc) {}
    };

    /// An array or GNU array-range designator, e.g., "[9]" or "[10...15]".
    struct ArrayOrRangeDesignatorInfo {
      /// Location of the first index expression within the designated
      /// initializer expression's list of subexpressions.
      unsigned Index;

      /// The location of the '[' starting the array range designator.
      SourceLocation LBracketLoc;

      /// The location of the ellipsis separating the start and end
      /// indices. Only valid for GNU array-range designators.
      SourceLocation EllipsisLoc;

      /// The location of the ']' terminating the array range designator.
      SourceLocation RBracketLoc;

      ArrayOrRangeDesignatorInfo(unsigned Index, SourceLocation LBracketLoc,
                                 SourceLocation RBracketLoc)
          : Index(Index), LBracketLoc(LBracketLoc), RBracketLoc(RBracketLoc) {}

      ArrayOrRangeDesignatorInfo(unsigned Index, SourceLocation LBracketLoc,
                                 SourceLocation EllipsisLoc,
                                 SourceLocation RBracketLoc)
          : Index(Index), LBracketLoc(LBracketLoc), EllipsisLoc(EllipsisLoc),
            RBracketLoc(RBracketLoc) {}
    };

    /// The kind of designator this describes.
    enum DesignatorKind {
      FieldDesignator,
      ArrayDesignator,
      ArrayRangeDesignator
    };

    DesignatorKind Kind;

    union {
      /// A field designator, e.g., ".x".
      struct FieldDesignatorInfo FieldInfo;

      /// An array or GNU array-range designator, e.g., "[9]" or "[10..15]".
      struct ArrayOrRangeDesignatorInfo ArrayOrRangeInfo;
    };

    Designator(DesignatorKind Kind) : Kind(Kind) {}

  public:
    Designator() {}

    bool isFieldDesignator() const { return Kind == FieldDesignator; }
    bool isArrayDesignator() const { return Kind == ArrayDesignator; }
    bool isArrayRangeDesignator() const { return Kind == ArrayRangeDesignator; }

    //===------------------------------------------------------------------===//
    // FieldDesignatorInfo

    /// Creates a field designator.
    static Designator CreateFieldDesignator(const IdentifierInfo *FieldName,
                                            SourceLocation DotLoc,
                                            SourceLocation FieldLoc) {
      Designator D(FieldDesignator);
      new (&D.FieldInfo) FieldDesignatorInfo(FieldName, DotLoc, FieldLoc);
      return D;
    }

    const IdentifierInfo *getFieldName() const;

    FieldDecl *getFieldDecl() const {
      assert(isFieldDesignator() && "Only valid on a field designator");
      if (FieldInfo.NameOrField & 0x01)
        return nullptr;
      return reinterpret_cast<FieldDecl *>(FieldInfo.NameOrField);
    }

    void setFieldDecl(FieldDecl *FD) {
      assert(isFieldDesignator() && "Only valid on a field designator");
      FieldInfo.NameOrField = reinterpret_cast<uintptr_t>(FD);
    }

    SourceLocation getDotLoc() const {
      assert(isFieldDesignator() && "Only valid on a field designator");
      return FieldInfo.DotLoc;
    }

    SourceLocation getFieldLoc() const {
      assert(isFieldDesignator() && "Only valid on a field designator");
      return FieldInfo.FieldLoc;
    }

    //===------------------------------------------------------------------===//
    // ArrayOrRangeDesignator

    /// Creates an array designator.
    static Designator CreateArrayDesignator(unsigned Index,
                                            SourceLocation LBracketLoc,
                                            SourceLocation RBracketLoc) {
      Designator D(ArrayDesignator);
      new (&D.ArrayOrRangeInfo)
          ArrayOrRangeDesignatorInfo(Index, LBracketLoc, RBracketLoc);
      return D;
    }

    /// Creates a GNU array-range designator.
    static Designator CreateArrayRangeDesignator(unsigned Index,
                                                 SourceLocation LBracketLoc,
                                                 SourceLocation EllipsisLoc,
                                                 SourceLocation RBracketLoc) {
      Designator D(ArrayRangeDesignator);
      new (&D.ArrayOrRangeInfo) ArrayOrRangeDesignatorInfo(
          Index, LBracketLoc, EllipsisLoc, RBracketLoc);
      return D;
    }

    unsigned getArrayIndex() const {
      assert((isArrayDesignator() || isArrayRangeDesignator()) &&
             "Only valid on an array or array-range designator");
      return ArrayOrRangeInfo.Index;
    }

    SourceLocation getLBracketLoc() const {
      assert((isArrayDesignator() || isArrayRangeDesignator()) &&
             "Only valid on an array or array-range designator");
      return ArrayOrRangeInfo.LBracketLoc;
    }

    SourceLocation getEllipsisLoc() const {
      assert(isArrayRangeDesignator() &&
             "Only valid on an array-range designator");
      return ArrayOrRangeInfo.EllipsisLoc;
    }

    SourceLocation getRBracketLoc() const {
      assert((isArrayDesignator() || isArrayRangeDesignator()) &&
             "Only valid on an array or array-range designator");
      return ArrayOrRangeInfo.RBracketLoc;
    }

    SourceLocation getBeginLoc() const LLVM_READONLY {
      if (isFieldDesignator())
        return getDotLoc().isInvalid() ? getFieldLoc() : getDotLoc();
      return getLBracketLoc();
    }

    SourceLocation getEndLoc() const LLVM_READONLY {
      return isFieldDesignator() ? getFieldLoc() : getRBracketLoc();
    }

    SourceRange getSourceRange() const LLVM_READONLY {
      return SourceRange(getBeginLoc(), getEndLoc());
    }
  };

  static DesignatedInitExpr *Create(const TreeContext &C,
                                    llvm::ArrayRef<Designator> Designators,
                                    llvm::ArrayRef<Expr *> IndexExprs,
                                    SourceLocation EqualOrColonLoc,
                                    bool GNUSyntax, Expr *Init);

  static DesignatedInitExpr *CreateEmpty(const TreeContext &C,
                                         unsigned NumIndexExprs);

  unsigned size() const { return NumDesignators; }

  // Iterator access to the designators.
  llvm::MutableArrayRef<Designator> designators() {
    return {Designators, NumDesignators};
  }

  llvm::ArrayRef<Designator> designators() const {
    return {Designators, NumDesignators};
  }

  Designator *getDesignator(unsigned Idx) { return &designators()[Idx]; }
  const Designator *getDesignator(unsigned Idx) const {
    return &designators()[Idx];
  }

  void setDesignators(const TreeContext &C, const Designator *Desigs,
                      unsigned NumDesigs);

  Expr *getArrayIndex(const Designator &D) const;
  Expr *getArrayRangeStart(const Designator &D) const;
  Expr *getArrayRangeEnd(const Designator &D) const;

  SourceLocation getEqualOrColonLoc() const { return EqualOrColonLoc; }
  void setEqualOrColonLoc(SourceLocation L) { EqualOrColonLoc = L; }

  bool isDirectInit() const { return EqualOrColonLoc.isInvalid(); }

  bool usesGNUSyntax() const { return GNUSyntax; }
  void setGNUSyntax(bool GNU) { GNUSyntax = GNU; }

  Expr *getInit() const {
    return cast<Expr>(*const_cast<DesignatedInitExpr *>(this)->child_begin());
  }

  void setInit(Expr *init) { *child_begin() = init; }

  unsigned getNumSubExprs() const { return NumSubExprs; }

  Expr *getSubExpr(unsigned Idx) const {
    assert(Idx < NumSubExprs && "Subscript out of range");
    return cast<Expr>(getTrailingObjects<Stmt *>()[Idx]);
  }

  void setSubExpr(unsigned Idx, Expr *E) {
    assert(Idx < NumSubExprs && "Subscript out of range");
    getTrailingObjects<Stmt *>()[Idx] = E;
  }

  void ExpandDesignator(const TreeContext &C, unsigned Idx,
                        const Designator *First, const Designator *Last);

  SourceRange getDesignatorsSourceRange() const;

  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DesignatedInitExprClass;
  }

  // Iterators
  child_range children() {
    Stmt **begin = getTrailingObjects<Stmt *>();
    return child_range(begin, begin + NumSubExprs);
  }
  const_child_range children() const {
    Stmt *const *begin = getTrailingObjects<Stmt *>();
    return const_child_range(begin, begin + NumSubExprs);
  }

  friend TrailingObjects;
};

class NoInitExpr : public Expr {
public:
  explicit NoInitExpr(QualType ty)
      : Expr(NoInitExprClass, ty, VK_PRValue, OK_Ordinary) {
    setDependence(computeDependence(this));
  }

  explicit NoInitExpr(EmptyShell Empty) : Expr(NoInitExprClass, Empty) {}

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == NoInitExprClass;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return SourceLocation(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return SourceLocation(); }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

// In cases like:
//   struct Q { int a, b, c; };
//   Q *getQ();
//   void foo() {
//     struct A { Q q; } a = { *getQ(), .q.b = 3 };
//   }
//
// We will have an InitListExpr for a, with type A, and then a
// DesignatedInitUpdateExpr for "a.q" with type Q. The "base" for this DIUE
// is the call expression *getQ(); the "updater" for the DIUE is ".q.b = 3"
//
class DesignatedInitUpdateExpr : public Expr {
  // BaseAndUpdaterExprs[0] is the base expression;
  // BaseAndUpdaterExprs[1] is an InitListExpr overwriting part of the base.
  Stmt *BaseAndUpdaterExprs[2];

public:
  DesignatedInitUpdateExpr(const TreeContext &C, SourceLocation lBraceLoc,
                           Expr *baseExprs, SourceLocation rBraceLoc);

  explicit DesignatedInitUpdateExpr(EmptyShell Empty)
      : Expr(DesignatedInitUpdateExprClass, Empty) {}

  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DesignatedInitUpdateExprClass;
  }

  Expr *getBase() const { return cast<Expr>(BaseAndUpdaterExprs[0]); }
  void setBase(Expr *Base) { BaseAndUpdaterExprs[0] = Base; }

  InitListExpr *getUpdater() const {
    return cast<InitListExpr>(BaseAndUpdaterExprs[1]);
  }
  void setUpdater(Expr *Updater) { BaseAndUpdaterExprs[1] = Updater; }

  // Iterators
  // children = the base and the updater
  child_range children() {
    return child_range(&BaseAndUpdaterExprs[0], &BaseAndUpdaterExprs[0] + 2);
  }
  const_child_range children() const {
    return const_child_range(&BaseAndUpdaterExprs[0],
                             &BaseAndUpdaterExprs[0] + 2);
  }
};

class ArrayInitLoopExpr : public Expr {
  Stmt *SubExprs[2];

  explicit ArrayInitLoopExpr(EmptyShell Empty)
      : Expr(ArrayInitLoopExprClass, Empty), SubExprs{} {}

public:
  explicit ArrayInitLoopExpr(QualType T, Expr *CommonInit, Expr *ElementInit)
      : Expr(ArrayInitLoopExprClass, T, VK_PRValue, OK_Ordinary),
        SubExprs{CommonInit, ElementInit} {
    setDependence(computeDependence(this));
  }

  OpaqueValueExpr *getCommonExpr() const {
    return cast<OpaqueValueExpr>(SubExprs[0]);
  }

  Expr *getSubExpr() const { return cast<Expr>(SubExprs[1]); }

  llvm::APInt getArraySize() const {
    return cast<ConstantArrayType>(getType()->castAsArrayTypeUnsafe())
        ->getSize();
  }

  static bool classof(const Stmt *S) {
    return S->getStmtClass() == ArrayInitLoopExprClass;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getCommonExpr()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getCommonExpr()->getEndLoc();
  }

  child_range children() { return child_range(SubExprs, SubExprs + 2); }
  const_child_range children() const {
    return const_child_range(SubExprs, SubExprs + 2);
  }
};

class ArrayInitIndexExpr : public Expr {
  explicit ArrayInitIndexExpr(EmptyShell Empty)
      : Expr(ArrayInitIndexExprClass, Empty) {}

public:
  explicit ArrayInitIndexExpr(QualType T)
      : Expr(ArrayInitIndexExprClass, T, VK_PRValue, OK_Ordinary) {
    setDependence(ExprDependence::None);
  }

  static bool classof(const Stmt *S) {
    return S->getStmtClass() == ArrayInitIndexExprClass;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return SourceLocation(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return SourceLocation(); }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class ImplicitValueInitExpr : public Expr {
public:
  explicit ImplicitValueInitExpr(QualType ty)
      : Expr(ImplicitValueInitExprClass, ty, VK_PRValue, OK_Ordinary) {
    setDependence(computeDependence(this));
  }

  explicit ImplicitValueInitExpr(EmptyShell Empty)
      : Expr(ImplicitValueInitExprClass, Empty) {}

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ImplicitValueInitExprClass;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return SourceLocation(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return SourceLocation(); }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class ParenListExpr final
    : public Expr,
      private llvm::TrailingObjects<ParenListExpr, Stmt *> {

  friend TrailingObjects;

  SourceLocation LParenLoc, RParenLoc;

  ParenListExpr(SourceLocation LParenLoc, llvm::ArrayRef<Expr *> Exprs,
                SourceLocation RParenLoc);

  ParenListExpr(EmptyShell Empty, unsigned NumExprs);

public:
  static ParenListExpr *Create(const TreeContext &Ctx, SourceLocation LParenLoc,
                               llvm::ArrayRef<Expr *> Exprs,
                               SourceLocation RParenLoc);

  static ParenListExpr *CreateEmpty(const TreeContext &Ctx, unsigned NumExprs);

  unsigned getNumExprs() const { return ParenListExprBits.NumExprs; }

  Expr *getExpr(unsigned Init) {
    assert(Init < getNumExprs() && "Initializer access out of range!");
    return getExprs()[Init];
  }

  const Expr *getExpr(unsigned Init) const {
    return const_cast<ParenListExpr *>(this)->getExpr(Init);
  }

  Expr **getExprs() {
    return reinterpret_cast<Expr **>(getTrailingObjects<Stmt *>());
  }

  llvm::ArrayRef<Expr *> exprs() {
    return llvm::ArrayRef(getExprs(), getNumExprs());
  }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  SourceLocation getBeginLoc() const { return getLParenLoc(); }
  SourceLocation getEndLoc() const { return getRParenLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ParenListExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(getTrailingObjects<Stmt *>(),
                       getTrailingObjects<Stmt *>() + getNumExprs());
  }
  const_child_range children() const {
    return const_child_range(getTrailingObjects<Stmt *>(),
                             getTrailingObjects<Stmt *>() + getNumExprs());
  }
};

class GenericSelectionExpr final
    : public Expr,
      private llvm::TrailingObjects<GenericSelectionExpr, Stmt *,
                                    TypeSourceInfo *> {

  friend TrailingObjects;

  unsigned NumAssocs : 15;
  unsigned ResultIndex : 15; // NB: ResultDependentIndex is tied to this width.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsExprPredicate : 1;
  enum : unsigned { ResultDependentIndex = 0x7FFF };

  unsigned getIndexOfControllingExpression() const {
    // If controlled by an expression, the first offset into the Stmt *
    // trailing array is the controlling expression, the associated expressions
    // follow this.
    assert(isExprPredicate() && "Asking for the controlling expression of a "
                                "selection expr predicated by a type");
    return 0;
  }

  unsigned getIndexOfControllingType() const {
    // If controlled by a type, the first offset into the TypeSourceInfo *
    // trailing array is the controlling type, the associated types follow this.
    assert(isTypePredicate() && "Asking for the controlling type of a "
                                "selection expr predicated by an expression");
    return 0;
  }

  unsigned getIndexOfStartOfAssociatedExprs() const {
    // If the predicate is a type, then the associated expressions are the only
    // Stmt * in the trailing array, otherwise we need to offset past the
    // predicate expression.
    return (int)isExprPredicate();
  }

  unsigned getIndexOfStartOfAssociatedTypes() const {
    // If the predicate is a type, then the associated types follow it in the
    // trailing array. Otherwise, the associated types are the only
    // TypeSourceInfo * in the trailing array.
    return (int)isTypePredicate();
  }

  SourceLocation DefaultLoc, RParenLoc;

  // GenericSelectionExpr is followed by several trailing objects.
  // They are (in order):
  //
  // * A single Stmt * for the controlling expression or a TypeSourceInfo * for
  //   the controlling type, depending on the result of isTypePredicate() or
  //   isExprPredicate().
  // * An array of getNumAssocs() Stmt * for the association expressions.
  // * An array of getNumAssocs() TypeSourceInfo *, one for each of the
  //   association expressions.
  unsigned numTrailingObjects(OverloadToken<Stmt *>) const {
    // Add one to account for the controlling expression; the remainder
    // are the associated expressions.
    return getNumAssocs() + (int)isExprPredicate();
  }

  unsigned numTrailingObjects(OverloadToken<TypeSourceInfo *>) const {
    // Add one to account for the controlling type predicate, the remainder
    // are the associated types.
    return getNumAssocs() + (int)isTypePredicate();
  }

  template <bool Const> class AssociationIteratorTy;
  template <bool Const> class AssociationTy {
    friend class GenericSelectionExpr;
    template <bool OtherConst> friend class AssociationIteratorTy;
    using ExprPtrTy = std::conditional_t<Const, const Expr *, Expr *>;
    using TSIPtrTy =
        std::conditional_t<Const, const TypeSourceInfo *, TypeSourceInfo *>;
    ExprPtrTy E;
    TSIPtrTy TSI;
    bool Selected;
    AssociationTy(ExprPtrTy E, TSIPtrTy TSI, bool Selected)
        : E(E), TSI(TSI), Selected(Selected) {}

  public:
    ExprPtrTy getAssociationExpr() const { return E; }
    TSIPtrTy getTypeSourceInfo() const { return TSI; }
    QualType getType() const { return TSI ? TSI->getType() : QualType(); }
    bool isSelected() const { return Selected; }
    AssociationTy *operator->() { return this; }
    const AssociationTy *operator->() const { return this; }
  }; // class AssociationTy

  template <bool Const>
  class AssociationIteratorTy
      : public llvm::iterator_facade_base<
            AssociationIteratorTy<Const>, std::input_iterator_tag,
            AssociationTy<Const>, std::ptrdiff_t, AssociationTy<Const>,
            AssociationTy<Const>> {
    friend class GenericSelectionExpr;
    using BaseTy = typename AssociationIteratorTy::iterator_facade_base;
    using StmtPtrPtrTy =
        std::conditional_t<Const, const Stmt *const *, Stmt **>;
    using TSIPtrPtrTy = std::conditional_t<Const, const TypeSourceInfo *const *,
                                           TypeSourceInfo **>;
    StmtPtrPtrTy E = nullptr;
    TSIPtrPtrTy TSI; // Kept in sync with E.
    unsigned Offset = 0, SelectedOffset = 0;
    AssociationIteratorTy(StmtPtrPtrTy E, TSIPtrPtrTy TSI, unsigned Offset,
                          unsigned SelectedOffset)
        : E(E), TSI(TSI), Offset(Offset), SelectedOffset(SelectedOffset) {}

  public:
    AssociationIteratorTy() : E(nullptr), TSI(nullptr) {}
    typename BaseTy::reference operator*() const {
      return AssociationTy<Const>(cast<Expr>(*E), *TSI,
                                  Offset == SelectedOffset);
    }
    typename BaseTy::pointer operator->() const { return **this; }
    using BaseTy::operator++;
    AssociationIteratorTy &operator++() {
      ++E;
      ++TSI;
      ++Offset;
      return *this;
    }
    bool operator==(AssociationIteratorTy Other) const { return E == Other.E; }
  }; // class AssociationIterator

  GenericSelectionExpr(const TreeContext &Context, SourceLocation GenericLoc,
                       Expr *ControllingExpr,
                       llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
                       llvm::ArrayRef<Expr *> AssocExprs,
                       SourceLocation DefaultLoc, SourceLocation RParenLoc,
                       unsigned ResultIndex);

  GenericSelectionExpr(const TreeContext &Context, SourceLocation GenericLoc,
                       Expr *ControllingExpr,
                       llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
                       llvm::ArrayRef<Expr *> AssocExprs,
                       SourceLocation DefaultLoc, SourceLocation RParenLoc);

  GenericSelectionExpr(const TreeContext &Context, SourceLocation GenericLoc,
                       TypeSourceInfo *ControllingType,
                       llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
                       llvm::ArrayRef<Expr *> AssocExprs,
                       SourceLocation DefaultLoc, SourceLocation RParenLoc,
                       unsigned ResultIndex);

  GenericSelectionExpr(const TreeContext &Context, SourceLocation GenericLoc,
                       TypeSourceInfo *ControllingType,
                       llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
                       llvm::ArrayRef<Expr *> AssocExprs,
                       SourceLocation DefaultLoc, SourceLocation RParenLoc);

  explicit GenericSelectionExpr(EmptyShell Empty, unsigned NumAssocs);

public:
  static GenericSelectionExpr *
  Create(const TreeContext &Context, SourceLocation GenericLoc,
         Expr *ControllingExpr, llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
         llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
         SourceLocation RParenLoc, unsigned ResultIndex);

  static GenericSelectionExpr *
  Create(const TreeContext &Context, SourceLocation GenericLoc,
         Expr *ControllingExpr, llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
         llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
         SourceLocation RParenLoc);

  static GenericSelectionExpr *
  Create(const TreeContext &Context, SourceLocation GenericLoc,
         TypeSourceInfo *ControllingType,
         llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
         llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
         SourceLocation RParenLoc, unsigned ResultIndex);

  static GenericSelectionExpr *
  Create(const TreeContext &Context, SourceLocation GenericLoc,
         TypeSourceInfo *ControllingType,
         llvm::ArrayRef<TypeSourceInfo *> AssocTypes,
         llvm::ArrayRef<Expr *> AssocExprs, SourceLocation DefaultLoc,
         SourceLocation RParenLoc);

  static GenericSelectionExpr *CreateEmpty(const TreeContext &Context,
                                           unsigned NumAssocs);

  using Association = AssociationTy<false>;
  using ConstAssociation = AssociationTy<true>;
  using AssociationIterator = AssociationIteratorTy<false>;
  using ConstAssociationIterator = AssociationIteratorTy<true>;
  using association_range = llvm::iterator_range<AssociationIterator>;
  using const_association_range =
      llvm::iterator_range<ConstAssociationIterator>;

  unsigned getNumAssocs() const { return NumAssocs; }

  unsigned getResultIndex() const {
    assert(!isResultDependent() &&
           "Generic selection is result-dependent but getResultIndex called!");
    return ResultIndex;
  }

  bool isResultDependent() const { return ResultIndex == ResultDependentIndex; }

  bool isExprPredicate() const { return IsExprPredicate; }
  bool isTypePredicate() const { return !IsExprPredicate; }

  Expr *getControllingExpr() {
    return cast<Expr>(
        getTrailingObjects<Stmt *>()[getIndexOfControllingExpression()]);
  }
  const Expr *getControllingExpr() const {
    return cast<Expr>(
        getTrailingObjects<Stmt *>()[getIndexOfControllingExpression()]);
  }

  TypeSourceInfo *getControllingType() {
    return getTrailingObjects<TypeSourceInfo *>()[getIndexOfControllingType()];
  }
  const TypeSourceInfo *getControllingType() const {
    return getTrailingObjects<TypeSourceInfo *>()[getIndexOfControllingType()];
  }

  Expr *getResultExpr() {
    return cast<Expr>(
        getTrailingObjects<Stmt *>()[getIndexOfStartOfAssociatedExprs() +
                                     getResultIndex()]);
  }
  const Expr *getResultExpr() const {
    return cast<Expr>(
        getTrailingObjects<Stmt *>()[getIndexOfStartOfAssociatedExprs() +
                                     getResultIndex()]);
  }

  llvm::ArrayRef<Expr *> getAssocExprs() const {
    return {reinterpret_cast<Expr *const *>(getTrailingObjects<Stmt *>() +
                                            getIndexOfStartOfAssociatedExprs()),
            NumAssocs};
  }
  llvm::ArrayRef<TypeSourceInfo *> getAssocTypeSourceInfos() const {
    return {getTrailingObjects<TypeSourceInfo *>() +
                getIndexOfStartOfAssociatedTypes(),
            NumAssocs};
  }

  Association getAssociation(unsigned I) {
    assert(I < getNumAssocs() &&
           "Out-of-range index in GenericSelectionExpr::getAssociation!");
    return Association(
        cast<Expr>(
            getTrailingObjects<Stmt *>()[getIndexOfStartOfAssociatedExprs() +
                                         I]),
        getTrailingObjects<
            TypeSourceInfo *>()[getIndexOfStartOfAssociatedTypes() + I],
        !isResultDependent() && (getResultIndex() == I));
  }
  ConstAssociation getAssociation(unsigned I) const {
    assert(I < getNumAssocs() &&
           "Out-of-range index in GenericSelectionExpr::getAssociation!");
    return ConstAssociation(
        cast<Expr>(
            getTrailingObjects<Stmt *>()[getIndexOfStartOfAssociatedExprs() +
                                         I]),
        getTrailingObjects<
            TypeSourceInfo *>()[getIndexOfStartOfAssociatedTypes() + I],
        !isResultDependent() && (getResultIndex() == I));
  }

  association_range associations() {
    AssociationIterator Begin(getTrailingObjects<Stmt *>() +
                                  getIndexOfStartOfAssociatedExprs(),
                              getTrailingObjects<TypeSourceInfo *>() +
                                  getIndexOfStartOfAssociatedTypes(),
                              /*Offset=*/0, ResultIndex);
    AssociationIterator End(Begin.E + NumAssocs, Begin.TSI + NumAssocs,
                            /*Offset=*/NumAssocs, ResultIndex);
    return llvm::make_range(Begin, End);
  }

  const_association_range associations() const {
    ConstAssociationIterator Begin(getTrailingObjects<Stmt *>() +
                                       getIndexOfStartOfAssociatedExprs(),
                                   getTrailingObjects<TypeSourceInfo *>() +
                                       getIndexOfStartOfAssociatedTypes(),
                                   /*Offset=*/0, ResultIndex);
    ConstAssociationIterator End(Begin.E + NumAssocs, Begin.TSI + NumAssocs,
                                 /*Offset=*/NumAssocs, ResultIndex);
    return llvm::make_range(Begin, End);
  }

  SourceLocation getGenericLoc() const {
    return GenericSelectionExprBits.GenericLoc;
  }
  SourceLocation getDefaultLoc() const { return DefaultLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  SourceLocation getBeginLoc() const { return getGenericLoc(); }
  SourceLocation getEndLoc() const { return getRParenLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == GenericSelectionExprClass;
  }

  child_range children() {
    return child_range(getTrailingObjects<Stmt *>(),
                       getTrailingObjects<Stmt *>() +
                           numTrailingObjects(OverloadToken<Stmt *>()));
  }
  const_child_range children() const {
    return const_child_range(getTrailingObjects<Stmt *>(),
                             getTrailingObjects<Stmt *>() +
                                 numTrailingObjects(OverloadToken<Stmt *>()));
  }
};

//===----------------------------------------------------------------------===//
// NeverC Extensions
//===----------------------------------------------------------------------===//

class ExtVectorElementExpr : public Expr {
  Stmt *Base;
  IdentifierInfo *Accessor;
  SourceLocation AccessorLoc;

public:
  ExtVectorElementExpr(QualType ty, ExprValueKind VK, Expr *base,
                       IdentifierInfo &accessor, SourceLocation loc)
      : Expr(ExtVectorElementExprClass, ty, VK,
             (VK == VK_PRValue ? OK_Ordinary : OK_VectorComponent)),
        Base(base), Accessor(&accessor), AccessorLoc(loc) {
    setDependence(computeDependence(this));
  }

  explicit ExtVectorElementExpr(EmptyShell Empty)
      : Expr(ExtVectorElementExprClass, Empty) {}

  const Expr *getBase() const { return cast<Expr>(Base); }
  Expr *getBase() { return cast<Expr>(Base); }
  void setBase(Expr *E) { Base = E; }

  IdentifierInfo &getAccessor() const { return *Accessor; }
  void setAccessor(IdentifierInfo *II) { Accessor = II; }

  SourceLocation getAccessorLoc() const { return AccessorLoc; }
  void setAccessorLoc(SourceLocation L) { AccessorLoc = L; }

  unsigned getNumElements() const;

  bool containsDuplicateElements() const;

  void getEncodedElementAccess(llvm::SmallVectorImpl<uint32_t> &Elts) const;

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getBase()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY { return AccessorLoc; }

  bool isArrow() const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ExtVectorElementExprClass;
  }

  // Iterators
  child_range children() { return child_range(&Base, &Base + 1); }
  const_child_range children() const {
    return const_child_range(&Base, &Base + 1);
  }
};

class PseudoObjectExpr final
    : public Expr,
      private llvm::TrailingObjects<PseudoObjectExpr, Expr *> {
  // PseudoObjectExprBits.NumSubExprs - The number of sub-expressions.
  // Always at least two, because the first sub-expression is the
  // syntactic form.

  // PseudoObjectExprBits.ResultIndex - The index of the
  // sub-expression holding the result.  0 means the result is void,
  // which is unambiguous because it's the index of the syntactic
  // form.  Note that this is therefore 1 higher than the value passed
  // in to Create, which is an index within the semantic forms.

  Expr **getSubExprsBuffer() { return getTrailingObjects<Expr *>(); }
  const Expr *const *getSubExprsBuffer() const {
    return getTrailingObjects<Expr *>();
  }

  PseudoObjectExpr(QualType type, ExprValueKind VK, Expr *syntactic,
                   llvm::ArrayRef<Expr *> semantic, unsigned resultIndex);

  PseudoObjectExpr(EmptyShell shell, unsigned numSemanticExprs);

  unsigned getNumSubExprs() const { return PseudoObjectExprBits.NumSubExprs; }

public:
  enum : unsigned { NoResult = ~0U };

  static PseudoObjectExpr *Create(const TreeContext &Context, Expr *syntactic,
                                  llvm::ArrayRef<Expr *> semantic,
                                  unsigned resultIndex);

  static PseudoObjectExpr *Create(const TreeContext &Context, EmptyShell shell,
                                  unsigned numSemanticExprs);

  Expr *getSyntacticForm() { return getSubExprsBuffer()[0]; }
  const Expr *getSyntacticForm() const { return getSubExprsBuffer()[0]; }

  unsigned getResultExprIndex() const {
    if (PseudoObjectExprBits.ResultIndex == 0)
      return NoResult;
    return PseudoObjectExprBits.ResultIndex - 1;
  }

  Expr *getResultExpr() {
    if (PseudoObjectExprBits.ResultIndex == 0)
      return nullptr;
    return getSubExprsBuffer()[PseudoObjectExprBits.ResultIndex];
  }
  const Expr *getResultExpr() const {
    return const_cast<PseudoObjectExpr *>(this)->getResultExpr();
  }

  unsigned getNumSemanticExprs() const { return getNumSubExprs() - 1; }

  typedef Expr *const *semantics_iterator;
  typedef const Expr *const *const_semantics_iterator;
  semantics_iterator semantics_begin() { return getSubExprsBuffer() + 1; }
  const_semantics_iterator semantics_begin() const {
    return getSubExprsBuffer() + 1;
  }
  semantics_iterator semantics_end() {
    return getSubExprsBuffer() + getNumSubExprs();
  }
  const_semantics_iterator semantics_end() const {
    return getSubExprsBuffer() + getNumSubExprs();
  }

  llvm::ArrayRef<Expr *> semantics() {
    return llvm::ArrayRef(semantics_begin(), semantics_end());
  }
  llvm::ArrayRef<const Expr *> semantics() const {
    return llvm::ArrayRef(semantics_begin(), semantics_end());
  }

  Expr *getSemanticExpr(unsigned index) {
    assert(index + 1 < getNumSubExprs());
    return getSubExprsBuffer()[index + 1];
  }
  const Expr *getSemanticExpr(unsigned index) const {
    return const_cast<PseudoObjectExpr *>(this)->getSemanticExpr(index);
  }

  SourceLocation getExprLoc() const LLVM_READONLY {
    return getSyntacticForm()->getExprLoc();
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getSyntacticForm()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getSyntacticForm()->getEndLoc();
  }

  child_range children() {
    const_child_range CCR =
        const_cast<const PseudoObjectExpr *>(this)->children();
    return child_range(cast_away_const(CCR.begin()),
                       cast_away_const(CCR.end()));
  }
  const_child_range children() const {
    Stmt *const *cs = const_cast<Stmt *const *>(
        reinterpret_cast<const Stmt *const *>(getSubExprsBuffer()));
    return const_child_range(cs, cs + getNumSubExprs());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == PseudoObjectExprClass;
  }

  friend TrailingObjects;
};

class AtomicExpr : public Expr {
public:
  enum AtomicOp {
#define BUILTIN(ID, TYPE, ATTRS)
#define ATOMIC_BUILTIN(ID, TYPE, ATTRS) AO##ID,
#include "neverc/Foundation/Builtin/Builtins.def"
    // Avoid trailing comma
    BI_First = 0
  };

private:
  enum { PTR, ORDER, VAL1, ORDER_FAIL, VAL2, WEAK, END_EXPR };
  Stmt *SubExprs[END_EXPR + 1];
  unsigned NumSubExprs;
  SourceLocation BuiltinLoc, RParenLoc;
  AtomicOp Op;

public:
  AtomicExpr(SourceLocation BLoc, llvm::ArrayRef<Expr *> args, QualType t,
             AtomicOp op, SourceLocation RP);

  static unsigned getNumSubExprs(AtomicOp Op);

  explicit AtomicExpr(EmptyShell Empty) : Expr(AtomicExprClass, Empty) {}

  Expr *getPtr() const { return cast<Expr>(SubExprs[PTR]); }
  Expr *getOrder() const { return cast<Expr>(SubExprs[ORDER]); }
  Expr *getScope() const {
    assert(getScopeModel() && "No scope");
    return cast<Expr>(SubExprs[NumSubExprs - 1]);
  }
  Expr *getVal1() const {
    if (Op == AO__c11_atomic_init)
      return cast<Expr>(SubExprs[ORDER]);
    assert(NumSubExprs > VAL1);
    return cast<Expr>(SubExprs[VAL1]);
  }
  Expr *getOrderFail() const {
    assert(NumSubExprs > ORDER_FAIL);
    return cast<Expr>(SubExprs[ORDER_FAIL]);
  }
  Expr *getVal2() const {
    if (Op == AO__atomic_exchange || Op == AO__scoped_atomic_exchange)
      return cast<Expr>(SubExprs[ORDER_FAIL]);
    assert(NumSubExprs > VAL2);
    return cast<Expr>(SubExprs[VAL2]);
  }
  Expr *getWeak() const {
    assert(NumSubExprs > WEAK);
    return cast<Expr>(SubExprs[WEAK]);
  }
  QualType getValueType() const;

  AtomicOp getOp() const { return Op; }
  llvm::StringRef getOpAsString() const {
    switch (Op) {
#define BUILTIN(ID, TYPE, ATTRS)
#define ATOMIC_BUILTIN(ID, TYPE, ATTRS)                                        \
  case AO##ID:                                                                 \
    return #ID;
#include "neverc/Foundation/Builtin/Builtins.def"
    }
    llvm_unreachable("not an atomic operator?");
  }
  unsigned getNumSubExprs() const { return NumSubExprs; }

  Expr **getSubExprs() { return reinterpret_cast<Expr **>(SubExprs); }
  const Expr *const *getSubExprs() const {
    return reinterpret_cast<Expr *const *>(SubExprs);
  }

  bool isVolatile() const {
    return getPtr()->getType()->getPointeeType().isVolatileQualified();
  }

  bool isCmpXChg() const {
    return getOp() == AO__c11_atomic_compare_exchange_strong ||
           getOp() == AO__c11_atomic_compare_exchange_weak ||
           getOp() == AO__atomic_compare_exchange ||
           getOp() == AO__atomic_compare_exchange_n ||
           getOp() == AO__scoped_atomic_compare_exchange ||
           getOp() == AO__scoped_atomic_compare_exchange_n;
  }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return BuiltinLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == AtomicExprClass;
  }

  // Iterators
  child_range children() {
    return child_range(SubExprs, SubExprs + NumSubExprs);
  }
  const_child_range children() const {
    return const_child_range(SubExprs, SubExprs + NumSubExprs);
  }

  static std::unique_ptr<AtomicScopeModel> getScopeModel(AtomicOp Op) {
    if (Op >= AO__scoped_atomic_load && Op <= AO__scoped_atomic_fetch_max)
      return AtomicScopeModel::create(AtomicScopeModelKind::Generic);
    return AtomicScopeModel::create(AtomicScopeModelKind::None);
  }

  std::unique_ptr<AtomicScopeModel> getScopeModel() const {
    return getScopeModel(getOp());
  }
};

class TypoExpr : public Expr {
  // The location for the typo name.
  SourceLocation TypoLoc;

public:
  TypoExpr(QualType T, SourceLocation TypoLoc)
      : Expr(TypoExprClass, T, VK_LValue, OK_Ordinary), TypoLoc(TypoLoc) {
    assert(T->isDependentType() && "TypoExpr given a non-dependent type");
    setDependence(ExprDependence::TypeValueInstantiation |
                  ExprDependence::Error);
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return TypoLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return TypoLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == TypoExprClass;
  }
};

class RecoveryExpr final : public Expr,
                           private llvm::TrailingObjects<RecoveryExpr, Expr *> {
public:
  static RecoveryExpr *Create(TreeContext &Ctx, QualType T,
                              SourceLocation BeginLoc, SourceLocation EndLoc,
                              llvm::ArrayRef<Expr *> SubExprs);
  static RecoveryExpr *CreateEmpty(TreeContext &Ctx, unsigned NumSubExprs);

  llvm::ArrayRef<Expr *> subExpressions() {
    auto *B = getTrailingObjects<Expr *>();
    return llvm::ArrayRef(B, B + NumExprs);
  }

  llvm::ArrayRef<const Expr *> subExpressions() const {
    return const_cast<RecoveryExpr *>(this)->subExpressions();
  }

  child_range children() {
    Stmt **B = reinterpret_cast<Stmt **>(getTrailingObjects<Expr *>());
    return child_range(B, B + NumExprs);
  }

  SourceLocation getBeginLoc() const { return BeginLoc; }
  SourceLocation getEndLoc() const { return EndLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == RecoveryExprClass;
  }

private:
  RecoveryExpr(TreeContext &Ctx, QualType T, SourceLocation BeginLoc,
               SourceLocation EndLoc, llvm::ArrayRef<Expr *> SubExprs);
  RecoveryExpr(EmptyShell Empty, unsigned NumSubExprs)
      : Expr(RecoveryExprClass, Empty), NumExprs(NumSubExprs) {}

  size_t numTrailingObjects(OverloadToken<Stmt *>) const { return NumExprs; }

  SourceLocation BeginLoc, EndLoc;
  unsigned NumExprs;
  friend TrailingObjects;
};

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const Expr *E) {
  DB.AddTaggedVal(reinterpret_cast<uint64_t>(E->getType().getAsOpaquePtr()),
                  DiagnosticsEngine::ak_qualtype);
  return DB;
}

} // end namespace neverc

#endif // NEVERC_TREE_EXPR_H
