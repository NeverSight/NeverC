#ifndef NEVERC_BUILD_STRINGUTILS_H
#define NEVERC_BUILD_STRINGUTILS_H

#include "llvm/ADT/StringRef.h"

#include <string>
#include <vector>

namespace neverc {
namespace build {

inline std::string trim(llvm::StringRef S) {
  return S.trim(" \t").str();
}

std::vector<std::string> splitWords(llvm::StringRef S);

std::vector<std::string> splitWordsRespectingVarRefs(llvm::StringRef S);

std::string joinWords(const std::vector<std::string> &Words,
                      llvm::StringRef Sep = " ");

bool matchPattern(llvm::StringRef Pattern, llvm::StringRef Text);

std::string stemFromPattern(llvm::StringRef Pattern, llvm::StringRef Text);

std::string firstWord(llvm::StringRef Line);

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_STRINGUTILS_H
