#ifndef NEVERC_FOUNDATION_TARGETINFO_H
#define NEVERC_FOUNDATION_TARGETINFO_H

#include "neverc/Foundation/Core/AddressSpaces.h"
#include "neverc/Foundation/Core/BitmaskEnum.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
struct fltSemantics;
}

namespace neverc {
class DiagnosticsEngine;
class LangOptions;
class CodeGenOptions;
class MacroBuilder;

struct ParsedTargetAttr {
  std::vector<std::string> Features;
  llvm::StringRef CPU;
  llvm::StringRef Tune;
  llvm::StringRef BranchProtection;
  llvm::StringRef Duplicate;
  bool operator==(const ParsedTargetAttr &Other) const {
    return Duplicate == Other.Duplicate && CPU == Other.CPU &&
           Tune == Other.Tune && BranchProtection == Other.BranchProtection &&
           Features == Other.Features;
  }
};

namespace Builtin {
struct Info;
}

enum class FloatModeKind {
  NoFloat = 0,
  Half = 1 << 0,
  Float = 1 << 1,
  Double = 1 << 2,
  LongDouble = 1 << 3,
  Float128 = 1 << 4,
  LLVM_MARK_AS_BITMASK_ENUM(Float128)
};

struct TransferrableTargetInfo {
  unsigned char PointerWidth, PointerAlign;
  unsigned char BoolWidth, BoolAlign;
  unsigned char IntWidth, IntAlign;
  unsigned char HalfWidth, HalfAlign;
  unsigned char BFloat16Width, BFloat16Align;
  unsigned char FloatWidth, FloatAlign;
  unsigned char DoubleWidth, DoubleAlign;
  unsigned char LongDoubleWidth, LongDoubleAlign, Float128Align;
  unsigned char LargeArrayMinWidth, LargeArrayAlign;
  unsigned char LongWidth, LongAlign;
  unsigned char LongLongWidth, LongLongAlign;
  unsigned char Int128Align;

  // Fixed point bit widths
  unsigned char ShortAccumWidth, ShortAccumAlign;
  unsigned char AccumWidth, AccumAlign;
  unsigned char LongAccumWidth, LongAccumAlign;
  unsigned char ShortFractWidth, ShortFractAlign;
  unsigned char FractWidth, FractAlign;
  unsigned char LongFractWidth, LongFractAlign;

  // If true, unsigned fixed point types have the same number of fractional bits
  // as their signed counterparts, forcing the unsigned types to have one extra
  // bit of padding. Otherwise, unsigned fixed point types have
  // one more fractional bit than its corresponding signed type. This is false
  // by default.
  bool PaddingOnUnsignedFixedPoint;

  // Fixed point integral and fractional bit sizes
  // Saturated types share the same integral/fractional bits as their
  // corresponding unsaturated types.
  // For simplicity, the fractional bits in a _Fract type will be one less the
  // width of that _Fract type. This leaves all signed _Fract types having no
  // padding and unsigned _Fract types will only have 1 bit of padding after the
  // sign if PaddingOnUnsignedFixedPoint is set.
  unsigned char ShortAccumScale;
  unsigned char AccumScale;
  unsigned char LongAccumScale;

  unsigned char DefaultAlignForAttributeAligned;
  unsigned char MinGlobalAlign;

  unsigned short SuitableAlign;
  unsigned short NewAlign;
  unsigned MaxVectorAlign;
  unsigned MaxTLSAlign;

  const llvm::fltSemantics *HalfFormat, *BFloat16Format, *FloatFormat,
      *DoubleFormat, *LongDoubleFormat, *Float128Format;

  ///===---- Target Data Type Query Methods -------------------------------===//
  enum IntType {
    NoInt = 0,
    SignedChar,
    UnsignedChar,
    SignedShort,
    UnsignedShort,
    SignedInt,
    UnsignedInt,
    SignedLong,
    UnsignedLong,
    SignedLongLong,
    UnsignedLongLong
  };

protected:
  IntType SizeType, IntMaxType, PtrDiffType, IntPtrType, WCharType, WIntType,
      Char16Type, Char32Type, Int64Type, Int16Type, SigAtomicType,
      ProcessIDType;

  LLVM_PREFERRED_TYPE(bool)
  unsigned UseBitFieldTypeAlignment : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned UseZeroLengthBitfieldAlignment : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned UseLeadingZeroLengthBitfield : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned UseExplicitBitFieldAlignment : 1;

  unsigned ZeroLengthBitfieldBoundary;

  unsigned MaxAlignedAttribute;
};

class TargetInfo : public TransferrableTargetInfo,
                   public llvm::RefCountedBase<TargetInfo> {
  std::shared_ptr<TargetOptions> TargetOpts;
  llvm::Triple Triple;

protected:
  // Target values set by the ctor of the actual target implementation.  Default
  // values are specified by the TargetInfo constructor.
  bool TLSSupported;
  bool VLASupported;
  bool NoAsmVariants;    // True if {|} are normal characters.
  bool HasLegalHalfType; // True if the backend supports operations on the half
                         // LLVM IR type.
  bool HalfArgsAndReturns;
  bool HasFloat128;
  bool HasFloat16;
  bool HasBFloat16;
  bool HasFullBFloat16; // True if the backend supports native bfloat16
                        // arithmetic. Used to determine excess precision
                        // support in the frontend.
  bool HasLongDouble;
  bool HasFPReturn;
  bool HasStrictFP;

  unsigned char MaxAtomicPromoteWidth, MaxAtomicInlineWidth;
  std::string DataLayoutString;
  const char *UserLabelPrefix;
  unsigned char RegParmMax, SSERegParmMax;
  const LangASMap *AddrSpaceMap;

  mutable llvm::StringRef PlatformName;
  mutable llvm::VersionTuple PlatformMinVersion;

  LLVM_PREFERRED_TYPE(bool)
  unsigned ComplexLongDoubleUsesFP2Ret : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned HasBuiltinMSVaList : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned HasAArch64SVETypes : 1;

  std::optional<unsigned> MaxBitIntWidth;

  // TargetInfo Constructor.  Default initializes all fields.
  TargetInfo(const llvm::Triple &T);

  // UserLabelPrefix must match DL's getGlobalPrefix() when interpreted
  // as a DataLayout object.
  void resetDataLayout(llvm::StringRef DL, const char *UserLabelPrefix = "");

  // Target features that are read-only and should not be disabled/enabled
  // by command line options. Such features are for emitting predefined
  // macros or checking availability of builtin functions and can be omitted
  // in function attributes in IR.
  llvm::StringSet<> ReadOnlyFeatures;

public:
  static TargetInfo *
  CreateTargetInfo(DiagnosticsEngine &Diags,
                   const std::shared_ptr<TargetOptions> &Opts);

  virtual ~TargetInfo();

  TargetOptions &getTargetOpts() const {
    assert(TargetOpts && "Missing target options");
    return *TargetOpts;
  }

  enum BuiltinVaListKind {
    /// typedef char* __builtin_va_list;
    CharPtrBuiltinVaList = 0,

    /// typedef void* __builtin_va_list;
    VoidPtrBuiltinVaList,

    /// __builtin_va_list as defined by the AArch64 ABI
    /// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0055a/IHI0055A_aapcs64.pdf
    AArch64ABIBuiltinVaList,

    /// __builtin_va_list as defined by the x86-64 ABI:
    /// http://refspecs.linuxbase.org/elf/x86_64-abi-0.21.pdf
    X86_64ABIBuiltinVaList
  };

protected:
  bool UseAddrSpaceMapMangling;

public:
  IntType getSizeType() const { return SizeType; }
  IntType getSignedSizeType() const {
    switch (SizeType) {
    case UnsignedShort:
      return SignedShort;
    case UnsignedInt:
      return SignedInt;
    case UnsignedLong:
      return SignedLong;
    case UnsignedLongLong:
      return SignedLongLong;
    default:
      llvm_unreachable("Invalid SizeType");
    }
  }
  IntType getIntMaxType() const { return IntMaxType; }
  IntType getUIntMaxType() const {
    return getCorrespondingUnsignedType(IntMaxType);
  }
  IntType getPtrDiffType(LangAS AddrSpace) const {
    return AddrSpace == LangAS::Default ? PtrDiffType
                                        : getPtrDiffTypeV(AddrSpace);
  }
  IntType getUnsignedPtrDiffType(LangAS AddrSpace) const {
    return getCorrespondingUnsignedType(getPtrDiffType(AddrSpace));
  }
  IntType getIntPtrType() const { return IntPtrType; }
  IntType getUIntPtrType() const {
    return getCorrespondingUnsignedType(IntPtrType);
  }
  IntType getWCharType() const { return WCharType; }
  IntType getWIntType() const { return WIntType; }
  IntType getChar16Type() const { return Char16Type; }
  IntType getChar32Type() const { return Char32Type; }
  IntType getInt64Type() const { return Int64Type; }
  IntType getUInt64Type() const {
    return getCorrespondingUnsignedType(Int64Type);
  }
  IntType getInt16Type() const { return Int16Type; }
  IntType getUInt16Type() const {
    return getCorrespondingUnsignedType(Int16Type);
  }
  IntType getSigAtomicType() const { return SigAtomicType; }
  IntType getProcessIDType() const { return ProcessIDType; }

  static IntType getCorrespondingUnsignedType(IntType T) {
    switch (T) {
    case SignedChar:
      return UnsignedChar;
    case SignedShort:
      return UnsignedShort;
    case SignedInt:
      return UnsignedInt;
    case SignedLong:
      return UnsignedLong;
    case SignedLongLong:
      return UnsignedLongLong;
    default:
      llvm_unreachable("Unexpected signed integer type");
    }
  }

  bool doUnsignedFixedPointTypesHavePadding() const {
    return PaddingOnUnsignedFixedPoint;
  }

  unsigned getTypeWidth(IntType T) const;

  virtual IntType getIntTypeByWidth(unsigned BitWidth, bool IsSigned) const;

  virtual IntType getLeastIntTypeByWidth(unsigned BitWidth,
                                         bool IsSigned) const;

  FloatModeKind getRealTypeByWidth(unsigned BitWidth,
                                   FloatModeKind ExplicitType) const;

  unsigned getTypeAlign(IntType T) const;

  static bool isTypeSigned(IntType T);

  uint64_t getPointerWidth(LangAS AddrSpace) const {
    return AddrSpace == LangAS::Default ? PointerWidth
                                        : getPointerWidthV(AddrSpace);
  }
  uint64_t getPointerAlign(LangAS AddrSpace) const {
    return AddrSpace == LangAS::Default ? PointerAlign
                                        : getPointerAlignV(AddrSpace);
  }

  virtual uint64_t getMaxPointerWidth() const { return PointerWidth; }

  virtual uint64_t getNullPointerValue(LangAS AddrSpace) const { return 0; }

  unsigned getBoolWidth() const { return BoolWidth; }

  unsigned getBoolAlign() const { return BoolAlign; }

  unsigned getCharWidth() const { return 8; }
  unsigned getCharAlign() const { return 8; }

  unsigned getShortWidth() const { return 16; }

  unsigned getShortAlign() const { return 16; }

  unsigned getIntWidth() const { return IntWidth; }
  unsigned getIntAlign() const { return IntAlign; }

  unsigned getLongWidth() const { return LongWidth; }
  unsigned getLongAlign() const { return LongAlign; }

  unsigned getLongLongWidth() const { return LongLongWidth; }
  unsigned getLongLongAlign() const { return LongLongAlign; }

  unsigned getInt128Align() const { return Int128Align; }

  unsigned getShortAccumWidth() const { return ShortAccumWidth; }
  unsigned getShortAccumAlign() const { return ShortAccumAlign; }

  unsigned getAccumWidth() const { return AccumWidth; }
  unsigned getAccumAlign() const { return AccumAlign; }

  unsigned getLongAccumWidth() const { return LongAccumWidth; }
  unsigned getLongAccumAlign() const { return LongAccumAlign; }

  unsigned getShortFractWidth() const { return ShortFractWidth; }
  unsigned getShortFractAlign() const { return ShortFractAlign; }

  unsigned getFractWidth() const { return FractWidth; }
  unsigned getFractAlign() const { return FractAlign; }

  unsigned getLongFractWidth() const { return LongFractWidth; }
  unsigned getLongFractAlign() const { return LongFractAlign; }

  unsigned getShortAccumScale() const { return ShortAccumScale; }
  unsigned getShortAccumIBits() const {
    return ShortAccumWidth - ShortAccumScale - 1;
  }

  unsigned getAccumScale() const { return AccumScale; }
  unsigned getAccumIBits() const { return AccumWidth - AccumScale - 1; }

  unsigned getLongAccumScale() const { return LongAccumScale; }
  unsigned getLongAccumIBits() const {
    return LongAccumWidth - LongAccumScale - 1;
  }

  unsigned getUnsignedShortAccumScale() const {
    return PaddingOnUnsignedFixedPoint ? ShortAccumScale : ShortAccumScale + 1;
  }
  unsigned getUnsignedShortAccumIBits() const {
    return PaddingOnUnsignedFixedPoint
               ? getShortAccumIBits()
               : ShortAccumWidth - getUnsignedShortAccumScale();
  }

  unsigned getUnsignedAccumScale() const {
    return PaddingOnUnsignedFixedPoint ? AccumScale : AccumScale + 1;
  }
  unsigned getUnsignedAccumIBits() const {
    return PaddingOnUnsignedFixedPoint ? getAccumIBits()
                                       : AccumWidth - getUnsignedAccumScale();
  }

  unsigned getUnsignedLongAccumScale() const {
    return PaddingOnUnsignedFixedPoint ? LongAccumScale : LongAccumScale + 1;
  }
  unsigned getUnsignedLongAccumIBits() const {
    return PaddingOnUnsignedFixedPoint
               ? getLongAccumIBits()
               : LongAccumWidth - getUnsignedLongAccumScale();
  }

  unsigned getShortFractScale() const { return ShortFractWidth - 1; }

  unsigned getFractScale() const { return FractWidth - 1; }

  unsigned getLongFractScale() const { return LongFractWidth - 1; }

  unsigned getUnsignedShortFractScale() const {
    return PaddingOnUnsignedFixedPoint ? getShortFractScale()
                                       : getShortFractScale() + 1;
  }

  unsigned getUnsignedFractScale() const {
    return PaddingOnUnsignedFixedPoint ? getFractScale() : getFractScale() + 1;
  }

  unsigned getUnsignedLongFractScale() const {
    return PaddingOnUnsignedFixedPoint ? getLongFractScale()
                                       : getLongFractScale() + 1;
  }

  virtual bool hasInt128Type() const {
    return (getPointerWidth(LangAS::Default) >= 64) ||
           getTargetOpts().ForceEnableInt128;
  }

  virtual bool hasBitIntType() const { return false; }

  // Different targets may support a different maximum width for the _BitInt
  // type, depending on what operations are supported.
  virtual size_t getMaxBitIntWidth() const {
    // Consider -fexperimental-max-bitint-width= first.
    if (MaxBitIntWidth)
      return std::min<size_t>(*MaxBitIntWidth, llvm::IntegerType::MAX_INT_BITS);

    return 128;
  }

  virtual bool hasLegalHalfType() const { return HasLegalHalfType; }

  virtual bool allowHalfArgsAndReturns() const { return HalfArgsAndReturns; }

  virtual bool hasFloat128Type() const { return HasFloat128; }

  virtual bool hasFloat16Type() const { return HasFloat16; }

  virtual bool hasBFloat16Type() const {
    return HasBFloat16 || HasFullBFloat16;
  }

  virtual bool hasFullBFloat16Type() const { return HasFullBFloat16; }

  virtual bool hasLongDoubleType() const { return HasLongDouble; }

  virtual bool hasFPReturn() const { return HasFPReturn; }

  virtual bool hasStrictFP() const { return HasStrictFP; }

  unsigned getSuitableAlign() const { return SuitableAlign; }

  unsigned getDefaultAlignForAttributeAligned() const {
    return DefaultAlignForAttributeAligned;
  }

  virtual unsigned getMinGlobalAlign(uint64_t) const { return MinGlobalAlign; }

  unsigned getNewAlign() const {
    return NewAlign ? NewAlign : std::max(LongDoubleAlign, LongLongAlign);
  }

  unsigned getWCharWidth() const { return getTypeWidth(WCharType); }
  unsigned getWCharAlign() const { return getTypeAlign(WCharType); }

  unsigned getChar16Width() const { return getTypeWidth(Char16Type); }
  unsigned getChar16Align() const { return getTypeAlign(Char16Type); }

  unsigned getChar32Width() const { return getTypeWidth(Char32Type); }
  unsigned getChar32Align() const { return getTypeAlign(Char32Type); }

  unsigned getHalfWidth() const { return HalfWidth; }
  unsigned getHalfAlign() const { return HalfAlign; }
  const llvm::fltSemantics &getHalfFormat() const { return *HalfFormat; }

  unsigned getFloatWidth() const { return FloatWidth; }
  unsigned getFloatAlign() const { return FloatAlign; }
  const llvm::fltSemantics &getFloatFormat() const { return *FloatFormat; }

  unsigned getBFloat16Width() const { return BFloat16Width; }
  unsigned getBFloat16Align() const { return BFloat16Align; }
  const llvm::fltSemantics &getBFloat16Format() const {
    return *BFloat16Format;
  }

  unsigned getDoubleWidth() const { return DoubleWidth; }
  unsigned getDoubleAlign() const { return DoubleAlign; }
  const llvm::fltSemantics &getDoubleFormat() const { return *DoubleFormat; }

  unsigned getLongDoubleWidth() const { return LongDoubleWidth; }
  unsigned getLongDoubleAlign() const { return LongDoubleAlign; }
  const llvm::fltSemantics &getLongDoubleFormat() const {
    return *LongDoubleFormat;
  }

  unsigned getFloat128Width() const { return 128; }
  unsigned getFloat128Align() const { return Float128Align; }
  const llvm::fltSemantics &getFloat128Format() const {
    return *Float128Format;
  }

  virtual const char *getLongDoubleMangling() const { return "e"; }

  virtual const char *getFloat128Mangling() const { return "g"; }

  virtual const char *getBFloat16Mangling() const { return "DF16b"; }

  virtual LangOptions::FPEvalMethodKind getFPEvalMethod() const {
    return LangOptions::FPEvalMethodKind::FEM_Source;
  }

  virtual bool supportSourceEvalMethod() const { return true; }

  // getLargeArrayMinWidth/Align - Return the minimum array size that is
  // 'large' and its alignment.
  unsigned getLargeArrayMinWidth() const { return LargeArrayMinWidth; }
  unsigned getLargeArrayAlign() const { return LargeArrayAlign; }

  unsigned getMaxAtomicPromoteWidth() const { return MaxAtomicPromoteWidth; }
  unsigned getMaxAtomicInlineWidth() const { return MaxAtomicInlineWidth; }
  virtual void setMaxAtomicWidth() {}
  virtual bool hasBuiltinAtomic(uint64_t AtomicSizeInBits,
                                uint64_t AlignmentInBits) const {
    return AtomicSizeInBits <= AlignmentInBits &&
           AtomicSizeInBits <= getMaxAtomicInlineWidth() &&
           (AtomicSizeInBits <= getCharWidth() ||
            llvm::isPowerOf2_64(AtomicSizeInBits / getCharWidth()));
  }

  unsigned getMaxVectorAlign() const { return MaxVectorAlign; }

  unsigned getIntMaxTWidth() const { return getTypeWidth(IntMaxType); }

  // Return the size of unwind_word for this target.
  virtual unsigned getUnwindWordWidth() const {
    return getPointerWidth(LangAS::Default);
  }

  virtual unsigned getRegisterWidth() const {
    // Currently we assume the register width on the target matches the pointer
    // width, we can introduce a new variable for this if/when some target wants
    // it.
    return PointerWidth;
  }

  const char *getUserLabelPrefix() const { return UserLabelPrefix; }

  bool useBitFieldTypeAlignment() const { return UseBitFieldTypeAlignment; }

  bool useZeroLengthBitfieldAlignment() const {
    return UseZeroLengthBitfieldAlignment;
  }

  bool useLeadingZeroLengthBitfield() const {
    return UseLeadingZeroLengthBitfield;
  }

  unsigned getZeroLengthBitfieldBoundary() const {
    return ZeroLengthBitfieldBoundary;
  }

  unsigned getMaxAlignedAttribute() const { return MaxAlignedAttribute; }

  //  honored, as in "__attribute__((aligned(2))) int b : 1;".
  bool useExplicitBitFieldAlignment() const {
    return UseExplicitBitFieldAlignment;
  }

  static const char *getTypeName(IntType T);

  const char *getTypeConstantSuffix(IntType T) const;

  static const char *getTypeFormatModifier(IntType T);

  virtual bool useFP16ConversionIntrinsics() const { return true; }

  bool useAddressSpaceMapMangling() const { return UseAddrSpaceMapMangling; }

  ///===---- Other target property query methods --------------------------===//

  virtual void getTargetDefines(const LangOptions &Opts,
                                MacroBuilder &Builder) const = 0;

  virtual llvm::ArrayRef<Builtin::Info> getTargetBuiltins() const = 0;

  virtual std::optional<std::pair<unsigned, unsigned>>
  getVScaleRange(const LangOptions &LangOpts) const {
    return std::nullopt;
  }
  virtual bool isCLZForZeroUndef() const { return true; }

  virtual BuiltinVaListKind getBuiltinVaListKind() const = 0;

  bool hasBuiltinMSVaList() const { return HasBuiltinMSVaList; }

  bool hasAArch64SVETypes() const { return HasAArch64SVETypes; }

  bool isValidClobber(llvm::StringRef Name) const;

  virtual bool isValidGCCRegisterName(llvm::StringRef Name) const;

  llvm::StringRef
  getNormalizedGCCRegisterName(llvm::StringRef Name,
                               bool ReturnCanonical = false) const;

  virtual bool isSPRegName(llvm::StringRef) const { return false; }

  virtual llvm::StringRef
  getConstraintRegister(llvm::StringRef Constraint,
                        llvm::StringRef Expression) const {
    return "";
  }

  struct ConstraintInfo {
    enum {
      CI_None = 0x00,
      CI_AllowsMemory = 0x01,
      CI_AllowsRegister = 0x02,
      CI_ReadWrite = 0x04,         // "+r" output constraint (read and write).
      CI_HasMatchingInput = 0x08,  // This output operand has a matching input.
      CI_ImmediateConstant = 0x10, // This operand must be an immediate constant
      CI_EarlyClobber = 0x20,      // "&" output constraint (early clobber).
    };
    unsigned Flags;
    int TiedOperand;
    struct {
      int Min;
      int Max;
      bool isConstrained;
    } ImmRange;
    llvm::SmallSet<int, 4> ImmSet;

    std::string ConstraintStr; // constraint: "=rm"
    std::string Name;          // Operand name: [foo] with no []'s.
  public:
    ConstraintInfo(llvm::StringRef ConstraintStr, llvm::StringRef Name)
        : Flags(0), TiedOperand(-1), ConstraintStr(ConstraintStr.str()),
          Name(Name.str()) {
      ImmRange.Min = ImmRange.Max = 0;
      ImmRange.isConstrained = false;
    }

    const std::string &getConstraintStr() const { return ConstraintStr; }
    const std::string &getName() const { return Name; }
    bool isReadWrite() const { return (Flags & CI_ReadWrite) != 0; }
    bool earlyClobber() { return (Flags & CI_EarlyClobber) != 0; }
    bool allowsRegister() const { return (Flags & CI_AllowsRegister) != 0; }
    bool allowsMemory() const { return (Flags & CI_AllowsMemory) != 0; }

    /// Return true if this output operand has a matching
    /// (tied) input operand.
    bool hasMatchingInput() const { return (Flags & CI_HasMatchingInput) != 0; }

    /// Return true if this input operand is a matching
    /// constraint that ties it to an output operand.
    ///
    /// If this returns true then getTiedOperand will indicate which output
    /// operand this is tied to.
    bool hasTiedOperand() const { return TiedOperand != -1; }
    unsigned getTiedOperand() const {
      assert(hasTiedOperand() && "Has no tied operand!");
      return (unsigned)TiedOperand;
    }

    bool requiresImmediateConstant() const {
      return (Flags & CI_ImmediateConstant) != 0;
    }
    bool isValidAsmImmediate(const llvm::APInt &Value) const {
      if (!ImmSet.empty())
        return Value.isSignedIntN(32) && ImmSet.contains(Value.getZExtValue());
      return !ImmRange.isConstrained ||
             (Value.sge(ImmRange.Min) && Value.sle(ImmRange.Max));
    }

    void setIsReadWrite() { Flags |= CI_ReadWrite; }
    void setEarlyClobber() { Flags |= CI_EarlyClobber; }
    void setAllowsMemory() { Flags |= CI_AllowsMemory; }
    void setAllowsRegister() { Flags |= CI_AllowsRegister; }
    void setHasMatchingInput() { Flags |= CI_HasMatchingInput; }
    void setRequiresImmediate(int Min, int Max) {
      Flags |= CI_ImmediateConstant;
      ImmRange.Min = Min;
      ImmRange.Max = Max;
      ImmRange.isConstrained = true;
    }
    void setRequiresImmediate(llvm::ArrayRef<int> Exacts) {
      Flags |= CI_ImmediateConstant;
      for (int Exact : Exacts)
        ImmSet.insert(Exact);
    }
    void setRequiresImmediate(int Exact) {
      Flags |= CI_ImmediateConstant;
      ImmSet.insert(Exact);
    }
    void setRequiresImmediate() { Flags |= CI_ImmediateConstant; }

    /// Indicate that this is an input operand that is tied to
    /// the specified output operand.
    ///
    /// Copy over the various constraint information from the output.
    void setTiedOperand(unsigned N, ConstraintInfo &Output) {
      Output.setHasMatchingInput();
      Flags = Output.Flags;
      TiedOperand = N;
      // Don't copy Name or constraint string.
    }
  };

  virtual bool validateGlobalRegisterVariable(llvm::StringRef RegName,
                                              unsigned RegSize,
                                              bool &HasSizeMismatch) const {
    HasSizeMismatch = false;
    return true;
  }

  bool validateOutputConstraint(ConstraintInfo &Info) const;
  bool validateInputConstraint(
      llvm::MutableArrayRef<ConstraintInfo> OutputConstraints,
      ConstraintInfo &info) const;

  virtual bool validateOutputSize(const llvm::StringMap<bool> &FeatureMap,
                                  llvm::StringRef /*Constraint*/,
                                  unsigned /*Size*/) const {
    return true;
  }

  virtual bool validateInputSize(const llvm::StringMap<bool> &FeatureMap,
                                 llvm::StringRef /*Constraint*/,
                                 unsigned /*Size*/) const {
    return true;
  }
  virtual bool
  validateConstraintModifier(llvm::StringRef /*Constraint*/, char /*Modifier*/,
                             unsigned /*Size*/,
                             std::string & /*SuggestedModifier*/) const {
    return true;
  }
  virtual bool
  validateAsmConstraint(const char *&Name,
                        TargetInfo::ConstraintInfo &info) const = 0;

  bool resolveSymbolicName(const char *&Name,
                           llvm::ArrayRef<ConstraintInfo> OutputConstraints,
                           unsigned &Index) const;

  // Constraint parm will be left pointing at the last character of
  // the constraint.  In practice, it won't be changed unless the
  // constraint is longer than one character.
  virtual std::string convertConstraint(const char *&Constraint) const {
    // 'p' defaults to 'r', but can be overridden by targets.
    if (*Constraint == 'p')
      return std::string("r");
    return std::string(1, *Constraint);
  }

  virtual std::optional<std::string> handleAsmEscapedChar(char C) const {
    return std::nullopt;
  }

  virtual std::string_view getClobbers() const = 0;

  virtual bool isNan2008() const { return true; }

  const llvm::Triple &getTriple() const { return Triple; }

  const char *getDataLayoutString() const {
    assert(!DataLayoutString.empty() && "Uninitialized DataLayout!");
    return DataLayoutString.c_str();
  }

  struct GCCRegAlias {
    const char *const Aliases[5];
    const char *const Register;
  };

  struct AddlRegName {
    const char *const Names[5];
    const unsigned RegNum;
  };

  virtual bool hasProtectedVisibility() const { return true; }

  virtual bool shouldDLLImportComdatSymbols() const {
    return getTriple().isWindowsMSVCEnvironment();
  }

  virtual void adjust(DiagnosticsEngine &Diags, LangOptions &Opts);

  virtual bool initFeatureMap(llvm::StringMap<bool> &Features,
                              DiagnosticsEngine &Diags, llvm::StringRef CPU,
                              const std::vector<std::string> &FeatureVec) const;

  virtual llvm::StringRef getABI() const { return llvm::StringRef(); }

  virtual bool setCPU(const std::string &Name) { return false; }

  virtual void
  fillValidCPUList(llvm::SmallVectorImpl<llvm::StringRef> &Values) const {}

  virtual void
  fillValidTuneCPUList(llvm::SmallVectorImpl<llvm::StringRef> &Values) const {
    fillValidCPUList(Values);
  }

  virtual bool isValidCPUName(llvm::StringRef Name) const { return true; }

  virtual bool isValidTuneCPUName(llvm::StringRef Name) const {
    return isValidCPUName(Name);
  }

  virtual ParsedTargetAttr parseTargetAttr(llvm::StringRef Str) const;

  virtual bool supportsTargetAttributeTune() const { return false; }

  virtual bool setABI(const std::string &Name) { return false; }

  virtual bool setFPMath(llvm::StringRef Name) { return false; }

  virtual bool hasFeatureEnabled(const llvm::StringMap<bool> &Features,
                                 llvm::StringRef Name) const {
    return Features.lookup(Name);
  }

  virtual void setFeatureEnabled(llvm::StringMap<bool> &Features,
                                 llvm::StringRef Name, bool Enabled) const {
    Features[Name] = Enabled;
  }

  virtual bool isValidFeatureName(llvm::StringRef Feature) const {
    return true;
  }

  virtual bool doesFeatureAffectCodeGen(llvm::StringRef Feature) const {
    return true;
  }

  virtual llvm::StringRef
  getFeatureDependencies(llvm::StringRef Feature) const {
    return llvm::StringRef();
  }

  struct BranchProtectionInfo {
    LangOptions::SignReturnAddressScopeKind SignReturnAddr =
        LangOptions::SignReturnAddressScopeKind::None;
    LangOptions::SignReturnAddressKeyKind SignKey =
        LangOptions::SignReturnAddressKeyKind::AKey;
    bool BranchTargetEnforcement = false;
    bool BranchProtectionPAuthLR = false;
  };

  virtual bool isBranchProtectionSupportedArch(llvm::StringRef Arch) const {
    return false;
  }

  virtual bool validateBranchProtection(llvm::StringRef Spec,
                                        llvm::StringRef Arch,
                                        BranchProtectionInfo &BPI,
                                        llvm::StringRef &Err) const {
    Err = "";
    return false;
  }

  virtual bool handleTargetFeatures(std::vector<std::string> &Features,
                                    DiagnosticsEngine &Diags) {
    return true;
  }

  virtual bool hasFeature(llvm::StringRef Feature) const { return false; }

  bool isReadOnlyFeature(llvm::StringRef Feature) const {
    return ReadOnlyFeatures.contains(Feature);
  }

  bool supportsMultiVersioning() const {
    return getTriple().isX86() || getTriple().isAArch64();
  }

  bool supportsIFunc() const {
    if (getTriple().isOSBinFormatMachO())
      return true;
    return getTriple().isOSBinFormatELF() && getTriple().isOSLinux();
  }

  // Validate the contents of the __builtin_cpu_supports(const char*)
  // argument.
  virtual bool validateCpuSupports(llvm::StringRef Name) const { return false; }

  // Return the target-specific priority for features/cpus/vendors so
  // that they can be properly sorted for checking.
  virtual unsigned multiVersionSortPriority(llvm::StringRef Name) const {
    return 0;
  }

  // Return the target-specific cost for feature
  // that taken into account in priority sorting.
  virtual unsigned multiVersionFeatureCost() const { return 0; }

  // Validate the contents of the __builtin_cpu_is(const char*)
  // argument.
  virtual bool validateCpuIs(llvm::StringRef Name) const { return false; }

  // Validate a cpu_dispatch/cpu_specific CPU option, which is a different list
  // from cpu_is, since it checks via features rather than CPUs directly.
  virtual bool validateCPUSpecificCPUDispatch(llvm::StringRef Name) const {
    return false;
  }

  // Get the character to be added for mangling purposes for cpu_specific.
  virtual char CPUSpecificManglingCharacter(llvm::StringRef Name) const {
    llvm_unreachable(
        "cpu_specific Multiversioning not implemented on this target");
  }

  // Get the value for the 'tune-cpu' flag for a cpu_specific variant with the
  // programmer-specified 'Name'.
  virtual llvm::StringRef getCPUSpecificTuneName(llvm::StringRef Name) const {
    llvm_unreachable(
        "cpu_specific Multiversioning not implemented on this target");
  }

  // Get a list of the features that make up the CPU option for
  // cpu_specific/cpu_dispatch so that it can be passed to llvm as optimization
  // options.
  virtual void getCPUSpecificCPUDispatchFeatures(
      llvm::StringRef Name,
      llvm::SmallVectorImpl<llvm::StringRef> &Features) const {
    llvm_unreachable(
        "cpu_specific Multiversioning not implemented on this target");
  }

  // Returns maximal number of args passed in registers.
  unsigned getRegParmMax() const {
    assert(RegParmMax < 7 && "RegParmMax value is larger than AST can handle");
    return RegParmMax;
  }

  bool isTLSSupported() const { return TLSSupported; }

  unsigned getMaxTLSAlign() const { return MaxTLSAlign; }

  bool isVLASupported() const { return VLASupported; }

  bool isSEHTrySupported() const {
    return getTriple().isOSWindows() &&
           (getTriple().isX86() ||
            getTriple().getArch() == llvm::Triple::aarch64);
  }

  bool hasNoAsmVariants() const { return NoAsmVariants; }

  virtual int getEHDataRegisterNumber(unsigned RegNo) const { return -1; }

  virtual const char *getStaticInitSectionSpecifier() const { return nullptr; }

  const LangASMap &getAddressSpaceMap() const { return *AddrSpaceMap; }
  unsigned getTargetAddressSpace(LangAS AS) const {
    if (isTargetAddressSpace(AS))
      return toTargetAddressSpace(AS);
    return getAddressSpaceMap()[(unsigned)AS];
  }

  virtual std::optional<LangAS> getConstantAddressSpace() const {
    return LangAS::Default;
  }

  llvm::StringRef getPlatformName() const { return PlatformName; }

  llvm::VersionTuple getPlatformMinVersion() const {
    return PlatformMinVersion;
  }

  bool isBigEndian() const { return false; }
  bool isLittleEndian() const { return true; }

  virtual bool supportsExtendIntArgs() const { return false; }

  virtual bool checkArithmeticFenceSupported() const { return false; }

  virtual CallingConv getDefaultCallingConv() const {
    // Not all targets will specify an explicit calling convention that we can
    // express.  This will always do the right thing, even though it's not
    // an explicit calling convention.
    return CC_C;
  }

  enum CallingConvCheckResult {
    CCCR_OK,
    CCCR_Warning,
    CCCR_Ignore,
    CCCR_Error,
  };

  virtual CallingConvCheckResult checkCallingConvention(CallingConv CC) const {
    switch (CC) {
    default:
      return CCCR_Warning;
    case CC_C:
      return CCCR_OK;
    }
  }

  enum CallingConvKind { CCK_Default, CCK_NeverCABI4, CCK_MicrosoftWin64 };

  virtual CallingConvKind getCallingConvKind(bool ABICompat4) const;

  virtual bool hasSjLjLowering() const { return false; }

  virtual bool checkCFProtectionBranchSupported(DiagnosticsEngine &Diags) const;

  virtual bool checkCFProtectionReturnSupported(DiagnosticsEngine &Diags) const;

  virtual std::optional<unsigned>
  getDWARFAddressSpace(unsigned AddressSpace) const {
    return std::nullopt;
  }

  const llvm::VersionTuple &getSDKVersion() const {
    return getTargetOpts().SDKVersion;
  }

  virtual bool validateTarget(DiagnosticsEngine &Diags) const { return true; }

  virtual bool allowDebugInfoForExternalRef() const { return false; }

protected:
  virtual uint64_t getPointerWidthV(LangAS AddrSpace) const {
    return PointerWidth;
  }
  virtual uint64_t getPointerAlignV(LangAS AddrSpace) const {
    return PointerAlign;
  }
  virtual enum IntType getPtrDiffTypeV(LangAS AddrSpace) const {
    return PtrDiffType;
  }
  virtual llvm::ArrayRef<const char *> getGCCRegNames() const = 0;
  virtual llvm::ArrayRef<GCCRegAlias> getGCCRegAliases() const = 0;
  virtual llvm::ArrayRef<AddlRegName> getGCCAddlRegNames() const {
    return std::nullopt;
  }

private:
  // Assert the values for the fractional and integral bits for each fixed point
  // type follow the restrictions given in clause 6.2.6.3 of N1169.
  void CheckFixedPointBits() const;
};

} // end namespace neverc

#endif
