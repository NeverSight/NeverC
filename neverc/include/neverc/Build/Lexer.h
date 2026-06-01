#ifndef NEVERC_BUILD_LEXER_H
#define NEVERC_BUILD_LEXER_H

#include <string>
#include <vector>

namespace neverc {
namespace build {

struct MakefileLine {
  enum Kind {
    Assignment,
    Rule,
    RecipeLine,
    Directive,
    Comment,
    Empty,
    Raw,
  };
  Kind Type = Raw;
  std::string Content;
  std::string OriginalContent;
  unsigned LineNumber = 0;
};

class Lexer {
public:
  explicit Lexer(const std::string &Filename, const std::string &Content);

  std::vector<MakefileLine> lex();

  bool hadError() const { return HadError; }
  const std::string &errorMessage() const { return ErrorMsg; }

private:
  void joinContinuationLines();
  MakefileLine classifyLine(const std::string &Line, unsigned LineNo,
                             bool InRecipe);

  std::string Filename;
  std::string Input;
  bool HadError = false;
  std::string ErrorMsg;
};

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_LEXER_H
