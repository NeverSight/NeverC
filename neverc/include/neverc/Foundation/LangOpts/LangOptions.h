#ifndef NEVERC_FOUNDATION_LANGOPTIONS_H
#define NEVERC_FOUNDATION_LANGOPTIONS_H

#include "neverc/Foundation/Core/Visibility.h"
#include "neverc/Foundation/LangOpts/LangStandard.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/HashBuilder.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Triple.h"
#include <map>
#include <string>
#include <vector>

namespace neverc {

class LangOptionsBase {
  friend class CompilerInvocation;
  friend class CompilerInvocationBase;

public:
  // Define simple language options (with no accessors).
#define LANGOPT(Name, Bits, Default, Description) unsigned Name : Bits;
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description)
#include "neverc/Foundation/LangOpts/LangOptions.def"

protected:
  // Define language options of enumeration type. These are private, and will
  // have accessors (below).
#define LANGOPT(Name, Bits, Default, Description)
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description)                   \
  unsigned Name : Bits;
#include "neverc/Foundation/LangOpts/LangOptions.def"
};

class LangOptions : public LangOptionsBase {
public:
  using Visibility = neverc::Visibility;
  using RoundingMode = llvm::RoundingMode;

  enum StackProtectorMode { SSPOff, SSPOn, SSPStrong, SSPReq };

  // Automatic variables live on the stack, and when trivial they're usually
  // uninitialized because it's undefined behavior to use them without
  // initializing them.
  enum class TrivialAutoVarInitKind { Uninitialized, Zero, Pattern };

  enum SignedOverflowBehaviorTy {
    // Default C standard behavior.
    SOB_Undefined,

    // -fwrapv
    SOB_Defined,

    // -ftrapv
    SOB_Trapping
  };

  enum DefaultCallingConvention {
    DCC_None,
    DCC_CDecl,
    DCC_FastCall,
    DCC_StdCall,
    DCC_VectorCall,
    DCC_RegCall
  };

  // Corresponds to _MSC_VER
  enum MSVCMajorVersion {
    MSVC2010 = 1600,
    MSVC2012 = 1700,
    MSVC2013 = 1800,
    MSVC2015 = 1900,
    MSVC2017 = 1910,
    MSVC2017_5 = 1912,
    MSVC2017_7 = 1914,
    MSVC2019 = 1920,
    MSVC2019_5 = 1925,
    MSVC2019_8 = 1928,
    MSVC2022_3 = 1933,
  };

  enum class NeverCABI {
    /// ABI version 3.8: e.g. \c <1 x long long> passing on x86_64.
    Ver3_8,

    /// ABI version 4.
    Ver4,

    /// ABI version 6.
    Ver6,

    /// ABI version 7: \c alignof / \c _Alignof vs \c __alignof.
    Ver7,

    /// ABI version 9: e.g. \c __int128 vector passing on x86_64.
    Ver9,

    /// ABI version 11.
    Ver11,

    /// ABI version 12.
    Ver12,

    /// ABI version 14.
    Ver14,

    /// ABI version 15.
    Ver15,

    /// ABI version 17.
    Ver17,

    /// Match current NeverC and the platform ABI as closely as practical.
    Latest
  };

  enum FPModeKind {
    // Disable the floating point pragma
    FPM_Off,

    // Enable the floating point pragma
    FPM_On,

    // Aggressively fuse FP ops (E.g. FMA) disregarding pragmas.
    FPM_Fast,

    // Aggressively fuse FP ops and honor pragmas.
    FPM_FastHonorPragmas
  };

  enum FPExceptionModeKind {
    /// Assume that floating-point exceptions are masked.
    FPE_Ignore,
    /// Transformations do not cause new exceptions but may hide some.
    FPE_MayTrap,
    /// Strictly preserve the floating-point exception semantics.
    FPE_Strict,
    /// Used internally to represent initial unspecified value.
    FPE_Default
  };

  enum FPEvalMethodKind {
    /// The evaluation method cannot be determined or is inconsistent for this
    /// target.
    FEM_Indeterminable = -1,
    /// Use the declared type for fp arithmetic.
    FEM_Source = 0,
    /// Use the type double for fp arithmetic.
    FEM_Double = 1,
    /// Use extended type for fp arithmetic.
    FEM_Extended = 2,
    /// Used only for FE option processing; this is only used to indicate that
    /// the user did not specify an explicit evaluation method on the command
    /// line and so the target should be queried for its default evaluation
    /// method instead.
    FEM_UnsetOnCommandLine = 3
  };

  enum ExcessPrecisionKind { FPP_Standard, FPP_Fast, FPP_None };

  enum class ExceptionHandlingKind { None, WinEH, DwarfCFI };

  enum class LaxVectorConversionKind {
    /// Permit no implicit vector bitcasts.
    None,
    /// Permit vector bitcasts between integer vectors with different numbers
    /// of elements but the same total bit-width.
    Integer,
    /// Permit vector bitcasts between all vectors with the same total
    /// bit-width.
    All,
  };

  enum class SignReturnAddressScopeKind {
    /// No signing for any function.
    None,
    /// Sign the return address of functions that spill LR.
    NonLeaf,
    /// Sign the return address of all functions,
    All
  };

  enum class SignReturnAddressKeyKind {
    /// Return address signing uses APIA key.
    AKey,
    /// Return address signing uses APIB key.
    BKey
  };

  enum class ThreadModelKind {
    /// POSIX Threads.
    POSIX,
    /// Single Threaded Environment.
    Single
  };

  enum class ExtendArgsKind {
    /// Integer arguments are sign or zero extended to 32/64 bits
    /// during default argument promotions.
    ExtendTo32,
    ExtendTo64
  };

  enum class DefaultVisiblityExportMapping {
    None,
    /// map only explicit default visibilities to exported
    Explicit,
    /// map all default visibilities to exported
    All,
  };

  enum class StrictFlexArraysLevelKind {
    /// Any trailing array member is a FAM.
    Default = 0,
    /// Any trailing array member of undefined, 0, or 1 size is a FAM.
    OneZeroOrIncomplete = 1,
    /// Any trailing array member of undefined or 0 size is a FAM.
    ZeroOrIncomplete = 2,
    /// Any trailing array member of undefined size is a FAM.
    IncompleteOnly = 3,
  };

  enum ComplexRangeKind { CX_Full, CX_Limited, CX_Improved };

public:
  LangStandard::Kind LangStd;

  std::string OverflowHandler;

  std::vector<std::string> NoBuiltinFuncs;

  std::map<std::string, std::string, std::greater<std::string>> MacroPrefixMap;

  bool IsHeaderFile = false;

  std::string RandstructSeed;

  bool UseTargetPathSeparator = false;

  LangOptions();

  static void
  setLangDefaults(LangOptions &Opts, Language Lang, const llvm::Triple &T,
                  std::vector<std::string> &Includes,
                  LangStandard::Kind LangStd = LangStandard::lang_unspecified);

  // Define accessors/mutators for language options of enumeration type.
#define LANGOPT(Name, Bits, Default, Description)
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description)                   \
  Type get##Name() const { return static_cast<Type>(Name); }                   \
  void set##Name(Type Value) { Name = static_cast<unsigned>(Value); }
#include "neverc/Foundation/LangOpts/LangOptions.def"

  bool isSignedOverflowDefined() const {
    return getSignedOverflowBehavior() == SOB_Defined;
  }

  bool isCompatibleWithMSVC(MSVCMajorVersion MajorVersion) const {
    return MSCompatibilityVersion >= MajorVersion * 100000U;
  }

  bool isNoBuiltinFunc(llvm::StringRef Name) const;

  bool requiresStrictPrototypes() const { return C23 || DisableKNRFunctions; }

  bool implicitFunctionsAllowed() const { return !requiresStrictPrototypes(); }

  bool hasAtExit() const { return true; }

  bool isImplicitIntRequired() const { return !C99; }

  bool isImplicitIntAllowed() const { return !C23; }

  bool hasSignReturnAddress() const {
    return getSignReturnAddressScope() != SignReturnAddressScopeKind::None;
  }

  bool isSignReturnAddressWithAKey() const {
    return getSignReturnAddressKey() == SignReturnAddressKeyKind::AKey;
  }

  bool isSignReturnAddressScopeAll() const {
    return getSignReturnAddressScope() == SignReturnAddressScopeKind::All;
  }

  bool hasSEHExceptions() const {
    return getExceptionHandling() == ExceptionHandlingKind::WinEH;
  }

  bool hasDWARFExceptions() const {
    return getExceptionHandling() == ExceptionHandlingKind::DwarfCFI;
  }

  bool hasDefaultVisibilityExportMapping() const {
    return getDefaultVisibilityExportMapping() !=
           DefaultVisiblityExportMapping::None;
  }

  bool isExplicitDefaultVisibilityExportMapping() const {
    return getDefaultVisibilityExportMapping() ==
           DefaultVisiblityExportMapping::Explicit;
  }

  bool isAllDefaultVisibilityExportMapping() const {
    return getDefaultVisibilityExportMapping() ==
           DefaultVisiblityExportMapping::All;
  }

  void remapPathPrefix(llvm::SmallVectorImpl<char> &Path) const;

  RoundingMode getDefaultRoundingMode() const {
    return RoundingMath ? RoundingMode::Dynamic
                        : RoundingMode::NearestTiesToEven;
  }

  FPExceptionModeKind getDefaultExceptionMode() const {
    FPExceptionModeKind EM = getFPExceptionMode();
    if (EM == FPExceptionModeKind::FPE_Default)
      return FPExceptionModeKind::FPE_Ignore;
    return EM;
  }
};

class FPOptionsOverride;
class FPOptions {
public:
  // We start by defining the layout.
  using storage_type = uint32_t;

  using RoundingMode = llvm::RoundingMode;

  static constexpr unsigned StorageBitSize = 8 * sizeof(storage_type);

  // Define a fake option named "First" so that we have a PREVIOUS even for the
  // real first option.
  static constexpr storage_type FirstShift = 0, FirstWidth = 0;
#define OPTION(NAME, TYPE, WIDTH, PREVIOUS)                                    \
  static constexpr storage_type NAME##Shift =                                  \
      PREVIOUS##Shift + PREVIOUS##Width;                                       \
  static constexpr storage_type NAME##Width = WIDTH;                           \
  static constexpr storage_type NAME##Mask = ((1 << NAME##Width) - 1)          \
                                             << NAME##Shift;
#include "neverc/Foundation/LangOpts/FPOptions.def"

  static constexpr storage_type TotalWidth = 0
#define OPTION(NAME, TYPE, WIDTH, PREVIOUS) +WIDTH
#include "neverc/Foundation/LangOpts/FPOptions.def"
      ;
  static_assert(TotalWidth <= StorageBitSize, "Too short type for FPOptions");

private:
  storage_type Value;

  FPOptionsOverride getChangesSlow(const FPOptions &Base) const;

public:
  FPOptions() : Value(0) {
    setFPContractMode(LangOptions::FPM_Off);
    setConstRoundingMode(RoundingMode::Dynamic);
    setSpecifiedExceptionMode(LangOptions::FPE_Default);
  }
  explicit FPOptions(const LangOptions &LO) {
    Value = 0;
    // The language fp contract option FPM_FastHonorPragmas has the same effect
    // as FPM_Fast in frontend. For simplicity, use FPM_Fast uniformly in
    // frontend.
    auto LangOptContractMode = LO.getDefaultFPContractMode();
    if (LangOptContractMode == LangOptions::FPM_FastHonorPragmas)
      LangOptContractMode = LangOptions::FPM_Fast;
    setFPContractMode(LangOptContractMode);
    setRoundingMath(LO.RoundingMath);
    setConstRoundingMode(LangOptions::RoundingMode::Dynamic);
    setSpecifiedExceptionMode(LO.getFPExceptionMode());
    setAllowFPReassociate(LO.AllowFPReassoc);
    setNoHonorNaNs(LO.NoHonorNaNs);
    setNoHonorInfs(LO.NoHonorInfs);
    setNoSignedZero(LO.NoSignedZero);
    setAllowReciprocal(LO.AllowRecip);
    setAllowApproxFunc(LO.ApproxFunc);
    if (getFPContractMode() == LangOptions::FPM_On &&
        getRoundingMode() == llvm::RoundingMode::Dynamic &&
        getExceptionMode() == LangOptions::FPE_Strict)
      // If the FP settings are set to the "strict" model, then
      // FENV access is set to true. (ffp-model=strict)
      setAllowFEnvAccess(true);
    else
      setAllowFEnvAccess(LangOptions::FPM_Off);
    setComplexRange(LO.getComplexRange());
  }

  bool allowFPContractWithinStatement() const {
    return getFPContractMode() == LangOptions::FPM_On;
  }
  void setAllowFPContractWithinStatement() {
    setFPContractMode(LangOptions::FPM_On);
  }

  bool allowFPContractAcrossStatement() const {
    return getFPContractMode() == LangOptions::FPM_Fast;
  }
  void setAllowFPContractAcrossStatement() {
    setFPContractMode(LangOptions::FPM_Fast);
  }

  bool isFPConstrained() const {
    return getRoundingMode() != llvm::RoundingMode::NearestTiesToEven ||
           getExceptionMode() != LangOptions::FPE_Ignore ||
           getAllowFEnvAccess();
  }

  RoundingMode getRoundingMode() const {
    RoundingMode RM = getConstRoundingMode();
    if (RM == RoundingMode::Dynamic) {
      // C23: 7.6.2p3  If the FE_DYNAMIC mode is specified and FENV_ACCESS is
      // "off", the translator may assume that the default rounding mode is in
      // effect.
      if (!getAllowFEnvAccess() && !getRoundingMath())
        RM = RoundingMode::NearestTiesToEven;
    }
    return RM;
  }

  LangOptions::FPExceptionModeKind getExceptionMode() const {
    LangOptions::FPExceptionModeKind EM = getSpecifiedExceptionMode();
    if (EM == LangOptions::FPExceptionModeKind::FPE_Default) {
      if (getAllowFEnvAccess())
        return LangOptions::FPExceptionModeKind::FPE_Strict;
      else
        return LangOptions::FPExceptionModeKind::FPE_Ignore;
    }
    return EM;
  }

  bool operator==(FPOptions other) const { return Value == other.Value; }

  static FPOptions defaultWithoutTrailingStorage(const LangOptions &LO);

  storage_type getAsOpaqueInt() const { return Value; }
  static FPOptions getFromOpaqueInt(storage_type Value) {
    FPOptions Opts;
    Opts.Value = Value;
    return Opts;
  }

  FPOptionsOverride getChangesFrom(const FPOptions &Base) const;

  // We can define most of the accessors automatically:
#define OPTION(NAME, TYPE, WIDTH, PREVIOUS)                                    \
  TYPE get##NAME() const {                                                     \
    return static_cast<TYPE>((Value & NAME##Mask) >> NAME##Shift);             \
  }                                                                            \
  void set##NAME(TYPE value) {                                                 \
    Value = (Value & ~NAME##Mask) | (storage_type(value) << NAME##Shift);      \
  }
#include "neverc/Foundation/LangOpts/FPOptions.def"
  LLVM_DUMP_METHOD void dump();
};

class FPOptionsOverride {
  FPOptions Options = FPOptions::getFromOpaqueInt(0);
  FPOptions::storage_type OverrideMask = 0;

public:
  using RoundingMode = llvm::RoundingMode;

  using storage_type = uint64_t;
  static_assert(sizeof(storage_type) >= 2 * sizeof(FPOptions::storage_type),
                "Too short type for FPOptionsOverride");

  static constexpr storage_type OverrideMaskBits =
      (static_cast<storage_type>(1) << FPOptions::StorageBitSize) - 1;

  FPOptionsOverride() {}
  FPOptionsOverride(const LangOptions &LO)
      : Options(LO), OverrideMask(OverrideMaskBits) {}
  FPOptionsOverride(FPOptions FPO)
      : Options(FPO), OverrideMask(OverrideMaskBits) {}
  FPOptionsOverride(FPOptions FPO, FPOptions::storage_type Mask)
      : Options(FPO), OverrideMask(Mask) {}

  bool requiresTrailingStorage() const { return OverrideMask != 0; }

  void setAllowFPContractWithinStatement() {
    setFPContractModeOverride(LangOptions::FPM_On);
  }

  void setAllowFPContractAcrossStatement() {
    setFPContractModeOverride(LangOptions::FPM_Fast);
  }

  void setDisallowFPContract() {
    setFPContractModeOverride(LangOptions::FPM_Off);
  }

  void setFPPreciseEnabled(bool Value) {
    setAllowFPReassociateOverride(!Value);
    setNoHonorNaNsOverride(!Value);
    setNoHonorInfsOverride(!Value);
    setNoSignedZeroOverride(!Value);
    setAllowReciprocalOverride(!Value);
    setAllowApproxFuncOverride(!Value);
    setMathErrnoOverride(Value);
    if (Value)
      /* Precise mode implies fp_contract=on and disables ffast-math */
      setAllowFPContractWithinStatement();
    else
      /* Precise mode disabled sets fp_contract=fast and enables ffast-math */
      setAllowFPContractAcrossStatement();
  }

  storage_type getAsOpaqueInt() const {
    return (static_cast<storage_type>(Options.getAsOpaqueInt())
            << FPOptions::StorageBitSize) |
           OverrideMask;
  }
  static FPOptionsOverride getFromOpaqueInt(storage_type I) {
    FPOptionsOverride Opts;
    Opts.OverrideMask = I & OverrideMaskBits;
    Opts.Options = FPOptions::getFromOpaqueInt(I >> FPOptions::StorageBitSize);
    return Opts;
  }

  FPOptions applyOverrides(FPOptions Base) {
    FPOptions Result =
        FPOptions::getFromOpaqueInt((Base.getAsOpaqueInt() & ~OverrideMask) |
                                    (Options.getAsOpaqueInt() & OverrideMask));
    return Result;
  }

  FPOptions applyOverrides(const LangOptions &LO) {
    return applyOverrides(FPOptions(LO));
  }

  bool operator==(FPOptionsOverride other) const {
    return Options == other.Options && OverrideMask == other.OverrideMask;
  }
  bool operator!=(FPOptionsOverride other) const { return !(*this == other); }

#define OPTION(NAME, TYPE, WIDTH, PREVIOUS)                                    \
  bool has##NAME##Override() const {                                           \
    return OverrideMask & FPOptions::NAME##Mask;                               \
  }                                                                            \
  TYPE get##NAME##Override() const {                                           \
    assert(has##NAME##Override());                                             \
    return Options.get##NAME();                                                \
  }                                                                            \
  void clear##NAME##Override() {                                               \
    /* Clear the actual value so that we don't have spurious differences when  \
     * testing equality. */                                                    \
    Options.set##NAME(TYPE(0));                                                \
    OverrideMask &= ~FPOptions::NAME##Mask;                                    \
  }                                                                            \
  void set##NAME##Override(TYPE value) {                                       \
    Options.set##NAME(value);                                                  \
    OverrideMask |= FPOptions::NAME##Mask;                                     \
  }
#include "neverc/Foundation/LangOpts/FPOptions.def"
  LLVM_DUMP_METHOD void dump();
};

inline FPOptionsOverride
FPOptions::getChangesFrom(const FPOptions &Base) const {
  if (Value == Base.Value)
    return FPOptionsOverride();
  return getChangesSlow(Base);
}

} // namespace neverc

#endif // NEVERC_FOUNDATION_LANGOPTIONS_H
