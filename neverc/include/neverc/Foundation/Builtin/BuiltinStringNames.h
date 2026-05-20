#ifndef NEVERC_FOUNDATION_BUILTINSTRINGNAMES_H
#define NEVERC_FOUNDATION_BUILTINSTRINGNAMES_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace BuiltinStringNames {

inline constexpr llvm::StringLiteral TypeName = "string";
inline constexpr llvm::StringLiteral RecordName = "__neverc_string";
inline constexpr llvm::StringLiteral InternalFunctionPrefix =
    "__neverc_string_";
inline constexpr llvm::StringLiteral PublicFunctionPrefix = "neverc_string_";

inline constexpr llvm::StringLiteral RuntimeFnAttr =
    "neverc-string-runtime";

inline constexpr llvm::StringLiteral WptrCleanupFunctionName =
    "__neverc_wptr_cleanup";

#define NEVERC_BUILTIN_STRING_FN(NameId, Spelling, IsPublic)                   \
  inline constexpr llvm::StringLiteral NameId##FunctionName = Spelling;
#include "neverc/Foundation/Builtin/BuiltinStringRoster.def"
#undef NEVERC_BUILTIN_STRING_FN

namespace detail {
inline constexpr bool startsWithCStr(const char *S, const char *Prefix) {
  for (std::size_t i = 0; Prefix[i] != '\0'; ++i)
    if (S[i] != Prefix[i])
      return false;
  return true;
}
} // namespace detail

#define NEVERC_BUILTIN_STRING_FN(NameId, Spelling, IsPublic)                   \
  static_assert((IsPublic)                                                     \
                    ? detail::startsWithCStr(Spelling, "neverc_string_")       \
                    : detail::startsWithCStr(Spelling, "__neverc_string_"),    \
                "BuiltinStringRoster.def row '" #NameId "' violates the "      \
                "neverc_string_ / __neverc_string_ prefix invariant that "     \
                "BuiltinString::isRuntimeFunctionName depends on.");
#include "neverc/Foundation/Builtin/BuiltinStringRoster.def"
#undef NEVERC_BUILTIN_STRING_FN

} // namespace BuiltinStringNames
} // namespace neverc

#endif
