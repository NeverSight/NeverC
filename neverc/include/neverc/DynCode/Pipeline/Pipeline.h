#ifndef NEVERC_DYNCODE_PIPELINE_H
#define NEVERC_DYNCODE_PIPELINE_H

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <memory>

namespace llvm {
class MachinePipelineHooks;
class PassBuilder;
}

namespace neverc {
namespace dyncode {

void registerDynCodePasses(llvm::PassBuilder &PB,
                             const DynCodeOptions &Opts);
void registerDynCodeMachinePasses(const DynCodeOptions &Opts);
std::shared_ptr<llvm::MachinePipelineHooks>
createDynCodeMachinePipelineHooks(const DynCodeOptions &Opts);
const DynCodeOptions &getCurrentDynCodeOptions();

void applyPostExtractObfuscationInterpose(llvm::SmallVectorImpl<uint8_t> &Bytes);
void applyPostFinalizeObfuscationInterpose(llvm::SmallVectorImpl<uint8_t> &Bytes);

}
}

#endif
