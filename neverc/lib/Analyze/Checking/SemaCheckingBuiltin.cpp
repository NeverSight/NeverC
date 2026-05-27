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
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <ctime>
#include <optional>
#include <string>
#include <utility>
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Individual builtin semantic checks
// ===----------------------------------------------------------------------===

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

