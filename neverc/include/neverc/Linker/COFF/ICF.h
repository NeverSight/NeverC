#ifndef LINKER_COFF_ICF_H
#define LINKER_COFF_ICF_H

#include "Linker/COFF/Config.h"
#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/ADT/ArrayRef.h"

namespace linker::coff {

class COFFLinkerContext;

void doICF(COFFLinkerContext &ctx);

} // namespace linker::coff

#endif
