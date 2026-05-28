#ifndef NEVERC_LIB_DRIVER_TOOLCHAINS_COMMONARGS_H
#define NEVERC_LIB_DRIVER_TOOLCHAINS_COMMONARGS_H

#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/InputInfo.h"
#include "neverc/Invoke/Multilib.h"
#include "neverc/Invoke/Tool.h"
#include "neverc/Invoke/ToolChain.h"
#include "neverc/Linker/Core/Driver/Dispatcher.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"

namespace neverc {
namespace driver {
namespace tools {

void addPathIfExists(const Driver &D, const llvm::Twine &Path,
                     ToolChain::path_list &Paths);

void AddLinkerInputs(const ToolChain &TC, const InputInfoList &Inputs,
                     const llvm::opt::ArgList &Args,
                     llvm::opt::ArgStringList &CmdArgs, const JobAction &JA);

void claimNoWarnArgs(const llvm::opt::ArgList &Args);

void AddRunTimeLibs(const ToolChain &TC, const Driver &D,
                    llvm::opt::ArgStringList &CmdArgs,
                    const llvm::opt::ArgList &Args);

const char *SplitDebugName(const JobAction &JA, const llvm::opt::ArgList &Args,
                           const InputInfo &Input, const InputInfo &Output);

void SplitDebugInfo(const ToolChain &TC, Compilation &C, const Tool &T,
                    const JobAction &JA, const llvm::opt::ArgList &Args,
                    const InputInfo &Output, const char *OutFile);

unsigned ParseFunctionAlignment(const ToolChain &TC,
                                const llvm::opt::ArgList &Args);

void addDebugInfoKind(llvm::opt::ArgStringList &CmdArgs,
                      llvm::codegenoptions::DebugInfoKind DebugInfoKind);

llvm::codegenoptions::DebugInfoKind
debugLevelToInfoKind(const llvm::opt::Arg &A);

// Extract the integer N from a string spelled "-dwarf-N", returning 0
// on mismatch. The llvm::StringRef input (rather than an Arg) allows
// for use by the "-Xassembler" option parser.
unsigned DwarfVersionNum(llvm::StringRef ArgValue);
// Find a DWARF format version option.
// This function is a complementary for DwarfVersionNum().
const llvm::opt::Arg *getDwarfNArg(const llvm::opt::ArgList &Args);
unsigned getDwarfVersion(const ToolChain &TC, const llvm::opt::ArgList &Args);

void addAsNeededOption(llvm::opt::ArgStringList &CmdArgs, bool as_needed);

bool areOptimizationsEnabled(const llvm::opt::ArgList &Args);

void addDirectoryList(const llvm::opt::ArgList &Args,
                      llvm::opt::ArgStringList &CmdArgs, const char *ArgName,
                      const char *EnvVar);

void AddTargetFeature(const llvm::opt::ArgList &Args,
                      std::vector<llvm::StringRef> &Features,
                      llvm::opt::OptSpecifier OnOpt,
                      llvm::opt::OptSpecifier OffOpt,
                      llvm::StringRef FeatureName);

std::string getCPUName(const Driver &D, const llvm::opt::ArgList &Args,
                       const llvm::Triple &T, bool FromAs = false);

void getTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                       const llvm::opt::ArgList &Args,
                       llvm::opt::ArgStringList &CmdArgs, bool ForAS);

void handleTargetFeaturesGroup(const Driver &D, const llvm::Triple &Triple,
                               const llvm::opt::ArgList &Args,
                               std::vector<llvm::StringRef> &Features,
                               llvm::opt::OptSpecifier Group);

llvm::SmallVector<llvm::StringRef>
unifyTargetFeatures(llvm::ArrayRef<llvm::StringRef> Features);

llvm::SmallString<128> getStatsFileName(const llvm::opt::ArgList &Args,
                                        const InputInfo &Output,
                                        const InputInfo &Input,
                                        const Driver &D);

void addMultilibFlag(bool Enabled, const llvm::StringRef Flag,
                     Multilib::flags_list &Flags);

void addX86AlignBranchArgs(const Driver &D, const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs);

void addMachineOutlinerArgs(const Driver &D, const llvm::opt::ArgList &Args,
                            llvm::opt::ArgStringList &CmdArgs,
                            const llvm::Triple &Triple);

void populateLinkerDriverConfig(const ToolChain &TC,
                                const llvm::opt::ArgList &Args,
                                ::linker::LinkerDriverConfig &Cfg);

bool getBundledMsvcSdkRoot(const Driver &D, const llvm::Triple &Triple,
                           llvm::SmallVectorImpl<char> &SdkRoot);

bool getBundledWdkRoot(const Driver &D, const llvm::Triple &Triple,
                       llvm::SmallVectorImpl<char> &WdkRoot);

bool getBundledRuntimeSharedRoot(const Driver &D, llvm::StringRef Sdk,
                                 llvm::SmallVectorImpl<char> &Root);

} // end namespace tools
} // end namespace driver
} // end namespace neverc

neverc::CodeGenOptions::FramePointerKind
getFramePointerKind(const llvm::opt::ArgList &Args, const llvm::Triple &Triple);

#endif // NEVERC_LIB_DRIVER_TOOLCHAINS_COMMONARGS_H
