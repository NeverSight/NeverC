#ifndef NEVERC_FOUNDATION_HEADERINCLUDEFORMATKIND_H
#define NEVERC_FOUNDATION_HEADERINCLUDEFORMATKIND_H
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include <utility>

namespace neverc {
enum HeaderIncludeFormatKind { HIFMT_None, HIFMT_Textual, HIFMT_JSON };

enum HeaderIncludeFilteringKind { HIFIL_None, HIFIL_Only_Direct_System };

inline HeaderIncludeFormatKind
stringToHeaderIncludeFormatKind(const char *Str) {
  return llvm::StringSwitch<HeaderIncludeFormatKind>(Str)
      .Case("textual", HIFMT_Textual)
      .Case("json", HIFMT_JSON)
      .Default(HIFMT_None);
}

inline bool stringToHeaderIncludeFiltering(const char *Str,
                                           HeaderIncludeFilteringKind &Kind) {
  std::pair<bool, HeaderIncludeFilteringKind> P =
      llvm::StringSwitch<std::pair<bool, HeaderIncludeFilteringKind>>(Str)
          .Case("none", {true, HIFIL_None})
          .Case("only-direct-system", {true, HIFIL_Only_Direct_System})
          .Default({false, HIFIL_None});
  Kind = P.second;
  return P.first;
}

inline const char *headerIncludeFormatKindToString(HeaderIncludeFormatKind K) {
  switch (K) {
  case HIFMT_None:
    llvm_unreachable("unexpected format kind");
  case HIFMT_Textual:
    return "textual";
  case HIFMT_JSON:
    return "json";
  }
  llvm_unreachable("Unknown HeaderIncludeFormatKind enum");
}

inline const char *
headerIncludeFilteringKindToString(HeaderIncludeFilteringKind K) {
  switch (K) {
  case HIFIL_None:
    return "none";
  case HIFIL_Only_Direct_System:
    return "only-direct-system";
  }
  llvm_unreachable("Unknown HeaderIncludeFilteringKind enum");
}

} // end namespace neverc

#endif // NEVERC_FOUNDATION_HEADERINCLUDEFORMATKIND_H
