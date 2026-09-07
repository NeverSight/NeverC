#ifndef NEVERC_TRANSLATE_JSON_H
#define NEVERC_TRANSLATE_JSON_H
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/JSON.h"
#include <cstddef>
namespace neverc::translate {
/// Bound recursive parser input before constructing JSON values. Braces inside
/// strings do not count. Syntax beyond balanced nesting remains the parser's
/// job.
inline bool jsonWithinLimits(llvm::StringRef Text, std::size_t MaxBytes,
                             std::size_t MaxDepth) {
  if (Text.size() > MaxBytes)
    return false;
  std::size_t Depth = 0;
  bool InString = false, Escape = false;
  for (char C : Text) {
    if (InString) {
      if (Escape)
        Escape = false;
      else if (C == '\\')
        Escape = true;
      else if (C == '"')
        InString = false;
    } else if (C == '"')
      InString = true;
    else if (C == '{' || C == '[') {
      if (++Depth > MaxDepth)
        return false;
    } else if (C == '}' || C == ']') {
      if (!Depth)
        return false;
      --Depth;
    }
  }
  return !InString && Depth == 0;
}

/// This LLVM fork borrows std::string JSON values. Metadata often outlives an
/// intermediate string expression, so explicitly use its owning constructor.
inline llvm::json::Value jsonString(llvm::StringRef Text) {
  return llvm::json::Value(llvm::SmallString<0>(Text));
}
} // namespace neverc::translate
#endif
