#include "neverc/Build/StringUtils.h"

#include <sstream>

namespace neverc {
namespace build {

std::vector<std::string> splitWords(llvm::StringRef S) {
  std::vector<std::string> Words;
  std::istringstream SS(S.str());
  std::string W;
  while (SS >> W)
    Words.push_back(W);
  return Words;
}

std::vector<std::string> splitWordsRespectingVarRefs(llvm::StringRef S) {
  std::vector<std::string> Words;
  size_t I = 0;
  while (I < S.size() && (S[I] == ' ' || S[I] == '\t'))
    ++I;

  size_t Start = I;
  int Depth = 0;
  while (I < S.size()) {
    if (S[I] == '$' && I + 1 < S.size() &&
        (S[I + 1] == '(' || S[I + 1] == '{')) {
      ++Depth;
      ++I;
    } else if (Depth > 0 && (S[I] == ')' || S[I] == '}')) {
      --Depth;
    } else if (Depth == 0 && (S[I] == ' ' || S[I] == '\t')) {
      if (I > Start)
        Words.push_back(S.substr(Start, I - Start).str());
      while (I < S.size() && (S[I] == ' ' || S[I] == '\t'))
        ++I;
      Start = I;
      continue;
    }
    ++I;
  }
  if (I > Start)
    Words.push_back(S.substr(Start, I - Start).str());
  return Words;
}

std::string joinWords(const std::vector<std::string> &Words,
                      llvm::StringRef Sep) {
  std::string R;
  for (size_t I = 0; I < Words.size(); ++I) {
    if (I > 0)
      R += Sep;
    R += Words[I];
  }
  return R;
}

bool matchPattern(llvm::StringRef Pattern, llvm::StringRef Text) {
  size_t PctPos = Pattern.find('%');
  if (PctPos == llvm::StringRef::npos)
    return Pattern == Text;
  llvm::StringRef Prefix = Pattern.substr(0, PctPos);
  llvm::StringRef Suffix = Pattern.substr(PctPos + 1);
  if (Text.size() < Prefix.size() + Suffix.size())
    return false;
  return Text.starts_with(Prefix) && Text.ends_with(Suffix);
}

std::string stemFromPattern(llvm::StringRef Pattern, llvm::StringRef Text) {
  size_t PctPos = Pattern.find('%');
  if (PctPos == llvm::StringRef::npos)
    return "";
  size_t PrefixLen = PctPos;
  size_t SuffixLen = Pattern.size() - PctPos - 1;
  return Text.substr(PrefixLen, Text.size() - PrefixLen - SuffixLen).str();
}

std::string firstWord(llvm::StringRef Line) {
  llvm::StringRef Trimmed = Line.ltrim(" \t");
  return Trimmed.substr(0, Trimmed.find_first_of(" \t\n\r")).str();
}

} // namespace build
} // namespace neverc
