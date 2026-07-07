#ifndef NEVERC_DYNCODE_WINIMPORTTABLES_H
#define NEVERC_DYNCODE_WINIMPORTTABLES_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace dyncode {

struct Win32ApiLookup {
  bool Found = false;
  llvm::StringRef Dll;
};

Win32ApiLookup lookupWin32Api(llvm::StringRef Name);
bool isLikelyWin32ApiName(llvm::StringRef Name);

}
}

#endif
