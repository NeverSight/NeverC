#ifndef LINKER_ELF_CALL_GRAPH_SORT_H
#define LINKER_ELF_CALL_GRAPH_SORT_H

#include "llvm/ADT/DenseMap.h"

namespace linker::elf {
class InputSectionBase;

llvm::DenseMap<const InputSectionBase *, int> computeCacheDirectedSortOrder();

llvm::DenseMap<const InputSectionBase *, int> computeCallGraphProfileOrder();
} // namespace linker::elf

#endif
