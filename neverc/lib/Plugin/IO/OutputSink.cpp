#include "OutputSink.h"
#include "PluginFileSystem.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <system_error>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr PluginHandleKind PluginOutputSinkHandleKind = 26;
constexpr PluginHandleKind PluginOutputSealHandleKind = 27;
constexpr uint64_t MaximumOutputNameBytes = UINT64_C(1) << 20;

struct OutputSinkHandlePayload {
  std::shared_ptr<PluginOutputState> State;
  NevercOutputSealHandle Seal{};
};

struct OutputSealHandlePayload {
  std::shared_ptr<PluginOutputState> State;
};

NevercStatus outputStatus(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

Expected<OutputSealHandlePayload *>
resolveSealPayload(PluginTaskContext &Task,
                   NevercOutputSealHandle Seal) {
  void *Raw = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Seal, PluginOutputSealHandleKind, &Raw);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "output seal handle is invalid");
  auto *Payload = static_cast<OutputSealHandlePayload *>(Raw);
  if (!Payload || !Payload->State ||
      !sameHandle(Payload->State->taskHandle(), Task.handle()))
    return createStringError(inconvertibleErrorCode(),
                             "output seal belongs to another task");
  return Payload;
}

bool validName(NevercStringView Name, StringRef &OutName) {
  if (Name.Length == 0 || Name.Length > MaximumOutputNameBytes ||
      Name.Length > std::numeric_limits<size_t>::max() || !Name.Data)
    return false;
  OutName = StringRef(Name.Data, static_cast<size_t>(Name.Length));
  return !OutName.contains('\0') && json::isUTF8(OutName);
}

bool validMetadataText(NevercStringView Value, bool AllowEmpty,
                       StringRef &OutValue) {
  if (Value.Length > MaximumOutputNameBytes ||
      Value.Length > std::numeric_limits<size_t>::max() ||
      (!Value.Data && Value.Length != 0))
    return false;
  OutValue = StringRef(Value.Data ? Value.Data : "",
                       static_cast<size_t>(Value.Length));
  return (AllowEmpty || !OutValue.empty()) && !OutValue.contains('\0') &&
         json::isUTF8(OutValue);
}

template <typename T>
NevercStatus writeCallerRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return outputStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value)
             ? outputStatus(NEVERC_STATUS_ABI_MISMATCH)
             : neverc_status_ok();
}

PluginTaskContext *resolveTask(PluginIOProcessBridge &Bridge,
                               NevercTaskHandle TaskHandle,
                               NevercStatus &OutStatus,
                               bool AllowEnding = false) {
  PluginTaskContext *Task = Bridge.services().findTaskScope(TaskHandle);
  if (!Task) {
    OutStatus = outputStatus(NEVERC_STATUS_STALE_HANDLE);
    return nullptr;
  }
  if (Task->isEnded()) {
    OutStatus = outputStatus(NEVERC_STATUS_INVALID_STATE);
    return nullptr;
  }
  if (!AllowEnding && Task->isEnding()) {
    OutStatus = outputStatus(NEVERC_STATUS_INVALID_STATE);
    return nullptr;
  }
  OutStatus = neverc_status_ok();
  return Task;
}

NevercStatus resolveSink(PluginIOProcessBridge &Bridge,
                         NevercTaskHandle TaskHandle,
                         NevercOutputSinkHandle Sink,
                         PluginTaskContext *&OutTask,
                         OutputSinkHandlePayload *&OutPayload,
                         bool AllowEnding = false) {
  NevercStatus Status;
  OutTask = resolveTask(Bridge, TaskHandle, Status, AllowEnding);
  OutPayload = nullptr;
  if (!OutTask)
    return Status;
  void *Raw = nullptr;
  Status =
      OutTask->handles().resolve(Sink, PluginOutputSinkHandleKind, &Raw);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  OutPayload = static_cast<OutputSinkHandlePayload *>(Raw);
  if (!sameHandle(OutPayload->State->taskHandle(), TaskHandle))
    return outputStatus(NEVERC_STATUS_WRONG_SCOPE);
  return neverc_status_ok();
}

NevercStatus createSinkHandle(
    PluginTaskContext &Task, std::shared_ptr<PluginOutputState> State,
    NevercOutputSinkHandle *OutSink) {
  auto *Payload =
      new (std::nothrow) OutputSinkHandlePayload{std::move(State), {}};
  if (!Payload)
    return outputStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Task.handles().create(
      PluginOutputSinkHandleKind, Payload,
      [](void *Value) { delete static_cast<OutputSinkHandlePayload *>(Value); });
  if (!Handle) {
    delete Payload;
    consumeError(Handle.takeError());
    return outputStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutSink = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL beginMemoryOutput(
    void *Context, NevercTaskHandle TaskHandle, NevercStringView LogicalName,
    uint64_t SizeBudget, NevercOutputSinkHandle *OutSink) {
  if (!Context || !OutSink)
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSink = {};
  StringRef Name;
  if (!validName(LogicalName, Name))
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  NevercStatus Status;
  PluginTaskContext *Task = resolveTask(Bridge, TaskHandle, Status);
  if (!Task)
    return Status;
  auto State = Bridge.createMemoryOutput(*Task, Name, SizeBudget);
  if (!State) {
    consumeError(State.takeError());
    return outputStatus(NEVERC_STATUS_DUPLICATE_ID);
  }
  return createSinkHandle(*Task, std::move(*State), OutSink);
}

NevercStatus NEVERC_CALL beginFileOutput(
    void *Context, NevercTaskHandle TaskHandle, NevercStringView FinalPath,
    uint64_t SizeBudget, NevercOutputSinkHandle *OutSink) {
  if (!Context || !OutSink)
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSink = {};
  StringRef Path;
  if (!validName(FinalPath, Path))
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  NevercStatus Status;
  PluginTaskContext *Task = resolveTask(Bridge, TaskHandle, Status);
  if (!Task)
    return Status;
  auto State = Bridge.createFileOutput(*Task, Path, SizeBudget);
  if (!State) {
    std::error_code ErrorCode =
        errorToErrorCode(State.takeError());
    if (ErrorCode == std::make_error_code(std::errc::file_exists))
      return outputStatus(NEVERC_STATUS_DUPLICATE_ID);
    if (ErrorCode ==
        std::make_error_code(std::errc::operation_canceled))
      return outputStatus(NEVERC_STATUS_CANCELLED);
    return outputStatus(NEVERC_STATUS_PLUGIN_FAILURE);
  }
  return createSinkHandle(*Task, std::move(*State), OutSink);
}

NevercStatus NEVERC_CALL beginStreamOutput(
    void *Context, NevercTaskHandle TaskHandle, NevercOutputStream Stream,
    uint64_t SizeBudget, NevercOutputSinkHandle *OutSink) {
  if (!Context || !OutSink ||
      (Stream != NEVERC_OUTPUT_STREAM_STDOUT &&
       Stream != NEVERC_OUTPUT_STREAM_STDERR))
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSink = {};
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  NevercStatus Status;
  PluginTaskContext *Task = resolveTask(Bridge, TaskHandle, Status);
  if (!Task)
    return Status;
  auto State = Bridge.createStreamOutput(*Task, Stream, SizeBudget);
  if (!State) {
    consumeError(State.takeError());
    return outputStatus(NEVERC_STATUS_PLUGIN_FAILURE);
  }
  return createSinkHandle(*Task, std::move(*State), OutSink);
}

NevercStatus NEVERC_CALL outputWrite(
    void *Context, NevercTaskHandle TaskHandle, NevercOutputSinkHandle Sink,
    NevercByteView Bytes) {
  if (!Context || Bytes.Length > std::numeric_limits<size_t>::max() ||
      (!Bytes.Data && Bytes.Length != 0))
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  PluginTaskContext *Task = nullptr;
  OutputSinkHandlePayload *Payload = nullptr;
  NevercStatus Status =
      resolveSink(Bridge, TaskHandle, Sink, Task, Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Payload->State->write(ArrayRef<uint8_t>(
      Bytes.Data, static_cast<size_t>(Bytes.Length)));
}

NevercStatus NEVERC_CALL outputWriteAt(
    void *Context, NevercTaskHandle TaskHandle, NevercOutputSinkHandle Sink,
    uint64_t Offset, NevercByteView Bytes) {
  if (!Context || Bytes.Length > std::numeric_limits<size_t>::max() ||
      (!Bytes.Data && Bytes.Length != 0))
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  PluginTaskContext *Task = nullptr;
  OutputSinkHandlePayload *Payload = nullptr;
  NevercStatus Status =
      resolveSink(Bridge, TaskHandle, Sink, Task, Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Payload->State->writeAt(
      Offset, ArrayRef<uint8_t>(Bytes.Data,
                                static_cast<size_t>(Bytes.Length)));
}

NevercStatus NEVERC_CALL outputTell(
    void *Context, NevercTaskHandle TaskHandle, NevercOutputSinkHandle Sink,
    uint64_t *OutPosition) {
  if (!Context || !OutPosition)
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutPosition = 0;
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  PluginTaskContext *Task = nullptr;
  OutputSinkHandlePayload *Payload = nullptr;
  NevercStatus Status =
      resolveSink(Bridge, TaskHandle, Sink, Task, Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Payload->State->tell(OutPosition);
}

NevercStatus NEVERC_CALL outputTruncate(
    void *Context, NevercTaskHandle TaskHandle, NevercOutputSinkHandle Sink,
    uint64_t Size) {
  if (!Context)
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  PluginTaskContext *Task = nullptr;
  OutputSinkHandlePayload *Payload = nullptr;
  NevercStatus Status =
      resolveSink(Bridge, TaskHandle, Sink, Task, Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Payload->State->truncate(Size);
}

NevercStatus NEVERC_CALL outputMetadataSet(
    void *Context, NevercTaskHandle TaskHandle, NevercOutputSinkHandle Sink,
    NevercStringView Key, NevercStringView Value) {
  if (!Context)
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef NativeKey;
  StringRef NativeValue;
  if (!validMetadataText(Key, false, NativeKey) ||
      !validMetadataText(Value, true, NativeValue))
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  PluginTaskContext *Task = nullptr;
  OutputSinkHandlePayload *Payload = nullptr;
  NevercStatus Status =
      resolveSink(Bridge, TaskHandle, Sink, Task, Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Payload->State->setMetadata(NativeKey, NativeValue);
}

NevercStatus NEVERC_CALL outputFinish(
    void *Context, NevercTaskHandle TaskHandle, NevercOutputSinkHandle Sink,
    NevercOutputSeal *OutSeal) {
  if (!Context || !OutSeal)
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  PluginTaskContext *Task = nullptr;
  OutputSinkHandlePayload *Payload = nullptr;
  NevercStatus Status =
      resolveSink(Bridge, TaskHandle, Sink, Task, Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Payload->State->finish();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (neverc_handle_is_null(Payload->Seal)) {
    auto *SealPayload =
        new (std::nothrow) OutputSealHandlePayload{Payload->State};
    if (!SealPayload)
      return outputStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    auto Handle = Task->handles().create(
        PluginOutputSealHandleKind, SealPayload, [](void *Value) {
          delete static_cast<OutputSealHandlePayload *>(Value);
        });
    if (!Handle) {
      delete SealPayload;
      consumeError(Handle.takeError());
      return outputStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Payload->Seal = *Handle;
  }
  NevercOutputSeal Seal = Payload->State->seal(Payload->Seal);
  return writeCallerRecord(OutSeal, Seal);
}

NevercStatus NEVERC_CALL outputAbort(
    void *Context, NevercTaskHandle TaskHandle, NevercOutputSinkHandle Sink) {
  if (!Context)
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  PluginTaskContext *Task = nullptr;
  OutputSinkHandlePayload *Payload = nullptr;
  NevercStatus Status =
      resolveSink(Bridge, TaskHandle, Sink, Task, Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Payload->State->abort();
}

NevercStatus NEVERC_CALL outputGetSummary(
    void *Context, NevercTaskHandle TaskHandle, NevercOutputSinkHandle Sink,
    NevercOutputSummary *OutSummary) {
  if (!Context || !OutSummary)
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  PluginTaskContext *Task = nullptr;
  OutputSinkHandlePayload *Payload = nullptr;
  NevercStatus Status =
      resolveSink(Bridge, TaskHandle, Sink, Task, Payload, true);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  NevercOutputSummary Summary = Payload->State->summary();
  return writeCallerRecord(OutSummary, Summary);
}

} // namespace

PluginOutputState::PluginOutputState(
    NevercTaskHandle TaskValue,
    std::shared_ptr<OutputTransaction> TransactionValue)
    : Task(TaskValue), Transaction(std::move(TransactionValue)) {}

NevercStatus
PluginOutputState::translate(OutputTransactionResult Result) {
  switch (Result) {
  case OutputTransactionResult::Success:
    return neverc_status_ok();
  case OutputTransactionResult::InvalidState:
    return outputStatus(NEVERC_STATUS_INVALID_STATE);
  case OutputTransactionResult::InvalidArgument:
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  case OutputTransactionResult::ResourceExhausted:
    return outputStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  case OutputTransactionResult::IOFailure:
    return outputStatus(NEVERC_STATUS_PLUGIN_FAILURE);
  case OutputTransactionResult::FailedPartial: {
    NevercStatus Status =
        outputStatus(NEVERC_STATUS_OUTPUT_PARTIAL);
    Status.Flags =
        NEVERC_STATUS_FLAG_OUTPUT_MAY_BE_PARTIAL |
        NEVERC_STATUS_FLAG_OUTPUT_RECOVERY_REQUIRED;
    return Status;
  }
  }
  return outputStatus(NEVERC_STATUS_INVALID_STATE);
}

NevercOutputSummary PluginOutputState::translate(
    const OutputTransactionSummary &Summary) {
  NevercOutputSummary Result{};
  Result.Header = {sizeof(Result), NEVERC_IO_API_MAJOR,
                   NEVERC_IO_API_MINOR, 0};
  switch (Summary.State) {
  case OutputTransactionState::Open:
    Result.State = NEVERC_OUTPUT_OPEN;
    break;
  case OutputTransactionState::Finished:
    Result.State = NEVERC_OUTPUT_FINISHED;
    break;
  case OutputTransactionState::Committed:
    Result.State = NEVERC_OUTPUT_COMMITTED;
    break;
  case OutputTransactionState::Aborted:
    Result.State = NEVERC_OUTPUT_ABORTED;
    break;
  case OutputTransactionState::FailedPartial:
    Result.State = NEVERC_OUTPUT_FAILED_PARTIAL;
    break;
  }
  switch (Summary.Kind) {
  case OutputDestinationKind::Memory:
    Result.Kind = NEVERC_OUTPUT_MEMORY;
    break;
  case OutputDestinationKind::File:
    Result.Kind = NEVERC_OUTPUT_FILE;
    break;
  case OutputDestinationKind::Stream:
    Result.Kind = NEVERC_OUTPUT_STREAM;
    break;
  }
  Result.Flags = Summary.Flags;
  Result.Size = Summary.Size;
  Result.PublicationGeneration = Summary.PublicationGeneration;
  std::copy(Summary.Digest.begin(), Summary.Digest.end(), Result.Digest);
  return Result;
}

NevercStatus PluginOutputState::write(ArrayRef<uint8_t> Bytes) {
  return translate(Transaction->write(Bytes));
}

NevercStatus PluginOutputState::writeAt(uint64_t Offset,
                                        ArrayRef<uint8_t> Bytes) {
  return translate(Transaction->writeAt(Offset, Bytes));
}

NevercStatus PluginOutputState::tell(uint64_t *OutPosition) const {
  if (!OutPosition)
    return outputStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return translate(Transaction->tell(*OutPosition));
}

NevercStatus PluginOutputState::truncate(uint64_t Size) {
  return translate(Transaction->truncate(Size));
}

NevercStatus PluginOutputState::setMetadata(StringRef Key, StringRef Value) {
  return translate(Transaction->setMetadata(Key, Value));
}

NevercStatus PluginOutputState::finish() {
  return translate(Transaction->finish());
}

NevercStatus PluginOutputState::abort() {
  return translate(Transaction->abort());
}

Expected<NevercOutputSummary> PluginOutputState::commit() {
  auto Summary = Transaction->commit();
  if (!Summary)
    return Summary.takeError();
  return translate(*Summary);
}

NevercOutputSummary PluginOutputState::summary() const {
  return translate(Transaction->summary());
}

NevercOutputSeal
PluginOutputState::seal(NevercOutputSealHandle Handle) const {
  OutputTransactionSummary Summary = Transaction->summary();
  NevercOutputSeal Result{};
  Result.Header = {sizeof(Result), NEVERC_IO_API_MAJOR,
                   NEVERC_IO_API_MINOR, 0};
  Result.Handle = Handle;
  Result.Kind = translate(Summary).Kind;
  Result.Size = Summary.Size;
  std::copy(Summary.Digest.begin(), Summary.Digest.end(), Result.Digest);
  return Result;
}

std::optional<PluginMemoryOutputSnapshot>
PluginOutputState::memorySnapshot() const {
  auto Snapshot = Transaction->memorySnapshot();
  if (!Snapshot)
    return std::nullopt;
  return PluginMemoryOutputSnapshot{Snapshot->Generation,
                                    std::move(Snapshot->Bytes)};
}

Expected<std::shared_ptr<PluginOutputState>>
PluginIOProcessBridge::createMemoryOutput(PluginTaskContext &Task,
                                          StringRef LogicalName,
                                          uint64_t SizeBudget) {
  auto Key = std::make_pair(Task.handle().Owner, Task.handle().Value);
  std::lock_guard<std::mutex> Lock(Mutex);
  auto &Outputs = TaskMemoryOutputs[Key];
  if (Outputs.count(LogicalName.str()) != 0)
    return createStringError(inconvertibleErrorCode(),
                             "duplicate memory output name");
  auto State = std::make_shared<PluginOutputState>(
      Task.handle(),
      OutputTransaction::createMemory(LogicalName, SizeBudget));
  Outputs.emplace(LogicalName.str(), State);
  TaskOutputs[Key].push_back(State);
  return State;
}

Expected<std::shared_ptr<PluginOutputState>>
PluginIOProcessBridge::createFileOutput(PluginTaskContext &Task,
                                        StringRef FinalPath,
                                        uint64_t SizeBudget) {
  auto Transaction = OutputTransaction::createFile(
      Outputs, FinalPath, SizeBudget,
      [&Task] {
        return Task.checkCancelled().Code == NEVERC_STATUS_CANCELLED;
      },
      {}, OutputLeaseOwner{Task.handle().Owner, Task.handle().Value});
  if (!Transaction)
    return Transaction.takeError();
  auto State = std::make_shared<PluginOutputState>(
      Task.handle(), std::move(*Transaction));
  const auto Key =
      std::make_pair(Task.handle().Owner, Task.handle().Value);
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    TaskOutputs[Key].push_back(State);
  }
  return State;
}

Expected<std::shared_ptr<PluginOutputState>>
PluginIOProcessBridge::createStreamOutput(PluginTaskContext &Task,
                                          NevercOutputStream Stream,
                                          uint64_t SizeBudget) {
  raw_ostream *Output =
      Stream == NEVERC_OUTPUT_STREAM_STDERR ? &errs() : &outs();
  const auto Key =
      std::make_pair(Task.handle().Owner, Task.handle().Value);
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto TaskIt = TaskOutputStreams.find(Key);
    if (TaskIt != TaskOutputStreams.end()) {
      auto StreamIt = TaskIt->second.find(Stream);
      if (StreamIt != TaskIt->second.end())
        Output = StreamIt->second;
    }
  }
  if (!Output)
    return createStringError(inconvertibleErrorCode(),
                             "output stream binding is null");
  StringRef Name = Stream == NEVERC_OUTPUT_STREAM_STDERR ? "stderr"
                                                         : "stdout";
  auto Transaction = OutputTransaction::createStream(
      Name, SizeBudget, [Output](ArrayRef<uint8_t> Bytes) {
        Output->write(reinterpret_cast<const char *>(Bytes.data()),
                      Bytes.size());
        Output->flush();
        return OutputTransactionResult::Success;
      });
  auto State = std::make_shared<PluginOutputState>(
      Task.handle(), std::move(Transaction));
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    TaskOutputs[Key].push_back(State);
  }
  return State;
}

Expected<std::string>
PluginIOProcessBridge::canonicalizeOutputPath(StringRef Path) const {
  return Outputs.canonicalize(Path);
}

Error PluginIOProcessBridge::bindOutputStream(PluginTaskContext &Task,
                                               NevercOutputStream Stream,
                                               raw_ostream &Output) {
  if (Stream != NEVERC_OUTPUT_STREAM_STDOUT &&
      Stream != NEVERC_OUTPUT_STREAM_STDERR)
    return createStringError(inconvertibleErrorCode(),
                             "invalid plugin output stream");
  if (Services.findTaskScope(Task.handle()) != &Task || Task.isEnded())
    return createStringError(inconvertibleErrorCode(),
                             "plugin output stream task is not live");
  const auto Key =
      std::make_pair(Task.handle().Owner, Task.handle().Value);
  std::lock_guard<std::mutex> Lock(Mutex);
  TaskOutputStreams[Key][Stream] = &Output;
  return Error::success();
}

std::shared_ptr<PluginOutputState>
PluginIOProcessBridge::findMemoryOutput(NevercTaskHandle Task,
                                        StringRef LogicalName) const {
  auto Key = std::make_pair(Task.Owner, Task.Value);
  std::lock_guard<std::mutex> Lock(Mutex);
  auto TaskIt = TaskMemoryOutputs.find(Key);
  if (TaskIt == TaskMemoryOutputs.end())
    return nullptr;
  auto OutputIt = TaskIt->second.find(LogicalName.str());
  return OutputIt == TaskIt->second.end() ? nullptr : OutputIt->second;
}

Error PluginIOProcessBridge::taskScopeEnding(
    NevercTaskHandle Task) {
  std::vector<std::shared_ptr<PluginOutputState>> OutputsToAbort;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = TaskOutputs.find(std::make_pair(Task.Owner, Task.Value));
    if (It != TaskOutputs.end())
      OutputsToAbort = It->second;
  }

  Error Errors = Error::success();
  for (const auto &Output : OutputsToAbort) {
    NevercOutputState State = Output->summary().State;
    if (State != NEVERC_OUTPUT_OPEN && State != NEVERC_OUTPUT_FINISHED)
      continue;
    NevercStatus Status = Output->abort();
    if (Status.Code != NEVERC_STATUS_OK)
      Errors = joinErrors(
          std::move(Errors),
          createStringError(
              inconvertibleErrorCode(),
              "failed to abort plugin output '" +
                  Output->logicalName() + "' before TaskEnd"));
  }
  return Errors;
}

void initializePluginOutputAPI(NevercIOAPI &API,
                               PluginIOProcessBridge &) {
  API.BeginMemoryOutput = beginMemoryOutput;
  API.BeginFileOutput = beginFileOutput;
  API.BeginStreamOutput = beginStreamOutput;
  API.OutputWrite = outputWrite;
  API.OutputWriteAt = outputWriteAt;
  API.OutputTell = outputTell;
  API.OutputTruncate = outputTruncate;
  API.OutputMetadataSet = outputMetadataSet;
  API.OutputFinish = outputFinish;
  API.OutputAbort = outputAbort;
  API.OutputGetSummary = outputGetSummary;
}

Expected<NevercOutputSummary>
hostCommitPluginOutput(PluginTaskContext &Task,
                       NevercOutputSealHandle Seal) {
  auto Payload = resolveSealPayload(Task, Seal);
  if (!Payload)
    return Payload.takeError();
  return (*Payload)->State->commit();
}

Expected<NevercOutputSummary>
hostAbortPluginOutput(PluginTaskContext &Task,
                      NevercOutputSealHandle Seal) {
  auto Payload = resolveSealPayload(Task, Seal);
  if (!Payload)
    return Payload.takeError();
  NevercStatus Status = (*Payload)->State->abort();
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(
        inconvertibleErrorCode(),
        "sealed output abort failed with status code " +
            Twine(Status.Code));
  return (*Payload)->State->summary();
}

Expected<PluginOutputSealSnapshot>
inspectPluginOutputSeal(PluginTaskContext &Task,
                        NevercOutputSealHandle Seal) {
  auto Payload = resolveSealPayload(Task, Seal);
  if (!Payload)
    return Payload.takeError();

  NevercOutputSummary Summary = (*Payload)->State->summary();
  PluginOutputSealSnapshot Snapshot;
  Snapshot.Handle = Seal;
  Snapshot.Kind = Summary.Kind;
  Snapshot.State = Summary.State;
  Snapshot.Flags = Summary.Flags;
  Snapshot.Size = Summary.Size;
  Snapshot.PublicationGeneration = Summary.PublicationGeneration;
  std::copy_n(Summary.Digest, Snapshot.Digest.size(),
              Snapshot.Digest.begin());
  Snapshot.Destination = (*Payload)->State->logicalName().str();
  return Snapshot;
}

Expected<std::string>
canonicalizePluginOutputPath(PluginTaskContext &Task,
                             StringRef Path) {
  auto Bridge = findPluginIOProcessBridge(Task.processServices());
  if (!Bridge)
    return createStringError(inconvertibleErrorCode(),
                             "plugin IO interface is not registered");
  if (Task.isEnded() ||
      Bridge->services().findTaskScope(Task.handle()) != &Task)
    return createStringError(inconvertibleErrorCode(),
                             "plugin output task is not live");
  return Bridge->canonicalizeOutputPath(Path);
}

std::optional<PluginMemoryOutputSnapshot>
findPluginMemoryOutput(PluginTaskContext &Task,
                       StringRef LogicalName) {
  auto Bridge = findPluginIOProcessBridge(Task.processServices());
  if (!Bridge)
    return std::nullopt;
  auto State = Bridge->findMemoryOutput(Task.handle(), LogicalName);
  return State ? State->memorySnapshot() : std::nullopt;
}

Error bindPluginOutputStream(PluginTaskContext &Task,
                             NevercOutputStream Stream,
                             raw_ostream &Output) {
  auto Bridge = findPluginIOProcessBridge(Task.processServices());
  if (!Bridge)
    return createStringError(inconvertibleErrorCode(),
                             "plugin IO interface is not registered");
  return Bridge->bindOutputStream(Task, Stream, Output);
}

} // namespace neverc::plugin
