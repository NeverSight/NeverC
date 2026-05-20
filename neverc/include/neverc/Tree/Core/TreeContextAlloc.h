#ifndef NEVERC_TREE_ASTCONTEXTALLOCATE_H
#define NEVERC_TREE_ASTCONTEXTALLOCATE_H

#include <cstddef>

namespace neverc {

class TreeContext;

} // namespace neverc

// Defined in TreeContext.h
void *operator new(size_t Bytes, const neverc::TreeContext &C,
                   size_t Alignment = 8);
void *operator new[](size_t Bytes, const neverc::TreeContext &C,
                     size_t Alignment = 8);

// It is good practice to pair new/delete operators.  Also, MSVC gives many
// warnings if a matching delete overload is not declared, even though the
// throw() spec guarantees it will not be implicitly called.
void operator delete(void *Ptr, const neverc::TreeContext &C, size_t);
void operator delete[](void *Ptr, const neverc::TreeContext &C, size_t);

#endif // NEVERC_TREE_ASTCONTEXTALLOCATE_H
