#include "neverc/Invoke/Tool.h"
#include "neverc/Invoke/InputInfo.h"

using namespace neverc::driver;

Tool::Tool(const char *_Name, const char *_ShortName, const ToolChain &TC)
    : Name(_Name), ShortName(_ShortName), TheToolChain(TC) {}

Tool::~Tool() {}

void Tool::ConstructJobMultipleOutputs(Compilation &C, const JobAction &JA,
                                       const InputInfoList &Outputs,
                                       const InputInfoList &Inputs,
                                       const llvm::opt::ArgList &TCArgs,
                                       const char *LinkingOutput) const {
  assert(Outputs.size() == 1 && "Expected only one output by default!");
  ConstructJob(C, JA, Outputs.front(), Inputs, TCArgs, LinkingOutput);
}
