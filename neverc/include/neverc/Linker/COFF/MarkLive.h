#ifndef LINKER_COFF_MARKLIVE_H
#define LINKER_COFF_MARKLIVE_H

#include "Linker/Core/Support/LlvmAliases.h"

namespace linker::coff {

class COFFLinkerContext;

void markLive(COFFLinkerContext &ctx);

} // namespace linker::coff

#endif //  LINKER_COFF_MARKLIVE_H
