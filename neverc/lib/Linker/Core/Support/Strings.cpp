//===----------------------------------------------------------------------===//
//
//  Strings — small text utilities + the quoted/glob name-pattern matchers
//  consumed by options such as `--exclude-libs` and `--retain-symbols-file`.
//
//  Two related pieces sit side by side because both deal with `StringRef`s
//  fished out of the input argument list:
//
//    * Lightweight text helpers — hex parsing, C-identifier validation,
//      whole-buffer save (`parseHex`, `isValidCIdentifier`, `saveBuffer`).
//    * Pattern-based name matching — `SingleStringMatcher`,
//      `StringMatcher`.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Support/Strings.h"

#include "Linker/Core/Runtime/Diagnostic.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/GlobPattern.h"

using namespace llvm;
using namespace linker;

//===----------------------------------------------------------------------===//
// Lightweight text helpers
//===----------------------------------------------------------------------===//

SmallVector<uint8_t, 0> linker::parseHex(StringRef s) {
  SmallVector<uint8_t, 0> hex;
  while (!s.empty()) {
    StringRef b = s.substr(0, 2);
    s = s.substr(2);
    uint8_t h;
    if (!to_integer(b, h, 16)) {
      error("not a hexadecimal value: " + b);
      return {};
    }
    hex.push_back(h);
  }
  return hex;
}

bool linker::isValidCIdentifier(StringRef s) {
  return !s.empty() && !isDigit(s[0]) &&
         llvm::all_of(s, [](char c) { return isAlnum(c) || c == '_'; });
}

void linker::saveBuffer(StringRef buffer, const Twine &path) {
  std::error_code ec;
  raw_fd_ostream os(path.str(), ec, sys::fs::OpenFlags::OF_None);
  if (ec)
    error("cannot create " + path + ": " + ec.message());
  os << buffer;
}

//===----------------------------------------------------------------------===//
// Quoted / glob name-pattern matchers
//===----------------------------------------------------------------------===//

SingleStringMatcher::SingleStringMatcher(StringRef Pattern) {
  if (Pattern.size() > 2 && Pattern.starts_with("\"") &&
      Pattern.ends_with("\"")) {
    ExactMatch = true;
    ExactPattern = Pattern.substr(1, Pattern.size() - 2);
  } else {
    Expected<GlobPattern> Glob = GlobPattern::create(Pattern);
    if (!Glob) {
      error(toString(Glob.takeError()) + ": " + Pattern);
      return;
    }
    ExactMatch = false;
    GlobPatternMatcher = *Glob;
  }
}

bool SingleStringMatcher::match(StringRef s) const {
  return ExactMatch ? (ExactPattern == s) : GlobPatternMatcher.match(s);
}

bool StringMatcher::match(StringRef s) const {
  for (const SingleStringMatcher &pat : patterns)
    if (pat.match(s))
      return true;
  return false;
}
