#include "ArtifactPlatform.h"
#if defined(_WIN32)
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ConvertUTF.h"
#include <windows.h>
#elif defined(__APPLE__)
#include <errno.h>
#include <stdio.h>
#elif defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace neverc::translate {
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
