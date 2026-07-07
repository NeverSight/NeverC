#ifndef NEVERC_DYNCODE_DYNCODEEXTRACTOR_H
#define NEVERC_DYNCODE_DYNCODEEXTRACTOR_H

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace dyncode {

int extractDynCode(llvm::StringRef InputObj, llvm::StringRef OutputBin,
                     const DynCodeOptions &Opts);

}
}

#endif
