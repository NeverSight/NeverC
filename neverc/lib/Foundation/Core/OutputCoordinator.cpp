#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <chrono>
#include <system_error>

using namespace llvm;

namespace neverc {

Expected<std::string>
OutputCoordinator::canonicalize(StringRef Path) const {
  if (Path.empty() || Path.contains('\0'))
    return createStringError(inconvertibleErrorCode(),
                             "output path is empty or contains NUL");

  SmallString<256> Absolute(Path);
  if (std::error_code Error = sys::fs::make_absolute(Absolute))
    return errorCodeToError(Error);
  (void)sys::path::remove_dots(Absolute, true);

  SmallString<256> Parent(sys::path::parent_path(Absolute));
  const std::string Filename = sys::path::filename(Absolute).str();
  SmallString<256> CanonicalParent;
  if (std::error_code Error =
          sys::fs::real_path(Parent, CanonicalParent, true))
    return errorCodeToError(Error);
  sys::path::append(CanonicalParent, Filename);
  return CanonicalParent.str().str();
}

Expected<OutputPathLease>
OutputCoordinator::acquire(StringRef Path,
                           CancellationCheck IsCancelled,
                           OutputLeaseOwner LeaseOwner) {
  auto Canonical = canonicalize(Path);
  if (!Canonical)
    return Canonical.takeError();

  std::unique_lock<std::mutex> Lock(Mutex);
  for (;;) {
    auto Active = ActivePaths.find(*Canonical);
    if (Active == ActivePaths.end())
      break;
    if (LeaseOwner && Active->second == LeaseOwner)
      return errorCodeToError(
          std::make_error_code(std::errc::file_exists));
    if (IsCancelled && IsCancelled())
      return errorCodeToError(
          std::make_error_code(std::errc::operation_canceled));
    Available.wait_for(Lock, std::chrono::milliseconds(10));
  }
  if (IsCancelled && IsCancelled())
    return errorCodeToError(
        std::make_error_code(std::errc::operation_canceled));
  ActivePaths.emplace(*Canonical, LeaseOwner);
  return OutputPathLease(*this, std::move(*Canonical));
}

void OutputCoordinator::release(StringRef CanonicalPath) {
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    ActivePaths.erase(CanonicalPath.str());
  }
  Available.notify_all();
}

} // namespace neverc
