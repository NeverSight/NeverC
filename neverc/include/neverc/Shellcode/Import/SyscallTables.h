#ifndef NEVERC_SHELLCODE_SYSCALLTABLES_H
#define NEVERC_SHELLCODE_SYSCALLTABLES_H

#include "neverc/Shellcode/Pipeline/TargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace neverc {
namespace shellcode {

struct SyscallLookup {
  bool Found = false;
  uint64_t Number = 0;
};

SyscallLookup lookupSyscall(const TargetDesc &T, llvm::StringRef Name);

struct CompatSlot {
  int64_t Value;
  bool IsUserArg;
};

struct SyscallCompat {
  bool Found = false;
  llvm::StringRef TargetName;
  llvm::ArrayRef<int64_t> PrependArgs;
  llvm::ArrayRef<int64_t> AppendArgs;
  llvm::ArrayRef<CompatSlot> Template;
};

SyscallCompat lookupSyscallCompat(const TargetDesc &T, llvm::StringRef Name);
bool isLikelySyscallName(llvm::StringRef Name);

}
}

#endif
