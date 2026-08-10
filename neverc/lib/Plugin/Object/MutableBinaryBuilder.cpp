#include "neverc/Plugin/Host/MutableBinaryBuilder.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Errc.h"
#include <algorithm>
#include <limits>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus binaryStatus(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

Error statusError(StringRef Operation, NevercStatus Status) {
  return createStringError(
      errc::invalid_argument,
      Operation + " failed with status " + Twine(Status.Code) +
          " (detail " + Twine(Status.Detail) + ")");
}

} // namespace

MutableBinaryBuilder::MutableBinaryBuilder(PluginTaskContext &TaskValue,
                                           const NevercIOAPI &IOValue,
                                           NevercOutputSinkHandle SinkValue)
    : Task(TaskValue), IO(IOValue), Sink(SinkValue) {
  Control = std::make_shared<OwnerControl>();
  Control->Owner = this;
  UnrestrictedFacade = createFacade(true);
  ReadOnlyFacade = createFacade(false);
}

std::shared_ptr<MutableBinaryBuilder::APIFacade>
MutableBinaryBuilder::createFacade(bool AllowMutation,
                                   const void *MutationDomain, uint64_t Token) {
  auto Facade = std::make_shared<APIFacade>();
  Facade->Task = &Task;
  Facade->TaskHandle = Task.handle();
  Facade->Control = Control;
  Facade->MutationDomain = MutationDomain;
  Facade->Token = Token;
  Facade->MutationAllowed = AllowMutation;
  Facade->API.Header = {sizeof(Facade->API), NEVERC_OBJECT_API_MAJOR,
                        NEVERC_OBJECT_API_MINOR, 0};
  Facade->API.Context = Facade.get();
  Facade->API.Reserve = reserve;
  Facade->API.Write = write;
  Facade->API.WriteAt = writeAt;
  Facade->API.Tell = tell;
  Facade->API.ReadAt = readAt;
  Facade->API.Insert = insert;
  Facade->API.Append = append;
  Facade->API.Resize = resize;
  Task.retainCallbackContext(Facade);
  return Facade;
}

const NevercMutableBinaryAPI &
MutableBinaryBuilder::capabilityAPI(const PluginPhaseExecutor &Executor,
                                    uint64_t Token) {
  return capabilityAPI(&Executor, Token);
}

const NevercMutableBinaryAPI &
MutableBinaryBuilder::capabilityAPI(const void *Domain, uint64_t Token) {
  if (!Domain || Token == 0)
    return ReadOnlyFacade->API;
  std::lock_guard<std::recursive_mutex> Lock(Control->Mutex);
  auto Existing = std::find_if(
      CapabilityFacades.begin(), CapabilityFacades.end(),
      [&](const std::shared_ptr<APIFacade> &Facade) {
        return Facade->MutationDomain == Domain && Facade->Token == Token;
      });
  if (Existing != CapabilityFacades.end())
    return (*Existing)->API;
  auto Facade = createFacade(true, Domain, Token);
  const NevercMutableBinaryAPI &Result = Facade->API;
  CapabilityFacades.push_back(std::move(Facade));
  return Result;
}

Expected<std::unique_ptr<MutableBinaryBuilder>>
MutableBinaryBuilder::create(PluginTaskContext &Task, const NevercIOAPI &IO,
                             NevercOutputSinkHandle Sink) {
  if (neverc_handle_is_null(Sink) || !IO.OutputWrite || !IO.OutputWriteAt ||
      !IO.OutputTell || !IO.OutputTruncate || !IO.OutputFinish ||
      !IO.OutputAbort || !IO.OutputGetSummary)
    return createStringError(
        errc::invalid_argument,
        "mutable binary builder requires a complete output API and sink");

  auto Result = std::unique_ptr<MutableBinaryBuilder>(
      new MutableBinaryBuilder(Task, IO, Sink));
  auto Handle =
      Task.handles().create(PluginMutableBinaryBuilderHandleKind, Result.get());
  if (!Handle)
    return Handle.takeError();
  Result->Handle = *Handle;
  return Result;
}

MutableBinaryBuilder::~MutableBinaryBuilder() {
  std::unique_lock<std::recursive_mutex> Lock(Control->Mutex);
  if (!neverc_handle_is_null(Handle))
    (void)Task.handles().release(Handle, PluginMutableBinaryBuilderHandleKind);
  Control->Owner = nullptr;
}

NevercStatus
MutableBinaryBuilder::validate(NevercTaskHandle TaskHandle,
                               NevercMutableBinaryBuilderHandle Builder) const {
  if (!sameHandle(TaskHandle, Task.handle()))
    return binaryStatus(NEVERC_STATUS_WRONG_SCOPE);
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Builder, PluginMutableBinaryBuilderHandleKind, &Payload);
  if (!neverc_status_is_ok(Status))
    return Status;
  if (Payload != this || !sameHandle(Builder, Handle))
    return binaryStatus(NEVERC_STATUS_STALE_HANDLE);
  if (Task.isEnding() || Task.isEnded())
    return binaryStatus(NEVERC_STATUS_INVALID_STATE);
  return neverc_status_ok();
}

MutableBinaryBuilder::OwnerLease
MutableBinaryBuilder::acquire(APIFacade &Facade, NevercTaskHandle TaskHandle,
                              NevercMutableBinaryBuilderHandle Builder,
                              bool RequireMutation, NevercStatus &Status) {
  if (!sameHandle(TaskHandle, Facade.TaskHandle)) {
    Status = binaryStatus(NEVERC_STATUS_WRONG_SCOPE);
    return {};
  }
  if (RequireMutation && (!Facade.MutationAllowed ||
                          (Facade.MutationDomain &&
                           !Facade.Task->validatesArtifactMutationCapability(
                               Facade.MutationDomain, Facade.Token)))) {
    Status = binaryStatus(NEVERC_STATUS_POLICY_VIOLATION);
    return {};
  }
  auto Control = Facade.Control;
  std::unique_lock<std::recursive_mutex> Lock(Control->Mutex);
  MutableBinaryBuilder *Owner = Control->Owner;
  if (!Owner) {
    Status = binaryStatus(NEVERC_STATUS_STALE_HANDLE);
    return {};
  }
  Status = Owner->validate(TaskHandle, Builder);
  if (!neverc_status_is_ok(Status))
    return {};
  Status = neverc_status_ok();
  return OwnerLease(std::move(Control), std::move(Lock), Owner);
}

NevercStatus NEVERC_CALL MutableBinaryBuilder::reserve(
    void *Context, NevercTaskHandle TaskHandle,
    NevercMutableBinaryBuilderHandle Builder, uint64_t Size) {
  if (!Context)
    return binaryStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Facade = *static_cast<APIFacade *>(Context);
  NevercStatus Status;
  OwnerLease Owner = acquire(Facade, TaskHandle, Builder, true, Status);
  if (!Owner)
    return Status;
  auto &Self = *Owner;
  uint64_t Position = 0;
  Status =
      Self.IO.OutputTell(Self.IO.Context, TaskHandle, Self.Sink, &Position);
  if (!neverc_status_is_ok(Status))
    return Status;
  if (Size > std::numeric_limits<uint64_t>::max() - Position)
    return binaryStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  const uint64_t NewSize = Position + Size;
  if (NewSize > std::numeric_limits<size_t>::max())
    return binaryStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  if (Position != Self.Bytes.size())
    return binaryStatus(NEVERC_STATUS_INVALID_STATE);
  std::vector<uint8_t> Replacement = Self.Bytes;
  Replacement.resize(static_cast<size_t>(NewSize), 0);
  Status =
      Self.IO.OutputTruncate(Self.IO.Context, TaskHandle, Self.Sink, NewSize);
  if (neverc_status_is_ok(Status))
    Self.Bytes.swap(Replacement);
  return Status;
}

NevercStatus NEVERC_CALL MutableBinaryBuilder::write(
    void *Context, NevercTaskHandle TaskHandle,
    NevercMutableBinaryBuilderHandle Builder, NevercByteView Bytes) {
  if (!Context || (!Bytes.Data && Bytes.Length != 0))
    return binaryStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Facade = *static_cast<APIFacade *>(Context);
  NevercStatus Status;
  OwnerLease Owner = acquire(Facade, TaskHandle, Builder, true, Status);
  if (!Owner)
    return Status;
  auto &Self = *Owner;
  if (Bytes.Length > std::numeric_limits<size_t>::max() ||
      Bytes.Length > std::numeric_limits<size_t>::max() - Self.Bytes.size())
    return binaryStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  std::vector<uint8_t> Replacement = Self.Bytes;
  if (Bytes.Length != 0)
    Replacement.insert(Replacement.end(), Bytes.Data,
                       Bytes.Data + static_cast<size_t>(Bytes.Length));
  Status = Self.IO.OutputWrite(Self.IO.Context, TaskHandle, Self.Sink, Bytes);
  if (neverc_status_is_ok(Status))
    Self.Bytes.swap(Replacement);
  return Status;
}

NevercStatus NEVERC_CALL
MutableBinaryBuilder::writeAt(void *Context, NevercTaskHandle TaskHandle,
                              NevercMutableBinaryBuilderHandle Builder,
                              uint64_t Offset, NevercByteView Bytes) {
  if (!Context || (!Bytes.Data && Bytes.Length != 0))
    return binaryStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Facade = *static_cast<APIFacade *>(Context);
  NevercStatus Status;
  OwnerLease Owner = acquire(Facade, TaskHandle, Builder, true, Status);
  if (!Owner)
    return Status;
  auto &Self = *Owner;
  if (Offset > Self.Bytes.size() || Bytes.Length > Self.Bytes.size() - Offset)
    return binaryStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  std::vector<uint8_t> Replacement = Self.Bytes;
  if (Bytes.Length != 0)
    std::copy(Bytes.Data, Bytes.Data + static_cast<size_t>(Bytes.Length),
              Replacement.begin() + static_cast<size_t>(Offset));
  Status = Self.IO.OutputWriteAt(Self.IO.Context, TaskHandle, Self.Sink, Offset,
                                 Bytes);
  if (neverc_status_is_ok(Status))
    Self.Bytes.swap(Replacement);
  return Status;
}

NevercStatus NEVERC_CALL MutableBinaryBuilder::tell(
    void *Context, NevercTaskHandle TaskHandle,
    NevercMutableBinaryBuilderHandle Builder, uint64_t *OutPosition) {
  if (!Context || !OutPosition)
    return binaryStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutPosition = 0;
  auto &Facade = *static_cast<APIFacade *>(Context);
  NevercStatus Status;
  OwnerLease Owner = acquire(Facade, TaskHandle, Builder, false, Status);
  if (!Owner)
    return Status;
  auto &Self = *Owner;
  NevercStatus Result =
      Self.IO.OutputTell(Self.IO.Context, TaskHandle, Self.Sink, OutPosition);
  if (neverc_status_is_ok(Result) && *OutPosition != Self.Bytes.size())
    return binaryStatus(NEVERC_STATUS_INVALID_STATE);
  return Result;
}

NevercStatus NEVERC_CALL
MutableBinaryBuilder::readAt(void *Context, NevercTaskHandle TaskHandle,
                             NevercMutableBinaryBuilderHandle Builder,
                             uint64_t Offset, NevercMutableByteView BytesView) {
  if (!Context || (!BytesView.Data && BytesView.Length != 0))
    return binaryStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Facade = *static_cast<APIFacade *>(Context);
  NevercStatus Status;
  OwnerLease Owner = acquire(Facade, TaskHandle, Builder, false, Status);
  if (!Owner)
    return Status;
  auto &Self = *Owner;
  if (Offset > Self.Bytes.size() ||
      BytesView.Length > Self.Bytes.size() - Offset)
    return binaryStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  std::copy(Self.Bytes.begin() + static_cast<size_t>(Offset),
            Self.Bytes.begin() + static_cast<size_t>(Offset + BytesView.Length),
            BytesView.Data);
  return neverc_status_ok();
}

NevercStatus
MutableBinaryBuilder::rewrite(NevercTaskHandle TaskHandle,
                              const std::vector<uint8_t> &Replacement) {
  NevercStatus Status =
      IO.OutputTruncate(IO.Context, TaskHandle, Sink, Replacement.size());
  if (!neverc_status_is_ok(Status))
    return Status;
  if (!Replacement.empty()) {
    Status = IO.OutputWriteAt(IO.Context, TaskHandle, Sink, 0,
                              {Replacement.data(), Replacement.size()});
    if (!neverc_status_is_ok(Status))
      return Status;
  }
  Bytes = Replacement;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL
MutableBinaryBuilder::insert(void *Context, NevercTaskHandle TaskHandle,
                             NevercMutableBinaryBuilderHandle Builder,
                             uint64_t Offset, NevercByteView BytesView) {
  if (!Context || (!BytesView.Data && BytesView.Length != 0))
    return binaryStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Facade = *static_cast<APIFacade *>(Context);
  NevercStatus Status;
  OwnerLease Owner = acquire(Facade, TaskHandle, Builder, true, Status);
  if (!Owner)
    return Status;
  auto &Self = *Owner;
  if (Offset > Self.Bytes.size() ||
      BytesView.Length > std::numeric_limits<size_t>::max() - Self.Bytes.size())
    return binaryStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  std::vector<uint8_t> Replacement = Self.Bytes;
  if (BytesView.Length != 0)
    Replacement.insert(Replacement.begin() + static_cast<size_t>(Offset),
                       BytesView.Data,
                       BytesView.Data + static_cast<size_t>(BytesView.Length));
  return Self.rewrite(TaskHandle, Replacement);
}

NevercStatus NEVERC_CALL MutableBinaryBuilder::append(
    void *Context, NevercTaskHandle TaskHandle,
    NevercMutableBinaryBuilderHandle Builder, NevercByteView BytesView) {
  return write(Context, TaskHandle, Builder, BytesView);
}

NevercStatus NEVERC_CALL MutableBinaryBuilder::resize(
    void *Context, NevercTaskHandle TaskHandle,
    NevercMutableBinaryBuilderHandle Builder, uint64_t Size) {
  if (!Context)
    return binaryStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Facade = *static_cast<APIFacade *>(Context);
  NevercStatus Status;
  OwnerLease Owner = acquire(Facade, TaskHandle, Builder, true, Status);
  if (!Owner)
    return Status;
  auto &Self = *Owner;
  if (Size > std::numeric_limits<size_t>::max())
    return binaryStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  std::vector<uint8_t> Replacement = Self.Bytes;
  Replacement.resize(static_cast<size_t>(Size), 0);
  return Self.rewrite(TaskHandle, Replacement);
}

Expected<NevercOutputSummary> MutableBinaryBuilder::summary() const {
  NevercOutputSummary Summary{};
  Summary.Header.StructSize = sizeof(Summary);
  NevercStatus Status =
      IO.OutputGetSummary(IO.Context, Task.handle(), Sink, &Summary);
  if (!neverc_status_is_ok(Status))
    return statusError("mutable binary summary", Status);
  return Summary;
}

Expected<NevercOutputSeal> MutableBinaryBuilder::finish() {
  NevercOutputSeal Seal{};
  Seal.Header.StructSize = sizeof(Seal);
  NevercStatus Status =
      IO.OutputFinish(IO.Context, Task.handle(), Sink, &Seal);
  if (!neverc_status_is_ok(Status))
    return statusError("mutable binary finish", Status);
  return Seal;
}

NevercStatus MutableBinaryBuilder::abort() {
  return IO.OutputAbort(IO.Context, Task.handle(), Sink);
}

} // namespace neverc::plugin
