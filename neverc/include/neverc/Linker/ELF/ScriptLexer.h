#ifndef LINKER_ELF_SCRIPT_LEXER_H
#define LINKER_ELF_SCRIPT_LEXER_H

#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <vector>

namespace linker::elf {

class ScriptLexer {
public:
  explicit ScriptLexer(MemoryBufferRef mb);

  void setError(const Twine &msg);
  void tokenize(MemoryBufferRef mb);
  StringRef skipSpace(StringRef s);
  bool atEOF();
  StringRef next();
  StringRef peek();
  StringRef peek2();
  void skip();
  bool consume(StringRef tok);
  void expect(StringRef expect);
  bool consumeLabel(StringRef tok);
  std::string getCurrentLocation();

  std::vector<MemoryBufferRef> mbs;
  std::vector<StringRef> tokens;
  bool inExpr = false;
  size_t pos = 0;

  size_t lastLineNumber = 0;
  size_t lastLineNumberOffset = 0;

protected:
  MemoryBufferRef getCurrentMB();

private:
  void maybeSplitExpr();
  StringRef getLine();
  size_t getLineNumber();
  size_t getColumnNumber();
};

} // namespace linker::elf

#endif
