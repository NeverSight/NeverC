#include "PluginFrontendTestSupport.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepPluginHooks.h"

using namespace neverc;
using namespace neverc::test;

namespace {

class DeleteFirstTokenHook final : public PrepPluginHooks {
public:
  bool interceptToken(const Token &, llvm::SmallVectorImpl<Token> &Output,
                      bool &Replaced) override {
    Output.clear();
    ++Calls;
    Replaced = Calls == 1;
    return true;
  }

  unsigned Calls = 0;
};

class PluginTokenInterceptorUnitTest : public PluginPrepTest {};

TEST_F(PluginTokenInterceptorUnitTest,
       CacheStoresFinalSequenceAndDoesNotReinterceptReplay) {
  DeleteFirstTokenHook Hook;
  prep().setPluginHooks(&Hook);
  prep().SaveLexState();

  Token First;
  prep().Lex(First);
  ASSERT_EQ(Hook.Calls, 2U);
  ASSERT_FALSE(First.getFlag(Token::IsReinjected));

  prep().RestoreLexState();
  Token Replayed;
  prep().Lex(Replayed);
  EXPECT_EQ(Hook.Calls, 2U);
  EXPECT_TRUE(Replayed.getFlag(Token::IsReinjected));
  EXPECT_EQ(Replayed.getKind(), First.getKind());
  EXPECT_EQ(Replayed.getLocation(), First.getLocation());

  prep().setPluginHooks(nullptr);
}

} // namespace
