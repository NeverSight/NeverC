#include "neverc/Foundation/Builtin/BuiltinMimalloc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"

using namespace neverc;

// Per-OS bitcode blobs produced by bin2c.py during the bootstrap build.
// Initial builds use empty placeholders (len == 0).
#include "BuiltinMimallocBitcode_linux.h"
#include "BuiltinMimallocBitcode_darwin.h"
#include "BuiltinMimallocBitcode_win.h"

bool BuiltinMimalloc::isSupported(llvm::Triple::OSType OS) {
  switch (OS) {
  case llvm::Triple::Linux:
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
  case llvm::Triple::Win32:
    return true;
  default:
    return false;
  }
}

llvm::StringRef BuiltinMimalloc::getEmbeddedBitcode(llvm::Triple::OSType OS) {
  switch (OS) {
  case llvm::Triple::Linux:
    if (kMimallocBitcode_linux_len == 0)
      return {};
    return llvm::StringRef(
        reinterpret_cast<const char *>(kMimallocBitcode_linux),
        kMimallocBitcode_linux_len);

  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
    if (kMimallocBitcode_darwin_len == 0)
      return {};
    return llvm::StringRef(
        reinterpret_cast<const char *>(kMimallocBitcode_darwin),
        kMimallocBitcode_darwin_len);

  case llvm::Triple::Win32:
    if (kMimallocBitcode_win_len == 0)
      return {};
    return llvm::StringRef(
        reinterpret_cast<const char *>(kMimallocBitcode_win),
        kMimallocBitcode_win_len);

  default:
    return {};
  }
}
