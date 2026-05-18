#ifndef NEVERC_SHELLCODE_TARGETDESC_H
#define NEVERC_SHELLCODE_TARGETDESC_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {
class Triple;
}

namespace neverc {
namespace shellcode {

enum class ShellcodeOS : uint8_t {
  Unknown = 0,
  Darwin,
  Linux,
  Android,
  Windows,
};

enum class ShellcodeArch : uint8_t {
  Unknown = 0,
  AArch64,
  X86_64,
};

enum class ObjectFormat : uint8_t {
  Unknown = 0,
  MachO,
  ELF,
  COFF,
};

enum class SyscallABI : uint8_t {
  None = 0,
  DarwinSvc80,
  DarwinSyscall,
  LinuxSvc0,
  LinuxSyscall,
  WindowsPEB,
};

enum class ExecutionLevel : uint8_t {
  User = 0,
  Kernel,
};

enum class KernelImportABI : uint8_t {
  None = 0,
  WindowsKernelResolverShim,
  LinuxKallsymsShim,
  DarwinXNUKextShim,
};

struct TargetDesc {
  ShellcodeOS OS = ShellcodeOS::Unknown;
  ShellcodeArch Arch = ShellcodeArch::Unknown;
  ObjectFormat Format = ObjectFormat::Unknown;
  SyscallABI Syscall = SyscallABI::None;

  llvm::StringRef TextSectionName = "";
  llvm::StringRef AsmTemplate = "";
  llvm::StringRef SyscallNumberReg = "";
  uint64_t SyscallNumberMask = 0;
  llvm::StringRef SyscallRetReg = "";
  const llvm::StringRef *ArgRegs = nullptr;
  unsigned NumArgRegs = 0;
  bool PCRelByDefault = true;

  llvm::StringRef TCBReadAsm = "";
  llvm::StringRef TCBReadConstraint = "";

  const char *const *DriverInjectFlags = nullptr;
  const char *const *KernelInjectFlags = nullptr;

  ExecutionLevel Level = ExecutionLevel::User;
  KernelImportABI KernelImport = KernelImportABI::None;
};

TargetDesc describeTriple(const llvm::Triple &TT,
                          ExecutionLevel Level = ExecutionLevel::User);

llvm::StringRef osName(ShellcodeOS OS);
llvm::StringRef archName(ShellcodeArch Arch);
llvm::StringRef formatName(ObjectFormat Fmt);
llvm::StringRef triplePrettyName(const TargetDesc &D);

}
}

#endif
