#ifndef NEVERC_LIB_BASIC_TARGET_X86_H
#define NEVERC_LIB_BASIC_TARGET_X86_H

#include "OSTargets.h"
#include "neverc/Foundation/Core/BitmaskEnum.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include <optional>

namespace neverc {
namespace targets {

static const unsigned X86AddrSpaceMap[] = {
    0,   // Default
    270, // ptr32_sptr
    271, // ptr32_uptr
    272, // ptr64
};

// X86 target abstract base class.
class LLVM_LIBRARY_VISIBILITY X86TargetInfo : public TargetInfo {

  enum X86SSEEnum {
    NoSSE,
    SSE1,
    SSE2,
    SSE3,
    SSSE3,
    SSE41,
    SSE42,
    AVX,
    AVX2,
    AVX512F
  } SSELevel = NoSSE;
  enum MMX3DNowEnum {
    NoMMX3DNow,
    MMX,
    AMD3DNow,
    AMD3DNowAthlon
  } MMX3DNowLevel = NoMMX3DNow;
  enum XOPEnum { NoXOP, SSE4A, FMA4, XOP } XOPLevel = NoXOP;
  enum AddrSpace { ptr32_sptr = 270, ptr32_uptr = 271, ptr64 = 272 };

  bool HasAES = false;
  bool HasVAES = false;
  bool HasPCLMUL = false;
  bool HasVPCLMULQDQ = false;
  bool HasGFNI = false;
  bool HasLZCNT = false;
  bool HasRDRND = false;
  bool HasFSGSBASE = false;
  bool HasBMI = false;
  bool HasBMI2 = false;
  bool HasPOPCNT = false;
  bool HasRTM = false;
  bool HasPRFCHW = false;
  bool HasRDSEED = false;
  bool HasADX = false;
  bool HasTBM = false;
  bool HasLWP = false;
  bool HasFMA = false;
  bool HasF16C = false;
  bool HasAVX10_1 = false;
  bool HasAVX10_1_512 = false;
  bool HasEVEX512 = false;
  bool HasAVX512CD = false;
  bool HasAVX512VPOPCNTDQ = false;
  bool HasAVX512VNNI = false;
  bool HasAVX512FP16 = false;
  bool HasAVX512BF16 = false;
  bool HasAVX512ER = false;
  bool HasAVX512PF = false;
  bool HasAVX512DQ = false;
  bool HasAVX512BITALG = false;
  bool HasAVX512BW = false;
  bool HasAVX512VL = false;
  bool HasAVX512VBMI = false;
  bool HasAVX512VBMI2 = false;
  bool HasAVXIFMA = false;
  bool HasAVX512IFMA = false;
  bool HasAVX512VP2INTERSECT = false;
  bool HasSHA = false;
  bool HasSHA512 = false;
  bool HasSHSTK = false;
  bool HasSM3 = false;
  bool HasSGX = false;
  bool HasSM4 = false;
  bool HasCX8 = false;
  bool HasCX16 = false;
  bool HasFXSR = false;
  bool HasXSAVE = false;
  bool HasXSAVEOPT = false;
  bool HasXSAVEC = false;
  bool HasXSAVES = false;
  bool HasMWAITX = false;
  bool HasCLZERO = false;
  bool HasCLDEMOTE = false;
  bool HasPCONFIG = false;
  bool HasPKU = false;
  bool HasCLFLUSHOPT = false;
  bool HasCLWB = false;
  bool HasMOVBE = false;
  bool HasPREFETCHI = false;
  bool HasPREFETCHWT1 = false;
  bool HasRDPID = false;
  bool HasRDPRU = false;
  bool HasRetpolineExternalThunk = false;
  bool HasLAHFSAHF = false;
  bool HasWBNOINVD = false;
  bool HasWAITPKG = false;
  bool HasMOVDIRI = false;
  bool HasMOVDIR64B = false;
  bool HasPTWRITE = false;
  bool HasINVPCID = false;
  bool HasENQCMD = false;
  bool HasAVXVNNIINT16 = false;
  bool HasAMXFP16 = false;
  bool HasCMPCCXADD = false;
  bool HasRAOINT = false;
  bool HasAVXVNNIINT8 = false;
  bool HasAVXNECONVERT = false;
  bool HasKL = false;     // For key locker
  bool HasWIDEKL = false; // For wide key locker
  bool HasHRESET = false;
  bool HasAVXVNNI = false;
  bool HasAMXTILE = false;
  bool HasAMXINT8 = false;
  bool HasAMXBF16 = false;
  bool HasAMXCOMPLEX = false;
  bool HasSERIALIZE = false;
  bool HasTSXLDTRK = false;
  bool HasUSERMSR = false;
  bool HasUINTR = false;
  bool HasCRC32 = false;
  bool HasX87 = false;
  bool HasEGPR = false;
  bool HasPush2Pop2 = false;
  bool HasPPX = false;
  bool HasNDD = false;
  bool HasCCMP = false;
  bool HasCF = false;

protected:
  llvm::X86::CPUKind CPU = llvm::X86::CK_None;

  enum FPMathKind { FP_Default, FP_SSE, FP_387 } FPMath = FP_Default;

public:
  X86TargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    BFloat16Width = BFloat16Align = 16;
    BFloat16Format = &llvm::APFloat::BFloat();
    LongDoubleFormat = &llvm::APFloat::x87DoubleExtended();
    AddrSpaceMap = &X86AddrSpaceMap;
    HasStrictFP = true;

    bool IsWinCOFF =
        getTriple().isOSWindows() && getTriple().isOSBinFormatCOFF();
    if (IsWinCOFF)
      MaxVectorAlign = MaxTLSAlign = 8192u * getCharWidth();
  }

  const char *getLongDoubleMangling() const override {
    return LongDoubleFormat == &llvm::APFloat::IEEEquad() ? "g" : "e";
  }

  LangOptions::FPEvalMethodKind getFPEvalMethod() const override {
    // X87 evaluates with 80 bits "long double" precision.
    return SSELevel == NoSSE ? LangOptions::FPEvalMethodKind::FEM_Extended
                             : LangOptions::FPEvalMethodKind::FEM_Source;
  }

  // EvalMethod `source` is not supported for targets with `NoSSE` feature.
  bool supportSourceEvalMethod() const override { return SSELevel > NoSSE; }

  llvm::ArrayRef<const char *> getGCCRegNames() const override;

  llvm::ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return std::nullopt;
  }

  llvm::ArrayRef<TargetInfo::AddlRegName> getGCCAddlRegNames() const override;

  bool isSPRegName(llvm::StringRef RegName) const override {
    return RegName.equals("esp") || RegName.equals("rsp");
  }

  bool validateCpuSupports(llvm::StringRef FeatureStr) const override;

  bool validateCpuIs(llvm::StringRef FeatureStr) const override;

  bool validateCPUSpecificCPUDispatch(llvm::StringRef Name) const override;

  char CPUSpecificManglingCharacter(llvm::StringRef Name) const override;

  void getCPUSpecificCPUDispatchFeatures(
      llvm::StringRef Name,
      llvm::SmallVectorImpl<llvm::StringRef> &Features) const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override;

  bool validateGlobalRegisterVariable(llvm::StringRef RegName, unsigned RegSize,
                                      bool &HasSizeMismatch) const override {
    // esp and ebp are valid as 32-bit sub-registers of rsp/rbp.
    if (RegName.equals("esp") || RegName.equals("ebp")) {
      // Check that the register size is 32-bit.
      HasSizeMismatch = RegSize != 32;
      return true;
    }

    return false;
  }

  bool validateOutputSize(const llvm::StringMap<bool> &FeatureMap,
                          llvm::StringRef Constraint,
                          unsigned Size) const override;

  bool validateInputSize(const llvm::StringMap<bool> &FeatureMap,
                         llvm::StringRef Constraint,
                         unsigned Size) const override;

  bool checkCFProtectionReturnSupported(DiagnosticsEngine &) const override {
    return true;
  };

  bool checkCFProtectionBranchSupported(DiagnosticsEngine &) const override {
    return true;
  };

  virtual bool validateOperandSize(const llvm::StringMap<bool> &FeatureMap,
                                   llvm::StringRef Constraint,
                                   unsigned Size) const;

  std::string convertConstraint(const char *&Constraint) const override;
  std::string_view getClobbers() const override {
    return "~{dirflag},~{fpsr},~{flags}";
  }

  llvm::StringRef
  getConstraintRegister(llvm::StringRef Constraint,
                        llvm::StringRef Expression) const override {
    static constexpr struct {
      char Key;
      char Reg[3];
    } RegMap[] = {
        {'a', {'a', 'x', 0}}, {'b', {'b', 'x', 0}}, {'c', {'c', 'x', 0}},
        {'d', {'d', 'x', 0}}, {'S', {'s', 'i', 0}}, {'D', {'d', 'i', 0}},
    };
    const char *P = Constraint.data();
    const char *End = P + Constraint.size();
    while (P != End && !isalpha(*P) && *P != '@')
      ++P;
    if (P == End)
      return "";
    char Ch = *P;
    if (Ch == 'r')
      return Expression;
    if (Ch == 'Y') {
      ++P;
      if (P != End && (*P == '0' || *P == 'z'))
        return "xmm0";
      return "";
    }
    for (const auto &E : RegMap) {
      if (E.Key == Ch)
        return E.Reg;
    }
    return "";
  }

  bool useFP16ConversionIntrinsics() const override { return false; }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  void setFeatureEnabled(llvm::StringMap<bool> &Features, llvm::StringRef Name,
                         bool Enabled) const final;

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 llvm::StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override;

  bool isValidFeatureName(llvm::StringRef Name) const override;

  bool hasFeature(llvm::StringRef Feature) const final;

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;

  llvm::StringRef getABI() const override {
    if (getTriple().getArch() == llvm::Triple::x86_64 && SSELevel >= AVX512F)
      return "avx512";
    if (getTriple().getArch() == llvm::Triple::x86_64 && SSELevel >= AVX)
      return "avx";
    return "";
  }

  bool supportsTargetAttributeTune() const override { return true; }

  bool isValidCPUName(llvm::StringRef Name) const override {
    return llvm::X86::parseArchX86(Name) != llvm::X86::CK_None;
  }

  bool isValidTuneCPUName(llvm::StringRef Name) const override {
    if (Name == "generic")
      return true;

    return isValidCPUName(Name);
  }

  void fillValidCPUList(
      llvm::SmallVectorImpl<llvm::StringRef> &Values) const override;
  void fillValidTuneCPUList(
      llvm::SmallVectorImpl<llvm::StringRef> &Values) const override;

  bool setCPU(const std::string &Name) override {
    CPU = llvm::X86::parseArchX86(Name);
    return CPU != llvm::X86::CK_None;
  }

  unsigned multiVersionSortPriority(llvm::StringRef Name) const override;

  bool setFPMath(llvm::StringRef Name) override;

  bool supportsExtendIntArgs() const override { return true; }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    // stdcall/fastcall are accepted for source compatibility but
    // have no effect on x86_64.
    switch (CC) {
    case CC_X86FastCall:
    case CC_X86StdCall:
    case CC_X86VectorCall:
    case CC_X86RegCall:
    case CC_C:
    case CC_PreserveMost:
      return CCCR_OK;
    default:
      return CCCR_Warning;
    }
  }

  bool checkArithmeticFenceSupported() const override { return true; }

  CallingConv getDefaultCallingConv() const override { return CC_C; }

  bool hasSjLjLowering() const override { return true; }

  uint64_t getPointerWidthV(LangAS AS) const override {
    unsigned TargetAddrSpace = getTargetAddressSpace(AS);
    if (TargetAddrSpace == ptr32_sptr || TargetAddrSpace == ptr32_uptr)
      return 32;
    if (TargetAddrSpace == ptr64)
      return 64;
    return PointerWidth;
  }

  uint64_t getPointerAlignV(LangAS AddrSpace) const override {
    return getPointerWidthV(AddrSpace);
  }
};

// x86-64 generic target
class LLVM_LIBRARY_VISIBILITY X86_64TargetInfo : public X86TargetInfo {
public:
  X86_64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : X86TargetInfo(Triple, Opts) {
    bool IsWinCOFF =
        getTriple().isOSWindows() && getTriple().isOSBinFormatCOFF();
    LongWidth = LongAlign = PointerWidth = PointerAlign = 64;
    LongDoubleWidth = 128;
    LongDoubleAlign = 128;
    LargeArrayMinWidth = 128;
    LargeArrayAlign = 128;
    SuitableAlign = 128;
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
    IntMaxType = SignedLong;
    Int64Type = SignedLong;
    RegParmMax = 6;

    resetDataLayout(IsWinCOFF ? "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:"
                                "64-i128:128-f80:128-n8:16:32:64-S128"
                              : "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:"
                                "64-i128:128-f80:128-n8:16:32:64-S128");

    // Use fp2ret for _Complex long double.
    ComplexLongDoubleUsesFP2Ret = true;

    // Make __builtin_ms_va_list available.
    HasBuiltinMSVaList = true;

    // x86-64 has atomics up to 16 bytes.
    MaxAtomicPromoteWidth = 128;
    MaxAtomicInlineWidth = 64;
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::X86_64ABIBuiltinVaList;
  }

  int getEHDataRegisterNumber(unsigned RegNo) const override {
    if (RegNo == 0)
      return 0;
    if (RegNo == 1)
      return 1;
    return -1;
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    switch (CC) {
    case CC_C:
    case CC_X86VectorCall:
    case CC_Win64:
    case CC_PreserveMost:
    case CC_PreserveAll:
    case CC_X86RegCall:
      return CCCR_OK;
    default:
      return CCCR_Warning;
    }
  }

  CallingConv getDefaultCallingConv() const override { return CC_C; }

  bool hasInt128Type() const override { return true; }

  unsigned getUnwindWordWidth() const override { return 64; }

  unsigned getRegisterWidth() const override { return 64; }

  bool validateGlobalRegisterVariable(llvm::StringRef RegName, unsigned RegSize,
                                      bool &HasSizeMismatch) const override {
    // rsp and rbp are the only 64-bit registers the x86 backend can currently
    // handle.
    if (RegName.equals("rsp") || RegName.equals("rbp")) {
      // Check that the register size is 64-bit.
      HasSizeMismatch = RegSize != 64;
      return true;
    }

    // Check if the register is a 32-bit register the backend can handle.
    return X86TargetInfo::validateGlobalRegisterVariable(RegName, RegSize,
                                                         HasSizeMismatch);
  }

  void setMaxAtomicWidth() override {
    if (hasFeature("cx16"))
      MaxAtomicInlineWidth = 128;
  }

  llvm::ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  bool hasBitIntType() const override { return true; }
  size_t getMaxBitIntWidth() const override {
    return llvm::IntegerType::MAX_INT_BITS;
  }
};

// x86-64 Windows target
class LLVM_LIBRARY_VISIBILITY WindowsX86_64TargetInfo
    : public WindowsTargetInfo<X86_64TargetInfo> {
public:
  WindowsX86_64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : WindowsTargetInfo<X86_64TargetInfo>(Triple, Opts) {
    LongWidth = LongAlign = 32;
    DoubleAlign = LongLongAlign = 64;
    IntMaxType = SignedLongLong;
    Int64Type = SignedLongLong;
    SizeType = UnsignedLongLong;
    PtrDiffType = SignedLongLong;
    IntPtrType = SignedLongLong;
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    switch (CC) {
    case CC_X86StdCall:
    case CC_X86FastCall:
      return CCCR_Ignore;
    case CC_C:
    case CC_X86VectorCall:
    case CC_PreserveMost:
    case CC_PreserveAll:
    case CC_X86_64SysV:
    case CC_X86RegCall:
      return CCCR_OK;
    default:
      return CCCR_Warning;
    }
  }
};

// x86-64 Windows Visual Studio target
class LLVM_LIBRARY_VISIBILITY MicrosoftX86_64TargetInfo
    : public WindowsX86_64TargetInfo {
public:
  MicrosoftX86_64TargetInfo(const llvm::Triple &Triple,
                            const TargetOptions &Opts)
      : WindowsX86_64TargetInfo(Triple, Opts) {
    LongDoubleWidth = LongDoubleAlign = 64;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    WindowsX86_64TargetInfo::getTargetDefines(Opts, Builder);
    Builder.defineMacro("_M_X64", "100");
    Builder.defineMacro("_M_AMD64", "100");
  }

  TargetInfo::CallingConvKind
  getCallingConvKind(bool ABICompat4) const override {
    return CCK_MicrosoftWin64;
  }
};

class LLVM_LIBRARY_VISIBILITY DarwinX86_64TargetInfo
    : public DarwinTargetInfo<X86_64TargetInfo> {
public:
  DarwinX86_64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : DarwinTargetInfo<X86_64TargetInfo>(Triple, Opts) {
    Int64Type = SignedLongLong;
    llvm::Triple T = llvm::Triple(Triple);
    resetDataLayout("e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-"
                    "f80:128-n8:16:32:64-S128",
                    "_");
  }

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override {
    if (!DarwinTargetInfo<X86_64TargetInfo>::handleTargetFeatures(Features,
                                                                  Diags))
      return false;
    // We now know the features we have: we can decide how to align vectors.
    MaxVectorAlign = hasFeature("avx512f") ? 512
                     : hasFeature("avx")   ? 256
                                           : 128;
    return true;
  }
};

} // namespace targets
} // namespace neverc
#endif // NEVERC_LIB_BASIC_TARGET_X86_H
