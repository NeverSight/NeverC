#ifndef NEVERC_INVOKE_TOOL_H
#define NEVERC_INVOKE_TOOL_H

#include "neverc/Invoke/InputInfo.h"

namespace llvm {
namespace opt {
class ArgList;
}
} // namespace llvm

namespace neverc {
namespace driver {

class Compilation;
class JobAction;
class ToolChain;

class Tool {
  const char *Name;

  const char *ShortName;

  const ToolChain &TheToolChain;

public:
  Tool(const char *Name, const char *ShortName, const ToolChain &TC);

public:
  virtual ~Tool();

  const char *getName() const { return Name; }

  const char *getShortName() const { return ShortName; }

  const ToolChain &getToolChain() const { return TheToolChain; }

  virtual bool hasIntegratedAssembler() const { return false; }
  virtual bool hasIntegratedBackend() const { return true; }
  virtual bool canEmitIR() const { return false; }
  virtual bool hasIntegratedCPP() const = 0;
  virtual bool isLinkJob() const { return false; }
  virtual bool isDsymutilJob() const { return false; }

  virtual bool hasGoodDiagnostics() const { return false; }

  virtual void ConstructJob(Compilation &C, const JobAction &JA,
                            const InputInfo &Output,
                            const InputInfoList &Inputs,
                            const llvm::opt::ArgList &TCArgs,
                            const char *LinkingOutput) const = 0;
  virtual void ConstructJobMultipleOutputs(Compilation &C, const JobAction &JA,
                                           const InputInfoList &Outputs,
                                           const InputInfoList &Inputs,
                                           const llvm::opt::ArgList &TCArgs,
                                           const char *LinkingOutput) const;
};

} // end namespace driver
} // end namespace neverc

#endif
