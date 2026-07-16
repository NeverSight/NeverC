#include "Plugin/PluginBootstrap.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::driver;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

TEST(PluginBootstrapTest,
     DiscoversDeduplicatesActivatesAndParsesRegisteredOptions) {
  auto StaticSpellings = PluginBootstrap::collectStaticOptionSpellings(
      neverc::driver::getDriverOptTable());
  EXPECT_NE(std::find(StaticSpellings.begin(), StaticSpellings.end(),
                      "-fplugin="),
            StaticSpellings.end());
  PluginBootstrap Bootstrap("neverc-plugin-bootstrap-tests",
                            LLVM_VERSION_MAJOR,
                            std::move(StaticSpellings));
  std::vector<PluginBootstrapToken> Tokens = {
      {std::string("-fplugin=") + NEVERC_TEST_OPTION_PLUGIN,
       PluginArgumentOrigin::CommandLine, "command line", 1},
      {std::string("-fplugin=") + NEVERC_TEST_OPTION_PLUGIN,
       PluginArgumentOrigin::Configuration, "test.cfg", 2},
  };
  ASSERT_FALSE(Bootstrap.discoverAndActivate(Tokens));
  ASSERT_TRUE(Bootstrap.isActive());
  ASSERT_EQ(Bootstrap.pluginIDs().size(), 1U);

  std::vector<std::string> Storage = {
      Tokens.front().Value, "--fixture-level", "3", "-fsyntax-only"};
  SmallVector<StringRef, 4> Arguments;
  for (const std::string &Argument : Storage)
    Arguments.push_back(Argument);
  auto Parsed = Bootstrap.parsePluginOptions(Arguments);
  ASSERT_TRUE(static_cast<bool>(Parsed))
      << takeErrorMessage(Parsed.takeError());
  const auto *Option = Parsed->find(
      "org.neverc.test.option", "--fixture-level");
  ASSERT_NE(Option, nullptr);
  ASSERT_EQ(Option->Values.size(), 1U);
  EXPECT_EQ(Option->Values.front(), "3");
  ASSERT_EQ(Parsed->remainingArguments().size(), 2U);
  EXPECT_EQ(Parsed->remainingArguments()[0], Tokens.front().Value);
  EXPECT_EQ(Parsed->remainingArguments()[1], "-fsyntax-only");
  ASSERT_EQ(Parsed->remainingArgumentIndices().size(), 2U);
  EXPECT_EQ(Parsed->remainingArgumentIndices()[0], 0U);
  EXPECT_EQ(Parsed->remainingArgumentIndices()[1], 3U);

  auto Session = Bootstrap.createSession(std::move(*Parsed));
  ASSERT_TRUE(static_cast<bool>(Session))
      << takeErrorMessage(Session.takeError());
  EXPECT_NE((*Session)->options().find(
                "org.neverc.test.option", "--fixture-level"),
            nullptr);
  EXPECT_FALSE((*Session)->end());
}

TEST(PluginBootstrapTest, KeepsNoPluginPathAllocationFree) {
  PluginBootstrap Bootstrap("neverc-plugin-bootstrap-tests",
                            LLVM_VERSION_MAJOR, {});
  std::vector<PluginBootstrapToken> Tokens = {
      {"-fplugin-pass=legacy.so", PluginArgumentOrigin::CommandLine,
       "command line", 1},
  };
  EXPECT_FALSE(Bootstrap.discoverAndActivate(Tokens));
  EXPECT_FALSE(Bootstrap.isActive());
  EXPECT_EQ(Bootstrap.services(), nullptr);
}

} // namespace
