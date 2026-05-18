#ifndef LLVM_SUPPORT_LOCALE_H
#define LLVM_SUPPORT_LOCALE_H

#include "llvm/Support/Unicode.h"

namespace llvm {

namespace sys {
namespace locale {

inline int columnWidth(StringRef Text) {
  return llvm::sys::unicode::columnWidthUTF8(Text);
}

inline bool isPrint(int UCS) { return llvm::sys::unicode::isPrintable(UCS); }

} // namespace locale
} // namespace sys
} // namespace llvm

#endif // LLVM_SUPPORT_LOCALE_H
