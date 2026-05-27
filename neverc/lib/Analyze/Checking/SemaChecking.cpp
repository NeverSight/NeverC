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

// Emit an error and return true if the current architecture is not in the list
// of supported architectures.
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

void checkNonNullArgument(Sema &S, const Expr *ArgExpr,
                          SourceLocation CallSiteLoc) {
  if (CheckNonNullExpr(S, ArgExpr))
    S.DiagRuntimeBehavior(CallSiteLoc, ArgExpr,
                          S.PDiag(diag::warn_null_arg)
                              << ArgExpr->getSourceRange());
}

bool isNonNullType(QualType type) {
  if (auto nullability = type->getNullability())
    return *nullability == NullabilityKind::NonNull;

  return false;
}

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

