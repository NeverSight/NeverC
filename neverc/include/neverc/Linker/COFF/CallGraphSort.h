#ifndef LINKER_COFF_CALL_GRAPH_SORT_H
#define LINKER_COFF_CALL_GRAPH_SORT_H

#include "llvm/ADT/DenseMap.h"

namespace linker::coff {
class SectionChunk;
class COFFLinkerContext;

llvm::DenseMap<const SectionChunk *, int>
computeCallGraphProfileOrder(const COFFLinkerContext &ctx);
} // namespace linker::coff

#endif
