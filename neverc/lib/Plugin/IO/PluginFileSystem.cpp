#include "PluginFileSystem.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <chrono>
#include <cstring>
#include <limits>
#include <system_error>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr size_t MaximumRouteDepth = 32;

struct ActiveRoute {
  const PluginVFSView *View = nullptr;
  const char *Operation = nullptr;
  std::string Path;
};

thread_local std::vector<ActiveRoute> ActiveRoutes;

class RouteGuard {
public:
  RouteGuard(const PluginVFSView &View, const char *Operation,
             StringRef Path) {
    if (ActiveRoutes.size() >= MaximumRouteDepth)
      return;
    for (const ActiveRoute &Route : ActiveRoutes) {
      if (Route.View == &View && StringRef(Route.Operation) == Operation &&
          Route.Path == Path)
        return;
    }
    ActiveRoutes.push_back({&View, Operation, Path.str()});
    Entered = true;
  }

  ~RouteGuard() {
    if (Entered)
      ActiveRoutes.pop_back();
  }

  bool entered() const { return Entered; }

private:
  bool Entered = false;
};

std::error_code statusError(NevercStatus Status) {
  if (Status.Code == NEVERC_STATUS_PLUGIN_FAILURE) {
    switch (Status.Detail) {
    case NEVERC_IO_ERROR_NOT_FOUND:
      return make_error_code(errc::no_such_file_or_directory);
    case NEVERC_IO_ERROR_PERMISSION_DENIED:
      return make_error_code(errc::permission_denied);
    case NEVERC_IO_ERROR_NOT_DIRECTORY:
      return make_error_code(errc::not_a_directory);
    case NEVERC_IO_ERROR_IS_DIRECTORY:
      return make_error_code(errc::is_a_directory);
    case NEVERC_IO_ERROR_INVALID_PATH:
      return make_error_code(errc::invalid_argument);
    default:
      return make_error_code(errc::io_error);
    }
  }
  switch (Status.Code) {
  case NEVERC_STATUS_CANCELLED:
    return std::make_error_code(std::errc::operation_canceled);
  case NEVERC_STATUS_RESOURCE_EXHAUSTED:
    return make_error_code(errc::not_enough_memory);
  case NEVERC_STATUS_NOT_FOUND:
    return make_error_code(errc::no_such_file_or_directory);
  case NEVERC_STATUS_REENTRANCY_DENIED:
    return make_error_code(errc::resource_deadlock_would_occur);
  case NEVERC_STATUS_INVALID_ARGUMENT:
  case NEVERC_STATUS_INVALID_DESCRIPTOR:
    return make_error_code(errc::invalid_argument);
  default:
    return make_error_code(errc::io_error);
  }
}

NevercStatus validationFailure() {
  NevercStatus Status = neverc_status_ok();
  Status.Code = NEVERC_STATUS_VERIFICATION_FAILED;
  return Status;
}

bool validSuccessStatus(NevercStatus Status) {
  return Status.Code == NEVERC_STATUS_OK && Status.Flags == 0 &&
         Status.Detail == 0;
}

bool validIOHeader(const NevercABITableHeader &Header, uint64_t Size) {
  return Header.StructSize >= Size &&
         Header.Major == NEVERC_IO_API_MAJOR &&
         Header.Minor <= NEVERC_IO_API_MINOR && Header.Flags == 0;
}

bool validView(NevercStringView View) {
  return View.Length <= std::numeric_limits<size_t>::max() &&
         (View.Data || View.Length == 0) &&
         !StringRef(View.Data ? View.Data : "",
                    static_cast<size_t>(View.Length))
              .contains('\0');
}

bool allZero(const void *Data, size_t Size) {
  const auto *Bytes = static_cast<const unsigned char *>(Data);
  for (size_t Index = 0; Index != Size; ++Index)
    if (Bytes[Index] != 0)
      return false;
  return true;
}

sys::fs::file_type toLLVMFileType(NevercVFSFileType Type) {
  switch (Type) {
  case NEVERC_VFS_FILE_UNKNOWN:
    return sys::fs::file_type::type_unknown;
  case NEVERC_VFS_FILE_REGULAR:
    return sys::fs::file_type::regular_file;
  case NEVERC_VFS_FILE_DIRECTORY:
    return sys::fs::file_type::directory_file;
  case NEVERC_VFS_FILE_SYMLINK:
    return sys::fs::file_type::symlink_file;
  case NEVERC_VFS_FILE_OTHER:
    return sys::fs::file_type::type_unknown;
  default:
    return sys::fs::file_type::status_error;
  }
}

NevercVFSFileType fromLLVMFileType(sys::fs::file_type Type) {
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

bool validProviderStatus(const NevercVFSStatus &Status) {
  constexpr int64_t NanosecondsPerSecond = INT64_C(1000000000);
  constexpr int64_t MinimumSeconds =
      std::numeric_limits<int64_t>::min() / NanosecondsPerSecond;
  constexpr int64_t MaximumSeconds =
      std::numeric_limits<int64_t>::max() / NanosecondsPerSecond;
  return validIOHeader(Status.Header, sizeof(Status)) &&
         Status.Type >= NEVERC_VFS_FILE_REGULAR &&
         Status.Type <= NEVERC_VFS_FILE_OTHER &&
         (Status.Local == NEVERC_FALSE || Status.Local == NEVERC_TRUE) &&
         Status.Reserved == 0 &&
         Status.ModificationTime >= MinimumSeconds &&
         Status.ModificationTime <= MaximumSeconds;
}

vfs::Status makeStatus(StringRef Path, const NevercVFSStatus &Status) {
  auto Duration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::seconds(Status.ModificationTime));
  return vfs::Status(
      Path, sys::fs::UniqueID(Status.UniqueID.Device, Status.UniqueID.File),
      sys::TimePoint<>(Duration), 0, 0, Status.Size,
      toLLVMFileType(Status.Type),
      static_cast<sys::fs::perms>(Status.Permissions));
}

bool providerApplies(PluginTaskContext &Task,
                     const PluginVFSProviderBinding &Provider,
                     StringRef Path, std::error_code &Error) {
  Error.clear();
  if (!Provider.RoutePrefix.empty() &&
      !Path.starts_with(Provider.RoutePrefix))
    return false;
  if (!Provider.Descriptor.MatchesPath)
    return true;

  NevercBool Matches = NEVERC_FALSE;
  auto Result = Task.invokeCallback(
      Provider.PluginID, "VFS.MatchesPath",
      [&] {
        NevercStatus Status = Provider.Descriptor.MatchesPath(
            Task.handle(), {Path.data(), static_cast<uint64_t>(Path.size())},
            Provider.Descriptor.UserData, &Matches);
        if (!validSuccessStatus(Status))
          return Status;
        if (Matches != NEVERC_FALSE && Matches != NEVERC_TRUE)
          return validationFailure();
        return Status;
      });
  if (!Result) {
    consumeError(Result.takeError());
    Error = make_error_code(errc::io_error);
    return false;
  }
  if (!validSuccessStatus(*Result)) {
    Error = statusError(*Result);
    return false;
  }
  return Matches == NEVERC_TRUE;
}

class CopiedVFSFile final : public vfs::File {
public:
  CopiedVFSFile(vfs::Status StatusValue, std::string ContentValue)
      : FileStatus(std::move(StatusValue)),
        Content(std::move(ContentValue)) {}

  ErrorOr<vfs::Status> status() override {
    if (Closed)
      return make_error_code(errc::bad_file_descriptor);
    return FileStatus;
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>>
  getBuffer(const Twine &Name, int64_t, bool RequiresNullTerminator,
            bool) override {
    if (Closed)
      return make_error_code(errc::bad_file_descriptor);
    if (RequiresNullTerminator)
      return MemoryBuffer::getMemBufferCopy(Content, Name);
    return MemoryBuffer::getMemBufferCopy(Content, Name);
  }

  std::error_code close() override {
    Closed = true;
    return {};
  }

  ~CopiedVFSFile() override { (void)close(); }

private:
  vfs::Status FileStatus;
  std::string Content;
  bool Closed = false;
};

class CopiedDirectoryIterator final : public vfs::detail::DirIterImpl {
public:
  explicit CopiedDirectoryIterator(
      std::vector<vfs::directory_entry> EntriesValue)
      : Entries(std::move(EntriesValue)) {
    updateCurrent();
  }

  std::error_code increment() override {
    if (Index < Entries.size())
      ++Index;
    updateCurrent();
    return {};
  }

private:
  void updateCurrent() {
    CurrentEntry =
        Index < Entries.size() ? Entries[Index] : vfs::directory_entry();
  }

  std::vector<vfs::directory_entry> Entries;
  size_t Index = 0;
};

SmallString<256> materializePath(const Twine &Path) {
  SmallString<256> Storage;
  Path.toVector(Storage);
  return Storage;
}

} // namespace

PluginVFSView::PluginVFSView(
    PluginIOProcessBridge &OwnerValue, NevercTaskHandle TaskValue,
    IntrusiveRefCntPtr<vfs::FileSystem> BaseFileSystemValue,
    std::vector<PluginVFSProviderBinding> ProvidersValue)
    : Owner(OwnerValue), Task(TaskValue),
      BaseFileSystem(std::move(BaseFileSystemValue)),
      Providers(std::move(ProvidersValue)) {}

PluginTaskContext *PluginVFSView::task() const {
  return Owner.services().findTaskScope(Task);
}

ErrorOr<vfs::Status> PluginVFSView::status(StringRef Path) {
  RouteGuard Route(*this, "status", Path);
  if (!Route.entered())
    return make_error_code(errc::resource_deadlock_would_occur);
  PluginTaskContext *TaskContext = task();
  if (!TaskContext || TaskContext->isEnded())
    return std::make_error_code(std::errc::operation_canceled);

  for (const PluginVFSProviderBinding &Provider : Providers) {
    if (!Provider.Descriptor.Status)
      continue;
    std::error_code PredicateError;
    if (!providerApplies(*TaskContext, Provider, Path, PredicateError)) {
      if (PredicateError)
        return PredicateError;
      continue;
    }

    NevercVFSStatusResult Result{};
    Result.Header = {sizeof(Result), NEVERC_IO_API_MAJOR,
                     NEVERC_IO_API_MINOR, 0};
    std::optional<vfs::Status> Copied;
    auto Callback = TaskContext->invokeCallback(
        Provider.PluginID, "VFS.Status",
        [&] {
          NevercStatus CallbackStatus = Provider.Descriptor.Status(
              Task, {Path.data(), static_cast<uint64_t>(Path.size())},
              Provider.Descriptor.UserData, &Result);
          if (!validSuccessStatus(CallbackStatus))
            return CallbackStatus;
          if (!validIOHeader(Result.Header, sizeof(Result)) ||
              Result.Reserved != 0)
            return validationFailure();
          if (Result.Disposition == NEVERC_VFS_RESULT_NOT_HANDLED) {
            if (!allZero(&Result.Status, sizeof(Result.Status)))
              return validationFailure();
            return CallbackStatus;
          }
          if (Result.Disposition != NEVERC_VFS_RESULT_HANDLED ||
              !validProviderStatus(Result.Status))
            return validationFailure();
          Copied.emplace(makeStatus(Path, Result.Status));
          return CallbackStatus;
        });
    if (!Callback) {
      consumeError(Callback.takeError());
      return make_error_code(errc::io_error);
    }
    if (!validSuccessStatus(*Callback))
      return statusError(*Callback);
    if (Result.Disposition == NEVERC_VFS_RESULT_HANDLED) {
      std::lock_guard<std::mutex> Lock(LocalityMutex);
      ProviderLocality[Path.str()] =
          Result.Status.Local == NEVERC_TRUE;
      return std::move(*Copied);
    }
  }
  {
    std::lock_guard<std::mutex> Lock(LocalityMutex);
    ProviderLocality.erase(Path.str());
  }
  return BaseFileSystem->status(Path);
}

ErrorOr<std::unique_ptr<vfs::File>>
PluginVFSView::openFileForRead(StringRef Path) {
  RouteGuard Route(*this, "open", Path);
  if (!Route.entered())
    return make_error_code(errc::resource_deadlock_would_occur);
  PluginTaskContext *TaskContext = task();
  if (!TaskContext || TaskContext->isEnded())
    return std::make_error_code(std::errc::operation_canceled);

  for (const PluginVFSProviderBinding &Provider : Providers) {
    if (!Provider.Descriptor.OpenRead)
      continue;
    std::error_code PredicateError;
    if (!providerApplies(*TaskContext, Provider, Path, PredicateError)) {
      if (PredicateError)
        return PredicateError;
      continue;
    }

    NevercVFSOpenReadResult Result{};
    Result.Header = {sizeof(Result), NEVERC_IO_API_MAJOR,
                     NEVERC_IO_API_MINOR, 0};
    std::optional<vfs::Status> CopiedStatus;
    std::string CopiedContent;
    auto Callback = TaskContext->invokeCallback(
        Provider.PluginID, "VFS.OpenRead",
        [&] {
          NevercStatus CallbackStatus = Provider.Descriptor.OpenRead(
              Task, {Path.data(), static_cast<uint64_t>(Path.size())},
              Provider.Descriptor.UserData, &Result);
          if (!validSuccessStatus(CallbackStatus))
            return CallbackStatus;
          if (!validIOHeader(Result.Header, sizeof(Result)) ||
              Result.Reserved != 0)
            return validationFailure();
          if (Result.Disposition == NEVERC_VFS_RESULT_NOT_HANDLED) {
            if (!allZero(&Result.Status, sizeof(Result.Status)) ||
                !allZero(&Result.Content, sizeof(Result.Content)))
              return validationFailure();
            return CallbackStatus;
          }
          if (Result.Disposition != NEVERC_VFS_RESULT_HANDLED ||
              !validProviderStatus(Result.Status) ||
              !validIOHeader(Result.Content.Header,
                             sizeof(Result.Content)) ||
              Result.Content.Length > std::numeric_limits<size_t>::max() ||
              (!Result.Content.Data && Result.Content.Length != 0) ||
              (Result.Content.NullTerminated != NEVERC_FALSE &&
               Result.Content.NullTerminated != NEVERC_TRUE) ||
              Result.Content.Reserved != 0 ||
              Result.Status.Type != NEVERC_VFS_FILE_REGULAR ||
              Result.Status.Size != Result.Content.Length)
            return validationFailure();
          CopiedStatus.emplace(makeStatus(Path, Result.Status));
          if (Result.Content.Length != 0)
            CopiedContent.assign(
                reinterpret_cast<const char *>(Result.Content.Data),
                static_cast<size_t>(Result.Content.Length));
          return CallbackStatus;
        });
    if (!Callback) {
      consumeError(Callback.takeError());
      return make_error_code(errc::io_error);
    }
    if (!validSuccessStatus(*Callback))
      return statusError(*Callback);
    if (Result.Disposition == NEVERC_VFS_RESULT_HANDLED)
      return std::make_unique<CopiedVFSFile>(
          std::move(*CopiedStatus), std::move(CopiedContent));
  }
  return BaseFileSystem->openFileForRead(Path);
}

vfs::directory_iterator PluginVFSView::dirBegin(
    StringRef Path, std::error_code &Error) {
  Error.clear();
  RouteGuard Route(*this, "directory", Path);
  if (!Route.entered()) {
    Error = make_error_code(errc::resource_deadlock_would_occur);
    return {};
  }
  PluginTaskContext *TaskContext = task();
  if (!TaskContext || TaskContext->isEnded()) {
    Error = std::make_error_code(std::errc::operation_canceled);
    return {};
  }

  for (const PluginVFSProviderBinding &Provider : Providers) {
    if (!Provider.Descriptor.ReadDirectory)
      continue;
    std::error_code PredicateError;
    if (!providerApplies(*TaskContext, Provider, Path, PredicateError)) {
      if (PredicateError) {
        Error = PredicateError;
        return {};
      }
      continue;
    }

    NevercVFSDirectoryResult Result{};
    Result.Header = {sizeof(Result), NEVERC_IO_API_MAJOR,
                     NEVERC_IO_API_MINOR, 0};
    std::vector<vfs::directory_entry> Copied;
    auto Callback = TaskContext->invokeCallback(
        Provider.PluginID, "VFS.ReadDirectory",
        [&] {
          NevercStatus CallbackStatus =
              Provider.Descriptor.ReadDirectory(
                  Task, {Path.data(), static_cast<uint64_t>(Path.size())},
                  Provider.Descriptor.UserData, &Result);
          if (!validSuccessStatus(CallbackStatus))
            return CallbackStatus;
          if (!validIOHeader(Result.Header, sizeof(Result)) ||
              Result.Reserved != 0)
            return validationFailure();
          if (Result.Disposition == NEVERC_VFS_RESULT_NOT_HANDLED) {
            if (Result.Entries || Result.EntryCount != 0)
              return validationFailure();
            return CallbackStatus;
          }
          if (Result.Disposition != NEVERC_VFS_RESULT_HANDLED ||
              Result.EntryCount > std::numeric_limits<size_t>::max() ||
              (!Result.Entries && Result.EntryCount != 0))
            return validationFailure();
          Copied.reserve(static_cast<size_t>(Result.EntryCount));
          for (size_t Index = 0;
               Index != static_cast<size_t>(Result.EntryCount); ++Index) {
            const NevercVFSDirectoryEntry &Entry = Result.Entries[Index];
            if (!validIOHeader(Entry.Header, sizeof(Entry)) ||
                !validView(Entry.Path) || Entry.Path.Length == 0 ||
                Entry.Type > NEVERC_VFS_FILE_OTHER ||
                Entry.Reserved != 0)
              return validationFailure();
            StringRef EntryPath(Entry.Path.Data,
                                static_cast<size_t>(Entry.Path.Length));
            Copied.emplace_back(EntryPath.str(),
                                toLLVMFileType(Entry.Type));
          }
          return CallbackStatus;
        });
    if (!Callback) {
      consumeError(Callback.takeError());
      Error = make_error_code(errc::io_error);
      return {};
    }
    if (!validSuccessStatus(*Callback)) {
      Error = statusError(*Callback);
      return {};
    }
    if (Result.Disposition == NEVERC_VFS_RESULT_HANDLED) {
      if (Copied.empty())
        return {};
      return vfs::directory_iterator(
          new CopiedDirectoryIterator(std::move(Copied)));
    }
  }
  return BaseFileSystem->dir_begin(Path, Error);
}

ErrorOr<SmallString<256>>
PluginVFSView::getCurrentWorkingDirectory() const {
  return BaseFileSystem->getCurrentWorkingDirectory();
}

std::error_code
PluginVFSView::setCurrentWorkingDirectory(StringRef Path) {
  return BaseFileSystem->setCurrentWorkingDirectory(Path);
}

std::error_code PluginVFSView::getRealPath(
    StringRef Path, SmallVectorImpl<char> &Output) const {
  auto &Mutable = *const_cast<PluginVFSView *>(this);
  RouteGuard Route(Mutable, "canonicalize", Path);
  if (!Route.entered())
    return make_error_code(errc::resource_deadlock_would_occur);
  PluginTaskContext *TaskContext = Mutable.task();
  if (!TaskContext || TaskContext->isEnded())
    return std::make_error_code(std::errc::operation_canceled);

  for (const PluginVFSProviderBinding &Provider : Providers) {
    if (!Provider.Descriptor.Canonicalize)
      continue;
    std::error_code PredicateError;
    if (!providerApplies(*TaskContext, Provider, Path, PredicateError)) {
      if (PredicateError)
        return PredicateError;
      continue;
    }

    NevercVFSCanonicalPathResult Result{};
    Result.Header = {sizeof(Result), NEVERC_IO_API_MAJOR,
                     NEVERC_IO_API_MINOR, 0};
    std::string Copied;
    auto Callback = TaskContext->invokeCallback(
        Provider.PluginID, "VFS.Canonicalize",
        [&] {
          NevercStatus CallbackStatus =
              Provider.Descriptor.Canonicalize(
                  Task, {Path.data(), static_cast<uint64_t>(Path.size())},
                  Provider.Descriptor.UserData, &Result);
          if (!validSuccessStatus(CallbackStatus))
            return CallbackStatus;
          if (!validIOHeader(Result.Header, sizeof(Result)) ||
              Result.Reserved != 0)
            return validationFailure();
          if (Result.Disposition == NEVERC_VFS_RESULT_NOT_HANDLED) {
            if (Result.Path.Data || Result.Path.Length != 0)
              return validationFailure();
            return CallbackStatus;
          }
          if (Result.Disposition != NEVERC_VFS_RESULT_HANDLED ||
              !validView(Result.Path) || Result.Path.Length == 0)
            return validationFailure();
          Copied.assign(Result.Path.Data,
                        static_cast<size_t>(Result.Path.Length));
          return CallbackStatus;
        });
    if (!Callback) {
      consumeError(Callback.takeError());
      return make_error_code(errc::io_error);
    }
    if (!validSuccessStatus(*Callback))
      return statusError(*Callback);
    if (Result.Disposition == NEVERC_VFS_RESULT_HANDLED) {
      Output.assign(Copied.begin(), Copied.end());
      return {};
    }
  }
  return BaseFileSystem->getRealPath(Path, Output);
}

std::error_code PluginVFSView::isLocal(StringRef Path, bool &Result) {
  {
    std::lock_guard<std::mutex> Lock(LocalityMutex);
    auto It = ProviderLocality.find(Path.str());
    if (It != ProviderLocality.end()) {
      Result = It->second;
      return {};
    }
  }
  std::error_code BaseError = BaseFileSystem->isLocal(Path, Result);
  if (!BaseError ||
      BaseError != errc::no_such_file_or_directory)
    return BaseError;
  auto Status = status(Path);
  if (!Status)
    return Status.getError();
  std::lock_guard<std::mutex> Lock(LocalityMutex);
  auto It = ProviderLocality.find(Path.str());
  Result = It != ProviderLocality.end() && It->second;
  return {};
}

PluginFileSystem::PluginFileSystem(std::shared_ptr<PluginVFSView> ViewValue)
    : View(std::move(ViewValue)) {}

ErrorOr<vfs::Status> PluginFileSystem::status(const Twine &Path) {
  SmallString<256> Storage = materializePath(Path);
  return View->status(Storage);
}

ErrorOr<std::unique_ptr<vfs::File>>
PluginFileSystem::openFileForRead(const Twine &Path) {
  SmallString<256> Storage = materializePath(Path);
  return View->openFileForRead(Storage);
}

vfs::directory_iterator
PluginFileSystem::dir_begin(const Twine &Dir, std::error_code &Error) {
  SmallString<256> Storage = materializePath(Dir);
  return View->dirBegin(Storage, Error);
}

ErrorOr<SmallString<256>>
PluginFileSystem::getCurrentWorkingDirectory() const {
  return View->getCurrentWorkingDirectory();
}

std::error_code
PluginFileSystem::setCurrentWorkingDirectory(const Twine &Path) {
  SmallString<256> Storage = materializePath(Path);
  return View->setCurrentWorkingDirectory(Storage);
}

std::error_code PluginFileSystem::getRealPath(
    const Twine &Path, SmallVectorImpl<char> &Output) const {
  SmallString<256> Storage = materializePath(Path);
  return View->getRealPath(Storage, Output);
}

std::error_code
PluginFileSystem::isLocal(const Twine &Path, bool &Result) {
  SmallString<256> Storage = materializePath(Path);
  return View->isLocal(Storage, Result);
}

PluginIOProcessBridge::PluginIOProcessBridge(
    PluginProcessServices &ServicesValue)
    : Services(ServicesValue),
      Outputs(ServicesValue.outputCoordinator()) {
  initializePluginIOAPI(API, *this);
}

std::vector<PluginVFSProviderBinding>
PluginIOProcessBridge::collectProviders(PluginSession &Session) const {
  std::vector<PluginVFSProviderBinding> Result;
  for (const auto &Module : Session.plugins()) {
    const PluginPublishedRegistration *Registration =
        Module->registration();
    if (!Registration)
      continue;
    for (const PluginRegistrationRecord &Record :
         Registration->records()) {
      if (Record.Kind != PluginRegistrationKind::VFSProvider)
        continue;
      PluginVFSProviderBinding Binding;
      Binding.PluginID = Module->descriptor().PluginID;
      Binding.ProviderID = Record.ProviderID;
      Binding.RoutePrefix = Record.RoutePrefix;
      Binding.Descriptor = Record.VFSProvider;
      Result.push_back(std::move(Binding));
    }
  }
  return Result;
}

IntrusiveRefCntPtr<vfs::FileSystem>
PluginIOProcessBridge::buildBaseView(
    PluginSession &Session,
    IntrusiveRefCntPtr<vfs::FileSystem> Base) {
  std::map<std::string, MemoryFile> Files;
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto MergeSession = [&](uint64_t Owner) {
      auto It = SessionMemoryFiles.find(Owner);
      if (It == SessionMemoryFiles.end())
        return;
      for (const auto &Entry : It->second)
        Files.insert_or_assign(Entry.first, Entry.second);
    };
    for (uint64_t Owner : Session.ancestorSessionOwners())
      MergeSession(Owner);
    MergeSession(Session.handle().Owner);
  }
  if (Files.empty())
    return Base;

  auto Memory = makeIntrusiveRefCnt<vfs::InMemoryFileSystem>();
  if (auto WorkingDirectory = Base->getCurrentWorkingDirectory())
    (void)Memory->setCurrentWorkingDirectory(*WorkingDirectory);
  for (const auto &Entry : Files) {
    const MemoryFile &File = Entry.second;
    auto Buffer = MemoryBuffer::getMemBufferCopy(
        StringRef(File.Content.data(), File.Content.size()), File.Path);
    (void)Memory->addFile(File.Path,
                          static_cast<time_t>(File.ModificationTime),
                          std::move(Buffer));
  }
  auto Overlay = makeIntrusiveRefCnt<vfs::OverlayFileSystem>(Base);
  Overlay->pushOverlay(Memory);
  return Overlay;
}

Expected<std::shared_ptr<PluginVFSView>>
PluginIOProcessBridge::createView(
    PluginTaskContext &TaskContext,
    IntrusiveRefCntPtr<vfs::FileSystem> BaseFileSystem) {
  if (!BaseFileSystem)
    return createStringError(inconvertibleErrorCode(),
                             "plugin VFS base file system is null");
  if (TaskContext.isEnded())
    return createStringError(inconvertibleErrorCode(),
                             "cannot create a VFS view for an ended task");
  auto Key = std::make_pair(TaskContext.handle().Owner,
                            TaskContext.handle().Value);
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto Existing = TaskViews.find(Key);
    if (Existing != TaskViews.end())
      return Existing->second;
  }

  IntrusiveRefCntPtr<vfs::FileSystem> FrozenBase =
      buildBaseView(TaskContext.session(), std::move(BaseFileSystem));
  auto View = std::make_shared<PluginVFSView>(
      *this, TaskContext.handle(), std::move(FrozenBase),
      collectProviders(TaskContext.session()));
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    TaskViews[Key] = View;
  }
  return View;
}

std::shared_ptr<PluginVFSView>
PluginIOProcessBridge::findView(NevercTaskHandle TaskHandle) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto It =
      TaskViews.find(std::make_pair(TaskHandle.Owner, TaskHandle.Value));
  return It == TaskViews.end() ? nullptr : It->second;
}

void PluginIOProcessBridge::taskScopeUnregistered(
    NevercTaskHandle TaskHandle) noexcept {
  std::lock_guard<std::mutex> Lock(Mutex);
  const auto Key = std::make_pair(TaskHandle.Owner, TaskHandle.Value);
  TaskViews.erase(Key);
  TaskMemoryOutputs.erase(Key);
  TaskOutputs.erase(Key);
  TaskOutputStreams.erase(Key);
  TaskDependencies.erase(Key);
}

NevercStatus PluginIOProcessBridge::addMemoryFile(
    NevercSessionHandle SessionHandle, NevercStringView Path,
    NevercByteView Content, int64_t ModificationTime) {
  NevercStatus Result = neverc_status_ok();
  if (!validView(Path) || Path.Length == 0 ||
      Content.Length > std::numeric_limits<size_t>::max() ||
      (!Content.Data && Content.Length != 0) ||
      !Services.findSessionScope(SessionHandle) ||
      ModificationTime < std::numeric_limits<time_t>::min() ||
      ModificationTime > std::numeric_limits<time_t>::max()) {
    Result.Code = NEVERC_STATUS_INVALID_ARGUMENT;
    return Result;
  }
  std::string OwnedPath(Path.Data, static_cast<size_t>(Path.Length));
  std::vector<char> OwnedContent;
  const auto *Begin = reinterpret_cast<const char *>(Content.Data);
  if (Content.Length != 0)
    OwnedContent.assign(Begin, Begin + static_cast<size_t>(Content.Length));

  std::lock_guard<std::mutex> Lock(Mutex);
  auto &Files = SessionMemoryFiles[SessionHandle.Owner];
  if (Files.count(OwnedPath) != 0) {
    Result.Code = NEVERC_STATUS_DUPLICATE_ID;
    return Result;
  }
  Files.emplace(OwnedPath,
                MemoryFile{OwnedPath, std::move(OwnedContent),
                           ModificationTime});
  return Result;
}

} // namespace neverc::plugin
