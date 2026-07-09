#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>
#include "Checking/SemaCheckingUtils.h"
#include "neverc/Tree/Format/FormatString.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include <algorithm>
#include <optional>

using namespace neverc;
using namespace sema;

// === Split from SemaChecking.cpp ===
// Memory-function checks: checkMemorySizeofForComparison, checkMemaccessSize,
// Sema::CheckMemaccessArguments, Sema::CheckStrlcpycatArguments,
// Sema::CheckStrncatArguments, Sema::CheckFreeArguments,
// Sema::checkFortifiedBuiltinMemoryFunction (+ local format-size helpers).

//===--- CHECK: Standard memory functions ---------------------------------===//

namespace {
bool checkMemorySizeofForComparison(Sema &S, const Expr *E,
                                    IdentifierInfo *FnName,
                                    SourceLocation FnLoc,
                                    SourceLocation RParenLoc) {
  const BinaryOperator *Size = dyn_cast<BinaryOperator>(E);
  if (!Size)
    return false;

  // if E is binop and op is <=>, >, <, >=, <=, ==, &&, ||:
  if (!Size->isComparisonOp() && !Size->isLogicalOp())
    return false;

  SourceRange SizeRange = Size->getSourceRange();
  S.Diag(Size->getOperatorLoc(), diag::warn_memsize_comparison)
      << SizeRange << FnName;
  S.Diag(FnLoc, diag::note_memsize_comparison_paren)
      << FnName
      << FixItHint::CreateInsertion(
             S.getLocForEndOfToken(Size->getLHS()->getEndLoc()), ")")
      << FixItHint::CreateRemoval(RParenLoc);
  S.Diag(SizeRange.getBegin(), diag::note_memsize_comparison_cast_silence)
      << FixItHint::CreateInsertion(SizeRange.getBegin(), "(size_t)(")
      << FixItHint::CreateInsertion(S.getLocForEndOfToken(SizeRange.getEnd()),
                                    ")");

  return true;
}

const UnaryExprOrTypeTraitExpr *getAsSizeOfExpr(const Expr *E) {
  if (const auto *Unary = dyn_cast<UnaryExprOrTypeTraitExpr>(E))
    if (Unary->getKind() == UETT_SizeOf)
      return Unary;
  return nullptr;
}

const Expr *getSizeOfExprArg(const Expr *E) {
  if (const UnaryExprOrTypeTraitExpr *SizeOf = getAsSizeOfExpr(E))
    if (!SizeOf->isArgumentType())
      return SizeOf->getArgumentExpr()->IgnoreParenImpCasts();
  return nullptr;
}

QualType getSizeOfArgType(const Expr *E) {
  if (const UnaryExprOrTypeTraitExpr *SizeOf = getAsSizeOfExpr(E))
    return SizeOf->getTypeOfArgument();
  return QualType();
}

bool doesExprLikelyComputeSize(const Expr *SizeofExpr) {
  SizeofExpr = SizeofExpr->IgnoreParenImpCasts();

  if (const auto *BO = dyn_cast<BinaryOperator>(SizeofExpr)) {
    if (BO->getOpcode() != BO_Mul && BO->getOpcode() != BO_Add)
      return false;

    return doesExprLikelyComputeSize(BO->getLHS()) ||
           doesExprLikelyComputeSize(BO->getRHS());
  }

  return getAsSizeOfExpr(SizeofExpr) != nullptr;
}

bool isArgumentExpandedFromMacro(SourceManager &SM, SourceLocation CallLoc,
                                 SourceLocation ArgLoc) {
  if (!CallLoc.isMacroID())
    return SM.getFileID(CallLoc) != SM.getFileID(ArgLoc);

  return SM.getFileID(SM.getImmediateMacroCallerLoc(CallLoc)) !=
         SM.getFileID(SM.getImmediateMacroCallerLoc(ArgLoc));
}

void checkMemaccessSize(Sema &S, unsigned BId, const CallExpr *Call) {
  if (BId != Builtin::BImemset && BId != Builtin::BIbzero)
    return;

  const Expr *SizeArg =
      Call->getArg(BId == Builtin::BImemset ? 2 : 1)->IgnoreImpCasts();

  auto isLiteralZero = [](const Expr *E) {
    return (isa<IntegerLiteral>(E) &&
            cast<IntegerLiteral>(E)->getValue() == 0) ||
           (isa<CharacterLiteral>(E) &&
            cast<CharacterLiteral>(E)->getValue() == 0);
  };

  // If we're memsetting or bzeroing 0 bytes, then this is likely an error.
  SourceLocation CallLoc = Call->getRParenLoc();
  SourceManager &SM = S.getSourceManager();
  if (isLiteralZero(SizeArg) &&
      !isArgumentExpandedFromMacro(SM, CallLoc, SizeArg->getExprLoc())) {

    SourceLocation DiagLoc = SizeArg->getExprLoc();

    // Some platforms #define bzero to __builtin_memset. See if this is the
    // case, and if so, emit a better diagnostic.
    if (BId == Builtin::BIbzero ||
        (CallLoc.isMacroID() && SourceScanner::getImmediateMacroName(
                                    CallLoc, SM, S.getLangOpts()) == "bzero")) {
      S.Diag(DiagLoc, diag::warn_suspicious_bzero_size);
      S.Diag(DiagLoc, diag::note_suspicious_bzero_size_silence);
    } else if (!isLiteralZero(Call->getArg(1)->IgnoreImpCasts())) {
      S.Diag(DiagLoc, diag::warn_suspicious_sizeof_memset) << 0;
      S.Diag(DiagLoc, diag::note_suspicious_sizeof_memset_silence) << 0;
    }
    return;
  }

  // If the second argument to a memset is a sizeof expression and the third
  // isn't, this is also likely an error. This should catch
  // 'memset(buf, sizeof(buf), 0xff)'.
  if (BId == Builtin::BImemset && doesExprLikelyComputeSize(Call->getArg(1)) &&
      !doesExprLikelyComputeSize(Call->getArg(2))) {
    SourceLocation DiagLoc = Call->getArg(1)->getExprLoc();
    S.Diag(DiagLoc, diag::warn_suspicious_sizeof_memset) << 1;
    S.Diag(DiagLoc, diag::note_suspicious_sizeof_memset_silence) << 1;
    return;
  }
}
} // namespace

void Sema::CheckMemaccessArguments(const CallExpr *Call, unsigned BId,
                                   IdentifierInfo *FnName) {
  assert(BId != 0);

  // It is possible to have a non-standard definition of memset.  Validate
  // we have enough arguments, and if not, abort further checking.
  unsigned ExpectedNumArgs =
      (BId == Builtin::BIstrndup || BId == Builtin::BIbzero ? 2 : 3);
  if (Call->getNumArgs() < ExpectedNumArgs)
    return;

  unsigned LastArg = (BId == Builtin::BImemset || BId == Builtin::BIbzero ||
                              BId == Builtin::BIstrndup
                          ? 1
                          : 2);
  unsigned LenArg =
      (BId == Builtin::BIbzero || BId == Builtin::BIstrndup ? 1 : 2);
  const Expr *LenExpr = Call->getArg(LenArg)->IgnoreParenImpCasts();

  if (checkMemorySizeofForComparison(*this, LenExpr, FnName,
                                     Call->getBeginLoc(), Call->getRParenLoc()))
    return;

  // Catch cases like 'memset(buf, sizeof(buf), 0)'.
  checkMemaccessSize(*this, BId, Call);

  // We have special checking when the length is a sizeof expression.
  QualType SizeOfArgTy = getSizeOfArgType(LenExpr);
  const Expr *SizeOfArg = getSizeOfExprArg(LenExpr);
  llvm::FoldingSetNodeID SizeOfArgID;

  // Although widely used, 'bzero' is not a standard function. Be more strict
  // with the argument types before allowing diagnostics and only allow the
  // form bzero(ptr, sizeof(...)).
  QualType FirstArgTy = Call->getArg(0)->IgnoreParenImpCasts()->getType();
  if (BId == Builtin::BIbzero && !FirstArgTy->getAs<PointerType>())
    return;

  for (unsigned ArgIdx = 0; ArgIdx != LastArg; ++ArgIdx) {
    const Expr *Dest = Call->getArg(ArgIdx)->IgnoreParenImpCasts();
    SourceRange ArgRange = Call->getArg(ArgIdx)->getSourceRange();

    QualType DestTy = Dest->getType();
    QualType PointeeTy;
    if (const PointerType *DestPtrTy = DestTy->getAs<PointerType>()) {
      PointeeTy = DestPtrTy->getPointeeType();

      // Never warn about void type pointers. This can be used to suppress
      // false positives.
      if (PointeeTy->isVoidType())
        continue;

      // Catch "memset(p, 0, sizeof(p))" -- needs to be sizeof(*p). Do this by
      // actually comparing the expressions for equality. Because computing the
      // expression IDs can be expensive, we only do this if the diagnostic is
      // enabled.
      if (SizeOfArg &&
          !Diags.isIgnored(diag::warn_sizeof_pointer_expr_memaccess,
                           SizeOfArg->getExprLoc())) {
        // We only compute IDs for expressions if the warning is enabled, and
        // cache the sizeof arg's ID.
        if (SizeOfArgID == llvm::FoldingSetNodeID())
          SizeOfArg->Profile(SizeOfArgID, Context, true);
        llvm::FoldingSetNodeID DestID;
        Dest->Profile(DestID, Context, true);
        if (DestID == SizeOfArgID) {
          unsigned ActionIdx = 0; // Default is to suggest dereferencing.
          llvm::StringRef ReadableName = FnName->getName();

          if (const UnaryOperator *UnaryOp = dyn_cast<UnaryOperator>(Dest))
            if (UnaryOp->getOpcode() == UO_AddrOf)
              ActionIdx = 1; // If its an address-of operator, just remove it.
          if (!PointeeTy->isIncompleteType() &&
              (Context.getTypeSize(PointeeTy) == Context.getCharWidth()))
            ActionIdx = 2; // If the pointee's size is sizeof(char),
                           // suggest an explicit length.

          // If the function is defined as a builtin macro, do not show macro
          // expansion.
          SourceLocation SL = SizeOfArg->getExprLoc();
          SourceRange DSR = Dest->getSourceRange();
          SourceRange SSR = SizeOfArg->getSourceRange();
          SourceManager &SM = getSourceManager();

          if (SM.isMacroArgExpansion(SL)) {
            ReadableName =
                SourceScanner::getImmediateMacroName(SL, SM, LangOpts);
            SL = SM.getSpellingLoc(SL);
            DSR = SourceRange(SM.getSpellingLoc(DSR.getBegin()),
                              SM.getSpellingLoc(DSR.getEnd()));
            SSR = SourceRange(SM.getSpellingLoc(SSR.getBegin()),
                              SM.getSpellingLoc(SSR.getEnd()));
          }

          DiagRuntimeBehavior(SL, SizeOfArg,
                              PDiag(diag::warn_sizeof_pointer_expr_memaccess)
                                  << ReadableName << PointeeTy << DestTy << DSR
                                  << SSR);
          DiagRuntimeBehavior(
              SL, SizeOfArg,
              PDiag(diag::warn_sizeof_pointer_expr_memaccess_note)
                  << ActionIdx << SSR);

          break;
        }
      }

      // Also check for cases where the sizeof argument is the exact same
      // type as the memory argument, and where it points to a user-defined
      // record type.
      if (SizeOfArgTy != QualType()) {
        if (PointeeTy->isRecordType() &&
            Context.typesAreCompatible(SizeOfArgTy, DestTy)) {
          DiagRuntimeBehavior(LenExpr->getExprLoc(), Dest,
                              PDiag(diag::warn_sizeof_pointer_type_memaccess)
                                  << FnName << SizeOfArgTy << ArgIdx
                                  << PointeeTy << Dest->getSourceRange()
                                  << LenExpr->getSourceRange());
          break;
        }
      }
    } else if (DestTy->isArrayType()) {
      PointeeTy = DestTy;
    }

    if (PointeeTy == QualType())
      continue;

    continue;

    DiagRuntimeBehavior(
        Dest->getExprLoc(), Dest,
        PDiag(diag::note_bad_memaccess_silence)
            << FixItHint::CreateInsertion(ArgRange.getBegin(), "(void*)"));
    break;
  }
}

// A little helper routine: ignore addition and subtraction of integer literals.
// This intentionally does not ignore all integer constant expressions because
// we don't want to remove sizeof().
namespace {
const Expr *ignoreLiteralAdditions(const Expr *Ex, TreeContext &Ctx) {
  Ex = Ex->IgnoreParenCasts();

  while (true) {
    const BinaryOperator *BO = dyn_cast<BinaryOperator>(Ex);
    if (!BO || !BO->isAdditiveOp())
      break;

    const Expr *RHS = BO->getRHS()->IgnoreParenCasts();
    const Expr *LHS = BO->getLHS()->IgnoreParenCasts();

    if (isa<IntegerLiteral>(RHS))
      Ex = LHS;
    else if (isa<IntegerLiteral>(LHS))
      Ex = RHS;
    else
      break;
  }

  return Ex;
}

bool isConstantSizeArrayWithMoreThanOneElement(QualType Ty,
                                               TreeContext &Context) {
  // Only handle constant-sized or VLAs, but not flexible members.
  if (const ConstantArrayType *CAT = Context.getAsConstantArrayType(Ty)) {
    // Only issue the FIXIT for arrays of size > 1.
    if (CAT->getSize().getSExtValue() <= 1)
      return false;
  } else if (!Ty->isVariableArrayType()) {
    return false;
  }
  return true;
}
} // namespace

// Warn if the user has made the 'size' argument to strlcpy or strlcat
// be the size of the source, instead of the destination.
void Sema::CheckStrlcpycatArguments(const CallExpr *Call,
                                    IdentifierInfo *FnName) {

  // Don't crash if the user has the wrong number of arguments
  unsigned NumArgs = Call->getNumArgs();
  if ((NumArgs != 3) && (NumArgs != 4))
    return;

  const Expr *SrcArg = ignoreLiteralAdditions(Call->getArg(1), Context);
  const Expr *SizeArg = ignoreLiteralAdditions(Call->getArg(2), Context);
  const Expr *CompareWithSrc = nullptr;

  if (checkMemorySizeofForComparison(*this, SizeArg, FnName,
                                     Call->getBeginLoc(), Call->getRParenLoc()))
    return;

  // Look for 'strlcpy(dst, x, sizeof(x))'
  if (const Expr *Ex = getSizeOfExprArg(SizeArg))
    CompareWithSrc = Ex;
  else {
    // Look for 'strlcpy(dst, x, strlen(x))'
    if (const CallExpr *SizeCall = dyn_cast<CallExpr>(SizeArg)) {
      if (SizeCall->getBuiltinCallee() == Builtin::BIstrlen &&
          SizeCall->getNumArgs() == 1)
        CompareWithSrc = ignoreLiteralAdditions(SizeCall->getArg(0), Context);
    }
  }

  if (!CompareWithSrc)
    return;

  // Determine if the argument to sizeof/strlen is equal to the source
  // argument.  In principle there's all kinds of things you could do
  // here, for instance creating an == expression and evaluating it with
  // EvaluateAsBooleanCondition, but this uses a more direct technique:
  const DeclRefExpr *SrcArgDRE = dyn_cast<DeclRefExpr>(SrcArg);
  if (!SrcArgDRE)
    return;

  const DeclRefExpr *CompareWithSrcDRE = dyn_cast<DeclRefExpr>(CompareWithSrc);
  if (!CompareWithSrcDRE ||
      SrcArgDRE->getDecl() != CompareWithSrcDRE->getDecl())
    return;

  const Expr *OriginalSizeArg = Call->getArg(2);
  Diag(CompareWithSrcDRE->getBeginLoc(), diag::warn_strlcpycat_wrong_size)
      << OriginalSizeArg->getSourceRange() << FnName;

  // Output a FIXIT hint if the destination is an array (rather than a
  // pointer to an array).  This could be enhanced to handle some
  // pointers if we know the actual size, like if DstArg is 'array+2'
  // we could say 'sizeof(array)-2'.
  const Expr *DstArg = Call->getArg(0)->IgnoreParenImpCasts();
  if (!isConstantSizeArrayWithMoreThanOneElement(DstArg->getType(), Context))
    return;

  llvm::SmallString<128> sizeString;
  llvm::raw_svector_ostream OS(sizeString);
  OS << "sizeof(";
  DstArg->printPretty(OS, nullptr, getPrintingPolicy());
  OS << ")";

  Diag(OriginalSizeArg->getBeginLoc(), diag::note_strlcpycat_wrong_size)
      << FixItHint::CreateReplacement(OriginalSizeArg->getSourceRange(),
                                      OS.str());
}

namespace {
bool referToTheSameDecl(const Expr *E1, const Expr *E2) {
  if (const DeclRefExpr *D1 = dyn_cast_or_null<DeclRefExpr>(E1))
    if (const DeclRefExpr *D2 = dyn_cast_or_null<DeclRefExpr>(E2))
      return D1->getDecl() == D2->getDecl();
  return false;
}

const Expr *getStrlenExprArg(const Expr *E) {
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    const FunctionDecl *FD = CE->getDirectCallee();
    if (!FD || FD->getMemoryFunctionKind() != Builtin::BIstrlen)
      return nullptr;
    return CE->getArg(0)->IgnoreParenCasts();
  }
  return nullptr;
}
} // namespace

// Warn on anti-patterns as the 'size' argument to strncat.
// The correct size argument should look like following:
//   strncat(dst, src, sizeof(dst) - strlen(dest) - 1);
void Sema::CheckStrncatArguments(const CallExpr *CE, IdentifierInfo *FnName) {
  // Don't crash if the user has the wrong number of arguments.
  if (CE->getNumArgs() < 3)
    return;
  const Expr *DstArg = CE->getArg(0)->IgnoreParenCasts();
  const Expr *SrcArg = CE->getArg(1)->IgnoreParenCasts();
  const Expr *LenArg = CE->getArg(2)->IgnoreParenCasts();

  if (checkMemorySizeofForComparison(*this, LenArg, FnName, CE->getBeginLoc(),
                                     CE->getRParenLoc()))
    return;

  // Identify common expressions, which are wrongly used as the size argument
  // to strncat and may lead to buffer overflows.
  unsigned PatternType = 0;
  if (const Expr *SizeOfArg = getSizeOfExprArg(LenArg)) {
    // - sizeof(dst)
    if (referToTheSameDecl(SizeOfArg, DstArg))
      PatternType = 1;
    // - sizeof(src)
    else if (referToTheSameDecl(SizeOfArg, SrcArg))
      PatternType = 2;
  } else if (const BinaryOperator *BE = dyn_cast<BinaryOperator>(LenArg)) {
    if (BE->getOpcode() == BO_Sub) {
      const Expr *L = BE->getLHS()->IgnoreParenCasts();
      const Expr *R = BE->getRHS()->IgnoreParenCasts();
      // - sizeof(dst) - strlen(dst)
      if (referToTheSameDecl(DstArg, getSizeOfExprArg(L)) &&
          referToTheSameDecl(DstArg, getStrlenExprArg(R)))
        PatternType = 1;
      // - sizeof(src) - (anything)
      else if (referToTheSameDecl(SrcArg, getSizeOfExprArg(L)))
        PatternType = 2;
    }
  }

  if (PatternType == 0)
    return;

  // Generate the diagnostic.
  SourceLocation SL = LenArg->getBeginLoc();
  SourceRange SR = LenArg->getSourceRange();
  SourceManager &SM = getSourceManager();

  // If the function is defined as a builtin macro, do not show macro expansion.
  if (SM.isMacroArgExpansion(SL)) {
    SL = SM.getSpellingLoc(SL);
    SR = SourceRange(SM.getSpellingLoc(SR.getBegin()),
                     SM.getSpellingLoc(SR.getEnd()));
  }

  // Check if the destination is an array (rather than a pointer to an array).
  QualType DstTy = DstArg->getType();
  bool isKnownSizeArray =
      isConstantSizeArrayWithMoreThanOneElement(DstTy, Context);
  if (!isKnownSizeArray) {
    if (PatternType == 1)
      Diag(SL, diag::warn_strncat_wrong_size) << SR;
    else
      Diag(SL, diag::warn_strncat_src_size) << SR;
    return;
  }

  if (PatternType == 1)
    Diag(SL, diag::warn_strncat_large_size) << SR;
  else
    Diag(SL, diag::warn_strncat_src_size) << SR;

  llvm::SmallString<128> sizeString;
  llvm::raw_svector_ostream OS(sizeString);
  OS << "sizeof(";
  DstArg->printPretty(OS, nullptr, getPrintingPolicy());
  OS << ") - ";
  OS << "strlen(";
  DstArg->printPretty(OS, nullptr, getPrintingPolicy());
  OS << ") - 1";

  Diag(SL, diag::note_strncat_wrong_size)
      << FixItHint::CreateReplacement(SR, OS.str());
}

namespace {
void CheckFreeArgumentsOnLvalue(Sema &S, const std::string &CalleeName,
                                const UnaryOperator *UnaryExpr, const Decl *D) {
  if (isa<FieldDecl, FunctionDecl, VarDecl>(D)) {
    S.Diag(UnaryExpr->getBeginLoc(), diag::warn_free_nonheap_object)
        << CalleeName << cast<NamedDecl>(D);
    return;
  }
}

void CheckFreeArgumentsAddressof(Sema &S, const std::string &CalleeName,
                                 const UnaryOperator *UnaryExpr) {
  if (const auto *Lvalue = dyn_cast<DeclRefExpr>(UnaryExpr->getSubExpr())) {
    const Decl *D = Lvalue->getDecl();
    if (isa<DeclaratorDecl>(D))
      return CheckFreeArgumentsOnLvalue(S, CalleeName, UnaryExpr, D);
  }

  if (const auto *Lvalue = dyn_cast<MemberExpr>(UnaryExpr->getSubExpr()))
    return CheckFreeArgumentsOnLvalue(S, CalleeName, UnaryExpr,
                                      Lvalue->getMemberDecl());
}

void CheckFreeArgumentsStackArray(Sema &S, const std::string &CalleeName,
                                  const DeclRefExpr *Lvalue) {
  const auto *Var = dyn_cast<VarDecl>(Lvalue->getDecl());
  if (Var == nullptr)
    return;

  S.Diag(Lvalue->getBeginLoc(), diag::warn_free_nonheap_object)
      << CalleeName << Var;
}

void CheckFreeArgumentsCast(Sema &S, const std::string &CalleeName,
                            const CastExpr *Cast) {
  llvm::SmallString<128> SizeString;
  llvm::raw_svector_ostream OS(SizeString);

  neverc::CastKind Kind = Cast->getCastKind();
  if (Kind == neverc::CK_BitCast &&
      !Cast->getSubExpr()->getType()->isFunctionPointerType())
    return;
  if (Kind == neverc::CK_IntegralToPointer &&
      !isa<IntegerLiteral>(
          Cast->getSubExpr()->IgnoreParenImpCasts()->IgnoreParens()))
    return;

  switch (Cast->getCastKind()) {
  case neverc::CK_BitCast:
  case neverc::CK_IntegralToPointer:
  case neverc::CK_FunctionToPointerDecay:
    OS << '\'';
    Cast->printPretty(OS, nullptr, S.getPrintingPolicy());
    OS << '\'';
    break;
  default:
    return;
  }

  S.Diag(Cast->getBeginLoc(), diag::warn_free_nonheap_object)
      << CalleeName << OS.str();
}
} // namespace

void Sema::CheckFreeArguments(const CallExpr *E) {
  const std::string CalleeName =
      cast<FunctionDecl>(E->getCalleeDecl())->getQualifiedNameAsString();

  { // Prefer something that doesn't involve a cast to make things simpler.
    const Expr *Arg = E->getArg(0)->IgnoreParenCasts();
    if (const auto *UnaryExpr = dyn_cast<UnaryOperator>(Arg))
      switch (UnaryExpr->getOpcode()) {
      case UnaryOperator::Opcode::UO_AddrOf:
        return CheckFreeArgumentsAddressof(*this, CalleeName, UnaryExpr);
      default:
        break;
      }

    if (const auto *Lvalue = dyn_cast<DeclRefExpr>(Arg))
      if (Lvalue->getType()->isArrayType())
        return CheckFreeArgumentsStackArray(*this, CalleeName, Lvalue);

    if (const auto *Label = dyn_cast<AddrLabelExpr>(Arg)) {
      Diag(Label->getBeginLoc(), diag::warn_free_nonheap_object)
          << CalleeName << Label->getLabel()->getIdentifier();
      return;
    }
  }
  // Maybe the cast was important, check after the other cases.
  if (const auto *Cast = dyn_cast<CastExpr>(E->getArg(0)))
    return CheckFreeArgumentsCast(*this, CalleeName, Cast);
}

// ===----------------------------------------------------------------------===
// Fortified memory function checking (moved from SemaChecking.cpp)
// ===----------------------------------------------------------------------===

class ScanfDiagnosticFormatHandler
    : public analyze_format_string::FormatStringHandler {
  // Accepts the argument index (relative to the first destination index) of the
  // argument whose size we want.
  using ComputeSizeFunction =
      llvm::function_ref<std::optional<llvm::APSInt>(unsigned)>;

  // Accepts the argument index (relative to the first destination index), the
  // destination size, and the source size).
  using DiagnoseFunction =
      llvm::function_ref<void(unsigned, unsigned, unsigned)>;

  ComputeSizeFunction ComputeSizeArgument;
  DiagnoseFunction Diagnose;

public:
  ScanfDiagnosticFormatHandler(ComputeSizeFunction ComputeSizeArgument,
                               DiagnoseFunction Diagnose)
      : ComputeSizeArgument(ComputeSizeArgument), Diagnose(Diagnose) {}

  bool HandleScanfSpecifier(const analyze_scanf::ScanfSpecifier &FS,
                            const char *StartSpecifier,
                            unsigned specifierLen) override {
    if (!FS.consumesDataArgument())
      return true;

    unsigned NulByte = 0;
    switch ((FS.getConversionSpecifier().getKind())) {
    default:
      return true;
    case analyze_format_string::ConversionSpecifier::sArg:
    case analyze_format_string::ConversionSpecifier::ScanListArg:
      NulByte = 1;
      break;
    case analyze_format_string::ConversionSpecifier::cArg:
      break;
    }

    analyze_format_string::OptionalAmount FW = FS.getFieldWidth();
    if (FW.getHowSpecified() !=
        analyze_format_string::OptionalAmount::HowSpecified::Constant)
      return true;

    unsigned SourceSize = FW.getConstantAmount() + NulByte;

    std::optional<llvm::APSInt> DestSizeAPS =
        ComputeSizeArgument(FS.getArgIndex());
    if (!DestSizeAPS)
      return true;

    unsigned DestSize = DestSizeAPS->getZExtValue();

    if (DestSize < SourceSize)
      Diagnose(FS.getArgIndex(), DestSize, SourceSize);

    return true;
  }
};

class EstimateSizeFormatHandler
    : public analyze_format_string::FormatStringHandler {
  size_t Size;

public:
  EstimateSizeFormatHandler(llvm::StringRef Format)
      : Size(std::min(Format.find(0), Format.size()) +
             1 /* null byte always written by sprintf */) {}

  bool HandlePrintfSpecifier(const analyze_printf::PrintfSpecifier &FS,
                             const char *, unsigned SpecifierLen,
                             const TargetInfo &) override {

    const size_t FieldWidth = computeFieldWidth(FS);
    const size_t Precision = computePrecision(FS);

    // The actual format.
    switch (FS.getConversionSpecifier().getKind()) {
    // Just a char.
    case analyze_format_string::ConversionSpecifier::cArg:
    case analyze_format_string::ConversionSpecifier::CArg:
      Size += std::max(FieldWidth, (size_t)1);
      break;
    // Just an integer.
    case analyze_format_string::ConversionSpecifier::dArg:
    case analyze_format_string::ConversionSpecifier::DArg:
    case analyze_format_string::ConversionSpecifier::iArg:
    case analyze_format_string::ConversionSpecifier::oArg:
    case analyze_format_string::ConversionSpecifier::OArg:
    case analyze_format_string::ConversionSpecifier::uArg:
    case analyze_format_string::ConversionSpecifier::UArg:
    case analyze_format_string::ConversionSpecifier::xArg:
    case analyze_format_string::ConversionSpecifier::XArg:
      Size += std::max(FieldWidth, Precision);
      break;

    // %g style conversion switches between %f or %e style dynamically.
    // %g removes trailing zeros, and does not print decimal point if there are
    // no digits that follow it. Thus %g can print a single digit.
    // If it is alternative form:
    // For g and G conversions, trailing zeros are not removed from the result.
    case analyze_format_string::ConversionSpecifier::gArg:
    case analyze_format_string::ConversionSpecifier::GArg:
      Size += 1;
      break;

    // Floating point number in the form '[+]ddd.ddd'.
    case analyze_format_string::ConversionSpecifier::fArg:
    case analyze_format_string::ConversionSpecifier::FArg:
      Size += std::max(FieldWidth, 1 /* integer part */ +
                                       (Precision ? 1 + Precision
                                                  : 0) /* period + decimal */);
      break;

    // Floating point number in the form '[-]d.ddde[+-]dd'.
    case analyze_format_string::ConversionSpecifier::eArg:
    case analyze_format_string::ConversionSpecifier::EArg:
      Size +=
          std::max(FieldWidth,
                   1 /* integer part */ +
                       (Precision ? 1 + Precision : 0) /* period + decimal */ +
                       1 /* e or E letter */ + 2 /* exponent */);
      break;

    // Floating point number in the form '[-]0xh.hhhhp±dd'.
    case analyze_format_string::ConversionSpecifier::aArg:
    case analyze_format_string::ConversionSpecifier::AArg:
      Size +=
          std::max(FieldWidth,
                   2 /* 0x */ + 1 /* integer part */ +
                       (Precision ? 1 + Precision : 0) /* period + decimal */ +
                       1 /* p or P letter */ + 1 /* + or - */ + 1 /* value */);
      break;

    // Just a string.
    case analyze_format_string::ConversionSpecifier::sArg:
    case analyze_format_string::ConversionSpecifier::SArg:
      Size += FieldWidth;
      break;

    // Just a pointer in the form '0xddd'.
    case analyze_format_string::ConversionSpecifier::pArg:
      Size += std::max(FieldWidth, 2 /* leading 0x */ + Precision);
      break;

    // A plain percent.
    case analyze_format_string::ConversionSpecifier::PercentArg:
      Size += 1;
      break;

    default:
      break;
    }

    Size += FS.hasPlusPrefix() || FS.hasSpacePrefix();

    if (FS.hasAlternativeForm()) {
      switch (FS.getConversionSpecifier().getKind()) {
      // For o conversion, it increases the precision, if and only if necessary,
      // to force the first digit of the result to be a zero
      // (if the value and precision are both 0, a single 0 is printed)
      case analyze_format_string::ConversionSpecifier::oArg:
      // For b conversion, a nonzero result has 0b prefixed to it.
      case analyze_format_string::ConversionSpecifier::bArg:
      // For x (or X) conversion, a nonzero result has 0x (or 0X) prefixed to
      // it.
      case analyze_format_string::ConversionSpecifier::xArg:
      case analyze_format_string::ConversionSpecifier::XArg:
        // Note: even when the prefix is added, if
        // (prefix_width <= FieldWidth - formatted_length) holds,
        // the prefix does not increase the format
        // size. e.g.(("%#3x", 0xf) is "0xf")

        // If the result is zero, o, b, x, X adds nothing.
        break;
      // For a, A, e, E, f, F, g, and G conversions,
      // the result of converting a floating-point number always contains a
      // decimal-point
      case analyze_format_string::ConversionSpecifier::aArg:
      case analyze_format_string::ConversionSpecifier::AArg:
      case analyze_format_string::ConversionSpecifier::eArg:
      case analyze_format_string::ConversionSpecifier::EArg:
      case analyze_format_string::ConversionSpecifier::fArg:
      case analyze_format_string::ConversionSpecifier::FArg:
      case analyze_format_string::ConversionSpecifier::gArg:
      case analyze_format_string::ConversionSpecifier::GArg:
        Size += (Precision ? 0 : 1);
        break;
      // For other conversions, the behavior is undefined.
      default:
        break;
      }
    }
    assert(SpecifierLen <= Size && "no underflow");
    Size -= SpecifierLen;
    return true;
  }

  size_t getSizeLowerBound() const { return Size; }

private:
  static size_t computeFieldWidth(const analyze_printf::PrintfSpecifier &FS) {
    const analyze_format_string::OptionalAmount &FW = FS.getFieldWidth();
    size_t FieldWidth = 0;
    if (FW.getHowSpecified() == analyze_format_string::OptionalAmount::Constant)
      FieldWidth = FW.getConstantAmount();
    return FieldWidth;
  }

  static size_t computePrecision(const analyze_printf::PrintfSpecifier &FS) {
    const analyze_format_string::OptionalAmount &FW = FS.getPrecision();
    size_t Precision = 0;

    // See man 3 printf for default precision value based on the specifier.
    switch (FW.getHowSpecified()) {
    case analyze_format_string::OptionalAmount::NotSpecified:
      switch (FS.getConversionSpecifier().getKind()) {
      default:
        break;
      case analyze_format_string::ConversionSpecifier::dArg: // %d
      case analyze_format_string::ConversionSpecifier::DArg: // %D
      case analyze_format_string::ConversionSpecifier::iArg: // %i
        Precision = 1;
        break;
      case analyze_format_string::ConversionSpecifier::oArg: // %d
      case analyze_format_string::ConversionSpecifier::OArg: // %D
      case analyze_format_string::ConversionSpecifier::uArg: // %d
      case analyze_format_string::ConversionSpecifier::UArg: // %D
      case analyze_format_string::ConversionSpecifier::xArg: // %d
      case analyze_format_string::ConversionSpecifier::XArg: // %D
        Precision = 1;
        break;
      case analyze_format_string::ConversionSpecifier::fArg: // %f
      case analyze_format_string::ConversionSpecifier::FArg: // %F
      case analyze_format_string::ConversionSpecifier::eArg: // %e
      case analyze_format_string::ConversionSpecifier::EArg: // %E
      case analyze_format_string::ConversionSpecifier::gArg: // %g
      case analyze_format_string::ConversionSpecifier::GArg: // %G
        Precision = 6;
        break;
      case analyze_format_string::ConversionSpecifier::pArg: // %d
        Precision = 1;
        break;
      }
      break;
    case analyze_format_string::OptionalAmount::Constant:
      Precision = FW.getConstantAmount();
      break;
    default:
      break;
    }
    return Precision;
  }
};


bool processFormatStringLiteral(const Expr *FormatExpr,
                                llvm::StringRef &FormatStrRef, size_t &StrLen,
                                TreeContext &Context) {
  if (const auto *Format = dyn_cast<StringLiteral>(FormatExpr);
      Format && (Format->isOrdinary() || Format->isUTF8())) {
    FormatStrRef = Format->getString();
    const ConstantArrayType *T =
        Context.getAsConstantArrayType(Format->getType());
    assert(T && "String literal not of constant array type!");
    size_t TypeSize = T->getSize().getZExtValue();
    // In case there's a null byte somewhere.
    StrLen = std::min(std::max(TypeSize, size_t(1)) - 1, FormatStrRef.find(0));
    return true;
  }
  return false;
}


// ===----------------------------------------------------------------------===
// Fortified memory function checking
// ===----------------------------------------------------------------------===

void Sema::checkFortifiedBuiltinMemoryFunction(FunctionDecl *FD,
                                               CallExpr *TheCall) {
  if (isConstantEvaluatedContext())
    return;

  bool UseDABAttr = false;
  const FunctionDecl *UseDecl = FD;

  const auto *DABAttr = FD->getAttr<DiagnoseAsBuiltinAttr>();
  if (DABAttr) {
    UseDecl = DABAttr->getFunction();
    assert(UseDecl && "Missing FunctionDecl in DiagnoseAsBuiltin attribute!");
    UseDABAttr = true;
  }

  unsigned BuiltinID = UseDecl->getBuiltinID(/*ConsiderWrappers=*/true);

  if (!BuiltinID)
    return;

  const TargetInfo &TI = getTreeContext().getTargetInfo();
  unsigned SizeTypeWidth = TI.getTypeWidth(TI.getSizeType());

  auto TranslateIndex = [&](unsigned Index) -> std::optional<unsigned> {
    // If we refer to a diagnose_as_builtin attribute, we need to change the
    // argument index to refer to the arguments of the called function. Unless
    // the index is out of bounds, which presumably means it's a variadic
    // function.
    if (!UseDABAttr)
      return Index;
    unsigned DABIndices = DABAttr->argIndices_size();
    unsigned NewIndex = Index < DABIndices
                            ? DABAttr->argIndices_begin()[Index]
                            : Index - DABIndices + FD->getNumParams();
    if (NewIndex >= TheCall->getNumArgs())
      return std::nullopt;
    return NewIndex;
  };

  auto ComputeExplicitObjectSizeArgument =
      [&](unsigned Index) -> std::optional<llvm::APSInt> {
    std::optional<unsigned> IndexOptional = TranslateIndex(Index);
    if (!IndexOptional)
      return std::nullopt;
    unsigned NewIndex = *IndexOptional;
    Expr::EvalResult Result;
    Expr *SizeArg = TheCall->getArg(NewIndex);
    if (!SizeArg->EvaluateAsInt(Result, getTreeContext()))
      return std::nullopt;
    llvm::APSInt Integer = Result.Val.getInt();
    Integer.setIsUnsigned(true);
    return Integer;
  };

  auto ComputeSizeArgument =
      [&](unsigned Index) -> std::optional<llvm::APSInt> {
    // If the parameter has a pass_object_size attribute, then we should use its
    // (potentially) more strict checking mode. Otherwise, conservatively assume
    // type 0.
    int BOSType = 0;
    // This check can fail for variadic functions.
    if (Index < FD->getNumParams()) {
      if (const auto *POS =
              FD->getParamDecl(Index)->getAttr<PassObjectSizeAttr>())
        BOSType = POS->getType();
    }

    std::optional<unsigned> IndexOptional = TranslateIndex(Index);
    if (!IndexOptional)
      return std::nullopt;
    unsigned NewIndex = *IndexOptional;

    if (NewIndex >= TheCall->getNumArgs())
      return std::nullopt;

    const Expr *ObjArg = TheCall->getArg(NewIndex);
    uint64_t Result;
    if (!ObjArg->tryEvaluateObjectSize(Result, getTreeContext(), BOSType))
      return std::nullopt;
    return llvm::APSInt::getUnsigned(Result).extOrTrunc(SizeTypeWidth);
  };

  auto ComputeStrLenArgument =
      [&](unsigned Index) -> std::optional<llvm::APSInt> {
    std::optional<unsigned> IndexOptional = TranslateIndex(Index);
    if (!IndexOptional)
      return std::nullopt;
    unsigned NewIndex = *IndexOptional;

    const Expr *ObjArg = TheCall->getArg(NewIndex);
    uint64_t Result;
    if (!ObjArg->tryEvaluateStrLen(Result, getTreeContext()))
      return std::nullopt;
    // Add 1 for null byte.
    return llvm::APSInt::getUnsigned(Result + 1).extOrTrunc(SizeTypeWidth);
  };

  std::optional<llvm::APSInt> SourceSize;
  std::optional<llvm::APSInt> DestinationSize;
  unsigned DiagID = 0;
  bool IsChkVariant = false;

  auto GetFunctionName = [&]() {
    llvm::StringRef FunctionName =
        getTreeContext().BuiltinInfo.getName(BuiltinID);
    // Skim off the details of whichever builtin was called to produce a better
    // diagnostic, as it's unlikely that the user wrote the __builtin
    // explicitly.
    if (IsChkVariant) {
      FunctionName = FunctionName.drop_front(std::strlen("__builtin___"));
      FunctionName = FunctionName.drop_back(std::strlen("_chk"));
    } else {
      FunctionName.consume_front("__builtin_");
    }
    return FunctionName;
  };

  switch (BuiltinID) {
  default:
    return;
  case Builtin::BI__builtin_strcpy:
  case Builtin::BIstrcpy: {
    DiagID = diag::warn_fortify_strlen_overflow;
    SourceSize = ComputeStrLenArgument(1);
    DestinationSize = ComputeSizeArgument(0);
    break;
  }

  case Builtin::BI__builtin___strcpy_chk: {
    DiagID = diag::warn_fortify_strlen_overflow;
    SourceSize = ComputeStrLenArgument(1);
    DestinationSize = ComputeExplicitObjectSizeArgument(2);
    IsChkVariant = true;
    break;
  }

  case Builtin::BIscanf:
  case Builtin::BIfscanf:
  case Builtin::BIsscanf: {
    unsigned FormatIndex = 1;
    unsigned DataIndex = 2;
    if (BuiltinID == Builtin::BIscanf) {
      FormatIndex = 0;
      DataIndex = 1;
    }

    const auto *FormatExpr =
        TheCall->getArg(FormatIndex)->IgnoreParenImpCasts();

    llvm::StringRef FormatStrRef;
    size_t StrLen;
    if (!processFormatStringLiteral(FormatExpr, FormatStrRef, StrLen, Context))
      return;

    auto Diagnose = [&](unsigned ArgIndex, unsigned DestSize,
                        unsigned SourceSize) {
      DiagID = diag::warn_fortify_scanf_overflow;
      unsigned Index = ArgIndex + DataIndex;
      llvm::StringRef FunctionName = GetFunctionName();
      DiagRuntimeBehavior(TheCall->getArg(Index)->getBeginLoc(), TheCall,
                          PDiag(DiagID) << FunctionName << (Index + 1)
                                        << DestSize << SourceSize);
    };

    auto ShiftedComputeSizeArgument = [&](unsigned Index) {
      return ComputeSizeArgument(Index + DataIndex);
    };
    ScanfDiagnosticFormatHandler H(ShiftedComputeSizeArgument, Diagnose);
    const char *FormatBytes = FormatStrRef.data();
    analyze_format_string::ParseScanfString(H, FormatBytes,
                                            FormatBytes + StrLen, getLangOpts(),
                                            Context.getTargetInfo());

    // Unlike the other cases, in this one we have already issued the diagnostic
    // here, so no need to continue (because unlike the other cases, here the
    // diagnostic refers to the argument number).
    return;
  }

  case Builtin::BIsprintf:
  case Builtin::BI__builtin___sprintf_chk: {
    size_t FormatIndex = BuiltinID == Builtin::BIsprintf ? 1 : 3;
    auto *FormatExpr = TheCall->getArg(FormatIndex)->IgnoreParenImpCasts();

    llvm::StringRef FormatStrRef;
    size_t StrLen;
    if (processFormatStringLiteral(FormatExpr, FormatStrRef, StrLen, Context)) {
      EstimateSizeFormatHandler H(FormatStrRef);
      const char *FormatBytes = FormatStrRef.data();
      if (!analyze_format_string::ParsePrintfString(
              H, FormatBytes, FormatBytes + StrLen, getLangOpts(),
              Context.getTargetInfo())) {
        DiagID = diag::warn_format_overflow;
        SourceSize = llvm::APSInt::getUnsigned(H.getSizeLowerBound())
                         .extOrTrunc(SizeTypeWidth);
        if (BuiltinID == Builtin::BI__builtin___sprintf_chk) {
          DestinationSize = ComputeExplicitObjectSizeArgument(2);
          IsChkVariant = true;
        } else {
          DestinationSize = ComputeSizeArgument(0);
        }
        break;
      }
    }
    return;
  }
  case Builtin::BI__builtin___memcpy_chk:
  case Builtin::BI__builtin___memmove_chk:
  case Builtin::BI__builtin___memset_chk:
  case Builtin::BI__builtin___strlcat_chk:
  case Builtin::BI__builtin___strlcpy_chk:
  case Builtin::BI__builtin___strncat_chk:
  case Builtin::BI__builtin___strncpy_chk:
  case Builtin::BI__builtin___stpncpy_chk:
  case Builtin::BI__builtin___memccpy_chk:
  case Builtin::BI__builtin___mempcpy_chk: {
    DiagID = diag::warn_builtin_chk_overflow;
    SourceSize = ComputeExplicitObjectSizeArgument(TheCall->getNumArgs() - 2);
    DestinationSize =
        ComputeExplicitObjectSizeArgument(TheCall->getNumArgs() - 1);
    IsChkVariant = true;
    break;
  }

  case Builtin::BI__builtin___snprintf_chk:
  case Builtin::BI__builtin___vsnprintf_chk: {
    DiagID = diag::warn_builtin_chk_overflow;
    SourceSize = ComputeExplicitObjectSizeArgument(1);
    DestinationSize = ComputeExplicitObjectSizeArgument(3);
    IsChkVariant = true;
    break;
  }

  case Builtin::BIstrncat:
  case Builtin::BI__builtin_strncat:
  case Builtin::BIstrncpy:
  case Builtin::BI__builtin_strncpy:
  case Builtin::BIstpncpy:
  case Builtin::BI__builtin_stpncpy: {
    // Whether these functions overflow depends on the runtime strlen of the
    // string, not just the buffer size, so emitting the "always overflow"
    // diagnostic isn't quite right. We should still diagnose passing a buffer
    // size larger than the destination buffer though; this is a runtime abort
    // in _FORTIFY_SOURCE mode, and is quite suspicious otherwise.
    DiagID = diag::warn_fortify_source_size_mismatch;
    SourceSize = ComputeExplicitObjectSizeArgument(TheCall->getNumArgs() - 1);
    DestinationSize = ComputeSizeArgument(0);
    break;
  }

  case Builtin::BImemcpy:
  case Builtin::BI__builtin_memcpy:
  case Builtin::BImemmove:
  case Builtin::BI__builtin_memmove:
  case Builtin::BImemset:
  case Builtin::BI__builtin_memset:
  case Builtin::BImempcpy:
  case Builtin::BI__builtin_mempcpy: {
    DiagID = diag::warn_fortify_source_overflow;
    SourceSize = ComputeExplicitObjectSizeArgument(TheCall->getNumArgs() - 1);
    DestinationSize = ComputeSizeArgument(0);
    break;
  }
  case Builtin::BIsnprintf:
  case Builtin::BI__builtin_snprintf:
  case Builtin::BIvsnprintf:
  case Builtin::BI__builtin_vsnprintf: {
    DiagID = diag::warn_fortify_source_size_mismatch;
    SourceSize = ComputeExplicitObjectSizeArgument(1);
    const auto *FormatExpr = TheCall->getArg(2)->IgnoreParenImpCasts();
    llvm::StringRef FormatStrRef;
    size_t StrLen;
    if (SourceSize &&
        processFormatStringLiteral(FormatExpr, FormatStrRef, StrLen, Context)) {
      EstimateSizeFormatHandler H(FormatStrRef);
      const char *FormatBytes = FormatStrRef.data();
      if (!analyze_format_string::ParsePrintfString(
              H, FormatBytes, FormatBytes + StrLen, getLangOpts(),
              Context.getTargetInfo())) {
        llvm::APSInt FormatSize =
            llvm::APSInt::getUnsigned(H.getSizeLowerBound())
                .extOrTrunc(SizeTypeWidth);
        if (FormatSize > *SourceSize && *SourceSize != 0) {
          unsigned TruncationDiagID = diag::warn_format_truncation;
          llvm::SmallString<16> SpecifiedSizeStr;
          llvm::SmallString<16> FormatSizeStr;
          SourceSize->toString(SpecifiedSizeStr, /*Radix=*/10);
          FormatSize.toString(FormatSizeStr, /*Radix=*/10);
          DiagRuntimeBehavior(TheCall->getBeginLoc(), TheCall,
                              PDiag(TruncationDiagID)
                                  << GetFunctionName() << SpecifiedSizeStr
                                  << FormatSizeStr);
        }
      }
    }
    DestinationSize = ComputeSizeArgument(0);
  }
  }

  if (!SourceSize || !DestinationSize ||
      llvm::APSInt::compareValues(*SourceSize, *DestinationSize) <= 0)
    return;

  llvm::StringRef FunctionName = GetFunctionName();

  llvm::SmallString<16> DestinationStr;
  llvm::SmallString<16> SourceStr;
  DestinationSize->toString(DestinationStr, /*Radix=*/10);
  SourceSize->toString(SourceStr, /*Radix=*/10);
  DiagRuntimeBehavior(TheCall->getBeginLoc(), TheCall,
                      PDiag(DiagID)
                          << FunctionName << DestinationStr << SourceStr);
}
