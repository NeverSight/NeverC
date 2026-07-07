#ifndef NEVERC_DYNCODE_PIPELINE_H
#define NEVERC_DYNCODE_PIPELINE_H

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>

namespace llvm {
class PassBuilder;
class TargetPassConfig;
}

namespace neverc {
namespace dyncode {

void registerDynCodePasses(llvm::PassBuilder &PB,
                             const DynCodeOptions &Opts);
void registerDynCodeMachinePasses(const DynCodeOptions &Opts);
const DynCodeOptions &getCurrentDynCodeOptions();

void applyPostExtractObfuscationInterpose(llvm::SmallVectorImpl<uint8_t> &Bytes);
void applyPostFinalizeObfuscationInterpose(llvm::SmallVectorImpl<uint8_t> &Bytes);

}
}

#endif
