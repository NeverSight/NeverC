#include "neverc/Shellcode/Import/SyscallTables.h"
#include "llvm/ADT/ArrayRef.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

struct Syscall {
  StringRef Name;
  uint64_t Number;
};

const Syscall kDarwinTable[] = {
#define NEVERC_SYSCALL(name, number) {#name, number},
#include "neverc/Shellcode/Tables/Syscalls_Darwin.def"
#include "neverc/Shellcode/Tables/UserExtra_Syscalls_Darwin.def"
#undef NEVERC_SYSCALL
};

const Syscall kLinuxArm64Table[] = {
#define NEVERC_SYSCALL(name, number) {#name, number},
#include "neverc/Shellcode/Tables/Syscalls_LinuxArm64.def"
#include "neverc/Shellcode/Tables/UserExtra_Syscalls_LinuxArm64.def"
#undef NEVERC_SYSCALL
};

const Syscall kLinuxX86_64Table[] = {
#define NEVERC_SYSCALL(name, number) {#name, number},
#include "neverc/Shellcode/Tables/Syscalls_LinuxX86_64.def"
#include "neverc/Shellcode/Tables/UserExtra_Syscalls_LinuxX86_64.def"
#undef NEVERC_SYSCALL
};

StringRef stripLeadingUnderscore(StringRef S) {
  return S.starts_with("_") ? S.drop_front(1) : S;
}

SyscallLookup lookup(ArrayRef<Syscall> Table, StringRef Name) {
  StringRef Bare = stripLeadingUnderscore(Name);
  for (const Syscall &S : Table)
    if (S.Name == Name || S.Name == Bare)
      return {true, S.Number};
  return {};
}

} // namespace

SyscallLookup lookupSyscall(const TargetDesc &T, StringRef Name) {
  switch (T.Syscall) {
  case SyscallABI::DarwinSvc80:
  case SyscallABI::DarwinSyscall:
    return lookup(kDarwinTable, Name);
  case SyscallABI::LinuxSvc0:
    return lookup(kLinuxArm64Table, Name);
  case SyscallABI::LinuxSyscall:
    return lookup(kLinuxX86_64Table, Name);
  case SyscallABI::WindowsPEB:
  case SyscallABI::None:
    return {};
  }
  return {};
}

namespace {

constexpr int64_t kAtFdCwd[] = {-100};
constexpr int64_t kZero[] = {0};
constexpr int64_t kAtRemovedir[] = {0x200};
constexpr int64_t kAtSymlinkNofollow[] = {0x100};

constexpr CompatSlot kSymlinkatTmpl[] = {
    {0, true},     // target (user arg 0)
    {-100, false}, // AT_FDCWD
    {1, true},     // path (user arg 1)
};

constexpr CompatSlot kLinkatTmpl[] = {
    {-100, false}, // AT_FDCWD (olddirfd)
    {0, true},     // old (user arg 0)
    {-100, false}, // AT_FDCWD (newdirfd)
    {1, true},     // new (user arg 1)
    {0, false},    // flags = 0
};

constexpr CompatSlot kRenameatTmpl[] = {
    {-100, false}, // AT_FDCWD (olddirfd)
    {0, true},     // old (user arg 0)
    {-100, false}, // AT_FDCWD (newdirfd)
    {1, true},     // new (user arg 1)
};

constexpr CompatSlot kSeteuidTmpl[] = {
    {-1, false}, // ruid = -1 (unchanged)
    {0, true},   // euid = user arg 0
    {-1, false}, // suid = -1 (unchanged)
};

constexpr CompatSlot kSetegidTmpl[] = {
    {-1, false}, // rgid = -1 (unchanged)
    {0, true},   // egid = user arg 0
    {-1, false}, // sgid = -1 (unchanged)
};

struct CompatEntry {
  StringRef Name;
  StringRef TargetName;
  const int64_t *PrependData;
  size_t PrependLen;
  const int64_t *AppendData;
  size_t AppendLen;
  const CompatSlot *TmplData;
  size_t TmplLen;
};

#define SIMPLE(name, tgt, pre, prelen, app, applen)                            \
  {name, tgt, pre, prelen, app, applen, nullptr, 0}
#define TMPL(name, tgt, tmpl)                                                  \
  {name, tgt, nullptr, 0, nullptr, 0, tmpl, sizeof(tmpl) / sizeof(tmpl[0])}

constexpr CompatEntry kLinuxArm64Compat[] = {
    SIMPLE("open", "openat", kAtFdCwd, 1, kZero, 1),
    SIMPLE("unlink", "unlinkat", kAtFdCwd, 1, kZero, 1),
    SIMPLE("rmdir", "unlinkat", kAtFdCwd, 1, kAtRemovedir, 1),
    SIMPLE("access", "faccessat", kAtFdCwd, 1, nullptr, 0),
    SIMPLE("readlink", "readlinkat", kAtFdCwd, 1, nullptr, 0),
    SIMPLE("stat", "fstatat", kAtFdCwd, 1, kZero, 1),
    SIMPLE("lstat", "fstatat", kAtFdCwd, 1, kAtSymlinkNofollow, 1),
    SIMPLE("mkdir", "mkdirat", kAtFdCwd, 1, nullptr, 0),
    SIMPLE("chmod", "fchmodat", kAtFdCwd, 1, nullptr, 0),
    SIMPLE("chown", "fchownat", kAtFdCwd, 1, kZero, 1),
    SIMPLE("dup2", "dup3", nullptr, 0, kZero, 1),
    SIMPLE("pipe", "pipe2", nullptr, 0, kZero, 1),
    TMPL("symlink", "symlinkat", kSymlinkatTmpl),
    TMPL("link", "linkat", kLinkatTmpl),
    TMPL("rename", "renameat", kRenameatTmpl),
    TMPL("seteuid", "setresuid", kSeteuidTmpl),
    TMPL("setegid", "setresgid", kSetegidTmpl),
};

#undef SIMPLE
#undef TMPL

constexpr CompatEntry kLinuxX86_64Compat[] = {
    {
        "seteuid",
        "setresuid",
        nullptr,
        0,
        nullptr,
        0,
        kSeteuidTmpl,
        sizeof(kSeteuidTmpl) / sizeof(kSeteuidTmpl[0]),
    },
    {
        "setegid",
        "setresgid",
        nullptr,
        0,
        nullptr,
        0,
        kSetegidTmpl,
        sizeof(kSetegidTmpl) / sizeof(kSetegidTmpl[0]),
    },
};

} // namespace

SyscallCompat lookupSyscallCompat(const TargetDesc &T, StringRef Name) {
  ArrayRef<CompatEntry> Table;
  switch (T.Syscall) {
  case SyscallABI::LinuxSvc0:
    Table = kLinuxArm64Compat;
    break;
  case SyscallABI::LinuxSyscall:
    Table = kLinuxX86_64Compat;
    break;
  default:
    return {};
  }
  StringRef Bare = stripLeadingUnderscore(Name);
  for (const CompatEntry &E : Table) {
    if (E.Name == Name || E.Name == Bare)
      return {true, E.TargetName,
              ArrayRef<int64_t>(E.PrependData, E.PrependLen),
              ArrayRef<int64_t>(E.AppendData, E.AppendLen),
              ArrayRef<CompatSlot>(E.TmplData, E.TmplLen)};
  }
  return {};
}

bool isLikelySyscallName(StringRef Name) {
  StringRef Bare = stripLeadingUnderscore(Name);
  static constexpr StringRef kCandidates[] = {
#define NEVERC_SYSCALL_LIKELY(name) StringRef(#name),
#include "neverc/Shellcode/Tables/SyscallLikelyNames.def"
#include "neverc/Shellcode/Tables/UserExtra_SyscallLikelyNames.def"
#undef NEVERC_SYSCALL_LIKELY
  };
  for (StringRef C : kCandidates)
    if (Bare == C)
      return true;
  return false;
}

} // namespace shellcode
} // namespace neverc
