#include "neverc/Plugin/Host/PluginOptionRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstring>

using namespace llvm;
using namespace neverc::plugin;

namespace {

NevercStringView view(const char *Text) {
  return {Text, static_cast<uint64_t>(std::strlen(Text))};
}

NevercOptionDescriptor option(const char *Spelling, NevercOptionForm Form,
                              NevercOptionValueType Type) {
  NevercOptionDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_DRIVER_API_MAJOR,
                       NEVERC_DRIVER_API_MINOR, 0};
  Descriptor.Spelling = view(Spelling);
  Descriptor.Form = Form;
  Descriptor.ValueType = Type;
  Descriptor.Multiplicity = NEVERC_OPTION_SINGLE;
  Descriptor.ArgumentCount = Form == NEVERC_OPTION_MULTI_ARG ? 2 : 0;
  return Descriptor;
}

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

OwnedPluginOption copyOption(StringRef PluginID,
                             const NevercOptionDescriptor &Descriptor) {
  auto Result = copyPluginOptionDescriptor(PluginID, Descriptor);
  if (!Result) {
    ADD_FAILURE() << takeErrorMessage(Result.takeError());
    return {};
  }
  return std::move(*Result);
}

TEST(PluginOptionRegistryTest, ParsesAllOptionFormsAndTypedValues) {
  PluginOptionRegistry Registry;
  std::vector<OwnedPluginOption> Options;

  Options.push_back(copyOption(
      "org.neverc.test.options",
      option("--trace", NEVERC_OPTION_FLAG, NEVERC_OPTION_BOOL)));

  NevercOptionDescriptor Level =
      option("--level=", NEVERC_OPTION_JOINED, NEVERC_OPTION_ENUM);
  const NevercOptionEnumValue Levels[] = {
      {{sizeof(NevercOptionEnumValue), NEVERC_DRIVER_API_MAJOR,
        NEVERC_DRIVER_API_MINOR, 0},
       view("fast"), 1, view("fast mode")},
      {{sizeof(NevercOptionEnumValue), NEVERC_DRIVER_API_MAJOR,
        NEVERC_DRIVER_API_MINOR, 0},
       view("small"), 2, view("small mode")},
  };
  Level.EnumValues = {Levels, 2, sizeof(Levels[0])};
  Options.push_back(copyOption("org.neverc.test.options", Level));

  Options.push_back(copyOption(
      "org.neverc.test.options",
      option("--count", NEVERC_OPTION_SEPARATE, NEVERC_OPTION_UINT)));

  NevercOptionDescriptor Pair =
      option("--pair", NEVERC_OPTION_MULTI_ARG, NEVERC_OPTION_STRING);
  Pair.Multiplicity = NEVERC_OPTION_APPEND;
  Options.push_back(copyOption("org.neverc.test.options", Pair));

  EXPECT_FALSE(Registry.registerBatch(std::move(Options)));
  EXPECT_FALSE(Registry.freeze());

  const std::array<StringRef, 9> Arguments = {
      "--trace", "--level=fast", "--count", "7", "--pair",
      "left",    "right",        "input.c", "--unknown"};
  auto Parsed = Registry.parse(Arguments);
  ASSERT_TRUE(static_cast<bool>(Parsed));
  EXPECT_EQ(Parsed->remainingArguments(),
            (ArrayRef<std::string>{"input.c", "--unknown"}));
  ASSERT_NE(Parsed->find("org.neverc.test.options", "--trace"), nullptr);
  EXPECT_EQ(Parsed->find("org.neverc.test.options", "--trace")->Values,
            (std::vector<std::string>{"true"}));
  EXPECT_EQ(Parsed->find("org.neverc.test.options", "--level=")->Values,
            (std::vector<std::string>{"fast"}));
  EXPECT_EQ(Parsed->find("org.neverc.test.options", "--count")->Values,
            (std::vector<std::string>{"7"}));
  EXPECT_EQ(Parsed->find("org.neverc.test.options", "--pair")->Values,
            (std::vector<std::string>{"left", "right"}));
}

TEST(PluginOptionRegistryTest, ResolvesAliasesAndLastWins) {
  PluginOptionRegistry Registry;
  NevercOptionDescriptor Descriptor =
      option("--mode", NEVERC_OPTION_SEPARATE, NEVERC_OPTION_STRING);
  Descriptor.Multiplicity = NEVERC_OPTION_LAST_WINS;
  const NevercStringView Aliases[] = {view("-m")};
  Descriptor.Aliases = {Aliases, 1, sizeof(Aliases[0])};
  EXPECT_FALSE(Registry.registerBatch(
      {copyOption("org.neverc.test.options", Descriptor)}));
  EXPECT_FALSE(Registry.freeze());

  const std::array<StringRef, 4> Arguments = {"--mode", "first", "-m",
                                              "second"};
  auto Parsed = Registry.parse(Arguments);
  ASSERT_TRUE(static_cast<bool>(Parsed));
  const auto *Mode =
      Parsed->find("org.neverc.test.options", "--mode");
  ASSERT_NE(Mode, nullptr);
  EXPECT_EQ(Mode->Values, (std::vector<std::string>{"second"}));
}

TEST(PluginOptionRegistryTest, RejectsStaticReservedAndPluginConflicts) {
  const std::array<StringRef, 1> Static = {"-c"};
  PluginOptionRegistry Registry(Static);

  Error StaticConflict = Registry.registerBatch({copyOption(
      "org.neverc.test.one",
      option("-c", NEVERC_OPTION_FLAG, NEVERC_OPTION_BOOL))});
  ASSERT_TRUE(static_cast<bool>(StaticConflict));
  EXPECT_NE(takeErrorMessage(std::move(StaticConflict)).find("static"),
            std::string::npos);

  EXPECT_FALSE(Registry.registerBatch({copyOption(
      "org.neverc.test.one",
      option("--shared", NEVERC_OPTION_FLAG, NEVERC_OPTION_BOOL))}));
  Error PluginConflict = Registry.registerBatch({copyOption(
      "org.neverc.test.two",
      option("--shared", NEVERC_OPTION_FLAG, NEVERC_OPTION_BOOL))});
  ASSERT_TRUE(static_cast<bool>(PluginConflict));
  EXPECT_NE(takeErrorMessage(std::move(PluginConflict)).find("conflict"),
            std::string::npos);

  Error Reserved = Registry.registerBatch({copyOption(
      "org.neverc.test.two",
      option("-fplugin=", NEVERC_OPTION_JOINED, NEVERC_OPTION_PATH))});
  ASSERT_TRUE(static_cast<bool>(Reserved));
  EXPECT_NE(takeErrorMessage(std::move(Reserved)).find("reserved"),
            std::string::npos);
}

TEST(PluginOptionRegistryTest, EnforcesRequiredConflictAndRequiresRules) {
  PluginOptionRegistry Registry;
  NevercOptionDescriptor Output =
      option("--output", NEVERC_OPTION_SEPARATE, NEVERC_OPTION_PATH);
  Output.Required = NEVERC_TRUE;
  NevercOptionDescriptor Fast =
      option("--fast", NEVERC_OPTION_FLAG, NEVERC_OPTION_BOOL);
  const NevercStringView Conflicts[] = {view("--safe")};
  Fast.Conflicts = {Conflicts, 1, sizeof(Conflicts[0])};
  NevercOptionDescriptor Safe =
      option("--safe", NEVERC_OPTION_FLAG, NEVERC_OPTION_BOOL);
  const NevercStringView Requires[] = {view("--output")};
  Safe.Requires = {Requires, 1, sizeof(Requires[0])};

  EXPECT_FALSE(Registry.registerBatch(
      {copyOption("org.neverc.test.options", Output),
       copyOption("org.neverc.test.options", Fast),
       copyOption("org.neverc.test.options", Safe)}));
  EXPECT_FALSE(Registry.freeze());

  auto Missing = Registry.parse(ArrayRef<StringRef>{"--fast"});
  ASSERT_FALSE(static_cast<bool>(Missing));
  EXPECT_NE(takeErrorMessage(Missing.takeError()).find("required"),
            std::string::npos);

  auto Conflict = Registry.parse(
      ArrayRef<StringRef>{"--output", "out.bin", "--fast", "--safe"});
  ASSERT_FALSE(static_cast<bool>(Conflict));
  EXPECT_NE(takeErrorMessage(Conflict.takeError()).find("conflicts"),
            std::string::npos);
}

TEST(PluginOptionRegistryTest, NamespacedArgumentsRequirePluginIDWhenAmbiguous) {
  PluginOptionRegistry Registry;
  EXPECT_FALSE(Registry.registerBatch(
      {copyOption("org.neverc.test.one",
                  option("--one-level", NEVERC_OPTION_SEPARATE,
                         NEVERC_OPTION_UINT)),
       copyOption("org.neverc.test.two",
                  option("--two-level", NEVERC_OPTION_SEPARATE,
                         NEVERC_OPTION_UINT))}));
  EXPECT_FALSE(Registry.freeze());

  auto Ambiguous =
      Registry.parse(ArrayRef<StringRef>{"-fplugin-arg=one-level=1"});
  ASSERT_FALSE(static_cast<bool>(Ambiguous));
  EXPECT_NE(takeErrorMessage(Ambiguous.takeError()).find("plugin ID"),
            std::string::npos);

  auto Parsed = Registry.parse(ArrayRef<StringRef>{
      "-fplugin-arg=org.neverc.test.two:two-level=2"});
  ASSERT_TRUE(static_cast<bool>(Parsed));
  const auto *Level =
      Parsed->find("org.neverc.test.two", "--two-level");
  ASSERT_NE(Level, nullptr);
  EXPECT_EQ(Level->Values, (std::vector<std::string>{"2"}));
}

TEST(PluginOptionRegistryTest, PublishesOptionsRegisteredByPlugin) {
  PluginProcessServices Services("neverc-plugin-option-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded = Services.registry().load(NEVERC_TEST_OPTION_PLUGIN);
  ASSERT_TRUE(static_cast<bool>(Loaded))
      << takeErrorMessage(Loaded.takeError());

  {
    const std::array<StringRef, 1> Selected = {
        "org.neverc.test.option"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(Plan))
        << takeErrorMessage(Plan.takeError());
    ASSERT_FALSE(activatePluginPlan(Services, *Plan));
  }

  ASSERT_EQ(Services.options().size(), 1U);
  ASSERT_FALSE(Services.options().freeze());
  auto Parsed =
      Services.options().parse(ArrayRef<StringRef>{"--fixture-level", "7"});
  ASSERT_TRUE(static_cast<bool>(Parsed))
      << takeErrorMessage(Parsed.takeError());
  const auto *Level =
      Parsed->find("org.neverc.test.option", "--fixture-level");
  ASSERT_NE(Level, nullptr);
  EXPECT_EQ(Level->Values, (std::vector<std::string>{"7"}));

  EXPECT_FALSE(Services.shutdown());
  EXPECT_EQ(Services.options().size(), 0U);
}

} // namespace
