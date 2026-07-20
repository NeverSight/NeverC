#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/TextDiagnosticBuffer.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginTargetInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc;
using namespace neverc::plugin;

namespace {

NevercStringView view(StringRef Text) {
  return {Text.data(), Text.size()};
}

struct TargetLanguageCallbackState {
  std::array<NevercStringView, 2> CPUs{
      view("fast"), view("generic")};
  std::array<NevercTargetFeatureState, 2> Features{};

  TargetLanguageCallbackState() {
    Features[0].Header = {sizeof(NevercTargetFeatureState),
                          NEVERC_TARGET_API_MAJOR,
                          NEVERC_TARGET_API_MINOR, 0};
    Features[0].Name = view("base");
    Features[0].Enabled = NEVERC_TRUE;
    Features[1].Header = Features[0].Header;
    Features[1].Name = view("simd");
  }
};

NevercStatus NEVERC_CALL validateCPU(
    NevercTaskHandle, NevercStringView CPU, void *,
    NevercBool *OutValid) {
  if (!OutValid || (!CPU.Data && CPU.Length != 0)) {
    NevercStatus Status = neverc_status_ok();
    Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
    return Status;
  }
  StringRef Name(CPU.Data ? CPU.Data : "", CPU.Length);
  *OutValid =
      Name == "fast" || Name == "generic" ? NEVERC_TRUE : NEVERC_FALSE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL canonicalizeCPU(
    NevercTaskHandle, NevercStringView CPU, void *,
    NevercStringView *OutCanonicalCPU) {
  if (!OutCanonicalCPU || (!CPU.Data && CPU.Length != 0)) {
    NevercStatus Status = neverc_status_ok();
    Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
    return Status;
  }
  StringRef Name(CPU.Data ? CPU.Data : "", CPU.Length);
  if (Name == "turbo")
    *OutCanonicalCPU = view("fast");
  else
    *OutCanonicalCPU = CPU;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL listCPUs(
    NevercTaskHandle, void *UserData,
    NevercStringArrayView *OutCPUs) {
  if (!UserData || !OutCPUs) {
    NevercStatus Status = neverc_status_ok();
    Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
    return Status;
  }
  auto *State = static_cast<TargetLanguageCallbackState *>(UserData);
  *OutCPUs = {State->CPUs.data(), State->CPUs.size(),
              sizeof(NevercStringView)};
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL resolveFeatures(
    NevercTaskHandle, NevercStringView,
    NevercStringArrayView Requested, void *UserData,
    NevercStructArrayView *OutFeatureStates) {
  if (!UserData || !OutFeatureStates ||
      (Requested.Count != 0 &&
       (!Requested.Data ||
        Requested.ElementStride < sizeof(NevercStringView)))) {
    NevercStatus Status = neverc_status_ok();
    Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
    return Status;
  }
  auto *State = static_cast<TargetLanguageCallbackState *>(UserData);
  State->Features[1].Enabled = NEVERC_FALSE;
  const auto *Bytes =
      reinterpret_cast<const uint8_t *>(Requested.Data);
  for (uint64_t I = 0; I != Requested.Count; ++I) {
    const auto *Item = reinterpret_cast<const NevercStringView *>(
        Bytes + I * Requested.ElementStride);
    StringRef Spelling(Item->Data ? Item->Data : "", Item->Length);
    if (Spelling == "+simd")
      State->Features[1].Enabled = NEVERC_TRUE;
    else if (Spelling == "-simd")
      State->Features[1].Enabled = NEVERC_FALSE;
  }
  *OutFeatureStates = {State->Features.data(), State->Features.size(),
                       sizeof(NevercTargetFeatureState)};
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL lowerBuiltin(
    void *, const NevercTargetBuiltinLoweringInvocation *Invocation,
    NevercIRValueHandle *OutResult) {
  if (!Invocation || !OutResult || Invocation->ArgumentCount == 0) {
    NevercStatus Status = neverc_status_ok();
    Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
    return Status;
  }
  *OutResult = Invocation->Arguments[0];
  return neverc_status_ok();
}

PluginTargetSnapshot::TargetRecord makeTarget(
    TargetLanguageCallbackState &Callbacks) {
  PluginTargetSnapshot::TargetRecord Record;
  Record.PluginID = "org.neverc.test.target-language";
  Record.ID = {UINT64_C(0x5100), UINT64_C(0x200)};
  Record.CanonicalName = "test.target-language";
  Record.Machine.RawTriple = "novel-acme-neveros-none";
  Record.Machine.Architecture = "novel";
  Record.Machine.Vendor = "acme";
  Record.Machine.OperatingSystem = "neveros";
  Record.Machine.Environment = "none";
  Record.Machine.DataLayout = "e-p:64:64-i64:64-n32:64-S128";
  Record.Machine.DefaultCPU = "generic";
  Record.Machine.CPUs = {"fast", "generic"};
  Record.Machine.Endianness = NEVERC_TARGET_ENDIAN_LITTLE;
  Record.Machine.PointerWidth = 64;
  Record.Machine.IntWidth = 32;
  Record.Machine.LongWidth = 64;
  Record.Machine.LongLongWidth = 64;
  Record.Machine.StackAlignment = 128;
  Record.Machine.MaximumAtomicWidth = 64;
  Record.Machine.MaximumVectorAlignment = 128;
  Record.Machine.BuiltinVaListKind = NEVERC_TARGET_VA_LIST_VOID_POINTER;
  Record.Machine.TLSSupported = true;
  Record.Machine.Features = {
      {"base", {}, {}, true},
      {"simd", {"base"}, {}, false},
  };
  Record.ValidateCPU = validateCPU;
  Record.CanonicalizeCPU = canonicalizeCPU;
  Record.ListCPUs = listCPUs;
  Record.ResolveFeatures = resolveFeatures;
  Record.TargetUserData = &Callbacks;
  Record.Builtins.push_back(
      {"__builtin_novel_identity", "ii", "nc", "", "",
       NEVERC_TARGET_BUILTIN_LANGUAGE_C, lowerBuiltin});
  Record.Registers.push_back(
      {"r0", {"zero"}, {"special-zero"}, 0});
  Record.Constraints.push_back(
      {"I", NEVERC_TARGET_CONSTRAINT_IMMEDIATE, 0, 0, {1, 3, 7}, 0,
       -1, "I"});
  Record.Constraints.push_back(
      {"r", NEVERC_TARGET_CONSTRAINT_ALLOWS_REGISTER, 0, 0, {},
       UINT32_C(0x61000001), 2, "r"});
  Record.Clobbers = "~{flags}";
  return Record;
}

TEST(PluginTargetLanguageTest,
     CPUCallbacksCanonicalizeValidateAndListOwnedResults) {
  TargetLanguageCallbackState Callbacks;
  PluginTargetInfo Target(makeTarget(Callbacks));

  EXPECT_TRUE(Target.setCPU("turbo"));
  EXPECT_TRUE(Target.isValidCPUName("fast"));
  EXPECT_FALSE(Target.isValidCPUName("missing"));

  SmallVector<StringRef, 4> CPUs;
  Target.fillValidCPUList(CPUs);
  ASSERT_EQ(CPUs.size(), 2U);
  EXPECT_EQ(CPUs[0], "fast");
  EXPECT_EQ(CPUs[1], "generic");
}

TEST(PluginTargetLanguageTest,
     FeatureCallbackProducesValidatedCanonicalMap) {
  TargetLanguageCallbackState Callbacks;
  PluginTargetInfo Target(makeTarget(Callbacks));
  CompilerInstance Compiler;
  TextDiagnosticBuffer Buffer;
  Compiler.createDiagnostics(&Buffer, /*ShouldOwnClient=*/false);

  StringMap<bool> Features;
  EXPECT_TRUE(Target.initFeatureMap(
      Features, Compiler.getDiagnostics(), "fast", {"+simd"}));
  EXPECT_TRUE(Features.lookup("base"));
  EXPECT_TRUE(Features.lookup("simd"));

  StringMap<bool> Invalid;
  EXPECT_FALSE(Target.initFeatureMap(
      Invalid, Compiler.getDiagnostics(), "fast", {"+unknown"}));
}

TEST(PluginTargetLanguageTest,
     RegisterAliasesAndStructuredConstraintsRemainStable) {
  TargetLanguageCallbackState Callbacks;
  PluginTargetInfo Target(makeTarget(Callbacks));

  EXPECT_TRUE(Target.isValidGCCRegisterName("r0"));
  EXPECT_TRUE(Target.isValidGCCRegisterName("zero"));
  EXPECT_TRUE(Target.isValidGCCRegisterName("special-zero"));
  EXPECT_EQ(Target.getClobbers(), "~{flags}");

  const char *Immediate = "I";
  TargetInfo::ConstraintInfo ImmediateInfo("I", "");
  ASSERT_TRUE(Target.validateAsmConstraint(Immediate, ImmediateInfo));
  EXPECT_TRUE(
      ImmediateInfo.isValidAsmImmediate(APInt(32, 3, true)));
  EXPECT_FALSE(
      ImmediateInfo.isValidAsmImmediate(APInt(32, 4, true)));

  const VerifiedTargetConstraint *Register =
      Target.findPluginConstraint("r");
  ASSERT_NE(Register, nullptr);
  EXPECT_EQ(Register->RegisterClassID, UINT32_C(0x61000001));
  EXPECT_EQ(Register->MatchingOperand, 2);
}

TEST(PluginTargetLanguageTest,
     BuiltinDescriptorCarriesRequiredLoweringProvider) {
  TargetLanguageCallbackState Callbacks;
  PluginTargetInfo Target(makeTarget(Callbacks));
  const VerifiedTargetBuiltin *Builtin =
      Target.getPluginBuiltin(Builtin::FirstTSBuiltin);
  ASSERT_NE(Builtin, nullptr);
  EXPECT_EQ(Builtin->Name, "__builtin_novel_identity");
  EXPECT_EQ(Builtin->Lower, lowerBuiltin);
}

TEST(PluginTargetLanguageTest,
     CFixturePublishesLanguageAndMachineCapabilitiesAtomically) {
  PluginProcessServices Services{"neverc-plugin-target-language-tests", 1};
  ASSERT_FALSE(registerPluginTargetInterfaces(Services));
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded =
      Services.registry().load(NEVERC_TEST_TARGET_LANGUAGE_PLUGIN);
  ASSERT_TRUE(static_cast<bool>(Loaded));
  auto Plan = makePluginActivationPlan(
      Services.registry(), {"org.neverc.test.target-language"});
  ASSERT_TRUE(static_cast<bool>(Plan));
  if (Error Activation = activatePluginPlan(Services, *Plan))
    FAIL() << toString(std::move(Activation)).str().str();

  const PluginPublishedRegistration *Registration =
      (*Loaded)->registration();
  ASSERT_NE(Registration, nullptr);
  const PluginRegistrationRecord *TargetRecord = nullptr;
  const PluginRegistrationRecord *ABIRecord = nullptr;
  const PluginRegistrationRecord *CallingConventionRecord = nullptr;
  for (const PluginRegistrationRecord &Record :
       Registration->records()) {
    switch (Record.Kind) {
    case PluginRegistrationKind::Target:
      TargetRecord = &Record;
      break;
    case PluginRegistrationKind::TargetABI:
      ABIRecord = &Record;
      break;
    case PluginRegistrationKind::CallingConvention:
      CallingConventionRecord = &Record;
      break;
    default:
      break;
    }
  }

  ASSERT_NE(TargetRecord, nullptr);
  ASSERT_NE(ABIRecord, nullptr);
  ASSERT_NE(CallingConventionRecord, nullptr);
  EXPECT_EQ(TargetRecord->Target.Macros.Count, 1U);
  EXPECT_EQ(TargetRecord->Target.Builtins.Count, 1U);
  EXPECT_EQ(TargetRecord->Target.Registers.Count, 1U);
  EXPECT_EQ(TargetRecord->Target.Constraints.Count, 2U);
  EXPECT_NE(TargetRecord->Target.ValidateCPU, nullptr);
  EXPECT_NE(TargetRecord->Target.CanonicalizeCPU, nullptr);
  EXPECT_NE(TargetRecord->Target.ListCPUs, nullptr);
  EXPECT_NE(TargetRecord->Target.ResolveFeatures, nullptr);
  EXPECT_NE(ABIRecord->TargetABI.ClassifyFunction, nullptr);
}

} // namespace
