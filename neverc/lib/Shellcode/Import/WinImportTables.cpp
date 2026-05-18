#include "neverc/Shellcode/Import/WinImportTables.h"
#include "llvm/ADT/ArrayRef.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

struct Entry {
  StringRef Api;
  StringRef Dll;
};

constexpr Entry kTable[] = {
#define NEVERC_WIN32_API(name, dll) {#name, dll},
#include "neverc/Shellcode/Tables/UserExtra_Win32Apis.def"
#include "neverc/Shellcode/Tables/Win32Apis.def"
#undef NEVERC_WIN32_API
};

} // namespace

Win32ApiLookup lookupWin32Api(StringRef Name) {
  for (const Entry &E : kTable)
    if (E.Api == Name)
      return {true, E.Dll};
  return {};
}

bool isLikelyWin32ApiName(StringRef Name) {
  if (Name.empty())
    return false;
  char First = Name.front();
  if (!((First >= 'A' && First <= 'Z') || First == '_'))
    return false;
  for (const Entry &E : kTable)
    if (E.Api == Name)
      return true;
  return false;
}

} // namespace shellcode
} // namespace neverc
