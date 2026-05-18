//===----------------------------------------------------------------------===//
//
//  Strings — small text utilities + the quoted/glob name-pattern
//  matchers (`SingleStringMatcher`, `StringMatcher`) consumed by options
//  like `--exclude-libs`, `--retain-symbols-file`, etc.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_SUPPORT_STRINGS_H
#define LINKER_CORE_SUPPORT_STRINGS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/GlobPattern.h"
#include <string>
#include <vector>

namespace linker {

llvm::SmallVector<uint8_t, 0> parseHex(llvm::StringRef s);
bool isValidCIdentifier(llvm::StringRef s);

// Dump `buffer` verbatim to `path`.
void saveBuffer(llvm::StringRef buffer, const llvm::Twine &path);

// A single pattern to match against.  A pattern is either a double-quoted
// string (matched exactly after removing the quotes) or a glob pattern in
// the sense of `llvm::GlobPattern`.
class SingleStringMatcher {
public:
  SingleStringMatcher(llvm::StringRef Pattern);

  bool match(llvm::StringRef s) const;

  // Returns true for the trivial pattern "*".
  bool isTrivialMatchAll() const {
    return !ExactMatch && GlobPatternMatcher.isTrivialMatchAll();
  }

private:
  bool ExactMatch;
  llvm::GlobPattern GlobPatternMatcher;
  llvm::StringRef ExactPattern;
};

// Collection of `SingleStringMatcher`s; matches if *any* pattern matches.
class StringMatcher {
private:
  std::vector<SingleStringMatcher> patterns;

public:
  StringMatcher() = default;

  StringMatcher(llvm::StringRef Pattern)
      : patterns({SingleStringMatcher(Pattern)}) {}

  void addPattern(SingleStringMatcher Matcher) { patterns.push_back(Matcher); }

  bool empty() const { return patterns.empty(); }

  bool match(llvm::StringRef s) const;
};

} // namespace linker

#endif // LINKER_CORE_SUPPORT_STRINGS_H
