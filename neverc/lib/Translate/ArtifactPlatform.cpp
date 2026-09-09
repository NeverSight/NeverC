#include "ArtifactPlatform.h"
#include <utility>
#if defined(_WIN32)
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/WindowsError.h"
#include <cstring>
#include <windows.h>
// Exact declaration from WindowsSupport.h; that header forces Win7 feature
// macros and hides this TU's newer SDK file-ID declarations. Reuse the linked
// UTF-8/long-path conversion without including that header or copying its code.
namespace llvm::sys::windows {
std::error_code widenPath(const Twine &Path8, SmallVectorImpl<wchar_t> &Path16,
                         size_t MaxPathLen);
}
#else
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#endif
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#if defined(__APPLE__)
#include <errno.h>
#include <stdio.h>
#elif defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace neverc::translate {
#if defined(_WIN32)
struct ArtifactFileIdentity::State {
  HANDLE Handle = INVALID_HANDLE_VALUE;
  FILE_ID_INFO Identity = {};

  ~State() {
    if (Handle != INVALID_HANDLE_VALUE)
      (void)::CloseHandle(Handle);
  }

  std::error_code read(llvm::StringRef Path, bool &Regular) {
    Regular = false;
    llvm::SmallVector<llvm::UTF16, 256> Validated;
    if (!llvm::convertUTF8ToUTF16String(Path, Validated))
      return std::make_error_code(std::errc::illegal_byte_sequence);
    // capture/matches reject embedded NUL before this explicit UTF-8 check.
    // Writer supplies canonical paths; keep the old long-path API support.
    llvm::SmallVector<wchar_t, 256> Wide;
    if (auto Error = llvm::sys::windows::widenPath(Path, Wide, MAX_PATH))
      return Error;
    Handle = ::CreateFileW(
        Wide.data(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (Handle == INVALID_HANDLE_VALUE)
      return llvm::mapWindowsError(::GetLastError());

    ::SetLastError(ERROR_SUCCESS);
    const DWORD Type = ::GetFileType(Handle);
    if (Type == FILE_TYPE_UNKNOWN) {
      const DWORD Error = ::GetLastError();
      if (Error != ERROR_SUCCESS)
        return llvm::mapWindowsError(Error);
    }
    if (Type != FILE_TYPE_DISK)
      return {};
    FILE_ATTRIBUTE_TAG_INFO Attributes = {};
    if (!::GetFileInformationByHandleEx(Handle, FileAttributeTagInfo, &Attributes,
                                      sizeof(Attributes)))
      return llvm::mapWindowsError(::GetLastError());
    if (Attributes.FileAttributes &
        (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
      return {};

    // A 64-bit BY_HANDLE_FILE_INFORMATION index is not unique on ReFS. Compare
    // the complete FileIdInfo pair; an unsupported query is an error, not a
    // reason to accept the host's pathname-derived sys::fs::UniqueID instead.
    if (!::GetFileInformationByHandleEx(Handle, FileIdInfo, &Identity,
                                      sizeof(Identity)))
      return llvm::mapWindowsError(::GetLastError());
    Regular = true;
    return {};
  }

  bool same(const State &Other) const {
    return Identity.VolumeSerialNumber == Other.Identity.VolumeSerialNumber &&
           std::memcmp(Identity.FileId.Identifier, Other.Identity.FileId.Identifier,
                       sizeof(Identity.FileId.Identifier)) == 0;
  }
};
#else
struct ArtifactFileIdentity::State {
  llvm::sys::fs::UniqueID Identity;

  std::error_code read(llvm::StringRef Path, bool &Regular) {
    Regular = false;
    llvm::sys::fs::file_status Status;
    if (auto Error = llvm::sys::fs::status(Path, Status, /*follow=*/false))
      return Error;
    if (!llvm::sys::fs::is_regular_file(Status))
      return {};
    Identity = Status.getUniqueID();
    Regular = true;
    return {};
  }

  bool same(const State &Other) const { return Identity == Other.Identity; }
};
#endif

ArtifactFileIdentity::ArtifactFileIdentity() = default;
ArtifactFileIdentity::~ArtifactFileIdentity() = default;
ArtifactFileIdentity::ArtifactFileIdentity(ArtifactFileIdentity &&) noexcept = default;
ArtifactFileIdentity &
ArtifactFileIdentity::operator=(ArtifactFileIdentity &&) noexcept = default;

std::error_code ArtifactFileIdentity::capture(llvm::StringRef Path,
                                            ArtifactFileIdentity &Result) {
  if (Path.empty() || Path.contains('\0'))
    return std::make_error_code(std::errc::invalid_argument);
  auto Candidate = std::make_unique<State>();
  bool Regular = false;
  if (auto Error = Candidate->read(Path, Regular))
    return Error;
  if (!Regular)
    return std::make_error_code(std::errc::invalid_argument);
  Result.Value = std::move(Candidate);
  return {};
}

std::error_code ArtifactFileIdentity::matches(llvm::StringRef Path,
                                            bool &Same) const {
  Same = false;
  if (!Value || Path.empty() || Path.contains('\0'))
    return std::make_error_code(std::errc::invalid_argument);
  State Current;
  bool Regular = false;
  if (auto Error = Current.read(Path, Regular))
    return Error;
  Same = Regular && Value->same(Current);
  return {};
}

#if defined(_WIN32)
namespace {
constexpr size_t MaxExistingPathBytes = 128 * 1024;
constexpr size_t MaxExistingPathComponents = 32768;
constexpr size_t MaxExistingPathWideUnits = 32768; // Includes the trailing NUL.

bool windowsSeparator(char C) { return C == '\\' || C == '/'; }

struct WindowsRoot {
  size_t End = 0;
  bool Verbatim = false;
};

bool windowsRoot(llvm::StringRef Path, WindowsRoot &Root) {
  Root = {};
  size_t Start = 0;
  bool UNC = false;
  const bool NativeVerbatim = Path.starts_with("\\\\?\\");
  const bool SlashVerbatim = Path.starts_with("//?/");
  auto Separator = [&](char C) {
    return NativeVerbatim ? C == '\\' : windowsSeparator(C);
  };
  if (NativeVerbatim || SlashVerbatim) {
    Root.Verbatim = true;
    Start = 4;
    if (Path.size() >= 8 && Path.substr(4, 3).equals_insensitive("UNC") &&
        Separator(Path[7])) {
      UNC = true;
      Start = 8;
    }
  } else if (Path.size() >= 2 && windowsSeparator(Path[0]) &&
             windowsSeparator(Path[1])) {
    UNC = true;
    Start = 2;
  }
  if (!UNC) {
    if (Path.size() < Start + 3)
      return false;
    const char Drive = Path[Start];
    if (!((Drive >= 'A' && Drive <= 'Z') || (Drive >= 'a' && Drive <= 'z')) ||
        Path[Start + 1] != ':' || !Separator(Path[Start + 2]))
      return false;
    Root.End = Start + 3;
    return true;
  }

  // A UNC root includes the share. LLVM's lexical root_path only includes the
  // server, which cannot serve as an existing filesystem root or '..' boundary.
  const size_t ServerStart = Start;
  while (Start < Path.size() && !Separator(Path[Start]))
    ++Start;
  const auto Server = Path.slice(ServerStart, Start);
  if (Server.empty() || Server == "." || Server == ".." || Server == "?" ||
      Start == Path.size())
    return false;
  ++Start;
  const size_t ShareStart = Start;
  while (Start < Path.size() && !Separator(Path[Start]))
    ++Start;
  const auto Share = Path.slice(ShareStart, Start);
  if (Share.empty() || Share == "." || Share == "..")
    return false;
  Root.End = Start;
  return true;
}

std::error_code readWindowsResolvedPath(llvm::StringRef Path,
                                       llvm::SmallVectorImpl<char> &Resolved,
                                       bool &Directory) {
  Resolved.clear();
  Directory = false;
  auto NativeError = []() {
    const auto Error = llvm::mapWindowsError(::GetLastError());
    return Error ? Error : std::make_error_code(std::errc::io_error);
  };
  llvm::SmallVector<wchar_t, 256> Wide;
  if (auto Error = llvm::sys::windows::widenPath(Path, Wide, MAX_PATH))
    return Error;
  if (Wide.size() >= MaxExistingPathWideUnits)
    return std::make_error_code(std::errc::filename_too_long);
  // Query the object without requesting directory-listing or read-attributes
  // access. Follow reparse points and do not enable any token privilege.
  const HANDLE Handle = ::CreateFileW(
      Wide.data(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (Handle == INVALID_HANDLE_VALUE)
    return NativeError();
  auto Close = llvm::make_scope_exit([Handle]() { (void)::CloseHandle(Handle); });
  ::SetLastError(ERROR_SUCCESS);
  const DWORD Type = ::GetFileType(Handle);
  if (Type == FILE_TYPE_UNKNOWN && ::GetLastError() != ERROR_SUCCESS)
    return NativeError();
  if (Type != FILE_TYPE_DISK)
    return std::make_error_code(std::errc::invalid_argument);
  FILE_STANDARD_INFO Standard = {};
  if (!::GetFileInformationByHandleEx(Handle, FileStandardInfo, &Standard,
                                    sizeof(Standard)))
    return NativeError();

  llvm::SmallVector<wchar_t, 256> FinalName;
  FinalName.resize_for_overwrite(256);
  constexpr DWORD Flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
  DWORD Count = ::GetFinalPathNameByHandleW(
      Handle, FinalName.data(), static_cast<DWORD>(FinalName.size()), Flags);
  if (Count == 0)
    return NativeError();
  if (Count >= FinalName.size()) {
    if (Count > MaxExistingPathWideUnits)
      return std::make_error_code(std::errc::filename_too_long);
    FinalName.resize_for_overwrite(Count);
    Count = ::GetFinalPathNameByHandleW(
        Handle, FinalName.data(), static_cast<DWORD>(FinalName.size()), Flags);
    if (Count == 0)
      return NativeError();
    // A concurrent rename may grow the required buffer again. Bound this
    // query; do not consume a partial name or fall back to the opened spelling.
    if (Count >= FinalName.size())
      return std::make_error_code(std::errc::filename_too_long);
  }
  const wchar_t *Data = FinalName.data();
  size_t Length = Count; // Successful counts exclude the terminating NUL.
  if (Length >= 8 && std::memcmp(Data, L"\\\\?\\UNC\\", 8 * sizeof(wchar_t)) == 0) {
    // Match the host's DOS/UNC output spelling without the namespace prefix.
    FinalName[6] = L'\\';
    Data += 6;
    Length -= 6;
  } else if (Length >= 7 &&
             std::memcmp(Data, L"\\\\?\\", 4 * sizeof(wchar_t)) == 0 &&
             ((Data[4] >= L'A' && Data[4] <= L'Z') ||
              (Data[4] >= L'a' && Data[4] <= L'z')) &&
             Data[5] == L':' && Data[6] == L'\\') {
    Data += 4;
    Length -= 4;
  } else {
    return std::make_error_code(std::errc::io_error);
  }
  llvm::SmallVector<llvm::UTF16, 256> Units;
  Units.reserve(Length);
  for (size_t I = 0; I < Length; ++I)
    Units.push_back(static_cast<llvm::UTF16>(Data[I]));
  llvm::SmallString<256> Converted;
  if (!llvm::convertUTF16ToUTF8String(Units, Converted))
    return std::make_error_code(std::errc::illegal_byte_sequence);
  if (Converted.size() > MaxExistingPathBytes)
    return std::make_error_code(std::errc::filename_too_long);
  WindowsRoot Root;
  if (Converted.empty() || Converted.str().contains('\0') ||
      !windowsRoot(Converted, Root) || Root.Verbatim)
    return std::make_error_code(std::errc::io_error);
  Resolved.assign(Converted.begin(), Converted.end());
  Directory = Standard.Directory != FALSE;
  return {};
}
} // namespace
#endif

std::error_code resolveExistingPath(llvm::StringRef Path,
                                    llvm::SmallVectorImpl<char> &Result) {
#if defined(_WIN32)
  // These bound parser work, not native filesystem latency. They exceed the
  // usual 32767 UTF-16-unit path's encoding needs, but reject longer redundant
  // spellings that a native API might otherwise shorten.
  if (Path.size() > MaxExistingPathBytes) {
    Result.clear();
    return std::make_error_code(std::errc::filename_too_long);
  }
#endif
  // Copy before clearing Result, because Path may reference its storage.
  const llvm::SmallString<256> Absolute(Path);
  Result.clear();
  if (Absolute.empty() || Absolute.str().contains('\0'))
    return std::make_error_code(std::errc::invalid_argument);
  llvm::SmallString<256> Current;
#if defined(_WIN32)
  WindowsRoot Root;
  if (!windowsRoot(Absolute, Root))
    return std::make_error_code(std::errc::invalid_argument);
  const bool NativeVerbatim = Absolute.str().starts_with("\\\\?\\");
  auto Separator = [&](char C) {
    return NativeVerbatim ? C == '\\' : windowsSeparator(C);
  };
  llvm::SmallVector<llvm::StringRef, 16> Components;
  size_t Position = Root.End;
  // Preflight the complete component count before the first native lookup.
  while (Position < Absolute.size()) {
    while (Position < Absolute.size() && Separator(Absolute[Position]))
      ++Position;
    const size_t Begin = Position;
    while (Position < Absolute.size() && !Separator(Absolute[Position]))
      ++Position;
    if (Components.size() == MaxExistingPathComponents)
      return std::make_error_code(std::errc::filename_too_long);
    Components.push_back(Begin == Absolute.size()
                             ? llvm::StringRef(".")
                             : Absolute.str().slice(Begin, Position));
  }
  llvm::SmallVector<llvm::UTF16, 256> Validated;
  if (!llvm::convertUTF8ToUTF16String(Absolute, Validated))
    return std::make_error_code(std::errc::illegal_byte_sequence);
  bool Directory = false;
  if (Root.Verbatim) {
    // Preserve whole native semantics. Do not assign ordinary navigation
    // meaning to literal components in an explicitly verbatim namespace.
    if (auto Error = readWindowsResolvedPath(Absolute, Current, Directory))
      return Error;
  } else {
    if (auto Error =
            readWindowsResolvedPath(Absolute.str().take_front(Root.End), Current,
                                    Directory))
      return Error;
    for (const auto Component : Components) {
      if (!Directory)
        return std::make_error_code(std::errc::not_a_directory);
      if (Component == ".")
        continue;

      llvm::SmallString<256> Next(Current);
      if (Component == "..") {
        WindowsRoot PhysicalRoot;
        if (!windowsRoot(Current, PhysicalRoot))
          return std::make_error_code(std::errc::io_error);
        // A link may have crossed drives or shares. Recompute the boundary
        // from its resolved target instead of retaining the input's root.
        while (Next.size() > PhysicalRoot.End &&
               windowsSeparator(Next.back()))
          Next.pop_back();
        while (Next.size() > PhysicalRoot.End &&
               !windowsSeparator(Next.back()))
          Next.pop_back();
        while (Next.size() > PhysicalRoot.End &&
               windowsSeparator(Next.back()))
          Next.pop_back();
      } else {
        if (!windowsSeparator(Next.back()))
          Next.push_back('\\');
        Next.append(Component.begin(), Component.end());
      }
      llvm::SmallString<256> Resolved;
      if (auto Error = readWindowsResolvedPath(Next, Resolved, Directory))
        return Error;
      Current = Resolved;
    }
  }
#else
  if (!llvm::sys::path::is_absolute(Absolute))
    return std::make_error_code(std::errc::invalid_argument);
  if (auto Error = llvm::sys::fs::real_path(Absolute, Current))
    return Error;
  if (Current.empty() || !llvm::sys::path::is_absolute(Current))
    return std::make_error_code(std::errc::io_error);
#endif
  Result.assign(Current.begin(), Current.end());
  return {};
}

std::error_code renameDirectoryExclusive(llvm::StringRef From,
                                         llvm::StringRef To) {
#if defined(_WIN32)
  llvm::SmallVector<llvm::UTF16, 256> WFrom, WTo;
  if (!llvm::convertUTF8ToUTF16String(From, WFrom) ||
      !llvm::convertUTF8ToUTF16String(To, WTo))
    return std::make_error_code(std::errc::illegal_byte_sequence);
  WFrom.push_back(0);
  WTo.push_back(0);
  if (MoveFileExW(reinterpret_cast<const wchar_t *>(WFrom.data()),
                  reinterpret_cast<const wchar_t *>(WTo.data()), 0))
    return {};
  return {static_cast<int>(GetLastError()), std::system_category()};
#elif defined(__APPLE__)
  if (::renamex_np(From.str().c_str(), To.str().c_str(), RENAME_EXCL) == 0)
    return {};
  return {errno, std::generic_category()};
#elif defined(__linux__) && defined(SYS_renameat2)
  if (::syscall(SYS_renameat2, AT_FDCWD, From.str().c_str(), AT_FDCWD,
                To.str().c_str(), 1 /* RENAME_NOREPLACE */) == 0)
    return {};
  return {errno, std::generic_category()};
#else
  return std::make_error_code(std::errc::operation_not_supported);
#endif
}
} // namespace neverc::translate
