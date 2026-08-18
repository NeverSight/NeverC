#include "Checking/SemaCheckingUtils.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Tree/Format/FormatString.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Misc free-function builtin semantic checks
// (annotation, dump_struct, call_with_static_chain, ...)
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
    Indent.resize(size_t(Depth) * Policy.Indentation, ' ');
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
