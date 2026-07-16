#ifndef NEVERC_LIB_PLUGIN_FRONTEND_PLUGINTOKENLEXER_H
#define NEVERC_LIB_PLUGIN_FRONTEND_PLUGINTOKENLEXER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

namespace neverc {
class PrepEngine;
class Token;
} // namespace neverc

namespace neverc::plugin {

class PluginTokenLexer {
public:
  static llvm::Error install(PrepEngine &Prep, llvm::ArrayRef<Token> Tokens);
};

} // namespace neverc::plugin

#endif
