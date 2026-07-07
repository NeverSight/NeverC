#ifndef NEVERC_DYNCODE_MIRPREPPASS_H
#define NEVERC_DYNCODE_MIRPREPPASS_H

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"

namespace llvm {
class FunctionPass;
}

namespace neverc {
namespace dyncode {

llvm::FunctionPass *createDynCodeMIRPrepPass(const DynCodeOptions &Opts);

}
}

#endif
