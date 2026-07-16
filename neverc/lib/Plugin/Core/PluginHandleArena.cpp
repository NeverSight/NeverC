#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "llvm/ADT/Twine.h"
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus handleStatus(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

Error handleError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

} // namespace

struct PluginHandleArena::Slot {
  PluginHandleKind Kind = 0;
  uint16_t Generation = 1;
  void *Payload = nullptr;
  DestroyFn Destroy;
  bool Occupied = false;
  bool Retired = false;
};

PluginHandleArena::PluginHandleArena(
    PluginProcessServices &ProcessServicesValue, uint64_t SessionOwnerValue,
    uint64_t ScopeOwnerValue, uint16_t InitialGenerationValue,
    uint32_t MaximumSlotsValue)
    : ProcessServices(ProcessServicesValue), SessionOwner(SessionOwnerValue),
      ScopeOwner(ScopeOwnerValue),
      InitialGeneration(InitialGenerationValue == 0 ? 1
                                                    : InitialGenerationValue),
      MaximumSlots(MaximumSlotsValue) {}

PluginHandleArena::~PluginHandleArena() { invalidateAll(); }

Expected<NevercHandle>
PluginHandleArena::create(PluginHandleKind Kind, void *Payload,
                          DestroyFn Destroy) {
  if (Kind == 0)
    return handleError("plugin handle kind must be nonzero");
  if (!Payload)
    return handleError("plugin handle payload must be non-null");

  std::lock_guard<std::mutex> Lock(Mutex);
  if (Closed)
    return handleError("plugin handle arena is closed");

  uint32_t Index = 0;
  Slot *Storage = nullptr;
  if (!FreeSlots.empty()) {
    Index = FreeSlots.back();
    FreeSlots.pop_back();
    Storage = Slots[Index].get();
  } else {
    if (Slots.size() >= MaximumSlots)
      return handleError("plugin handle slot space is exhausted");
    Index = static_cast<uint32_t>(Slots.size());
    auto NewSlot = std::make_unique<Slot>();
    NewSlot->Generation = InitialGeneration;
    Storage = NewSlot.get();
    Slots.push_back(std::move(NewSlot));
  }

  Storage->Kind = Kind;
  Storage->Payload = Payload;
  Storage->Destroy = std::move(Destroy);
  Storage->Occupied = true;
  ++LiveCount;
  return NevercHandle{
      ScopeOwner,
      encode(Kind, Storage->Generation, Index + UINT32_C(1))};
}

NevercStatus PluginHandleArena::resolve(NevercHandle Handle,
                                        PluginHandleKind ExpectedKind,
                                        void **OutPayload) const {
  if (!OutPayload)
    return handleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutPayload = nullptr;
  std::lock_guard<std::mutex> Lock(Mutex);
  Slot *Storage = nullptr;
  NevercStatus Status =
      validateLocked(Handle, ExpectedKind, &Storage);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutPayload = Storage->Payload;
  return neverc_status_ok();
}

NevercStatus PluginHandleArena::release(NevercHandle Handle,
                                        PluginHandleKind ExpectedKind) {
  DestroyFn Destroy;
  void *Payload = nullptr;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    Slot *Storage = nullptr;
    NevercStatus Status =
        validateLocked(Handle, ExpectedKind, &Storage);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;

    Payload = Storage->Payload;
    Destroy = std::move(Storage->Destroy);
    Storage->Payload = nullptr;
    Storage->Kind = 0;
    Storage->Occupied = false;
    --LiveCount;
    // Slots are individually allocated, so derive the index from the wire
    // value.
    uint32_t Index =
        static_cast<uint32_t>(Handle.Value & UINT64_C(0xffffffff)) - 1;
    if (Storage->Generation == std::numeric_limits<uint16_t>::max()) {
      Storage->Retired = true;
      ++RetiredCount;
    } else {
      ++Storage->Generation;
      FreeSlots.push_back(Index);
    }
  }
  if (Destroy) {
    try {
      Destroy(Payload);
    } catch (...) {
      return handleStatus(NEVERC_STATUS_PLUGIN_EXCEPTION);
    }
  }
  return neverc_status_ok();
}

void PluginHandleArena::invalidateAll() {
  std::vector<std::pair<DestroyFn, void *>> Destroyers;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Closed)
      return;
    Closed = true;
    Destroyers.reserve(LiveCount);
    for (auto &Storage : Slots) {
      if (!Storage->Occupied)
        continue;
      Destroyers.emplace_back(std::move(Storage->Destroy),
                              Storage->Payload);
      Storage->Payload = nullptr;
      Storage->Kind = 0;
      Storage->Occupied = false;
      if (Storage->Generation ==
          std::numeric_limits<uint16_t>::max()) {
        if (!Storage->Retired) {
          Storage->Retired = true;
          ++RetiredCount;
        }
      } else {
        ++Storage->Generation;
      }
    }
    LiveCount = 0;
    FreeSlots.clear();
  }
  for (auto It = Destroyers.rbegin(); It != Destroyers.rend(); ++It) {
    if (!It->first)
      continue;
    try {
      It->first(It->second);
    } catch (...) {
    }
  }
}

size_t PluginHandleArena::liveCount() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return LiveCount;
}

size_t PluginHandleArena::retiredSlotCount() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return RetiredCount;
}

NevercStatus PluginHandleArena::validateLocked(
    NevercHandle Handle, PluginHandleKind ExpectedKind,
    Slot **OutSlot) const {
  if (OutSlot)
    *OutSlot = nullptr;
  if (ExpectedKind == 0 || Handle.Owner == 0 || Handle.Value == 0)
    return handleStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercStatus OwnerStatus = ProcessServices.classifyScopeOwner(
      SessionOwner, ScopeOwner, Handle.Owner);
  if (OwnerStatus.Code != NEVERC_STATUS_OK)
    return OwnerStatus;

  PluginHandleKind EncodedKind =
      static_cast<PluginHandleKind>(Handle.Value >> 48);
  uint16_t Generation =
      static_cast<uint16_t>((Handle.Value >> 32) & UINT64_C(0xffff));
  uint32_t EncodedIndex =
      static_cast<uint32_t>(Handle.Value & UINT64_C(0xffffffff));
  if (EncodedKind == 0 || Generation == 0 || EncodedIndex == 0 ||
      EncodedIndex > Slots.size())
    return handleStatus(NEVERC_STATUS_STALE_HANDLE);
  Slot *Storage = Slots[EncodedIndex - 1].get();
  if (!Storage->Occupied || Storage->Retired ||
      Storage->Generation != Generation)
    return handleStatus(NEVERC_STATUS_STALE_HANDLE);
  if (Storage->Kind != EncodedKind ||
      Storage->Kind != ExpectedKind)
    return handleStatus(NEVERC_STATUS_WRONG_TYPE);
  if (OutSlot)
    *OutSlot = Storage;
  return neverc_status_ok();
}

uint64_t PluginHandleArena::encode(PluginHandleKind Kind,
                                   uint16_t Generation,
                                   uint32_t SlotIndex) {
  return (static_cast<uint64_t>(Kind) << 48) |
         (static_cast<uint64_t>(Generation) << 32) | SlotIndex;
}

} // namespace neverc::plugin
