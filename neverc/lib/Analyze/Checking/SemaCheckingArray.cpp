#include "Checking/SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <optional>
#include <string>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Array access & empty-statement diagnostics (moved from SemaCheckingStmt.cpp)
// ===----------------------------------------------------------------------===

void Sema::CheckArrayAccess(const Expr *BaseExpr, const Expr *IndexExpr,
                            const ArraySubscriptExpr *ASE, bool AllowOnePastEnd,
                            bool IndexNegated) {
  if (LLVM_UNLIKELY(Diags.getIgnoreAllWarnings()))
    return;

  if (isConstantEvaluatedContext())
    return;

  IndexExpr = IndexExpr->IgnoreParenImpCasts();

  const Type *EffectiveType =
      BaseExpr->getType()->getPointeeOrArrayElementType();
  BaseExpr = BaseExpr->IgnoreParenCasts();
  const ConstantArrayType *ArrayTy =
      Context.getAsConstantArrayType(BaseExpr->getType());

  LangOptions::StrictFlexArraysLevelKind StrictFlexArraysLevel =
      getLangOpts().getStrictFlexArraysLevel();

  const Type *BaseType =
      ArrayTy == nullptr ? nullptr : ArrayTy->getElementType().getTypePtr();
  bool IsUnboundedArray =
      BaseType == nullptr ||
      BaseExpr->isFlexibleArrayMemberLike(Context, StrictFlexArraysLevel,
                                          /*IgnoreMacroSubstitution=*/true);

  Expr::EvalResult Result;
  if (!IndexExpr->EvaluateAsInt(Result, Context, Expr::SE_AllowSideEffects))
    return;

  llvm::APSInt index = Result.Val.getInt();
  if (IndexNegated) {
    index.setIsUnsigned(false);
    index = -index;
  }

  if (IsUnboundedArray) {
    if (EffectiveType->isFunctionType())
      return;
    if (index.isUnsigned() || !index.isNegative()) {
      const auto &ASTC = getTreeContext();
      unsigned AddrBits = ASTC.getTargetInfo().getPointerWidth(
          EffectiveType->getCanonicalTypeInternal().getAddressSpace());
      if (index.getBitWidth() < AddrBits)
        index = index.zext(AddrBits);
      std::optional<CharUnits> ElemCharUnits =
          ASTC.getTypeSizeInCharsIfKnown(EffectiveType);
      // PR50741 - If EffectiveType has unknown size (e.g., if it's a void
      // pointer) bounds-checking isn't meaningful.
      if (!ElemCharUnits || ElemCharUnits->isZero())
        return;
      llvm::APInt ElemBytes(index.getBitWidth(), ElemCharUnits->getQuantity());
      // If index has more active bits than address space, we already know
      // we have a bounds violation to warn about.  Otherwise, compute
      // address of (index + 1)th element, and warn about bounds violation
      // only if that address exceeds address space.
      if (index.getActiveBits() <= AddrBits) {
        bool Overflow;
        llvm::APInt Product(index);
        Product += 1;
        Product = Product.umul_ov(ElemBytes, Overflow);
        if (!Overflow && Product.getActiveBits() <= AddrBits)
          return;
      }

      // Need to compute max possible elements in address space, since that
      // is included in diag message.
      llvm::APInt MaxElems = llvm::APInt::getMaxValue(AddrBits);
      MaxElems = MaxElems.zext(std::max(AddrBits + 1, ElemBytes.getBitWidth()));
      MaxElems += 1;
      ElemBytes = ElemBytes.zextOrTrunc(MaxElems.getBitWidth());
      MaxElems = MaxElems.udiv(ElemBytes);

      unsigned DiagID =
          ASE ? diag::warn_array_index_exceeds_max_addressable_bounds
              : diag::warn_ptr_arith_exceeds_max_addressable_bounds;

      // Diag message shows element size in bits and in "bytes" (platform-
      // dependent CharUnits)
      DiagRuntimeBehavior(BaseExpr->getBeginLoc(), BaseExpr,
                          PDiag(DiagID)
                              << toString(index, 10, true) << AddrBits
                              << (unsigned)ASTC.toBits(*ElemCharUnits)
                              << toString(ElemBytes, 10, false)
                              << toString(MaxElems, 10, false)
                              << (unsigned)MaxElems.getLimitedValue(~0U)
                              << IndexExpr->getSourceRange());

      const NamedDecl *ND = nullptr;
      // Try harder to find a NamedDecl to point at in the note.
      while (const auto *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr))
        BaseExpr = ASE->getBase()->IgnoreParenCasts();
      if (const auto *DRE = dyn_cast<DeclRefExpr>(BaseExpr))
        ND = DRE->getDecl();
      if (const auto *ME = dyn_cast<MemberExpr>(BaseExpr))
        ND = ME->getMemberDecl();

      if (ND)
        DiagRuntimeBehavior(ND->getBeginLoc(), BaseExpr,
                            PDiag(diag::note_array_declared_here) << ND);
    }
    return;
  }

  if (index.isUnsigned() || !index.isNegative()) {
    // It is possible that the type of the base expression after
    // IgnoreParenCasts is incomplete, even though the type of the base
    // expression before IgnoreParenCasts is complete (see PR39746 for an
    // example). In this case we have no information about whether the array
    // access exceeds the array bounds. However we can still diagnose an array
    // access which precedes the array bounds.
    if (BaseType->isIncompleteType())
      return;

    llvm::APInt size = ArrayTy->getSize();

    if (BaseType != EffectiveType) {
      // Make sure we're comparing apples to apples when comparing index to
      // size.
      uint64_t ptrarith_typesize = Context.getTypeSize(EffectiveType);
      uint64_t array_typesize = Context.getTypeSize(BaseType);

      // Handle ptrarith_typesize being zero, such as when casting to void*.
      // Use the size in bits (what "getTypeSize()" returns) rather than bytes.
      if (!ptrarith_typesize)
        ptrarith_typesize = Context.getCharWidth();

      if (ptrarith_typesize != array_typesize) {
        // There's a cast to a different size type involved.
        uint64_t ratio = array_typesize / ptrarith_typesize;

        if (ptrarith_typesize * ratio == array_typesize)
          size *= llvm::APInt(size.getBitWidth(), ratio);
      }
    }

    if (size.getBitWidth() > index.getBitWidth())
      index = index.zext(size.getBitWidth());
    else if (size.getBitWidth() < index.getBitWidth())
      size = size.zext(index.getBitWidth());

    // For array subscripting the index must be less than size, but for pointer
    // arithmetic also allow the index (offset) to be equal to size since
    // computing the next address after the end of the array is legal and
    // commonly used for one-past-the-end pointers.
    if (AllowOnePastEnd ? index.ule(size) : index.ult(size))
      return;

    // Suppress the warning if the subscript expression (as identified by the
    // ']' location) and the index expression are both from macro expansions
    // within a system header.
    if (ASE) {
      SourceLocation RBracketLoc =
          SourceMgr.getSpellingLoc(ASE->getRBracketLoc());
      if (SourceMgr.isInSystemHeader(RBracketLoc)) {
        SourceLocation IndexLoc =
            SourceMgr.getSpellingLoc(IndexExpr->getBeginLoc());
        if (SourceMgr.isWrittenInSameFile(RBracketLoc, IndexLoc))
          return;
      }
    }

    unsigned DiagID = ASE ? diag::warn_array_index_exceeds_bounds
                          : diag::warn_ptr_arith_exceeds_bounds;
    unsigned CastMsg = (!ASE || BaseType == EffectiveType) ? 0 : 1;
    QualType CastMsgTy = ASE ? ASE->getLHS()->getType() : QualType();

    DiagRuntimeBehavior(
        BaseExpr->getBeginLoc(), BaseExpr,
        PDiag(DiagID) << toString(index, 10, true) << ArrayTy->desugar()
                      << CastMsg << CastMsgTy << IndexExpr->getSourceRange());
  } else {
    unsigned DiagID = diag::warn_array_index_precedes_bounds;
    if (!ASE) {
      DiagID = diag::warn_ptr_arith_precedes_bounds;
      if (index.isNegative())
        index = -index;
    }

    DiagRuntimeBehavior(BaseExpr->getBeginLoc(), BaseExpr,
                        PDiag(DiagID) << toString(index, 10, true)
                                      << IndexExpr->getSourceRange());
  }

  const NamedDecl *ND = nullptr;
  // Try harder to find a NamedDecl to point at in the note.
  while (const auto *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr))
    BaseExpr = ASE->getBase()->IgnoreParenCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(BaseExpr))
    ND = DRE->getDecl();
  if (const auto *ME = dyn_cast<MemberExpr>(BaseExpr))
    ND = ME->getMemberDecl();

  if (ND)
    DiagRuntimeBehavior(ND->getBeginLoc(), BaseExpr,
                        PDiag(diag::note_array_declared_here) << ND);
}

void Sema::CheckArrayAccess(const Expr *expr) {
  if (LLVM_UNLIKELY(Diags.getIgnoreAllWarnings()))
    return;

  switch (expr->getStmtClass()) {
  case Stmt::BinaryOperatorClass:
  case Stmt::CompoundAssignOperatorClass:
  case Stmt::IntegerLiteralClass:
  case Stmt::FloatingLiteralClass:
  case Stmt::CharacterLiteralClass:
  case Stmt::DeclRefExprClass:
  case Stmt::CallExprClass:
    return;
  default:
    break;
  }
  int AllowOnePastEnd = 0;
  while (expr) {
    expr = expr->IgnoreParenImpCasts();
    switch (expr->getStmtClass()) {
    case Stmt::ArraySubscriptExprClass: {
      const ArraySubscriptExpr *ASE = cast<ArraySubscriptExpr>(expr);
      CheckArrayAccess(ASE->getBase(), ASE->getIdx(), ASE, AllowOnePastEnd > 0);
      expr = ASE->getBase();
      break;
    }
    case Stmt::MemberExprClass: {
      expr = cast<MemberExpr>(expr)->getBase();
      break;
    }
    case Stmt::UnaryOperatorClass: {
      // Only unwrap the * and & unary operators
      const UnaryOperator *UO = cast<UnaryOperator>(expr);
      expr = UO->getSubExpr();
      switch (UO->getOpcode()) {
      case UO_AddrOf:
        AllowOnePastEnd++;
        break;
      case UO_Deref:
        AllowOnePastEnd--;
        break;
      default:
        return;
      }
      break;
    }
    case Stmt::ConditionalOperatorClass: {
      const ConditionalOperator *cond = cast<ConditionalOperator>(expr);
      if (const Expr *lhs = cond->getLHS())
        CheckArrayAccess(lhs);
      if (const Expr *rhs = cond->getRHS())
        CheckArrayAccess(rhs);
      return;
    }
    default:
      return;
    }
  }
}

// ===----------------------------------------------------------------------===
// Empty body diagnostics
// ===----------------------------------------------------------------------===

namespace {
bool shouldDiagnoseEmptyStmtBody(const SourceManager &SourceMgr,
                                 SourceLocation StmtLoc, const NullStmt *Body) {
  // Do not warn if the body is a macro that expands to nothing, e.g:
  //
  // #define CALL(x)
  // if (condition)
  //   CALL(0);
  if (Body->hasLeadingEmptyMacro())
    return false;

  // Get line numbers of statement and body.
  bool StmtLineInvalid;
  unsigned StmtLine =
      SourceMgr.getPresumedLineNumber(StmtLoc, &StmtLineInvalid);
  if (StmtLineInvalid)
    return false;

  bool BodyLineInvalid;
  unsigned BodyLine =
      SourceMgr.getSpellingLineNumber(Body->getSemiLoc(), &BodyLineInvalid);
  if (BodyLineInvalid)
    return false;

  // Warn if null statement and body are on the same line.
  if (StmtLine != BodyLine)
    return false;

  return true;
}
} // namespace

void Sema::DiagnoseEmptyStmtBody(SourceLocation StmtLoc, const Stmt *Body,
                                 unsigned DiagID) {
  const NullStmt *NBody = dyn_cast<NullStmt>(Body);
  if (!NBody)
    return;

  if (Diags.isIgnored(DiagID, NBody->getSemiLoc()))
    return;

  if (!shouldDiagnoseEmptyStmtBody(SourceMgr, StmtLoc, NBody))
    return;

  Diag(NBody->getSemiLoc(), DiagID);
  Diag(NBody->getSemiLoc(), diag::note_empty_body_on_separate_line);
}

void Sema::DiagnoseEmptyLoopBody(const Stmt *S, const Stmt *PossibleBody) {
  SourceLocation StmtLoc;
  const Stmt *Body;
  unsigned DiagID;
  if (const ForStmt *FS = dyn_cast<ForStmt>(S)) {
    StmtLoc = FS->getRParenLoc();
    Body = FS->getBody();
    DiagID = diag::warn_empty_for_body;
  } else if (const WhileStmt *WS = dyn_cast<WhileStmt>(S)) {
    StmtLoc = WS->getRParenLoc();
    Body = WS->getBody();
    DiagID = diag::warn_empty_while_body;
  } else
    return; // Neither `for' nor `while'.

  // The body should be a null statement.
  const NullStmt *NBody = dyn_cast<NullStmt>(Body);
  if (!NBody)
    return;

  // Skip expensive checks if diagnostic is disabled.
  if (Diags.isIgnored(DiagID, NBody->getSemiLoc()))
    return;

  // Do the usual checks.
  if (!shouldDiagnoseEmptyStmtBody(SourceMgr, StmtLoc, NBody))
    return;

  // `for(...);' and `while(...);' are popular idioms, so in order to keep
  // noise level low, emit diagnostics only if for/while is followed by a
  // CompoundStmt, e.g.:
  //    for (int i = 0; i < n; i++);
  //    {
  //      a(i);
  //    }
  // or if for/while is followed by a statement with more indentation
  // than for/while itself:
  //    for (int i = 0; i < n; i++);
  //      a(i);
  bool ProbableTypo = isa<CompoundStmt>(PossibleBody);
  if (!ProbableTypo) {
    bool BodyColInvalid;
    unsigned BodyCol = SourceMgr.getPresumedColumnNumber(
        PossibleBody->getBeginLoc(), &BodyColInvalid);
    if (BodyColInvalid)
      return;

    bool StmtColInvalid;
    unsigned StmtCol =
        SourceMgr.getPresumedColumnNumber(S->getBeginLoc(), &StmtColInvalid);
    if (StmtColInvalid)
      return;

    if (BodyCol > StmtCol)
      ProbableTypo = true;
  }

  if (ProbableTypo) {
    Diag(NBody->getSemiLoc(), DiagID);
    Diag(NBody->getSemiLoc(), diag::note_empty_body_on_separate_line);
  }
}
