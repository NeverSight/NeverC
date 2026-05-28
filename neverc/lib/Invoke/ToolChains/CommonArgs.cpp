#include "CommonArgs.h"
#include "Arch/AArch64.h"
#include "Arch/X86.h"
#include "neverc/Config/config.h"
#include "neverc/Foundation/Core/Version.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/InputInfo.h"
#include "neverc/Invoke/Job.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Invoke/ToolChain.h"
#include "neverc/Invoke/Util.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/TargetParser.h"

using namespace neverc::driver;
using namespace neverc::driver::tools;
using namespace neverc;
using namespace llvm::opt;

// ===----------------------------------------------------------------------===
// Frame pointer configuration
// ===----------------------------------------------------------------------===

namespace {
bool useFramePointerForTargetByDefault(const llvm::opt::ArgList &Args,
                                       const llvm::Triple &Triple) {
  if (Triple.isOSLinux()) {
    switch (Triple.getArch()) {
    case llvm::Triple::x86_64:
    case llvm::Triple::aarch64:
      return !neverc::driver::tools::areOptimizationsEnabled(Args);
    default:
      return true;
    }
  }

  if (Triple.isOSWindows()) {
    switch (Triple.getArch()) {
    case llvm::Triple::x86_64:
      return Triple.isOSBinFormatMachO();
    default:
      return false;
    }
  }

  return true;
}
} // namespace

neverc::CodeGenOptions::FramePointerKind
getFramePointerKind(const llvm::opt::ArgList &Args,
                    const llvm::Triple &Triple) {
  // We have 4 states:
  //
  //  00) leaf retained, non-leaf retained
  //  01) leaf retained, non-leaf omitted (this is invalid)
  //  10) leaf omitted, non-leaf retained
  //      (what -momit-leaf-frame-pointer was designed for)
  //  11) leaf omitted, non-leaf omitted
  //
  //  "omit" options taking precedence over "no-omit" options is the only way
  //  to make 3 valid states representable
  llvm::opt::Arg *A =
      Args.getLastArg(neverc::driver::options::OPT_fomit_frame_pointer,
                      neverc::driver::options::OPT_fno_omit_frame_pointer);

  bool OmitFP = A && A->getOption().matches(
                         neverc::driver::options::OPT_fomit_frame_pointer);
  bool NoOmitFP = A && A->getOption().matches(
                           neverc::driver::options::OPT_fno_omit_frame_pointer);
  bool OmitLeafFP =
      Args.hasFlag(neverc::driver::options::OPT_momit_leaf_frame_pointer,
                   neverc::driver::options::OPT_mno_omit_leaf_frame_pointer,
                   Triple.isAArch64());
  if (NoOmitFP ||
      (!OmitFP && useFramePointerForTargetByDefault(Args, Triple))) {
    if (OmitLeafFP)
      return neverc::CodeGenOptions::FramePointerKind::NonLeaf;
    return neverc::CodeGenOptions::FramePointerKind::All;
  }
  return neverc::CodeGenOptions::FramePointerKind::None;
}

// ===----------------------------------------------------------------------===
// Path & target feature utilities
// ===----------------------------------------------------------------------===

void tools::addPathIfExists(const Driver &D, const llvm::Twine &Path,
                            ToolChain::path_list &Paths) {
  if (D.getVFS().exists(Path))
    Paths.push_back(Path.str());
}

void tools::handleTargetFeaturesGroup(const Driver &D,
                                      const llvm::Triple &Triple,
                                      const ArgList &Args,
                                      std::vector<llvm::StringRef> &Features,
                                      OptSpecifier Group) {
  for (const Arg *A : Args.filtered(Group)) {
    llvm::StringRef Name = A->getOption().getName();
    A->claim();

    assert(Name.starts_with("m") && "Invalid feature name.");
    Name = Name.substr(1);

    bool IsNegative = Name.starts_with("no-");
    if (IsNegative)
      Name = Name.substr(3);

    Features.push_back(Args.MakeArgString((IsNegative ? "-" : "+") + Name));
  }
}

llvm::SmallVector<llvm::StringRef>
tools::unifyTargetFeatures(llvm::ArrayRef<llvm::StringRef> Features) {
  // Only add a feature if it hasn't been seen before starting from the end.
  llvm::SmallVector<llvm::StringRef> UnifiedFeatures;
  llvm::DenseSet<llvm::StringRef> UsedFeatures;
  for (llvm::StringRef Feature : llvm::reverse(Features)) {
    if (UsedFeatures.insert(Feature.drop_front()).second)
      UnifiedFeatures.insert(UnifiedFeatures.begin(), Feature);
  }

  return UnifiedFeatures;
}

void tools::addDirectoryList(const ArgList &Args, ArgStringList &CmdArgs,
                             const char *ArgName, const char *EnvVar) {
  const char *DirList = ::getenv(EnvVar);
  bool CombinedArg = false;

  if (!DirList)
    return; // Nothing to do.

  llvm::StringRef Name(ArgName);
  if (Name.equals("-I") || Name.equals("-L") || Name.empty())
    CombinedArg = true;

  llvm::StringRef Dirs(DirList);
  if (Dirs.empty()) // Empty string should not add '.'.
    return;

  llvm::StringRef::size_type Delim;
  while ((Delim = Dirs.find(llvm::sys::EnvPathSeparator)) !=
         llvm::StringRef::npos) {
    if (Delim == 0) { // Leading colon.
      if (CombinedArg) {
        CmdArgs.push_back(Args.MakeArgString(std::string(ArgName) + "."));
      } else {
        CmdArgs.push_back(ArgName);
        CmdArgs.push_back(".");
      }
    } else {
      if (CombinedArg) {
        CmdArgs.push_back(
            Args.MakeArgString(std::string(ArgName) + Dirs.substr(0, Delim)));
      } else {
        CmdArgs.push_back(ArgName);
        CmdArgs.push_back(Args.MakeArgString(Dirs.substr(0, Delim)));
      }
    }
    Dirs = Dirs.substr(Delim + 1);
  }

  if (Dirs.empty()) { // Trailing colon.
    if (CombinedArg) {
      CmdArgs.push_back(Args.MakeArgString(std::string(ArgName) + "."));
    } else {
      CmdArgs.push_back(ArgName);
      CmdArgs.push_back(".");
    }
  } else { // Add the last path.
    if (CombinedArg) {
      CmdArgs.push_back(Args.MakeArgString(std::string(ArgName) + Dirs));
    } else {
      CmdArgs.push_back(ArgName);
      CmdArgs.push_back(Args.MakeArgString(Dirs));
    }
  }
}

void tools::AddLinkerInputs(const ToolChain &TC, const InputInfoList &Inputs,
                            const ArgList &Args, ArgStringList &CmdArgs,
                            const JobAction &JA) {
  const Driver &D = TC.getDriver();

  // Add extra linker input arguments which are not treated as inputs
  // (constructed via -Xarch_).
  Args.AddAllArgValues(CmdArgs, options::OPT_Zlinker_input);

  // LIBRARY_PATH are included before user inputs and only supported on native
  // toolchains.
  if (!TC.isCrossCompiling())
    addDirectoryList(Args, CmdArgs, "-L", "LIBRARY_PATH");

  for (const auto &II : Inputs) {
    if (!TC.HasNativeLLVMSupport() && types::isLLVMIR(II.getType()))
      // Don't try to pass LLVM inputs unless we have native support.
      D.Diag(diag::err_drv_no_linker_llvm_support) << TC.getTripleString();

    // Add filenames immediately.
    if (II.isFilename()) {
      CmdArgs.push_back(II.getFilename());
      continue;
    }

    // In some error cases, the input could be Nothing; skip those.
    if (II.isNothing())
      continue;

    // Otherwise, this is a linker input argument.
    const Arg &A = II.getInputArg();

    A.renderAsInput(Args, CmdArgs);
  }
}

void tools::AddTargetFeature(const ArgList &Args,
                             std::vector<llvm::StringRef> &Features,
                             OptSpecifier OnOpt, OptSpecifier OffOpt,
                             llvm::StringRef FeatureName) {
  if (Arg *A = Args.getLastArg(OnOpt, OffOpt)) {
    if (A->getOption().matches(OnOpt))
      Features.push_back(Args.MakeArgString("+" + FeatureName));
    else
      Features.push_back(Args.MakeArgString("-" + FeatureName));
  }
}

std::string tools::getCPUName(const Driver &D, const ArgList &Args,
                              const llvm::Triple &T, bool FromAs) {
  Arg *A;

  switch (T.getArch()) {
  default:
    return "";

  case llvm::Triple::aarch64:
    return aarch64::getAArch64TargetCPU(Args, T, A);

  case llvm::Triple::x86_64:
    return x86::getX86TargetCPU(D, Args, T);
  }
}

void tools::getTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                              const ArgList &Args, ArgStringList &CmdArgs,
                              bool ForAS) {
  std::vector<llvm::StringRef> Features;
  switch (Triple.getArch()) {
  default:
    break;
  case llvm::Triple::aarch64:
    aarch64::getAArch64TargetFeatures(D, Triple, Args, Features, ForAS);
    break;
  case llvm::Triple::x86_64:
    x86::getX86TargetFeatures(D, Triple, Args, Features);
    break;
  }

  for (auto Feature : unifyTargetFeatures(Features)) {
    CmdArgs.push_back("-target-feature");
    CmdArgs.push_back(Feature.data());
  }
}

// ===----------------------------------------------------------------------===
// Linker driver configuration
// ===----------------------------------------------------------------------===

void tools::populateLinkerDriverConfig(const ToolChain &TC,
                                       const llvm::opt::ArgList &Args,
                                       ::linker::LinkerDriverConfig &Cfg) {
  const Driver &D = TC.getDriver();

  Cfg.saveTemps = D.isSaveTempsEnabled();
  Cfg.timeTraceEnabled =
      Args.hasArg(options::OPT_ftime_trace, options::OPT_ftime_trace_EQ);

  if (const Arg *A = Args.getLastArg(options::OPT_ftime_trace_granularity_EQ)) {
    unsigned V = 0;
    llvm::StringRef(A->getValue()).getAsInteger(10, V);
    if (V)
      Cfg.timeTraceGranularity = V;
  }

  if (const Arg *A = Args.getLastArg(options::OPT_ferror_limit_EQ)) {
    unsigned V = 0;
    llvm::StringRef(A->getValue()).getAsInteger(10, V);
    Cfg.errorLimit = V;
  }

  Cfg.verbose = Args.hasArg(options::OPT_v);
  Cfg.demangle = true;
  Cfg.nostdlib = Args.hasArg(options::OPT_nostdlib);
  Cfg.suppressWarnings = Args.hasArg(options::OPT_w);
  for (const Arg *A : Args.filtered(options::OPT_W_Joined)) {
    llvm::StringRef Val = A->getValue();
    if (Val == "error")
      Cfg.fatalWarnings = true;
    else if (Val == "no-error")
      Cfg.fatalWarnings = false;
  }
  Cfg.sysroot = D.SysRoot;

  if (const Arg *A = Args.getLastArg(options::OPT_gz_EQ)) {
    llvm::StringRef V = A->getValue();
    if (V == "none" || V == "zlib" || V == "zstd")
      Cfg.compressDebugSections = V.str();
  }
  // ltoPartitions > 1 enables our ParallelCodeGenHook / ParallelOptCodeGenHook
  // in the LTO backend.  The hooks auto-detect the actual partition count
  // internally via hardware_concurrency(), so we only need the minimum value
  // that triggers the hook path.  This also minimizes buf.resize(maxTasks)
  // allocation in compile() — only 2 SmallVector<char,0> instead of N.
  Cfg.ltoPartitions = 2;

  // Optimization levels derived from -O.
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    int Level = 2;
    if (A->getOption().matches(options::OPT_O4) ||
        A->getOption().matches(options::OPT_Ofast))
      Level = 3;
    else if (A->getOption().matches(options::OPT_O)) {
      llvm::StringRef V = A->getValue();
      if (V == "g")
        Level = 1;
      else if (V == "s" || V == "z")
        Level = 2;
      else
        V.getAsInteger(10, Level);
    } else if (A->getOption().matches(options::OPT_O0))
      Level = 0;
    Cfg.ltoOptLevel = Level;
    // For auto-LTO (user did not explicitly ask for -flto), cap LTO link
    // optimization at O2: O3 in LTO link adds ~15-20% link time on
    // medium-size projects (Redis ~26K IR insts: +600ms) for ~2% binary
    // shrink, which is a poor tradeoff for the default. Users wanting full
    // O3 LTO can pass -flto explicitly.
    if (Level > 2 && TC.getDriver().isAutoLTO())
      Cfg.ltoOptLevel = 2;
    // Linker output optimization (ICF, section merge, string dedup) caps at 2.
    Cfg.linkerOptLevel = Level > 2 ? 2 : Level;
  }

  // -fbasic-block-sections= and -funique-basic-block-section-names forwarded
  // to LTO codegen, replacing --lto-basic-block-sections= on the linker CLI.
  if (const Arg *A = Args.getLastArg(options::OPT_fbasic_block_sections_EQ))
    Cfg.ltoBasicBlockSections = A->getValue();
  Cfg.ltoUniqueBasicBlockSectionNames =
      Args.hasFlag(options::OPT_funique_basic_block_section_names,
                   options::OPT_fno_unique_basic_block_section_names, false);

  // LLVM options for LTO codegen, previously passed via --mllvm on the
  // synthetic linker command line.
  // Target CPU — pass directly to LinkerDriverConfig so LTO codegen
  // uses it without a cl::opt round-trip through mllvmOpts.
  Cfg.cpu = getCPUName(D, Args, TC.getTriple());

  // Options with direct TargetOptions mappings — skip the cl::opt
  // string round-trip and set named fields on LinkerDriverConfig.
  if (Arg *A = Args.getLastArg(options::OPT_fglobal_isel,
                               options::OPT_fno_global_isel))
    Cfg.globalISel = A->getOption().matches(options::OPT_fglobal_isel) ? 1 : 0;

  if (Arg *A =
          Args.getLastArg(options::OPT_gTune_Group, options::OPT_ggdbN_Group))
    Cfg.debuggerTuning = A->getOption().matches(options::OPT_glldb) ? 2 : 1;

  if (auto *A = Args.getLastArg(options::OPT_fsplit_machine_functions,
                                options::OPT_fno_split_machine_functions))
    Cfg.splitMachineFunctions =
        A->getOption().matches(options::OPT_fsplit_machine_functions);

  if (Args.hasFlag(options::OPT_fjmc, options::OPT_fno_jmc, false) &&
      TC.getEffectiveTriple().isOSBinFormatELF())
    Cfg.jmcInstrument = true;

  Cfg.emulatedTLS =
      Args.hasFlag(options::OPT_femulated_tls, options::OPT_fno_emulated_tls,
                   TC.getTriple().hasDefaultEmulatedTLS());

  Cfg.stackSizeSection =
      Args.hasFlag(options::OPT_fstack_size_section,
                   options::OPT_fno_stack_size_section, false);

  {
    auto &M = Cfg.mllvmOpts;

    if (Args.hasArg(options::OPT_gdwarf_aranges))
      M.emplace_back("-generate-arange-section");

    if (Arg *A = Args.getLastArg(options::OPT_fcrash_diagnostics_dir))
      M.emplace_back(std::string("-crash-diagnostics-dir=") + A->getValue());

    // -Rpass / -Rpass-missed / -Rpass-analysis
    if (const Arg *A = Args.getLastArg(options::OPT_Rpass_EQ))
      M.emplace_back(std::string("-pass-remarks=") + A->getValue());
    if (const Arg *A = Args.getLastArg(options::OPT_Rpass_missed_EQ))
      M.emplace_back(std::string("-pass-remarks-missed=") + A->getValue());
    if (const Arg *A = Args.getLastArg(options::OPT_Rpass_analysis_EQ))
      M.emplace_back(std::string("-pass-remarks-analysis=") + A->getValue());

    // x86 branch alignment
    if (Args.hasArg(options::OPT_mbranches_within_32B_boundaries))
      M.emplace_back("-x86-branches-within-32B-boundaries");
    if (const Arg *A =
            Args.getLastArg(options::OPT_malign_branch_boundary_EQ)) {
      unsigned Boundary;
      if (!llvm::StringRef(A->getValue()).getAsInteger(10, Boundary) &&
          Boundary >= 16 && llvm::isPowerOf2_64(Boundary))
        M.emplace_back("-x86-align-branch-boundary=" +
                       std::to_string(Boundary));
    }
    if (const Arg *A = Args.getLastArg(options::OPT_malign_branch_EQ)) {
      std::string AlignBranch;
      for (llvm::StringRef T : A->getValues()) {
        if (!AlignBranch.empty())
          AlignBranch += '+';
        AlignBranch += T;
      }
      M.emplace_back("-x86-align-branch=" + AlignBranch);
    }
    if (const Arg *A = Args.getLastArg(options::OPT_mpad_max_prefix_size_EQ)) {
      unsigned PrefixSize;
      if (!llvm::StringRef(A->getValue()).getAsInteger(10, PrefixSize))
        M.emplace_back("-x86-pad-max-prefix-size=" +
                       std::to_string(PrefixSize));
    }

    // Machine outliner
    if (Arg *A =
            Args.getLastArg(options::OPT_moutline, options::OPT_mno_outline)) {
      if (A->getOption().matches(options::OPT_moutline)) {
        if (TC.getTriple().isAArch64())
          M.emplace_back("-enable-machine-outliner");
      } else {
        M.emplace_back("-enable-machine-outliner=never");
      }
    }
  }

  // Optimization remarks for LTO link.
  if (willEmitRemarks(Args)) {
    llvm::StringRef Format = "yaml";
    if (const Arg *A =
            Args.getLastArg(options::OPT_fsave_optimization_record_EQ))
      Format = A->getValue();
    Cfg.optRemarksFormat = Format.str();

    if (const Arg *A =
            Args.getLastArg(options::OPT_foptimization_record_file_EQ))
      Cfg.optRemarksFilename = A->getValue();

    if (const Arg *A =
            Args.getLastArg(options::OPT_foptimization_record_passes_EQ))
      Cfg.optRemarksPasses = A->getValue();
  }

  if (Args.hasFlag(options::OPT_fdiagnostics_show_hotness,
                   options::OPT_fno_diagnostics_show_hotness, false))
    Cfg.optRemarksWithHotness = true;

  if (const Arg *A =
          Args.getLastArg(options::OPT_fdiagnostics_hotness_threshold_EQ))
    Cfg.optRemarksHotnessThreshold = A->getValue();

  // Pass plugins forwarded to LTO.
  for (const Arg *A : Args.filtered(options::OPT_fpass_plugin_EQ))
    Cfg.passPlugins.emplace_back(A->getValue());

  // Unified linker-level options derived from compiler flags.
  // gc-sections on for -O1+; ICF safe for -O2, all for -O3.
  bool Optimized = areOptimizationsEnabled(Args);
  Cfg.gcSections = Optimized;
  Cfg.ehFrameHdr = true;
  if (Cfg.linkerOptLevel >= 2)
    Cfg.icfLevel = (Cfg.linkerOptLevel >= 3) ? 2 : 1;
  Cfg.buildId = "fast";

  if (TC.getTriple().isOSLinux())
    Cfg.hashStyle = "gnu";

  if (Args.hasArg(options::OPT_s))
    Cfg.stripLevel = 2;
  if (TC.getDriver().isAutoLTO())
    Cfg.stripLocals = true;
  Cfg.traceFiles = Args.hasArg(options::OPT_t);

  // Explicit overrides for linker optimization defaults.
  if (Arg *A = Args.getLastArg(options::OPT_fgc_sections,
                               options::OPT_fno_gc_sections))
    Cfg.gcSections = A->getOption().matches(options::OPT_fgc_sections);

  if (const Arg *A = Args.getLastArg(options::OPT_ficf_EQ)) {
    llvm::StringRef V = A->getValue();
    if (V == "none")
      Cfg.icfLevel = 0;
    else if (V == "safe")
      Cfg.icfLevel = 1;
    else if (V == "all")
      Cfg.icfLevel = 2;
    else
      D.Diag(diag::err_drv_invalid_value) << A->getAsString(Args) << V;
  }

  if (const Arg *A = Args.getLastArg(options::OPT_fbuild_id_EQ))
    Cfg.buildId = A->getValue();
  if (Args.hasArg(options::OPT_fno_build_id))
    Cfg.buildId.clear();

  // Link map output.
  if (const Arg *A = Args.getLastArg(options::OPT_flinker_map_EQ))
    Cfg.mapFile = A->getValue();

  // Diagnostic output for gc/icf.
  Cfg.printGCSections = Args.hasFlag(options::OPT_fprint_gc_sections,
                                     options::OPT_fno_print_gc_sections, false);
  Cfg.printICFSections =
      Args.hasFlag(options::OPT_fprint_icf_sections,
                   options::OPT_fno_print_icf_sections, false);

  // Call-graph profile-guided section reordering.
  if (Args.hasArg(options::OPT_fno_call_graph_profile_sort))
    Cfg.callGraphProfileSort = "none";
  if (const Arg *A = Args.getLastArg(options::OPT_fcall_graph_profile_sort_EQ))
    Cfg.callGraphProfileSort = A->getValue();
  if (const Arg *A = Args.getLastArg(options::OPT_fcall_graph_ordering_file_EQ))
    Cfg.callGraphOrderingFile = A->getValue();
  if (const Arg *A = Args.getLastArg(options::OPT_fprint_symbol_order_EQ))
    Cfg.printSymbolOrder = A->getValue();
}

// ===----------------------------------------------------------------------===
// Linking & optimization utilities
// ===----------------------------------------------------------------------===

void tools::addAsNeededOption(llvm::opt::ArgStringList &CmdArgs,
                              bool as_needed) {
  CmdArgs.push_back(as_needed ? "--as-needed" : "--no-as-needed");
}

bool tools::areOptimizationsEnabled(const ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_O_Group))
    return !A->getOption().matches(options::OPT_O0);
  // Defaults to -O0.
  return false;
}

const char *tools::SplitDebugName(const JobAction &JA, const ArgList &Args,
                                  const InputInfo &Input,
                                  const InputInfo &Output) {
  auto AddPostfix = [](auto &F) { F += ".dwo"; };
  if (Arg *A = Args.getLastArg(options::OPT_gsplit_dwarf_EQ))
    if (llvm::StringRef(A->getValue()) == "single" && Output.isFilename())
      return Args.MakeArgString(Output.getFilename());

  llvm::SmallString<128> T;
  if (const Arg *A = Args.getLastArg(options::OPT_dumpdir)) {
    T = A->getValue();
  } else {
    Arg *FinalOutput = Args.getLastArg(options::OPT_o);
    if (FinalOutput && Args.hasArg(options::OPT_c)) {
      T = FinalOutput->getValue();
      llvm::sys::path::remove_filename(T);
      llvm::sys::path::append(T,
                              llvm::sys::path::stem(FinalOutput->getValue()));
      AddPostfix(T);
      return Args.MakeArgString(T);
    }
  }

  T += llvm::sys::path::stem(Input.getBaseInput());
  AddPostfix(T);
  return Args.MakeArgString(T);
}

void tools::SplitDebugInfo(const ToolChain &TC, Compilation &C, const Tool &T,
                           const JobAction &JA, const ArgList &Args,
                           const InputInfo &Output, const char *OutFile) {
  ArgStringList ExtractArgs;
  ExtractArgs.push_back("--extract-dwo");

  ArgStringList StripArgs;
  StripArgs.push_back("--strip-dwo");

  // Grabbing the output of the earlier compile step.
  StripArgs.push_back(Output.getFilename());
  ExtractArgs.push_back(Output.getFilename());
  ExtractArgs.push_back(OutFile);

  const char *Exec =
      Args.MakeArgString(TC.GetProgramPath(NEVERC_DEFAULT_OBJECT_COPY_TOOL));
  InputInfo II(types::TY_Object, Output.getFilename(), Output.getFilename());

  // First extract the dwo sections.
  C.addCommand(std::make_unique<Command>(JA, T,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, ExtractArgs, II, Output));

  // Then remove them from the original .o file.
  C.addCommand(std::make_unique<Command>(
      JA, T, ResponseFileSupport::AtFileCurCP(), Exec, StripArgs, II, Output));
}

// Claim options we don't want to warn if they are unused. We do this for
// options that build systems might add but are unused when assembling or only
// running the preprocessor for example.
void tools::claimNoWarnArgs(const ArgList &Args) {
  // Don't warn about unused -f(no-)?lto.  This can happen when we're
  // preprocessing, precompiling or assembling.
  Args.ClaimAllArgs(options::OPT_flto_EQ);
  Args.ClaimAllArgs(options::OPT_flto);
  Args.ClaimAllArgs(options::OPT_fno_lto);
}

// `-falign-functions` indicates that the functions should be aligned to a
// 16-byte boundary.
//
// `-falign-functions=1` is the same as `-fno-align-functions`.
//
// The scalar `n` in `-falign-functions=n` must be an integral value between
// [0, 65536].  If the value is not a power-of-two, it will be rounded up to
// the nearest power-of-two.
//
// If we return `0`, the frontend will default to the backend's preferred
// alignment.
//
// NOTE: icc only allows values between [0, 4096].  icc uses `-falign-functions`
// to mean `-falign-functions=16`.  GCC defaults to the backend's preferred
// alignment.  For unaligned functions, we default to the backend's preferred
// alignment.
unsigned tools::ParseFunctionAlignment(const ToolChain &TC,
                                       const ArgList &Args) {
  const Arg *A = Args.getLastArg(options::OPT_falign_functions,
                                 options::OPT_falign_functions_EQ,
                                 options::OPT_fno_align_functions);
  if (!A || A->getOption().matches(options::OPT_fno_align_functions))
    return 0;

  if (A->getOption().matches(options::OPT_falign_functions))
    return 0;

  unsigned Value = 0;
  if (llvm::StringRef(A->getValue()).getAsInteger(10, Value) || Value > 65536)
    TC.getDriver().Diag(diag::err_drv_invalid_int_value)
        << A->getAsString(Args) << A->getValue();
  return Value ? llvm::Log2_32_Ceil(std::min(Value, 65536u)) : Value;
}

void tools::addDebugInfoKind(
    ArgStringList &CmdArgs, llvm::codegenoptions::DebugInfoKind DebugInfoKind) {
  switch (DebugInfoKind) {
  case llvm::codegenoptions::DebugDirectivesOnly:
    CmdArgs.push_back("-debug-info-kind=line-directives-only");
    break;
  case llvm::codegenoptions::DebugLineTablesOnly:
    CmdArgs.push_back("-debug-info-kind=line-tables-only");
    break;
  case llvm::codegenoptions::DebugInfoConstructor:
    CmdArgs.push_back("-debug-info-kind=constructor");
    break;
  case llvm::codegenoptions::LimitedDebugInfo:
    CmdArgs.push_back("-debug-info-kind=limited");
    break;
  case llvm::codegenoptions::FullDebugInfo:
    CmdArgs.push_back("-debug-info-kind=standalone");
    break;
  case llvm::codegenoptions::UnusedTypeInfo:
    CmdArgs.push_back("-debug-info-kind=unused-types");
    break;
  default:
    break;
  }
}

// Convert an arg of the form "-gN" or "-ggdbN" or one of their aliases
// to the corresponding DebugInfoKind.
llvm::codegenoptions::DebugInfoKind tools::debugLevelToInfoKind(const Arg &A) {
  assert(A.getOption().matches(options::OPT_gN_Group) &&
         "Not a -g option that specifies a debug-info level");
  if (A.getOption().matches(options::OPT_g0) ||
      A.getOption().matches(options::OPT_ggdb0))
    return llvm::codegenoptions::NoDebugInfo;
  if (A.getOption().matches(options::OPT_gline_tables_only) ||
      A.getOption().matches(options::OPT_ggdb1))
    return llvm::codegenoptions::DebugLineTablesOnly;
  if (A.getOption().matches(options::OPT_gline_directives_only))
    return llvm::codegenoptions::DebugDirectivesOnly;
  return llvm::codegenoptions::DebugInfoConstructor;
}

namespace {
unsigned parseDebugDefaultVersion(const ToolChain &TC, const ArgList &Args) {
  const Arg *A = Args.getLastArg(options::OPT_fdebug_default_version);

  if (!A)
    return 0;

  unsigned Value = 0;
  if (llvm::StringRef(A->getValue()).getAsInteger(10, Value) || Value > 5 ||
      Value < 2)
    TC.getDriver().Diag(diag::err_drv_invalid_int_value)
        << A->getAsString(Args) << A->getValue();
  return Value;
}
} // namespace

unsigned tools::DwarfVersionNum(llvm::StringRef ArgValue) {
  return llvm::StringSwitch<unsigned>(ArgValue)
      .Case("-gdwarf-2", 2)
      .Case("-gdwarf-3", 3)
      .Case("-gdwarf-4", 4)
      .Case("-gdwarf-5", 5)
      .Default(0);
}

const Arg *tools::getDwarfNArg(const ArgList &Args) {
  return Args.getLastArg(options::OPT_gdwarf_2, options::OPT_gdwarf_3,
                         options::OPT_gdwarf_4, options::OPT_gdwarf_5,
                         options::OPT_gdwarf);
}

unsigned tools::getDwarfVersion(const ToolChain &TC,
                                const llvm::opt::ArgList &Args) {
  unsigned DwarfVersion = parseDebugDefaultVersion(TC, Args);
  if (const Arg *GDwarfN = getDwarfNArg(Args))
    if (int N = DwarfVersionNum(GDwarfN->getSpelling()))
      DwarfVersion = N;
  if (DwarfVersion == 0) {
    DwarfVersion = TC.GetDefaultDwarfVersion();
    assert(DwarfVersion && "toolchain default DWARF version must be nonzero");
  }
  return DwarfVersion;
}

enum class LibGccType { UnspecifiedLibGcc, StaticLibGcc, SharedLibGcc };

namespace {
LibGccType getLibGccType(const ToolChain &TC, const Driver &D,
                         const ArgList &Args) {
  if (Args.hasArg(options::OPT_static_libgcc) ||
      Args.hasArg(options::OPT_static) || Args.hasArg(options::OPT_static_pie))
    return LibGccType::StaticLibGcc;
  if (Args.hasArg(options::OPT_shared_libgcc))
    return LibGccType::SharedLibGcc;
  return LibGccType::UnspecifiedLibGcc;
}
} // namespace

// GCC adds libgcc arguments in various ways:
//
// gcc <none>:     -lgcc --as-needed -lgcc_s --no-as-needed
// gcc shared:                       -lgcc_s               -lgcc
// gcc static:     -lgcc             -lgcc_eh
// gcc static-pie: -lgcc             -lgcc_eh
//
// Also, certain targets need additional adjustments.

namespace {
void addUnwindLibrary(const ToolChain &TC, const Driver &D,
                      ArgStringList &CmdArgs, const ArgList &Args) {
  ToolChain::UnwindLibType UNW = TC.GetUnwindLibType(Args);

  // Targets that don't use unwind libraries.
  if (TC.getTriple().isWindowsMSVCEnvironment() || UNW == ToolChain::UNW_None)
    return;

  LibGccType LGT = getLibGccType(TC, D, Args);
  bool AsNeeded = LGT == LibGccType::UnspecifiedLibGcc;
  if (AsNeeded)
    addAsNeededOption(CmdArgs, true);

  switch (UNW) {
  case ToolChain::UNW_None:
    return;
  case ToolChain::UNW_Libgcc: {
    if (LGT == LibGccType::StaticLibGcc)
      CmdArgs.push_back("-lgcc_eh");
    else
      CmdArgs.push_back("-lgcc_s");
    break;
  }
  case ToolChain::UNW_CompilerRT:
    if (LGT == LibGccType::StaticLibGcc) {
      CmdArgs.push_back("-l:libunwind.a");
    } else if (LGT == LibGccType::SharedLibGcc) {
      CmdArgs.push_back("-l:libunwind.so");
    } else {
      // Let the linker choose between libunwind.so and libunwind.a
      // depending on what's available, and depending on the -static flag
      CmdArgs.push_back("-lunwind");
    }
    break;
  }

  if (AsNeeded)
    addAsNeededOption(CmdArgs, false);
}

void addLibgcc(const ToolChain &TC, const Driver &D, ArgStringList &CmdArgs,
               const ArgList &Args) {
  LibGccType LGT = getLibGccType(TC, D, Args);
  if (LGT == LibGccType::StaticLibGcc || LGT == LibGccType::UnspecifiedLibGcc)
    CmdArgs.push_back("-lgcc");
  addUnwindLibrary(TC, D, CmdArgs, Args);
  if (LGT == LibGccType::SharedLibGcc)
    CmdArgs.push_back("-lgcc");
}
} // namespace

// ===----------------------------------------------------------------------===
// Runtime libraries & misc helpers
// ===----------------------------------------------------------------------===

void tools::AddRunTimeLibs(const ToolChain &TC, const Driver &D,
                           ArgStringList &CmdArgs, const ArgList &Args) {
  // Make use of compiler-rt if --rtlib option is used
  ToolChain::RuntimeLibType RLT = TC.GetRuntimeLibType(Args);

  switch (RLT) {
  case ToolChain::RLT_CompilerRT:
    CmdArgs.push_back(TC.getCompilerRTArgString(Args, "builtins"));
    addUnwindLibrary(TC, D, CmdArgs, Args);
    break;
  case ToolChain::RLT_Libgcc:
    // Make sure libgcc is not used under MSVC environment by default
    if (TC.getTriple().isKnownWindowsMSVCEnvironment()) {
      // Issue error diagnostic if libgcc is explicitly specified
      // through command line as --rtlib option argument.
      Arg *A = Args.getLastArg(options::OPT_rtlib_EQ);
      if (A && A->getValue() != llvm::StringRef("platform")) {
        TC.getDriver().Diag(diag::err_drv_unsupported_rtlib_for_platform)
            << A->getValue() << "MSVC";
      }
    } else
      addLibgcc(TC, D, CmdArgs, Args);
    break;
  }
}

llvm::SmallString<128> tools::getStatsFileName(const llvm::opt::ArgList &Args,
                                               const InputInfo &Output,
                                               const InputInfo &Input,
                                               const Driver &D) {
  const Arg *A = Args.getLastArg(options::OPT_save_stats_EQ);
  if (!A)
    return {};

  llvm::SmallString<128> StatsFile;
  llvm::StringRef SaveStats = A->getValue();
  if (SaveStats == "obj" && Output.isFilename()) {
    StatsFile.assign(Output.getFilename());
    llvm::sys::path::remove_filename(StatsFile);
  } else if (SaveStats != "cwd") {
    D.Diag(diag::err_drv_invalid_value) << A->getAsString(Args) << SaveStats;
    return {};
  }

  llvm::StringRef BaseName = llvm::sys::path::filename(Input.getBaseInput());
  llvm::sys::path::append(StatsFile, BaseName);
  llvm::sys::path::replace_extension(StatsFile, "stats");
  return StatsFile;
}

void tools::addMultilibFlag(bool Enabled, const llvm::StringRef Flag,
                            Multilib::flags_list &Flags) {
  assert(Flag.front() == '-');
  if (Enabled) {
    Flags.push_back(Flag.str());
  } else {
    Flags.push_back(("!" + Flag.substr(1)).str());
  }
}

void tools::addX86AlignBranchArgs(const Driver &D, const ArgList &Args,
                                  ArgStringList &CmdArgs) {
  auto addArg = [&](const llvm::Twine &Arg) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(Args.MakeArgString(Arg));
  };

  if (Args.hasArg(options::OPT_mbranches_within_32B_boundaries)) {
    addArg(llvm::Twine("-x86-branches-within-32B-boundaries"));
  }
  if (const Arg *A = Args.getLastArg(options::OPT_malign_branch_boundary_EQ)) {
    llvm::StringRef Value = A->getValue();
    unsigned Boundary;
    if (Value.getAsInteger(10, Boundary) || Boundary < 16 ||
        !llvm::isPowerOf2_64(Boundary)) {
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << Value << A->getOption().getName();
    } else {
      addArg("-x86-align-branch-boundary=" + llvm::Twine(Boundary));
    }
  }
  if (const Arg *A = Args.getLastArg(options::OPT_malign_branch_EQ)) {
    std::string AlignBranch;
    for (llvm::StringRef T : A->getValues()) {
      if (T != "fused" && T != "jcc" && T != "jmp" && T != "call" &&
          T != "ret" && T != "indirect")
        D.Diag(diag::err_drv_invalid_malign_branch_EQ)
            << T << "fused, jcc, jmp, call, ret, indirect";
      if (!AlignBranch.empty())
        AlignBranch += '+';
      AlignBranch += T;
    }
    addArg("-x86-align-branch=" + llvm::Twine(AlignBranch));
  }
  if (const Arg *A = Args.getLastArg(options::OPT_mpad_max_prefix_size_EQ)) {
    llvm::StringRef Value = A->getValue();
    unsigned PrefixSize;
    if (Value.getAsInteger(10, PrefixSize)) {
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << Value << A->getOption().getName();
    } else {
      addArg("-x86-pad-max-prefix-size=" + llvm::Twine(PrefixSize));
    }
  }
}

void tools::addMachineOutlinerArgs(const Driver &D,
                                   const llvm::opt::ArgList &Args,
                                   llvm::opt::ArgStringList &CmdArgs,
                                   const llvm::Triple &Triple) {
  auto addArg = [&](const llvm::Twine &Arg) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(Args.MakeArgString(Arg));
  };

  if (Arg *A =
          Args.getLastArg(options::OPT_moutline, options::OPT_mno_outline)) {
    if (A->getOption().matches(options::OPT_moutline)) {
      if (!Triple.isAArch64()) {
        D.Diag(diag::warn_drv_moutline_unsupported_opt) << Triple.getArchName();
      } else {
        addArg(llvm::Twine("-enable-machine-outliner"));
      }
    } else {
      // Disable all outlining behaviour.
      addArg(llvm::Twine("-enable-machine-outliner=never"));
    }
  }
}

bool tools::getBundledMsvcSdkRoot(const Driver &D, const llvm::Triple &Triple,
                                  llvm::SmallVectorImpl<char> &SdkRoot) {
  llvm::SmallString<128> P(llvm::sys::path::parent_path(D.getInstalledDir()));
  llvm::StringRef Arch;
  switch (Triple.getArch()) {
  case llvm::Triple::x86_64:
    Arch = "x64";
    break;
  case llvm::Triple::aarch64:
    Arch = "arm64";
    break;
  default:
    return false;
  }
  llvm::sys::path::append(P, "runtime", "windows", Arch, "msvc");
  if (llvm::sys::fs::is_directory(P)) {
    SdkRoot.assign(P.begin(), P.end());
    return true;
  }
  return false;
}

bool tools::getBundledWdkRoot(const Driver &D, const llvm::Triple &Triple,
                              llvm::SmallVectorImpl<char> &WdkRoot) {
  llvm::SmallString<128> P(llvm::sys::path::parent_path(D.getInstalledDir()));
  llvm::StringRef Arch;
  switch (Triple.getArch()) {
  case llvm::Triple::x86_64:
    Arch = "x64";
    break;
  default:
    return false;
  }
  llvm::sys::path::append(P, "runtime", "windows", Arch, "wdk");
  if (llvm::sys::fs::is_directory(P)) {
    WdkRoot.assign(P.begin(), P.end());
    return true;
  }
  return false;
}

bool tools::getBundledRuntimeSharedRoot(const Driver &D,
                                        llvm::StringRef Sdk,
                                        llvm::SmallVectorImpl<char> &Root) {
  llvm::SmallString<128> P(llvm::sys::path::parent_path(D.getInstalledDir()));
  llvm::sys::path::append(P, "runtime", "windows", "shared", Sdk);
  if (llvm::sys::fs::is_directory(P)) {
    Root.assign(P.begin(), P.end());
    return true;
  }
  return false;
}

