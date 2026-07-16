#include "OutputPlatform.h"
#include <cerrno>
#include <string>

#if defined(_WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace llvm;

namespace neverc::output_platform {

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

} // namespace neverc::output_platform
