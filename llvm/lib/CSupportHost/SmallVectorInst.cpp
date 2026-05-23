//===-- SmallVectorInst.cpp - explicit SmallVectorBase instantiations ----===//
//
// SmallVector.h uses extern template for SmallVectorBase<uint32_t/uint64_t>.
// One TU must provide the out-of-line member definitions for the linker.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"

namespace llvm {
template class SmallVectorBase<uint32_t>;
#if SIZE_MAX > UINT32_MAX
template class SmallVectorBase<uint64_t>;
#endif
} // namespace llvm
