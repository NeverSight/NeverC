#ifndef NEVERC_INVOKE_UTIL_H
#define NEVERC_INVOKE_UTIL_H

#include "neverc/Linker/Core/Driver/Dispatcher.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace neverc {

namespace driver {
class Action;
class JobAction;

typedef llvm::DenseMap<const JobAction *, const char *> ArgStringMap;

typedef llvm::SmallVector<Action *, 3> ActionList;

using LinkerFlavor = linker::Flavor;

} // end namespace driver
} // end namespace neverc

#endif
