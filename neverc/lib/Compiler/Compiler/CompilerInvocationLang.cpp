#include "CompilerInvocationArg.h"
#include "neverc/Config/config.h"
#include "neverc/Foundation/LangOpts/LangStandard.h"
#include "neverc/Invoke/Driver.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VersionTuple.h"
#include <fstream>

// ===----------------------------------------------------------------------===
// Header search options
// ===----------------------------------------------------------------------===

std::string CompilerInvocation::GetResourcesPath(const char *Argv0,
                                                 void *MainAddr) {
  std::string Executable = llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
  return Driver::GetResourcesPath(Executable, NEVERC_RESOURCE_DIR);
}

void generateHeaderSearchArgs(const HeaderIndexOptions &Opts,
                              ArgumentConsumer Consumer) {
  const HeaderIndexOptions *HeaderIdxOpts = &Opts;
#define HEADER_SEARCH_OPTION_WITH_MARSHALLING(...)                             \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef HEADER_SEARCH_OPTION_WITH_MARSHALLING

  auto Matches = [](const HeaderIndexOptions::Entry &Entry,
                    llvm::ArrayRef<frontend::IncludeDirGroup> Groups,
                    std::optional<bool> IsFramework,
                    std::optional<bool> IgnoreSysRoot) {
    return llvm::is_contained(Groups, Entry.Group) &&
           (!IsFramework || (Entry.IsFramework == *IsFramework)) &&
           (!IgnoreSysRoot || (Entry.IgnoreSysRoot == *IgnoreSysRoot));
  };

  auto It = Opts.UserEntries.begin();
  auto End = Opts.UserEntries.end();

  for (; It < End && Matches(*It, {frontend::IndexHeaderMap, frontend::Angled},
                             std::nullopt, true);
       ++It) {
    OptSpecifier Opt = [It, Matches]() {
      if (Matches(*It, frontend::IndexHeaderMap, true, true))
        return OPT_F;
      if (Matches(*It, frontend::IndexHeaderMap, false, true))
        return OPT_I;
      if (Matches(*It, frontend::Angled, true, true))
        return OPT_F;
      if (Matches(*It, frontend::Angled, false, true))
        return OPT_I;
      llvm_unreachable("Unexpected HeaderIndexOptions::Entry.");
    }();

    if (It->Group == frontend::IndexHeaderMap)
      emitArg(Consumer, OPT_index_header_map);
    emitArg(Consumer, Opt, It->Path);
  };

  for (; It < End &&
         Matches(*It, {frontend::After, frontend::Angled}, false, true);
       ++It) {
    OptSpecifier Opt =
        It->Group == frontend::After ? OPT_iwithprefix : OPT_iwithprefixbefore;
    emitArg(Consumer, Opt, It->Path);
  }

  for (; It < End && Matches(*It, {frontend::After}, false, true); ++It)
    emitArg(Consumer, OPT_idirafter, It->Path);
  for (; It < End && Matches(*It, {frontend::Quoted}, false, true); ++It)
    emitArg(Consumer, OPT_iquote, It->Path);
  for (; It < End && Matches(*It, {frontend::System}, false, std::nullopt);
       ++It)
    emitArg(Consumer, It->IgnoreSysRoot ? OPT_isystem : OPT_iwithsysroot,
            It->Path);
  for (; It < End && Matches(*It, {frontend::System}, true, true); ++It)
    emitArg(Consumer, OPT_iframework, It->Path);
  for (; It < End && Matches(*It, {frontend::System}, true, false); ++It)
    emitArg(Consumer, OPT_iframeworkwithsysroot, It->Path);

  for (; It < End && Matches(*It, {frontend::CSystem}, false, true); ++It)
    emitArg(Consumer, OPT_c_isystem, It->Path);

  for (; It < End &&
         Matches(*It, {frontend::System, frontend::ExternCSystem}, false, true);
       ++It) {
    OptSpecifier Opt = It->Group == frontend::System
                           ? OPT_internal_isystem
                           : OPT_internal_externc_isystem;
    emitArg(Consumer, Opt, It->Path);
  }

  assert(It == End && "Unhandled HeaderSearchOption::Entry.");

  for (const auto &P : Opts.SystemHeaderPrefixes) {
    OptSpecifier Opt = P.IsSystemHeader ? OPT_system_header_prefix
                                        : OPT_no_system_header_prefix;
    emitArg(Consumer, Opt, P.Prefix);
  }

  for (const std::string &F : Opts.VFSOverlayFiles)
    emitArg(Consumer, OPT_ivfsoverlay, F);
}

bool parseHeaderSearchArgs(HeaderIndexOptions &Opts, ArgList &Args,
                           DiagnosticsEngine &Diags,
                           const std::string &WorkingDir) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  HeaderIndexOptions *HeaderIdxOpts = &Opts;

#define HEADER_SEARCH_OPTION_WITH_MARSHALLING(...)                             \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef HEADER_SEARCH_OPTION_WITH_MARSHALLING

  bool IsIndexHeaderMap = false;
  bool IsSysrootSpecified =
      Args.hasArg(OPT__sysroot_EQ) || Args.hasArg(OPT_isysroot);
  for (const auto *A : Args.filtered(OPT_I, OPT_F, OPT_index_header_map)) {
    if (A->getOption().matches(OPT_index_header_map)) {
      IsIndexHeaderMap = true;
      continue;
    }

    frontend::IncludeDirGroup Group =
        IsIndexHeaderMap ? frontend::IndexHeaderMap : frontend::Angled;

    bool IsFramework = A->getOption().matches(OPT_F);
    std::string Path = A->getValue();

    if (IsSysrootSpecified && !IsFramework && A->getValue()[0] == '=') {
      llvm::SmallString<32> Buffer;
      llvm::sys::path::append(Buffer, Opts.Sysroot,
                              llvm::StringRef(A->getValue()).substr(1));
      Path = std::string(Buffer.str());
    }

    Opts.AddPath(Path, Group, IsFramework,
                 /*IgnoreSysroot*/ true);
    IsIndexHeaderMap = false;
  }

  llvm::StringRef Prefix = "";
  for (const auto *A :
       Args.filtered(OPT_iprefix, OPT_iwithprefix, OPT_iwithprefixbefore)) {
    if (A->getOption().matches(OPT_iprefix))
      Prefix = A->getValue();
    else if (A->getOption().matches(OPT_iwithprefix))
      Opts.AddPath(Prefix.str() + A->getValue(), frontend::After, false, true);
    else
      Opts.AddPath(Prefix.str() + A->getValue(), frontend::Angled, false, true);
  }

  for (const auto *A : Args.filtered(OPT_idirafter))
    Opts.AddPath(A->getValue(), frontend::After, false, true);
  for (const auto *A : Args.filtered(OPT_iquote))
    Opts.AddPath(A->getValue(), frontend::Quoted, false, true);
  for (const auto *A : Args.filtered(OPT_isystem, OPT_iwithsysroot))
    Opts.AddPath(A->getValue(), frontend::System, false,
                 !A->getOption().matches(OPT_iwithsysroot));
  for (const auto *A : Args.filtered(OPT_iframework))
    Opts.AddPath(A->getValue(), frontend::System, true, true);
  for (const auto *A : Args.filtered(OPT_iframeworkwithsysroot))
    Opts.AddPath(A->getValue(), frontend::System, /*IsFramework=*/true,
                 /*IgnoreSysRoot=*/false);

  for (const auto *A : Args.filtered(OPT_c_isystem))
    Opts.AddPath(A->getValue(), frontend::CSystem, false, true);

  for (const auto *A :
       Args.filtered(OPT_internal_isystem, OPT_internal_externc_isystem)) {
    frontend::IncludeDirGroup Group = frontend::System;
    if (A->getOption().matches(OPT_internal_externc_isystem))
      Group = frontend::ExternCSystem;
    Opts.AddPath(A->getValue(), Group, false, true);
  }

  for (const auto *A :
       Args.filtered(OPT_system_header_prefix, OPT_no_system_header_prefix))
    Opts.AddSystemHeaderPrefix(
        A->getValue(), A->getOption().matches(OPT_system_header_prefix));

  for (const auto *A : Args.filtered(OPT_ivfsoverlay, OPT_vfsoverlay))
    Opts.AddVFSOverlayFile(A->getValue());

  return Diags.getNumErrors() == NumErrorsBefore;
}

// ===----------------------------------------------------------------------===
// Language options
// ===----------------------------------------------------------------------===

namespace {
bool isInputCompatibleWithStandard(InputKind IK, const LangStandard &S) {
  switch (IK.getLanguage()) {
  case Language::Unknown:
  case Language::LLVM_IR:
    llvm_unreachable("should not parse language flags for this input");

  case Language::C:
    return S.getLanguage() == Language::C;

  case Language::Asm:
    return true;
  }
}

llvm::StringRef getInputKindName(InputKind IK) {
  switch (IK.getLanguage()) {
  case Language::C:
    return "C";
  case Language::Asm:
    return "Asm";
  case Language::LLVM_IR:
    return "LLVM IR";
  case Language::Unknown:
    break;
  }
  llvm_unreachable("unknown input language");
}
} // namespace

void CompilerInvocationBase::GenerateLangArgs(const LangOptions &Opts,
                                              ArgumentConsumer Consumer,
                                              const llvm::Triple &T,
                                              InputKind IK) {
  if (IK.getLanguage() == Language::LLVM_IR)
    return;

  auto LangStandard = LangStandard::getLangStandardForKind(Opts.LangStd);
  emitArg(Consumer, OPT_std_EQ, LangStandard.getName());

  const LangOptions *LangOpts = &Opts;

#define LANG_OPTION_WITH_MARSHALLING(...)                                      \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef LANG_OPTION_WITH_MARSHALLING

  if (Opts.GNUCVersion != 0) {
    unsigned Major = Opts.GNUCVersion / 100 / 100;
    unsigned Minor = (Opts.GNUCVersion / 100) % 100;
    unsigned Patch = Opts.GNUCVersion % 100;
    emitArg(Consumer, OPT_fgnuc_version_EQ,
            llvm::Twine(Major) + "." + llvm::Twine(Minor) + "." +
                llvm::Twine(Patch));
  }

  if (Opts.SignedOverflowBehavior == LangOptions::SOB_Trapping) {
    emitArg(Consumer, OPT_ftrapv);
    emitArg(Consumer, OPT_ftrapv_handler, Opts.OverflowHandler);
  } else if (Opts.SignedOverflowBehavior == LangOptions::SOB_Defined) {
    emitArg(Consumer, OPT_fwrapv);
  }

  if (Opts.MSCompatibilityVersion != 0) {
    unsigned Major = Opts.MSCompatibilityVersion / 10000000;
    unsigned Minor = (Opts.MSCompatibilityVersion / 100000) % 100;
    unsigned Subminor = Opts.MSCompatibilityVersion % 100000;
    emitArg(Consumer, OPT_fms_compatibility_version,
            llvm::Twine(Major) + "." + llvm::Twine(Minor) + "." +
                llvm::Twine(Subminor));
  }

  if (!Opts.GNUMode && !Opts.MSVCCompat) {
    if (!Opts.Trigraphs)
      emitArg(Consumer, OPT_fno_trigraphs);
  } else {
    if (Opts.Trigraphs)
      emitArg(Consumer, OPT_ftrigraphs);
  }

  if (Opts.NoBuiltin && !Opts.Freestanding)
    emitArg(Consumer, OPT_fno_builtin);

  if (!Opts.NoBuiltin)
    for (const auto &Func : Opts.NoBuiltinFuncs)
      emitArg(Consumer, OPT_fno_builtin_, Func);

  if (Opts.LongDoubleSize == 128)
    emitArg(Consumer, OPT_mlong_double_128);
  else if (Opts.LongDoubleSize == 64)
    emitArg(Consumer, OPT_mlong_double_64);
  else if (Opts.LongDoubleSize == 80)
    emitArg(Consumer, OPT_mlong_double_80);

  if (Opts.DefaultFPContractMode == LangOptions::FPM_Fast)
    emitArg(Consumer, OPT_ffp_contract, "fast");
  else if (Opts.DefaultFPContractMode == LangOptions::FPM_On)
    emitArg(Consumer, OPT_ffp_contract, "on");
  else if (Opts.DefaultFPContractMode == LangOptions::FPM_Off)
    emitArg(Consumer, OPT_ffp_contract, "off");
  else if (Opts.DefaultFPContractMode == LangOptions::FPM_FastHonorPragmas)
    emitArg(Consumer, OPT_ffp_contract, "fast-honor-pragmas");

  if (Opts.getSignReturnAddressScope() ==
      LangOptions::SignReturnAddressScopeKind::All)
    emitArg(Consumer, OPT_msign_return_address_EQ, "all");
  else if (Opts.getSignReturnAddressScope() ==
           LangOptions::SignReturnAddressScopeKind::NonLeaf)
    emitArg(Consumer, OPT_msign_return_address_EQ, "non-leaf");

  if (Opts.getSignReturnAddressKey() ==
      LangOptions::SignReturnAddressKeyKind::BKey)
    emitArg(Consumer, OPT_msign_return_address_key_EQ, "b_key");

  if (Opts.UseTargetPathSeparator)
    emitArg(Consumer, OPT_ffile_reproducible);
  else
    emitArg(Consumer, OPT_fno_file_reproducible);

  for (const auto &MP : Opts.MacroPrefixMap)
    emitArg(Consumer, OPT_fmacro_prefix_map_EQ, MP.first + "=" + MP.second);

  if (!Opts.RandstructSeed.empty())
    emitArg(Consumer, OPT_frandomize_layout_seed_EQ, Opts.RandstructSeed);
}

bool CompilerInvocation::ParseLangArgs(LangOptions &Opts, ArgList &Args,
                                       InputKind IK, const llvm::Triple &T,
                                       std::vector<std::string> &Includes,
                                       DiagnosticsEngine &Diags) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  Opts.PIE = T.isOSLinux();

  if (IK.getLanguage() == Language::LLVM_IR)
    return Diags.getNumErrors() == NumErrorsBefore;

  LangStandard::Kind LangStd = LangStandard::lang_unspecified;
  if (const Arg *A = Args.getLastArg(OPT_std_EQ)) {
    LangStd = LangStandard::getLangKind(A->getValue());
    if (LangStd == LangStandard::lang_unspecified) {
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
      for (unsigned KindValue = 0; KindValue != LangStandard::lang_unspecified;
           ++KindValue) {
        const LangStandard &Std = LangStandard::getLangStandardForKind(
            static_cast<LangStandard::Kind>(KindValue));
        if (isInputCompatibleWithStandard(IK, Std)) {
          auto Diag = Diags.Report(diag::note_drv_use_standard);
          Diag << Std.getName() << Std.getDescription();
          unsigned NumAliases = 0;
#define LANGSTANDARD(id, name, lang, desc, features)
#define LANGSTANDARD_ALIAS(id, alias)                                          \
  if (KindValue == LangStandard::lang_##id)                                    \
    ++NumAliases;
#define LANGSTANDARD_ALIAS_DEPR(id, alias)
#include "neverc/Foundation/LangOpts/LangStandards.def"
          Diag << NumAliases;
#define LANGSTANDARD(id, name, lang, desc, features)
#define LANGSTANDARD_ALIAS(id, alias)                                          \
  if (KindValue == LangStandard::lang_##id)                                    \
    Diag << alias;
#define LANGSTANDARD_ALIAS_DEPR(id, alias)
#include "neverc/Foundation/LangOpts/LangStandards.def"
        }
      }
    } else {
      const LangStandard &Std = LangStandard::getLangStandardForKind(LangStd);
      if (!isInputCompatibleWithStandard(IK, Std)) {
        Diags.Report(diag::err_drv_argument_not_allowed_with)
            << A->getAsString(Args) << getInputKindName(IK);
      }
    }
  }

  LangOptions::setLangDefaults(Opts, IK.getLanguage(), T, Includes, LangStd);

  LangOptions *LangOpts = &Opts;

#define LANG_OPTION_WITH_MARSHALLING(...)                                      \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef LANG_OPTION_WITH_MARSHALLING

  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    llvm::StringRef Name = A->getValue();
    if (Name == "full" || Name == "branch") {
      Opts.CFProtectionBranch = 1;
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_fgnuc_version_EQ)) {
    llvm::VersionTuple GNUCVer;
    bool Invalid = GNUCVer.tryParse(A->getValue());
    unsigned Major = GNUCVer.getMajor();
    unsigned Minor = GNUCVer.getMinor();
    unsigned Patch = GNUCVer.getSubminor();
    if (Invalid || GNUCVer.getBuild() || Minor >= 100 || Patch >= 100) {
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    }
    Opts.GNUCVersion = Major * 100 * 100 + Minor * 100 + Patch;
  }

  if (Args.hasArg(OPT_ftrapv)) {
    Opts.setSignedOverflowBehavior(LangOptions::SOB_Trapping);
    Opts.OverflowHandler =
        std::string(Args.getLastArgValue(OPT_ftrapv_handler));
  } else if (Args.hasArg(OPT_fwrapv))
    Opts.setSignedOverflowBehavior(LangOptions::SOB_Defined);

  Opts.MSCompatibilityVersion = 0;
  if (const Arg *A = Args.getLastArg(OPT_fms_compatibility_version)) {
    llvm::VersionTuple VT;
    if (VT.tryParse(A->getValue()))
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    Opts.MSCompatibilityVersion =
        VT.getMajor() * 10000000 + VT.getMinor() * 100000 + VT.getSubminor();
  }

  Opts.Trigraphs = !Opts.GNUMode && !Opts.MSVCCompat;
  Opts.Trigraphs =
      Args.hasFlag(OPT_ftrigraphs, OPT_fno_trigraphs, Opts.Trigraphs);

  Opts.NoBuiltin = Args.hasArg(OPT_fno_builtin) || Opts.Freestanding;
  if (!Opts.NoBuiltin)
    collectNoBuiltinFuncs(Args, Opts.NoBuiltinFuncs);
  if (Arg *A = Args.getLastArg(options::OPT_LongDouble_Group)) {
    if (A->getOption().matches(options::OPT_mlong_double_64))
      Opts.LongDoubleSize = 64;
    else if (A->getOption().matches(options::OPT_mlong_double_80))
      Opts.LongDoubleSize = 80;
    else if (A->getOption().matches(options::OPT_mlong_double_128))
      Opts.LongDoubleSize = 128;
    else
      Opts.LongDoubleSize = 0;
  }
  if (Arg *A = Args.getLastArg(OPT_mrtd)) {
    if (Opts.getDefaultCallingConv() != LangOptions::DCC_None)
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << "-fdefault-calling-conv";
    else {
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << T.getTriple();
    }
  }

  unsigned Opt = getOptimizationLevel(Args, IK, Diags),
           OptSize = getOptimizationLevelSize(Args);
  Opts.Optimize = Opt != 0;
  Opts.OptimizeSize = OptSize != 0;

  Opts.NoInlineDefine = !Opts.Optimize;
  if (Arg *InlineArg = Args.getLastArg(
          options::OPT_finline_functions, options::OPT_finline_hint_functions,
          options::OPT_fno_inline_functions, options::OPT_fno_inline))
    if (InlineArg->getOption().matches(options::OPT_fno_inline))
      Opts.NoInlineDefine = true;

  if (Arg *A = Args.getLastArg(OPT_ffp_contract)) {
    llvm::StringRef Val = A->getValue();
    if (Val == "fast")
      Opts.setDefaultFPContractMode(LangOptions::FPM_Fast);
    else if (Val == "on")
      Opts.setDefaultFPContractMode(LangOptions::FPM_On);
    else if (Val == "off")
      Opts.setDefaultFPContractMode(LangOptions::FPM_Off);
    else if (Val == "fast-honor-pragmas")
      Opts.setDefaultFPContractMode(LangOptions::FPM_FastHonorPragmas);
    else
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  if (Args.hasArg(OPT_fsanitize_EQ) || Args.hasArg(OPT_fsanitize_recover_EQ) ||
      Args.hasArg(OPT_fsanitize_trap_EQ) ||
      Args.hasArg(OPT_fsanitize_ignorelist_EQ) ||
      Args.hasArg(OPT_fsanitize_system_ignorelist_EQ)) {
    Diags.Report(diag::err_drv_unsupported) << "sanitizers (-fsanitize=...)";
  }

  if (Arg *A = Args.getLastArg(OPT_msign_return_address_EQ)) {
    llvm::StringRef SignScope = A->getValue();

    if (SignScope.equals_insensitive("none"))
      Opts.setSignReturnAddressScope(
          LangOptions::SignReturnAddressScopeKind::None);
    else if (SignScope.equals_insensitive("all"))
      Opts.setSignReturnAddressScope(
          LangOptions::SignReturnAddressScopeKind::All);
    else if (SignScope.equals_insensitive("non-leaf"))
      Opts.setSignReturnAddressScope(
          LangOptions::SignReturnAddressScopeKind::NonLeaf);
    else
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << SignScope;

    if (Arg *A = Args.getLastArg(OPT_msign_return_address_key_EQ)) {
      llvm::StringRef SignKey = A->getValue();
      if (!SignScope.empty() && !SignKey.empty()) {
        if (SignKey == "a_key")
          Opts.setSignReturnAddressKey(
              LangOptions::SignReturnAddressKeyKind::AKey);
        else if (SignKey == "b_key")
          Opts.setSignReturnAddressKey(
              LangOptions::SignReturnAddressKeyKind::BKey);
        else
          Diags.Report(diag::err_drv_invalid_value)
              << A->getAsString(Args) << SignKey;
      }
    }
  }

  for (const auto &A : Args.getAllArgValues(OPT_fmacro_prefix_map_EQ)) {
    auto Split = llvm::StringRef(A).split('=');
    Opts.MacroPrefixMap.insert(
        {std::string(Split.first), std::string(Split.second)});
  }

  Opts.UseTargetPathSeparator =
      !Args.getLastArg(OPT_fno_file_reproducible) &&
      (Args.getLastArg(OPT_ffile_compilation_dir_EQ) ||
       Args.getLastArg(OPT_fmacro_prefix_map_EQ) ||
       Args.getLastArg(OPT_ffile_reproducible));

  if (Arg *A = Args.getLastArg(options::OPT_mvscale_min_EQ)) {
    unsigned VScaleMin;
    if (llvm::StringRef(A->getValue()).getAsInteger(10, VScaleMin) ||
        VScaleMin == 0)
      Diags.Report(diag::err_frontend_unbounded_vscale_min);
  }

  if (const Arg *A = Args.getLastArg(OPT_frandomize_layout_seed_file_EQ)) {
    std::ifstream SeedFile(A->getValue(0));

    if (!SeedFile.is_open())
      Diags.Report(diag::err_drv_cannot_open_randomize_layout_seed_file)
          << A->getValue(0);

    std::getline(SeedFile, Opts.RandstructSeed);
  }

  if (const Arg *A = Args.getLastArg(OPT_frandomize_layout_seed_EQ))
    Opts.RandstructSeed = A->getValue(0);

  return Diags.getNumErrors() == NumErrorsBefore;
}
