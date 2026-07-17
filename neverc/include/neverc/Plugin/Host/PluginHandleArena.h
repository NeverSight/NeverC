#ifndef NEVERC_PLUGIN_HOST_PLUGINHANDLEARENA_H
#define NEVERC_PLUGIN_HOST_PLUGINHANDLEARENA_H

#include "neverc/Plugin/PluginCore.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace neverc::plugin {

class PluginProcessServices;

using PluginHandleKind = uint16_t;

constexpr PluginHandleKind PluginArtifactHandleKind = 1;
constexpr PluginHandleKind PluginProofHandleKind = 2;
constexpr PluginHandleKind PluginDiagnosticHandleKind = 3;
constexpr PluginHandleKind PluginExtensionHandleKind = 4;
constexpr PluginHandleKind PluginSourceLocationHandleKind = 20;
constexpr PluginHandleKind PluginSourceRangeHandleKind = 21;
constexpr PluginHandleKind PluginSourceFileHandleKind = 22;
constexpr PluginHandleKind PluginTokenHandleKind = 29;
constexpr PluginHandleKind PluginTokenStreamHandleKind = 30;
constexpr PluginHandleKind PluginIdentifierHandleKind = 31;
constexpr PluginHandleKind PluginMacroDefinitionHandleKind = 32;
constexpr PluginHandleKind PluginMacroDirectiveHandleKind = 33;
constexpr PluginHandleKind PluginTokenBuilderHandleKind = 34;
constexpr PluginHandleKind PluginMacroArgumentHandleKind = 35;
constexpr PluginHandleKind PluginTokenStreamBuilderHandleKind = 36;
constexpr PluginHandleKind PluginASTEntityHandleKind = 37;
constexpr PluginHandleKind PluginASTBuilderHandleKind = 38;
constexpr PluginHandleKind PluginASTMutationHandleKind = 39;
constexpr PluginHandleKind PluginParserCursorHandleKind = 40;
constexpr PluginHandleKind PluginParserCheckpointHandleKind = 41;
constexpr PluginHandleKind PluginParsedAttributeHandleKind = 42;
constexpr PluginHandleKind PluginSemaScopeHandleKind = 43;
constexpr PluginHandleKind PluginSemaLookupResultHandleKind = 44;
constexpr PluginHandleKind PluginSemaMutationLeaseHandleKind = 45;
constexpr PluginHandleKind PluginSemaConversionSequenceHandleKind = 46;
constexpr PluginHandleKind PluginSemaConstantValueHandleKind = 47;

class PluginHandleArena {
public:
  using DestroyFn = std::function<void(void *)>;

  PluginHandleArena(
      PluginProcessServices &ProcessServices, uint64_t SessionOwner,
      uint64_t ScopeOwner, uint16_t InitialGeneration = 1,
      uint32_t MaximumSlots = std::numeric_limits<uint32_t>::max());
  ~PluginHandleArena();

  PluginHandleArena(const PluginHandleArena &) = delete;
  PluginHandleArena &operator=(const PluginHandleArena &) = delete;

  llvm::Expected<NevercHandle> create(PluginHandleKind Kind, void *Payload,
                                      DestroyFn Destroy = {});
  NevercStatus resolve(NevercHandle Handle, PluginHandleKind ExpectedKind,
                       void **OutPayload) const;
  NevercStatus release(NevercHandle Handle, PluginHandleKind ExpectedKind);
  void invalidateAll();

  uint64_t sessionOwner() const { return SessionOwner; }
  uint64_t scopeOwner() const { return ScopeOwner; }
  size_t liveCount() const;
  size_t retiredSlotCount() const;

private:
  struct Slot;

  NevercStatus validateLocked(NevercHandle Handle,
                              PluginHandleKind ExpectedKind,
                              Slot **OutSlot) const;
  static uint64_t encode(PluginHandleKind Kind, uint16_t Generation,
                         uint32_t SlotIndex);

  PluginProcessServices &ProcessServices;
  uint64_t SessionOwner;
  uint64_t ScopeOwner;
  uint16_t InitialGeneration;
  uint32_t MaximumSlots;
  mutable std::mutex Mutex;
  std::vector<std::unique_ptr<Slot>> Slots;
  std::vector<uint32_t> FreeSlots;
  size_t LiveCount = 0;
  size_t RetiredCount = 0;
  bool Closed = false;
};

} // namespace neverc::plugin

#endif
