#ifndef NEVERC_LIB_DRIVER_TOOLCHAINS_NEVERC_H
#define NEVERC_LIB_DRIVER_TOOLCHAINS_NEVERC_H

#include "MSVC.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/Tool.h"
#include "neverc/Invoke/Types.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

namespace neverc {
namespace driver {

namespace tools {

class LLVM_LIBRARY_VISIBILITY NeverC : public Tool {
  // Indicates whether this instance has integrated backend using
  // internal LLVM infrastructure.
  bool HasBackend;

public:
  static const char *getBaseInputName(const llvm::opt::ArgList &Args,
                                      const InputInfo &Input);
  static const char *getBaseInputStem(const llvm::opt::ArgList &Args,
                                      const InputInfoList &Inputs);
  static const char *getDependencyFileName(const llvm::opt::ArgList &Args,
                                           const InputInfoList &Inputs);

private:
  void AddPreprocessingOptions(Compilation &C, const JobAction &JA,
                               const Driver &D, const llvm::opt::ArgList &Args,
                               llvm::opt::ArgStringList &CmdArgs,
                               const InputInfo &Output,
                               const InputInfoList &Inputs) const;

  void RenderTargetOptions(const llvm::Triple &EffectiveTriple,
                           const llvm::opt::ArgList &Args, bool Kernel,
                           llvm::opt::ArgStringList &CmdArgs) const;

  void AddAArch64TargetArgs(const llvm::opt::ArgList &Args,
                            llvm::opt::ArgStringList &CmdArgs) const;
  void AddX86TargetArgs(const llvm::opt::ArgList &Args,
                        llvm::opt::ArgStringList &CmdArgs) const;

  void AddMSVCCompatArgs(Compilation &C, const llvm::opt::ArgList &Args,
                         types::ID InputType,
                         llvm::opt::ArgStringList &CmdArgs) const;

public:
  NeverC(const ToolChain &TC, bool HasIntegratedBackend = true);
  ~NeverC() override;

  bool hasGoodDiagnostics() const override { return true; }
  bool hasIntegratedAssembler() const override { return true; }
  bool hasIntegratedBackend() const override { return HasBackend; }
  bool hasIntegratedCPP() const override { return true; }
  bool canEmitIR() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY NeverCAs : public Tool {
public:
  NeverCAs(const ToolChain &TC)
      : Tool("neverc::as", "neverc integrated assembler", TC) {}
  void AddX86TargetArgs(const llvm::opt::ArgList &Args,
                        llvm::opt::ArgStringList &CmdArgs) const;
  bool hasGoodDiagnostics() const override { return true; }
  bool hasIntegratedAssembler() const override { return false; }
  bool hasIntegratedCPP() const override { return false; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

enum class DwarfFissionKind { None, Split, Single };

DwarfFissionKind getDebugFissionKind(const Driver &D,
                                     const llvm::opt::ArgList &Args,
                                     llvm::opt::Arg *&Arg);

} // end namespace tools

} // end namespace driver
} // end namespace neverc

#endif // NEVERC_LIB_DRIVER_TOOLCHAINS_NEVERC_H
