#include "neverc/Foundation/Std/BuiltinStd.h"
#include "neverc/Foundation/Builtin/BuiltinStringNames.h"
#include "neverc/Foundation/Std/StdModule.h"
#include "llvm/TargetParser/Triple.h"

using namespace neverc;

bool BuiltinStd::isStdRuntimeFunction(llvm::StringRef Name) {
  return Name.starts_with(StdModule::FuncPrefix) &&
         !Name.starts_with(BuiltinStringNames::PublicFunctionPrefix);
}

namespace {

/// One embedded translation unit, as laid down by build_std_bitcode.py.
struct StdBitcodeEntry {
  const char *name;
  const unsigned char *data;
  unsigned int len;
};

} // namespace

// Per-OS+arch blobs produced during the bootstrap build; initial builds use
// empty placeholders.  See BuiltinStd.h for why one blob cannot serve every
// target.
#include "BuiltinStdBitcode_darwin_arm64.h"
#include "BuiltinStdBitcode_darwin_x64.h"
#include "BuiltinStdBitcode_linux_arm64.h"
#include "BuiltinStdBitcode_linux_x64.h"
#include "BuiltinStdBitcode_win_arm64.h"
#include "BuiltinStdBitcode_win_x64.h"

namespace {

struct ModuleTable {
  const StdBitcodeEntry *Entries = nullptr;
  unsigned Count = 0;
};

#define NEVERC_STD_TABLE(Tag)                                                  \
  ModuleTable { kStdBitcodeEntries_##Tag, kStdBitcodeEntryCount_##Tag }

ModuleTable lookupTable(const llvm::Triple &TT) {
  const bool IsArm64 = TT.getArch() == llvm::Triple::aarch64;
  if (!IsArm64 && TT.getArch() != llvm::Triple::x86_64)
    return {};

  switch (TT.getOS()) {
  case llvm::Triple::Linux:
    return IsArm64 ? NEVERC_STD_TABLE(linux_arm64) : NEVERC_STD_TABLE(linux_x64);
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
    return IsArm64 ? NEVERC_STD_TABLE(darwin_arm64)
                   : NEVERC_STD_TABLE(darwin_x64);
  case llvm::Triple::Win32:
    return IsArm64 ? NEVERC_STD_TABLE(win_arm64) : NEVERC_STD_TABLE(win_x64);
  default:
    return {};
  }
}

#undef NEVERC_STD_TABLE

} // namespace

unsigned BuiltinStd::getEmbeddedModuleCount(const llvm::Triple &TT) {
  return lookupTable(TT).Count;
}

std::pair<llvm::StringRef, llvm::StringRef>
BuiltinStd::getEmbeddedModule(const llvm::Triple &TT, unsigned Idx) {
  const ModuleTable Table = lookupTable(TT);
  if (Idx >= Table.Count)
    return {};
  const StdBitcodeEntry &E = Table.Entries[Idx];
  if (E.len == 0)
    return {};
  return {llvm::StringRef(E.name),
          llvm::StringRef(reinterpret_cast<const char *>(E.data), E.len)};
}
