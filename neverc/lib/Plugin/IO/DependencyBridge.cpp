#include "DependencyBridge.h"
#include "PluginFileSystem.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr PluginHandleKind PluginDependencyHandleKind = 28;
constexpr uint64_t MaximumDependencyTextBytes = UINT64_C(1) << 20;

NevercStatus dependencyStatus(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

bool copyText(NevercStringView View, bool AllowEmpty, std::string &Out) {
  if (View.Length > MaximumDependencyTextBytes ||
      View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  StringRef Text(View.Data ? View.Data : "",
                 static_cast<size_t>(View.Length));
  if ((!AllowEmpty && Text.empty()) || Text.contains('\0') ||
      !json::isUTF8(Text))
    return false;
  Out = Text.str();
  return true;
}

NevercStatus NEVERC_CALL recordDependency(
    void *Context, NevercTaskHandle TaskHandle,
    const NevercDependencyDescriptor *Descriptor,
    NevercDependencyHandle *OutDependency) {
  if (!Context || !Descriptor || !OutDependency)
    return dependencyStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutDependency = {};
  if (Descriptor->Header.StructSize < sizeof(*Descriptor) ||
      Descriptor->Header.Major != NEVERC_IO_API_MAJOR ||
      Descriptor->Header.Minor > NEVERC_IO_API_MINOR ||
      Descriptor->Header.Flags != 0 || Descriptor->Reserved != 0 ||
      Descriptor->ContentDigest.Length != 32 ||
      !Descriptor->ContentDigest.Data ||
      Descriptor->Kind < NEVERC_INPUT_DEPENDENCY_SOURCE ||
      Descriptor->Kind > NEVERC_INPUT_DEPENDENCY_PLUGIN ||
      (Descriptor->System != NEVERC_FALSE &&
       Descriptor->System != NEVERC_TRUE))
    return dependencyStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  auto Snapshot = std::make_shared<PluginDependencySnapshot>();
  if (!copyText(Descriptor->CanonicalPath, false,
                Snapshot->CanonicalPath) ||
      !copyText(Descriptor->ProviderID, true, Snapshot->ProviderID))
    return dependencyStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  std::copy_n(Descriptor->ContentDigest.Data,
              Snapshot->ContentDigest.size(),
              Snapshot->ContentDigest.begin());
  Snapshot->Kind = Descriptor->Kind;
  Snapshot->System = Descriptor->System == NEVERC_TRUE;

  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  PluginTaskContext *Task = Bridge.services().findTaskScope(TaskHandle);
  if (!Task)
    return dependencyStatus(NEVERC_STATUS_STALE_HANDLE);
  if (Task->isEnded())
    return dependencyStatus(NEVERC_STATUS_INVALID_STATE);

  auto *Payload =
      new (std::nothrow) std::shared_ptr<PluginDependencySnapshot>(Snapshot);
  if (!Payload)
    return dependencyStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Task->handles().create(
      PluginDependencyHandleKind, Payload, [](void *Value) {
        delete static_cast<
            std::shared_ptr<PluginDependencySnapshot> *>(Value);
      });
  if (!Handle) {
    delete Payload;
    consumeError(Handle.takeError());
    return dependencyStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Bridge.recordDependency(*Task, std::move(Snapshot));
  *OutDependency = *Handle;
  return neverc_status_ok();
}

} // namespace

void PluginIOProcessBridge::recordDependency(
    PluginTaskContext &Task,
    std::shared_ptr<PluginDependencySnapshot> Dependency) {
  const auto Key =
      std::make_pair(Task.handle().Owner, Task.handle().Value);
  std::lock_guard<std::mutex> Lock(Mutex);
  TaskDependencies[Key].push_back(std::move(Dependency));
}

std::vector<PluginDependencySnapshot>
PluginIOProcessBridge::dependencies(NevercTaskHandle Task) const {
  const auto Key = std::make_pair(Task.Owner, Task.Value);
  std::lock_guard<std::mutex> Lock(Mutex);
  std::vector<PluginDependencySnapshot> Result;
  auto It = TaskDependencies.find(Key);
  if (It == TaskDependencies.end())
    return Result;
  Result.reserve(It->second.size());
  for (const auto &Dependency : It->second)
    Result.push_back(*Dependency);
  return Result;
}

void initializePluginDependencyAPI(NevercIOAPI &API,
                                   PluginIOProcessBridge &) {
  API.RecordDependency = recordDependency;
}

std::vector<PluginDependencySnapshot>
getPluginDependencies(PluginTaskContext &Task) {
  auto Bridge = findPluginIOProcessBridge(Task.processServices());
  return Bridge ? Bridge->dependencies(Task.handle())
                : std::vector<PluginDependencySnapshot>();
}

} // namespace neverc::plugin
