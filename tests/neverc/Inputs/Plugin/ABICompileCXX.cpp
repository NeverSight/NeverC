#pragma pack(push, 16)
#include "neverc/Plugin/PluginCore.h"

struct alignas(16) CallerAlignedMember {
  unsigned char Bytes[16];
};

struct CallerPackSixteenProbe {
  unsigned char Prefix;
  CallerAlignedMember Value;
};
#pragma pack(pop)

#include <cstddef>
#include <type_traits>

static_assert(NEVERC_PLUGIN_ABI_MAJOR == 1);
static_assert(sizeof(NevercABITableHeader) == 16);
static_assert(alignof(NevercABITableHeader) == 8);
static_assert(offsetof(CallerPackSixteenProbe, Value) == 16);
static_assert(std::is_standard_layout_v<NevercPluginDescriptor>);
static_assert(std::is_same_v<NevercPluginEntryFn,
                             NevercStatus(NEVERC_CALL *)(
                                 const NevercBootstrapAPI *,
                                 NevercPluginDescriptor *)>);

static NevercStatus NEVERC_CALL
compileOnlyProvider(const NevercPhaseFrame *Frame,
                    NevercPhaseResult *OutResult, void *UserData) {
  (void)Frame;
  (void)OutResult;
  (void)UserData;
  return neverc_status_ok();
}

NevercPhaseProviderFn NevercPluginCoreCXXCompileFixture =
    compileOnlyProvider;
