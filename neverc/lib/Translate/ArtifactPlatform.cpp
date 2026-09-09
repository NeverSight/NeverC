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
// UTF-8/long-path conversion without changing SDK macros or copying its code.
namespace llvm::sys::windows {
std::error_code widenPath(const Twine &Path8, SmallVectorImpl<wchar_t> &Path16,
                         size_t MaxPathLen);
}
#else
#include "llvm/Support/FileSystem.h"
#endif
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
