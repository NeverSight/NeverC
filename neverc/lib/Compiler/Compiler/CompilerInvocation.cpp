#include "neverc/Compiler/CompilerInvocation.h"
#include "CompilerInvocationArg.h"
#include "neverc/Compiler/DependencyOutputOptions.h"
#include "neverc/Compiler/PrepOutputOptions.h"
#include "neverc/Foundation/Core/Visibility.h"
#include "llvm/Support/Process.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include <ctime>
#include <memory>

// ===----------------------------------------------------------------------===
// CompilerInvocationBase / CowCompilerInvocation
// ===----------------------------------------------------------------------===

namespace {
template <class T> std::shared_ptr<T> make_shared_copy(const T &X) {
  return std::make_shared<T>(X);
}

template <class T>
llvm::IntrusiveRefCntPtr<T> makeIntrusiveRefCntCopy(const T &X) {
  return llvm::makeIntrusiveRefCnt<T>(X);
}
} // namespace

CompilerInvocationBase::CompilerInvocationBase()
    : LangOpts(std::make_shared<LangOptions>()),
      TargetOpts(std::make_shared<TargetOptions>()),
      DiagnosticOpts(llvm::makeIntrusiveRefCnt<DiagnosticOptions>()),
      HSOpts(std::make_shared<HeaderIndexOptions>()),
      PPOpts(std::make_shared<PrepOptions>()),
      CodeGenOpts(std::make_shared<CodeGenOptions>()),
      FSOpts(std::make_shared<FileSystemOptions>()),
      FrontendOpts(std::make_shared<FrontendOptions>()),
      DependencyOutputOpts(std::make_shared<DependencyOutputOptions>()),
      PrepOutputOpts(std::make_shared<PrepOutputOptions>()) {}

CompilerInvocationBase &
CompilerInvocationBase::deep_copy_assign(const CompilerInvocationBase &X) {
  if (this != &X) {
    LangOpts = make_shared_copy(X.getLangOpts());
    TargetOpts = make_shared_copy(X.getTargetOpts());
    DiagnosticOpts = makeIntrusiveRefCntCopy(X.getDiagnosticOpts());
    HSOpts = make_shared_copy(X.getHeaderIdxOpts());
    PPOpts = make_shared_copy(X.getPrepOpts());
    CodeGenOpts = make_shared_copy(X.getCodeGenOpts());
    FSOpts = make_shared_copy(X.getFileSystemOpts());
    FrontendOpts = make_shared_copy(X.getFrontendOpts());
    DependencyOutputOpts = make_shared_copy(X.getDependencyOutputOpts());
    PrepOutputOpts = make_shared_copy(X.getPrepOutputOpts());
  }
  return *this;
}

CompilerInvocationBase &
CompilerInvocationBase::shallow_copy_assign(const CompilerInvocationBase &X) {
  if (this != &X) {
    LangOpts = X.LangOpts;
    TargetOpts = X.TargetOpts;
    DiagnosticOpts = X.DiagnosticOpts;
    HSOpts = X.HSOpts;
    PPOpts = X.PPOpts;
    CodeGenOpts = X.CodeGenOpts;
    FSOpts = X.FSOpts;
    FrontendOpts = X.FrontendOpts;
    DependencyOutputOpts = X.DependencyOutputOpts;
    PrepOutputOpts = X.PrepOutputOpts;
  }
  return *this;
}

namespace {
template <typename T> T &ensureOwned(std::shared_ptr<T> &Storage) {
  if (Storage.use_count() > 1)
    Storage = std::make_shared<T>(*Storage);
  return *Storage;
}

template <typename T> T &ensureOwned(llvm::IntrusiveRefCntPtr<T> &Storage) {
  if (Storage.useCount() > 1)
    Storage = llvm::makeIntrusiveRefCnt<T>(*Storage);
  return *Storage;
}
} // namespace

LangOptions &CowCompilerInvocation::getMutLangOpts() {
  return ensureOwned(LangOpts);
}

TargetOptions &CowCompilerInvocation::getMutTargetOpts() {
  return ensureOwned(TargetOpts);
}

DiagnosticOptions &CowCompilerInvocation::getMutDiagnosticOpts() {
  return ensureOwned(DiagnosticOpts);
}

HeaderIndexOptions &CowCompilerInvocation::getMutHeaderIdxOpts() {
  return ensureOwned(HSOpts);
}

PrepOptions &CowCompilerInvocation::getMutPrepOpts() {
  return ensureOwned(PPOpts);
}

CodeGenOptions &CowCompilerInvocation::getMutCodeGenOpts() {
  return ensureOwned(CodeGenOpts);
}

FileSystemOptions &CowCompilerInvocation::getMutFileSystemOpts() {
  return ensureOwned(FSOpts);
}

FrontendOptions &CowCompilerInvocation::getMutFrontendOpts() {
  return ensureOwned(FrontendOpts);
}

DependencyOutputOptions &CowCompilerInvocation::getMutDependencyOutputOpts() {
  return ensureOwned(DependencyOutputOpts);
}

PrepOutputOptions &CowCompilerInvocation::getMutPrepOutputOpts() {
  return ensureOwned(PrepOutputOpts);
}

// ===----------------------------------------------------------------------===
// Validation
// ===----------------------------------------------------------------------===

namespace {
bool validateInvocation(CompilerInvocation &Invocation,
                        DiagnosticsEngine &Diags, const ArgList &Args,
                        InputKind IK) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  LangOptions &LangOpts = Invocation.getLangOpts();
  CodeGenOptions &CodeGenOpts = Invocation.getCodeGenOpts();
  TargetOptions &TargetOpts = Invocation.getTargetOpts();
  FrontendOptions &FrontendOpts = Invocation.getFrontendOpts();
  CodeGenOpts.DisableFree = FrontendOpts.DisableFree;
  if (FrontendOpts.ShowStats)
    CodeGenOpts.ClearASTBeforeBackend = false;
  LangOpts.SpeculativeLoadHardening = CodeGenOpts.SpeculativeLoadHardening;

  llvm::Triple T(TargetOpts.Triple);

  CodeGenOpts.CodeModel = TargetOpts.CodeModel;
  CodeGenOpts.LargeDataThreshold = TargetOpts.LargeDataThreshold;

  if (LangOpts.IgnoreExceptions) {
    LangOpts.Exceptions = 0;
    LangOpts.EHAsynch = 0;
    LangOpts.setExceptionHandling(LangOptions::ExceptionHandlingKind::None);
  }

  if (LangOpts.getExceptionHandling() !=
          LangOptions::ExceptionHandlingKind::None &&
      T.isWindowsMSVCEnvironment())
    Diags.Report(diag::err_fe_invalid_exception_model)
        << static_cast<unsigned>(LangOpts.getExceptionHandling()) << T.str();

  if (Args.hasArg(OPT_ffp_eval_method_EQ)) {
    if (LangOpts.ApproxFunc)
      Diags.Report(diag::err_incompatible_fp_eval_method_options) << 0;
    if (LangOpts.AllowFPReassoc)
      Diags.Report(diag::err_incompatible_fp_eval_method_options) << 1;
    if (LangOpts.AllowRecip)
      Diags.Report(diag::err_incompatible_fp_eval_method_options) << 2;
  }

  if (Arg *A = Args.getLastArg(OPT_fdefault_calling_conv_EQ)) {
    auto DefaultCC = LangOpts.getDefaultCallingConv();

    bool emitError = (DefaultCC == LangOptions::DCC_FastCall ||
                      DefaultCC == LangOptions::DCC_StdCall);
    emitError |= (DefaultCC == LangOptions::DCC_VectorCall ||
                  DefaultCC == LangOptions::DCC_RegCall) &&
                 !T.isX86();
    if (emitError)
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << T.getTriple();
  }

  return Diags.getNumErrors() == NumErrorsBefore;
}

// ===----------------------------------------------------------------------===
// Dependency, filesystem & diagnostic options
// ===----------------------------------------------------------------------===

void generateDependencyOutputArgs(const DependencyOutputOptions &Opts,
                                  ArgumentConsumer Consumer) {
  const DependencyOutputOptions &DependencyOutputOpts = Opts;
#define DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(...)                         \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING

  if (Opts.ShowIncludesDest != ShowIncludesDestination::None)
    emitArg(Consumer, OPT_show_includes);

  for (const auto &Dep : Opts.ExtraDeps) {
    if (Dep.second == EDK_DepFileEntry)
      emitArg(Consumer, OPT_fdepfile_entry, Dep.first);
  }
}

bool parseDependencyOutputArgs(DependencyOutputOptions &Opts, ArgList &Args,
                               DiagnosticsEngine &Diags,
                               frontend::ActionKind Action,
                               bool ShowLineMarkers) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  DependencyOutputOptions &DependencyOutputOpts = Opts;
#define DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(...)                         \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING

  if (Args.hasArg(OPT_show_includes)) {
    if (Action == frontend::PrintPreprocessedInput || !ShowLineMarkers)
      Opts.ShowIncludesDest = ShowIncludesDestination::Stderr;
    else
      Opts.ShowIncludesDest = ShowIncludesDestination::Stdout;
  } else {
    Opts.ShowIncludesDest = ShowIncludesDestination::None;
  }

  for (const auto *A : Args.filtered(OPT_fdepfile_entry))
    Opts.ExtraDeps.emplace_back(A->getValue(), EDK_DepFileEntry);

  if ((Opts.HeaderIncludeFormat == HIFMT_Textual &&
       Opts.HeaderIncludeFiltering != HIFIL_None) ||
      (Opts.HeaderIncludeFormat == HIFMT_JSON &&
       Opts.HeaderIncludeFiltering != HIFIL_Only_Direct_System))
    Diags.Report(diag::err_drv_print_header_env_var_combination)
        << Args.getLastArg(OPT_header_include_format_EQ)->getValue()
        << Args.getLastArg(OPT_header_include_filtering_EQ)->getValue();

  return Diags.getNumErrors() == NumErrorsBefore;
}

bool resolveColorOption(const ArgList &Args, bool DefaultColor) {
  enum {
    Colors_On,
    Colors_Off,
    Colors_Auto
  } ShowColors = DefaultColor ? Colors_Auto : Colors_Off;
  for (auto *A : Args) {
    const Option &O = A->getOption();
    if (O.matches(options::OPT_fcolor_diagnostics)) {
      ShowColors = Colors_On;
    } else if (O.matches(options::OPT_fno_color_diagnostics)) {
      ShowColors = Colors_Off;
    } else if (O.matches(options::OPT_fdiagnostics_color_EQ)) {
      llvm::StringRef Value(A->getValue());
      if (Value == "always")
        ShowColors = Colors_On;
      else if (Value == "never")
        ShowColors = Colors_Off;
      else if (Value == "auto")
        ShowColors = Colors_Auto;
    }
  }
  return ShowColors == Colors_On ||
         (ShowColors == Colors_Auto &&
          llvm::sys::Process::StandardErrHasColors());
}

void generateFileSystemArgs(const FileSystemOptions &Opts,
                            ArgumentConsumer Consumer) {
  const FileSystemOptions &FileSystemOpts = Opts;

#define FILE_SYSTEM_OPTION_WITH_MARSHALLING(...)                               \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef FILE_SYSTEM_OPTION_WITH_MARSHALLING
}

bool parseFileSystemArgs(FileSystemOptions &Opts, const ArgList &Args,
                         DiagnosticsEngine &Diags) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  FileSystemOptions &FileSystemOpts = Opts;

#define FILE_SYSTEM_OPTION_WITH_MARSHALLING(...)                               \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef FILE_SYSTEM_OPTION_WITH_MARSHALLING

  return Diags.getNumErrors() == NumErrorsBefore;
}
} // namespace

void CompilerInvocationBase::GenerateDiagnosticArgs(
    const DiagnosticOptions &Opts, ArgumentConsumer Consumer,
    bool DefaultDiagColor) {
  const DiagnosticOptions *DiagnosticOpts = &Opts;
#define DIAG_OPTION_WITH_MARSHALLING(...)                                      \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef DIAG_OPTION_WITH_MARSHALLING

  if (Opts.ShowColors)
    emitArg(Consumer, OPT_fcolor_diagnostics);

  for (const auto &Warning : Opts.Warnings) {
    if (Warning == "undef-prefix")
      continue;
    Consumer(llvm::StringRef("-W") + Warning);
  }

  for (const auto &Remark : Opts.Remarks) {
    llvm::StringRef IgnoredRemarks[] = {"pass",          "no-pass",
                                        "pass-analysis", "no-pass-analysis",
                                        "pass-missed",   "no-pass-missed"};
    if (llvm::is_contained(IgnoredRemarks, Remark))
      continue;

    Consumer(llvm::StringRef("-R") + Remark);
  }
}

std::unique_ptr<DiagnosticOptions>
neverc::CreateAndPopulateDiagOpts(llvm::ArrayRef<const char *> Argv) {
  auto DiagOpts = std::make_unique<DiagnosticOptions>();
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args = getDriverOptTable().ParseArgs(
      Argv.slice(1), MissingArgIndex, MissingArgCount);

  bool ShowColors = true;
  if (auto NoColor = llvm::sys::Process::GetEnv("NO_COLOR");
      NoColor && !NoColor->empty()) {
    ShowColors = false;
  }

  (void)ParseDiagnosticArgs(*DiagOpts, Args, /*Diags=*/nullptr, ShowColors);
  return DiagOpts;
}

bool neverc::ParseDiagnosticArgs(DiagnosticOptions &Opts, ArgList &Args,
                                 DiagnosticsEngine *Diags,
                                 bool DefaultDiagColor) {
  std::optional<DiagnosticsEngine> IgnoringDiags;
  if (!Diags) {
    IgnoringDiags.emplace(new DiagnosticIDs(), new DiagnosticOptions(),
                          new IgnoringDiagConsumer());
    Diags = &*IgnoringDiags;
  }

  unsigned NumErrorsBefore = Diags->getNumErrors();

  DiagnosticOptions *DiagnosticOpts = &Opts;

#define DIAG_OPTION_WITH_MARSHALLING(...)                                      \
  PARSE_OPTION_WITH_MARSHALLING(Args, *Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef DIAG_OPTION_WITH_MARSHALLING

  llvm::sys::Process::UseANSIEscapeCodes(Opts.UseANSIEscapeCodes);

  Opts.ShowColors = resolveColorOption(Args, DefaultDiagColor);

  if (Opts.TabStop == 0 || Opts.TabStop > DiagnosticOptions::MaxTabStop) {
    Diags->Report(diag::warn_ignoring_ftabstop_value)
        << Opts.TabStop << DiagnosticOptions::DefaultTabStop;
    Opts.TabStop = DiagnosticOptions::DefaultTabStop;
  }

  addDiagnosticArgs(Args, OPT_W_Group, OPT_W_value_Group, Opts.Warnings);
  addDiagnosticArgs(Args, OPT_R_Group, OPT_R_value_Group, Opts.Remarks);

  return Diags->getNumErrors() == NumErrorsBefore;
}

// ===----------------------------------------------------------------------===
// Frontend options
// ===----------------------------------------------------------------------===

namespace {
const auto &getFrontendActionTable() {
  static const std::pair<frontend::ActionKind, unsigned> Table[] = {
      {frontend::GenAssembly, OPT_S},
      {frontend::GenBC, OPT_emit_llvm_bc},
      {frontend::GenLLVM, OPT_emit_llvm},
      {frontend::GenObj, OPT_emit_obj},
      {frontend::ParseSyntaxOnly, OPT_fsyntax_only},
      {frontend::PrintPreprocessedInput, OPT_E},
      {frontend::RunPreprocessorOnly, OPT_Eonly},
  };

  return Table;
}

std::optional<frontend::ActionKind> getFrontendAction(OptSpecifier &Opt) {
  for (const auto &ActionOpt : getFrontendActionTable())
    if (ActionOpt.second == Opt.getID())
      return ActionOpt.first;

  return std::nullopt;
}

std::optional<OptSpecifier>
getProgramActionOpt(frontend::ActionKind ProgramAction) {
  for (const auto &ActionOpt : getFrontendActionTable())
    if (ActionOpt.first == ProgramAction)
      return OptSpecifier(ActionOpt.second);

  return std::nullopt;
}

void generateFrontendArgs(const FrontendOptions &Opts,
                          ArgumentConsumer Consumer, bool IsHeader) {
  const FrontendOptions &FrontendOpts = Opts;
#define FRONTEND_OPTION_WITH_MARSHALLING(...)                                  \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef FRONTEND_OPTION_WITH_MARSHALLING

  std::optional<OptSpecifier> ProgramActionOpt =
      getProgramActionOpt(Opts.ProgramAction);
  assert(ProgramActionOpt && "Frontend action without option.");
  emitArg(Consumer, *ProgramActionOpt);

  {
    llvm::StringRef Preprocessed =
        Opts.DashX.isPreprocessed() ? "-cpp-output" : "";
    llvm::StringRef Header = IsHeader ? "-header" : "";

    llvm::StringRef Lang;
    switch (Opts.DashX.getLanguage()) {
    case Language::C:
      Lang = "c";
      break;
    case Language::Asm:
      Lang = "assembler-with-cpp";
      break;
    case Language::Unknown:
      Lang = "unknown";
      break;
    case Language::LLVM_IR:
      Lang = "ir";
      break;
    }

    emitArg(Consumer, OPT_x, Lang + Header + Preprocessed);
  }

  for (const auto &Input : Opts.Inputs)
    Consumer(Input.getFile());
}

bool parseFrontendArgs(FrontendOptions &Opts, ArgList &Args,
                       DiagnosticsEngine &Diags, bool &IsHeaderFile) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  FrontendOptions &FrontendOpts = Opts;

#define FRONTEND_OPTION_WITH_MARSHALLING(...)                                  \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef FRONTEND_OPTION_WITH_MARSHALLING

  Opts.ProgramAction = frontend::ParseSyntaxOnly;
  if (const Arg *A = Args.getLastArg(OPT_Action_Group)) {
    OptSpecifier Opt = OptSpecifier(A->getOption().getID());
    std::optional<frontend::ActionKind> ProgramAction = getFrontendAction(Opt);
    if (!ProgramAction) {
      Diags.Report(diag::err_drv_unsupported) << A->getAsString(Args);
    } else {
      Opts.ProgramAction = *ProgramAction;
    }
  }

  InputKind DashX(Language::Unknown);
  if (const Arg *A = Args.getLastArg(OPT_x)) {
    llvm::StringRef XValue = A->getValue();

    bool Preprocessed = XValue.consume_back("-cpp-output");
    bool IsHeader = XValue.consume_back("-header");

    IsHeaderFile = IsHeader && !Preprocessed;

    if (XValue != "c" && XValue != "c-header" && XValue != "cpp-output" &&
        XValue != "assembler-with-cpp" && XValue != "ir" && XValue != "ast") {
      Diags.Report(diag::err_drv_unsupported) << XValue;
      return false;
    }
    DashX = llvm::StringSwitch<InputKind>(XValue)
                .Case("c", Language::C)
                .Default(Language::Unknown);

    if (DashX.isUnknown() && !Preprocessed && !IsHeaderFile)
      DashX = llvm::StringSwitch<InputKind>(XValue)
                  .Case("cpp-output", InputKind(Language::C).getPreprocessed())
                  .Case("assembler-with-cpp", Language::Asm)
                  .Case("ir", Language::LLVM_IR)
                  .Default(Language::Unknown);

    if (DashX.isUnknown())
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();

    if (Preprocessed)
      DashX = DashX.getPreprocessed();
    if (IsHeaderFile)
      DashX = DashX.getHeader();
  }

  std::vector<std::string> Inputs = Args.getAllArgValues(OPT_INPUT);
  Opts.Inputs.clear();
  if (Inputs.empty())
    Inputs.push_back("-");

  for (unsigned i = 0, e = Inputs.size(); i != e; ++i) {
    InputKind IK = DashX;
    if (IK.isUnknown()) {
      IK = FrontendOptions::getInputKindForExtension(
          llvm::StringRef(Inputs[i]).rsplit('.').second);
      if (IK.isUnknown())
        IK = Language::C;
    }

    if (IK.getLanguage() != Language::C && IK.getLanguage() != Language::Asm &&
        IK.getLanguage() != Language::LLVM_IR &&
        IK.getLanguage() != Language::Unknown) {
      Diags.Report(diag::err_drv_unsupported) << Inputs[i];
      continue;
    }

    if (DashX.isUnknown() && i == 0)
      DashX = IK;

    Opts.Inputs.emplace_back(std::move(Inputs[i]), IK);
  }

  Opts.DashX = DashX;

  return Diags.getNumErrors() == NumErrorsBefore;
}

// ===----------------------------------------------------------------------===
// Preprocessor & target options
// ===----------------------------------------------------------------------===

bool isStrictlyPreprocessorAction(frontend::ActionKind Action) {
  switch (Action) {
  case frontend::GenAssembly:
  case frontend::GenBC:
  case frontend::GenLLVM:
  case frontend::GenObj:
  case frontend::ParseSyntaxOnly:
    return false;

  case frontend::PrintPreprocessedInput:
  case frontend::RunPreprocessorOnly:
    return true;
  }
  llvm_unreachable("invalid frontend action");
}

void generatePreprocessorArgs(const PrepOptions &Opts,
                              ArgumentConsumer Consumer,
                              const LangOptions &LangOpts,
                              const FrontendOptions &FrontendOpts,
                              const CodeGenOptions &CodeGenOpts) {
  const PrepOptions *PPO = &Opts;

#define PREPROCESSOR_OPTION_WITH_MARSHALLING(...)                              \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef PREPROCESSOR_OPTION_WITH_MARSHALLING

  for (const auto &M : Opts.Macros) {
    if (M.first == "__CET__=1" && !M.second &&
        !CodeGenOpts.CFProtectionReturn && CodeGenOpts.CFProtectionBranch)
      continue;
    if (M.first == "__CET__=2" && !M.second && CodeGenOpts.CFProtectionReturn &&
        !CodeGenOpts.CFProtectionBranch)
      continue;
    if (M.first == "__CET__=3" && !M.second && CodeGenOpts.CFProtectionReturn &&
        CodeGenOpts.CFProtectionBranch)
      continue;

    emitArg(Consumer, M.second ? OPT_U : OPT_D, M.first);
  }

  for (const auto &I : Opts.Includes)
    emitArg(Consumer, OPT_include, I);

  if (Opts.SourceDateEpoch)
    emitArg(Consumer, OPT_source_date_epoch,
            llvm::Twine(*Opts.SourceDateEpoch));

  if (Opts.DefineTargetOSMacros)
    emitArg(Consumer, OPT_fdefine_target_os_macros);
}

bool parsePreprocessorArgs(PrepOptions &Opts, ArgList &Args,
                           DiagnosticsEngine &Diags,
                           frontend::ActionKind Action,
                           const FrontendOptions &FrontendOpts) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  PrepOptions *PPO = &Opts;

#define PREPROCESSOR_OPTION_WITH_MARSHALLING(...)                              \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef PREPROCESSOR_OPTION_WITH_MARSHALLING

  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    llvm::StringRef Name = A->getValue();
    if (Name == "branch")
      Opts.addMacroDef("__CET__=1");
    else if (Name == "return")
      Opts.addMacroDef("__CET__=2");
    else if (Name == "full")
      Opts.addMacroDef("__CET__=3");
  }

  for (const auto *A : Args.filtered(OPT_D, OPT_U)) {
    if (A->getOption().matches(OPT_D))
      Opts.addMacroDef(A->getValue());
    else
      Opts.addMacroUndef(A->getValue());
  }

  for (const auto *A : Args.filtered(OPT_include))
    Opts.Includes.emplace_back(A->getValue());

  if (const Arg *A = Args.getLastArg(OPT_source_date_epoch)) {
    llvm::StringRef Epoch = A->getValue();
    const uint64_t MaxTimestamp =
        std::min<uint64_t>(std::numeric_limits<time_t>::max(), 253402300799);
    uint64_t V;
    if (Epoch.getAsInteger(10, V) || V > MaxTimestamp) {
      Diags.Report(diag::err_fe_invalid_source_date_epoch)
          << Epoch << MaxTimestamp;
    } else {
      Opts.SourceDateEpoch = V;
    }
  }

  Opts.DefineTargetOSMacros =
      Args.hasFlag(OPT_fdefine_target_os_macros,
                   OPT_fno_define_target_os_macros, Opts.DefineTargetOSMacros);

  return Diags.getNumErrors() == NumErrorsBefore;
}

void generatePrepOutputArgs(const PrepOutputOptions &Opts,
                            ArgumentConsumer Consumer,
                            frontend::ActionKind Action) {
  const PrepOutputOptions &PrepOutputOpts = Opts;

#define PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(...)                       \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING

  bool Generate_dM = isStrictlyPreprocessorAction(Action) && !Opts.ShowCPP;
  if (Generate_dM)
    emitArg(Consumer, OPT_dM);
  if (!Generate_dM && Opts.ShowMacros)
    emitArg(Consumer, OPT_dD);
  if (Opts.DirectivesOnly)
    emitArg(Consumer, OPT_fdirectives_only);
}

bool parsePrepOutputArgs(PrepOutputOptions &Opts, ArgList &Args,
                         DiagnosticsEngine &Diags,
                         frontend::ActionKind Action) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  PrepOutputOptions &PrepOutputOpts = Opts;

#define PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(...)                       \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING

  Opts.ShowCPP = isStrictlyPreprocessorAction(Action) && !Args.hasArg(OPT_dM);
  Opts.ShowMacros = Args.hasArg(OPT_dM) || Args.hasArg(OPT_dD);
  Opts.DirectivesOnly = Args.hasArg(OPT_fdirectives_only);

  return Diags.getNumErrors() == NumErrorsBefore;
}

void generateTargetArgs(const TargetOptions &Opts, ArgumentConsumer Consumer) {
  const TargetOptions *TargetOpts = &Opts;
#define TARGET_OPTION_WITH_MARSHALLING(...)                                    \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef TARGET_OPTION_WITH_MARSHALLING

  if (!Opts.SDKVersion.empty())
    emitArg(Consumer, OPT_target_sdk_version_EQ, Opts.SDKVersion.getAsString());
}

bool parseTargetArgs(TargetOptions &Opts, ArgList &Args,
                     DiagnosticsEngine &Diags) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  TargetOptions *TargetOpts = &Opts;

#define TARGET_OPTION_WITH_MARSHALLING(...)                                    \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef TARGET_OPTION_WITH_MARSHALLING

  if (Arg *A = Args.getLastArg(options::OPT_target_sdk_version_EQ)) {
    llvm::VersionTuple Version;
    if (Version.tryParse(A->getValue()))
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    else
      Opts.SDKVersion = Version;
  }

  return Diags.getNumErrors() == NumErrorsBefore;
}
} // namespace

// ===----------------------------------------------------------------------===
// CreateFromArgs & command-line generation
// ===----------------------------------------------------------------------===

bool CompilerInvocation::CreateFromArgsImpl(
    CompilerInvocation &Res, llvm::ArrayRef<const char *> CommandLineArgs,
    DiagnosticsEngine &Diags, const char *Argv0) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  const OptTable &Opts = getDriverOptTable();
  llvm::opt::Visibility VisibilityMask(options::NeverCOption);
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args = Opts.ParseArgs(CommandLineArgs, MissingArgIndex,
                                     MissingArgCount, VisibilityMask);
  LangOptions &LangOpts = Res.getLangOpts();

  if (MissingArgCount)
    Diags.Report(diag::err_drv_missing_argument)
        << Args.getArgString(MissingArgIndex) << MissingArgCount;

  for (const auto *A : Args.filtered(OPT_UNKNOWN)) {
    auto ArgString = A->getAsString(Args);
    std::string Nearest;
    if (Opts.findNearest(ArgString, Nearest, VisibilityMask) > 1)
      Diags.Report(diag::err_drv_unknown_argument) << ArgString;
    else
      Diags.Report(diag::err_drv_unknown_argument_with_suggestion)
          << ArgString << Nearest;
  }

  parseFileSystemArgs(Res.getFileSystemOpts(), Args, Diags);
  ParseDiagnosticArgs(Res.getDiagnosticOpts(), Args, &Diags,
                      /*DefaultDiagColor=*/false);
  parseFrontendArgs(Res.getFrontendOpts(), Args, Diags, LangOpts.IsHeaderFile);
  InputKind DashX = Res.getFrontendOpts().DashX;
  parseTargetArgs(Res.getTargetOpts(), Args, Diags);
  llvm::Triple T(Res.getTargetOpts().Triple);
  parseHeaderSearchArgs(Res.getHeaderIdxOpts(), Args, Diags,
                        Res.getFileSystemOpts().WorkingDir);

  ParseLangArgs(LangOpts, Args, DashX, T, Res.getPrepOpts().Includes, Diags);

  if (llvm::any_of(Res.getFrontendOpts().Inputs, [](const FrontendInputFile &F) {
        return llvm::sys::path::extension(F.getFile()) == ".nc";
      })) {
    LangOpts.NeverCTypes = 1;
    LangOpts.BuiltinString = 1;
  }

  ParseCodeGenArgs(Res.getCodeGenOpts(), Args, DashX, Diags, T,
                   Res.getFrontendOpts().OutputFile, LangOpts);

  parsePreprocessorArgs(Res.getPrepOpts(), Args, Diags,
                        Res.getFrontendOpts().ProgramAction,
                        Res.getFrontendOpts());
  parsePrepOutputArgs(Res.getPrepOutputOpts(), Args, Diags,
                      Res.getFrontendOpts().ProgramAction);

  parseDependencyOutputArgs(Res.getDependencyOutputOpts(), Args, Diags,
                            Res.getFrontendOpts().ProgramAction,
                            Res.getPrepOutputOpts().ShowLineMarkers);
  if (!Res.getDependencyOutputOpts().OutputFile.empty() &&
      Res.getDependencyOutputOpts().Targets.empty())
    Diags.Report(diag::err_fe_dependency_file_requires_MT);

  validateInvocation(Res, Diags, Args, DashX);

  return Diags.getNumErrors() == NumErrorsBefore;
}

bool CompilerInvocation::CreateFromArgs(
    CompilerInvocation &Invocation,
    llvm::ArrayRef<const char *> CommandLineArgs, DiagnosticsEngine &Diags,
    const char *Argv0) {
  return CreateFromArgsImpl(Invocation, CommandLineArgs, Diags, Argv0);
}

void CompilerInvocationBase::generateFrontendCommandLine(
    ArgumentConsumer Consumer) const {
  llvm::Triple T(getTargetOpts().Triple);

  generateFileSystemArgs(getFileSystemOpts(), Consumer);
  GenerateDiagnosticArgs(getDiagnosticOpts(), Consumer,
                         /*DefaultDiagColor=*/false);
  generateFrontendArgs(getFrontendOpts(), Consumer, getLangOpts().IsHeaderFile);
  generateTargetArgs(getTargetOpts(), Consumer);
  generateHeaderSearchArgs(getHeaderIdxOpts(), Consumer);
  GenerateLangArgs(getLangOpts(), Consumer, T, getFrontendOpts().DashX);
  GenerateCodeGenArgs(getCodeGenOpts(), Consumer, T,
                      getFrontendOpts().OutputFile, &getLangOpts());
  generatePreprocessorArgs(getPrepOpts(), Consumer, getLangOpts(),
                           getFrontendOpts(), getCodeGenOpts());
  generatePrepOutputArgs(getPrepOutputOpts(), Consumer,
                         getFrontendOpts().ProgramAction);
  generateDependencyOutputArgs(getDependencyOutputOpts(), Consumer);
}

std::vector<std::string>
CompilerInvocationBase::getFrontendCommandLine() const {
  std::vector<std::string> Args;
  generateFrontendCommandLine(
      [&Args](const llvm::Twine &Arg) { Args.push_back(Arg.str()); });
  return Args;
}

// ===----------------------------------------------------------------------===
// VFS creation
// ===----------------------------------------------------------------------===

llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
neverc::createVFSFromCompilerInvocation(const CompilerInvocation &CI,
                                        DiagnosticsEngine &Diags) {
  return createVFSFromCompilerInvocation(CI, Diags,
                                         llvm::vfs::getRealFileSystem());
}

llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
neverc::createVFSFromCompilerInvocation(
    const CompilerInvocation &CI, DiagnosticsEngine &Diags,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS) {
  return createVFSFromOverlayFiles(CI.getHeaderIdxOpts().VFSOverlayFiles, Diags,
                                   std::move(BaseFS));
}

llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
neverc::createVFSFromOverlayFiles(
    llvm::ArrayRef<std::string> VFSOverlayFiles, DiagnosticsEngine &Diags,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS) {
  if (VFSOverlayFiles.empty())
    return BaseFS;

  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> Result = BaseFS;
  for (const auto &File : VFSOverlayFiles) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Buffer =
        Result->getBufferForFile(File);
    if (!Buffer) {
      Diags.Report(diag::err_missing_vfs_overlay_file) << File;
      continue;
    }

    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS =
        llvm::vfs::getVFSFromYAML(std::move(Buffer.get()),
                                  /*DiagHandler*/ nullptr, File,
                                  /*DiagContext*/ nullptr, Result);
    if (!FS) {
      Diags.Report(diag::err_invalid_vfs_overlay) << File;
      continue;
    }

    Result = FS;
  }
  return Result;
}
