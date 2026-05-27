#include "neverc/Compiler/FrontendOptions.h"
#include "neverc/Foundation/Core/MacroBuilder.h"
#include "neverc/Foundation/Core/Version.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepOptions.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DerivedTypes.h"
#include "neverc/Foundation/Target/TargetOSMacros.def"
using namespace neverc;

namespace neverc {
void initializePredefinedMacros(const TargetInfo &TI,
                                const LangOptions &LangOpts,
                                const FrontendOptions &FEOpts,
                                const PrepOptions &PPOpts,
                                MacroBuilder &Builder);
} // namespace neverc

// ===----------------------------------------------------------------------===
// Type / float macro helpers
// ===----------------------------------------------------------------------===

namespace {

template <typename T>
T pickFP(const llvm::fltSemantics *Sem, T IEEEHalfVal, T IEEESingleVal,
         T IEEEDoubleVal, T X87DoubleExtendedVal, T IEEEQuadVal) {
  if (Sem == (const llvm::fltSemantics *)&llvm::APFloat::IEEEhalf())
    return IEEEHalfVal;
  if (Sem == (const llvm::fltSemantics *)&llvm::APFloat::IEEEsingle())
    return IEEESingleVal;
  if (Sem == (const llvm::fltSemantics *)&llvm::APFloat::IEEEdouble())
    return IEEEDoubleVal;
  if (Sem == (const llvm::fltSemantics *)&llvm::APFloat::x87DoubleExtended())
    return X87DoubleExtendedVal;
  assert(Sem == (const llvm::fltSemantics *)&llvm::APFloat::IEEEquad());
  return IEEEQuadVal;
}

void defineFloatMacros(MacroBuilder &Builder, llvm::StringRef Prefix,
                       const llvm::fltSemantics *Sem, llvm::StringRef Ext) {
  const char *DenormMin, *Epsilon, *Max, *Min;
  DenormMin = pickFP(Sem, "5.9604644775390625e-8", "1.40129846e-45",
                     "4.9406564584124654e-324", "3.64519953188247460253e-4951",
                     "6.47517511943802511092443895822764655e-4966");
  int Digits = pickFP(Sem, 3, 6, 15, 18, 33);
  int DecimalDigits = pickFP(Sem, 5, 9, 17, 21, 36);
  Epsilon = pickFP(Sem, "9.765625e-4", "1.19209290e-7",
                   "2.2204460492503131e-16", "1.08420217248550443401e-19",
                   "1.92592994438723585305597794258492732e-34");
  int MantissaDigits = pickFP(Sem, 11, 24, 53, 64, 113);
  int Min10Exp = pickFP(Sem, -4, -37, -307, -4931, -4931);
  int Max10Exp = pickFP(Sem, 4, 38, 308, 4932, 4932);
  int MinExp = pickFP(Sem, -13, -125, -1021, -16381, -16381);
  int MaxExp = pickFP(Sem, 16, 128, 1024, 16384, 16384);
  Min = pickFP(Sem, "6.103515625e-5", "1.17549435e-38",
               "2.2250738585072014e-308", "3.36210314311209350626e-4932",
               "3.36210314311209350626267781732175260e-4932");
  Max = pickFP(Sem, "6.5504e+4", "3.40282347e+38", "1.7976931348623157e+308",
               "1.18973149535723176502e+4932",
               "1.18973149535723176508575932662800702e+4932");

  llvm::SmallString<32> DefPrefix;
  DefPrefix = "__";
  DefPrefix += Prefix;
  DefPrefix += "_";

  Builder.defineMacro(DefPrefix + "DENORM_MIN__", llvm::Twine(DenormMin) + Ext);
  Builder.defineMacro(DefPrefix + "HAS_DENORM__");
  Builder.defineMacro(DefPrefix + "DIG__", llvm::Twine(Digits));
  Builder.defineMacro(DefPrefix + "DECIMAL_DIG__", llvm::Twine(DecimalDigits));
  Builder.defineMacro(DefPrefix + "EPSILON__", llvm::Twine(Epsilon) + Ext);
  Builder.defineMacro(DefPrefix + "HAS_INFINITY__");
  Builder.defineMacro(DefPrefix + "HAS_QUIET_NAN__");
  Builder.defineMacro(DefPrefix + "MANT_DIG__", llvm::Twine(MantissaDigits));

  Builder.defineMacro(DefPrefix + "MAX_10_EXP__", llvm::Twine(Max10Exp));
  Builder.defineMacro(DefPrefix + "MAX_EXP__", llvm::Twine(MaxExp));
  Builder.defineMacro(DefPrefix + "MAX__", llvm::Twine(Max) + Ext);

  Builder.defineMacro(DefPrefix + "MIN_10_EXP__",
                      "(" + llvm::Twine(Min10Exp) + ")");
  Builder.defineMacro(DefPrefix + "MIN_EXP__", "(" + llvm::Twine(MinExp) + ")");
  Builder.defineMacro(DefPrefix + "MIN__", llvm::Twine(Min) + Ext);
}

void defineTypeSize(const llvm::Twine &MacroName, unsigned TypeWidth,
                    llvm::StringRef ValSuffix, bool isSigned,
                    MacroBuilder &Builder) {
  llvm::APInt MaxVal = isSigned ? llvm::APInt::getSignedMaxValue(TypeWidth)
                                : llvm::APInt::getMaxValue(TypeWidth);
  Builder.defineMacro(MacroName, toString(MaxVal, 10, isSigned) + ValSuffix);
}

void defineTypeSize(const llvm::Twine &MacroName, TargetInfo::IntType Ty,
                    const TargetInfo &TI, MacroBuilder &Builder) {
  defineTypeSize(MacroName, TI.getTypeWidth(Ty), TI.getTypeConstantSuffix(Ty),
                 TI.isTypeSigned(Ty), Builder);
}

void defineFmt(const llvm::Twine &Prefix, TargetInfo::IntType Ty,
               const TargetInfo &TI, MacroBuilder &Builder) {
  bool IsSigned = TI.isTypeSigned(Ty);
  llvm::StringRef FmtModifier = TI.getTypeFormatModifier(Ty);
  for (const char *Fmt = IsSigned ? "di" : "ouxX"; *Fmt; ++Fmt) {
    Builder.defineMacro(Prefix + "_FMT" + llvm::Twine(*Fmt) + "__",
                        llvm::Twine("\"") + FmtModifier + llvm::Twine(*Fmt) +
                            "\"");
  }
}

void defineType(const llvm::Twine &MacroName, TargetInfo::IntType Ty,
                MacroBuilder &Builder) {
  Builder.defineMacro(MacroName, TargetInfo::getTypeName(Ty));
}

void defineTypeWidth(const llvm::Twine &MacroName, TargetInfo::IntType Ty,
                     const TargetInfo &TI, MacroBuilder &Builder) {
  Builder.defineMacro(MacroName, llvm::Twine(TI.getTypeWidth(Ty)));
}

void defineTypeSizeof(llvm::StringRef MacroName, unsigned BitWidth,
                      const TargetInfo &TI, MacroBuilder &Builder) {
  Builder.defineMacro(MacroName, llvm::Twine(BitWidth / TI.getCharWidth()));
}

void defineTypeSizeAndWidth(const llvm::Twine &Prefix, TargetInfo::IntType Ty,
                            const TargetInfo &TI, MacroBuilder &Builder) {
  defineTypeSize(Prefix + "_MAX__", Ty, TI, Builder);
  defineTypeWidth(Prefix + "_WIDTH__", Ty, TI, Builder);
}

void defineExactWidthIntType(TargetInfo::IntType Ty, const TargetInfo &TI,
                             MacroBuilder &Builder) {
  int TypeWidth = TI.getTypeWidth(Ty);
  bool IsSigned = TI.isTypeSigned(Ty);

  if (TypeWidth == 64)
    Ty = IsSigned ? TI.getInt64Type() : TI.getUInt64Type();

  if (TypeWidth == 16)
    Ty = IsSigned ? TI.getInt16Type() : TI.getUInt16Type();

  const char *Prefix = IsSigned ? "__INT" : "__UINT";

  defineType(Prefix + llvm::Twine(TypeWidth) + "_TYPE__", Ty, Builder);
  defineFmt(Prefix + llvm::Twine(TypeWidth), Ty, TI, Builder);

  llvm::StringRef ConstSuffix(TI.getTypeConstantSuffix(Ty));
  Builder.defineMacro(Prefix + llvm::Twine(TypeWidth) + "_C_SUFFIX__",
                      ConstSuffix);
}

void defineExactWidthIntTypeSize(TargetInfo::IntType Ty, const TargetInfo &TI,
                                 MacroBuilder &Builder) {
  int TypeWidth = TI.getTypeWidth(Ty);
  bool IsSigned = TI.isTypeSigned(Ty);

  if (TypeWidth == 64)
    Ty = IsSigned ? TI.getInt64Type() : TI.getUInt64Type();

  const char *Prefix = IsSigned ? "__INT" : "__UINT";
  defineTypeSize(Prefix + llvm::Twine(TypeWidth) + "_MAX__", Ty, TI, Builder);
}

void defineLeastWidthIntType(unsigned TypeWidth, bool IsSigned,
                             const TargetInfo &TI, MacroBuilder &Builder) {
  TargetInfo::IntType Ty = TI.getLeastIntTypeByWidth(TypeWidth, IsSigned);
  if (Ty == TargetInfo::NoInt)
    return;

  const char *Prefix = IsSigned ? "__INT_LEAST" : "__UINT_LEAST";
  defineType(Prefix + llvm::Twine(TypeWidth) + "_TYPE__", Ty, Builder);
  if (IsSigned)
    defineTypeSizeAndWidth(Prefix + llvm::Twine(TypeWidth), Ty, TI, Builder);
  else
    defineTypeSize(Prefix + llvm::Twine(TypeWidth) + "_MAX__", Ty, TI, Builder);
  defineFmt(Prefix + llvm::Twine(TypeWidth), Ty, TI, Builder);
}

void defineFastIntType(unsigned TypeWidth, bool IsSigned, const TargetInfo &TI,
                       MacroBuilder &Builder) {
  TargetInfo::IntType Ty = TI.getLeastIntTypeByWidth(TypeWidth, IsSigned);
  if (Ty == TargetInfo::NoInt)
    return;

  const char *Prefix = IsSigned ? "__INT_FAST" : "__UINT_FAST";
  defineType(Prefix + llvm::Twine(TypeWidth) + "_TYPE__", Ty, Builder);
  if (IsSigned)
    defineTypeSizeAndWidth(Prefix + llvm::Twine(TypeWidth), Ty, TI, Builder);
  else
    defineTypeSize(Prefix + llvm::Twine(TypeWidth) + "_MAX__", Ty, TI, Builder);
  defineFmt(Prefix + llvm::Twine(TypeWidth), Ty, TI, Builder);
}

const char *getLockFreeValue(unsigned TypeWidth, const TargetInfo &TI) {
  if (TI.hasBuiltinAtomic(TypeWidth, TypeWidth))
    return "2"; // "always lock free"
  return "1"; // "sometimes lock free"
}

} // anonymous namespace

// ===----------------------------------------------------------------------===
// initializePredefinedMacros
// ===----------------------------------------------------------------------===

void neverc::initializePredefinedMacros(const TargetInfo &TI,
                                        const LangOptions &LangOpts,
                                        const FrontendOptions &FEOpts,
                                        const PrepOptions &PPOpts,
                                        MacroBuilder &Builder) {
  // Compiler version introspection macros.
  Builder.defineMacro("__llvm__"); // LLVM Backend
  Builder.defineMacro("__neverc__");
#define TOSTR2(X) #X
#define TOSTR(X) TOSTR2(X)
  Builder.defineMacro("__neverc_major__", TOSTR(NEVERC_VERSION_MAJOR));
  Builder.defineMacro("__neverc_minor__", TOSTR(NEVERC_VERSION_MINOR));
  Builder.defineMacro("__neverc_patchlevel__",
                      TOSTR(NEVERC_VERSION_PATCHLEVEL));
#undef TOSTR
#undef TOSTR2
  Builder.defineMacro("__neverc_version__",
                      "\"" NEVERC_VERSION_STRING " " +
                          getNeverCFullRepositoryVersion() + "\"");

  if (LangOpts.GNUCVersion != 0) {
    unsigned GNUCMajor = LangOpts.GNUCVersion / 100 / 100;
    unsigned GNUCMinor = LangOpts.GNUCVersion / 100 % 100;
    unsigned GNUCPatch = LangOpts.GNUCVersion % 100;
    Builder.defineMacro("__GNUC__", llvm::Twine(GNUCMajor));
    Builder.defineMacro("__GNUC_MINOR__", llvm::Twine(GNUCMinor));
    Builder.defineMacro("__GNUC_PATCHLEVEL__", llvm::Twine(GNUCPatch));
  }

  // Define macros for the C11 memory orderings
  Builder.defineMacro("__ATOMIC_RELAXED", "0");
  Builder.defineMacro("__ATOMIC_CONSUME", "1");
  Builder.defineMacro("__ATOMIC_ACQUIRE", "2");
  Builder.defineMacro("__ATOMIC_RELEASE", "3");
  Builder.defineMacro("__ATOMIC_ACQ_REL", "4");
  Builder.defineMacro("__ATOMIC_SEQ_CST", "5");

  // Define macros for floating-point data classes, used in __builtin_isfpclass.
  Builder.defineMacro("__FPCLASS_SNAN", "0x0001");
  Builder.defineMacro("__FPCLASS_QNAN", "0x0002");
  Builder.defineMacro("__FPCLASS_NEGINF", "0x0004");
  Builder.defineMacro("__FPCLASS_NEGNORMAL", "0x0008");
  Builder.defineMacro("__FPCLASS_NEGSUBNORMAL", "0x0010");
  Builder.defineMacro("__FPCLASS_NEGZERO", "0x0020");
  Builder.defineMacro("__FPCLASS_POSZERO", "0x0040");
  Builder.defineMacro("__FPCLASS_POSSUBNORMAL", "0x0080");
  Builder.defineMacro("__FPCLASS_POSNORMAL", "0x0100");
  Builder.defineMacro("__FPCLASS_POSINF", "0x0200");

  Builder.defineMacro(
      "__VERSION__", "\"" + llvm::Twine(getNeverCFullVersionForMacro()) + "\"");

  // Standard conforming mode?
  if (!LangOpts.GNUMode && !LangOpts.MSVCCompat)
    Builder.defineMacro("__STRICT_ANSI__");

  if (!LangOpts.IgnoreExceptions) {
    if (!LangOpts.MSVCCompat && LangOpts.Exceptions)
      Builder.defineMacro("__EXCEPTIONS");

    if (LangOpts.hasSEHExceptions())
      Builder.defineMacro("__SEH__");
  }

  if (LangOpts.Deprecated)
    Builder.defineMacro("__DEPRECATED");

  if (LangOpts.MicrosoftExt) {
    if (LangOpts.WChar) {
      Builder.defineMacro("_WCHAR_T_DEFINED");
      Builder.defineMacro("_NATIVE_WCHAR_T_DEFINED");
    }
  }

  // Macros to help identify the narrow and wide character sets
  Builder.defineMacro("__neverc_literal_encoding__", "\"UTF-8\"");
  if (TI.getTypeWidth(TI.getWCharType()) >= 32) {
    Builder.defineMacro("__neverc_wide_literal_encoding__", "\"UTF-32\"");
  } else {
    Builder.defineMacro("__neverc_wide_literal_encoding__", "\"UTF-16\"");
  }

  if (LangOpts.Optimize)
    Builder.defineMacro("__OPTIMIZE__");
  if (LangOpts.OptimizeSize)
    Builder.defineMacro("__OPTIMIZE_SIZE__");

  if (LangOpts.FastMath)
    Builder.defineMacro("__FAST_MATH__");

  Builder.defineMacro("__ORDER_LITTLE_ENDIAN__", "1234");
  Builder.defineMacro("__ORDER_BIG_ENDIAN__", "4321");
  Builder.defineMacro("__ORDER_PDP_ENDIAN__", "3412");
  Builder.defineMacro("__BYTE_ORDER__", "__ORDER_LITTLE_ENDIAN__");
  Builder.defineMacro("__LITTLE_ENDIAN__");

  if (TI.getPointerWidth(LangAS::Default) == 64 && TI.getLongWidth() == 64 &&
      TI.getIntWidth() == 32) {
    Builder.defineMacro("_LP64");
    Builder.defineMacro("__LP64__");
  }

  // Define type sizing macros based on the target properties.
  assert(TI.getCharWidth() == 8 && "Only support 8-bit char so far");
  Builder.defineMacro("__CHAR_BIT__", llvm::Twine(TI.getCharWidth()));

  Builder.defineMacro("__BOOL_WIDTH__", llvm::Twine(TI.getBoolWidth()));
  Builder.defineMacro("__SHRT_WIDTH__", llvm::Twine(TI.getShortWidth()));
  Builder.defineMacro("__INT_WIDTH__", llvm::Twine(TI.getIntWidth()));
  Builder.defineMacro("__LONG_WIDTH__", llvm::Twine(TI.getLongWidth()));
  Builder.defineMacro("__LLONG_WIDTH__", llvm::Twine(TI.getLongLongWidth()));

  size_t BitIntMaxWidth = TI.getMaxBitIntWidth();
  assert(BitIntMaxWidth <= llvm::IntegerType::MAX_INT_BITS &&
         "Target defined a max bit width larger than LLVM can support!");
  assert(BitIntMaxWidth >= TI.getLongLongWidth() &&
         "Target defined a max bit width smaller than the C standard allows!");
  Builder.defineMacro("__BITINT_MAXWIDTH__", llvm::Twine(BitIntMaxWidth));

  defineTypeSize("__SCHAR_MAX__", TargetInfo::SignedChar, TI, Builder);
  defineTypeSize("__SHRT_MAX__", TargetInfo::SignedShort, TI, Builder);
  defineTypeSize("__INT_MAX__", TargetInfo::SignedInt, TI, Builder);
  defineTypeSize("__LONG_MAX__", TargetInfo::SignedLong, TI, Builder);
  defineTypeSize("__LONG_LONG_MAX__", TargetInfo::SignedLongLong, TI, Builder);
  defineTypeSizeAndWidth("__WCHAR", TI.getWCharType(), TI, Builder);
  defineTypeSizeAndWidth("__WINT", TI.getWIntType(), TI, Builder);
  defineTypeSizeAndWidth("__INTMAX", TI.getIntMaxType(), TI, Builder);
  defineTypeSizeAndWidth("__SIZE", TI.getSizeType(), TI, Builder);

  defineTypeSizeAndWidth("__UINTMAX", TI.getUIntMaxType(), TI, Builder);
  defineTypeSizeAndWidth("__PTRDIFF", TI.getPtrDiffType(LangAS::Default), TI,
                         Builder);
  defineTypeSizeAndWidth("__INTPTR", TI.getIntPtrType(), TI, Builder);
  defineTypeSizeAndWidth("__UINTPTR", TI.getUIntPtrType(), TI, Builder);

  defineTypeSizeof("__SIZEOF_DOUBLE__", TI.getDoubleWidth(), TI, Builder);
  defineTypeSizeof("__SIZEOF_FLOAT__", TI.getFloatWidth(), TI, Builder);
  defineTypeSizeof("__SIZEOF_INT__", TI.getIntWidth(), TI, Builder);
  defineTypeSizeof("__SIZEOF_LONG__", TI.getLongWidth(), TI, Builder);
  defineTypeSizeof("__SIZEOF_LONG_DOUBLE__", TI.getLongDoubleWidth(), TI,
                   Builder);
  defineTypeSizeof("__SIZEOF_LONG_LONG__", TI.getLongLongWidth(), TI, Builder);
  defineTypeSizeof("__SIZEOF_POINTER__", TI.getPointerWidth(LangAS::Default),
                   TI, Builder);
  defineTypeSizeof("__SIZEOF_SHORT__", TI.getShortWidth(), TI, Builder);
  defineTypeSizeof("__SIZEOF_PTRDIFF_T__",
                   TI.getTypeWidth(TI.getPtrDiffType(LangAS::Default)), TI,
                   Builder);
  defineTypeSizeof("__SIZEOF_SIZE_T__", TI.getTypeWidth(TI.getSizeType()), TI,
                   Builder);
  defineTypeSizeof("__SIZEOF_WCHAR_T__", TI.getTypeWidth(TI.getWCharType()), TI,
                   Builder);
  defineTypeSizeof("__SIZEOF_WINT_T__", TI.getTypeWidth(TI.getWIntType()), TI,
                   Builder);
  if (TI.hasInt128Type())
    defineTypeSizeof("__SIZEOF_INT128__", 128, TI, Builder);

  defineType("__INTMAX_TYPE__", TI.getIntMaxType(), Builder);
  defineFmt("__INTMAX", TI.getIntMaxType(), TI, Builder);
  Builder.defineMacro("__INTMAX_C_SUFFIX__",
                      TI.getTypeConstantSuffix(TI.getIntMaxType()));
  defineType("__UINTMAX_TYPE__", TI.getUIntMaxType(), Builder);
  defineFmt("__UINTMAX", TI.getUIntMaxType(), TI, Builder);
  Builder.defineMacro("__UINTMAX_C_SUFFIX__",
                      TI.getTypeConstantSuffix(TI.getUIntMaxType()));
  defineType("__PTRDIFF_TYPE__", TI.getPtrDiffType(LangAS::Default), Builder);
  defineFmt("__PTRDIFF", TI.getPtrDiffType(LangAS::Default), TI, Builder);
  defineType("__INTPTR_TYPE__", TI.getIntPtrType(), Builder);
  defineFmt("__INTPTR", TI.getIntPtrType(), TI, Builder);
  defineType("__SIZE_TYPE__", TI.getSizeType(), Builder);
  defineFmt("__SIZE", TI.getSizeType(), TI, Builder);
  defineType("__WCHAR_TYPE__", TI.getWCharType(), Builder);
  defineType("__WINT_TYPE__", TI.getWIntType(), Builder);
  defineTypeSizeAndWidth("__SIG_ATOMIC", TI.getSigAtomicType(), TI, Builder);
  defineType("__CHAR16_TYPE__", TI.getChar16Type(), Builder);
  defineType("__CHAR32_TYPE__", TI.getChar32Type(), Builder);

  defineType("__UINTPTR_TYPE__", TI.getUIntPtrType(), Builder);
  defineFmt("__UINTPTR", TI.getUIntPtrType(), TI, Builder);

  assert(TI.getTypeWidth(TI.getUIntPtrType()) ==
             TI.getTypeWidth(TI.getIntPtrType()) &&
         "uintptr_t and intptr_t have different widths?");
  assert(TI.getTypeWidth(TI.getUIntMaxType()) ==
             TI.getTypeWidth(TI.getIntMaxType()) &&
         "uintmax_t and intmax_t have different widths?");

  if (TI.hasFloat16Type())
    defineFloatMacros(Builder, "FLT16", &TI.getHalfFormat(), "F16");
  defineFloatMacros(Builder, "FLT", &TI.getFloatFormat(), "F");
  defineFloatMacros(Builder, "DBL", &TI.getDoubleFormat(), "");
  defineFloatMacros(Builder, "LDBL", &TI.getLongDoubleFormat(), "L");

  // Define a __POINTER_WIDTH__ macro for stdint.h.
  Builder.defineMacro("__POINTER_WIDTH__",
                      llvm::Twine((int)TI.getPointerWidth(LangAS::Default)));

  // Define __BIGGEST_ALIGNMENT__ to be compatible with gcc.
  Builder.defineMacro("__BIGGEST_ALIGNMENT__",
                      llvm::Twine(TI.getSuitableAlign() / TI.getCharWidth()));

  if (!LangOpts.CharIsSigned)
    Builder.defineMacro("__CHAR_UNSIGNED__");

  if (!TargetInfo::isTypeSigned(TI.getWCharType()))
    Builder.defineMacro("__WCHAR_UNSIGNED__");

  if (!TargetInfo::isTypeSigned(TI.getWIntType()))
    Builder.defineMacro("__WINT_UNSIGNED__");

  // Define exact-width integer types for stdint.h
  defineExactWidthIntType(TargetInfo::SignedChar, TI, Builder);

  if (TI.getShortWidth() > TI.getCharWidth())
    defineExactWidthIntType(TargetInfo::SignedShort, TI, Builder);

  if (TI.getIntWidth() > TI.getShortWidth())
    defineExactWidthIntType(TargetInfo::SignedInt, TI, Builder);

  if (TI.getLongWidth() > TI.getIntWidth())
    defineExactWidthIntType(TargetInfo::SignedLong, TI, Builder);

  if (TI.getLongLongWidth() > TI.getLongWidth())
    defineExactWidthIntType(TargetInfo::SignedLongLong, TI, Builder);

  defineExactWidthIntType(TargetInfo::UnsignedChar, TI, Builder);
  defineExactWidthIntTypeSize(TargetInfo::UnsignedChar, TI, Builder);
  defineExactWidthIntTypeSize(TargetInfo::SignedChar, TI, Builder);

  if (TI.getShortWidth() > TI.getCharWidth()) {
    defineExactWidthIntType(TargetInfo::UnsignedShort, TI, Builder);
    defineExactWidthIntTypeSize(TargetInfo::UnsignedShort, TI, Builder);
    defineExactWidthIntTypeSize(TargetInfo::SignedShort, TI, Builder);
  }

  if (TI.getIntWidth() > TI.getShortWidth()) {
    defineExactWidthIntType(TargetInfo::UnsignedInt, TI, Builder);
    defineExactWidthIntTypeSize(TargetInfo::UnsignedInt, TI, Builder);
    defineExactWidthIntTypeSize(TargetInfo::SignedInt, TI, Builder);
  }

  if (TI.getLongWidth() > TI.getIntWidth()) {
    defineExactWidthIntType(TargetInfo::UnsignedLong, TI, Builder);
    defineExactWidthIntTypeSize(TargetInfo::UnsignedLong, TI, Builder);
    defineExactWidthIntTypeSize(TargetInfo::SignedLong, TI, Builder);
  }

  if (TI.getLongLongWidth() > TI.getLongWidth()) {
    defineExactWidthIntType(TargetInfo::UnsignedLongLong, TI, Builder);
    defineExactWidthIntTypeSize(TargetInfo::UnsignedLongLong, TI, Builder);
    defineExactWidthIntTypeSize(TargetInfo::SignedLongLong, TI, Builder);
  }

  defineLeastWidthIntType(8, true, TI, Builder);
  defineLeastWidthIntType(8, false, TI, Builder);
  defineLeastWidthIntType(16, true, TI, Builder);
  defineLeastWidthIntType(16, false, TI, Builder);
  defineLeastWidthIntType(32, true, TI, Builder);
  defineLeastWidthIntType(32, false, TI, Builder);
  defineLeastWidthIntType(64, true, TI, Builder);
  defineLeastWidthIntType(64, false, TI, Builder);

  defineFastIntType(8, true, TI, Builder);
  defineFastIntType(8, false, TI, Builder);
  defineFastIntType(16, true, TI, Builder);
  defineFastIntType(16, false, TI, Builder);
  defineFastIntType(32, true, TI, Builder);
  defineFastIntType(32, false, TI, Builder);
  defineFastIntType(64, true, TI, Builder);
  defineFastIntType(64, false, TI, Builder);

  Builder.defineMacro("__USER_LABEL_PREFIX__", TI.getUserLabelPrefix());

  if (!LangOpts.MathErrno)
    Builder.defineMacro("__NO_MATH_ERRNO__");

  if (LangOpts.FastMath || LangOpts.FiniteMathOnly)
    Builder.defineMacro("__FINITE_MATH_ONLY__", "1");
  else
    Builder.defineMacro("__FINITE_MATH_ONLY__", "0");

  if (LangOpts.GNUCVersion) {
    if (LangOpts.GNUInline)
      Builder.defineMacro("__GNUC_GNU_INLINE__");
    else
      Builder.defineMacro("__GNUC_STDC_INLINE__");

    Builder.defineMacro("__GCC_ATOMIC_TEST_AND_SET_TRUEVAL", "1");
  }

  auto addLockFreeMacros = [&](const llvm::Twine &Prefix) {
#define DEFINE_LOCK_FREE_MACRO(TYPE, Type)                                     \
  Builder.defineMacro(Prefix + #TYPE "_LOCK_FREE",                             \
                      getLockFreeValue(TI.get##Type##Width(), TI));
    DEFINE_LOCK_FREE_MACRO(BOOL, Bool);
    DEFINE_LOCK_FREE_MACRO(CHAR, Char);
    if (LangOpts.Char8)
      DEFINE_LOCK_FREE_MACRO(CHAR8_T, Char);
    DEFINE_LOCK_FREE_MACRO(CHAR16_T, Char16);
    DEFINE_LOCK_FREE_MACRO(CHAR32_T, Char32);
    DEFINE_LOCK_FREE_MACRO(WCHAR_T, WChar);
    DEFINE_LOCK_FREE_MACRO(SHORT, Short);
    DEFINE_LOCK_FREE_MACRO(INT, Int);
    DEFINE_LOCK_FREE_MACRO(LONG, Long);
    DEFINE_LOCK_FREE_MACRO(LLONG, LongLong);
    Builder.defineMacro(
        Prefix + "POINTER_LOCK_FREE",
        getLockFreeValue(TI.getPointerWidth(LangAS::Default), TI));
#undef DEFINE_LOCK_FREE_MACRO
  };
  addLockFreeMacros("__NEVERC_ATOMIC_");
  addLockFreeMacros("__CLANG_ATOMIC_");
  if (LangOpts.GNUCVersion)
    addLockFreeMacros("__GCC_ATOMIC_");

  if (LangOpts.NoInlineDefine)
    Builder.defineMacro("__NO_INLINE__");

  if (LangOpts.PIE) {
    Builder.defineMacro("__PIE__", llvm::Twine(llvm::PIELevel::Large));
    Builder.defineMacro("__pie__", llvm::Twine(llvm::PIELevel::Large));
  }

  // Macros to control C99 numerics and <float.h>
  Builder.defineMacro("__FLT_RADIX__", "2");
  Builder.defineMacro("__DECIMAL_DIG__", "__LDBL_DECIMAL_DIG__");

  if (LangOpts.getStackProtector() == LangOptions::SSPOn)
    Builder.defineMacro("__SSP__");
  else if (LangOpts.getStackProtector() == LangOptions::SSPStrong)
    Builder.defineMacro("__SSP_STRONG__", "2");
  else if (LangOpts.getStackProtector() == LangOptions::SSPReq)
    Builder.defineMacro("__SSP_ALL__", "3");

  if (TI.getTriple().isOSDarwin()) {
    Builder.defineMacro("__nonnull", "_Nonnull");
    Builder.defineMacro("__null_unspecified", "_Null_unspecified");
    Builder.defineMacro("__nullable", "_Nullable");
  }

  if (TI.getTriple().isOSDarwin() && TI.getTriple().isSimulatorEnvironment())
    Builder.defineMacro("__APPLE_EMBEDDED_SIMULATOR__", "1");

  if (TI.getTriple().isOSBinFormatELF()) {
    Builder.defineMacro("__ELF__");
    Builder.defineMacro("__NEVERC__ELF__");
  } else if (TI.getTriple().isOSBinFormatCOFF()) {
    Builder.defineMacro("__NEVERC__PE__");
  } else if (TI.getTriple().isOSBinFormatMachO()) {
    Builder.defineMacro("__NEVERC__MACOS__");
  }

  if (LangOpts.BuiltinMimalloc)
    Builder.defineMacro("__NEVERC_MIMALLOC__", "1");

  // Target OS macro definitions.
  if (PPOpts.DefineTargetOSMacros) {
    const llvm::Triple &Triple = TI.getTriple();
#define TARGET_OS(Name, Predicate)                                             \
  Builder.defineMacro(#Name, (Predicate) ? "1" : "0");
#undef TARGET_OS
  }

  TI.getTargetDefines(LangOpts, Builder);

#ifdef _WIN32
  Builder.append("#if !defined(offsetof) || __has_feature(modules)");
  Builder.append("#define offsetof(t, d) __builtin_offsetof(t, d)");
  Builder.append("#endif");
#endif
}
