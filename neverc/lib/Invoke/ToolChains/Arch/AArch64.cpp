#include "AArch64.h"
#include "../CommonArgs.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/Host.h"

using namespace neverc::driver;
using namespace neverc::driver::tools;
using namespace neverc;
using namespace llvm::opt;

// ===----------------------------------------------------------------------===
// CPU & architecture selection
// ===----------------------------------------------------------------------===

namespace {
bool isCPUDeterminedByTriple(const llvm::Triple &Triple) {
  return Triple.isMacOSX();
}
} // namespace

std::string aarch64::getAArch64TargetCPU(const ArgList &Args,
                                         const llvm::Triple &Triple, Arg *&A) {
  std::string CPU;
  // If we have -mcpu, use that.
  if ((A = Args.getLastArg(options::OPT_mcpu_EQ))) {
    llvm::StringRef Mcpu = A->getValue();
    CPU = std::string(Mcpu.split("+").first.lower().str());
  }

  CPU = llvm::AArch64::resolveCPUAlias(CPU);

  // Handle CPU name is 'native'.
  if (CPU == "native")
    return std::string(llvm::sys::getHostCPUName());

  if (CPU.size())
    return CPU;

  if (Triple.isTargetMachineMac() &&
      Triple.getArch() == llvm::Triple::aarch64) {
    // Apple Silicon macs default to M1 CPUs.
    return "apple-m1";
  }

  return "generic";
}

// Decode AArch64 features from string like +[no]featureA+[no]featureB+...

namespace {
bool decodeAArch64Features(const Driver &D, llvm::StringRef text,
                           std::vector<llvm::StringRef> &Features,
                           const llvm::AArch64::ArchInfo &ArchInfo) {
  llvm::SmallVector<llvm::StringRef, 8> Split;
  text.split(Split, llvm::StringRef("+"), -1, false);

  for (llvm::StringRef Feature : Split) {
    llvm::StringRef FeatureName = llvm::AArch64::getArchExtFeature(Feature);
    if (!FeatureName.empty())
      Features.push_back(FeatureName);
    else if (Feature == "neon" || Feature == "noneon")
      D.Diag(neverc::diag::err_drv_no_neon_modifier);
    else
      return false;

    // +sme implies +bf16.
    // +sme-f64f64 and +sme-i16i64 both imply +sme.
    if (Feature == "sme") {
      Features.push_back("+bf16");
    } else if (Feature == "nosme") {
      Features.push_back("-sme-f64f64");
      Features.push_back("-sme-i16i64");
    } else if (Feature == "sme-f64f64") {
      Features.push_back("+sme");
      Features.push_back("+bf16");
    } else if (Feature == "sme-i16i64") {
      Features.push_back("+sme");
      Features.push_back("+bf16");
    } else if (Feature == "nobf16") {
      Features.push_back("-sme");
      Features.push_back("-sme-f64f64");
      Features.push_back("-sme-i16i64");
    }

    if (Feature == "sve2")
      Features.push_back("+sve");
    else if (Feature == "sve2-bitperm" || Feature == "sve2-sha3" ||
             Feature == "sve2-aes" || Feature == "sve2-sm4") {
      Features.push_back("+sve");
      Features.push_back("+sve2");
    } else if (Feature == "nosve") {
      Features.push_back("-sve2");
      Features.push_back("-sve2-bitperm");
      Features.push_back("-sve2-sha3");
      Features.push_back("-sve2-aes");
      Features.push_back("-sve2-sm4");
    } else if (Feature == "nosve2") {
      Features.push_back("-sve2-bitperm");
      Features.push_back("-sve2-sha3");
      Features.push_back("-sve2-aes");
      Features.push_back("-sve2-sm4");
    }

    // +sve implies +f32mm if the base architecture is >= v8.6A (except v9A)
    // It isn't the case in general that sve implies both f64mm and f32mm
    if ((ArchInfo == llvm::AArch64::ARMV8_6A ||
         ArchInfo == llvm::AArch64::ARMV8_7A ||
         ArchInfo == llvm::AArch64::ARMV8_8A ||
         ArchInfo == llvm::AArch64::ARMV8_9A ||
         ArchInfo == llvm::AArch64::ARMV9_1A ||
         ArchInfo == llvm::AArch64::ARMV9_2A ||
         ArchInfo == llvm::AArch64::ARMV9_3A ||
         ArchInfo == llvm::AArch64::ARMV9_4A) &&
        Feature == "sve")
      Features.push_back("+f32mm");
  }
  return true;
}

// Check if the CPU name and feature modifiers in -mcpu are legal. If yes,
// decode CPU and feature.
bool decodeAArch64Mcpu(const Driver &D, llvm::StringRef Mcpu,
                       llvm::StringRef &CPU,
                       std::vector<llvm::StringRef> &Features) {
  std::pair<llvm::StringRef, llvm::StringRef> Split = Mcpu.split("+");
  CPU = Split.first;
  const llvm::AArch64::ArchInfo *ArchInfo = &llvm::AArch64::ARMV8A;

  if (CPU == "native")
    CPU = llvm::sys::getHostCPUName();

  if (CPU == "generic") {
    Features.push_back("+neon");
  } else {
    const std::optional<llvm::AArch64::CpuInfo> CpuInfo =
        llvm::AArch64::parseCpu(CPU);
    if (!CpuInfo)
      return false;
    ArchInfo = &CpuInfo->Arch;

    Features.push_back(ArchInfo->ArchFeature);

    auto Extension = CpuInfo->getImpliedExtensions();
    if (!llvm::AArch64::getExtensionFeatures(Extension, Features))
      return false;
  }

  if (Split.second.size() &&
      !decodeAArch64Features(D, Split.second, Features, *ArchInfo))
    return false;

  return true;
}

bool getAArch64ArchFeaturesFromMarch(const Driver &D, llvm::StringRef March,
                                     const ArgList &Args,
                                     std::vector<llvm::StringRef> &Features) {
  auto MarchLowerCase = March.lower();
  std::pair<llvm::StringRef, llvm::StringRef> Split =
      llvm::StringRef(MarchLowerCase).split("+");

  std::optional<llvm::AArch64::ArchInfo> ArchInfo =
      llvm::AArch64::parseArch(Split.first);
  if (Split.first == "native")
    ArchInfo = llvm::AArch64::getArchForCpu(llvm::sys::getHostCPUName().str());
  if (!ArchInfo)
    return false;
  Features.push_back(ArchInfo->ArchFeature);

  // Enable SVE2 by default on Armv9-A.
  // It can still be disabled if +nosve2 is present.
  // We must do this early so that decodeAArch64Features has the correct state
  if ((*ArchInfo == llvm::AArch64::ARMV9A ||
       *ArchInfo == llvm::AArch64::ARMV9_1A ||
       *ArchInfo == llvm::AArch64::ARMV9_2A)) {
    Features.push_back("+sve");
    Features.push_back("+sve2");
  }

  if ((Split.second.size() &&
       !decodeAArch64Features(D, Split.second, Features, *ArchInfo)))
    return false;

  return true;
}

bool getAArch64ArchFeaturesFromMcpu(const Driver &D, llvm::StringRef Mcpu,
                                    const ArgList &Args,
                                    std::vector<llvm::StringRef> &Features) {
  llvm::StringRef CPU;
  auto McpuLowerCase = Mcpu.lower();
  if (!decodeAArch64Mcpu(D, McpuLowerCase, CPU, Features))
    return false;

  return true;
}

bool getAArch64MicroArchFeaturesFromMtune(
    const Driver &D, llvm::StringRef Mtune, const ArgList &Args,
    std::vector<llvm::StringRef> &Features) {
  auto MtuneLowerCase = Mtune.lower();
  // Check CPU name is valid
  std::vector<llvm::StringRef> MtuneFeatures;
  llvm::StringRef Tune;
  if (!decodeAArch64Mcpu(D, MtuneLowerCase, Tune, MtuneFeatures))
    return false;

  // Handle CPU name is 'native'.
  if (MtuneLowerCase == "native")
    MtuneLowerCase = std::string(llvm::sys::getHostCPUName());
  if (MtuneLowerCase == "cyclone" ||
      llvm::StringRef(MtuneLowerCase).starts_with("apple")) {
    Features.push_back("+zcm");
    Features.push_back("+zcz");
  }
  return true;
}

bool getAArch64MicroArchFeaturesFromMcpu(
    const Driver &D, llvm::StringRef Mcpu, const ArgList &Args,
    std::vector<llvm::StringRef> &Features) {
  llvm::StringRef CPU;
  std::vector<llvm::StringRef> DecodedFeature;
  auto McpuLowerCase = Mcpu.lower();
  if (!decodeAArch64Mcpu(D, McpuLowerCase, CPU, DecodedFeature))
    return false;

  return getAArch64MicroArchFeaturesFromMtune(D, CPU, Args, Features);
}
} // namespace

// ===----------------------------------------------------------------------===
// Target feature collection
// ===----------------------------------------------------------------------===

void aarch64::getAArch64TargetFeatures(const Driver &D,
                                       const llvm::Triple &Triple,
                                       const ArgList &Args,
                                       std::vector<llvm::StringRef> &Features,
                                       bool ForAS) {
  Arg *A;
  bool success = true;
  // Enable NEON by default.
  Features.push_back("+neon");
  llvm::StringRef WaMArch;
  if (ForAS)
    for (const auto *A :
         Args.filtered(options::OPT_Wa_COMMA, options::OPT_Xassembler))
      for (llvm::StringRef Value : A->getValues())
        if (Value.starts_with("-march="))
          WaMArch = Value.substr(7);
  // Call getAArch64ArchFeaturesFromMarch only if "-Wa,-march=" or
  // "-Xassembler -march" is detected. Otherwise it may return false
  // and causes NeverC to error out.
  if (!WaMArch.empty())
    success = getAArch64ArchFeaturesFromMarch(D, WaMArch, Args, Features);
  else if ((A = Args.getLastArg(options::OPT_march_EQ)))
    success = getAArch64ArchFeaturesFromMarch(D, A->getValue(), Args, Features);
  else if ((A = Args.getLastArg(options::OPT_mcpu_EQ)))
    success = getAArch64ArchFeaturesFromMcpu(D, A->getValue(), Args, Features);
  else if (isCPUDeterminedByTriple(Triple))
    success = getAArch64ArchFeaturesFromMcpu(
        D, getAArch64TargetCPU(Args, Triple, A), Args, Features);
  else
    // Default to 'A' profile if the architecture is not specified.
    success = getAArch64ArchFeaturesFromMarch(D, "armv8-a", Args, Features);

  if (success && (A = Args.getLastArg(neverc::driver::options::OPT_mtune_EQ)))
    success =
        getAArch64MicroArchFeaturesFromMtune(D, A->getValue(), Args, Features);
  else if (success && (A = Args.getLastArg(options::OPT_mcpu_EQ)))
    success =
        getAArch64MicroArchFeaturesFromMcpu(D, A->getValue(), Args, Features);
  else if (success && isCPUDeterminedByTriple(Triple))
    success = getAArch64MicroArchFeaturesFromMcpu(
        D, getAArch64TargetCPU(Args, Triple, A), Args, Features);

  if (!success) {
    auto Diag = D.Diag(diag::err_drv_unsupported_option_argument);
    // If "-Wa,-march=" is used, 'WaMArch' will contain the argument's value,
    // while 'A' is uninitialized. Only dereference 'A' in the other case.
    if (!WaMArch.empty())
      Diag << "-march=" << WaMArch;
    else
      Diag << A->getSpelling() << A->getValue();
  }

  if (Args.getLastArg(options::OPT_mgeneral_regs_only)) {
    Features.push_back("-fp-armv8");
    Features.push_back("-crypto");
    Features.push_back("-neon");
  }

  if (Arg *A = Args.getLastArg(options::OPT_mtp_mode_EQ)) {
    llvm::StringRef Mtp = A->getValue();
    if (Mtp == "el3" || Mtp == "tpidr_el3")
      Features.push_back("+tpidr-el3");
    else if (Mtp == "el2" || Mtp == "tpidr_el2")
      Features.push_back("+tpidr-el2");
    else if (Mtp == "el1" || Mtp == "tpidr_el1")
      Features.push_back("+tpidr-el1");
    else if (Mtp == "tpidrro_el0")
      Features.push_back("+tpidrro-el0");
    else if (Mtp != "el0" && Mtp != "tpidr_el0")
      D.Diag(diag::err_drv_invalid_mtp) << A->getAsString(Args);
  }

  // Enable/disable straight line speculation hardening.
  if (Arg *A = Args.getLastArg(options::OPT_mharden_sls_EQ)) {
    llvm::StringRef Scope = A->getValue();
    bool EnableRetBr = false;
    bool EnableBlr = false;
    bool DisableComdat = false;
    if (Scope != "none") {
      llvm::SmallVector<llvm::StringRef, 4> Opts;
      Scope.split(Opts, ",");
      for (auto Opt : Opts) {
        Opt = Opt.trim();
        if (Opt == "all") {
          EnableBlr = true;
          EnableRetBr = true;
          continue;
        }
        if (Opt == "retbr") {
          EnableRetBr = true;
          continue;
        }
        if (Opt == "blr") {
          EnableBlr = true;
          continue;
        }
        if (Opt == "comdat") {
          DisableComdat = false;
          continue;
        }
        if (Opt == "nocomdat") {
          DisableComdat = true;
          continue;
        }
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Scope;
        break;
      }
    }

    if (EnableRetBr)
      Features.push_back("+harden-sls-retbr");
    if (EnableBlr)
      Features.push_back("+harden-sls-blr");
    if (DisableComdat) {
      Features.push_back("+harden-sls-nocomdat");
    }
  }

  if (Args.hasArg(options::OPT_mcrc))
    Features.push_back("+crc");

  int V8Version = -1;
  int V9Version = -1;
  bool HasNoSM4 = false;
  bool HasNoSHA3 = false;
  bool HasNoSHA2 = false;
  bool HasNoAES = false;
  bool HasSM4 = false;
  bool HasSHA3 = false;
  bool HasSHA2 = false;
  bool HasAES = false;
  bool HasCrypto = false;
  bool HasNoCrypto = false;
  int FullFP16Pos = -1;
  int NoFullFP16Pos = -1;
  int FP16FMLPos = -1;
  int NoFP16FMLPos = -1;
  int ArchFeatPos = -1;

  for (auto I = Features.begin(), E = Features.end(); I != E; I++) {
    if (*I == "+v8a")
      V8Version = 0;
    else if (*I == "+v8.1a")
      V8Version = 1;
    else if (*I == "+v8.2a")
      V8Version = 2;
    else if (*I == "+v8.3a")
      V8Version = 3;
    else if (*I == "+v8.4a")
      V8Version = 4;
    else if (*I == "+v8.5a")
      V8Version = 5;
    else if (*I == "+v8.6a")
      V8Version = 6;
    else if (*I == "+v8.7a")
      V8Version = 7;
    else if (*I == "+v8.8a")
      V8Version = 8;
    else if (*I == "+v8.9a")
      V8Version = 9;
    else if (*I == "+v9a")
      V9Version = 0;
    else if (*I == "+v9.1a")
      V9Version = 1;
    else if (*I == "+v9.2a")
      V9Version = 2;
    else if (*I == "+v9.3a")
      V9Version = 3;
    else if (*I == "+v9.4a")
      V9Version = 4;
    else if (*I == "+v9.5a")
      V9Version = 5;
    else if (*I == "+sm4")
      HasSM4 = true;
    else if (*I == "+sha3")
      HasSHA3 = true;
    else if (*I == "+sha2")
      HasSHA2 = true;
    else if (*I == "+aes")
      HasAES = true;
    else if (*I == "-sm4")
      HasNoSM4 = true;
    else if (*I == "-sha3")
      HasNoSHA3 = true;
    else if (*I == "-sha2")
      HasNoSHA2 = true;
    else if (*I == "-aes")
      HasNoAES = true;
    else if (*I == "+fp16fml")
      FP16FMLPos = I - Features.begin();
    else if (*I == "-fp16fml")
      NoFP16FMLPos = I - Features.begin();
    else if (*I == "-fullfp16")
      NoFullFP16Pos = I - Features.begin();
    else if (*I == "+fullfp16")
      FullFP16Pos = I - Features.begin();
    // Whichever option comes after (right-most option) will win
    else if (*I == "+crypto") {
      HasCrypto = true;
      HasNoCrypto = false;
    } else if (*I == "-crypto" || *I == "-neon") {
      HasCrypto = false;
      HasNoCrypto = true;
      HasSM4 = HasSHA2 = HasSHA3 = HasAES = false;
    }
    // Register the iterator position if this is an architecture feature
    if (ArchFeatPos == -1 && (V8Version != -1 || V9Version != -1))
      ArchFeatPos = I - Features.begin();
  }

  // Handle (arch-dependent) fp16fml/fullfp16 relationship.
  if (V8Version >= 4) {
    // "-fullfp16" "+fullfp16" && "+fp16fml" "+fullfp16" && no "+fullfp16"
    // "-fp16fml" = "+fp16fml"
    if (FullFP16Pos > NoFullFP16Pos && FullFP16Pos > FP16FMLPos &&
        FullFP16Pos > NoFP16FMLPos)
      // Only entangled feature that can be to the right of this +fullfp16 is
      // -fp16fml. Only append the +fp16fml if there is no -fp16fml after the
      // +fullfp16.
      Features.push_back("+fp16fml");
    else
      goto fp16_fml_fallthrough;
  } else {
  fp16_fml_fallthrough:
    // In both of these cases, putting the 'other' feature on the end of the
    // vector will result in the same effect as placing it immediately after the
    // current feature.
    // "+fp16fml"  "-fullfp16" = "-fp16fml"
    if (NoFullFP16Pos > FP16FMLPos)
      Features.push_back("-fp16fml");
    // "-fullfp16" "+fp16fml" = "+fullfp16"
    else if (NoFullFP16Pos < FP16FMLPos)
      Features.push_back("+fullfp16");
  }

  // Context sensitive meaning of Crypto:
  // 1) For Arch >= ARMv8.4a:  crypto = sm4 + sha3 + sha2 + aes
  // 2) For Arch <= ARMv8.3a:  crypto = sha2 + aes
  if (V8Version >= 4 || V9Version >= 0) {
    if (HasCrypto && !HasNoCrypto) {
      // Check if we have NOT disabled an algorithm with something like:
      //   +crypto, -algorithm
      // And if "-algorithm" does not occur, we enable that crypto algorithm.
      if (!HasNoSM4)
        Features.push_back("+sm4");
      if (!HasNoSHA3)
        Features.push_back("+sha3");
      if (!HasNoSHA2)
        Features.push_back("+sha2");
      if (!HasNoAES)
        Features.push_back("+aes");
    } else if (HasNoCrypto) {
      // Check if we have NOT enabled a crypto algorithm with something like:
      //   -crypto, +algorithm
      // And if "+algorithm" does not occur, we disable that crypto algorithm.
      if (!HasSM4)
        Features.push_back("-sm4");
      if (!HasSHA3)
        Features.push_back("-sha3");
      if (!HasSHA2)
        Features.push_back("-sha2");
      if (!HasAES)
        Features.push_back("-aes");
    }
  } else {
    if (HasCrypto && !HasNoCrypto) {
      if (!HasNoSHA2)
        Features.push_back("+sha2");
      if (!HasNoAES)
        Features.push_back("+aes");
    } else if (HasNoCrypto) {
      if (!HasSHA2)
        Features.push_back("-sha2");
      if (!HasAES)
        Features.push_back("-aes");
      if (V8Version == 2 || V8Version == 3) {
        Features.push_back("-sm4");
        Features.push_back("-sha3");
      }
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_mno_unaligned_access,
                               options::OPT_munaligned_access)) {
    if (A->getOption().matches(options::OPT_mno_unaligned_access))
      Features.push_back("+strict-align");
  }

  if (Args.hasArg(options::OPT_ffixed_x1))
    Features.push_back("+reserve-x1");

  if (Args.hasArg(options::OPT_ffixed_x2))
    Features.push_back("+reserve-x2");

  if (Args.hasArg(options::OPT_ffixed_x3))
    Features.push_back("+reserve-x3");

  if (Args.hasArg(options::OPT_ffixed_x4))
    Features.push_back("+reserve-x4");

  if (Args.hasArg(options::OPT_ffixed_x5))
    Features.push_back("+reserve-x5");

  if (Args.hasArg(options::OPT_ffixed_x6))
    Features.push_back("+reserve-x6");

  if (Args.hasArg(options::OPT_ffixed_x7))
    Features.push_back("+reserve-x7");

  if (Args.hasArg(options::OPT_ffixed_x9))
    Features.push_back("+reserve-x9");

  if (Args.hasArg(options::OPT_ffixed_x10))
    Features.push_back("+reserve-x10");

  if (Args.hasArg(options::OPT_ffixed_x11))
    Features.push_back("+reserve-x11");

  if (Args.hasArg(options::OPT_ffixed_x12))
    Features.push_back("+reserve-x12");

  if (Args.hasArg(options::OPT_ffixed_x13))
    Features.push_back("+reserve-x13");

  if (Args.hasArg(options::OPT_ffixed_x14))
    Features.push_back("+reserve-x14");

  if (Args.hasArg(options::OPT_ffixed_x15))
    Features.push_back("+reserve-x15");

  if (Args.hasArg(options::OPT_ffixed_x18))
    Features.push_back("+reserve-x18");

  if (Args.hasArg(options::OPT_ffixed_x20))
    Features.push_back("+reserve-x20");

  if (Args.hasArg(options::OPT_ffixed_x21))
    Features.push_back("+reserve-x21");

  if (Args.hasArg(options::OPT_ffixed_x22))
    Features.push_back("+reserve-x22");

  if (Args.hasArg(options::OPT_ffixed_x23))
    Features.push_back("+reserve-x23");

  if (Args.hasArg(options::OPT_ffixed_x24))
    Features.push_back("+reserve-x24");

  if (Args.hasArg(options::OPT_ffixed_x25))
    Features.push_back("+reserve-x25");

  if (Args.hasArg(options::OPT_ffixed_x26))
    Features.push_back("+reserve-x26");

  if (Args.hasArg(options::OPT_ffixed_x27))
    Features.push_back("+reserve-x27");

  if (Args.hasArg(options::OPT_ffixed_x28))
    Features.push_back("+reserve-x28");

  if (Args.hasArg(options::OPT_ffixed_x30))
    Features.push_back("+reserve-x30");

  if (Args.hasArg(options::OPT_fcall_saved_x8))
    Features.push_back("+call-saved-x8");

  if (Args.hasArg(options::OPT_fcall_saved_x9))
    Features.push_back("+call-saved-x9");

  if (Args.hasArg(options::OPT_fcall_saved_x10))
    Features.push_back("+call-saved-x10");

  if (Args.hasArg(options::OPT_fcall_saved_x11))
    Features.push_back("+call-saved-x11");

  if (Args.hasArg(options::OPT_fcall_saved_x12))
    Features.push_back("+call-saved-x12");

  if (Args.hasArg(options::OPT_fcall_saved_x13))
    Features.push_back("+call-saved-x13");

  if (Args.hasArg(options::OPT_fcall_saved_x14))
    Features.push_back("+call-saved-x14");

  if (Args.hasArg(options::OPT_fcall_saved_x15))
    Features.push_back("+call-saved-x15");

  if (Args.hasArg(options::OPT_fcall_saved_x18))
    Features.push_back("+call-saved-x18");

  if (Args.hasArg(options::OPT_mno_neg_immediates))
    Features.push_back("+no-neg-immediates");

  if (Arg *A = Args.getLastArg(options::OPT_mfix_cortex_a53_835769,
                               options::OPT_mno_fix_cortex_a53_835769)) {
    if (A->getOption().matches(options::OPT_mfix_cortex_a53_835769))
      Features.push_back("+fix-cortex-a53-835769");
    else
      Features.push_back("-fix-cortex-a53-835769");
  }

  if (Args.getLastArg(options::OPT_mno_bti_at_return_twice))
    Features.push_back("+no-bti-at-return-twice");
}
