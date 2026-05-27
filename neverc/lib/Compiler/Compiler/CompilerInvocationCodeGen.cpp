#include "CompilerInvocationArg.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Remarks/HotnessThresholdParser.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Regex.h"
#include "llvm/Target/TargetOptions.h"

// ===----------------------------------------------------------------------===
// CodeGen options
// ===----------------------------------------------------------------------===

namespace {
CodeGenOptions::OptRemark parseOptimizationRemark(DiagnosticsEngine &Diags,
                                                  ArgList &Args,
                                                  OptSpecifier OptEQ,
                                                  llvm::StringRef Name) {
  CodeGenOptions::OptRemark Result;

  auto InitializeResultPattern =
      [&Diags, &Args, &Result](const Arg *A, llvm::StringRef Pattern) {
        Result.Pattern = Pattern.str();

        llvm::SmallString<256> RegexError;
        Result.Regex = std::make_shared<llvm::Regex>(Result.Pattern);
        if (!Result.Regex->isValid(RegexError)) {
          Diags.Report(diag::err_drv_optimization_remark_pattern)
              << RegexError << A->getAsString(Args);
          return false;
        }

        return true;
      };

  for (Arg *A : Args) {
    if (A->getOption().matches(OPT_R_Joined)) {
      llvm::StringRef Value = A->getValue();

      if (Value == Name)
        Result.Kind = CodeGenOptions::RK_Enabled;
      else if (Value == "everything")
        Result.Kind = CodeGenOptions::RK_EnabledEverything;
      else if (Value.split('-') == std::make_pair(llvm::StringRef("no"), Name))
        Result.Kind = CodeGenOptions::RK_Disabled;
      else if (Value == "no-everything")
        Result.Kind = CodeGenOptions::RK_DisabledEverything;
      else
        continue;

      if (Result.Kind == CodeGenOptions::RK_Disabled ||
          Result.Kind == CodeGenOptions::RK_DisabledEverything) {
        Result.Pattern = "";
        Result.Regex = nullptr;
      } else {
        InitializeResultPattern(A, ".*");
      }
    } else if (A->getOption().matches(OptEQ)) {
      Result.Kind = CodeGenOptions::RK_WithPattern;
      if (!InitializeResultPattern(A, A->getValue()))
        return CodeGenOptions::OptRemark();
    }
  }

  return Result;
}
} // namespace

void CompilerInvocationBase::GenerateCodeGenArgs(const CodeGenOptions &Opts,
                                                 ArgumentConsumer Consumer,
                                                 const llvm::Triple &T,
                                                 const std::string &OutputFile,
                                                 const LangOptions *LangOpts) {
  const CodeGenOptions &CodeGenOpts = Opts;

  if (Opts.OptimizationLevel == 0)
    emitArg(Consumer, OPT_O0);
  else
    emitArg(Consumer, OPT_O, llvm::Twine(Opts.OptimizationLevel));

#define CODEGEN_OPTION_WITH_MARSHALLING(...)                                   \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef CODEGEN_OPTION_WITH_MARSHALLING

  if (Opts.OptimizationLevel > 0) {
    if (Opts.Inlining == CodeGenOptions::NormalInlining)
      emitArg(Consumer, OPT_finline_functions);
    else if (Opts.Inlining == CodeGenOptions::OnlyHintInlining)
      emitArg(Consumer, OPT_finline_hint_functions);
    else if (Opts.Inlining == CodeGenOptions::OnlyAlwaysInlining)
      emitArg(Consumer, OPT_fno_inline);
  }

  if (Opts.DirectAccessExternalData)
    emitArg(Consumer, OPT_fdirect_access_external_data);

  std::optional<llvm::StringRef> DebugInfoVal;
  switch (Opts.DebugInfo) {
  case llvm::codegenoptions::DebugLineTablesOnly:
    DebugInfoVal = "line-tables-only";
    break;
  case llvm::codegenoptions::DebugDirectivesOnly:
    DebugInfoVal = "line-directives-only";
    break;
  case llvm::codegenoptions::DebugInfoConstructor:
    DebugInfoVal = "constructor";
    break;
  case llvm::codegenoptions::LimitedDebugInfo:
    DebugInfoVal = "limited";
    break;
  case llvm::codegenoptions::FullDebugInfo:
    DebugInfoVal = "standalone";
    break;
  case llvm::codegenoptions::UnusedTypeInfo:
    DebugInfoVal = "unused-types";
    break;
  case llvm::codegenoptions::NoDebugInfo: // default value
    DebugInfoVal = std::nullopt;
    break;
  case llvm::codegenoptions::LocTrackingOnly: // implied value
    DebugInfoVal = std::nullopt;
    break;
  }
  if (DebugInfoVal)
    emitArg(Consumer, OPT_debug_info_kind_EQ, *DebugInfoVal);

  for (const auto &Prefix : Opts.DebugPrefixMap)
    emitArg(Consumer, OPT_fdebug_prefix_map_EQ,
            Prefix.first + "=" + Prefix.second);

  if (Opts.NewStructPathTBAA)
    emitArg(Consumer, OPT_new_struct_path_tbaa);

  if (Opts.OptimizeSize == 1)
    emitArg(Consumer, OPT_O, "s");
  else if (Opts.OptimizeSize == 2)
    emitArg(Consumer, OPT_O, "z");

  // SimplifyLibCalls is set only in the absence of -fno-builtin and
  // -ffreestanding. We'll consider that when generating them.

  // NoBuiltinFuncs are generated by LangOptions.

  if (Opts.UnrollLoops && Opts.OptimizationLevel <= 1)
    emitArg(Consumer, OPT_funroll_loops);
  else if (!Opts.UnrollLoops && Opts.OptimizationLevel > 1)
    emitArg(Consumer, OPT_fno_unroll_loops);

  if (!Opts.BinutilsVersion.empty())
    emitArg(Consumer, OPT_fbinutils_version_EQ, Opts.BinutilsVersion);

  if (Opts.DebugNameTable ==
      static_cast<unsigned>(llvm::DICompileUnit::DebugNameTableKind::GNU))
    emitArg(Consumer, OPT_ggnu_pubnames);
  else if (Opts.DebugNameTable ==
           static_cast<unsigned>(
               llvm::DICompileUnit::DebugNameTableKind::Default))
    emitArg(Consumer, OPT_gpubnames);

  if (Opts.TimePasses) {
    if (Opts.TimePassesPerRun)
      emitArg(Consumer, OPT_ftime_report_EQ, "per-pass-run");
    else
      emitArg(Consumer, OPT_ftime_report);
  }

  if (Opts.PrepareForLTO)
    emitArg(Consumer, OPT_flto_EQ, "full");

  if (Opts.SaveTempsFilePrefix == OutputFile)
    emitArg(Consumer, OPT_save_temps_EQ, "obj");

  if (Opts.CFProtectionReturn && Opts.CFProtectionBranch)
    emitArg(Consumer, OPT_fcf_protection_EQ, "full");
  else if (Opts.CFProtectionReturn)
    emitArg(Consumer, OPT_fcf_protection_EQ, "return");
  else if (Opts.CFProtectionBranch)
    emitArg(Consumer, OPT_fcf_protection_EQ, "branch");

  if (Opts.FunctionReturnThunks)
    emitArg(Consumer, OPT_mfunction_return_EQ, "thunk-extern");

  for (const auto &F : Opts.LinkBitcodeFiles) {
    bool Builtint = F.LinkFlags == llvm::Linker::Flags::LinkOnlyNeeded &&
                    F.PropagateAttrs && F.Internalize;
    emitArg(Consumer,
            Builtint ? OPT_mlink_builtin_bitcode : OPT_mlink_bitcode_file,
            F.Filename);
  }

  if (Opts.EmulatedTLS)
    emitArg(Consumer, OPT_femulated_tls);

  if (Opts.FPDenormalMode != llvm::DenormalMode::getIEEE())
    emitArg(Consumer, OPT_fdenormal_fp_math_EQ, Opts.FPDenormalMode.str());

  if ((Opts.FPDenormalMode != Opts.FP32DenormalMode) ||
      (Opts.FP32DenormalMode != llvm::DenormalMode::getIEEE()))
    emitArg(Consumer, OPT_fdenormal_fp_math_f32_EQ,
            Opts.FP32DenormalMode.str());

  if (!Opts.OptRecordPasses.empty())
    emitArg(Consumer, OPT_opt_record_passes, Opts.OptRecordPasses);

  if (!Opts.OptRecordFormat.empty())
    emitArg(Consumer, OPT_opt_record_format, Opts.OptRecordFormat);

  generateOptimizationRemark(Consumer, OPT_Rpass_EQ, "pass",
                             Opts.OptimizationRemark);

  generateOptimizationRemark(Consumer, OPT_Rpass_missed_EQ, "pass-missed",
                             Opts.OptimizationRemarkMissed);

  generateOptimizationRemark(Consumer, OPT_Rpass_analysis_EQ, "pass-analysis",
                             Opts.OptimizationRemarkAnalysis);

  emitArg(Consumer, OPT_fdiagnostics_hotness_threshold_EQ,
          Opts.DiagnosticsHotnessThreshold
              ? llvm::Twine(*Opts.DiagnosticsHotnessThreshold)
              : "auto");

  if (!Opts.EmitVersionIdentMetadata)
    emitArg(Consumer, OPT_Qn);

  switch (Opts.FiniteLoops) {
  case CodeGenOptions::FiniteLoopsKind::Language:
    break;
  case CodeGenOptions::FiniteLoopsKind::Always:
    emitArg(Consumer, OPT_ffinite_loops);
    break;
  case CodeGenOptions::FiniteLoopsKind::Never:
    emitArg(Consumer, OPT_fno_finite_loops);
    break;
  }
}

bool CompilerInvocation::ParseCodeGenArgs(CodeGenOptions &Opts, ArgList &Args,
                                          InputKind IK,
                                          DiagnosticsEngine &Diags,
                                          const llvm::Triple &T,
                                          const std::string &OutputFile,
                                          const LangOptions &LangOptsRef) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  unsigned OptimizationLevel = getOptimizationLevel(Args, IK, Diags);
  unsigned MaxOptLevel = 3;
  if (OptimizationLevel > MaxOptLevel) {
    Diags.Report(diag::warn_drv_optimization_value)
        << Args.getLastArg(OPT_O)->getAsString(Args) << "-O" << MaxOptLevel;
    OptimizationLevel = MaxOptLevel;
  }
  Opts.OptimizationLevel = OptimizationLevel;

  CodeGenOptions &CodeGenOpts = Opts;
  const LangOptions *LangOpts = &LangOptsRef;

#define CODEGEN_OPTION_WITH_MARSHALLING(...)                                   \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef CODEGEN_OPTION_WITH_MARSHALLING

  if (Opts.OptimizationLevel == 0) {
    Opts.setInlining(CodeGenOptions::OnlyAlwaysInlining);
  } else if (const Arg *A = Args.getLastArg(options::OPT_finline_functions,
                                            options::OPT_finline_hint_functions,
                                            options::OPT_fno_inline_functions,
                                            options::OPT_fno_inline)) {
    if (A->getOption().matches(options::OPT_finline_functions))
      Opts.setInlining(CodeGenOptions::NormalInlining);
    else if (A->getOption().matches(options::OPT_finline_hint_functions))
      Opts.setInlining(CodeGenOptions::OnlyHintInlining);
    else
      Opts.setInlining(CodeGenOptions::OnlyAlwaysInlining);
  } else {
    Opts.setInlining(CodeGenOptions::NormalInlining);
  }

  Opts.DirectAccessExternalData =
      Args.hasArg(OPT_fdirect_access_external_data) &&
      !Args.hasArg(OPT_fno_direct_access_external_data);

  if (Arg *A = Args.getLastArg(OPT_debug_info_kind_EQ)) {
    unsigned Val =
        llvm::StringSwitch<unsigned>(A->getValue())
            .Case("line-tables-only", llvm::codegenoptions::DebugLineTablesOnly)
            .Case("line-directives-only",
                  llvm::codegenoptions::DebugDirectivesOnly)
            .Case("constructor", llvm::codegenoptions::DebugInfoConstructor)
            .Case("limited", llvm::codegenoptions::LimitedDebugInfo)
            .Case("standalone", llvm::codegenoptions::FullDebugInfo)
            .Case("unused-types", llvm::codegenoptions::UnusedTypeInfo)
            .Default(~0U);
    if (Val == ~0U)
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    else
      Opts.setDebugInfo(static_cast<llvm::codegenoptions::DebugInfoKind>(Val));
  }

  for (const auto &Arg : Args.getAllArgValues(OPT_fdebug_prefix_map_EQ)) {
    auto Split = llvm::StringRef(Arg).split('=');
    Opts.DebugPrefixMap.emplace_back(Split.first, Split.second);
  }

  const llvm::Triple::ArchType DebugEntryValueArchs[] = {llvm::Triple::x86_64,
                                                         llvm::Triple::aarch64};

  if (Opts.OptimizationLevel > 0 && Opts.hasReducedDebugInfo() &&
      llvm::is_contained(DebugEntryValueArchs, T.getArch()))
    Opts.EmitCallSiteInfo = true;

  if (!Opts.EnableDIPreservationVerify && Opts.DIBugsReportFilePath.size()) {
    Diags.Report(diag::warn_ignoring_verify_debuginfo_preserve_export)
        << Opts.DIBugsReportFilePath;
    Opts.DIBugsReportFilePath = "";
  }

  Opts.NewStructPathTBAA = !Args.hasArg(OPT_no_struct_path_tbaa) &&
                           Args.hasArg(OPT_new_struct_path_tbaa);
  Opts.OptimizeSize = getOptimizationLevelSize(Args);
  Opts.SimplifyLibCalls = !LangOpts->NoBuiltin;
  if (Opts.SimplifyLibCalls)
    Opts.NoBuiltinFuncs = LangOpts->NoBuiltinFuncs;
  Opts.UnrollLoops = Args.hasFlag(OPT_funroll_loops, OPT_fno_unroll_loops,
                                  (Opts.OptimizationLevel > 1));
  Opts.BinutilsVersion =
      std::string(Args.getLastArgValue(OPT_fbinutils_version_EQ));

  Opts.DebugNameTable = static_cast<unsigned>(
      Args.hasArg(OPT_ggnu_pubnames)
          ? llvm::DICompileUnit::DebugNameTableKind::GNU
      : Args.hasArg(OPT_gpubnames)
          ? llvm::DICompileUnit::DebugNameTableKind::Default
          : llvm::DICompileUnit::DebugNameTableKind::None);

  if (const Arg *A = Args.getLastArg(OPT_ftime_report, OPT_ftime_report_EQ)) {
    Opts.TimePasses = true;

    if (A->getOption().getID() == OPT_ftime_report_EQ) {
      llvm::StringRef Val = A->getValue();
      if (Val == "per-pass")
        Opts.TimePassesPerRun = false;
      else if (Val == "per-pass-run")
        Opts.TimePassesPerRun = true;
      else
        Diags.Report(diag::err_drv_invalid_value)
            << A->getAsString(Args) << A->getValue();
    }
  }

  Opts.PrepareForLTO = false;
  Opts.InMemoryLTOOutput = false;
  if (Arg *A = Args.getLastArg(OPT_flto_EQ)) {
    Opts.PrepareForLTO = true;
    llvm::StringRef S = A->getValue();
    if (S != "full")
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << S;
  }
  if (Arg *A = Args.getLastArg(OPT_save_temps_EQ))
    Opts.SaveTempsFilePrefix =
        llvm::StringSwitch<std::string>(A->getValue())
            .Case("obj", OutputFile)
            .Default(llvm::sys::path::filename(OutputFile).str());

  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    llvm::StringRef Name = A->getValue();
    if (Name == "full") {
      Opts.CFProtectionReturn = 1;
      Opts.CFProtectionBranch = 1;
    } else if (Name == "return")
      Opts.CFProtectionReturn = 1;
    else if (Name == "branch")
      Opts.CFProtectionBranch = 1;
    else if (Name != "none")
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Name;
  }

  if (const Arg *A = Args.getLastArg(OPT_mfunction_return_EQ)) {
    auto Val = llvm::StringSwitch<llvm::FunctionReturnThunksKind>(A->getValue())
                   .Case("keep", llvm::FunctionReturnThunksKind::Keep)
                   .Case("thunk-extern", llvm::FunctionReturnThunksKind::Extern)
                   .Default(llvm::FunctionReturnThunksKind::Invalid);
    if (!T.isX86())
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << T.getTriple();
    else if (Val == llvm::FunctionReturnThunksKind::Invalid)
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    else if (Val == llvm::FunctionReturnThunksKind::Extern &&
             Args.getLastArgValue(OPT_mcmodel_EQ).equals("large"))
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getAsString(Args)
          << Args.getLastArg(OPT_mcmodel_EQ)->getAsString(Args);
    else
      Opts.FunctionReturnThunks = static_cast<unsigned>(Val);
  }

  for (auto *A :
       Args.filtered(OPT_mlink_bitcode_file, OPT_mlink_builtin_bitcode)) {
    CodeGenOptions::BitcodeFileToLink F;
    F.Filename = A->getValue();
    if (A->getOption().matches(OPT_mlink_builtin_bitcode)) {
      F.LinkFlags = llvm::Linker::Flags::LinkOnlyNeeded;
      F.PropagateAttrs = true;
      F.Internalize = true;
    }
    Opts.LinkBitcodeFiles.push_back(F);
  }

  if (Arg *A = Args.getLastArg(OPT_fdenormal_fp_math_EQ)) {
    llvm::StringRef Val = A->getValue();
    Opts.FPDenormalMode = llvm::parseDenormalFPAttribute(Val);
    Opts.FP32DenormalMode = Opts.FPDenormalMode;
    if (!Opts.FPDenormalMode.isValid())
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  if (Arg *A = Args.getLastArg(OPT_fdenormal_fp_math_f32_EQ)) {
    llvm::StringRef Val = A->getValue();
    Opts.FP32DenormalMode = llvm::parseDenormalFPAttribute(Val);
    if (!Opts.FP32DenormalMode.isValid())
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  bool NeedLocTracking = false;

  if (!Opts.OptRecordFile.empty())
    NeedLocTracking = true;

  if (Arg *A = Args.getLastArg(OPT_opt_record_passes)) {
    Opts.OptRecordPasses = A->getValue();
    NeedLocTracking = true;
  }

  if (Arg *A = Args.getLastArg(OPT_opt_record_format)) {
    Opts.OptRecordFormat = A->getValue();
    NeedLocTracking = true;
  }

  Opts.OptimizationRemark =
      parseOptimizationRemark(Diags, Args, OPT_Rpass_EQ, "pass");

  Opts.OptimizationRemarkMissed =
      parseOptimizationRemark(Diags, Args, OPT_Rpass_missed_EQ, "pass-missed");

  Opts.OptimizationRemarkAnalysis = parseOptimizationRemark(
      Diags, Args, OPT_Rpass_analysis_EQ, "pass-analysis");

  NeedLocTracking |= Opts.OptimizationRemark.hasValidPattern() ||
                     Opts.OptimizationRemarkMissed.hasValidPattern() ||
                     Opts.OptimizationRemarkAnalysis.hasValidPattern();

  if (auto *arg =
          Args.getLastArg(options::OPT_fdiagnostics_hotness_threshold_EQ)) {
    auto ResultOrErr =
        llvm::remarks::parseHotnessThresholdOption(arg->getValue());

    if (!ResultOrErr)
      Diags.Report(diag::err_drv_invalid_diagnotics_hotness_threshold)
          << "-fdiagnostics-hotness-threshold=";
    else
      Opts.DiagnosticsHotnessThreshold = *ResultOrErr;
  }

  if (!Opts.StackUsageOutput.empty())
    NeedLocTracking = true;

  if (NeedLocTracking &&
      Opts.getDebugInfo() == llvm::codegenoptions::NoDebugInfo)
    Opts.setDebugInfo(llvm::codegenoptions::LocTrackingOnly);

  Opts.EmitVersionIdentMetadata = Args.hasFlag(OPT_Qy, OPT_Qn, true);

  if (Args.hasArg(options::OPT_ffinite_loops))
    Opts.FiniteLoops = CodeGenOptions::FiniteLoopsKind::Always;
  else if (Args.hasArg(options::OPT_fno_finite_loops))
    Opts.FiniteLoops = CodeGenOptions::FiniteLoopsKind::Never;

  if (Args.hasArg(OPT_fno_parallel_codegen)) {
    Opts.ParallelCodeGen = 1;
  } else if (const Arg *A = Args.getLastArg(OPT_fparallel_codegen_EQ)) {
    unsigned Val;
    if (llvm::StringRef(A->getValue()).getAsInteger(10, Val))
      Diags.Report(diag::err_drv_invalid_int_value)
          << A->getAsString(Args) << A->getValue();
    else
      Opts.ParallelCodeGen = Val;
  }

  return Diags.getNumErrors() == NumErrorsBefore;
}
