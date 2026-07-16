#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginDriver.h"
#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(NEVERC_PLUGIN_ABI_MAJOR == 1);
static_assert(NEVERC_PLUGIN_ABI_MINOR == 0);
static_assert(sizeof(NevercABITableHeader) == 16);
static_assert(alignof(NevercABITableHeader) == 8);
static_assert(offsetof(NevercABITableHeader, StructSize) == 0);
static_assert(offsetof(NevercABITableHeader, Flags) == 8);
static_assert(sizeof(NevercStringView) == 16);
static_assert(sizeof(NevercInterfaceID) == 16);
static_assert(sizeof(NevercHandle) == 16);
static_assert(sizeof(NevercStatus) == 16);
static_assert(sizeof(NevercDiagnosticNote) == 32);
static_assert(sizeof(NevercDiagnosticDescriptor) == 160);
static_assert(sizeof(NevercPhaseResult) == 56);
static_assert(offsetof(NevercCompatibilityKey, Header) == 0);
static_assert(offsetof(NevercInterfaceRequirement, Header) == 0);
static_assert(offsetof(NevercPluginDependency, Header) == 0);
static_assert(offsetof(NevercDiagnosticDescriptor, Header) == 0);
static_assert(offsetof(NevercPhaseResult, Header) == 0);
static_assert(offsetof(NevercPhaseFrame, Header) == 0);
static_assert(offsetof(NevercBootstrapAPI, Header) == 0);
static_assert(offsetof(NevercCoreAPI, Header) == 0);
static_assert(offsetof(NevercRegistrarAPI, Header) == 0);
static_assert(offsetof(NevercPluginDescriptor, Header) == 0);
static_assert(offsetof(NevercDriverAPI, Header) == 0);
static_assert(offsetof(NevercToolChainRequest, Header) == 0);
static_assert(offsetof(NevercToolChainSelectionDescriptor, Header) == 0);
static_assert(offsetof(NevercToolChainSelection, Header) == 0);
static_assert(offsetof(NevercDriverInput, Header) == 0);
static_assert(offsetof(NevercActionNode, Header) == 0);
static_assert(offsetof(NevercActionNodeDescriptor, Header) == 0);
static_assert(offsetof(NevercJobFile, Header) == 0);
static_assert(offsetof(NevercPluginJobContext, Header) == 0);
static_assert(offsetof(NevercJobDescriptor, Header) == 0);
static_assert(offsetof(NevercJob, Header) == 0);
static_assert(offsetof(NevercJobExecutionRequest, Header) == 0);
static_assert(offsetof(NevercJobResultDescriptor, Header) == 0);
static_assert(offsetof(NevercJobResult, Header) == 0);
static_assert(sizeof(NevercToolChainRequest) == 136);
static_assert(sizeof(NevercToolChainSelectionDescriptor) == 120);
static_assert(sizeof(NevercToolChainSelection) == 128);
static_assert(sizeof(NevercDriverInput) == 48);
static_assert(sizeof(NevercActionNode) == 72);
static_assert(sizeof(NevercActionNodeDescriptor) == 80);
static_assert(sizeof(NevercJobFile) == 40);
static_assert(sizeof(NevercPluginJobContext) == 152);
static_assert(sizeof(NevercJobDescriptor) == 216);
static_assert(sizeof(NevercJob) == 128);
static_assert(sizeof(NevercJobExecutionRequest) == 264);
static_assert(sizeof(NevercJobResultDescriptor) == 96);
static_assert(sizeof(NevercJobResult) == 96);
static_assert(sizeof(NevercDriverAPI) == 560);
static_assert(std::is_standard_layout_v<NevercToolChainRequest>);
static_assert(std::is_standard_layout_v<NevercToolChainSelection>);
static_assert(std::is_standard_layout_v<NevercActionNode>);
static_assert(std::is_same_v<NevercBool, std::uint32_t>);
static_assert(std::is_same_v<NevercStatusCode, std::int32_t>);
static_assert(std::is_same_v<NevercPhaseAction, std::uint32_t>);
static_assert(std::is_same_v<NevercTaskKind, std::uint32_t>);

namespace {

TEST(PluginABIContractTest, ReportsInitialPublicVersion) {
  EXPECT_EQ(NEVERC_PLUGIN_ABI_MAJOR, 1);
  EXPECT_EQ(NEVERC_PLUGIN_ABI_MINOR, 0);
  EXPECT_STREQ(NEVERC_PLUGIN_ENTRY_POINT, "neverc_plugin_entry");
}

TEST(PluginABIContractTest, StatusAndNullHandleHelpersAreUnambiguous) {
  NevercStatus Status = neverc_status_ok();
  EXPECT_EQ(neverc_status_is_ok(Status), NEVERC_TRUE);
  EXPECT_EQ(Status.Flags, NEVERC_STATUS_FLAG_NONE);
  EXPECT_EQ(Status.Detail, 0u);

  EXPECT_EQ(neverc_handle_is_null(NevercHandle{0, 0}), NEVERC_TRUE);
  EXPECT_EQ(neverc_handle_is_null(NevercHandle{1, 0}), NEVERC_FALSE);
  EXPECT_EQ(neverc_handle_is_null(NevercHandle{0, 1}), NEVERC_FALSE);
}

TEST(PluginABIContractTest, FieldAvailabilityUsesProducerSize) {
  NevercPluginDescriptor Descriptor{};
  Descriptor.Header.StructSize =
      static_cast<std::uint32_t>(offsetof(NevercPluginDescriptor, Register));
  EXPECT_TRUE(NEVERC_ABI_FIELD_AVAILABLE(
      &Descriptor.Header, NevercPluginDescriptor, ProcessBegin));
  EXPECT_FALSE(NEVERC_ABI_FIELD_AVAILABLE(
      &Descriptor.Header, NevercPluginDescriptor, Register));

  Descriptor.Header.StructSize = sizeof(Descriptor);
  EXPECT_TRUE(NEVERC_ABI_FIELD_AVAILABLE(
      &Descriptor.Header, NevercPluginDescriptor, Destroy));
}

TEST(PluginABIContractTest, PublicStructsUseEightByteMaximumPacking) {
  EXPECT_EQ(alignof(NevercPluginDescriptor), 8u);
  EXPECT_EQ(alignof(NevercCoreAPI), 8u);
  EXPECT_EQ(sizeof(NevercPhaseResult) % 8u, 0u);
}

} // namespace
