#ifndef NEVERC_SHELLCODE_MMAPABI_H
#define NEVERC_SHELLCODE_MMAPABI_H

#include "neverc/Shellcode/Pipeline/TargetDesc.h"

namespace neverc {
namespace shellcode {
namespace MmapABI {

inline constexpr int ProtRW = 3; // PROT_READ(0x1) | PROT_WRITE(0x2)
inline constexpr int PrivateAnonLinux = 0x22;  // MAP_PRIVATE(0x02) | MAP_ANONYMOUS(0x20)
inline constexpr int PrivateAnonDarwin = 0x1002; // MAP_PRIVATE(0x02) | MAP_ANONYMOUS(0x1000)

inline int anonFlags(ShellcodeOS OS) {
  return (OS == ShellcodeOS::Darwin) ? PrivateAnonDarwin : PrivateAnonLinux;
}

} // namespace MmapABI
} // namespace shellcode
} // namespace neverc

#endif
