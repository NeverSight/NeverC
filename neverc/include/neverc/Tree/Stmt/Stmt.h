#ifndef NEVERC_AST_STMT_H
#define NEVERC_AST_STMT_H

#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/LangOpts/TypeTraits.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Decl/DeclGroup.h"
#include "neverc/Tree/Expr/OperationKinds.h"
#include "neverc/Tree/Stmt/StmtIterator.h"
#include "neverc/Tree/Type/DependenceFlags.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <string>

namespace llvm {

class FoldingSetNodeID;

} // namespace llvm

namespace neverc {

using llvm::cast;
using llvm::cast_or_null;
using llvm::dyn_cast;
using llvm::isa;
using llvm::isa_and_present;

class TreeContext;
class Attr;
class Decl;
class Expr;
class AddrLabelExpr;
class LabelDecl;
class PrinterHelper;
struct PrintingPolicy;
class StringLiteral;
class Token;
class VarDecl;
enum class CharacterLiteralKind;
enum class ConstantResultStorageKind;
enum class PredefinedIdentKind;
enum class SourceLocIdentKind;
enum class StringLiteralKind;

//===----------------------------------------------------------------------===//
// AST classes for statements.
//===----------------------------------------------------------------------===//

class alignas(void *) Stmt {
public:
  enum StmtClass {
    NoStmtClass = 0,
#define STMT(CLASS, PARENT) CLASS##Class,
#define STMT_RANGE(BASE, FIRST, LAST)                                          \
  first##BASE##Constant = FIRST##Class, last##BASE##Constant = LAST##Class,
#define LAST_STMT_RANGE(BASE, FIRST, LAST)                                     \
  first##BASE##Constant = FIRST##Class, last##BASE##Constant = LAST##Class
#define ABSTRACT_STMT(STMT)
#include "neverc/Tree/StmtNodes.td.h"
  };

  // Make vanilla 'new' and 'delete' illegal for Stmts.
protected:
  void *operator new(size_t bytes) noexcept {
    llvm_unreachable("Stmts cannot be allocated with regular 'new'.");
  }

  void operator delete(void *data) noexcept {
    llvm_unreachable("Stmts cannot be released with regular 'delete'.");
  }

  //===--- Statement bitfields classes ---===//

  class StmtBitfields {

    friend class Stmt;

    /// The statement class.
    LLVM_PREFERRED_TYPE(StmtClass)
    unsigned sClass : 8;
  };
  enum { NumStmtBits = 8 };

  class NullStmtBitfields {

    friend class NullStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// True if the null statement was preceded by an empty macro, e.g:
    /// @code
    ///   #define CALL(x)
    ///   CALL(0);
    /// @endcode
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasLeadingEmptyMacro : 1;

    /// The location of the semi-colon.
    SourceLocation SemiLoc;
  };

  class CompoundStmtBitfields {

    friend class CompoundStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// True if the compound statement has one or more pragmas that set some
    /// floating-point features.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasFPFeatures : 1;

    unsigned NumStmts;
  };

  class LabelStmtBitfields {
    friend class LabelStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    SourceLocation IdentLoc;
  };

  class AttributedStmtBitfields {

    friend class AttributedStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// Number of attributes.
    unsigned NumAttrs : 32 - NumStmtBits;

    /// The location of the attribute.
    SourceLocation AttrLoc;
  };

  class IfStmtBitfields {

    friend class IfStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// True if this if statement has storage for an else statement.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasElse : 1;

    /// True if this if statement has storage for a variable declaration.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasVar : 1;

    /// True if this if statement has storage for an init statement.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasInit : 1;

    /// The location of the "if".
    SourceLocation IfLoc;
  };

  class SwitchStmtBitfields {
    friend class SwitchStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// True if the SwitchStmt has storage for an init statement.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasInit : 1;

    /// True if the SwitchStmt has storage for a condition variable.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasVar : 1;

    /// If the SwitchStmt is a switch on an enum value, records whether all
    /// the enum values were covered by CaseStmts.  The coverage information
    /// value is meant to be a hint for possible clients.
    LLVM_PREFERRED_TYPE(bool)
    unsigned AllEnumCasesCovered : 1;

    /// The location of the "switch".
    SourceLocation SwitchLoc;
  };

  class WhileStmtBitfields {

    friend class WhileStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// True if the WhileStmt has storage for a condition variable.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasVar : 1;

    /// The location of the "while".
    SourceLocation WhileLoc;
  };

  class DoStmtBitfields {
    friend class DoStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// The location of the "do".
    SourceLocation DoLoc;
  };

  class ForStmtBitfields {
    friend class ForStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// The location of the "for".
    SourceLocation ForLoc;
  };

  class GotoStmtBitfields {
    friend class GotoStmt;
    friend class IndirectGotoStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// The location of the "goto".
    SourceLocation GotoLoc;
  };

  class ContinueStmtBitfields {
    friend class ContinueStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// The location of the "continue".
    SourceLocation ContinueLoc;
  };

  class BreakStmtBitfields {
    friend class BreakStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// The location of the "break".
    SourceLocation BreakLoc;
  };

  class ReturnStmtBitfields {
    friend class ReturnStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// True if this ReturnStmt has storage for an NRVO candidate.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasNRVOCandidate : 1;

    /// The location of the "return".
    SourceLocation RetLoc;
  };

  class SwitchCaseBitfields {
    friend class SwitchCase;
    friend class CaseStmt;

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    /// Used by CaseStmt to store whether it is a case statement
    /// of the form case LHS ... RHS (a GNU extension).
    LLVM_PREFERRED_TYPE(bool)
    unsigned CaseStmtIsGNURange : 1;

    /// The location of the "case" or "default" keyword.
    SourceLocation KeywordLoc;
  };

  //===--- Expression bitfields classes ---===//

  class ExprBitfields {

    friend class AtomicExpr;         // ctor
    friend class CallExpr;           // ctor
    friend class DeclRefExpr;        // computeDependence
    friend class DesignatedInitExpr; // ctor
    friend class Expr;
    friend class InitListExpr;      // ctor
    friend class OffsetOfExpr;      // ctor
    friend class OpaqueValueExpr;   // ctor
    friend class ParenListExpr;     // ctor
    friend class PseudoObjectExpr;  // ctor
    friend class ShuffleVectorExpr; // ctor

    LLVM_PREFERRED_TYPE(StmtBitfields)
    unsigned : NumStmtBits;

    LLVM_PREFERRED_TYPE(ExprValueKind)
    unsigned ValueKind : 2;
    LLVM_PREFERRED_TYPE(ExprObjectKind)
    unsigned ObjectKind : 3;
    LLVM_PREFERRED_TYPE(ExprDependence)
    unsigned Dependent : llvm::BitWidth<ExprDependence>;
  };
  enum { NumExprBits = NumStmtBits + 5 + llvm::BitWidth<ExprDependence> };

  class ConstantExprBitfields {

    friend class ConstantExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    /// The kind of result that is tail-allocated.
    LLVM_PREFERRED_TYPE(ConstantResultStorageKind)
    unsigned ResultKind : 2;

    /// The kind of Result as defined by APValue::ValueKind.
    LLVM_PREFERRED_TYPE(APValue::ValueKind)
    unsigned APValueKind : 4;

    /// When ResultKind == ConstantResultStorageKind::Int64, true if the
    /// tail-allocated integer is unsigned.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsUnsigned : 1;

    /// When ResultKind == ConstantResultStorageKind::Int64. the BitWidth of the
    /// tail-allocated integer. 7 bits because it is the minimal number of bits
    /// to represent a value from 0 to 64 (the size of the tail-allocated
    /// integer).
    unsigned BitWidth : 7;

    /// When ResultKind == ConstantResultStorageKind::APValue, true if the
    /// TreeContext will cleanup the tail-allocated APValue.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasCleanup : 1;

    /// True if this ConstantExpr was created for immediate invocation.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsImmediateInvocation : 1;
  };

  class PredefinedExprBitfields {

    friend class PredefinedExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    LLVM_PREFERRED_TYPE(PredefinedIdentKind)
    unsigned Kind : 4;

    /// True if this PredefinedExpr has a trailing "StringLiteral *"
    /// for the predefined identifier.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasFunctionName : 1;

    /// True if this PredefinedExpr should be treated as a StringLiteral (for
    /// MSVC compatibility).
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsTransparent : 1;

    /// The location of this PredefinedExpr.
    SourceLocation Loc;
  };

  class DeclRefExprBitfields {

    friend class DeclRefExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    LLVM_PREFERRED_TYPE(bool)
    unsigned HasFoundDecl : 1;
    LLVM_PREFERRED_TYPE(bool)
    unsigned HadMultipleCandidates : 1;
    LLVM_PREFERRED_TYPE(NonOdrUseReason)
    unsigned NonOdrUseReason : 2;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsImmediateEscalating : 1;

    /// The location of the declaration name itself.
    SourceLocation Loc;
  };

  class FloatingLiteralBitfields {
    friend class FloatingLiteral;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    static_assert(
        llvm::APFloat::S_MaxSemantics < 16,
        "Too many Semantics enum values to fit in bitfield of size 4");
    LLVM_PREFERRED_TYPE(llvm::APFloat::Semantics)
    unsigned Semantics : 4; // Provides semantics for APFloat construction
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsExact : 1;
  };

  class StringLiteralBitfields {

    friend class StringLiteral;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    /// The kind of this string literal.
    /// One of the enumeration values of StringLiteral::StringKind.
    LLVM_PREFERRED_TYPE(StringLiteralKind)
    unsigned Kind : 3;

    /// The width of a single character in bytes. Only values of 1, 2,
    /// and 4 bytes are supported. StringLiteral::mapCharByteWidth maps
    /// the target + string kind to the appropriate CharByteWidth.
    unsigned CharByteWidth : 3;

    LLVM_PREFERRED_TYPE(bool)
    unsigned IsPascal : 1;

    /// The number of concatenated token this string is made of.
    /// This is the number of trailing SourceLocation.
    unsigned NumConcatenated;
  };

  class CharacterLiteralBitfields {
    friend class CharacterLiteral;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    LLVM_PREFERRED_TYPE(CharacterLiteralKind)
    unsigned Kind : 3;
  };

  class UnaryOperatorBitfields {
    friend class UnaryOperator;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    LLVM_PREFERRED_TYPE(UnaryOperatorKind)
    unsigned Opc : 5;
    LLVM_PREFERRED_TYPE(bool)
    unsigned CanOverflow : 1;
    //
    /// This is only meaningful for operations on floating point
    /// types when additional values need to be in trailing storage.
    /// It is 0 otherwise.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasFPFeatures : 1;

    SourceLocation Loc;
  };

  class UnaryExprOrTypeTraitExprBitfields {
    friend class UnaryExprOrTypeTraitExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    LLVM_PREFERRED_TYPE(UnaryExprOrTypeTrait)
    unsigned Kind : 3;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsType : 1; // true if operand is a type, false if an expression.
  };

  class ArrayOrMatrixSubscriptExprBitfields {
    friend class ArraySubscriptExpr;
    friend class MatrixSubscriptExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    SourceLocation RBracketLoc;
  };

  class CallExprBitfields {
    friend class CallExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    unsigned NumPreArgs : 1;

    /// True if the call expression has some floating-point features.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasFPFeatures : 1;

    /// Padding used to align OffsetToTrailingObjects to a byte multiple.
    unsigned : 24 - 3 - NumExprBits;

    /// The offset in bytes from the this pointer to the start of the
    /// trailing objects belonging to CallExpr. Intentionally byte sized
    /// for faster access.
    unsigned OffsetToTrailingObjects : 8;
  };
  enum { NumCallExprBits = 32 };

  class MemberExprBitfields {

    friend class MemberExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    LLVM_PREFERRED_TYPE(bool)
    unsigned IsArrow : 1;

    /// True when the found decl differs from the member decl.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasQualifierOrFoundDecl : 1;

    LLVM_PREFERRED_TYPE(bool)
    unsigned HadMultipleCandidates : 1;

    LLVM_PREFERRED_TYPE(NonOdrUseReason)
    unsigned NonOdrUseReason : 2;

    /// This is the location of the -> or . in the expression.
    SourceLocation OperatorLoc;
  };

  class CastExprBitfields {
    friend class CastExpr;
    friend class ImplicitCastExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    LLVM_PREFERRED_TYPE(CastKind)
    unsigned Kind : 7;
    LLVM_PREFERRED_TYPE(bool)
    unsigned PartOfExplicitCast : 1; // Only set for ImplicitCastExpr.

    LLVM_PREFERRED_TYPE(bool)
    unsigned HasFPFeatures : 1;
  };

  class BinaryOperatorBitfields {
    friend class BinaryOperator;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    LLVM_PREFERRED_TYPE(BinaryOperatorKind)
    unsigned Opc : 6;

    /// This is only meaningful for operations on floating point
    /// types when additional values need to be in trailing storage.
    /// It is 0 otherwise.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasFPFeatures : 1;

    SourceLocation OpLoc;
  };

  class InitListExprBitfields {
    friend class InitListExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    /// Whether this initializer list originally had a GNU array-range
    /// designator in it. This is a temporary marker used by CodeGen.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HadArrayRangeDesignator : 1;
  };

  class ParenListExprBitfields {

    friend class ParenListExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    /// The number of expressions in the paren list.
    unsigned NumExprs;
  };

  class GenericSelectionExprBitfields {

    friend class GenericSelectionExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    /// The location of the "_Generic".
    SourceLocation GenericLoc;
  };

  class PseudoObjectExprBitfields {

    friend class PseudoObjectExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    unsigned NumSubExprs : 16;
    unsigned ResultIndex : 16;
  };

  class SourceLocExprBitfields {

    friend class SourceLocExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    /// The kind of source location builtin represented by the SourceLocExpr.
    /// Ex. __builtin_LINE, __builtin_FUNCTION, etc.
    LLVM_PREFERRED_TYPE(SourceLocIdentKind)
    unsigned Kind : 3;
  };

  class StmtExprBitfields {

    friend class StmtExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;
  };

  //===--- Null pointer literal (C23) ---===//

  class NullPtrLiteralExprBitfields {
    friend class NullPtrLiteralExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    /// The location of the null pointer literal.
    SourceLocation Loc;
  };

  class ExprWithCleanupsBitfields {

    friend class ExprWithCleanups;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    // When false, it must not have side effects.
    LLVM_PREFERRED_TYPE(bool)
    unsigned CleanupsHaveSideEffects : 1;

    unsigned NumObjects : 32 - 1 - NumExprBits;
  };

  //===--- NeverC Extensions bitfields classes ---===//

  class OpaqueValueExprBitfields {

    friend class OpaqueValueExpr;

    LLVM_PREFERRED_TYPE(ExprBitfields)
    unsigned : NumExprBits;

    /// The OVE is a unique semantic reference to its source expression if this
    /// bit is set to true.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsUnique : 1;

    SourceLocation Loc;
  };

  union {
    // Same order as in StmtNodes.td.
    // Statements
    StmtBitfields StmtBits;
    NullStmtBitfields NullStmtBits;
    CompoundStmtBitfields CompoundStmtBits;
    LabelStmtBitfields LabelStmtBits;
    AttributedStmtBitfields AttributedStmtBits;
    IfStmtBitfields IfStmtBits;
    SwitchStmtBitfields SwitchStmtBits;
    WhileStmtBitfields WhileStmtBits;
    DoStmtBitfields DoStmtBits;
    ForStmtBitfields ForStmtBits;
    GotoStmtBitfields GotoStmtBits;
    ContinueStmtBitfields ContinueStmtBits;
    BreakStmtBitfields BreakStmtBits;
    ReturnStmtBitfields ReturnStmtBits;
    SwitchCaseBitfields SwitchCaseBits;

    // Expressions
    ExprBitfields ExprBits;
    ConstantExprBitfields ConstantExprBits;
    PredefinedExprBitfields PredefinedExprBits;
    DeclRefExprBitfields DeclRefExprBits;
    FloatingLiteralBitfields FloatingLiteralBits;
    StringLiteralBitfields StringLiteralBits;
    CharacterLiteralBitfields CharacterLiteralBits;
    UnaryOperatorBitfields UnaryOperatorBits;
    UnaryExprOrTypeTraitExprBitfields UnaryExprOrTypeTraitExprBits;
    ArrayOrMatrixSubscriptExprBitfields ArrayOrMatrixSubscriptExprBits;
    CallExprBitfields CallExprBits;
    MemberExprBitfields MemberExprBits;
    CastExprBitfields CastExprBits;
    BinaryOperatorBitfields BinaryOperatorBits;
    InitListExprBitfields InitListExprBits;
    ParenListExprBitfields ParenListExprBits;
    GenericSelectionExprBitfields GenericSelectionExprBits;
    PseudoObjectExprBitfields PseudoObjectExprBits;
    SourceLocExprBitfields SourceLocExprBits;

    // GNU Extensions.
    StmtExprBitfields StmtExprBits;

    // Shared expression shapes (e.g. cleanups, nullptr literal)
    NullPtrLiteralExprBitfields NullPtrLiteralExprBits;
    ExprWithCleanupsBitfields ExprWithCleanupsBits;

    // NeverC Extensions
    OpaqueValueExprBitfields OpaqueValueExprBits;
  };

public:
  // Only allow allocation of Stmts using the allocator in TreeContext
  // or by doing a placement new.
  void *operator new(size_t bytes, const TreeContext &C,
                     unsigned alignment = 8);

  void *operator new(size_t bytes, const TreeContext *C,
                     unsigned alignment = 8) {
    return operator new(bytes, *C, alignment);
  }

  void *operator new(size_t bytes, void *mem) noexcept { return mem; }

  void operator delete(void *, const TreeContext &, unsigned) noexcept {}
  void operator delete(void *, const TreeContext *, unsigned) noexcept {}
  void operator delete(void *, size_t) noexcept {}
  void operator delete(void *, void *) noexcept {}

public:
  struct EmptyShell {};

  enum Likelihood {
    LH_Unlikely = -1, ///< Branch has the [[unlikely]] attribute.
    LH_None,          ///< No attribute set or branches of the IfStmt have
                      ///< the same attribute.
    LH_Likely         ///< Branch has the [[likely]] attribute.
  };

protected:
  template <typename T, typename TPtr = T *, typename StmtPtr = Stmt *>
  struct CastIterator
      : llvm::iterator_adaptor_base<CastIterator<T, TPtr, StmtPtr>, StmtPtr *,
                                    std::random_access_iterator_tag, TPtr> {
    using Base = typename CastIterator::iterator_adaptor_base;

    CastIterator() : Base(nullptr) {}
    CastIterator(StmtPtr *I) : Base(I) {}

    typename Base::value_type operator*() const {
      return cast_or_null<T>(*this->I);
    }
  };

  template <typename T>
  using ConstCastIterator = CastIterator<T, const T *const, const Stmt *const>;

  using ExprIterator = CastIterator<Expr>;
  using ConstExprIterator = ConstCastIterator<Expr>;

private:
  static bool StatisticsEnabled;

protected:
  explicit Stmt(StmtClass SC, EmptyShell) : Stmt(SC) {}

public:
  Stmt() = delete;
  Stmt(const Stmt &) = delete;
  Stmt(Stmt &&) = delete;
  Stmt &operator=(const Stmt &) = delete;
  Stmt &operator=(Stmt &&) = delete;

  Stmt(StmtClass SC) {
    static_assert(sizeof(*this) <= 8,
                  "changing bitfields changed sizeof(Stmt)");
    static_assert(sizeof(*this) % alignof(void *) == 0,
                  "Insufficient alignment!");
    StmtBits.sClass = SC;
    if (StatisticsEnabled)
      Stmt::addStmtClass(SC);
  }

  StmtClass getStmtClass() const {
    return static_cast<StmtClass>(StmtBits.sClass);
  }

  const char *getStmtClassName() const;

  SourceRange getSourceRange() const LLVM_READONLY;
  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  // global temp stats (until we have a per-module visitor)
  static void addStmtClass(const StmtClass s);
  static void EnableStatistics();
  static void PrintStats();

  static Likelihood getLikelihood(llvm::ArrayRef<const Attr *> Attrs);

  static Likelihood getLikelihood(const Stmt *S);

  static const Attr *getLikelihoodAttr(const Stmt *S);

  static Likelihood getLikelihood(const Stmt *Then, const Stmt *Else);

  static std::tuple<bool, const Attr *, const Attr *>
  determineLikelihoodConflict(const Stmt *Then, const Stmt *Else);

  void dump() const;
  void dump(llvm::raw_ostream &OS, const TreeContext &Context) const;

  int64_t getID(const TreeContext &Context) const;

  void dumpPretty(const TreeContext &Context) const;
  void printPretty(llvm::raw_ostream &OS, PrinterHelper *Helper,
                   const PrintingPolicy &Policy, unsigned Indentation = 0,
                   llvm::StringRef NewlineSymbol = "\n") const;
  void printPrettyControlled(llvm::raw_ostream &OS, PrinterHelper *Helper,
                             const PrintingPolicy &Policy,
                             unsigned Indentation = 0,
                             llvm::StringRef NewlineSymbol = "\n") const;

  Stmt *IgnoreContainers(bool IgnoreCaptured = false);
  const Stmt *IgnoreContainers(bool IgnoreCaptured = false) const {
    return const_cast<Stmt *>(this)->IgnoreContainers(IgnoreCaptured);
  }

  const Stmt *stripLabelLikeStatements() const;
  Stmt *stripLabelLikeStatements() {
    return const_cast<Stmt *>(
        const_cast<const Stmt *>(this)->stripLabelLikeStatements());
  }

  using child_iterator = StmtIterator;
  using const_child_iterator = ConstStmtIterator;

  using child_range = llvm::iterator_range<child_iterator>;
  using const_child_range = llvm::iterator_range<const_child_iterator>;

  child_range children();

  const_child_range children() const {
    auto Children = const_cast<Stmt *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_iterator child_begin() { return children().begin(); }
  child_iterator child_end() { return children().end(); }

  const_child_iterator child_begin() const { return children().begin(); }
  const_child_iterator child_end() const { return children().end(); }

  void Profile(llvm::FoldingSetNodeID &ID, const TreeContext &Context,
               bool Canonical) const;
};

class DeclStmt : public Stmt {
  DeclGroupRef DG;
  SourceLocation StartLoc, EndLoc;

public:
  DeclStmt(DeclGroupRef dg, SourceLocation startLoc, SourceLocation endLoc)
      : Stmt(DeclStmtClass), DG(dg), StartLoc(startLoc), EndLoc(endLoc) {}

  explicit DeclStmt(EmptyShell Empty) : Stmt(DeclStmtClass, Empty) {}

  bool isSingleDecl() const { return DG.isSingleDecl(); }

  const Decl *getSingleDecl() const { return DG.getSingleDecl(); }
  Decl *getSingleDecl() { return DG.getSingleDecl(); }

  const DeclGroupRef getDeclGroup() const { return DG; }
  DeclGroupRef getDeclGroup() { return DG; }
  void setDeclGroup(DeclGroupRef DGR) { DG = DGR; }

  void setStartLoc(SourceLocation L) { StartLoc = L; }
  SourceLocation getEndLoc() const { return EndLoc; }
  void setEndLoc(SourceLocation L) { EndLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return StartLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DeclStmtClass;
  }

  // Iterators over subexpressions.
  child_range children() {
    return child_range(child_iterator(DG.begin(), DG.end()),
                       child_iterator(DG.end(), DG.end()));
  }

  const_child_range children() const {
    auto Children = const_cast<DeclStmt *>(this)->children();
    return const_child_range(Children);
  }

  using decl_iterator = DeclGroupRef::iterator;
  using const_decl_iterator = DeclGroupRef::const_iterator;
  using decl_range = llvm::iterator_range<decl_iterator>;
  using decl_const_range = llvm::iterator_range<const_decl_iterator>;

  decl_range decls() { return decl_range(decl_begin(), decl_end()); }

  decl_const_range decls() const {
    return decl_const_range(decl_begin(), decl_end());
  }

  decl_iterator decl_begin() { return DG.begin(); }
  decl_iterator decl_end() { return DG.end(); }
  const_decl_iterator decl_begin() const { return DG.begin(); }
  const_decl_iterator decl_end() const { return DG.end(); }

  using reverse_decl_iterator = std::reverse_iterator<decl_iterator>;

  reverse_decl_iterator decl_rbegin() {
    return reverse_decl_iterator(decl_end());
  }

  reverse_decl_iterator decl_rend() {
    return reverse_decl_iterator(decl_begin());
  }
};

class NullStmt : public Stmt {
public:
  NullStmt(SourceLocation L, bool hasLeadingEmptyMacro = false)
      : Stmt(NullStmtClass) {
    NullStmtBits.HasLeadingEmptyMacro = hasLeadingEmptyMacro;
    setSemiLoc(L);
  }

  explicit NullStmt(EmptyShell Empty) : Stmt(NullStmtClass, Empty) {}

  SourceLocation getSemiLoc() const { return NullStmtBits.SemiLoc; }
  void setSemiLoc(SourceLocation L) { NullStmtBits.SemiLoc = L; }

  bool hasLeadingEmptyMacro() const {
    return NullStmtBits.HasLeadingEmptyMacro;
  }

  SourceLocation getBeginLoc() const { return getSemiLoc(); }
  SourceLocation getEndLoc() const { return getSemiLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == NullStmtClass;
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class CompoundStmt final
    : public Stmt,
      private llvm::TrailingObjects<CompoundStmt, Stmt *, FPOptionsOverride> {

  friend TrailingObjects;

  SourceLocation LBraceLoc;

  SourceLocation RBraceLoc;

  CompoundStmt(llvm::ArrayRef<Stmt *> Stmts, FPOptionsOverride FPFeatures,
               SourceLocation LB, SourceLocation RB);
  explicit CompoundStmt(EmptyShell Empty) : Stmt(CompoundStmtClass, Empty) {}

  void setStmts(llvm::ArrayRef<Stmt *> Stmts);

  void setStoredFPFeatures(FPOptionsOverride F) {
    assert(hasStoredFPFeatures());
    *getTrailingObjects<FPOptionsOverride>() = F;
  }

  size_t numTrailingObjects(OverloadToken<Stmt *>) const {
    return CompoundStmtBits.NumStmts;
  }

public:
  static CompoundStmt *Create(const TreeContext &C,
                              llvm::ArrayRef<Stmt *> Stmts,
                              FPOptionsOverride FPFeatures, SourceLocation LB,
                              SourceLocation RB);

  // Build an empty compound statement with a location.
  explicit CompoundStmt(SourceLocation Loc)
      : Stmt(CompoundStmtClass), LBraceLoc(Loc), RBraceLoc(Loc) {
    CompoundStmtBits.NumStmts = 0;
    CompoundStmtBits.HasFPFeatures = 0;
  }

  // Build an empty compound statement.
  static CompoundStmt *CreateEmpty(const TreeContext &C, unsigned NumStmts,
                                   bool HasFPFeatures);

  bool body_empty() const { return CompoundStmtBits.NumStmts == 0; }
  unsigned size() const { return CompoundStmtBits.NumStmts; }

  bool hasStoredFPFeatures() const { return CompoundStmtBits.HasFPFeatures; }

  FPOptionsOverride getStoredFPFeatures() const {
    assert(hasStoredFPFeatures());
    return *getTrailingObjects<FPOptionsOverride>();
  }

  using body_iterator = Stmt **;
  using body_range = llvm::iterator_range<body_iterator>;

  body_range body() { return body_range(body_begin(), body_end()); }
  body_iterator body_begin() { return getTrailingObjects<Stmt *>(); }
  body_iterator body_end() { return body_begin() + size(); }
  Stmt *body_front() { return !body_empty() ? body_begin()[0] : nullptr; }

  Stmt *body_back() {
    return !body_empty() ? body_begin()[size() - 1] : nullptr;
  }

  using const_body_iterator = Stmt *const *;
  using body_const_range = llvm::iterator_range<const_body_iterator>;

  body_const_range body() const {
    return body_const_range(body_begin(), body_end());
  }

  const_body_iterator body_begin() const {
    return getTrailingObjects<Stmt *>();
  }

  const_body_iterator body_end() const { return body_begin() + size(); }

  const Stmt *body_front() const {
    return !body_empty() ? body_begin()[0] : nullptr;
  }

  const Stmt *body_back() const {
    return !body_empty() ? body_begin()[size() - 1] : nullptr;
  }

  using reverse_body_iterator = std::reverse_iterator<body_iterator>;

  reverse_body_iterator body_rbegin() {
    return reverse_body_iterator(body_end());
  }

  reverse_body_iterator body_rend() {
    return reverse_body_iterator(body_begin());
  }

  using const_reverse_body_iterator =
      std::reverse_iterator<const_body_iterator>;

  const_reverse_body_iterator body_rbegin() const {
    return const_reverse_body_iterator(body_end());
  }

  const_reverse_body_iterator body_rend() const {
    return const_reverse_body_iterator(body_begin());
  }

  // Get the Stmt that StmtExpr would consider to be the result of this
  // compound statement. This is used by StmtExpr to properly emulate the GCC
  // compound expression extension, which ignores trailing NullStmts when
  // getting the result of the expression.
  // i.e. ({ 5;;; })
  //           ^^ ignored
  // If we don't find something that isn't a NullStmt, just return the last
  // Stmt.
  Stmt *getStmtExprResult() {
    for (auto *B : llvm::reverse(body())) {
      if (!isa<NullStmt>(B))
        return B;
    }
    return body_back();
  }

  const Stmt *getStmtExprResult() const {
    return const_cast<CompoundStmt *>(this)->getStmtExprResult();
  }

  SourceLocation getBeginLoc() const { return LBraceLoc; }
  SourceLocation getEndLoc() const { return RBraceLoc; }

  SourceLocation getLBracLoc() const { return LBraceLoc; }
  SourceLocation getRBracLoc() const { return RBraceLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CompoundStmtClass;
  }

  // Iterators
  child_range children() { return child_range(body_begin(), body_end()); }

  const_child_range children() const {
    return const_child_range(body_begin(), body_end());
  }
};

// SwitchCase is the base class for CaseStmt and DefaultStmt,
class SwitchCase : public Stmt {
protected:
  SourceLocation ColonLoc;

  // The location of the "case" or "default" keyword. Stored in SwitchCaseBits.
  // SourceLocation KeywordLoc;

  SwitchCase *NextSwitchCase = nullptr;

  SwitchCase(StmtClass SC, SourceLocation KWLoc, SourceLocation ColonLoc)
      : Stmt(SC), ColonLoc(ColonLoc) {
    setKeywordLoc(KWLoc);
  }

  SwitchCase(StmtClass SC, EmptyShell) : Stmt(SC) {}

public:
  const SwitchCase *getNextSwitchCase() const { return NextSwitchCase; }
  SwitchCase *getNextSwitchCase() { return NextSwitchCase; }
  void setNextSwitchCase(SwitchCase *SC) { NextSwitchCase = SC; }

  SourceLocation getKeywordLoc() const { return SwitchCaseBits.KeywordLoc; }
  void setKeywordLoc(SourceLocation L) { SwitchCaseBits.KeywordLoc = L; }
  SourceLocation getColonLoc() const { return ColonLoc; }
  void setColonLoc(SourceLocation L) { ColonLoc = L; }

  inline Stmt *getSubStmt();
  const Stmt *getSubStmt() const {
    return const_cast<SwitchCase *>(this)->getSubStmt();
  }

  SourceLocation getBeginLoc() const { return getKeywordLoc(); }
  inline SourceLocation getEndLoc() const LLVM_READONLY;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CaseStmtClass ||
           T->getStmtClass() == DefaultStmtClass;
  }
};

class CaseStmt final
    : public SwitchCase,
      private llvm::TrailingObjects<CaseStmt, Stmt *, SourceLocation> {
  friend TrailingObjects;

  // CaseStmt is followed by several trailing objects, some of which optional.
  // Note that it would be more convenient to put the optional trailing objects
  // at the end but this would impact children().
  // The trailing objects are in order:
  //
  // * A "Stmt *" for the LHS of the case statement. Always present.
  //
  // * A "Stmt *" for the RHS of the case statement. This is a GNU extension
  //   which allow ranges in cases statement of the form LHS ... RHS.
  //   Present if and only if caseStmtIsGNURange() is true.
  //
  // * A "Stmt *" for the substatement of the case statement. Always present.
  //
  // * A SourceLocation for the location of the ... if this is a case statement
  //   with a range. Present if and only if caseStmtIsGNURange() is true.
  enum { LhsOffset = 0, SubStmtOffsetFromRhs = 1 };
  enum { NumMandatoryStmtPtr = 2 };

  unsigned numTrailingObjects(OverloadToken<Stmt *>) const {
    return NumMandatoryStmtPtr + caseStmtIsGNURange();
  }

  unsigned numTrailingObjects(OverloadToken<SourceLocation>) const {
    return caseStmtIsGNURange();
  }

  unsigned lhsOffset() const { return LhsOffset; }
  unsigned rhsOffset() const { return LhsOffset + caseStmtIsGNURange(); }
  unsigned subStmtOffset() const { return rhsOffset() + SubStmtOffsetFromRhs; }

  CaseStmt(Expr *lhs, Expr *rhs, SourceLocation caseLoc,
           SourceLocation ellipsisLoc, SourceLocation colonLoc)
      : SwitchCase(CaseStmtClass, caseLoc, colonLoc) {
    // Handle GNU case statements of the form LHS ... RHS.
    bool IsGNURange = rhs != nullptr;
    SwitchCaseBits.CaseStmtIsGNURange = IsGNURange;
    setLHS(lhs);
    setSubStmt(nullptr);
    if (IsGNURange) {
      setRHS(rhs);
      setEllipsisLoc(ellipsisLoc);
    }
  }

  explicit CaseStmt(EmptyShell Empty, bool CaseStmtIsGNURange)
      : SwitchCase(CaseStmtClass, Empty) {
    SwitchCaseBits.CaseStmtIsGNURange = CaseStmtIsGNURange;
  }

public:
  static CaseStmt *Create(const TreeContext &Ctx, Expr *lhs, Expr *rhs,
                          SourceLocation caseLoc, SourceLocation ellipsisLoc,
                          SourceLocation colonLoc);

  static CaseStmt *CreateEmpty(const TreeContext &Ctx, bool CaseStmtIsGNURange);

  bool caseStmtIsGNURange() const { return SwitchCaseBits.CaseStmtIsGNURange; }

  SourceLocation getCaseLoc() const { return getKeywordLoc(); }
  void setCaseLoc(SourceLocation L) { setKeywordLoc(L); }

  SourceLocation getEllipsisLoc() const {
    return caseStmtIsGNURange() ? *getTrailingObjects<SourceLocation>()
                                : SourceLocation();
  }

  void setEllipsisLoc(SourceLocation L) {
    assert(
        caseStmtIsGNURange() &&
        "setEllipsisLoc but this is not a case stmt of the form LHS ... RHS!");
    *getTrailingObjects<SourceLocation>() = L;
  }

  Expr *getLHS() {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[lhsOffset()]);
  }

  const Expr *getLHS() const {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[lhsOffset()]);
  }

  void setLHS(Expr *Val) {
    getTrailingObjects<Stmt *>()[lhsOffset()] = reinterpret_cast<Stmt *>(Val);
  }

  Expr *getRHS() {
    return caseStmtIsGNURange() ? reinterpret_cast<Expr *>(
                                      getTrailingObjects<Stmt *>()[rhsOffset()])
                                : nullptr;
  }

  const Expr *getRHS() const {
    return caseStmtIsGNURange() ? reinterpret_cast<Expr *>(
                                      getTrailingObjects<Stmt *>()[rhsOffset()])
                                : nullptr;
  }

  void setRHS(Expr *Val) {
    assert(caseStmtIsGNURange() &&
           "setRHS but this is not a case stmt of the form LHS ... RHS!");
    getTrailingObjects<Stmt *>()[rhsOffset()] = reinterpret_cast<Stmt *>(Val);
  }

  Stmt *getSubStmt() { return getTrailingObjects<Stmt *>()[subStmtOffset()]; }
  const Stmt *getSubStmt() const {
    return getTrailingObjects<Stmt *>()[subStmtOffset()];
  }

  void setSubStmt(Stmt *S) {
    getTrailingObjects<Stmt *>()[subStmtOffset()] = S;
  }

  SourceLocation getBeginLoc() const { return getKeywordLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    // Handle deeply nested case statements with iteration instead of recursion.
    const CaseStmt *CS = this;
    while (const auto *CS2 = dyn_cast<CaseStmt>(CS->getSubStmt()))
      CS = CS2;

    return CS->getSubStmt()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CaseStmtClass;
  }

  // Iterators
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

class DefaultStmt : public SwitchCase {
  Stmt *SubStmt;

public:
  DefaultStmt(SourceLocation DL, SourceLocation CL, Stmt *substmt)
      : SwitchCase(DefaultStmtClass, DL, CL), SubStmt(substmt) {}

  explicit DefaultStmt(EmptyShell Empty)
      : SwitchCase(DefaultStmtClass, Empty) {}

  Stmt *getSubStmt() { return SubStmt; }
  const Stmt *getSubStmt() const { return SubStmt; }
  void setSubStmt(Stmt *S) { SubStmt = S; }

  SourceLocation getDefaultLoc() const { return getKeywordLoc(); }
  void setDefaultLoc(SourceLocation L) { setKeywordLoc(L); }

  SourceLocation getBeginLoc() const { return getKeywordLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return SubStmt->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DefaultStmtClass;
  }

  // Iterators
  child_range children() { return child_range(&SubStmt, &SubStmt + 1); }

  const_child_range children() const {
    return const_child_range(&SubStmt, &SubStmt + 1);
  }
};

SourceLocation SwitchCase::getEndLoc() const {
  if (const auto *CS = dyn_cast<CaseStmt>(this))
    return CS->getEndLoc();
  else if (const auto *DS = dyn_cast<DefaultStmt>(this))
    return DS->getEndLoc();
  llvm_unreachable("SwitchCase is neither a CaseStmt nor a DefaultStmt!");
}

Stmt *SwitchCase::getSubStmt() {
  if (auto *CS = dyn_cast<CaseStmt>(this))
    return CS->getSubStmt();
  else if (auto *DS = dyn_cast<DefaultStmt>(this))
    return DS->getSubStmt();
  llvm_unreachable("SwitchCase is neither a CaseStmt nor a DefaultStmt!");
}

class ValueStmt : public Stmt {
protected:
  using Stmt::Stmt;

public:
  const Expr *getExprStmt() const;
  Expr *getExprStmt() {
    const ValueStmt *ConstThis = this;
    return const_cast<Expr *>(ConstThis->getExprStmt());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstValueStmtConstant &&
           T->getStmtClass() <= lastValueStmtConstant;
  }
};

class LabelStmt : public ValueStmt {
  LabelDecl *TheDecl;
  Stmt *SubStmt;
  bool SideEntry = false;

public:
  LabelStmt(SourceLocation IL, LabelDecl *D, Stmt *substmt)
      : ValueStmt(LabelStmtClass), TheDecl(D), SubStmt(substmt) {
    setIdentLoc(IL);
  }

  explicit LabelStmt(EmptyShell Empty) : ValueStmt(LabelStmtClass, Empty) {}

  SourceLocation getIdentLoc() const { return LabelStmtBits.IdentLoc; }
  void setIdentLoc(SourceLocation L) { LabelStmtBits.IdentLoc = L; }

  LabelDecl *getDecl() const { return TheDecl; }
  void setDecl(LabelDecl *D) { TheDecl = D; }

  const char *getName() const;
  Stmt *getSubStmt() { return SubStmt; }

  const Stmt *getSubStmt() const { return SubStmt; }
  void setSubStmt(Stmt *SS) { SubStmt = SS; }

  SourceLocation getBeginLoc() const { return getIdentLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return SubStmt->getEndLoc();
  }

  child_range children() { return child_range(&SubStmt, &SubStmt + 1); }

  const_child_range children() const {
    return const_child_range(&SubStmt, &SubStmt + 1);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == LabelStmtClass;
  }
  bool isSideEntry() const { return SideEntry; }
  void setSideEntry(bool SE) { SideEntry = SE; }
};

class AttributedStmt final
    : public ValueStmt,
      private llvm::TrailingObjects<AttributedStmt, const Attr *> {

  friend TrailingObjects;

  Stmt *SubStmt;

  AttributedStmt(SourceLocation Loc, llvm::ArrayRef<const Attr *> Attrs,
                 Stmt *SubStmt)
      : ValueStmt(AttributedStmtClass), SubStmt(SubStmt) {
    AttributedStmtBits.NumAttrs = Attrs.size();
    AttributedStmtBits.AttrLoc = Loc;
    std::copy(Attrs.begin(), Attrs.end(), getAttrArrayPtr());
  }

  explicit AttributedStmt(EmptyShell Empty, unsigned NumAttrs)
      : ValueStmt(AttributedStmtClass, Empty) {
    AttributedStmtBits.NumAttrs = NumAttrs;
    AttributedStmtBits.AttrLoc = SourceLocation{};
    std::fill_n(getAttrArrayPtr(), NumAttrs, nullptr);
  }

  const Attr *const *getAttrArrayPtr() const {
    return getTrailingObjects<const Attr *>();
  }
  const Attr **getAttrArrayPtr() { return getTrailingObjects<const Attr *>(); }

public:
  static AttributedStmt *Create(const TreeContext &C, SourceLocation Loc,
                                llvm::ArrayRef<const Attr *> Attrs,
                                Stmt *SubStmt);

  // Build an empty attributed statement.
  static AttributedStmt *CreateEmpty(const TreeContext &C, unsigned NumAttrs);

  SourceLocation getAttrLoc() const { return AttributedStmtBits.AttrLoc; }
  llvm::ArrayRef<const Attr *> getAttrs() const {
    return llvm::ArrayRef(getAttrArrayPtr(), AttributedStmtBits.NumAttrs);
  }

  Stmt *getSubStmt() { return SubStmt; }
  const Stmt *getSubStmt() const { return SubStmt; }

  SourceLocation getBeginLoc() const { return getAttrLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return SubStmt->getEndLoc();
  }

  child_range children() { return child_range(&SubStmt, &SubStmt + 1); }

  const_child_range children() const {
    return const_child_range(&SubStmt, &SubStmt + 1);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == AttributedStmtClass;
  }
};

class IfStmt final
    : public Stmt,
      private llvm::TrailingObjects<IfStmt, Stmt *, SourceLocation> {
  friend TrailingObjects;

  // IfStmt is followed by several trailing objects, some of which optional.
  // Note that it would be more convenient to put the optional trailing
  // objects at then end but this would change the order of the children.
  // The trailing objects are in order:
  //
  // * A "Stmt *" for the init statement.
  //    Present if and only if hasInitStorage().
  //
  // * A "Stmt *" for the condition variable.
  //    Present if and only if hasVarStorage(). This is in fact a "DeclStmt *".
  //
  // * A "Stmt *" for the condition.
  //    Always present. This is in fact a "Expr *".
  //
  // * A "Stmt *" for the then statement.
  //    Always present.
  //
  // * A "Stmt *" for the else statement.
  //    Present if and only if hasElseStorage().
  //
  // * A "SourceLocation" for the location of the "else".
  //    Present if and only if hasElseStorage().
  enum { InitOffset = 0, ThenOffsetFromCond = 1, ElseOffsetFromCond = 2 };
  enum { NumMandatoryStmtPtr = 2 };
  SourceLocation LParenLoc;
  SourceLocation RParenLoc;

  unsigned numTrailingObjects(OverloadToken<Stmt *>) const {
    return NumMandatoryStmtPtr + hasElseStorage() + hasVarStorage() +
           hasInitStorage();
  }

  unsigned numTrailingObjects(OverloadToken<SourceLocation>) const {
    return hasElseStorage();
  }

  unsigned initOffset() const { return InitOffset; }
  unsigned varOffset() const { return InitOffset + hasInitStorage(); }
  unsigned condOffset() const {
    return InitOffset + hasInitStorage() + hasVarStorage();
  }
  unsigned thenOffset() const { return condOffset() + ThenOffsetFromCond; }
  unsigned elseOffset() const { return condOffset() + ElseOffsetFromCond; }

  IfStmt(const TreeContext &Ctx, SourceLocation IL, Stmt *Init, VarDecl *Var,
         Expr *Cond, SourceLocation LParenLoc, SourceLocation RParenLoc,
         Stmt *Then, SourceLocation EL, Stmt *Else);

  explicit IfStmt(EmptyShell Empty, bool HasElse, bool HasVar, bool HasInit);

public:
  static IfStmt *Create(const TreeContext &Ctx, SourceLocation IL, Stmt *Init,
                        VarDecl *Var, Expr *Cond, SourceLocation LPL,
                        SourceLocation RPL, Stmt *Then,
                        SourceLocation EL = SourceLocation(),
                        Stmt *Else = nullptr);

  static IfStmt *CreateEmpty(const TreeContext &Ctx, bool HasElse, bool HasVar,
                             bool HasInit);

  bool hasInitStorage() const { return IfStmtBits.HasInit; }

  bool hasVarStorage() const { return IfStmtBits.HasVar; }

  bool hasElseStorage() const { return IfStmtBits.HasElse; }

  Expr *getCond() {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  const Expr *getCond() const {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  void setCond(Expr *Cond) {
    getTrailingObjects<Stmt *>()[condOffset()] = reinterpret_cast<Stmt *>(Cond);
  }

  Stmt *getThen() { return getTrailingObjects<Stmt *>()[thenOffset()]; }
  const Stmt *getThen() const {
    return getTrailingObjects<Stmt *>()[thenOffset()];
  }

  void setThen(Stmt *Then) {
    getTrailingObjects<Stmt *>()[thenOffset()] = Then;
  }

  Stmt *getElse() {
    return hasElseStorage() ? getTrailingObjects<Stmt *>()[elseOffset()]
                            : nullptr;
  }

  const Stmt *getElse() const {
    return hasElseStorage() ? getTrailingObjects<Stmt *>()[elseOffset()]
                            : nullptr;
  }

  void setElse(Stmt *Else) {
    assert(hasElseStorage() &&
           "This if statement has no storage for an else statement!");
    getTrailingObjects<Stmt *>()[elseOffset()] = Else;
  }

  VarDecl *getConditionVariable();
  const VarDecl *getConditionVariable() const {
    return const_cast<IfStmt *>(this)->getConditionVariable();
  }

  void setConditionVariable(const TreeContext &Ctx, VarDecl *V);

  DeclStmt *getConditionVariableDeclStmt() {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  const DeclStmt *getConditionVariableDeclStmt() const {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  void setConditionVariableDeclStmt(DeclStmt *CondVar) {
    assert(hasVarStorage());
    getTrailingObjects<Stmt *>()[varOffset()] = CondVar;
  }

  Stmt *getInit() {
    return hasInitStorage() ? getTrailingObjects<Stmt *>()[initOffset()]
                            : nullptr;
  }

  const Stmt *getInit() const {
    return hasInitStorage() ? getTrailingObjects<Stmt *>()[initOffset()]
                            : nullptr;
  }

  void setInit(Stmt *Init) {
    assert(hasInitStorage() &&
           "This if statement has no storage for an init statement!");
    getTrailingObjects<Stmt *>()[initOffset()] = Init;
  }

  SourceLocation getIfLoc() const { return IfStmtBits.IfLoc; }
  void setIfLoc(SourceLocation IfLoc) { IfStmtBits.IfLoc = IfLoc; }

  SourceLocation getElseLoc() const {
    return hasElseStorage() ? *getTrailingObjects<SourceLocation>()
                            : SourceLocation();
  }

  void setElseLoc(SourceLocation ElseLoc) {
    assert(hasElseStorage() &&
           "This if statement has no storage for an else statement!");
    *getTrailingObjects<SourceLocation>() = ElseLoc;
  }

  SourceLocation getBeginLoc() const { return getIfLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    if (getElse())
      return getElse()->getEndLoc();
    return getThen()->getEndLoc();
  }
  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation Loc) { RParenLoc = Loc; }

  // Iterators over subexpressions.  The iterators will include iterating
  // over the initialization expression referenced by the condition variable.
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

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == IfStmtClass;
  }
};

class SwitchStmt final : public Stmt,
                         private llvm::TrailingObjects<SwitchStmt, Stmt *> {
  friend TrailingObjects;

  SwitchCase *FirstCase = nullptr;

  // SwitchStmt is followed by several trailing objects,
  // some of which optional. Note that it would be more convenient to
  // put the optional trailing objects at the end but this would change
  // the order in children().
  // The trailing objects are in order:
  //
  // * A "Stmt *" for the init statement.
  //    Present if and only if hasInitStorage().
  //
  // * A "Stmt *" for the condition variable.
  //    Present if and only if hasVarStorage(). This is in fact a "DeclStmt *".
  //
  // * A "Stmt *" for the condition.
  //    Always present. This is in fact an "Expr *".
  //
  // * A "Stmt *" for the body.
  //    Always present.
  enum { InitOffset = 0, BodyOffsetFromCond = 1 };
  enum { NumMandatoryStmtPtr = 2 };
  SourceLocation LParenLoc;
  SourceLocation RParenLoc;

  unsigned numTrailingObjects(OverloadToken<Stmt *>) const {
    return NumMandatoryStmtPtr + hasInitStorage() + hasVarStorage();
  }

  unsigned initOffset() const { return InitOffset; }
  unsigned varOffset() const { return InitOffset + hasInitStorage(); }
  unsigned condOffset() const {
    return InitOffset + hasInitStorage() + hasVarStorage();
  }
  unsigned bodyOffset() const { return condOffset() + BodyOffsetFromCond; }

  SwitchStmt(const TreeContext &Ctx, Stmt *Init, VarDecl *Var, Expr *Cond,
             SourceLocation LParenLoc, SourceLocation RParenLoc);

  explicit SwitchStmt(EmptyShell Empty, bool HasInit, bool HasVar);

public:
  static SwitchStmt *Create(const TreeContext &Ctx, Stmt *Init, VarDecl *Var,
                            Expr *Cond, SourceLocation LParenLoc,
                            SourceLocation RParenLoc);

  static SwitchStmt *CreateEmpty(const TreeContext &Ctx, bool HasInit,
                                 bool HasVar);

  bool hasInitStorage() const { return SwitchStmtBits.HasInit; }

  bool hasVarStorage() const { return SwitchStmtBits.HasVar; }

  Expr *getCond() {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  const Expr *getCond() const {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  void setCond(Expr *Cond) {
    getTrailingObjects<Stmt *>()[condOffset()] = reinterpret_cast<Stmt *>(Cond);
  }

  Stmt *getBody() { return getTrailingObjects<Stmt *>()[bodyOffset()]; }
  const Stmt *getBody() const {
    return getTrailingObjects<Stmt *>()[bodyOffset()];
  }

  void setBody(Stmt *Body) {
    getTrailingObjects<Stmt *>()[bodyOffset()] = Body;
  }

  Stmt *getInit() {
    return hasInitStorage() ? getTrailingObjects<Stmt *>()[initOffset()]
                            : nullptr;
  }

  const Stmt *getInit() const {
    return hasInitStorage() ? getTrailingObjects<Stmt *>()[initOffset()]
                            : nullptr;
  }

  void setInit(Stmt *Init) {
    assert(hasInitStorage() &&
           "This switch statement has no storage for an init statement!");
    getTrailingObjects<Stmt *>()[initOffset()] = Init;
  }

  VarDecl *getConditionVariable();
  const VarDecl *getConditionVariable() const {
    return const_cast<SwitchStmt *>(this)->getConditionVariable();
  }

  void setConditionVariable(const TreeContext &Ctx, VarDecl *VD);

  DeclStmt *getConditionVariableDeclStmt() {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  const DeclStmt *getConditionVariableDeclStmt() const {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  void setConditionVariableDeclStmt(DeclStmt *CondVar) {
    assert(hasVarStorage());
    getTrailingObjects<Stmt *>()[varOffset()] = CondVar;
  }

  SwitchCase *getSwitchCaseList() { return FirstCase; }
  const SwitchCase *getSwitchCaseList() const { return FirstCase; }
  void setSwitchCaseList(SwitchCase *SC) { FirstCase = SC; }

  SourceLocation getSwitchLoc() const { return SwitchStmtBits.SwitchLoc; }
  void setSwitchLoc(SourceLocation L) { SwitchStmtBits.SwitchLoc = L; }
  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation Loc) { LParenLoc = Loc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation Loc) { RParenLoc = Loc; }

  void setBody(Stmt *S, SourceLocation SL) {
    setBody(S);
    setSwitchLoc(SL);
  }

  void addSwitchCase(SwitchCase *SC) {
    assert(!SC->getNextSwitchCase() &&
           "case/default already added to a switch");
    SC->setNextSwitchCase(FirstCase);
    FirstCase = SC;
  }

  void setAllEnumCasesCovered() { SwitchStmtBits.AllEnumCasesCovered = true; }

  bool isAllEnumCasesCovered() const {
    return SwitchStmtBits.AllEnumCasesCovered;
  }

  SourceLocation getBeginLoc() const { return getSwitchLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getBody() ? getBody()->getEndLoc()
                     : reinterpret_cast<const Stmt *>(getCond())->getEndLoc();
  }

  // Iterators
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

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SwitchStmtClass;
  }
};

class WhileStmt final : public Stmt,
                        private llvm::TrailingObjects<WhileStmt, Stmt *> {
  friend TrailingObjects;

  // WhileStmt is followed by several trailing objects,
  // some of which optional. Note that it would be more
  // convenient to put the optional trailing object at the end
  // but this would affect children().
  // The trailing objects are in order:
  //
  // * A "Stmt *" for the condition variable.
  //    Present if and only if hasVarStorage(). This is in fact a "DeclStmt *".
  //
  // * A "Stmt *" for the condition.
  //    Always present. This is in fact an "Expr *".
  //
  // * A "Stmt *" for the body.
  //    Always present.
  //
  enum { VarOffset = 0, BodyOffsetFromCond = 1 };
  enum { NumMandatoryStmtPtr = 2 };

  SourceLocation LParenLoc, RParenLoc;

  unsigned varOffset() const { return VarOffset; }
  unsigned condOffset() const { return VarOffset + hasVarStorage(); }
  unsigned bodyOffset() const { return condOffset() + BodyOffsetFromCond; }

  unsigned numTrailingObjects(OverloadToken<Stmt *>) const {
    return NumMandatoryStmtPtr + hasVarStorage();
  }

  WhileStmt(const TreeContext &Ctx, VarDecl *Var, Expr *Cond, Stmt *Body,
            SourceLocation WL, SourceLocation LParenLoc,
            SourceLocation RParenLoc);

  explicit WhileStmt(EmptyShell Empty, bool HasVar);

public:
  static WhileStmt *Create(const TreeContext &Ctx, VarDecl *Var, Expr *Cond,
                           Stmt *Body, SourceLocation WL,
                           SourceLocation LParenLoc, SourceLocation RParenLoc);

  static WhileStmt *CreateEmpty(const TreeContext &Ctx, bool HasVar);

  bool hasVarStorage() const { return WhileStmtBits.HasVar; }

  Expr *getCond() {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  const Expr *getCond() const {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  void setCond(Expr *Cond) {
    getTrailingObjects<Stmt *>()[condOffset()] = reinterpret_cast<Stmt *>(Cond);
  }

  Stmt *getBody() { return getTrailingObjects<Stmt *>()[bodyOffset()]; }
  const Stmt *getBody() const {
    return getTrailingObjects<Stmt *>()[bodyOffset()];
  }

  void setBody(Stmt *Body) {
    getTrailingObjects<Stmt *>()[bodyOffset()] = Body;
  }

  VarDecl *getConditionVariable();
  const VarDecl *getConditionVariable() const {
    return const_cast<WhileStmt *>(this)->getConditionVariable();
  }

  void setConditionVariable(const TreeContext &Ctx, VarDecl *V);

  DeclStmt *getConditionVariableDeclStmt() {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  const DeclStmt *getConditionVariableDeclStmt() const {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  void setConditionVariableDeclStmt(DeclStmt *CondVar) {
    assert(hasVarStorage());
    getTrailingObjects<Stmt *>()[varOffset()] = CondVar;
  }

  SourceLocation getWhileLoc() const { return WhileStmtBits.WhileLoc; }
  void setWhileLoc(SourceLocation L) { WhileStmtBits.WhileLoc = L; }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation L) { LParenLoc = L; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const { return getWhileLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getBody()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == WhileStmtClass;
  }

  // Iterators
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

class DoStmt : public Stmt {
  enum { BODY, COND, END_EXPR };
  Stmt *SubExprs[END_EXPR];
  SourceLocation WhileLoc;
  SourceLocation RParenLoc; // Location of final ')' in do stmt condition.

public:
  DoStmt(Stmt *Body, Expr *Cond, SourceLocation DL, SourceLocation WL,
         SourceLocation RP)
      : Stmt(DoStmtClass), WhileLoc(WL), RParenLoc(RP) {
    setCond(Cond);
    setBody(Body);
    setDoLoc(DL);
  }

  explicit DoStmt(EmptyShell Empty) : Stmt(DoStmtClass, Empty) {}

  Expr *getCond() { return reinterpret_cast<Expr *>(SubExprs[COND]); }
  const Expr *getCond() const {
    return reinterpret_cast<Expr *>(SubExprs[COND]);
  }

  void setCond(Expr *Cond) { SubExprs[COND] = reinterpret_cast<Stmt *>(Cond); }

  Stmt *getBody() { return SubExprs[BODY]; }
  const Stmt *getBody() const { return SubExprs[BODY]; }
  void setBody(Stmt *Body) { SubExprs[BODY] = Body; }

  SourceLocation getDoLoc() const { return DoStmtBits.DoLoc; }
  void setDoLoc(SourceLocation L) { DoStmtBits.DoLoc = L; }
  SourceLocation getWhileLoc() const { return WhileLoc; }
  void setWhileLoc(SourceLocation L) { WhileLoc = L; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const { return getDoLoc(); }
  SourceLocation getEndLoc() const { return getRParenLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DoStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }

  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
};

class ForStmt : public Stmt {

  enum { INIT, CONDVAR, COND, INC, BODY, END_EXPR };
  Stmt *SubExprs[END_EXPR]; // SubExprs[INIT] is an expression or declstmt.
  SourceLocation LParenLoc, RParenLoc;

public:
  ForStmt(const TreeContext &C, Stmt *Init, Expr *Cond, VarDecl *condVar,
          Expr *Inc, Stmt *Body, SourceLocation FL, SourceLocation LP,
          SourceLocation RP);

  explicit ForStmt(EmptyShell Empty) : Stmt(ForStmtClass, Empty) {}

  Stmt *getInit() { return SubExprs[INIT]; }

  VarDecl *getConditionVariable() const;
  void setConditionVariable(const TreeContext &C, VarDecl *V);

  DeclStmt *getConditionVariableDeclStmt() {
    return reinterpret_cast<DeclStmt *>(SubExprs[CONDVAR]);
  }

  const DeclStmt *getConditionVariableDeclStmt() const {
    return reinterpret_cast<DeclStmt *>(SubExprs[CONDVAR]);
  }

  void setConditionVariableDeclStmt(DeclStmt *CondVar) {
    SubExprs[CONDVAR] = CondVar;
  }

  Expr *getCond() { return reinterpret_cast<Expr *>(SubExprs[COND]); }
  Expr *getInc() { return reinterpret_cast<Expr *>(SubExprs[INC]); }
  Stmt *getBody() { return SubExprs[BODY]; }

  const Stmt *getInit() const { return SubExprs[INIT]; }
  const Expr *getCond() const {
    return reinterpret_cast<Expr *>(SubExprs[COND]);
  }
  const Expr *getInc() const { return reinterpret_cast<Expr *>(SubExprs[INC]); }
  const Stmt *getBody() const { return SubExprs[BODY]; }

  void setInit(Stmt *S) { SubExprs[INIT] = S; }
  void setCond(Expr *E) { SubExprs[COND] = reinterpret_cast<Stmt *>(E); }
  void setInc(Expr *E) { SubExprs[INC] = reinterpret_cast<Stmt *>(E); }
  void setBody(Stmt *S) { SubExprs[BODY] = S; }

  SourceLocation getForLoc() const { return ForStmtBits.ForLoc; }
  void setForLoc(SourceLocation L) { ForStmtBits.ForLoc = L; }
  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation L) { LParenLoc = L; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const { return getForLoc(); }
  SourceLocation getEndLoc() const { return getBody()->getEndLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ForStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }

  const_child_range children() const {
    return const_child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
};

class GotoStmt : public Stmt {
  LabelDecl *Label;
  SourceLocation LabelLoc;

public:
  GotoStmt(LabelDecl *label, SourceLocation GL, SourceLocation LL)
      : Stmt(GotoStmtClass), Label(label), LabelLoc(LL) {
    setGotoLoc(GL);
  }

  explicit GotoStmt(EmptyShell Empty) : Stmt(GotoStmtClass, Empty) {}

  LabelDecl *getLabel() const { return Label; }
  void setLabel(LabelDecl *D) { Label = D; }

  SourceLocation getGotoLoc() const { return GotoStmtBits.GotoLoc; }
  void setGotoLoc(SourceLocation L) { GotoStmtBits.GotoLoc = L; }
  SourceLocation getLabelLoc() const { return LabelLoc; }
  void setLabelLoc(SourceLocation L) { LabelLoc = L; }

  SourceLocation getBeginLoc() const { return getGotoLoc(); }
  SourceLocation getEndLoc() const { return getLabelLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == GotoStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class IndirectGotoStmt : public Stmt {
  SourceLocation StarLoc;
  Stmt *Target;

public:
  IndirectGotoStmt(SourceLocation gotoLoc, SourceLocation starLoc, Expr *target)
      : Stmt(IndirectGotoStmtClass), StarLoc(starLoc) {
    setTarget(target);
    setGotoLoc(gotoLoc);
  }

  explicit IndirectGotoStmt(EmptyShell Empty)
      : Stmt(IndirectGotoStmtClass, Empty) {}

  void setGotoLoc(SourceLocation L) { GotoStmtBits.GotoLoc = L; }
  SourceLocation getGotoLoc() const { return GotoStmtBits.GotoLoc; }
  void setStarLoc(SourceLocation L) { StarLoc = L; }
  SourceLocation getStarLoc() const { return StarLoc; }

  Expr *getTarget() { return reinterpret_cast<Expr *>(Target); }
  const Expr *getTarget() const {
    return reinterpret_cast<const Expr *>(Target);
  }
  void setTarget(Expr *E) { Target = reinterpret_cast<Stmt *>(E); }

  LabelDecl *getConstantTarget();
  const LabelDecl *getConstantTarget() const {
    return const_cast<IndirectGotoStmt *>(this)->getConstantTarget();
  }

  SourceLocation getBeginLoc() const { return getGotoLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Target->getEndLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == IndirectGotoStmtClass;
  }

  // Iterators
  child_range children() { return child_range(&Target, &Target + 1); }

  const_child_range children() const {
    return const_child_range(&Target, &Target + 1);
  }
};

class ContinueStmt : public Stmt {
public:
  ContinueStmt(SourceLocation CL) : Stmt(ContinueStmtClass) {
    setContinueLoc(CL);
  }

  explicit ContinueStmt(EmptyShell Empty) : Stmt(ContinueStmtClass, Empty) {}

  SourceLocation getContinueLoc() const { return ContinueStmtBits.ContinueLoc; }
  void setContinueLoc(SourceLocation L) { ContinueStmtBits.ContinueLoc = L; }

  SourceLocation getBeginLoc() const { return getContinueLoc(); }
  SourceLocation getEndLoc() const { return getContinueLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ContinueStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class BreakStmt : public Stmt {
public:
  BreakStmt(SourceLocation BL) : Stmt(BreakStmtClass) { setBreakLoc(BL); }

  explicit BreakStmt(EmptyShell Empty) : Stmt(BreakStmtClass, Empty) {}

  SourceLocation getBreakLoc() const { return BreakStmtBits.BreakLoc; }
  void setBreakLoc(SourceLocation L) { BreakStmtBits.BreakLoc = L; }

  SourceLocation getBeginLoc() const { return getBreakLoc(); }
  SourceLocation getEndLoc() const { return getBreakLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == BreakStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class ReturnStmt final
    : public Stmt,
      private llvm::TrailingObjects<ReturnStmt, const VarDecl *> {
  friend TrailingObjects;

  Stmt *RetExpr;

  // ReturnStmt is followed optionally by a trailing "const VarDecl *"
  // for the NRVO candidate. Present if and only if hasNRVOCandidate().

  bool hasNRVOCandidate() const { return ReturnStmtBits.HasNRVOCandidate; }

  unsigned numTrailingObjects(OverloadToken<const VarDecl *>) const {
    return hasNRVOCandidate();
  }

  ReturnStmt(SourceLocation RL, Expr *E, const VarDecl *NRVOCandidate);

  explicit ReturnStmt(EmptyShell Empty, bool HasNRVOCandidate);

public:
  static ReturnStmt *Create(const TreeContext &Ctx, SourceLocation RL, Expr *E,
                            const VarDecl *NRVOCandidate);

  static ReturnStmt *CreateEmpty(const TreeContext &Ctx, bool HasNRVOCandidate);

  Expr *getRetValue() { return reinterpret_cast<Expr *>(RetExpr); }
  const Expr *getRetValue() const { return reinterpret_cast<Expr *>(RetExpr); }
  void setRetValue(Expr *E) { RetExpr = reinterpret_cast<Stmt *>(E); }

  const VarDecl *getNRVOCandidate() const {
    return hasNRVOCandidate() ? *getTrailingObjects<const VarDecl *>()
                              : nullptr;
  }

  void setNRVOCandidate(const VarDecl *Var) {
    assert(hasNRVOCandidate() &&
           "This return statement has no storage for an NRVO candidate!");
    *getTrailingObjects<const VarDecl *>() = Var;
  }

  SourceLocation getReturnLoc() const { return ReturnStmtBits.RetLoc; }
  void setReturnLoc(SourceLocation L) { ReturnStmtBits.RetLoc = L; }

  SourceLocation getBeginLoc() const { return getReturnLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return RetExpr ? RetExpr->getEndLoc() : getReturnLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ReturnStmtClass;
  }

  // Iterators
  child_range children() {
    if (RetExpr)
      return child_range(&RetExpr, &RetExpr + 1);
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    if (RetExpr)
      return const_child_range(&RetExpr, &RetExpr + 1);
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

class AsmStmt : public Stmt {
protected:
  SourceLocation AsmLoc;

  bool IsSimple;

  bool IsVolatile;

  unsigned NumOutputs;
  unsigned NumInputs;
  unsigned NumClobbers;

  Stmt **Exprs = nullptr;

  AsmStmt(StmtClass SC, SourceLocation asmloc, bool issimple, bool isvolatile,
          unsigned numoutputs, unsigned numinputs, unsigned numclobbers)
      : Stmt(SC), AsmLoc(asmloc), IsSimple(issimple), IsVolatile(isvolatile),
        NumOutputs(numoutputs), NumInputs(numinputs), NumClobbers(numclobbers) {
  }

public:
  explicit AsmStmt(StmtClass SC, EmptyShell Empty) : Stmt(SC, Empty) {}

  SourceLocation getAsmLoc() const { return AsmLoc; }
  void setAsmLoc(SourceLocation L) { AsmLoc = L; }

  bool isSimple() const { return IsSimple; }
  void setSimple(bool V) { IsSimple = V; }

  bool isVolatile() const { return IsVolatile; }
  void setVolatile(bool V) { IsVolatile = V; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return {}; }
  SourceLocation getEndLoc() const LLVM_READONLY { return {}; }

  //===--- Asm String Analysis ---===//

  std::string generateAsmString(const TreeContext &C) const;

  //===--- Output operands ---===//

  unsigned getNumOutputs() const { return NumOutputs; }

  llvm::StringRef getOutputConstraint(unsigned i) const;

  bool isOutputPlusConstraint(unsigned i) const {
    return getOutputConstraint(i)[0] == '+';
  }

  const Expr *getOutputExpr(unsigned i) const;

  unsigned getNumPlusOperands() const;

  //===--- Input operands ---===//

  unsigned getNumInputs() const { return NumInputs; }

  llvm::StringRef getInputConstraint(unsigned i) const;

  const Expr *getInputExpr(unsigned i) const;

  //===--- Other ---===//

  unsigned getNumClobbers() const { return NumClobbers; }
  llvm::StringRef getClobber(unsigned i) const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == GCCAsmStmtClass ||
           T->getStmtClass() == MSAsmStmtClass;
  }

  // Input expr iterators.

  using inputs_iterator = ExprIterator;
  using const_inputs_iterator = ConstExprIterator;
  using inputs_range = llvm::iterator_range<inputs_iterator>;
  using inputs_const_range = llvm::iterator_range<const_inputs_iterator>;

  inputs_iterator begin_inputs() { return &Exprs[0] + NumOutputs; }

  inputs_iterator end_inputs() { return &Exprs[0] + NumOutputs + NumInputs; }

  inputs_range inputs() { return inputs_range(begin_inputs(), end_inputs()); }

  const_inputs_iterator begin_inputs() const { return &Exprs[0] + NumOutputs; }

  const_inputs_iterator end_inputs() const {
    return &Exprs[0] + NumOutputs + NumInputs;
  }

  inputs_const_range inputs() const {
    return inputs_const_range(begin_inputs(), end_inputs());
  }

  // Output expr iterators.

  using outputs_iterator = ExprIterator;
  using const_outputs_iterator = ConstExprIterator;
  using outputs_range = llvm::iterator_range<outputs_iterator>;
  using outputs_const_range = llvm::iterator_range<const_outputs_iterator>;

  outputs_iterator begin_outputs() { return &Exprs[0]; }

  outputs_iterator end_outputs() { return &Exprs[0] + NumOutputs; }

  outputs_range outputs() {
    return outputs_range(begin_outputs(), end_outputs());
  }

  const_outputs_iterator begin_outputs() const { return &Exprs[0]; }

  const_outputs_iterator end_outputs() const { return &Exprs[0] + NumOutputs; }

  outputs_const_range outputs() const {
    return outputs_const_range(begin_outputs(), end_outputs());
  }

  child_range children() {
    return child_range(&Exprs[0], &Exprs[0] + NumOutputs + NumInputs);
  }

  const_child_range children() const {
    return const_child_range(&Exprs[0], &Exprs[0] + NumOutputs + NumInputs);
  }
};

class GCCAsmStmt : public AsmStmt {

  SourceLocation RParenLoc;
  StringLiteral *AsmStr;

  StringLiteral **Constraints = nullptr;
  StringLiteral **Clobbers = nullptr;
  IdentifierInfo **Names = nullptr;
  unsigned NumLabels = 0;

public:
  GCCAsmStmt(const TreeContext &C, SourceLocation asmloc, bool issimple,
             bool isvolatile, unsigned numoutputs, unsigned numinputs,
             IdentifierInfo **names, StringLiteral **constraints, Expr **exprs,
             StringLiteral *asmstr, unsigned numclobbers,
             StringLiteral **clobbers, unsigned numlabels,
             SourceLocation rparenloc);

  explicit GCCAsmStmt(EmptyShell Empty) : AsmStmt(GCCAsmStmtClass, Empty) {}

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  //===--- Asm String Analysis ---===//

  const StringLiteral *getAsmString() const { return AsmStr; }
  StringLiteral *getAsmString() { return AsmStr; }
  void setAsmString(StringLiteral *E) { AsmStr = E; }

  class AsmStringPiece {
  public:
    enum Kind {
      String, // String in .ll asm string form, "$" -> "$$" and "%%" -> "%".
      Operand // Operand reference, with optional modifier %c4.
    };

  private:
    Kind MyKind;
    std::string Str;
    unsigned OperandNo;

    // Source range for operand references.
    CharSourceRange Range;

  public:
    AsmStringPiece(const std::string &S) : MyKind(String), Str(S) {}
    AsmStringPiece(unsigned OpNo, const std::string &S, SourceLocation Begin,
                   SourceLocation End)
        : MyKind(Operand), Str(S), OperandNo(OpNo),
          Range(CharSourceRange::getCharRange(Begin, End)) {}

    bool isString() const { return MyKind == String; }
    bool isOperand() const { return MyKind == Operand; }

    const std::string &getString() const { return Str; }

    unsigned getOperandNo() const {
      assert(isOperand());
      return OperandNo;
    }

    CharSourceRange getRange() const {
      assert(isOperand() && "Range is currently used only for Operands.");
      return Range;
    }

    /// getModifier - Get the modifier for this operand, if present.  This
    /// returns '\0' if there was no modifier.
    char getModifier() const;
  };

  //// flattening of named references like %[foo] to Operand AsmStringPiece's.
  unsigned AnalyzeAsmString(llvm::SmallVectorImpl<AsmStringPiece> &Pieces,
                            const TreeContext &C, unsigned &DiagOffs) const;

  std::string generateAsmString(const TreeContext &C) const;

  //===--- Output operands ---===//

  IdentifierInfo *getOutputIdentifier(unsigned i) const { return Names[i]; }

  llvm::StringRef getOutputName(unsigned i) const {
    if (IdentifierInfo *II = getOutputIdentifier(i))
      return II->getName();

    return {};
  }

  llvm::StringRef getOutputConstraint(unsigned i) const;

  const StringLiteral *getOutputConstraintLiteral(unsigned i) const {
    return Constraints[i];
  }
  StringLiteral *getOutputConstraintLiteral(unsigned i) {
    return Constraints[i];
  }

  Expr *getOutputExpr(unsigned i);

  const Expr *getOutputExpr(unsigned i) const {
    return const_cast<GCCAsmStmt *>(this)->getOutputExpr(i);
  }

  //===--- Input operands ---===//

  IdentifierInfo *getInputIdentifier(unsigned i) const {
    return Names[i + NumOutputs];
  }

  llvm::StringRef getInputName(unsigned i) const {
    if (IdentifierInfo *II = getInputIdentifier(i))
      return II->getName();

    return {};
  }

  llvm::StringRef getInputConstraint(unsigned i) const;

  const StringLiteral *getInputConstraintLiteral(unsigned i) const {
    return Constraints[i + NumOutputs];
  }
  StringLiteral *getInputConstraintLiteral(unsigned i) {
    return Constraints[i + NumOutputs];
  }

  Expr *getInputExpr(unsigned i);
  void setInputExpr(unsigned i, Expr *E);

  const Expr *getInputExpr(unsigned i) const {
    return const_cast<GCCAsmStmt *>(this)->getInputExpr(i);
  }

  //===--- Labels ---===//

  bool isAsmGoto() const { return NumLabels > 0; }

  unsigned getNumLabels() const { return NumLabels; }

  IdentifierInfo *getLabelIdentifier(unsigned i) const {
    return Names[i + NumOutputs + NumInputs];
  }

  AddrLabelExpr *getLabelExpr(unsigned i) const;
  llvm::StringRef getLabelName(unsigned i) const;
  using labels_iterator = CastIterator<AddrLabelExpr>;
  using const_labels_iterator = ConstCastIterator<AddrLabelExpr>;
  using labels_range = llvm::iterator_range<labels_iterator>;
  using labels_const_range = llvm::iterator_range<const_labels_iterator>;

  labels_iterator begin_labels() { return &Exprs[0] + NumOutputs + NumInputs; }

  labels_iterator end_labels() {
    return &Exprs[0] + NumOutputs + NumInputs + NumLabels;
  }

  labels_range labels() { return labels_range(begin_labels(), end_labels()); }

  const_labels_iterator begin_labels() const {
    return &Exprs[0] + NumOutputs + NumInputs;
  }

  const_labels_iterator end_labels() const {
    return &Exprs[0] + NumOutputs + NumInputs + NumLabels;
  }

  labels_const_range labels() const {
    return labels_const_range(begin_labels(), end_labels());
  }

private:
  void setOutputsAndInputsAndClobbers(
      const TreeContext &C, IdentifierInfo **Names, StringLiteral **Constraints,
      Stmt **Exprs, unsigned NumOutputs, unsigned NumInputs, unsigned NumLabels,
      StringLiteral **Clobbers, unsigned NumClobbers);

public:
  //===--- Other ---===//

  int getNamedOperand(llvm::StringRef SymbolicName) const;

  llvm::StringRef getClobber(unsigned i) const;

  StringLiteral *getClobberStringLiteral(unsigned i) { return Clobbers[i]; }
  const StringLiteral *getClobberStringLiteral(unsigned i) const {
    return Clobbers[i];
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return AsmLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == GCCAsmStmtClass;
  }
};

class MSAsmStmt : public AsmStmt {

  SourceLocation LBraceLoc, EndLoc;
  llvm::StringRef AsmStr;

  unsigned NumAsmToks = 0;

  Token *AsmToks = nullptr;
  llvm::StringRef *Constraints = nullptr;
  llvm::StringRef *Clobbers = nullptr;

public:
  MSAsmStmt(const TreeContext &C, SourceLocation asmloc,
            SourceLocation lbraceloc, bool issimple, bool isvolatile,
            llvm::ArrayRef<Token> asmtoks, unsigned numoutputs,
            unsigned numinputs, llvm::ArrayRef<llvm::StringRef> constraints,
            llvm::ArrayRef<Expr *> exprs, llvm::StringRef asmstr,
            llvm::ArrayRef<llvm::StringRef> clobbers, SourceLocation endloc);

  explicit MSAsmStmt(EmptyShell Empty) : AsmStmt(MSAsmStmtClass, Empty) {}

  SourceLocation getLBraceLoc() const { return LBraceLoc; }
  void setLBraceLoc(SourceLocation L) { LBraceLoc = L; }
  SourceLocation getEndLoc() const { return EndLoc; }
  void setEndLoc(SourceLocation L) { EndLoc = L; }

  bool hasBraces() const { return LBraceLoc.isValid(); }

  unsigned getNumAsmToks() { return NumAsmToks; }
  Token *getAsmToks() { return AsmToks; }

  //===--- Asm String Analysis ---===//
  llvm::StringRef getAsmString() const { return AsmStr; }

  std::string generateAsmString(const TreeContext &C) const;

  //===--- Output operands ---===//

  llvm::StringRef getOutputConstraint(unsigned i) const {
    assert(i < NumOutputs);
    return Constraints[i];
  }

  Expr *getOutputExpr(unsigned i);

  const Expr *getOutputExpr(unsigned i) const {
    return const_cast<MSAsmStmt *>(this)->getOutputExpr(i);
  }

  //===--- Input operands ---===//

  llvm::StringRef getInputConstraint(unsigned i) const {
    assert(i < NumInputs);
    return Constraints[i + NumOutputs];
  }

  Expr *getInputExpr(unsigned i);
  void setInputExpr(unsigned i, Expr *E);

  const Expr *getInputExpr(unsigned i) const {
    return const_cast<MSAsmStmt *>(this)->getInputExpr(i);
  }

  //===--- Other ---===//

  llvm::ArrayRef<llvm::StringRef> getAllConstraints() const {
    return llvm::ArrayRef(Constraints, NumInputs + NumOutputs);
  }

  llvm::ArrayRef<llvm::StringRef> getClobbers() const {
    return llvm::ArrayRef(Clobbers, NumClobbers);
  }

  llvm::ArrayRef<Expr *> getAllExprs() const {
    return llvm::ArrayRef(reinterpret_cast<Expr **>(Exprs),
                          NumInputs + NumOutputs);
  }

  llvm::StringRef getClobber(unsigned i) const { return getClobbers()[i]; }

private:
  void initialize(const TreeContext &C, llvm::StringRef AsmString,
                  llvm::ArrayRef<Token> AsmToks,
                  llvm::ArrayRef<llvm::StringRef> Constraints,
                  llvm::ArrayRef<Expr *> Exprs,
                  llvm::ArrayRef<llvm::StringRef> Clobbers);

public:
  SourceLocation getBeginLoc() const LLVM_READONLY { return AsmLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == MSAsmStmtClass;
  }

  child_range children() {
    return child_range(&Exprs[0], &Exprs[NumInputs + NumOutputs]);
  }

  const_child_range children() const {
    return const_child_range(&Exprs[0], &Exprs[NumInputs + NumOutputs]);
  }
};

class SEHExceptStmt : public Stmt {

  SourceLocation Loc;
  Stmt *Children[2];

  enum { FILTER_EXPR, BLOCK };

  SEHExceptStmt(SourceLocation Loc, Expr *FilterExpr, Stmt *Block);
  explicit SEHExceptStmt(EmptyShell E) : Stmt(SEHExceptStmtClass, E) {}

public:
  static SEHExceptStmt *Create(const TreeContext &C, SourceLocation ExceptLoc,
                               Expr *FilterExpr, Stmt *Block);

  SourceLocation getBeginLoc() const LLVM_READONLY { return getExceptLoc(); }

  SourceLocation getExceptLoc() const { return Loc; }
  SourceLocation getEndLoc() const { return getBlock()->getEndLoc(); }

  Expr *getFilterExpr() const {
    return reinterpret_cast<Expr *>(Children[FILTER_EXPR]);
  }

  CompoundStmt *getBlock() const { return cast<CompoundStmt>(Children[BLOCK]); }

  child_range children() { return child_range(Children, Children + 2); }

  const_child_range children() const {
    return const_child_range(Children, Children + 2);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SEHExceptStmtClass;
  }
};

class SEHFinallyStmt : public Stmt {

  SourceLocation Loc;
  Stmt *Block;

  SEHFinallyStmt(SourceLocation Loc, Stmt *Block);
  explicit SEHFinallyStmt(EmptyShell E) : Stmt(SEHFinallyStmtClass, E) {}

public:
  static SEHFinallyStmt *Create(const TreeContext &C, SourceLocation FinallyLoc,
                                Stmt *Block);

  SourceLocation getBeginLoc() const LLVM_READONLY { return getFinallyLoc(); }

  SourceLocation getFinallyLoc() const { return Loc; }
  SourceLocation getEndLoc() const { return Block->getEndLoc(); }

  CompoundStmt *getBlock() const { return cast<CompoundStmt>(Block); }

  child_range children() { return child_range(&Block, &Block + 1); }

  const_child_range children() const {
    return const_child_range(&Block, &Block + 1);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SEHFinallyStmtClass;
  }
};

class SEHTryStmt : public Stmt {

  SourceLocation TryLoc;
  Stmt *Children[2];

  enum { TRY = 0, HANDLER = 1 };

  SEHTryStmt(SourceLocation TryLoc, Stmt *TryBlock, Stmt *Handler);

  explicit SEHTryStmt(EmptyShell E) : Stmt(SEHTryStmtClass, E) {}

public:
  static SEHTryStmt *Create(const TreeContext &C, SourceLocation TryLoc,
                            Stmt *TryBlock, Stmt *Handler);

  SourceLocation getBeginLoc() const LLVM_READONLY { return getTryLoc(); }

  SourceLocation getTryLoc() const { return TryLoc; }
  SourceLocation getEndLoc() const { return Children[HANDLER]->getEndLoc(); }

  CompoundStmt *getTryBlock() const {
    return cast<CompoundStmt>(Children[TRY]);
  }

  Stmt *getHandler() const { return Children[HANDLER]; }

  SEHExceptStmt *getExceptHandler() const;
  SEHFinallyStmt *getFinallyHandler() const;

  child_range children() { return child_range(Children, Children + 2); }

  const_child_range children() const {
    return const_child_range(Children, Children + 2);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SEHTryStmtClass;
  }
};

class SEHLeaveStmt : public Stmt {
  SourceLocation LeaveLoc;

public:
  explicit SEHLeaveStmt(SourceLocation LL)
      : Stmt(SEHLeaveStmtClass), LeaveLoc(LL) {}

  explicit SEHLeaveStmt(EmptyShell Empty) : Stmt(SEHLeaveStmtClass, Empty) {}

  SourceLocation getLeaveLoc() const { return LeaveLoc; }
  void setLeaveLoc(SourceLocation L) { LeaveLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LeaveLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return LeaveLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SEHLeaveStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }

  const_child_range children() const {
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
};

} // namespace neverc

#endif // NEVERC_AST_STMT_H
