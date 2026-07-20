#include "neverc/Plugin/Host/MCLayoutEngine.h"
#include "neverc/Plugin/Host/MCUnit.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TargetID{UINT64_C(0x4e43504d434c4159), 1};
constexpr NevercInterfaceID SchemaID{UINT64_C(0x4e43504d43534348), 15};
constexpr NevercInterfaceID BackendID{UINT64_C(0x4e43504d43424143), 15};
constexpr char SchemaDigest[] =
    "1515151515151515151515151515151515151515151515151515151515151515";

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

NevercStringView view(const char *Value) {
  return {Value, std::char_traits<char>::length(Value)};
}

NevercStatus status(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

class MCTaskScope {
public:
  MCTaskScope()
      : Services("neverc-plugin-mc-layout-tests", LLVM_VERSION_MAJOR) {}

  bool initialize() {
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto CreatedPlan = makePluginActivationPlan(Services.registry(), {});
    if (!CreatedPlan) {
      ADD_FAILURE() << errorText(CreatedPlan.takeError());
      return false;
    }
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorText(CreatedSession.takeError());
      return false;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_CODEGEN);
    if (!CreatedTask) {
      ADD_FAILURE() << errorText(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~MCTaskScope() {
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
};

NevercTargetDescriptor targetDescriptor() {
  NevercTargetDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_TARGET_API_MAJOR,
                       NEVERC_TARGET_API_MINOR, 0};
  Descriptor.TargetID = TargetID;
  Descriptor.CanonicalName = view("test.mc-layout-target");
  Descriptor.MCSchemaID = SchemaID;
  Descriptor.Machine.Header = {
      sizeof(Descriptor.Machine), NEVERC_TARGET_API_MAJOR,
      NEVERC_TARGET_API_MINOR, 0};
  Descriptor.Machine.RawTriple = view("test-unknown-none-none");
  Descriptor.Machine.Architecture = view("test");
  Descriptor.Machine.DataLayout =
      view("e-p:64:64-i64:64-n32:64-S128");
  Descriptor.Machine.DefaultCPU = view("generic");
  Descriptor.Machine.SchemaDigest = view(SchemaDigest);
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

NevercMCSchemaDescriptor schemaDescriptor() {
  NevercMCSchemaDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_MC_API_MAJOR,
                       NEVERC_MC_API_MINOR, 0};
  Descriptor.SchemaID = SchemaID;
  Descriptor.TargetID = TargetID;
  Descriptor.CanonicalName = view("test.mc-layout-schema");
  Descriptor.Digest = view(SchemaDigest);
  return Descriptor;
}

NevercStatus NEVERC_CALL getFixupKindInfo(
    void *, NevercMCFixupKind Kind, uint32_t,
    NevercMCFixupKindInfo *OutInfo) {
  if (!OutInfo)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  OutInfo->TargetOffset = 0;
  switch (Kind) {
  case NEVERC_MC_FIXUP_PC_REL_1:
    OutInfo->TargetSize = 8;
    OutInfo->Flags = NEVERC_MC_FIXUP_INFO_PC_RELATIVE |
                     NEVERC_MC_FIXUP_INFO_SIGNED |
                     NEVERC_MC_FIXUP_INFO_RELAXABLE;
    return neverc_status_ok();
  case NEVERC_MC_FIXUP_PC_REL_4:
    OutInfo->TargetSize = 32;
    OutInfo->Flags = NEVERC_MC_FIXUP_INFO_PC_RELATIVE |
                     NEVERC_MC_FIXUP_INFO_SIGNED;
    return neverc_status_ok();
  case NEVERC_MC_FIXUP_DATA_2:
    OutInfo->TargetSize = 16;
    return neverc_status_ok();
  default:
    return status(NEVERC_STATUS_NOT_FOUND);
  }
}

NevercStatus NEVERC_CALL mapRelocation(
    void *, const NevercMCLayoutFixupRequest *,
    uint32_t *OutRelocationKind) {
  if (!OutRelocationKind)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutRelocationKind = 42;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL shouldRelax(
    void *, const NevercMCLayoutFixupRequest *Request,
    NevercBool *OutRelax) {
  if (!Request || !OutRelax)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutRelax =
      Request->MayRelax == NEVERC_TRUE &&
              Request->Width == 8 &&
              (!Request->IsResolved ||
               Request->Value < -128 || Request->Value > 127)
          ? NEVERC_TRUE
          : NEVERC_FALSE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL relaxFragment(
    void *, const NevercMCLayoutFixupRequest *,
    NevercMCRelaxationResult *OutResult) {
  static const uint8_t LongBranch[] = {0xe9, 0, 0, 0, 0};
  if (!OutResult)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  OutResult->Changed = NEVERC_TRUE;
  OutResult->ReplacementBytes = {LongBranch, sizeof(LongBranch)};
  OutResult->NewFixupOffset = 1;
  OutResult->NewFixupWidth = 32;
  OutResult->NewFixupKind = NEVERC_MC_FIXUP_PC_REL_4;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL applyFixup(
    void *, const NevercMCLayoutFixupRequest *Request,
    NevercMutableByteView Bytes) {
  if (!Request || !Bytes.Data || Request->Width % 8 != 0 ||
      Request->FixupOffset + Request->Width / 8 > Bytes.Length)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  uint64_t Value = static_cast<uint64_t>(Request->Value);
  for (uint32_t I = 0; I != Request->Width / 8; ++I)
    Bytes.Data[Request->FixupOffset + I] =
        static_cast<uint8_t>(Value >> (I * 8));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL writeNops(
    void *, uint64_t Count, const NevercMCByteSink *Sink) {
  static const std::array<uint8_t, 64> Nops = [] {
    std::array<uint8_t, 64> Bytes{};
    Bytes.fill(UINT8_C(0x90));
    return Bytes;
  }();
  if (!Sink || Count > Nops.size())
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  return Sink->WriteBytes(Sink->Context, {Nops.data(), Count});
}

NevercStatus NEVERC_CALL alwaysRelax(
    void *, const NevercMCLayoutFixupRequest *,
    NevercBool *OutRelax) {
  if (!OutRelax)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutRelax = NEVERC_TRUE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL growFragment(
    void *, const NevercMCLayoutFixupRequest *Request,
    NevercMCRelaxationResult *OutResult) {
  static const std::array<uint8_t, 64> Bytes{};
  if (!Request || !OutResult ||
      Request->FragmentSize >= Bytes.size())
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  OutResult->Changed = NEVERC_TRUE;
  OutResult->ReplacementBytes = {
      Bytes.data(), Request->FragmentSize + 1};
  OutResult->NewFixupOffset = 0;
  OutResult->NewFixupWidth = 8;
  OutResult->NewFixupKind = NEVERC_MC_FIXUP_PC_REL_1;
  return neverc_status_ok();
}

struct LayoutEnvironment {
  MCTaskScope Scope;
  std::shared_ptr<const PluginTargetSnapshot> Targets;
  std::shared_ptr<const MCLayoutBackendRegistry> Backends;

  bool initialize(
      uint32_t MaximumIterations = 8,
      NevercMCShouldRelaxFixupFn ShouldRelax = shouldRelax,
      NevercMCRelaxFragmentFn Relax = relaxFragment) {
    if (!Scope.initialize())
      return false;
    NevercTargetDescriptor Target = targetDescriptor();
    NevercMCSchemaDescriptor Schema = schemaDescriptor();
    PluginTargetRegistrationView TargetRegistration;
    TargetRegistration.PluginID = "org.neverc.test.mc-layout";
    TargetRegistration.Targets = ArrayRef<NevercTargetDescriptor>(Target);
    TargetRegistration.MCSchemas =
        ArrayRef<NevercMCSchemaDescriptor>(Schema);
    auto FrozenTargets = PluginTargetRegistry::freeze(
        ArrayRef<PluginTargetRegistrationView>(TargetRegistration),
        PluginTargetRequest{});
    if (!FrozenTargets) {
      ADD_FAILURE() << errorText(FrozenTargets.takeError());
      return false;
    }
    Targets = std::move(*FrozenTargets);

    NevercMCAsmBackendDescriptor Backend{};
    Backend.Header = {sizeof(Backend), NEVERC_MC_API_MAJOR,
                      NEVERC_MC_API_MINOR, 0};
    Backend.ProviderID = BackendID;
    Backend.TargetID = TargetID;
    Backend.SchemaID = SchemaID;
    Backend.MaximumLayoutIterations = MaximumIterations;
    Backend.MinimumInstructionAlignment = 1;
    Backend.GetFixupKindInfo = getFixupKindInfo;
    Backend.MapRelocation = mapRelocation;
    Backend.ShouldRelaxFixup = ShouldRelax;
    Backend.RelaxFragment = Relax;
    Backend.ApplyFixup = applyFixup;
    Backend.WriteNops = writeNops;
    MCLayoutBackendRegistrationView Registration;
    Registration.PluginID = "org.neverc.test.mc-layout";
    Registration.Backends =
        ArrayRef<NevercMCAsmBackendDescriptor>(Backend);
    auto FrozenBackends = MCLayoutBackendRegistry::freeze(
        ArrayRef<MCLayoutBackendRegistrationView>(Registration),
        *Targets);
    if (!FrozenBackends) {
      ADD_FAILURE() << errorText(FrozenBackends.takeError());
      return false;
    }
    Backends = std::move(*FrozenBackends);
    return true;
  }
};

std::unique_ptr<PluginMCUnit> branchUnit(int64_t TargetValue,
                                        bool MayRelax = true,
                                        uint32_t Width = 8) {
  auto Unit = std::make_unique<PluginMCUnit>();
  Unit->setTargetIdentity(TargetID, SchemaDigest);
  auto Section = std::make_unique<PluginMCSection>();
  Section->Name = ".text";
  Section->Flags =
      NEVERC_MC_SECTION_ALLOCATED | NEVERC_MC_SECTION_EXECUTABLE;
  auto Fragment = std::make_unique<PluginMCFragment>();
  Fragment->Parent = Section.get();
  Fragment->Kind = NEVERC_MC_FRAGMENT_RELAXABLE;
  Fragment->Contents = {UINT8_C(0x70), 0};
  auto Expression = std::make_unique<PluginMCExpression>();
  Expression->Kind = NEVERC_MC_EXPRESSION_CONSTANT;
  Expression->Constant = TargetValue;
  auto Fixup = std::make_unique<PluginMCFixup>();
  Fixup->Parent = Fragment.get();
  Fixup->Expression = Expression.get();
  Fixup->Offset = Width == 8 ? 1 : 0;
  Fixup->Width = Width;
  Fixup->IsPCRelative = true;
  Fixup->IsSigned = true;
  Fixup->MayRelax = MayRelax;
  Fixup->Kind = Width == 8 ? NEVERC_MC_FIXUP_PC_REL_1
                           : NEVERC_MC_FIXUP_PC_REL_4;
  Fragment->Fixups.push_back(std::move(Fixup));
  Section->Fragments.push_back(std::move(Fragment));
  Unit->expressions().push_back(std::move(Expression));
  Unit->sections().push_back(std::move(Section));
  return Unit;
}

std::unique_ptr<PluginMCUnit> unresolvedUnit() {
  auto Unit = std::make_unique<PluginMCUnit>();
  Unit->setTargetIdentity(TargetID, SchemaDigest);
  auto Section = std::make_unique<PluginMCSection>();
  Section->Name = ".text";
  auto Fragment = std::make_unique<PluginMCFragment>();
  Fragment->Parent = Section.get();
  Fragment->Kind = NEVERC_MC_FRAGMENT_ENCODED_WITH_FIXUPS;
  Fragment->Contents.resize(4);
  auto Symbol = std::make_unique<PluginMCSymbol>();
  Symbol->Name = "external";
  Symbol->Definition = NEVERC_MC_SYMBOL_DEFINITION_UNDEFINED;
  auto Expression = std::make_unique<PluginMCExpression>();
  Expression->Kind = NEVERC_MC_EXPRESSION_SYMBOL_REF;
  Expression->Symbol = Symbol.get();
  auto Fixup = std::make_unique<PluginMCFixup>();
  Fixup->Parent = Fragment.get();
  Fixup->Expression = Expression.get();
  Fixup->Width = 32;
  Fixup->IsPCRelative = true;
  Fixup->IsSigned = true;
  Fixup->Kind = NEVERC_MC_FIXUP_PC_REL_4;
  Fragment->Fixups.push_back(std::move(Fixup));
  Section->Fragments.push_back(std::move(Fragment));
  Unit->symbols().push_back(std::move(Symbol));
  Unit->expressions().push_back(std::move(Expression));
  Unit->sections().push_back(std::move(Section));
  return Unit;
}

TEST(PluginMCLayoutTest, RelaxesShortBranchAndProducesStableDigest) {
  LayoutEnvironment Environment;
  ASSERT_TRUE(Environment.initialize());
  auto Engine = MCLayoutEngine::create(
      Environment.Backends, Environment.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(Engine))
      << errorText(Engine.takeError());

  auto First = branchUnit(300);
  auto FirstResult =
      (*Engine)->layout(Environment.Scope.task(), *First, {});
  ASSERT_TRUE(static_cast<bool>(FirstResult))
      << errorText(FirstResult.takeError());
  EXPECT_EQ(FirstResult->Iterations, 2U);
  ASSERT_EQ(FirstResult->Sections.size(), 1U);
  ASSERT_EQ(FirstResult->Sections[0].Bytes.size(), 5U);
  EXPECT_EQ(FirstResult->Sections[0].Bytes[0], UINT8_C(0xe9));
  EXPECT_EQ(FirstResult->Relocations.size(), 0U);
  EXPECT_EQ(FirstResult->Digest.size(), 64U);

  auto Second = branchUnit(300);
  auto SecondResult =
      (*Engine)->layout(Environment.Scope.task(), *Second, {});
  ASSERT_TRUE(static_cast<bool>(SecondResult))
      << errorText(SecondResult.takeError());
  EXPECT_EQ(SecondResult->Digest, FirstResult->Digest);
  EXPECT_EQ(SecondResult->Sections[0].Bytes,
            FirstResult->Sections[0].Bytes);
}

TEST(PluginMCLayoutTest, EmitsRelocationForUnresolvedSymbol) {
  LayoutEnvironment Environment;
  ASSERT_TRUE(Environment.initialize());
  auto Engine = MCLayoutEngine::create(
      Environment.Backends, Environment.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(Engine))
      << errorText(Engine.takeError());
  auto Unit = unresolvedUnit();
  auto Result = (*Engine)->layout(Environment.Scope.task(), *Unit, {});
  ASSERT_TRUE(static_cast<bool>(Result))
      << errorText(Result.takeError());
  ASSERT_EQ(Result->Relocations.size(), 1U);
  EXPECT_EQ(Result->Relocations[0].RelocationKind, 42U);
  EXPECT_EQ(Result->Relocations[0].SymbolName, "external");
  EXPECT_TRUE(Result->Relocations[0].IsPCRelative);
}

TEST(PluginMCLayoutTest, RejectsOverlappingAndOverflowingFixups) {
  LayoutEnvironment Environment;
  ASSERT_TRUE(Environment.initialize());
  auto Engine = MCLayoutEngine::create(
      Environment.Backends, Environment.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(Engine))
      << errorText(Engine.takeError());

  auto Overlap = branchUnit(1, false, 32);
  auto &Fragment =
      *Overlap->sections().front()->Fragments.front();
  Fragment.Contents.resize(4);
  auto Extra = std::make_unique<PluginMCFixup>();
  Extra->Parent = &Fragment;
  Extra->Expression = Overlap->expressions().front().get();
  Extra->Offset = 1;
  Extra->Width = 16;
  Extra->Kind = NEVERC_MC_FIXUP_DATA_2;
  Fragment.Fixups.push_back(std::move(Extra));
  auto OverlapResult =
      (*Engine)->layout(Environment.Scope.task(), *Overlap, {});
  ASSERT_FALSE(static_cast<bool>(OverlapResult));
  EXPECT_NE(errorText(OverlapResult.takeError()).find("overlapping"),
            std::string::npos);

  auto Overflow = branchUnit(300, false);
  auto OverflowResult =
      (*Engine)->layout(Environment.Scope.task(), *Overflow, {});
  ASSERT_FALSE(static_cast<bool>(OverflowResult));
  EXPECT_NE(errorText(OverflowResult.takeError()).find("out of range"),
            std::string::npos);
}

TEST(PluginMCLayoutTest, NonConvergenceRollsBackUnit) {
  LayoutEnvironment Environment;
  ASSERT_TRUE(Environment.initialize(2, alwaysRelax, growFragment));
  auto Engine = MCLayoutEngine::create(
      Environment.Backends, Environment.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(Engine))
      << errorText(Engine.takeError());
  auto Unit = branchUnit(1);
  auto &Fragment = *Unit->sections().front()->Fragments.front();
  const std::vector<uint8_t> Original = Fragment.Contents;
  auto Result = (*Engine)->layout(Environment.Scope.task(), *Unit, {});
  ASSERT_FALSE(static_cast<bool>(Result));
  EXPECT_NE(errorText(Result.takeError()).find("did not converge"),
            std::string::npos);
  EXPECT_EQ(Fragment.Contents, Original);
  EXPECT_EQ(Fragment.Fixups.front()->Width, 8U);
}

TEST(PluginMCLayoutTest, MaterializesExecutableAlignmentWithTargetNops) {
  LayoutEnvironment Environment;
  ASSERT_TRUE(Environment.initialize());
  auto Engine = MCLayoutEngine::create(
      Environment.Backends, Environment.Targets, TargetID);
  ASSERT_TRUE(static_cast<bool>(Engine))
      << errorText(Engine.takeError());

  PluginMCUnit Unit;
  Unit.setTargetIdentity(TargetID, SchemaDigest);
  auto Section = std::make_unique<PluginMCSection>();
  Section->Name = ".text";
  Section->Flags =
      NEVERC_MC_SECTION_ALLOCATED | NEVERC_MC_SECTION_EXECUTABLE;
  auto First = std::make_unique<PluginMCFragment>();
  First->Parent = Section.get();
  First->Contents = {UINT8_C(0x11)};
  auto Second = std::make_unique<PluginMCFragment>();
  Second->Parent = Section.get();
  Second->Alignment = 4;
  Second->Contents = {UINT8_C(0x22)};
  Section->Fragments.push_back(std::move(First));
  Section->Fragments.push_back(std::move(Second));
  Unit.sections().push_back(std::move(Section));

  auto Result = (*Engine)->layout(Environment.Scope.task(), Unit, {});
  ASSERT_TRUE(static_cast<bool>(Result))
      << errorText(Result.takeError());
  ASSERT_EQ(Result->Sections.size(), 1U);
  const std::vector<uint8_t> Expected = {
      UINT8_C(0x11), UINT8_C(0x90), UINT8_C(0x90),
      UINT8_C(0x90), UINT8_C(0x22)};
  EXPECT_EQ(Result->Sections[0].Bytes, Expected);
}

} // namespace
