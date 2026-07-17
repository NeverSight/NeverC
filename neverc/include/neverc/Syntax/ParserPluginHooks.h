#ifndef NEVERC_SYNTAX_PARSERPLUGINHOOKS_H
#define NEVERC_SYNTAX_PARSERPLUGINHOOKS_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Scan/Token.h"

namespace neverc {

class Decl;
class Expr;
class ParsedAttributes;
class Parser;
class QualType;
class Stmt;

enum class ParserPluginOutcome {
  NotHandled,
  Handled,
  Error,
};

class ParserPluginHooks {
public:
  virtual ~ParserPluginHooks() = default;

  virtual ParserPluginOutcome parseDeclaration(Parser &Parser,
                                               Decl *&Result) = 0;
  virtual ParserPluginOutcome parseStatement(Parser &Parser, Stmt *&Result) = 0;
  virtual ParserPluginOutcome parseExpression(Parser &Parser,
                                              Expr *&Result) = 0;
  virtual ParserPluginOutcome parseTypeName(Parser &Parser,
                                            QualType &Result) = 0;
  virtual ParserPluginOutcome parseAttribute(Parser &Parser,
                                             ParsedAttributes &Result) = 0;
  virtual ParserPluginOutcome parseKeyword(Parser &Parser, Expr *&Result) = 0;

protected:
  struct CursorState {
    Token CurrentToken;
    SourceLocation PreviousTokenLocation;
    unsigned short ParenCount = 0;
    unsigned short BracketCount = 0;
    unsigned short BraceCount = 0;
  };

  static CursorState saveCursor(Parser &Parser);
  static void commitCursor(Parser &Parser);
  static void restoreCursor(Parser &Parser, const CursorState &State);
  static const Token &peekCursor(const Parser &Parser, unsigned Offset);
  static void consumeCursor(Parser &Parser);
};

} // namespace neverc

#endif
