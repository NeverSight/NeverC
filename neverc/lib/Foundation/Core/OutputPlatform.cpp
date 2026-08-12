#include "OutputPlatform.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <cerrno>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#include "llvm/ADT/SmallVector.h"
#ifndef NOGDI
#define NOGDI
#endif
#include "llvm/Support/Windows/WindowsSupport.h"
#include <aclapi.h>
#include <io.h>
#include <vector>
#else
#include <fcntl.h>
#if defined(__APPLE__)
#include <sys/acl.h>
#endif
#include <unistd.h>
#endif

using namespace llvm;

namespace neverc::output_platform {
namespace {

#if defined(__APPLE__)
std::error_code replaceExtendedAcl(int FileDescriptor, acl_t Acl) {
  if (::acl_set_fd_np(FileDescriptor, Acl, ACL_TYPE_EXTENDED) == 0)
    return {};
  return std::error_code(errno, std::generic_category());
}

std::error_code clearExtendedAcl(int FileDescriptor) {
  acl_t EmptyAcl = ::acl_init(0);
  if (!EmptyAcl)
    return std::error_code(errno, std::generic_category());
  const std::error_code Result = replaceExtendedAcl(FileDescriptor, EmptyAcl);
  ::acl_free(EmptyAcl);
  return Result;
}
#endif

} // namespace

std::error_code syncFileDescriptor(int FileDescriptor) {
#if defined(_WIN32)
  if (_commit(FileDescriptor) == 0)
    return {};
#else
  if (::fsync(FileDescriptor) == 0)
    return {};
#endif
  return std::error_code(errno, std::generic_category());
}

std::error_code seekFileToEnd(int FileDescriptor) {
#if defined(_WIN32)
  if (::_lseeki64(FileDescriptor, 0, SEEK_END) != -1)
    return {};
#else
  if (::lseek(FileDescriptor, 0, SEEK_END) != static_cast<off_t>(-1))
    return {};
#endif
  return std::error_code(errno, std::generic_category());
}

std::error_code closeFileDescriptor(int FileDescriptor) {
#if defined(_WIN32)
  if (::_close(FileDescriptor) == 0)
    return {};
#else
  if (::close(FileDescriptor) == 0)
    return {};
#endif
  return std::error_code(errno, std::generic_category());
}

static std::error_code probeMissingPathAlias(StringRef Left, StringRef Right,
                                             bool &Result) {
  SmallString<256> Model(Left);
  Model += ".neverc-alias-%%%%%%%%";
  SmallString<256> ProbePath;
  int ProbeFile = -1;
  if (std::error_code EC =
          sys::fs::createUniqueFile(Model, ProbeFile, ProbePath))
    return EC;

  const std::error_code CloseError = closeFileDescriptor(ProbeFile);
  std::error_code CompareError;
  Result = false;
  if (!StringRef(ProbePath).starts_with(Left)) {
    CompareError = std::make_error_code(std::errc::invalid_argument);
  } else {
    SmallString<256> RightProbe(Right);
    RightProbe += StringRef(ProbePath).drop_front(Left.size());
    CompareError = sys::fs::equivalent(ProbePath, RightProbe, Result);
    if (CompareError ==
            std::make_error_code(std::errc::no_such_file_or_directory) ||
        CompareError == std::make_error_code(std::errc::not_a_directory))
      CompareError.clear();
  }
  const std::error_code CleanupError = sys::fs::remove(ProbePath);
  if (CloseError)
    return CloseError;
  if (CompareError)
    return CompareError;
  return CleanupError;
}

std::error_code pathsReferToSameLocation(StringRef Left, StringRef Right,
                                         bool &Result) {
  Result = Left == Right;
  if (Result)
    return {};

  bool Equivalent = false;
  const std::error_code EquivalentError =
      sys::fs::equivalent(Left, Right, Equivalent);
  if (!EquivalentError) {
    Result = Equivalent;
    return {};
  }
  if (EquivalentError !=
          std::make_error_code(std::errc::no_such_file_or_directory) &&
      EquivalentError != std::make_error_code(std::errc::not_a_directory))
    return EquivalentError;

  // Ask the actual filesystem: case-folding and Unicode normalization vary by
  // volume and cannot be inferred safely from path spelling alone.
  return probeMissingPathAlias(Left, Right, Result);
}

std::error_code syncParentDirectory(StringRef Path) {
#if defined(_WIN32)
  (void)Path;
  return std::make_error_code(std::errc::operation_not_supported);
#else
  const size_t Separator = Path.rfind('/');
  std::string Parent =
      Separator == StringRef::npos
          ? std::string(".")
          : (Separator == 0 ? std::string("/")
                            : Path.take_front(Separator).str());
  int Flags = O_RDONLY;
#ifdef O_DIRECTORY
  Flags |= O_DIRECTORY;
#endif
  int Directory = ::open(Parent.c_str(), Flags);
  if (Directory < 0)
    return std::error_code(errno, std::generic_category());
  int SyncResult = ::fsync(Directory);
  int SyncError = errno;
  int CloseResult = ::close(Directory);
  int CloseError = errno;
  if (SyncResult != 0)
    return std::error_code(SyncError, std::generic_category());
  if (CloseResult != 0)
    return std::error_code(CloseError, std::generic_category());
  return {};
#endif
}

std::error_code isLinkLikePath(StringRef Path, bool &Result) {
  Result = false;
#if defined(_WIN32)
  SmallVector<wchar_t, 256> WidePath;
  if (std::error_code EC = sys::windows::widenPath(Path, WidePath))
    return EC;

  const DWORD Attributes = ::GetFileAttributesW(WidePath.data());
  if (Attributes == INVALID_FILE_ATTRIBUTES)
    return mapWindowsError(::GetLastError());
  Result = (Attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  sys::fs::file_status Status;
  if (std::error_code EC = sys::fs::status(Path, Status, false))
    return EC;
  Result = sys::fs::is_symlink_file(Status);
#endif
  return {};
}

std::error_code renameLinkLikePath(StringRef From, StringRef To) {
#if defined(_WIN32)
  SmallVector<wchar_t, 256> WideFrom;
  if (std::error_code EC = sys::windows::widenPath(From, WideFrom))
    return EC;

  ScopedFileHandle Handle(::CreateFileW(
      WideFrom.data(), DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr));
  if (!Handle)
    return mapWindowsError(::GetLastError());
  return sys::fs::rename_handle(Handle, To);
#else
  return sys::fs::rename(From, To);
#endif
}

std::error_code renameStagingFile(StringRef From, int FileDescriptor,
                                  bool &DeleteDispositionActive, StringRef To) {
#if defined(_WIN32)
  (void)From;
  const HANDLE Handle =
      reinterpret_cast<HANDLE>(::_get_osfhandle(FileDescriptor));
  if (Handle == INVALID_HANDLE_VALUE)
    return mapWindowsError(ERROR_INVALID_HANDLE);

  const bool RestoreDeleteDisposition = DeleteDispositionActive;
  if (RestoreDeleteDisposition) {
    if (std::error_code EC = sys::fs::setDeleteDisposition(Handle, false))
      return EC;
    DeleteDispositionActive = false;
  }

  const std::error_code Result = sys::fs::rename_handle(Handle, To);
  if (Result && RestoreDeleteDisposition) {
    if (!sys::fs::setDeleteDisposition(Handle, true))
      DeleteDispositionActive = true;
  }
  return Result;
#else
  (void)FileDescriptor;
  (void)DeleteDispositionActive;
  return sys::fs::rename(From, To);
#endif
}

std::error_code restrictFileToOwner(StringRef Path, int FileDescriptor,
                                    bool &DeleteDispositionActive) {
#if defined(_WIN32)
  const HANDLE Handle =
      reinterpret_cast<HANDLE>(::_get_osfhandle(FileDescriptor));
  if (Handle == INVALID_HANDLE_VALUE)
    return mapWindowsError(ERROR_INVALID_HANDLE);

  const bool RestoreDeleteDisposition = DeleteDispositionActive;
  if (RestoreDeleteDisposition) {
    if (std::error_code EC = sys::fs::setDeleteDisposition(Handle, false))
      return EC;
    DeleteDispositionActive = false;
  }

  std::error_code Result;
  (void)Path;
  {
    HANDLE RawToken = nullptr;
    DWORD WindowsError =
        ::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &RawToken)
            ? ERROR_SUCCESS
            : ::GetLastError();
    ScopedJobHandle Token(RawToken);
    DWORD TokenSize = 0;
    if (WindowsError == ERROR_SUCCESS) {
      if (::GetTokenInformation(Token, TokenUser, nullptr, 0, &TokenSize)) {
        WindowsError = ERROR_INVALID_DATA;
      } else {
        const DWORD TokenError = ::GetLastError();
        if (TokenError != ERROR_INSUFFICIENT_BUFFER)
          WindowsError = TokenError;
      }
    }

    std::vector<uint8_t> TokenInformation;
    if (WindowsError == ERROR_SUCCESS) {
      TokenInformation.resize(TokenSize);
      WindowsError =
          ::GetTokenInformation(Token, TokenUser, TokenInformation.data(),
                                TokenSize, &TokenSize)
              ? ERROR_SUCCESS
              : ::GetLastError();
    }
    PSID User =
        WindowsError == ERROR_SUCCESS
            ? reinterpret_cast<TOKEN_USER *>(TokenInformation.data())->User.Sid
            : nullptr;
    if (WindowsError == ERROR_SUCCESS && (!User || !::IsValidSid(User)))
      WindowsError = ERROR_INVALID_SID;

    PACL OwnerOnlyAcl = nullptr;
    if (WindowsError == ERROR_SUCCESS) {
      EXPLICIT_ACCESSW Access{};
      Access.grfAccessPermissions = FILE_ALL_ACCESS;
      Access.grfAccessMode = SET_ACCESS;
      Access.grfInheritance = NO_INHERITANCE;
      Access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
      Access.Trustee.TrusteeType = TRUSTEE_IS_USER;
      Access.Trustee.ptstrName = static_cast<LPWSTR>(User);
      WindowsError = ::SetEntriesInAclW(1, &Access, nullptr, &OwnerOnlyAcl);
    }
    if (WindowsError == ERROR_SUCCESS)
      WindowsError = ::SetSecurityInfo(Handle, SE_FILE_OBJECT,
                                       OWNER_SECURITY_INFORMATION |
                                           DACL_SECURITY_INFORMATION |
                                           PROTECTED_DACL_SECURITY_INFORMATION,
                                       User, nullptr, OwnerOnlyAcl, nullptr);

    if (OwnerOnlyAcl)
      ::LocalFree(OwnerOnlyAcl);
    if (WindowsError != ERROR_SUCCESS)
      Result = mapWindowsError(WindowsError);
  }

  if (RestoreDeleteDisposition) {
    const std::error_code Restore = sys::fs::setDeleteDisposition(Handle, true);
    if (!Restore)
      DeleteDispositionActive = true;
    else if (!Result)
      Result = Restore;
  }
  return Result;
#else
  (void)Path;
  (void)DeleteDispositionActive;
#if defined(__APPLE__)
  if (std::error_code EC = clearExtendedAcl(FileDescriptor))
    return EC;
#endif
  return sys::fs::setPermissions(FileDescriptor,
                                 sys::fs::owner_read | sys::fs::owner_write);
#endif
}

std::error_code applyBackupFilePermissions(StringRef SourcePath,
                                           StringRef DestinationPath,
                                           int DestinationFileDescriptor,
                                           unsigned PosixPermissions) {
#if defined(_WIN32)
  SmallVector<wchar_t, 256> WideSource;
  if (std::error_code EC = sys::windows::widenPath(SourcePath, WideSource))
    return EC;
  (void)DestinationPath;

  PSID Owner = nullptr;
  PACL Dacl = nullptr;
  PSECURITY_DESCRIPTOR SecurityDescriptor = nullptr;
  DWORD WindowsError = ::GetNamedSecurityInfoW(
      WideSource.data(), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &Owner, nullptr,
      &Dacl, nullptr, &SecurityDescriptor);
  SECURITY_DESCRIPTOR_CONTROL Control = 0;
  DWORD Revision = 0;
  if (WindowsError == ERROR_SUCCESS &&
      !::GetSecurityDescriptorControl(SecurityDescriptor, &Control, &Revision))
    WindowsError = ::GetLastError();
  if (WindowsError == ERROR_SUCCESS) {
    SECURITY_INFORMATION Information =
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    Information |= (Control & SE_DACL_PROTECTED)
                       ? PROTECTED_DACL_SECURITY_INFORMATION
                       : UNPROTECTED_DACL_SECURITY_INFORMATION;
    const HANDLE DestinationHandle = reinterpret_cast<HANDLE>(
        ::_get_osfhandle(DestinationFileDescriptor));
    if (DestinationHandle == INVALID_HANDLE_VALUE)
      WindowsError = ERROR_INVALID_HANDLE;
    else
      WindowsError =
          ::SetSecurityInfo(DestinationHandle, SE_FILE_OBJECT, Information,
                            Owner, nullptr, Dacl, nullptr);
  }
  if (SecurityDescriptor)
    ::LocalFree(SecurityDescriptor);
  if (WindowsError != ERROR_SUCCESS)
    return mapWindowsError(WindowsError);
#else
  (void)DestinationPath;
#endif
  if (std::error_code EC = sys::fs::setPermissions(
          DestinationFileDescriptor,
          static_cast<sys::fs::perms>(PosixPermissions)))
    return EC;
#if defined(__APPLE__)
  const std::string Source = SourcePath.str();
  acl_t SourceAcl = ::acl_get_file(Source.c_str(), ACL_TYPE_EXTENDED);
  if (!SourceAcl) {
    const int AclError = errno;
    if (AclError == ENOENT)
      return clearExtendedAcl(DestinationFileDescriptor);
    return std::error_code(AclError, std::generic_category());
  }
  const std::error_code Result =
      replaceExtendedAcl(DestinationFileDescriptor, SourceAcl);
  ::acl_free(SourceAcl);
  return Result;
#else
  (void)SourcePath;
  return {};
#endif
}

} // namespace neverc::output_platform
