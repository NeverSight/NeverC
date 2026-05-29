#include "neverc/Shellcode/Pipeline/TargetDesc.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

const StringRef kArm64ArgRegs[] = {"x0", "x1", "x2", "x3",
                                   "x4", "x5", "x6", "x7"};

const StringRef kX86_64LinuxArgRegs[] = {"rdi", "rsi", "rdx",
                                         "r10", "r8",  "r9"};

const char *const kX86_64UnixInjectFlags[] = {
#define NEVERC_SC_FLAG(flag) flag,
#include "neverc/Shellcode/Tables/TargetInjectFlags_Unix_X86_64.def"
#include "neverc/Shellcode/Tables/UserExtra_TargetInjectFlags_Unix_X86_64.def"
#undef NEVERC_SC_FLAG
    nullptr,
};

const char *const kWindowsX86_64InjectFlags[] = {
#define NEVERC_SC_FLAG(flag) flag,
#include "neverc/Shellcode/Tables/TargetInjectFlags_Windows_X86_64.def"
#include "neverc/Shellcode/Tables/UserExtra_TargetInjectFlags_Windows_X86_64.def"
#undef NEVERC_SC_FLAG
    nullptr,
};

const char *const kAArch64UnixInjectFlags[] = {
#define NEVERC_SC_FLAG(flag) flag,
#include "neverc/Shellcode/Tables/TargetInjectFlags_Unix_AArch64.def"
#include "neverc/Shellcode/Tables/UserExtra_TargetInjectFlags_Unix_AArch64.def"
#undef NEVERC_SC_FLAG
    nullptr,
};

const char *const kWindowsAArch64InjectFlags[] = {
#define NEVERC_SC_FLAG(flag) flag,
#include "neverc/Shellcode/Tables/TargetInjectFlags_Windows_AArch64.def"
#include "neverc/Shellcode/Tables/UserExtra_TargetInjectFlags_Windows_AArch64.def"
#undef NEVERC_SC_FLAG
    nullptr,
};

const char *const kX86_64UnixKernelInjectFlags[] = {
#define NEVERC_SC_FLAG(flag) flag,
#include "neverc/Shellcode/Tables/TargetKernelFlags_Unix_X86_64.def"
#include "neverc/Shellcode/Tables/UserExtra_TargetKernelFlags_Unix_X86_64.def"
#undef NEVERC_SC_FLAG
    nullptr,
};

const char *const kX86_64WindowsKernelInjectFlags[] = {
#define NEVERC_SC_FLAG(flag) flag,
#include "neverc/Shellcode/Tables/TargetKernelFlags_Windows_X86_64.def"
#include "neverc/Shellcode/Tables/UserExtra_TargetKernelFlags_Windows_X86_64.def"
#undef NEVERC_SC_FLAG
    nullptr,
};

const char *const kAArch64UnixKernelInjectFlags[] = {
#define NEVERC_SC_FLAG(flag) flag,
#include "neverc/Shellcode/Tables/TargetKernelFlags_Unix_AArch64.def"
#include "neverc/Shellcode/Tables/UserExtra_TargetKernelFlags_Unix_AArch64.def"
#undef NEVERC_SC_FLAG
    nullptr,
};

const char *const kAArch64WindowsKernelInjectFlags[] = {
#define NEVERC_SC_FLAG(flag) flag,
#include "neverc/Shellcode/Tables/TargetKernelFlags_Windows_AArch64.def"
#include "neverc/Shellcode/Tables/UserExtra_TargetKernelFlags_Windows_AArch64.def"
#undef NEVERC_SC_FLAG
    nullptr,
};

constexpr StringRef kTCBReadAsmX86_64Win = "movq %gs:0x60, $0";
constexpr StringRef kTCBReadAsmAArch64Win = "ldr $0, [x18, #0x60]";
constexpr StringRef kTCBReadConstraintDefault = "=r";

} // namespace

llvm::StringRef osName(ShellcodeOS OS) {
  switch (OS) {
  case ShellcodeOS::Darwin:
    return "darwin";
  case ShellcodeOS::Linux:
    return "linux";
  case ShellcodeOS::Android:
    return "android";
  case ShellcodeOS::Windows:
    return "windows";
  default:
    return "unknown";
  }
}

llvm::StringRef archName(ShellcodeArch Arch) {
  switch (Arch) {
  case ShellcodeArch::AArch64:
    return "arm64";
  case ShellcodeArch::X86_64:
    return "x86_64";
  default:
    return "unknown";
  }
}

llvm::StringRef formatName(ObjectFormat Fmt) {
  switch (Fmt) {
  case ObjectFormat::MachO:
    return "Mach-O";
  case ObjectFormat::ELF:
    return "ELF";
  case ObjectFormat::COFF:
    return "COFF";
  default:
    return "unknown";
  }
}

llvm::StringRef triplePrettyName(const TargetDesc &D) {
  if (D.OS == ShellcodeOS::Darwin && D.Arch == ShellcodeArch::AArch64)
    return "arm64-apple-macos";
  if (D.OS == ShellcodeOS::Darwin && D.Arch == ShellcodeArch::X86_64)
    return "x86_64-apple-macos";
  if (D.OS == ShellcodeOS::Linux && D.Arch == ShellcodeArch::AArch64)
    return "aarch64-linux-gnu";
  if (D.OS == ShellcodeOS::Linux && D.Arch == ShellcodeArch::X86_64)
    return "x86_64-linux-gnu";
  if (D.OS == ShellcodeOS::Android && D.Arch == ShellcodeArch::AArch64)
    return "aarch64-linux-android";
  if (D.OS == ShellcodeOS::Android && D.Arch == ShellcodeArch::X86_64)
    return "x86_64-linux-android";
  if (D.OS == ShellcodeOS::Windows && D.Arch == ShellcodeArch::AArch64)
    return "aarch64-pc-windows-msvc";
  if (D.OS == ShellcodeOS::Windows && D.Arch == ShellcodeArch::X86_64)
    return "x86_64-pc-windows-msvc";
  return "unknown";
}

TargetDesc describeTriple(const Triple &TT, ExecutionLevel Level) {
  TargetDesc D;
  D.Level = Level;

  switch (TT.getArch()) {
  case Triple::aarch64:
    D.Arch = ShellcodeArch::AArch64;
    break;
  case Triple::x86_64:
    D.Arch = ShellcodeArch::X86_64;
    break;
  default:
    return TargetDesc{};
  }

  auto pickKernelInject = [&](const char *const *X86,
                              const char *const *ARM64) -> const char *const * {
    return D.Arch == ShellcodeArch::X86_64 ? X86 : ARM64;
  };

  if (TT.isOSDarwin()) {
    D.OS = ShellcodeOS::Darwin;
    D.Format = ObjectFormat::MachO;
    D.TextSectionName = "__text";
    if (D.Arch == ShellcodeArch::AArch64) {
      D.Syscall = SyscallABI::DarwinSvc80;
      D.AsmTemplate = "svc #0x80";
      D.SyscallNumberReg = "x16";
      D.SyscallRetReg = "x0";
      D.ArgRegs = kArm64ArgRegs;
      D.NumArgRegs = 8;
      D.DriverInjectFlags = kAArch64UnixInjectFlags;
    } else if (D.Arch == ShellcodeArch::X86_64) {
      D.Syscall = SyscallABI::DarwinSyscall;
      D.AsmTemplate = "syscall";
      D.SyscallNumberReg = "rax";
      D.SyscallRetReg = "rax";
      D.ArgRegs = kX86_64LinuxArgRegs;
      D.NumArgRegs = 6;
      D.SyscallNumberMask = 0x2000000;
      D.DriverInjectFlags = kX86_64UnixInjectFlags;
    }
    if (Level == ExecutionLevel::Kernel) {
      D.Syscall = SyscallABI::None;
      D.AsmTemplate = "";
      D.SyscallNumberReg = "";
      D.SyscallRetReg = "";
      D.ArgRegs = nullptr;
      D.NumArgRegs = 0;
      D.KernelImport = KernelImportABI::DarwinXNUKextShim;
      D.KernelInjectFlags = pickKernelInject(kX86_64UnixKernelInjectFlags,
                                             kAArch64UnixKernelInjectFlags);
    }
    return D;
  }

  if (TT.isAndroid()) {
    D.OS = ShellcodeOS::Android;
    D.Format = ObjectFormat::ELF;
    D.TextSectionName = ".text";
    if (D.Arch == ShellcodeArch::AArch64) {
      D.Syscall = SyscallABI::LinuxSvc0;
      D.AsmTemplate = "svc #0";
      D.SyscallNumberReg = "x8";
      D.SyscallRetReg = "x0";
      D.ArgRegs = kArm64ArgRegs;
      D.NumArgRegs = 8;
      D.DriverInjectFlags = kAArch64UnixInjectFlags;
    } else if (D.Arch == ShellcodeArch::X86_64) {
      D.Syscall = SyscallABI::LinuxSyscall;
      D.AsmTemplate = "syscall";
      D.SyscallNumberReg = "rax";
      D.SyscallRetReg = "rax";
      D.ArgRegs = kX86_64LinuxArgRegs;
      D.NumArgRegs = 6;
      D.DriverInjectFlags = kX86_64UnixInjectFlags;
    }
    if (Level == ExecutionLevel::Kernel) {
      D.Syscall = SyscallABI::None;
      D.AsmTemplate = "";
      D.SyscallNumberReg = "";
      D.SyscallRetReg = "";
      D.ArgRegs = nullptr;
      D.NumArgRegs = 0;
      D.KernelImport = KernelImportABI::LinuxKallsymsShim;
      D.KernelInjectFlags = pickKernelInject(kX86_64UnixKernelInjectFlags,
                                             kAArch64UnixKernelInjectFlags);
    }
    return D;
  }

  if (TT.isOSLinux()) {
    D.OS = ShellcodeOS::Linux;
    D.Format = ObjectFormat::ELF;
    D.TextSectionName = ".text";
    if (D.Arch == ShellcodeArch::AArch64) {
      D.Syscall = SyscallABI::LinuxSvc0;
      D.AsmTemplate = "svc #0";
      D.SyscallNumberReg = "x8";
      D.SyscallRetReg = "x0";
      D.ArgRegs = kArm64ArgRegs;
      D.NumArgRegs = 8;
      D.DriverInjectFlags = kAArch64UnixInjectFlags;
    } else if (D.Arch == ShellcodeArch::X86_64) {
      D.Syscall = SyscallABI::LinuxSyscall;
      D.AsmTemplate = "syscall";
      D.SyscallNumberReg = "rax";
      D.SyscallRetReg = "rax";
      D.ArgRegs = kX86_64LinuxArgRegs;
      D.NumArgRegs = 6;
      D.DriverInjectFlags = kX86_64UnixInjectFlags;
    }
    if (Level == ExecutionLevel::Kernel) {
      D.Syscall = SyscallABI::None;
      D.AsmTemplate = "";
      D.SyscallNumberReg = "";
      D.SyscallRetReg = "";
      D.ArgRegs = nullptr;
      D.NumArgRegs = 0;
      D.KernelImport = KernelImportABI::LinuxKallsymsShim;
      D.KernelInjectFlags = pickKernelInject(kX86_64UnixKernelInjectFlags,
                                             kAArch64UnixKernelInjectFlags);
    }
    return D;
  }

  if (TT.isOSWindows()) {
    if (D.Arch != ShellcodeArch::X86_64 && D.Arch != ShellcodeArch::AArch64)
      return TargetDesc{};
    D.OS = ShellcodeOS::Windows;
    D.Format = ObjectFormat::COFF;
    D.TextSectionName = ".text";
    D.Syscall = SyscallABI::WindowsPEB;
    D.TCBReadConstraint = kTCBReadConstraintDefault;
    if (D.Arch == ShellcodeArch::AArch64) {
      D.ArgRegs = kArm64ArgRegs;
      D.NumArgRegs = 8;
      D.TCBReadAsm = kTCBReadAsmAArch64Win;
      D.DriverInjectFlags = kWindowsAArch64InjectFlags;
    } else {
      D.TCBReadAsm = kTCBReadAsmX86_64Win;
      D.DriverInjectFlags = kWindowsX86_64InjectFlags;
    }
    if (Level == ExecutionLevel::Kernel) {
      D.Syscall = SyscallABI::None;
      D.TCBReadAsm = "";
      D.TCBReadConstraint = "";
      D.ArgRegs = nullptr;
      D.NumArgRegs = 0;
      D.KernelImport = KernelImportABI::WindowsKernelResolverShim;
      D.KernelInjectFlags = pickKernelInject(kX86_64WindowsKernelInjectFlags,
                                             kAArch64WindowsKernelInjectFlags);
    }
    return D;
  }

  return TargetDesc{};
}

} // namespace shellcode
} // namespace neverc
