#ifndef NEVERC_TRANSLATE_CPP_TARGET_H
#define NEVERC_TRANSLATE_CPP_TARGET_H

#include "neverc/Foundation/Target/NativeTargetABI.h"

namespace neverc::translate {

static inline bool isCppTranslationTarget(const llvm::Triple &T) {
  return hasNative64BitABI(T) &&
         (T.isMacOSX() || (T.isOSLinux() && !T.isAndroid()) || T.isOSWindows());
}

} // namespace neverc::translate

#endif
