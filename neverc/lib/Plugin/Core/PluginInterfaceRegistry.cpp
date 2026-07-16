#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include <algorithm>
#include <cstddef>
#include <tuple>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error interfaceError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

bool equalID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

auto interfaceKey(NevercInterfaceID Interface, uint16_t Major) {
  return std::tuple<uint64_t, uint64_t, uint16_t>(
      Interface.High, Interface.Low, Major);
}

NevercStatus status(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

} // namespace

Error PluginInterfaceRegistry::registerInterface(
    NevercInterfaceID Interface, NevercInterfaceStability Stability,
    const void *Table, OwnedCompatibilityKey Compatibility) {
  if (Frozen)
    return interfaceError("plugin interface registry is frozen");
  if (Interface.High == 0 && Interface.Low == 0)
    return interfaceError("plugin interface ID must not be zero");
  if (!Table)
    return interfaceError("plugin interface table must not be null");
  if (Stability != NEVERC_INTERFACE_STABLE &&
      Stability != NEVERC_INTERFACE_LOCKSTEP)
    return interfaceError("plugin interface has invalid stability");

  const auto *Header = static_cast<const NevercABITableHeader *>(Table);
  if (Header->StructSize < sizeof(NevercABITableHeader))
    return interfaceError("plugin interface table is shorter than its header");
  if (Header->Major == 0)
    return interfaceError("plugin interface major version must not be zero");
  if (Header->Flags != 0)
    return interfaceError("plugin interface table has unsupported flags");
  if (llvm::any_of(Records, [&](const InterfaceRecord &Record) {
        return equalID(Record.Interface, Interface) &&
               Record.Major == Header->Major;
      }))
    return interfaceError("duplicate plugin interface ID and major version");

  if (Stability == NEVERC_INTERFACE_LOCKSTEP &&
      (Compatibility.ProducerBuildID.empty() ||
       Compatibility.TargetABIKey.empty() || Compatibility.LLVMMajor == 0))
    return interfaceError(
        "lockstep plugin interface requires a complete compatibility key");

  InterfaceRecord Record;
  Record.Interface = Interface;
  Record.Major = Header->Major;
  Record.Minor = Header->Minor;
  Record.StructSize = Header->StructSize;
  Record.Stability = Stability;
  Record.Table = Table;
  Record.Compatibility = std::move(Compatibility);
  Records.push_back(std::move(Record));
  return Error::success();
}

Error PluginInterfaceRegistry::freeze() {
  if (Frozen)
    return Error::success();
  llvm::sort(Records, [](const InterfaceRecord &Left,
                         const InterfaceRecord &Right) {
    return interfaceKey(Left.Interface, Left.Major) <
           interfaceKey(Right.Interface, Right.Major);
  });
  Frozen = true;
  return Error::success();
}

const PluginInterfaceRegistry::InterfaceRecord *
PluginInterfaceRegistry::find(NevercInterfaceID Interface,
                              uint16_t Major) const {
  auto Key = interfaceKey(Interface, Major);
  auto It = std::lower_bound(
      Records.begin(), Records.end(), Key,
      [](const InterfaceRecord &Record, const auto &Wanted) {
        return interfaceKey(Record.Interface, Record.Major) < Wanted;
      });
  if (It == Records.end() || !equalID(It->Interface, Interface) ||
      It->Major != Major)
    return nullptr;
  return &*It;
}

bool PluginInterfaceRegistry::containsID(NevercInterfaceID Interface) const {
  return llvm::any_of(Records, [&](const InterfaceRecord &Record) {
    return equalID(Record.Interface, Interface);
  });
}

Expected<InterfaceQueryResult>
PluginInterfaceRegistry::query(NevercInterfaceID Interface, uint16_t Major,
                               uint16_t MinimumMinor) const {
  if (!Frozen)
    return interfaceError("plugin interface registry is not frozen");
  const InterfaceRecord *Record = find(Interface, Major);
  if (!Record) {
    if (containsID(Interface))
      return interfaceError("plugin interface major version is unavailable");
    return interfaceError("plugin interface is unavailable");
  }
  if (Record->Minor < MinimumMinor)
    return interfaceError("plugin interface minor version is too old");

  return InterfaceQueryResult{Record->Table, Record->Minor, Record->StructSize,
                              Record->Stability, &Record->Compatibility};
}

Error PluginInterfaceRegistry::validateRequirement(
    const OwnedInterfaceRequirement &Requirement) const {
  auto Result =
      query(Requirement.Interface, Requirement.Major,
            Requirement.MinimumMinor);
  if (!Result)
    return Result.takeError();
  if (Result->Stability != Requirement.Stability)
    return interfaceError("plugin interface stability does not match");
  if (Requirement.Stability == NEVERC_INTERFACE_LOCKSTEP) {
    const OwnedCompatibilityKey &Actual = *Result->Compatibility;
    const OwnedCompatibilityKey &Expected = Requirement.Compatibility;
    if (Actual.ProducerBuildID != Expected.ProducerBuildID ||
        Actual.TargetABIKey != Expected.TargetABIKey ||
        Actual.LLVMMajor != Expected.LLVMMajor)
      return interfaceError(
          "plugin interface lockstep compatibility key does not match");
  }
  return Error::success();
}

NevercStatus PluginInterfaceRegistry::queryForC(
    NevercInterfaceID Interface, uint16_t Major, uint16_t MinimumMinor,
    const void **OutTable, uint16_t *OutMinor, uint64_t *OutStructSize) const {
  if (OutTable)
    *OutTable = nullptr;
  if (OutMinor)
    *OutMinor = 0;
  if (OutStructSize)
    *OutStructSize = 0;
  if (!OutTable || !OutMinor || !OutStructSize)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  if (!Frozen)
    return status(NEVERC_STATUS_INVALID_STATE);

  const InterfaceRecord *Record = find(Interface, Major);
  if (!Record)
    return status(containsID(Interface) ? NEVERC_STATUS_VERSION_MISMATCH
                                        : NEVERC_STATUS_MISSING_INTERFACE);
  if (Record->Minor < MinimumMinor)
    return status(NEVERC_STATUS_VERSION_MISMATCH);

  *OutTable = Record->Table;
  *OutMinor = Record->Minor;
  *OutStructSize = Record->StructSize;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL
queryPluginInterface(void *Context, NevercInterfaceID Interface,
                     uint16_t Major, uint16_t MinimumMinor,
                     const void **OutTable, uint16_t *OutMinor,
                     uint64_t *OutStructSize) {
  if (!Context) {
    if (OutTable)
      *OutTable = nullptr;
    if (OutMinor)
      *OutMinor = 0;
    if (OutStructSize)
      *OutStructSize = 0;
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  return static_cast<const PluginInterfaceRegistry *>(Context)->queryForC(
      Interface, Major, MinimumMinor, OutTable, OutMinor, OutStructSize);
}

} // namespace neverc::plugin
