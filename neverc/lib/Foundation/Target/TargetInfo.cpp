#include "neverc/Foundation/Target/TargetInfo.h"
#include "AArch64.h"
#include "OSTargets.h"
#include "Targets.h"
#include "X86.h"
#include "neverc/Config/config.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/AddressSpaces.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/DarwinSDKInfo.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/MacroBuilder.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/DiagnosticFrontend.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/SpecialCaseList.h"
#include <cstdlib>
#include <limits>
#include <optional>
#include <vector>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Target info implementation
// ===----------------------------------------------------------------------===

namespace {
const LangASMap DefaultAddrSpaceMap = {0};
} // namespace

// TargetInfo Constructor.
TargetInfo::TargetInfo(const llvm::Triple &T) : Triple(T) {
  // Set defaults; concrete targets (x86_64, AArch64) override as needed.
  TLSSupported = true;
  VLASupported = true;
  NoAsmVariants = false;
  HasLegalHalfType = false;
  HalfArgsAndReturns = false;
  HasFloat128 = false;
  HasFloat16 = false;
  HasBFloat16 = false;
  HasFullBFloat16 = false;
  HasLongDouble = true;
  HasFPReturn = true;
  HasStrictFP = false;
  PointerWidth = PointerAlign = 32;
  BoolWidth = BoolAlign = 8;
  IntWidth = IntAlign = 32;
  LongWidth = LongAlign = 32;
  LongLongWidth = LongLongAlign = 64;
  Int128Align = 128;

  // Fixed point default bit widths
  ShortAccumWidth = ShortAccumAlign = 16;
  AccumWidth = AccumAlign = 32;
  LongAccumWidth = LongAccumAlign = 64;
  ShortFractWidth = ShortFractAlign = 8;
  FractWidth = FractAlign = 16;
  LongFractWidth = LongFractAlign = 32;

  // Fixed point default integral and fractional bit sizes
  // We give the _Accum 1 fewer fractional bits than their corresponding _Fract
  // types by default to have the same number of fractional bits between _Accum
  // and _Fract types.
  PaddingOnUnsignedFixedPoint = false;
  ShortAccumScale = 7;
  AccumScale = 15;
  LongAccumScale = 31;

  SuitableAlign = 64;
  DefaultAlignForAttributeAligned = 128;
  MinGlobalAlign = 0;
  // From the glibc documentation, on GNU systems, malloc guarantees 16-byte
  // alignment on 64-bit systems. See
  // https://www.gnu.org/software/libc/manual/html_node/Malloc-Examples.html.
  // This alignment guarantee also applies to Windows. On Darwin the alignment
  // is 16 bytes.
  if (T.isGNUEnvironment() || T.isWindowsMSVCEnvironment())
    NewAlign = 128;
  else if (T.isOSDarwin())
    NewAlign = 128;
  else
    NewAlign = 0; // Infer from basic type alignment.
  HalfWidth = 16;
  HalfAlign = 16;
  FloatWidth = 32;
  FloatAlign = 32;
  DoubleWidth = 64;
  DoubleAlign = 64;
  LongDoubleWidth = 64;
  LongDoubleAlign = 64;
  Float128Align = 128;
  LargeArrayMinWidth = 0;
  LargeArrayAlign = 0;
  MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 0;
  MaxVectorAlign = 0;
  MaxTLSAlign = 0;
  SizeType = UnsignedLong;
  PtrDiffType = SignedLong;
  IntMaxType = SignedLongLong;
  IntPtrType = SignedLong;
  WCharType = SignedInt;
  WIntType = SignedInt;
  Char16Type = UnsignedShort;
  Char32Type = UnsignedInt;
  Int64Type = SignedLongLong;
  Int16Type = SignedShort;
  SigAtomicType = SignedInt;
  ProcessIDType = SignedInt;
  UseBitFieldTypeAlignment = true;
  UseZeroLengthBitfieldAlignment = false;
  UseLeadingZeroLengthBitfield = true;
  UseExplicitBitFieldAlignment = true;
  ZeroLengthBitfieldBoundary = 0;
  MaxAlignedAttribute = 0;
  HalfFormat = &llvm::APFloat::IEEEhalf();
  FloatFormat = &llvm::APFloat::IEEEsingle();
  DoubleFormat = &llvm::APFloat::IEEEdouble();
  LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  Float128Format = &llvm::APFloat::IEEEquad();
  UserLabelPrefix = "_";
  RegParmMax = 0;
  SSERegParmMax = 0;
  HasBuiltinMSVaList = false;
  HasAArch64SVETypes = false;

  // Default to no types using fpret.

  // Default to not using fp2ret for __Complex long double
  ComplexLongDoubleUsesFP2Ret = false;

  // Default to an empty address space map.
  AddrSpaceMap = &DefaultAddrSpaceMap;
  UseAddrSpaceMapMangling = false;

  // Default to an unknown platform name.
  PlatformName = "unknown";
  PlatformMinVersion = llvm::VersionTuple();

  MaxBitIntWidth.reset();
}

// Out of line virtual dtor for TargetInfo.
TargetInfo::~TargetInfo() {}

void TargetInfo::resetDataLayout(llvm::StringRef DL, const char *ULP) {
  DataLayoutString = DL.str();
  UserLabelPrefix = ULP;
}

bool TargetInfo::checkCFProtectionBranchSupported(
    DiagnosticsEngine &Diags) const {
  Diags.Report(diag::err_opt_not_valid_on_target) << "cf-protection=branch";
  return false;
}

bool TargetInfo::checkCFProtectionReturnSupported(
    DiagnosticsEngine &Diags) const {
  Diags.Report(diag::err_opt_not_valid_on_target) << "cf-protection=return";
  return false;
}

const char *TargetInfo::getTypeName(IntType T) {
  switch (T) {
  default:
    return "";
  case SignedChar:
    return "signed char";
  case UnsignedChar:
    return "unsigned char";
  case SignedShort:
    return "short";
  case UnsignedShort:
    return "unsigned short";
  case SignedInt:
    return "int";
  case UnsignedInt:
    return "unsigned int";
  case SignedLong:
    return "long int";
  case UnsignedLong:
    return "long unsigned int";
  case SignedLongLong:
    return "long long int";
  case UnsignedLongLong:
    return "long long unsigned int";
  }
}

const char *TargetInfo::getTypeConstantSuffix(IntType T) const {
  switch (T) {
  default:
    return "";
  case SignedChar:
  case SignedShort:
  case SignedInt:
    return "";
  case SignedLong:
    return "L";
  case SignedLongLong:
    return "LL";
  case UnsignedChar:
    if (getCharWidth() < getIntWidth())
      return "";
    [[fallthrough]];
  case UnsignedShort:
    if (getShortWidth() < getIntWidth())
      return "";
    [[fallthrough]];
  case UnsignedInt:
    return "U";
  case UnsignedLong:
    return "UL";
  case UnsignedLongLong:
    return "ULL";
  }
}

const char *TargetInfo::getTypeFormatModifier(IntType T) {
  switch (T) {
  default:
    return "";
  case SignedChar:
  case UnsignedChar:
    return "hh";
  case SignedShort:
  case UnsignedShort:
    return "h";
  case SignedInt:
  case UnsignedInt:
    return "";
  case SignedLong:
  case UnsignedLong:
    return "l";
  case SignedLongLong:
  case UnsignedLongLong:
    return "ll";
  }
}

unsigned TargetInfo::getTypeWidth(IntType T) const {
  switch (T) {
  default:
    return 0;
  case SignedChar:
  case UnsignedChar:
    return getCharWidth();
  case SignedShort:
  case UnsignedShort:
    return getShortWidth();
  case SignedInt:
  case UnsignedInt:
    return getIntWidth();
  case SignedLong:
  case UnsignedLong:
    return getLongWidth();
  case SignedLongLong:
  case UnsignedLongLong:
    return getLongLongWidth();
  };
}

TargetInfo::IntType TargetInfo::getIntTypeByWidth(unsigned BitWidth,
                                                  bool IsSigned) const {
  if (getCharWidth() == BitWidth)
    return IsSigned ? SignedChar : UnsignedChar;
  if (getShortWidth() == BitWidth)
    return IsSigned ? SignedShort : UnsignedShort;
  if (getIntWidth() == BitWidth)
    return IsSigned ? SignedInt : UnsignedInt;
  if (getLongWidth() == BitWidth)
    return IsSigned ? SignedLong : UnsignedLong;
  if (getLongLongWidth() == BitWidth)
    return IsSigned ? SignedLongLong : UnsignedLongLong;
  return NoInt;
}

TargetInfo::IntType TargetInfo::getLeastIntTypeByWidth(unsigned BitWidth,
                                                       bool IsSigned) const {
  if (getCharWidth() >= BitWidth)
    return IsSigned ? SignedChar : UnsignedChar;
  if (getShortWidth() >= BitWidth)
    return IsSigned ? SignedShort : UnsignedShort;
  if (getIntWidth() >= BitWidth)
    return IsSigned ? SignedInt : UnsignedInt;
  if (getLongWidth() >= BitWidth)
    return IsSigned ? SignedLong : UnsignedLong;
  if (getLongLongWidth() >= BitWidth)
    return IsSigned ? SignedLongLong : UnsignedLongLong;
  return NoInt;
}

FloatModeKind TargetInfo::getRealTypeByWidth(unsigned BitWidth,
                                             FloatModeKind ExplicitType) const {
  if (getHalfWidth() == BitWidth)
    return FloatModeKind::Half;
  if (getFloatWidth() == BitWidth)
    return FloatModeKind::Float;
  if (getDoubleWidth() == BitWidth)
    return FloatModeKind::Double;

  switch (BitWidth) {
  case 96:
    if (&getLongDoubleFormat() == &llvm::APFloat::x87DoubleExtended())
      return FloatModeKind::LongDouble;
    break;
  case 128:
    // The caller explicitly asked for an IEEE compliant type but we still
    // have to check if the target supports it.
    if (ExplicitType == FloatModeKind::Float128)
      return hasFloat128Type() ? FloatModeKind::Float128
                               : FloatModeKind::NoFloat;
    if (&getLongDoubleFormat() == &llvm::APFloat::IEEEquad())
      return FloatModeKind::LongDouble;
    if (hasFloat128Type())
      return FloatModeKind::Float128;
    break;
  }

  return FloatModeKind::NoFloat;
}

unsigned TargetInfo::getTypeAlign(IntType T) const {
  switch (T) {
  default:
    return 0;
  case SignedChar:
  case UnsignedChar:
    return getCharAlign();
  case SignedShort:
  case UnsignedShort:
    return getShortAlign();
  case SignedInt:
  case UnsignedInt:
    return getIntAlign();
  case SignedLong:
  case UnsignedLong:
    return getLongAlign();
  case SignedLongLong:
  case UnsignedLongLong:
    return getLongLongAlign();
  };
}

bool TargetInfo::isTypeSigned(IntType T) {
  switch (T) {
  default:
    return false;
  case SignedChar:
  case SignedShort:
  case SignedInt:
  case SignedLong:
  case SignedLongLong:
    return true;
  case UnsignedChar:
  case UnsignedShort:
  case UnsignedInt:
  case UnsignedLong:
  case UnsignedLongLong:
    return false;
  };
}

void TargetInfo::adjust(DiagnosticsEngine &Diags, LangOptions &Opts) {
  if (Opts.NoBitFieldTypeAlign)
    UseBitFieldTypeAlignment = false;

  switch (Opts.WCharSize) {
  default:
    llvm::report_fatal_error("invalid wchar_t width");
  case 0:
    break;
  case 1:
    WCharType = Opts.WCharIsSigned ? SignedChar : UnsignedChar;
    break;
  case 2:
    WCharType = Opts.WCharIsSigned ? SignedShort : UnsignedShort;
    break;
  case 4:
    WCharType = Opts.WCharIsSigned ? SignedInt : UnsignedInt;
    break;
  }

  if (Opts.AlignDouble) {
    DoubleAlign = LongLongAlign = 64;
    LongDoubleAlign = 64;
  }

  if (Opts.DoubleSize) {
    if (Opts.DoubleSize == 32) {
      DoubleWidth = 32;
      LongDoubleWidth = 32;
      DoubleFormat = &llvm::APFloat::IEEEsingle();
      LongDoubleFormat = &llvm::APFloat::IEEEsingle();
    } else if (Opts.DoubleSize == 64) {
      DoubleWidth = 64;
      LongDoubleWidth = 64;
      DoubleFormat = &llvm::APFloat::IEEEdouble();
      LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    }
  }

  if (Opts.LongDoubleSize) {
    if (Opts.LongDoubleSize == DoubleWidth) {
      LongDoubleWidth = DoubleWidth;
      LongDoubleAlign = DoubleAlign;
      LongDoubleFormat = DoubleFormat;
    } else if (Opts.LongDoubleSize == 128) {
      LongDoubleWidth = LongDoubleAlign = 128;
      LongDoubleFormat = &llvm::APFloat::IEEEquad();
    } else if (Opts.LongDoubleSize == 80) {
      LongDoubleFormat = &llvm::APFloat::x87DoubleExtended();
      if (getTriple().isWindowsMSVCEnvironment()) {
        LongDoubleWidth = 128;
        LongDoubleAlign = 128;
      } else { // Non-MSVC (e.g. Linux x86_64): 128-bit aligned 80-bit storage
        LongDoubleWidth = 128;
        LongDoubleAlign = 128;
      }
    }
  }

  // Each unsigned fixed point type has the same number of fractional bits as
  // its corresponding signed type.
  PaddingOnUnsignedFixedPoint |= Opts.PaddingOnUnsignedFixedPoint;
  CheckFixedPointBits();

  if (Opts.ProtectParens && !checkArithmeticFenceSupported()) {
    Diags.Report(diag::err_opt_not_valid_on_target) << "-fprotect-parens";
    Opts.ProtectParens = false;
  }

  if (Opts.MaxBitIntWidth)
    MaxBitIntWidth = static_cast<unsigned>(Opts.MaxBitIntWidth);
}

bool TargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
    llvm::StringRef CPU, const std::vector<std::string> &FeatureVec) const {
  for (const auto &F : FeatureVec) {
    llvm::StringRef Name = F;
    if (LLVM_UNLIKELY(Name.empty()))
      continue;
    char Prefix = Name[0];
    if (LLVM_UNLIKELY(Prefix != '+' && Prefix != '-'))
      Diags.Report(diag::warn_fe_backend_invalid_feature_flag) << Name;
    else
      setFeatureEnabled(Features, Name.substr(1), Prefix == '+');
  }
  return true;
}

ParsedTargetAttr TargetInfo::parseTargetAttr(llvm::StringRef Features) const {
  ParsedTargetAttr Ret;
  if (Features == "default")
    return Ret;
  llvm::SmallVector<llvm::StringRef, 1> AttrFeatures;
  Features.split(AttrFeatures, ",");

  // Grab the various features and prepend a "+" to turn on the feature to
  // the backend and add them to our existing set of features.
  for (auto &Feature : AttrFeatures) {
    // Go ahead and trim whitespace rather than either erroring or
    // accepting it weirdly.
    Feature = Feature.trim();

    if (Feature.starts_with("fpmath="))
      continue;

    if (Feature.starts_with("branch-protection=")) {
      Ret.BranchProtection = Feature.split('=').second.trim();
      continue;
    }

    // While we're here iterating check for a different target cpu.
    if (Feature.starts_with("arch=")) {
      if (!Ret.CPU.empty())
        Ret.Duplicate = "arch=";
      else
        Ret.CPU = Feature.split("=").second.trim();
    } else if (Feature.starts_with("tune=")) {
      if (!Ret.Tune.empty())
        Ret.Duplicate = "tune=";
      else
        Ret.Tune = Feature.split("=").second.trim();
    } else if (Feature.starts_with("no-"))
      Ret.Features.push_back("-" + Feature.split("-").second.str());
    else
      Ret.Features.push_back("+" + Feature.str());
  }
  return Ret;
}

TargetInfo::CallingConvKind
TargetInfo::getCallingConvKind(bool ABICompat4) const {
  if (ABICompat4)
    return CCK_NeverCABI4OrPS4;
  return CCK_Default;
}

namespace {
llvm::StringRef stripRegisterPrefix(llvm::StringRef Name) {
  if (Name[0] == '%' || Name[0] == '#')
    Name = Name.substr(1);

  return Name;
}
} // namespace

bool TargetInfo::isValidClobber(llvm::StringRef Name) const {
  return (isValidGCCRegisterName(Name) || Name == "memory" || Name == "cc" ||
          Name == "unwind");
}

bool TargetInfo::isValidGCCRegisterName(llvm::StringRef Name) const {
  if (Name.empty())
    return false;

  Name = stripRegisterPrefix(Name);
  if (Name.empty())
    return false;

  llvm::ArrayRef<const char *> Names = getGCCRegNames();

  if (isDigit(Name[0])) {
    unsigned n;
    if (!Name.getAsInteger(0, n))
      return n < Names.size();
  }

  if (llvm::is_contained(Names, Name))
    return true;

  for (const AddlRegName &ARN : getGCCAddlRegNames())
    for (const char *AN : ARN.Names) {
      if (!AN)
        break;
      // Make sure the register that the additional name is for is within
      // the bounds of the register names from above.
      if (AN == Name && ARN.RegNum < Names.size())
        return true;
    }

  // Now check aliases.
  for (const GCCRegAlias &GRA : getGCCRegAliases())
    for (const char *A : GRA.Aliases) {
      if (!A)
        break;
      if (A == Name)
        return true;
    }

  return false;
}

llvm::StringRef
TargetInfo::getNormalizedGCCRegisterName(llvm::StringRef Name,
                                         bool ReturnCanonical) const {
  assert(isValidGCCRegisterName(Name) && "Invalid register passed in");

  Name = stripRegisterPrefix(Name);

  llvm::ArrayRef<const char *> Names = getGCCRegNames();

  if (isDigit(Name[0])) {
    unsigned n;
    if (!Name.getAsInteger(0, n)) {
      assert(n < Names.size() && "Out of bounds register number!");
      return Names[n];
    }
  }

  for (const AddlRegName &ARN : getGCCAddlRegNames())
    for (const char *AN : ARN.Names) {
      if (!AN)
        break;
      if (AN == Name && ARN.RegNum < Names.size())
        return ReturnCanonical ? Names[ARN.RegNum] : Name;
    }

  for (const GCCRegAlias &RA : getGCCRegAliases())
    for (const char *A : RA.Aliases) {
      if (!A)
        break;
      if (A == Name)
        return RA.Register;
    }

  return Name;
}

bool TargetInfo::validateOutputConstraint(ConstraintInfo &Info) const {
  const char *Name = Info.getConstraintStr().c_str();
  // An output constraint must start with '=' or '+'
  if (*Name != '=' && *Name != '+')
    return false;

  if (*Name == '+')
    Info.setIsReadWrite();

  Name++;
  while (*Name) {
    switch (*Name) {
    default:
      if (!validateAsmConstraint(Name, Info)) {
        return false;
      }
      break;
    case '&': // early clobber.
      Info.setEarlyClobber();
      break;
    case '%': // commutative.
      break;
    case 'r': // general register.
      Info.setAllowsRegister();
      break;
    case 'm': // memory operand.
    case 'o': // offsetable memory operand.
    case 'V': // non-offsetable memory operand.
    case '<': // autodecrement memory operand.
    case '>': // autoincrement memory operand.
      Info.setAllowsMemory();
      break;
    case 'g': // general register, memory operand or immediate integer.
    case 'X': // any operand.
      Info.setAllowsRegister();
      Info.setAllowsMemory();
      break;
    case ',': // multiple alternative constraint.  Pass it.
      if (Name[1] == '=' || Name[1] == '+')
        Name++;
      break;
    case '#': // Ignore as constraint.
      while (Name[1] && Name[1] != ',')
        Name++;
      break;
    case '?': // Disparage slightly code.
    case '!': // Disparage severely.
    case '*': // Ignore for choosing register preferences.
    case 'i': // Ignore i,n,E,F as output constraints (match from the other
              // chars)
    case 'n':
    case 'E':
    case 'F':
      break; // Pass them.
    }

    Name++;
  }

  // Early clobber with a read-write constraint which doesn't permit registers
  // is invalid.
  if (Info.earlyClobber() && Info.isReadWrite() && !Info.allowsRegister())
    return false;

  // If a constraint allows neither memory nor register operands it contains
  // only modifiers. Reject it.
  return Info.allowsMemory() || Info.allowsRegister();
}

bool TargetInfo::resolveSymbolicName(
    const char *&Name, llvm::ArrayRef<ConstraintInfo> OutputConstraints,
    unsigned &Index) const {
  assert(*Name == '[' && "Symbolic name did not start with '['");
  Name++;
  const char *Start = Name;
  while (*Name && *Name != ']')
    Name++;

  if (!*Name) {
    // Missing ']'
    return false;
  }

  std::string SymbolicName(Start, Name - Start);

  for (Index = 0; Index != OutputConstraints.size(); ++Index)
    if (SymbolicName == OutputConstraints[Index].getName())
      return true;

  return false;
}

bool TargetInfo::validateInputConstraint(
    llvm::MutableArrayRef<ConstraintInfo> OutputConstraints,
    ConstraintInfo &Info) const {
  const char *Name = Info.ConstraintStr.c_str();

  if (!*Name)
    return false;

  while (*Name) {
    switch (*Name) {
    default:
      if (*Name >= '0' && *Name <= '9') {
        const char *DigitStart = Name;
        while (Name[1] >= '0' && Name[1] <= '9')
          Name++;
        const char *DigitEnd = Name;
        unsigned i;
        if (llvm::StringRef(DigitStart, DigitEnd - DigitStart + 1)
                .getAsInteger(10, i))
          return false;

        if (i >= OutputConstraints.size())
          return false;

        // A number must refer to an output only operand.
        if (OutputConstraints[i].isReadWrite())
          return false;

        // If the constraint is already tied, it must be tied to the
        // same operand referenced to by the number.
        if (Info.hasTiedOperand() && Info.getTiedOperand() != i)
          return false;

        // The constraint should have the same info as the respective
        // output constraint.
        Info.setTiedOperand(i, OutputConstraints[i]);
      } else if (!validateAsmConstraint(Name, Info)) {
        return false;
      }
      break;
    case '[': {
      unsigned Index = 0;
      if (!resolveSymbolicName(Name, OutputConstraints, Index))
        return false;

      // If the constraint is already tied, it must be tied to the
      // same operand referenced to by the number.
      if (Info.hasTiedOperand() && Info.getTiedOperand() != Index)
        return false;

      // A number must refer to an output only operand.
      if (OutputConstraints[Index].isReadWrite())
        return false;

      Info.setTiedOperand(Index, OutputConstraints[Index]);
      break;
    }
    case '%': // commutative
      break;
    case 'i': // immediate integer.
      break;
    case 'n': // immediate integer with a known value.
      Info.setRequiresImmediate();
      break;
    case 'I': // Various constant constraints with target-specific meanings.
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
      if (!validateAsmConstraint(Name, Info))
        return false;
      break;
    case 'r': // general register.
      Info.setAllowsRegister();
      break;
    case 'm': // memory operand.
    case 'o': // offsettable memory operand.
    case 'V': // non-offsettable memory operand.
    case '<': // autodecrement memory operand.
    case '>': // autoincrement memory operand.
      Info.setAllowsMemory();
      break;
    case 'g': // general register, memory operand or immediate integer.
    case 'X': // any operand.
      Info.setAllowsRegister();
      Info.setAllowsMemory();
      break;
    case 'E': // immediate floating point.
    case 'F': // immediate floating point.
    case 'p': // address operand.
      break;
    case ',': // multiple alternative constraint.  Ignore comma.
      break;
    case '#': // Ignore as constraint.
      while (Name[1] && Name[1] != ',')
        Name++;
      break;
    case '?': // Disparage slightly code.
    case '!': // Disparage severely.
    case '*': // Ignore for choosing register preferences.
      break;  // Pass them.
    }

    Name++;
  }

  return true;
}

void TargetInfo::CheckFixedPointBits() const {
  assert(ShortAccumScale + getShortAccumIBits() + 1 <= ShortAccumWidth);
  assert(AccumScale + getAccumIBits() + 1 <= AccumWidth);
  assert(LongAccumScale + getLongAccumIBits() + 1 <= LongAccumWidth);
  assert(getUnsignedShortAccumScale() + getUnsignedShortAccumIBits() <=
         ShortAccumWidth);
  assert(getUnsignedAccumScale() + getUnsignedAccumIBits() <= AccumWidth);
  assert(getUnsignedLongAccumScale() + getUnsignedLongAccumIBits() <=
         LongAccumWidth);

  assert(getShortFractScale() + 1 <= ShortFractWidth);
  assert(getFractScale() + 1 <= FractWidth);
  assert(getLongFractScale() + 1 <= LongFractWidth);
  assert(getUnsignedShortFractScale() <= ShortFractWidth);
  assert(getUnsignedFractScale() <= FractWidth);
  assert(getUnsignedLongFractScale() <= LongFractWidth);

  // Each unsigned fract type has either the same number of fractional bits
  // as, or one more fractional bit than, its corresponding signed fract type.
  assert(getShortFractScale() == getUnsignedShortFractScale() ||
         getShortFractScale() == getUnsignedShortFractScale() - 1);
  assert(getFractScale() == getUnsignedFractScale() ||
         getFractScale() == getUnsignedFractScale() - 1);
  assert(getLongFractScale() == getUnsignedLongFractScale() ||
         getLongFractScale() == getUnsignedLongFractScale() - 1);

  // When arranged in order of increasing rank (see 6.3.1.3a), the number of
  // fractional bits is nondecreasing for each of the following sets of
  // fixed-point types:
  // - signed fract types
  // - unsigned fract types
  // - signed accum types
  // - unsigned accum types.
  assert(getLongFractScale() >= getFractScale() &&
         getFractScale() >= getShortFractScale());
  assert(getUnsignedLongFractScale() >= getUnsignedFractScale() &&
         getUnsignedFractScale() >= getUnsignedShortFractScale());
  assert(LongAccumScale >= AccumScale && AccumScale >= ShortAccumScale);
  assert(getUnsignedLongAccumScale() >= getUnsignedAccumScale() &&
         getUnsignedAccumScale() >= getUnsignedShortAccumScale());

  // When arranged in order of increasing rank (see 6.3.1.3a), the number of
  // integral bits is nondecreasing for each of the following sets of
  // fixed-point types:
  // - signed accum types
  // - unsigned accum types
  assert(getLongAccumIBits() >= getAccumIBits() &&
         getAccumIBits() >= getShortAccumIBits());
  assert(getUnsignedLongAccumIBits() >= getUnsignedAccumIBits() &&
         getUnsignedAccumIBits() >= getUnsignedShortAccumIBits());

  // Each signed accum type has at least as many integral bits as its
  // corresponding unsigned accum type.
  assert(getShortAccumIBits() >= getUnsignedShortAccumIBits());
  assert(getAccumIBits() >= getUnsignedAccumIBits());
  assert(getLongAccumIBits() >= getUnsignedLongAccumIBits());
}

std::optional<llvm::VersionTuple>
DarwinSDKInfo::RelatedTargetVersionMapping::map(
    const llvm::VersionTuple &Key, const llvm::VersionTuple &MinimumValue,
    std::optional<llvm::VersionTuple> MaximumValue) const {
  if (Key < MinimumKeyVersion)
    return MinimumValue;
  if (Key > MaximumKeyVersion)
    return MaximumValue;
  auto KV = Mapping.find(Key.normalize());
  if (KV != Mapping.end())
    return KV->getSecond();
  if (Key.hasMinor())
    return map(llvm::VersionTuple(Key.getMajor()), MinimumValue, MaximumValue);
  return std::nullopt;
}

std::optional<DarwinSDKInfo::RelatedTargetVersionMapping>
DarwinSDKInfo::RelatedTargetVersionMapping::parseJSON(
    const llvm::json::Object &Obj, llvm::VersionTuple MaximumDeploymentTarget) {
  llvm::VersionTuple Min =
      llvm::VersionTuple(std::numeric_limits<unsigned>::max());
  llvm::VersionTuple Max = llvm::VersionTuple(0);
  llvm::VersionTuple MinValue = Min;
  llvm::DenseMap<llvm::VersionTuple, llvm::VersionTuple> Mapping;
  for (const auto &KV : Obj) {
    auto Val = KV.getSecond().getAsString();
    if (Val.data()) {
      llvm::VersionTuple KeyVersion;
      llvm::VersionTuple ValueVersion;
      if (KeyVersion.tryParse(KV.getFirst()) || ValueVersion.tryParse(Val))
        return std::nullopt;
      Mapping[KeyVersion.normalize()] = ValueVersion;
      if (KeyVersion < Min)
        Min = KeyVersion;
      if (KeyVersion > Max)
        Max = KeyVersion;
      if (ValueVersion < MinValue)
        MinValue = ValueVersion;
    }
  }
  if (Mapping.empty())
    return std::nullopt;
  return RelatedTargetVersionMapping(
      Min, Max, MinValue, MaximumDeploymentTarget, std::move(Mapping));
}

namespace {
std::optional<llvm::VersionTuple> getVersionKey(const llvm::json::Object &Obj,
                                                llvm::StringRef Key) {
  auto Value = Obj.getString(Key);
  if (!Value.data())
    return std::nullopt;
  llvm::VersionTuple Version;
  if (Version.tryParse(Value))
    return std::nullopt;
  return Version;
}
} // namespace

std::optional<DarwinSDKInfo>
DarwinSDKInfo::parseDarwinSDKSettingsJSON(const llvm::json::Object *Obj) {
  auto Version = getVersionKey(*Obj, "Version");
  if (!Version)
    return std::nullopt;
  auto MaximumDeploymentVersion =
      getVersionKey(*Obj, "MaximumDeploymentTarget");
  if (!MaximumDeploymentVersion)
    return std::nullopt;
  llvm::DenseMap<OSEnvPair::StorageType,
                 std::optional<RelatedTargetVersionMapping>>
      VersionMappings;
  if (const auto *VM = Obj->getObject("VersionMap")) {
    for (const auto &KV : *VM) {
      auto Pair = llvm::StringRef(KV.getFirst()).split("_");
      if (Pair.first.compare_insensitive("ios") == 0) {
        llvm::Triple TT(llvm::Twine("--") + Pair.second.lower());
        if (TT.getOS() != llvm::Triple::UnknownOS) {
          auto Mapping = RelatedTargetVersionMapping::parseJSON(
              *KV.getSecond().getAsObject(), *MaximumDeploymentVersion);
          if (Mapping)
            VersionMappings[OSEnvPair(llvm::Triple::IOS,
                                      llvm::Triple::UnknownEnvironment,
                                      TT.getOS(),
                                      llvm::Triple::UnknownEnvironment)
                                .Value] = std::move(Mapping);
        }
      }
    }
  }

  return DarwinSDKInfo(std::move(*Version),
                       std::move(*MaximumDeploymentVersion),
                       std::move(VersionMappings));
}

llvm::Expected<std::optional<DarwinSDKInfo>>
neverc::parseDarwinSDKInfo(llvm::vfs::FileSystem &VFS,
                           llvm::StringRef SDKRootPath) {
  llvm::SmallString<256> Filepath = SDKRootPath;
  llvm::sys::path::append(Filepath, "SDKSettings.json");
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      VFS.getBufferForFile(Filepath);
  if (!File) {
    return std::nullopt;
  }
  llvm::Expected<llvm::json::Value> Result =
      llvm::json::parse(File.get()->getBuffer());
  if (!Result)
    return Result.takeError();

  if (const auto *Obj = Result->getAsObject()) {
    if (auto SDKInfo = DarwinSDKInfo::parseDarwinSDKSettingsJSON(Obj))
      return std::move(SDKInfo);
  }
  return llvm::make_error<llvm::StringError>("invalid SDKSettings.json",
                                             llvm::inconvertibleErrorCode());
}

namespace neverc {
namespace targets {

void DefineStd(MacroBuilder &Builder, llvm::StringRef MacroName,
               const LangOptions &Opts) {
  assert(MacroName[0] != '_' && "Identifier should be in the user's namespace");

  if (Opts.GNUMode)
    Builder.defineMacro(MacroName);

  Builder.defineMacro("__" + MacroName);

  Builder.defineMacro("__" + MacroName + "__");
}

void defineCPUMacros(MacroBuilder &Builder, llvm::StringRef CPUName,
                     bool Tuning) {
  Builder.defineMacro("__" + CPUName);
  Builder.defineMacro("__" + CPUName + "__");
  if (Tuning)
    Builder.defineMacro("__tune_" + CPUName + "__");
}

std::unique_ptr<TargetInfo> AllocateTarget(const llvm::Triple &Triple,
                                           const TargetOptions &Opts) {
  llvm::Triple::OSType os = Triple.getOS();

  switch (Triple.getArch()) {
  default:
    return nullptr;

  case llvm::Triple::aarch64:
    if (Triple.isOSDarwin())
      return std::make_unique<DarwinAArch64TargetInfo>(Triple, Opts);
    if (os == llvm::Triple::Linux)
      return std::make_unique<LinuxTargetInfo<AArch64leTargetInfo>>(Triple,
                                                                    Opts);
    if (os == llvm::Triple::Win32) {
      if (Triple.getEnvironment() == llvm::Triple::MSVC)
        return std::make_unique<WindowsTargetInfo<AArch64leTargetInfo>>(Triple,
                                                                        Opts);
      return nullptr;
    }
    return nullptr;

  case llvm::Triple::x86_64:
    if (os == llvm::Triple::Win32) {
      if (Triple.getEnvironment() == llvm::Triple::MSVC)
        return std::make_unique<MicrosoftX86_64TargetInfo>(Triple, Opts);
      return nullptr;
    }
    if (Triple.isOSDarwin())
      return std::make_unique<DarwinX86_64TargetInfo>(Triple, Opts);
    if (os == llvm::Triple::Linux)
      return std::make_unique<X86_64TargetInfo>(Triple, Opts);
    return nullptr;
  }
}
} // namespace targets
} // namespace neverc

using namespace neverc::targets;

TargetInfo *
TargetInfo::CreateTargetInfo(DiagnosticsEngine &Diags,
                             const std::shared_ptr<TargetOptions> &Opts) {
  llvm::Triple Triple(Opts->Triple);

  std::unique_ptr<TargetInfo> Target = AllocateTarget(Triple, *Opts);
  if (!Target) {
    Diags.Report(diag::err_target_unknown_triple) << Triple.str();
    return nullptr;
  }
  Target->TargetOpts = Opts;

  if (!Opts->CPU.empty() && !Target->setCPU(Opts->CPU)) {
    Diags.Report(diag::err_target_unknown_cpu) << Opts->CPU;
    llvm::SmallVector<llvm::StringRef, 32> ValidList;
    Target->fillValidCPUList(ValidList);
    if (!ValidList.empty())
      Diags.Report(diag::note_valid_options) << llvm::join(ValidList, ", ");
    return nullptr;
  }

  if (!Opts->TuneCPU.empty() && !Target->isValidTuneCPUName(Opts->TuneCPU)) {
    Diags.Report(diag::err_target_unknown_cpu) << Opts->TuneCPU;
    llvm::SmallVector<llvm::StringRef, 32> ValidList;
    Target->fillValidTuneCPUList(ValidList);
    if (!ValidList.empty())
      Diags.Report(diag::note_valid_options) << llvm::join(ValidList, ", ");
    return nullptr;
  }

  if (!Opts->ABI.empty() && !Target->setABI(Opts->ABI)) {
    Diags.Report(diag::err_target_unknown_abi) << Opts->ABI;
    return nullptr;
  }

  if (!Opts->FPMath.empty() && !Target->setFPMath(Opts->FPMath)) {
    Diags.Report(diag::err_target_unknown_fpmath) << Opts->FPMath;
    return nullptr;
  }

  llvm::erase_if(Opts->FeaturesAsWritten, [&](llvm::StringRef Name) {
    if (Target->isReadOnlyFeature(Name.substr(1))) {
      Diags.Report(diag::warn_fe_backend_readonly_feature_flag) << Name;
      return true;
    }
    return false;
  });
  if (!Target->initFeatureMap(Opts->FeatureMap, Diags, Opts->CPU,
                              Opts->FeaturesAsWritten))
    return nullptr;

  Opts->Features.clear();
  for (const auto &F : Opts->FeatureMap)
    Opts->Features.push_back((F.getValue() ? "+" : "-") + F.getKey().str());
  llvm::sort(Opts->Features);

  if (!Target->handleTargetFeatures(Opts->Features, Diags))
    return nullptr;

  Target->setMaxAtomicWidth();

  if (!Target->validateTarget(Diags))
    return nullptr;

  Target->CheckFixedPointBits();

  return Target.release();
}
