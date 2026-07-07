#ifndef NEVERC_DYNCODE_MMAPABI_H
#define NEVERC_DYNCODE_MMAPABI_H

#include "neverc/DynCode/Pipeline/TargetDesc.h"

namespace neverc {
namespace dyncode {
namespace MmapABI {

inline constexpr int ProtRW = 3; // PROT_READ(0x1) | PROT_WRITE(0x2)
inline constexpr int PrivateAnonLinux = 0x22;  // MAP_PRIVATE(0x02) | MAP_ANONYMOUS(0x20)
inline constexpr int PrivateAnonDarwin = 0x1002; // MAP_PRIVATE(0x02) | MAP_ANONYMOUS(0x1000)

inline int anonFlags(DynCodeOS OS) {
  return (OS == DynCodeOS::Darwin) ? PrivateAnonDarwin : PrivateAnonLinux;
}

} // namespace MmapABI
} // namespace dyncode
} // namespace neverc

#endif
