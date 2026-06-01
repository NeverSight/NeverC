#ifndef NEVERC_BUILD_PARSER_H
#define NEVERC_BUILD_PARSER_H

#include "neverc/Build/AST.h"
#include "neverc/Build/Lexer.h"
#include <memory>
#include <string>

namespace neverc {
namespace build {

class Parser {
public:
  explicit Parser(const std::string &Filename,
                  std::vector<MakefileLine> Lines);

  std::unique_ptr<MakefileAST> parse();

  bool hadError() const { return HadError; }
  const std::string &errorMessage() const { return ErrorMsg; }

private:
  std::unique_ptr<Statement> parseLine(size_t &Idx);
  std::unique_ptr<VarAssign> parseAssignment(const std::string &Line,
                                              unsigned LineNo);
  std::unique_ptr<Rule> parseRule(const std::string &Line, unsigned LineNo,
                                   size_t &Idx);
  std::unique_ptr<Conditional> parseConditional(const std::string &Line,
                                                  unsigned LineNo,
                                                  size_t &Idx);
  std::unique_ptr<Include> parseInclude(const std::string &Line,
                                         unsigned LineNo);
  std::unique_ptr<DefineBlock> parseDefine(const std::string &Line,
                                            unsigned LineNo, size_t &Idx);
  std::unique_ptr<ExportDirective> parseExport(const std::string &Line,
                                                unsigned LineNo);

  void error(unsigned LineNo, const std::string &Msg);

  bool isAssignment(const std::string &Line) const;
  bool isRule(const std::string &Line) const;

  std::string Filename;
  std::vector<MakefileLine> Lines;
  bool HadError = false;
  std::string ErrorMsg;
};

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_PARSER_H
