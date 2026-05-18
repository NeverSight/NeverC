#ifndef NEVERC_SHELLCODE_SYMBOLNAMES_H
#define NEVERC_SHELLCODE_SYMBOLNAMES_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace shellcode {
namespace SymbolNames {

inline llvm::StringRef stripObjectLeadingUnderscore(llvm::StringRef Name) {
  if (!Name.starts_with("_"))
    return Name;
  if (Name.starts_with("___"))
    return Name.drop_front(1);
  if (Name.starts_with("__"))
    return Name;
  return Name.drop_front(1);
}

inline llvm::StringRef canonicalRuntimeName(llvm::StringRef Name) {
  Name = stripObjectLeadingUnderscore(Name);
  auto Dot = Name.find('.');
  if (Dot != llvm::StringRef::npos)
    Name = Name.take_front(Dot);
  return Name;
}

}
}
}

#endif
