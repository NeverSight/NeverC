#include "neverc/Foundation/Builtin/BuiltinMimalloc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"

using namespace neverc;

// Per-OS bitcode blobs produced by bin2c.py during the bootstrap build.
// Initial builds use empty placeholders (len == 0).
#include "BuiltinMimallocBitcode_linux.h"
#include "BuiltinMimallocBitcode_darwin.h"
#include "BuiltinMimallocBitcode_win.h"

namespace {
const unsigned char *lookupBlob(llvm::Triple::OSType OS, unsigned &Len) {
  switch (OS) {
  case llvm::Triple::Linux:
    Len = kMimallocBitcode_linux_len;
    return kMimallocBitcode_linux;
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
    Len = kMimallocBitcode_darwin_len;
    return kMimallocBitcode_darwin;
  case llvm::Triple::Win32:
    Len = kMimallocBitcode_win_len;
    return kMimallocBitcode_win;
  default:
    Len = 0;
    return nullptr;
  }
}
} // namespace

bool BuiltinMimalloc::isSupported(llvm::Triple::OSType OS) {
  unsigned Len;
  return lookupBlob(OS, Len) != nullptr;
}

llvm::StringRef BuiltinMimalloc::getEmbeddedBitcode(llvm::Triple::OSType OS) {
  unsigned Len;
  const unsigned char *Data = lookupBlob(OS, Len);
  if (!Data || Len == 0)
    return {};
  return llvm::StringRef(reinterpret_cast<const char *>(Data), Len);
}
