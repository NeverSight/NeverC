#ifndef NEVERC_SCAN_PREPPLUGINHOOKS_H
#define NEVERC_SCAN_PREPPLUGINHOOKS_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "neverc/Scan/Token.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace neverc {

class IdentifierInfo;
class MacroArgStorage;
class MacroRecord;

struct PrepIncludeHook {
  enum class Action { Continue, Skip, Redirect };

  SourceLocation Location;
  Token IncludeToken;
  llvm::StringRef Filename;
  llvm::StringRef SearchPath;
  llvm::StringRef RelativePath;
  bool IsAngled = false;
  bool IsImport = false;
  bool IsIncludeNext = false;
  Action Result = Action::Continue;
  std::string ReplacementFilename;
  bool ReplacementIsAngled = false;
};

struct PrepMacroHook {
  enum class Operation { Define, Undefine, Expand, ExpandBuiltin };
  enum class Action { Continue, Suppress, ReplaceTokens };

  Operation Kind = Operation::Expand;
  Token NameToken;
  IdentifierInfo *Name = nullptr;
  MacroRecord *Definition = nullptr;
  MacroArgStorage *Arguments = nullptr;
  Action Result = Action::Continue;
  llvm::SmallVector<Token, 4> ReplacementTokens;
};

struct PrepPragmaHook {
  enum class Action { Continue, Handled, ReplaceTokens };

  SourceLocation Location;
  PragmaIntroducerKind Introducer = PIK_HashPragma;
  llvm::StringRef Namespace;
  llvm::StringRef Name;
  llvm::ArrayRef<Token> Tokens;
  Action Result = Action::Continue;
  llvm::SmallVector<Token, 4> ReplacementTokens;
};

struct PrepFeatureQueryHook {
  enum class Kind {
    HasFeature,
    HasExtension,
    HasBuiltin,
    HasInclude,
    HasIncludeNext
  };

  SourceLocation Location;
  Kind Query = Kind::HasFeature;
  llvm::StringRef Name;
  bool BuiltinValue = false;
  bool Value = false;
  bool Replaced = false;
};

class PrepPluginHooks {
public:
  virtual ~PrepPluginHooks() = default;

  PrepPluginHooks(const PrepPluginHooks &) = delete;
  PrepPluginHooks &operator=(const PrepPluginHooks &) = delete;

  virtual bool interceptToken(const Token &Input,
                              llvm::SmallVectorImpl<Token> &Output,
                              bool &Replaced) = 0;
  virtual bool hasIncludeInterceptor() const { return false; }
  virtual bool hasMacroInterceptor() const { return false; }
  virtual bool hasPragmaInterceptor() const { return false; }
  virtual bool hasFeatureQueryInterceptor() const { return false; }
  virtual bool interceptInclude(PrepIncludeHook &) { return true; }
  virtual bool interceptMacro(PrepMacroHook &) { return true; }
  virtual bool interceptPragma(PrepPragmaHook &) { return true; }
  virtual bool interceptFeatureQuery(PrepFeatureQueryHook &) { return true; }

protected:
  PrepPluginHooks() = default;
};

} // namespace neverc

#endif
