//===----------------------------------------------------------------------===//
//
//  LlvmAliases — the one header every linker TU pulls in to promote the
//  handful of `llvm::` types and cast operators into the `linker::`
//  namespace.  Keeping the `using` aliases in a single place means the
//  rest of the tree can mention `StringRef`, `SmallVector`, `Twine`, ...
//  without reaching for `llvm/ADT/*` or an explicit `llvm::` qualifier.
//
//  The header is deliberately minimal: no function declarations, no
//  runtime symbols, only type aliases + the `std::hash<StringRef>`
//  specialisation the linker's unordered containers rely on.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_SUPPORT_LLVMALIASES_H
#define LINKER_CORE_SUPPORT_LLVMALIASES_H

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

namespace llvm {
class Error;
class MemoryBuffer;
class MemoryBufferRef;
class Twine;
class raw_ostream;
template <typename T> class ArrayRef;
template <typename T> class ErrorOr;
template <typename T> class Expected;
template <typename T> class MutableArrayRef;
template <typename T, unsigned N> class SmallVector;
template <unsigned InternalLen> class SmallString;
} // namespace llvm

namespace linker {

// LLVM casting operators, pulled into `linker::` for convenience.
using llvm::cast;
using llvm::cast_or_null;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::isa;

// ADT / Error / IO types referenced throughout the fork.
using llvm::ArrayRef;
using llvm::Error;
using llvm::ErrorOr;
using llvm::Expected;
using llvm::MemoryBuffer;
using llvm::MemoryBufferRef;
using llvm::MutableArrayRef;
using llvm::raw_ostream;
using llvm::SmallString;
using llvm::SmallVector;
using llvm::StringRef;
using llvm::Twine;

} // namespace linker

// `std::unordered_map<StringRef, ...>` and friends need a `std::hash`
// specialisation; LLVM's `DenseMap` provides its own DenseMapInfo already.
namespace std {
template <> struct hash<llvm::StringRef> {
  size_t operator()(const llvm::StringRef &s) const {
    return llvm::hash_value(s);
  }
};
} // namespace std

#endif // LINKER_CORE_SUPPORT_LLVMALIASES_H
