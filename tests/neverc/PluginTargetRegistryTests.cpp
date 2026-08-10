#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

NevercStringView view(const char *Text) {
  return {Text, static_cast<uint64_t>(std::char_traits<char>::length(Text))};
}

std::string errorText(Error ErrorValue) {
  auto Text = toString(std::move(ErrorValue));
  return Text.str().str();
}

std::string text(NevercStringView Text) {
  return std::string(Text.Data ? Text.Data : "",
                     static_cast<size_t>(Text.Length));
}

NevercStatus NEVERC_CALL classifyDirectABI(
    void *, const NevercABIFunctionQuery *Query,
    NevercABIArgumentClassification *ReturnValue,
    NevercABIArgumentClassificationArray *Arguments) {
  ReturnValue->Kind =
      Query->ReturnType.Kind == NEVERC_ABI_TYPE_VOID
          ? NEVERC_ABI_ARGUMENT_IGNORE
          : NEVERC_ABI_ARGUMENT_DIRECT;
  for (uint64_t I = 0; I != Arguments->Count; ++I) {
    auto *Argument = reinterpret_cast<
        NevercABIArgumentClassification *>(
        reinterpret_cast<uint8_t *>(Arguments->Data) +
        I * Arguments->ElementStride);
    Argument->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL lowerTestBuiltin(
    void *, const NevercTargetBuiltinLoweringInvocation *,
    NevercIRValueHandle *OutResult) {
  if (OutResult)
    *OutResult = {};
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL validateTestCPU(
    NevercTaskHandle, NevercStringView, void *, NevercBool *OutValid) {
  if (OutValid)
    *OutValid = NEVERC_TRUE;
  return neverc_status_ok();
}

NevercTargetDescriptor target(NevercTargetID ID, const char *Name) {
  NevercTargetDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_TARGET_API_MAJOR,
                       NEVERC_TARGET_API_MINOR, 0};
  Descriptor.TargetID = ID;
  Descriptor.CanonicalName = view(Name);
  Descriptor.Machine.Header = {
      sizeof(Descriptor.Machine), NEVERC_TARGET_API_MAJOR,
      NEVERC_TARGET_API_MINOR, 0};
  Descriptor.Machine.RawTriple = view("test-unknown-none-none");
  Descriptor.Machine.Architecture = view("test");
  Descriptor.Machine.DataLayout =
      view("e-p:64:64-i64:64-n32:64-S128");
  Descriptor.Machine.DefaultCPU = view("generic");
  Descriptor.Machine.SchemaDigest = view(
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  Descriptor.Machine.SupportedRelocationModels =
      NEVERC_TARGET_RELOCATION_MASK_STATIC;
  Descriptor.Machine.SupportedCodeModels =
      NEVERC_TARGET_CODE_MODEL_MASK_SMALL;
  Descriptor.Machine.DefaultRelocationModel =
      NEVERC_TARGET_RELOCATION_STATIC;
  Descriptor.Machine.DefaultCodeModel = NEVERC_TARGET_CODE_MODEL_SMALL;
  Descriptor.Machine.ExceptionModel = NEVERC_TARGET_EXCEPTION_NONE;
  Descriptor.Machine.UnwindModel = NEVERC_TARGET_UNWIND_NONE;
  Descriptor.Machine.Endianness = NEVERC_TARGET_ENDIAN_LITTLE;
  Descriptor.Machine.PointerWidth = 64;
  Descriptor.Machine.IntWidth = 32;
  Descriptor.Machine.LongWidth = 64;
  Descriptor.Machine.LongLongWidth = 64;
  Descriptor.Machine.StackAlignment = 128;
  Descriptor.Machine.MaximumAtomicWidth = 64;
  Descriptor.Machine.MaximumVectorAlignment = 128;
  Descriptor.Machine.BuiltinVaListKind =
      NEVERC_TARGET_VA_LIST_VOID_POINTER;
  Descriptor.Machine.ExecutionLevels = NEVERC_TARGET_EXECUTION_USER;
  Descriptor.Machine.DefaultExecutionLevel =
      NEVERC_TARGET_EXECUTION_USER;
  Descriptor.Machine.TLSSupported = NEVERC_TRUE;
  return Descriptor;
}

std::vector<std::string> llvmTargetNames() {
  std::vector<std::string> Names;
  for (const llvm::Target &Target : llvm::TargetRegistry::targets())
    Names.emplace_back(Target.getName());
  return Names;
}

NevercTargetTripleMatcher matcher(const char *Architecture,
                                  const char *Vendor,
                                  const char *OperatingSystem,
                                  const char *Environment) {
  NevercTargetTripleMatcher Matcher{};
  Matcher.Header = {sizeof(Matcher), NEVERC_TARGET_API_MAJOR,
                    NEVERC_TARGET_API_MINOR, 0};
  Matcher.Architecture = view(Architecture);
  Matcher.Vendor = view(Vendor);
  Matcher.OperatingSystem = view(OperatingSystem);
  Matcher.Environment = view(Environment);
  return Matcher;
}

TEST(PluginTargetRegistryTest, RejectsDuplicateTargetIDAtomically) {
  constexpr NevercTargetID Duplicate{UINT64_C(0x1111), UINT64_C(0x2222)};
  NevercTargetDescriptor First = target(Duplicate, "test.first");
  NevercTargetDescriptor Second = target(Duplicate, "test.second");

  PluginTargetRegistrationView FirstPlugin;
  FirstPlugin.PluginID = "org.neverc.test.target-first";
  FirstPlugin.Targets = ArrayRef<NevercTargetDescriptor>(First);
  PluginTargetRegistrationView SecondPlugin;
  SecondPlugin.PluginID = "org.neverc.test.target-second";
  SecondPlugin.Targets = ArrayRef<NevercTargetDescriptor>(Second);
  const std::array<PluginTargetRegistrationView, 2> Registrations = {
      FirstPlugin, SecondPlugin};

  auto Frozen =
      PluginTargetRegistry::freeze(Registrations, PluginTargetRequest{});
  ASSERT_FALSE(static_cast<bool>(Frozen));
  const std::string Message = errorText(Frozen.takeError());
  EXPECT_NE(Message.find("duplicate Target ID"), std::string::npos);
  EXPECT_NE(Message.find("org.neverc.test.target-first"),
            std::string::npos);
  EXPECT_NE(Message.find("org.neverc.test.target-second"),
            std::string::npos);
}

TEST(PluginTargetRegistryTest, PublishesSixIndependentRegistrarInterfaces) {
  PluginProcessServices Services{"neverc-plugin-target-registry-tests", 1};
  ASSERT_FALSE(registerPluginTargetInterfaces(Services));
  ASSERT_FALSE(Services.interfaces().freeze());

  auto Target = Services.interfaces().query(
      {NEVERC_INTERFACE_TARGET_HIGH, NEVERC_INTERFACE_TARGET_LOW},
      NEVERC_TARGET_API_MAJOR, NEVERC_TARGET_API_MINOR);
  auto ABI = Services.interfaces().query(
      {NEVERC_INTERFACE_TARGET_ABI_HIGH, NEVERC_INTERFACE_TARGET_ABI_LOW},
      NEVERC_TARGET_ABI_API_MAJOR, NEVERC_TARGET_ABI_API_MINOR);
  auto CallingConvention = Services.interfaces().query(
      {NEVERC_INTERFACE_CALLING_CONVENTION_HIGH,
       NEVERC_INTERFACE_CALLING_CONVENTION_LOW},
      NEVERC_CALLING_CONVENTION_API_MAJOR,
      NEVERC_CALLING_CONVENTION_API_MINOR);
  auto MC = Services.interfaces().query(
      {NEVERC_INTERFACE_MC_HIGH, NEVERC_INTERFACE_MC_LOW},
      NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR);
  auto Object = Services.interfaces().query(
      {NEVERC_INTERFACE_OBJECT_HIGH, NEVERC_INTERFACE_OBJECT_LOW},
      NEVERC_OBJECT_API_MAJOR, NEVERC_OBJECT_API_MINOR);
  auto Format = Services.interfaces().query(
      {NEVERC_INTERFACE_OBJECT_FORMAT_HIGH,
       NEVERC_INTERFACE_OBJECT_FORMAT_LOW},
      NEVERC_OBJECT_FORMAT_API_MAJOR, NEVERC_OBJECT_FORMAT_API_MINOR);

  ASSERT_TRUE(static_cast<bool>(Target));
  ASSERT_TRUE(static_cast<bool>(ABI));
  ASSERT_TRUE(static_cast<bool>(CallingConvention));
  ASSERT_TRUE(static_cast<bool>(MC));
  ASSERT_TRUE(static_cast<bool>(Object));
  ASSERT_TRUE(static_cast<bool>(Format));
  EXPECT_NE(static_cast<const NevercTargetAPI *>(Target->Table)
                ->RegisterTarget,
            nullptr);
  EXPECT_NE(static_cast<const NevercTargetAPI *>(Target->Table)
                ->RegisterCodeGenEdge,
            nullptr);
  EXPECT_NE(
      static_cast<const NevercTargetABIAPI *>(ABI->Table)->RegisterABI,
      nullptr);
  EXPECT_NE(static_cast<const NevercCallingConventionAPI *>(
                CallingConvention->Table)
                ->RegisterCallingConvention,
            nullptr);
  EXPECT_NE(static_cast<const NevercMCAPI *>(MC->Table)->RegisterSchema,
            nullptr);
  EXPECT_NE(static_cast<const NevercMCAPI *>(MC->Table)->RegisterEncoder,
            nullptr);
  EXPECT_NE(static_cast<const NevercMCAPI *>(MC->Table)->RegisterDecoder,
            nullptr);
  EXPECT_NE(static_cast<const NevercMCAPI *>(MC->Table)->RegisterAsmBackend,
            nullptr);
  EXPECT_EQ(static_cast<const NevercObjectAPI *>(Object->Table)->Context,
            nullptr);
  EXPECT_NE(static_cast<const NevercObjectFormatAPI *>(Format->Table)
                ->RegisterFormat,
            nullptr);
}

TEST(PluginTargetRegistryTest,
     CrossReferenceFailureRollsBackPublishedRegistration) {
  PluginProcessServices Services{"neverc-plugin-target-rollback-tests", 1};
  ASSERT_FALSE(registerPluginTargetInterfaces(Services));
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded =
      Services.registry().load(NEVERC_TEST_TARGET_UNKNOWN_FORMAT_PLUGIN);
  ASSERT_TRUE(static_cast<bool>(Loaded));
  auto Plan = makePluginActivationPlan(
      Services.registry(), {"org.neverc.test.target-unknown-format"});
  ASSERT_TRUE(static_cast<bool>(Plan));

  Error Activation = activatePluginPlan(Services, *Plan);
  ASSERT_TRUE(static_cast<bool>(Activation));
  const std::string Message = errorText(std::move(Activation));
  EXPECT_NE(Message.find("unknown object Format ID"), std::string::npos);
  EXPECT_EQ((*Loaded)->registration(), nullptr);
}

TEST(PluginTargetRegistryTest,
     RegistrarFailureRollsBackEarlierTargetRegistration) {
  PluginProcessServices Services{"neverc-plugin-target-registrar-tests", 1};
  ASSERT_FALSE(registerPluginTargetInterfaces(Services));
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded = Services.registry().load(
      NEVERC_TEST_TARGET_REGISTRATION_FAILURE_PLUGIN);
  ASSERT_TRUE(static_cast<bool>(Loaded));
  auto Plan = makePluginActivationPlan(
      Services.registry(),
      {"org.neverc.test.target-registration-failure"});
  ASSERT_TRUE(static_cast<bool>(Plan));

  Error Activation = activatePluginPlan(Services, *Plan);
  ASSERT_TRUE(static_cast<bool>(Activation));
  consumeError(std::move(Activation));
  EXPECT_EQ((*Loaded)->registration(), nullptr);
}

TEST(PluginTargetRegistryTest,
     SessionSnapshotPinsTargetOwnerUntilSnapshotRelease) {
  PluginProcessServices Services{"neverc-plugin-target-session-tests", 1};
  ASSERT_FALSE(registerPluginTargetInterfaces(Services));
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded = Services.registry().load(NEVERC_TEST_TARGET_VALID_PLUGIN);
  ASSERT_TRUE(static_cast<bool>(Loaded));

  std::unique_ptr<PluginSession> Session;
  {
    auto Plan = makePluginActivationPlan(
        Services.registry(), {"org.neverc.test.target-valid"});
    ASSERT_TRUE(static_cast<bool>(Plan));
    ASSERT_FALSE(activatePluginPlan(Services, *Plan));
    auto Created = PluginSession::create(Services, *Plan);
    if (!Created)
      FAIL() << errorText(Created.takeError());
    Session = std::move(*Created);
  }

  auto Snapshot =
      findPluginTargetSnapshot(Services, Session->handle());
  ASSERT_NE(Snapshot, nullptr);
  ASSERT_EQ(Snapshot->targetCount(), 1U);
  const auto *Target = Snapshot->findTarget(
      {UINT64_C(0x4e43545445535401), UINT64_C(1)});
  ASSERT_NE(Target, nullptr);
  EXPECT_EQ(Target->Owner.get(), Loaded->get());
  Error Busy = Services.registry().unload("org.neverc.test.target-valid");
  ASSERT_TRUE(static_cast<bool>(Busy));
  consumeError(std::move(Busy));

  ASSERT_FALSE(Session->end());
  Session.reset();
  Error Pinned =
      Services.registry().unload("org.neverc.test.target-valid");
  ASSERT_TRUE(static_cast<bool>(Pinned));
  consumeError(std::move(Pinned));
  Snapshot.reset();
  EXPECT_FALSE(
      Services.registry().unload("org.neverc.test.target-valid"));
}

TEST(PluginTargetRegistryTest, LeavesLLVMGlobalTargetRegistryUnchanged) {
  const std::vector<std::string> Before = llvmTargetNames();
  PluginProcessServices Services{"neverc-plugin-target-global-tests", 1};
  ASSERT_FALSE(registerPluginTargetInterfaces(Services));
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded = Services.registry().load(NEVERC_TEST_TARGET_VALID_PLUGIN);
  ASSERT_TRUE(static_cast<bool>(Loaded));
  {
    auto Plan = makePluginActivationPlan(
        Services.registry(), {"org.neverc.test.target-valid"});
    ASSERT_TRUE(static_cast<bool>(Plan));
    ASSERT_FALSE(activatePluginPlan(Services, *Plan));
  }
  EXPECT_EQ(llvmTargetNames(), Before);
  EXPECT_FALSE(
      Services.registry().unload("org.neverc.test.target-valid"));
}

TEST(PluginTargetRegistryTest, RejectsOverlappingTripleMatchers) {
  NevercTargetTripleMatcher FirstMatcher =
      matcher("x86_64", "", "linux", "");
  NevercTargetTripleMatcher SecondMatcher =
      matcher("x86_64", "pc", "linux", "");
  NevercTargetDescriptor First =
      target({UINT64_C(0x1000), UINT64_C(1)}, "test.first");
  First.TripleMatchers = {&FirstMatcher, 1, sizeof(FirstMatcher)};
  NevercTargetDescriptor Second =
      target({UINT64_C(0x1000), UINT64_C(2)}, "test.second");
  Second.TripleMatchers = {&SecondMatcher, 1, sizeof(SecondMatcher)};

  PluginTargetRegistrationView FirstPlugin;
  FirstPlugin.PluginID = "org.neverc.test.target-first";
  FirstPlugin.Targets = ArrayRef<NevercTargetDescriptor>(First);
  PluginTargetRegistrationView SecondPlugin;
  SecondPlugin.PluginID = "org.neverc.test.target-second";
  SecondPlugin.Targets = ArrayRef<NevercTargetDescriptor>(Second);
  const std::array<PluginTargetRegistrationView, 2> Registrations = {
      FirstPlugin, SecondPlugin};

  auto Frozen =
      PluginTargetRegistry::freeze(Registrations, PluginTargetRequest{});
  ASSERT_FALSE(static_cast<bool>(Frozen));
  const std::string Message = errorText(Frozen.takeError());
  EXPECT_NE(Message.find("overlapping triple matcher"), std::string::npos);
  EXPECT_NE(Message.find("architecture"), std::string::npos);
}

TEST(PluginTargetRegistryTest, MatchesNameRawTripleAndTriplePattern) {
  NevercTargetTripleMatcher Matcher =
      matcher("x86_64", "", "linux", "");
  Matcher.Priority = 7;
  NevercTargetDescriptor Target =
      target({UINT64_C(0x1000), UINT64_C(3)}, "test.matchable");
  Target.TripleMatchers = {&Matcher, 1, sizeof(Matcher)};
  PluginTargetRegistrationView Plugin;
  Plugin.PluginID = "org.neverc.test.target-matchable";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin),
      PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Frozen))
      << errorText(Frozen.takeError());
  EXPECT_NE((*Frozen)->matchTarget("test.matchable"), nullptr);
  EXPECT_NE((*Frozen)->matchTarget("test-unknown-none-none"), nullptr);
  EXPECT_NE((*Frozen)->matchTarget("x86_64-pc-linux-gnu"), nullptr);
  EXPECT_EQ((*Frozen)->matchTarget("aarch64-apple-darwin-none"),
            nullptr);
}

TEST(PluginTargetRegistryTest, OwnsLanguageAndAsmExtensionDescriptors) {
  char MacroName[] = "__NOVEL__";
  char BuiltinName[] = "__builtin_novel";
  char RegisterAlias[] = "zero";
  char ConstraintSpelling[] = "r";
  char Clobbers[] = "~{flags}";

  NevercTargetMacroDescriptor Macro{};
  Macro.Header = {sizeof(Macro), NEVERC_TARGET_API_MAJOR,
                  NEVERC_TARGET_API_MINOR, 0};
  Macro.Name = view(MacroName);
  Macro.Value = view("9");

  NevercTargetBuiltinDescriptor Builtin{};
  Builtin.Header = {sizeof(Builtin), NEVERC_TARGET_API_MAJOR,
                    NEVERC_TARGET_API_MINOR, 0};
  Builtin.Name = view(BuiltinName);
  Builtin.TypeEncoding = view("ii");
  Builtin.Attributes = view("nc");
  Builtin.Languages = NEVERC_TARGET_BUILTIN_LANGUAGE_C;
  Builtin.Lower = lowerTestBuiltin;

  NevercStringView RegisterAliases[] = {view(RegisterAlias)};
  NevercTargetRegisterDescriptor Register{};
  Register.Header = {sizeof(Register), NEVERC_TARGET_API_MAJOR,
                     NEVERC_TARGET_API_MINOR, 0};
  Register.Name = view("r0");
  Register.Aliases = {RegisterAliases, 1,
                      sizeof(RegisterAliases[0])};

  NevercTargetConstraintDescriptor Constraint{};
  Constraint.Header = {sizeof(Constraint), NEVERC_TARGET_API_MAJOR,
                       NEVERC_TARGET_API_MINOR, 0};
  Constraint.Spelling = view(ConstraintSpelling);
  Constraint.ConvertedConstraint = view("r");
  Constraint.Flags = NEVERC_TARGET_CONSTRAINT_ALLOWS_REGISTER;

  NevercTargetDescriptor Target =
      target({UINT64_C(0x1000), UINT64_C(4)}, "test.extensions");
  Target.Macros = {&Macro, 1, sizeof(Macro)};
  Target.Builtins = {&Builtin, 1, sizeof(Builtin)};
  Target.Registers = {&Register, 1, sizeof(Register)};
  Target.Constraints = {&Constraint, 1, sizeof(Constraint)};
  Target.Clobbers = view(Clobbers);
  PluginTargetRegistrationView Plugin;
  Plugin.PluginID = "org.neverc.test.target-extensions";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin),
      PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Frozen))
      << errorText(Frozen.takeError());

  MacroName[2] = 'X';
  BuiltinName[2] = 'X';
  RegisterAlias[0] = 'X';
  ConstraintSpelling[0] = 'X';
  Clobbers[2] = 'X';

  ASSERT_EQ((*Frozen)->targetCount(), 1U);
  const auto &Record = (*Frozen)->targets().front();
  ASSERT_EQ(Record.Macros.size(), 1U);
  EXPECT_EQ(Record.Macros[0].Name, "__NOVEL__");
  ASSERT_EQ(Record.Builtins.size(), 1U);
  EXPECT_EQ(Record.Builtins[0].Name, "__builtin_novel");
  ASSERT_EQ(Record.Registers.size(), 1U);
  EXPECT_EQ(Record.Registers[0].Aliases[0], "zero");
  ASSERT_EQ(Record.Constraints.size(), 1U);
  EXPECT_EQ(Record.Constraints[0].Spelling, "r");
  EXPECT_EQ(Record.Clobbers, "~{flags}");
}

TEST(PluginTargetRegistryTest,
     RejectsMalformedLanguageExtensionArray) {
  NevercTargetMacroDescriptor Macro{};
  Macro.Header = {sizeof(Macro), NEVERC_TARGET_API_MAJOR,
                  NEVERC_TARGET_API_MINOR, 0};
  Macro.Name = view("__BROKEN__");
  NevercTargetDescriptor Target =
      target({UINT64_C(0x1000), UINT64_C(5)},
             "test.malformed-extensions");
  Target.Macros = {&Macro, 1, sizeof(Macro) - 1};
  PluginTargetRegistrationView Plugin;
  Plugin.PluginID =
      "org.neverc.test.target-malformed-extensions";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin),
      PluginTargetRequest{});

  ASSERT_FALSE(static_cast<bool>(Frozen));
  EXPECT_NE(errorText(Frozen.takeError()).find("macro array"),
            std::string::npos);
}

TEST(PluginTargetRegistryTest, RejectsMacroTokenInjection) {
  NevercTargetMacroDescriptor Macro{};
  Macro.Header = {sizeof(Macro), NEVERC_TARGET_API_MAJOR,
                  NEVERC_TARGET_API_MINOR, 0};
  Macro.Name = view("__BROKEN__");
  Macro.Value = view("1\n#error injected");
  NevercTargetDescriptor Target =
      target({UINT64_C(0x1000), UINT64_C(6)},
             "test.macro-token-injection");
  Target.Macros = {&Macro, 1, sizeof(Macro)};
  PluginTargetRegistrationView Plugin;
  Plugin.PluginID = "org.neverc.test.target-macro-token-injection";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin),
      PluginTargetRequest{});
  ASSERT_FALSE(static_cast<bool>(Frozen));
  EXPECT_NE(errorText(Frozen.takeError()).find("invalid tokens"),
            std::string::npos);
}

TEST(PluginTargetRegistryTest, RejectsBuiltinWithoutLoweringProvider) {
  NevercTargetBuiltinDescriptor Builtin{};
  Builtin.Header = {sizeof(Builtin), NEVERC_TARGET_API_MAJOR,
                    NEVERC_TARGET_API_MINOR, 0};
  Builtin.Name = view("__builtin_missing_lowering");
  Builtin.TypeEncoding = view("ii");
  Builtin.Languages = NEVERC_TARGET_BUILTIN_LANGUAGE_C;
  NevercTargetDescriptor Target =
      target({UINT64_C(0x1000), UINT64_C(7)},
             "test.builtin-missing-lowering");
  Target.Builtins = {&Builtin, 1, sizeof(Builtin)};
  PluginTargetRegistrationView Plugin;
  Plugin.PluginID = "org.neverc.test.target-builtin-missing-lowering";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin),
      PluginTargetRequest{});
  ASSERT_FALSE(static_cast<bool>(Frozen));
  EXPECT_NE(errorText(Frozen.takeError()).find("builtin descriptor"),
            std::string::npos);
}

TEST(PluginTargetRegistryTest, RejectsIncompleteCPUCallbackSet) {
  NevercTargetDescriptor Target =
      target({UINT64_C(0x1000), UINT64_C(8)},
             "test.incomplete-cpu-callbacks");
  Target.ValidateCPU = validateTestCPU;
  PluginTargetRegistrationView Plugin;
  Plugin.PluginID = "org.neverc.test.target-incomplete-cpu-callbacks";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin),
      PluginTargetRequest{});
  ASSERT_FALSE(static_cast<bool>(Frozen));
  EXPECT_NE(errorText(Frozen.takeError()).find("complete Target CPU"),
            std::string::npos);
}

TEST(PluginTargetRegistryTest, RejectsUnknownObjectFormatReference) {
  NevercTargetDescriptor Target =
      target({UINT64_C(0x2000), UINT64_C(1)}, "test.unknown-format");
  Target.DefaultObjectFormatID = {UINT64_C(0xdead), UINT64_C(0xbeef)};

  PluginTargetRegistrationView Plugin;
  Plugin.PluginID = "org.neverc.test.target-unknown-format";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin),
      PluginTargetRequest{});
  ASSERT_FALSE(static_cast<bool>(Frozen));
  const std::string Message = errorText(Frozen.takeError());
  EXPECT_NE(Message.find("unknown object Format ID"), std::string::npos);
  EXPECT_NE(Message.find("test.unknown-format"), std::string::npos);
}

TEST(PluginTargetRegistryTest, RejectsCodeGenDependencyCycle) {
  constexpr NevercTargetID TargetID{UINT64_C(0x3000), UINT64_C(1)};
  constexpr NevercInterfaceID FirstID{UINT64_C(0x3001), UINT64_C(1)};
  constexpr NevercInterfaceID SecondID{UINT64_C(0x3001), UINT64_C(2)};
  const NevercInterfaceID FirstDependencies[] = {SecondID};
  const NevercInterfaceID SecondDependencies[] = {FirstID};
  NevercCodeGenEdgeDescriptor Edges[2]{};
  Edges[0].Header = {sizeof(Edges[0]), NEVERC_TARGET_API_MAJOR,
                     NEVERC_TARGET_API_MINOR, 0};
  Edges[0].EdgeID = FirstID;
  Edges[0].CanonicalName = view("test.edge.first");
  Edges[0].TargetID = TargetID;
  Edges[0].InputKind = NEVERC_CODEGEN_PRODUCT_IR;
  Edges[0].OutputKind = NEVERC_CODEGEN_PRODUCT_MIR;
  Edges[0].Dependencies = {FirstDependencies, 1,
                           sizeof(FirstDependencies[0])};
  Edges[1].Header = {sizeof(Edges[1]), NEVERC_TARGET_API_MAJOR,
                     NEVERC_TARGET_API_MINOR, 0};
  Edges[1].EdgeID = SecondID;
  Edges[1].CanonicalName = view("test.edge.second");
  Edges[1].TargetID = TargetID;
  Edges[1].InputKind = NEVERC_CODEGEN_PRODUCT_MIR;
  Edges[1].OutputKind = NEVERC_CODEGEN_PRODUCT_MC;
  Edges[1].Dependencies = {SecondDependencies, 1,
                           sizeof(SecondDependencies[0])};
  NevercTargetDescriptor Target = target(TargetID, "test.cyclic");

  PluginTargetRegistrationView Plugin;
  Plugin.PluginID = "org.neverc.test.target-cycle";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);
  Plugin.CodeGenEdges = Edges;

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin),
      PluginTargetRequest{});
  ASSERT_FALSE(static_cast<bool>(Frozen));
  const std::string Message = errorText(Frozen.takeError());
  EXPECT_NE(Message.find("codegen dependency cycle"), std::string::npos);
  EXPECT_NE(Message.find("test.edge.first"), std::string::npos);
  EXPECT_NE(Message.find("test.edge.second"), std::string::npos);
}

TEST(PluginTargetRegistryTest, DiagnosesMalformedTripleSeparately) {
  NevercTargetTripleMatcher Matcher =
      matcher("x86_64", "", "linux", "");
  NevercTargetDescriptor Target =
      target({UINT64_C(0x3500), UINT64_C(1)}, "test.malformed");
  Target.TripleMatchers = {&Matcher, 1, sizeof(Matcher)};
  PluginTargetRegistrationView Plugin;
  Plugin.PluginID = "org.neverc.test.target-malformed";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);
  PluginTargetRequest Request;
  Request.Triple = "x86_64--linux";

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin), Request);
  ASSERT_FALSE(static_cast<bool>(Frozen));
  EXPECT_NE(errorText(Frozen.takeError()).find("malformed target triple"),
            std::string::npos);
}

TEST(PluginTargetRegistryTest, DiagnosesUnknownCPUSeparately) {
  NevercTargetTripleMatcher Matcher =
      matcher("x86_64", "pc", "linux", "gnu");
  NevercTargetDescriptor Target =
      target({UINT64_C(0x3500), UINT64_C(2)}, "test.cpu");
  const NevercStringView CPUs[] = {view("generic")};
  Target.Machine.CPUs = {CPUs, 1, sizeof(CPUs[0])};
  Target.TripleMatchers = {&Matcher, 1, sizeof(Matcher)};
  PluginTargetRegistrationView Plugin;
  Plugin.PluginID = "org.neverc.test.target-cpu";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);
  PluginTargetRequest Request;
  Request.Triple = "x86_64-pc-linux-gnu";
  Request.CPU = "missing-cpu";

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin), Request);
  ASSERT_FALSE(static_cast<bool>(Frozen));
  EXPECT_NE(errorText(Frozen.takeError()).find("unknown CPU"),
            std::string::npos);
}

TEST(PluginTargetRegistryTest, DiagnosesIncompatibleFeaturesSeparately) {
  NevercTargetTripleMatcher Matcher =
      matcher("x86_64", "pc", "linux", "gnu");
  NevercTargetDescriptor Target =
      target({UINT64_C(0x3500), UINT64_C(3)}, "test.features");
  const NevercStringView AlphaConflicts[] = {view("beta")};
  const NevercStringView BetaConflicts[] = {view("alpha")};
  NevercTargetFeatureDescriptor Features[2]{};
  Features[0].Header = {sizeof(Features[0]), NEVERC_TARGET_API_MAJOR,
                        NEVERC_TARGET_API_MINOR, 0};
  Features[0].Name = view("alpha");
  Features[0].Conflicts = {AlphaConflicts, 1,
                           sizeof(AlphaConflicts[0])};
  Features[1].Header = {sizeof(Features[1]), NEVERC_TARGET_API_MAJOR,
                        NEVERC_TARGET_API_MINOR, 0};
  Features[1].Name = view("beta");
  Features[1].Conflicts = {BetaConflicts, 1,
                           sizeof(BetaConflicts[0])};
  Target.Machine.Features = {Features, 2, sizeof(Features[0])};
  Target.TripleMatchers = {&Matcher, 1, sizeof(Matcher)};
  PluginTargetRegistrationView Plugin;
  Plugin.PluginID = "org.neverc.test.target-features";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);
  PluginTargetRequest Request;
  Request.Triple = "x86_64-pc-linux-gnu";
  Request.Features = "+alpha,+beta";

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin), Request);
  ASSERT_FALSE(static_cast<bool>(Frozen));
  EXPECT_NE(errorText(Frozen.takeError()).find(
                "incompatible target features"),
            std::string::npos);
}

TEST(PluginTargetRegistryTest, FreezesSelectedTargetAndProviderRoute) {
  constexpr NevercTargetID TargetID{UINT64_C(0x4000), UINT64_C(1)};
  constexpr NevercTargetABIID ABIID{UINT64_C(0x4001), UINT64_C(1)};
  constexpr NevercCallingConventionID CallingConventionID{
      UINT64_C(0x4002), UINT64_C(1)};
  constexpr NevercInterfaceID SchemaID{UINT64_C(0x4003), UINT64_C(1)};
  constexpr NevercObjectFormatID FormatID{UINT64_C(0x4004), UINT64_C(1)};
  constexpr NevercInterfaceID FirstEdgeID{UINT64_C(0x4005), UINT64_C(1)};
  constexpr NevercInterfaceID SecondEdgeID{UINT64_C(0x4005), UINT64_C(2)};

  NevercTargetTripleMatcher Matcher =
      matcher("x86_64", "pc", "linux", "gnu");
  NevercTargetDescriptor Target = target(TargetID, "test.complete");
  const NevercStringView CPUs[] = {view("cli"), view("config"),
                                    view("generic")};
  Target.Machine.CPUs = {CPUs, 3, sizeof(CPUs[0])};
  Target.TripleMatchers = {&Matcher, 1, sizeof(Matcher)};
  Target.DefaultABI = ABIID;
  Target.DefaultCallingConvention = CallingConventionID;
  Target.MCSchemaID = SchemaID;
  Target.DefaultObjectFormatID = FormatID;
  const NevercInterfaceID MachineABIs[] = {ABIID};
  const NevercInterfaceID MachineCallingConventions[] = {
      CallingConventionID};
  const NevercInterfaceID MachineFormats[] = {FormatID};
  Target.Machine.ABIs = {MachineABIs, 1, sizeof(MachineABIs[0])};
  Target.Machine.CallingConventions = {
      MachineCallingConventions, 1,
      sizeof(MachineCallingConventions[0])};
  Target.Machine.ObjectFormats = {MachineFormats, 1,
                                  sizeof(MachineFormats[0])};

  NevercTargetABIDescriptor ABI{};
  ABI.Header = {sizeof(ABI), NEVERC_TARGET_ABI_API_MAJOR,
                NEVERC_TARGET_ABI_API_MINOR, 0};
  ABI.ABIID = ABIID;
  ABI.TargetID = TargetID;
  ABI.CanonicalName = view("test.abi");
  ABI.ClassifyFunction = classifyDirectABI;
  ABI.VAArg.Header = {sizeof(ABI.VAArg),
                      NEVERC_TARGET_ABI_API_MAJOR,
                      NEVERC_TARGET_ABI_API_MINOR, 0};
  ABI.VAArg.Kind = NEVERC_ABI_VA_ARG_LLVM;
  NevercCallingConventionDescriptor CallingConvention{};
  CallingConvention.Header = {
      sizeof(CallingConvention), NEVERC_CALLING_CONVENTION_API_MAJOR,
      NEVERC_CALLING_CONVENTION_API_MINOR, 0};
  CallingConvention.CallingConventionID = CallingConventionID;
  CallingConvention.TargetID = TargetID;
  CallingConvention.CanonicalName = view("test.cc");
  NevercMCSchemaDescriptor Schema{};
  Schema.Header = {sizeof(Schema), NEVERC_MC_API_MAJOR,
                   NEVERC_MC_API_MINOR, 0};
  Schema.SchemaID = SchemaID;
  Schema.TargetID = TargetID;
  Schema.CanonicalName = view("test.mc");
  Schema.Digest = view(
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  NevercMCSchemaValueDescriptor Opcode{};
  Opcode.Header = {sizeof(Opcode), NEVERC_MC_API_MAJOR,
                   NEVERC_MC_API_MINOR, 0};
  Opcode.StableID = 10;
  Opcode.BackendValue = 100;
  Opcode.CanonicalName = view("test.opcode");
  Schema.Opcodes = {&Opcode, 1, sizeof(Opcode)};
  const NevercTargetID SupportedTargets[] = {TargetID};
  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = FormatID;
  Format.CanonicalName = view("test.object");
  Format.SupportedTargets = {SupportedTargets, 1,
                             sizeof(SupportedTargets[0])};
  Format.DefaultExtension = view(".test");

  const NevercInterfaceID SecondDependencies[] = {FirstEdgeID};
  NevercCodeGenEdgeDescriptor Edges[2]{};
  Edges[0].Header = {sizeof(Edges[0]), NEVERC_TARGET_API_MAJOR,
                     NEVERC_TARGET_API_MINOR, 0};
  Edges[0].EdgeID = FirstEdgeID;
  Edges[0].CanonicalName = view("test.route.ir-to-mir");
  Edges[0].TargetID = TargetID;
  Edges[0].InputKind = NEVERC_CODEGEN_PRODUCT_IR;
  Edges[0].OutputKind = NEVERC_CODEGEN_PRODUCT_MIR;
  Edges[0].Flags = NEVERC_CODEGEN_EDGE_BUILTIN;
  Edges[1].Header = {sizeof(Edges[1]), NEVERC_TARGET_API_MAJOR,
                     NEVERC_TARGET_API_MINOR, 0};
  Edges[1].EdgeID = SecondEdgeID;
  Edges[1].CanonicalName = view("test.route.mir-to-mc");
  Edges[1].TargetID = TargetID;
  Edges[1].InputKind = NEVERC_CODEGEN_PRODUCT_MIR;
  Edges[1].OutputKind = NEVERC_CODEGEN_PRODUCT_MC;
  Edges[1].Dependencies = {SecondDependencies, 1,
                           sizeof(SecondDependencies[0])};

  PluginTargetRegistrationView Plugin;
  Plugin.PluginID = "org.neverc.test.target-complete";
  Plugin.Targets = ArrayRef<NevercTargetDescriptor>(Target);
  Plugin.ABIs = ArrayRef<NevercTargetABIDescriptor>(ABI);
  Plugin.CallingConventions =
      ArrayRef<NevercCallingConventionDescriptor>(CallingConvention);
  Plugin.MCSchemas = ArrayRef<NevercMCSchemaDescriptor>(Schema);
  Plugin.ObjectFormats =
      ArrayRef<NevercObjectFormatDescriptor>(Format);
  Plugin.CodeGenEdges = Edges;
  PluginTargetRequest Request;
  Request.Triple = "x86_64-pc-linux-gnu";
  Request.CPU = "cli";
  Request.Configuration.CPU = "config";
  Request.Platform.CPU = "platform";

  auto Frozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin), Request);
  if (!Frozen)
    FAIL() << errorText(Frozen.takeError());
  EXPECT_EQ((*Frozen)->targetCount(), 1U);
  EXPECT_EQ((*Frozen)->abiCount(), 1U);
  EXPECT_EQ((*Frozen)->callingConventionCount(), 1U);
  EXPECT_EQ((*Frozen)->mcSchemaCount(), 1U);
  EXPECT_EQ((*Frozen)->objectFormatCount(), 1U);
  ASSERT_NE((*Frozen)->selectedTarget(), nullptr);
  EXPECT_EQ((*Frozen)->selectedTarget()->CanonicalName, "test.complete");
  ASSERT_NE((*Frozen)->findABI(ABIID), nullptr);
  EXPECT_EQ((*Frozen)->findABI(ABIID)->ClassifyFunction,
            classifyDirectABI);
  ASSERT_NE(
      (*Frozen)->findCallingConvention(CallingConventionID), nullptr);
  ASSERT_NE((*Frozen)->findMCSchema(SchemaID), nullptr);
  ASSERT_EQ((*Frozen)->findMCSchema(SchemaID)->Opcodes.size(), 1U);
  EXPECT_EQ((*Frozen)->findMCSchema(SchemaID)->Opcodes[0].StableID,
            10U);
  ASSERT_NE((*Frozen)->targetKey(), nullptr);
  EXPECT_EQ(text((*Frozen)->targetKey()->view().CPU), "cli");
  ASSERT_EQ((*Frozen)->route().size(), 2U);
  EXPECT_EQ((*Frozen)->route()[0]->CanonicalName,
            "test.route.ir-to-mir");
  EXPECT_EQ((*Frozen)->route()[0]->Flags,
            NEVERC_CODEGEN_EDGE_BUILTIN);
  EXPECT_EQ((*Frozen)->route()[1]->CanonicalName,
            "test.route.mir-to-mc");

  PluginTargetRequest DefaultRequest;
  DefaultRequest.Triple = "x86_64-pc-linux-gnu";
  DefaultRequest.Platform.CPU = "platform";
  auto DefaultFrozen = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(Plugin), DefaultRequest);
  ASSERT_TRUE(static_cast<bool>(DefaultFrozen));
  ASSERT_NE((*DefaultFrozen)->targetKey(), nullptr);
  EXPECT_EQ(text((*DefaultFrozen)->targetKey()->view().CPU), "generic");
}

} // namespace
