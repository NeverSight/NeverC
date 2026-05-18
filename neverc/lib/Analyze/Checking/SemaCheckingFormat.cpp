#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/SyncScope.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/AttrIterator.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Format/FormatString.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Locale.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <bitset>
#include <cassert>
#include <optional>
#include <string>
#include <utility>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Format string helpers
// ===----------------------------------------------------------------------===
// This translation unit hosts all *format string* checks: the printf/scanf
// dispatcher (checkFormatString), the checker base class (CheckFormatHandler),
// CheckPrintfHandler, CheckScanfHandler, the FormatStringLiteral helper, the
// uncovered-arg tracker, and Sema::GetFormatStringType / CheckFormatArguments.
// Original line range: 5989..8013.

namespace {
// Mirrored from SemaChecking.cpp; kept file-local in this TU.
constexpr unsigned short combineFAPK(Sema::FormatArgumentPassingKind A,
                                     Sema::FormatArgumentPassingKind B) {
  return (A << 8) | B;
}
} // namespace

namespace {

class UncoveredArgHandler {
  enum { Unknown = -1, AllCovered = -2 };

  signed FirstUncoveredArg = Unknown;
  llvm::SmallVector<const Expr *, 4> DiagnosticExprs;

public:
  UncoveredArgHandler() = default;

  bool hasUncoveredArg() const { return (FirstUncoveredArg >= 0); }

  unsigned getUncoveredArg() const {
    assert(hasUncoveredArg() && "no uncovered argument");
    return FirstUncoveredArg;
  }

  void setAllCovered() {
    // A string has been found with all arguments covered, so clear out
    // the diagnostics.
    DiagnosticExprs.clear();
    FirstUncoveredArg = AllCovered;
  }

  void Update(signed NewFirstUncoveredArg, const Expr *StrExpr) {
    assert(NewFirstUncoveredArg >= 0 && "Outside range");

    // Don't update if a previous string covers all arguments.
    if (FirstUncoveredArg == AllCovered)
      return;

    // UncoveredArgHandler tracks the highest uncovered argument index
    // and with it all the strings that match this index.
    if (NewFirstUncoveredArg == FirstUncoveredArg)
      DiagnosticExprs.push_back(StrExpr);
    else if (NewFirstUncoveredArg > FirstUncoveredArg) {
      DiagnosticExprs.clear();
      DiagnosticExprs.push_back(StrExpr);
      FirstUncoveredArg = NewFirstUncoveredArg;
    }
  }

  void Diagnose(Sema &S, bool IsFunctionCall, const Expr *ArgExpr);
};

enum StringLiteralCheckType {
  SLCT_NotALiteral,
  SLCT_UncheckedLiteral,
  SLCT_CheckedLiteral
};

} // namespace

namespace {
void sumOffsets(llvm::APSInt &Offset, llvm::APSInt Addend,
                BinaryOperatorKind BinOpKind, bool AddendIsRight) {
  unsigned BitWidth = Offset.getBitWidth();
  unsigned AddendBitWidth = Addend.getBitWidth();
  // There might be negative interim results.
  if (Addend.isUnsigned()) {
    Addend = Addend.zext(++AddendBitWidth);
    Addend.setIsSigned(true);
  }
  // Adjust the bit width of the APSInts.
  if (AddendBitWidth > BitWidth) {
    Offset = Offset.sext(AddendBitWidth);
    BitWidth = AddendBitWidth;
  } else if (BitWidth > AddendBitWidth) {
    Addend = Addend.sext(BitWidth);
  }

  bool Ov = false;
  llvm::APSInt ResOffset = Offset;
  if (BinOpKind == BO_Add)
    ResOffset = Offset.sadd_ov(Addend, Ov);
  else {
    assert(AddendIsRight && BinOpKind == BO_Sub &&
           "operator must be add or sub with addend on the right");
    ResOffset = Offset.ssub_ov(Addend, Ov);
  }

  // We add an offset to a pointer here so we should support an offset as big as
  // possible.
  if (Ov) {
    assert(BitWidth <= std::numeric_limits<unsigned>::max() / 2 &&
           "index (intermediate) result too big");
    Offset = Offset.sext(2 * BitWidth);
    sumOffsets(Offset, Addend, BinOpKind, AddendIsRight);
    return;
  }

  Offset = ResOffset;
}
} // namespace

namespace {

class FormatStringLiteral {
  const StringLiteral *FExpr;
  int64_t Offset;

public:
  FormatStringLiteral(const StringLiteral *fexpr, int64_t Offset = 0)
      : FExpr(fexpr), Offset(Offset) {}

  llvm::StringRef getString() const {
    return FExpr->getString().drop_front(Offset);
  }

  unsigned getByteLength() const {
    return FExpr->getByteLength() - getCharByteWidth() * Offset;
  }

  unsigned getLength() const { return FExpr->getLength() - Offset; }
  unsigned getCharByteWidth() const { return FExpr->getCharByteWidth(); }

  StringLiteralKind getKind() const { return FExpr->getKind(); }

  QualType getType() const { return FExpr->getType(); }

  bool isAscii() const { return FExpr->isOrdinary(); }
  bool isWide() const { return FExpr->isWide(); }
  bool isUTF8() const { return FExpr->isUTF8(); }
  bool isUTF16() const { return FExpr->isUTF16(); }
  bool isUTF32() const { return FExpr->isUTF32(); }
  bool isPascal() const { return FExpr->isPascal(); }

  SourceLocation
  getLocationOfByte(unsigned ByteNo, const SourceManager &SM,
                    const LangOptions &Features, const TargetInfo &Target,
                    unsigned *StartToken = nullptr,
                    unsigned *StartTokenByteOffset = nullptr) const {
    return FExpr->getLocationOfByte(ByteNo + Offset, SM, Features, Target,
                                    StartToken, StartTokenByteOffset);
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return FExpr->getBeginLoc().getLocWithOffset(Offset);
  }

  SourceLocation getEndLoc() const LLVM_READONLY { return FExpr->getEndLoc(); }
};

} // namespace

namespace {
void checkFormatString(Sema &S, const FormatStringLiteral *FExpr,
                       const Expr *OrigFormatExpr,
                       llvm::ArrayRef<const Expr *> Args,
                       Sema::FormatArgumentPassingKind APK, unsigned format_idx,
                       unsigned firstDataArg, Sema::FormatStringType Type,
                       bool inFunctionCall, Sema::VariadicCallType CallType,
                       llvm::SmallBitVector &CheckedVarArgs,
                       UncoveredArgHandler &UncoveredArg,
                       bool IgnoreStringsWithoutSpecifiers);

const Expr *maybeConstEvalStringLiteral(TreeContext &Context, const Expr *E);
} // namespace

// Determine if an expression is a string literal or constant string.
// If this function returns false on the arguments to a function expecting a
// format string, we will usually need to emit a warning.
// True string literals are then checked by checkFormatString.
namespace {
StringLiteralCheckType
checkFormatStringExpr(Sema &S, const Expr *E, llvm::ArrayRef<const Expr *> Args,
                      Sema::FormatArgumentPassingKind APK, unsigned format_idx,
                      unsigned firstDataArg, Sema::FormatStringType Type,
                      Sema::VariadicCallType CallType, bool InFunctionCall,
                      llvm::SmallBitVector &CheckedVarArgs,
                      UncoveredArgHandler &UncoveredArg, llvm::APSInt Offset,
                      bool IgnoreStringsWithoutSpecifiers = false) {
  if (S.isConstantEvaluatedContext())
    return SLCT_NotALiteral;
tryAgain:
  assert(Offset.isSigned() && "invalid offset");

  E = E->IgnoreParenCasts();

  if (E->isNullPointerConstant(S.Context, Expr::NPC_ValueDependentIsNotNull))
    // Technically -Wformat-nonliteral does not warn about this case.
    // The behavior of printf and friends in this case is implementation
    // dependent.  Ideally if the format string cannot be null then
    // it should have a 'nonnull' attribute in the function prototype.
    return SLCT_UncheckedLiteral;

  switch (E->getStmtClass()) {
  case Stmt::InitListExprClass:
    // Handle expressions like {"foobar"}.
    if (const neverc::Expr *SLE = maybeConstEvalStringLiteral(S.Context, E)) {
      return checkFormatStringExpr(S, SLE, Args, APK, format_idx, firstDataArg,
                                   Type, CallType, /*InFunctionCall*/ false,
                                   CheckedVarArgs, UncoveredArg, Offset,
                                   IgnoreStringsWithoutSpecifiers);
    }
    return SLCT_NotALiteral;
  case Stmt::BinaryConditionalOperatorClass:
  case Stmt::ConditionalOperatorClass: {
    // The expression is a literal if both sub-expressions were, and it was
    // completely checked only if both sub-expressions were checked.
    const AbstractConditionalOperator *C = cast<AbstractConditionalOperator>(E);

    // Determine whether it is necessary to check both sub-expressions, for
    // example, because the condition expression is a constant that can be
    // evaluated at compile time.
    bool CheckLeft = true, CheckRight = true;

    bool Cond;
    if (C->getCond()->EvaluateAsBooleanCondition(
            Cond, S.getTreeContext(), S.isConstantEvaluatedContext())) {
      if (Cond)
        CheckRight = false;
      else
        CheckLeft = false;
    }

    // We need to maintain the offsets for the right and the left hand side
    // separately to check if every possible indexed expression is a valid
    // string literal. They might have different offsets for different string
    // literals in the end.
    StringLiteralCheckType Left;
    if (!CheckLeft)
      Left = SLCT_UncheckedLiteral;
    else {
      Left = checkFormatStringExpr(S, C->getTrueExpr(), Args, APK, format_idx,
                                   firstDataArg, Type, CallType, InFunctionCall,
                                   CheckedVarArgs, UncoveredArg, Offset,
                                   IgnoreStringsWithoutSpecifiers);
      if (Left == SLCT_NotALiteral || !CheckRight) {
        return Left;
      }
    }

    StringLiteralCheckType Right = checkFormatStringExpr(
        S, C->getFalseExpr(), Args, APK, format_idx, firstDataArg, Type,
        CallType, InFunctionCall, CheckedVarArgs, UncoveredArg, Offset,
        IgnoreStringsWithoutSpecifiers);

    return (CheckLeft && Left < Right) ? Left : Right;
  }

  case Stmt::ImplicitCastExprClass:
    E = cast<ImplicitCastExpr>(E)->getSubExpr();
    goto tryAgain;

  case Stmt::OpaqueValueExprClass:
    if (const Expr *src = cast<OpaqueValueExpr>(E)->getSourceExpr()) {
      E = src;
      goto tryAgain;
    }
    return SLCT_NotALiteral;

  case Stmt::PredefinedExprClass:
    // While __func__, etc., are technically not string literals, they
    // cannot contain format specifiers and thus are not a security
    // liability.
    return SLCT_UncheckedLiteral;

  case Stmt::DeclRefExprClass: {
    const DeclRefExpr *DR = cast<DeclRefExpr>(E);

    // As an exception, do not flag errors for variables binding to
    // const string literals.
    if (const VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl())) {
      bool isConstant = false;
      QualType T = DR->getType();

      if (const ArrayType *AT = S.Context.getAsArrayType(T)) {
        isConstant = AT->getElementType().isConstant(S.Context);
      } else if (const PointerType *PT = T->getAs<PointerType>()) {
        isConstant = T.isConstant(S.Context) &&
                     PT->getPointeeType().isConstant(S.Context);
      }

      if (isConstant) {
        if (const Expr *Init = VD->getAnyInitializer()) {
          // Look through initializers like const char c[] = { "foo" }
          if (const InitListExpr *InitList = dyn_cast<InitListExpr>(Init)) {
            if (InitList->isStringLiteralInit())
              Init = InitList->getInit(0)->IgnoreParenImpCasts();
          }
          return checkFormatStringExpr(
              S, Init, Args, APK, format_idx, firstDataArg, Type, CallType,
              /*InFunctionCall*/ false, CheckedVarArgs, UncoveredArg, Offset);
        }
      }

      // When the format argument is an argument of this function, and this
      // function also has the format attribute, there are several interactions
      // for which there shouldn't be a warning. For instance, when calling
      // v*printf from a function that has the printf format attribute, we
      // should not emit a warning about using `fmt`, even though it's not
      // constant, because the arguments have already been checked for the
      // caller of `logmessage`:
      //
      //  __attribute__((format(printf, 1, 2)))
      //  void logmessage(char const *fmt, ...) {
      //    va_list ap;
      //    va_start(ap, fmt);
      //    vprintf(fmt, ap);  /* do not emit a warning about "fmt" */
      //    ...
      // }
      //
      // Another interaction that we need to support is calling a variadic
      // format function from a format function that has fixed arguments. For
      // instance:
      //
      //  __attribute__((format(printf, 1, 2)))
      //  void logstring(char const *fmt, char const *str) {
      //    printf(fmt, str);  /* do not emit a warning about "fmt" */
      //  }
      //
      // Due to implementation difficulty, we only check the format, not the
      // format arguments, in all cases.
      //
      if (const auto *PV = dyn_cast<ParmVarDecl>(VD)) {
        if (const auto *D = dyn_cast<Decl>(PV->getDeclContext())) {
          for (const auto *PVFormat : D->specific_attrs<FormatAttr>()) {
            bool IsVariadic = false;
            if (const FunctionType *FnTy = D->getFunctionType())
              IsVariadic = cast<FunctionProtoType>(FnTy)->isVariadic();
            Sema::FormatStringInfo CallerFSI;
            if (Sema::getFormatStringInfo(PVFormat, IsVariadic, &CallerFSI)) {
              // We also check if the formats are compatible.
              // We can't pass a 'scanf' string to a 'printf' function.
              if (PV->getFunctionScopeIndex() == CallerFSI.FormatIdx &&
                  Type == S.GetFormatStringType(PVFormat)) {
                // Lastly, check that argument passing kinds transition in a
                // way that makes sense:
                // from a caller with FAPK_VAList, allow FAPK_VAList
                // from a caller with FAPK_Fixed, allow FAPK_Fixed
                // from a caller with FAPK_Fixed, allow FAPK_Variadic
                // from a caller with FAPK_Variadic, allow FAPK_VAList
                switch (combineFAPK(CallerFSI.ArgPassingKind, APK)) {
                case combineFAPK(Sema::FAPK_VAList, Sema::FAPK_VAList):
                case combineFAPK(Sema::FAPK_Fixed, Sema::FAPK_Fixed):
                case combineFAPK(Sema::FAPK_Fixed, Sema::FAPK_Variadic):
                case combineFAPK(Sema::FAPK_Variadic, Sema::FAPK_VAList):
                  return SLCT_UncheckedLiteral;
                }
              }
            }
          }
        }
      }
    }

    return SLCT_NotALiteral;
  }

  case Stmt::CallExprClass: {
    const CallExpr *CE = cast<CallExpr>(E);
    if (const NamedDecl *ND =
            dyn_cast_or_null<NamedDecl>(CE->getCalleeDecl())) {
      bool IsFirst = true;
      StringLiteralCheckType CommonResult;
      for (const auto *FA : ND->specific_attrs<FormatArgAttr>()) {
        const Expr *Arg = CE->getArg(FA->getFormatIdx().getASTIndex());
        StringLiteralCheckType Result = checkFormatStringExpr(
            S, Arg, Args, APK, format_idx, firstDataArg, Type, CallType,
            InFunctionCall, CheckedVarArgs, UncoveredArg, Offset,
            IgnoreStringsWithoutSpecifiers);
        if (IsFirst) {
          CommonResult = Result;
          IsFirst = false;
        }
      }
      if (!IsFirst)
        return CommonResult;
    }
    if (const Expr *SLE = maybeConstEvalStringLiteral(S.Context, E))
      return checkFormatStringExpr(S, SLE, Args, APK, format_idx, firstDataArg,
                                   Type, CallType, /*InFunctionCall*/ false,
                                   CheckedVarArgs, UncoveredArg, Offset,
                                   IgnoreStringsWithoutSpecifiers);
    return SLCT_NotALiteral;
  }
  case Stmt::StringLiteralClass: {
    const StringLiteral *StrE = nullptr;

    StrE = cast<StringLiteral>(E);

    if (StrE) {
      if (Offset.isNegative() || Offset > StrE->getLength()) {
        return SLCT_NotALiteral;
      }
      FormatStringLiteral FStr(StrE, Offset.sextOrTrunc(64).getSExtValue());
      checkFormatString(S, &FStr, E, Args, APK, format_idx, firstDataArg, Type,
                        InFunctionCall, CallType, CheckedVarArgs, UncoveredArg,
                        IgnoreStringsWithoutSpecifiers);
      return SLCT_CheckedLiteral;
    }

    return SLCT_NotALiteral;
  }
  case Stmt::BinaryOperatorClass: {
    const BinaryOperator *BinOp = cast<BinaryOperator>(E);

    // A string literal + an int offset is still a string literal.
    if (BinOp->isAdditiveOp()) {
      Expr::EvalResult LResult, RResult;

      bool LIsInt = BinOp->getLHS()->EvaluateAsInt(
          LResult, S.Context, Expr::SE_NoSideEffects,
          S.isConstantEvaluatedContext());
      bool RIsInt = BinOp->getRHS()->EvaluateAsInt(
          RResult, S.Context, Expr::SE_NoSideEffects,
          S.isConstantEvaluatedContext());

      if (LIsInt != RIsInt) {
        BinaryOperatorKind BinOpKind = BinOp->getOpcode();

        if (LIsInt) {
          if (BinOpKind == BO_Add) {
            sumOffsets(Offset, LResult.Val.getInt(), BinOpKind, RIsInt);
            E = BinOp->getRHS();
            goto tryAgain;
          }
        } else {
          sumOffsets(Offset, RResult.Val.getInt(), BinOpKind, RIsInt);
          E = BinOp->getLHS();
          goto tryAgain;
        }
      }
    }

    return SLCT_NotALiteral;
  }
  case Stmt::UnaryOperatorClass: {
    const UnaryOperator *UnaOp = cast<UnaryOperator>(E);
    auto ASE = dyn_cast<ArraySubscriptExpr>(UnaOp->getSubExpr());
    if (UnaOp->getOpcode() == UO_AddrOf && ASE) {
      Expr::EvalResult IndexResult;
      if (ASE->getRHS()->EvaluateAsInt(IndexResult, S.Context,
                                       Expr::SE_NoSideEffects,
                                       S.isConstantEvaluatedContext())) {
        sumOffsets(Offset, IndexResult.Val.getInt(), BO_Add,
                   /*RHS is int*/ true);
        E = ASE->getBase();
        goto tryAgain;
      }
    }

    return SLCT_NotALiteral;
  }

  default:
    return SLCT_NotALiteral;
  }
}

// If this expression can be evaluated at compile-time,
// check if the result is a StringLiteral and return it
// otherwise return nullptr
const Expr *maybeConstEvalStringLiteral(TreeContext &Context, const Expr *E) {
  Expr::EvalResult Result;
  if (E->EvaluateAsRValue(Result, Context) && Result.Val.isLValue()) {
    const auto *LVE = Result.Val.getLValueBase().dyn_cast<const Expr *>();
    if (isa_and_nonnull<StringLiteral>(LVE))
      return LVE;
  }
  return nullptr;
}
} // namespace

Sema::FormatStringType Sema::GetFormatStringType(const FormatAttr *Format) {
  return llvm::StringSwitch<FormatStringType>(Format->getType()->getName())
      .Case("scanf", FST_Scanf)
      .Cases("printf", "printf0", FST_Printf)
      .Case("strftime", FST_Strftime)
      .Case("strfmon", FST_Strfmon)
      .Case("os_trace", FST_OSLog)
      .Case("os_log", FST_OSLog)
      .Default(FST_Unknown);
}

// ===----------------------------------------------------------------------===
// Format argument validation
// ===----------------------------------------------------------------------===

bool Sema::CheckFormatArguments(const FormatAttr *Format,
                                llvm::ArrayRef<const Expr *> Args,
                                VariadicCallType CallType, SourceLocation Loc,
                                SourceRange Range,
                                llvm::SmallBitVector &CheckedVarArgs) {
  FormatStringInfo FSI;
  if (getFormatStringInfo(Format, CallType != VariadicDoesNotApply, &FSI))
    return CheckFormatArguments(Args, FSI.ArgPassingKind, FSI.FormatIdx,
                                FSI.FirstDataArg, GetFormatStringType(Format),
                                CallType, Loc, Range, CheckedVarArgs);
  return false;
}

bool Sema::CheckFormatArguments(llvm::ArrayRef<const Expr *> Args,
                                Sema::FormatArgumentPassingKind APK,
                                unsigned format_idx, unsigned firstDataArg,
                                FormatStringType Type,
                                VariadicCallType CallType, SourceLocation Loc,
                                SourceRange Range,
                                llvm::SmallBitVector &CheckedVarArgs) {
  // CHECK: printf/scanf-like function is called with no format string.
  if (format_idx >= Args.size()) {
    Diag(Loc, diag::warn_missing_format_string) << Range;
    return false;
  }

  const Expr *OrigFormatExpr = Args[format_idx]->IgnoreParenCasts();

  // CHECK: format string is not a string literal.
  //
  // Dynamically generated format strings are difficult to
  // automatically vet at compile time.  Requiring that format strings
  // are string literals: (1) permits the checking of format strings by
  // the compiler and thereby (2) can practically remove the source of
  // many format string exploits.

  // Format string is a C string (e.g. "%d").
  UncoveredArgHandler UncoveredArg;
  StringLiteralCheckType CT = checkFormatStringExpr(
      *this, OrigFormatExpr, Args, APK, format_idx, firstDataArg, Type,
      CallType,
      /*IsFunctionCall*/ true, CheckedVarArgs, UncoveredArg,
      /*no string offset*/ llvm::APSInt(64, false) = 0);

  // Generate a diagnostic where an uncovered argument is detected.
  if (UncoveredArg.hasUncoveredArg()) {
    unsigned ArgIdx = UncoveredArg.getUncoveredArg() + firstDataArg;
    assert(ArgIdx < Args.size() && "ArgIdx outside bounds");
    UncoveredArg.Diagnose(*this, /*IsFunctionCall*/ true, Args[ArgIdx]);
  }

  if (CT != SLCT_NotALiteral)
    // Literal format string found, check done!
    return CT == SLCT_CheckedLiteral;

  // Strftime is particular as it always uses a single 'time' argument,
  // so it is safe to pass a non-literal string.
  if (Type == FST_Strftime)
    return false;

  SourceLocation FormatLoc = Args[format_idx]->getBeginLoc();

  // If there are no arguments specified, warn with -Wformat-security, otherwise
  // warn only with -Wformat-nonliteral.
  if (Args.size() == firstDataArg) {
    Diag(FormatLoc, diag::warn_format_nonliteral_noargs)
        << OrigFormatExpr->getSourceRange();
    if (Type == FST_Printf)
      Diag(FormatLoc, diag::note_format_security_fixit)
          << FixItHint::CreateInsertion(FormatLoc, "\"%s\", ");
  } else {
    Diag(FormatLoc, diag::warn_format_nonliteral)
        << OrigFormatExpr->getSourceRange();
  }
  return false;
}

namespace {

class CheckFormatHandler : public analyze_format_string::FormatStringHandler {
protected:
  Sema &S;
  const FormatStringLiteral *FExpr;
  const Expr *OrigFormatExpr;
  const Sema::FormatStringType FSType;
  const unsigned FirstDataArg;
  const unsigned NumDataArgs;
  const char *Beg; // Start of format string.
  const Sema::FormatArgumentPassingKind ArgPassingKind;
  llvm::ArrayRef<const Expr *> Args;
  unsigned FormatIdx;
  llvm::SmallBitVector CoveredArgs;
  bool usesPositionalArgs = false;
  bool atFirstArg = true;
  bool inFunctionCall;
  Sema::VariadicCallType CallType;
  llvm::SmallBitVector &CheckedVarArgs;
  UncoveredArgHandler &UncoveredArg;

public:
  CheckFormatHandler(Sema &s, const FormatStringLiteral *fexpr,
                     const Expr *origFormatExpr,
                     const Sema::FormatStringType type, unsigned firstDataArg,
                     unsigned numDataArgs, const char *beg,
                     Sema::FormatArgumentPassingKind APK,
                     llvm::ArrayRef<const Expr *> Args, unsigned formatIdx,
                     bool inFunctionCall, Sema::VariadicCallType callType,
                     llvm::SmallBitVector &CheckedVarArgs,
                     UncoveredArgHandler &UncoveredArg)
      : S(s), FExpr(fexpr), OrigFormatExpr(origFormatExpr), FSType(type),
        FirstDataArg(firstDataArg), NumDataArgs(numDataArgs), Beg(beg),
        ArgPassingKind(APK), Args(Args), FormatIdx(formatIdx),
        inFunctionCall(inFunctionCall), CallType(callType),
        CheckedVarArgs(CheckedVarArgs), UncoveredArg(UncoveredArg) {
    CoveredArgs.resize(numDataArgs);
    CoveredArgs.reset();
  }

  void DoneProcessing();

  void HandleIncompleteSpecifier(const char *startSpecifier,
                                 unsigned specifierLen) override;

  void HandleInvalidLengthModifier(
      const analyze_format_string::FormatSpecifier &FS,
      const analyze_format_string::ConversionSpecifier &CS,
      const char *startSpecifier, unsigned specifierLen, unsigned DiagID);

  void HandleNonStandardLengthModifier(
      const analyze_format_string::FormatSpecifier &FS,
      const char *startSpecifier, unsigned specifierLen);

  void HandleNonStandardConversionSpecifier(
      const analyze_format_string::ConversionSpecifier &CS,
      const char *startSpecifier, unsigned specifierLen);

  void HandlePosition(const char *startPos, unsigned posLen) override;

  void HandleInvalidPosition(const char *startSpecifier, unsigned specifierLen,
                             analyze_format_string::PositionContext p) override;

  void HandleZeroPosition(const char *startPos, unsigned posLen) override;

  void HandleNullChar(const char *nullCharacter) override;

  template <typename Range>
  static void
  GenFormatDiagnostic(Sema &S, bool inFunctionCall, const Expr *ArgumentExpr,
                      const PartialDiagnostic &PDiag, SourceLocation StringLoc,
                      bool IsStringLocation, Range StringRange,
                      llvm::ArrayRef<FixItHint> Fixit = std::nullopt);

protected:
  bool HandleInvalidConversionSpecifier(unsigned argIndex, SourceLocation Loc,
                                        const char *startSpec,
                                        unsigned specifierLen,
                                        const char *csStart, unsigned csLen);

  void HandlePositionalNonpositionalArgs(SourceLocation Loc,
                                         const char *startSpec,
                                         unsigned specifierLen);

  SourceRange getFormatStringRange();
  CharSourceRange getSpecifierRange(const char *startSpecifier,
                                    unsigned specifierLen);
  SourceLocation getLocationOfByte(const char *x);

  const Expr *getDataArg(unsigned i) const;

  bool CheckNumArgs(const analyze_format_string::FormatSpecifier &FS,
                    const analyze_format_string::ConversionSpecifier &CS,
                    const char *startSpecifier, unsigned specifierLen,
                    unsigned argIndex);

  template <typename Range>
  void GenFormatDiagnostic(PartialDiagnostic PDiag, SourceLocation StringLoc,
                           bool IsStringLocation, Range StringRange,
                           llvm::ArrayRef<FixItHint> Fixit = std::nullopt);
};

} // namespace

SourceRange CheckFormatHandler::getFormatStringRange() {
  return OrigFormatExpr->getSourceRange();
}

CharSourceRange
CheckFormatHandler::getSpecifierRange(const char *startSpecifier,
                                      unsigned specifierLen) {
  SourceLocation Start = getLocationOfByte(startSpecifier);
  SourceLocation End = getLocationOfByte(startSpecifier + specifierLen - 1);

  // Advance the end SourceLocation by one due to half-open ranges.
  End = End.getLocWithOffset(1);

  return CharSourceRange::getCharRange(Start, End);
}

SourceLocation CheckFormatHandler::getLocationOfByte(const char *x) {
  return FExpr->getLocationOfByte(x - Beg, S.getSourceManager(),
                                  S.getLangOpts(), S.Context.getTargetInfo());
}

// ===----------------------------------------------------------------------===
// CheckFormatHandler base methods
// ===----------------------------------------------------------------------===

void CheckFormatHandler::HandleIncompleteSpecifier(const char *startSpecifier,
                                                   unsigned specifierLen) {
  GenFormatDiagnostic(S.PDiag(diag::warn_printf_incomplete_specifier),
                      getLocationOfByte(startSpecifier),
                      /*IsStringLocation*/ true,
                      getSpecifierRange(startSpecifier, specifierLen));
}

void CheckFormatHandler::HandleInvalidLengthModifier(
    const analyze_format_string::FormatSpecifier &FS,
    const analyze_format_string::ConversionSpecifier &CS,
    const char *startSpecifier, unsigned specifierLen, unsigned DiagID) {
  using namespace analyze_format_string;

  const LengthModifier &LM = FS.getLengthModifier();
  CharSourceRange LMRange = getSpecifierRange(LM.getStart(), LM.getLength());

  // See if we know how to fix this length modifier.
  std::optional<LengthModifier> FixedLM = FS.getCorrectedLengthModifier();
  if (FixedLM) {
    GenFormatDiagnostic(S.PDiag(DiagID) << LM.toString() << CS.toString(),
                        getLocationOfByte(LM.getStart()),
                        /*IsStringLocation*/ true,
                        getSpecifierRange(startSpecifier, specifierLen));

    S.Diag(getLocationOfByte(LM.getStart()), diag::note_format_fix_specifier)
        << FixedLM->toString()
        << FixItHint::CreateReplacement(LMRange, FixedLM->toString());

  } else {
    FixItHint Hint;
    if (DiagID == diag::warn_format_nonsensical_length)
      Hint = FixItHint::CreateRemoval(LMRange);

    GenFormatDiagnostic(S.PDiag(DiagID) << LM.toString() << CS.toString(),
                        getLocationOfByte(LM.getStart()),
                        /*IsStringLocation*/ true,
                        getSpecifierRange(startSpecifier, specifierLen), Hint);
  }
}

void CheckFormatHandler::HandleNonStandardLengthModifier(
    const analyze_format_string::FormatSpecifier &FS,
    const char *startSpecifier, unsigned specifierLen) {
  using namespace analyze_format_string;

  const LengthModifier &LM = FS.getLengthModifier();
  CharSourceRange LMRange = getSpecifierRange(LM.getStart(), LM.getLength());

  // See if we know how to fix this length modifier.
  std::optional<LengthModifier> FixedLM = FS.getCorrectedLengthModifier();
  if (FixedLM) {
    GenFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                            << LM.toString() << 0,
                        getLocationOfByte(LM.getStart()),
                        /*IsStringLocation*/ true,
                        getSpecifierRange(startSpecifier, specifierLen));

    S.Diag(getLocationOfByte(LM.getStart()), diag::note_format_fix_specifier)
        << FixedLM->toString()
        << FixItHint::CreateReplacement(LMRange, FixedLM->toString());

  } else {
    GenFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                            << LM.toString() << 0,
                        getLocationOfByte(LM.getStart()),
                        /*IsStringLocation*/ true,
                        getSpecifierRange(startSpecifier, specifierLen));
  }
}

void CheckFormatHandler::HandleNonStandardConversionSpecifier(
    const analyze_format_string::ConversionSpecifier &CS,
    const char *startSpecifier, unsigned specifierLen) {
  using namespace analyze_format_string;

  // See if we know how to fix this conversion specifier.
  std::optional<ConversionSpecifier> FixedCS = CS.getStandardSpecifier();
  if (FixedCS) {
    GenFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                            << CS.toString() << /*conversion specifier*/ 1,
                        getLocationOfByte(CS.getStart()),
                        /*IsStringLocation*/ true,
                        getSpecifierRange(startSpecifier, specifierLen));

    CharSourceRange CSRange = getSpecifierRange(CS.getStart(), CS.getLength());
    S.Diag(getLocationOfByte(CS.getStart()), diag::note_format_fix_specifier)
        << FixedCS->toString()
        << FixItHint::CreateReplacement(CSRange, FixedCS->toString());
  } else {
    GenFormatDiagnostic(S.PDiag(diag::warn_format_non_standard)
                            << CS.toString() << /*conversion specifier*/ 1,
                        getLocationOfByte(CS.getStart()),
                        /*IsStringLocation*/ true,
                        getSpecifierRange(startSpecifier, specifierLen));
  }
}

void CheckFormatHandler::HandlePosition(const char *startPos, unsigned posLen) {
  GenFormatDiagnostic(S.PDiag(diag::warn_format_non_standard_positional_arg),
                      getLocationOfByte(startPos),
                      /*IsStringLocation*/ true,
                      getSpecifierRange(startPos, posLen));
}

void CheckFormatHandler::HandleInvalidPosition(
    const char *startSpecifier, unsigned specifierLen,
    analyze_format_string::PositionContext p) {
  GenFormatDiagnostic(
      S.PDiag(diag::warn_format_invalid_positional_specifier) << (unsigned)p,
      getLocationOfByte(startSpecifier), /*IsStringLocation*/ true,
      getSpecifierRange(startSpecifier, specifierLen));
}

void CheckFormatHandler::HandleZeroPosition(const char *startPos,
                                            unsigned posLen) {
  GenFormatDiagnostic(S.PDiag(diag::warn_format_zero_positional_specifier),
                      getLocationOfByte(startPos),
                      /*IsStringLocation*/ true,
                      getSpecifierRange(startPos, posLen));
}

void CheckFormatHandler::HandleNullChar(const char *nullCharacter) {
  // The presence of a null character is likely an error.
  GenFormatDiagnostic(
      S.PDiag(diag::warn_printf_format_string_contains_null_char),
      getLocationOfByte(nullCharacter),
      /*IsStringLocation*/ true, getFormatStringRange());
}

// Note that this may return NULL if there was an error parsing or building
// one of the argument expressions.
const Expr *CheckFormatHandler::getDataArg(unsigned i) const {
  return Args[FirstDataArg + i];
}

void CheckFormatHandler::DoneProcessing() {
  // Does the number of data arguments exceed the number of
  // format conversions in the format string?
  if (ArgPassingKind != Sema::FAPK_VAList) {
    // Find any arguments that weren't covered.
    CoveredArgs.flip();
    signed notCoveredArg = CoveredArgs.find_first();
    if (notCoveredArg >= 0) {
      assert((unsigned)notCoveredArg < NumDataArgs);
      UncoveredArg.Update(notCoveredArg, OrigFormatExpr);
    } else {
      UncoveredArg.setAllCovered();
    }
  }
}

void UncoveredArgHandler::Diagnose(Sema &S, bool IsFunctionCall,
                                   const Expr *ArgExpr) {
  assert(hasUncoveredArg() && !DiagnosticExprs.empty() && "Invalid state");

  if (!ArgExpr)
    return;

  SourceLocation Loc = ArgExpr->getBeginLoc();

  if (S.getSourceManager().isInSystemMacro(Loc))
    return;

  PartialDiagnostic PDiag = S.PDiag(diag::warn_printf_data_arg_not_used);
  for (auto E : DiagnosticExprs)
    PDiag << E->getSourceRange();

  CheckFormatHandler::GenFormatDiagnostic(
      S, IsFunctionCall, DiagnosticExprs[0], PDiag, Loc,
      /*IsStringLocation*/ false, DiagnosticExprs[0]->getSourceRange());
}

bool CheckFormatHandler::HandleInvalidConversionSpecifier(
    unsigned argIndex, SourceLocation Loc, const char *startSpec,
    unsigned specifierLen, const char *csStart, unsigned csLen) {
  bool keepGoing = true;
  if (argIndex < NumDataArgs) {
    // Consider the argument coverered, even though the specifier doesn't
    // make sense.
    CoveredArgs.set(argIndex);
  } else {
    // If argIndex exceeds the number of data arguments we
    // don't issue a warning because that is just a cascade of warnings (and
    // they may have intended '%%' anyway). We don't want to continue processing
    // the format string after this point, however, as we will like just get
    // gibberish when trying to match arguments.
    keepGoing = false;
  }

  llvm::StringRef Specifier(csStart, csLen);

  // If the specifier in non-printable, it could be the first byte of a UTF-8
  // sequence. In that case, print the UTF-8 code point. If not, print the byte
  // hex value.
  std::string CodePointStr;
  if (!llvm::sys::locale::isPrint(*csStart)) {
    llvm::UTF32 CodePoint;
    const llvm::UTF8 **B = reinterpret_cast<const llvm::UTF8 **>(&csStart);
    const llvm::UTF8 *E = reinterpret_cast<const llvm::UTF8 *>(csStart + csLen);
    llvm::ConversionResult Result =
        llvm::convertUTF8Sequence(B, E, &CodePoint, llvm::strictConversion);

    if (Result != llvm::conversionOK) {
      unsigned char FirstChar = *csStart;
      CodePoint = (llvm::UTF32)FirstChar;
    }

    llvm::raw_string_ostream OS(CodePointStr);
    if (CodePoint < 256)
      OS << "\\x" << llvm::format("%02x", CodePoint);
    else if (CodePoint <= 0xFFFF)
      OS << "\\u" << llvm::format("%04x", CodePoint);
    else
      OS << "\\U" << llvm::format("%08x", CodePoint);
    OS.flush();
    Specifier = CodePointStr;
  }

  GenFormatDiagnostic(
      S.PDiag(diag::warn_format_invalid_conversion) << Specifier, Loc,
      /*IsStringLocation*/ true, getSpecifierRange(startSpec, specifierLen));

  return keepGoing;
}

void CheckFormatHandler::HandlePositionalNonpositionalArgs(
    SourceLocation Loc, const char *startSpec, unsigned specifierLen) {
  GenFormatDiagnostic(
      S.PDiag(diag::warn_format_mix_positional_nonpositional_args), Loc,
      /*isStringLoc*/ true, getSpecifierRange(startSpec, specifierLen));
}

bool CheckFormatHandler::CheckNumArgs(
    const analyze_format_string::FormatSpecifier &FS,
    const analyze_format_string::ConversionSpecifier &CS,
    const char *startSpecifier, unsigned specifierLen, unsigned argIndex) {

  if (argIndex >= NumDataArgs) {
    PartialDiagnostic PDiag =
        FS.usesPositionalArg()
            ? (S.PDiag(diag::warn_printf_positional_arg_exceeds_data_args)
               << (argIndex + 1) << NumDataArgs)
            : S.PDiag(diag::warn_printf_insufficient_data_args);
    GenFormatDiagnostic(PDiag, getLocationOfByte(CS.getStart()),
                        /*IsStringLocation*/ true,
                        getSpecifierRange(startSpecifier, specifierLen));

    // Since more arguments than conversion tokens are given, by extension
    // all arguments are covered, so mark this as so.
    UncoveredArg.setAllCovered();
    return false;
  }
  return true;
}

template <typename Range>
void CheckFormatHandler::GenFormatDiagnostic(PartialDiagnostic PDiag,
                                             SourceLocation Loc,
                                             bool IsStringLocation,
                                             Range StringRange,
                                             llvm::ArrayRef<FixItHint> FixIt) {
  GenFormatDiagnostic(S, inFunctionCall, Args[FormatIdx], PDiag, Loc,
                      IsStringLocation, StringRange, FixIt);
}

template <typename Range>
void CheckFormatHandler::GenFormatDiagnostic(
    Sema &S, bool InFunctionCall, const Expr *ArgumentExpr,
    const PartialDiagnostic &PDiag, SourceLocation Loc, bool IsStringLocation,
    Range StringRange, llvm::ArrayRef<FixItHint> FixIt) {
  if (InFunctionCall) {
    const Sema::SemaDiagnosticBuilder &D = S.Diag(Loc, PDiag);
    D << StringRange;
    D << FixIt;
  } else {
    S.Diag(IsStringLocation ? ArgumentExpr->getExprLoc() : Loc, PDiag)
        << ArgumentExpr->getSourceRange();

    const Sema::SemaDiagnosticBuilder &Note =
        S.Diag(IsStringLocation ? Loc : StringRange.getBegin(),
               diag::note_format_string_defined);

    Note << StringRange;
    Note << FixIt;
  }
}

//===--- CHECK: Printf format string checking
//------------------------------===//

namespace {

class CheckPrintfHandler : public CheckFormatHandler {
public:
  CheckPrintfHandler(Sema &s, const FormatStringLiteral *fexpr,
                     const Expr *origFormatExpr,
                     const Sema::FormatStringType type, unsigned firstDataArg,
                     unsigned numDataArgs, bool, const char *beg,
                     Sema::FormatArgumentPassingKind APK,
                     llvm::ArrayRef<const Expr *> Args, unsigned formatIdx,
                     bool inFunctionCall, Sema::VariadicCallType CallType,
                     llvm::SmallBitVector &CheckedVarArgs,
                     UncoveredArgHandler &UncoveredArg)
      : CheckFormatHandler(s, fexpr, origFormatExpr, type, firstDataArg,
                           numDataArgs, beg, APK, Args, formatIdx,
                           inFunctionCall, CallType, CheckedVarArgs,
                           UncoveredArg) {}

  bool HandleInvalidPrintfConversionSpecifier(
      const analyze_printf::PrintfSpecifier &FS, const char *startSpecifier,
      unsigned specifierLen) override;

  void handleInvalidMaskType(llvm::StringRef MaskType) override;

  bool HandlePrintfSpecifier(const analyze_printf::PrintfSpecifier &FS,
                             const char *startSpecifier, unsigned specifierLen,
                             const TargetInfo &Target) override;
  bool checkFormatExpr(const analyze_printf::PrintfSpecifier &FS,
                       const char *StartSpecifier, unsigned SpecifierLen,
                       const Expr *E);

  bool HandleAmount(const analyze_format_string::OptionalAmount &Amt,
                    unsigned k, const char *startSpecifier,
                    unsigned specifierLen);
  void HandleInvalidAmount(const analyze_printf::PrintfSpecifier &FS,
                           const analyze_printf::OptionalAmount &Amt,
                           unsigned type, const char *startSpecifier,
                           unsigned specifierLen);
  void HandleFlag(const analyze_printf::PrintfSpecifier &FS,
                  const analyze_printf::OptionalFlag &flag,
                  const char *startSpecifier, unsigned specifierLen);
  void HandleIgnoredFlag(const analyze_printf::PrintfSpecifier &FS,
                         const analyze_printf::OptionalFlag &ignoredFlag,
                         const analyze_printf::OptionalFlag &flag,
                         const char *startSpecifier, unsigned specifierLen);
};

} // namespace

// ===----------------------------------------------------------------------===
// Printf format checking
// ===----------------------------------------------------------------------===

bool CheckPrintfHandler::HandleInvalidPrintfConversionSpecifier(
    const analyze_printf::PrintfSpecifier &FS, const char *startSpecifier,
    unsigned specifierLen) {
  const analyze_printf::PrintfConversionSpecifier &CS =
      FS.getConversionSpecifier();

  return HandleInvalidConversionSpecifier(
      FS.getArgIndex(), getLocationOfByte(CS.getStart()), startSpecifier,
      specifierLen, CS.getStart(), CS.getLength());
}

void CheckPrintfHandler::handleInvalidMaskType(llvm::StringRef MaskType) {
  S.Diag(getLocationOfByte(MaskType.data()), diag::err_invalid_mask_type_size);
}

bool CheckPrintfHandler::HandleAmount(
    const analyze_format_string::OptionalAmount &Amt, unsigned k,
    const char *startSpecifier, unsigned specifierLen) {
  if (Amt.hasDataArgument()) {
    if (ArgPassingKind != Sema::FAPK_VAList) {
      unsigned argIndex = Amt.getArgIndex();
      if (argIndex >= NumDataArgs) {
        GenFormatDiagnostic(S.PDiag(diag::warn_printf_asterisk_missing_arg)
                                << k,
                            getLocationOfByte(Amt.getStart()),
                            /*IsStringLocation*/ true,
                            getSpecifierRange(startSpecifier, specifierLen));
        // Don't do any more checking.  We will just emit
        // spurious errors.
        return false;
      }

      // Type check the data argument.  It should be an 'int'.
      // Although not in conformance with C99, we also allow the argument to be
      // an 'unsigned int' as that is a reasonably safe case.  GCC also
      // doesn't emit a warning for that case.
      CoveredArgs.set(argIndex);
      const Expr *Arg = getDataArg(argIndex);
      if (!Arg)
        return false;

      QualType T = Arg->getType();

      const analyze_printf::ArgType &AT = Amt.getArgType(S.Context);
      assert(AT.isValid());

      if (!AT.matchesType(S.Context, T)) {
        GenFormatDiagnostic(S.PDiag(diag::warn_printf_asterisk_wrong_type)
                                << k << AT.getRepresentativeTypeName(S.Context)
                                << T << Arg->getSourceRange(),
                            getLocationOfByte(Amt.getStart()),
                            /*IsStringLocation*/ true,
                            getSpecifierRange(startSpecifier, specifierLen));
        // Don't do any more checking.  We will just emit
        // spurious errors.
        return false;
      }
    }
  }
  return true;
}

void CheckPrintfHandler::HandleInvalidAmount(
    const analyze_printf::PrintfSpecifier &FS,
    const analyze_printf::OptionalAmount &Amt, unsigned type,
    const char *startSpecifier, unsigned specifierLen) {
  const analyze_printf::PrintfConversionSpecifier &CS =
      FS.getConversionSpecifier();

  FixItHint fixit =
      Amt.getHowSpecified() == analyze_printf::OptionalAmount::Constant
          ? FixItHint::CreateRemoval(
                getSpecifierRange(Amt.getStart(), Amt.getConstantLength()))
          : FixItHint();

  GenFormatDiagnostic(S.PDiag(diag::warn_printf_nonsensical_optional_amount)
                          << type << CS.toString(),
                      getLocationOfByte(Amt.getStart()),
                      /*IsStringLocation*/ true,
                      getSpecifierRange(startSpecifier, specifierLen), fixit);
}

void CheckPrintfHandler::HandleFlag(const analyze_printf::PrintfSpecifier &FS,
                                    const analyze_printf::OptionalFlag &flag,
                                    const char *startSpecifier,
                                    unsigned specifierLen) {
  // Warn about pointless flag with a fixit removal.
  const analyze_printf::PrintfConversionSpecifier &CS =
      FS.getConversionSpecifier();
  GenFormatDiagnostic(
      S.PDiag(diag::warn_printf_nonsensical_flag)
          << flag.toString() << CS.toString(),
      getLocationOfByte(flag.getPosition()),
      /*IsStringLocation*/ true,
      getSpecifierRange(startSpecifier, specifierLen),
      FixItHint::CreateRemoval(getSpecifierRange(flag.getPosition(), 1)));
}

void CheckPrintfHandler::HandleIgnoredFlag(
    const analyze_printf::PrintfSpecifier &FS,
    const analyze_printf::OptionalFlag &ignoredFlag,
    const analyze_printf::OptionalFlag &flag, const char *startSpecifier,
    unsigned specifierLen) {
  // Warn about ignored flag with a fixit removal.
  GenFormatDiagnostic(S.PDiag(diag::warn_printf_ignored_flag)
                          << ignoredFlag.toString() << flag.toString(),
                      getLocationOfByte(ignoredFlag.getPosition()),
                      /*IsStringLocation*/ true,
                      getSpecifierRange(startSpecifier, specifierLen),
                      FixItHint::CreateRemoval(
                          getSpecifierRange(ignoredFlag.getPosition(), 1)));
}

bool CheckPrintfHandler::HandlePrintfSpecifier(
    const analyze_printf::PrintfSpecifier &FS, const char *startSpecifier,
    unsigned specifierLen, const TargetInfo &Target) {
  using namespace analyze_format_string;
  using namespace analyze_printf;

  const PrintfConversionSpecifier &CS = FS.getConversionSpecifier();

  if (FS.consumesDataArgument()) {
    if (atFirstArg) {
      atFirstArg = false;
      usesPositionalArgs = FS.usesPositionalArg();
    } else if (usesPositionalArgs != FS.usesPositionalArg()) {
      HandlePositionalNonpositionalArgs(getLocationOfByte(CS.getStart()),
                                        startSpecifier, specifierLen);
      return false;
    }
  }

  // First check if the field width, precision, and conversion specifier
  // have matching data arguments.
  if (!HandleAmount(FS.getFieldWidth(), /* field width */ 0, startSpecifier,
                    specifierLen)) {
    return false;
  }

  if (!HandleAmount(FS.getPrecision(), /* precision */ 1, startSpecifier,
                    specifierLen)) {
    return false;
  }

  if (!CS.consumesDataArgument()) {

    return true;
  }

  // Consume the argument.
  unsigned argIndex = FS.getArgIndex();
  if (argIndex < NumDataArgs) {
    // The check to see if the argIndex is valid will come later.
    // We set the bit here because we may exit early from this
    // function if we encounter some other error.
    CoveredArgs.set(argIndex);
  }

  // %P can only be used with os_log.
  if (FSType != Sema::FST_OSLog && CS.getKind() == ConversionSpecifier::PArg) {
    return HandleInvalidPrintfConversionSpecifier(FS, startSpecifier,
                                                  specifierLen);
  }

  // %n is not allowed with os_log.
  if (FSType == Sema::FST_OSLog && CS.getKind() == ConversionSpecifier::nArg) {
    GenFormatDiagnostic(S.PDiag(diag::warn_os_log_format_narg),
                        getLocationOfByte(CS.getStart()),
                        /*IsStringLocation*/ false,
                        getSpecifierRange(startSpecifier, specifierLen));

    return true;
  }

  // Only scalars are allowed for os_trace.
  if (FSType == Sema::FST_OSTrace &&
      (CS.getKind() == ConversionSpecifier::PArg ||
       CS.getKind() == ConversionSpecifier::sArg)) {
    return HandleInvalidPrintfConversionSpecifier(FS, startSpecifier,
                                                  specifierLen);
  }

  // Check for use of public/private annotation outside of os_log().
  if (FSType != Sema::FST_OSLog) {
    if (FS.isPublic().isSet()) {
      GenFormatDiagnostic(S.PDiag(diag::warn_format_invalid_annotation)
                              << "public",
                          getLocationOfByte(FS.isPublic().getPosition()),
                          /*IsStringLocation*/ false,
                          getSpecifierRange(startSpecifier, specifierLen));
    }
    if (FS.isPrivate().isSet()) {
      GenFormatDiagnostic(S.PDiag(diag::warn_format_invalid_annotation)
                              << "private",
                          getLocationOfByte(FS.isPrivate().getPosition()),
                          /*IsStringLocation*/ false,
                          getSpecifierRange(startSpecifier, specifierLen));
    }
  }

  if (!FS.hasValidFieldWidth()) {
    HandleInvalidAmount(FS, FS.getFieldWidth(), /* field width */ 0,
                        startSpecifier, specifierLen);
  }

  if (!FS.hasValidPrecision()) {
    HandleInvalidAmount(FS, FS.getPrecision(), /* precision */ 1,
                        startSpecifier, specifierLen);
  }

  // Precision is mandatory for %P specifier.
  if (CS.getKind() == ConversionSpecifier::PArg &&
      FS.getPrecision().getHowSpecified() == OptionalAmount::NotSpecified) {
    GenFormatDiagnostic(S.PDiag(diag::warn_format_P_no_precision),
                        getLocationOfByte(startSpecifier),
                        /*IsStringLocation*/ false,
                        getSpecifierRange(startSpecifier, specifierLen));
  }

  if (!FS.hasValidThousandsGroupingPrefix())
    HandleFlag(FS, FS.hasThousandsGrouping(), startSpecifier, specifierLen);
  if (!FS.hasValidLeadingZeros())
    HandleFlag(FS, FS.hasLeadingZeros(), startSpecifier, specifierLen);
  if (!FS.hasValidPlusPrefix())
    HandleFlag(FS, FS.hasPlusPrefix(), startSpecifier, specifierLen);
  if (!FS.hasValidSpacePrefix())
    HandleFlag(FS, FS.hasSpacePrefix(), startSpecifier, specifierLen);
  if (!FS.hasValidAlternativeForm())
    HandleFlag(FS, FS.hasAlternativeForm(), startSpecifier, specifierLen);
  if (!FS.hasValidLeftJustified())
    HandleFlag(FS, FS.isLeftJustified(), startSpecifier, specifierLen);

  if (FS.hasSpacePrefix() && FS.hasPlusPrefix()) // ' ' ignored by '+'
    HandleIgnoredFlag(FS, FS.hasSpacePrefix(), FS.hasPlusPrefix(),
                      startSpecifier, specifierLen);
  if (FS.hasLeadingZeros() && FS.isLeftJustified()) // '0' ignored by '-'
    HandleIgnoredFlag(FS, FS.hasLeadingZeros(), FS.isLeftJustified(),
                      startSpecifier, specifierLen);

  if (!FS.hasValidLengthModifier(S.getTreeContext().getTargetInfo(),
                                 S.getLangOpts()))
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_nonsensical_length);
  else if (!FS.hasStandardLengthModifier())
    HandleNonStandardLengthModifier(FS, startSpecifier, specifierLen);
  else if (!FS.hasStandardLengthConversionCombination())
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_non_standard_conversion_spec);

  if (!FS.hasStandardConversionSpecifier(S.getLangOpts()))
    HandleNonStandardConversionSpecifier(CS, startSpecifier, specifierLen);

  // The remaining checks depend on the data arguments.
  if (ArgPassingKind == Sema::FAPK_VAList)
    return true;

  if (!CheckNumArgs(FS, CS, startSpecifier, specifierLen, argIndex))
    return false;

  const Expr *Arg = getDataArg(argIndex);
  if (!Arg)
    return true;

  return checkFormatExpr(FS, startSpecifier, specifierLen, Arg);
}

namespace {
bool requiresParensToAddCast(const Expr *E) {
  // Take care of a few common cases where they aren't.
  const Expr *Inside = E->IgnoreImpCasts();
  if (const PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(Inside))
    Inside = POE->getSyntacticForm()->IgnoreImpCasts();

  switch (Inside->getStmtClass()) {
  case Stmt::ArraySubscriptExprClass:
  case Stmt::CallExprClass:
  case Stmt::CharacterLiteralClass:
  case Stmt::DeclRefExprClass:
  case Stmt::FloatingLiteralClass:
  case Stmt::IntegerLiteralClass:
  case Stmt::MemberExprClass:
  case Stmt::ParenExprClass:
  case Stmt::StringLiteralClass:
  case Stmt::UnaryOperatorClass:
    return false;
  default:
    return true;
  }
}

std::pair<QualType, llvm::StringRef>
shouldNotPrintDirectly(const TreeContext &Context, QualType IntendedTy,
                       const Expr *E) {
  // Use a 'while' to peel off layers of typedefs.
  QualType TyTy = IntendedTy;
  while (const TypedefType *UserTy = TyTy->getAs<TypedefType>()) {
    llvm::StringRef Name = UserTy->getDecl()->getName();
    QualType CastTy = llvm::StringSwitch<QualType>(Name)
                          .Case("CFIndex", Context.LongTy)
                          .Case("SInt32", Context.IntTy)
                          .Case("UInt32", Context.UnsignedIntTy)
                          .Default(QualType());

    if (!CastTy.isNull())
      return std::make_pair(CastTy, Name);

    TyTy = UserTy->desugar();
  }

  // Strip parens if necessary.
  if (const ParenExpr *PE = dyn_cast<ParenExpr>(E))
    return shouldNotPrintDirectly(Context, PE->getSubExpr()->getType(),
                                  PE->getSubExpr());

  // If this is a conditional expression, then its result type is constructed
  // via usual arithmetic conversions and thus there might be no necessary
  // typedef sugar there.  Recurse to operands to check for CFIndex &
  // Co. usage condition.
  if (const ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
    QualType TrueTy, FalseTy;
    llvm::StringRef TrueName, FalseName;

    std::tie(TrueTy, TrueName) = shouldNotPrintDirectly(
        Context, CO->getTrueExpr()->getType(), CO->getTrueExpr());
    std::tie(FalseTy, FalseName) = shouldNotPrintDirectly(
        Context, CO->getFalseExpr()->getType(), CO->getFalseExpr());

    if (TrueTy == FalseTy)
      return std::make_pair(TrueTy, TrueName);
    else if (TrueTy.isNull())
      return std::make_pair(FalseTy, FalseName);
    else if (FalseTy.isNull())
      return std::make_pair(TrueTy, TrueName);
  }

  return std::make_pair(QualType(), llvm::StringRef());
}

bool isArithmeticArgumentPromotion(Sema &S, const ImplicitCastExpr *ICE) {
  QualType From = ICE->getSubExpr()->getType();
  QualType To = ICE->getType();
  // It's an integer promotion if the destination type is the promoted
  // source type.
  if (ICE->getCastKind() == CK_IntegralCast &&
      S.Context.isPromotableIntegerType(From) &&
      S.Context.getPromotedIntegerType(From) == To)
    return true;
  // Look through vector types for default argument promotion.
  if (const auto *VecTy = From->getAs<ExtVectorType>())
    From = VecTy->getElementType();
  if (const auto *VecTy = To->getAs<ExtVectorType>())
    To = VecTy->getElementType();
  // It's a floating promotion if the source type is a lower rank.
  return ICE->getCastKind() == CK_FloatingCast &&
         S.Context.getFloatingTypeOrder(From, To) < 0;
}
} // namespace

bool CheckPrintfHandler::checkFormatExpr(
    const analyze_printf::PrintfSpecifier &FS, const char *StartSpecifier,
    unsigned SpecifierLen, const Expr *E) {
  using namespace analyze_format_string;
  using namespace analyze_printf;

  // Now type check the data expression that matches the
  // format specifier.
  const analyze_printf::ArgType &AT = FS.getArgType(S.Context);
  if (!AT.isValid())
    return true;

  QualType ExprTy = E->getType();
  while (const TypeOfExprType *TET = dyn_cast<TypeOfExprType>(ExprTy)) {
    ExprTy = TET->getUnderlyingExpr()->getType();
  }

  // Decay functions/arrays to pointers before comparing to the format string
  // type.
  if (ExprTy->canDecayToPointerType())
    ExprTy = S.Context.getDecayedType(ExprTy);

  // Diagnose attempts to print a boolean value as a character. Unlike other
  // -Wformat diagnostics, this is fine from a type perspective, but it still
  // doesn't make sense.
  if (FS.getConversionSpecifier().getKind() == ConversionSpecifier::cArg &&
      E->isKnownToHaveBooleanValue()) {
    const CharSourceRange &CSR =
        getSpecifierRange(StartSpecifier, SpecifierLen);
    llvm::SmallString<4> FSString;
    llvm::raw_svector_ostream os(FSString);
    FS.toString(os);
    GenFormatDiagnostic(S.PDiag(diag::warn_format_bool_as_character)
                            << FSString,
                        E->getExprLoc(), false, CSR);
    return true;
  }

  ArgType::MatchKind ImplicitMatch = ArgType::NoMatch;
  ArgType::MatchKind Match = AT.matchesType(S.Context, ExprTy);
  if (Match == ArgType::Match)
    return true;

  // NoMatchPromotionTypeConfusion should be only returned in ImplictCastExpr
  assert(Match != ArgType::NoMatchPromotionTypeConfusion);

  // Look through argument promotions for our error message's reported type.
  // This includes the integral and floating promotions, but excludes array
  // and function pointer decay (seeing that an argument intended to be a
  // string has type 'char [6]' is probably more confusing than 'char *') and
  // certain bitfield promotions (bitfields can be 'demoted' to a lesser type).
  if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (isArithmeticArgumentPromotion(S, ICE)) {
      E = ICE->getSubExpr();
      ExprTy = E->getType();

      // Check if we didn't match because of an implicit cast from a 'char'
      // or 'short' to an 'int'.  This is done because printf is a varargs
      // function.
      if (ICE->getType() == S.Context.IntTy ||
          ICE->getType() == S.Context.UnsignedIntTy) {
        // All further checking is done on the subexpression
        ImplicitMatch = AT.matchesType(S.Context, ExprTy);
        if (ImplicitMatch == ArgType::Match)
          return true;
      }
    }
  } else if (const CharacterLiteral *CL = dyn_cast<CharacterLiteral>(E)) {
    // Special case for 'a', which has type 'int' in C.
    // Note, however, that we do /not/ want to treat multibyte constants like
    // 'MooV' as characters! This form is deprecated but still exists. In
    // addition, don't treat expressions as of type 'char' if one byte length
    // modifier is provided.
    if (ExprTy == S.Context.IntTy &&
        FS.getLengthModifier().getKind() != LengthModifier::AsChar)
      if (llvm::isUIntN(S.Context.getCharWidth(), CL->getValue())) {
        ExprTy = S.Context.CharTy;
        // To improve check results, we consider a character literal in C
        // to be a 'char' rather than an 'int'. 'printf("%hd", 'a');' is
        // more likely a type confusion situation, so we will suggest to
        // use '%hhd' instead by discarding the MatchPromotion.
        if (Match == ArgType::MatchPromotion)
          Match = ArgType::NoMatch;
      }
  }
  if (Match == ArgType::MatchPromotion) {
    // WG14 N2562 only clarified promotions in *printf
    if (ImplicitMatch != ArgType::NoMatchPromotionTypeConfusion &&
        ImplicitMatch != ArgType::NoMatchTypeConfusion)
      return true;
    Match = ArgType::NoMatch;
  }
  if (ImplicitMatch == ArgType::NoMatchPedantic ||
      ImplicitMatch == ArgType::NoMatchTypeConfusion)
    Match = ImplicitMatch;
  assert(Match != ArgType::MatchPromotion);

  // Look through enums to their underlying type.
  bool IsEnum = false;
  QualType IntendedTy = ExprTy;
  if (auto EnumTy = ExprTy->getAs<EnumType>()) {
    IntendedTy = EnumTy->getDecl()->getIntegerType();
    ExprTy = IntendedTy;
    IsEnum = true;
  }

  // Special-case some of Darwin's platform-independence types by suggesting
  // casts to primitive types that are known to be large enough.
  bool ShouldNotPrintDirectly = false;
  llvm::StringRef CastTyName;
  if (S.Context.getTargetInfo().getTriple().isOSDarwin()) {
    QualType CastTy;
    std::tie(CastTy, CastTyName) =
        shouldNotPrintDirectly(S.Context, IntendedTy, E);
    if (!CastTy.isNull()) {
      IntendedTy = CastTy;
      ShouldNotPrintDirectly = true;
    }
  }

  // We may be able to offer a FixItHint if it is a supported type.
  PrintfSpecifier fixedFS = FS;
  bool Success = fixedFS.fixType(IntendedTy, S.getLangOpts(), S.Context);

  if (Success) {
    llvm::SmallString<16> buf;
    llvm::raw_svector_ostream os(buf);
    fixedFS.toString(os);

    CharSourceRange SpecRange = getSpecifierRange(StartSpecifier, SpecifierLen);

    if (IntendedTy == ExprTy && !ShouldNotPrintDirectly) {
      unsigned Diag;
      switch (Match) {
      case ArgType::Match:
      case ArgType::MatchPromotion:
      case ArgType::NoMatchPromotionTypeConfusion:
        llvm_unreachable("expected non-matching");
      case ArgType::NoMatchPedantic:
        Diag = diag::warn_format_conversion_argument_type_mismatch_pedantic;
        break;
      case ArgType::NoMatchTypeConfusion:
        Diag = diag::warn_format_conversion_argument_type_mismatch_confusion;
        break;
      case ArgType::NoMatch:
        Diag = diag::warn_format_conversion_argument_type_mismatch;
        break;
      }

      // In this case, the specifier is wrong and should be changed to match
      // the argument.
      GenFormatDiagnostic(S.PDiag(Diag)
                              << AT.getRepresentativeTypeName(S.Context)
                              << IntendedTy << IsEnum << E->getSourceRange(),
                          E->getBeginLoc(),
                          /*IsStringLocation*/ false, SpecRange,
                          FixItHint::CreateReplacement(SpecRange, os.str()));
    } else {
      // The canonical type for formatting this value is different from the
      // actual type of the expression (e.g., Darwin's CFIndex is typedef'd
      // as 'int' but should be printed as 'long').
      // Rather than emitting a normal format/argument mismatch, we want to
      // add a cast to the recommended type (and correct the format string
      // if necessary).
      llvm::SmallString<16> CastBuf;
      llvm::raw_svector_ostream CastFix(CastBuf);
      CastFix << "(";
      IntendedTy.print(CastFix, S.Context.getPrintingPolicy());
      CastFix << ")";

      llvm::SmallVector<FixItHint, 4> Hints;
      if (AT.matchesType(S.Context, IntendedTy) != ArgType::Match ||
          ShouldNotPrintDirectly)
        Hints.push_back(FixItHint::CreateReplacement(SpecRange, os.str()));

      if (const CStyleCastExpr *CCast = dyn_cast<CStyleCastExpr>(E)) {
        // If there's already a cast present, just replace it.
        SourceRange CastRange(CCast->getLParenLoc(), CCast->getRParenLoc());
        Hints.push_back(FixItHint::CreateReplacement(CastRange, CastFix.str()));

      } else if (!requiresParensToAddCast(E)) {
        // If the expression has high enough precedence,
        // just write the C-style cast.
        Hints.push_back(
            FixItHint::CreateInsertion(E->getBeginLoc(), CastFix.str()));
      } else {
        // Otherwise, add parens around the expression as well as the cast.
        CastFix << "(";
        Hints.push_back(
            FixItHint::CreateInsertion(E->getBeginLoc(), CastFix.str()));

        // We don't use getLocForEndOfToken because it returns invalid source
        // locations for macro expansions (by design).
        SourceLocation EndLoc = S.SourceMgr.getSpellingLoc(E->getEndLoc());
        SourceLocation After = EndLoc.getLocWithOffset(
            SourceScanner::measureTokenLength(EndLoc, S.SourceMgr, S.LangOpts));
        Hints.push_back(FixItHint::CreateInsertion(After, ")"));
      }

      if (ShouldNotPrintDirectly) {
        // The expression has a type that should not be printed directly.
        // We extract the name from the typedef because we don't want to show
        // the underlying type in the diagnostic.
        llvm::StringRef Name;
        if (const auto *TypedefTy = ExprTy->getAs<TypedefType>())
          Name = TypedefTy->getDecl()->getName();
        else
          Name = CastTyName;
        unsigned Diag = Match == ArgType::NoMatchPedantic
                            ? diag::warn_format_argument_needs_cast_pedantic
                            : diag::warn_format_argument_needs_cast;
        GenFormatDiagnostic(S.PDiag(Diag) << Name << IntendedTy << IsEnum
                                          << E->getSourceRange(),
                            E->getBeginLoc(), /*IsStringLocation=*/false,
                            SpecRange, Hints);
      } else {
        // In this case, the expression could be printed using a different
        // specifier, but we've decided that the specifier is probably correct
        // and we should cast instead. Just use the normal warning message.
        GenFormatDiagnostic(
            S.PDiag(diag::warn_format_conversion_argument_type_mismatch)
                << AT.getRepresentativeTypeName(S.Context) << ExprTy << IsEnum
                << E->getSourceRange(),
            E->getBeginLoc(), /*IsStringLocation*/ false, SpecRange, Hints);
      }
    }
  } else {
    const CharSourceRange &CSR =
        getSpecifierRange(StartSpecifier, SpecifierLen);
    // Since the warning for passing non-POD types to variadic functions
    // was deferred until now, we emit a warning for non-POD
    // arguments here.
    bool GenTypeMismatch = false;
    switch (S.isValidVarArgType(ExprTy)) {
    case Sema::VAK_Valid: {
      unsigned Diag;
      switch (Match) {
      case ArgType::Match:
      case ArgType::MatchPromotion:
      case ArgType::NoMatchPromotionTypeConfusion:
        llvm_unreachable("expected non-matching");
      case ArgType::NoMatchPedantic:
        Diag = diag::warn_format_conversion_argument_type_mismatch_pedantic;
        break;
      case ArgType::NoMatchTypeConfusion:
        Diag = diag::warn_format_conversion_argument_type_mismatch_confusion;
        break;
      case ArgType::NoMatch:
        Diag = diag::warn_format_conversion_argument_type_mismatch;
        break;
      }

      GenFormatDiagnostic(S.PDiag(Diag)
                              << AT.getRepresentativeTypeName(S.Context)
                              << ExprTy << IsEnum << CSR << E->getSourceRange(),
                          E->getBeginLoc(), /*IsStringLocation*/ false, CSR);
      break;
    }
    case Sema::VAK_Undefined:
    case Sema::VAK_MSVCUndefined:
      if (CallType == Sema::VariadicDoesNotApply) {
        GenTypeMismatch = true;
      } else {
        GenFormatDiagnostic(
            S.PDiag(diag::warn_non_pod_vararg_with_format_string)
                << false << ExprTy << AT.getRepresentativeTypeName(S.Context)
                << CSR << E->getSourceRange(),
            E->getBeginLoc(), /*IsStringLocation*/ false, CSR);
      }
      break;

    case Sema::VAK_Invalid:
      if (CallType == Sema::VariadicDoesNotApply)
        GenTypeMismatch = true;
      else
        S.Diag(E->getBeginLoc(), diag::err_cannot_pass_to_vararg_format)
            << isa<InitListExpr>(E) << ExprTy
            << AT.getRepresentativeTypeName(S.Context) << E->getSourceRange();
      break;
    }

    if (GenTypeMismatch) {
      // The function is not variadic, so we do not generate warnings about
      // being allowed to pass that object as a variadic argument. Instead,
      // since there are inherently no printf specifiers for types which cannot
      // be passed as variadic arguments, emit a plain old specifier mismatch
      // argument.
      GenFormatDiagnostic(
          S.PDiag(diag::warn_format_conversion_argument_type_mismatch)
              << AT.getRepresentativeTypeName(S.Context) << ExprTy << false
              << E->getSourceRange(),
          E->getBeginLoc(), false, CSR);
    }

    assert(FirstDataArg + FS.getArgIndex() < CheckedVarArgs.size() &&
           "format string specifier index out of range");
    CheckedVarArgs[FirstDataArg + FS.getArgIndex()] = true;
  }

  return true;
}

//===--- CHECK: Scanf format string checking ------------------------------===//

namespace {

class CheckScanfHandler : public CheckFormatHandler {
public:
  CheckScanfHandler(Sema &s, const FormatStringLiteral *fexpr,
                    const Expr *origFormatExpr, Sema::FormatStringType type,
                    unsigned firstDataArg, unsigned numDataArgs,
                    const char *beg, Sema::FormatArgumentPassingKind APK,
                    llvm::ArrayRef<const Expr *> Args, unsigned formatIdx,
                    bool inFunctionCall, Sema::VariadicCallType CallType,
                    llvm::SmallBitVector &CheckedVarArgs,
                    UncoveredArgHandler &UncoveredArg)
      : CheckFormatHandler(s, fexpr, origFormatExpr, type, firstDataArg,
                           numDataArgs, beg, APK, Args, formatIdx,
                           inFunctionCall, CallType, CheckedVarArgs,
                           UncoveredArg) {}

  bool HandleScanfSpecifier(const analyze_scanf::ScanfSpecifier &FS,
                            const char *startSpecifier,
                            unsigned specifierLen) override;

  bool
  HandleInvalidScanfConversionSpecifier(const analyze_scanf::ScanfSpecifier &FS,
                                        const char *startSpecifier,
                                        unsigned specifierLen) override;

  void HandleIncompleteScanList(const char *start, const char *end) override;
};

} // namespace

// ===----------------------------------------------------------------------===
// Scanf format checking
// ===----------------------------------------------------------------------===

void CheckScanfHandler::HandleIncompleteScanList(const char *start,
                                                 const char *end) {
  GenFormatDiagnostic(S.PDiag(diag::warn_scanf_scanlist_incomplete),
                      getLocationOfByte(end), /*IsStringLocation*/ true,
                      getSpecifierRange(start, end - start));
}

bool CheckScanfHandler::HandleInvalidScanfConversionSpecifier(
    const analyze_scanf::ScanfSpecifier &FS, const char *startSpecifier,
    unsigned specifierLen) {
  const analyze_scanf::ScanfConversionSpecifier &CS =
      FS.getConversionSpecifier();

  return HandleInvalidConversionSpecifier(
      FS.getArgIndex(), getLocationOfByte(CS.getStart()), startSpecifier,
      specifierLen, CS.getStart(), CS.getLength());
}

bool CheckScanfHandler::HandleScanfSpecifier(
    const analyze_scanf::ScanfSpecifier &FS, const char *startSpecifier,
    unsigned specifierLen) {
  using namespace analyze_scanf;
  using namespace analyze_format_string;

  const ScanfConversionSpecifier &CS = FS.getConversionSpecifier();

  // Handle case where '%' and '*' don't consume an argument.  These shouldn't
  // be used to decide if we are using positional arguments consistently.
  if (FS.consumesDataArgument()) {
    if (atFirstArg) {
      atFirstArg = false;
      usesPositionalArgs = FS.usesPositionalArg();
    } else if (usesPositionalArgs != FS.usesPositionalArg()) {
      HandlePositionalNonpositionalArgs(getLocationOfByte(CS.getStart()),
                                        startSpecifier, specifierLen);
      return false;
    }
  }

  const OptionalAmount &Amt = FS.getFieldWidth();
  if (Amt.getHowSpecified() == OptionalAmount::Constant) {
    if (Amt.getConstantAmount() == 0) {
      const CharSourceRange &R =
          getSpecifierRange(Amt.getStart(), Amt.getConstantLength());
      GenFormatDiagnostic(S.PDiag(diag::warn_scanf_nonzero_width),
                          getLocationOfByte(Amt.getStart()),
                          /*IsStringLocation*/ true, R,
                          FixItHint::CreateRemoval(R));
    }
  }

  if (!FS.consumesDataArgument()) {

    return true;
  }

  // Consume the argument.
  unsigned argIndex = FS.getArgIndex();
  if (argIndex < NumDataArgs) {
    // The check to see if the argIndex is valid will come later.
    // We set the bit here because we may exit early from this
    // function if we encounter some other error.
    CoveredArgs.set(argIndex);
  }

  if (!FS.hasValidLengthModifier(S.getTreeContext().getTargetInfo(),
                                 S.getLangOpts()))
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_nonsensical_length);
  else if (!FS.hasStandardLengthModifier())
    HandleNonStandardLengthModifier(FS, startSpecifier, specifierLen);
  else if (!FS.hasStandardLengthConversionCombination())
    HandleInvalidLengthModifier(FS, CS, startSpecifier, specifierLen,
                                diag::warn_format_non_standard_conversion_spec);

  if (!FS.hasStandardConversionSpecifier(S.getLangOpts()))
    HandleNonStandardConversionSpecifier(CS, startSpecifier, specifierLen);

  if (ArgPassingKind == Sema::FAPK_VAList)
    return true;

  if (!CheckNumArgs(FS, CS, startSpecifier, specifierLen, argIndex))
    return false;

  const Expr *Ex = getDataArg(argIndex);
  if (!Ex)
    return true;

  const analyze_format_string::ArgType &AT = FS.getArgType(S.Context);

  if (!AT.isValid()) {
    return true;
  }

  analyze_format_string::ArgType::MatchKind Match =
      AT.matchesType(S.Context, Ex->getType());
  bool Pedantic = Match == analyze_format_string::ArgType::NoMatchPedantic;
  if (Match == analyze_format_string::ArgType::Match)
    return true;

  ScanfSpecifier fixedFS = FS;
  bool Success = fixedFS.fixType(Ex->getType(), Ex->IgnoreImpCasts()->getType(),
                                 S.getLangOpts(), S.Context);

  unsigned Diag =
      Pedantic ? diag::warn_format_conversion_argument_type_mismatch_pedantic
               : diag::warn_format_conversion_argument_type_mismatch;

  if (Success) {
    llvm::SmallString<128> buf;
    llvm::raw_svector_ostream os(buf);
    fixedFS.toString(os);

    GenFormatDiagnostic(
        S.PDiag(Diag) << AT.getRepresentativeTypeName(S.Context)
                      << Ex->getType() << false << Ex->getSourceRange(),
        Ex->getBeginLoc(),
        /*IsStringLocation*/ false,
        getSpecifierRange(startSpecifier, specifierLen),
        FixItHint::CreateReplacement(
            getSpecifierRange(startSpecifier, specifierLen), os.str()));
  } else {
    GenFormatDiagnostic(S.PDiag(Diag)
                            << AT.getRepresentativeTypeName(S.Context)
                            << Ex->getType() << false << Ex->getSourceRange(),
                        Ex->getBeginLoc(),
                        /*IsStringLocation*/ false,
                        getSpecifierRange(startSpecifier, specifierLen));
  }

  return true;
}

namespace {
void checkFormatString(Sema &S, const FormatStringLiteral *FExpr,
                       const Expr *OrigFormatExpr,
                       llvm::ArrayRef<const Expr *> Args,
                       Sema::FormatArgumentPassingKind APK, unsigned format_idx,
                       unsigned firstDataArg, Sema::FormatStringType Type,
                       bool inFunctionCall, Sema::VariadicCallType CallType,
                       llvm::SmallBitVector &CheckedVarArgs,
                       UncoveredArgHandler &UncoveredArg,
                       bool IgnoreStringsWithoutSpecifiers) {
  // CHECK: is the format string a wide literal?
  if (!FExpr->isAscii() && !FExpr->isUTF8()) {
    CheckFormatHandler::GenFormatDiagnostic(
        S, inFunctionCall, Args[format_idx],
        S.PDiag(diag::warn_format_string_is_wide_literal), FExpr->getBeginLoc(),
        /*IsStringLocation*/ true, OrigFormatExpr->getSourceRange());
    return;
  }

  // Str - The format string.  NOTE: this is NOT null-terminated!
  llvm::StringRef StrRef = FExpr->getString();
  const char *Str = StrRef.data();
  // Account for cases where the string literal is truncated in a declaration.
  const ConstantArrayType *T =
      S.Context.getAsConstantArrayType(FExpr->getType());
  assert(T && "String literal not of constant array type!");
  size_t TypeSize = T->getSize().getZExtValue();
  size_t StrLen = std::min(std::max(TypeSize, size_t(1)) - 1, StrRef.size());
  const unsigned numDataArgs = Args.size() - firstDataArg;

  if (IgnoreStringsWithoutSpecifiers &&
      !analyze_format_string::parseFormatStringHasFormattingSpecifiers(
          Str, Str + StrLen, S.getLangOpts(), S.Context.getTargetInfo()))
    return;

  // Emit a warning if the string literal is truncated and does not contain an
  // embedded null character.
  if (TypeSize <= StrRef.size() && !StrRef.substr(0, TypeSize).contains('\0')) {
    CheckFormatHandler::GenFormatDiagnostic(
        S, inFunctionCall, Args[format_idx],
        S.PDiag(diag::warn_printf_format_string_not_null_terminated),
        FExpr->getBeginLoc(),
        /*IsStringLocation=*/true, OrigFormatExpr->getSourceRange());
    return;
  }

  // CHECK: empty format string?
  if (StrLen == 0 && numDataArgs > 0) {
    CheckFormatHandler::GenFormatDiagnostic(
        S, inFunctionCall, Args[format_idx],
        S.PDiag(diag::warn_empty_format_string), FExpr->getBeginLoc(),
        /*IsStringLocation*/ true, OrigFormatExpr->getSourceRange());
    return;
  }

  if (Type == Sema::FST_Printf || Type == Sema::FST_OSLog ||
      Type == Sema::FST_OSTrace) {
    CheckPrintfHandler H(S, FExpr, OrigFormatExpr, Type, firstDataArg,
                         numDataArgs, Type == Sema::FST_OSTrace, Str, APK, Args,
                         format_idx, inFunctionCall, CallType, CheckedVarArgs,
                         UncoveredArg);

    if (!analyze_format_string::ParsePrintfString(
            H, Str, Str + StrLen, S.getLangOpts(), S.Context.getTargetInfo()))
      H.DoneProcessing();
  } else if (Type == Sema::FST_Scanf) {
    CheckScanfHandler H(S, FExpr, OrigFormatExpr, Type, firstDataArg,
                        numDataArgs, Str, APK, Args, format_idx, inFunctionCall,
                        CallType, CheckedVarArgs, UncoveredArg);

    if (!analyze_format_string::ParseScanfString(
            H, Str, Str + StrLen, S.getLangOpts(), S.Context.getTargetInfo()))
      H.DoneProcessing();
  }
}
} // namespace
