//===- NeverC.cpp -- NeverC toolchain driver argument construction --------===//
//
// Translates user-facing driver flags into cc1 / assembler / linker
// command-line arguments.  The bulk of this file is helper functions
// that render one logical flag group (floating-point model, debug info,
// sanitisers, SSP, target options, ...) into the ArgStringList that
// eventually becomes the cc1 invocation.
//
//===----------------------------------------------------------------------===//

#include "NeverC.h"
#include "Arch/AArch64.h"
#include "Arch/X86.h"
#include "CommonArgs.h"
#include "neverc/Compiler/FrontendOptions.h"
#include "neverc/Config/config.h"
#include "neverc/Foundation/Core/MakeSupport.h"
#include "neverc/Foundation/Core/Version.h"
#include "neverc/Foundation/Diagnostic/CLWarnings.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/InputInfo.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Invoke/Types.h"
#include "neverc/Scan/HeaderIndexOptions.h"
#include "neverc/Scan/PrepOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/Host.h"
#include <cctype>

using namespace neverc::driver;
using namespace neverc::driver::tools;
using namespace neverc;
using namespace llvm::opt;

// ===----------------------------------------------------------------------===
// Argument validation & rendering helpers
// ===----------------------------------------------------------------------===

namespace {

void checkPreprocessingOptions(const Driver &D, const ArgList &Args) {
  if (Arg *A = Args.getLastArg(neverc::driver::options::OPT_C, options::OPT_CC,
                               options::OPT_fminimize_whitespace,
                               options::OPT_fno_minimize_whitespace,
                               options::OPT_fkeep_system_includes,
                               options::OPT_fno_keep_system_includes)) {
    if (!Args.hasArg(options::OPT_E) &&
        !Args.hasArg(options::OPT_msvc_preprocess_to_file) &&
        !Args.hasArg(options::OPT_msvc_preprocess_no_linemarkers)) {
      D.Diag(neverc::diag::err_drv_argument_only_allowed_with)
          << A->getBaseArg().getAsString(Args) << "-E";
    }
  }
}

void checkCodeGenerationOptions(const Driver &D, const ArgList &Args) {
  if (Args.hasArg(options::OPT_static))
    if (const Arg *A = Args.getLastArg(options::OPT_dynamic))
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << A->getAsString(Args) << "-static";
}

void escapeSpacesAndBackslashes(const char *Arg,
                                llvm::SmallVectorImpl<char> &Res) {
  for (; *Arg; ++Arg) {
    switch (*Arg) {
    default:
      break;
    case ' ':
    case '\\':
      Res.push_back('\\');
      break;
    }
    Res.push_back(*Arg);
  }
}

void populateDirectInvocationOptsForFrontendJob(
    DirectInvocationOpts &DO, const JobAction &JA, const InputInfo &Output,
    llvm::ArrayRef<InputInfo> FrontendInputs, const ArgStringList &CmdArgs,
    llvm::StringRef TripleStr, llvm::StringRef CPU) {
  auto TO = std::make_shared<neverc::TargetOptions>();
  TO->Triple = TripleStr.str();
  TO->CPU = CPU.str();

  auto HSO = std::make_shared<HeaderIndexOptions>();
  auto PPO = std::make_shared<PrepOptions>();
  auto FEO = std::make_shared<FrontendOptions>();

  if (isa<PreprocessJobAction>(JA)) {
    FEO->ProgramAction = Output.getType() == types::TY_Dependencies
                             ? frontend::RunPreprocessorOnly
                             : frontend::PrintPreprocessedInput;
  } else if (JA.getType() == types::TY_Nothing) {
    FEO->ProgramAction = frontend::ParseSyntaxOnly;
  } else if (JA.getType() == types::TY_LLVM_IR ||
             JA.getType() == types::TY_LTO_IR) {
    FEO->ProgramAction = frontend::GenLLVM;
  } else if (JA.getType() == types::TY_LLVM_BC ||
             JA.getType() == types::TY_LTO_BC) {
    FEO->ProgramAction = frontend::GenBC;
  } else if (JA.getType() == types::TY_PP_Asm) {
    FEO->ProgramAction = frontend::GenAssembly;
  } else {
    FEO->ProgramAction = frontend::GenObj;
  }

  if (Output.isFilename())
    FEO->OutputFile = Output.getFilename();

  for (size_t i = 0, e = CmdArgs.size(); i < e; ++i) {
    llvm::StringRef A(CmdArgs[i]);

    if (A == "-target-feature" && i + 1 < e) {
      TO->FeaturesAsWritten.push_back(CmdArgs[++i]);
    } else if (A == "-tune-cpu" && i + 1 < e)
      TO->TuneCPU = CmdArgs[++i];
    else if (A == "-target-abi" && i + 1 < e)
      TO->ABI = CmdArgs[++i];

    else if (A == "-isysroot" && i + 1 < e)
      HSO->Sysroot = CmdArgs[++i];
    else if (A == "-resource-dir" && i + 1 < e)
      HSO->ResourceDir = CmdArgs[++i];
    else if (A == "-nostdsysteminc")
      HSO->UseStandardSystemIncludes = false;
    else if (A == "-nobuiltininc")
      HSO->UseBuiltinIncludes = false;
    else if (A == "-v")
      HSO->Verbose = true;
    else if (A == "-I" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::Angled, false, true);
    else if (A == "-isystem" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::System, false, true);
    else if (A == "-iwithsysroot" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::System, false, false);
    else if (A == "-internal-isystem" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::System, false, true);
    else if (A == "-internal-externc-isystem" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::ExternCSystem, false, true);
    else if (A == "-c-isystem" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::CSystem, false, true);
    else if (A == "-iquote" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::Quoted, false, true);
    else if (A == "-idirafter" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::After, false, true);
    else if (A == "-iframework" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::System, true, true);
    else if (A == "-iframeworkwithsysroot" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::System, true, false);
    else if (A == "-F" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::Angled, true, true);
    else if (A == "-ivfsoverlay" && i + 1 < e)
      HSO->AddVFSOverlayFile(CmdArgs[++i]);
    else if (A == "-iprefix" && i + 1 < e)
      ++i;
    else if (A == "-iwithprefix" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::After, false, true);
    else if (A == "-iwithprefixbefore" && i + 1 < e)
      HSO->AddPath(CmdArgs[++i], frontend::Angled, false, true);
    else if (A == "-system-header-prefix" && i + 1 < e)
      HSO->AddSystemHeaderPrefix(CmdArgs[++i], true);
    else if (A == "-no-system-header-prefix" && i + 1 < e)
      HSO->AddSystemHeaderPrefix(CmdArgs[++i], false);

    else if (A.starts_with("-D") && A.size() > 2)
      PPO->addMacroDef(A.drop_front(2));
    else if (A == "-D" && i + 1 < e)
      PPO->addMacroDef(CmdArgs[++i]);
    else if (A.starts_with("-U") && A.size() > 2)
      PPO->addMacroUndef(A.drop_front(2));
    else if (A == "-U" && i + 1 < e)
      PPO->addMacroUndef(CmdArgs[++i]);
    else if (A == "-include" && i + 1 < e)
      PPO->Includes.push_back(CmdArgs[++i]);
    else if (A == "-source-date-epoch" && i + 1 < e) {
      uint64_t Epoch;
      if (!llvm::StringRef(CmdArgs[++i]).getAsInteger(10, Epoch))
        PPO->SourceDateEpoch = Epoch;
    } else if (A == "-undef")
      PPO->UsePredefines = false;
    else if (A == "-fdefine-target-os-macros")
      PPO->DefineTargetOSMacros = true;

    else if (A == "-mllvm" && i + 1 < e)
      FEO->LLVMArgs.push_back(CmdArgs[++i]);
    else if (A == "-disable-free")
      FEO->DisableFree = true;
    else if (A.starts_with("-ftime-trace-granularity=")) {
      unsigned G;
      if (!A.drop_front(24).getAsInteger(10, G))
        FEO->TimeTraceGranularity = G;
    } else if (A.starts_with("-ftime-trace="))
      FEO->TimeTracePath = A.drop_front(13).str();
    else if (A == "-ftime-trace" && i + 1 < e)
      FEO->TimeTracePath = CmdArgs[++i];
    else if (A.starts_with("-stats-file="))
      FEO->StatsFile = A.drop_front(12).str();
    else if (A == "-x" && i + 1 < e) {
      llvm::StringRef XVal(CmdArgs[++i]);
      bool PP = XVal.consume_back("-cpp-output");
      bool IsHdr = XVal.consume_back("-header");
      InputKind IK =
          llvm::StringSwitch<InputKind>(XVal)
              .Case("c", Language::C)
              .Case("cpp-output", InputKind(Language::C).getPreprocessed())
              .Case("assembler-with-cpp", Language::Asm)
              .Case("ir", Language::LLVM_IR)
              .Default(Language::Unknown);
      if (PP)
        IK = IK.getPreprocessed();
      if (IsHdr)
        IK = IK.getHeader();
      FEO->DashX = IK;
    }
  }

  for (const InputInfo &I : FrontendInputs) {
    if (I.isFilename()) {
      InputKind IK = FEO->DashX;
      if (IK.isUnknown()) {
        IK = FrontendOptions::getInputKindForExtension(
            llvm::StringRef(I.getFilename()).rsplit('.').second);
        if (IK.isUnknown())
          IK = InputKind(Language::C);
      }
      FEO->Inputs.push_back(FrontendInputFile(I.getFilename(), IK));
    }
  }

  DO.TargetOpts = std::move(TO);
  DO.HeaderIdxOpts = std::move(HSO);
  DO.PPOpts = std::move(PPO);
  DO.FrontendOpts = std::move(FEO);
}

bool getRefinementStep(llvm::StringRef In, const Driver &D, const Arg &A,
                       size_t &Position) {
  const char RefinementStepToken = ':';
  Position = In.find(RefinementStepToken);
  if (Position != llvm::StringRef::npos) {
    llvm::StringRef Option = A.getOption().getName();
    llvm::StringRef RefStep = In.substr(Position + 1);
    // Allow exactly one numeric character for the additional refinement
    // step parameter. This is reasonable for all currently-supported
    // operations and architectures because we would expect that a larger value
    // of refinement steps would cause the estimate "optimization" to
    // under-perform the native operation. Also, if the estimate does not
    // converge quickly, it probably will not ever converge, so further
    // refinement steps will not produce a better answer.
    if (RefStep.size() != 1) {
      D.Diag(diag::err_drv_invalid_value) << Option << RefStep;
      return false;
    }
    char RefStepChar = RefStep[0];
    if (RefStepChar < '0' || RefStepChar > '9') {
      D.Diag(diag::err_drv_invalid_value) << Option << RefStep;
      return false;
    }
  }
  return true;
}

void parseMRecip(const Driver &D, const ArgList &Args,
                 ArgStringList &OutStrings) {
  llvm::StringRef DisabledPrefixIn = "!";
  llvm::StringRef DisabledPrefixOut = "!";
  llvm::StringRef EnabledPrefixOut = "";
  llvm::StringRef Out = "-mrecip=";

  Arg *A = Args.getLastArg(options::OPT_mrecip, options::OPT_mrecip_EQ);
  if (!A)
    return;

  unsigned NumOptions = A->getNumValues();
  if (NumOptions == 0) {
    // No option is the same as "all".
    OutStrings.push_back(Args.MakeArgString(Out + "all"));
    return;
  }

  // Pass through "all", "none", or "default" with an optional refinement step.
  if (NumOptions == 1) {
    llvm::StringRef Val = A->getValue(0);
    size_t RefStepLoc;
    if (!getRefinementStep(Val, D, *A, RefStepLoc))
      return;
    llvm::StringRef ValBase = Val.slice(0, RefStepLoc);
    if (ValBase == "all" || ValBase == "none" || ValBase == "default") {
      OutStrings.push_back(Args.MakeArgString(Out + Val));
      return;
    }
  }

  // Each reciprocal type may be enabled or disabled individually.
  // Check each input value for validity, concatenate them all back together,
  // and pass through.

  llvm::StringMap<bool> OptionStrings;
  OptionStrings.insert(std::make_pair("divd", false));
  OptionStrings.insert(std::make_pair("divf", false));
  OptionStrings.insert(std::make_pair("divh", false));
  OptionStrings.insert(std::make_pair("vec-divd", false));
  OptionStrings.insert(std::make_pair("vec-divf", false));
  OptionStrings.insert(std::make_pair("vec-divh", false));
  OptionStrings.insert(std::make_pair("sqrtd", false));
  OptionStrings.insert(std::make_pair("sqrtf", false));
  OptionStrings.insert(std::make_pair("sqrth", false));
  OptionStrings.insert(std::make_pair("vec-sqrtd", false));
  OptionStrings.insert(std::make_pair("vec-sqrtf", false));
  OptionStrings.insert(std::make_pair("vec-sqrth", false));

  for (unsigned i = 0; i != NumOptions; ++i) {
    llvm::StringRef Val = A->getValue(i);

    bool IsDisabled = Val.starts_with(DisabledPrefixIn);
    // Ignore the disablement token for string matching.
    if (IsDisabled)
      Val = Val.substr(1);

    size_t RefStep;
    if (!getRefinementStep(Val, D, *A, RefStep))
      return;

    llvm::StringRef ValBase = Val.slice(0, RefStep);
    llvm::StringMap<bool>::iterator OptionIter = OptionStrings.find(ValBase);
    if (OptionIter == OptionStrings.end()) {
      // Try again specifying float suffix.
      OptionIter = OptionStrings.find(ValBase.str() + 'f');
      if (OptionIter == OptionStrings.end()) {
        // The input name did not match any known option string.
        D.Diag(diag::err_drv_unknown_argument) << Val;
        return;
      }
      // The option was specified without a half or float or double suffix.
      // Make sure that the double or half entry was not already specified.
      // The float entry will be checked below.
      if (OptionStrings[ValBase.str() + 'd'] ||
          OptionStrings[ValBase.str() + 'h']) {
        D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Val;
        return;
      }
    }

    if (OptionIter->second == true) {
      // Duplicate option specified.
      D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Val;
      return;
    }

    // Mark the matched option as found. Do not allow duplicate specifiers.
    OptionIter->second = true;

    // If the precision was not specified, also mark the double and half entry
    // as found.
    if (ValBase.back() != 'f' && ValBase.back() != 'd' &&
        ValBase.back() != 'h') {
      OptionStrings[ValBase.str() + 'd'] = true;
      OptionStrings[ValBase.str() + 'h'] = true;
    }

    llvm::StringRef Prefix = IsDisabled ? DisabledPrefixOut : EnabledPrefixOut;
    Out = Args.MakeArgString(Out + Prefix + Val);
    if (i != NumOptions - 1)
      Out = Args.MakeArgString(Out + ",");
  }

  OutStrings.push_back(Args.MakeArgString(Out));
}

void parseMPreferVectorWidth(const Driver &D, const ArgList &Args,
                             ArgStringList &CmdArgs) {
  Arg *A = Args.getLastArg(options::OPT_mprefer_vector_width_EQ);
  if (!A)
    return;

  llvm::StringRef Value = A->getValue();
  if (Value == "none") {
    CmdArgs.push_back("-mprefer-vector-width=none");
  } else {
    unsigned Width;
    if (Value.getAsInteger(10, Width)) {
      D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Value;
      return;
    }
    CmdArgs.push_back(Args.MakeArgString("-mprefer-vector-width=" + Value));
  }
}

void addExceptionArgs(const ArgList &Args) {
  Args.ClaimAllArgs(options::OPT_fexceptions);
  Args.ClaimAllArgs(options::OPT_fno_exceptions);
  Args.ClaimAllArgs(options::OPT_fasync_exceptions);
  Args.ClaimAllArgs(options::OPT_fno_async_exceptions);
}

bool shouldEnableAutolink(const ArgList &Args, const ToolChain &TC,
                          const JobAction &JA) {
  return Args.hasFlag(options::OPT_fautolink, options::OPT_fno_autolink, true);
}

const char *addDebugCompDirArg(const ArgList &Args, ArgStringList &CmdArgs,
                               const llvm::vfs::FileSystem &VFS) {
  if (Arg *A = Args.getLastArg(options::OPT_ffile_compilation_dir_EQ,
                               options::OPT_fdebug_compilation_dir_EQ)) {
    if (A->getOption().matches(options::OPT_ffile_compilation_dir_EQ))
      CmdArgs.push_back(Args.MakeArgString(
          llvm::Twine("-fdebug-compilation-dir=") + A->getValue()));
    else
      A->render(Args, CmdArgs);
  } else if (auto CWD = VFS.getCurrentWorkingDirectory()) {
    CmdArgs.push_back(Args.MakeArgString("-fdebug-compilation-dir=" + *CWD));
  }
  llvm::StringRef Path(CmdArgs.back());
  return Path.substr(Path.find('=') + 1).data();
}

void addDebugObjectName(const ArgList &Args, ArgStringList &CmdArgs,
                        const char *DebugCompilationDir,
                        const char *OutputFileName) {
  // No need to generate a value for -object-file-name if it was provided.
  if (Args.hasArg(options::OPT_object_file_name_EQ))
    return;

  llvm::SmallString<128> ObjFileNameForDebug(OutputFileName);
  if (ObjFileNameForDebug != "-" &&
      !llvm::sys::path::is_absolute(ObjFileNameForDebug) &&
      (!DebugCompilationDir ||
       llvm::sys::path::is_absolute(DebugCompilationDir))) {
    // Make the path absolute in the debug infos like MSVC does.
    llvm::sys::fs::make_absolute(ObjFileNameForDebug);
  }
  // If the object file name is a relative path, then always use Windows
  // backslash style as -object-file-name is used for embedding object file path
  // in debug info and it can only be generated when targeting on Windows.
  // Otherwise, just use native absolute path.
  llvm::sys::path::Style Style =
      llvm::sys::path::is_absolute(ObjFileNameForDebug)
          ? llvm::sys::path::Style::native
          : llvm::sys::path::Style::windows_backslash;
  llvm::sys::path::remove_dots(ObjFileNameForDebug, /*remove_dot_dot=*/true,
                               Style);
  CmdArgs.push_back(Args.MakeArgString(llvm::Twine("-object-file-name=") +
                                       ObjFileNameForDebug));
}

void addDebugPrefixMapArg(const Driver &D, const ToolChain &TC,
                          const ArgList &Args, ArgStringList &CmdArgs) {
  auto AddOneArg = [&](llvm::StringRef Map, llvm::StringRef Name) {
    if (!Map.contains('='))
      D.Diag(diag::err_drv_invalid_argument_to_option) << Map << Name;
    else
      CmdArgs.push_back(Args.MakeArgString("-fdebug-prefix-map=" + Map));
  };

  for (const Arg *A : Args.filtered(options::OPT_ffile_prefix_map_EQ,
                                    options::OPT_fdebug_prefix_map_EQ)) {
    AddOneArg(A->getValue(), A->getOption().getName());
    A->claim();
  }
  std::string GlobalRemapEntry = TC.GetGlobalDebugPathRemapping();
  if (GlobalRemapEntry.empty())
    return;
  AddOneArg(GlobalRemapEntry, "environment");
}

void addMacroPrefixMapArg(const Driver &D, const ArgList &Args,
                          ArgStringList &CmdArgs) {
  for (const Arg *A : Args.filtered(options::OPT_ffile_prefix_map_EQ,
                                    options::OPT_fmacro_prefix_map_EQ)) {
    llvm::StringRef Map = A->getValue();
    if (!Map.contains('='))
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << Map << A->getOption().getName();
    else
      CmdArgs.push_back(Args.MakeArgString("-fmacro-prefix-map=" + Map));
    A->claim();
  }
}

bool shouldEnableVectorizerAtOLevel(const ArgList &Args, bool isSlpVec) {
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O4) ||
        A->getOption().matches(options::OPT_Ofast))
      return true;

    if (A->getOption().matches(options::OPT_O0))
      return false;

    assert(A->getOption().matches(options::OPT_O) && "Must have a -O flag");

    // Vectorize -Os.
    llvm::StringRef S(A->getValue());
    if (S == "s")
      return true;

    // Don't vectorize -Oz, unless it's the slp vectorizer.
    if (S == "z")
      return isSlpVec;

    unsigned OptLevel = 0;
    if (S.getAsInteger(10, OptLevel))
      return false;

    return isSlpVec ? OptLevel > 2 : OptLevel > 1;
  }

  return false;
}

void addDashXForInput(const ArgList &Args, const InputInfo &Input,
                      ArgStringList &CmdArgs) {
  CmdArgs.push_back("-x");
  CmdArgs.push_back(types::getTypeName(Input.getType()));
}

bool containsCompileAction(const Action *A) {
  if (isa<CompileJobAction>(A) || isa<BackendJobAction>(A))
    return true;

  return llvm::any_of(A->inputs(), containsCompileAction);
}

bool useRelaxAll(Compilation &C, const ArgList &Args) {
  bool RelaxDefault = true;

  if (Arg *A = Args.getLastArg(options::OPT_O_Group))
    RelaxDefault = A->getOption().matches(options::OPT_O0);

  if (RelaxDefault) {
    RelaxDefault = false;
    for (const auto &Act : C.getActions()) {
      if (containsCompileAction(Act)) {
        RelaxDefault = true;
        break;
      }
    }
  }

  return Args.hasFlag(options::OPT_mrelax_all, options::OPT_mno_relax_all,
                      RelaxDefault);
}

void renderDebugEnablingArgs(const ArgList &Args, ArgStringList &CmdArgs,
                             llvm::codegenoptions::DebugInfoKind DebugInfoKind,
                             unsigned DwarfVersion,
                             llvm::DebuggerKind DebuggerTuning) {
  addDebugInfoKind(CmdArgs, DebugInfoKind);
  if (DwarfVersion > 0)
    CmdArgs.push_back(
        Args.MakeArgString("-dwarf-version=" + llvm::Twine(DwarfVersion)));
  switch (DebuggerTuning) {
  case llvm::DebuggerKind::GDB:
    CmdArgs.push_back("-debugger-tuning=gdb");
    break;
  case llvm::DebuggerKind::LLDB:
    CmdArgs.push_back("-debugger-tuning=lldb");
    break;
  default:
    break;
  }
}

bool checkDebugInfoOption(const Arg *A, const ArgList &Args, const Driver &D,
                          const ToolChain &TC) {
  assert(A && "Expected non-nullptr argument.");
  if (TC.supportsDebugInfoOption(A))
    return true;
  D.Diag(diag::warn_drv_unsupported_debug_info_opt_for_target)
      << A->getAsString(Args) << TC.getTripleString();
  return false;
}

void renderDebugInfoCompressionArgs(const ArgList &Args, ArgStringList &CmdArgs,
                                    const Driver &D, const ToolChain &TC) {
  const Arg *A = Args.getLastArg(options::OPT_gz_EQ);
  if (!A)
    return;
  if (checkDebugInfoOption(A, Args, D, TC)) {
    llvm::StringRef Value = A->getValue();
    if (Value == "none") {
      CmdArgs.push_back("--compress-debug-sections=none");
    } else if (Value == "zlib") {
      if (llvm::compression::zlib::isAvailable()) {
        CmdArgs.push_back(Args.MakeArgString("--compress-debug-sections=" +
                                             llvm::Twine(Value)));
      } else {
        D.Diag(diag::warn_debug_compression_unavailable) << "zlib";
      }
    } else if (Value == "zstd") {
      if (llvm::compression::zstd::isAvailable()) {
        CmdArgs.push_back(Args.MakeArgString("--compress-debug-sections=" +
                                             llvm::Twine(Value)));
      } else {
        D.Diag(diag::warn_debug_compression_unavailable) << "zstd";
      }
    } else {
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Value;
    }
  }
}

} // namespace

// ===----------------------------------------------------------------------===
// Preprocessing options
// ===----------------------------------------------------------------------===

void NeverC::AddPreprocessingOptions(Compilation &C, const JobAction &JA,
                                     const Driver &D, const ArgList &Args,
                                     ArgStringList &CmdArgs,
                                     const InputInfo &Output,
                                     const InputInfoList &Inputs) const {
  checkPreprocessingOptions(D, Args);

  Args.AddLastArg(CmdArgs, options::OPT_C);
  Args.AddLastArg(CmdArgs, options::OPT_CC);

  Arg *ArgM = Args.getLastArg(options::OPT_MM);
  if (!ArgM)
    ArgM = Args.getLastArg(options::OPT_M);
  Arg *ArgMD = Args.getLastArg(options::OPT_MMD);
  if (!ArgMD)
    ArgMD = Args.getLastArg(options::OPT_MD);

  // -M and -MM imply -w.
  if (ArgM)
    CmdArgs.push_back("-w");
  else
    ArgM = ArgMD;

  if (ArgM) {
    const char *DepFile;
    if (Arg *MF = Args.getLastArg(options::OPT_MF)) {
      DepFile = MF->getValue();
      C.addFailureResultFile(DepFile, &JA);
    } else if (Output.getType() == types::TY_Dependencies) {
      DepFile = Output.getFilename();
    } else if (!ArgMD) {
      DepFile = "-";
    } else {
      DepFile = getDependencyFileName(Args, Inputs);
      C.addFailureResultFile(DepFile, &JA);
    }
    CmdArgs.push_back("-dependency-file");
    CmdArgs.push_back(DepFile);

    bool HasTarget = false;
    for (const Arg *A : Args.filtered(options::OPT_MT, options::OPT_MQ)) {
      HasTarget = true;
      A->claim();
      if (A->getOption().matches(options::OPT_MT)) {
        A->render(Args, CmdArgs);
      } else {
        CmdArgs.push_back("-MT");
        llvm::SmallString<128> Quoted;
        quoteMakeTarget(A->getValue(), Quoted);
        CmdArgs.push_back(Args.MakeArgString(Quoted));
      }
    }

    if (!HasTarget) {
      const char *DepTarget;

      // If user provided -o, that is the dependency target, except
      // when we are only generating a dependency file.
      Arg *OutputOpt = Args.getLastArg(options::OPT_o);
      if (OutputOpt && Output.getType() != types::TY_Dependencies) {
        DepTarget = OutputOpt->getValue();
      } else {
        // Otherwise derive from the base input.
        llvm::SmallString<128> P(Inputs[0].getBaseInput());
        llvm::sys::path::replace_extension(P, "o");
        DepTarget = Args.MakeArgString(llvm::sys::path::filename(P));
      }

      CmdArgs.push_back("-MT");
      llvm::SmallString<128> Quoted;
      quoteMakeTarget(DepTarget, Quoted);
      CmdArgs.push_back(Args.MakeArgString(Quoted));
    }

    if (ArgM->getOption().matches(options::OPT_M) ||
        ArgM->getOption().matches(options::OPT_MD))
      CmdArgs.push_back("-sys-header-deps");
  }

  if (Args.hasArg(options::OPT_MG)) {
    if (!ArgM || ArgM->getOption().matches(options::OPT_MD) ||
        ArgM->getOption().matches(options::OPT_MMD))
      D.Diag(diag::err_drv_mg_requires_m_or_mm);
    CmdArgs.push_back("-MG");
  }

  Args.AddLastArg(CmdArgs, options::OPT_MP);
  Args.AddLastArg(CmdArgs, options::OPT_MV);

  for (const Arg *A : Args.filtered(options::OPT_neverc_i_Group)) {
    if (A->getOption().matches(options::OPT_isystem_after)) {
      // Handling of paths which must come late.  These entries are handled by
      // the toolchain itself after the resource dir is inserted in the right
      // search order.
      // Do not claim the argument so that the use of the argument does not
      // silently go unnoticed on toolchains which do not honour the option.
      continue;
    } else if (A->getOption().matches(options::OPT_ibuiltininc)) {
      // This is used only by the driver. No need to pass to the frontend.
      continue;
    }

    // Not translated, render as usual.
    A->claim();
    A->render(Args, CmdArgs);
  }

  Args.addAllArgs(CmdArgs,
                  {options::OPT_D, options::OPT_U, options::OPT_I_Group,
                   options::OPT_F, options::OPT_index_header_map});

  // Add -Wp, and -Xpreprocessor if using the preprocessor.

  Args.AddAllArgValues(CmdArgs, options::OPT_Wp_COMMA,
                       options::OPT_Xpreprocessor);

  // -I- is a deprecated GCC feature, reject it.
  if (Arg *A = Args.getLastArg(options::OPT_I_))
    D.Diag(diag::err_drv_I_dash_not_supported) << A->getAsString(Args);

  // If we have a --sysroot, and don't have an explicit -isysroot flag, add an
  // -isysroot to the frontend invocation.
  llvm::StringRef sysroot = C.getSysRoot();
  if (sysroot != "") {
    if (!Args.hasArg(options::OPT_isysroot)) {
      CmdArgs.push_back("-isysroot");
      CmdArgs.push_back(C.getArgs().MakeArgString(sysroot));
    }
  }

  // CPATH - included following the user specified includes (but prior to
  // builtin and standard includes).
  addDirectoryList(Args, CmdArgs, "-I", "CPATH");
  // C_INCLUDE_PATH - system includes enabled when compiling C.
  addDirectoryList(Args, CmdArgs, "-c-isystem", "C_INCLUDE_PATH");

  getToolChain().AddNeverCSystemIncludeArgs(Args, CmdArgs);

  addMacroPrefixMapArg(D, Args, CmdArgs);

  Args.AddLastArg(CmdArgs, options::OPT_ffile_reproducible,
                  options::OPT_fno_file_reproducible);

  if (const char *Epoch = std::getenv("SOURCE_DATE_EPOCH")) {
    CmdArgs.push_back("-source-date-epoch");
    CmdArgs.push_back(Args.MakeArgString(Epoch));
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fdefine_target_os_macros,
                    options::OPT_fno_define_target_os_macros);
}

namespace {

bool isSignedCharDefault(const llvm::Triple &Triple) {
  switch (Triple.getArch()) {
  default:
    return true;
  case llvm::Triple::aarch64:
    return Triple.isOSDarwin() || Triple.isOSWindows();
  }
}

bool hasMultipleInvocations(const llvm::Triple &Triple, const ArgList &Args) {
  // Supported only on Darwin where we invoke the compiler multiple times
  // followed by an invocation to lipo.
  if (!Triple.isOSDarwin())
    return false;
  // If more than one "-arch <arch>" is specified, we're targeting multiple
  // architectures resulting in a fat binary.
  return Args.getAllArgValues(options::OPT_arch).size() > 1;
}

bool checkRemarksOptions(const Driver &D, const ArgList &Args,
                         const llvm::Triple &Triple) {
  // When enabling remarks, we need to error if:
  // * The remark file is specified but we're targeting multiple architectures,
  // which means more than one remark file is being generated.
  bool hasMultipleInvocations = ::hasMultipleInvocations(Triple, Args);
  bool hasExplicitOutputFile =
      Args.getLastArg(options::OPT_foptimization_record_file_EQ);
  if (hasMultipleInvocations && hasExplicitOutputFile) {
    D.Diag(diag::err_drv_invalid_output_with_multiple_archs)
        << "-foptimization-record-file";
    return false;
  }
  return true;
}

void renderRemarksOptions(const ArgList &Args, ArgStringList &CmdArgs,
                          const llvm::Triple &Triple, const InputInfo &Input,
                          const InputInfo &Output, const JobAction &JA) {
  llvm::StringRef Format = "yaml";
  if (const Arg *A = Args.getLastArg(options::OPT_fsave_optimization_record_EQ))
    Format = A->getValue();

  CmdArgs.push_back("-opt-record-file");

  const Arg *A = Args.getLastArg(options::OPT_foptimization_record_file_EQ);
  if (A) {
    CmdArgs.push_back(A->getValue());
  } else {
    bool hasMultipleArchs =
        Triple.isOSDarwin() && // Only supported on Darwin platforms.
        Args.getAllArgValues(options::OPT_arch).size() > 1;

    llvm::SmallString<128> F;

    if (Args.hasArg(options::OPT_c) || Args.hasArg(options::OPT_S)) {
      if (Arg *FinalOutput = Args.getLastArg(options::OPT_o))
        F = FinalOutput->getValue();
    } else {
      if (Format != "yaml" &&    // For YAML, keep the original behavior.
          Triple.isOSDarwin() && // Enable this only on darwin, since it's the
                                 // only platform supporting .dSYM bundles.
          Output.isFilename())
        F = Output.getFilename();
    }

    if (F.empty()) {
      // Use the input filename.
      F = llvm::sys::path::stem(Input.getBaseInput());
    }

    // If we're having more than one "-arch", we should name the files
    // differently so that every frontend invocation writes to a different file.
    // We're doing that by appending "-<arch>" with "<arch>" being the arch
    // name from the triple.
    if (hasMultipleArchs) {
      // First, remember the extension.
      llvm::SmallString<64> OldExtension = llvm::sys::path::extension(F);
      // then, remove it.
      llvm::sys::path::replace_extension(F, "");
      // attach -<arch> to it.
      F += "-";
      F += Triple.getArchName();
      // put back the extension.
      llvm::sys::path::replace_extension(F, OldExtension);
    }

    llvm::SmallString<32> Extension;
    Extension += "opt.";
    Extension += Format;

    llvm::sys::path::replace_extension(F, Extension);
    CmdArgs.push_back(Args.MakeArgString(F));
  }

  if (const Arg *A =
          Args.getLastArg(options::OPT_foptimization_record_passes_EQ)) {
    CmdArgs.push_back("-opt-record-passes");
    CmdArgs.push_back(A->getValue());
  }

  if (!Format.empty()) {
    CmdArgs.push_back("-opt-record-format");
    CmdArgs.push_back(Format.data());
  }
}

void addUnalignedAccessWarning(ArgStringList &CmdArgs) {
  auto StrictAlignIter =
      llvm::find_if(llvm::reverse(CmdArgs), [](llvm::StringRef Arg) {
        return Arg == "+strict-align" || Arg == "-strict-align";
      });
  if (StrictAlignIter != CmdArgs.rend() &&
      llvm::StringRef(*StrictAlignIter) == "+strict-align")
    CmdArgs.push_back("-Wunaligned-access");
}

void collectARMPACBTIOptions(const ToolChain &TC, const ArgList &Args,
                             ArgStringList &CmdArgs, bool isAArch64) {
  const Arg *A = isAArch64
                     ? Args.getLastArg(options::OPT_msign_return_address_EQ,
                                       options::OPT_mbranch_protection_EQ)
                     : Args.getLastArg(options::OPT_mbranch_protection_EQ);
  if (!A)
    return;

  const Driver &D = TC.getDriver();
  const llvm::Triple &Triple = TC.getEffectiveTriple();
  if (!isAArch64)
    D.Diag(diag::warn_incompatible_branch_protection_option)
        << Triple.getArchName();

  llvm::StringRef Scope, Key;
  bool IndirectBranches, BranchProtectionPAuthLR;

  if (A->getOption().matches(options::OPT_msign_return_address_EQ)) {
    Scope = A->getValue();
    if (Scope != "none" && Scope != "non-leaf" && Scope != "all")
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Scope;
    Key = "a_key";
    IndirectBranches = false;
    BranchProtectionPAuthLR = false;
  } else {
    llvm::StringRef DiagMsg;
    llvm::AArch64::ParsedBranchProtection PBP;
    if (!llvm::AArch64::parseBranchProtection(A->getValue(), PBP, DiagMsg))
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << DiagMsg;
    if (!isAArch64 && PBP.Key == "b_key")
      D.Diag(diag::warn_unsupported_branch_protection)
          << "b-key" << A->getAsString(Args);
    Scope = PBP.Scope;
    Key = PBP.Key;
    BranchProtectionPAuthLR = PBP.BranchProtectionPAuthLR;
    IndirectBranches = PBP.BranchTargetEnforcement;
  }

  CmdArgs.push_back(
      Args.MakeArgString(llvm::Twine("-msign-return-address=") + Scope));
  if (!Scope.equals("none"))
    CmdArgs.push_back(
        Args.MakeArgString(llvm::Twine("-msign-return-address-key=") + Key));
  if (BranchProtectionPAuthLR)
    CmdArgs.push_back(
        Args.MakeArgString(llvm::Twine("-mbranch-protection-pauth-lr")));
  if (IndirectBranches)
    CmdArgs.push_back("-mbranch-target-enforce");
}

} // namespace

// ===----------------------------------------------------------------------===
// Target architecture options
// ===----------------------------------------------------------------------===

void NeverC::RenderTargetOptions(const llvm::Triple &EffectiveTriple,
                                 const ArgList &Args, bool Kernel,
                                 ArgStringList &CmdArgs) const {
  const ToolChain &TC = getToolChain();

  getTargetFeatures(TC.getDriver(), EffectiveTriple, Args, CmdArgs, false);

  switch (TC.getArch()) {
  default:
    break;

  case llvm::Triple::aarch64:
    AddAArch64TargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::x86_64:
    AddX86TargetArgs(Args, CmdArgs);
    break;
  }
}

namespace {
void RenderAArch64ABI(const llvm::Triple &Triple, const ArgList &Args,
                      ArgStringList &CmdArgs) {
  const char *ABIName = nullptr;
  if (Arg *A = Args.getLastArg(options::OPT_mabi_EQ))
    ABIName = A->getValue();
  else if (Triple.isOSDarwin())
    ABIName = "darwinpcs";
  else
    ABIName = "aapcs";

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName);
}
} // namespace

void NeverC::AddAArch64TargetArgs(const ArgList &Args,
                                  ArgStringList &CmdArgs) const {
  const llvm::Triple &Triple = getToolChain().getEffectiveTriple();

  if (!Args.hasFlag(options::OPT_mred_zone, options::OPT_mno_red_zone, true) ||
      Args.hasArg(options::OPT_mkernel))
    CmdArgs.push_back("-disable-red-zone");

  if (!Args.hasFlag(options::OPT_mimplicit_float,
                    options::OPT_mno_implicit_float, true))
    CmdArgs.push_back("-no-implicit-float");

  RenderAArch64ABI(Triple, Args, CmdArgs);

  // Forward the -mglobal-merge option for explicit control over the pass.
  if (Arg *A = Args.getLastArg(options::OPT_mglobal_merge,
                               options::OPT_mno_global_merge)) {
    CmdArgs.push_back("-mllvm");
    if (A->getOption().matches(options::OPT_mno_global_merge))
      CmdArgs.push_back("-aarch64-enable-global-merge=false");
    else
      CmdArgs.push_back("-aarch64-enable-global-merge=true");
  }

  // Enable/disable return address signing and indirect branch targets.
  collectARMPACBTIOptions(getToolChain(), Args, CmdArgs, true /*isAArch64*/);
  if (Arg *A = Args.getLastArg(options::OPT_msve_vector_bits_EQ)) {
    llvm::StringRef Val = A->getValue();
    const Driver &D = getToolChain().getDriver();
    if (Val.equals("128") || Val.equals("256") || Val.equals("512") ||
        Val.equals("1024") || Val.equals("2048") || Val.equals("128+") ||
        Val.equals("256+") || Val.equals("512+") || Val.equals("1024+") ||
        Val.equals("2048+")) {
      unsigned Bits = 0;
      if (Val.ends_with("+"))
        Val = Val.substr(0, Val.size() - 1);
      else {
        assert(!Val.getAsInteger(10, Bits) && "Failed to parse value");
        CmdArgs.push_back(
            Args.MakeArgString("-mvscale-max=" + llvm::Twine(Bits / 128)));
      }

      assert(!Val.getAsInteger(10, Bits) && "Failed to parse value");
      CmdArgs.push_back(
          Args.MakeArgString("-mvscale-min=" + llvm::Twine(Bits / 128)));
      // Silently drop requests for vector-length agnostic code as it's implied.
    } else if (!Val.equals("scalable"))
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Val;
  }

  if (const Arg *A = Args.getLastArg(neverc::driver::options::OPT_mtune_EQ)) {
    CmdArgs.push_back("-tune-cpu");
    if (strcmp(A->getValue(), "native") == 0)
      CmdArgs.push_back(Args.MakeArgString(llvm::sys::getHostCPUName()));
    else
      CmdArgs.push_back(A->getValue());
  }

  addUnalignedAccessWarning(CmdArgs);
}

void NeverC::AddX86TargetArgs(const ArgList &Args,
                              ArgStringList &CmdArgs) const {
  const Driver &D = getToolChain().getDriver();
  addX86AlignBranchArgs(D, Args, CmdArgs);

  if (!Args.hasFlag(options::OPT_mred_zone, options::OPT_mno_red_zone, true) ||
      Args.hasArg(options::OPT_mkernel))
    CmdArgs.push_back("-disable-red-zone");

  if (!Args.hasFlag(options::OPT_mtls_direct_seg_refs,
                    options::OPT_mno_tls_direct_seg_refs, true))
    CmdArgs.push_back("-mno-tls-direct-seg-refs");

  // Default to avoid implicit floating-point for kernel code, but allow
  // that to be overridden with -mno-soft-float.
  bool NoImplicitFloat = Args.hasArg(options::OPT_mkernel);
  if (Arg *A = Args.getLastArg(
          options::OPT_msoft_float, options::OPT_mno_soft_float,
          options::OPT_mimplicit_float, options::OPT_mno_implicit_float)) {
    const Option &O = A->getOption();
    NoImplicitFloat = (O.matches(options::OPT_mno_implicit_float) ||
                       O.matches(options::OPT_msoft_float));
  }
  if (NoImplicitFloat)
    CmdArgs.push_back("-no-implicit-float");

  if (Arg *A = Args.getLastArg(options::OPT_masm_EQ)) {
    llvm::StringRef Value = A->getValue();
    if (Value == "intel" || Value == "att") {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back(Args.MakeArgString("-x86-asm-syntax=" + Value));
      CmdArgs.push_back(Args.MakeArgString("-inline-asm=" + Value));
    } else {
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Value;
    }
  } else if (getToolChain().getTriple().isWindowsMSVCEnvironment()) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-x86-asm-syntax=intel");
  }

  if (Arg *A = Args.getLastArg(options::OPT_mskip_rax_setup,
                               options::OPT_mno_skip_rax_setup))
    if (A->getOption().matches(options::OPT_mskip_rax_setup))
      CmdArgs.push_back(Args.MakeArgString("-mskip-rax-setup"));

  std::string TuneCPU;
  if (!Args.hasArg(neverc::driver::options::OPT_march_EQ))
    TuneCPU = "generic";

  // Override based on -mtune.
  if (const Arg *A = Args.getLastArg(neverc::driver::options::OPT_mtune_EQ)) {
    llvm::StringRef Name = A->getValue();

    if (Name == "native") {
      Name = llvm::sys::getHostCPUName();
      if (!Name.empty())
        TuneCPU = std::string(Name);
    } else
      TuneCPU = std::string(Name);
  }

  if (!TuneCPU.empty()) {
    CmdArgs.push_back("-tune-cpu");
    CmdArgs.push_back(Args.MakeArgString(TuneCPU));
  }
}

// ===----------------------------------------------------------------------===
// Assembler, FP model & complex-range helpers
// ===----------------------------------------------------------------------===

namespace {

void collectArgsForIntegratedAssembler(Compilation &C, const ArgList &Args,
                                       ArgStringList &CmdArgs,
                                       const Driver &D) {
  if (useRelaxAll(C, Args))
    CmdArgs.push_back("-mrelax-all");

  // Only default to -mincremental-linker-compatible if we think we are
  // targeting the MSVC linker.
  bool DefaultIncrementalLinkerCompatible =
      C.getDefaultToolChain().getTriple().isWindowsMSVCEnvironment();
  if (Args.hasFlag(options::OPT_mincremental_linker_compatible,
                   options::OPT_mno_incremental_linker_compatible,
                   DefaultIncrementalLinkerCompatible))
    CmdArgs.push_back("-mincremental-linker-compatible");

  Args.AddLastArg(CmdArgs, options::OPT_femit_dwarf_unwind_EQ);

  Args.addOptInFlag(CmdArgs, options::OPT_femit_compact_unwind_non_canonical,
                    options::OPT_fno_emit_compact_unwind_non_canonical);

  // If you add more args here, also add them to the block below that
  // starts with "// If collectArgsForIntegratedAssembler() isn't called below".

  // When passing -I arguments to the assembler we sometimes need to
  // unconditionally take the next argument.  For example, when parsing
  // '-Wa,-I -Wa,foo' we need to accept the -Wa,foo arg after seeing the
  // -Wa,-I arg and when parsing '-Wa,-I,foo' we need to accept the 'foo'
  // arg after parsing the '-I' arg.
  bool TakeNextArg = false;

  bool UseRelaxRelocations = C.getDefaultToolChain().useRelaxRelocations();
  bool UseNoExecStack = false;
  for (const Arg *A :
       Args.filtered(options::OPT_Wa_COMMA, options::OPT_Xassembler,
                     options::OPT_mimplicit_it_EQ)) {
    A->claim();

    for (llvm::StringRef Value : A->getValues()) {
      if (TakeNextArg) {
        CmdArgs.push_back(Value.data());
        TakeNextArg = false;
        continue;
      }

      if (C.getDefaultToolChain().getTriple().isOSBinFormatCOFF() &&
          Value == "-mbig-obj")
        continue; // LLVM handles bigobj automatically

      if (Value == "-force_cpusubtype_ALL") {
        // Do nothing, this is the default and we don't support anything else.
      } else if (Value == "-L") {
        CmdArgs.push_back("-msave-temp-labels");
      } else if (Value == "--fatal-warnings") {
        CmdArgs.push_back("-massembler-fatal-warnings");
      } else if (Value == "--no-warn" || Value == "-W") {
        CmdArgs.push_back("-massembler-no-warn");
      } else if (Value == "--noexecstack") {
        UseNoExecStack = true;
      } else if (Value.starts_with("-compress-debug-sections") ||
                 Value.starts_with("--compress-debug-sections") ||
                 Value == "-nocompress-debug-sections" ||
                 Value == "--nocompress-debug-sections") {
        CmdArgs.push_back(Value.data());
      } else if (Value == "-mrelax-relocations=yes" ||
                 Value == "--mrelax-relocations=yes") {
        UseRelaxRelocations = true;
      } else if (Value == "-mrelax-relocations=no" ||
                 Value == "--mrelax-relocations=no") {
        UseRelaxRelocations = false;
      } else if (Value.starts_with("-I")) {
        CmdArgs.push_back(Value.data());
        // We need to consume the next argument if the current arg is a plain
        // -I. The next arg will be the include directory.
        if (Value == "-I")
          TakeNextArg = true;
      } else if (Value.starts_with("-gdwarf-")) {
        // "-gdwarf-N" options are not assembler options.
        unsigned DwarfVersion = DwarfVersionNum(Value);
        if (DwarfVersion ==
            0) { // Send it onward, and let the assembler complain.
          CmdArgs.push_back(Value.data());
        } else {
          renderDebugEnablingArgs(Args, CmdArgs,
                                  llvm::codegenoptions::DebugInfoConstructor,
                                  DwarfVersion, llvm::DebuggerKind::Default);
        }
      } else if (Value.starts_with("-mcpu") || Value.starts_with("-mfpu") ||
                 Value.starts_with("-mhwdiv") || Value.starts_with("-march")) {
        // Do nothing, we'll validate it later.
      } else if (Value == "-defsym") {
        if (A->getNumValues() != 2) {
          D.Diag(diag::err_drv_defsym_invalid_format) << Value;
          break;
        }
        const char *S = A->getValue(1);
        auto Pair = llvm::StringRef(S).split('=');
        auto Sym = Pair.first;
        auto SVal = Pair.second;

        if (Sym.empty() || SVal.empty()) {
          D.Diag(diag::err_drv_defsym_invalid_format) << S;
          break;
        }
        int64_t IVal;
        if (SVal.getAsInteger(0, IVal)) {
          D.Diag(diag::err_drv_defsym_invalid_symval) << SVal;
          break;
        }
        CmdArgs.push_back(Value.data());
        TakeNextArg = true;
      } else if (Value == "-fdebug-compilation-dir") {
        CmdArgs.push_back("-fdebug-compilation-dir");
        TakeNextArg = true;
      } else if (Value.consume_front("-fdebug-compilation-dir=")) {
        // The flag is a -Wa / -Xassembler argument and Options doesn't
        // parse the argument, so this isn't automatically aliased to
        // -fdebug-compilation-dir (without '=') here.
        CmdArgs.push_back("-fdebug-compilation-dir");
        CmdArgs.push_back(Value.data());
      } else if (Value == "--version") {
        D.PrintVersion(C, llvm::outs());
      } else {
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Value;
      }
    }
  }
  if (!UseRelaxRelocations)
    CmdArgs.push_back("-mrelax-relocations=no");
  if (UseNoExecStack)
    CmdArgs.push_back("-mnoexecstack");
}

llvm::StringRef enumComplexRangeToStr(LangOptions::ComplexRangeKind Range) {
  llvm::StringRef RangeStr = "";
  switch (Range) {
  case LangOptions::ComplexRangeKind::CX_Limited:
    return "-fcx-limited-range";
    break;
  case LangOptions::ComplexRangeKind::CX_Improved:
    return "-fcx-fortran-rules";
    break;
  default:
    return RangeStr;
    break;
  }
}

void genComplexRangeDiag(const Driver &D, LangOptions::ComplexRangeKind Range1,
                         LangOptions::ComplexRangeKind Range2) {
  if (Range1 != LangOptions::ComplexRangeKind::CX_Full)
    D.Diag(neverc::diag::warn_drv_overriding_option)
        << enumComplexRangeToStr(Range1) << enumComplexRangeToStr(Range2);
}

std::string renderComplexRangeOption(std::string Range) {
  std::string ComplexRangeStr = "-complex-range=";
  ComplexRangeStr += Range;
  return ComplexRangeStr;
}

void renderFloatingPointOptions(const ToolChain &TC, const Driver &D,
                                bool OFastEnabled, const ArgList &Args,
                                ArgStringList &CmdArgs, const JobAction &JA) {
  // Handle various floating point optimization flags, mapping them to the
  // appropriate LLVM code generation flags. This is complicated by several
  // "umbrella" flags, so we do this by stepping through the flags incrementally
  // adjusting what we think is enabled/disabled, then at the end setting the
  // LLVM flags based on the final state.
  bool HonorINFs = true;
  bool HonorNaNs = true;
  bool ApproxFunc = false;
  // -fmath-errno is the default on some platforms, e.g. BSD-derived OSes.
  bool MathErrno = TC.IsMathErrnoDefault();
  bool AssociativeMath = false;
  bool ReciprocalMath = false;
  bool SignedZeros = true;
  bool TrappingMath = false;        // Implemented via -ffp-exception-behavior
  bool TrappingMathPresent = false; // Is trapping-math in args, and not
                                    // overriden by ffp-exception-behavior?
  bool RoundingFPMath = false;
  bool RoundingMathPresent = false; // Is rounding-math in args?
  // -ffp-model values: strict, fast, precise
  llvm::StringRef FPModel = "";
  // -ffp-exception-behavior options: strict, maytrap, ignore
  llvm::StringRef FPExceptionBehavior = "";
  // -ffp-eval-method options: double, extended, source
  llvm::StringRef FPEvalMethod = "";
  const llvm::DenormalMode DefaultDenormalFPMath =
      TC.getDefaultDenormalModeForType(Args, JA);
  const llvm::DenormalMode DefaultDenormalFP32Math =
      TC.getDefaultDenormalModeForType(Args, JA, &llvm::APFloat::IEEEsingle());

  llvm::DenormalMode DenormalFPMath = DefaultDenormalFPMath;
  llvm::DenormalMode DenormalFP32Math = DefaultDenormalFP32Math;
  llvm::StringRef FPContract = "on";
  llvm::StringRef LastSeenFfpContractOption;
  bool SeenUnsafeMathModeOption = false;
  bool StrictFPModel = false;
  llvm::StringRef Float16ExcessPrecision = "";
  llvm::StringRef BFloat16ExcessPrecision = "";
  LangOptions::ComplexRangeKind Range = LangOptions::ComplexRangeKind::CX_Full;

  for (const Arg *A : Args) {
    auto optID = A->getOption().getID();
    bool PreciseFPModel = false;
    switch (optID) {
    default:
      break;
    case options::OPT_fcx_limited_range: {
      genComplexRangeDiag(D, Range, LangOptions::ComplexRangeKind::CX_Limited);
      Range = LangOptions::ComplexRangeKind::CX_Limited;
      std::string ComplexRangeStr = renderComplexRangeOption("limited");
      if (!ComplexRangeStr.empty())
        CmdArgs.push_back(Args.MakeArgString(ComplexRangeStr));
      break;
    }
    case options::OPT_fno_cx_limited_range:
      Range = LangOptions::ComplexRangeKind::CX_Full;
      break;
    case options::OPT_fcx_fortran_rules: {
      genComplexRangeDiag(D, Range, LangOptions::ComplexRangeKind::CX_Improved);
      Range = LangOptions::ComplexRangeKind::CX_Improved;
      std::string ComplexRangeStr = renderComplexRangeOption("fortran");
      if (!ComplexRangeStr.empty())
        CmdArgs.push_back(Args.MakeArgString(ComplexRangeStr));
      break;
    }
    case options::OPT_fno_cx_fortran_rules:
      Range = LangOptions::ComplexRangeKind::CX_Full;
      break;
    case options::OPT_ffp_model_EQ: {
      // If -ffp-model= is seen, reset to fno-fast-math
      HonorINFs = true;
      HonorNaNs = true;
      ApproxFunc = false;
      // Turning *off* -ffast-math restores the toolchain default.
      MathErrno = TC.IsMathErrnoDefault();
      AssociativeMath = false;
      ReciprocalMath = false;
      SignedZeros = true;
      // -fno_fast_math restores default denormal and fpcontract handling
      FPContract = "on";
      DenormalFPMath = llvm::DenormalMode::getIEEE();

      DenormalFP32Math = llvm::DenormalMode::getIEEE();

      llvm::StringRef Val = A->getValue();
      if (OFastEnabled && !Val.equals("fast")) {
        // Only -ffp-model=fast is compatible with OFast, ignore.
        D.Diag(neverc::diag::warn_drv_overriding_option)
            << Args.MakeArgString("-ffp-model=" + Val) << "-Ofast";
        break;
      }
      StrictFPModel = false;
      PreciseFPModel = true;
      // ffp-model= is a Driver option, it is entirely rewritten into more
      // granular options before being passed into the frontend.
      // Use the gcc option in the switch below.
      if (!FPModel.empty() && !FPModel.equals(Val))
        D.Diag(neverc::diag::warn_drv_overriding_option)
            << Args.MakeArgString("-ffp-model=" + FPModel)
            << Args.MakeArgString("-ffp-model=" + Val);
      if (Val.equals("fast")) {
        optID = options::OPT_ffast_math;
        FPModel = Val;
        FPContract = "fast";
      } else if (Val.equals("precise")) {
        optID = options::OPT_ffp_contract;
        FPModel = Val;
        FPContract = "on";
        PreciseFPModel = true;
      } else if (Val.equals("strict")) {
        StrictFPModel = true;
        optID = options::OPT_frounding_math;
        FPExceptionBehavior = "strict";
        FPModel = Val;
        FPContract = "off";
        TrappingMath = true;
      } else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
      break;
    }
    }

    switch (optID) {
    // If this isn't an FP option skip the claim below
    default:
      continue;

    // Options controlling individual features
    case options::OPT_fhonor_infinities:
      HonorINFs = true;
      break;
    case options::OPT_fno_honor_infinities:
      HonorINFs = false;
      break;
    case options::OPT_fhonor_nans:
      HonorNaNs = true;
      break;
    case options::OPT_fno_honor_nans:
      HonorNaNs = false;
      break;
    case options::OPT_fapprox_func:
      ApproxFunc = true;
      break;
    case options::OPT_fno_approx_func:
      ApproxFunc = false;
      break;
    case options::OPT_fmath_errno:
      MathErrno = true;
      break;
    case options::OPT_fno_math_errno:
      MathErrno = false;
      break;
    case options::OPT_fassociative_math:
      AssociativeMath = true;
      break;
    case options::OPT_fno_associative_math:
      AssociativeMath = false;
      break;
    case options::OPT_freciprocal_math:
      ReciprocalMath = true;
      break;
    case options::OPT_fno_reciprocal_math:
      ReciprocalMath = false;
      break;
    case options::OPT_fsigned_zeros:
      SignedZeros = true;
      break;
    case options::OPT_fno_signed_zeros:
      SignedZeros = false;
      break;
    case options::OPT_ftrapping_math:
      if (!TrappingMathPresent && !FPExceptionBehavior.empty() &&
          !FPExceptionBehavior.equals("strict"))
        // Warn that previous value of option is overridden.
        D.Diag(neverc::diag::warn_drv_overriding_option)
            << Args.MakeArgString("-ffp-exception-behavior=" +
                                  FPExceptionBehavior)
            << "-ftrapping-math";
      TrappingMath = true;
      TrappingMathPresent = true;
      FPExceptionBehavior = "strict";
      break;
    case options::OPT_fno_trapping_math:
      if (!TrappingMathPresent && !FPExceptionBehavior.empty() &&
          !FPExceptionBehavior.equals("ignore"))
        // Warn that previous value of option is overridden.
        D.Diag(neverc::diag::warn_drv_overriding_option)
            << Args.MakeArgString("-ffp-exception-behavior=" +
                                  FPExceptionBehavior)
            << "-fno-trapping-math";
      TrappingMath = false;
      TrappingMathPresent = true;
      FPExceptionBehavior = "ignore";
      break;

    case options::OPT_frounding_math:
      RoundingFPMath = true;
      RoundingMathPresent = true;
      break;

    case options::OPT_fno_rounding_math:
      RoundingFPMath = false;
      RoundingMathPresent = false;
      break;

    case options::OPT_fdenormal_fp_math_EQ:
      DenormalFPMath = llvm::parseDenormalFPAttribute(A->getValue());
      DenormalFP32Math = DenormalFPMath;
      if (!DenormalFPMath.isValid()) {
        D.Diag(diag::err_drv_invalid_value)
            << A->getAsString(Args) << A->getValue();
      }
      break;

    case options::OPT_fdenormal_fp_math_f32_EQ:
      DenormalFP32Math = llvm::parseDenormalFPAttribute(A->getValue());
      if (!DenormalFP32Math.isValid()) {
        D.Diag(diag::err_drv_invalid_value)
            << A->getAsString(Args) << A->getValue();
      }
      break;

    // Validate and pass through -ffp-contract option.
    case options::OPT_ffp_contract: {
      llvm::StringRef Val = A->getValue();
      if (PreciseFPModel) {
        // -ffp-model=precise enables ffp-contract=on.
        // -ffp-model=precise sets PreciseFPModel to on and Val to
        // "precise". FPContract is set.
        ;
      } else if (Val.equals("fast") || Val.equals("on") || Val.equals("off") ||
                 Val.equals("fast-honor-pragmas")) {
        FPContract = Val;
        LastSeenFfpContractOption = Val;
      } else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
      break;
    }

    // Validate and pass through -ffp-model option.
    case options::OPT_ffp_model_EQ:
      // This should only occur in the error case
      // since the optID has been replaced by a more granular
      // floating point option.
      break;

    // Validate and pass through -ffp-exception-behavior option.
    case options::OPT_ffp_exception_behavior_EQ: {
      llvm::StringRef Val = A->getValue();
      if (!TrappingMathPresent && !FPExceptionBehavior.empty() &&
          !FPExceptionBehavior.equals(Val))
        // Warn that previous value of option is overridden.
        D.Diag(neverc::diag::warn_drv_overriding_option)
            << Args.MakeArgString("-ffp-exception-behavior=" +
                                  FPExceptionBehavior)
            << Args.MakeArgString("-ffp-exception-behavior=" + Val);
      TrappingMath = TrappingMathPresent = false;
      if (Val.equals("ignore") || Val.equals("maytrap"))
        FPExceptionBehavior = Val;
      else if (Val.equals("strict")) {
        FPExceptionBehavior = Val;
        TrappingMath = TrappingMathPresent = true;
      } else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
      break;
    }

    // Validate and pass through -ffp-eval-method option.
    case options::OPT_ffp_eval_method_EQ: {
      llvm::StringRef Val = A->getValue();
      if (Val.equals("double") || Val.equals("extended") ||
          Val.equals("source"))
        FPEvalMethod = Val;
      else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
      break;
    }

    case options::OPT_fexcess_precision_EQ: {
      llvm::StringRef Val = A->getValue();
      const llvm::Triple::ArchType Arch = TC.getArch();
      if (Arch == llvm::Triple::x86_64) {
        if (Val.equals("standard") || Val.equals("fast"))
          Float16ExcessPrecision = Val;
        // To make it GCC compatible, allow the value of "16" which
        // means disable excess precision, the same meaning as neverc's
        // equivalent value "none".
        else if (Val.equals("16"))
          Float16ExcessPrecision = "none";
        else
          D.Diag(diag::err_drv_unsupported_option_argument)
              << A->getSpelling() << Val;
      } else {
        if (!(Val.equals("standard") || Val.equals("fast")))
          D.Diag(diag::err_drv_unsupported_option_argument)
              << A->getSpelling() << Val;
      }
      BFloat16ExcessPrecision = Float16ExcessPrecision;
      break;
    }
    case options::OPT_ffinite_math_only:
      HonorINFs = false;
      HonorNaNs = false;
      break;
    case options::OPT_fno_finite_math_only:
      HonorINFs = true;
      HonorNaNs = true;
      break;

    case options::OPT_funsafe_math_optimizations:
      AssociativeMath = true;
      ReciprocalMath = true;
      SignedZeros = false;
      ApproxFunc = true;
      TrappingMath = false;
      FPExceptionBehavior = "";
      FPContract = "fast";
      SeenUnsafeMathModeOption = true;
      break;
    case options::OPT_fno_unsafe_math_optimizations:
      AssociativeMath = false;
      ReciprocalMath = false;
      SignedZeros = true;
      ApproxFunc = false;
      TrappingMath = true;
      FPExceptionBehavior = "strict";

      // The target may have opted to flush by default, so force IEEE.
      DenormalFPMath = llvm::DenormalMode::getIEEE();
      DenormalFP32Math = llvm::DenormalMode::getIEEE();
      if (LastSeenFfpContractOption != "") {
        FPContract = LastSeenFfpContractOption;
      } else if (SeenUnsafeMathModeOption)
        FPContract = "on";
      break;

    case options::OPT_Ofast:
      // If -Ofast is the optimization level, then -ffast-math should be enabled
      if (!OFastEnabled)
        continue;
      [[fallthrough]];
    case options::OPT_ffast_math: {
      HonorINFs = false;
      HonorNaNs = false;
      MathErrno = false;
      AssociativeMath = true;
      ReciprocalMath = true;
      ApproxFunc = true;
      SignedZeros = false;
      TrappingMath = false;
      RoundingFPMath = false;
      FPExceptionBehavior = "";
      // If fast-math is set then set the fp-contract mode to fast.
      FPContract = "fast";
      SeenUnsafeMathModeOption = true;
      // ffast-math enables fortran rules for complex multiplication and
      // division.
      std::string ComplexRangeStr = renderComplexRangeOption("limited");
      if (!ComplexRangeStr.empty())
        CmdArgs.push_back(Args.MakeArgString(ComplexRangeStr));
      break;
    }
    case options::OPT_fno_fast_math:
      HonorINFs = true;
      HonorNaNs = true;
      // Turning on -ffast-math (with either flag) removes the need for
      // MathErrno. However, turning *off* -ffast-math merely restores the
      // toolchain default (which may be false).
      MathErrno = TC.IsMathErrnoDefault();
      AssociativeMath = false;
      ReciprocalMath = false;
      ApproxFunc = false;
      SignedZeros = true;
      // -fno_fast_math restores default denormal and fpcontract handling
      DenormalFPMath = DefaultDenormalFPMath;
      DenormalFP32Math = llvm::DenormalMode::getIEEE();
      if (LastSeenFfpContractOption != "") {
        FPContract = LastSeenFfpContractOption;
      } else if (SeenUnsafeMathModeOption)
        FPContract = "on";
      break;
    }
    if (StrictFPModel) {
      // If -ffp-model=strict has been specified on command line but
      // subsequent options conflict then emit warning diagnostic.
      if (HonorINFs && HonorNaNs && !AssociativeMath && !ReciprocalMath &&
          SignedZeros && TrappingMath && RoundingFPMath && !ApproxFunc &&
          DenormalFPMath == llvm::DenormalMode::getIEEE() &&
          DenormalFP32Math == llvm::DenormalMode::getIEEE() &&
          FPContract.equals("off"))
        // OK: Current Arg doesn't conflict with -ffp-model=strict
        ;
      else {
        StrictFPModel = false;
        FPModel = "";
        auto RHS = (A->getNumValues() == 0)
                       ? A->getSpelling()
                       : Args.MakeArgString(A->getSpelling() + A->getValue());
        if (RHS != "-ffp-model=strict")
          D.Diag(neverc::diag::warn_drv_overriding_option)
              << "-ffp-model=strict" << RHS;
      }
    }

    // If we handled this option claim it
    A->claim();
  }

  if (!HonorINFs)
    CmdArgs.push_back("-menable-no-infs");

  if (!HonorNaNs)
    CmdArgs.push_back("-menable-no-nans");

  if (ApproxFunc)
    CmdArgs.push_back("-fapprox-func");

  if (MathErrno)
    CmdArgs.push_back("-fmath-errno");

  if (AssociativeMath && ReciprocalMath && !SignedZeros && ApproxFunc &&
      !TrappingMath)
    CmdArgs.push_back("-funsafe-math-optimizations");

  if (!SignedZeros)
    CmdArgs.push_back("-fno-signed-zeros");

  if (AssociativeMath && !SignedZeros && !TrappingMath)
    CmdArgs.push_back("-mreassociate");

  if (ReciprocalMath)
    CmdArgs.push_back("-freciprocal-math");

  if (TrappingMath) {
    // FP Exception Behavior is also set to strict
    assert(FPExceptionBehavior.equals("strict"));
  }

  // The default is IEEE.
  if (DenormalFPMath != llvm::DenormalMode::getIEEE()) {
    llvm::SmallString<64> DenormFlag;
    llvm::raw_svector_ostream ArgStr(DenormFlag);
    ArgStr << "-fdenormal-fp-math=" << DenormalFPMath;
    CmdArgs.push_back(Args.MakeArgString(ArgStr.str()));
  }

  if (DenormalFP32Math != DenormalFPMath) {
    llvm::SmallString<64> DenormFlag;
    llvm::raw_svector_ostream ArgStr(DenormFlag);
    ArgStr << "-fdenormal-fp-math-f32=" << DenormalFP32Math;
    CmdArgs.push_back(Args.MakeArgString(ArgStr.str()));
  }

  if (!FPContract.empty())
    CmdArgs.push_back(Args.MakeArgString("-ffp-contract=" + FPContract));

  if (!RoundingFPMath)
    CmdArgs.push_back(Args.MakeArgString("-fno-rounding-math"));

  if (RoundingFPMath && RoundingMathPresent)
    CmdArgs.push_back(Args.MakeArgString("-frounding-math"));

  if (!FPExceptionBehavior.empty())
    CmdArgs.push_back(
        Args.MakeArgString("-ffp-exception-behavior=" + FPExceptionBehavior));

  if (!FPEvalMethod.empty())
    CmdArgs.push_back(Args.MakeArgString("-ffp-eval-method=" + FPEvalMethod));

  if (!Float16ExcessPrecision.empty())
    CmdArgs.push_back(Args.MakeArgString("-ffloat16-excess-precision=" +
                                         Float16ExcessPrecision));
  if (!BFloat16ExcessPrecision.empty())
    CmdArgs.push_back(Args.MakeArgString("-fbfloat16-excess-precision=" +
                                         BFloat16ExcessPrecision));

  parseMRecip(D, Args, CmdArgs);

  // -ffast-math enables the __FAST_MATH__ preprocessor macro, but check for the
  // individual features enabled by -ffast-math instead of the option itself as
  // that's consistent with gcc's behaviour.
  if (!HonorINFs && !HonorNaNs && !MathErrno && AssociativeMath && ApproxFunc &&
      ReciprocalMath && !SignedZeros && !TrappingMath && !RoundingFPMath) {
    CmdArgs.push_back("-ffast-math");
    if (FPModel.equals("fast")) {
      if (FPContract.equals("fast"))
        // All set, do nothing.
        ;
      else if (FPContract.empty())
        // Enable -ffp-contract=fast
        CmdArgs.push_back(Args.MakeArgString("-ffp-contract=fast"));
      else
        D.Diag(neverc::diag::warn_drv_overriding_option)
            << "-ffp-model=fast"
            << Args.MakeArgString("-ffp-contract=" + FPContract);
    }
  }
  if (!HonorINFs && !HonorNaNs)
    CmdArgs.push_back("-ffinite-math-only");

  if (const Arg *A = Args.getLastArg(options::OPT_mfpmath_EQ)) {
    CmdArgs.push_back("-mfpmath");
    CmdArgs.push_back(A->getValue());
  }

  // Disable a codegen optimization for floating-point casts.
  if (Args.hasFlag(options::OPT_fno_strict_float_cast_overflow,
                   options::OPT_fstrict_float_cast_overflow, false))
    CmdArgs.push_back("-fno-strict-float-cast-overflow");

  if (Args.hasArg(options::OPT_fcx_limited_range))
    CmdArgs.push_back("-fcx-limited-range");
  if (Args.hasArg(options::OPT_fcx_fortran_rules))
    CmdArgs.push_back("-fcx-fortran-rules");
  if (Args.hasArg(options::OPT_fno_cx_limited_range))
    CmdArgs.push_back("-fno-cx-limited-range");
  if (Args.hasArg(options::OPT_fno_cx_fortran_rules))
    CmdArgs.push_back("-fno-cx-fortran-rules");
}

bool isValidSymbolName(llvm::StringRef S) {
  if (S.empty())
    return false;

  if (std::isdigit(S[0]))
    return false;

  return llvm::all_of(S, [](char C) { return std::isalnum(C) || C == '_'; });
}

void renderSSPOptions(const Driver &D, const ToolChain &TC, const ArgList &Args,
                      ArgStringList &CmdArgs, bool Kernel) {
  const llvm::Triple &EffectiveTriple = TC.getEffectiveTriple();

  LangOptions::StackProtectorMode StackProtectorLevel = LangOptions::SSPOff;
  LangOptions::StackProtectorMode DefaultStackProtectorLevel =
      TC.GetDefaultStackProtectorLevel(Kernel);

  if (Arg *A = Args.getLastArg(options::OPT_fno_stack_protector,
                               options::OPT_fstack_protector_all,
                               options::OPT_fstack_protector_strong,
                               options::OPT_fstack_protector)) {
    if (A->getOption().matches(options::OPT_fstack_protector))
      StackProtectorLevel =
          std::max<>(LangOptions::SSPOn, DefaultStackProtectorLevel);
    else if (A->getOption().matches(options::OPT_fstack_protector_strong))
      StackProtectorLevel = LangOptions::SSPStrong;
    else if (A->getOption().matches(options::OPT_fstack_protector_all))
      StackProtectorLevel = LangOptions::SSPReq;

  } else {
    StackProtectorLevel = DefaultStackProtectorLevel;
  }

  if (StackProtectorLevel) {
    CmdArgs.push_back("-stack-protector");
    CmdArgs.push_back(Args.MakeArgString(llvm::Twine(StackProtectorLevel)));
  }

  // --param ssp-buffer-size=
  for (const Arg *A : Args.filtered(options::OPT__param)) {
    llvm::StringRef Str(A->getValue());
    if (Str.starts_with("ssp-buffer-size=")) {
      if (StackProtectorLevel) {
        llvm::StringRef Val = Str.drop_front(16);
        unsigned Dummy;
        if (Val.getAsInteger(10, Dummy))
          D.Diag(diag::err_drv_invalid_int_value)
              << A->getAsString(Args) << Val;
        else {
          CmdArgs.push_back("-stack-protector-buffer-size");
          CmdArgs.push_back(Args.MakeArgString(Val));
        }
      }
      A->claim();
    }
  }

  const std::string &TripleStr = EffectiveTriple.getTriple();
  if (Arg *A = Args.getLastArg(options::OPT_mstack_protector_guard_EQ)) {
    llvm::StringRef Value = A->getValue();
    if (!EffectiveTriple.isX86() && !EffectiveTriple.isAArch64())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    if (EffectiveTriple.isX86() && Value != "tls" && Value != "global") {
      D.Diag(diag::err_drv_invalid_value_with_suggestion)
          << A->getOption().getName() << Value << "tls global";
      return;
    }
    if (EffectiveTriple.isAArch64() && Value != "sysreg" && Value != "global") {
      D.Diag(diag::err_drv_invalid_value_with_suggestion)
          << A->getOption().getName() << Value << "sysreg global";
      return;
    }
    A->render(Args, CmdArgs);
  }

  if (Arg *A = Args.getLastArg(options::OPT_mstack_protector_guard_offset_EQ)) {
    llvm::StringRef Value = A->getValue();
    if (!EffectiveTriple.isX86() && !EffectiveTriple.isAArch64())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    int Offset;
    if (Value.getAsInteger(10, Offset)) {
      D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Value;
      return;
    }
    A->render(Args, CmdArgs);
  }

  if (Arg *A = Args.getLastArg(options::OPT_mstack_protector_guard_reg_EQ)) {
    llvm::StringRef Value = A->getValue();
    if (!EffectiveTriple.isX86() && !EffectiveTriple.isAArch64())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    if (EffectiveTriple.isX86() && (Value != "fs" && Value != "gs")) {
      D.Diag(diag::err_drv_invalid_value_with_suggestion)
          << A->getOption().getName() << Value << "fs gs";
      return;
    }
    if (EffectiveTriple.isAArch64() && Value != "sp_el0") {
      D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Value;
      return;
    }
    A->render(Args, CmdArgs);
  }

  if (Arg *A = Args.getLastArg(options::OPT_mstack_protector_guard_symbol_EQ)) {
    llvm::StringRef Value = A->getValue();
    if (!isValidSymbolName(Value)) {
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << A->getOption().getName() << "legal symbol name";
      return;
    }
    A->render(Args, CmdArgs);
  }
}

void renderSCPOptions(const ToolChain &TC, const ArgList &Args,
                      ArgStringList &CmdArgs) {
  const llvm::Triple &EffectiveTriple = TC.getEffectiveTriple();

  if (!EffectiveTriple.isOSLinux())
    return;

  if (!EffectiveTriple.isX86() && !EffectiveTriple.isAArch64())
    return;

  Args.addOptInFlag(CmdArgs, options::OPT_fstack_clash_protection,
                    options::OPT_fno_stack_clash_protection);
}

void renderTrivialAutoVarInitOptions(const Driver &D, const ToolChain &TC,
                                     const ArgList &Args,
                                     ArgStringList &CmdArgs) {
  auto DefaultTrivialAutoVarInit = TC.GetDefaultTrivialAutoVarInit();
  llvm::StringRef TrivialAutoVarInit = "";

  for (const Arg *A : Args) {
    switch (A->getOption().getID()) {
    default:
      continue;
    case options::OPT_ftrivial_auto_var_init: {
      A->claim();
      llvm::StringRef Val = A->getValue();
      if (Val == "uninitialized" || Val == "zero" || Val == "pattern")
        TrivialAutoVarInit = Val;
      else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
      break;
    }
    }
  }

  if (TrivialAutoVarInit.empty())
    switch (DefaultTrivialAutoVarInit) {
    case LangOptions::TrivialAutoVarInitKind::Uninitialized:
      break;
    case LangOptions::TrivialAutoVarInitKind::Pattern:
      TrivialAutoVarInit = "pattern";
      break;
    case LangOptions::TrivialAutoVarInitKind::Zero:
      TrivialAutoVarInit = "zero";
      break;
    }

  if (!TrivialAutoVarInit.empty()) {
    if (TrivialAutoVarInit == "zero" &&
        !Args.hasArg(options::OPT_enable_trivial_var_init_zero))
      D.Diag(diag::warn_drv_trivial_auto_var_init_zero_disabled);
    CmdArgs.push_back(
        Args.MakeArgString("-ftrivial-auto-var-init=" + TrivialAutoVarInit));
  }

  if (Arg *A =
          Args.getLastArg(options::OPT_ftrivial_auto_var_init_stop_after)) {
    if (!Args.hasArg(options::OPT_ftrivial_auto_var_init) ||
        llvm::StringRef(
            Args.getLastArg(options::OPT_ftrivial_auto_var_init)->getValue()) ==
            "uninitialized")
      D.Diag(diag::err_drv_trivial_auto_var_init_stop_after_missing_dependency);
    A->claim();
    llvm::StringRef Val = A->getValue();
    if (std::stoi(Val.str()) <= 0)
      D.Diag(diag::err_drv_trivial_auto_var_init_stop_after_invalid_value);
    CmdArgs.push_back(
        Args.MakeArgString("-ftrivial-auto-var-init-stop-after=" + Val));
  }
}

void renderBuiltinOptions(const ToolChain &TC, const llvm::Triple &T,
                          const ArgList &Args, ArgStringList &CmdArgs) {
  // -fbuiltin is default unless -mkernel is used.
  bool UseBuiltins =
      Args.hasFlag(options::OPT_fbuiltin, options::OPT_fno_builtin,
                   !Args.hasArg(options::OPT_mkernel));
  if (!UseBuiltins)
    CmdArgs.push_back("-fno-builtin");

  // -ffreestanding implies -fno-builtin.
  if (Args.hasArg(options::OPT_ffreestanding))
    UseBuiltins = false;

  for (const Arg *A : Args.filtered(options::OPT_fno_builtin_)) {
    A->claim();

    // If -fno-builtin is specified, then there's no need to pass the option to
    // the frontend.
    if (UseBuiltins)
      A->render(Args, CmdArgs);
  }
}

void renderCharacterOptions(const ArgList &Args, const llvm::Triple &T,
                            ArgStringList &CmdArgs) {
  // -fsigned-char is default.
  if (const Arg *A = Args.getLastArg(
          options::OPT_fsigned_char, options::OPT_fno_signed_char,
          options::OPT_funsigned_char, options::OPT_fno_unsigned_char)) {
    if (A->getOption().matches(options::OPT_funsigned_char) ||
        A->getOption().matches(options::OPT_fno_signed_char)) {
      CmdArgs.push_back("-fno-signed-char");
    }
  } else if (!isSignedCharDefault(T)) {
    CmdArgs.push_back("-fno-signed-char");
  }

  if (const Arg *A = Args.getLastArg(options::OPT_fshort_wchar,
                                     options::OPT_fno_short_wchar)) {
    if (A->getOption().matches(options::OPT_fshort_wchar)) {
      CmdArgs.push_back("-fwchar-type=short");
      CmdArgs.push_back("-fno-signed-wchar");
    } else {
      bool IsAArch64 = T.isAArch64();
      CmdArgs.push_back("-fwchar-type=int");
      if (IsAArch64 && !T.isOSWindows())
        CmdArgs.push_back("-fno-signed-wchar");
      else
        CmdArgs.push_back("-fsigned-wchar");
    }
  }
}

void renderDiagnosticsOptions(const Driver &D, const ArgList &Args,
                              ArgStringList &CmdArgs) {
  bool CaretDefault = true;
  bool ColumnDefault = true;

  if (const Arg *A = Args.getLastArg(options::OPT_fms_diagnostics)) {
    llvm::StringRef Val = A->getValue();
    if (Val == "caret") {
      CaretDefault = true;
      ColumnDefault = true;
    } else if (Val == "column") {
      CaretDefault = false;
      ColumnDefault = true;
    } else if (Val == "classic") {
      CaretDefault = false;
      ColumnDefault = false;
    }
  }

  // -fcaret-diagnostics is default.
  if (!Args.hasFlag(options::OPT_fcaret_diagnostics,
                    options::OPT_fno_caret_diagnostics, CaretDefault))
    CmdArgs.push_back("-fno-caret-diagnostics");

  Args.addOptOutFlag(CmdArgs, options::OPT_fdiagnostics_fixit_info,
                     options::OPT_fno_diagnostics_fixit_info);
  Args.addOptOutFlag(CmdArgs, options::OPT_fdiagnostics_show_option,
                     options::OPT_fno_diagnostics_show_option);

  if (const Arg *A =
          Args.getLastArg(options::OPT_fdiagnostics_show_category_EQ)) {
    CmdArgs.push_back("-fdiagnostics-show-category");
    CmdArgs.push_back(A->getValue());
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fdiagnostics_show_hotness,
                    options::OPT_fno_diagnostics_show_hotness);

  if (const Arg *A =
          Args.getLastArg(options::OPT_fdiagnostics_hotness_threshold_EQ)) {
    std::string Opt =
        std::string("-fdiagnostics-hotness-threshold=") + A->getValue();
    CmdArgs.push_back(Args.MakeArgString(Opt));
  }

  if (const Arg *A = Args.getLastArg(options::OPT_fdiagnostics_format_EQ)) {
    CmdArgs.push_back("-fdiagnostics-format");
    CmdArgs.push_back(A->getValue());
  }

  if (const Arg *A = Args.getLastArg(
          options::OPT_fdiagnostics_show_note_include_stack,
          options::OPT_fno_diagnostics_show_note_include_stack)) {
    const Option &O = A->getOption();
    if (O.matches(options::OPT_fdiagnostics_show_note_include_stack))
      CmdArgs.push_back("-fdiagnostics-show-note-include-stack");
    else
      CmdArgs.push_back("-fno-diagnostics-show-note-include-stack");
  }

  // Color diagnostics are parsed by the driver directly from argv and later
  // re-parsed to construct this job; claim any possible color diagnostic here
  // to avoid warn_drv_unused_argument and diagnose bad
  // OPT_fdiagnostics_color_EQ values.
  Args.getLastArg(options::OPT_fcolor_diagnostics,
                  options::OPT_fno_color_diagnostics);
  if (const Arg *A = Args.getLastArg(options::OPT_fdiagnostics_color_EQ)) {
    llvm::StringRef Value(A->getValue());
    if (Value != "always" && Value != "never" && Value != "auto")
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << Value << A->getOption().getName();
  }

  if (D.getDiags().getDiagnosticOptions().ShowColors)
    CmdArgs.push_back("-fcolor-diagnostics");

  if (Args.hasArg(options::OPT_fansi_escape_codes))
    CmdArgs.push_back("-fansi-escape-codes");

  Args.addOptOutFlag(CmdArgs, options::OPT_fshow_source_location,
                     options::OPT_fno_show_source_location);

  Args.addOptOutFlag(CmdArgs, options::OPT_fdiagnostics_show_line_numbers,
                     options::OPT_fno_diagnostics_show_line_numbers);

  if (Args.hasArg(options::OPT_fdiagnostics_absolute_paths))
    CmdArgs.push_back("-fdiagnostics-absolute-paths");

  if (!Args.hasFlag(options::OPT_fshow_column, options::OPT_fno_show_column,
                    ColumnDefault))
    CmdArgs.push_back("-fno-show-column");

  Args.addOptOutFlag(CmdArgs, options::OPT_fspell_checking,
                     options::OPT_fno_spell_checking);
}

} // namespace

// ===----------------------------------------------------------------------===
// Debug, DWARF & runtime library options
// ===----------------------------------------------------------------------===

DwarfFissionKind tools::getDebugFissionKind(const Driver &D,
                                            const ArgList &Args, Arg *&Arg) {
  Arg = Args.getLastArg(options::OPT_gsplit_dwarf, options::OPT_gsplit_dwarf_EQ,
                        options::OPT_gno_split_dwarf);
  if (!Arg || Arg->getOption().matches(options::OPT_gno_split_dwarf))
    return DwarfFissionKind::None;

  if (Arg->getOption().matches(options::OPT_gsplit_dwarf))
    return DwarfFissionKind::Split;

  llvm::StringRef Value = Arg->getValue();
  if (Value == "split")
    return DwarfFissionKind::Split;
  if (Value == "single")
    return DwarfFissionKind::Single;

  D.Diag(diag::err_drv_unsupported_option_argument)
      << Arg->getSpelling() << Arg->getValue();
  return DwarfFissionKind::None;
}

namespace {

void renderDwarfFormat(const Driver &D, const llvm::Triple &T,
                       const ArgList &Args, ArgStringList &CmdArgs,
                       unsigned DwarfVersion) {
  auto *DwarfFormatArg =
      Args.getLastArg(options::OPT_gdwarf64, options::OPT_gdwarf32);
  if (!DwarfFormatArg)
    return;

  if (DwarfFormatArg->getOption().matches(options::OPT_gdwarf64)) {
    if (DwarfVersion < 3)
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << DwarfFormatArg->getAsString(Args) << "DWARFv3 or greater";
    else if (!T.isArch64Bit())
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << DwarfFormatArg->getAsString(Args) << "64 bit architecture";
    else if (!T.isOSBinFormatELF())
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << DwarfFormatArg->getAsString(Args) << "ELF platforms";
  }

  DwarfFormatArg->render(Args, CmdArgs);
}

void renderDebugOptions(const ToolChain &TC, const Driver &D,
                        const llvm::Triple &T, const ArgList &Args,
                        bool IRInput, ArgStringList &CmdArgs,
                        const InputInfo &Output,
                        llvm::codegenoptions::DebugInfoKind &DebugInfoKind,
                        DwarfFissionKind &DwarfFission) {
  // The 'g' groups options involve a somewhat intricate sequence of decisions
  // about what to pass from the driver to the frontend, but by the time they
  // reach the frontend they've been factored into three well-defined orthogonal
  // choices:
  //  * what level of debug info to generate
  //  * what dwarf version to write
  //  * what debugger tuning to use
  // This avoids having to monkey around further in the frontend.
  llvm::DebuggerKind DebuggerTuning = TC.getDefaultDebuggerTuning();

  bool SplitDWARFInlining =
      Args.hasFlag(options::OPT_fsplit_dwarf_inlining,
                   options::OPT_fno_split_dwarf_inlining, false);

  // Normally -gsplit-dwarf is only useful with -gN. For IR input, neverc does
  // object file generation and no IR generation, -gN should not be needed. So
  // allow -gsplit-dwarf with either -gN or IR input.
  if (IRInput || Args.hasArg(options::OPT_g_Group)) {
    Arg *SplitDWARFArg;
    DwarfFission = getDebugFissionKind(D, Args, SplitDWARFArg);
    if (DwarfFission != DwarfFissionKind::None &&
        !checkDebugInfoOption(SplitDWARFArg, Args, D, TC)) {
      DwarfFission = DwarfFissionKind::None;
      SplitDWARFInlining = false;
    }
  }
  if (const Arg *A = Args.getLastArg(options::OPT_g_Group)) {
    DebugInfoKind = llvm::codegenoptions::DebugInfoConstructor;

    // If the last option explicitly specified a debug-info level, use it.
    if (checkDebugInfoOption(A, Args, D, TC) &&
        A->getOption().matches(options::OPT_gN_Group)) {
      DebugInfoKind = debugLevelToInfoKind(*A);
      // For -g0 or -gline-tables-only, drop -gsplit-dwarf. This gets a bit more
      // complicated if you've disabled inline info in the skeleton CUs
      // (SplitDWARFInlining) - then there's value in composing split-dwarf and
      // line-tables-only, so let those compose naturally in that case.
      if (DebugInfoKind == llvm::codegenoptions::NoDebugInfo ||
          DebugInfoKind == llvm::codegenoptions::DebugDirectivesOnly ||
          (DebugInfoKind == llvm::codegenoptions::DebugLineTablesOnly &&
           SplitDWARFInlining))
        DwarfFission = DwarfFissionKind::None;
    }
  }

  if (const Arg *A =
          Args.getLastArg(options::OPT_gTune_Group, options::OPT_ggdbN_Group)) {
    if (checkDebugInfoOption(A, Args, D, TC)) {
      if (A->getOption().matches(options::OPT_glldb))
        DebuggerTuning = llvm::DebuggerKind::LLDB;
      else
        DebuggerTuning = llvm::DebuggerKind::GDB;
    }
  }

  bool GenDwarf = false;
  if (const Arg *A = getDwarfNArg(Args))
    GenDwarf = checkDebugInfoOption(A, Args, D, TC);

  if (!GenDwarf && DebugInfoKind != llvm::codegenoptions::NoDebugInfo)
    GenDwarf = true;

  unsigned RequestedDWARFVersion = 0; // DWARF version requested by the user
  unsigned EffectiveDWARFVersion = 0; // DWARF version TC can generate. It may
                                      // be lower than what the user wanted.
  if (GenDwarf) {
    RequestedDWARFVersion = getDwarfVersion(TC, Args);
    // Clamp effective DWARF version to the max supported by the toolchain.
    EffectiveDWARFVersion =
        std::min(RequestedDWARFVersion, TC.getMaxDwarfVersion());
  } else {
    Args.ClaimAllArgs(options::OPT_fdebug_default_version);
  }

  // -gline-directives-only supported only for the DWARF debug info.
  if (RequestedDWARFVersion == 0 &&
      DebugInfoKind == llvm::codegenoptions::DebugDirectivesOnly)
    DebugInfoKind = llvm::codegenoptions::NoDebugInfo;

  if (const Arg *A = Args.getLastArg(options::OPT_gstrict_dwarf))
    (void)checkDebugInfoOption(A, Args, D, TC);
  if (Args.hasFlag(options::OPT_gstrict_dwarf, options::OPT_gno_strict_dwarf,
                   false))
    CmdArgs.push_back("-gstrict-dwarf");

  // And we handle flag -grecord-gcc-switches later with DWARFDebugFlags.
  Args.ClaimAllArgs(options::OPT_g_flags_Group);

  if (const Arg *A = Args.getLastArg(options::OPT_gcolumn_info))
    (void)checkDebugInfoOption(A, Args, D, TC);
  if (!Args.hasFlag(options::OPT_gcolumn_info, options::OPT_gno_column_info,
                    true))
    CmdArgs.push_back("-gno-column-info");

  if (T.isOSBinFormatELF() && SplitDWARFInlining)
    CmdArgs.push_back("-fsplit-dwarf-inlining");

  // After we've dealt with all combinations of things that could
  // make DebugInfoKind be other than None or DebugLineTablesOnly,
  // figure out if we need to "upgrade" it to standalone debug info.
  // We parse these two '-f' options whether or not they will be used,
  // to claim them even if you wrote "-fstandalone-debug -gline-tables-only"
  bool NeedFullDebug = Args.hasFlag(
      options::OPT_fstandalone_debug, options::OPT_fno_standalone_debug,
      DebuggerTuning == llvm::DebuggerKind::LLDB ||
          TC.GetDefaultStandaloneDebug());
  if (const Arg *A = Args.getLastArg(options::OPT_fstandalone_debug))
    (void)checkDebugInfoOption(A, Args, D, TC);

  if (DebugInfoKind == llvm::codegenoptions::LimitedDebugInfo ||
      DebugInfoKind == llvm::codegenoptions::DebugInfoConstructor) {
    if (Args.hasFlag(options::OPT_fno_eliminate_unused_debug_types,
                     options::OPT_feliminate_unused_debug_types, false))
      DebugInfoKind = llvm::codegenoptions::UnusedTypeInfo;
    else if (NeedFullDebug)
      DebugInfoKind = llvm::codegenoptions::FullDebugInfo;
  }

  if (Args.hasFlag(options::OPT_gembed_source, options::OPT_gno_embed_source,
                   false)) {
    // Source embedding is a vendor extension to DWARF v5. By now we have
    // checked if a DWARF version was stated explicitly, and have otherwise
    // fallen back to the target default, so if this is still not at least 5
    // we emit an error.
    const Arg *A = Args.getLastArg(options::OPT_gembed_source);
    if (RequestedDWARFVersion < 5)
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << A->getAsString(Args) << "-gdwarf-5";
    else if (EffectiveDWARFVersion < 5)
      // The toolchain has reduced allowed dwarf version, so we can't enable
      // -gembed-source.
      D.Diag(diag::warn_drv_dwarf_version_limited_by_target)
          << A->getAsString(Args) << TC.getTripleString() << 5
          << EffectiveDWARFVersion;
    else if (checkDebugInfoOption(A, Args, D, TC))
      CmdArgs.push_back("-gembed-source");
  }

  Args.addOptOutFlag(CmdArgs, options::OPT_ginline_line_tables,
                     options::OPT_gno_inline_line_tables);

  // When emitting remarks, we need at least debug lines in the output.
  if (willEmitRemarks(Args) &&
      DebugInfoKind <= llvm::codegenoptions::DebugDirectivesOnly)
    DebugInfoKind = llvm::codegenoptions::DebugLineTablesOnly;

  // Adjust the debug info kind for the given toolchain.
  TC.adjustDebugInfoKind(DebugInfoKind, Args);

  renderDebugEnablingArgs(Args, CmdArgs, DebugInfoKind, EffectiveDWARFVersion,
                          DebuggerTuning);

  // -ggnu-pubnames turns on gnu style pubnames in the backend.
  const auto *PubnamesArg =
      Args.getLastArg(options::OPT_ggnu_pubnames, options::OPT_gno_gnu_pubnames,
                      options::OPT_gpubnames, options::OPT_gno_pubnames);
  if (DwarfFission != DwarfFissionKind::None ||
      (PubnamesArg && checkDebugInfoOption(PubnamesArg, Args, D, TC)))
    if (!PubnamesArg ||
        (!PubnamesArg->getOption().matches(options::OPT_gno_gnu_pubnames) &&
         !PubnamesArg->getOption().matches(options::OPT_gno_pubnames)))
      CmdArgs.push_back(PubnamesArg && PubnamesArg->getOption().matches(
                                           options::OPT_gpubnames)
                            ? "-gpubnames"
                            : "-ggnu-pubnames");
  if (const Arg *A = Args.getLastArg(options::OPT_gsrc_hash_EQ)) {
    llvm::StringRef v = A->getValue();
    CmdArgs.push_back(Args.MakeArgString("-gsrc-hash=" + v));
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fdebug_ranges_base_address,
                    options::OPT_fno_debug_ranges_base_address);

  // -gdwarf-aranges turns on the emission of the aranges section in the
  // backend.
  if (const Arg *A = Args.getLastArg(options::OPT_gdwarf_aranges);
      A && checkDebugInfoOption(A, Args, D, TC)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-generate-arange-section");
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fforce_dwarf_frame,
                    options::OPT_fno_force_dwarf_frame);

  if (Args.hasFlag(options::OPT_fdebug_types_section,
                   options::OPT_fno_debug_types_section, false)) {
    if (!T.isOSBinFormatELF()) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << Args.getLastArg(options::OPT_fdebug_types_section)
                 ->getAsString(Args)
          << T.getTriple();
    } else if (checkDebugInfoOption(
                   Args.getLastArg(options::OPT_fdebug_types_section), Args, D,
                   TC)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-generate-type-units");
    }
  }

  if (!Args.hasFlag(options::OPT_fdwarf_directory_asm,
                    options::OPT_fno_dwarf_directory_asm, true))
    CmdArgs.push_back("-fno-dwarf-directory-asm");

  renderDwarfFormat(D, T, Args, CmdArgs, EffectiveDWARFVersion);
  renderDebugInfoCompressionArgs(Args, CmdArgs, D, TC);

  // This controls whether or not we perform JustMyCode instrumentation.
  if (Args.hasFlag(options::OPT_fjmc, options::OPT_fno_jmc, false)) {
    if (TC.getTriple().isOSBinFormatELF() ||
        TC.getTriple().isWindowsMSVCEnvironment()) {
      if (DebugInfoKind >= llvm::codegenoptions::DebugInfoConstructor)
        CmdArgs.push_back("-fjmc");
      else
        D.Diag(neverc::diag::warn_drv_jmc_requires_debuginfo) << "-fjmc"
                                                              << "-g";
    } else {
      D.Diag(neverc::diag::warn_drv_fjmc_for_elf_only);
    }
  }

  const char *DebugCompilationDir =
      addDebugCompDirArg(Args, CmdArgs, D.getVFS());

  addDebugPrefixMapArg(D, TC, Args, CmdArgs);
}

void processVSRuntimeLibrary(const Driver &D, const ArgList &Args,
                             ArgStringList &CmdArgs) {
  unsigned RTOptionID = options::OPT_msvc_runtime_MT;

  if (Args.hasArg(options::OPT_create_dll_debug))
    // The /LDd option implies /MTd. The dependent lib part can be overridden,
    // but defining _DEBUG is sticky.
    RTOptionID = options::OPT_msvc_runtime_MTd;

  if (Arg *A = Args.getLastArg(options::OPT_msvc_runtime_Group))
    RTOptionID = A->getOption().getID();

  if (Arg *A = Args.getLastArg(options::OPT_fms_runtime_lib_EQ)) {
    RTOptionID = llvm::StringSwitch<unsigned>(A->getValue())
                     .Case("static", options::OPT_msvc_runtime_MT)
                     .Case("static_dbg", options::OPT_msvc_runtime_MTd)
                     .Case("dll", options::OPT_msvc_runtime_MD)
                     .Case("dll_dbg", options::OPT_msvc_runtime_MDd)
                     .Default(options::OPT_msvc_runtime_MT);
  }

  llvm::StringRef FlagForCRT;
  switch (RTOptionID) {
  case options::OPT_msvc_runtime_MD:
    if (Args.hasArg(options::OPT_create_dll_debug))
      CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-D_DLL");
    FlagForCRT = "--dependent-lib=msvcrt";
    break;
  case options::OPT_msvc_runtime_MDd:
    CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-D_DLL");
    FlagForCRT = "--dependent-lib=msvcrtd";
    break;
  case options::OPT_msvc_runtime_MT:
    if (Args.hasArg(options::OPT_create_dll_debug))
      CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    FlagForCRT = "--dependent-lib=libcmt";
    break;
  case options::OPT_msvc_runtime_MTd:
    CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    FlagForCRT = "--dependent-lib=libcmtd";
    break;
  default:
    llvm_unreachable("Unexpected option ID.");
  }

  if (Args.hasArg(options::OPT_fms_omit_default_lib)) {
    CmdArgs.push_back("-D_VC_NODEFAULTLIB");
  } else {
    CmdArgs.push_back(FlagForCRT.data());

    bool IsDLL = RTOptionID == options::OPT_msvc_runtime_MD ||
                 RTOptionID == options::OPT_msvc_runtime_MDd;
    bool IsDebug = RTOptionID == options::OPT_msvc_runtime_MTd ||
                   RTOptionID == options::OPT_msvc_runtime_MDd;

    CmdArgs.push_back(
        IsDLL ? (IsDebug ? "--dependent-lib=vcruntimed" : "--dependent-lib=vcruntime")
              : (IsDebug ? "--dependent-lib=libvcruntimed" : "--dependent-lib=libvcruntime"));
    CmdArgs.push_back(
        IsDLL ? (IsDebug ? "--dependent-lib=ucrtd" : "--dependent-lib=ucrt")
              : (IsDebug ? "--dependent-lib=libucrtd" : "--dependent-lib=libucrt"));

    CmdArgs.push_back("--dependent-lib=legacy_stdio_definitions");
    CmdArgs.push_back("-D_NO_CRT_STDIO_INLINE");

    // This provides POSIX compatibility (maps 'open' to '_open'), which most
    // users want.  The /Za flag to cl.exe turns this off, but it's not
    // implemented in neverc.
    CmdArgs.push_back("--dependent-lib=oldnames");
  }
}

bool hasNcExtension(const InputInfoList &Inputs) {
  for (const InputInfo &I : Inputs) {
    if (!I.getBaseInput())
      continue;
    if (llvm::sys::path::extension(I.getBaseInput()) == ".nc")
      return true;
  }
  return false;
}

void addNeverCFeatureFlags(const ArgList &Args, ArgStringList &CmdArgs,
                           const InputInfoList &Inputs) {
  bool IsNcInput = hasNcExtension(Inputs);
  if (IsNcInput) {
    CmdArgs.push_back("-fneverc-types");
    CmdArgs.push_back("-fbuiltin-string");
  } else {
    Args.addOptInFlag(CmdArgs, options::OPT_fneverc_types,
                      options::OPT_fno_neverc_types);
    Args.addOptInFlag(CmdArgs, options::OPT_fbuiltin_string,
                      options::OPT_fno_builtin_string);
  }

  // -fbuiltin-mimalloc is suppressed when:
  //   - -fno-builtin is active (no CRT override makes sense)
  //   - -mkernel is active (implies -fno-builtin; no userspace heap)
  //   - -fshellcode-mode is active (no heap in shellcode)
  //   - -ffreestanding is active (no libc to override)
  bool SuppressMimalloc =
      Args.hasArg(options::OPT_fno_builtin) ||
      Args.hasArg(options::OPT_mkernel) ||
      Args.hasArg(options::OPT_fshellcode_mode) ||
      Args.hasArg(options::OPT_ffreestanding);
  if (!SuppressMimalloc)
    Args.addOptOutFlag(CmdArgs, options::OPT_fbuiltin_mimalloc,
                       options::OPT_fno_builtin_mimalloc);
}

/// Optionally embed the invocation command line into DWARF or a section.
void renderDebugRecordingFlags(const ToolChain &TC, const Driver &D,
                               const llvm::Triple &Triple,
                               const std::string &TripleStr, const char *Exec,
                               const ArgList &Args, ArgStringList &CmdArgs) {
  auto GRecordSwitches =
      Args.hasFlag(options::OPT_grecord_command_line,
                   options::OPT_gno_record_command_line, false);
  auto FRecordSwitches =
      Args.hasFlag(options::OPT_frecord_command_line,
                   options::OPT_fno_record_command_line, false);
  if (FRecordSwitches && !Triple.isOSBinFormatELF() &&
      !Triple.isOSBinFormatMachO())
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << Args.getLastArg(options::OPT_frecord_command_line)->getAsString(Args)
        << TripleStr;

  if (!TC.UseDwarfDebugFlags() && !GRecordSwitches && !FRecordSwitches)
    return;

  ArgStringList OriginalArgs;
  for (const auto &Arg : Args)
    Arg->render(Args, OriginalArgs);

  llvm::SmallString<256> Flags;
  escapeSpacesAndBackslashes(Exec, Flags);
  for (const char *OriginalArg : OriginalArgs) {
    llvm::SmallString<128> EscapedArg;
    escapeSpacesAndBackslashes(OriginalArg, EscapedArg);
    Flags += " ";
    Flags += EscapedArg;
  }
  auto FlagsArgString = Args.MakeArgString(Flags);
  if (TC.UseDwarfDebugFlags() || GRecordSwitches) {
    CmdArgs.push_back("-dwarf-debug-flags");
    CmdArgs.push_back(FlagsArgString);
  }
  if (FRecordSwitches) {
    CmdArgs.push_back("-record-command-line");
    CmdArgs.push_back(FlagsArgString);
  }
}

void addNeverCSpecificFlags(const ArgList &Args, ArgStringList &CmdArgs) {
  if (Args.hasArg(options::OPT_fjumptable_rdata)) {
    CmdArgs.push_back("-fjumptable-rdata");
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-jumptable-in-function-section=false");
  }
  if (Args.hasArg(options::OPT_fdisable_inline_opt))
    CmdArgs.push_back("-fdisable-inline-opt");
  if (Args.hasArg(options::OPT_fdisable_try_stmt))
    CmdArgs.push_back("-fdisable-try-stmt");
  if (Args.hasArg(options::OPT_fdisable_cfi_check))
    CmdArgs.push_back("-fdisable-cfi-check");
  if (Args.hasArg(options::OPT_fandroid_kernel_driver_mode)) {
    CmdArgs.push_back("-fandroid-kernel-driver-mode");
    CmdArgs.push_back("-fdisable-cfi-check");
  }
  if (Args.hasArg(options::OPT_ftreat_warnings_as_errors))
    CmdArgs.push_back("-ftreat-warnings-as-errors");
  if (Args.hasArg(options::OPT_fprint_arguments))
    CmdArgs.push_back("-fprint-arguments");
  if (Args.hasArg(options::OPT_fauto_generate_bitcode))
    CmdArgs.push_back("-fauto-generate-bitcode");
  if (Args.hasArg(options::OPT_fauto_generate_ir))
    CmdArgs.push_back("-fauto-generate-ir");

  if (Args.hasArg(options::OPT_funused_function))
    CmdArgs.push_back("-Wunused-function");
  else
    CmdArgs.push_back("-Wno-unused-function");
  if (Args.hasArg(options::OPT_funused_variable))
    CmdArgs.push_back("-Wunused-variable");
  else
    CmdArgs.push_back("-Wno-unused-variable");
  if (Args.hasArg(options::OPT_funused_value))
    CmdArgs.push_back("-Wunused-value");
  else
    CmdArgs.push_back("-Wno-unused-value");
}

} // namespace

// ===----------------------------------------------------------------------===
// NeverC::ConstructJob — main cc1 argument construction
// ===----------------------------------------------------------------------===

void NeverC::ConstructJob(Compilation &C, const JobAction &JA,
                          const InputInfo &Output, const InputInfoList &Inputs,
                          const ArgList &Args,
                          const char *LinkingOutput) const {
  const auto &TC = getToolChain();
  const llvm::Triple &RawTriple = TC.getTriple();
  const llvm::Triple &Triple = TC.getEffectiveTriple();
  const std::string &TripleStr = Triple.getTriple();

  bool Kernel = Args.hasArg(options::OPT_mkernel);
  const Driver &D = TC.getDriver();
  ArgStringList CmdArgs;

  assert(Inputs.size() >= 1 && "Must have at least one input.");

  bool IsUsingLTO = D.isUsingLTO();
  auto LTOMode = D.getLTOMode();

  const InputInfo &Input = Inputs[0];

  for (const InputInfo &I : Inputs) {
    if (&I == &Input || I.getType() == types::TY_Nothing) {
    } else {
      llvm_unreachable("unexpectedly given multiple inputs");
    }
  }

  bool IsWindowsMSVC = RawTriple.isWindowsMSVCEnvironment();
  bool IsWindowsCOFF = RawTriple.isOSWindows() || RawTriple.isOSBinFormatCOFF();

  CmdArgs.push_back("-triple");
  CmdArgs.push_back(Args.MakeArgString(TripleStr));

  Args.ClaimAllArgs(options::OPT_MJ);
  Args.ClaimAllArgs(options::OPT_gen_cdb_fragment_path);

  // Push all default warning arguments that are specific to
  // the given target.  These come before user provided warning options
  // are provided.
  TC.addNeverCWarningOptions(CmdArgs);

  // Claim assembler-only args when not actually assembling.
  if (!isa<AssembleJobAction>(JA)) {
    Args.ClaimAllArgs(options::OPT_mrelax_all);
    Args.ClaimAllArgs(options::OPT_mno_relax_all);
    Args.ClaimAllArgs(options::OPT_mincremental_linker_compatible);
    Args.ClaimAllArgs(options::OPT_mno_incremental_linker_compatible);
    Args.ClaimAllArgs(options::OPT_Wa_COMMA);
    Args.ClaimAllArgs(options::OPT_Xassembler);
    Args.ClaimAllArgs(options::OPT_femit_dwarf_unwind_EQ);
  }

  if (isa<PreprocessJobAction>(JA)) {
    if (Output.getType() == types::TY_Dependencies)
      CmdArgs.push_back("-Eonly");
    else {
      CmdArgs.push_back("-E");
    }
  } else if (isa<AssembleJobAction>(JA)) {
    CmdArgs.push_back("-emit-obj");

    collectArgsForIntegratedAssembler(C, Args, CmdArgs, D);

    // Also ignore explicit -force_cpusubtype_ALL option.
    Args.ClaimAllArgs(options::OPT_force__cpusubtype__ALL);
  } else {
    assert((isa<CompileJobAction>(JA) || isa<BackendJobAction>(JA)) &&
           "Invalid action for neverc tool.");
    if (JA.getType() == types::TY_Nothing) {
      CmdArgs.push_back("-fsyntax-only");
    } else if (JA.getType() == types::TY_LLVM_IR ||
               JA.getType() == types::TY_LTO_IR) {
      CmdArgs.push_back("-emit-llvm");
    } else if (JA.getType() == types::TY_LLVM_BC ||
               JA.getType() == types::TY_LTO_BC) {
      CmdArgs.push_back("-emit-llvm-bc");
    } else if (JA.getType() == types::TY_PP_Asm) {
      CmdArgs.push_back("-S");
    } else {
      assert(JA.getType() == types::TY_PP_Asm && "Unexpected output type!");
    }

    if (JA.getType() == types::TY_LLVM_BC)
      CmdArgs.push_back("-emit-llvm-uselists");

    if (IsUsingLTO) {
      assert(LTOMode == LTOK_Full);
      CmdArgs.push_back("-flto=full");
    }
  }

  Args.AddLastArg(CmdArgs, options::OPT_dumpdir);

  if (Args.getLastArg(options::OPT_save_temps_EQ))
    Args.AddLastArg(CmdArgs, options::OPT_save_temps_EQ);

  if (!C.isForDiagnostics())
    CmdArgs.push_back("-disable-free");
  CmdArgs.push_back("-clear-ast-before-backend");

#ifdef NDEBUG
  const bool IsAssertBuild = false;
#else
  const bool IsAssertBuild = true;
#endif

  if (Args.hasFlag(options::OPT_fno_verify_intermediate_code,
                   options::OPT_fverify_intermediate_code, !IsAssertBuild)) {
    CmdArgs.push_back("-disable-llvm-verifier");
  }

  if (Args.hasFlag(options::OPT_fdiscard_value_names,
                   options::OPT_fno_discard_value_names, !IsAssertBuild)) {
    if (Args.hasArg(options::OPT_fdiscard_value_names) &&
        llvm::any_of(Inputs, [](const neverc::driver::InputInfo &II) {
          return types::isLLVMIR(II.getType());
        })) {
      D.Diag(diag::warn_ignoring_fdiscard_for_bitcode);
    }
    CmdArgs.push_back("-discard-value-names");
  }

  CmdArgs.push_back("-main-file-name");
  CmdArgs.push_back(getBaseInputName(Args, Input));

  if (Args.hasArg(options::OPT_static))
    CmdArgs.push_back("-static-define");

  if (Args.hasArg(options::OPT_municode))
    CmdArgs.push_back("-DUNICODE");

  checkCodeGenerationOptions(D, Args);

  unsigned FunctionAlignment = ParseFunctionAlignment(TC, Args);
  assert(FunctionAlignment <= 31 && "function alignment will be truncated!");
  if (FunctionAlignment) {
    CmdArgs.push_back("-function-alignment");
    CmdArgs.push_back(Args.MakeArgString(std::to_string(FunctionAlignment)));
  }

  // We support -falign-loops=N where N is a power of 2. GCC supports more
  // forms.
  if (const Arg *A = Args.getLastArg(options::OPT_falign_loops_EQ)) {
    unsigned Value = 0;
    if (llvm::StringRef(A->getValue()).getAsInteger(10, Value) || Value > 65536)
      TC.getDriver().Diag(diag::err_drv_invalid_int_value)
          << A->getAsString(Args) << A->getValue();
    else if (Value & (Value - 1))
      TC.getDriver().Diag(diag::err_drv_alignment_not_power_of_two)
          << A->getAsString(Args) << A->getValue();
    // Treat =0 as unspecified (use the target preference).
    if (Value)
      CmdArgs.push_back(Args.MakeArgString(
          "-falign-loops=" + llvm::Twine(std::min(Value, 65536u))));
  }

  if (Triple.isOSBinFormatELF() && !Triple.isOSLinux()) {
    Arg *A = Args.getLastArg(options::OPT_fsemantic_interposition,
                             options::OPT_fno_semantic_interposition);
    bool SupportsLocalAlias = Triple.isAArch64() || Triple.isX86();
    if (!A)
      CmdArgs.push_back("-fhalf-no-semantic-interposition");
    else if (A->getOption().matches(options::OPT_fsemantic_interposition))
      A->render(Args, CmdArgs);
    else if (!SupportsLocalAlias)
      CmdArgs.push_back("-fhalf-no-semantic-interposition");
  }

  {
    std::string Model;
    if (Arg *A = Args.getLastArg(options::OPT_mthread_model)) {
      if (!TC.isThreadModelSupported(A->getValue()))
        D.Diag(diag::err_drv_invalid_thread_model_for_target)
            << A->getValue() << A->getAsString(Args);
      Model = A->getValue();
    } else
      Model = TC.getThreadModel();
    if (Model != "posix") {
      CmdArgs.push_back("-mthread-model");
      CmdArgs.push_back(Args.MakeArgString(Model));
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_fveclib)) {
    llvm::StringRef Name = A->getValue();
    if (Name == "SVML") {
      if (Triple.getArch() != llvm::Triple::x86_64)
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << Name << Triple.getArchName();
    } else if (Name == "LIBMVEC-X86") {
      if (Triple.getArch() != llvm::Triple::x86_64)
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << Name << Triple.getArchName();
    } else if (Name == "SLEEF" || Name == "ArmPL") {
      if (Triple.getArch() != llvm::Triple::aarch64)
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << Name << Triple.getArchName();
    }
    A->render(Args, CmdArgs);
  }

  if (Args.hasFlag(options::OPT_fmerge_all_constants,
                   options::OPT_fno_merge_all_constants, false))
    CmdArgs.push_back("-fmerge-all-constants");

  Args.addOptOutFlag(CmdArgs, options::OPT_fdelete_null_pointer_checks,
                     options::OPT_fno_delete_null_pointer_checks);

  // LLVM Code Generator Options.

  if (Arg *A = Args.getLastArg(options::OPT_Wframe_larger_than_EQ)) {
    llvm::StringRef V = A->getValue(), V1 = V;
    unsigned Size;
    if (V1.consumeInteger(10, Size) || !V1.empty())
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << V << A->getOption().getName();
    else
      CmdArgs.push_back(Args.MakeArgString("-fwarn-stack-size=" + V));
  }

  Args.addOptOutFlag(CmdArgs, options::OPT_fjump_tables,
                     options::OPT_fno_jump_tables);
  Args.addOptOutFlag(CmdArgs, options::OPT_fpreserve_as_comments,
                     options::OPT_fno_preserve_as_comments);

  if (Args.hasFlag(options::OPT_mrtd, options::OPT_mno_rtd, false))
    CmdArgs.push_back("-fdefault-calling-conv=stdcall");

  CodeGenOptions::FramePointerKind FPKeepKind =
      getFramePointerKind(Args, RawTriple);
  const char *FPKeepKindStr = nullptr;
  switch (FPKeepKind) {
  case CodeGenOptions::FramePointerKind::None:
    FPKeepKindStr = "-mframe-pointer=none";
    break;
  case CodeGenOptions::FramePointerKind::NonLeaf:
    FPKeepKindStr = "-mframe-pointer=non-leaf";
    break;
  case CodeGenOptions::FramePointerKind::All:
    FPKeepKindStr = "-mframe-pointer=all";
    break;
  }
  assert(FPKeepKindStr && "unknown FramePointerKind");
  CmdArgs.push_back(FPKeepKindStr);

  Args.addOptOutFlag(CmdArgs, options::OPT_fzero_initialized_in_bss,
                     options::OPT_fno_zero_initialized_in_bss);

  bool OFastEnabled = isOptimizationLevelFast(Args);
  OptSpecifier StrictAliasingAliasOption =
      OFastEnabled ? options::OPT_Ofast : options::OPT_fstrict_aliasing;
  bool TBAAOnByDefault = !IsWindowsMSVC;
  if (!Args.hasFlag(options::OPT_fstrict_aliasing, StrictAliasingAliasOption,
                    options::OPT_fno_strict_aliasing, TBAAOnByDefault))
    CmdArgs.push_back("-relaxed-aliasing");
  if (!Args.hasFlag(options::OPT_fstruct_path_tbaa,
                    options::OPT_fno_struct_path_tbaa, true))
    CmdArgs.push_back("-no-struct-path-tbaa");
  Args.addOptOutFlag(CmdArgs, options::OPT_foptimize_sibling_calls,
                     options::OPT_fno_optimize_sibling_calls);
  Args.AddLastArg(CmdArgs, options::OPT_ffine_grained_bitfield_accesses,
                  options::OPT_fno_fine_grained_bitfield_accesses);

  Args.addOptInFlag(CmdArgs, options::OPT_fsplit_stack,
                    options::OPT_fno_split_stack);

  // -fprotect-parens=0 is default.
  if (Args.hasFlag(options::OPT_fprotect_parens,
                   options::OPT_fno_protect_parens, false))
    CmdArgs.push_back("-fprotect-parens");

  renderFloatingPointOptions(TC, D, OFastEnabled, Args, CmdArgs, JA);

  if (Arg *A = Args.getLastArg(options::OPT_fextend_args_EQ)) {
    const llvm::Triple::ArchType Arch = TC.getArch();
    if (Arch == llvm::Triple::x86_64) {
      llvm::StringRef V = A->getValue();
      if (V == "64")
        CmdArgs.push_back("-fextend-arguments=64");
      else if (V != "32")
        D.Diag(diag::err_drv_invalid_argument_to_option)
            << A->getValue() << A->getOption().getName();
    } else
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getOption().getName() << TripleStr;
  }

  if (Arg *A = Args.getLastArg(options::OPT_mdouble_EQ)) {
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getAsString(Args) << TripleStr;
  }

  if (Arg *A = Args.getLastArg(options::OPT_LongDouble_Group)) {
    if (TC.getTriple().isX86() || TC.getTriple().isAArch64())
      A->render(Args, CmdArgs);
    else
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
  }

  // Decide whether to use verbose asm. Verbose assembly is the default on
  // toolchains which have the integrated assembler on by default.
  bool IsIntegratedAssemblerDefault = TC.IsIntegratedAssemblerDefault();
  if (!Args.hasFlag(options::OPT_fverbose_asm, options::OPT_fno_verbose_asm,
                    IsIntegratedAssemblerDefault))
    CmdArgs.push_back("-fno-verbose-asm");

  // Parse 'none' or '$major.$minor'. Disallow -fbinutils-version=0 because we
  // use that to indicate the MC default in the backend.
  if (Arg *A = Args.getLastArg(options::OPT_fbinutils_version_EQ)) {
    llvm::StringRef V = A->getValue();
    unsigned Num;
    if (V == "none")
      A->render(Args, CmdArgs);
    else if (!V.consumeInteger(10, Num) && Num > 0 &&
             (V.empty() || (V.consume_front(".") &&
                            !V.consumeInteger(10, Num) && V.empty())))
      A->render(Args, CmdArgs);
    else
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << A->getValue() << A->getOption().getName();
  }

  if (Args.hasFlag(options::OPT_mms_bitfields, options::OPT_mno_ms_bitfields,
                   false)) {
    CmdArgs.push_back("-mms-bitfields");
  }

  if (Args.hasFlag(options::OPT_fms_volatile, options::OPT_fno_ms_volatile,
                   Triple.isX86() && IsWindowsMSVC))
    CmdArgs.push_back("-fms-volatile");

  if (Arg *A = Args.getLastArg(options::OPT_fdirect_access_external_data,
                               options::OPT_fno_direct_access_external_data)) {
    if (A->getOption().matches(options::OPT_fdirect_access_external_data))
      A->render(Args, CmdArgs);
  }

  if (Args.hasFlag(options::OPT_fno_plt, options::OPT_fplt, false)) {
    CmdArgs.push_back("-fno-plt");
  }

  bool Freestanding =
      Args.hasFlag(options::OPT_ffreestanding, options::OPT_fhosted, false) ||
      Kernel;
  if (Freestanding)
    CmdArgs.push_back("-ffreestanding");

  Args.AddLastArg(CmdArgs, options::OPT_fno_knr_functions);
  addNeverCFeatureFlags(Args, CmdArgs, Inputs);

  bool IsAsyncUnwindTablesDefault = TC.getDefaultUnwindTableLevel(Args) ==
                                    ToolChain::UnwindTableLevel::Asynchronous;
  bool IsSyncUnwindTablesDefault = TC.getDefaultUnwindTableLevel(Args) ==
                                   ToolChain::UnwindTableLevel::Synchronous;

  bool AsyncDefault = IsAsyncUnwindTablesDefault;
  bool AsyncUnwindTables =
      Args.hasFlag(options::OPT_fasynchronous_unwind_tables,
                   options::OPT_fno_asynchronous_unwind_tables,
                   AsyncDefault && !Freestanding);
  bool UnwindTables =
      Args.hasFlag(options::OPT_funwind_tables, options::OPT_fno_unwind_tables,
                   IsSyncUnwindTablesDefault && !Freestanding);
  if (AsyncUnwindTables)
    CmdArgs.push_back("-funwind-tables=2");
  else if (UnwindTables)
    CmdArgs.push_back("-funwind-tables=1");

  TC.addNeverCTargetOptions(Args, CmdArgs);

  if (Arg *A = Args.getLastArg(options::OPT_mcmodel_EQ)) {
    llvm::StringRef CM = A->getValue();
    bool Ok = false;
    if (Triple.isAArch64()) {
      Ok = CM == "tiny" || CM == "small";
    } else if (Triple.getArch() == llvm::Triple::x86_64) {
      Ok = llvm::is_contained({"small", "kernel", "medium", "large", "tiny"},
                              CM);
    }
    if (Ok) {
      CmdArgs.push_back(Args.MakeArgString("-mcmodel=" + CM));
    } else {
      D.Diag(diag::err_drv_unsupported_option_argument_for_target)
          << A->getSpelling() << CM << TripleStr;
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_mlarge_data_threshold_EQ)) {
    if (!Triple.isX86()) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getOption().getName() << TripleStr;
    } else {
      bool IsMediumCM = false;
      if (Arg *A = Args.getLastArg(options::OPT_mcmodel_EQ))
        IsMediumCM = llvm::StringRef(A->getValue()) == "medium";
      if (!IsMediumCM) {
        D.Diag(diag::warn_drv_large_data_threshold_invalid_code_model)
            << A->getOption().getRenderName();
      } else {
        A->render(Args, CmdArgs);
      }
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_mtls_size_EQ)) {
    llvm::StringRef Value = A->getValue();
    unsigned TLSSize = 0;
    Value.getAsInteger(10, TLSSize);
    if (!Triple.isAArch64() || !Triple.isOSBinFormatELF())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getOption().getName() << TripleStr;
    if (TLSSize != 12 && TLSSize != 24 && TLSSize != 32 && TLSSize != 48)
      D.Diag(diag::err_drv_invalid_int_value)
          << A->getOption().getName() << Value;
    Args.AddLastArg(CmdArgs, options::OPT_mtls_size_EQ);
  }

  // Add the target cpu
  std::string CPU = getCPUName(D, Args, Triple, /*FromAs*/ false);
  if (!CPU.empty()) {
    CmdArgs.push_back("-target-cpu");
    CmdArgs.push_back(Args.MakeArgString(CPU));
  }

  RenderTargetOptions(Triple, Args, Kernel, CmdArgs);

  types::ID InputType = Input.getType();
  if (IsWindowsMSVC)
    AddMSVCCompatArgs(C, Args, InputType, CmdArgs);

  llvm::codegenoptions::DebugInfoKind DebugInfoKind =
      llvm::codegenoptions::NoDebugInfo;
  DwarfFissionKind DwarfFission = DwarfFissionKind::None;
  renderDebugOptions(TC, D, RawTriple, Args, types::isLLVMIR(InputType),
                     CmdArgs, Output, DebugInfoKind, DwarfFission);

  // Add the split debug info name to the command lines here so we
  // can propagate it to the backend.
  bool SplitDWARF = (DwarfFission != DwarfFissionKind::None) &&
                    (TC.getTriple().isOSBinFormatELF() ||
                     TC.getTriple().isOSBinFormatCOFF()) &&
                    (isa<AssembleJobAction>(JA) || isa<CompileJobAction>(JA) ||
                     isa<BackendJobAction>(JA));
  if (SplitDWARF) {
    const char *SplitDWARFOut = SplitDebugName(JA, Args, Input, Output);
    CmdArgs.push_back("-split-dwarf-file");
    CmdArgs.push_back(SplitDWARFOut);
    if (DwarfFission == DwarfFissionKind::Split) {
      CmdArgs.push_back("-split-dwarf-output");
      CmdArgs.push_back(SplitDWARFOut);
    }
  }

  // Pass the linker version in use.
  if (Arg *A = Args.getLastArg(options::OPT_mlinker_version_EQ)) {
    CmdArgs.push_back("-target-linker-version");
    CmdArgs.push_back(A->getValue());
  }

  Args.AddAllArgs(CmdArgs, options::OPT_v);

  if (Args.getLastArg(options::OPT_H)) {
    CmdArgs.push_back("-H");
    CmdArgs.push_back("-sys-header-deps");
  }
  Args.AddAllArgs(CmdArgs, options::OPT_fshow_skipped_includes);

  Args.AddLastArg(CmdArgs, options::OPT_P);

  // Give the gen diagnostics more chances to succeed, by avoiding intentional
  // crashes.
  if (D.CCGenDiagnostics)
    CmdArgs.push_back("-disable-pragma-debug-crash");

  // Allow backend to put its diagnostic files in the same place as frontend
  // crash diagnostics files.
  if (Args.hasArg(options::OPT_fcrash_diagnostics_dir)) {
    llvm::StringRef Dir =
        Args.getLastArgValue(options::OPT_fcrash_diagnostics_dir);
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(Args.MakeArgString("-crash-diagnostics-dir=" + Dir));
  }

  bool UseSeparateSections = false;

  if (Args.hasFlag(options::OPT_ffunction_sections,
                   options::OPT_fno_function_sections, UseSeparateSections)) {
    CmdArgs.push_back("-ffunction-sections");
  }

  if (Arg *A = Args.getLastArg(options::OPT_fbasic_block_sections_EQ)) {
    llvm::StringRef Val = A->getValue();
    if (Triple.isX86() && Triple.isOSBinFormatELF()) {
      if (Val != "all" && Val != "labels" && Val != "none" &&
          !Val.starts_with("list="))
        D.Diag(diag::err_drv_invalid_value)
            << A->getAsString(Args) << A->getValue();
      else
        A->render(Args, CmdArgs);
    } else if (Val != "none") {
      // =none is allowed everywhere. It's useful for overriding the option
      // and is the same as not specifying the option.
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    }
  }

  if (Args.hasFlag(options::OPT_fdata_sections, options::OPT_fno_data_sections,
                   UseSeparateSections)) {
    CmdArgs.push_back("-fdata-sections");
  }

  Args.addOptOutFlag(CmdArgs, options::OPT_funique_section_names,
                     options::OPT_fno_unique_section_names);
  Args.addOptInFlag(CmdArgs, options::OPT_funique_internal_linkage_names,
                    options::OPT_fno_unique_internal_linkage_names);
  Args.addOptInFlag(CmdArgs, options::OPT_funique_basic_block_section_names,
                    options::OPT_fno_unique_basic_block_section_names);
  if (Arg *A = Args.getLastArg(options::OPT_fsplit_machine_functions,
                               options::OPT_fno_split_machine_functions)) {
    if (!A->getOption().matches(options::OPT_fno_split_machine_functions)) {
      // This codegen pass is only available on x86 and AArch64 ELF targets.
      if ((Triple.isX86() || Triple.isAArch64()) && Triple.isOSBinFormatELF())
        A->render(Args, CmdArgs);
      else
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << A->getAsString(Args) << TripleStr;
    }
  }

  // Pass options for controlling the default header search paths.
  if (Args.hasArg(options::OPT_nostdinc)) {
    CmdArgs.push_back("-nostdsysteminc");
    CmdArgs.push_back("-nobuiltininc");
  } else {
    if (Args.hasArg(options::OPT_nostdlibinc))
      CmdArgs.push_back("-nostdsysteminc");
    Args.AddLastArg(CmdArgs, options::OPT_nobuiltininc);
  }

  // Pass the path to compiler resource files.
  CmdArgs.push_back("-resource-dir");
  CmdArgs.push_back(D.ResourceDir.c_str());

  Args.AddLastArg(CmdArgs, options::OPT_working_directory);

  // Add preprocessing options like -I, -D, etc. if we are using the
  // preprocessor.
  //
  if (types::getPreprocessedType(InputType) != types::TY_INVALID)
    AddPreprocessingOptions(C, JA, D, Args, CmdArgs, Output, Inputs);

  Args.ClaimAllArgs(options::OPT_D);

  // Manually translate -O4 to -O3; let neverc reject others.
  //
  // In auto-LTO mode, the frontend runs at -O0: LTO does the real
  // optimization, and -O0 keeps bitcode small so LTO runs ~3x faster.
  if (IsUsingLTO && D.isAutoLTO()) {
    CmdArgs.push_back("-O0");
  } else if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O4)) {
      CmdArgs.push_back("-O3");
      D.Diag(diag::warn_O4_is_O3);
    } else {
      A->render(Args, CmdArgs);
    }
  }

  claimNoWarnArgs(Args);

  Args.AddAllArgs(CmdArgs, options::OPT_R_Group);

  for (const Arg *A :
       Args.filtered(options::OPT_W_Group, options::OPT_msvc_wd)) {
    A->claim();
    if (A->getOption().getID() == options::OPT_msvc_wd) {
      unsigned WarningNumber;
      if (llvm::StringRef(A->getValue()).getAsInteger(10, WarningNumber)) {
        D.Diag(diag::err_drv_invalid_int_value)
            << A->getAsString(Args) << A->getValue();
        continue;
      }

      if (auto Group = diagGroupFromCLWarningID(WarningNumber)) {
        CmdArgs.push_back(Args.MakeArgString(
            "-Wno-" + DiagnosticIDs::getWarningOptionForGroup(*Group)));
      }
      continue;
    }
    A->render(Args, CmdArgs);
  }

  if (Args.hasFlag(options::OPT_pedantic, options::OPT_no_pedantic, false))
    CmdArgs.push_back("-pedantic");
  Args.AddLastArg(CmdArgs, options::OPT_pedantic_errors);
  Args.AddLastArg(CmdArgs, options::OPT_w);

  Args.addOptInFlag(CmdArgs, options::OPT_ffixed_point,
                    options::OPT_fno_fixed_point);
  // -ansi is equivalent to -std=c89.
  //
  // If a std is supplied, only add -trigraphs if it follows the
  // option.
  bool ImplyMSVCStd = false;
  const Arg *Std = Args.getLastArg(options::OPT_std_EQ, options::OPT_ansi);
  if (Std) {
    if (Std->getOption().matches(options::OPT_ansi))
      CmdArgs.push_back("-std=c89");
    else
      Std->render(Args, CmdArgs);

    // If -f(no-)trigraphs appears after the language standard flag, honor it.
    if (Arg *A = Args.getLastArg(options::OPT_std_EQ, options::OPT_ansi,
                                 options::OPT_ftrigraphs,
                                 options::OPT_fno_trigraphs))
      if (A != Std)
        A->render(Args, CmdArgs);
  } else {
    // Honor -std-default.
    //
    if (!Args.hasArg(options::OPT_msvc_std)) {
      Args.AddAllArgsTranslated(CmdArgs, options::OPT_std_default_EQ, "-std=",
                                /*Joined=*/true);
    } else
      ImplyMSVCStd = true;

    Args.AddLastArg(CmdArgs, options::OPT_ftrigraphs,
                    options::OPT_fno_trigraphs);
  }

  Arg *WriteStrings =
      Args.getLastArg(options::OPT_Wwrite_strings,
                      options::OPT_Wno_write_strings, options::OPT_w);
  if (WriteStrings &&
      WriteStrings->getOption().matches(options::OPT_Wwrite_strings))
    CmdArgs.push_back("-fconst-strings");

  // Translate GCC's misnamer '-fasm' arguments to '-fgnu-keywords'.
  if (Arg *Asm = Args.getLastArg(options::OPT_fasm, options::OPT_fno_asm)) {
    if (Asm->getOption().matches(options::OPT_fasm))
      CmdArgs.push_back("-fgnu-keywords");
    else
      CmdArgs.push_back("-fno-gnu-keywords");
  }

  if (!shouldEnableAutolink(Args, TC, JA))
    CmdArgs.push_back("-fno-autolink");

  Args.AddLastArg(CmdArgs, options::OPT_fconstexpr_depth_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fconstexpr_steps_EQ);

  if (Arg *A = Args.getLastArg(options::OPT_fbracket_depth_EQ)) {
    CmdArgs.push_back("-fbracket-depth");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_Wlarge_by_value_copy_EQ,
                               options::OPT_Wlarge_by_value_copy_def)) {
    if (A->getNumValues()) {
      llvm::StringRef bytes = A->getValue();
      CmdArgs.push_back(Args.MakeArgString("-Wlarge-by-value-copy=" + bytes));
    } else
      CmdArgs.push_back("-Wlarge-by-value-copy=64"); // default value
  }

  if (Arg *A = Args.getLastArg(options::OPT_ftabstop_EQ)) {
    CmdArgs.push_back("-ftabstop");
    CmdArgs.push_back(A->getValue());
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fstack_size_section,
                    options::OPT_fno_stack_size_section);

  if (Args.hasArg(options::OPT_fstack_usage)) {
    CmdArgs.push_back("-stack-usage-file");

    if (Arg *OutputOpt = Args.getLastArg(options::OPT_o)) {
      llvm::SmallString<128> OutputFilename(OutputOpt->getValue());
      llvm::sys::path::replace_extension(OutputFilename, "su");
      CmdArgs.push_back(Args.MakeArgString(OutputFilename));
    } else
      CmdArgs.push_back(Args.MakeArgString(
          llvm::Twine(getBaseInputStem(Args, Inputs)) + ".su"));
  }

  CmdArgs.push_back("-ferror-limit");
  if (Arg *A = Args.getLastArg(options::OPT_ferror_limit_EQ))
    CmdArgs.push_back(A->getValue());
  else
    CmdArgs.push_back("4294967295");

  Args.AddLastArg(CmdArgs, options::OPT_fconstexpr_backtrace_limit_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fmacro_backtrace_limit_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fspell_checking_limit_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fcaret_diagnostics_max_lines_EQ);

  // Pass -fmessage-length=.
  unsigned MessageLength = 0;
  if (Arg *A = Args.getLastArg(options::OPT_fmessage_length_EQ)) {
    llvm::StringRef V(A->getValue());
    if (V.getAsInteger(0, MessageLength))
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << V << A->getOption().getName();
  } else {
    // If -fmessage-length=N was not specified, determine whether this is a
    // terminal and, if so, implicitly define -fmessage-length appropriately.
    MessageLength = llvm::sys::Process::StandardErrColumns();
  }
  if (MessageLength != 0)
    CmdArgs.push_back(
        Args.MakeArgString("-fmessage-length=" + llvm::Twine(MessageLength)));

  if (Arg *A = Args.getLastArg(options::OPT_frandomize_layout_seed_EQ))
    CmdArgs.push_back(Args.MakeArgString("-frandomize-layout-seed=" +
                                         llvm::Twine(A->getValue(0))));

  if (Arg *A = Args.getLastArg(options::OPT_frandomize_layout_seed_file_EQ))
    CmdArgs.push_back(Args.MakeArgString("-frandomize-layout-seed-file=" +
                                         llvm::Twine(A->getValue(0))));

  // -fvisibility= and -fvisibility-ms-compat are of a piece.
  if (const Arg *A = Args.getLastArg(options::OPT_fvisibility_EQ,
                                     options::OPT_fvisibility_ms_compat)) {
    if (A->getOption().matches(options::OPT_fvisibility_EQ)) {
      A->render(Args, CmdArgs);
    } else {
      assert(A->getOption().matches(options::OPT_fvisibility_ms_compat));
      CmdArgs.push_back("-fvisibility=hidden");
      CmdArgs.push_back("-ftype-visibility=default");
    }
  }

  if (const Arg *A =
          Args.getLastArg(options::OPT_fvisibility_from_dllstorageclass,
                          options::OPT_fno_visibility_from_dllstorageclass)) {
    if (A->getOption().matches(options::OPT_fvisibility_from_dllstorageclass)) {
      CmdArgs.push_back("-fvisibility-from-dllstorageclass");
      Args.AddLastArg(CmdArgs, options::OPT_fvisibility_dllexport_EQ);
      Args.AddLastArg(CmdArgs, options::OPT_fvisibility_nodllstorageclass_EQ);
      Args.AddLastArg(CmdArgs, options::OPT_fvisibility_externs_dllimport_EQ);
      Args.AddLastArg(CmdArgs,
                      options::OPT_fvisibility_externs_nodllstorageclass_EQ);
    }
  }

  Args.AddLastArg(CmdArgs, options::OPT_ftlsmodel_EQ);

  // Forward -f (flag) options which we can pass directly.
  Args.AddLastArg(CmdArgs, options::OPT_femit_all_decls);
  Args.AddLastArg(CmdArgs, options::OPT_fdigraphs, options::OPT_fno_digraphs);
  Args.AddLastArg(CmdArgs, options::OPT_fzero_call_used_regs_EQ);

  if (Args.hasFlag(options::OPT_femulated_tls, options::OPT_fno_emulated_tls,
                   Triple.hasDefaultEmulatedTLS()))
    CmdArgs.push_back("-femulated-tls");

  if (Arg *A = Args.getLastArg(options::OPT_fzero_call_used_regs_EQ)) {
    if (!Triple.isX86() && !Triple.isAArch64())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
  }

  if (Args.hasArg(options::OPT_fsanitize_EQ, options::OPT_fsanitize_recover_EQ,
                  options::OPT_fsanitize_trap_EQ,
                  options::OPT_fsanitize_ignorelist_EQ,
                  options::OPT_fsanitize_system_ignorelist_EQ)) {
    D.Diag(diag::err_drv_unsupported) << "Sanitizer (-fsanitize*)";
    return;
  }

  if (Arg *A = Args.getLastArg(options::OPT_fpatchable_function_entry_EQ)) {
    llvm::StringRef S0 = A->getValue(), S = S0;
    unsigned Size, Offset = 0;
    if (!Triple.isAArch64() && !Triple.isX86())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    else if (S.consumeInteger(10, Size) ||
             (!S.empty() && (!S.consume_front(",") ||
                             S.consumeInteger(10, Offset) || !S.empty())))
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << S0 << A->getOption().getName();
    else if (Size < Offset)
      D.Diag(diag::err_drv_unsupported_fpatchable_function_entry_argument);
    else {
      CmdArgs.push_back(
          Args.MakeArgString(A->getSpelling() + llvm::Twine(Size)));
      CmdArgs.push_back(Args.MakeArgString(
          "-fpatchable-function-entry-offset=" + llvm::Twine(Offset)));
    }
  }

  Args.AddLastArg(CmdArgs, options::OPT_fms_hotpatch);

  for (const Arg *A : Args.filtered(options::OPT_b, options::OPT_K)) {
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getSpelling() << TripleStr;
  }

  Args.AddLastArg(CmdArgs, options::OPT_flax_vector_conversions_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fdiagnostics_print_source_range_info);
  Args.AddLastArg(CmdArgs, options::OPT_fdiagnostics_parseable_fixits);
  Args.AddLastArg(CmdArgs, options::OPT_ftime_report);
  Args.AddLastArg(CmdArgs, options::OPT_ftime_report_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_ftrapv);
  Args.AddLastArg(CmdArgs, options::OPT_malign_double);
  Args.AddLastArg(CmdArgs, options::OPT_fno_temp_file);

  if (const char *Name = C.getTimeTraceFile(&JA)) {
    CmdArgs.push_back(Args.MakeArgString("-ftime-trace=" + llvm::Twine(Name)));
    Args.AddLastArg(CmdArgs, options::OPT_ftime_trace_granularity_EQ);
  }

  if (Arg *A = Args.getLastArg(options::OPT_ftrapv_handler_EQ)) {
    CmdArgs.push_back("-ftrapv-handler");
    CmdArgs.push_back(A->getValue());
  }

  Args.AddLastArg(CmdArgs, options::OPT_ftrap_function_EQ);

  if (Arg *A = Args.getLastArg(options::OPT_fwrapv, options::OPT_fno_wrapv)) {
    if (A->getOption().matches(options::OPT_fwrapv))
      CmdArgs.push_back("-fwrapv");
  } else if (Arg *A = Args.getLastArg(options::OPT_fstrict_overflow,
                                      options::OPT_fno_strict_overflow)) {
    if (A->getOption().matches(options::OPT_fno_strict_overflow))
      CmdArgs.push_back("-fwrapv");
  }

  Args.AddLastArg(CmdArgs, options::OPT_ffinite_loops,
                  options::OPT_fno_finite_loops);

  Args.AddLastArg(CmdArgs, options::OPT_fwritable_strings);
  Args.AddLastArg(CmdArgs, options::OPT_funroll_loops,
                  options::OPT_fno_unroll_loops);

  Args.AddLastArg(CmdArgs, options::OPT_fstrict_flex_arrays_EQ);

  Args.AddLastArg(CmdArgs, options::OPT_pthread);

  Args.addOptInFlag(CmdArgs, options::OPT_mspeculative_load_hardening,
                    options::OPT_mno_speculative_load_hardening);

  renderSSPOptions(D, TC, Args, CmdArgs, Kernel);
  renderSCPOptions(TC, Args, CmdArgs);
  renderTrivialAutoVarInitOptions(D, TC, Args, CmdArgs);

  Args.addOptInFlag(CmdArgs, options::OPT_mstackrealign,
                    options::OPT_mno_stackrealign);

  if (Args.hasArg(options::OPT_mstack_alignment)) {
    llvm::StringRef alignment =
        Args.getLastArgValue(options::OPT_mstack_alignment);
    CmdArgs.push_back(Args.MakeArgString("-mstack-alignment=" + alignment));
  }

  if (Args.hasArg(options::OPT_mstack_probe_size)) {
    llvm::StringRef Size = Args.getLastArgValue(options::OPT_mstack_probe_size);

    if (!Size.empty())
      CmdArgs.push_back(Args.MakeArgString("-mstack-probe-size=" + Size));
    else
      CmdArgs.push_back("-mstack-probe-size=0");
  }

  Args.addOptOutFlag(CmdArgs, options::OPT_mstack_arg_probe,
                     options::OPT_mno_stack_arg_probe);

  if (Arg *A = Args.getLastArg(options::OPT_fcf_protection_EQ)) {
    CmdArgs.push_back(
        Args.MakeArgString(llvm::Twine("-fcf-protection=") + A->getValue()));
  }

  if (Arg *A = Args.getLastArg(options::OPT_mfunction_return_EQ))
    CmdArgs.push_back(
        Args.MakeArgString(llvm::Twine("-mfunction-return=") + A->getValue()));

  Args.AddLastArg(CmdArgs, options::OPT_mindirect_branch_cs_prefix);

  if (Args.hasFlag(options::OPT_funique_internal_linkage_names,
                   options::OPT_fno_unique_internal_linkage_names, false))
    CmdArgs.push_back("-funique-internal-linkage-names");

  renderBuiltinOptions(TC, RawTriple, Args, CmdArgs);

  if (Args.hasFlag(options::OPT_fshort_enums, options::OPT_fno_short_enums,
                   false))
    CmdArgs.push_back("-fshort-enums");

  renderCharacterOptions(Args, RawTriple, CmdArgs);

  CmdArgs.push_back("-fno-use-cxa-atexit");

  Args.addOptInFlag(CmdArgs, options::OPT_fuse_line_directives,
                    options::OPT_fno_use_line_directives);

  // -fno-minimize-whitespace is default.
  if (Args.hasFlag(options::OPT_fminimize_whitespace,
                   options::OPT_fno_minimize_whitespace, false)) {
    types::ID InputType = Inputs[0].getType();
    if (!isDerivedFromC(InputType))
      D.Diag(diag::err_drv_opt_unsupported_input_type)
          << "-fminimize-whitespace" << types::getTypeName(InputType);
    CmdArgs.push_back("-fminimize-whitespace");
  }

  // -fno-keep-system-includes is default.
  if (Args.hasFlag(options::OPT_fkeep_system_includes,
                   options::OPT_fno_keep_system_includes, false)) {
    types::ID InputType = Inputs[0].getType();
    if (!isDerivedFromC(InputType))
      D.Diag(diag::err_drv_opt_unsupported_input_type)
          << "-fkeep-system-includes" << types::getTypeName(InputType);
    CmdArgs.push_back("-fkeep-system-includes");
  }

  // Enable -fms-extensions by default for Windows/COFF targets.
  if (Args.hasFlag(options::OPT_fms_extensions, options::OPT_fno_ms_extensions,
                   IsWindowsCOFF))
    CmdArgs.push_back("-fms-extensions");

  // Enable -fms-compatibility by default for Windows/COFF targets when
  // -fms-extensions is enabled.
  bool IsMSVCCompat = Args.hasFlag(
      options::OPT_fms_compatibility, options::OPT_fno_ms_compatibility,
      (IsWindowsCOFF && Args.hasFlag(options::OPT_fms_extensions,
                                     options::OPT_fno_ms_extensions, true)));
  if (IsMSVCCompat)
    CmdArgs.push_back("-fms-compatibility");

  if (IsWindowsMSVC)
    processVSRuntimeLibrary(D, Args, CmdArgs);

  addNeverCSpecificFlags(Args, CmdArgs);
  llvm::VersionTuple GNUCVer;
  if (Arg *A = Args.getLastArg(options::OPT_fgnuc_version_EQ)) {
    llvm::StringRef Val = A->getValue();
    Val = Val.empty() ? "0" : Val; // Treat "" as 0 or disable.
    bool Invalid = GNUCVer.tryParse(Val);
    unsigned Minor = GNUCVer.getMinor();
    unsigned Patch = GNUCVer.getSubminor();
    if (Invalid || GNUCVer.getBuild() || Minor >= 100 || Patch >= 100) {
      D.Diag(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    }
  } else if (!IsMSVCCompat) {
    // Imitate GCC 4.2.1 by default if -fms-compatibility is not in effect.
    GNUCVer = llvm::VersionTuple(4, 2, 1);
  }
  if (!GNUCVer.empty()) {
    CmdArgs.push_back(
        Args.MakeArgString("-fgnuc-version=" + GNUCVer.getAsString()));
  }

  llvm::VersionTuple MSVT = TC.computeMSVCVersion(&D, Args);
  if (!MSVT.empty())
    CmdArgs.push_back(
        Args.MakeArgString("-fms-compatibility-version=" + MSVT.getAsString()));

  if (ImplyMSVCStd) {
    llvm::StringRef LanguageStandard;
    if (const Arg *StdArg = Args.getLastArg(options::OPT_msvc_std)) {
      Std = StdArg;
      LanguageStandard = llvm::StringSwitch<llvm::StringRef>(StdArg->getValue())
                             .Case("c11", "-std=c11")
                             .Case("c17", "-std=c17")
                             .Default("");
      if (LanguageStandard.empty())
        D.Diag(neverc::diag::warn_drv_unused_argument)
            << StdArg->getAsString(Args);
    }
    CmdArgs.push_back(LanguageStandard.data());
  }

  if (Args.hasFlag(options::OPT_fdeclspec, options::OPT_fno_declspec, false))
    CmdArgs.push_back("-fdeclspec");
  else if (Args.hasArg(options::OPT_fno_declspec))
    CmdArgs.push_back("-fno-declspec");

  // -fgnu-keywords default varies depending on language; only pass if
  // specified.
  Args.AddLastArg(CmdArgs, options::OPT_fgnu_keywords,
                  options::OPT_fno_gnu_keywords);

  Args.addOptInFlag(CmdArgs, options::OPT_fgnu89_inline,
                    options::OPT_fno_gnu89_inline);

  const Arg *InlineArg = Args.getLastArg(options::OPT_finline_functions,
                                         options::OPT_finline_hint_functions,
                                         options::OPT_fno_inline_functions);
  if (Arg *A = Args.getLastArg(options::OPT_fno_inline)) {
    A->render(Args, CmdArgs);
  } else if (InlineArg) {
    InlineArg->render(Args, CmdArgs);
  }

  Args.AddLastArg(CmdArgs, options::OPT_finline_max_stacksize_EQ);

  if (!IsWindowsMSVC)
    addExceptionArgs(Args);

  if (Args.hasFlag(options::OPT_fignore_exceptions,
                   options::OPT_fno_ignore_exceptions, false)) {
    CmdArgs.push_back("-fno-exceptions");
    CmdArgs.push_back("-fno-async-exceptions");
  } else {
    Arg *A = Args.getLastArg(options::OPT_fseh_exceptions);
    if (A || TC.GetExceptionModel(Args) == llvm::ExceptionHandling::WinEH)
      CmdArgs.push_back("-exception-model=seh");
  }

  // Honor -fpack-struct= and -fpack-struct, if given. Note that
  // -fno-pack-struct doesn't apply to -fpack-struct=.
  if (Arg *A = Args.getLastArg(options::OPT_fpack_struct_EQ)) {
    std::string PackStructStr = "-fpack-struct=";
    PackStructStr += A->getValue();
    CmdArgs.push_back(Args.MakeArgString(PackStructStr));
  } else if (Args.hasFlag(options::OPT_fpack_struct,
                          options::OPT_fno_pack_struct, false)) {
    CmdArgs.push_back("-fpack-struct=1");
  }
  bool SkipMaxTypeAlign = Args.hasArg(options::OPT_fno_max_type_align);
  if (Arg *A = Args.getLastArg(options::OPT_fmax_type_align_EQ)) {
    if (!SkipMaxTypeAlign) {
      std::string MaxTypeAlignStr = "-fmax-type-align=";
      MaxTypeAlignStr += A->getValue();
      CmdArgs.push_back(Args.MakeArgString(MaxTypeAlignStr));
    }
  } else if (RawTriple.isOSDarwin()) {
    if (!SkipMaxTypeAlign) {
      std::string MaxTypeAlignStr = "-fmax-type-align=16";
      CmdArgs.push_back(Args.MakeArgString(MaxTypeAlignStr));
    }
  }

  if (!Args.hasFlag(options::OPT_Qy, options::OPT_Qn, true))
    CmdArgs.push_back("-Qn");

  Args.addOptInFlag(CmdArgs, options::OPT_fcommon, options::OPT_fno_common);

  if (!Args.hasFlag(options::OPT_fsigned_bitfields,
                    options::OPT_funsigned_bitfields, true))
    D.Diag(diag::warn_drv_unsupported)
        << Args.getLastArg(options::OPT_funsigned_bitfields)->getAsString(Args);

  if (Arg *inputCharset = Args.getLastArg(options::OPT_finput_charset_EQ)) {
    llvm::StringRef value = inputCharset->getValue();
    if (!value.equals_insensitive("utf-8"))
      D.Diag(diag::err_drv_invalid_value)
          << inputCharset->getAsString(Args) << value;
  }

  if (Arg *execCharset = Args.getLastArg(options::OPT_fexec_charset_EQ)) {
    llvm::StringRef value = execCharset->getValue();
    if (!value.equals_insensitive("utf-8"))
      D.Diag(diag::err_drv_invalid_value)
          << execCharset->getAsString(Args) << value;
  }

  renderDiagnosticsOptions(D, Args, CmdArgs);

  Args.addOptInFlag(CmdArgs, options::OPT_fasm_blocks,
                    options::OPT_fno_asm_blocks);

  Args.addOptOutFlag(CmdArgs, options::OPT_fgnu_inline_asm,
                     options::OPT_fno_gnu_inline_asm);

  bool EnableVec = shouldEnableVectorizerAtOLevel(Args, false);
  OptSpecifier VectorizeAliasOption =
      EnableVec ? options::OPT_O_Group : options::OPT_fvectorize;
  if (Args.hasFlag(options::OPT_fvectorize, VectorizeAliasOption,
                   options::OPT_fno_vectorize, EnableVec))
    CmdArgs.push_back("-vectorize-loops");

  bool EnableSLPVec = shouldEnableVectorizerAtOLevel(Args, true);
  OptSpecifier SLPVectAliasOption =
      EnableSLPVec ? options::OPT_O_Group : options::OPT_fslp_vectorize;
  if (Args.hasFlag(options::OPT_fslp_vectorize, SLPVectAliasOption,
                   options::OPT_fno_slp_vectorize, EnableSLPVec))
    CmdArgs.push_back("-vectorize-slp");

  parseMPreferVectorWidth(D, Args, CmdArgs);

  if (Arg *A = Args.getLastArg(options::OPT_fparallel_codegen_EQ))
    A->render(Args, CmdArgs);
  else if (Args.hasArg(options::OPT_fno_parallel_codegen))
    CmdArgs.push_back("-fno-parallel-codegen");

  // -fdollars-in-identifiers default varies depending on platform and
  // language; only pass if specified.
  if (Arg *A = Args.getLastArg(options::OPT_fdollars_in_identifiers,
                               options::OPT_fno_dollars_in_identifiers)) {
    if (A->getOption().matches(options::OPT_fdollars_in_identifiers))
      CmdArgs.push_back("-fdollars-in-identifiers");
    else
      CmdArgs.push_back("-fno-dollars-in-identifiers");
  }

  // Remarks can be enabled with any of the `-f.*optimization-record.*` flags.
  if (willEmitRemarks(Args) && checkRemarksOptions(D, Args, Triple))
    renderRemarksOptions(Args, CmdArgs, Triple, Input, Output, JA);

  Args.addOptInFlag(CmdArgs, options::OPT_fdirectives_only,
                    options::OPT_fno_directives_only);

  // Only allow -traditional or -traditional-cpp outside in preprocessing modes.
  if (Arg *A = Args.getLastArg(options::OPT_traditional,
                               options::OPT_traditional_cpp)) {
    if (isa<PreprocessJobAction>(JA))
      CmdArgs.push_back("-traditional-cpp");
    else
      D.Diag(diag::err_drv_unsupported) << A->getAsString(Args);
  }

  Args.AddLastArg(CmdArgs, options::OPT_dM);
  Args.AddLastArg(CmdArgs, options::OPT_dD);
  Args.AddLastArg(CmdArgs, options::OPT_dI);

  Args.AddLastArg(CmdArgs, options::OPT_fmax_tokens_EQ);

  // Forward -fpass-plugin=name.so to the frontend.
  for (const Arg *A : Args.filtered(options::OPT_fpass_plugin_EQ)) {
    CmdArgs.push_back(
        Args.MakeArgString(llvm::Twine("-fpass-plugin=") + A->getValue()));
    A->claim();
  }

  // Forward --vfsoverlay to the frontend.
  for (const Arg *A : Args.filtered(options::OPT_vfsoverlay)) {
    CmdArgs.push_back("--vfsoverlay");
    CmdArgs.push_back(A->getValue());
    A->claim();
  }

  // Setup statistics file output.
  llvm::SmallString<128> StatsFile = getStatsFileName(Args, Output, Input, D);
  if (!StatsFile.empty()) {
    CmdArgs.push_back(
        Args.MakeArgString(llvm::Twine("-stats-file=") + StatsFile));
  }

  // Forward internal -fshellcode-mode marker to the frontend when set.
  Args.AddLastArg(CmdArgs, options::OPT_fshellcode_mode);

  // Forward -mllvm arguments to the LLVM option parser.
  for (const Arg *A : Args.filtered(options::OPT_mllvm)) {
    A->claim();

    if (llvm::StringRef(A->getValue(0)) == "-disable-llvm-optzns") {
      CmdArgs.push_back("-disable-llvm-optzns");
    } else {
      A->render(Args, CmdArgs);
    }
  }

  if (C.getDriver().isSaveTempsEnabled() && isa<CompileJobAction>(JA))
    CmdArgs.push_back("-disable-llvm-passes");

  Args.AddAllArgs(CmdArgs, options::OPT_undef);

  const char *Exec = D.getNeverCProgramPath();

  renderDebugRecordingFlags(TC, D, Triple, TripleStr, Exec, Args, CmdArgs);

  if (Arg *A = Args.getLastArg(options::OPT_fglobal_isel,
                               options::OPT_fno_global_isel)) {
    CmdArgs.push_back("-mllvm");
    if (A->getOption().matches(options::OPT_fglobal_isel)) {
      CmdArgs.push_back("-global-isel=1");

      // GISel is on by default on AArch64 -O0, so don't bother adding
      // the fallback remarks for it. Other combinations will add a warning of
      // some kind.
      bool IsArchSupported = Triple.getArch() == llvm::Triple::aarch64;
      bool IsOptLevelSupported = false;

      Arg *A = Args.getLastArg(options::OPT_O_Group);
      if (Triple.getArch() == llvm::Triple::aarch64) {
        if (!A || A->getOption().matches(options::OPT_O0))
          IsOptLevelSupported = true;
      }
      if (!IsArchSupported || !IsOptLevelSupported) {
        CmdArgs.push_back("-mllvm");
        CmdArgs.push_back("-global-isel-abort=2");

        if (!IsArchSupported)
          D.Diag(diag::warn_drv_global_isel_incomplete) << Triple.getArchName();
        else
          D.Diag(diag::warn_drv_global_isel_incomplete_opt);
      }
    } else {
      CmdArgs.push_back("-global-isel=0");
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_fforce_enable_int128,
                               options::OPT_fno_force_enable_int128)) {
    if (A->getOption().matches(options::OPT_fforce_enable_int128))
      CmdArgs.push_back("-fforce-enable-int128");
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fkeep_static_consts,
                    options::OPT_fno_keep_static_consts);
  Args.addOptInFlag(CmdArgs, options::OPT_fkeep_persistent_storage_variables,
                    options::OPT_fno_keep_persistent_storage_variables);
  addMachineOutlinerArgs(D, Args, CmdArgs, Triple);

  if (Arg *A = Args.getLastArg(options::OPT_moutline_atomics,
                               options::OPT_mno_outline_atomics)) {
    // Option -moutline-atomics supported for AArch64 target only.
    if (!Triple.isAArch64()) {
      D.Diag(diag::warn_drv_moutline_atomics_unsupported_opt)
          << Triple.getArchName() << A->getOption().getName();
    } else {
      if (A->getOption().matches(options::OPT_moutline_atomics)) {
        CmdArgs.push_back("-target-feature");
        CmdArgs.push_back("+outline-atomics");
      } else {
        CmdArgs.push_back("-target-feature");
        CmdArgs.push_back("-outline-atomics");
      }
    }
  } else if (Triple.isAArch64() &&
             getToolChain().IsAArch64OutlineAtomicsDefault(Args)) {
    CmdArgs.push_back("-target-feature");
    CmdArgs.push_back("+outline-atomics");
  }

  if (Triple.isAArch64() &&
      (Args.hasArg(options::OPT_mno_fmv) ||
       getToolChain().GetRuntimeLibType(Args) != ToolChain::RLT_CompilerRT)) {
    // Disable Function Multiversioning on AArch64 target.
    CmdArgs.push_back("-target-feature");
    CmdArgs.push_back("-fmv");
  }

  if (Args.hasFlag(options::OPT_faddrsig, options::OPT_fno_addrsig,
                   TC.getTriple().isOSBinFormatELF() ||
                       TC.getTriple().isOSBinFormatCOFF()))
    CmdArgs.push_back("-faddrsig");

  if ((Triple.isOSBinFormatELF() || Triple.isOSBinFormatMachO()) &&
      (UnwindTables || AsyncUnwindTables ||
       DebugInfoKind != llvm::codegenoptions::NoDebugInfo))
    CmdArgs.push_back("-D__GCC_HAVE_DWARF2_CFI_ASM=1");

  if (Arg *A = Args.getLastArg(options::OPT_fsymbol_partition_EQ)) {
    std::string Str = A->getAsString(Args);
    if (!TC.getTriple().isOSBinFormatELF())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << Str << TC.getTripleString();
    CmdArgs.push_back(Args.MakeArgString(Str));
  }

  if (Output.getType() == types::TY_Dependencies) {
    // Handled with other dependency code.
  } else if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    assert(Output.isNothing() && "Invalid output.");
  }

  addDashXForInput(Args, Input, CmdArgs);

  llvm::ArrayRef<InputInfo> FrontendInputs = Input;
  if (Input.isNothing())
    FrontendInputs = {};

  for (const InputInfo &Input : FrontendInputs) {
    if (Input.isFilename())
      CmdArgs.push_back(Input.getFilename());
    else
      Input.getInputArg().renderAsInput(Args, CmdArgs);
  }

  {
    auto Cmd = std::make_unique<FrontendCommand>(
        JA, *this, ResponseFileSupport::AtFileUTF8(), Exec, CmdArgs, Inputs,
        Output, D.getPrependArg());

    populateDirectInvocationOptsForFrontendJob(Cmd->getDirectOpts(), JA, Output,
                                               FrontendInputs, CmdArgs,
                                               TripleStr, CPU);

    if (IsUsingLTO && !Args.hasArg(options::OPT_c, options::OPT_S,
                                   options::OPT_E, options::OPT_fsyntax_only))
      Cmd->getDirectOpts().InMemoryLTOOutput = true;

    C.addCommand(std::move(Cmd));
  }

  // Make the compile command echo its inputs for /showFilenames.
  if (Output.getType() == types::TY_Object &&
      Args.hasFlag(options::OPT_fms_show_filenames,
                   options::OPT_fno_ms_show_filenames, false)) {
    C.getJobs().getJobs().back()->PrintInputFilenames = true;
  }

  // Claim some arguments which NeverC supports automatically.

  // Disable warnings for NeverC -E -emit-llvm foo.c
  Args.ClaimAllArgs(options::OPT_emit_llvm);
}

NeverC::NeverC(const ToolChain &TC, bool HasIntegratedBackend)
    : Tool("neverc", "neverc frontend", TC), HasBackend(HasIntegratedBackend) {}

NeverC::~NeverC() {}

// ===----------------------------------------------------------------------===
// MSVC compatibility & assembler tool
// ===----------------------------------------------------------------------===

void NeverC::AddMSVCCompatArgs(Compilation &C, const ArgList &Args,
                               types::ID InputType,
                               ArgStringList &CmdArgs) const {
  if (Arg *ShowIncludes =
          Args.getLastArg(options::OPT_fms_show_includes,
                          options::OPT_fms_show_includes_user)) {
    CmdArgs.push_back("--show-includes");
    if (ShowIncludes->getOption().matches(options::OPT_fms_show_includes))
      CmdArgs.push_back("-sys-header-deps");
  }

  // MSVC Buffer Security Check (/GS).  Off by default; opt-in via
  // -fms-buffer-security-check.  -fno-stack-protector takes precedence.
  if (Args.hasFlag(options::OPT_fms_buffer_security_check,
                   options::OPT_fno_ms_buffer_security_check,
                   /*Default=*/false) &&
      !Args.hasArg(options::OPT_fno_stack_protector)) {
    CmdArgs.push_back("-stack-protector");
    CmdArgs.push_back(Args.MakeArgString(llvm::Twine(LangOptions::SSPStrong)));
  }

  const Driver &D = getToolChain().getDriver();

  Args.ClaimAllArgs(options::OPT_fms_exception_model);

  // /EP should expand to -E -P.
  if (Args.hasArg(options::OPT_msvc_preprocess_no_linemarkers)) {
    CmdArgs.push_back("-E");
    CmdArgs.push_back("-P");
  }

  if (Args.hasArg(options::OPT_fms_kernel)) {
    std::vector<std::string> Values =
        Args.getAllArgValues(options::OPT_msvc_arch);
    if (!Values.empty()) {
      llvm::SmallSet<std::string, 4> SupportedArches;

      for (auto &V : Values)
        if (!SupportedArches.contains(V))
          D.Diag(diag::err_drv_argument_not_allowed_with)
              << std::string("-msvc-arch=").append(V) << "-fms-kernel";
    }
  }

  // Parse the default calling convention option.
  if (Arg *CCArg = Args.getLastArg(options::OPT_fms_default_calling_conv)) {
    llvm::StringRef Val = CCArg->getValue();
    const char *DCCFlag = nullptr;
    bool ArchSupported = true;
    llvm::Triple::ArchType Arch = getToolChain().getArch();

    if (Val == "cdecl")
      DCCFlag = "-fdefault-calling-conv=cdecl";
    else if (Val == "fastcall")
      DCCFlag = "-fdefault-calling-conv=fastcall";
    else if (Val == "stdcall")
      DCCFlag = "-fdefault-calling-conv=stdcall";
    else if (Val == "vectorcall") {
      ArchSupported = Arch == llvm::Triple::x86_64;
      DCCFlag = "-fdefault-calling-conv=vectorcall";
    } else if (Val == "regcall") {
      ArchSupported = Arch == llvm::Triple::x86_64;
      DCCFlag = "-fdefault-calling-conv=regcall";
    }

    if (ArchSupported && DCCFlag)
      CmdArgs.push_back(DCCFlag);
  }

  if (!Args.hasArg(options::OPT_fdiagnostics_format_EQ)) {
    CmdArgs.push_back("-fdiagnostics-format");
    CmdArgs.push_back("msvc");
  }

  if (Args.hasArg(options::OPT_fms_kernel))
    CmdArgs.push_back("-fms-kernel");

  for (const Arg *A : Args.filtered(options::OPT_fms_guard)) {
    llvm::StringRef GuardArgs = A->getValue();
    // The only valid options are "cf", "cf,nochecks", "cf-", "ehcont" and
    // "ehcont-".
    if (GuardArgs.equals_insensitive("cf")) {
      CmdArgs.push_back("-cfguard");
    } else if (GuardArgs.equals_insensitive("cf,nochecks")) {
      CmdArgs.push_back("-cfguard-no-checks");
    } else if (GuardArgs.equals_insensitive("ehcont")) {
      CmdArgs.push_back("-ehcontguard");
    } else if (GuardArgs.equals_insensitive("cf-") ||
               GuardArgs.equals_insensitive("ehcont-")) {
      // Do nothing, but we might want to emit a security warning in future.
    } else {
      D.Diag(diag::err_drv_invalid_value) << A->getSpelling() << GuardArgs;
    }
    A->claim();
  }
}

const char *NeverC::getBaseInputName(const ArgList &Args,
                                     const InputInfo &Input) {
  return Args.MakeArgString(llvm::sys::path::filename(Input.getBaseInput()));
}

const char *NeverC::getBaseInputStem(const ArgList &Args,
                                     const InputInfoList &Inputs) {
  const char *Str = getBaseInputName(Args, Inputs[0]);

  if (const char *End = strrchr(Str, '.'))
    return Args.MakeArgString(std::string(Str, End));

  return Str;
}

const char *NeverC::getDependencyFileName(const ArgList &Args,
                                          const InputInfoList &Inputs) {
  if (Arg *OutputOpt = Args.getLastArg(options::OPT_o)) {
    llvm::SmallString<128> OutputFilename(OutputOpt->getValue());
    llvm::sys::path::replace_extension(OutputFilename, llvm::Twine('d'));
    return Args.MakeArgString(OutputFilename);
  }

  return Args.MakeArgString(llvm::Twine(getBaseInputStem(Args, Inputs)) + ".d");
}

void NeverCAs::AddX86TargetArgs(const ArgList &Args,
                                ArgStringList &CmdArgs) const {
  addX86AlignBranchArgs(getToolChain().getDriver(), Args, CmdArgs);

  if (Arg *A = Args.getLastArg(options::OPT_masm_EQ)) {
    llvm::StringRef Value = A->getValue();
    if (Value == "intel" || Value == "att") {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back(Args.MakeArgString("-x86-asm-syntax=" + Value));
    } else {
      getToolChain().getDriver().Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Value;
    }
  }
}

void NeverCAs::ConstructJob(Compilation &C, const JobAction &JA,
                            const InputInfo &Output,
                            const InputInfoList &Inputs, const ArgList &Args,
                            const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  assert(Inputs.size() == 1 && "Unexpected number of inputs.");
  const InputInfo &Input = Inputs[0];

  const llvm::Triple &Triple = getToolChain().getEffectiveTriple();
  const std::string &TripleStr = Triple.getTriple();
  const auto &D = getToolChain().getDriver();

  // Don't warn about "neverc -w -c foo.s"
  Args.ClaimAllArgs(options::OPT_w);
  // and "neverc -emit-llvm -c foo.s"
  Args.ClaimAllArgs(options::OPT_emit_llvm);

  claimNoWarnArgs(Args);

  // Add the "effective" target triple.
  CmdArgs.push_back("-triple");
  CmdArgs.push_back(Args.MakeArgString(TripleStr));

  getToolChain().addNeverCFrontendAsTargetOptions(Args, CmdArgs);

  CmdArgs.push_back("-filetype");
  CmdArgs.push_back("obj");

  CmdArgs.push_back("-main-file-name");
  CmdArgs.push_back(NeverC::getBaseInputName(Args, Input));

  // Add the target cpu
  std::string CPU = getCPUName(D, Args, Triple, /*FromAs*/ true);
  if (!CPU.empty()) {
    CmdArgs.push_back("-target-cpu");
    CmdArgs.push_back(Args.MakeArgString(CPU));
  }

  // Add the target features
  getTargetFeatures(D, Triple, Args, CmdArgs, true);

  // Ignore explicit -force_cpusubtype_ALL option.
  Args.ClaimAllArgs(options::OPT_force__cpusubtype__ALL);

  // Pass along any -I options so we get proper .include search paths.
  Args.AddAllArgs(CmdArgs, options::OPT_I_Group);

  auto FindSource = [](const Action *S) -> const Action * {
    while (S->getKind() != Action::InputClass) {
      assert(!S->getInputs().empty() && "unexpected root action!");
      S = S->getInputs()[0];
    }
    return S;
  };
  const Action *SourceAction = FindSource(&JA);

  // Forward -g and handle debug info related flags, assuming we are dealing
  // with an actual assembly file.
  bool WantDebug = false;
  Args.ClaimAllArgs(options::OPT_g_Group);
  if (Arg *A = Args.getLastArg(options::OPT_g_Group))
    WantDebug = !A->getOption().matches(options::OPT_g0) &&
                !A->getOption().matches(options::OPT_ggdb0);

  llvm::codegenoptions::DebugInfoKind DebugInfoKind =
      llvm::codegenoptions::NoDebugInfo;

  // Add the -fdebug-compilation-dir flag if needed.
  const char *DebugCompilationDir =
      addDebugCompDirArg(Args, CmdArgs, C.getDriver().getVFS());

  if (SourceAction->getType() == types::TY_Asm ||
      SourceAction->getType() == types::TY_PP_Asm) {
    // You might think that it would be ok to set DebugInfoKind outside of
    // the guard for source type, however there is a test which asserts
    // that some assembler invocation receives no -debug-info-kind,
    // and it's not clear whether that test is just overly restrictive.
    DebugInfoKind = (WantDebug ? llvm::codegenoptions::DebugInfoConstructor
                               : llvm::codegenoptions::NoDebugInfo);

    addDebugPrefixMapArg(getToolChain().getDriver(), getToolChain(), Args,
                         CmdArgs);

    CmdArgs.push_back("-dwarf-debug-producer");
    CmdArgs.push_back(Args.MakeArgString(getNeverCFullVersion()));

    // And pass along -I options
    Args.AddAllArgs(CmdArgs, options::OPT_I);
  }
  const unsigned DwarfVersion = getDwarfVersion(getToolChain(), Args);
  renderDebugEnablingArgs(Args, CmdArgs, DebugInfoKind, DwarfVersion,
                          llvm::DebuggerKind::Default);
  renderDwarfFormat(D, Triple, Args, CmdArgs, DwarfVersion);
  renderDebugInfoCompressionArgs(Args, CmdArgs, D, getToolChain());

  // Optionally embed assembler-level arguments into the debug info, for build
  // analysis.
  if (getToolChain().UseDwarfDebugFlags()) {
    ArgStringList OriginalArgs;
    for (const auto &Arg : Args)
      Arg->render(Args, OriginalArgs);

    llvm::SmallString<256> Flags;
    const char *Exec = getToolChain().getDriver().getNeverCProgramPath();
    escapeSpacesAndBackslashes(Exec, Flags);
    for (const char *OriginalArg : OriginalArgs) {
      llvm::SmallString<128> EscapedArg;
      escapeSpacesAndBackslashes(OriginalArg, EscapedArg);
      Flags += " ";
      Flags += EscapedArg;
    }
    CmdArgs.push_back("-dwarf-debug-flags");
    CmdArgs.push_back(Args.MakeArgString(Flags));
  }

  // Add target specific flags.
  switch (getToolChain().getArch()) {
  default:
    break;

  case llvm::Triple::x86_64:
    AddX86TargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::aarch64:
    if (Args.hasArg(options::OPT_mmark_bti_property)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-aarch64-mark-bti-property");
    }
    break;
  }

  Args.ClaimAllArgs(options::OPT_W_Group);

  collectArgsForIntegratedAssembler(C, Args, CmdArgs,
                                    getToolChain().getDriver());

  Args.AddAllArgs(CmdArgs, options::OPT_mllvm);

  if (DebugInfoKind > llvm::codegenoptions::NoDebugInfo && Output.isFilename())
    addDebugObjectName(Args, CmdArgs, DebugCompilationDir,
                       Output.getFilename());

  // Fixup any previous commands that use -object-file-name because when we
  // generated them, the final .obj name wasn't yet known.
  for (Command &J : C.getJobs()) {
    if (SourceAction != FindSource(&J.getSource()))
      continue;
    auto &JArgs = J.getArguments();
    for (unsigned I = 0; I < JArgs.size(); ++I) {
      if (llvm::StringRef(JArgs[I]).starts_with("-object-file-name=") &&
          Output.isFilename()) {
        ArgStringList NewArgs(JArgs.begin(), JArgs.begin() + I);
        addDebugObjectName(Args, NewArgs, DebugCompilationDir,
                           Output.getFilename());
        NewArgs.append(JArgs.begin() + I + 1, JArgs.end());
        J.replaceArguments(NewArgs);
        break;
      }
    }
  }

  assert(Output.isFilename() && "Unexpected lipo output.");
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  const llvm::Triple &T = getToolChain().getTriple();
  Arg *A;
  if (getDebugFissionKind(D, Args, A) == DwarfFissionKind::Split &&
      T.isOSBinFormatELF()) {
    CmdArgs.push_back("-split-dwarf-output");
    CmdArgs.push_back(SplitDebugName(JA, Args, Input, Output));
  }

  assert(Input.isFilename() && "Invalid input.");
  CmdArgs.push_back(Input.getFilename());

  const char *Exec = getToolChain().getDriver().getNeverCProgramPath();
  auto AsCmd = std::make_unique<FrontendCommand>(
      JA, *this, ResponseFileSupport::AtFileUTF8(), Exec, CmdArgs, Inputs,
      Output, D.getPrependArg());
  populateDirectInvocationOptsForFrontendJob(AsCmd->getDirectOpts(), JA, Output,
                                             Inputs, CmdArgs, TripleStr, CPU);
  C.addCommand(std::move(AsCmd));
}
