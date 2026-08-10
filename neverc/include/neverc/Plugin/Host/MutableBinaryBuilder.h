#ifndef NEVERC_PLUGIN_HOST_MUTABLEBINARYBUILDER_H
#define NEVERC_PLUGIN_HOST_MUTABLEBINARYBUILDER_H

#include "neverc/Plugin/PluginObject.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <mutex>
#include <vector>

namespace neverc::plugin {

class PluginTaskContext;
class PluginPhaseExecutor;

class MutableBinaryBuilder {
public:
  static llvm::Expected<std::unique_ptr<MutableBinaryBuilder>>
  create(PluginTaskContext &Task, const NevercIOAPI &IO,
         NevercOutputSinkHandle Sink);

  ~MutableBinaryBuilder();

  MutableBinaryBuilder(const MutableBinaryBuilder &) = delete;
  MutableBinaryBuilder &operator=(const MutableBinaryBuilder &) = delete;

  const NevercMutableBinaryAPI &api() const { return UnrestrictedFacade->API; }
  const NevercMutableBinaryAPI &readOnlyAPI() const {
    return ReadOnlyFacade->API;
  }
  const NevercMutableBinaryAPI &
  capabilityAPI(const PluginPhaseExecutor &Executor, uint64_t Token);
  const NevercMutableBinaryAPI &capabilityAPI(const void *Domain,
                                              uint64_t Token);
  NevercMutableBinaryBuilderHandle handle() const { return Handle; }
  llvm::ArrayRef<uint8_t> bytes() const { return Bytes; }

  llvm::Expected<NevercOutputSummary> summary() const;
  llvm::Expected<NevercOutputSeal> finish();
  NevercStatus abort();

private:
  struct OwnerControl {
    std::recursive_mutex Mutex;
    MutableBinaryBuilder *Owner = nullptr;
  };

  struct APIFacade {
    NevercMutableBinaryAPI API{};
    PluginTaskContext *Task = nullptr;
    NevercTaskHandle TaskHandle{};
    std::shared_ptr<OwnerControl> Control;
    const void *MutationDomain = nullptr;
    uint64_t Token = 0;
    bool MutationAllowed = false;
  };

  class OwnerLease {
  public:
    OwnerLease() = default;
    OwnerLease(std::shared_ptr<OwnerControl> ControlValue,
               std::unique_lock<std::recursive_mutex> LockValue,
               MutableBinaryBuilder *OwnerValue)
        : Control(std::move(ControlValue)), Lock(std::move(LockValue)),
          Owner(OwnerValue) {}
    explicit operator bool() const { return Owner != nullptr; }
    MutableBinaryBuilder &operator*() const { return *Owner; }
    MutableBinaryBuilder *operator->() const { return Owner; }

  private:
    std::shared_ptr<OwnerControl> Control;
    std::unique_lock<std::recursive_mutex> Lock;
    MutableBinaryBuilder *Owner = nullptr;
  };

  MutableBinaryBuilder(PluginTaskContext &Task, const NevercIOAPI &IO,
                       NevercOutputSinkHandle Sink);
  std::shared_ptr<APIFacade> createFacade(bool AllowMutation,
                                          const void *MutationDomain = nullptr,
                                          uint64_t Token = 0);
  static OwnerLease acquire(APIFacade &Facade, NevercTaskHandle Task,
                            NevercMutableBinaryBuilderHandle Builder,
                            bool RequireMutation, NevercStatus &Status);
  static NevercStatus NEVERC_CALL
  reserve(void *Context, NevercTaskHandle Task,
          NevercMutableBinaryBuilderHandle Builder, uint64_t Size);
  static NevercStatus NEVERC_CALL write(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, NevercByteView Bytes);
  static NevercStatus NEVERC_CALL writeAt(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, uint64_t Offset,
      NevercByteView Bytes);
  static NevercStatus NEVERC_CALL tell(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, uint64_t *OutPosition);
  static NevercStatus NEVERC_CALL readAt(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, uint64_t Offset,
      NevercMutableByteView Bytes);
  static NevercStatus NEVERC_CALL insert(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, uint64_t Offset,
      NevercByteView Bytes);
  static NevercStatus NEVERC_CALL append(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, NevercByteView Bytes);
  static NevercStatus NEVERC_CALL resize(
      void *Context, NevercTaskHandle Task,
      NevercMutableBinaryBuilderHandle Builder, uint64_t Size);
  NevercStatus rewrite(NevercTaskHandle Task,
                       const std::vector<uint8_t> &Replacement);
  NevercStatus validate(NevercTaskHandle Task,
                        NevercMutableBinaryBuilderHandle Builder) const;

  PluginTaskContext &Task;
  const NevercIOAPI &IO;
  NevercOutputSinkHandle Sink{};
  std::shared_ptr<OwnerControl> Control;
  std::shared_ptr<APIFacade> UnrestrictedFacade;
  std::shared_ptr<APIFacade> ReadOnlyFacade;
  std::vector<std::shared_ptr<APIFacade>> CapabilityFacades;
  NevercMutableBinaryBuilderHandle Handle{};
  std::vector<uint8_t> Bytes;
};

} // namespace neverc::plugin

#endif
