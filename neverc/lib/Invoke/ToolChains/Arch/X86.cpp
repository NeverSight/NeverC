#include "X86.h"
#include "ToolChains/CommonArgs.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/Options.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Option/ArgList.h"
#include "llvm/TargetParser/Host.h"
#include <cassert>

using namespace neverc::driver;
using namespace neverc::driver::tools;
using namespace neverc;
using namespace llvm::opt;

// ===----------------------------------------------------------------------===
// CPU selection
// ===----------------------------------------------------------------------===

std::string x86::getX86TargetCPU(const Driver &D, const ArgList &Args,
                                 const llvm::Triple &Triple) {
  if (const Arg *A = Args.getLastArg(neverc::driver::options::OPT_march_EQ)) {
    llvm::StringRef CPU = A->getValue();
    if (CPU != "native")
      return std::string(CPU);

    llvm::Triple HostTriple(llvm::sys::getProcessTriple());
    if (HostTriple.getArch() != Triple.getArch()) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << Triple.str();
      return "";
    }

    CPU = llvm::sys::getHostCPUName();
    if (!CPU.empty() && CPU != "generic")
      return std::string(CPU);
  }

  if (const Arg *A = Args.getLastArg(options::OPT_msvc_arch)) {
    // Mapping built by looking at lib/Basic's X86TargetInfo::initFeatureMap().
    // The keys are case-sensitive; this matches link.exe (x86-64).
    llvm::StringMap<llvm::StringRef> ArchMap({
        {"AVX", "sandybridge"},
        {"AVX2", "haswell"},
        {"AVX512F", "knl"},
        {"AVX512", "skylake-avx512"},
    });
    llvm::StringRef CPU = ArchMap.lookup(A->getValue());
    if (CPU.empty()) {
      std::vector<llvm::StringRef> ValidArchs{ArchMap.keys().begin(),
                                              ArchMap.keys().end()};
      sort(ValidArchs);
      D.Diag(diag::warn_drv_invalid_arch_name_with_suggestion)
          << A->getValue() << false << join(ValidArchs, ", ");
    }
    return std::string(CPU);
  }

  // Select the default CPU if none was given (or detection failed).
  assert(Triple.getArch() == llvm::Triple::x86_64 &&
         "getX86TargetCPU is only used for x86-64");

  if (Triple.isOSDarwin()) {
    // macosx10.12 drops support for all pre-Penryn Macs.
    // Simulators can still run on 10.11 though, like Xcode.
    if (Triple.isMacOSX() && !Triple.isOSVersionLT(10, 12))
      return "penryn";

    return "core2";
  }

  // Non-Darwin x86-64 defaults (Linux and other supported OSes).
  return "x86-64";
}

// ===----------------------------------------------------------------------===
// Target feature collection
// ===----------------------------------------------------------------------===

void x86::getX86TargetFeatures(const Driver &D, const llvm::Triple &Triple,
                               const ArgList &Args,
                               std::vector<llvm::StringRef> &Features) {
  // Claim and report unsupported -mabi=. Note: we don't support "sysv_abi" or
  // "ms_abi" as default function attributes.
  if (const Arg *A = Args.getLastArg(neverc::driver::options::OPT_mabi_EQ)) {
    llvm::StringRef DefaultAbi = Triple.isOSWindows() ? "ms" : "sysv";
    if (A->getValue() != DefaultAbi)
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << Triple.getTriple();
  }

  // If -march=native, autodetect the feature list.
  if (const Arg *A = Args.getLastArg(neverc::driver::options::OPT_march_EQ)) {
    if (llvm::StringRef(A->getValue()) == "native") {
      if (llvm::Triple(llvm::sys::getProcessTriple()).getArch() !=
          Triple.getArch())
        return;
      llvm::StringMap<bool> HostFeatures;
      if (llvm::sys::getHostCPUFeatures(HostFeatures))
        for (auto &F : HostFeatures)
          Features.push_back(
              Args.MakeArgString((F.second ? "+" : "-") + F.first()));
    }
  }

  // Translate the high level `-mretpoline` flag to the specific target feature
  // flags. We also detect if the user asked for retpoline external thunks but
  // failed to ask for retpolines themselves (through any of the different
  // flags). This is a bit hacky but keeps existing usages working. We should
  // consider deprecating this and instead warn if the user requests external
  // retpoline thunks and *doesn't* request some form of retpolines.
  auto SpectreOpt = neverc::driver::options::ID::OPT_INVALID;
  if (Args.hasArgNoClaim(options::OPT_mretpoline, options::OPT_mno_retpoline,
                         options::OPT_mspeculative_load_hardening,
                         options::OPT_mno_speculative_load_hardening)) {
    if (Args.hasFlag(options::OPT_mretpoline, options::OPT_mno_retpoline,
                     false)) {
      Features.push_back("+retpoline-indirect-calls");
      Features.push_back("+retpoline-indirect-branches");
      SpectreOpt = options::OPT_mretpoline;
    } else if (Args.hasFlag(options::OPT_mspeculative_load_hardening,
                            options::OPT_mno_speculative_load_hardening,
                            false)) {
      // On x86, speculative load hardening relies on at least using retpolines
      // for indirect calls.
      Features.push_back("+retpoline-indirect-calls");
      SpectreOpt = options::OPT_mspeculative_load_hardening;
    }
  } else if (Args.hasFlag(options::OPT_mretpoline_external_thunk,
                          options::OPT_mno_retpoline_external_thunk, false)) {
    Features.push_back("+retpoline-indirect-calls");
    Features.push_back("+retpoline-indirect-branches");
    SpectreOpt = options::OPT_mretpoline_external_thunk;
  }

  auto LVIOpt = neverc::driver::options::ID::OPT_INVALID;
  if (Args.hasFlag(options::OPT_mlvi_hardening, options::OPT_mno_lvi_hardening,
                   false)) {
    Features.push_back("+lvi-load-hardening");
    Features.push_back("+lvi-cfi"); // load hardening implies CFI protection
    LVIOpt = options::OPT_mlvi_hardening;
  } else if (Args.hasFlag(options::OPT_mlvi_cfi, options::OPT_mno_lvi_cfi,
                          false)) {
    Features.push_back("+lvi-cfi");
    LVIOpt = options::OPT_mlvi_cfi;
  }

  if (Args.hasFlag(options::OPT_m_seses, options::OPT_mno_seses, false)) {
    if (LVIOpt == options::OPT_mlvi_hardening)
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << D.getOpts().getOptionName(options::OPT_mlvi_hardening)
          << D.getOpts().getOptionName(options::OPT_m_seses);

    if (SpectreOpt != neverc::driver::options::ID::OPT_INVALID)
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << D.getOpts().getOptionName(SpectreOpt)
          << D.getOpts().getOptionName(options::OPT_m_seses);

    Features.push_back("+seses");
    if (!Args.hasArg(options::OPT_mno_lvi_cfi)) {
      Features.push_back("+lvi-cfi");
      LVIOpt = options::OPT_mlvi_cfi;
    }
  }

  if (SpectreOpt != neverc::driver::options::ID::OPT_INVALID &&
      LVIOpt != neverc::driver::options::ID::OPT_INVALID) {
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << D.getOpts().getOptionName(SpectreOpt)
        << D.getOpts().getOptionName(LVIOpt);
  }

  for (const Arg *A : Args.filtered(options::OPT_m_x86_AVX10_Features_Group)) {
    llvm::StringRef Name = A->getOption().getName();
    A->claim();

    // Skip over "-m".
    assert(Name.starts_with("m") && "Invalid feature name.");
    Name = Name.substr(1);

    bool IsNegative = Name.consume_front("no-");

#ifndef NDEBUG
    assert(Name.starts_with("avx10.") && "Invalid AVX10 feature name.");
    llvm::StringRef Version, Width;
    std::tie(Version, Width) = Name.substr(6).split('-');
    assert(Version == "1" && "Invalid AVX10 feature name.");
    assert((Width == "256" || Width == "512") && "Invalid AVX10 feature name.");
#endif

    Features.push_back(Args.MakeArgString((IsNegative ? "-" : "+") + Name));
  }

  // Now add any that the user explicitly requested on the command line,
  // which may override the defaults.
  for (const Arg *A : Args.filtered(options::OPT_m_x86_Features_Group,
                                    options::OPT_mgeneral_regs_only)) {
    llvm::StringRef Name = A->getOption().getName();
    A->claim();

    // Skip over "-m".
    assert(Name.starts_with("m") && "Invalid feature name.");
    Name = Name.substr(1);

    // Replace -mgeneral-regs-only with -x87, -mmx, -sse
    if (A->getOption().getID() == options::OPT_mgeneral_regs_only) {
      Features.insert(Features.end(), {"-x87", "-mmx", "-sse"});
      continue;
    }

    bool IsNegative = Name.starts_with("no-");
    if (A->getOption().matches(options::OPT_mapx_features_EQ) ||
        A->getOption().matches(options::OPT_mno_apx_features_EQ)) {

      for (llvm::StringRef Value : A->getValues()) {
        if (Value == "egpr" || Value == "push2pop2" || Value == "ppx" ||
            Value == "ndd" || Value == "ccmp" || Value == "cf") {
          Features.push_back(
              Args.MakeArgString((IsNegative ? "-" : "+") + Value));
          continue;
        }
        D.Diag(neverc::diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Value;
      }
      continue;
    }
    if (IsNegative)
      Name = Name.substr(3);
    Features.push_back(Args.MakeArgString((IsNegative ? "-" : "+") + Name));
  }

  // Enable/disable straight line speculation hardening.
  if (Arg *A = Args.getLastArg(options::OPT_mharden_sls_EQ)) {
    llvm::StringRef Scope = A->getValue();
    if (Scope == "all") {
      Features.push_back("+harden-sls-ijmp");
      Features.push_back("+harden-sls-ret");
    } else if (Scope == "return") {
      Features.push_back("+harden-sls-ret");
    } else if (Scope == "indirect-jmp") {
      Features.push_back("+harden-sls-ijmp");
    } else if (Scope != "none") {
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Scope;
    }
  }

  // -mno-gather, -mno-scatter support
  if (Args.hasArg(options::OPT_mno_gather))
    Features.push_back("+prefer-no-gather");
  if (Args.hasArg(options::OPT_mno_scatter))
    Features.push_back("+prefer-no-scatter");
}
