//===--- LockFileManager.h - File-level locking utility ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_LOCKFILEMANAGER_H
#define LLVM_SUPPORT_LOCKFILEMANAGER_H

#include "csupport/lprocess.h"
#include "llvm/ADT/SmallString.h"

namespace llvm {
class StringRef;

/// Class that manages the creation of a lock file to aid
/// implicit coordination between different processes.
///
/// The implicit coordination works by creating a ".lock" file alongside
/// the file that we're coordinating for, using the atomicity of the file
/// system to ensure that only a single process can create that ".lock" file.
/// When the lock file is removed, the owning process has finished the
/// operation.
class LockFileManager {
public:
  /// Describes the state of a lock file.
  enum LockFileState {
    /// The lock file has been created and is owned by this instance
    /// of the object.
    LFS_Owned,
    /// The lock file already exists and is owned by some other
    /// instance.
    LFS_Shared,
    /// An error occurred while trying to create or find the lock
    /// file.
    LFS_Error
  };

  /// Describes the result of waiting for the owner to release the lock.
  enum WaitForUnlockResult {
    /// The lock was released successfully.
    Res_Success,
    /// Owner died while holding the lock.
    Res_OwnerDied,
    /// Reached timeout while waiting for the owner to release the lock.
    Res_Timeout
  };

private:
  SmallString<128> FileName;
  SmallString<128> LockFileName;
  SmallString<128> UniqueLockFileName;

  struct OwnerInfo {
    SmallString<64> Hostname;
    int PID;
  };
  OwnerInfo Owner = {SmallString<64>(), -1}; // PID=-1 means no owner
  int ErrorCode = 0;
  SmallString<256> ErrorDiagMsg;

  LockFileManager(const LockFileManager &) = delete;
  LockFileManager &operator=(const LockFileManager &) = delete;

  /// Returns OwnerInfo with PID=-1 if lock file not found.
  static OwnerInfo readLockFile(StringRef LockFileName);

  static bool processStillExecuting(StringRef Hostname, int PID);

public:
  LockFileManager(StringRef FileName);
  ~LockFileManager();

  /// Determine the state of the lock file.
  LockFileState getState() const;

  operator LockFileState() const { return getState(); }

  /// For a shared lock, wait until the owner releases the lock.
  /// Total timeout for the file to appear is ~1.5 minutes.
  /// \param MaxSeconds the maximum total wait time in seconds.
  WaitForUnlockResult waitForUnlock(const unsigned MaxSeconds = 90);

  /// Remove the lock file.  This may delete a different lock file than
  /// the one previously read if there is a race.
  /// Returns 0 on success, errno-style value on failure.
  int unsafeRemoveLockFile();

  /// Get error message, or "" if there is no error.
  SmallString<256> getErrorMessage() const;

  /// Set error and error message (ec is an errno-style value, 0 = no error).
  void setError(int ec, StringRef ErrorMsg = "") {
    ErrorCode = ec;
    ErrorDiagMsg = ErrorMsg;
  }
};

} // end namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

#include "csupport/cpp_compat_stl.h"
#include "csupport/llock_lfile_lmanager.h"

namespace llvm {

using std::chrono::duration_cast;

/* using chrono aliases from cpp_compat_stl.h (steady_clock_t, chrono_sec) */

#ifdef _WIN32
#include <windows.h>
#endif
#if LLVM_ON_UNIX
#include <unistd.h>
#endif

#if defined(__APPLE__) &&                                                      \
    defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) &&                  \
    (__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ > 1050)
#define USE_OSX_GETHOSTUUID 1
#else
#define USE_OSX_GETHOSTUUID 0
#endif

#if USE_OSX_GETHOSTUUID
#include <uuid/uuid.h>
#endif

/// Attempt to read the lock file with the given name, if it exists.
///
/// \param LockFileName The name of the lock file to read.
///
/// \returns The process ID of the process that owns this lock file
inline LockFileManager::OwnerInfo
LockFileManager::readLockFile(StringRef LockFileName) {
  ErrorOr<uptr_t<MemoryBuffer>> MBOrErr = MemoryBuffer::getFile(LockFileName);
  if (!MBOrErr) {
    sys::fs::remove(LockFileName);
    return {SmallString<64>(), -1};
  }
  MemoryBuffer &MB = *MBOrErr.get();

  auto hp_split = getToken(MB.getBuffer(), " ");
  StringRef Hostname = hp_split.first;
  StringRef PIDStr = hp_split.second;
  PIDStr = PIDStr.substr(PIDStr.find_first_not_of(" "));
  int PID;
  if (!PIDStr.getAsInteger(10, PID)) {
    OwnerInfo Owner = {SmallString<64>(Hostname), PID};
    if (processStillExecuting(Owner.Hostname, Owner.PID))
      return Owner;
  }

  sys::fs::remove(LockFileName);
  return {SmallString<64>(), -1};
}

inline static int getHostID(SmallVectorImpl<char> &HostID) {
  HostID.clear();
  char buf[256];
  size_t len = 0;
  int err = csupport_get_host_id(buf, sizeof(buf), &len);
  if (err)
    return err;
  HostID.append(buf, buf + len);
  return 0;
}

inline bool LockFileManager::processStillExecuting(StringRef HostID, int PID) {
#if LLVM_ON_UNIX && !defined(__ANDROID__)
  SmallString<256> StoredHostID;
  if (getHostID(StoredHostID))
    return true; // Conservatively assume it's executing on error.

  // Check whether the process is dead. If so, we're done.
  if (StoredHostID == HostID && getsid(PID) == -1 && errno == ESRCH)
    return false;
#endif

  return true;
}

namespace {

/// An RAII helper object ensure that the unique lock file is removed.
///
/// Ensures that if there is an error or a signal before we finish acquiring the
/// lock, the unique file will be removed. And if we successfully take the lock,
/// the signal handler is left in place so that signals while the lock is held
/// will remove the unique lock file. The caller should ensure there is a
/// matching call to sys::DontRemoveFileOnSignal when the lock is released.
class RemoveUniqueLockFileOnSignal {
  StringRef Filename;
  bool RemoveImmediately;

public:
  RemoveUniqueLockFileOnSignal(StringRef Name)
      : Filename(Name), RemoveImmediately(true) {
    sys::RemoveFileOnSignal(Filename, 0);
  }

  ~RemoveUniqueLockFileOnSignal() {
    if (!RemoveImmediately) {
      // Leave the signal handler enabled. It will be removed when the lock is
      // released.
      return;
    }
    sys::fs::remove(Filename);
    sys::DontRemoveFileOnSignal(Filename);
  }

  void lockAcquired() { RemoveImmediately = false; }
};

} // end anonymous namespace

inline LockFileManager::LockFileManager(StringRef FileName) {
  this->FileName = FileName;
  if (auto EC = sys::fs::make_absolute(this->FileName)) {
    SmallString<256> S("failed to obtain absolute path for ");
    S.append(this->FileName);
    setError(EC.value(), SmallString<256>(S.str()));
    return;
  }
  LockFileName = this->FileName;
  LockFileName += ".lock";

  Owner = readLockFile(LockFileName);
  if (Owner.PID != -1)
    return;

  UniqueLockFileName = LockFileName;
  UniqueLockFileName += "-%%%%%%%%";
  int UniqueLockFileID;
  if (auto EC = sys::fs::createUniqueFile(UniqueLockFileName, UniqueLockFileID,
                                          UniqueLockFileName)) {
    SmallString<256> S("failed to create unique file ");
    S += UniqueLockFileName;
    setError(EC.value(), S.str());
    return;
  }

  {
    SmallString<256> HostID;
    if (int ec = getHostID(HostID)) {
      setError(ec, "failed to get host id");
      return;
    }

    raw_fd_ostream Out(UniqueLockFileID, /*shouldClose=*/true);
    Out << HostID << ' ' << csupport_get_process_id();
    Out.close();

    if (Out.has_error()) {
      SmallString<256> S("failed to write to ");
      S += UniqueLockFileName;
      setError(Out.error().value(), S.str());
      sys::fs::remove(UniqueLockFileName);
      return;
    }
  }

  RemoveUniqueLockFileOnSignal RemoveUniqueFile(UniqueLockFileName);

  while (true) {
    auto EC = sys::fs::create_link(UniqueLockFileName, LockFileName);
    if (!EC) {
      RemoveUniqueFile.lockAcquired();
      return;
    }

    if (EC != errc::file_exists) {
      SmallString<256> S;
      raw_svector_ostream OSS(S);
      OSS << "failed to create link " << LockFileName.str() << " to "
          << UniqueLockFileName.str();
      setError(EC.value(), OSS.str());
      return;
    }

    Owner = readLockFile(LockFileName);
    if (Owner.PID != -1) {
      sys::fs::remove(UniqueLockFileName);
      return;
    }

    if (!sys::fs::exists(LockFileName))
      continue;

    if ((EC = sys::fs::remove(LockFileName))) {
      SmallString<256> S("failed to remove lockfile ");
      S += UniqueLockFileName;
      setError(EC.value(), S.str());
      return;
    }
  }
}

inline LockFileManager::LockFileState LockFileManager::getState() const {
  if (Owner.PID != -1)
    return LFS_Shared;

  if (ErrorCode)
    return LFS_Error;

  return LFS_Owned;
}

inline SmallString<256> LockFileManager::getErrorMessage() const {
  if (ErrorCode) {
    SmallString<256> Str(ErrorDiagMsg);
    const char *ECMsg = strerror(ErrorCode);
    if (ECMsg && ECMsg[0]) {
      Str += ": ";
      Str += ECMsg;
    }
    return Str;
  }
  return SmallString<256>();
}

inline LockFileManager::~LockFileManager() {
  if (getState() != LFS_Owned)
    return;

  // Since we own the lock, remove the lock file and our own unique lock file.
  sys::fs::remove(LockFileName);
  sys::fs::remove(UniqueLockFileName);
  // The unique file is now gone, so remove it from the signal handler. This
  // matches a sys::RemoveFileOnSignal() in LockFileManager().
  sys::DontRemoveFileOnSignal(UniqueLockFileName);
}

inline LockFileManager::WaitForUnlockResult
LockFileManager::waitForUnlock(const unsigned MaxSeconds) {
  if (getState() != LFS_Shared)
    return Res_Success;

  // Since we don't yet have an event-based method to wait for the lock file,
  // implement randomized exponential backoff, similar to Ethernet collision
  // algorithm. This improves performance on machines with high core counts
  // when the file lock is heavily contended by multiple NeverC processes
  const unsigned long MinWaitDurationMS = 10;
  const unsigned long MaxWaitMultiplier = 50; // 500ms max wait
  unsigned long WaitMultiplier = 1;
  unsigned long ElapsedTimeSeconds = 0;

  (void)0; /* random via arc4random_uniform */

  auto StartTime = steady_clock_t::now();

  do {
    // FIXME: implement event-based waiting

    // Sleep for the designated interval, to allow the owning process time to
    // finish up and remove the lock file.
    unsigned long WaitDurationMS =
        MinWaitDurationMS * (1 + (arc4random() % WaitMultiplier));
    usleep(WaitDurationMS * 1000);

    if (sys::fs::access(LockFileName.c_str(), sys::fs::AccessMode::Exist) ==
        errc::no_such_file_or_directory) {
      // If the original file wasn't created, somone thought the lock was dead.
      if (!sys::fs::exists(FileName))
        return Res_OwnerDied;
      return Res_Success;
    }

    // If the process owning the lock died without cleaning up, just bail out.
    if (!processStillExecuting(Owner.Hostname, Owner.PID))
      return Res_OwnerDied;

    WaitMultiplier *= 2;
    if (WaitMultiplier > MaxWaitMultiplier) {
      WaitMultiplier = MaxWaitMultiplier;
    }

    ElapsedTimeSeconds =
        duration_cast<chrono_sec>(steady_clock_t::now() - StartTime).count();

  } while (ElapsedTimeSeconds < MaxSeconds);

  // Give up.
  return Res_Timeout;
}

inline int LockFileManager::unsafeRemoveLockFile() {
  return sys::fs::remove(LockFileName).value();
}

} // namespace llvm

#endif // LLVM_SUPPORT_LOCKFILEMANAGER_H
