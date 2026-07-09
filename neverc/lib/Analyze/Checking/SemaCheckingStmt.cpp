//===- SemaCheckingStmt.cpp - Bitfield / params / cast-align --------------===//
//
// Remaining SemaCheckingStmt topics after Sequence split: bit-field init,
// function-def parameter validation, and -Wcast-align.
//
#include "Checking/SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/SyncScope.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Format/FormatString.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/SaveAndRestore.h"
#include <cassert>
#include <optional>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Bit-field & parameter validation
// ===----------------------------------------------------------------------===

void Sema::CheckBitFieldInitialization(SourceLocation InitLoc,
                                       FieldDecl *BitField, Expr *Init) {
  (void)AnalyzeBitFieldAssignment(*this, BitField, Init, InitLoc);
}

namespace {
void diagnoseArrayStarInParamType(Sema &S, QualType PType, SourceLocation Loc) {
  if (!PType->isVariablyModifiedType())
    return;
  if (const auto *PointerTy = dyn_cast<PointerType>(PType)) {
    diagnoseArrayStarInParamType(S, PointerTy->getPointeeType(), Loc);
    return;
  }
  if (const auto *ParenTy = dyn_cast<ParenType>(PType)) {
    diagnoseArrayStarInParamType(S, ParenTy->getInnerType(), Loc);
    return;
  }

  const ArrayType *AT = S.Context.getAsArrayType(PType);
  if (!AT)
    return;

  if (AT->getSizeModifier() != ArraySizeModifier::Star) {
    diagnoseArrayStarInParamType(S, AT->getElementType(), Loc);
    return;
  }

  S.Diag(Loc, diag::err_array_star_in_function_definition);
}
} // namespace

bool Sema::CheckParmsForFunctionDef(llvm::ArrayRef<ParmVarDecl *> Parameters,
                                    bool CheckParameterNames) {
  bool HasInvalidParm = false;
  for (ParmVarDecl *Param : Parameters) {
    assert(Param && "null in a parameter list");
    // C99 6.7.5.3p4: parameters in a function *definition* shall have complete
    // type.
    if (!Param->isInvalidDecl() &&
        RequireCompleteType(Param->getLocation(), Param->getType(),
                            diag::err_typecheck_decl_incomplete_type)) {
      Param->setInvalidDecl();
      HasInvalidParm = true;
    }

    // C99 6.9.1p5: If the declarator includes a parameter type list, the
    // declaration of each parameter shall include an identifier.
    if (CheckParameterNames && Param->getIdentifier() == nullptr &&
        !Param->isImplicit()) {
      // Diagnose this as an extension in C17 and earlier.
      if (!getLangOpts().C23)
        Diag(Param->getLocation(), diag::ext_parameter_name_omitted_c23);
    }

    // C99 6.7.5.3p12:
    //   If the function declarator is not part of a definition of that
    //   function, parameters may have incomplete type and may use the [*]
    //   notation in their sequences of declarator specifiers to specify
    //   variable length array types.
    QualType PType = Param->getOriginalType();
    diagnoseArrayStarInParamType(*this, PType, Param->getLocation());

    // Parameters with the pass_object_size attribute only need to be marked
    // constant at function definitions. Because we lack information about
    // whether we're on a declaration or definition when we're instantiating the
    // attribute, we need to check for constness here.
    if (const auto *Attr = Param->getAttr<PassObjectSizeAttr>())
      if (!Param->getType().isConstQualified())
        Diag(Param->getLocation(), diag::err_attribute_pointers_only)
            << Attr->getSpelling() << 1;
  }

  return HasInvalidParm;
}

// ===----------------------------------------------------------------------===
// Cast alignment
// ===----------------------------------------------------------------------===

namespace {

static std::optional<std::pair<CharUnits, CharUnits>>
getBaseAlignmentAndOffsetFromPtr(const Expr *E, TreeContext &Ctx);
std::optional<std::pair<CharUnits, CharUnits>>
getAlignmentAndOffsetFromBinAddOrSub(const Expr *PtrE, const Expr *IntE,
                                     bool IsSub, TreeContext &Ctx) {
  QualType PointeeType = PtrE->getType()->getPointeeType();

  if (!PointeeType->isConstantSizeType())
    return std::nullopt;

  auto P = getBaseAlignmentAndOffsetFromPtr(PtrE, Ctx);

  if (!P)
    return std::nullopt;

  CharUnits EltSize = Ctx.getTypeSizeInChars(PointeeType);
  if (std::optional<llvm::APSInt> IdxRes = IntE->getIntegerConstantExpr(Ctx)) {
    CharUnits Offset = EltSize * IdxRes->getExtValue();
    if (IsSub)
      Offset = -Offset;
    return std::make_pair(P->first, P->second + Offset);
  }

  // If the integer expression isn't a constant expression, compute the lower
  // bound of the alignment using the alignment and offset of the pointer
  // expression and the element size.
  return std::make_pair(
      P->first.alignmentAtOffset(P->second).alignmentAtOffset(EltSize),
      CharUnits::Zero());
}

static std::optional<std::pair<CharUnits, CharUnits>>
getBaseAlignmentAndOffsetFromLValue(const Expr *E, TreeContext &Ctx) {
  E = E->IgnoreParens();
  switch (E->getStmtClass()) {
  default:
    break;
  case Stmt::CStyleCastExprClass:
  case Stmt::ImplicitCastExprClass: {
    auto *CE = cast<CastExpr>(E);
    const Expr *From = CE->getSubExpr();
    switch (CE->getCastKind()) {
    default:
      break;
    case CK_NoOp:
      return getBaseAlignmentAndOffsetFromLValue(From, Ctx);
    }
    break;
  }
  case Stmt::ArraySubscriptExprClass: {
    auto *ASE = cast<ArraySubscriptExpr>(E);
    return getAlignmentAndOffsetFromBinAddOrSub(ASE->getBase(), ASE->getIdx(),
                                                false, Ctx);
  }
  case Stmt::DeclRefExprClass: {
    if (auto *VD = dyn_cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl())) {
      // Dependent alignment cannot be resolved -> bail out.
      if (VD->hasDependentAlignment())
        break;
      return std::make_pair(Ctx.getDeclAlign(VD), CharUnits::Zero());
    }
    break;
  }
  case Stmt::MemberExprClass: {
    auto *ME = cast<MemberExpr>(E);
    auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
    if (!FD || FD->getParent()->isInvalidDecl())
      break;
    std::optional<std::pair<CharUnits, CharUnits>> P;
    if (ME->isArrow())
      P = getBaseAlignmentAndOffsetFromPtr(ME->getBase(), Ctx);
    else
      P = getBaseAlignmentAndOffsetFromLValue(ME->getBase(), Ctx);
    if (!P)
      break;
    const StructRecordLayout &Layout =
        Ctx.getStructRecordLayout(FD->getParent());
    uint64_t Offset = Layout.getFieldOffset(FD->getFieldIndex());
    return std::make_pair(P->first,
                          P->second + CharUnits::fromQuantity(Offset));
  }
  case Stmt::UnaryOperatorClass: {
    auto *UO = cast<UnaryOperator>(E);
    switch (UO->getOpcode()) {
    default:
      break;
    case UO_Deref:
      return getBaseAlignmentAndOffsetFromPtr(UO->getSubExpr(), Ctx);
    }
    break;
  }
  case Stmt::BinaryOperatorClass: {
    auto *BO = cast<BinaryOperator>(E);
    auto Opcode = BO->getOpcode();
    switch (Opcode) {
    default:
      break;
    case BO_Comma:
      return getBaseAlignmentAndOffsetFromLValue(BO->getRHS(), Ctx);
    }
    break;
  }
  }
  return std::nullopt;
}

static std::optional<std::pair<CharUnits, CharUnits>>
getBaseAlignmentAndOffsetFromPtr(const Expr *E, TreeContext &Ctx) {
  E = E->IgnoreParens();
  switch (E->getStmtClass()) {
  default:
    break;
  case Stmt::CStyleCastExprClass:
  case Stmt::ImplicitCastExprClass: {
    auto *CE = cast<CastExpr>(E);
    const Expr *From = CE->getSubExpr();
    switch (CE->getCastKind()) {
    default:
      break;
    case CK_NoOp:
      return getBaseAlignmentAndOffsetFromPtr(From, Ctx);
    case CK_ArrayToPointerDecay:
      return getBaseAlignmentAndOffsetFromLValue(From, Ctx);
    }
    break;
  }
  case Stmt::UnaryOperatorClass: {
    auto *UO = cast<UnaryOperator>(E);
    if (UO->getOpcode() == UO_AddrOf)
      return getBaseAlignmentAndOffsetFromLValue(UO->getSubExpr(), Ctx);
    break;
  }
  case Stmt::BinaryOperatorClass: {
    auto *BO = cast<BinaryOperator>(E);
    auto Opcode = BO->getOpcode();
    switch (Opcode) {
    default:
      break;
    case BO_Add:
    case BO_Sub: {
      const Expr *LHS = BO->getLHS(), *RHS = BO->getRHS();
      if (Opcode == BO_Add && !RHS->getType()->isIntegralOrEnumerationType())
        std::swap(LHS, RHS);
      return getAlignmentAndOffsetFromBinAddOrSub(LHS, RHS, Opcode == BO_Sub,
                                                  Ctx);
    }
    case BO_Comma:
      return getBaseAlignmentAndOffsetFromPtr(BO->getRHS(), Ctx);
    }
    break;
  }
  }
  return std::nullopt;
}

CharUnits getPresumedAlignmentOfPointer(const Expr *E, Sema &S) {
  // See if we can compute the alignment of a VarDecl and an offset from it.
  std::optional<std::pair<CharUnits, CharUnits>> P =
      getBaseAlignmentAndOffsetFromPtr(E, S.Context);

  if (P)
    return P->first.alignmentAtOffset(P->second);

  // If that failed, return the type's alignment.
  return S.Context.getTypeAlignInChars(E->getType()->getPointeeType());
}
} // namespace

void Sema::CheckCastAlign(Expr *Op, QualType T, SourceRange TRange) {
  // This is actually a lot of work to potentially be doing on every
  // cast; don't do it if we're ignoring -Wcast_align (as is the default).
  if (getDiagnostics().isIgnored(diag::warn_cast_align, TRange.getBegin()))
    return;

  // Require that the destination be a pointer type.
  const PointerType *DestPtr = T->getAs<PointerType>();
  if (!DestPtr)
    return;

  // If the destination has alignment 1, we're done.
  QualType DestPointee = DestPtr->getPointeeType();
  if (DestPointee->isIncompleteType())
    return;
  CharUnits DestAlign = Context.getTypeAlignInChars(DestPointee);
  if (DestAlign.isOne())
    return;

  // Require that the source be a pointer type.
  const PointerType *SrcPtr = Op->getType()->getAs<PointerType>();
  if (!SrcPtr)
    return;
  QualType SrcPointee = SrcPtr->getPointeeType();

  // Explicitly allow casts from cv void*.  We already implicitly
  // allowed casts to cv void*, since they have alignment 1.
  // Also allow casts involving incomplete types, which implicitly
  // includes 'void'.
  if (SrcPointee->isIncompleteType())
    return;

  CharUnits SrcAlign = getPresumedAlignmentOfPointer(Op, *this);

  if (SrcAlign >= DestAlign)
    return;

  Diag(TRange.getBegin(), diag::warn_cast_align)
      << Op->getType() << T << static_cast<unsigned>(SrcAlign.getQuantity())
      << static_cast<unsigned>(DestAlign.getQuantity()) << TRange
      << Op->getSourceRange();
}
