#ifndef NEVERC_LIB_BASIC_TARGET_AARCH64_H
#define NEVERC_LIB_BASIC_TARGET_AARCH64_H

#include "OSTargets.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include <optional>

namespace neverc {
namespace targets {

class LLVM_LIBRARY_VISIBILITY AArch64TargetInfo : public TargetInfo {
  virtual void setDataLayout() = 0;
  static const TargetInfo::GCCRegAlias GCCRegAliases[];
  static const char *const GCCRegNames[];

  enum FPUModeEnum : unsigned {
    FPUMode = 1u << 0,
    NeonMode = 1u << 1,
    SveMode = 1u << 2,
  };

  enum AArch64Feature : uint64_t {
    FEAT_CRC = 1ULL << 0,
    FEAT_AES = 1ULL << 1,
    FEAT_SHA2 = 1ULL << 2,
    FEAT_SHA3 = 1ULL << 3,
    FEAT_SM4 = 1ULL << 4,
    FEAT_Unaligned = 1ULL << 5,
    FEAT_FullFP16 = 1ULL << 6,
    FEAT_DotProd = 1ULL << 7,
    FEAT_FP16FML = 1ULL << 8,
    FEAT_MTE = 1ULL << 9,
    FEAT_TME = 1ULL << 10,
    FEAT_PAuth = 1ULL << 11,
    FEAT_LS64 = 1ULL << 12,
    FEAT_RandGen = 1ULL << 13,
    FEAT_MatMul = 1ULL << 14,
    FEAT_BFloat16 = 1ULL << 15,
    FEAT_SVE2 = 1ULL << 16,
    FEAT_SVE2AES = 1ULL << 17,
    FEAT_SVE2SHA3 = 1ULL << 18,
    FEAT_SVE2SM4 = 1ULL << 19,
    FEAT_SVE2BitPerm = 1ULL << 20,
    FEAT_MatmulFP64 = 1ULL << 21,
    FEAT_MatmulFP32 = 1ULL << 22,
    FEAT_LSE = 1ULL << 23,
    FEAT_FlagM = 1ULL << 24,
    FEAT_AltNZCV = 1ULL << 25,
    FEAT_MOPS = 1ULL << 26,
    FEAT_D128 = 1ULL << 27,
    FEAT_RCPC = 1ULL << 28,
    FEAT_RDM = 1ULL << 29,
    FEAT_DIT = 1ULL << 30,
    FEAT_CCPP = 1ULL << 31,
    FEAT_CCDP = 1ULL << 32,
    FEAT_FRInt3264 = 1ULL << 33,
    FEAT_SME = 1ULL << 34,
    FEAT_SMEF64F64 = 1ULL << 35,
    FEAT_SMEI16I64 = 1ULL << 36,
    FEAT_SB = 1ULL << 37,
    FEAT_PredRes = 1ULL << 38,
    FEAT_SSBS = 1ULL << 39,
    FEAT_BTI = 1ULL << 40,
    FEAT_WFxT = 1ULL << 41,
    FEAT_JSCVT = 1ULL << 42,
    FEAT_FCMA = 1ULL << 43,
    FEAT_NoFP = 1ULL << 44,
    FEAT_NoNeon = 1ULL << 45,
    FEAT_NoSVE = 1ULL << 46,
    FEAT_FMV = 1ULL << 47,
    FEAT_GCS = 1ULL << 48,
    FEAT_RCPC3 = 1ULL << 49,
    FEAT_SMEFA64 = 1ULL << 50,
  };

  unsigned FPU = FPUMode;
  uint64_t Features = FEAT_Unaligned | FEAT_FMV;

  bool hasAArch64Feature(uint64_t F) const { return (Features & F) != 0; }
  void setAArch64Feature(uint64_t F, bool On) {
    Features = On ? (Features | F) : (Features & ~F);
  }

  bool HasCRC = false;
  bool HasAES = false;
  bool HasSHA2 = false;
  bool HasSHA3 = false;
  bool HasSM4 = false;
  bool HasUnaligned = true;
  bool HasFullFP16 = false;
  bool HasDotProd = false;
  bool HasFP16FML = false;
  bool HasMTE = false;
  bool HasTME = false;
  bool HasPAuth = false;
  bool HasLS64 = false;
  bool HasRandGen = false;
  bool HasMatMul = false;
  bool HasBFloat16 = false;
  bool HasSVE2 = false;
  bool HasSVE2AES = false;
  bool HasSVE2SHA3 = false;
  bool HasSVE2SM4 = false;
  bool HasSVE2BitPerm = false;
  bool HasMatmulFP64 = false;
  bool HasMatmulFP32 = false;
  bool HasLSE = false;
  bool HasFlagM = false;
  bool HasAlternativeNZCV = false;
  bool HasMOPS = false;
  bool HasD128 = false;
  bool HasRCPC = false;
  bool HasRDM = false;
  bool HasDIT = false;
  bool HasCCPP = false;
  bool HasCCDP = false;
  bool HasFRInt3264 = false;
  bool HasSME = false;
  bool HasSMEF64F64 = false;
  bool HasSMEI16I64 = false;
  bool HasSB = false;
  bool HasPredRes = false;
  bool HasSSBS = false;
  bool HasBTI = false;
  bool HasWFxT = false;
  bool HasJSCVT = false;
  bool HasFCMA = false;
  bool HasNoFP = false;
  bool HasNoNeon = false;
  bool HasNoSVE = false;
  bool HasFMV = true;
  bool HasGCS = false;
  bool HasRCPC3 = false;
  bool HasSMEFA64 = false;

  const llvm::AArch64::ArchInfo *ArchInfo = &llvm::AArch64::ARMV8A;

  std::string ABI;

public:
  AArch64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  llvm::StringRef getABI() const override;
  bool setABI(const std::string &Name) override;

  bool validateBranchProtection(llvm::StringRef Spec, llvm::StringRef Arch,
                                BranchProtectionInfo &BPI,
                                llvm::StringRef &Err) const override;

  bool isValidCPUName(llvm::StringRef Name) const override;
  void fillValidCPUList(
      llvm::SmallVectorImpl<llvm::StringRef> &Values) const override;
  bool setCPU(const std::string &Name) override;

  unsigned multiVersionSortPriority(llvm::StringRef Name) const override;
  unsigned multiVersionFeatureCost() const override;

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 llvm::StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override;
  bool useFP16ConversionIntrinsics() const override { return false; }

  void setArchFeatures();

  void getTargetDefinesARMV81A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV82A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV83A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV84A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV85A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV86A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV87A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV88A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV89A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV9A(const LangOptions &Opts,
                              MacroBuilder &Builder) const;
  void getTargetDefinesARMV91A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV92A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV93A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV94A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV95A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  llvm::ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  std::optional<std::pair<unsigned, unsigned>>
  getVScaleRange(const LangOptions &LangOpts) const override;
  bool doesFeatureAffectCodeGen(llvm::StringRef Name) const override;
  llvm::StringRef getFeatureDependencies(llvm::StringRef Name) const override;
  bool validateCpuSupports(llvm::StringRef FeatureStr) const override;
  bool hasFeature(llvm::StringRef Feature) const override;
  void setFeatureEnabled(llvm::StringMap<bool> &Features, llvm::StringRef Name,
                         bool Enabled) const override;
  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;
  ParsedTargetAttr parseTargetAttr(llvm::StringRef Str) const override;
  bool supportsTargetAttributeTune() const override { return true; }

  bool checkArithmeticFenceSupported() const override { return true; }

  bool hasBFloat16Type() const override;

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override;

  bool isCLZForZeroUndef() const override;

  BuiltinVaListKind getBuiltinVaListKind() const override;

  llvm::ArrayRef<const char *> getGCCRegNames() const override;
  llvm::ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  std::string convertConstraint(const char *&Constraint) const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override;
  bool
  validateConstraintModifier(llvm::StringRef Constraint, char Modifier,
                             unsigned Size,
                             std::string &SuggestedModifier) const override;
  std::string_view getClobbers() const override;

  llvm::StringRef
  getConstraintRegister(llvm::StringRef Constraint,
                        llvm::StringRef Expression) const override {
    return Expression;
  }

  int getEHDataRegisterNumber(unsigned RegNo) const override;

  const char *getBFloat16Mangling() const override { return "u6__bf16"; };
  bool hasInt128Type() const override;

  bool hasBitIntType() const override { return true; }
};

class LLVM_LIBRARY_VISIBILITY AArch64leTargetInfo : public AArch64TargetInfo {
public:
  AArch64leTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

private:
  void setDataLayout() override;
};

class LLVM_LIBRARY_VISIBILITY DarwinAArch64TargetInfo
    : public DarwinTargetInfo<AArch64leTargetInfo> {
public:
  DarwinAArch64TargetInfo(const llvm::Triple &Triple,
                          const TargetOptions &Opts);

  BuiltinVaListKind getBuiltinVaListKind() const override;

protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override;
};

} // namespace targets
} // namespace neverc

#endif // NEVERC_LIB_BASIC_TARGET_AARCH64_H
