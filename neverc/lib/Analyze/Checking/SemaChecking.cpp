#include "SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/Core/SyncScope.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/SourceScanner.h"
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
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <optional>
#include <ctime>
#include <string>
#include <tuple>
#include <utility>
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"

using namespace neverc;
using namespace sema;

SourceLocation Sema::getLocationOfStringLiteralByte(const StringLiteral *SL,
                                                    unsigned ByteNo) const {
  return SL->getLocationOfByte(ByteNo, getSourceManager(), LangOpts,
                               Context.getTargetInfo());
}

// ===----------------------------------------------------------------------===
// Argument validation helpers
// ===----------------------------------------------------------------------===

namespace {
bool checkArgCountAtLeast(Sema &S, CallExpr *Call, unsigned MinArgCount) {
  unsigned ArgCount = Call->getNumArgs();
  if (ArgCount >= MinArgCount)
    return false;

  return S.Diag(Call->getEndLoc(), diag::err_typecheck_call_too_few_args)
         << MinArgCount << ArgCount << Call->getSourceRange();
}

bool checkArgCountAtMost(Sema &S, CallExpr *Call, unsigned MaxArgCount) {
  unsigned ArgCount = Call->getNumArgs();
  if (ArgCount <= MaxArgCount)
    return false;
  return S.Diag(Call->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
         << MaxArgCount << ArgCount << Call->getSourceRange();
}

bool checkArgCountRange(Sema &S, CallExpr *Call, unsigned MinArgCount,
                        unsigned MaxArgCount) {
  return checkArgCountAtLeast(S, Call, MinArgCount) ||
         checkArgCountAtMost(S, Call, MaxArgCount);
}

bool checkArgCount(Sema &S, CallExpr *Call, unsigned DesiredArgCount) {
  unsigned ArgCount = Call->getNumArgs();
  if (ArgCount == DesiredArgCount)
    return false;

  if (checkArgCountAtLeast(S, Call, DesiredArgCount))
    return true;
  assert(ArgCount > DesiredArgCount && "should have diagnosed this");

  // Highlight all the excess arguments.
  SourceRange Range(Call->getArg(DesiredArgCount)->getBeginLoc(),
                    Call->getArg(ArgCount - 1)->getEndLoc());

  return S.Diag(Range.getBegin(), diag::err_typecheck_call_too_many_args)
         << DesiredArgCount << ArgCount << Call->getArg(1)->getSourceRange();
}

bool convertArgumentToType(Sema &S, Expr *&Value, QualType Ty) {
  InitializedEntity Entity =
      InitializedEntity::InitializeParameter(S.Context, Ty, false);
  ExprResult Result =
      S.PerformCopyInitialization(Entity, SourceLocation(), Value);
  if (Result.isInvalid())
    return true;
  Value = Result.get();
  return false;
}
} // namespace

// ===----------------------------------------------------------------------===
// Individual builtin semantic checks
// ===----------------------------------------------------------------------===

namespace {
bool semaBuiltinAnnotation(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 2))
    return true;

  // First argument should be an integer.
  Expr *ValArg = TheCall->getArg(0);
  QualType Ty = ValArg->getType();
  if (!Ty->isIntegerType()) {
    S.Diag(ValArg->getBeginLoc(), diag::err_builtin_annotation_first_arg)
        << ValArg->getSourceRange();
    return true;
  }

  // Second argument should be a constant string.
  Expr *StrArg = TheCall->getArg(1)->IgnoreParenCasts();
  StringLiteral *Literal = dyn_cast<StringLiteral>(StrArg);
  if (!Literal || !Literal->isOrdinary()) {
    S.Diag(StrArg->getBeginLoc(), diag::err_builtin_annotation_second_arg)
        << StrArg->getSourceRange();
    return true;
  }

  TheCall->setType(Ty);
  return false;
}

ExprResult semaBuiltinNeverCXorstr(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 1))
    return ExprError();

  Expr *Arg = TheCall->getArg(0)->IgnoreParenCasts();
  StringLiteral *SL = dyn_cast<StringLiteral>(Arg);
  if (!SL) {
    S.Diag(Arg->getBeginLoc(), diag::err_expr_not_string_literal)
        << Arg->getSourceRange();
    return ExprError();
  }

  if (SL->getKind() != StringLiteralKind::Ordinary &&
      SL->getKind() != StringLiteralKind::UTF8) {
    StringLiteral *Folded = foldNeverCStringWideLiteralToUtf8(S, SL);
    if (Folded)
      SL = Folded;
  }

  llvm::StringRef Bytes = SL->getBytes();
  unsigned Len = Bytes.size();

  static uint64_t Counter = 0;
  uint64_t BaseKey = S.getLangOpts().StringEncryptKey;
  if (BaseKey == 0) {
    static uint64_t TimeKey =
        static_cast<uint64_t>(std::time(nullptr)) * 0x9E3779B97F4A7C15ULL;
    TimeKey |= 1;
    BaseKey = TimeKey;
  }
  QualType SizeTy = S.Context.getSizeType();
  unsigned SizeBits = S.Context.getTypeSize(SizeTy);
  unsigned KeyBytes = SizeBits / 8;

  uint64_t Key = BaseKey ^ (++Counter * 0x517CC1B727220A95ULL);
  Key &= llvm::maskTrailingOnes<uint64_t>(SizeBits);

  auto encryptByte = [KeyBytes](unsigned char byte, uint64_t key,
                                unsigned idx) -> char {
    auto k = static_cast<unsigned char>(key >> (8 * (idx % KeyBytes)));
    return static_cast<char>(byte ^ k);
  };

  llvm::SmallVector<char, 256> EncBytes(Len);
  for (unsigned i = 0; i < Len; ++i)
    EncBytes[i] = encryptByte(static_cast<unsigned char>(Bytes[i]), Key, i);

  SourceLocation Loc = TheCall->getBeginLoc();
  SourceLocation EndLoc = TheCall->getEndLoc();

  QualType EncStrTy =
      S.Context.getStringLiteralArrayType(S.Context.CharTy, Len);
  SmallVector<SourceLocation, 1> SLLocs;
  SLLocs.push_back(SL->getBeginLoc());
  StringLiteral *EncSL = StringLiteral::Create(
      S.Context, llvm::StringRef(EncBytes.data(), Len),
      StringLiteralKind::Ordinary, EncStrTy, SLLocs.data(), SLLocs.size());

  IntegerLiteral *LenLit = IntegerLiteral::Create(
      S.Context, llvm::APInt(SizeBits, Len), SizeTy, Loc);
  IntegerLiteral *KeyLit = IntegerLiteral::Create(
      S.Context, llvm::APInt(SizeBits, Key), SizeTy, Loc);

  FunctionDecl *FD = S.lookupNeverCStringFunctionDecl(
      "__neverc_xorstr_decrypt", nullptr, Loc);
  if (!FD) {
    unsigned DiagID = S.Diags.getCustomDiagID(
        DiagnosticsEngine::Error,
        "'__builtin_neverc_xorstr' requires '#include <neverc/xorstr.h>'");
    S.Diag(Loc, DiagID);
    return ExprError();
  }

  ExprResult DeclRef =
      S.MakeDeclRefExpr(FD, FD->getType(), VK_LValue, Loc);
  if (DeclRef.isInvalid())
    return ExprError();

  Expr *Args[] = {EncSL, LenLit, KeyLit};
  return S.FormCallExpr(nullptr, DeclRef.get(), Loc, Args, EndLoc);
}

bool semaBuiltinMSVCAnnotation(Sema &S, CallExpr *TheCall) {
  // We need at least one argument.
  if (TheCall->getNumArgs() < 1) {
    S.Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args_at_least)
        << 1 << TheCall->getNumArgs() << TheCall->getCallee()->getSourceRange();
    return true;
  }

  // All arguments should be wide string literals.
  for (Expr *Arg : TheCall->arguments()) {
    auto *Literal = dyn_cast<StringLiteral>(Arg->IgnoreParenCasts());
    if (!Literal || !Literal->isWide()) {
      S.Diag(Arg->getBeginLoc(), diag::err_msvc_annotation_wide_str)
          << Arg->getSourceRange();
      return true;
    }
  }

  return false;
}

bool semaBuiltinFunctionStart(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 1))
    return true;

  ExprResult Arg = S.DefaultFunctionArrayLvalueConversion(TheCall->getArg(0));
  if (Arg.isInvalid())
    return true;

  TheCall->setArg(0, Arg.get());
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(
      Arg.get()->getAsBuiltinConstantDeclRef(S.getTreeContext()));

  if (!FD) {
    S.Diag(TheCall->getBeginLoc(), diag::err_function_start_invalid_type)
        << TheCall->getSourceRange();
    return true;
  }

  return !S.checkAddressOfFunctionIsAvailable(FD, /*Complain=*/true,
                                              TheCall->getBeginLoc());
}

bool semaBuiltinPreserveAI(Sema &S, CallExpr *TheCall) {
  if (checkArgCount(S, TheCall, 1))
    return true;

  TheCall->setType(TheCall->getArg(0)->getType());
  return false;
}

bool semaBuiltinAlignment(Sema &S, CallExpr *TheCall, unsigned ID) {
  if (checkArgCount(S, TheCall, 2))
    return true;

  neverc::Expr *Source = TheCall->getArg(0);
  bool IsBooleanAlignBuiltin = ID == Builtin::BI__builtin_is_aligned;

  auto IsValidIntegerType = [](QualType Ty) {
    return Ty->isIntegerType() && !Ty->isEnumeralType() && !Ty->isBooleanType();
  };
  QualType SrcTy = Source->getType();
  // We should also be able to use it with arrays (but not functions!).
  if (SrcTy->canDecayToPointerType() && SrcTy->isArrayType()) {
    SrcTy = S.Context.getDecayedType(SrcTy);
  }
  if ((!SrcTy->isPointerType() && !IsValidIntegerType(SrcTy)) ||
      SrcTy->isFunctionPointerType()) {
    S.Diag(Source->getExprLoc(), diag::err_typecheck_expect_scalar_operand)
        << SrcTy;
    return true;
  }

  neverc::Expr *AlignOp = TheCall->getArg(1);
  if (!IsValidIntegerType(AlignOp->getType())) {
    S.Diag(AlignOp->getExprLoc(), diag::err_typecheck_expect_int)
        << AlignOp->getType();
    return true;
  }
  Expr::EvalResult AlignResult;
  unsigned MaxAlignmentBits = S.Context.getIntWidth(SrcTy) - 1;
  if (AlignOp->EvaluateAsInt(AlignResult, S.Context,
                             Expr::SE_AllowSideEffects)) {
    llvm::APSInt AlignValue = AlignResult.Val.getInt();
    llvm::APSInt MaxValue(
        llvm::APInt::getOneBitSet(MaxAlignmentBits + 1, MaxAlignmentBits));
    if (AlignValue < 1) {
      S.Diag(AlignOp->getExprLoc(), diag::err_alignment_too_small) << 1;
      return true;
    }
    if (llvm::APSInt::compareValues(AlignValue, MaxValue) > 0) {
      S.Diag(AlignOp->getExprLoc(), diag::err_alignment_too_big)
          << toString(MaxValue, 10);
      return true;
    }
    if (!AlignValue.isPowerOf2()) {
      S.Diag(AlignOp->getExprLoc(), diag::err_alignment_not_power_of_two);
      return true;
    }
    if (AlignValue == 1) {
      S.Diag(AlignOp->getExprLoc(), diag::warn_alignment_builtin_useless)
          << IsBooleanAlignBuiltin;
    }
  }

  ExprResult SrcArg = S.PerformCopyInitialization(
      InitializedEntity::InitializeParameter(S.Context, SrcTy, false),
      SourceLocation(), Source);
  if (SrcArg.isInvalid())
    return true;
  TheCall->setArg(0, SrcArg.get());
  ExprResult AlignArg =
      S.PerformCopyInitialization(InitializedEntity::InitializeParameter(
                                      S.Context, AlignOp->getType(), false),
                                  SourceLocation(), AlignOp);
  if (AlignArg.isInvalid())
    return true;
  TheCall->setArg(1, AlignArg.get());
  // For align_up/align_down, the return type is the same as the (potentially
  // decayed) argument type including qualifiers. For is_aligned(), the result
  // is always bool.
  TheCall->setType(IsBooleanAlignBuiltin ? S.Context.BoolTy : SrcTy);
  return false;
}

bool semaBuiltinOverflow(Sema &S, CallExpr *TheCall, unsigned BuiltinID) {
  if (checkArgCount(S, TheCall, 3))
    return true;

  std::pair<unsigned, const char *> Builtins[] = {
      {Builtin::BI__builtin_add_overflow, "ckd_add"},
      {Builtin::BI__builtin_sub_overflow, "ckd_sub"},
      {Builtin::BI__builtin_mul_overflow, "ckd_mul"},
  };

  bool CkdOperation =
      llvm::any_of(Builtins, [&](const std::pair<unsigned, const char *> &P) {
        return BuiltinID == P.first && TheCall->getExprLoc().isMacroID() &&
               SourceScanner::getImmediateMacroName(
                   TheCall->getExprLoc(), S.getSourceManager(),
                   S.getLangOpts()) == P.second;
      });

  auto ValidCkdIntType = [](QualType QT) {
    // A valid checked integer type is an integer type other than a plain char,
    // bool, a bit-precise type, or an enumeration type.
    if (const auto *BT = QT.getCanonicalType()->getAs<BuiltinType>())
      return (BT->getKind() >= BuiltinType::Short &&
              BT->getKind() <= BuiltinType::Int128) ||
             (BT->getKind() >= BuiltinType::UShort &&
              BT->getKind() <= BuiltinType::UInt128) ||
             BT->getKind() == BuiltinType::UChar ||
             BT->getKind() == BuiltinType::SChar;
    return false;
  };

  // First two arguments should be integers.
  for (unsigned I = 0; I < 2; ++I) {
    ExprResult Arg = S.DefaultFunctionArrayLvalueConversion(TheCall->getArg(I));
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(I, Arg.get());

    QualType Ty = Arg.get()->getType();
    bool IsValid = CkdOperation ? ValidCkdIntType(Ty) : Ty->isIntegerType();
    if (!IsValid) {
      S.Diag(Arg.get()->getBeginLoc(), diag::err_overflow_builtin_must_be_int)
          << CkdOperation << Ty << Arg.get()->getSourceRange();
      return true;
    }
  }

  // Third argument should be a pointer to a non-const integer.
  // IRGen correctly handles volatile, restrict, and address spaces, and
  // the other qualifiers aren't possible.
  {
    ExprResult Arg = S.DefaultFunctionArrayLvalueConversion(TheCall->getArg(2));
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(2, Arg.get());

    QualType Ty = Arg.get()->getType();
    const auto *PtrTy = Ty->getAs<PointerType>();
    if (!PtrTy || !PtrTy->getPointeeType()->isIntegerType() ||
        (!ValidCkdIntType(PtrTy->getPointeeType()) && CkdOperation) ||
        PtrTy->getPointeeType().isConstQualified()) {
      S.Diag(Arg.get()->getBeginLoc(),
             diag::err_overflow_builtin_must_be_ptr_int)
          << CkdOperation << Ty << Arg.get()->getSourceRange();
      return true;
    }
  }

  // Disallow signed bit-precise integer args larger than 128 bits to mul
  // function until we improve backend support.
  if (BuiltinID == Builtin::BI__builtin_mul_overflow) {
    for (unsigned I = 0; I < 3; ++I) {
      const auto Arg = TheCall->getArg(I);
      // Third argument will be a pointer.
      auto Ty = I < 2 ? Arg->getType() : Arg->getType()->getPointeeType();
      if (Ty->isBitIntType() && Ty->isSignedIntegerType() &&
          S.getTreeContext().getIntWidth(Ty) > 128)
        return S.Diag(Arg->getBeginLoc(),
                      diag::err_overflow_builtin_bit_int_max_size)
               << 128;
    }
  }

  return false;
}
} // namespace

namespace {
struct BuiltinDumpStructGenerator {
  Sema &S;
  CallExpr *TheCall;
  SourceLocation Loc = TheCall->getBeginLoc();
  llvm::SmallVector<Expr *, 32> Actions;
  DiagnosticErrorTrap ErrorTracker;
  PrintingPolicy Policy;

  BuiltinDumpStructGenerator(Sema &S, CallExpr *TheCall)
      : S(S), TheCall(TheCall), ErrorTracker(S.getDiagnostics()),
        Policy{S.Context.getPrintingPolicy()} {
    Policy.AnonymousTagLocations = false;
  }

  Expr *makeOpaqueValueExpr(Expr *Inner) {
    auto *OVE = new (S.Context)
        OpaqueValueExpr(Loc, Inner->getType(), Inner->getValueKind(),
                        Inner->getObjectKind(), Inner);
    Actions.push_back(OVE);
    return OVE;
  }

  Expr *getStringLiteral(llvm::StringRef Str) {
    Expr *Lit = S.Context.getPredefinedStringLiteralFromCache(Str);
    // Wrap the literal in parentheses to attach a source location.
    return new (S.Context) ParenExpr(Loc, Loc, Lit);
  }

  bool callPrintFunction(llvm::StringRef Format,
                         llvm::ArrayRef<Expr *> Exprs = {}) {
    llvm::SmallVector<Expr *, 8> Args;
    assert(TheCall->getNumArgs() >= 2);
    Args.reserve((TheCall->getNumArgs() - 2) + /*Format*/ 1 + Exprs.size());
    Args.assign(TheCall->arg_begin() + 2, TheCall->arg_end());
    Args.push_back(getStringLiteral(Format));
    Args.insert(Args.end(), Exprs.begin(), Exprs.end());

    // Register a note to explain why we're performing the call.
    ExprResult RealCall =
        S.FormCallExpr(/*Scope=*/nullptr, TheCall->getArg(1),
                       TheCall->getBeginLoc(), Args, TheCall->getRParenLoc());
    if (!RealCall.isInvalid())
      Actions.push_back(RealCall.get());
    // Bail out if we've hit any errors, even if we managed to build the
    // call. We don't want to produce more than one error.
    return RealCall.isInvalid() || ErrorTracker.hasErrorOccurred();
  }

  Expr *getIndentString(unsigned Depth) {
    if (!Depth)
      return nullptr;

    llvm::SmallString<32> Indent;
    Indent.resize(Depth * Policy.Indentation, ' ');
    return getStringLiteral(Indent);
  }

  Expr *getTypeString(QualType T) {
    return getStringLiteral(T.getAsString(Policy));
  }

  bool appendFormatSpecifier(QualType T, llvm::SmallVectorImpl<char> &Str) {
    llvm::raw_svector_ostream OS(Str);

    // Format 'bool', 'char', 'signed char', 'unsigned char' as numbers, rather
    // than trying to print a single character.
    if (auto *BT = T->getAs<BuiltinType>()) {
      switch (BT->getKind()) {
      case BuiltinType::Bool:
        OS << "%d";
        return true;
      case BuiltinType::Char_U:
      case BuiltinType::UChar:
        OS << "%hhu";
        return true;
      case BuiltinType::Char_S:
      case BuiltinType::SChar:
        OS << "%hhd";
        return true;
      default:
        break;
      }
    }

    analyze_printf::PrintfSpecifier Specifier;
    if (Specifier.fixType(T, S.getLangOpts(), S.Context)) {
      // We were able to guess how to format this.
      if (Specifier.getConversionSpecifier().getKind() ==
          analyze_printf::PrintfConversionSpecifier::sArg) {
        // Wrap double-quotes around a '%s' specifier and limit its maximum
        // length. Ideally we'd also somehow escape special characters in the
        // contents but printf doesn't support that.
        OS << '"';
        Specifier.setPrecision(analyze_printf::OptionalAmount(32u));
        Specifier.toString(OS);
        OS << '"';
      } else {
        Specifier.toString(OS);
      }
      return true;
    }

    if (T->isPointerType()) {
      // Format all pointers with '%p'.
      OS << "%p";
      return true;
    }

    return false;
  }

  bool dumpUnnamedRecord(const RecordDecl *RD, Expr *E, unsigned Depth) {
    Expr *IndentLit = getIndentString(Depth);
    Expr *TypeLit = getTypeString(S.Context.getRecordType(RD));
    if (IndentLit ? callPrintFunction("%s%s", {IndentLit, TypeLit})
                  : callPrintFunction("%s", {TypeLit}))
      return true;

    return dumpRecordValue(RD, E, IndentLit, Depth);
  }

  // Dump a record value. E should be a pointer or lvalue referring to an RD.
  bool dumpRecordValue(const RecordDecl *RD, Expr *E, Expr *RecordIndent,
                       unsigned Depth) {
    // If RD is a union, we should probably turn off printing
    // `const char*` members with `%s`, because that is very
    // likely to crash if that's not the active member. Whatever we decide, we
    // should document it.

    // Build an OpaqueValueExpr so we can refer to E more than once without
    // triggering re-evaluation.
    Expr *RecordArg = makeOpaqueValueExpr(E);
    bool RecordArgIsPtr = RecordArg->getType()->isPointerType();

    if (callPrintFunction(" {\n"))
      return true;

    Expr *FieldIndentArg = getIndentString(Depth + 1);

    // Dump each field.
    for (auto *D : RD->decls()) {
      auto *IFD = dyn_cast<IndirectFieldDecl>(D);
      auto *FD = IFD ? IFD->getAnonField() : dyn_cast<FieldDecl>(D);
      if (!FD || FD->isUnnamedBitfield() || FD->isAnonymousStructOrUnion())
        continue;

      llvm::SmallString<20> Format = llvm::StringRef("%s%s %s ");
      llvm::SmallVector<Expr *, 5> Args = {FieldIndentArg,
                                           getTypeString(FD->getType()),
                                           getStringLiteral(FD->getName())};

      if (FD->isBitField()) {
        Format += ": %zu ";
        QualType SizeT = S.Context.getSizeType();
        llvm::APInt BitWidth(S.Context.getIntWidth(SizeT),
                             FD->getBitWidthValue(S.Context));
        Args.push_back(IntegerLiteral::Create(S.Context, BitWidth, SizeT, Loc));
      }

      Format += "=";

      ExprResult Field = IFD ? S.FormAnonymousStructUnionMemberReference(
                                   Loc, IFD, IFD, RecordArg, Loc)
                             : S.FormFieldReferenceExpr(
                                   RecordArg, RecordArgIsPtr, Loc, FD, FD,
                                   DeclarationNameInfo(FD->getDeclName(), Loc));
      if (Field.isInvalid())
        return true;

      auto *InnerRD = FD->getType()->getAsRecordDecl();
      if (InnerRD) {
        // Recursively print the values of members of aggregate record type.
        if (callPrintFunction(Format, Args) ||
            dumpRecordValue(InnerRD, Field.get(), FieldIndentArg, Depth + 1))
          return true;
      } else {
        Format += " ";
        if (appendFormatSpecifier(FD->getType(), Format)) {
          // We know how to print this field.
          Args.push_back(Field.get());
        } else {
          // We don't know how to print this field. Print out its address
          // with a format specifier that a smart tool will be able to
          // recognize and treat specially.
          Format += "*%p";
          ExprResult FieldAddr =
              S.FormUnaryOp(nullptr, Loc, UO_AddrOf, Field.get());
          if (FieldAddr.isInvalid())
            return true;
          Args.push_back(FieldAddr.get());
        }
        Format += "\n";
        if (callPrintFunction(Format, Args))
          return true;
      }
    }

    return RecordIndent ? callPrintFunction("%s}\n", RecordIndent)
                        : callPrintFunction("}\n");
  }

  Expr *buildWrapper() {
    auto *Wrapper = PseudoObjectExpr::Create(S.Context, TheCall, Actions,
                                             PseudoObjectExpr::NoResult);
    TheCall->setType(Wrapper->getType());
    TheCall->setValueKind(Wrapper->getValueKind());
    return Wrapper;
  }
};
} // namespace

namespace {
ExprResult semaBuiltinDumpStruct(Sema &S, CallExpr *TheCall) {
  if (checkArgCountAtLeast(S, TheCall, 2))
    return ExprError();

  ExprResult PtrArgResult = S.DefaultLvalueConversion(TheCall->getArg(0));
  if (PtrArgResult.isInvalid())
    return ExprError();
  TheCall->setArg(0, PtrArgResult.get());

  // First argument should be a pointer to a struct.
  QualType PtrArgType = PtrArgResult.get()->getType();
  if (!PtrArgType->isPointerType() ||
      !PtrArgType->getPointeeType()->isRecordType()) {
    S.Diag(PtrArgResult.get()->getBeginLoc(),
           diag::err_expected_struct_pointer_argument)
        << 1 << TheCall->getDirectCallee() << PtrArgType;
    return ExprError();
  }
  QualType Pointee = PtrArgType->getPointeeType();
  const RecordDecl *RD = Pointee->getAsRecordDecl();
  // Ensure the type is complete; otherwise, access to its data() may crash.
  if (S.RequireCompleteType(PtrArgResult.get()->getBeginLoc(), Pointee,
                            diag::err_incomplete_type))
    return ExprError();
  // Second argument is a callable, but we can't fully validate it until we try
  // calling it.
  QualType FnArgType = TheCall->getArg(1)->getType();
  if (!FnArgType->isFunctionType() && !FnArgType->isFunctionPointerType()) {
    auto *BT = FnArgType->getAs<BuiltinType>();
    switch (BT ? BT->getKind() : BuiltinType::Void) {
    case BuiltinType::Dependent:
    case BuiltinType::Overload:
    case BuiltinType::PseudoObject:
    case BuiltinType::BuiltinFn:
      // This might be a callable.
      break;

    default:
      S.Diag(TheCall->getArg(1)->getBeginLoc(),
             diag::err_expected_callable_argument)
          << 2 << TheCall->getDirectCallee() << FnArgType;
      return ExprError();
    }
  }

  BuiltinDumpStructGenerator Generator(S, TheCall);

  // Wrap parentheses around the given pointer. This is not necessary for
  // correct code generation, but it means that when we pretty-print the call
  // arguments in our diagnostics we will produce '(&s)->n' instead of the
  // incorrect '&s->n'.
  Expr *PtrArg = PtrArgResult.get();
  PtrArg = new (S.Context)
      ParenExpr(PtrArg->getBeginLoc(),
                S.getLocForEndOfToken(PtrArg->getEndLoc()), PtrArg);
  if (Generator.dumpUnnamedRecord(RD, PtrArg, 0))
    return ExprError();

  return Generator.buildWrapper();
}
} // namespace

namespace {
bool semaBuiltinCallWithStaticChain(Sema &S, CallExpr *BuiltinCall) {
  if (checkArgCount(S, BuiltinCall, 2))
    return true;

  SourceLocation BuiltinLoc = BuiltinCall->getBeginLoc();
  Expr *Builtin = BuiltinCall->getCallee()->IgnoreImpCasts();
  Expr *Call = BuiltinCall->getArg(0);
  Expr *Chain = BuiltinCall->getArg(1);

  if (Call->getStmtClass() != Stmt::CallExprClass) {
    S.Diag(BuiltinLoc, diag::err_first_argument_to_cwsc_not_call)
        << Call->getSourceRange();
    return true;
  }

  auto CE = cast<CallExpr>(Call);

  const Decl *TargetDecl = CE->getCalleeDecl();
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(TargetDecl))
    if (FD->getBuiltinID()) {
      S.Diag(BuiltinLoc, diag::err_first_argument_to_cwsc_builtin_call)
          << Call->getSourceRange();
      return true;
    }

  ExprResult ChainResult = S.UsualUnaryConversions(Chain);
  if (ChainResult.isInvalid())
    return true;
  if (!ChainResult.get()->getType()->isPointerType()) {
    S.Diag(BuiltinLoc, diag::err_second_argument_to_cwsc_not_pointer)
        << Chain->getSourceRange();
    return true;
  }

  QualType ReturnTy = CE->getCallReturnType(S.Context);
  QualType ArgTys[2] = {ReturnTy, ChainResult.get()->getType()};
  QualType BuiltinTy = S.Context.getFunctionType(
      ReturnTy, ArgTys, FunctionProtoType::ExtProtoInfo());
  QualType BuiltinPtrTy = S.Context.getPointerType(BuiltinTy);

  Builtin =
      S.ImpCastExprToType(Builtin, BuiltinPtrTy, CK_BuiltinFnToFnPtr).get();

  BuiltinCall->setType(CE->getType());
  BuiltinCall->setValueKind(CE->getValueKind());
  BuiltinCall->setObjectKind(CE->getObjectKind());
  BuiltinCall->setCallee(Builtin);
  BuiltinCall->setArg(1, ChainResult.get());

  return false;
}
} // namespace

namespace {

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

} // namespace

namespace {
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
} // namespace

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

namespace {
bool semaBuiltinSEHScopeCheck(Sema &SemaRef, CallExpr *TheCall,
                              Scope::ScopeFlags NeededScopeFlags,
                              unsigned DiagID) {
  Scope *S = SemaRef.getCurScope();
  while (S && !S->isSEHExceptScope())
    S = S->getParent();
  if (!S || !(S->getFlags() & NeededScopeFlags)) {
    auto *DRE = cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
    SemaRef.Diag(TheCall->getExprLoc(), DiagID)
        << DRE->getDecl()->getIdentifier();
    return true;
  }

  return false;
}
} // namespace

// Emit an error and return true if the current architecture is not in the list
// of supported architectures.
namespace {
bool checkBuiltinTargetInSupported(
    Sema &S, unsigned BuiltinID, CallExpr *TheCall,
    llvm::ArrayRef<llvm::Triple::ArchType> SupportedArchs) {
  llvm::Triple::ArchType CurArch =
      S.getTreeContext().getTargetInfo().getTriple().getArch();
  if (llvm::is_contained(SupportedArchs, CurArch))
    return false;
  S.Diag(TheCall->getBeginLoc(), diag::err_builtin_target_unsupported)
      << TheCall->getSourceRange();
  return true;
}

void checkNonNullArgument(Sema &S, const Expr *ArgExpr,
                          SourceLocation CallSiteLoc);
} // namespace

bool Sema::CheckTSBuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                      CallExpr *TheCall) {
  switch (TI.getTriple().getArch()) {
  default:
    // Some builtins don't require additional checking, so just consider these
    // acceptable.
    return false;
  case llvm::Triple::aarch64:
    return CheckAArch64BuiltinFunctionCall(TI, BuiltinID, TheCall);
  case llvm::Triple::x86_64:
    return CheckX86BuiltinFunctionCall(TI, BuiltinID, TheCall);
  }
}

// Check if \p Ty is a valid type for the elementwise math builtins. If it is
// not a valid type, emit an error message and return true. Otherwise return
// false.
namespace {
bool checkMathBuiltinElementType(Sema &S, SourceLocation Loc, QualType Ty) {
  if (!Ty->getAs<VectorType>() && !ConstantMatrixType::isValidElementType(Ty)) {
    return S.Diag(Loc, diag::err_builtin_invalid_arg_type)
           << 1 << /* vector, integer or float ty*/ 0 << Ty;
  }

  return false;
}

bool checkFPMathBuiltinElementType(Sema &S, SourceLocation Loc, QualType ArgTy,
                                   int ArgIndex) {
  QualType EltTy = ArgTy;
  if (auto *VecTy = EltTy->getAs<VectorType>())
    EltTy = VecTy->getElementType();

  if (!EltTy->isRealFloatingType()) {
    return S.Diag(Loc, diag::err_builtin_invalid_arg_type)
           << ArgIndex << /* vector or float ty*/ 5 << ArgTy;
  }

  return false;
}
} // namespace

// ===----------------------------------------------------------------------===
// Builtin function call dispatch
// ===----------------------------------------------------------------------===

ExprResult Sema::CheckBuiltinFunctionCall(FunctionDecl *FDecl,
                                          unsigned BuiltinID,
                                          CallExpr *TheCall) {
  ExprResult TheCallResult(TheCall);

  // Find out if any arguments are required to be integer constant expressions.
  unsigned ICEArguments = 0;
  TreeContext::GetBuiltinTypeError Error;
  Context.GetBuiltinType(BuiltinID, Error, &ICEArguments);
  if (Error != TreeContext::GE_None)
    ICEArguments = 0; // Don't diagnose previously diagnosed errors.

  // If any arguments are required to be ICE's, check and diagnose.
  for (unsigned ArgNo = 0; ICEArguments != 0; ++ArgNo) {
    // Skip arguments not required to be ICE's.
    if ((ICEArguments & (1 << ArgNo)) == 0)
      continue;

    llvm::APSInt Result;
    // If we don't have enough arguments, continue so we can issue better
    // diagnostic in checkArgCount(...)
    if (ArgNo < TheCall->getNumArgs() &&
        SemaBuiltinConstantArg(TheCall, ArgNo, Result))
      return true;
    ICEArguments &= ~(1 << ArgNo);
  }

  switch (BuiltinID) {
  case Builtin::BI__builtin_ms_va_start:
  case Builtin::BI__builtin_stdarg_start:
  case Builtin::BI__builtin_va_start:
    if (SemaBuiltinVAStart(BuiltinID, TheCall))
      return ExprError();
    break;
  case Builtin::BI__va_start: {
    if (Context.getTargetInfo().getTriple().getArch() ==
        llvm::Triple::aarch64) {
      if (SemaBuiltinVAStartARMMicrosoft(TheCall))
        return ExprError();
    } else if (SemaBuiltinVAStart(BuiltinID, TheCall)) {
      return ExprError();
    }
    break;
  }

  // The acquire, release, and no fence variants are AArch64 only.
  case Builtin::BI_interlockedbittestandset_acq:
  case Builtin::BI_interlockedbittestandset_rel:
  case Builtin::BI_interlockedbittestandset_nf:
  case Builtin::BI_interlockedbittestandreset_acq:
  case Builtin::BI_interlockedbittestandreset_rel:
  case Builtin::BI_interlockedbittestandreset_nf:
    if (checkBuiltinTargetInSupported(*this, BuiltinID, TheCall,
                                      {llvm::Triple::aarch64}))
      return ExprError();
    break;

  // The 64-bit bittest variants are x64 and AArch64 only.
  case Builtin::BI_bittest64:
  case Builtin::BI_bittestandcomplement64:
  case Builtin::BI_bittestandreset64:
  case Builtin::BI_bittestandset64:
  case Builtin::BI_interlockedbittestandreset64:
  case Builtin::BI_interlockedbittestandset64:
    if (checkBuiltinTargetInSupported(
            *this, BuiltinID, TheCall,
            {llvm::Triple::x86_64, llvm::Triple::aarch64}))
      return ExprError();
    break;

  case Builtin::BI__builtin_set_flt_rounds:
    if (checkBuiltinTargetInSupported(
            *this, BuiltinID, TheCall,
            {llvm::Triple::x86_64, llvm::Triple::aarch64}))
      return ExprError();
    break;

  case Builtin::BI__builtin_isgreater:
  case Builtin::BI__builtin_isgreaterequal:
  case Builtin::BI__builtin_isless:
  case Builtin::BI__builtin_islessequal:
  case Builtin::BI__builtin_islessgreater:
  case Builtin::BI__builtin_isunordered:
    if (SemaBuiltinUnorderedCompare(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_fpclassify:
    if (SemaBuiltinFPClassification(TheCall, 6))
      return ExprError();
    break;
  case Builtin::BI__builtin_isfpclass:
    if (SemaBuiltinFPClassification(TheCall, 2))
      return ExprError();
    break;
  case Builtin::BI__builtin_isfinite:
  case Builtin::BI__builtin_isinf:
  case Builtin::BI__builtin_isinf_sign:
  case Builtin::BI__builtin_isnan:
  case Builtin::BI__builtin_issignaling:
  case Builtin::BI__builtin_isnormal:
  case Builtin::BI__builtin_issubnormal:
  case Builtin::BI__builtin_iszero:
  case Builtin::BI__builtin_signbit:
  case Builtin::BI__builtin_signbitf:
  case Builtin::BI__builtin_signbitl:
    if (SemaBuiltinFPClassification(TheCall, 1))
      return ExprError();
    break;
  case Builtin::BI__builtin_shufflevector:
    return SemaBuiltinShuffleVector(TheCall);
    // TheCall will be freed by the smart pointer here, but that's fine, since
    // SemaBuiltinShuffleVector guts it, but then doesn't release it.
  case Builtin::BI__builtin_prefetch:
    if (SemaBuiltinPrefetch(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_alloca_with_align:
  case Builtin::BI__builtin_alloca_with_align_uninitialized:
    if (SemaBuiltinAllocaWithAlign(TheCall))
      return ExprError();
    [[fallthrough]];
  case Builtin::BI__builtin_alloca:
  case Builtin::BI__builtin_alloca_uninitialized:
    Diag(TheCall->getBeginLoc(), diag::warn_alloca)
        << TheCall->getDirectCallee();
    break;
  case Builtin::BI__arithmetic_fence:
    if (SemaBuiltinArithmeticFence(TheCall))
      return ExprError();
    break;
  case Builtin::BI__assume:
  case Builtin::BI__builtin_assume:
    if (SemaBuiltinAssume(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_assume_aligned:
    if (SemaBuiltinAssumeAligned(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_dynamic_object_size:
  case Builtin::BI__builtin_object_size:
    if (SemaBuiltinConstantArgRange(TheCall, 1, 0, 3))
      return ExprError();
    break;
  case Builtin::BI__builtin_longjmp:
    if (SemaBuiltinLongjmp(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_setjmp:
    if (SemaBuiltinSetjmp(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_classify_type:
    if (checkArgCount(*this, TheCall, 1))
      return true;
    TheCall->setType(Context.IntTy);
    break;
  case Builtin::BI__builtin_complex:
    if (SemaBuiltinComplex(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_constant_p: {
    if (checkArgCount(*this, TheCall, 1))
      return true;
    ExprResult Arg = DefaultFunctionArrayLvalueConversion(TheCall->getArg(0));
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(0, Arg.get());
    TheCall->setType(Context.IntTy);
    break;
  }
  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
  case Builtin::BI__sync_fetch_and_nand:
  case Builtin::BI__sync_fetch_and_nand_1:
  case Builtin::BI__sync_fetch_and_nand_2:
  case Builtin::BI__sync_fetch_and_nand_4:
  case Builtin::BI__sync_fetch_and_nand_8:
  case Builtin::BI__sync_fetch_and_nand_16:
  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
  case Builtin::BI__sync_nand_and_fetch:
  case Builtin::BI__sync_nand_and_fetch_1:
  case Builtin::BI__sync_nand_and_fetch_2:
  case Builtin::BI__sync_nand_and_fetch_4:
  case Builtin::BI__sync_nand_and_fetch_8:
  case Builtin::BI__sync_nand_and_fetch_16:
  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16:
  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16:
  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
  case Builtin::BI__sync_lock_release:
  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16:
  case Builtin::BI__sync_swap:
  case Builtin::BI__sync_swap_1:
  case Builtin::BI__sync_swap_2:
  case Builtin::BI__sync_swap_4:
  case Builtin::BI__sync_swap_8:
  case Builtin::BI__sync_swap_16:
    return SemaBuiltinAtomicOverloaded(TheCallResult);
  case Builtin::BI__sync_synchronize:
    Diag(TheCall->getBeginLoc(), diag::warn_atomic_implicit_seq_cst)
        << TheCall->getCallee()->getSourceRange();
    break;
  case Builtin::BI__builtin_nontemporal_load:
  case Builtin::BI__builtin_nontemporal_store:
    return SemaBuiltinNontemporalOverloaded(TheCallResult);
  case Builtin::BI__builtin_memcpy_inline: {
    neverc::Expr *SizeOp = TheCall->getArg(2);
    // We warn about copying to or from `nullptr` pointers when `size` is
    // greater than 0. When `size` is value dependent we cannot evaluate its
    // value so we bail out.
    if (!SizeOp->EvaluateKnownConstInt(Context).isZero()) {
      checkNonNullArgument(*this, TheCall->getArg(0), TheCall->getExprLoc());
      checkNonNullArgument(*this, TheCall->getArg(1), TheCall->getExprLoc());
    }
    break;
  }
  case Builtin::BI__builtin_memset_inline: {
    neverc::Expr *SizeOp = TheCall->getArg(2);
    if (!SizeOp->EvaluateKnownConstInt(Context).isZero())
      checkNonNullArgument(*this, TheCall->getArg(0), TheCall->getExprLoc());
    break;
  }
#define BUILTIN(ID, TYPE, ATTRS)
#define ATOMIC_BUILTIN(ID, TYPE, ATTRS)                                        \
  case Builtin::BI##ID:                                                        \
    return SemaAtomicOpsOverloaded(TheCallResult, AtomicExpr::AO##ID);
#include "neverc/Foundation/Builtin/Builtins.def"
  case Builtin::BI__annotation:
    if (semaBuiltinMSVCAnnotation(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_annotation:
    if (semaBuiltinAnnotation(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_neverc_xorstr:
    return semaBuiltinNeverCXorstr(*this, TheCall);
  case Builtin::BI__builtin_function_start:
    if (semaBuiltinFunctionStart(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_is_aligned:
  case Builtin::BI__builtin_align_up:
  case Builtin::BI__builtin_align_down:
    if (semaBuiltinAlignment(*this, TheCall, BuiltinID))
      return ExprError();
    break;
  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_mul_overflow:
    if (semaBuiltinOverflow(*this, TheCall, BuiltinID))
      return ExprError();
    break;
  case Builtin::BI__builtin_dump_struct:
    return semaBuiltinDumpStruct(*this, TheCall);
  case Builtin::BI__builtin_expect_with_probability: {
    // We first want to ensure we are called with 3 arguments
    if (checkArgCount(*this, TheCall, 3))
      return ExprError();
    // then check probability is constant float in range [0.0, 1.0]
    const Expr *ProbArg = TheCall->getArg(2);
    llvm::SmallVector<PartialDiagnosticAt, 8> Notes;
    Expr::EvalResult Eval;
    Eval.Diag = &Notes;
    if ((!ProbArg->EvaluateAsConstantExpr(Eval, Context)) ||
        !Eval.Val.isFloat()) {
      Diag(ProbArg->getBeginLoc(), diag::err_probability_not_constant_float)
          << ProbArg->getSourceRange();
      for (const PartialDiagnosticAt &PDiag : Notes)
        Diag(PDiag.first, PDiag.second);
      return ExprError();
    }
    llvm::APFloat Probability = Eval.Val.getFloat();
    bool LoseInfo = false;
    Probability.convert(llvm::APFloat::IEEEdouble(),
                        llvm::RoundingMode::Dynamic, &LoseInfo);
    if (!(Probability >= llvm::APFloat(0.0) &&
          Probability <= llvm::APFloat(1.0))) {
      Diag(ProbArg->getBeginLoc(), diag::err_probability_out_of_range)
          << ProbArg->getSourceRange();
      return ExprError();
    }
    break;
  }
  case Builtin::BI__builtin_preserve_access_index:
    if (semaBuiltinPreserveAI(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_call_with_static_chain:
    if (semaBuiltinCallWithStaticChain(*this, TheCall))
      return ExprError();
    break;
  case Builtin::BI__exception_code:
  case Builtin::BI_exception_code:
    if (semaBuiltinSEHScopeCheck(*this, TheCall, Scope::SEHExceptScope,
                                 diag::err_seh___except_block))
      return ExprError();
    break;
  case Builtin::BI__exception_info:
  case Builtin::BI_exception_info:
    if (semaBuiltinSEHScopeCheck(*this, TheCall, Scope::SEHFilterScope,
                                 diag::err_seh___except_filter))
      return ExprError();
    break;
  case Builtin::BI__builtin_os_log_format:
    Cleanup.setExprNeedsCleanups(true);
    [[fallthrough]];
  case Builtin::BI__builtin_os_log_format_buffer_size:
    if (SemaBuiltinOSLogFormat(TheCall))
      return ExprError();
    break;
  case Builtin::BI__builtin_frame_address:
  case Builtin::BI__builtin_return_address: {
    if (SemaBuiltinConstantArgRange(TheCall, 0, 0, 0xFFFF))
      return ExprError();

    // -Wframe-address warning if non-zero passed to builtin
    // return/frame address.
    Expr::EvalResult Result;
    if (TheCall->getArg(0)->EvaluateAsInt(Result, getTreeContext()) &&
        Result.Val.getInt() != 0)
      Diag(TheCall->getBeginLoc(), diag::warn_frame_address)
          << ((BuiltinID == Builtin::BI__builtin_return_address)
                  ? "__builtin_return_address"
                  : "__builtin_frame_address")
          << TheCall->getSourceRange();
    break;
  }

  case Builtin::BI__builtin_nondeterministic_value: {
    if (SemaBuiltinNonDeterministicValue(TheCall))
      return ExprError();
    break;
  }

  // __builtin_elementwise_abs restricts the element type to signed integers or
  // floating point types only.
  case Builtin::BI__builtin_elementwise_abs: {
    if (PrepareBuiltinElementwiseMathOneArgCall(TheCall))
      return ExprError();

    QualType ArgTy = TheCall->getArg(0)->getType();
    QualType EltTy = ArgTy;

    if (auto *VecTy = EltTy->getAs<VectorType>())
      EltTy = VecTy->getElementType();
    if (EltTy->isUnsignedIntegerType()) {
      Diag(TheCall->getArg(0)->getBeginLoc(),
           diag::err_builtin_invalid_arg_type)
          << 1 << /* signed integer or float ty*/ 3 << ArgTy;
      return ExprError();
    }
    break;
  }

  // These builtins restrict the element type to floating point
  // types only.
  case Builtin::BI__builtin_elementwise_ceil:
  case Builtin::BI__builtin_elementwise_cos:
  case Builtin::BI__builtin_elementwise_exp:
  case Builtin::BI__builtin_elementwise_exp2:
  case Builtin::BI__builtin_elementwise_floor:
  case Builtin::BI__builtin_elementwise_log:
  case Builtin::BI__builtin_elementwise_log2:
  case Builtin::BI__builtin_elementwise_log10:
  case Builtin::BI__builtin_elementwise_roundeven:
  case Builtin::BI__builtin_elementwise_round:
  case Builtin::BI__builtin_elementwise_rint:
  case Builtin::BI__builtin_elementwise_nearbyint:
  case Builtin::BI__builtin_elementwise_sin:
  case Builtin::BI__builtin_elementwise_sqrt:
  case Builtin::BI__builtin_elementwise_trunc:
  case Builtin::BI__builtin_elementwise_canonicalize: {
    if (PrepareBuiltinElementwiseMathOneArgCall(TheCall))
      return ExprError();

    QualType ArgTy = TheCall->getArg(0)->getType();
    if (checkFPMathBuiltinElementType(*this, TheCall->getArg(0)->getBeginLoc(),
                                      ArgTy, 1))
      return ExprError();
    break;
  }
  case Builtin::BI__builtin_elementwise_fma: {
    if (SemaBuiltinElementwiseTernaryMath(TheCall))
      return ExprError();
    break;
  }

  // These builtins restrict the element type to floating point
  // types only, and take in two arguments.
  case Builtin::BI__builtin_elementwise_pow: {
    if (SemaBuiltinElementwiseMath(TheCall))
      return ExprError();

    QualType ArgTy = TheCall->getArg(0)->getType();
    if (checkFPMathBuiltinElementType(*this, TheCall->getArg(0)->getBeginLoc(),
                                      ArgTy, 1) ||
        checkFPMathBuiltinElementType(*this, TheCall->getArg(1)->getBeginLoc(),
                                      ArgTy, 2))
      return ExprError();
    break;
  }

  // These builtins restrict the element type to integer
  // types only.
  case Builtin::BI__builtin_elementwise_add_sat:
  case Builtin::BI__builtin_elementwise_sub_sat: {
    if (SemaBuiltinElementwiseMath(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    QualType ArgTy = Arg->getType();
    QualType EltTy = ArgTy;

    if (auto *VecTy = EltTy->getAs<VectorType>())
      EltTy = VecTy->getElementType();

    if (!EltTy->isIntegerType()) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1 << /* integer ty */ 6 << ArgTy;
      return ExprError();
    }
    break;
  }

  case Builtin::BI__builtin_elementwise_min:
  case Builtin::BI__builtin_elementwise_max:
    if (SemaBuiltinElementwiseMath(TheCall))
      return ExprError();
    break;

  case Builtin::BI__builtin_elementwise_bitreverse: {
    if (PrepareBuiltinElementwiseMathOneArgCall(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    QualType ArgTy = Arg->getType();
    QualType EltTy = ArgTy;

    if (auto *VecTy = EltTy->getAs<VectorType>())
      EltTy = VecTy->getElementType();

    if (!EltTy->isIntegerType()) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1 << /* integer ty */ 6 << ArgTy;
      return ExprError();
    }
    break;
  }

  case Builtin::BI__builtin_elementwise_copysign: {
    if (checkArgCount(*this, TheCall, 2))
      return ExprError();

    ExprResult Magnitude = UsualUnaryConversions(TheCall->getArg(0));
    ExprResult Sign = UsualUnaryConversions(TheCall->getArg(1));
    if (Magnitude.isInvalid() || Sign.isInvalid())
      return ExprError();

    QualType MagnitudeTy = Magnitude.get()->getType();
    QualType SignTy = Sign.get()->getType();
    if (checkFPMathBuiltinElementType(*this, TheCall->getArg(0)->getBeginLoc(),
                                      MagnitudeTy, 1) ||
        checkFPMathBuiltinElementType(*this, TheCall->getArg(1)->getBeginLoc(),
                                      SignTy, 2)) {
      return ExprError();
    }

    if (MagnitudeTy.getCanonicalType() != SignTy.getCanonicalType()) {
      return Diag(Sign.get()->getBeginLoc(),
                  diag::err_typecheck_call_different_arg_types)
             << MagnitudeTy << SignTy;
    }

    TheCall->setArg(0, Magnitude.get());
    TheCall->setArg(1, Sign.get());
    TheCall->setType(Magnitude.get()->getType());
    break;
  }
  case Builtin::BI__builtin_reduce_max:
  case Builtin::BI__builtin_reduce_min: {
    if (PrepareBuiltinReduceMathOneArgCall(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    const auto *TyA = Arg->getType()->getAs<VectorType>();
    if (!TyA) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1 << /* vector ty*/ 4 << Arg->getType();
      return ExprError();
    }

    TheCall->setType(TyA->getElementType());
    break;
  }

  // These builtins support vectors of integers only.
  case Builtin::BI__builtin_reduce_add:
  case Builtin::BI__builtin_reduce_mul:
  case Builtin::BI__builtin_reduce_xor:
  case Builtin::BI__builtin_reduce_or:
  case Builtin::BI__builtin_reduce_and: {
    if (PrepareBuiltinReduceMathOneArgCall(TheCall))
      return ExprError();

    const Expr *Arg = TheCall->getArg(0);
    const auto *TyA = Arg->getType()->getAs<VectorType>();
    if (!TyA || !TyA->getElementType()->isIntegerType()) {
      Diag(Arg->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << 1 << /* vector of integers */ 6 << Arg->getType();
      return ExprError();
    }
    TheCall->setType(TyA->getElementType());
    break;
  }

  case Builtin::BI__builtin_matrix_transpose:
    return SemaBuiltinMatrixTranspose(TheCall, TheCallResult);

  case Builtin::BI__builtin_matrix_column_major_load:
    return SemaBuiltinMatrixColumnMajorLoad(TheCall, TheCallResult);

  case Builtin::BI__builtin_matrix_column_major_store:
    return SemaBuiltinMatrixColumnMajorStore(TheCall, TheCallResult);
  }

  if (Context.BuiltinInfo.isTSBuiltin(BuiltinID)) {
    if (CheckTSBuiltinFunctionCall(Context.getTargetInfo(), BuiltinID, TheCall))
      return ExprError();
  }

  return TheCallResult;
}

// Get the valid immediate range for the specified NEON type code.
namespace {
unsigned RFT(unsigned t, bool shift = false, bool ForceQuad = false) {
  NeonTypeFlags Type(t);
  int IsQuad = ForceQuad ? true : Type.isQuad();
  switch (Type.getEltType()) {
  case NeonTypeFlags::Int8:
  case NeonTypeFlags::Poly8:
    return shift ? 7 : (8 << IsQuad) - 1;
  case NeonTypeFlags::Int16:
  case NeonTypeFlags::Poly16:
    return shift ? 15 : (4 << IsQuad) - 1;
  case NeonTypeFlags::Int32:
    return shift ? 31 : (2 << IsQuad) - 1;
  case NeonTypeFlags::Int64:
  case NeonTypeFlags::Poly64:
    return shift ? 63 : (1 << IsQuad) - 1;
  case NeonTypeFlags::Poly128:
    return shift ? 127 : (1 << IsQuad) - 1;
  case NeonTypeFlags::Float16:
    assert(!shift && "cannot shift float types!");
    return (4 << IsQuad) - 1;
  case NeonTypeFlags::Float32:
    assert(!shift && "cannot shift float types!");
    return (2 << IsQuad) - 1;
  case NeonTypeFlags::Float64:
    assert(!shift && "cannot shift float types!");
    return (1 << IsQuad) - 1;
  case NeonTypeFlags::BFloat16:
    assert(!shift && "cannot shift float types!");
    return (4 << IsQuad) - 1;
  }
  llvm_unreachable("Invalid NeonTypeFlag!");
}

QualType getNeonEltType(NeonTypeFlags Flags, TreeContext &Context,
                        bool IsPolyUnsigned, bool IsInt64Long) {
  switch (Flags.getEltType()) {
  case NeonTypeFlags::Int8:
    return Flags.isUnsigned() ? Context.UnsignedCharTy : Context.SignedCharTy;
  case NeonTypeFlags::Int16:
    return Flags.isUnsigned() ? Context.UnsignedShortTy : Context.ShortTy;
  case NeonTypeFlags::Int32:
    return Flags.isUnsigned() ? Context.UnsignedIntTy : Context.IntTy;
  case NeonTypeFlags::Int64:
    if (IsInt64Long)
      return Flags.isUnsigned() ? Context.UnsignedLongTy : Context.LongTy;
    else
      return Flags.isUnsigned() ? Context.UnsignedLongLongTy
                                : Context.LongLongTy;
  case NeonTypeFlags::Poly8:
    return IsPolyUnsigned ? Context.UnsignedCharTy : Context.SignedCharTy;
  case NeonTypeFlags::Poly16:
    return IsPolyUnsigned ? Context.UnsignedShortTy : Context.ShortTy;
  case NeonTypeFlags::Poly64:
    if (IsInt64Long)
      return Context.UnsignedLongTy;
    else
      return Context.UnsignedLongLongTy;
  case NeonTypeFlags::Poly128:
    break;
  case NeonTypeFlags::Float16:
    return Context.HalfTy;
  case NeonTypeFlags::Float32:
    return Context.FloatTy;
  case NeonTypeFlags::Float64:
    return Context.DoubleTy;
  case NeonTypeFlags::BFloat16:
    return Context.BFloat16Ty;
  }
  llvm_unreachable("Invalid NeonTypeFlag!");
}
} // namespace

// ===----------------------------------------------------------------------===
// ARM / AArch64 builtin validation
// ===----------------------------------------------------------------------===

enum ArmStreamingType { ArmNonStreaming, ArmStreaming, ArmStreamingCompatible };

bool Sema::ParseSVEImmChecks(
    CallExpr *TheCall,
    llvm::SmallVector<std::tuple<int, int, int>, 3> &ImmChecks) {
  // Perform all the immediate checks for this builtin call.
  bool HasError = false;
  for (auto &I : ImmChecks) {
    int ArgNum, CheckTy, ElementSizeInBits;
    std::tie(ArgNum, CheckTy, ElementSizeInBits) = I;

    typedef bool (*OptionSetCheckFnTy)(int64_t Value);

    // Function that checks whether the operand (ArgNum) is an immediate
    // that is one of the predefined values.
    auto CheckImmediateInSet = [&](OptionSetCheckFnTy CheckImm,
                                   int ErrDiag) -> bool {
      Expr *Arg = TheCall->getArg(ArgNum);
      llvm::APSInt Imm;
      if (SemaBuiltinConstantArg(TheCall, ArgNum, Imm))
        return true;

      if (!CheckImm(Imm.getSExtValue()))
        return Diag(TheCall->getBeginLoc(), ErrDiag) << Arg->getSourceRange();
      return false;
    };

    switch ((SVETypeFlags::ImmCheckType)CheckTy) {
    case SVETypeFlags::ImmCheck0_31:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 31))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_13:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 13))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck1_16:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1, 16))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_7:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 7))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck1_1:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1, 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck1_3:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1, 3))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck1_7:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1, 7))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckExtract:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0,
                                      (2048 / ElementSizeInBits) - 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckShiftRight:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1, ElementSizeInBits))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckShiftRightNarrow:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1,
                                      ElementSizeInBits / 2))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckShiftLeft:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0,
                                      ElementSizeInBits - 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckLaneIndex:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0,
                                      (128 / (1 * ElementSizeInBits)) - 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckLaneIndexCompRotate:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0,
                                      (128 / (2 * ElementSizeInBits)) - 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckLaneIndexDot:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0,
                                      (128 / (4 * ElementSizeInBits)) - 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckComplexRot90_270:
      if (CheckImmediateInSet([](int64_t V) { return V == 90 || V == 270; },
                              diag::err_rotation_argument_to_cadd))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckComplexRotAll90:
      if (CheckImmediateInSet(
              [](int64_t V) {
                return V == 0 || V == 90 || V == 180 || V == 270;
              },
              diag::err_rotation_argument_to_cmla))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_1:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_2:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 2))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_3:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 3))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_0:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 0))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_15:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 15))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_255:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 255))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck2_4_Mul2:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 2, 4) ||
          SemaBuiltinConstantArgMultiple(TheCall, ArgNum, 2))
        HasError = true;
      break;
    }
  }

  return HasError;
}

namespace {
ArmStreamingType getArmStreamingFnType(const FunctionDecl *FD) {
  if (FD->hasAttr<ArmLocallyStreamingAttr>())
    return ArmStreaming;
  if (const auto *T = FD->getType()->getAs<FunctionProtoType>()) {
    if (T->getAArch64SMEAttributes() & FunctionType::SME_PStateSMEnabledMask)
      return ArmStreaming;
    if (T->getAArch64SMEAttributes() & FunctionType::SME_PStateSMCompatibleMask)
      return ArmStreamingCompatible;
  }
  return ArmNonStreaming;
}
} // namespace

namespace {
void checkArmStreamingBuiltin(Sema &S, CallExpr *TheCall,
                              const FunctionDecl *FD,
                              ArmStreamingType BuiltinType) {
  ArmStreamingType FnType = getArmStreamingFnType(FD);
  if (FnType == ArmStreaming && BuiltinType == ArmNonStreaming) {
    S.Diag(TheCall->getBeginLoc(), diag::warn_attribute_arm_sm_incompat_builtin)
        << TheCall->getSourceRange() << "streaming";
  }

  if (FnType == ArmStreamingCompatible &&
      BuiltinType != ArmStreamingCompatible) {
    S.Diag(TheCall->getBeginLoc(), diag::warn_attribute_arm_sm_incompat_builtin)
        << TheCall->getSourceRange() << "streaming compatible";
    return;
  }

  if (FnType == ArmNonStreaming && BuiltinType == ArmStreaming) {
    S.Diag(TheCall->getBeginLoc(), diag::warn_attribute_arm_sm_incompat_builtin)
        << TheCall->getSourceRange() << "non-streaming";
  }
}

bool hasSMEZAState(const FunctionDecl *FD) {
  if (FD->hasAttr<ArmNewZAAttr>())
    return true;
  if (const auto *T = FD->getType()->getAs<FunctionProtoType>())
    if (T->getAArch64SMEAttributes() & FunctionType::SME_PStateZASharedMask)
      return true;
  return false;
}

bool hasSMEZAState(unsigned BuiltinID) {
  switch (BuiltinID) {
  default:
    return false;
#define GET_SME_BUILTIN_HAS_ZA_STATE
#include "neverc/Foundation/arm_sme_builtins_za_state.td.h"
#undef GET_SME_BUILTIN_HAS_ZA_STATE
  }
}
} // namespace

bool Sema::CheckSMEBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall) {
  if (const FunctionDecl *FD = getCurFunctionDecl()) {
    std::optional<ArmStreamingType> BuiltinType;

    switch (BuiltinID) {
#define GET_SME_STREAMING_ATTRS
#include "neverc/Foundation/arm_sme_streaming_attrs.td.h"
#undef GET_SME_STREAMING_ATTRS
    }

    if (BuiltinType)
      checkArmStreamingBuiltin(*this, TheCall, FD, *BuiltinType);

    if (hasSMEZAState(BuiltinID) && !hasSMEZAState(FD))
      Diag(TheCall->getBeginLoc(),
           diag::warn_attribute_arm_za_builtin_no_za_state)
          << TheCall->getSourceRange();
  }

  // Range check SME intrinsics that take immediate values.
  llvm::SmallVector<std::tuple<int, int, int>, 3> ImmChecks;

  switch (BuiltinID) {
  default:
    return false;
#define GET_SME_IMMEDIATE_CHECK
#include "neverc/Foundation/arm_sme_sema_rangechecks.td.h"
#undef GET_SME_IMMEDIATE_CHECK
  }

  return ParseSVEImmChecks(TheCall, ImmChecks);
}

bool Sema::CheckSVEBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall) {
  if (const FunctionDecl *FD = getCurFunctionDecl()) {
    std::optional<ArmStreamingType> BuiltinType;

    switch (BuiltinID) {
#define GET_SVE_STREAMING_ATTRS
#include "neverc/Foundation/arm_sve_streaming_attrs.td.h"
#undef GET_SVE_STREAMING_ATTRS
    }
    if (BuiltinType)
      checkArmStreamingBuiltin(*this, TheCall, FD, *BuiltinType);
  }
  // Range check SVE intrinsics that take immediate values.
  llvm::SmallVector<std::tuple<int, int, int>, 3> ImmChecks;

  switch (BuiltinID) {
  default:
    return false;
#define GET_SVE_IMMEDIATE_CHECK
#include "neverc/Foundation/arm_sve_sema_rangechecks.td.h"
#undef GET_SVE_IMMEDIATE_CHECK
  }

  return ParseSVEImmChecks(TheCall, ImmChecks);
}

bool Sema::CheckNeonBuiltinFunctionCall(const TargetInfo &TI,
                                        unsigned BuiltinID, CallExpr *TheCall) {
  if (const FunctionDecl *FD = getCurFunctionDecl()) {

    switch (BuiltinID) {
    default:
      break;
#define GET_NEON_BUILTINS
#define TARGET_BUILTIN(id, ...) case NEON::BI##id:
#define BUILTIN(id, ...) case NEON::BI##id:
#include "neverc/Foundation/arm_neon.td.h"
      checkArmStreamingBuiltin(*this, TheCall, FD, ArmNonStreaming);
      break;
#undef TARGET_BUILTIN
#undef BUILTIN
#undef GET_NEON_BUILTINS
    }
  }

  llvm::APSInt Result;
  uint64_t mask = 0;
  unsigned TV = 0;
  int PtrArgNum = -1;
  bool HasConstPtr = false;
  switch (BuiltinID) {
#define GET_NEON_OVERLOAD_CHECK
#include "neverc/Foundation/arm_fp16.td.h"
#undef GET_NEON_OVERLOAD_CHECK
  }

  // For NEON intrinsics which are overloaded on vector element type, validate
  // the immediate which specifies which variant to emit.
  unsigned ImmArg = TheCall->getNumArgs() - 1;
  if (mask) {
    if (SemaBuiltinConstantArg(TheCall, ImmArg, Result))
      return true;

    TV = Result.getLimitedValue(64);
    if ((TV > 63) || (mask & (1ULL << TV)) == 0)
      return Diag(TheCall->getBeginLoc(), diag::err_invalid_neon_type_code)
             << TheCall->getArg(ImmArg)->getSourceRange();
  }

  if (PtrArgNum >= 0) {
    Expr *Arg = TheCall->getArg(PtrArgNum);
    if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Arg))
      Arg = ICE->getSubExpr();
    ExprResult RHS = DefaultFunctionArrayLvalueConversion(Arg);
    QualType RHSTy = RHS.get()->getType();

    llvm::Triple::ArchType Arch = TI.getTriple().getArch();
    bool IsPolyUnsigned = Arch == llvm::Triple::aarch64;
    bool IsInt64Long = TI.getInt64Type() == TargetInfo::SignedLong;
    QualType EltTy =
        getNeonEltType(NeonTypeFlags(TV), Context, IsPolyUnsigned, IsInt64Long);
    if (HasConstPtr)
      EltTy = EltTy.withConst();
    QualType LHSTy = Context.getPointerType(EltTy);
    AssignConvertType ConvTy;
    ConvTy = CheckSingleAssignmentConstraints(LHSTy, RHS);
    if (RHS.isInvalid())
      return true;
    if (DiagnoseAssignmentResult(ConvTy, Arg->getBeginLoc(), LHSTy, RHSTy,
                                 RHS.get(), AA_Assigning))
      return true;
  }

  // For NEON intrinsics which take an immediate value as part of the
  // instruction, range check them here.
  unsigned i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default:
    return false;
#define GET_NEON_IMMEDIATE_CHECK
#undef GET_NEON_IMMEDIATE_CHECK
  }

  return SemaBuiltinConstantArgRange(TheCall, i, l, u + l);
}

bool Sema::CheckARMBuiltinExclusiveCall(unsigned BuiltinID, CallExpr *TheCall,
                                        unsigned MaxWidth) {
  assert((BuiltinID == AArch64::BI__builtin_arm_ldrex ||
          BuiltinID == AArch64::BI__builtin_arm_ldaex ||
          BuiltinID == AArch64::BI__builtin_arm_strex ||
          BuiltinID == AArch64::BI__builtin_arm_stlex) &&
         "unexpected AArch64 builtin");
  bool IsLdrex = BuiltinID == AArch64::BI__builtin_arm_ldrex ||
                 BuiltinID == AArch64::BI__builtin_arm_ldaex;

  DeclRefExpr *DRE =
      cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());

  // Ensure that we have the proper number of arguments.
  if (checkArgCount(*this, TheCall, IsLdrex ? 1 : 2))
    return true;

  // Inspect the pointer argument of the atomic builtin.  This should always be
  // a pointer type, whose element is an integral scalar or pointer type.
  // Because it is a pointer type, we don't have to worry about any implicit
  // casts here.
  Expr *PointerArg = TheCall->getArg(IsLdrex ? 0 : 1);
  ExprResult PointerArgRes = DefaultFunctionArrayLvalueConversion(PointerArg);
  if (PointerArgRes.isInvalid())
    return true;
  PointerArg = PointerArgRes.get();

  const PointerType *pointerType = PointerArg->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return true;
  }

  // ldrex takes a "const volatile T*" and strex takes a "volatile T*". Our next
  // task is to insert the appropriate casts into the AST. First work out just
  // what the appropriate type is.
  QualType ValType = pointerType->getPointeeType();
  QualType AddrType = ValType.getUnqualifiedType().withVolatile();
  if (IsLdrex)
    AddrType.addConst();

  // Issue a warning if the cast is dodgy.
  CastKind CastNeeded = CK_NoOp;
  if (!AddrType.isAtLeastAsQualifiedAs(ValType)) {
    CastNeeded = CK_BitCast;
    Diag(DRE->getBeginLoc(), diag::ext_typecheck_convert_discards_qualifiers)
        << PointerArg->getType() << Context.getPointerType(AddrType)
        << AA_Passing << PointerArg->getSourceRange();
  }

  // Finally, do the cast and replace the argument with the corrected version.
  AddrType = Context.getPointerType(AddrType);
  PointerArgRes = ImpCastExprToType(PointerArg, AddrType, CastNeeded);
  if (PointerArgRes.isInvalid())
    return true;
  PointerArg = PointerArgRes.get();

  TheCall->setArg(IsLdrex ? 0 : 1, PointerArg);

  // In general, we allow ints, floats and pointers to be loaded and stored.
  if (!ValType->isIntegerType() && !ValType->isAnyPointerType() &&
      !ValType->isFloatingType()) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer_intfltptr)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return true;
  }

  // But AArch64 doesn't have instructions to deal with 128-bit versions.
  if (Context.getTypeSize(ValType) > MaxWidth) {
    assert(MaxWidth == 64 && "Diagnostic unexpectedly inaccurate");
    Diag(DRE->getBeginLoc(), diag::err_atomic_exclusive_builtin_pointer_size)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return true;
  }

  if (IsLdrex) {
    TheCall->setType(ValType);
    return false;
  }
  ExprResult ValArg = TheCall->getArg(0);
  InitializedEntity Entity = InitializedEntity::InitializeParameter(
      Context, ValType, /*consume*/ false);
  ValArg = PerformCopyInitialization(Entity, SourceLocation(), ValArg);
  if (ValArg.isInvalid())
    return true;
  TheCall->setArg(0, ValArg.get());

  // __builtin_arm_strex always returns an int. It's marked as such in the .def,
  // but the custom checker bypasses all default analysis.
  TheCall->setType(Context.IntTy);
  return false;
}

bool Sema::CheckAArch64BuiltinFunctionCall(const TargetInfo &TI,
                                           unsigned BuiltinID,
                                           CallExpr *TheCall) {
  if (BuiltinID == AArch64::BI__builtin_arm_ldrex ||
      BuiltinID == AArch64::BI__builtin_arm_ldaex ||
      BuiltinID == AArch64::BI__builtin_arm_strex ||
      BuiltinID == AArch64::BI__builtin_arm_stlex) {
    return CheckARMBuiltinExclusiveCall(BuiltinID, TheCall, 128);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_prefetch) {
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 1) ||
           SemaBuiltinConstantArgRange(TheCall, 2, 0, 3) ||
           SemaBuiltinConstantArgRange(TheCall, 3, 0, 1) ||
           SemaBuiltinConstantArgRange(TheCall, 4, 0, 1);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_rsr64 ||
      BuiltinID == AArch64::BI__builtin_arm_wsr64 ||
      BuiltinID == AArch64::BI__builtin_arm_rsr128 ||
      BuiltinID == AArch64::BI__builtin_arm_wsr128)
    return SemaBuiltinARMSpecialReg(BuiltinID, TheCall, 0, 5, true);

  // Memory Tagging Extensions (MTE) Intrinsics
  if (BuiltinID == AArch64::BI__builtin_arm_irg ||
      BuiltinID == AArch64::BI__builtin_arm_addg ||
      BuiltinID == AArch64::BI__builtin_arm_gmi ||
      BuiltinID == AArch64::BI__builtin_arm_ldg ||
      BuiltinID == AArch64::BI__builtin_arm_stg ||
      BuiltinID == AArch64::BI__builtin_arm_subp) {
    return SemaBuiltinARMMemoryTaggingCall(BuiltinID, TheCall);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_rsr ||
      BuiltinID == AArch64::BI__builtin_arm_rsrp ||
      BuiltinID == AArch64::BI__builtin_arm_wsr ||
      BuiltinID == AArch64::BI__builtin_arm_wsrp)
    return SemaBuiltinARMSpecialReg(BuiltinID, TheCall, 0, 5, true);

  // Only check the valid encoding range. Any constant in this range would be
  // converted to a register of the form S1_2_C3_C4_5. Let the hardware throw
  // an exception for incorrect registers. This matches MSVC behavior.
  if (BuiltinID == AArch64::BI_ReadStatusReg ||
      BuiltinID == AArch64::BI_WriteStatusReg)
    return SemaBuiltinConstantArgRange(TheCall, 0, 0, 0x7fff);

  if (BuiltinID == AArch64::BI__getReg)
    return SemaBuiltinConstantArgRange(TheCall, 0, 0, 31);

  if (BuiltinID == AArch64::BI__break)
    return SemaBuiltinConstantArgRange(TheCall, 0, 0, 0xffff);

  if (CheckNeonBuiltinFunctionCall(TI, BuiltinID, TheCall))
    return true;

  if (CheckSVEBuiltinFunctionCall(BuiltinID, TheCall))
    return true;

  if (CheckSMEBuiltinFunctionCall(BuiltinID, TheCall))
    return true;

  // For intrinsics which take an immediate value as part of the instruction,
  // range check them here.
  unsigned i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default:
    return false;
  case AArch64::BI__builtin_arm_dmb:
  case AArch64::BI__builtin_arm_dsb:
  case AArch64::BI__builtin_arm_isb:
    l = 0;
    u = 15;
    break;
  case AArch64::BI__builtin_arm_tcancel:
    l = 0;
    u = 65535;
    break;
  }

  return SemaBuiltinConstantArgRange(TheCall, i, l, u + l);
}

namespace {
bool semaBuiltinCpuSupports(Sema &S, const TargetInfo &TI, CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(0);

  if (!isa<StringLiteral>(Arg->IgnoreParenImpCasts()))
    return S.Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
           << Arg->getSourceRange();

  llvm::StringRef Feature =
      cast<StringLiteral>(Arg->IgnoreParenImpCasts())->getString();
  if (!TI.validateCpuSupports(Feature))
    return S.Diag(TheCall->getBeginLoc(), diag::err_invalid_cpu_supports)
           << Arg->getSourceRange();
  return false;
}

bool semaBuiltinCpuIs(Sema &S, const TargetInfo &TI, CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(0);

  if (!isa<StringLiteral>(Arg->IgnoreParenImpCasts()))
    return S.Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
           << Arg->getSourceRange();

  llvm::StringRef Feature =
      cast<StringLiteral>(Arg->IgnoreParenImpCasts())->getString();
  if (!TI.validateCpuIs(Feature))
    return S.Diag(TheCall->getBeginLoc(), diag::err_invalid_cpu_is)
           << Arg->getSourceRange();
  return false;
}
} // namespace

// ===----------------------------------------------------------------------===
// X86 builtin validation
// ===----------------------------------------------------------------------===

bool Sema::CheckX86BuiltinRoundingOrSAE(unsigned BuiltinID, CallExpr *TheCall) {
  // Indicates if this instruction has rounding control or just SAE.
  bool HasRC = false;

  unsigned ArgNum = 0;
  switch (BuiltinID) {
  default:
    return false;
  case X86::BI__builtin_ia32_vcvttsd2si32:
  case X86::BI__builtin_ia32_vcvttsd2si64:
  case X86::BI__builtin_ia32_vcvttsd2usi32:
  case X86::BI__builtin_ia32_vcvttsd2usi64:
  case X86::BI__builtin_ia32_vcvttss2si32:
  case X86::BI__builtin_ia32_vcvttss2si64:
  case X86::BI__builtin_ia32_vcvttss2usi32:
  case X86::BI__builtin_ia32_vcvttss2usi64:
  case X86::BI__builtin_ia32_vcvttsh2si32:
  case X86::BI__builtin_ia32_vcvttsh2si64:
  case X86::BI__builtin_ia32_vcvttsh2usi32:
  case X86::BI__builtin_ia32_vcvttsh2usi64:
    ArgNum = 1;
    break;
  case X86::BI__builtin_ia32_maxpd512:
  case X86::BI__builtin_ia32_maxps512:
  case X86::BI__builtin_ia32_minpd512:
  case X86::BI__builtin_ia32_minps512:
  case X86::BI__builtin_ia32_maxph512:
  case X86::BI__builtin_ia32_minph512:
    ArgNum = 2;
    break;
  case X86::BI__builtin_ia32_vcvtph2pd512_mask:
  case X86::BI__builtin_ia32_vcvtph2psx512_mask:
  case X86::BI__builtin_ia32_cvtps2pd512_mask:
  case X86::BI__builtin_ia32_cvttpd2dq512_mask:
  case X86::BI__builtin_ia32_cvttpd2qq512_mask:
  case X86::BI__builtin_ia32_cvttpd2udq512_mask:
  case X86::BI__builtin_ia32_cvttpd2uqq512_mask:
  case X86::BI__builtin_ia32_cvttps2dq512_mask:
  case X86::BI__builtin_ia32_cvttps2qq512_mask:
  case X86::BI__builtin_ia32_cvttps2udq512_mask:
  case X86::BI__builtin_ia32_cvttps2uqq512_mask:
  case X86::BI__builtin_ia32_vcvttph2w512_mask:
  case X86::BI__builtin_ia32_vcvttph2uw512_mask:
  case X86::BI__builtin_ia32_vcvttph2dq512_mask:
  case X86::BI__builtin_ia32_vcvttph2udq512_mask:
  case X86::BI__builtin_ia32_vcvttph2qq512_mask:
  case X86::BI__builtin_ia32_vcvttph2uqq512_mask:
  case X86::BI__builtin_ia32_exp2pd_mask:
  case X86::BI__builtin_ia32_exp2ps_mask:
  case X86::BI__builtin_ia32_getexppd512_mask:
  case X86::BI__builtin_ia32_getexpps512_mask:
  case X86::BI__builtin_ia32_getexpph512_mask:
  case X86::BI__builtin_ia32_rcp28pd_mask:
  case X86::BI__builtin_ia32_rcp28ps_mask:
  case X86::BI__builtin_ia32_rsqrt28pd_mask:
  case X86::BI__builtin_ia32_rsqrt28ps_mask:
  case X86::BI__builtin_ia32_vcomisd:
  case X86::BI__builtin_ia32_vcomiss:
  case X86::BI__builtin_ia32_vcomish:
  case X86::BI__builtin_ia32_vcvtph2ps512_mask:
    ArgNum = 3;
    break;
  case X86::BI__builtin_ia32_cmppd512_mask:
  case X86::BI__builtin_ia32_cmpps512_mask:
  case X86::BI__builtin_ia32_cmpsd_mask:
  case X86::BI__builtin_ia32_cmpss_mask:
  case X86::BI__builtin_ia32_cmpsh_mask:
  case X86::BI__builtin_ia32_vcvtsh2sd_round_mask:
  case X86::BI__builtin_ia32_vcvtsh2ss_round_mask:
  case X86::BI__builtin_ia32_cvtss2sd_round_mask:
  case X86::BI__builtin_ia32_getexpsd128_round_mask:
  case X86::BI__builtin_ia32_getexpss128_round_mask:
  case X86::BI__builtin_ia32_getexpsh128_round_mask:
  case X86::BI__builtin_ia32_getmantpd512_mask:
  case X86::BI__builtin_ia32_getmantps512_mask:
  case X86::BI__builtin_ia32_getmantph512_mask:
  case X86::BI__builtin_ia32_maxsd_round_mask:
  case X86::BI__builtin_ia32_maxss_round_mask:
  case X86::BI__builtin_ia32_maxsh_round_mask:
  case X86::BI__builtin_ia32_minsd_round_mask:
  case X86::BI__builtin_ia32_minss_round_mask:
  case X86::BI__builtin_ia32_minsh_round_mask:
  case X86::BI__builtin_ia32_rcp28sd_round_mask:
  case X86::BI__builtin_ia32_rcp28ss_round_mask:
  case X86::BI__builtin_ia32_reducepd512_mask:
  case X86::BI__builtin_ia32_reduceps512_mask:
  case X86::BI__builtin_ia32_reduceph512_mask:
  case X86::BI__builtin_ia32_rndscalepd_mask:
  case X86::BI__builtin_ia32_rndscaleps_mask:
  case X86::BI__builtin_ia32_rndscaleph_mask:
  case X86::BI__builtin_ia32_rsqrt28sd_round_mask:
  case X86::BI__builtin_ia32_rsqrt28ss_round_mask:
    ArgNum = 4;
    break;
  case X86::BI__builtin_ia32_fixupimmpd512_mask:
  case X86::BI__builtin_ia32_fixupimmpd512_maskz:
  case X86::BI__builtin_ia32_fixupimmps512_mask:
  case X86::BI__builtin_ia32_fixupimmps512_maskz:
  case X86::BI__builtin_ia32_fixupimmsd_mask:
  case X86::BI__builtin_ia32_fixupimmsd_maskz:
  case X86::BI__builtin_ia32_fixupimmss_mask:
  case X86::BI__builtin_ia32_fixupimmss_maskz:
  case X86::BI__builtin_ia32_getmantsd_round_mask:
  case X86::BI__builtin_ia32_getmantss_round_mask:
  case X86::BI__builtin_ia32_getmantsh_round_mask:
  case X86::BI__builtin_ia32_rangepd512_mask:
  case X86::BI__builtin_ia32_rangeps512_mask:
  case X86::BI__builtin_ia32_rangesd128_round_mask:
  case X86::BI__builtin_ia32_rangess128_round_mask:
  case X86::BI__builtin_ia32_reducesd_mask:
  case X86::BI__builtin_ia32_reducess_mask:
  case X86::BI__builtin_ia32_reducesh_mask:
  case X86::BI__builtin_ia32_rndscalesd_round_mask:
  case X86::BI__builtin_ia32_rndscaless_round_mask:
  case X86::BI__builtin_ia32_rndscalesh_round_mask:
    ArgNum = 5;
    break;
  case X86::BI__builtin_ia32_vcvtsd2si64:
  case X86::BI__builtin_ia32_vcvtsd2si32:
  case X86::BI__builtin_ia32_vcvtsd2usi32:
  case X86::BI__builtin_ia32_vcvtsd2usi64:
  case X86::BI__builtin_ia32_vcvtss2si32:
  case X86::BI__builtin_ia32_vcvtss2si64:
  case X86::BI__builtin_ia32_vcvtss2usi32:
  case X86::BI__builtin_ia32_vcvtss2usi64:
  case X86::BI__builtin_ia32_vcvtsh2si32:
  case X86::BI__builtin_ia32_vcvtsh2si64:
  case X86::BI__builtin_ia32_vcvtsh2usi32:
  case X86::BI__builtin_ia32_vcvtsh2usi64:
  case X86::BI__builtin_ia32_sqrtpd512:
  case X86::BI__builtin_ia32_sqrtps512:
  case X86::BI__builtin_ia32_sqrtph512:
    ArgNum = 1;
    HasRC = true;
    break;
  case X86::BI__builtin_ia32_addph512:
  case X86::BI__builtin_ia32_divph512:
  case X86::BI__builtin_ia32_mulph512:
  case X86::BI__builtin_ia32_subph512:
  case X86::BI__builtin_ia32_addpd512:
  case X86::BI__builtin_ia32_addps512:
  case X86::BI__builtin_ia32_divpd512:
  case X86::BI__builtin_ia32_divps512:
  case X86::BI__builtin_ia32_mulpd512:
  case X86::BI__builtin_ia32_mulps512:
  case X86::BI__builtin_ia32_subpd512:
  case X86::BI__builtin_ia32_subps512:
  case X86::BI__builtin_ia32_cvtsi2sd64:
  case X86::BI__builtin_ia32_cvtsi2ss32:
  case X86::BI__builtin_ia32_cvtsi2ss64:
  case X86::BI__builtin_ia32_cvtusi2sd64:
  case X86::BI__builtin_ia32_cvtusi2ss32:
  case X86::BI__builtin_ia32_cvtusi2ss64:
  case X86::BI__builtin_ia32_vcvtusi2sh:
  case X86::BI__builtin_ia32_vcvtusi642sh:
  case X86::BI__builtin_ia32_vcvtsi2sh:
  case X86::BI__builtin_ia32_vcvtsi642sh:
    ArgNum = 2;
    HasRC = true;
    break;
  case X86::BI__builtin_ia32_cvtdq2ps512_mask:
  case X86::BI__builtin_ia32_cvtudq2ps512_mask:
  case X86::BI__builtin_ia32_vcvtpd2ph512_mask:
  case X86::BI__builtin_ia32_vcvtps2phx512_mask:
  case X86::BI__builtin_ia32_cvtpd2ps512_mask:
  case X86::BI__builtin_ia32_cvtpd2dq512_mask:
  case X86::BI__builtin_ia32_cvtpd2qq512_mask:
  case X86::BI__builtin_ia32_cvtpd2udq512_mask:
  case X86::BI__builtin_ia32_cvtpd2uqq512_mask:
  case X86::BI__builtin_ia32_cvtps2dq512_mask:
  case X86::BI__builtin_ia32_cvtps2qq512_mask:
  case X86::BI__builtin_ia32_cvtps2udq512_mask:
  case X86::BI__builtin_ia32_cvtps2uqq512_mask:
  case X86::BI__builtin_ia32_cvtqq2pd512_mask:
  case X86::BI__builtin_ia32_cvtqq2ps512_mask:
  case X86::BI__builtin_ia32_cvtuqq2pd512_mask:
  case X86::BI__builtin_ia32_cvtuqq2ps512_mask:
  case X86::BI__builtin_ia32_vcvtdq2ph512_mask:
  case X86::BI__builtin_ia32_vcvtudq2ph512_mask:
  case X86::BI__builtin_ia32_vcvtw2ph512_mask:
  case X86::BI__builtin_ia32_vcvtuw2ph512_mask:
  case X86::BI__builtin_ia32_vcvtph2w512_mask:
  case X86::BI__builtin_ia32_vcvtph2uw512_mask:
  case X86::BI__builtin_ia32_vcvtph2dq512_mask:
  case X86::BI__builtin_ia32_vcvtph2udq512_mask:
  case X86::BI__builtin_ia32_vcvtph2qq512_mask:
  case X86::BI__builtin_ia32_vcvtph2uqq512_mask:
  case X86::BI__builtin_ia32_vcvtqq2ph512_mask:
  case X86::BI__builtin_ia32_vcvtuqq2ph512_mask:
    ArgNum = 3;
    HasRC = true;
    break;
  case X86::BI__builtin_ia32_addsh_round_mask:
  case X86::BI__builtin_ia32_addss_round_mask:
  case X86::BI__builtin_ia32_addsd_round_mask:
  case X86::BI__builtin_ia32_divsh_round_mask:
  case X86::BI__builtin_ia32_divss_round_mask:
  case X86::BI__builtin_ia32_divsd_round_mask:
  case X86::BI__builtin_ia32_mulsh_round_mask:
  case X86::BI__builtin_ia32_mulss_round_mask:
  case X86::BI__builtin_ia32_mulsd_round_mask:
  case X86::BI__builtin_ia32_subsh_round_mask:
  case X86::BI__builtin_ia32_subss_round_mask:
  case X86::BI__builtin_ia32_subsd_round_mask:
  case X86::BI__builtin_ia32_scalefph512_mask:
  case X86::BI__builtin_ia32_scalefpd512_mask:
  case X86::BI__builtin_ia32_scalefps512_mask:
  case X86::BI__builtin_ia32_scalefsd_round_mask:
  case X86::BI__builtin_ia32_scalefss_round_mask:
  case X86::BI__builtin_ia32_scalefsh_round_mask:
  case X86::BI__builtin_ia32_cvtsd2ss_round_mask:
  case X86::BI__builtin_ia32_vcvtss2sh_round_mask:
  case X86::BI__builtin_ia32_vcvtsd2sh_round_mask:
  case X86::BI__builtin_ia32_sqrtsd_round_mask:
  case X86::BI__builtin_ia32_sqrtss_round_mask:
  case X86::BI__builtin_ia32_sqrtsh_round_mask:
  case X86::BI__builtin_ia32_vfmaddsd3_mask:
  case X86::BI__builtin_ia32_vfmaddsd3_maskz:
  case X86::BI__builtin_ia32_vfmaddsd3_mask3:
  case X86::BI__builtin_ia32_vfmaddss3_mask:
  case X86::BI__builtin_ia32_vfmaddss3_maskz:
  case X86::BI__builtin_ia32_vfmaddss3_mask3:
  case X86::BI__builtin_ia32_vfmaddsh3_mask:
  case X86::BI__builtin_ia32_vfmaddsh3_maskz:
  case X86::BI__builtin_ia32_vfmaddsh3_mask3:
  case X86::BI__builtin_ia32_vfmaddpd512_mask:
  case X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubpd512_mask3:
  case X86::BI__builtin_ia32_vfmaddps512_mask:
  case X86::BI__builtin_ia32_vfmaddps512_maskz:
  case X86::BI__builtin_ia32_vfmaddps512_mask3:
  case X86::BI__builtin_ia32_vfmsubps512_mask3:
  case X86::BI__builtin_ia32_vfmaddph512_mask:
  case X86::BI__builtin_ia32_vfmaddph512_maskz:
  case X86::BI__builtin_ia32_vfmaddph512_mask3:
  case X86::BI__builtin_ia32_vfmsubph512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask:
  case X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddps512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubph512_mask:
  case X86::BI__builtin_ia32_vfmaddsubph512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubph512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddph512_mask3:
  case X86::BI__builtin_ia32_vfmaddcsh_mask:
  case X86::BI__builtin_ia32_vfmaddcsh_round_mask:
  case X86::BI__builtin_ia32_vfmaddcsh_round_mask3:
  case X86::BI__builtin_ia32_vfmaddcph512_mask:
  case X86::BI__builtin_ia32_vfmaddcph512_maskz:
  case X86::BI__builtin_ia32_vfmaddcph512_mask3:
  case X86::BI__builtin_ia32_vfcmaddcsh_mask:
  case X86::BI__builtin_ia32_vfcmaddcsh_round_mask:
  case X86::BI__builtin_ia32_vfcmaddcsh_round_mask3:
  case X86::BI__builtin_ia32_vfcmaddcph512_mask:
  case X86::BI__builtin_ia32_vfcmaddcph512_maskz:
  case X86::BI__builtin_ia32_vfcmaddcph512_mask3:
  case X86::BI__builtin_ia32_vfmulcsh_mask:
  case X86::BI__builtin_ia32_vfmulcph512_mask:
  case X86::BI__builtin_ia32_vfcmulcsh_mask:
  case X86::BI__builtin_ia32_vfcmulcph512_mask:
    ArgNum = 4;
    HasRC = true;
    break;
  }

  llvm::APSInt Result;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  // Make sure rounding mode is either ROUND_CUR_DIRECTION or ROUND_NO_EXC bit
  // is set. If the intrinsic has rounding control(bits 1:0), make sure its only
  // combined with ROUND_NO_EXC. If the intrinsic does not have rounding
  // control, allow ROUND_NO_EXC and ROUND_CUR_DIRECTION together.
  if (Result == 4 /*ROUND_CUR_DIRECTION*/ || Result == 8 /*ROUND_NO_EXC*/ ||
      (!HasRC && Result == 12 /*ROUND_CUR_DIRECTION|ROUND_NO_EXC*/) ||
      (HasRC && Result.getZExtValue() >= 8 && Result.getZExtValue() <= 11))
    return false;

  return Diag(TheCall->getBeginLoc(), diag::err_x86_builtin_invalid_rounding)
         << Arg->getSourceRange();
}

// Check if the gather/scatter scale is legal.
bool Sema::CheckX86BuiltinGatherScatterScale(unsigned BuiltinID,
                                             CallExpr *TheCall) {
  unsigned ArgNum = 0;
  switch (BuiltinID) {
  default:
    return false;
  case X86::BI__builtin_ia32_gatherpfdpd:
  case X86::BI__builtin_ia32_gatherpfdps:
  case X86::BI__builtin_ia32_gatherpfqpd:
  case X86::BI__builtin_ia32_gatherpfqps:
  case X86::BI__builtin_ia32_scatterpfdpd:
  case X86::BI__builtin_ia32_scatterpfdps:
  case X86::BI__builtin_ia32_scatterpfqpd:
  case X86::BI__builtin_ia32_scatterpfqps:
    ArgNum = 3;
    break;
  case X86::BI__builtin_ia32_gatherd_pd:
  case X86::BI__builtin_ia32_gatherd_pd256:
  case X86::BI__builtin_ia32_gatherq_pd:
  case X86::BI__builtin_ia32_gatherq_pd256:
  case X86::BI__builtin_ia32_gatherd_ps:
  case X86::BI__builtin_ia32_gatherd_ps256:
  case X86::BI__builtin_ia32_gatherq_ps:
  case X86::BI__builtin_ia32_gatherq_ps256:
  case X86::BI__builtin_ia32_gatherd_q:
  case X86::BI__builtin_ia32_gatherd_q256:
  case X86::BI__builtin_ia32_gatherq_q:
  case X86::BI__builtin_ia32_gatherq_q256:
  case X86::BI__builtin_ia32_gatherd_d:
  case X86::BI__builtin_ia32_gatherd_d256:
  case X86::BI__builtin_ia32_gatherq_d:
  case X86::BI__builtin_ia32_gatherq_d256:
  case X86::BI__builtin_ia32_gather3div2df:
  case X86::BI__builtin_ia32_gather3div2di:
  case X86::BI__builtin_ia32_gather3div4df:
  case X86::BI__builtin_ia32_gather3div4di:
  case X86::BI__builtin_ia32_gather3div4sf:
  case X86::BI__builtin_ia32_gather3div4si:
  case X86::BI__builtin_ia32_gather3div8sf:
  case X86::BI__builtin_ia32_gather3div8si:
  case X86::BI__builtin_ia32_gather3siv2df:
  case X86::BI__builtin_ia32_gather3siv2di:
  case X86::BI__builtin_ia32_gather3siv4df:
  case X86::BI__builtin_ia32_gather3siv4di:
  case X86::BI__builtin_ia32_gather3siv4sf:
  case X86::BI__builtin_ia32_gather3siv4si:
  case X86::BI__builtin_ia32_gather3siv8sf:
  case X86::BI__builtin_ia32_gather3siv8si:
  case X86::BI__builtin_ia32_gathersiv8df:
  case X86::BI__builtin_ia32_gathersiv16sf:
  case X86::BI__builtin_ia32_gatherdiv8df:
  case X86::BI__builtin_ia32_gatherdiv16sf:
  case X86::BI__builtin_ia32_gathersiv8di:
  case X86::BI__builtin_ia32_gathersiv16si:
  case X86::BI__builtin_ia32_gatherdiv8di:
  case X86::BI__builtin_ia32_gatherdiv16si:
  case X86::BI__builtin_ia32_scatterdiv2df:
  case X86::BI__builtin_ia32_scatterdiv2di:
  case X86::BI__builtin_ia32_scatterdiv4df:
  case X86::BI__builtin_ia32_scatterdiv4di:
  case X86::BI__builtin_ia32_scatterdiv4sf:
  case X86::BI__builtin_ia32_scatterdiv4si:
  case X86::BI__builtin_ia32_scatterdiv8sf:
  case X86::BI__builtin_ia32_scatterdiv8si:
  case X86::BI__builtin_ia32_scattersiv2df:
  case X86::BI__builtin_ia32_scattersiv2di:
  case X86::BI__builtin_ia32_scattersiv4df:
  case X86::BI__builtin_ia32_scattersiv4di:
  case X86::BI__builtin_ia32_scattersiv4sf:
  case X86::BI__builtin_ia32_scattersiv4si:
  case X86::BI__builtin_ia32_scattersiv8sf:
  case X86::BI__builtin_ia32_scattersiv8si:
  case X86::BI__builtin_ia32_scattersiv8df:
  case X86::BI__builtin_ia32_scattersiv16sf:
  case X86::BI__builtin_ia32_scatterdiv8df:
  case X86::BI__builtin_ia32_scatterdiv16sf:
  case X86::BI__builtin_ia32_scattersiv8di:
  case X86::BI__builtin_ia32_scattersiv16si:
  case X86::BI__builtin_ia32_scatterdiv8di:
  case X86::BI__builtin_ia32_scatterdiv16si:
    ArgNum = 4;
    break;
  }

  llvm::APSInt Result;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  if (Result == 1 || Result == 2 || Result == 4 || Result == 8)
    return false;

  return Diag(TheCall->getBeginLoc(), diag::err_x86_builtin_invalid_scale)
         << Arg->getSourceRange();
}

enum { TileRegLow = 0, TileRegHigh = 7 };

bool Sema::CheckX86BuiltinTileArgumentsRange(CallExpr *TheCall,
                                             llvm::ArrayRef<int> ArgNums) {
  for (int ArgNum : ArgNums) {
    if (SemaBuiltinConstantArgRange(TheCall, ArgNum, TileRegLow, TileRegHigh))
      return true;
  }
  return false;
}

bool Sema::CheckX86BuiltinTileDuplicate(CallExpr *TheCall,
                                        llvm::ArrayRef<int> ArgNums) {
  // Because the max number of tile register is TileRegHigh + 1, so here we use
  // each bit to represent the usage of them in bitset.
  std::bitset<TileRegHigh + 1> ArgValues;
  for (int ArgNum : ArgNums) {
    llvm::APSInt Result;
    if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
      return true;
    int ArgExtValue = Result.getExtValue();
    assert((ArgExtValue >= TileRegLow && ArgExtValue <= TileRegHigh) &&
           "Incorrect tile register num.");
    if (ArgValues.test(ArgExtValue))
      return Diag(TheCall->getBeginLoc(),
                  diag::err_x86_builtin_tile_arg_duplicate)
             << TheCall->getArg(ArgNum)->getSourceRange();
    ArgValues.set(ArgExtValue);
  }
  return false;
}

bool Sema::CheckX86BuiltinTileRangeAndDuplicate(CallExpr *TheCall,
                                                llvm::ArrayRef<int> ArgNums) {
  return CheckX86BuiltinTileArgumentsRange(TheCall, ArgNums) ||
         CheckX86BuiltinTileDuplicate(TheCall, ArgNums);
}

bool Sema::CheckX86BuiltinTileArguments(unsigned BuiltinID, CallExpr *TheCall) {
  switch (BuiltinID) {
  default:
    return false;
  case X86::BI__builtin_ia32_tileloadd64:
  case X86::BI__builtin_ia32_tileloaddt164:
  case X86::BI__builtin_ia32_tilestored64:
  case X86::BI__builtin_ia32_tilezero:
    return CheckX86BuiltinTileArgumentsRange(TheCall, 0);
  case X86::BI__builtin_ia32_tdpbssd:
  case X86::BI__builtin_ia32_tdpbsud:
  case X86::BI__builtin_ia32_tdpbusd:
  case X86::BI__builtin_ia32_tdpbuud:
  case X86::BI__builtin_ia32_tdpbf16ps:
  case X86::BI__builtin_ia32_tdpfp16ps:
  case X86::BI__builtin_ia32_tcmmimfp16ps:
  case X86::BI__builtin_ia32_tcmmrlfp16ps:
    return CheckX86BuiltinTileRangeAndDuplicate(TheCall, {0, 1, 2});
  }
}
bool Sema::CheckX86BuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                       CallExpr *TheCall) {
  if (BuiltinID == X86::BI__builtin_cpu_supports)
    return semaBuiltinCpuSupports(*this, TI, TheCall);

  if (BuiltinID == X86::BI__builtin_cpu_is)
    return semaBuiltinCpuIs(*this, TI, TheCall);

  // If the intrinsic has rounding or SAE make sure its valid.
  if (CheckX86BuiltinRoundingOrSAE(BuiltinID, TheCall))
    return true;

  // If the intrinsic has a gather/scatter scale immediate make sure its valid.
  if (CheckX86BuiltinGatherScatterScale(BuiltinID, TheCall))
    return true;

  // If the intrinsic has a tile arguments, make sure they are valid.
  if (CheckX86BuiltinTileArguments(BuiltinID, TheCall))
    return true;

  // For intrinsics which take an immediate value as part of the instruction,
  // range check them here.
  int i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default:
    return false;
  case X86::BI__builtin_ia32_vec_ext_v2si:
  case X86::BI__builtin_ia32_vec_ext_v2di:
  case X86::BI__builtin_ia32_vextractf128_pd256:
  case X86::BI__builtin_ia32_vextractf128_ps256:
  case X86::BI__builtin_ia32_vextractf128_si256:
  case X86::BI__builtin_ia32_extract128i256:
  case X86::BI__builtin_ia32_extractf64x4_mask:
  case X86::BI__builtin_ia32_extracti64x4_mask:
  case X86::BI__builtin_ia32_extractf32x8_mask:
  case X86::BI__builtin_ia32_extracti32x8_mask:
  case X86::BI__builtin_ia32_extractf64x2_256_mask:
  case X86::BI__builtin_ia32_extracti64x2_256_mask:
  case X86::BI__builtin_ia32_extractf32x4_256_mask:
  case X86::BI__builtin_ia32_extracti32x4_256_mask:
    i = 1;
    l = 0;
    u = 1;
    break;
  case X86::BI__builtin_ia32_vec_set_v2di:
  case X86::BI__builtin_ia32_vinsertf128_pd256:
  case X86::BI__builtin_ia32_vinsertf128_ps256:
  case X86::BI__builtin_ia32_vinsertf128_si256:
  case X86::BI__builtin_ia32_insert128i256:
  case X86::BI__builtin_ia32_insertf32x8:
  case X86::BI__builtin_ia32_inserti32x8:
  case X86::BI__builtin_ia32_insertf64x4:
  case X86::BI__builtin_ia32_inserti64x4:
  case X86::BI__builtin_ia32_insertf64x2_256:
  case X86::BI__builtin_ia32_inserti64x2_256:
  case X86::BI__builtin_ia32_insertf32x4_256:
  case X86::BI__builtin_ia32_inserti32x4_256:
    i = 2;
    l = 0;
    u = 1;
    break;
  case X86::BI__builtin_ia32_vpermilpd:
  case X86::BI__builtin_ia32_vec_ext_v4hi:
  case X86::BI__builtin_ia32_vec_ext_v4si:
  case X86::BI__builtin_ia32_vec_ext_v4sf:
  case X86::BI__builtin_ia32_vec_ext_v4di:
  case X86::BI__builtin_ia32_extractf32x4_mask:
  case X86::BI__builtin_ia32_extracti32x4_mask:
  case X86::BI__builtin_ia32_extractf64x2_512_mask:
  case X86::BI__builtin_ia32_extracti64x2_512_mask:
    i = 1;
    l = 0;
    u = 3;
    break;
  case X86::BI_mm_prefetch:
  case X86::BI__builtin_ia32_vec_ext_v8hi:
  case X86::BI__builtin_ia32_vec_ext_v8si:
    i = 1;
    l = 0;
    u = 7;
    break;
  case X86::BI__builtin_ia32_sha1rnds4:
  case X86::BI__builtin_ia32_blendpd:
  case X86::BI__builtin_ia32_shufpd:
  case X86::BI__builtin_ia32_vec_set_v4hi:
  case X86::BI__builtin_ia32_vec_set_v4si:
  case X86::BI__builtin_ia32_vec_set_v4di:
  case X86::BI__builtin_ia32_shuf_f32x4_256:
  case X86::BI__builtin_ia32_shuf_f64x2_256:
  case X86::BI__builtin_ia32_shuf_i32x4_256:
  case X86::BI__builtin_ia32_shuf_i64x2_256:
  case X86::BI__builtin_ia32_insertf64x2_512:
  case X86::BI__builtin_ia32_inserti64x2_512:
  case X86::BI__builtin_ia32_insertf32x4:
  case X86::BI__builtin_ia32_inserti32x4:
    i = 2;
    l = 0;
    u = 3;
    break;
  case X86::BI__builtin_ia32_vpermil2pd:
  case X86::BI__builtin_ia32_vpermil2pd256:
  case X86::BI__builtin_ia32_vpermil2ps:
  case X86::BI__builtin_ia32_vpermil2ps256:
    i = 3;
    l = 0;
    u = 3;
    break;
  case X86::BI__builtin_ia32_cmpb128_mask:
  case X86::BI__builtin_ia32_cmpw128_mask:
  case X86::BI__builtin_ia32_cmpd128_mask:
  case X86::BI__builtin_ia32_cmpq128_mask:
  case X86::BI__builtin_ia32_cmpb256_mask:
  case X86::BI__builtin_ia32_cmpw256_mask:
  case X86::BI__builtin_ia32_cmpd256_mask:
  case X86::BI__builtin_ia32_cmpq256_mask:
  case X86::BI__builtin_ia32_cmpb512_mask:
  case X86::BI__builtin_ia32_cmpw512_mask:
  case X86::BI__builtin_ia32_cmpd512_mask:
  case X86::BI__builtin_ia32_cmpq512_mask:
  case X86::BI__builtin_ia32_ucmpb128_mask:
  case X86::BI__builtin_ia32_ucmpw128_mask:
  case X86::BI__builtin_ia32_ucmpd128_mask:
  case X86::BI__builtin_ia32_ucmpq128_mask:
  case X86::BI__builtin_ia32_ucmpb256_mask:
  case X86::BI__builtin_ia32_ucmpw256_mask:
  case X86::BI__builtin_ia32_ucmpd256_mask:
  case X86::BI__builtin_ia32_ucmpq256_mask:
  case X86::BI__builtin_ia32_ucmpb512_mask:
  case X86::BI__builtin_ia32_ucmpw512_mask:
  case X86::BI__builtin_ia32_ucmpd512_mask:
  case X86::BI__builtin_ia32_ucmpq512_mask:
  case X86::BI__builtin_ia32_vpcomub:
  case X86::BI__builtin_ia32_vpcomuw:
  case X86::BI__builtin_ia32_vpcomud:
  case X86::BI__builtin_ia32_vpcomuq:
  case X86::BI__builtin_ia32_vpcomb:
  case X86::BI__builtin_ia32_vpcomw:
  case X86::BI__builtin_ia32_vpcomd:
  case X86::BI__builtin_ia32_vpcomq:
  case X86::BI__builtin_ia32_vec_set_v8hi:
  case X86::BI__builtin_ia32_vec_set_v8si:
    i = 2;
    l = 0;
    u = 7;
    break;
  case X86::BI__builtin_ia32_vpermilpd256:
  case X86::BI__builtin_ia32_roundps:
  case X86::BI__builtin_ia32_roundpd:
  case X86::BI__builtin_ia32_roundps256:
  case X86::BI__builtin_ia32_roundpd256:
  case X86::BI__builtin_ia32_getmantpd128_mask:
  case X86::BI__builtin_ia32_getmantpd256_mask:
  case X86::BI__builtin_ia32_getmantps128_mask:
  case X86::BI__builtin_ia32_getmantps256_mask:
  case X86::BI__builtin_ia32_getmantpd512_mask:
  case X86::BI__builtin_ia32_getmantps512_mask:
  case X86::BI__builtin_ia32_getmantph128_mask:
  case X86::BI__builtin_ia32_getmantph256_mask:
  case X86::BI__builtin_ia32_getmantph512_mask:
  case X86::BI__builtin_ia32_vec_ext_v16qi:
  case X86::BI__builtin_ia32_vec_ext_v16hi:
    i = 1;
    l = 0;
    u = 15;
    break;
  case X86::BI__builtin_ia32_pblendd128:
  case X86::BI__builtin_ia32_blendps:
  case X86::BI__builtin_ia32_blendpd256:
  case X86::BI__builtin_ia32_shufpd256:
  case X86::BI__builtin_ia32_roundss:
  case X86::BI__builtin_ia32_roundsd:
  case X86::BI__builtin_ia32_rangepd128_mask:
  case X86::BI__builtin_ia32_rangepd256_mask:
  case X86::BI__builtin_ia32_rangepd512_mask:
  case X86::BI__builtin_ia32_rangeps128_mask:
  case X86::BI__builtin_ia32_rangeps256_mask:
  case X86::BI__builtin_ia32_rangeps512_mask:
  case X86::BI__builtin_ia32_getmantsd_round_mask:
  case X86::BI__builtin_ia32_getmantss_round_mask:
  case X86::BI__builtin_ia32_getmantsh_round_mask:
  case X86::BI__builtin_ia32_vec_set_v16qi:
  case X86::BI__builtin_ia32_vec_set_v16hi:
    i = 2;
    l = 0;
    u = 15;
    break;
  case X86::BI__builtin_ia32_vec_ext_v32qi:
    i = 1;
    l = 0;
    u = 31;
    break;
  case X86::BI__builtin_ia32_cmpps:
  case X86::BI__builtin_ia32_cmpss:
  case X86::BI__builtin_ia32_cmppd:
  case X86::BI__builtin_ia32_cmpsd:
  case X86::BI__builtin_ia32_cmpps256:
  case X86::BI__builtin_ia32_cmppd256:
  case X86::BI__builtin_ia32_cmpps128_mask:
  case X86::BI__builtin_ia32_cmppd128_mask:
  case X86::BI__builtin_ia32_cmpps256_mask:
  case X86::BI__builtin_ia32_cmppd256_mask:
  case X86::BI__builtin_ia32_cmpps512_mask:
  case X86::BI__builtin_ia32_cmppd512_mask:
  case X86::BI__builtin_ia32_cmpsd_mask:
  case X86::BI__builtin_ia32_cmpss_mask:
  case X86::BI__builtin_ia32_vec_set_v32qi:
    i = 2;
    l = 0;
    u = 31;
    break;
  case X86::BI__builtin_ia32_permdf256:
  case X86::BI__builtin_ia32_permdi256:
  case X86::BI__builtin_ia32_permdf512:
  case X86::BI__builtin_ia32_permdi512:
  case X86::BI__builtin_ia32_vpermilps:
  case X86::BI__builtin_ia32_vpermilps256:
  case X86::BI__builtin_ia32_vpermilpd512:
  case X86::BI__builtin_ia32_vpermilps512:
  case X86::BI__builtin_ia32_pshufd:
  case X86::BI__builtin_ia32_pshufd256:
  case X86::BI__builtin_ia32_pshufd512:
  case X86::BI__builtin_ia32_pshufhw:
  case X86::BI__builtin_ia32_pshufhw256:
  case X86::BI__builtin_ia32_pshufhw512:
  case X86::BI__builtin_ia32_pshuflw:
  case X86::BI__builtin_ia32_pshuflw256:
  case X86::BI__builtin_ia32_pshuflw512:
  case X86::BI__builtin_ia32_vcvtps2ph:
  case X86::BI__builtin_ia32_vcvtps2ph_mask:
  case X86::BI__builtin_ia32_vcvtps2ph256:
  case X86::BI__builtin_ia32_vcvtps2ph256_mask:
  case X86::BI__builtin_ia32_vcvtps2ph512_mask:
  case X86::BI__builtin_ia32_rndscaleps_128_mask:
  case X86::BI__builtin_ia32_rndscalepd_128_mask:
  case X86::BI__builtin_ia32_rndscaleps_256_mask:
  case X86::BI__builtin_ia32_rndscalepd_256_mask:
  case X86::BI__builtin_ia32_rndscaleps_mask:
  case X86::BI__builtin_ia32_rndscalepd_mask:
  case X86::BI__builtin_ia32_rndscaleph_mask:
  case X86::BI__builtin_ia32_reducepd128_mask:
  case X86::BI__builtin_ia32_reducepd256_mask:
  case X86::BI__builtin_ia32_reducepd512_mask:
  case X86::BI__builtin_ia32_reduceps128_mask:
  case X86::BI__builtin_ia32_reduceps256_mask:
  case X86::BI__builtin_ia32_reduceps512_mask:
  case X86::BI__builtin_ia32_reduceph128_mask:
  case X86::BI__builtin_ia32_reduceph256_mask:
  case X86::BI__builtin_ia32_reduceph512_mask:
  case X86::BI__builtin_ia32_prold512:
  case X86::BI__builtin_ia32_prolq512:
  case X86::BI__builtin_ia32_prold128:
  case X86::BI__builtin_ia32_prold256:
  case X86::BI__builtin_ia32_prolq128:
  case X86::BI__builtin_ia32_prolq256:
  case X86::BI__builtin_ia32_prord512:
  case X86::BI__builtin_ia32_prorq512:
  case X86::BI__builtin_ia32_prord128:
  case X86::BI__builtin_ia32_prord256:
  case X86::BI__builtin_ia32_prorq128:
  case X86::BI__builtin_ia32_prorq256:
  case X86::BI__builtin_ia32_fpclasspd128_mask:
  case X86::BI__builtin_ia32_fpclasspd256_mask:
  case X86::BI__builtin_ia32_fpclassps128_mask:
  case X86::BI__builtin_ia32_fpclassps256_mask:
  case X86::BI__builtin_ia32_fpclassps512_mask:
  case X86::BI__builtin_ia32_fpclasspd512_mask:
  case X86::BI__builtin_ia32_fpclassph128_mask:
  case X86::BI__builtin_ia32_fpclassph256_mask:
  case X86::BI__builtin_ia32_fpclassph512_mask:
  case X86::BI__builtin_ia32_fpclasssd_mask:
  case X86::BI__builtin_ia32_fpclassss_mask:
  case X86::BI__builtin_ia32_fpclasssh_mask:
  case X86::BI__builtin_ia32_pslldqi128_byteshift:
  case X86::BI__builtin_ia32_pslldqi256_byteshift:
  case X86::BI__builtin_ia32_pslldqi512_byteshift:
  case X86::BI__builtin_ia32_psrldqi128_byteshift:
  case X86::BI__builtin_ia32_psrldqi256_byteshift:
  case X86::BI__builtin_ia32_psrldqi512_byteshift:
  case X86::BI__builtin_ia32_kshiftliqi:
  case X86::BI__builtin_ia32_kshiftlihi:
  case X86::BI__builtin_ia32_kshiftlisi:
  case X86::BI__builtin_ia32_kshiftlidi:
  case X86::BI__builtin_ia32_kshiftriqi:
  case X86::BI__builtin_ia32_kshiftrihi:
  case X86::BI__builtin_ia32_kshiftrisi:
  case X86::BI__builtin_ia32_kshiftridi:
    i = 1;
    l = 0;
    u = 255;
    break;
  case X86::BI__builtin_ia32_vperm2f128_pd256:
  case X86::BI__builtin_ia32_vperm2f128_ps256:
  case X86::BI__builtin_ia32_vperm2f128_si256:
  case X86::BI__builtin_ia32_permti256:
  case X86::BI__builtin_ia32_pblendw128:
  case X86::BI__builtin_ia32_pblendw256:
  case X86::BI__builtin_ia32_blendps256:
  case X86::BI__builtin_ia32_pblendd256:
  case X86::BI__builtin_ia32_palignr128:
  case X86::BI__builtin_ia32_palignr256:
  case X86::BI__builtin_ia32_palignr512:
  case X86::BI__builtin_ia32_alignq512:
  case X86::BI__builtin_ia32_alignd512:
  case X86::BI__builtin_ia32_alignd128:
  case X86::BI__builtin_ia32_alignd256:
  case X86::BI__builtin_ia32_alignq128:
  case X86::BI__builtin_ia32_alignq256:
  case X86::BI__builtin_ia32_vcomisd:
  case X86::BI__builtin_ia32_vcomiss:
  case X86::BI__builtin_ia32_shuf_f32x4:
  case X86::BI__builtin_ia32_shuf_f64x2:
  case X86::BI__builtin_ia32_shuf_i32x4:
  case X86::BI__builtin_ia32_shuf_i64x2:
  case X86::BI__builtin_ia32_shufpd512:
  case X86::BI__builtin_ia32_shufps:
  case X86::BI__builtin_ia32_shufps256:
  case X86::BI__builtin_ia32_shufps512:
  case X86::BI__builtin_ia32_dbpsadbw128:
  case X86::BI__builtin_ia32_dbpsadbw256:
  case X86::BI__builtin_ia32_dbpsadbw512:
  case X86::BI__builtin_ia32_vpshldd128:
  case X86::BI__builtin_ia32_vpshldd256:
  case X86::BI__builtin_ia32_vpshldd512:
  case X86::BI__builtin_ia32_vpshldq128:
  case X86::BI__builtin_ia32_vpshldq256:
  case X86::BI__builtin_ia32_vpshldq512:
  case X86::BI__builtin_ia32_vpshldw128:
  case X86::BI__builtin_ia32_vpshldw256:
  case X86::BI__builtin_ia32_vpshldw512:
  case X86::BI__builtin_ia32_vpshrdd128:
  case X86::BI__builtin_ia32_vpshrdd256:
  case X86::BI__builtin_ia32_vpshrdd512:
  case X86::BI__builtin_ia32_vpshrdq128:
  case X86::BI__builtin_ia32_vpshrdq256:
  case X86::BI__builtin_ia32_vpshrdq512:
  case X86::BI__builtin_ia32_vpshrdw128:
  case X86::BI__builtin_ia32_vpshrdw256:
  case X86::BI__builtin_ia32_vpshrdw512:
    i = 2;
    l = 0;
    u = 255;
    break;
  case X86::BI__builtin_ia32_fixupimmpd512_mask:
  case X86::BI__builtin_ia32_fixupimmpd512_maskz:
  case X86::BI__builtin_ia32_fixupimmps512_mask:
  case X86::BI__builtin_ia32_fixupimmps512_maskz:
  case X86::BI__builtin_ia32_fixupimmsd_mask:
  case X86::BI__builtin_ia32_fixupimmsd_maskz:
  case X86::BI__builtin_ia32_fixupimmss_mask:
  case X86::BI__builtin_ia32_fixupimmss_maskz:
  case X86::BI__builtin_ia32_fixupimmpd128_mask:
  case X86::BI__builtin_ia32_fixupimmpd128_maskz:
  case X86::BI__builtin_ia32_fixupimmpd256_mask:
  case X86::BI__builtin_ia32_fixupimmpd256_maskz:
  case X86::BI__builtin_ia32_fixupimmps128_mask:
  case X86::BI__builtin_ia32_fixupimmps128_maskz:
  case X86::BI__builtin_ia32_fixupimmps256_mask:
  case X86::BI__builtin_ia32_fixupimmps256_maskz:
  case X86::BI__builtin_ia32_pternlogd512_mask:
  case X86::BI__builtin_ia32_pternlogd512_maskz:
  case X86::BI__builtin_ia32_pternlogq512_mask:
  case X86::BI__builtin_ia32_pternlogq512_maskz:
  case X86::BI__builtin_ia32_pternlogd128_mask:
  case X86::BI__builtin_ia32_pternlogd128_maskz:
  case X86::BI__builtin_ia32_pternlogd256_mask:
  case X86::BI__builtin_ia32_pternlogd256_maskz:
  case X86::BI__builtin_ia32_pternlogq128_mask:
  case X86::BI__builtin_ia32_pternlogq128_maskz:
  case X86::BI__builtin_ia32_pternlogq256_mask:
  case X86::BI__builtin_ia32_pternlogq256_maskz:
  case X86::BI__builtin_ia32_vsm3rnds2:
    i = 3;
    l = 0;
    u = 255;
    break;
  case X86::BI__builtin_ia32_gatherpfdpd:
  case X86::BI__builtin_ia32_gatherpfdps:
  case X86::BI__builtin_ia32_gatherpfqpd:
  case X86::BI__builtin_ia32_gatherpfqps:
  case X86::BI__builtin_ia32_scatterpfdpd:
  case X86::BI__builtin_ia32_scatterpfdps:
  case X86::BI__builtin_ia32_scatterpfqpd:
  case X86::BI__builtin_ia32_scatterpfqps:
    i = 4;
    l = 2;
    u = 3;
    break;
  case X86::BI__builtin_ia32_reducesd_mask:
  case X86::BI__builtin_ia32_reducess_mask:
  case X86::BI__builtin_ia32_rndscalesd_round_mask:
  case X86::BI__builtin_ia32_rndscaless_round_mask:
  case X86::BI__builtin_ia32_rndscalesh_round_mask:
  case X86::BI__builtin_ia32_reducesh_mask:
    i = 4;
    l = 0;
    u = 255;
    break;
  case X86::BI__builtin_ia32_cmpccxadd32:
  case X86::BI__builtin_ia32_cmpccxadd64:
    i = 3;
    l = 0;
    u = 15;
    break;
  }

  // Note that we don't force a hard error on the range check here, allowing
  // macro-generated dead code to potentially have out-of-
  // range values. These need to code generate, but don't need to necessarily
  // make any sense. We use a warning that defaults to an error.
  return SemaBuiltinConstantArgRange(TheCall, i, l, u, /*RangeIsError*/ false);
}

// ===----------------------------------------------------------------------===
// Call validation, format strings & non-null checks
// ===----------------------------------------------------------------------===

bool Sema::getFormatStringInfo(const FormatAttr *Format, bool IsVariadic,
                               FormatStringInfo *FSI) {
  if (Format->getFirstArg() == 0)
    FSI->ArgPassingKind = FAPK_VAList;
  else if (IsVariadic)
    FSI->ArgPassingKind = FAPK_Variadic;
  else
    FSI->ArgPassingKind = FAPK_Fixed;
  FSI->FormatIdx = Format->getFormatIdx() - 1;
  FSI->FirstDataArg =
      FSI->ArgPassingKind == FAPK_VAList ? 0 : Format->getFirstArg() - 1;
  return true;
}

bool CheckNonNullExpr(Sema &S, const Expr *Expr) {
  // If the expression has non-null type, it doesn't evaluate to null.
  if (auto nullability = Expr->IgnoreImplicit()->getType()->getNullability()) {
    if (*nullability == NullabilityKind::NonNull)
      return false;
  }

  // As a special case, transparent unions initialized with zero are
  // considered null for the purposes of the nonnull attribute.
  if (const RecordType *UT = Expr->getType()->getAsUnionType()) {
    if (UT->getDecl()->hasAttr<TransparentUnionAttr>())
      if (const CompoundLiteralExpr *CLE = dyn_cast<CompoundLiteralExpr>(Expr))
        if (const InitListExpr *ILE =
                dyn_cast<InitListExpr>(CLE->getInitializer()))
          Expr = ILE->getInit(0);
  }

  bool Result;
  return (Expr->EvaluateAsBooleanCondition(Result, S.Context) && !Result);
}

namespace {
void checkNonNullArgument(Sema &S, const Expr *ArgExpr,
                          SourceLocation CallSiteLoc) {
  if (CheckNonNullExpr(S, ArgExpr))
    S.DiagRuntimeBehavior(CallSiteLoc, ArgExpr,
                          S.PDiag(diag::warn_null_arg)
                              << ArgExpr->getSourceRange());
}
} // namespace

bool isNonNullType(QualType type) {
  if (auto nullability = type->getNullability())
    return *nullability == NullabilityKind::NonNull;

  return false;
}

namespace {
void checkNonNullArguments(Sema &S, const NamedDecl *FDecl,
                           const FunctionProtoType *Proto,
                           llvm::ArrayRef<const Expr *> Args,
                           SourceLocation CallSiteLoc) {
  assert((FDecl || Proto) && "Need a function declaration or prototype");

  // Already checked by constant evaluator.
  if (S.isConstantEvaluatedContext())
    return;
  llvm::SmallBitVector NonNullArgs;
  if (FDecl) {
    for (const auto *NonNull : FDecl->specific_attrs<NonNullAttr>()) {
      if (!NonNull->args_size()) {
        // Easy case: all pointer arguments are nonnull.
        for (const auto *Arg : Args)
          if (S.isValidPointerAttrType(Arg->getType()))
            checkNonNullArgument(S, Arg, CallSiteLoc);
        return;
      }

      for (const ParamIdx &Idx : NonNull->args()) {
        unsigned IdxAST = Idx.getASTIndex();
        if (IdxAST >= Args.size())
          continue;
        if (NonNullArgs.empty())
          NonNullArgs.resize(Args.size());
        NonNullArgs.set(IdxAST);
      }
    }
  }

  if (const auto *FD = dyn_cast_or_null<FunctionDecl>(FDecl)) {
    llvm::ArrayRef<ParmVarDecl *> parms;
    parms = FD->parameters();

    unsigned ParamIndex = 0;
    for (llvm::ArrayRef<ParmVarDecl *>::iterator I = parms.begin(),
                                                 E = parms.end();
         I != E; ++I, ++ParamIndex) {
      const ParmVarDecl *PVD = *I;
      if (PVD->hasAttr<NonNullAttr>() || isNonNullType(PVD->getType())) {
        if (NonNullArgs.empty())
          NonNullArgs.resize(Args.size());

        NonNullArgs.set(ParamIndex);
      }
    }
  } else {
    // If we have a non-function, non-method declaration but no
    // function prototype, try to dig out the function prototype.
    if (!Proto) {
      if (const ValueDecl *VD = dyn_cast<ValueDecl>(FDecl)) {
        QualType type = VD->getType();
        if (auto pointerType = type->getAs<PointerType>())
          type = pointerType->getPointeeType();

        // Dig out the function prototype, if there is one.
        Proto = type->getAs<FunctionProtoType>();
      }
    }

    // Fill in non-null argument information from the nullability
    // information on the parameter types (if we have them).
    if (Proto) {
      unsigned Index = 0;
      for (auto paramType : Proto->getParamTypes()) {
        if (isNonNullType(paramType)) {
          if (NonNullArgs.empty())
            NonNullArgs.resize(Args.size());

          NonNullArgs.set(Index);
        }

        ++Index;
      }
    }
  }
  for (unsigned ArgIndex = 0, ArgIndexEnd = NonNullArgs.size();
       ArgIndex != ArgIndexEnd; ++ArgIndex) {
    if (NonNullArgs[ArgIndex])
      checkNonNullArgument(S, Args[ArgIndex], Args[ArgIndex]->getExprLoc());
  }
}
} // namespace

void Sema::CheckArgAlignment(SourceLocation Loc, NamedDecl *FDecl,
                             llvm::StringRef ParamName, QualType ArgTy,
                             QualType ParamTy) {

  // If a function accepts a pointer type
  if (!ParamTy->isPointerType())
    return;

  // If the parameter is a pointer type, get the pointee type for the
  // argument too. If the parameter is a reference type, don't try to get
  // the pointee type for the argument.
  if (ParamTy->isPointerType())
    ArgTy = ArgTy->getPointeeType();

  // Remove reference or pointer
  ParamTy = ParamTy->getPointeeType();

  // Find expected alignment, and the actual alignment of the passed object.
  // getTypeAlignInChars requires complete types
  if (ArgTy.isNull() || ParamTy->isIncompleteType() ||
      ArgTy->isIncompleteType() || ParamTy->isUndeducedType() ||
      ArgTy->isUndeducedType())
    return;

  CharUnits ParamAlign = Context.getTypeAlignInChars(ParamTy);
  CharUnits ArgAlign = Context.getTypeAlignInChars(ArgTy);

  // If the argument is less aligned than the parameter, there is a
  // potential alignment issue.
  if (ArgAlign < ParamAlign)
    Diag(Loc, diag::warn_param_mismatched_alignment)
        << (int)ArgAlign.getQuantity() << (int)ParamAlign.getQuantity()
        << ParamName << (FDecl != nullptr) << FDecl;
}

void Sema::checkCall(NamedDecl *FDecl, const FunctionProtoType *Proto,
                     llvm::ArrayRef<const Expr *> Args, SourceLocation Loc,
                     SourceRange Range, VariadicCallType CallType) {
  // Printf and scanf checking.
  llvm::SmallBitVector CheckedVarArgs;
  if (FDecl) {
    for (const auto *I : FDecl->specific_attrs<FormatAttr>()) {
      // Only create vector if there are format attributes.
      CheckedVarArgs.resize(Args.size());

      CheckFormatArguments(I, Args, CallType, Loc, Range, CheckedVarArgs);
    }
  }

  // Refuse POD arguments that weren't caught by the format string
  // checks above.
  auto *FD = dyn_cast_or_null<FunctionDecl>(FDecl);
  if (CallType != VariadicDoesNotApply &&
      (!FD || FD->getBuiltinID() != Builtin::BI__noop)) {
    unsigned NumParams = Proto ? Proto->getNumParams()
                         : FD  ? FD->getNumParams()
                               : 0;

    for (unsigned ArgIdx = NumParams; ArgIdx < Args.size(); ++ArgIdx) {
      // Args[ArgIdx] can be null in malformed code.
      if (const Expr *Arg = Args[ArgIdx]) {
        if (CheckedVarArgs.empty() || !CheckedVarArgs[ArgIdx])
          checkVariadicArgument(Arg);
      }
    }
  }

  if (FDecl || Proto) {
    checkNonNullArguments(*this, FDecl, Proto, Args, Loc);

    // Type safety checking.
    if (FDecl) {
      for (const auto *I : FDecl->specific_attrs<ArgumentWithTypeTagAttr>())
        CheckArgumentWithTypeTag(I, Args, Loc);
    }
  }

  // Check that passed arguments match the alignment of original arguments.
  // Try to get the missing prototype from the declaration.
  if (!Proto && FDecl) {
    const auto *FT = FDecl->getFunctionType();
    if (isa_and_nonnull<FunctionProtoType>(FT))
      Proto = cast<FunctionProtoType>(FDecl->getFunctionType());
  }
  if (Proto) {
    // For variadic functions, we may have more args than parameters.
    // For some K&R functions, we may have less args than parameters.
    const auto N = std::min<unsigned>(Proto->getNumParams(), Args.size());
    for (unsigned ArgIdx = 0; ArgIdx < N; ++ArgIdx) {
      // Args[ArgIdx] can be null in malformed code.
      if (const Expr *Arg = Args[ArgIdx]) {
        if (Arg->containsErrors())
          continue;

        QualType ParamTy = Proto->getParamType(ArgIdx);
        QualType ArgTy = Arg->getType();
        CheckArgAlignment(Arg->getExprLoc(), FDecl, std::to_string(ArgIdx + 1),
                          ArgTy, ParamTy);
      }
    }

    // If the callee has an AArch64 SME attribute to indicate that it is an
    // __arm_streaming function, then the caller requires SME to be available.
    FunctionProtoType::ExtProtoInfo ExtInfo = Proto->getExtProtoInfo();
    if (ExtInfo.AArch64SMEAttributes & FunctionType::SME_PStateSMEnabledMask) {
      if (auto *CallerFD = dyn_cast<FunctionDecl>(CurContext)) {
        llvm::StringMap<bool> CallerFeatureMap;
        Context.getFunctionFeatureMap(CallerFeatureMap, CallerFD);
        if (!CallerFeatureMap.contains("sme"))
          Diag(Loc, diag::err_sme_call_in_non_sme_target);
      } else if (!Context.getTargetInfo().hasFeature("sme")) {
        Diag(Loc, diag::err_sme_call_in_non_sme_target);
      }
    }

    // If the callee uses AArch64 SME ZA state but the caller doesn't define
    // any, then this is an error.
    if (ExtInfo.AArch64SMEAttributes & FunctionType::SME_PStateZASharedMask) {
      bool CallerHasZAState = false;
      if (const auto *CallerFD = dyn_cast<FunctionDecl>(CurContext)) {
        if (CallerFD->hasAttr<ArmNewZAAttr>())
          CallerHasZAState = true;
        else if (const auto *FPT =
                     CallerFD->getType()->getAs<FunctionProtoType>())
          CallerHasZAState = FPT->getExtProtoInfo().AArch64SMEAttributes &
                             FunctionType::SME_PStateZASharedMask;
      }

      if (!CallerHasZAState)
        Diag(Loc, diag::err_sme_za_call_no_za_state);
    }
  }

  if (FDecl && FDecl->hasAttr<AllocAlignAttr>()) {
    auto *AA = FDecl->getAttr<AllocAlignAttr>();
    const Expr *Arg = Args[AA->getParamIndex().getASTIndex()];
    {
      Expr::EvalResult Align;
      if (Arg->EvaluateAsInt(Align, Context)) {
        const llvm::APSInt &I = Align.Val.getInt();
        if (!I.isPowerOf2())
          Diag(Arg->getExprLoc(), diag::warn_alignment_not_power_of_two)
              << Arg->getSourceRange();

        if (I > Sema::MaximumAlignment)
          Diag(Arg->getExprLoc(), diag::warn_assume_aligned_too_great)
              << Arg->getSourceRange() << Sema::MaximumAlignment;
      }
    }
  }

  if (FD)
    diagnoseArgDependentDiagnoseIfAttrs(FD, Args, Loc);
}

bool Sema::CheckFunctionCall(FunctionDecl *FDecl, CallExpr *TheCall,
                             const FunctionProtoType *Proto) {
  VariadicCallType CallType =
      (Proto && Proto->isVariadic()) ? VariadicFunction : VariadicDoesNotApply;
  Expr **Args = TheCall->getArgs();
  unsigned NumArgs = TheCall->getNumArgs();

  checkCall(FDecl, Proto, llvm::ArrayRef(Args, NumArgs),
            TheCall->getRParenLoc(), TheCall->getCallee()->getSourceRange(),
            CallType);

  IdentifierInfo *FnInfo = FDecl->getIdentifier();
  // None of the checks below are needed for functions without a simple name.
  if (!FnInfo)
    return false;

  // Enforce TCB except for builtin calls, which are always allowed.
  if (FDecl->getBuiltinID() == 0)
    CheckTCBEnforcement(TheCall->getExprLoc(), FDecl);

  CheckAbsoluteValueFunction(TheCall, FDecl);

  unsigned CMId = FDecl->getMemoryFunctionKind();
  switch (CMId) {
  case 0:
    return false;
  case Builtin::BIstrlcpy: // fallthrough
  case Builtin::BIstrlcat:
    CheckStrlcpycatArguments(TheCall, FnInfo);
    break;
  case Builtin::BIstrncat:
    CheckStrncatArguments(TheCall, FnInfo);
    break;
  case Builtin::BIfree:
    CheckFreeArguments(TheCall);
    break;
  default:
    CheckMemaccessArguments(TheCall, CMId, FnInfo);
  }

  return false;
}

bool Sema::CheckPointerCall(NamedDecl *NDecl, CallExpr *TheCall,
                            const FunctionProtoType *Proto) {
  QualType Ty;
  if (const auto *V = dyn_cast<VarDecl>(NDecl))
    Ty = V->getType();
  else if (const auto *F = dyn_cast<FieldDecl>(NDecl))
    Ty = F->getType();
  else
    return false;

  if (!Ty->isFunctionPointerType() && !Ty->isFunctionProtoType())
    return false;

  VariadicCallType CallType;
  if (!Proto || !Proto->isVariadic()) {
    CallType = VariadicDoesNotApply;
  } else {
    CallType = VariadicFunction;
  }

  checkCall(NDecl, Proto,
            llvm::ArrayRef(TheCall->getArgs(), TheCall->getNumArgs()),
            TheCall->getRParenLoc(), TheCall->getCallee()->getSourceRange(),
            CallType);

  return false;
}

bool Sema::CheckOtherCall(CallExpr *TheCall, const FunctionProtoType *Proto) {
  VariadicCallType CallType =
      (Proto && Proto->isVariadic()) ? VariadicFunction : VariadicDoesNotApply;
  checkCall(/*FDecl=*/nullptr, Proto,
            llvm::ArrayRef(TheCall->getArgs(), TheCall->getNumArgs()),
            TheCall->getRParenLoc(), TheCall->getCallee()->getSourceRange(),
            CallType);

  return false;
}

namespace {
bool isValidOrderingForOp(int64_t Ordering, AtomicExpr::AtomicOp Op) {
  if (!llvm::isValidAtomicOrderingCABI(Ordering))
    return false;

  auto OrderingCABI = (llvm::AtomicOrderingCABI)Ordering;
  switch (Op) {
  case AtomicExpr::AO__c11_atomic_init:
    llvm_unreachable("There is no ordering argument for an init");

  case AtomicExpr::AO__c11_atomic_load:
  case AtomicExpr::AO__atomic_load_n:
  case AtomicExpr::AO__atomic_load:
  case AtomicExpr::AO__scoped_atomic_load_n:
  case AtomicExpr::AO__scoped_atomic_load:
    return OrderingCABI != llvm::AtomicOrderingCABI::release &&
           OrderingCABI != llvm::AtomicOrderingCABI::acq_rel;

  case AtomicExpr::AO__c11_atomic_store:
  case AtomicExpr::AO__atomic_store:
  case AtomicExpr::AO__atomic_store_n:
  case AtomicExpr::AO__scoped_atomic_store:
  case AtomicExpr::AO__scoped_atomic_store_n:
    return OrderingCABI != llvm::AtomicOrderingCABI::consume &&
           OrderingCABI != llvm::AtomicOrderingCABI::acquire &&
           OrderingCABI != llvm::AtomicOrderingCABI::acq_rel;

  default:
    return true;
  }
}
} // namespace

// ===----------------------------------------------------------------------===
// Atomic operations
// ===----------------------------------------------------------------------===

ExprResult Sema::SemaAtomicOpsOverloaded(ExprResult TheCallResult,
                                         AtomicExpr::AtomicOp Op) {
  CallExpr *TheCall = cast<CallExpr>(TheCallResult.get());
  DeclRefExpr *DRE =
      cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
  MultiExprArg Args{TheCall->getArgs(), TheCall->getNumArgs()};
  return FormAtomicExpr({TheCall->getBeginLoc(), TheCall->getEndLoc()},
                        DRE->getSourceRange(), TheCall->getRParenLoc(), Args,
                        Op);
}

ExprResult Sema::FormAtomicExpr(SourceRange CallRange, SourceRange ExprRange,
                                SourceLocation RParenLoc, MultiExprArg Args,
                                AtomicExpr::AtomicOp Op,
                                AtomicArgumentOrder ArgOrder) {
  // All the operations take one of the following forms.
  enum {
    // C    __c11_atomic_init(A *, C)
    Init,

    // C    __c11_atomic_load(A *, int)
    Load,

    // void __atomic_load(A *, CP, int)
    LoadCopy,

    // void __atomic_store(A *, CP, int)
    Copy,

    // C    __c11_atomic_add(A *, M, int)
    Arithmetic,

    // C    __atomic_exchange_n(A *, CP, int)
    Xchg,

    // void __atomic_exchange(A *, C *, CP, int)
    GNUXchg,

    // bool __c11_atomic_compare_exchange_strong(A *, C *, CP, int, int)
    C11CmpXchg,

    // bool __atomic_compare_exchange(A *, C *, CP, bool, int, int)
    GNUCmpXchg
  } Form = Init;

  const unsigned NumForm = GNUCmpXchg + 1;
  const unsigned NumArgs[] = {2, 2, 3, 3, 3, 3, 4, 5, 6};
  const unsigned NumVals[] = {1, 0, 1, 1, 1, 1, 2, 2, 3};
  // where:
  //   C is an appropriate type,
  //   A is volatile _Atomic(C) for __c11 builtins and is C for GNU builtins,
  //   CP is C for __c11 builtins and GNU _n builtins and is C * otherwise,
  //   M is C if C is an integer, and ptrdiff_t if C is a pointer, and
  //   the int parameters are for orderings.

  static_assert(sizeof(NumArgs) / sizeof(NumArgs[0]) == NumForm &&
                    sizeof(NumVals) / sizeof(NumVals[0]) == NumForm,
                "need to update code for modified forms");
  static_assert(AtomicExpr::AO__c11_atomic_init == 0 &&
                    AtomicExpr::AO__c11_atomic_fetch_min + 1 ==
                        AtomicExpr::AO__atomic_load,
                "need to update code for modified C11 atomics");
  bool IsScoped = Op >= AtomicExpr::AO__scoped_atomic_load &&
                  Op <= AtomicExpr::AO__scoped_atomic_fetch_max;
  bool IsC11 = Op >= AtomicExpr::AO__c11_atomic_init &&
               Op <= AtomicExpr::AO__c11_atomic_fetch_min;
  bool IsN = Op == AtomicExpr::AO__atomic_load_n ||
             Op == AtomicExpr::AO__atomic_store_n ||
             Op == AtomicExpr::AO__atomic_exchange_n ||
             Op == AtomicExpr::AO__atomic_compare_exchange_n ||
             Op == AtomicExpr::AO__scoped_atomic_load_n ||
             Op == AtomicExpr::AO__scoped_atomic_store_n ||
             Op == AtomicExpr::AO__scoped_atomic_exchange_n ||
             Op == AtomicExpr::AO__scoped_atomic_compare_exchange_n;
  // Bit mask for extra allowed value types other than integers for atomic
  // arithmetic operations. Add/sub allow pointer and floating point. Min/max
  // allow floating point.
  enum ArithOpExtraValueType {
    AOEVT_None = 0,
    AOEVT_Pointer = 1,
    AOEVT_FP = 2,
  };
  unsigned ArithAllows = AOEVT_None;

  switch (Op) {
  case AtomicExpr::AO__c11_atomic_init:
    Form = Init;
    break;

  case AtomicExpr::AO__c11_atomic_load:
  case AtomicExpr::AO__atomic_load_n:
  case AtomicExpr::AO__scoped_atomic_load_n:
    Form = Load;
    break;

  case AtomicExpr::AO__atomic_load:
  case AtomicExpr::AO__scoped_atomic_load:
    Form = LoadCopy;
    break;

  case AtomicExpr::AO__c11_atomic_store:
  case AtomicExpr::AO__atomic_store:
  case AtomicExpr::AO__atomic_store_n:
  case AtomicExpr::AO__scoped_atomic_store:
  case AtomicExpr::AO__scoped_atomic_store_n:
    Form = Copy;
    break;
  case AtomicExpr::AO__atomic_fetch_add:
  case AtomicExpr::AO__atomic_fetch_sub:
  case AtomicExpr::AO__atomic_add_fetch:
  case AtomicExpr::AO__atomic_sub_fetch:
  case AtomicExpr::AO__scoped_atomic_fetch_add:
  case AtomicExpr::AO__scoped_atomic_fetch_sub:
  case AtomicExpr::AO__scoped_atomic_add_fetch:
  case AtomicExpr::AO__scoped_atomic_sub_fetch:
  case AtomicExpr::AO__c11_atomic_fetch_add:
  case AtomicExpr::AO__c11_atomic_fetch_sub:
    ArithAllows = AOEVT_Pointer | AOEVT_FP;
    Form = Arithmetic;
    break;
  case AtomicExpr::AO__atomic_fetch_max:
  case AtomicExpr::AO__atomic_fetch_min:
  case AtomicExpr::AO__atomic_max_fetch:
  case AtomicExpr::AO__atomic_min_fetch:
  case AtomicExpr::AO__scoped_atomic_fetch_max:
  case AtomicExpr::AO__scoped_atomic_fetch_min:
  case AtomicExpr::AO__scoped_atomic_max_fetch:
  case AtomicExpr::AO__scoped_atomic_min_fetch:
  case AtomicExpr::AO__c11_atomic_fetch_max:
  case AtomicExpr::AO__c11_atomic_fetch_min:
    ArithAllows = AOEVT_FP;
    Form = Arithmetic;
    break;
  case AtomicExpr::AO__c11_atomic_fetch_and:
  case AtomicExpr::AO__c11_atomic_fetch_or:
  case AtomicExpr::AO__c11_atomic_fetch_xor:
  case AtomicExpr::AO__c11_atomic_fetch_nand:
  case AtomicExpr::AO__atomic_fetch_and:
  case AtomicExpr::AO__atomic_fetch_or:
  case AtomicExpr::AO__atomic_fetch_xor:
  case AtomicExpr::AO__atomic_fetch_nand:
  case AtomicExpr::AO__atomic_and_fetch:
  case AtomicExpr::AO__atomic_or_fetch:
  case AtomicExpr::AO__atomic_xor_fetch:
  case AtomicExpr::AO__atomic_nand_fetch:
  case AtomicExpr::AO__scoped_atomic_fetch_and:
  case AtomicExpr::AO__scoped_atomic_fetch_or:
  case AtomicExpr::AO__scoped_atomic_fetch_xor:
  case AtomicExpr::AO__scoped_atomic_fetch_nand:
  case AtomicExpr::AO__scoped_atomic_and_fetch:
  case AtomicExpr::AO__scoped_atomic_or_fetch:
  case AtomicExpr::AO__scoped_atomic_xor_fetch:
  case AtomicExpr::AO__scoped_atomic_nand_fetch:
    Form = Arithmetic;
    break;

  case AtomicExpr::AO__c11_atomic_exchange:
  case AtomicExpr::AO__atomic_exchange_n:
  case AtomicExpr::AO__scoped_atomic_exchange_n:
    Form = Xchg;
    break;

  case AtomicExpr::AO__atomic_exchange:
  case AtomicExpr::AO__scoped_atomic_exchange:
    Form = GNUXchg;
    break;

  case AtomicExpr::AO__c11_atomic_compare_exchange_strong:
  case AtomicExpr::AO__c11_atomic_compare_exchange_weak:
    Form = C11CmpXchg;
    break;

  case AtomicExpr::AO__atomic_compare_exchange:
  case AtomicExpr::AO__atomic_compare_exchange_n:
  case AtomicExpr::AO__scoped_atomic_compare_exchange:
  case AtomicExpr::AO__scoped_atomic_compare_exchange_n:
    Form = GNUCmpXchg;
    break;
  }

  unsigned AdjustedNumArgs = NumArgs[Form];
  if (IsScoped)
    ++AdjustedNumArgs;
  if (Args.size() < AdjustedNumArgs) {
    Diag(CallRange.getEnd(), diag::err_typecheck_call_too_few_args)
        << AdjustedNumArgs << static_cast<unsigned>(Args.size()) << ExprRange;
    return ExprError();
  } else if (Args.size() > AdjustedNumArgs) {
    Diag(Args[AdjustedNumArgs]->getBeginLoc(),
         diag::err_typecheck_call_too_many_args)
        << AdjustedNumArgs << static_cast<unsigned>(Args.size()) << ExprRange;
    return ExprError();
  }

  // Inspect the first argument of the atomic operation.
  Expr *Ptr = Args[0];
  ExprResult ConvertedPtr = DefaultFunctionArrayLvalueConversion(Ptr);
  if (ConvertedPtr.isInvalid())
    return ExprError();

  Ptr = ConvertedPtr.get();
  const PointerType *pointerType = Ptr->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(ExprRange.getBegin(), diag::err_atomic_builtin_must_be_pointer)
        << Ptr->getType() << Ptr->getSourceRange();
    return ExprError();
  }

  // For a __c11 builtin, this should be a pointer to an _Atomic type.
  QualType AtomTy = pointerType->getPointeeType(); // 'A'
  QualType ValType = AtomTy;                       // 'C'
  if (IsC11) {
    if (!AtomTy->isAtomicType()) {
      Diag(ExprRange.getBegin(), diag::err_atomic_op_needs_atomic)
          << Ptr->getType() << Ptr->getSourceRange();
      return ExprError();
    }
    if (Form != Load && Form != LoadCopy && AtomTy.isConstQualified()) {
      Diag(ExprRange.getBegin(), diag::err_atomic_op_needs_non_const_atomic)
          << (AtomTy.isConstQualified() ? 0 : 1) << Ptr->getType()
          << Ptr->getSourceRange();
      return ExprError();
    }
    ValType = AtomTy->castAs<AtomicType>()->getValueType();
  } else if (Form != Load && Form != LoadCopy) {
    if (ValType.isConstQualified()) {
      Diag(ExprRange.getBegin(), diag::err_atomic_op_needs_non_const_pointer)
          << Ptr->getType() << Ptr->getSourceRange();
      return ExprError();
    }
  }

  // For an arithmetic operation, the implied arithmetic must be well-formed.
  if (Form == Arithmetic) {
    // GCC does not enforce these rules for GNU atomics, but we do to help catch
    // trivial type errors.
    auto IsAllowedValueType = [&](QualType ValType,
                                  unsigned AllowedType) -> bool {
      if (ValType->isIntegerType())
        return true;
      if (ValType->isPointerType())
        return AllowedType & AOEVT_Pointer;
      if (!(ValType->isFloatingType() && (AllowedType & AOEVT_FP)))
        return false;
      // LLVM Parser does not allow atomicrmw with x86_fp80 type.
      if (ValType->isSpecificBuiltinType(BuiltinType::LongDouble) &&
          &Context.getTargetInfo().getLongDoubleFormat() ==
              &llvm::APFloat::x87DoubleExtended())
        return false;
      return true;
    };
    if (!IsAllowedValueType(ValType, ArithAllows)) {
      auto DID = ArithAllows & AOEVT_FP
                     ? (ArithAllows & AOEVT_Pointer
                            ? diag::err_atomic_op_needs_atomic_int_ptr_or_fp
                            : diag::err_atomic_op_needs_atomic_int_or_fp)
                     : diag::err_atomic_op_needs_atomic_int;
      Diag(ExprRange.getBegin(), DID)
          << IsC11 << Ptr->getType() << Ptr->getSourceRange();
      return ExprError();
    }
    if (IsC11 && ValType->isPointerType() &&
        RequireCompleteType(Ptr->getBeginLoc(), ValType->getPointeeType(),
                            diag::err_incomplete_type)) {
      return ExprError();
    }
  } else if (IsN && !ValType->isIntegerType() && !ValType->isPointerType()) {
    // For __atomic_*_n operations, the value type must be a scalar integral or
    // pointer type which is 1, 2, 4, 8 or 16 bytes in length.
    Diag(ExprRange.getBegin(), diag::err_atomic_op_needs_atomic_int_or_ptr)
        << IsC11 << Ptr->getType() << Ptr->getSourceRange();
    return ExprError();
  }

  if (!IsC11 && !AtomTy.isTriviallyCopyableType(Context) &&
      !AtomTy->isScalarType()) {
    // For GNU atomics, require a trivially-copyable type. This is not part of
    // the GNU atomics specification but we enforce it for consistency with
    // other atomics which generally all require a trivially-copyable type. This
    // is because atomics just copy bits.
    Diag(ExprRange.getBegin(), diag::err_atomic_op_needs_trivial_copy)
        << Ptr->getType() << Ptr->getSourceRange();
    return ExprError();
  }

  // All atomic operations have an overload which takes a pointer to a volatile
  // 'A'.  We shouldn't let the volatile-ness of the pointee-type inject itself
  // into the result or the other operands. Similarly atomic_load takes a
  // pointer to a const 'A'.
  ValType.removeLocalVolatile();
  ValType.removeLocalConst();
  QualType ResultType = ValType;
  if (Form == Copy || Form == LoadCopy || Form == GNUXchg || Form == Init)
    ResultType = Context.VoidTy;
  else if (Form == C11CmpXchg || Form == GNUCmpXchg)
    ResultType = Context.BoolTy;

  // The type of a parameter passed 'by value'. In the GNU atomics, such
  // arguments are actually passed as pointers.
  QualType ByValType = ValType; // 'CP'
  bool IsPassedByAddress = false;
  if (!IsC11 && !IsN) {
    ByValType = Ptr->getType();
    IsPassedByAddress = true;
  }

  llvm::SmallVector<Expr *, 5> APIOrderedArgs;
  if (ArgOrder == Sema::AtomicArgumentOrder::AST) {
    APIOrderedArgs.push_back(Args[0]);
    switch (Form) {
    case Init:
    case Load:
      APIOrderedArgs.push_back(Args[1]); // Val1/Order
      break;
    case LoadCopy:
    case Copy:
    case Arithmetic:
    case Xchg:
      APIOrderedArgs.push_back(Args[2]); // Val1
      APIOrderedArgs.push_back(Args[1]); // Order
      break;
    case GNUXchg:
      APIOrderedArgs.push_back(Args[2]); // Val1
      APIOrderedArgs.push_back(Args[3]); // Val2
      APIOrderedArgs.push_back(Args[1]); // Order
      break;
    case C11CmpXchg:
      APIOrderedArgs.push_back(Args[2]); // Val1
      APIOrderedArgs.push_back(Args[4]); // Val2
      APIOrderedArgs.push_back(Args[1]); // Order
      APIOrderedArgs.push_back(Args[3]); // OrderFail
      break;
    case GNUCmpXchg:
      APIOrderedArgs.push_back(Args[2]); // Val1
      APIOrderedArgs.push_back(Args[4]); // Val2
      APIOrderedArgs.push_back(Args[5]); // Weak
      APIOrderedArgs.push_back(Args[1]); // Order
      APIOrderedArgs.push_back(Args[3]); // OrderFail
      break;
    }
  } else
    APIOrderedArgs.append(Args.begin(), Args.end());

  // The first argument's non-CV pointer type is used to deduce the type of
  // subsequent arguments, except for:
  //  - weak flag (always converted to bool)
  //  - memory order (always converted to int)
  //  - scope  (always converted to int)
  for (unsigned i = 0; i != APIOrderedArgs.size(); ++i) {
    QualType Ty;
    if (i < NumVals[Form] + 1) {
      switch (i) {
      case 0:
        // The first argument is always a pointer. It has a fixed type.
        // It is always dereferenced, a nullptr is undefined.
        checkNonNullArgument(*this, APIOrderedArgs[i], ExprRange.getBegin());
        // Nothing else to do: we already know all we want about this pointer.
        continue;
      case 1:
        // The second argument is the non-atomic operand. For arithmetic, this
        // is always passed by value, and for a compare_exchange it is always
        // passed by address. For the rest, GNU uses by-address and C11 uses
        // by-value.
        assert(Form != Load);
        if (Form == Arithmetic && ValType->isPointerType())
          Ty = Context.getPointerDiffType();
        else if (Form == Init || Form == Arithmetic)
          Ty = ValType;
        else if (Form == Copy || Form == Xchg) {
          if (IsPassedByAddress) {
            // The value pointer is always dereferenced, a nullptr is undefined.
            checkNonNullArgument(*this, APIOrderedArgs[i],
                                 ExprRange.getBegin());
          }
          Ty = ByValType;
        } else {
          Expr *ValArg = APIOrderedArgs[i];
          // The value pointer is always dereferenced, a nullptr is undefined.
          checkNonNullArgument(*this, ValArg, ExprRange.getBegin());
          LangAS AS = LangAS::Default;
          // Keep address space of non-atomic pointer type.
          if (const PointerType *PtrTy =
                  ValArg->getType()->getAs<PointerType>()) {
            AS = PtrTy->getPointeeType().getAddressSpace();
          }
          Ty = Context.getPointerType(
              Context.getAddrSpaceQualType(ValType.getUnqualifiedType(), AS));
        }
        break;
      case 2:
        // The third argument to compare_exchange / GNU exchange is the desired
        // value, either by-value (for the C11 and *_n variant) or as a pointer.
        if (IsPassedByAddress)
          checkNonNullArgument(*this, APIOrderedArgs[i], ExprRange.getBegin());
        Ty = ByValType;
        break;
      case 3:
        // The fourth argument to GNU compare_exchange is a 'weak' flag.
        Ty = Context.BoolTy;
        break;
      }
    } else {
      // The order(s) and scope are always converted to int.
      Ty = Context.IntTy;
    }

    InitializedEntity Entity =
        InitializedEntity::InitializeParameter(Context, Ty, false);
    ExprResult Arg = APIOrderedArgs[i];
    Arg = PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return true;
    APIOrderedArgs[i] = Arg.get();
  }

  // Permute the arguments into a 'consistent' order.
  llvm::SmallVector<Expr *, 5> SubExprs;
  SubExprs.push_back(Ptr);
  switch (Form) {
  case Init:
    // Note, AtomicExpr::getVal1() has a special case for this atomic.
    SubExprs.push_back(APIOrderedArgs[1]); // Val1
    break;
  case Load:
    SubExprs.push_back(APIOrderedArgs[1]); // Order
    break;
  case LoadCopy:
  case Copy:
  case Arithmetic:
  case Xchg:
    SubExprs.push_back(APIOrderedArgs[2]); // Order
    SubExprs.push_back(APIOrderedArgs[1]); // Val1
    break;
  case GNUXchg:
    // Note, AtomicExpr::getVal2() has a special case for this atomic.
    SubExprs.push_back(APIOrderedArgs[3]); // Order
    SubExprs.push_back(APIOrderedArgs[1]); // Val1
    SubExprs.push_back(APIOrderedArgs[2]); // Val2
    break;
  case C11CmpXchg:
    SubExprs.push_back(APIOrderedArgs[3]); // Order
    SubExprs.push_back(APIOrderedArgs[1]); // Val1
    SubExprs.push_back(APIOrderedArgs[4]); // OrderFail
    SubExprs.push_back(APIOrderedArgs[2]); // Val2
    break;
  case GNUCmpXchg:
    SubExprs.push_back(APIOrderedArgs[4]); // Order
    SubExprs.push_back(APIOrderedArgs[1]); // Val1
    SubExprs.push_back(APIOrderedArgs[5]); // OrderFail
    SubExprs.push_back(APIOrderedArgs[2]); // Val2
    SubExprs.push_back(APIOrderedArgs[3]); // Weak
    break;
  }

  // If the memory orders are constants, check they are valid.
  if (SubExprs.size() >= 2 && Form != Init) {
    std::optional<llvm::APSInt> Success =
        SubExprs[1]->getIntegerConstantExpr(Context);
    if (Success && !isValidOrderingForOp(Success->getSExtValue(), Op)) {
      Diag(SubExprs[1]->getBeginLoc(),
           diag::warn_atomic_op_has_invalid_memory_order)
          << /*success=*/(Form == C11CmpXchg || Form == GNUCmpXchg)
          << SubExprs[1]->getSourceRange();
    }
    if (SubExprs.size() >= 5) {
      if (std::optional<llvm::APSInt> Failure =
              SubExprs[3]->getIntegerConstantExpr(Context)) {
        if (!llvm::is_contained(
                {llvm::AtomicOrderingCABI::relaxed,
                 llvm::AtomicOrderingCABI::consume,
                 llvm::AtomicOrderingCABI::acquire,
                 llvm::AtomicOrderingCABI::seq_cst},
                (llvm::AtomicOrderingCABI)Failure->getSExtValue())) {
          Diag(SubExprs[3]->getBeginLoc(),
               diag::warn_atomic_op_has_invalid_memory_order)
              << /*failure=*/2 << SubExprs[3]->getSourceRange();
        }
      }
    }
  }

  if (auto ScopeModel = AtomicExpr::getScopeModel(Op)) {
    auto *Scope = Args[Args.size() - 1];
    if (std::optional<llvm::APSInt> Result =
            Scope->getIntegerConstantExpr(Context)) {
      if (!ScopeModel->isValid(Result->getZExtValue()))
        Diag(Scope->getBeginLoc(), diag::err_atomic_op_has_invalid_synch_scope)
            << Scope->getSourceRange();
    }
    SubExprs.push_back(Scope);
  }

  AtomicExpr *AE = new (Context)
      AtomicExpr(ExprRange.getBegin(), SubExprs, ResultType, Op, RParenLoc);

  if ((Op == AtomicExpr::AO__c11_atomic_load ||
       Op == AtomicExpr::AO__c11_atomic_store) &&
      Context.AtomicUsesUnsupportedLibcall(AE))
    Diag(AE->getBeginLoc(), diag::err_atomic_load_store_uses_lib)
        << ((Op == AtomicExpr::AO__c11_atomic_load) ? 0 : 1);

  if (ValType->isBitIntType()) {
    Diag(Ptr->getExprLoc(), diag::err_atomic_builtin_bit_int_prohibit);
    return ExprError();
  }

  return AE;
}

namespace {
bool checkBuiltinArgument(Sema &S, CallExpr *E, unsigned ArgIndex) {
  FunctionDecl *Fn = E->getDirectCallee();
  assert(Fn && "builtin call without direct callee!");

  ParmVarDecl *Param = Fn->getParamDecl(ArgIndex);
  InitializedEntity Entity =
      InitializedEntity::InitializeParameter(S.Context, Param);

  ExprResult Arg = E->getArg(ArgIndex);
  Arg = S.PerformCopyInitialization(Entity, SourceLocation(), Arg);
  if (Arg.isInvalid())
    return true;

  E->setArg(ArgIndex, Arg.get());
  return false;
}
} // namespace

ExprResult Sema::SemaBuiltinAtomicOverloaded(ExprResult TheCallResult) {
  CallExpr *TheCall = static_cast<CallExpr *>(TheCallResult.get());
  Expr *Callee = TheCall->getCallee();
  DeclRefExpr *DRE = cast<DeclRefExpr>(Callee->IgnoreParenCasts());
  FunctionDecl *FDecl = cast<FunctionDecl>(DRE->getDecl());

  // Ensure that we have at least one argument to do type inference from.
  if (TheCall->getNumArgs() < 1) {
    Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args_at_least)
        << 1 << TheCall->getNumArgs() << Callee->getSourceRange();
    return ExprError();
  }

  // Inspect the first argument of the atomic builtin.  This should always be
  // a pointer type, whose element is an integral scalar or pointer type.
  // Because it is a pointer type, we don't have to worry about any implicit
  // casts here.
  Expr *FirstArg = TheCall->getArg(0);
  ExprResult FirstArgResult = DefaultFunctionArrayLvalueConversion(FirstArg);
  if (FirstArgResult.isInvalid())
    return ExprError();
  FirstArg = FirstArgResult.get();
  TheCall->setArg(0, FirstArg);

  const PointerType *pointerType = FirstArg->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer)
        << FirstArg->getType() << FirstArg->getSourceRange();
    return ExprError();
  }

  QualType ValType = pointerType->getPointeeType();
  if (!ValType->isIntegerType() && !ValType->isAnyPointerType()) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer_intptr)
        << FirstArg->getType() << FirstArg->getSourceRange();
    return ExprError();
  }

  if (ValType.isConstQualified()) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_cannot_be_const)
        << FirstArg->getType() << FirstArg->getSourceRange();
    return ExprError();
  }

  // Strip any qualifiers off ValType.
  ValType = ValType.getUnqualifiedType();

  // The majority of builtins return a value, but a few have special return
  // types, so allow them to override appropriately below.
  QualType ResultType = ValType;

  // We need to figure out which concrete builtin this maps onto.  For example,
  // __sync_fetch_and_add with a 2 byte object turns into
  // __sync_fetch_and_add_2.
#define BUILTIN_ROW(x)                                                         \
  {                                                                            \
    Builtin::BI##x##_1, Builtin::BI##x##_2, Builtin::BI##x##_4,                \
        Builtin::BI##x##_8, Builtin::BI##x##_16                                \
  }

  static const unsigned BuiltinIndices[][5] = {
      BUILTIN_ROW(__sync_fetch_and_add),
      BUILTIN_ROW(__sync_fetch_and_sub),
      BUILTIN_ROW(__sync_fetch_and_or),
      BUILTIN_ROW(__sync_fetch_and_and),
      BUILTIN_ROW(__sync_fetch_and_xor),
      BUILTIN_ROW(__sync_fetch_and_nand),

      BUILTIN_ROW(__sync_add_and_fetch),
      BUILTIN_ROW(__sync_sub_and_fetch),
      BUILTIN_ROW(__sync_and_and_fetch),
      BUILTIN_ROW(__sync_or_and_fetch),
      BUILTIN_ROW(__sync_xor_and_fetch),
      BUILTIN_ROW(__sync_nand_and_fetch),

      BUILTIN_ROW(__sync_val_compare_and_swap),
      BUILTIN_ROW(__sync_bool_compare_and_swap),
      BUILTIN_ROW(__sync_lock_test_and_set),
      BUILTIN_ROW(__sync_lock_release),
      BUILTIN_ROW(__sync_swap)};
#undef BUILTIN_ROW

  // Determine the index of the size.
  unsigned SizeIndex;
  switch (Context.getTypeSizeInChars(ValType).getQuantity()) {
  case 1:
    SizeIndex = 0;
    break;
  case 2:
    SizeIndex = 1;
    break;
  case 4:
    SizeIndex = 2;
    break;
  case 8:
    SizeIndex = 3;
    break;
  case 16:
    SizeIndex = 4;
    break;
  default:
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_pointer_size)
        << FirstArg->getType() << FirstArg->getSourceRange();
    return ExprError();
  }

  // Each of these builtins has one pointer argument, followed by some number of
  // values (0, 1 or 2) followed by a potentially empty varags list of stuff
  // that we ignore.  Find out which row of BuiltinIndices to read from as well
  // as the number of fixed args.
  unsigned BuiltinID = FDecl->getBuiltinID();
  unsigned BuiltinIndex, NumFixed = 1;
  bool WarnAboutSemanticsChange = false;
  switch (BuiltinID) {
  default:
    llvm_unreachable("Unknown overloaded atomic builtin!");
  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
    BuiltinIndex = 0;
    break;

  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
    BuiltinIndex = 1;
    break;

  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
    BuiltinIndex = 2;
    break;

  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
    BuiltinIndex = 3;
    break;

  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
    BuiltinIndex = 4;
    break;

  case Builtin::BI__sync_fetch_and_nand:
  case Builtin::BI__sync_fetch_and_nand_1:
  case Builtin::BI__sync_fetch_and_nand_2:
  case Builtin::BI__sync_fetch_and_nand_4:
  case Builtin::BI__sync_fetch_and_nand_8:
  case Builtin::BI__sync_fetch_and_nand_16:
    BuiltinIndex = 5;
    WarnAboutSemanticsChange = true;
    break;

  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
    BuiltinIndex = 6;
    break;

  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
    BuiltinIndex = 7;
    break;

  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
    BuiltinIndex = 8;
    break;

  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
    BuiltinIndex = 9;
    break;

  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
    BuiltinIndex = 10;
    break;

  case Builtin::BI__sync_nand_and_fetch:
  case Builtin::BI__sync_nand_and_fetch_1:
  case Builtin::BI__sync_nand_and_fetch_2:
  case Builtin::BI__sync_nand_and_fetch_4:
  case Builtin::BI__sync_nand_and_fetch_8:
  case Builtin::BI__sync_nand_and_fetch_16:
    BuiltinIndex = 11;
    WarnAboutSemanticsChange = true;
    break;

  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16:
    BuiltinIndex = 12;
    NumFixed = 2;
    break;

  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16:
    BuiltinIndex = 13;
    NumFixed = 2;
    ResultType = Context.BoolTy;
    break;

  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
    BuiltinIndex = 14;
    break;

  case Builtin::BI__sync_lock_release:
  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16:
    BuiltinIndex = 15;
    NumFixed = 0;
    ResultType = Context.VoidTy;
    break;

  case Builtin::BI__sync_swap:
  case Builtin::BI__sync_swap_1:
  case Builtin::BI__sync_swap_2:
  case Builtin::BI__sync_swap_4:
  case Builtin::BI__sync_swap_8:
  case Builtin::BI__sync_swap_16:
    BuiltinIndex = 16;
    break;
  }

  // Now that we know how many fixed arguments we expect, first check that we
  // have at least that many.
  if (TheCall->getNumArgs() < 1 + NumFixed) {
    Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args_at_least)
        << 1 + NumFixed << TheCall->getNumArgs() << Callee->getSourceRange();
    return ExprError();
  }

  Diag(TheCall->getEndLoc(), diag::warn_atomic_implicit_seq_cst)
      << Callee->getSourceRange();

  if (WarnAboutSemanticsChange) {
    Diag(TheCall->getEndLoc(), diag::warn_sync_fetch_and_nand_semantics_change)
        << Callee->getSourceRange();
  }

  // Get the decl for the concrete builtin from this, we can tell what the
  // concrete integer type we should convert to is.
  unsigned NewBuiltinID = BuiltinIndices[BuiltinIndex][SizeIndex];
  llvm::StringRef NewBuiltinName = Context.BuiltinInfo.getName(NewBuiltinID);
  FunctionDecl *NewBuiltinDecl;
  if (NewBuiltinID == BuiltinID)
    NewBuiltinDecl = FDecl;
  else {
    // Perform builtin lookup to avoid redeclaring it.
    DeclarationName DN(&Context.Idents.get(NewBuiltinName));
    LookupResult Res(*this, DN, DRE->getBeginLoc(), ResolveOrdinary);
    ResolveName(Res, TUScope, /*AllowBuiltinCreation=*/true);
    assert(Res.getFoundDecl());
    NewBuiltinDecl = dyn_cast<FunctionDecl>(Res.getFoundDecl());
    if (!NewBuiltinDecl)
      return ExprError();
  }

  // The first argument --- the pointer --- has a fixed type; we
  // deduce the types of the rest of the arguments accordingly.  Walk
  // the remaining arguments, converting them to the deduced value type.
  for (unsigned i = 0; i != NumFixed; ++i) {
    ExprResult Arg = TheCall->getArg(i + 1);

    // GCC does an implicit conversion to the pointer or integer ValType.  This
    // can fail in some cases (1i -> int**), check for this error case now.
    InitializedEntity Entity = InitializedEntity::InitializeParameter(
        Context, ValType, /*consume*/ false);
    Arg = PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return ExprError();

    // Okay, we have something that *can* be converted to the right type.  Check
    // to see if there is a potentially weird extension going on here.  This can
    // happen when you do an atomic operation on something like an char* and
    // pass in 42.  The 42 gets converted to char.  This is even more strange
    // for things like 45.123 -> char, etc.
    TheCall->setArg(i + 1, Arg.get());
  }
  DeclRefExpr *NewDRE = DeclRefExpr::Create(
      Context, NewBuiltinDecl, DRE->getLocation(), Context.BuiltinFnTy,
      DRE->getValueKind(), nullptr, DRE->isNonOdrUse());
  QualType CalleePtrTy = Context.getPointerType(NewBuiltinDecl->getType());
  ExprResult PromotedCall =
      ImpCastExprToType(NewDRE, CalleePtrTy, CK_BuiltinFnToFnPtr);
  TheCall->setCallee(PromotedCall.get());

  // Change the result type of the call to match the original value type. This
  // is arbitrary, but the codegen for these builtins ins design to handle it
  // gracefully.
  TheCall->setType(ResultType);

  // Prohibit problematic uses of bit-precise integer types with atomic
  // builtins. The arguments would have already been converted to the first
  // argument's type, so only need to check the first argument.
  const auto *BitIntValType = ValType->getAs<BitIntType>();
  if (BitIntValType && !llvm::isPowerOf2_64(BitIntValType->getNumBits())) {
    Diag(FirstArg->getExprLoc(), diag::err_atomic_builtin_ext_int_size);
    return ExprError();
  }

  return TheCallResult;
}

ExprResult Sema::SemaBuiltinNontemporalOverloaded(ExprResult TheCallResult) {
  CallExpr *TheCall = (CallExpr *)TheCallResult.get();
  DeclRefExpr *DRE =
      cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
  FunctionDecl *FDecl = cast<FunctionDecl>(DRE->getDecl());
  unsigned BuiltinID = FDecl->getBuiltinID();
  assert((BuiltinID == Builtin::BI__builtin_nontemporal_store ||
          BuiltinID == Builtin::BI__builtin_nontemporal_load) &&
         "Unexpected nontemporal load/store builtin!");
  bool isStore = BuiltinID == Builtin::BI__builtin_nontemporal_store;
  unsigned numArgs = isStore ? 2 : 1;

  // Ensure that we have the proper number of arguments.
  if (checkArgCount(*this, TheCall, numArgs))
    return ExprError();

  // Inspect the last argument of the nontemporal builtin.  This should always
  // be a pointer type, from which we imply the type of the memory access.
  // Because it is a pointer type, we don't have to worry about any implicit
  // casts here.
  Expr *PointerArg = TheCall->getArg(numArgs - 1);
  ExprResult PointerArgResult =
      DefaultFunctionArrayLvalueConversion(PointerArg);

  if (PointerArgResult.isInvalid())
    return ExprError();
  PointerArg = PointerArgResult.get();
  TheCall->setArg(numArgs - 1, PointerArg);

  const PointerType *pointerType = PointerArg->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(DRE->getBeginLoc(), diag::err_nontemporal_builtin_must_be_pointer)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return ExprError();
  }

  QualType ValType = pointerType->getPointeeType();

  // Strip any qualifiers off ValType.
  ValType = ValType.getUnqualifiedType();
  if (!ValType->isIntegerType() && !ValType->isAnyPointerType() &&
      !ValType->isFloatingType() && !ValType->isVectorType()) {
    Diag(DRE->getBeginLoc(),
         diag::err_nontemporal_builtin_must_be_pointer_intfltptr_or_vector)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return ExprError();
  }

  if (!isStore) {
    TheCall->setType(ValType);
    return TheCallResult;
  }

  ExprResult ValArg = TheCall->getArg(0);
  InitializedEntity Entity = InitializedEntity::InitializeParameter(
      Context, ValType, /*consume*/ false);
  ValArg = PerformCopyInitialization(Entity, SourceLocation(), ValArg);
  if (ValArg.isInvalid())
    return ExprError();

  TheCall->setArg(0, ValArg.get());
  TheCall->setType(Context.VoidTy);
  return TheCallResult;
}

ExprResult Sema::CheckOSLogFormatStringArg(Expr *Arg) {
  Arg = Arg->IgnoreParenCasts();
  auto *Literal = dyn_cast<StringLiteral>(Arg);
  if (!Literal || (!Literal->isOrdinary() && !Literal->isUTF8())) {
    return ExprError(
        Diag(Arg->getBeginLoc(), diag::err_os_log_format_not_string_constant)
        << Arg->getSourceRange());
  }

  ExprResult Result(Literal);
  QualType ResultTy = Context.getPointerType(Context.CharTy.withConst());
  InitializedEntity Entity =
      InitializedEntity::InitializeParameter(Context, ResultTy, false);
  Result = PerformCopyInitialization(Entity, SourceLocation(), Result);
  return Result;
}

// ===----------------------------------------------------------------------===
// Miscellaneous builtin checks (va_start, prefetch, assume, etc.)
// ===----------------------------------------------------------------------===

namespace {
bool checkVAStartABI(Sema &S, unsigned BuiltinID, Expr *Fn) {
  const llvm::Triple &TT = S.Context.getTargetInfo().getTriple();
  bool IsX64 = TT.getArch() == llvm::Triple::x86_64;
  bool IsAArch64 = TT.getArch() == llvm::Triple::aarch64;
  bool IsWindows = TT.isOSWindows();
  bool IsMSVAStart = BuiltinID == Builtin::BI__builtin_ms_va_start;
  if (IsX64 || IsAArch64) {
    CallingConv CC = CC_C;
    if (const FunctionDecl *FD = S.getCurFunctionDecl())
      CC = FD->getType()->castAs<FunctionType>()->getCallConv();
    if (IsMSVAStart) {
      // Don't allow this in System V ABI functions.
      if (CC == CC_X86_64SysV || (!IsWindows && CC != CC_Win64))
        return S.Diag(Fn->getBeginLoc(),
                      diag::err_ms_va_start_used_in_sysv_function);
    } else {
      // On x86-64/AArch64 Unix, don't allow this in Win64 ABI functions.
      // On x64 Windows, don't allow this in System V ABI functions.
      // (Yes, that means there's no corresponding way to support variadic
      // System V ABI functions on Windows.)
      if ((IsWindows && CC == CC_X86_64SysV) || (!IsWindows && CC == CC_Win64))
        return S.Diag(Fn->getBeginLoc(),
                      diag::err_va_start_used_in_wrong_abi_function)
               << !IsWindows;
    }
    return false;
  }

  if (IsMSVAStart)
    return S.Diag(Fn->getBeginLoc(), diag::err_builtin_x64_aarch64_only);
  return false;
}

bool checkVAStartIsInVariadicFunction(Sema &S, Expr *Fn,
                                      ParmVarDecl **LastParam = nullptr) {
  // Determine whether the current function or block is variadic
  // and get its parameter list.
  bool IsVariadic = false;
  llvm::ArrayRef<ParmVarDecl *> Params;
  DeclContext *Caller = S.CurContext;
  if (auto *FD = dyn_cast<FunctionDecl>(Caller)) {
    IsVariadic = FD->isVariadic();
    Params = FD->parameters();
  } else {
    // This must be some other declcontext that parses exprs.
    S.Diag(Fn->getBeginLoc(), diag::err_va_start_outside_function);
    return true;
  }

  if (!IsVariadic) {
    S.Diag(Fn->getBeginLoc(), diag::err_va_start_fixed_function);
    return true;
  }

  if (LastParam)
    *LastParam = Params.empty() ? nullptr : Params.back();

  return false;
}
} // namespace

bool Sema::SemaBuiltinVAStart(unsigned BuiltinID, CallExpr *TheCall) {
  Expr *Fn = TheCall->getCallee();

  if (checkVAStartABI(*this, BuiltinID, Fn))
    return true;

  // In C23 mode, va_start only needs one argument. However, the builtin still
  // requires two arguments (which matches the behavior of the GCC builtin),
  // <stdarg.h> passes `0` as the second argument in C23 mode.
  if (checkArgCount(*this, TheCall, 2))
    return true;

  // Type-check the first argument normally.
  if (checkBuiltinArgument(*this, TheCall, 0))
    return true;

  ParmVarDecl *LastParam;
  if (checkVAStartIsInVariadicFunction(*this, Fn, &LastParam))
    return true;

  // Verify that the second argument to the builtin is the last argument of the
  // current function. In C23 mode, if the second argument is an
  // integer constant expression with value 0, then we don't bother with this
  // check.
  bool SecondArgIsLastNamedArgument = false;
  const Expr *Arg = TheCall->getArg(1)->IgnoreParenCasts();
  if (std::optional<llvm::APSInt> Val =
          TheCall->getArg(1)->getIntegerConstantExpr(Context);
      Val && LangOpts.C23 && *Val == 0)
    return false;

  // These are valid if SecondArgIsLastNamedArgument is false after the next
  // block.
  QualType Type;
  SourceLocation ParamLoc;
  bool IsCRegister = false;

  if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Arg)) {
    if (const ParmVarDecl *PV = dyn_cast<ParmVarDecl>(DR->getDecl())) {
      SecondArgIsLastNamedArgument = PV == LastParam;

      Type = PV->getType();
      ParamLoc = PV->getLocation();
      IsCRegister = PV->getStorageClass() == SC_Register;
    }
  }

  if (!SecondArgIsLastNamedArgument)
    Diag(TheCall->getArg(1)->getBeginLoc(),
         diag::warn_second_arg_of_va_start_not_last_named_param);
  else if (IsCRegister || Type->isSpecificBuiltinType(BuiltinType::Float) ||
           [=] {
             // Promotable integers are UB, but enumerations need a bit of
             // extra checking to see what their promotable type actually is.
             if (!Context.isPromotableIntegerType(Type))
               return false;
             if (!Type->isEnumeralType())
               return true;
             const EnumDecl *ED = Type->castAs<EnumType>()->getDecl();
             return !(ED &&
                      Context.typesAreCompatible(ED->getPromotionType(), Type));
           }()) {
    unsigned Reason = IsCRegister ? 1 : 0;
    Diag(Arg->getBeginLoc(), diag::warn_va_start_type_is_undefined) << Reason;
    Diag(ParamLoc, diag::note_parameter_type) << Type;
  }

  return false;
}

bool Sema::SemaBuiltinVAStartARMMicrosoft(CallExpr *Call) {
  auto IsSuitablyTypedFormatArgument =
      []([[maybe_unused]] const Expr *Arg) -> bool { return true; };

  // void __va_start(va_list *ap, const char *named_addr, size_t slot_size,
  //                 const char *named_addr);

  Expr *Func = Call->getCallee();

  if (Call->getNumArgs() < 3)
    return Diag(Call->getEndLoc(),
                diag::err_typecheck_call_too_few_args_at_least)
           << 3 << Call->getNumArgs();

  // Type-check the first argument normally.
  if (checkBuiltinArgument(*this, Call, 0))
    return true;
  if (checkVAStartIsInVariadicFunction(*this, Func))
    return true;

  // __va_start on Windows does not validate the parameter qualifiers

  const Expr *Arg1 = Call->getArg(1)->IgnoreParens();
  const Type *Arg1Ty = Arg1->getType().getCanonicalType().getTypePtr();

  const Expr *Arg2 = Call->getArg(2)->IgnoreParens();
  const Type *Arg2Ty = Arg2->getType().getCanonicalType().getTypePtr();

  const QualType &ConstCharPtrTy =
      Context.getPointerType(Context.CharTy.withConst());
  if (!Arg1Ty->isPointerType() || !IsSuitablyTypedFormatArgument(Arg1))
    Diag(Arg1->getBeginLoc(), diag::err_typecheck_convert_incompatible)
        << Arg1->getType() << ConstCharPtrTy << 1 /* different class */
        << 0                                      /* qualifier difference */
        << 3                                      /* parameter mismatch */
        << 2 << Arg1->getType() << ConstCharPtrTy;

  const QualType SizeTy = Context.getSizeType();
  if (Arg2Ty->getCanonicalTypeInternal().withoutLocalFastQualifiers() != SizeTy)
    Diag(Arg2->getBeginLoc(), diag::err_typecheck_convert_incompatible)
        << Arg2->getType() << SizeTy << 1 /* different class */
        << 0                              /* qualifier difference */
        << 3                              /* parameter mismatch */
        << 3 << Arg2->getType() << SizeTy;

  return false;
}

bool Sema::SemaBuiltinUnorderedCompare(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 2))
    return true;

  ExprResult OrigArg0 = TheCall->getArg(0);
  ExprResult OrigArg1 = TheCall->getArg(1);

  // Do standard promotions between the two arguments, returning their common
  // type.
  QualType Res = UsualArithmeticConversions(
      OrigArg0, OrigArg1, TheCall->getExprLoc(), ACK_Comparison);
  if (OrigArg0.isInvalid() || OrigArg1.isInvalid())
    return true;

  // Make sure any conversions are pushed back into the call; this is
  // type safe since unordered compare builtins are declared as "_Bool
  // foo(...)".
  TheCall->setArg(0, OrigArg0.get());
  TheCall->setArg(1, OrigArg1.get());

  // If the common type isn't a real floating type, then the arguments were
  // invalid for this operation.
  if (Res.isNull() || !Res->isRealFloatingType())
    return Diag(OrigArg0.get()->getBeginLoc(),
                diag::err_typecheck_call_invalid_ordered_compare)
           << OrigArg0.get()->getType() << OrigArg1.get()->getType()
           << SourceRange(OrigArg0.get()->getBeginLoc(),
                          OrigArg1.get()->getEndLoc());

  return false;
}

bool Sema::SemaBuiltinFPClassification(CallExpr *TheCall, unsigned NumArgs) {
  if (checkArgCount(*this, TheCall, NumArgs))
    return true;

  bool IsFPClass = NumArgs == 2;

  // Find out position of floating-point argument.
  unsigned FPArgNo = IsFPClass ? 0 : NumArgs - 1;

  // We can count on all parameters preceding the floating-point just being int.
  // Try all of those.
  for (unsigned i = 0; i < FPArgNo; ++i) {
    Expr *Arg = TheCall->getArg(i);

    ExprResult Res = PerformImplicitConversion(Arg, Context.IntTy, AA_Passing);
    if (Res.isInvalid())
      return true;
    TheCall->setArg(i, Res.get());
  }

  Expr *OrigArg = TheCall->getArg(FPArgNo);

  // Usual Unary Conversions will convert half to float, which we want for
  // machines that use fp16 conversion intrinsics. Else, we wnat to leave the
  // type how it is, but do normal L->Rvalue conversions.
  if (Context.getTargetInfo().useFP16ConversionIntrinsics())
    OrigArg = UsualUnaryConversions(OrigArg).get();
  else
    OrigArg = DefaultFunctionArrayLvalueConversion(OrigArg).get();
  TheCall->setArg(FPArgNo, OrigArg);

  QualType VectorResultTy;
  QualType ElementTy = OrigArg->getType();
  if (ElementTy->isVectorType() && IsFPClass) {
    VectorResultTy = GetSignedVectorType(ElementTy);
    ElementTy = ElementTy->getAs<VectorType>()->getElementType();
  }

  // This operation requires a non-_Complex floating-point number.
  if (!ElementTy->isRealFloatingType())
    return Diag(OrigArg->getBeginLoc(),
                diag::err_typecheck_call_invalid_unary_fp)
           << OrigArg->getType() << OrigArg->getSourceRange();

  // __builtin_isfpclass has integer parameter that specify test mask. It is
  // passed in (...), so it should be analyzed completely here.
  if (IsFPClass)
    if (SemaBuiltinConstantArgRange(TheCall, 1, 0, llvm::fcAllFlags))
      return true;

  if (IsFPClass) {
    QualType ResultTy;
    if (!VectorResultTy.isNull())
      ResultTy = VectorResultTy;
    else
      ResultTy = Context.IntTy;
    TheCall->setType(ResultTy);
  }

  return false;
}

bool Sema::SemaBuiltinComplex(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 2))
    return true;

  for (unsigned I = 0; I != 2; ++I) {
    Expr *Arg = TheCall->getArg(I);
    QualType T = Arg->getType();

    // Despite supporting _Complex int, GCC requires a real floating point type
    // for the operands of __builtin_complex.
    if (!T->isRealFloatingType()) {
      return Diag(Arg->getBeginLoc(), diag::err_typecheck_call_requires_real_fp)
             << Arg->getType() << Arg->getSourceRange();
    }

    ExprResult Converted = DefaultLvalueConversion(Arg);
    if (Converted.isInvalid())
      return true;
    TheCall->setArg(I, Converted.get());
  }

  Expr *Real = TheCall->getArg(0);
  Expr *Imag = TheCall->getArg(1);
  if (!Context.hasSameType(Real->getType(), Imag->getType())) {
    return Diag(Real->getBeginLoc(),
                diag::err_typecheck_call_different_arg_types)
           << Real->getType() << Imag->getType() << Real->getSourceRange()
           << Imag->getSourceRange();
  }

  // We don't allow _Complex _Float16 nor _Complex __fp16 as type specifiers;
  // don't allow this builtin to form those types either.
  if (Real->getType()->isFloat16Type())
    return Diag(TheCall->getBeginLoc(), diag::err_invalid_complex_spec)
           << tok::getKeywordSpelling(tok::kw__Float16);
  if (Real->getType()->isHalfType())
    return Diag(TheCall->getBeginLoc(), diag::err_invalid_complex_spec)
           << "half";

  TheCall->setType(Context.getComplexType(Real->getType()));
  return false;
}

// Customized Sema Checking for VSX builtins that have the following signature:
// vector [...] builtinName(vector [...], vector [...], const int);
// Which takes the same type of vectors (any legal vector type) for the first
// two arguments and takes compile time constant for the third argument.
// Example builtins are :
// This is declared to take (...), so we have to check everything.
ExprResult Sema::SemaBuiltinShuffleVector(CallExpr *TheCall) {
  if (TheCall->getNumArgs() < 2)
    return ExprError(Diag(TheCall->getEndLoc(),
                          diag::err_typecheck_call_too_few_args_at_least)
                     << 2 << TheCall->getNumArgs()
                     << TheCall->getSourceRange());

  // Determine which of the following types of shufflevector we're checking:
  // 1) unary, vector mask: (lhs, mask)
  // 2) binary, scalar mask: (lhs, rhs, index, ..., index)
  QualType resType = TheCall->getArg(0)->getType();
  unsigned numElements = 0;

  {
    QualType LHSType = TheCall->getArg(0)->getType();
    QualType RHSType = TheCall->getArg(1)->getType();

    if (!LHSType->isVectorType() || !RHSType->isVectorType())
      return ExprError(
          Diag(TheCall->getBeginLoc(), diag::err_vec_builtin_non_vector)
          << TheCall->getDirectCallee()
          << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                         TheCall->getArg(1)->getEndLoc()));

    numElements = LHSType->castAs<VectorType>()->getNumElements();
    unsigned numResElements = TheCall->getNumArgs() - 2;

    // Check to see if we have a call with 2 vector arguments, the unary shuffle
    // with mask.  If so, verify that RHS is an integer vector type with the
    // same number of elts as lhs.
    if (TheCall->getNumArgs() == 2) {
      if (!RHSType->hasIntegerRepresentation() ||
          RHSType->castAs<VectorType>()->getNumElements() != numElements)
        return ExprError(Diag(TheCall->getBeginLoc(),
                              diag::err_vec_builtin_incompatible_vector)
                         << TheCall->getDirectCallee()
                         << SourceRange(TheCall->getArg(1)->getBeginLoc(),
                                        TheCall->getArg(1)->getEndLoc()));
    } else if (!Context.hasSameUnqualifiedType(LHSType, RHSType)) {
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_vec_builtin_incompatible_vector)
                       << TheCall->getDirectCallee()
                       << SourceRange(TheCall->getArg(0)->getBeginLoc(),
                                      TheCall->getArg(1)->getEndLoc()));
    } else if (numElements != numResElements) {
      QualType eltType = LHSType->castAs<VectorType>()->getElementType();
      resType =
          Context.getVectorType(eltType, numResElements, VectorKind::Generic);
    }
  }

  for (unsigned i = 2; i < TheCall->getNumArgs(); i++) {

    std::optional<llvm::APSInt> Result;
    if (!(Result = TheCall->getArg(i)->getIntegerConstantExpr(Context)))
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_shufflevector_nonconstant_argument)
                       << TheCall->getArg(i)->getSourceRange());

    // Allow -1 which will be translated to undef in the IR.
    if (Result->isSigned() && Result->isAllOnes())
      continue;

    if (Result->getActiveBits() > 64 ||
        Result->getZExtValue() >= numElements * 2)
      return ExprError(Diag(TheCall->getBeginLoc(),
                            diag::err_shufflevector_argument_too_large)
                       << TheCall->getArg(i)->getSourceRange());
  }

  llvm::SmallVector<Expr *, 32> exprs;

  for (unsigned i = 0, e = TheCall->getNumArgs(); i != e; i++) {
    exprs.push_back(TheCall->getArg(i));
    TheCall->setArg(i, nullptr);
  }

  return new (Context) ShuffleVectorExpr(Context, exprs, resType,
                                         TheCall->getCallee()->getBeginLoc(),
                                         TheCall->getRParenLoc());
}

ExprResult Sema::SemaConvertVectorExpr(Expr *E, TypeSourceInfo *TInfo,
                                       SourceLocation BuiltinLoc,
                                       SourceLocation RParenLoc) {
  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType DstTy = TInfo->getType();
  QualType SrcTy = E->getType();

  if (!SrcTy->isVectorType())
    return ExprError(Diag(BuiltinLoc, diag::err_convertvector_non_vector)
                     << E->getSourceRange());
  if (!DstTy->isVectorType())
    return ExprError(Diag(BuiltinLoc, diag::err_builtin_non_vector_type)
                     << "second"
                     << "__builtin_convertvector");

  {
    unsigned SrcElts = SrcTy->castAs<VectorType>()->getNumElements();
    unsigned DstElts = DstTy->castAs<VectorType>()->getNumElements();
    if (SrcElts != DstElts)
      return ExprError(
          Diag(BuiltinLoc, diag::err_convertvector_incompatible_vector)
          << E->getSourceRange());
  }

  return new (Context)
      ConvertVectorExpr(E, TInfo, DstTy, VK, OK, BuiltinLoc, RParenLoc);
}

// This is declared to take (const void*, ...) and can take two
// optional constant int args.
bool Sema::SemaBuiltinPrefetch(CallExpr *TheCall) {
  unsigned NumArgs = TheCall->getNumArgs();

  if (NumArgs > 3)
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
           << 3 << NumArgs << TheCall->getSourceRange();

  // Argument 0 is checked for us and the remaining arguments must be
  // constant integers.
  for (unsigned i = 1; i != NumArgs; ++i)
    if (SemaBuiltinConstantArgRange(TheCall, i, 0, i == 1 ? 1 : 3))
      return true;

  return false;
}

bool Sema::SemaBuiltinArithmeticFence(CallExpr *TheCall) {
  if (!Context.getTargetInfo().checkArithmeticFenceSupported())
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_target_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());
  if (checkArgCount(*this, TheCall, 1))
    return true;
  Expr *Arg = TheCall->getArg(0);
  QualType ArgTy = Arg->getType();
  if (!ArgTy->hasFloatingRepresentation())
    return Diag(TheCall->getEndLoc(), diag::err_typecheck_expect_flt_or_vector)
           << ArgTy;
  if (Arg->isLValue()) {
    ExprResult FirstArg = DefaultLvalueConversion(Arg);
    TheCall->setArg(0, FirstArg.get());
  }
  TheCall->setType(TheCall->getArg(0)->getType());
  return false;
}

// __assume does not evaluate its arguments, and should warn if its argument
// has side effects.
bool Sema::SemaBuiltinAssume(CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(0);
  if (Arg->HasSideEffects(Context))
    Diag(Arg->getBeginLoc(), diag::warn_assume_side_effects)
        << Arg->getSourceRange()
        << cast<FunctionDecl>(TheCall->getCalleeDecl())->getIdentifier();

  return false;
}

bool Sema::SemaBuiltinAllocaWithAlign(CallExpr *TheCall) {
  // The alignment must be a constant integer.
  Expr *Arg = TheCall->getArg(1);

  {
    if (const auto *UE =
            dyn_cast<UnaryExprOrTypeTraitExpr>(Arg->IgnoreParenImpCasts()))
      if (UE->getKind() == UETT_AlignOf ||
          UE->getKind() == UETT_PreferredAlignOf)
        Diag(TheCall->getBeginLoc(), diag::warn_alloca_align_alignof)
            << Arg->getSourceRange();

    llvm::APSInt Result = Arg->EvaluateKnownConstInt(Context);

    if (!Result.isPowerOf2())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_not_power_of_two)
             << Arg->getSourceRange();

    if (Result < Context.getCharWidth())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_too_small)
             << (unsigned)Context.getCharWidth() << Arg->getSourceRange();

    if (Result > std::numeric_limits<int32_t>::max())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_too_big)
             << std::numeric_limits<int32_t>::max() << Arg->getSourceRange();
  }

  return false;
}

bool Sema::SemaBuiltinAssumeAligned(CallExpr *TheCall) {
  if (checkArgCountRange(*this, TheCall, 2, 3))
    return true;

  unsigned NumArgs = TheCall->getNumArgs();
  Expr *FirstArg = TheCall->getArg(0);

  {
    ExprResult FirstArgResult = DefaultFunctionArrayLvalueConversion(FirstArg);
    if (checkBuiltinArgument(*this, TheCall, 0))
      return true;
    /// In-place updation of FirstArg by checkBuiltinArgument is ignored.
    TheCall->setArg(0, FirstArgResult.get());
  }

  // The alignment must be a constant integer.
  Expr *SecondArg = TheCall->getArg(1);

  {
    llvm::APSInt Result;
    if (SemaBuiltinConstantArg(TheCall, 1, Result))
      return true;

    if (!Result.isPowerOf2())
      return Diag(TheCall->getBeginLoc(), diag::err_alignment_not_power_of_two)
             << SecondArg->getSourceRange();

    if (Result > Sema::MaximumAlignment)
      Diag(TheCall->getBeginLoc(), diag::warn_assume_aligned_too_great)
          << SecondArg->getSourceRange() << Sema::MaximumAlignment;
  }

  if (NumArgs > 2) {
    Expr *ThirdArg = TheCall->getArg(2);
    if (convertArgumentToType(*this, ThirdArg, Context.getSizeType()))
      return true;
    TheCall->setArg(2, ThirdArg);
  }

  return false;
}

bool Sema::SemaBuiltinOSLogFormat(CallExpr *TheCall) {
  unsigned BuiltinID =
      cast<FunctionDecl>(TheCall->getCalleeDecl())->getBuiltinID();
  bool IsSizeCall = BuiltinID == Builtin::BI__builtin_os_log_format_buffer_size;

  unsigned NumArgs = TheCall->getNumArgs();
  unsigned NumRequiredArgs = IsSizeCall ? 1 : 2;
  if (NumArgs < NumRequiredArgs) {
    return Diag(TheCall->getEndLoc(), diag::err_typecheck_call_too_few_args)
           << NumRequiredArgs << NumArgs << TheCall->getSourceRange();
  }
  if (NumArgs >= NumRequiredArgs + 0x100) {
    return Diag(TheCall->getEndLoc(),
                diag::err_typecheck_call_too_many_args_at_most)
           << (NumRequiredArgs + 0xff) << NumArgs << TheCall->getSourceRange();
  }
  unsigned i = 0;

  // For formatting call, check buffer arg.
  if (!IsSizeCall) {
    ExprResult Arg(TheCall->getArg(i));
    InitializedEntity Entity = InitializedEntity::InitializeParameter(
        Context, Context.VoidPtrTy, false);
    Arg = PerformCopyInitialization(Entity, SourceLocation(), Arg);
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(i, Arg.get());
    i++;
  }
  unsigned FormatIdx = i;
  {
    ExprResult Arg = CheckOSLogFormatStringArg(TheCall->getArg(i));
    if (Arg.isInvalid())
      return true;
    TheCall->setArg(i, Arg.get());
    i++;
  }

  // Make sure variadic args are scalar.
  unsigned FirstDataArg = i;
  while (i < NumArgs) {
    ExprResult Arg = DefaultVariadicArgumentPromotion(TheCall->getArg(i));
    if (Arg.isInvalid())
      return true;
    CharUnits ArgSize = Context.getTypeSizeInChars(Arg.get()->getType());
    if (ArgSize.getQuantity() >= 0x100) {
      return Diag(Arg.get()->getEndLoc(), diag::err_os_log_argument_too_big)
             << i << (int)ArgSize.getQuantity() << 0xff
             << TheCall->getSourceRange();
    }
    TheCall->setArg(i, Arg.get());
    i++;
  }

  // Check formatting specifiers. NOTE: We're only doing this for the non-size
  // call to avoid duplicate diagnostics.
  if (!IsSizeCall) {
    llvm::SmallBitVector CheckedVarArgs(NumArgs, false);
    llvm::ArrayRef<const Expr *> Args(TheCall->getArgs(),
                                      TheCall->getNumArgs());
    bool Success = CheckFormatArguments(
        Args, FAPK_Variadic, FormatIdx, FirstDataArg, FST_OSLog,
        VariadicFunction, TheCall->getBeginLoc(), SourceRange(),
        CheckedVarArgs);
    if (!Success)
      return true;
  }

  if (IsSizeCall) {
    TheCall->setType(Context.getSizeType());
  } else {
    TheCall->setType(Context.VoidPtrTy);
  }
  return false;
}

bool Sema::SemaBuiltinConstantArg(CallExpr *TheCall, int ArgNum,
                                  llvm::APSInt &Result) {
  Expr *Arg = TheCall->getArg(ArgNum);
  DeclRefExpr *DRE =
      cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
  FunctionDecl *FDecl = cast<FunctionDecl>(DRE->getDecl());

  std::optional<llvm::APSInt> R;
  if (!(R = Arg->getIntegerConstantExpr(Context)))
    return Diag(TheCall->getBeginLoc(), diag::err_constant_integer_arg_type)
           << FDecl->getDeclName() << Arg->getSourceRange();
  Result = *R;
  return false;
}

bool Sema::SemaBuiltinConstantArgRange(CallExpr *TheCall, int ArgNum, int Low,
                                       int High, bool RangeIsError) {
  if (isConstantEvaluatedContext())
    return false;
  llvm::APSInt Result;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  if (Result.getSExtValue() < Low || Result.getSExtValue() > High) {
    if (RangeIsError)
      return Diag(TheCall->getBeginLoc(), diag::err_argument_invalid_range)
             << toString(Result, 10) << Low << High << Arg->getSourceRange();
    else
      // Defer the warning until we know if the code will be emitted so that
      // dead code can ignore this.
      DiagRuntimeBehavior(TheCall->getBeginLoc(), TheCall,
                          PDiag(diag::warn_argument_invalid_range)
                              << toString(Result, 10) << Low << High
                              << Arg->getSourceRange());
  }

  return false;
}

bool Sema::SemaBuiltinConstantArgMultiple(CallExpr *TheCall, int ArgNum,
                                          unsigned Num) {
  llvm::APSInt Result;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  if (Result.getSExtValue() % Num != 0)
    return Diag(TheCall->getBeginLoc(), diag::err_argument_not_multiple)
           << Num << Arg->getSourceRange();

  return false;
}

bool Sema::SemaBuiltinConstantArgPower2(CallExpr *TheCall, int ArgNum) {
  llvm::APSInt Result;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  // Bit-twiddling to test for a power of 2: for x > 0, x & (x-1) is zero if
  // and only if x is a power of 2.
  if (Result.isStrictlyPositive() && (Result & (Result - 1)) == 0)
    return false;

  return Diag(TheCall->getBeginLoc(), diag::err_argument_not_power_of_2)
         << Arg->getSourceRange();
}

bool Sema::SemaBuiltinARMMemoryTaggingCall(unsigned BuiltinID,
                                           CallExpr *TheCall) {
  if (BuiltinID == AArch64::BI__builtin_arm_irg) {
    if (checkArgCount(*this, TheCall, 2))
      return true;
    Expr *Arg0 = TheCall->getArg(0);
    Expr *Arg1 = TheCall->getArg(1);

    ExprResult FirstArg = DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;
    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();
    TheCall->setArg(0, FirstArg.get());

    ExprResult SecArg = DefaultLvalueConversion(Arg1);
    if (SecArg.isInvalid())
      return true;
    QualType SecArgType = SecArg.get()->getType();
    if (!SecArgType->isIntegerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_integer)
             << "second" << SecArgType << Arg1->getSourceRange();

    // Derive the return type from the pointer argument.
    TheCall->setType(FirstArgType);
    return false;
  }

  if (BuiltinID == AArch64::BI__builtin_arm_addg) {
    if (checkArgCount(*this, TheCall, 2))
      return true;

    Expr *Arg0 = TheCall->getArg(0);
    ExprResult FirstArg = DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;
    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();
    TheCall->setArg(0, FirstArg.get());

    // Derive the return type from the pointer argument.
    TheCall->setType(FirstArgType);

    // Second arg must be an constant in range [0,15]
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 15);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_gmi) {
    if (checkArgCount(*this, TheCall, 2))
      return true;
    Expr *Arg0 = TheCall->getArg(0);
    Expr *Arg1 = TheCall->getArg(1);

    ExprResult FirstArg = DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;
    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();

    QualType SecArgType = Arg1->getType();
    if (!SecArgType->isIntegerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_integer)
             << "second" << SecArgType << Arg1->getSourceRange();
    TheCall->setType(Context.IntTy);
    return false;
  }

  if (BuiltinID == AArch64::BI__builtin_arm_ldg ||
      BuiltinID == AArch64::BI__builtin_arm_stg) {
    if (checkArgCount(*this, TheCall, 1))
      return true;
    Expr *Arg0 = TheCall->getArg(0);
    ExprResult FirstArg = DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;

    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();
    TheCall->setArg(0, FirstArg.get());

    // Derive the return type from the pointer argument.
    if (BuiltinID == AArch64::BI__builtin_arm_ldg)
      TheCall->setType(FirstArgType);
    return false;
  }

  if (BuiltinID == AArch64::BI__builtin_arm_subp) {
    Expr *ArgA = TheCall->getArg(0);
    Expr *ArgB = TheCall->getArg(1);

    ExprResult ArgExprA = DefaultFunctionArrayLvalueConversion(ArgA);
    ExprResult ArgExprB = DefaultFunctionArrayLvalueConversion(ArgB);

    if (ArgExprA.isInvalid() || ArgExprB.isInvalid())
      return true;

    QualType ArgTypeA = ArgExprA.get()->getType();
    QualType ArgTypeB = ArgExprB.get()->getType();

    auto isNull = [&](Expr *E) -> bool {
      return E->isNullPointerConstant(Context,
                                      Expr::NPC_ValueDependentIsNotNull);
    };

    // argument should be either a pointer or null
    if (!ArgTypeA->isAnyPointerType() && !isNull(ArgA))
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_null_or_pointer)
             << "first" << ArgTypeA << ArgA->getSourceRange();

    if (!ArgTypeB->isAnyPointerType() && !isNull(ArgB))
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_null_or_pointer)
             << "second" << ArgTypeB << ArgB->getSourceRange();

    // Ensure Pointee types are compatible
    if (ArgTypeA->isAnyPointerType() && !isNull(ArgA) &&
        ArgTypeB->isAnyPointerType() && !isNull(ArgB)) {
      QualType pointeeA = ArgTypeA->getPointeeType();
      QualType pointeeB = ArgTypeB->getPointeeType();
      if (!Context.typesAreCompatible(
              Context.getCanonicalType(pointeeA).getUnqualifiedType(),
              Context.getCanonicalType(pointeeB).getUnqualifiedType())) {
        return Diag(TheCall->getBeginLoc(),
                    diag::err_typecheck_sub_ptr_compatible)
               << ArgTypeA << ArgTypeB << ArgA->getSourceRange()
               << ArgB->getSourceRange();
      }
    }

    // at least one argument should be pointer type
    if (!ArgTypeA->isAnyPointerType() && !ArgTypeB->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_any2arg_pointer)
             << ArgTypeA << ArgTypeB << ArgA->getSourceRange();

    if (isNull(ArgA)) // adopt type of the other pointer
      ArgExprA = ImpCastExprToType(ArgExprA.get(), ArgTypeB, CK_NullToPointer);

    if (isNull(ArgB))
      ArgExprB = ImpCastExprToType(ArgExprB.get(), ArgTypeA, CK_NullToPointer);

    TheCall->setArg(0, ArgExprA.get());
    TheCall->setArg(1, ArgExprB.get());
    TheCall->setType(Context.LongLongTy);
    return false;
  }
  assert(false && "Unhandled AArch64 MTE intrinsic");
  return true;
}

bool Sema::SemaBuiltinARMSpecialReg(unsigned BuiltinID, CallExpr *TheCall,
                                    int ArgNum, unsigned ExpectedFieldNum,
                                    bool AllowName) {
  bool IsAArch64Builtin = BuiltinID == AArch64::BI__builtin_arm_rsr64 ||
                          BuiltinID == AArch64::BI__builtin_arm_wsr64 ||
                          BuiltinID == AArch64::BI__builtin_arm_rsr128 ||
                          BuiltinID == AArch64::BI__builtin_arm_wsr128 ||
                          BuiltinID == AArch64::BI__builtin_arm_rsr ||
                          BuiltinID == AArch64::BI__builtin_arm_rsrp ||
                          BuiltinID == AArch64::BI__builtin_arm_wsr ||
                          BuiltinID == AArch64::BI__builtin_arm_wsrp;
  assert(IsAArch64Builtin && "Unexpected AArch64 builtin.");
  bool IsARMBuiltin = false;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (!isa<StringLiteral>(Arg->IgnoreParenImpCasts()))
    return Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
           << Arg->getSourceRange();

  llvm::StringRef Reg =
      cast<StringLiteral>(Arg->IgnoreParenImpCasts())->getString();
  llvm::SmallVector<llvm::StringRef, 6> Fields;
  Reg.split(Fields, ":");

  if (Fields.size() != ExpectedFieldNum && !(AllowName && Fields.size() == 1))
    return Diag(TheCall->getBeginLoc(), diag::err_arm_invalid_specialreg)
           << Arg->getSourceRange();

  // If the string is the name of a register then we cannot check that it is
  // valid here but if the string is of one the forms described in ACLE then we
  // can check that the supplied fields are integers and within the valid
  // ranges.
  if (Fields.size() > 1) {
    bool FiveFields = Fields.size() == 5;

    bool ValidString = true;
    if (IsARMBuiltin) {
      ValidString &= Fields[0].starts_with_insensitive("cp") ||
                     Fields[0].starts_with_insensitive("p");
      if (ValidString)
        Fields[0] = Fields[0].drop_front(
            Fields[0].starts_with_insensitive("cp") ? 2 : 1);

      ValidString &= Fields[2].starts_with_insensitive("c");
      if (ValidString)
        Fields[2] = Fields[2].drop_front(1);

      if (FiveFields) {
        ValidString &= Fields[3].starts_with_insensitive("c");
        if (ValidString)
          Fields[3] = Fields[3].drop_front(1);
      }
    }

    llvm::SmallVector<int, 5> Ranges;
    if (FiveFields)
      Ranges.append({IsAArch64Builtin ? 1 : 15, 7, 15, 15, 7});
    else
      Ranges.append({15, 7, 15});

    for (unsigned i = 0; i < Fields.size(); ++i) {
      int IntField;
      ValidString &= !Fields[i].getAsInteger(10, IntField);
      ValidString &= (IntField >= 0 && IntField <= Ranges[i]);
    }

    if (!ValidString)
      return Diag(TheCall->getBeginLoc(), diag::err_arm_invalid_specialreg)
             << Arg->getSourceRange();
  } else if (IsAArch64Builtin && Fields.size() == 1) {
    // This code validates writes to PSTATE registers.

    // Not a write.
    if (TheCall->getNumArgs() != 2)
      return false;

    // The 128-bit system register accesses do not touch PSTATE.
    if (BuiltinID == AArch64::BI__builtin_arm_rsr128 ||
        BuiltinID == AArch64::BI__builtin_arm_wsr128)
      return false;

    // These are the named PSTATE accesses using "MSR (immediate)" instructions,
    // along with the upper limit on the immediates allowed.
    auto MaxLimit = llvm::StringSwitch<std::optional<unsigned>>(Reg)
                        .CaseLower("spsel", 15)
                        .CaseLower("daifclr", 15)
                        .CaseLower("daifset", 15)
                        .CaseLower("pan", 15)
                        .CaseLower("uao", 15)
                        .CaseLower("dit", 15)
                        .CaseLower("ssbs", 15)
                        .CaseLower("tco", 15)
                        .CaseLower("allint", 1)
                        .CaseLower("pm", 1)
                        .Default(std::nullopt);

    // If this is not a named PSTATE, just continue without validating, as this
    // will be lowered to an "MSR (register)" instruction directly
    if (!MaxLimit)
      return false;

    // Here we only allow constants in the range for that pstate, as required by
    // the ACLE.
    //
    // While NeverC also accepts the names of system registers in its ACLE
    // intrinsics, we prevent this with the PSTATE names used in MSR (immediate)
    // as the value written via a register is different to the value used as an
    // immediate to have the same effect. e.g., for the instruction `msr tco,
    // x0`, it is bit 25 of register x0 that is written into PSTATE.TCO, but
    // with `msr tco, #imm`, it is bit 0 of xN that is written into PSTATE.TCO.
    //
    // If a programmer wants to codegen the MSR (register) form of `msr tco,
    // xN`, they can still do so by specifying the register using five
    // colon-separated numbers in a string.
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, *MaxLimit);
  }

  return false;
}

bool Sema::SemaBuiltinLongjmp(CallExpr *TheCall) {
  if (!Context.getTargetInfo().hasSjLjLowering())
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_longjmp_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());

  Expr *Arg = TheCall->getArg(1);
  llvm::APSInt Result;

  if (SemaBuiltinConstantArg(TheCall, 1, Result))
    return true;

  if (Result != 1)
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_longjmp_invalid_val)
           << SourceRange(Arg->getBeginLoc(), Arg->getEndLoc());

  return false;
}

bool Sema::SemaBuiltinSetjmp(CallExpr *TheCall) {
  if (!Context.getTargetInfo().hasSjLjLowering())
    return Diag(TheCall->getBeginLoc(), diag::err_builtin_setjmp_unsupported)
           << SourceRange(TheCall->getBeginLoc(), TheCall->getEndLoc());
  return false;
}

//===--- CHECK: Warn on use of wrong absolute value function. -------------===//

// Returns the related absolute value function that is larger, of 0 if one
// does not exist.
namespace {
unsigned getLargerAbsoluteValueFunction(unsigned AbsFunction) {
  switch (AbsFunction) {
  default:
    return 0;

  case Builtin::BI__builtin_abs:
    return Builtin::BI__builtin_labs;
  case Builtin::BI__builtin_labs:
    return Builtin::BI__builtin_llabs;
  case Builtin::BI__builtin_llabs:
    return 0;

  case Builtin::BI__builtin_fabsf:
    return Builtin::BI__builtin_fabs;
  case Builtin::BI__builtin_fabs:
    return Builtin::BI__builtin_fabsl;
  case Builtin::BI__builtin_fabsl:
    return 0;

  case Builtin::BI__builtin_cabsf:
    return Builtin::BI__builtin_cabs;
  case Builtin::BI__builtin_cabs:
    return Builtin::BI__builtin_cabsl;
  case Builtin::BI__builtin_cabsl:
    return 0;

  case Builtin::BIabs:
    return Builtin::BIlabs;
  case Builtin::BIlabs:
    return Builtin::BIllabs;
  case Builtin::BIllabs:
    return 0;

  case Builtin::BIfabsf:
    return Builtin::BIfabs;
  case Builtin::BIfabs:
    return Builtin::BIfabsl;
  case Builtin::BIfabsl:
    return 0;

  case Builtin::BIcabsf:
    return Builtin::BIcabs;
  case Builtin::BIcabs:
    return Builtin::BIcabsl;
  case Builtin::BIcabsl:
    return 0;
  }
}

// Returns the argument type of the absolute value function.
QualType getAbsoluteValueArgumentType(TreeContext &Context, unsigned AbsType) {
  if (AbsType == 0)
    return QualType();

  TreeContext::GetBuiltinTypeError Error = TreeContext::GE_None;
  QualType BuiltinType = Context.GetBuiltinType(AbsType, Error);
  if (Error != TreeContext::GE_None)
    return QualType();

  const FunctionProtoType *FT = BuiltinType->getAs<FunctionProtoType>();
  if (!FT)
    return QualType();

  if (FT->getNumParams() != 1)
    return QualType();

  return FT->getParamType(0);
}
} // namespace

// Returns the best absolute value function, or zero, based on type and
// current absolute value function.
namespace {
unsigned getBestAbsFunction(TreeContext &Context, QualType ArgType,
                            unsigned AbsFunctionKind) {
  unsigned BestKind = 0;
  uint64_t ArgSize = Context.getTypeSize(ArgType);
  for (unsigned Kind = AbsFunctionKind; Kind != 0;
       Kind = getLargerAbsoluteValueFunction(Kind)) {
    QualType ParamType = getAbsoluteValueArgumentType(Context, Kind);
    if (Context.getTypeSize(ParamType) >= ArgSize) {
      if (BestKind == 0)
        BestKind = Kind;
      else if (Context.hasSameType(ParamType, ArgType)) {
        BestKind = Kind;
        break;
      }
    }
  }
  return BestKind;
}
} // namespace

enum AbsoluteValueKind { AVK_Integer, AVK_Floating, AVK_Complex };

namespace {
AbsoluteValueKind getAbsoluteValueKind(QualType T) {
  if (T->isIntegralOrEnumerationType())
    return AVK_Integer;
  if (T->isRealFloatingType())
    return AVK_Floating;
  if (T->isAnyComplexType())
    return AVK_Complex;

  llvm_unreachable("Type not integer, floating, or complex");
}
} // namespace

// Changes the absolute value function to a different type.  Preserves whether
// the function is a builtin.
namespace {
unsigned changeAbsFunction(unsigned AbsKind, AbsoluteValueKind ValueKind) {
  switch (ValueKind) {
  case AVK_Integer:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_fabsf:
    case Builtin::BI__builtin_fabs:
    case Builtin::BI__builtin_fabsl:
    case Builtin::BI__builtin_cabsf:
    case Builtin::BI__builtin_cabs:
    case Builtin::BI__builtin_cabsl:
      return Builtin::BI__builtin_abs;
    case Builtin::BIfabsf:
    case Builtin::BIfabs:
    case Builtin::BIfabsl:
    case Builtin::BIcabsf:
    case Builtin::BIcabs:
    case Builtin::BIcabsl:
      return Builtin::BIabs;
    }
  case AVK_Floating:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_abs:
    case Builtin::BI__builtin_labs:
    case Builtin::BI__builtin_llabs:
    case Builtin::BI__builtin_cabsf:
    case Builtin::BI__builtin_cabs:
    case Builtin::BI__builtin_cabsl:
      return Builtin::BI__builtin_fabsf;
    case Builtin::BIabs:
    case Builtin::BIlabs:
    case Builtin::BIllabs:
    case Builtin::BIcabsf:
    case Builtin::BIcabs:
    case Builtin::BIcabsl:
      return Builtin::BIfabsf;
    }
  case AVK_Complex:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_abs:
    case Builtin::BI__builtin_labs:
    case Builtin::BI__builtin_llabs:
    case Builtin::BI__builtin_fabsf:
    case Builtin::BI__builtin_fabs:
    case Builtin::BI__builtin_fabsl:
      return Builtin::BI__builtin_cabsf;
    case Builtin::BIabs:
    case Builtin::BIlabs:
    case Builtin::BIllabs:
    case Builtin::BIfabsf:
    case Builtin::BIfabs:
    case Builtin::BIfabsl:
      return Builtin::BIcabsf;
    }
  }
  llvm_unreachable("Unable to convert function");
}

unsigned getAbsoluteValueFunctionKind(const FunctionDecl *FDecl) {
  const IdentifierInfo *FnInfo = FDecl->getIdentifier();
  if (!FnInfo)
    return 0;

  switch (FDecl->getBuiltinID()) {
  default:
    return 0;
  case Builtin::BI__builtin_abs:
  case Builtin::BI__builtin_fabs:
  case Builtin::BI__builtin_fabsf:
  case Builtin::BI__builtin_fabsl:
  case Builtin::BI__builtin_labs:
  case Builtin::BI__builtin_llabs:
  case Builtin::BI__builtin_cabs:
  case Builtin::BI__builtin_cabsf:
  case Builtin::BI__builtin_cabsl:
  case Builtin::BIabs:
  case Builtin::BIlabs:
  case Builtin::BIllabs:
  case Builtin::BIfabs:
  case Builtin::BIfabsf:
  case Builtin::BIfabsl:
  case Builtin::BIcabs:
  case Builtin::BIcabsf:
  case Builtin::BIcabsl:
    return FDecl->getBuiltinID();
  }
  llvm_unreachable("Unknown Builtin type");
}
} // namespace

// If the replacement is valid, emit a note with replacement function.
// Additionally, suggest including the proper header if not already included.
namespace {
void emitReplacement(Sema &S, SourceLocation Loc, SourceRange Range,
                     unsigned AbsKind, QualType ArgType) {
  bool GenHeaderHint = true;
  const char *HeaderName = nullptr;
  llvm::StringRef FunctionName = S.Context.BuiltinInfo.getName(AbsKind);
  HeaderName = S.Context.BuiltinInfo.getHeaderName(AbsKind);

  if (HeaderName) {
    DeclarationName DN(&S.Context.Idents.get(FunctionName));
    LookupResult R(S, DN, Loc, neverc::ResolveAny);
    R.suppressDiagnostics();
    S.ResolveName(R, S.getCurScope());

    if (R.isSingleResult()) {
      FunctionDecl *FD = dyn_cast<FunctionDecl>(R.getFoundDecl());
      if (FD && FD->getBuiltinID() == AbsKind) {
        GenHeaderHint = false;
      } else {
        return;
      }
    } else if (!R.empty()) {
      return;
    }
  }

  S.Diag(Loc, diag::note_replace_abs_function)
      << FunctionName << FixItHint::CreateReplacement(Range, FunctionName);

  if (!HeaderName)
    return;

  if (!GenHeaderHint)
    return;

  S.Diag(Loc, diag::note_include_header_or_declare)
      << HeaderName << FunctionName;
}
} // namespace

// ===----------------------------------------------------------------------===
// Diagnostics: abs, alignment, type tags, elementwise math & matrix
// ===----------------------------------------------------------------------===

void Sema::CheckAbsoluteValueFunction(const CallExpr *Call,
                                      const FunctionDecl *FDecl) {
  if (Call->getNumArgs() != 1)
    return;

  unsigned AbsKind = getAbsoluteValueFunctionKind(FDecl);
  if (AbsKind == 0)
    return;

  QualType ArgType = Call->getArg(0)->IgnoreParenImpCasts()->getType();
  QualType ParamType = Call->getArg(0)->getType();

  // Unsigned types cannot be negative.  Suggest removing the absolute value
  // function call.
  if (ArgType->isUnsignedIntegerType()) {
    llvm::StringRef FunctionName = Context.BuiltinInfo.getName(AbsKind);
    Diag(Call->getExprLoc(), diag::warn_unsigned_abs) << ArgType << ParamType;
    Diag(Call->getExprLoc(), diag::note_remove_abs)
        << FunctionName
        << FixItHint::CreateRemoval(Call->getCallee()->getSourceRange());
    return;
  }

  // Taking the absolute value of a pointer is very suspicious, they probably
  // wanted to index into an array, dereference a pointer, call a function, etc.
  if (ArgType->isPointerType() || ArgType->canDecayToPointerType()) {
    unsigned DiagType = 0;
    if (ArgType->isFunctionType())
      DiagType = 1;
    else if (ArgType->isArrayType())
      DiagType = 2;

    Diag(Call->getExprLoc(), diag::warn_pointer_abs) << DiagType << ArgType;
    return;
  }

  AbsoluteValueKind ArgValueKind = getAbsoluteValueKind(ArgType);
  AbsoluteValueKind ParamValueKind = getAbsoluteValueKind(ParamType);

  // The argument and parameter are the same kind.  Check if they are the right
  // size.
  if (ArgValueKind == ParamValueKind) {
    if (Context.getTypeSize(ArgType) <= Context.getTypeSize(ParamType))
      return;

    unsigned NewAbsKind = getBestAbsFunction(Context, ArgType, AbsKind);
    Diag(Call->getExprLoc(), diag::warn_abs_too_small)
        << FDecl << ArgType << ParamType;

    if (NewAbsKind == 0)
      return;

    emitReplacement(*this, Call->getExprLoc(),
                    Call->getCallee()->getSourceRange(), NewAbsKind, ArgType);
    return;
  }

  // ArgValueKind != ParamValueKind
  // The wrong type of absolute value function was used.  Attempt to find the
  // proper one.
  unsigned NewAbsKind = changeAbsFunction(AbsKind, ArgValueKind);
  NewAbsKind = getBestAbsFunction(Context, ArgType, NewAbsKind);
  if (NewAbsKind == 0)
    return;

  Diag(Call->getExprLoc(), diag::warn_wrong_absolute_value_type)
      << FDecl << ParamValueKind << ArgValueKind;

  emitReplacement(*this, Call->getExprLoc(),
                  Call->getCallee()->getSourceRange(), NewAbsKind, ArgType);
}

//===--- Layout compatibility ----------------------------------------------//

namespace {
bool isLayoutCompatible(TreeContext &C, QualType T1, QualType T2);

bool isLayoutCompatible(TreeContext &C, EnumDecl *ED1, EnumDecl *ED2) {
  // Same fixed underlying type => layout-compatible enums.
  return ED1->isComplete() && ED2->isComplete() &&
         C.hasSameType(ED1->getIntegerType(), ED2->getIntegerType());
}

bool isLayoutCompatible(TreeContext &C, FieldDecl *Field1, FieldDecl *Field2) {
  if (!isLayoutCompatible(C, Field1->getType(), Field2->getType()))
    return false;

  if (Field1->isBitField() != Field2->isBitField())
    return false;

  if (Field1->isBitField()) {
    // Make sure that the bit-fields are the same length.
    unsigned Bits1 = Field1->getBitWidthValue(C);
    unsigned Bits2 = Field2->getBitWidthValue(C);

    if (Bits1 != Bits2)
      return false;
  }

  return true;
}

bool isLayoutCompatibleStruct(TreeContext &C, RecordDecl *RD1,
                              RecordDecl *RD2) {
  RecordDecl::field_iterator Field2 = RD2->field_begin(),
                             Field2End = RD2->field_end(),
                             Field1 = RD1->field_begin(),
                             Field1End = RD1->field_end();
  for (; Field1 != Field1End && Field2 != Field2End; ++Field1, ++Field2) {
    if (!isLayoutCompatible(C, *Field1, *Field2))
      return false;
  }
  if (Field1 != Field1End || Field2 != Field2End)
    return false;

  return true;
}

bool isLayoutCompatibleUnion(TreeContext &C, RecordDecl *RD1, RecordDecl *RD2) {
  llvm::SmallPtrSet<FieldDecl *, 8> UnmatchedFields;
  for (auto *Field2 : RD2->fields())
    UnmatchedFields.insert(Field2);

  for (auto *Field1 : RD1->fields()) {
    llvm::SmallPtrSet<FieldDecl *, 8>::iterator I = UnmatchedFields.begin(),
                                                E = UnmatchedFields.end();

    for (; I != E; ++I) {
      if (isLayoutCompatible(C, Field1, *I)) {
        bool Result = UnmatchedFields.erase(*I);
        (void)Result;
        assert(Result);
        break;
      }
    }
    if (I == E)
      return false;
  }

  return UnmatchedFields.empty();
}

bool isLayoutCompatible(TreeContext &C, RecordDecl *RD1, RecordDecl *RD2) {
  if (RD1->isUnion() != RD2->isUnion())
    return false;

  if (RD1->isUnion())
    return isLayoutCompatibleUnion(C, RD1, RD2);
  else
    return isLayoutCompatibleStruct(C, RD1, RD2);
}

bool isLayoutCompatible(TreeContext &C, QualType T1, QualType T2) {
  if (T1.isNull() || T2.isNull())
    return false;

  // Identical types are always layout-compatible.
  if (C.hasSameType(T1, T2))
    return true;

  T1 = T1.getCanonicalType().getUnqualifiedType();
  T2 = T2.getCanonicalType().getUnqualifiedType();

  const Type::TypeClass TC1 = T1->getTypeClass();
  const Type::TypeClass TC2 = T2->getTypeClass();

  if (TC1 != TC2)
    return false;

  if (TC1 == Type::Enum) {
    return isLayoutCompatible(C, cast<EnumType>(T1)->getDecl(),
                              cast<EnumType>(T2)->getDecl());
  } else if (TC1 == Type::Record) {
    if (!T1->isStandardLayoutType() || !T2->isStandardLayoutType())
      return false;

    return isLayoutCompatible(C, cast<RecordType>(T1)->getDecl(),
                              cast<RecordType>(T2)->getDecl());
  }

  return false;
}
} // namespace

//===--- CHECK: pointer_with_type_tag attribute: datatypes should match ----//

namespace {
bool findTypeTagExpr(const Expr *TypeExpr, const TreeContext &Ctx,
                     const ValueDecl **VD, uint64_t *MagicValue,
                     bool isConstantEvaluated) {
  while (true) {
    if (!TypeExpr)
      return false;

    TypeExpr = TypeExpr->IgnoreParenImpCasts()->IgnoreParenCasts();

    switch (TypeExpr->getStmtClass()) {
    case Stmt::UnaryOperatorClass: {
      const UnaryOperator *UO = cast<UnaryOperator>(TypeExpr);
      if (UO->getOpcode() == UO_AddrOf || UO->getOpcode() == UO_Deref) {
        TypeExpr = UO->getSubExpr();
        continue;
      }
      return false;
    }

    case Stmt::DeclRefExprClass: {
      const DeclRefExpr *DRE = cast<DeclRefExpr>(TypeExpr);
      *VD = DRE->getDecl();
      return true;
    }

    case Stmt::IntegerLiteralClass: {
      const IntegerLiteral *IL = cast<IntegerLiteral>(TypeExpr);
      llvm::APInt MagicValueAPInt = IL->getValue();
      if (MagicValueAPInt.getActiveBits() <= 64) {
        *MagicValue = MagicValueAPInt.getZExtValue();
        return true;
      } else
        return false;
    }

    case Stmt::BinaryConditionalOperatorClass:
    case Stmt::ConditionalOperatorClass: {
      const AbstractConditionalOperator *ACO =
          cast<AbstractConditionalOperator>(TypeExpr);
      bool Result;
      if (ACO->getCond()->EvaluateAsBooleanCondition(Result, Ctx,
                                                     isConstantEvaluated)) {
        if (Result)
          TypeExpr = ACO->getTrueExpr();
        else
          TypeExpr = ACO->getFalseExpr();
        continue;
      }
      return false;
    }

    case Stmt::BinaryOperatorClass: {
      const BinaryOperator *BO = cast<BinaryOperator>(TypeExpr);
      if (BO->getOpcode() == BO_Comma) {
        TypeExpr = BO->getRHS();
        continue;
      }
      return false;
    }

    default:
      return false;
    }
  }
}

bool getMatchingCType(const IdentifierInfo *ArgumentKind, const Expr *TypeExpr,
                      const TreeContext &Ctx,
                      const llvm::DenseMap<Sema::TypeTagMagicValue,
                                           Sema::TypeTagData> *MagicValues,
                      bool &FoundWrongKind, Sema::TypeTagData &TypeInfo,
                      bool isConstantEvaluated) {
  FoundWrongKind = false;

  // Variable declaration that has type_tag_for_datatype attribute.
  const ValueDecl *VD = nullptr;

  uint64_t MagicValue;

  if (!findTypeTagExpr(TypeExpr, Ctx, &VD, &MagicValue, isConstantEvaluated))
    return false;

  if (VD) {
    if (TypeTagForDatatypeAttr *I = VD->getAttr<TypeTagForDatatypeAttr>()) {
      if (I->getArgumentKind() != ArgumentKind) {
        FoundWrongKind = true;
        return false;
      }
      TypeInfo.Type = I->getMatchingCType();
      TypeInfo.LayoutCompatible = I->getLayoutCompatible();
      TypeInfo.MustBeNull = I->getMustBeNull();
      return true;
    }
    return false;
  }

  if (!MagicValues)
    return false;

  llvm::DenseMap<Sema::TypeTagMagicValue, Sema::TypeTagData>::const_iterator I =
      MagicValues->find(std::make_pair(ArgumentKind, MagicValue));
  if (I == MagicValues->end())
    return false;

  TypeInfo = I->second;
  return true;
}
} // namespace

void Sema::RegisterTypeTagForDatatype(const IdentifierInfo *ArgumentKind,
                                      uint64_t MagicValue, QualType Type,
                                      bool LayoutCompatible, bool MustBeNull) {
  if (!TypeTagForDatatypeMagicValues)
    TypeTagForDatatypeMagicValues.reset(
        new llvm::DenseMap<TypeTagMagicValue, TypeTagData>);

  TypeTagMagicValue Magic(ArgumentKind, MagicValue);
  (*TypeTagForDatatypeMagicValues)[Magic] =
      TypeTagData(Type, LayoutCompatible, MustBeNull);
}

namespace {
bool isSameCharType(QualType T1, QualType T2) {
  const BuiltinType *BT1 = T1->getAs<BuiltinType>();
  if (!BT1)
    return false;

  const BuiltinType *BT2 = T2->getAs<BuiltinType>();
  if (!BT2)
    return false;

  BuiltinType::Kind T1Kind = BT1->getKind();
  BuiltinType::Kind T2Kind = BT2->getKind();

  return (T1Kind == BuiltinType::SChar && T2Kind == BuiltinType::Char_S) ||
         (T1Kind == BuiltinType::UChar && T2Kind == BuiltinType::Char_U) ||
         (T1Kind == BuiltinType::Char_U && T2Kind == BuiltinType::UChar) ||
         (T1Kind == BuiltinType::Char_S && T2Kind == BuiltinType::SChar);
}
} // namespace

void Sema::CheckArgumentWithTypeTag(const ArgumentWithTypeTagAttr *Attr,
                                    const llvm::ArrayRef<const Expr *> ExprArgs,
                                    SourceLocation CallSiteLoc) {
  const IdentifierInfo *ArgumentKind = Attr->getArgumentKind();
  bool IsPointerAttr = Attr->getIsPointer();

  // Retrieve the argument representing the 'type_tag'.
  unsigned TypeTagIdxAST = Attr->getTypeTagIdx().getASTIndex();
  if (TypeTagIdxAST >= ExprArgs.size()) {
    Diag(CallSiteLoc, diag::err_tag_index_out_of_range)
        << 0 << Attr->getTypeTagIdx().getSourceIndex();
    return;
  }
  const Expr *TypeTagExpr = ExprArgs[TypeTagIdxAST];
  bool FoundWrongKind;
  TypeTagData TypeInfo;
  if (!getMatchingCType(ArgumentKind, TypeTagExpr, Context,
                        TypeTagForDatatypeMagicValues.get(), FoundWrongKind,
                        TypeInfo, isConstantEvaluatedContext())) {
    if (FoundWrongKind)
      Diag(TypeTagExpr->getExprLoc(),
           diag::warn_type_tag_for_datatype_wrong_kind)
          << TypeTagExpr->getSourceRange();
    return;
  }

  // Retrieve the argument representing the 'arg_idx'.
  unsigned ArgumentIdxAST = Attr->getArgumentIdx().getASTIndex();
  if (ArgumentIdxAST >= ExprArgs.size()) {
    Diag(CallSiteLoc, diag::err_tag_index_out_of_range)
        << 1 << Attr->getArgumentIdx().getSourceIndex();
    return;
  }
  const Expr *ArgumentExpr = ExprArgs[ArgumentIdxAST];
  if (IsPointerAttr) {
    // Skip implicit cast of pointer to `void *' (as a function argument).
    if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(ArgumentExpr))
      if (ICE->getType()->isVoidPointerType() &&
          ICE->getCastKind() == CK_BitCast)
        ArgumentExpr = ICE->getSubExpr();
  }
  QualType ArgumentType = ArgumentExpr->getType();

  // Passing a `void*' pointer shouldn't trigger a warning.
  if (IsPointerAttr && ArgumentType->isVoidPointerType())
    return;

  if (TypeInfo.MustBeNull) {
    // Type tag with matching void type requires a null pointer.
    if (!ArgumentExpr->isNullPointerConstant(
            Context, Expr::NPC_ValueDependentIsNotNull)) {
      Diag(ArgumentExpr->getExprLoc(),
           diag::warn_type_safety_null_pointer_required)
          << ArgumentKind->getName() << ArgumentExpr->getSourceRange()
          << TypeTagExpr->getSourceRange();
    }
    return;
  }

  QualType RequiredType = TypeInfo.Type;
  if (IsPointerAttr)
    RequiredType = Context.getPointerType(RequiredType);

  bool mismatch = false;
  if (!TypeInfo.LayoutCompatible) {
    mismatch = !Context.hasSameType(ArgumentType, RequiredType);

    // `char` vs `signed char` / `unsigned char`: treat as equivalent when the
    // target's plain `char` matches the required signedness.
    if (mismatch)
      if ((IsPointerAttr && isSameCharType(ArgumentType->getPointeeType(),
                                           RequiredType->getPointeeType())) ||
          (!IsPointerAttr && isSameCharType(ArgumentType, RequiredType)))
        mismatch = false;
  } else if (IsPointerAttr)
    mismatch = !isLayoutCompatible(Context, ArgumentType->getPointeeType(),
                                   RequiredType->getPointeeType());
  else
    mismatch = !isLayoutCompatible(Context, ArgumentType, RequiredType);

  if (mismatch)
    Diag(ArgumentExpr->getExprLoc(), diag::warn_type_safety_type_mismatch)
        << ArgumentType << ArgumentKind << TypeInfo.LayoutCompatible
        << RequiredType << ArgumentExpr->getSourceRange()
        << TypeTagExpr->getSourceRange();
}

void Sema::AddPotentialMisalignedMembers(Expr *E, RecordDecl *RD, ValueDecl *MD,
                                         CharUnits Alignment) {
  MisalignedMembers.emplace_back(E, RD, MD, Alignment);
}

void Sema::DiagnoseMisalignedMembers() {
  for (MisalignedMember &m : MisalignedMembers) {
    const NamedDecl *ND = m.RD;
    if (ND->getName().empty()) {
      if (const TypedefNameDecl *TD = m.RD->getTypedefNameForAnonDecl())
        ND = TD;
    }
    Diag(m.E->getBeginLoc(), diag::warn_taking_address_of_packed_member)
        << m.MD << ND << m.E->getSourceRange();
  }
  MisalignedMembers.clear();
}

void Sema::DiscardMisalignedMemberAddress(const Type *T, Expr *E) {
  E = E->IgnoreParens();
  if (!T->isPointerType() && !T->isIntegerType())
    return;
  if (isa<UnaryOperator>(E) &&
      cast<UnaryOperator>(E)->getOpcode() == UO_AddrOf) {
    auto *Op = cast<UnaryOperator>(E)->getSubExpr()->IgnoreParens();
    if (isa<MemberExpr>(Op)) {
      auto *MA = llvm::find(MisalignedMembers, MisalignedMember(Op));
      if (MA != MisalignedMembers.end() &&
          (T->isIntegerType() ||
           (T->isPointerType() && (T->getPointeeType()->isIncompleteType() ||
                                   Context.getTypeAlignInChars(
                                       T->getPointeeType()) <= MA->Alignment))))
        MisalignedMembers.erase(MA);
    }
  }
}

void Sema::RefersToMemberWithReducedAlignment(
    Expr *E,
    llvm::function_ref<void(Expr *, RecordDecl *, FieldDecl *, CharUnits)>
        Action) {
  const auto *ME = dyn_cast<MemberExpr>(E);
  if (!ME)
    return;

  // No need to check expressions with an __unaligned-qualified type.
  if (E->getType().getQualifiers().hasUnaligned())
    return;

  // For a chain of MemberExpr like "a.b.c.d" this list
  // will keep FieldDecl's like [d, c, b].
  llvm::SmallVector<FieldDecl *, 4> ReverseMemberChain;
  const MemberExpr *TopME = nullptr;
  bool AnyIsPacked = false;
  do {
    QualType BaseType = ME->getBase()->getType();
    if (ME->isArrow())
      BaseType = BaseType->getPointeeType();
    RecordDecl *RD = BaseType->castAs<RecordType>()->getDecl();
    if (RD->isInvalidDecl())
      return;

    ValueDecl *MD = ME->getMemberDecl();
    auto *FD = dyn_cast<FieldDecl>(MD);
    // We do not care about non-data members.
    if (!FD || FD->isInvalidDecl())
      return;

    AnyIsPacked =
        AnyIsPacked || (RD->hasAttr<PackedAttr>() || MD->hasAttr<PackedAttr>());
    ReverseMemberChain.push_back(FD);

    TopME = ME;
    ME = dyn_cast<MemberExpr>(ME->getBase()->IgnoreParens());
  } while (ME);
  assert(TopME && "We did not compute a topmost MemberExpr!");

  // Not the scope of this diagnostic.
  if (!AnyIsPacked)
    return;

  const Expr *TopBase = TopME->getBase()->IgnoreParenImpCasts();
  const auto *DRE = dyn_cast<DeclRefExpr>(TopBase);
  // The innermost base of the member expression may be too complicated.
  // For now, just disregard these cases. This is left for future
  // improvement.
  if (!DRE)
    return;

  // Alignment expected by the whole expression.
  CharUnits ExpectedAlignment = Context.getTypeAlignInChars(E->getType());

  // No need to do anything else with this case.
  if (ExpectedAlignment.isOne())
    return;

  // Synthesize offset of the whole access.
  CharUnits Offset;
  for (const FieldDecl *FD : llvm::reverse(ReverseMemberChain))
    Offset += Context.toCharUnitsFromBits(Context.getFieldOffset(FD));

  // Compute the CompleteObjectAlignment as the alignment of the whole chain.
  CharUnits CompleteObjectAlignment = Context.getTypeAlignInChars(
      ReverseMemberChain.back()->getParent()->getTypeForDecl());

  // The base expression of the innermost MemberExpr may give
  // stronger guarantees than the struct/union containing the member.
  if (DRE && !TopME->isArrow()) {
    const ValueDecl *VD = DRE->getDecl();
    CompleteObjectAlignment =
        std::max(CompleteObjectAlignment, Context.getDeclAlign(VD));
  }
  if (Offset % ExpectedAlignment != 0 ||
      // It may fulfill the offset it but the effective alignment may still be
      // lower than the expected expression alignment.
      CompleteObjectAlignment < ExpectedAlignment) {
    // If this happens, we want to determine a sensible culprit of this.
    // Intuitively, watching the chain of member expressions from right to
    // left, we start with the required alignment (as required by the field
    // type) but some packed attribute in that chain has reduced the alignment.
    // It may happen that another packed structure increases it again. But if
    // we are here such increase has not been enough. So pointing the first
    // FieldDecl that either is packed or else its RecordDecl is,
    // seems reasonable.
    FieldDecl *FD = nullptr;
    CharUnits Alignment;
    for (FieldDecl *FDI : ReverseMemberChain) {
      if (FDI->hasAttr<PackedAttr>() ||
          FDI->getParent()->hasAttr<PackedAttr>()) {
        FD = FDI;
        Alignment = std::min(
            Context.getTypeAlignInChars(FD->getType()),
            Context.getTypeAlignInChars(FD->getParent()->getTypeForDecl()));
        break;
      }
    }
    assert(FD && "We did not find a packed FieldDecl!");
    Action(E, FD->getParent(), FD, Alignment);
  }
}

void Sema::CheckAddressOfPackedMember(Expr *rhs) {
  using namespace std::placeholders;

  RefersToMemberWithReducedAlignment(
      rhs, std::bind(&Sema::AddPotentialMisalignedMembers, std::ref(*this), _1,
                     _2, _3, _4));
}

bool Sema::PrepareBuiltinElementwiseMathOneArgCall(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 1))
    return true;

  ExprResult A = UsualUnaryConversions(TheCall->getArg(0));
  if (A.isInvalid())
    return true;

  TheCall->setArg(0, A.get());
  QualType TyA = A.get()->getType();

  if (checkMathBuiltinElementType(*this, A.get()->getBeginLoc(), TyA))
    return true;

  TheCall->setType(TyA);
  return false;
}

bool Sema::SemaBuiltinElementwiseMath(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 2))
    return true;

  ExprResult A = TheCall->getArg(0);
  ExprResult B = TheCall->getArg(1);
  // Do standard promotions between the two arguments, returning their common
  // type.
  QualType Res =
      UsualArithmeticConversions(A, B, TheCall->getExprLoc(), ACK_Comparison);
  if (A.isInvalid() || B.isInvalid())
    return true;

  QualType TyA = A.get()->getType();
  QualType TyB = B.get()->getType();

  if (Res.isNull() || TyA.getCanonicalType() != TyB.getCanonicalType())
    return Diag(A.get()->getBeginLoc(),
                diag::err_typecheck_call_different_arg_types)
           << TyA << TyB;

  if (checkMathBuiltinElementType(*this, A.get()->getBeginLoc(), TyA))
    return true;

  TheCall->setArg(0, A.get());
  TheCall->setArg(1, B.get());
  TheCall->setType(Res);
  return false;
}

bool Sema::SemaBuiltinElementwiseTernaryMath(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 3))
    return true;

  Expr *Args[3];
  for (int I = 0; I < 3; ++I) {
    ExprResult Converted = UsualUnaryConversions(TheCall->getArg(I));
    if (Converted.isInvalid())
      return true;
    Args[I] = Converted.get();
  }

  int ArgOrdinal = 1;
  for (Expr *Arg : Args) {
    if (checkFPMathBuiltinElementType(*this, Arg->getBeginLoc(), Arg->getType(),
                                      ArgOrdinal++))
      return true;
  }

  for (int I = 1; I < 3; ++I) {
    if (Args[0]->getType().getCanonicalType() !=
        Args[I]->getType().getCanonicalType()) {
      return Diag(Args[0]->getBeginLoc(),
                  diag::err_typecheck_call_different_arg_types)
             << Args[0]->getType() << Args[I]->getType();
    }

    TheCall->setArg(I, Args[I]);
  }

  TheCall->setType(Args[0]->getType());
  return false;
}

bool Sema::PrepareBuiltinReduceMathOneArgCall(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 1))
    return true;

  ExprResult A = UsualUnaryConversions(TheCall->getArg(0));
  if (A.isInvalid())
    return true;

  TheCall->setArg(0, A.get());
  return false;
}

bool Sema::SemaBuiltinNonDeterministicValue(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 1))
    return true;

  ExprResult Arg = TheCall->getArg(0);
  QualType TyArg = Arg.get()->getType();

  if (!TyArg->isBuiltinType() && !TyArg->isVectorType())
    return Diag(TheCall->getArg(0)->getBeginLoc(),
                diag::err_builtin_invalid_arg_type)
           << 1 << /*vector, integer or floating point ty*/ 0 << TyArg;

  TheCall->setType(TyArg);
  return false;
}

ExprResult Sema::SemaBuiltinMatrixTranspose(CallExpr *TheCall,
                                            ExprResult CallResult) {
  if (checkArgCount(*this, TheCall, 1))
    return ExprError();

  ExprResult MatrixArg = DefaultLvalueConversion(TheCall->getArg(0));
  if (MatrixArg.isInvalid())
    return MatrixArg;
  Expr *Matrix = MatrixArg.get();

  auto *MType = Matrix->getType()->getAs<ConstantMatrixType>();
  if (!MType) {
    Diag(Matrix->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << 1 << /* matrix ty*/ 1 << Matrix->getType();
    return ExprError();
  }
  QualType ResultType = Context.getConstantMatrixType(
      MType->getElementType(), MType->getNumColumns(), MType->getNumRows());

  // Change the return type to the type of the returned matrix.
  TheCall->setType(ResultType);

  // Update call argument to use the possibly converted matrix argument.
  TheCall->setArg(0, Matrix);
  return CallResult;
}

// Get and verify the matrix dimensions.
namespace {
std::optional<unsigned>
getAndVerifyMatrixDimension(Expr *Expr, llvm::StringRef Name, Sema &S) {
  SourceLocation ErrorPos;
  std::optional<llvm::APSInt> Value =
      Expr->getIntegerConstantExpr(S.Context, &ErrorPos);
  if (!Value) {
    S.Diag(Expr->getBeginLoc(), diag::err_builtin_matrix_scalar_unsigned_arg)
        << Name;
    return {};
  }
  uint64_t Dim = Value->getZExtValue();
  if (!ConstantMatrixType::isDimensionValid(Dim)) {
    S.Diag(Expr->getBeginLoc(), diag::err_builtin_matrix_invalid_dimension)
        << Name << ConstantMatrixType::getMaxElementsPerDimension();
    return {};
  }
  return Dim;
}
} // namespace

ExprResult Sema::SemaBuiltinMatrixColumnMajorLoad(CallExpr *TheCall,
                                                  ExprResult CallResult) {
  if (checkArgCount(*this, TheCall, 4))
    return ExprError();

  unsigned PtrArgIdx = 0;
  Expr *PtrExpr = TheCall->getArg(PtrArgIdx);
  Expr *RowsExpr = TheCall->getArg(1);
  Expr *ColumnsExpr = TheCall->getArg(2);
  Expr *StrideExpr = TheCall->getArg(3);

  bool ArgError = false;
  {
    ExprResult PtrConv = DefaultFunctionArrayLvalueConversion(PtrExpr);
    if (PtrConv.isInvalid())
      return PtrConv;
    PtrExpr = PtrConv.get();
    TheCall->setArg(0, PtrExpr);
  }

  auto *PtrTy = PtrExpr->getType()->getAs<PointerType>();
  QualType ElementTy;
  if (!PtrTy) {
    Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << PtrArgIdx + 1 << /*pointer to element ty*/ 2 << PtrExpr->getType();
    ArgError = true;
  } else {
    ElementTy = PtrTy->getPointeeType().getUnqualifiedType();

    if (!ConstantMatrixType::isValidElementType(ElementTy)) {
      Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << PtrArgIdx + 1 << /* pointer to element ty*/ 2
          << PtrExpr->getType();
      ArgError = true;
    }
  }

  // Apply default Lvalue conversions and convert the expression to size_t.
  auto ApplyArgumentConversions = [this](Expr *E) {
    ExprResult Conv = DefaultLvalueConversion(E);
    if (Conv.isInvalid())
      return Conv;

    return tryConvertExprToType(Conv.get(), Context.getSizeType());
  };

  // Apply conversion to row and column expressions.
  ExprResult RowsConv = ApplyArgumentConversions(RowsExpr);
  if (!RowsConv.isInvalid()) {
    RowsExpr = RowsConv.get();
    TheCall->setArg(1, RowsExpr);
  } else
    RowsExpr = nullptr;

  ExprResult ColumnsConv = ApplyArgumentConversions(ColumnsExpr);
  if (!ColumnsConv.isInvalid()) {
    ColumnsExpr = ColumnsConv.get();
    TheCall->setArg(2, ColumnsExpr);
  } else
    ColumnsExpr = nullptr;
  std::optional<unsigned> MaybeRows;
  if (RowsExpr)
    MaybeRows = getAndVerifyMatrixDimension(RowsExpr, "row", *this);

  std::optional<unsigned> MaybeColumns;
  if (ColumnsExpr)
    MaybeColumns = getAndVerifyMatrixDimension(ColumnsExpr, "column", *this);
  ExprResult StrideConv = ApplyArgumentConversions(StrideExpr);
  if (StrideConv.isInvalid())
    return ExprError();
  StrideExpr = StrideConv.get();
  TheCall->setArg(3, StrideExpr);

  if (MaybeRows) {
    if (std::optional<llvm::APSInt> Value =
            StrideExpr->getIntegerConstantExpr(Context)) {
      uint64_t Stride = Value->getZExtValue();
      if (Stride < *MaybeRows) {
        Diag(StrideExpr->getBeginLoc(),
             diag::err_builtin_matrix_stride_too_small);
        ArgError = true;
      }
    }
  }

  if (ArgError || !MaybeRows || !MaybeColumns)
    return ExprError();

  TheCall->setType(
      Context.getConstantMatrixType(ElementTy, *MaybeRows, *MaybeColumns));
  return CallResult;
}

ExprResult Sema::SemaBuiltinMatrixColumnMajorStore(CallExpr *TheCall,
                                                   ExprResult CallResult) {
  if (checkArgCount(*this, TheCall, 3))
    return ExprError();

  unsigned PtrArgIdx = 1;
  Expr *MatrixExpr = TheCall->getArg(0);
  Expr *PtrExpr = TheCall->getArg(PtrArgIdx);
  Expr *StrideExpr = TheCall->getArg(2);

  bool ArgError = false;

  {
    ExprResult MatrixConv = DefaultLvalueConversion(MatrixExpr);
    if (MatrixConv.isInvalid())
      return MatrixConv;
    MatrixExpr = MatrixConv.get();
    TheCall->setArg(0, MatrixExpr);
  }
  auto *MatrixTy = MatrixExpr->getType()->getAs<ConstantMatrixType>();
  if (!MatrixTy) {
    Diag(MatrixExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << 1 << /*matrix ty */ 1 << MatrixExpr->getType();
    ArgError = true;
  }

  {
    ExprResult PtrConv = DefaultFunctionArrayLvalueConversion(PtrExpr);
    if (PtrConv.isInvalid())
      return PtrConv;
    PtrExpr = PtrConv.get();
    TheCall->setArg(1, PtrExpr);
  }
  auto *PtrTy = PtrExpr->getType()->getAs<PointerType>();
  if (!PtrTy) {
    Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << PtrArgIdx + 1 << /*pointer to element ty*/ 2 << PtrExpr->getType();
    ArgError = true;
  } else {
    QualType ElementTy = PtrTy->getPointeeType();
    if (ElementTy.isConstQualified()) {
      Diag(PtrExpr->getBeginLoc(), diag::err_builtin_matrix_store_to_const);
      ArgError = true;
    }
    ElementTy = ElementTy.getUnqualifiedType().getCanonicalType();
    if (MatrixTy &&
        !Context.hasSameType(ElementTy, MatrixTy->getElementType())) {
      Diag(PtrExpr->getBeginLoc(),
           diag::err_builtin_matrix_pointer_arg_mismatch)
          << ElementTy << MatrixTy->getElementType();
      ArgError = true;
    }
  }

  // Apply default Lvalue conversions and convert the stride expression to
  // size_t.
  {
    ExprResult StrideConv = DefaultLvalueConversion(StrideExpr);
    if (StrideConv.isInvalid())
      return StrideConv;

    StrideConv = tryConvertExprToType(StrideConv.get(), Context.getSizeType());
    if (StrideConv.isInvalid())
      return StrideConv;
    StrideExpr = StrideConv.get();
    TheCall->setArg(2, StrideExpr);
  }
  if (MatrixTy) {
    if (std::optional<llvm::APSInt> Value =
            StrideExpr->getIntegerConstantExpr(Context)) {
      uint64_t Stride = Value->getZExtValue();
      if (Stride < MatrixTy->getNumRows()) {
        Diag(StrideExpr->getBeginLoc(),
             diag::err_builtin_matrix_stride_too_small);
        ArgError = true;
      }
    }
  }

  if (ArgError)
    return ExprError();

  return CallResult;
}

void Sema::CheckTCBEnforcement(const SourceLocation CallExprLoc,
                               const NamedDecl *Callee) {
  // This warning does not make sense in code that has no runtime behavior.
  if (isUnevaluatedContext())
    return;

  const FunctionDecl *Caller = getCurFunctionDecl();

  if (!Caller || !Caller->hasAttr<EnforceTCBAttr>())
    return;

  // Search through the enforce_tcb and enforce_tcb_leaf attributes to find
  // all TCBs the callee is a part of.
  llvm::StringSet<> CalleeTCBs;
  for (const auto *A : Callee->specific_attrs<EnforceTCBAttr>())
    CalleeTCBs.insert(A->getTCBName());
  for (const auto *A : Callee->specific_attrs<EnforceTCBLeafAttr>())
    CalleeTCBs.insert(A->getTCBName());

  // Go through the TCBs the caller is a part of and emit warnings if Caller
  // is in a TCB that the Callee is not.
  for (const auto *A : Caller->specific_attrs<EnforceTCBAttr>()) {
    llvm::StringRef CallerTCB = A->getTCBName();
    if (!CalleeTCBs.contains(CallerTCB)) {
      this->Diag(CallExprLoc, diag::warn_tcb_enforcement_violation)
          << Callee << CallerTCB;
    }
  }
}
