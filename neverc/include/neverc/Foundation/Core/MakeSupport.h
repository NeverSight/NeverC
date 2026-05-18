#ifndef NEVERC_BASIC_MAKESUPPORT_H
#define NEVERC_BASIC_MAKESUPPORT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace neverc {

void quoteMakeTarget(llvm::StringRef Target, llvm::SmallVectorImpl<char> &Res);

} // namespace neverc

#endif // NEVERC_BASIC_MAKESUPPORT_H
