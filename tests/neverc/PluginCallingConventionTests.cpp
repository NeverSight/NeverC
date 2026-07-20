#include "neverc/Plugin/Host/CallingConventionPlan.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

NevercABITypeDescriptor type(NevercABITypeKind Kind,
                              uint32_t Width,
                              uint32_t Alignment) {
  NevercABITypeDescriptor Type{};
  Type.Header = {sizeof(Type), NEVERC_TARGET_ABI_API_MAJOR,
                 NEVERC_TARGET_ABI_API_MINOR, 0};
  Type.Kind = Kind;
  Type.BitWidth = Width;
  Type.Alignment = Alignment;
  return Type;
}

NevercStatus NEVERC_CALL planFunction(
    void *, const NevercCallingConventionQuery *Query,
    NevercCallingConventionPlan *Plan) {
  static std::array<NevercCallingConventionLocation, 1> Returns;
  static std::array<NevercCallingConventionLocation, 2> Arguments;
  static const std::array<uint32_t, 1> CalleeSaved = {12};

  if (!Query || !Plan || Query->Function.Parameters.Count != 2)
    return NevercStatus{NEVERC_STATUS_INVALID_ARGUMENT, 0};

  Returns = {};
  Returns[0].Header = {
      sizeof(Returns[0]), NEVERC_CALLING_CONVENTION_API_MAJOR,
      NEVERC_CALLING_CONVENTION_API_MINOR, 0};
  Returns[0].Kind = NEVERC_CC_LOCATION_REGISTER;
  Returns[0].ValueIndex = 0;
  Returns[0].Size = 4;
  Returns[0].Alignment = 4;
  Returns[0].RegisterNumber = 10;

  Arguments = {};
  Arguments[0].Header = {
      sizeof(Arguments[0]), NEVERC_CALLING_CONVENTION_API_MAJOR,
      NEVERC_CALLING_CONVENTION_API_MINOR, 0};
  Arguments[0].Kind = NEVERC_CC_LOCATION_REGISTER;
  Arguments[0].ValueIndex = 0;
  Arguments[0].Size = 4;
  Arguments[0].Alignment = 4;
  Arguments[0].RegisterNumber = 11;
  Arguments[1].Header = Arguments[0].Header;
  Arguments[1].Kind = NEVERC_CC_LOCATION_STACK;
  Arguments[1].ValueIndex = 1;
  Arguments[1].Size = 8;
  Arguments[1].Alignment = 8;

  Plan->ReturnLocations = {
      Returns.data(), Returns.size(),
      sizeof(NevercCallingConventionLocation)};
  Plan->ArgumentLocations = {
      Arguments.data(), Arguments.size(),
      sizeof(NevercCallingConventionLocation)};
  Plan->CalleeSavedRegisters = {
      CalleeSaved.data(), CalleeSaved.size(), sizeof(uint32_t)};
  Plan->StackAlignment = 16;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL planForeignRegister(
    void *UserData, const NevercCallingConventionQuery *Query,
    NevercCallingConventionPlan *Plan) {
  NevercStatus Status = planFunction(UserData, Query, Plan);
  if (neverc_status_is_ok(Status))
    const_cast<NevercCallingConventionLocation *>(
        static_cast<const NevercCallingConventionLocation *>(
            Plan->ReturnLocations.Data))
        ->RegisterNumber = 99;
  return Status;
}

NevercStatus NEVERC_CALL planMisalignedStack(
    void *UserData, const NevercCallingConventionQuery *Query,
    NevercCallingConventionPlan *Plan) {
  NevercStatus Status = planFunction(UserData, Query, Plan);
  if (neverc_status_is_ok(Status)) {
    auto *Locations =
        const_cast<NevercCallingConventionLocation *>(
            static_cast<const NevercCallingConventionLocation *>(
                Plan->ArgumentLocations.Data));
    Locations[1].StackOffset = 3;
  }
  return Status;
}

TEST(PluginCallingConventionTest,
     MaterializesValidatedRegisterAndStackPlan) {
  PluginTargetSnapshot::TargetRecord Target;
  Target.CanonicalName = "test.cc-target";
  Target.ID = {0x100, 0x200};
  Target.Machine.SchemaDigest =
      "0123456789abcdef0123456789abcdef"
      "0123456789abcdef0123456789abcdef";
  Target.Machine.StackAlignment = 128;
  Target.Registers = {
      {"ret", {}, {}, 10},
      {"arg", {}, {}, 11},
      {"saved", {}, {}, 12},
  };

  PluginTargetSnapshot::NamedRecord Convention;
  Convention.CanonicalName = "test.cc";
  Convention.ID = {0x300, 0x400};
  Convention.TargetID = Target.ID;
  Convention.PlanCallingConvention = planFunction;

  CallingConventionPlanner Planner(Convention, Target);
  const std::vector<NevercABITypeDescriptor> Parameters = {
      type(NEVERC_ABI_TYPE_INTEGER, 32, 4),
      type(NEVERC_ABI_TYPE_INTEGER, 64, 8)};
  auto Plan = Planner.materialize(
      type(NEVERC_ABI_TYPE_INTEGER, 32, 4), Parameters,
      /*Variadic=*/false, /*RequiredArguments=*/2);

  ASSERT_TRUE(static_cast<bool>(Plan))
      << errorText(Plan.takeError());
  ASSERT_EQ(Plan->ReturnLocations.size(), 1U);
  EXPECT_EQ(Plan->ReturnLocations[0].RegisterNumber, 10U);
  ASSERT_EQ(Plan->ArgumentLocations.size(), 2U);
  EXPECT_EQ(Plan->ArgumentLocations[1].Kind,
            NEVERC_CC_LOCATION_STACK);
  ASSERT_EQ(Plan->CalleeSavedRegisters.size(), 1U);
  EXPECT_EQ(Plan->CalleeSavedRegisters[0], 12U);
  EXPECT_EQ(Plan->StackAlignment, 16U);
  EXPECT_NE(Plan->serialize().find("neverc-cc-plan-v1"),
            std::string::npos);
}

TEST(PluginCallingConventionTest, RejectsForeignPhysicalRegister) {
  PluginTargetSnapshot::TargetRecord Target;
  Target.CanonicalName = "test.cc-target";
  Target.ID = {0x100, 0x200};
  Target.Machine.SchemaDigest =
      "0123456789abcdef0123456789abcdef"
      "0123456789abcdef0123456789abcdef";
  Target.Machine.StackAlignment = 128;
  Target.Registers = {
      {"ret", {}, {}, 10},
      {"arg", {}, {}, 11},
      {"saved", {}, {}, 12},
  };
  PluginTargetSnapshot::NamedRecord Convention;
  Convention.CanonicalName = "test.cc";
  Convention.ID = {0x300, 0x400};
  Convention.TargetID = Target.ID;
  Convention.PlanCallingConvention = planForeignRegister;
  CallingConventionPlanner Planner(Convention, Target);
  const std::vector<NevercABITypeDescriptor> Parameters = {
      type(NEVERC_ABI_TYPE_INTEGER, 32, 4),
      type(NEVERC_ABI_TYPE_INTEGER, 64, 8)};

  auto Plan = Planner.materialize(
      type(NEVERC_ABI_TYPE_INTEGER, 32, 4), Parameters, false, 2);

  ASSERT_FALSE(static_cast<bool>(Plan));
  EXPECT_NE(errorText(Plan.takeError()).find(
                "not in the target schema"),
            std::string::npos);
}

TEST(PluginCallingConventionTest, RejectsMisalignedStackSlot) {
  PluginTargetSnapshot::TargetRecord Target;
  Target.CanonicalName = "test.cc-target";
  Target.ID = {0x100, 0x200};
  Target.Machine.SchemaDigest =
      "0123456789abcdef0123456789abcdef"
      "0123456789abcdef0123456789abcdef";
  Target.Machine.StackAlignment = 128;
  Target.Registers = {
      {"ret", {}, {}, 10},
      {"arg", {}, {}, 11},
      {"saved", {}, {}, 12},
  };
  PluginTargetSnapshot::NamedRecord Convention;
  Convention.CanonicalName = "test.cc";
  Convention.ID = {0x300, 0x400};
  Convention.TargetID = Target.ID;
  Convention.PlanCallingConvention = planMisalignedStack;
  CallingConventionPlanner Planner(Convention, Target);
  const std::vector<NevercABITypeDescriptor> Parameters = {
      type(NEVERC_ABI_TYPE_INTEGER, 32, 4),
      type(NEVERC_ABI_TYPE_INTEGER, 64, 8)};

  auto Plan = Planner.materialize(
      type(NEVERC_ABI_TYPE_INTEGER, 32, 4), Parameters, false, 2);

  ASSERT_FALSE(static_cast<bool>(Plan));
  EXPECT_NE(errorText(Plan.takeError()).find("misaligned"),
            std::string::npos);
}

} // namespace
