#include "PluginTokenLexer.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/Token.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <new>

using namespace llvm;

namespace neverc::plugin {

Error PluginTokenLexer::install(PrepEngine &Prep, ArrayRef<Token> Tokens) {
  if (Tokens.empty())
    return createStringError(inconvertibleErrorCode(),
                             "plugin token stream is empty");
  auto Storage =
      std::unique_ptr<Token[]>(new (std::nothrow) Token[Tokens.size()]);
  if (!Storage)
    return createStringError(inconvertibleErrorCode(),
                             "unable to allocate plugin token stream");
  std::copy(Tokens.begin(), Tokens.end(), Storage.get());
  if (!Prep.setInitialTokenStream(std::move(Storage),
                                  static_cast<unsigned>(Tokens.size())))
    return createStringError(inconvertibleErrorCode(),
                             "unable to install plugin token stream");
  return Error::success();
}

} // namespace neverc::plugin
