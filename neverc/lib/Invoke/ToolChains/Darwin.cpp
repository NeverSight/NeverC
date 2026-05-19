#include "Darwin.h"
#include "CommonArgs.h"
#include "neverc/Config/config.h"
#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/Options.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/TargetParser.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdlib> // ::getenv

using namespace neverc::driver;
using namespace neverc::driver::tools;
using namespace neverc::driver::toolchains;
using namespace neverc;
using namespace llvm::opt;

llvm::Triple::ArchType
darwin::getArchTypeForMachOArchName(llvm::StringRef Str) {
  // See arch(3) and llvm-gcc's driver-driver.c. We don't implement support for
  // archs which Darwin doesn't use.

  // The matching this routine does is fairly pointless, since it is neither the
  // complete architecture list, nor a reasonable subset. The problem is that
  // historically the driver accepts this and also ties its -march=
  // handling to the architecture name, so we need to be careful before removing
  // support for it.

  // This code must be kept in sync with NeverC's Darwin specific argument
  // translation.

  return llvm::StringSwitch<llvm::Triple::ArchType>(Str)
      .Case("x86_64", llvm::Triple::x86_64)
      .Case("arm64", llvm::Triple::aarch64)
      .Default(llvm::Triple::UnknownArch);
}

// ===----------------------------------------------------------------------===
// Architecture & tool helpers
// ===----------------------------------------------------------------------===

void darwin::setTripleTypeForMachOArchName(llvm::Triple &T, llvm::StringRef Str,
                                           const ArgList &Args) {
  const llvm::Triple::ArchType Arch = getArchTypeForMachOArchName(Str);
  T.setArch(Arch);
  if (Arch != llvm::Triple::UnknownArch)
    T.setArchName(Str);
}

void darwin::MachOTool::anchor() {}

void darwin::MachOTool::AddMachOArch(const ArgList &Args,
                                     ArgStringList &CmdArgs) const {
  llvm::StringRef ArchName = getMachOToolChain().getMachOArchName(Args);

  // Derived from darwin_arch spec.
  CmdArgs.push_back("-arch");
  CmdArgs.push_back(Args.MakeArgString(ArchName));
}

// ===----------------------------------------------------------------------===
// Linker job construction
// ===----------------------------------------------------------------------===

void darwin::Linker::AddLinkArgs(Compilation &C, const ArgList &Args,
                                 ArgStringList &CmdArgs,
                                 const InputInfoList &Inputs,
                                 llvm::VersionTuple Version) const {
  const Driver &D = getToolChain().getDriver();
  const toolchains::MachO &MachOTC = getMachOToolChain();

  // -demangle is now passed via LinkerDriverConfig.

  // -export_dynamic (-rdynamic) is now conveyed via
  // LinkerDriverConfig.exportDynamic (set after populateLinkerDriverConfig).

  // -no_deduplicate removed: ICF is now controlled via
  // LinkerDriverConfig.icfLevel (derived from -O level).

  // Output type and static/dynamic linking mode are now conveyed via
  // LinkerDriverConfig.shared / .bundle / .staticLink (set after
  // populateLinkerDriverConfig).
  // -arch is now set via LinkerDriverConfig.archName.
  if (!Args.hasArg(options::OPT_dynamiclib)) {
    Args.AddLastArg(CmdArgs, options::OPT_force__cpusubtype__ALL);

    Args.AddAllArgs(CmdArgs, options::OPT_bundle__loader);
    Args.AddAllArgs(CmdArgs, options::OPT_client__name);

    Arg *A;
    if ((A = Args.getLastArg(options::OPT_compatibility__version)) ||
        (A = Args.getLastArg(options::OPT_current__version)) ||
        (A = Args.getLastArg(options::OPT_install__name)))
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << A->getAsString(Args) << "-dynamiclib";

    Args.AddLastArg(CmdArgs, options::OPT_force__flat__namespace);
    Args.AddLastArg(CmdArgs, options::OPT_private__bundle);
  } else {
    Arg *A;
    if ((A = Args.getLastArg(options::OPT_bundle)) ||
        (A = Args.getLastArg(options::OPT_bundle__loader)) ||
        (A = Args.getLastArg(options::OPT_client__name)) ||
        (A = Args.getLastArg(options::OPT_force__flat__namespace)) ||
        (A = Args.getLastArg(options::OPT_private__bundle)))
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << A->getAsString(Args) << "-dynamiclib";

    Args.AddAllArgsTranslated(CmdArgs, options::OPT_compatibility__version,
                              "-dylib_compatibility_version");
    Args.AddAllArgsTranslated(CmdArgs, options::OPT_current__version,
                              "-dylib_current_version");

    Args.AddAllArgsTranslated(CmdArgs, options::OPT_install__name,
                              "-dylib_install_name");
  }

  Args.AddLastArg(CmdArgs, options::OPT_all__load);
  Args.AddAllArgs(CmdArgs, options::OPT_allowable__client);
  Args.AddLastArg(CmdArgs, options::OPT_bind__at__load);
  if (MachOTC.isTargetIOSBased())
    Args.AddLastArg(CmdArgs, options::OPT_arch__errors__fatal);
  Args.AddLastArg(CmdArgs, options::OPT_dead__strip);
  Args.AddLastArg(CmdArgs, options::OPT_no__dead__strip__inits__and__terms);
  Args.AddAllArgs(CmdArgs, options::OPT_dylib__file);
  Args.AddLastArg(CmdArgs, options::OPT_dynamic);
  Args.AddAllArgs(CmdArgs, options::OPT_exported__symbols__list);
  Args.AddLastArg(CmdArgs, options::OPT_flat__namespace);
  Args.AddAllArgs(CmdArgs, options::OPT_force__load);
  Args.AddAllArgs(CmdArgs, options::OPT_headerpad__max__install__names);
  Args.AddAllArgs(CmdArgs, options::OPT_image__base);
  Args.AddAllArgs(CmdArgs, options::OPT_init);

  // -platform_version is now set via LinkerDriverConfig.

  Args.AddLastArg(CmdArgs, options::OPT_nomultidefs);
  Args.AddLastArg(CmdArgs, options::OPT_multi__module);
  Args.AddLastArg(CmdArgs, options::OPT_single__module);
  Args.AddAllArgs(CmdArgs, options::OPT_multiply__defined);
  Args.AddAllArgs(CmdArgs, options::OPT_multiply__defined__unused);

  // -pie/-no_pie removed: now conveyed via LinkerDriverConfig.pie
  // (set after populateLinkerDriverConfig below).

  // GlobalISel, kernel/freestanding mllvm opts are now conveyed via
  // LinkerDriverConfig.mllvmOpts (populated in populateLinkerDriverConfig).

  Args.AddLastArg(CmdArgs, options::OPT_prebind);
  Args.AddLastArg(CmdArgs, options::OPT_noprebind);
  Args.AddLastArg(CmdArgs, options::OPT_nofixprebinding);
  Args.AddLastArg(CmdArgs, options::OPT_prebind__all__twolevel__modules);
  Args.AddLastArg(CmdArgs, options::OPT_read__only__relocs);
  Args.AddAllArgs(CmdArgs, options::OPT_sectcreate);
  Args.AddAllArgs(CmdArgs, options::OPT_sectorder);
  Args.AddAllArgs(CmdArgs, options::OPT_seg1addr);
  Args.AddAllArgs(CmdArgs, options::OPT_segprot);
  Args.AddAllArgs(CmdArgs, options::OPT_segaddr);
  Args.AddAllArgs(CmdArgs, options::OPT_segs__read__only__addr);
  Args.AddAllArgs(CmdArgs, options::OPT_segs__read__write__addr);
  Args.AddAllArgs(CmdArgs, options::OPT_seg__addr__table);
  Args.AddAllArgs(CmdArgs, options::OPT_seg__addr__table__filename);
  Args.AddAllArgs(CmdArgs, options::OPT_sub__library);
  Args.AddAllArgs(CmdArgs, options::OPT_sub__umbrella);

  // -syslibroot is now conveyed via LinkerDriverConfig.sysroot.

  Args.AddLastArg(CmdArgs, options::OPT_twolevel__namespace);
  Args.AddLastArg(CmdArgs, options::OPT_twolevel__namespace__hints);
  Args.AddAllArgs(CmdArgs, options::OPT_umbrella);
  Args.AddAllArgs(CmdArgs, options::OPT_undefined);
  Args.AddAllArgs(CmdArgs, options::OPT_unexported__symbols__list);
  Args.AddAllArgs(CmdArgs, options::OPT_weak__reference__mismatches);
  Args.AddLastArg(CmdArgs, options::OPT_X_Flag);
  Args.AddAllArgs(CmdArgs, options::OPT_y);
  Args.AddLastArg(CmdArgs, options::OPT_w);
  Args.AddAllArgs(CmdArgs, options::OPT_pagezero__size);
  Args.AddAllArgs(CmdArgs, options::OPT_segs__read__);
  Args.AddLastArg(CmdArgs, options::OPT_seglinkedit);
  Args.AddLastArg(CmdArgs, options::OPT_noseglinkedit);
  Args.AddAllArgs(CmdArgs, options::OPT_sectalign);
  Args.AddAllArgs(CmdArgs, options::OPT_sectobjectsymbols);
  Args.AddAllArgs(CmdArgs, options::OPT_segcreate);
  Args.AddLastArg(CmdArgs, options::OPT_why_load);
  Args.AddLastArg(CmdArgs, options::OPT_whatsloaded);
  Args.AddAllArgs(CmdArgs, options::OPT_dylinker__install__name);
  Args.AddLastArg(CmdArgs, options::OPT_dylinker);
  Args.AddLastArg(CmdArgs, options::OPT_Mach);
}

// checkRemarksOptions, renderRemarksOptions: removed; remarks are now
// conveyed via LinkerDriverConfig and populated in ConstructJob below.

void darwin::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  assert(Output.getType() == types::TY_Image && "Invalid linker output type.");

  // If the number of arguments surpasses the system limits, we will encode the
  // input files in a separate file, shortening the command line. To this end,
  // build a list of input file names that can be passed via a file with the
  // -filelist linker option.
  llvm::opt::ArgStringList InputFileList;

  // The logic here is derived from gcc's behavior; most of which
  // comes from specs (starting with link_command). Consult gcc for
  // more information.
  ArgStringList CmdArgs;

  llvm::VersionTuple Version = getMachOToolChain().getLinkerVersion(Args);

  const char *Exec = Args.MakeArgString(getToolChain().GetLinkerPath());

  AddLinkArgs(C, Args, CmdArgs, Inputs, Version);

  // Remarks, -moutline, and -mllvm options are now conveyed via
  // LinkerDriverConfig (populated by populateLinkerDriverConfig).
  // Darwin-specific mllvm opts are added below after LinkerCommand creation.

  // -s (strip all) is now passed via LinkerDriverConfig.stripLevel.
  // -t (trace) is now passed via LinkerDriverConfig.traceFiles.
  // -r is now conveyed via LinkerDriverConfig.relocatable.
  Args.addAllArgs(CmdArgs, {options::OPT_d_Flag, options::OPT_Z_Flag,
                            options::OPT_u_Group});

  // Output file is passed via LinkerDriverConfig.outputFile.

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles))
    getMachOToolChain().addStartObjectFileArgs(Args, CmdArgs);

  Args.AddAllArgs(CmdArgs, options::OPT_L);

  AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);
  // Build the input file for -filelist (list of linker input files) in case we
  // need it later
  for (const auto &II : Inputs) {
    if (!II.isFilename()) {
      // This is a linker input argument.
      // We cannot mix input arguments and file names in a -filelist input, thus
      // we prematurely stop our list (remaining files shall be passed as
      // arguments).
      if (InputFileList.size() > 0)
        break;

      continue;
    }

    InputFileList.push_back(II.getFilename());
  }

  if (LinkingOutput) {
    CmdArgs.push_back("-arch_multiple");
    CmdArgs.push_back("-final_output");
    CmdArgs.push_back(LinkingOutput);
  }

  if (Args.hasArg(options::OPT_fnested_functions))
    CmdArgs.push_back("-allow_stack_execute");

  // -threads= is now passed via LinkerDriverConfig.

  bool NoStdOrDefaultLibs =
      Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs);
  bool ForceLinkBuiltins = Args.hasArg(options::OPT_fapple_link_rtlib);
  if (!NoStdOrDefaultLibs || ForceLinkBuiltins) {
    // link_ssp spec is empty.

    // If we have both -nostdlib/nodefaultlibs and -fapple-link-rtlib then
    // we just want to link the builtins, not the other libs like libSystem.
    if (NoStdOrDefaultLibs && ForceLinkBuiltins) {
      getMachOToolChain().AddLinkRuntimeLib(Args, CmdArgs, "builtins");
    } else {
      // Let the tool chain choose which runtime library to link.
      getMachOToolChain().AddLinkRuntimeLibArgs(Args, CmdArgs,
                                                ForceLinkBuiltins);

      // No need to do anything for pthreads. Claim argument to avoid warning.
      Args.ClaimAllArgs(options::OPT_pthread);
      Args.ClaimAllArgs(options::OPT_pthreads);
    }
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    // endfile_spec is empty.
  }

  Args.AddAllArgs(CmdArgs, options::OPT_T_Group);
  Args.AddAllArgs(CmdArgs, options::OPT_F);

  // -iframework should be forwarded as -F.
  for (const Arg *A : Args.filtered(options::OPT_iframework))
    CmdArgs.push_back(Args.MakeArgString(std::string("-F") + A->getValue()));

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    if (Arg *A = Args.getLastArg(options::OPT_fveclib)) {
      if (A->getValue() == llvm::StringRef("Accelerate")) {
        CmdArgs.push_back("-framework");
        CmdArgs.push_back("Accelerate");
      }
    }
  }

  auto LCmd = std::make_unique<LinkerCommand>(
      JA, *this, ResponseFileSupport::AtFileUTF8(), Exec, CmdArgs, Inputs,
      LinkerFlavor::Darwin, Output);
  LCmd->setInputFileList(std::move(InputFileList));
  populateLinkerDriverConfig(getToolChain(), Args, LCmd->getDriverConfig());
  auto &MacCfg = LCmd->getDriverConfig();

  // On Darwin, -isysroot serves as the syslibroot when --sysroot= is absent.
  if (MacCfg.sysroot.empty()) {
    llvm::StringRef SR = C.getSysRoot();
    if (!SR.empty())
      MacCfg.sysroot = SR.str();
    else if (const Arg *A = Args.getLastArg(options::OPT_isysroot))
      MacCfg.sysroot = A->getValue();
  }

  MacCfg.outputFile = Output.getFilename();
  MacCfg.shared = Args.hasArg(options::OPT_dynamiclib);
  MacCfg.bundle = Args.hasArg(options::OPT_bundle);
  MacCfg.relocatable = Args.hasArg(options::OPT_r);
  MacCfg.staticLink = Args.hasArg(options::OPT_static);
  MacCfg.exportDynamic = Args.hasArg(options::OPT_rdynamic);
  MacCfg.archName = getMachOToolChain().getMachOArchName(Args).str();
  MacCfg.repro = true; // MachO default: reproducible output (zero mod times)
  {
    const auto &DarwinTC = static_cast<const Darwin &>(getToolChain());
    DarwinTC.populatePlatformVersionConfig(Args, MacCfg);
  }
  MacCfg.pie = true;

  // globalISel is already conveyed via LinkerDriverConfig.globalISel
  // (set in populateLinkerDriverConfig → createLTOConfig).

  // Darwin-specific mllvm opts that have no direct TargetOptions mapping.
  if (Args.hasArg(options::OPT_mkernel) ||
      Args.hasArg(options::OPT_ffreestanding))
    MacCfg.mllvmOpts.emplace_back("-disable-atexit-based-global-dtor-lowering");

  // Remarks: fill default output path if user requested remarks but
  // didn't specify -foptimization-record-file=.  The direct fields
  // (optRemarksFilename/Format/Passes) flow through createLTOConfig().
  if (willEmitRemarks(Args) && MacCfg.optRemarksFilename.empty() &&
      Output.isFilename()) {
    MacCfg.optRemarksFilename =
        std::string(Output.getFilename()) + ".opt." +
        (MacCfg.optRemarksFormat.empty() ? "yaml" : MacCfg.optRemarksFormat);
  }

  C.addCommand(std::move(LCmd));
}

// ===----------------------------------------------------------------------===
// Static lib, lipo & dsymutil tools
// ===----------------------------------------------------------------------===

void darwin::StaticLibTool::ConstructJob(Compilation &C, const JobAction &JA,
                                         const InputInfo &Output,
                                         const InputInfoList &Inputs,
                                         const ArgList &Args,
                                         const char *LinkingOutput) const {
  const Driver &D = getToolChain().getDriver();

  Args.ClaimAllArgs(options::OPT_g_Group);
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  Args.ClaimAllArgs(options::OPT_w);

  const auto *OutputFileName = Output.getFilename();
  if (Output.isFilename() && llvm::sys::fs::exists(OutputFileName)) {
    if (std::error_code EC = llvm::sys::fs::remove(OutputFileName)) {
      D.Diag(diag::err_drv_unable_to_remove_file) << EC.message();
      return;
    }
  }

  ArgStringList CmdArgs;
  const char *Exec =
      Args.MakeArgString(getToolChain().GetProgramPath("libtool"));
  C.addCommand(std::make_unique<ArchiveCommand>(
      JA, *this, ResponseFileSupport::None(), Exec, CmdArgs, Inputs, Output));
}

void darwin::Lipo::ConstructJob(Compilation &C, const JobAction &JA,
                                const InputInfo &Output,
                                const InputInfoList &Inputs,
                                const ArgList &Args,
                                const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  CmdArgs.push_back("-create");
  assert(Output.isFilename() && "Unexpected lipo output.");

  CmdArgs.push_back("-output");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs) {
    assert(II.isFilename() && "Unexpected lipo input.");
    CmdArgs.push_back(II.getFilename());
  }

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("lipo"));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Output));
}

void darwin::Dsymutil::ConstructJob(Compilation &C, const JobAction &JA,
                                    const InputInfo &Output,
                                    const InputInfoList &Inputs,
                                    const ArgList &Args,
                                    const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  assert(Inputs.size() == 1 && "Unable to handle multiple inputs.");
  const InputInfo &Input = Inputs[0];
  assert(Input.isFilename() && "Unexpected dsymutil input.");
  CmdArgs.push_back(Input.getFilename());

  const char *Exec =
      Args.MakeArgString(getToolChain().GetProgramPath("dsymutil"));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Output));
}

// ===----------------------------------------------------------------------===
// ToolChain construction & configuration
// ===----------------------------------------------------------------------===

MachO::MachO(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  // We expect 'as', 'ld', etc. to be adjacent to our install dir.
  getProgramPaths().push_back(getDriver().getInstalledDir());
  if (getDriver().getInstalledDir() != getDriver().Dir)
    getProgramPaths().push_back(getDriver().Dir);
}

Darwin::Darwin(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : MachO(D, Triple, Args), TargetInitialized(false) {}

types::ID MachO::LookupTypeForExtension(llvm::StringRef Ext) const {
  types::ID Ty = ToolChain::LookupTypeForExtension(Ext);

  // Darwin always preprocesses assembly files (unless -x is used explicitly).
  if (Ty == types::TY_PP_Asm)
    return types::TY_Asm;

  return Ty;
}

bool MachO::HasNativeLLVMSupport() const { return true; }

llvm::StringRef MachO::getMachOArchName(const ArgList &Args) const {
  switch (getTriple().getArch()) {
  default:
    return getDefaultUniversalArchName();

  case llvm::Triple::aarch64:
    return "arm64";
  }
}

llvm::VersionTuple
MachO::getLinkerVersion(const llvm::opt::ArgList &Args) const {
  if (LinkerVersion) {
#ifndef NDEBUG
    llvm::VersionTuple NewLinkerVersion;
    if (Arg *A = Args.getLastArg(options::OPT_mlinker_version_EQ))
      assert(!NewLinkerVersion.tryParse(A->getValue()));
    assert(NewLinkerVersion == LinkerVersion);
#endif
    return *LinkerVersion;
  }

  llvm::VersionTuple NewLinkerVersion;
  if (Arg *A = Args.getLastArg(options::OPT_mlinker_version_EQ))
    if (NewLinkerVersion.tryParse(A->getValue()))
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << A->getAsString(Args);

  LinkerVersion = NewLinkerVersion;
  return *LinkerVersion;
}

Darwin::~Darwin() {}

MachO::~MachO() {}

std::string Darwin::ComputeEffectiveTriple(const ArgList &Args,
                                           types::ID InputType) const {
  llvm::Triple Triple(ComputeLLVMTriple(Args, InputType));

  // If the target isn't initialized (e.g., an unknown Darwin platform, return
  // the default triple).
  if (!isTargetInitialized())
    return Triple.getTriple();

  llvm::SmallString<16> Str;
  if (isTargetIOSBased())
    Str += "ios";
  else
    Str += "macosx";
  Str += getTripleTargetVersion().getAsString();
  Triple.setOSName(Str);

  return Triple.getTriple();
}

Tool *MachO::getTool(Action::ActionClass AC) const {
  switch (AC) {
  case Action::LipoJobClass:
    if (!Lipo)
      Lipo.reset(new tools::darwin::Lipo(*this));
    return Lipo.get();
  case Action::DsymutilJobClass:
    if (!Dsymutil)
      Dsymutil.reset(new tools::darwin::Dsymutil(*this));
    return Dsymutil.get();
  default:
    return ToolChain::getTool(AC);
  }
}

Tool *MachO::buildLinker() const { return new tools::darwin::Linker(*this); }

Tool *MachO::buildStaticLibTool() const {
  return new tools::darwin::StaticLibTool(*this);
}

DarwinNeverC::DarwinNeverC(const Driver &D, const llvm::Triple &Triple,
                           const ArgList &Args)
    : Darwin(D, Triple, Args) {}

// ===----------------------------------------------------------------------===
// Runtime libraries & platform support
// ===----------------------------------------------------------------------===

void DarwinNeverC::addNeverCWarningOptions(ArgStringList &FrontendArgs) const {
  // Always error about undefined 'TARGET_OS_*' macros.
  FrontendArgs.push_back("-Wundef-prefix=TARGET_OS_");
  FrontendArgs.push_back("-Werror=undef-prefix");

  // For modern targets, promote certain warnings to errors.
  if (getTriple().isArch64Bit()) {
    // For iOS, also error about implicit function declarations, as that can
    // impact calling conventions.
    if (!isTargetMacOS())
      FrontendArgs.push_back("-Werror=implicit-function-declaration");
  }
}

unsigned DarwinNeverC::GetDefaultDwarfVersion() const {
  // Default to use DWARF 2 on OS X 10.10 / iOS 8 and lower.
  if ((isTargetMacOSBased() && isMacosxVersionLT(10, 11)) ||
      (isTargetIOSBased() && isIPhoneOSVersionLT(9)))
    return 2;
  return 4;
}

void MachO::AddLinkRuntimeLib(const ArgList &Args, ArgStringList &CmdArgs,
                              llvm::StringRef Component,
                              RuntimeLinkOptions Opts, bool IsShared) const {
  auto buildDarwinLibName = [&](llvm::StringRef Prefix) {
    llvm::SmallString<64> Name(Prefix);
    // On Darwin the builtins component is not in the library name
    if (Component != "builtins") {
      Name += Component;
      if (!(Opts & RLO_IsEmbedded))
        Name += "_";
    }
    Name += getOSLibraryNameSuffix();
    Name += IsShared ? "_dynamic.dylib" : ".a";
    return Name;
  };

  llvm::SmallString<128> Dir(getDriver().ResourceDir);
  llvm::sys::path::append(Dir, "lib", "darwin");
  if (Opts & RLO_IsEmbedded)
    llvm::sys::path::append(Dir, "macho_embedded");

  llvm::SmallString<128> P(Dir);
  bool LinkedRuntime = false;
  for (llvm::StringRef Prefix :
       {llvm::StringRef("libneverc_rt."), llvm::StringRef("libclang_rt.")}) {
    llvm::SmallString<128> Candidate(P);
    llvm::sys::path::append(Candidate, buildDarwinLibName(Prefix));
    if ((Opts & RLO_AlwaysLink) || getVFS().exists(Candidate)) {
      CmdArgs.push_back(Args.MakeArgString(Candidate));
      LinkedRuntime = true;
      break;
    }
  }

  // For now, allow missing resource libraries to support developers who may
  // not have compiler-rt checked out or integrated into their build (unless
  // we explicitly force linking with this library).

  // Adding the rpaths might negatively interact when other rpaths are involved,
  // so we should make sure we add the rpaths last, after all user-specified
  // rpaths. This is currently true from this place, but we need to be
  // careful if this function is ever called before user's rpaths are emitted.
  if ((Opts & RLO_AddRPath) && LinkedRuntime) {
    assert(IsShared && "must be a dynamic library");

    // Add @executable_path to rpath to support having the dylib copied with
    // the executable.
    CmdArgs.push_back("-rpath");
    CmdArgs.push_back("@executable_path");

    // Add the path to the resource dir to rpath to support using the dylib
    // from the default location without copying.
    CmdArgs.push_back("-rpath");
    CmdArgs.push_back(Args.MakeArgString(Dir));
  }
}

llvm::StringRef Darwin::getPlatformFamily() const {
  switch (TargetPlatform) {
  case DarwinPlatformKind::MacOS:
    return "MacOSX";
  case DarwinPlatformKind::IPhoneOS:
    return "iPhone";
  }
  llvm_unreachable("Unsupported platform");
}

llvm::StringRef Darwin::getSDKName(llvm::StringRef isysroot) {
  // Assume SDK has path: SOME_PATH/SDKs/PlatformXX.YY.sdk
  auto BeginSDK = llvm::sys::path::rbegin(isysroot);
  auto EndSDK = llvm::sys::path::rend(isysroot);
  for (auto IT = BeginSDK; IT != EndSDK; ++IT) {
    llvm::StringRef SDK = *IT;
    if (SDK.ends_with(".sdk"))
      return SDK.slice(0, SDK.size() - 4);
  }
  return "";
}

llvm::StringRef Darwin::getOSLibraryNameSuffix(bool IgnoreSim) const {
  switch (TargetPlatform) {
  case DarwinPlatformKind::MacOS:
    return "osx";
  case DarwinPlatformKind::IPhoneOS:
    return TargetEnvironment == NativeEnvironment || IgnoreSim ? "ios"
                                                               : "iossim";
  }
  llvm_unreachable("Unsupported platform");
}

ToolChain::RuntimeLibType
DarwinNeverC::GetRuntimeLibType(const ArgList &Args) const {
  if (Arg *A = Args.getLastArg(options::OPT_rtlib_EQ)) {
    llvm::StringRef Value = A->getValue();
    if (Value != "compiler-rt" && Value != "platform")
      getDriver().Diag(neverc::diag::err_drv_unsupported_rtlib_for_platform)
          << Value << "darwin";
  }

  return ToolChain::RLT_CompilerRT;
}

void DarwinNeverC::AddLinkRuntimeLibArgs(const ArgList &Args,
                                         ArgStringList &CmdArgs,
                                         bool ForceLinkBuiltinRT) const {
  // Call once to ensure diagnostic is printed if wrong value was specified
  GetRuntimeLibType(Args);

  // Darwin doesn't support real static executables, don't link any runtime
  // libraries with -static.
  if (Args.hasArg(options::OPT_static) || Args.hasArg(options::OPT_mkernel)) {
    if (ForceLinkBuiltinRT)
      AddLinkRuntimeLib(Args, CmdArgs, "builtins");
    return;
  }

  // Reject -static-libgcc for now, we can deal with this when and if someone
  // cares.
  if (const Arg *A = Args.getLastArg(options::OPT_static_libgcc)) {
    getDriver().Diag(diag::err_drv_unsupported_opt) << A->getAsString(Args);
    return;
  }

  // Otherwise link libSystem, then the dynamic runtime library, and finally any
  // target specific static runtime library.
  CmdArgs.push_back("-lSystem");

  // Select the dynamic runtime library and the target specific static library.
  if (isTargetIOSBased()) {
    // If we are compiling as iOS / simulator, don't attempt to link libgcc_s.1,
    // it never went into the SDK.
    // Linking against libgcc_s.1 isn't needed for iOS 5.0+
    if (isIPhoneOSVersionLT(5, 0) && !isTargetIOSSimulator() &&
        getTriple().getArch() != llvm::Triple::aarch64)
      CmdArgs.push_back("-lgcc_s.1");
  }
  AddLinkRuntimeLib(Args, CmdArgs, "builtins");
}

namespace {
std::string getSystemOrSDKMacOSVersion(llvm::StringRef MacOSSDKVersion) {
  llvm::Triple SystemTriple{llvm::sys::getDefaultTargetTriple()};
  if (!SystemTriple.isMacOSX())
    return std::string(MacOSSDKVersion);
  llvm::VersionTuple SystemVersion;
  SystemTriple.getMacOSXVersion(SystemVersion);

  unsigned Major, Minor, Micro;
  bool HadExtra;
  if (!Driver::GetReleaseVersion(MacOSSDKVersion, Major, Minor, Micro,
                                 HadExtra))
    return std::string(MacOSSDKVersion);
  llvm::VersionTuple SDKVersion(Major, Minor, Micro);

  if (SDKVersion > SystemVersion)
    return SystemVersion.getAsString().str().str();
  return std::string(MacOSSDKVersion);
}
} // namespace

namespace {

struct DarwinPlatform {
  enum SourceKind {
    /// The OS was specified using the -target argument.
    TargetArg,
    /// The OS was specified using the -mtargetos= argument.
    MTargetOSArg,
    /// The OS was specified using the -m<os>-version-min argument.
    OSVersionArg,
    /// The OS was specified using the OS_DEPLOYMENT_TARGET environment.
    DeploymentTargetEnv,
    /// The OS was inferred from the SDK.
    InferredFromSDK,
    /// The OS was inferred from the -arch.
    InferredFromArch
  };

  using DarwinPlatformKind = Darwin::DarwinPlatformKind;
  using DarwinEnvironmentKind = Darwin::DarwinEnvironmentKind;

  DarwinPlatformKind getPlatform() const { return Platform; }

  DarwinEnvironmentKind getEnvironment() const { return Environment; }

  void setEnvironment(DarwinEnvironmentKind Kind) {
    Environment = Kind;
    InferSimulatorFromArch = false;
  }

  llvm::StringRef getOSVersion() const {
    if (Kind == OSVersionArg)
      return Argument->getValue();
    return OSVersion;
  }

  void setOSVersion(llvm::StringRef S) {
    assert(Kind == TargetArg && "Unexpected kind!");
    OSVersion = std::string(S);
  }

  bool hasOSVersion() const { return HasOSVersion; }

  bool isExplicitlySpecified() const { return Kind <= DeploymentTargetEnv; }

  bool canInferSimulatorFromArch() const { return InferSimulatorFromArch; }

  void addOSVersionMinArgument(DerivedArgList &Args, const OptTable &Opts) {
    if (Argument)
      return;
    assert(Kind != TargetArg && Kind != MTargetOSArg && Kind != OSVersionArg &&
           "Invalid kind");
    options::ID Opt;
    switch (Platform) {
    case DarwinPlatformKind::MacOS:
      Opt = options::OPT_mmacos_version_min_EQ;
      break;
    case DarwinPlatformKind::IPhoneOS:
      Opt = options::OPT_mios_version_min_EQ;
      break;
    }
    Argument = Args.MakeJoinedArg(nullptr, Opts.getOption(Opt), OSVersion);
    Args.append(Argument);
  }

  std::string getAsString(DerivedArgList &Args, const OptTable &Opts) {
    switch (Kind) {
    case TargetArg:
    case MTargetOSArg:
    case OSVersionArg:
    case InferredFromSDK:
    case InferredFromArch:
      assert(Argument && "OS version argument not yet inferred");
      return Argument->getAsString(Args);
    case DeploymentTargetEnv:
      return (llvm::Twine(EnvVarName) + "=" + OSVersion).str();
    }
    llvm_unreachable("Unsupported Darwin Source Kind");
  }

  void setEnvironment(llvm::Triple::EnvironmentType EnvType) {
    if (EnvType == llvm::Triple::Simulator)
      Environment = DarwinEnvironmentKind::Simulator;
  }

  static DarwinPlatform createFromTarget(const llvm::Triple &TT,
                                         llvm::StringRef OSVersion, Arg *A) {
    DarwinPlatform Result(TargetArg, getPlatformFromOS(TT.getOS()), OSVersion,
                          A);
    llvm::VersionTuple OsVersion = TT.getOSVersion();
    if (OsVersion.getMajor() == 0)
      Result.HasOSVersion = false;
    Result.setEnvironment(TT.getEnvironment());
    return Result;
  }
  static DarwinPlatform
  createFromMTargetOS(llvm::Triple::OSType OS, llvm::VersionTuple OSVersion,
                      llvm::Triple::EnvironmentType Environment, Arg *A) {
    DarwinPlatform Result(MTargetOSArg, getPlatformFromOS(OS),
                          OSVersion.getAsString(), A);
    Result.InferSimulatorFromArch = false;
    Result.setEnvironment(Environment);
    return Result;
  }
  static DarwinPlatform createOSVersionArg(DarwinPlatformKind Platform, Arg *A,
                                           bool IsSimulator) {
    DarwinPlatform Result{OSVersionArg, Platform, A};
    if (IsSimulator)
      Result.Environment = DarwinEnvironmentKind::Simulator;
    return Result;
  }
  static DarwinPlatform createDeploymentTargetEnv(DarwinPlatformKind Platform,
                                                  llvm::StringRef EnvVarName,
                                                  llvm::StringRef Value) {
    DarwinPlatform Result(DeploymentTargetEnv, Platform, Value);
    Result.EnvVarName = EnvVarName;
    return Result;
  }
  static DarwinPlatform createFromSDK(DarwinPlatformKind Platform,
                                      llvm::StringRef Value,
                                      bool IsSimulator = false) {
    DarwinPlatform Result(InferredFromSDK, Platform, Value);
    if (IsSimulator)
      Result.Environment = DarwinEnvironmentKind::Simulator;
    Result.InferSimulatorFromArch = false;
    return Result;
  }
  static DarwinPlatform createFromArch(llvm::Triple::OSType OS,
                                       llvm::StringRef Value) {
    return DarwinPlatform(InferredFromArch, getPlatformFromOS(OS), Value);
  }

  DarwinSDKInfo inferSDKInfo() {
    assert(Kind == InferredFromSDK && "can infer SDK info only");
    llvm::VersionTuple Version;
    assert(!Version.tryParse(OSVersion) && "invalid SDK version");
    return DarwinSDKInfo(Version,
                         /*MaximumDeploymentTarget=*/llvm::VersionTuple(
                             Version.getMajor(), 0, 99));
  }

private:
  DarwinPlatform(SourceKind Kind, DarwinPlatformKind Platform, Arg *Argument)
      : Kind(Kind), Platform(Platform), Argument(Argument) {}
  DarwinPlatform(SourceKind Kind, DarwinPlatformKind Platform,
                 llvm::StringRef Value, Arg *Argument = nullptr)
      : Kind(Kind), Platform(Platform), OSVersion(Value), Argument(Argument) {}

  static DarwinPlatformKind getPlatformFromOS(llvm::Triple::OSType OS) {
    switch (OS) {
    case llvm::Triple::Darwin:
    case llvm::Triple::MacOSX:
      return DarwinPlatformKind::MacOS;
    case llvm::Triple::IOS:
      return DarwinPlatformKind::IPhoneOS;
    default:
      llvm_unreachable("Unable to infer Darwin variant");
    }
  }

  SourceKind Kind;
  DarwinPlatformKind Platform;
  DarwinEnvironmentKind Environment = DarwinEnvironmentKind::NativeEnvironment;
  std::string OSVersion;
  bool HasOSVersion = true, InferSimulatorFromArch = true;
  Arg *Argument;
  llvm::StringRef EnvVarName;
};

std::optional<DarwinPlatform>
getDeploymentTargetFromOSVersionArg(DerivedArgList &Args,
                                    const Driver &TheDriver) {
  Arg *macOSVersion = Args.getLastArg(options::OPT_mmacos_version_min_EQ);
  Arg *iOSVersion = Args.getLastArg(options::OPT_mios_version_min_EQ,
                                    options::OPT_mios_simulator_version_min_EQ);
  if (macOSVersion) {
    if (iOSVersion) {
      TheDriver.Diag(diag::err_drv_argument_not_allowed_with)
          << macOSVersion->getAsString(Args) << iOSVersion->getAsString(Args);
    }
    return DarwinPlatform::createOSVersionArg(Darwin::MacOS, macOSVersion,
                                              /*IsSimulator=*/false);
  } else if (iOSVersion) {
    return DarwinPlatform::createOSVersionArg(
        Darwin::IPhoneOS, iOSVersion,
        iOSVersion->getOption().getID() ==
            options::OPT_mios_simulator_version_min_EQ);
  }
  return std::nullopt;
}

std::optional<DarwinPlatform>
getDeploymentTargetFromEnvironmentVariables(const Driver &TheDriver,
                                            const llvm::Triple &Triple) {
  std::string Targets[Darwin::LastDarwinPlatform + 1];
  const char *EnvVars[] = {
      "MACOSX_DEPLOYMENT_TARGET",
      "IPHONEOS_DEPLOYMENT_TARGET",
  };
  static_assert(std::size(EnvVars) == Darwin::LastDarwinPlatform + 1,
                "Missing platform");
  for (const auto &I : llvm::enumerate(llvm::ArrayRef(EnvVars))) {
    if (char *Env = ::getenv(I.value()))
      Targets[I.index()] = Env;
  }

  // Allow conflicts among OSX and iOS for historical reasons, but choose the
  // default platform.
  if (!Targets[Darwin::MacOS].empty() && !Targets[Darwin::IPhoneOS].empty()) {
    if (Triple.getArch() == llvm::Triple::aarch64)
      Targets[Darwin::MacOS] = "";
    else
      Targets[Darwin::IPhoneOS] = "";
  }

  for (const auto &Target : llvm::enumerate(llvm::ArrayRef(Targets))) {
    if (!Target.value().empty())
      return DarwinPlatform::createDeploymentTargetEnv(
          (Darwin::DarwinPlatformKind)Target.index(), EnvVars[Target.index()],
          Target.value());
  }
  return std::nullopt;
}

namespace {
llvm::StringRef dropSDKNamePrefix(llvm::StringRef SDKName) {
  size_t PrefixPos = SDKName.find('.');
  if (PrefixPos == llvm::StringRef::npos)
    return "";
  return SDKName.substr(PrefixPos + 1);
}
} // namespace

std::optional<DarwinPlatform>
inferDeploymentTargetFromSDK(DerivedArgList &Args,
                             const std::optional<DarwinSDKInfo> &SDKInfo) {
  const Arg *A = Args.getLastArg(options::OPT_isysroot);
  if (!A)
    return std::nullopt;
  llvm::StringRef isysroot = A->getValue();
  llvm::StringRef SDK = Darwin::getSDKName(isysroot);
  if (!SDK.size())
    return std::nullopt;

  std::string Version;
  if (SDKInfo) {
    Version = SDKInfo->getVersion().getAsString().str().str();
  } else {
    // Slice the version number out.
    // Version number is between the first and the last number.
    size_t StartVer = SDK.find_first_of("0123456789");
    size_t EndVer = SDK.find_last_of("0123456789");
    if (StartVer != llvm::StringRef::npos && EndVer > StartVer)
      Version = std::string(SDK.slice(StartVer, EndVer + 1));
  }
  if (Version.empty())
    return std::nullopt;

  auto CreatePlatformFromSDKName =
      [&](llvm::StringRef SDK) -> std::optional<DarwinPlatform> {
    if (SDK.starts_with("iPhoneOS") || SDK.starts_with("iPhoneSimulator"))
      return DarwinPlatform::createFromSDK(
          Darwin::IPhoneOS, Version,
          /*IsSimulator=*/SDK.starts_with("iPhoneSimulator"));
    else if (SDK.starts_with("MacOSX"))
      return DarwinPlatform::createFromSDK(Darwin::MacOS,
                                           getSystemOrSDKMacOSVersion(Version));
    return std::nullopt;
  };
  if (auto Result = CreatePlatformFromSDKName(SDK))
    return Result;
  // The SDK can be an SDK variant with a name like `<prefix>.<platform>`.
  return CreatePlatformFromSDKName(dropSDKNamePrefix(SDK));
}

std::string getOSVersion(llvm::Triple::OSType OS, const llvm::Triple &Triple,
                         const Driver &TheDriver) {
  llvm::VersionTuple OsVersion;
  llvm::Triple SystemTriple{llvm::sys::getDefaultTargetTriple()};
  switch (OS) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
    // If there is no version specified on triple, and both host and target are
    // macos, use the host triple to infer OS version.
    if (Triple.isMacOSX() && SystemTriple.isMacOSX() &&
        !Triple.getOSMajorVersion())
      SystemTriple.getMacOSXVersion(OsVersion);
    else if (!Triple.getMacOSXVersion(OsVersion))
      TheDriver.Diag(diag::err_drv_invalid_darwin_version)
          << Triple.getOSName();
    break;
  case llvm::Triple::IOS:
    OsVersion = Triple.getiOSVersion();
    break;
  default:
    llvm_unreachable("Unexpected OS type");
    break;
  }

  std::string OSVersion;
  llvm::raw_string_ostream(OSVersion)
      << OsVersion.getMajor() << '.' << OsVersion.getMinor() << '.'
      << OsVersion.getSubminor();
  return OSVersion;
}

std::optional<DarwinPlatform>
inferDeploymentTargetFromArch(DerivedArgList &Args, const Darwin &Toolchain,
                              const llvm::Triple &Triple,
                              const Driver &TheDriver) {
  llvm::Triple::OSType OSTy = llvm::Triple::UnknownOS;

  llvm::StringRef MachOArchName = Toolchain.getMachOArchName(Args);
  if (MachOArchName == "arm64")
    OSTy = llvm::Triple::MacOSX;
  else
    OSTy = llvm::Triple::MacOSX; // x86, x86_64
  if (OSTy == llvm::Triple::UnknownOS)
    return std::nullopt;
  return DarwinPlatform::createFromArch(OSTy,
                                        getOSVersion(OSTy, Triple, TheDriver));
}

std::optional<DarwinPlatform> getDeploymentTargetFromTargetArg(
    DerivedArgList &Args, const llvm::Triple &Triple, const Driver &TheDriver) {
  if (!Args.hasArg(options::OPT_target))
    return std::nullopt;
  if (Triple.getOS() == llvm::Triple::Darwin ||
      Triple.getOS() == llvm::Triple::UnknownOS)
    return std::nullopt;
  std::string OSVersion = getOSVersion(Triple.getOS(), Triple, TheDriver);
  return DarwinPlatform::createFromTarget(Triple, OSVersion,
                                          Args.getLastArg(options::OPT_target));
}

std::optional<DarwinPlatform>
getDeploymentTargetFromMTargetOSArg(DerivedArgList &Args,
                                    const Driver &TheDriver) {
  auto *A = Args.getLastArg(options::OPT_mtargetos_EQ);
  if (!A)
    return std::nullopt;
  llvm::Triple TT(llvm::Twine("unknown-apple-") + A->getValue());
  switch (TT.getOS()) {
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
    break;
  default:
    TheDriver.Diag(diag::err_drv_invalid_os_in_arg)
        << TT.getOSName() << A->getAsString(Args);
    return std::nullopt;
  }

  llvm::VersionTuple Version = TT.getOSVersion();
  if (!Version.getMajor()) {
    TheDriver.Diag(diag::err_drv_invalid_version_number)
        << A->getAsString(Args);
    return std::nullopt;
  }
  return DarwinPlatform::createFromMTargetOS(TT.getOS(), Version,
                                             TT.getEnvironment(), A);
}

std::optional<DarwinSDKInfo> parseSDKSettings(llvm::vfs::FileSystem &VFS,
                                              const ArgList &Args,
                                              const Driver &TheDriver) {
  const Arg *A = Args.getLastArg(options::OPT_isysroot);
  if (!A)
    return std::nullopt;
  llvm::StringRef isysroot = A->getValue();
  auto SDKInfoOrErr = parseDarwinSDKInfo(VFS, isysroot);
  if (!SDKInfoOrErr) {
    llvm::consumeError(SDKInfoOrErr.takeError());
    TheDriver.Diag(diag::warn_drv_darwin_sdk_invalid_settings);
    return std::nullopt;
  }
  return *SDKInfoOrErr;
}

} // namespace

namespace {
std::optional<std::string> findDefaultMacOSXSDK() {
  using namespace llvm;
  using namespace llvm::sys;

  // Prefer CommandLineTools SDKs, then Xcode SDKs.
  static constexpr const char *SDKRoots[] = {
      "/Library/Developer/CommandLineTools/SDKs",
      "/Applications/Xcode.app/Contents/Developer/Platforms/"
      "MacOSX.platform/Developer/SDKs",
  };

  std::optional<std::string> Best;
  llvm::VersionTuple BestVer;
  bool BestHasVer = false;

  for (const char *Root : SDKRoots) {
    if (!fs::is_directory(Root))
      continue;

    std::error_code EC;
    for (fs::directory_iterator It(Root, EC), End; It != End && !EC;
         It.increment(EC)) {
      llvm::StringRef P = It->path();
      llvm::StringRef Name = path::filename(P);
      if (!Name.starts_with("MacOSX") || !Name.ends_with(".sdk"))
        continue;

      // Parse versions like "MacOSX15.5.sdk". If it is just "MacOSX.sdk",
      // treat it as version-less and only use it as a fallback.
      llvm::StringRef VerStr =
          Name.drop_front(strlen("MacOSX")).drop_back(strlen(".sdk"));
      llvm::VersionTuple Ver;
      bool HasVer = false;
      if (!VerStr.empty()) {
        unsigned Maj = 0, Min = 0, Mic = 0;
        bool HadExtra = false;
        HasVer = neverc::driver::Driver::GetReleaseVersion(VerStr, Maj, Min,
                                                           Mic, HadExtra) ||
                 (Maj != 0);
        Ver = llvm::VersionTuple(Maj, Min, Mic);
      }

      if (!Best) {
        Best = std::string(P);
        BestVer = Ver;
        BestHasVer = HasVer;
        continue;
      }

      if (HasVer) {
        if (!BestHasVer || Ver > BestVer) {
          Best = std::string(P);
          BestVer = Ver;
          BestHasVer = true;
        }
      } else if (!BestHasVer) {
        // Both are version-less; prefer the first one we saw.
      }
    }
  }

  if (Best && llvm::sys::fs::exists(*Best))
    return Best;
  return std::nullopt;
}
} // namespace

// ===----------------------------------------------------------------------===
// Deployment target & SDK version
// ===----------------------------------------------------------------------===

void Darwin::AddDeploymentTarget(DerivedArgList &Args) const {
  const OptTable &Opts = getDriver().getOpts();

  // Support allowing the SDKROOT environment variable used by xcrun and other
  // Xcode tools to define the default sysroot, by making it the default for
  // isysroot.
  if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
    // Warn if the path does not exist.
    if (!getVFS().exists(A->getValue()))
      getDriver().Diag(neverc::diag::warn_missing_sysroot) << A->getValue();
  } else {
    if (char *env = ::getenv("SDKROOT")) {
      // We only use this value as the default if it is an absolute path,
      // exists, and it is not the root path.
      if (llvm::sys::path::is_absolute(env) && getVFS().exists(env) &&
          llvm::StringRef(env) != "/") {
        Args.append(Args.MakeSeparateArg(
            nullptr, Opts.getOption(options::OPT_isysroot), env));
      }
    } else {
      // If SDKROOT is not set, try to infer a usable default macOS SDK.
      // The embedded linker does not have Apple ld's implicit SDK search
      // paths (e.g. for -lSystem).
      if (auto SDK = findDefaultMacOSXSDK()) {
        Args.append(Args.MakeSeparateArg(nullptr,
                                         Opts.getOption(options::OPT_isysroot),
                                         Args.MakeArgString(*SDK)));
      }
    }
  }

  // Read the SDKSettings.json file for more information, like the SDK version
  // that we can pass down to the compiler.
  SDKInfo = parseSDKSettings(getVFS(), Args, getDriver());

  // The OS and the version can be specified using the -target argument.
  std::optional<DarwinPlatform> OSTarget =
      getDeploymentTargetFromTargetArg(Args, getTriple(), getDriver());
  if (OSTarget) {
    // Disallow mixing -target and -mtargetos=.
    if (const auto *MTargetOSArg = Args.getLastArg(options::OPT_mtargetos_EQ)) {
      std::string TargetArgStr = OSTarget->getAsString(Args, Opts);
      std::string MTargetOSArgStr = MTargetOSArg->getAsString(Args);
      getDriver().Diag(diag::err_drv_cannot_mix_options)
          << TargetArgStr << MTargetOSArgStr;
    }
    std::optional<DarwinPlatform> OSVersionArgTarget =
        getDeploymentTargetFromOSVersionArg(Args, getDriver());
    if (OSVersionArgTarget) {
      unsigned TargetMajor, TargetMinor, TargetMicro;
      bool TargetExtra;
      unsigned ArgMajor, ArgMinor, ArgMicro;
      bool ArgExtra;
      if (OSTarget->getPlatform() != OSVersionArgTarget->getPlatform() ||
          (Driver::GetReleaseVersion(OSTarget->getOSVersion(), TargetMajor,
                                     TargetMinor, TargetMicro, TargetExtra) &&
           Driver::GetReleaseVersion(OSVersionArgTarget->getOSVersion(),
                                     ArgMajor, ArgMinor, ArgMicro, ArgExtra) &&
           (llvm::VersionTuple(TargetMajor, TargetMinor, TargetMicro) !=
                llvm::VersionTuple(ArgMajor, ArgMinor, ArgMicro) ||
            TargetExtra != ArgExtra))) {
        // Select the OS version from the -m<os>-version-min argument when
        // the -target does not include an OS version.
        if (OSTarget->getPlatform() == OSVersionArgTarget->getPlatform() &&
            !OSTarget->hasOSVersion()) {
          OSTarget->setOSVersion(OSVersionArgTarget->getOSVersion());
        } else {
          // Warn about -m<os>-version-min that doesn't match the OS version
          // that's specified in the target.
          std::string OSVersionArg =
              OSVersionArgTarget->getAsString(Args, Opts);
          std::string TargetArg = OSTarget->getAsString(Args, Opts);
          getDriver().Diag(neverc::diag::warn_drv_overriding_option)
              << OSVersionArg << TargetArg;
        }
      }
    }
  } else if ((OSTarget =
                  getDeploymentTargetFromMTargetOSArg(Args, getDriver()))) {
    // The OS target can be specified using the -mtargetos= argument.
    // Disallow mixing -mtargetos= and -m<os>version-min=.
    std::optional<DarwinPlatform> OSVersionArgTarget =
        getDeploymentTargetFromOSVersionArg(Args, getDriver());
    if (OSVersionArgTarget) {
      std::string MTargetOSArgStr = OSTarget->getAsString(Args, Opts);
      std::string OSVersionArgStr = OSVersionArgTarget->getAsString(Args, Opts);
      getDriver().Diag(diag::err_drv_cannot_mix_options)
          << MTargetOSArgStr << OSVersionArgStr;
    }
  } else {
    // The OS target can be specified using the -m<os>version-min argument.
    OSTarget = getDeploymentTargetFromOSVersionArg(Args, getDriver());
    // If no deployment target was specified on the command line, check for
    // environment defines.
    if (!OSTarget) {
      OSTarget =
          getDeploymentTargetFromEnvironmentVariables(getDriver(), getTriple());
      if (OSTarget) {
        // Don't infer simulator from the arch when the SDK is also specified.
        std::optional<DarwinPlatform> SDKTarget =
            inferDeploymentTargetFromSDK(Args, SDKInfo);
        if (SDKTarget)
          OSTarget->setEnvironment(SDKTarget->getEnvironment());
      }
    }
    // If there is no command-line argument to specify the Target version and
    // no environment variable defined, see if we can set the default based
    // on -isysroot using SDKSettings.json if it exists.
    if (!OSTarget) {
      OSTarget = inferDeploymentTargetFromSDK(Args, SDKInfo);
      /// If the target was successfully constructed from the SDK path, try to
      /// infer the SDK info if the SDK doesn't have it.
      if (OSTarget && !SDKInfo)
        SDKInfo = OSTarget->inferSDKInfo();
    }
    // If no OS targets have been specified, try to guess platform from -target
    // or arch name and compute the version from the triple.
    if (!OSTarget)
      OSTarget =
          inferDeploymentTargetFromArch(Args, *this, getTriple(), getDriver());
  }

  assert(OSTarget && "Unable to infer Darwin variant");
  OSTarget->addOSVersionMinArgument(Args, Opts);
  DarwinPlatformKind Platform = OSTarget->getPlatform();

  unsigned Major, Minor, Micro;
  bool HadExtra;
  // The major version should not be over this number.
  const unsigned MajorVersionLimit = 1000;
  if (Platform == MacOS) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major < 10 || Major >= MajorVersionLimit || Minor >= 100 ||
        Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
  } else if (Platform == IPhoneOS) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major >= MajorVersionLimit || Minor >= 100 || Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
  } else
    llvm_unreachable("unknown kind of Darwin platform");

  DarwinEnvironmentKind Environment = OSTarget->getEnvironment();
  // Recognize iOS targets with an x86 architecture as the iOS simulator.
  if (Environment == NativeEnvironment && Platform != MacOS &&
      OSTarget->canInferSimulatorFromArch() && getTriple().isX86())
    Environment = Simulator;

  setTarget(Platform, Environment, Major, Minor, Micro);

  if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
    llvm::StringRef SDK = getSDKName(A->getValue());
    if (SDK.size() > 0) {
      size_t StartVer = SDK.find_first_of("0123456789");
      llvm::StringRef SDKName = SDK.slice(0, StartVer);
      // Cross-triple syntax-only checks often reuse the host macOS SDK; warning
      // is noise when not producing object code.
      if (!Args.hasArg(options::OPT_fsyntax_only) &&
          !SDKName.starts_with(getPlatformFamily()) &&
          !dropSDKNamePrefix(SDKName).starts_with(getPlatformFamily()))
        getDriver().Diag(diag::warn_incompatible_sysroot)
            << SDKName << getPlatformFamily();
    }
  }
}

// Returns the effective sysroot from either -isysroot or --sysroot.
llvm::SmallString<128>
DarwinNeverC::GetEffectiveSysroot(const llvm::opt::ArgList &DriverArgs) const {
  llvm::SmallString<128> Path("/");
  if (DriverArgs.hasArg(options::OPT_isysroot))
    Path = DriverArgs.getLastArgValue(options::OPT_isysroot);
  else if (!getDriver().SysRoot.empty())
    Path = getDriver().SysRoot;
  return Path;
}

// ===----------------------------------------------------------------------===
// System includes & search paths
// ===----------------------------------------------------------------------===

void DarwinNeverC::AddNeverCSystemIncludeArgs(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &FrontendArgs) const {
  const Driver &D = getDriver();

  llvm::SmallString<128> Sysroot = GetEffectiveSysroot(DriverArgs);

  bool NoStdInc = DriverArgs.hasArg(options::OPT_nostdinc);
  bool NoStdlibInc = DriverArgs.hasArg(options::OPT_nostdlibinc);
  bool NoBuiltinInc = DriverArgs.hasFlag(
      options::OPT_nobuiltininc, options::OPT_ibuiltininc, /*Default=*/false);
  bool ForceBuiltinInc = DriverArgs.hasFlag(
      options::OPT_ibuiltininc, options::OPT_nobuiltininc, /*Default=*/false);

  // Add <sysroot>/usr/local/include
  if (!NoStdInc && !NoStdlibInc) {
    llvm::SmallString<128> P(Sysroot);
    llvm::sys::path::append(P, "usr", "local", "include");
    addSystemInclude(DriverArgs, FrontendArgs, P);
  }

  // Add the NeverC builtin headers (<resource>/include)
  if (!(NoStdInc && !ForceBuiltinInc) && !NoBuiltinInc) {
    llvm::SmallString<128> P(D.ResourceDir);
    llvm::sys::path::append(P, "include");
    addSystemInclude(DriverArgs, FrontendArgs, P);
  }

  if (NoStdInc || NoStdlibInc)
    return;

  // Check for configure-time C include directories.
  llvm::StringRef CIncludeDirs(C_INCLUDE_DIRS);
  if (!CIncludeDirs.empty()) {
    llvm::SmallVector<llvm::StringRef, 5> dirs;
    CIncludeDirs.split(dirs, ":");
    for (llvm::StringRef dir : dirs) {
      llvm::StringRef Prefix =
          llvm::sys::path::is_absolute(dir) ? "" : llvm::StringRef(Sysroot);
      addExternCSystemInclude(DriverArgs, FrontendArgs, Prefix + dir);
    }
  } else {
    // Otherwise, add <sysroot>/usr/include.
    llvm::SmallString<128> P(Sysroot);
    llvm::sys::path::append(P, "usr", "include");
    addExternCSystemInclude(DriverArgs, FrontendArgs, P.str());
  }
}

DerivedArgList *MachO::TranslateArgs(const DerivedArgList &Args,
                                     llvm::StringRef BoundArch) const {
  DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());
  const OptTable &Opts = getDriver().getOpts();

  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT_Xarch__)) {
      // Skip this argument unless the architecture matches either the toolchain
      // triple arch, or the arch being bound.
      llvm::StringRef XarchArch = A->getValue(0);
      if (!(XarchArch == getArchName() ||
            (!BoundArch.empty() && XarchArch == BoundArch)))
        continue;

      Arg *OriginalArg = A;
      TranslateXarchArgs(Args, A, DAL);

      // Linker input arguments require custom handling. The problem is that we
      // have already constructed the phase actions, so we can not treat them as
      // "input arguments".
      if (A->getOption().hasFlag(options::LinkerInput)) {
        for (const char *Value : A->getValues()) {
          DAL->AddSeparateArg(
              OriginalArg, Opts.getOption(options::OPT_Zlinker_input), Value);
        }
        continue;
      }
    }

    // Sob. These is strictly gcc compatible for the time being. Apple
    // gcc translates options twice, which means that self-expanding
    // options add duplicates.
    switch ((options::ID)A->getOption().getID()) {
    default:
      DAL->append(A);
      break;

    case options::OPT_mkernel:
      DAL->append(A);
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_static));
      break;

    case options::OPT_dependency_file:
      DAL->AddSeparateArg(A, Opts.getOption(options::OPT_MF), A->getValue());
      break;

    case options::OPT_gfull:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_g_Flag));
      DAL->AddFlagArg(
          A, Opts.getOption(options::OPT_fno_eliminate_unused_debug_symbols));
      break;

    case options::OPT_gused:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_g_Flag));
      DAL->AddFlagArg(
          A, Opts.getOption(options::OPT_feliminate_unused_debug_symbols));
      break;

    case options::OPT_shared:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_dynamiclib));
      break;
    }
  }

  // Add the arch options based on the particular spelling of -arch, to match
  // how the driver works.
  if (!BoundArch.empty()) {
    llvm::StringRef Name = BoundArch;
    const Option MArch = Opts.getOption(neverc::driver::options::OPT_march_EQ);

    if (Name == "x86_64")
      DAL->AddFlagArg(nullptr, Opts.getOption(options::OPT_m64));
    else if (Name == "arm64")
      DAL->AddJoinedArg(nullptr, MArch, "armv8-a");
  }

  return DAL;
}

void MachO::AddLinkRuntimeLibArgs(const ArgList &Args, ArgStringList &CmdArgs,
                                  bool ForceLinkBuiltinRT) const {
  llvm::SmallString<32> CompilerRT = llvm::StringRef("hard");
  CompilerRT += "_pic";

  AddLinkRuntimeLib(Args, CmdArgs, CompilerRT, RLO_IsEmbedded);
}

void Darwin::addNeverCTargetOptions(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &FrontendArgs) const {
  // `-target-sdk-version=` is shared between compiler and assembler frontends;
  // forward to the assembler path so both frontends see identical SDK metadata.
  addNeverCFrontendAsTargetOptions(DriverArgs, FrontendArgs);
}

void Darwin::addNeverCFrontendAsTargetOptions(
    const llvm::opt::ArgList &Args,
    llvm::opt::ArgStringList &FrontendAsArgs) const {
  if (SDKInfo) {
    std::string Arg;
    llvm::raw_string_ostream OS(Arg);
    OS << "-target-sdk-version=" << SDKInfo->getVersion();
    FrontendAsArgs.push_back(Args.MakeArgString(OS.str()));
  }
}

DerivedArgList *Darwin::TranslateArgs(const DerivedArgList &Args,
                                      llvm::StringRef BoundArch) const {
  // First get the generic Apple args, before moving onto Darwin-specific ones.
  DerivedArgList *DAL = MachO::TranslateArgs(Args, BoundArch);

  // If no architecture is bound, none of the translations here are relevant.
  if (BoundArch.empty())
    return DAL;

  // Add an explicit version min argument for the deployment target. We do this
  // after argument translation because -Xarch_ arguments may add a version min
  // argument.
  AddDeploymentTarget(*DAL);

  // For iOS 6, undo the translation to add -static for -mkernel.
  if (isTargetIOSBased() && !isIPhoneOSVersionLT(6, 0)) {
    for (ArgList::iterator it = DAL->begin(), ie = DAL->end(); it != ie;) {
      Arg *A = *it;
      ++it;
      if (A->getOption().getID() != options::OPT_mkernel)
        continue;
      assert(it != ie && "unexpected argument translation");
      A = *it;
      assert(A->getOption().getID() == options::OPT_static &&
             "missing expected -static argument");
      *it = nullptr;
      ++it;
    }
  }

  return DAL;
}

ToolChain::UnwindTableLevel
MachO::getDefaultUnwindTableLevel(const ArgList &Args) const {
  // Unwind tables are not emitted if -fno-exceptions is supplied (except when
  // targeting x86_64).
  if (getArch() == llvm::Triple::x86_64 ||
      Args.hasFlag(options::OPT_fexceptions, options::OPT_fno_exceptions, true))
    return getArch() == llvm::Triple::aarch64 ? UnwindTableLevel::Synchronous
                                              : UnwindTableLevel::Asynchronous;

  return UnwindTableLevel::None;
}

bool MachO::UseDwarfDebugFlags() const { return false; }

std::string MachO::GetGlobalDebugPathRemapping() const { return {}; }

llvm::ExceptionHandling Darwin::GetExceptionModel(const ArgList &Args) const {
  return llvm::ExceptionHandling::None;
}

void Darwin::addMinVersionArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  llvm::VersionTuple TargetVersion = getTripleTargetVersion();

  if (isTargetIOSSimulator())
    CmdArgs.push_back("-ios_simulator_version_min");
  else if (isTargetIOSBased())
    CmdArgs.push_back("-iphoneos_version_min");
  else {
    assert(isTargetMacOS() && "unexpected target");
    CmdArgs.push_back("-macosx_version_min");
  }

  llvm::VersionTuple MinTgtVers =
      getEffectiveTriple().getMinimumSupportedOSVersion();
  if (!MinTgtVers.empty() && MinTgtVers > TargetVersion)
    TargetVersion = MinTgtVers;
  CmdArgs.push_back(Args.MakeArgString(TargetVersion.getAsString()));
}

namespace {
const char *getPlatformName(Darwin::DarwinPlatformKind Platform) {
  switch (Platform) {
  case Darwin::MacOS:
    return "macos";
  case Darwin::IPhoneOS:
    return "ios";
  }
  llvm_unreachable("invalid platform");
}
} // namespace

void Darwin::addPlatformVersionArgs(const llvm::opt::ArgList &Args,
                                    llvm::opt::ArgStringList &CmdArgs) const {
  // -platform_version <platform> <target_version> <sdk_version>
  // Both the target and SDK version support only up to 3 components.
  CmdArgs.push_back("-platform_version");
  std::string PlatformName = getPlatformName(TargetPlatform);
  if (TargetEnvironment == Darwin::Simulator)
    PlatformName += "-simulator";
  CmdArgs.push_back(Args.MakeArgString(PlatformName));
  llvm::VersionTuple TargetVersion = getTripleTargetVersion().withoutBuild();
  llvm::VersionTuple MinTgtVers =
      getEffectiveTriple().getMinimumSupportedOSVersion();
  if (!MinTgtVers.empty() && MinTgtVers > TargetVersion)
    TargetVersion = MinTgtVers;
  CmdArgs.push_back(Args.MakeArgString(TargetVersion.getAsString()));

  if (SDKInfo) {
    llvm::VersionTuple SDKVersion = SDKInfo->getVersion().withoutBuild();
    if (!SDKVersion.hasMinor())
      SDKVersion = llvm::VersionTuple(SDKVersion.getMajor(), 0);
    CmdArgs.push_back(Args.MakeArgString(SDKVersion.getAsString()));
  } else {
    // Use the deployment target as the SDK version when SDKSettings.json is
    // unavailable. An empty 0.0.0 here would confuse runtime checks that
    // depend on a sensible linked SDK version.
    CmdArgs.push_back(Args.MakeArgString(TargetVersion.getAsString()));
  }
}

void Darwin::populatePlatformVersionConfig(
    const llvm::opt::ArgList &Args, ::linker::LinkerDriverConfig &Cfg) const {
  std::string PlatformName = getPlatformName(TargetPlatform);
  if (TargetEnvironment == Darwin::Simulator)
    PlatformName += "-simulator";
  Cfg.platformName = std::move(PlatformName);

  llvm::VersionTuple TargetVersion = getTripleTargetVersion().withoutBuild();
  llvm::VersionTuple MinTgtVers =
      getEffectiveTriple().getMinimumSupportedOSVersion();
  if (!MinTgtVers.empty() && MinTgtVers > TargetVersion)
    TargetVersion = MinTgtVers;
  Cfg.platformMinVersion = TargetVersion.getAsString().str().str();

  if (SDKInfo) {
    llvm::VersionTuple SDKVersion = SDKInfo->getVersion().withoutBuild();
    if (!SDKVersion.hasMinor())
      SDKVersion = llvm::VersionTuple(SDKVersion.getMajor(), 0);
    Cfg.platformSdkVersion = SDKVersion.getAsString().str().str();
  } else {
    Cfg.platformSdkVersion = TargetVersion.getAsString().str().str();
  }
}

// Add additional link args for the -dynamiclib option.

namespace {
void addDynamicLibLinkArgs(const Darwin &D, const ArgList &Args,
                           ArgStringList &CmdArgs) {
  // Derived from darwin_dylib1 spec.
  if (D.isTargetIPhoneOS()) {
    if (D.isIPhoneOSVersionLT(3, 1))
      CmdArgs.push_back("-ldylib1.o");
    return;
  }

  if (!D.isTargetMacOS())
    return;
  if (D.isMacosxVersionLT(10, 5))
    CmdArgs.push_back("-ldylib1.o");
  else if (D.isMacosxVersionLT(10, 6))
    CmdArgs.push_back("-ldylib1.10.5.o");
}

// Add additional link args for the -bundle option.
void addBundleLinkArgs(const Darwin &D, const ArgList &Args,
                       ArgStringList &CmdArgs) {
  if (Args.hasArg(options::OPT_static))
    return;
  // Derived from darwin_bundle1 spec.
  if ((D.isTargetIPhoneOS() && D.isIPhoneOSVersionLT(3, 1)) ||
      (D.isTargetMacOS() && D.isMacosxVersionLT(10, 6)))
    CmdArgs.push_back("-lbundle1.o");
}

void addDefaultCRTLinkArgs(const Darwin &D, const ArgList &Args,
                           ArgStringList &CmdArgs) {
  // Derived from darwin_crt1 spec.
  if (D.isTargetIPhoneOS()) {
    if (D.getArch() == llvm::Triple::aarch64)
      ; // iOS does not need any crt1 files for arm64
    else if (D.isIPhoneOSVersionLT(3, 1))
      CmdArgs.push_back("-lcrt1.o");
    else if (D.isIPhoneOSVersionLT(6, 0))
      CmdArgs.push_back("-lcrt1.3.1.o");
    return;
  }

  if (!D.isTargetMacOS())
    return;
  if (D.isMacosxVersionLT(10, 5))
    CmdArgs.push_back("-lcrt1.o");
  else if (D.isMacosxVersionLT(10, 6))
    CmdArgs.push_back("-lcrt1.10.5.o");
  else if (D.isMacosxVersionLT(10, 8))
    CmdArgs.push_back("-lcrt1.10.6.o");
  // darwin_crt2 spec is empty.
}
} // namespace

void Darwin::addStartObjectFileArgs(const ArgList &Args,
                                    ArgStringList &CmdArgs) const {
  // Derived from startfile spec.
  if (Args.hasArg(options::OPT_dynamiclib))
    addDynamicLibLinkArgs(*this, Args, CmdArgs);
  else if (Args.hasArg(options::OPT_bundle))
    addBundleLinkArgs(*this, Args, CmdArgs);
  else if (Args.hasArg(options::OPT_static) ||
           Args.hasArg(options::OPT_object) ||
           Args.hasArg(options::OPT_preload))
    CmdArgs.push_back("-lcrt0.o");
  else
    addDefaultCRTLinkArgs(*this, Args, CmdArgs);

  if (isTargetMacOS() && Args.hasArg(options::OPT_shared_libgcc) &&
      isMacosxVersionLT(10, 5)) {
    const char *Str = Args.MakeArgString(GetFilePath("crt3.o"));
    CmdArgs.push_back(Str);
  }
}

void Darwin::printVerboseInfo(llvm::raw_ostream &OS) const {}
