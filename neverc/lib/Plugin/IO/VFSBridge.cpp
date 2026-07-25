#include "DependencyBridge.h"
#include "PluginFileSystem.h"
#include "OutputSink.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr PluginHandleKind PluginIOFileHandleKind = 23;
constexpr PluginHandleKind PluginIOBufferHandleKind = 24;
constexpr PluginHandleKind PluginIODirectoryCursorHandleKind = 25;

struct IOFilePayload {
  std::shared_ptr<PluginVFSView> View;
  std::unique_ptr<vfs::File> File;
  std::optional<std::string> Content;
  std::string Name;
  std::mutex Mutex;
  bool Closed = false;
};

struct IOBufferPayload {
  std::vector<uint8_t> Storage;
  size_t Length = 0;
  bool NullTerminated = false;
};

struct IODirectoryCursorPayload {
  std::shared_ptr<PluginVFSView> View;
  std::vector<vfs::directory_entry> Entries;
  size_t Index = 0;
};

NevercStatus ioStatus(NevercStatusCode Code,
                      NevercIOErrorCode Detail = NEVERC_IO_ERROR_NONE) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  Status.Detail = Detail;
  return Status;
}

NevercIOErrorCode classifyError(std::error_code Error) {
  if (Error == errc::no_such_file_or_directory)
    return NEVERC_IO_ERROR_NOT_FOUND;
  if (Error == errc::permission_denied)
    return NEVERC_IO_ERROR_PERMISSION_DENIED;
  if (Error == errc::not_a_directory)
    return NEVERC_IO_ERROR_NOT_DIRECTORY;
  if (Error == errc::is_a_directory)
    return NEVERC_IO_ERROR_IS_DIRECTORY;
  if (Error == errc::invalid_argument ||
      Error == errc::filename_too_long)
    return NEVERC_IO_ERROR_INVALID_PATH;
  return NEVERC_IO_ERROR_IO;
}

NevercStatus errorStatus(std::error_code Error) {
  if (!Error)
    return neverc_status_ok();
  if (Error == std::errc::operation_canceled)
    return ioStatus(NEVERC_STATUS_CANCELLED);
  if (Error == errc::not_enough_memory)
    return ioStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  if (Error == errc::resource_deadlock_would_occur)
    return ioStatus(NEVERC_STATUS_REENTRANCY_DENIED);
  return ioStatus(NEVERC_STATUS_PLUGIN_FAILURE, classifyError(Error));
}

bool validPath(NevercStringView Path, StringRef &OutPath) {
  if (Path.Length > std::numeric_limits<size_t>::max() ||
      (!Path.Data && Path.Length != 0))
    return false;
  OutPath = StringRef(Path.Data ? Path.Data : "",
                      static_cast<size_t>(Path.Length));
  return !OutPath.empty() && !OutPath.contains('\0');
}

template <typename T>
NevercStatus writeCallerRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return ioStatus(NEVERC_STATUS_ABI_MISMATCH);
  size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value)
             ? ioStatus(NEVERC_STATUS_ABI_MISMATCH)
             : neverc_status_ok();
}

NevercVFSFileType fileType(sys::fs::file_type Type) {
  switch (Type) {
  case sys::fs::file_type::regular_file:
    return NEVERC_VFS_FILE_REGULAR;
  case sys::fs::file_type::directory_file:
    return NEVERC_VFS_FILE_DIRECTORY;
  case sys::fs::file_type::symlink_file:
    return NEVERC_VFS_FILE_SYMLINK;
  case sys::fs::file_type::type_unknown:
    return NEVERC_VFS_FILE_UNKNOWN;
  default:
    return NEVERC_VFS_FILE_OTHER;
  }
}

PluginTaskContext *resolveTask(PluginIOProcessBridge &Bridge,
                               NevercTaskHandle TaskHandle,
                               std::shared_ptr<PluginVFSView> &OutView,
                               NevercStatus &OutStatus) {
  OutView.reset();
  PluginTaskContext *Task =
      Bridge.services().findTaskScope(TaskHandle);
  if (!Task) {
    OutStatus = ioStatus(NEVERC_STATUS_STALE_HANDLE);
    return nullptr;
  }
  if (Task->isEnded()) {
    OutStatus = ioStatus(NEVERC_STATUS_INVALID_STATE);
    return nullptr;
  }
  OutView = Bridge.findView(TaskHandle);
  if (!OutView) {
    OutStatus = ioStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    return nullptr;
  }
  OutStatus = neverc_status_ok();
  return Task;
}

NevercStatus fillStatus(PluginVFSView &View,
                        const vfs::Status &Native,
                        NevercVFSStatus *OutStatus) {
  bool Local = false;
  std::error_code LocalError = View.isLocal(Native.getName(), Local);
  if (LocalError)
    Local = false;
  auto Seconds = std::chrono::duration_cast<std::chrono::seconds>(
                     Native.getLastModificationTime().time_since_epoch())
                     .count();
  if (Seconds < std::numeric_limits<int64_t>::min() ||
      Seconds > std::numeric_limits<int64_t>::max())
    return ioStatus(NEVERC_STATUS_VERIFICATION_FAILED);

  sys::fs::UniqueID UniqueID = Native.getUniqueID();
  NevercVFSStatus Status{};
  Status.Header = {sizeof(Status), NEVERC_IO_API_MAJOR,
                   NEVERC_IO_API_MINOR, 0};
  Status.Type = fileType(Native.getType());
  Status.Permissions =
      static_cast<uint32_t>(Native.getPermissions());
  Status.Size = Native.getSize();
  Status.ModificationTime = static_cast<int64_t>(Seconds);
  Status.UniqueID = {UniqueID.getDevice(), UniqueID.getFile()};
  Status.Local = Local ? NEVERC_TRUE : NEVERC_FALSE;
  return writeCallerRecord(OutStatus, Status);
}

Expected<NevercBufferHandle>
createBuffer(PluginTaskContext &Task, ArrayRef<uint8_t> Bytes,
             bool NullTerminated) {
  auto *Payload = new (std::nothrow) IOBufferPayload;
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "cannot allocate IO buffer payload");
  Payload->Length = Bytes.size();
  Payload->NullTerminated = NullTerminated;
  Payload->Storage.assign(Bytes.begin(), Bytes.end());
  if (NullTerminated)
    Payload->Storage.push_back(0);
  auto Handle = Task.handles().create(
      PluginIOBufferHandleKind, Payload,
      [](void *Value) { delete static_cast<IOBufferPayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

NevercStatus NEVERC_CALL registerVFSProvider(
    void *, void *Registrar,
    const NevercVFSProviderDescriptor *Descriptor) {
  return registerPluginVFSProvider(Registrar, Descriptor);
}

NevercStatus NEVERC_CALL statPath(
    void *Context, NevercTaskHandle TaskHandle, NevercStringView Path,
    NevercVFSStatus *OutStatus) {
  if (!Context || !OutStatus)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef NativePath;
  if (!validPath(Path, NativePath))
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  if (!resolveTask(Bridge, TaskHandle, View, Status))
    return Status;
  ErrorOr<vfs::Status> NativeStatus = View->status(NativePath);
  if (!NativeStatus)
    return errorStatus(NativeStatus.getError());
  return fillStatus(*View, *NativeStatus, OutStatus);
}

NevercStatus NEVERC_CALL openFileForRead(
    void *Context, NevercTaskHandle TaskHandle, NevercStringView Path,
    NevercFileHandle *OutFile) {
  if (!Context || !OutFile)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutFile = {};
  StringRef NativePath;
  if (!validPath(Path, NativePath))
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  PluginTaskContext *Task =
      resolveTask(Bridge, TaskHandle, View, Status);
  if (!Task)
    return Status;
  ErrorOr<std::unique_ptr<vfs::File>> Opened =
      View->openFileForRead(NativePath);
  if (!Opened)
    return errorStatus(Opened.getError());

  auto *Payload = new (std::nothrow)
      IOFilePayload{View, std::move(*Opened), std::nullopt,
                    NativePath.str()};
  if (!Payload)
    return ioStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Task->handles().create(
      PluginIOFileHandleKind, Payload, [](void *Value) {
        auto *File = static_cast<IOFilePayload *>(Value);
        if (File->File && !File->Closed)
          (void)File->File->close();
        delete File;
      });
  if (!Handle) {
    delete Payload;
    consumeError(Handle.takeError());
    return ioStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutFile = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL readFile(
    void *Context, NevercTaskHandle TaskHandle, NevercFileHandle File,
    uint64_t Offset, uint64_t Length, NevercBufferHandle *OutBuffer) {
  if (!Context || !OutBuffer)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBuffer = {};
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  PluginTaskContext *Task =
      resolveTask(Bridge, TaskHandle, View, Status);
  if (!Task)
    return Status;

  void *Raw = nullptr;
  Status = Task->handles().resolve(
      File, PluginIOFileHandleKind, &Raw);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Payload = *static_cast<IOFilePayload *>(Raw);
  if (Payload.View != View)
    return ioStatus(NEVERC_STATUS_WRONG_SCOPE);

  std::lock_guard<std::mutex> Lock(Payload.Mutex);
  if (Payload.Closed)
    return ioStatus(NEVERC_STATUS_INVALID_STATE);
  if (!Payload.Content) {
    auto Buffer = Payload.File->getBuffer(
        Payload.Name, -1, true, false);
    if (!Buffer)
      return errorStatus(Buffer.getError());
    Payload.Content = (*Buffer)->getBuffer().str();
  }
  if (Offset > Payload.Content->size() ||
      Length > std::numeric_limits<size_t>::max())
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  size_t NativeOffset = static_cast<size_t>(Offset);
  size_t NativeLength =
      std::min(static_cast<size_t>(Length),
               Payload.Content->size() - NativeOffset);
  StringRef Slice(*Payload.Content);
  Slice = Slice.substr(NativeOffset, NativeLength);
  auto Buffer = createBuffer(
      *Task,
      ArrayRef<uint8_t>(
          reinterpret_cast<const uint8_t *>(Slice.data()),
          Slice.size()),
      true);
  if (!Buffer) {
    consumeError(Buffer.takeError());
    return ioStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutBuffer = *Buffer;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL closeFile(
    void *Context, NevercTaskHandle TaskHandle, NevercFileHandle File) {
  if (!Context)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  PluginTaskContext *Task =
      resolveTask(Bridge, TaskHandle, View, Status);
  if (!Task)
    return Status;
  void *Raw = nullptr;
  Status = Task->handles().resolve(
      File, PluginIOFileHandleKind, &Raw);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Payload = *static_cast<IOFilePayload *>(Raw);
  {
    std::lock_guard<std::mutex> Lock(Payload.Mutex);
    if (Payload.Closed)
      return ioStatus(NEVERC_STATUS_STALE_HANDLE);
    std::error_code Error = Payload.File->close();
    if (Error)
      return errorStatus(Error);
    Payload.Closed = true;
  }
  return Task->handles().release(File, PluginIOFileHandleKind);
}

NevercStatus NEVERC_CALL copyBuffer(
    void *Context, NevercTaskHandle TaskHandle, NevercByteView Bytes,
    NevercBool NullTerminated, NevercBufferHandle *OutBuffer) {
  if (!Context || !OutBuffer ||
      Bytes.Length > std::numeric_limits<size_t>::max() ||
      (!Bytes.Data && Bytes.Length != 0) ||
      (NullTerminated != NEVERC_FALSE &&
       NullTerminated != NEVERC_TRUE))
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBuffer = {};
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  PluginTaskContext *Task =
      resolveTask(Bridge, TaskHandle, View, Status);
  if (!Task)
    return Status;
  auto Buffer = createBuffer(
      *Task,
      ArrayRef<uint8_t>(Bytes.Data,
                        static_cast<size_t>(Bytes.Length)),
      NullTerminated == NEVERC_TRUE);
  if (!Buffer) {
    consumeError(Buffer.takeError());
    return ioStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutBuffer = *Buffer;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getBufferView(
    void *Context, NevercTaskHandle TaskHandle,
    NevercBufferHandle Buffer, NevercBufferView *OutView) {
  if (!Context || !OutView)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  PluginTaskContext *Task =
      resolveTask(Bridge, TaskHandle, View, Status);
  if (!Task)
    return Status;
  void *Raw = nullptr;
  Status = Task->handles().resolve(
      Buffer, PluginIOBufferHandleKind, &Raw);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Payload = *static_cast<IOBufferPayload *>(Raw);
  NevercBufferView Record{};
  Record.Header = {sizeof(Record), NEVERC_IO_API_MAJOR,
                   NEVERC_IO_API_MINOR, 0};
  Record.Data = Payload.Storage.empty() ? nullptr
                                       : Payload.Storage.data();
  Record.Length = Payload.Length;
  Record.NullTerminated =
      Payload.NullTerminated ? NEVERC_TRUE : NEVERC_FALSE;
  return writeCallerRecord(OutView, Record);
}

NevercStatus NEVERC_CALL releaseBuffer(
    void *Context, NevercTaskHandle TaskHandle,
    NevercBufferHandle Buffer) {
  if (!Context)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  PluginTaskContext *Task =
      resolveTask(Bridge, TaskHandle, View, Status);
  if (!Task)
    return Status;
  return Task->handles().release(Buffer, PluginIOBufferHandleKind);
}

NevercStatus createPathBuffer(PluginTaskContext &Task, StringRef Path,
                              NevercBufferHandle *OutBuffer) {
  auto Buffer = createBuffer(
      Task,
      ArrayRef<uint8_t>(
          reinterpret_cast<const uint8_t *>(Path.data()), Path.size()),
      true);
  if (!Buffer) {
    consumeError(Buffer.takeError());
    return ioStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutBuffer = *Buffer;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL canonicalize(
    void *Context, NevercTaskHandle TaskHandle, NevercStringView Path,
    NevercBufferHandle *OutBuffer) {
  if (!Context || !OutBuffer)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBuffer = {};
  StringRef NativePath;
  if (!validPath(Path, NativePath))
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  PluginTaskContext *Task =
      resolveTask(Bridge, TaskHandle, View, Status);
  if (!Task)
    return Status;
  SmallString<256> Canonical;
  std::error_code Error = View->getRealPath(NativePath, Canonical);
  if (Error)
    return errorStatus(Error);
  return createPathBuffer(*Task, Canonical, OutBuffer);
}

NevercStatus NEVERC_CALL getCurrentDirectory(
    void *Context, NevercTaskHandle TaskHandle,
    NevercBufferHandle *OutBuffer) {
  if (!Context || !OutBuffer)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBuffer = {};
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  PluginTaskContext *Task =
      resolveTask(Bridge, TaskHandle, View, Status);
  if (!Task)
    return Status;
  auto Current = View->getCurrentWorkingDirectory();
  if (!Current)
    return errorStatus(Current.getError());
  return createPathBuffer(*Task, *Current, OutBuffer);
}

NevercStatus NEVERC_CALL setCurrentDirectory(
    void *Context, NevercTaskHandle TaskHandle, NevercStringView Path) {
  if (!Context)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef NativePath;
  if (!validPath(Path, NativePath))
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  if (!resolveTask(Bridge, TaskHandle, View, Status))
    return Status;
  return errorStatus(View->setCurrentWorkingDirectory(NativePath));
}

NevercStatus NEVERC_CALL openDirectory(
    void *Context, NevercTaskHandle TaskHandle, NevercStringView Path,
    NevercDirectoryCursorHandle *OutCursor) {
  if (!Context || !OutCursor)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCursor = {};
  StringRef NativePath;
  if (!validPath(Path, NativePath))
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  PluginTaskContext *Task =
      resolveTask(Bridge, TaskHandle, View, Status);
  if (!Task)
    return Status;

  std::error_code Error;
  vfs::directory_iterator Iterator =
      View->dirBegin(NativePath, Error);
  if (Error)
    return errorStatus(Error);
  std::vector<vfs::directory_entry> Entries;
  vfs::directory_iterator End;
  while (Iterator != End) {
    // Canonicalize and GetWorkingDirectory hand back host-native separators,
    // so traversal has to as well. Otherwise a plugin cannot match an entry
    // against a canonicalized path on Windows. This is a no-op on POSIX.
    SmallString<256> EntryPath(Iterator->path());
    sys::path::native(EntryPath);
    Entries.emplace_back(std::string(EntryPath), Iterator->type());
    Iterator.increment(Error);
    if (Error)
      return errorStatus(Error);
  }

  auto *Payload = new (std::nothrow)
      IODirectoryCursorPayload{View, std::move(Entries), 0};
  if (!Payload)
    return ioStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Task->handles().create(
      PluginIODirectoryCursorHandleKind, Payload,
      [](void *Value) {
        delete static_cast<IODirectoryCursorPayload *>(Value);
      });
  if (!Handle) {
    delete Payload;
    consumeError(Handle.takeError());
    return ioStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutCursor = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL readDirectory(
    void *Context, NevercTaskHandle TaskHandle,
    NevercDirectoryCursorHandle Cursor,
    NevercVFSDirectoryEntry *OutEntry, NevercBool *OutHasEntry) {
  if (!Context || !OutEntry || !OutHasEntry)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutHasEntry = NEVERC_FALSE;
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  PluginTaskContext *Task =
      resolveTask(Bridge, TaskHandle, View, Status);
  if (!Task)
    return Status;
  void *Raw = nullptr;
  Status = Task->handles().resolve(
      Cursor, PluginIODirectoryCursorHandleKind, &Raw);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Payload =
      *static_cast<IODirectoryCursorPayload *>(Raw);
  if (Payload.View != View)
    return ioStatus(NEVERC_STATUS_WRONG_SCOPE);
  if (Payload.Index == Payload.Entries.size())
    return neverc_status_ok();

  const vfs::directory_entry &Native =
      Payload.Entries[Payload.Index++];
  NevercVFSDirectoryEntry Entry{};
  Entry.Header = {sizeof(Entry), NEVERC_IO_API_MAJOR,
                  NEVERC_IO_API_MINOR, 0};
  Entry.Path = {Native.path().data(),
                static_cast<uint64_t>(Native.path().size())};
  Entry.Type = fileType(Native.type());
  Status = writeCallerRecord(OutEntry, Entry);
  if (Status.Code != NEVERC_STATUS_OK) {
    --Payload.Index;
    return Status;
  }
  *OutHasEntry = NEVERC_TRUE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL closeDirectory(
    void *Context, NevercTaskHandle TaskHandle,
    NevercDirectoryCursorHandle Cursor) {
  if (!Context)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginIOProcessBridge *>(Context);
  std::shared_ptr<PluginVFSView> View;
  NevercStatus Status;
  PluginTaskContext *Task =
      resolveTask(Bridge, TaskHandle, View, Status);
  if (!Task)
    return Status;
  return Task->handles().release(
      Cursor, PluginIODirectoryCursorHandleKind);
}

NevercStatus NEVERC_CALL addMemoryFile(
    void *Context, NevercSessionHandle Session, NevercStringView Path,
    NevercByteView Content, int64_t ModificationTime) {
  if (!Context)
    return ioStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return static_cast<PluginIOProcessBridge *>(Context)->addMemoryFile(
      Session, Path, Content, ModificationTime);
}

} // namespace

NevercInterfaceID ioPluginInterfaceID() {
  return {NEVERC_INTERFACE_IO_HIGH, NEVERC_INTERFACE_IO_LOW};
}

void initializePluginIOAPI(NevercIOAPI &API,
                           PluginIOProcessBridge &Bridge) {
  API = {};
  API.Header = {sizeof(API), NEVERC_IO_API_MAJOR,
                NEVERC_IO_API_MINOR, 0};
  API.Context = &Bridge;
  API.RegisterVFSProvider = registerVFSProvider;
  API.Stat = statPath;
  API.OpenFileForRead = openFileForRead;
  API.ReadFile = readFile;
  API.CloseFile = closeFile;
  API.CopyBuffer = copyBuffer;
  API.GetBufferView = getBufferView;
  API.ReleaseBuffer = releaseBuffer;
  API.Canonicalize = canonicalize;
  API.GetWorkingDirectory = getCurrentDirectory;
  API.SetWorkingDirectory = setCurrentDirectory;
  API.OpenDirectory = openDirectory;
  API.ReadDirectory = readDirectory;
  API.CloseDirectory = closeDirectory;
  API.AddMemoryFile = addMemoryFile;
  initializePluginOutputAPI(API, Bridge);
  initializePluginDependencyAPI(API, Bridge);
}

std::shared_ptr<PluginIOProcessBridge>
findPluginIOProcessBridge(PluginProcessServices &Services) {
  return std::static_pointer_cast<PluginIOProcessBridge>(
      Services.findHostService(ioPluginInterfaceID()));
}

Error registerPluginIOInterface(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(inconvertibleErrorCode(),
                             "cannot register plugin IO after interface freeze");
  auto Bridge = std::make_shared<PluginIOProcessBridge>(Services);
  if (Error E =
          Services.registerHostService(ioPluginInterfaceID(), Bridge))
    return E;
  return Services.interfaces().registerInterface(
      ioPluginInterfaceID(), NEVERC_INTERFACE_STABLE, &Bridge->api(), {});
}

Expected<IntrusiveRefCntPtr<vfs::FileSystem>>
createPluginFileSystem(
    PluginTaskContext &Task,
    IntrusiveRefCntPtr<vfs::FileSystem> BaseFileSystem) {
  auto Bridge = findPluginIOProcessBridge(Task.processServices());
  if (!Bridge)
    return createStringError(inconvertibleErrorCode(),
                             "plugin IO interface is not registered");
  auto View = Bridge->createView(Task, std::move(BaseFileSystem));
  if (!View)
    return View.takeError();
  return IntrusiveRefCntPtr<vfs::FileSystem>(
      makeIntrusiveRefCnt<PluginFileSystem>(*View));
}

} // namespace neverc::plugin
