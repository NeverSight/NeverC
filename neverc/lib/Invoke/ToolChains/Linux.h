#ifndef NEVERC_LIB_DRIVER_TOOLCHAINS_LINUX_H
#define NEVERC_LIB_DRIVER_TOOLCHAINS_LINUX_H

#include "Gnu.h"
#include "neverc/Invoke/ToolChain.h"

namespace neverc {
namespace driver {
namespace toolchains {

class LLVM_LIBRARY_VISIBILITY Linux : public Generic_ELF {
public:
  Linux(const Driver &D, const llvm::Triple &Triple,
        const llvm::opt::ArgList &Args);

  bool HasNativeLLVMSupport() const override;

  std::string getMultiarchTriple(const Driver &D,
                                 const llvm::Triple &TargetTriple,
                                 llvm::StringRef SysRoot) const override;

  void AddNeverCSystemIncludeArgs(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &FrontendArgs) const override;
  RuntimeLibType GetDefaultRuntimeLibType() const override;
  unsigned GetDefaultDwarfVersion() const override;
  bool
  IsAArch64OutlineAtomicsDefault(const llvm::opt::ArgList &Args) const override;
  bool IsMathErrnoDefault() const override;
  std::string computeSysRoot() const override;

  std::string getDynamicLinker(const llvm::opt::ArgList &Args) const override;

  void addExtraOpts(llvm::opt::ArgStringList &CmdArgs) const override;

  std::vector<std::string> ExtraOpts;

  llvm::DenormalMode getDefaultDenormalModeForType(
      const llvm::opt::ArgList &DriverArgs, const JobAction &JA,
      const llvm::fltSemantics *FPType = nullptr) const override;

protected:
  Tool *buildLinker() const override;
  Tool *buildStaticLibTool() const override;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace neverc

#endif // NEVERC_LIB_DRIVER_TOOLCHAINS_LINUX_H
