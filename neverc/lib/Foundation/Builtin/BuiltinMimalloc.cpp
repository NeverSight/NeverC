#include "neverc/Foundation/Builtin/BuiltinMimalloc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"

using namespace neverc;

// Per-OS+arch bitcode blobs produced by bin2c.py during the bootstrap build.
// Initial builds use empty placeholders (len == 0).
#include "BuiltinMimallocBitcode_linux_x64.h"
#include "BuiltinMimallocBitcode_linux_arm64.h"
#include "BuiltinMimallocBitcode_darwin_x64.h"
#include "BuiltinMimallocBitcode_darwin_arm64.h"
#include "BuiltinMimallocBitcode_win_x64.h"
#include "BuiltinMimallocBitcode_win_arm64.h"

namespace {

const unsigned char *lookupBlob(const llvm::Triple &TT, unsigned &Len) {
  const bool IsArm64 = TT.getArch() == llvm::Triple::aarch64;
  const bool IsX64 = TT.getArch() == llvm::Triple::x86_64;

  switch (TT.getOS()) {
  case llvm::Triple::Linux:
    if (IsArm64) {
      Len = kMimallocBitcode_linux_arm64_len;
      return kMimallocBitcode_linux_arm64;
    }
    if (IsX64) {
      Len = kMimallocBitcode_linux_x64_len;
      return kMimallocBitcode_linux_x64;
    }
    break;
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
    if (IsArm64) {
      Len = kMimallocBitcode_darwin_arm64_len;
      return kMimallocBitcode_darwin_arm64;
    }
    if (IsX64) {
      Len = kMimallocBitcode_darwin_x64_len;
      return kMimallocBitcode_darwin_x64;
    }
    break;
  case llvm::Triple::Win32:
    if (IsArm64) {
      Len = kMimallocBitcode_win_arm64_len;
      return kMimallocBitcode_win_arm64;
    }
    if (IsX64) {
      Len = kMimallocBitcode_win_x64_len;
      return kMimallocBitcode_win_x64;
    }
    break;
  default:
    break;
  }
  Len = 0;
  return nullptr;
}

} // namespace

bool BuiltinMimalloc::isSupported(const llvm::Triple &TT) {
  unsigned Len;
  return lookupBlob(TT, Len) != nullptr;
}

llvm::StringRef BuiltinMimalloc::getEmbeddedBitcode(const llvm::Triple &TT) {
  unsigned Len;
  const unsigned char *Data = lookupBlob(TT, Len);
  if (!Data || Len == 0)
    return {};
  return llvm::StringRef(reinterpret_cast<const char *>(Data), Len);
}
