//=- CachePruning.h - Helper to manage the pruning of a cache dir -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements pruning of a directory intended for cache storage, using
// various policies.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CACHEPRUNING_H
#define LLVM_SUPPORT_CACHEPRUNING_H

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/WithColor.h"
#include <algorithm>
#include <chrono>
#include <optional>
#include <system_error>

namespace llvm {

template <typename T> class Expected;
class StringRef;

/// Policy for the pruneCache() function. A default constructed
/// CachePruningPolicy provides a reasonable default policy.
struct CachePruningPolicy {
  /// The pruning interval. This is intended to be used to avoid scanning the
  /// directory too often. It does not impact the decision of which file to
  /// prune. A value of 0 forces the scan to occur. A value of std::nullopt
  /// disables pruning.
  std::optional<std::chrono::seconds> Interval = std::chrono::seconds(1200);

  /// The expiration for a file. When a file hasn't been accessed for Expiration
  /// seconds, it is removed from the cache. A value of 0 disables the
  /// expiration-based pruning.
  std::chrono::seconds Expiration = std::chrono::hours(7 * 24); // 1w

  /// The maximum size for the cache directory, in terms of percentage of the
  /// available space on the disk. Set to 100 to indicate no limit, 50 to
  /// indicate that the cache size will not be left over half the available disk
  /// space. A value over 100 will be reduced to 100. A value of 0 disables the
  /// percentage size-based pruning.
  unsigned MaxSizePercentageOfAvailableSpace = 75;

  /// The maximum size for the cache directory in bytes. A value over the amount
  /// of available space on the disk will be reduced to the amount of available
  /// space. A value of 0 disables the absolute size-based pruning.
  uint64_t MaxSizeBytes = 0;

  /// The maximum number of files in the cache directory. A value of 0 disables
  /// the number of files based pruning.
  ///
  /// This defaults to 1000000 because with that many files there are
  /// diminishing returns on the effectiveness of the cache. Some systems have a
  /// limit on total number of files, and some also limit the number of files
  /// per directory, such as Linux ext4, with the default setting (block size is
  /// 4096 and large_dir disabled), there is a per-directory entry limit of
  /// 508*510*floor(4096/(40+8))~=20M for average filename length of 40.
  uint64_t MaxSizeFiles = 1000000;
};

/// Parse the given string as a cache pruning policy. Defaults are taken from a
/// default constructed CachePruningPolicy object.
/// For example: "prune_interval=30s:prune_after=24h:cache_size=50%"
/// which means a pruning interval of 30 seconds, expiration time of 24 hours
/// and maximum cache size of 50% of available disk space.
Expected<CachePruningPolicy> parseCachePruningPolicy(StringRef PolicyStr);

/// Peform pruning using the supplied policy, returns true if pruning
/// occurred, i.e. if Policy.Interval was expired.
///
/// Check whether cache pruning happens using the supplied policy, adds a
/// LTO warning if cache_size_bytes or cache_size_files is too small for the
/// current link job. The warning recommends the user to consider adjusting
/// --lto-cache-policy.
///
/// As a safeguard against data loss if the user specifies the wrong directory
/// as their cache directory, this function will ignore files not matching the
/// pattern "llvmcache-*".
bool pruneCache(StringRef Path, CachePruningPolicy Policy,
                ArrayRef<std::unique_ptr<MemoryBuffer>> Files = {});
} // namespace llvm

/*== Inline implementations (moved from cpp_bridge.cpp) ==*/

#include "csupport/cpp_compat_stl.h"
#include "csupport/lcache_lpruning.h"

#undef DEBUG_TYPE
#define DEBUG_TYPE "cache-pruning"

namespace {
struct FileInfo {
  llvm::sys::TimePoint<> Time;
  uint64_t Size;
  llvm::SmallString<256> Path;

  bool operator<(const FileInfo &Other) const {
    if (Time != Other.Time)
      return Time < Other.Time;
    if (Other.Size != Size)
      return Other.Size < Size;
    return Path < Other.Path;
  }
};
} // anonymous namespace

inline static void writeTimestampFile(llvm::StringRef TimestampFile) {
  errc_t EC;
  llvm::raw_fd_ostream Out(TimestampFile.str(), EC, llvm::sys::fs::OF_None);
}

using CacheSec = chrono_sec;
using CacheMin = chrono_min;
using CacheHrs = chrono_hrs;

inline static llvm::Expected<CacheSec> parseDuration(llvm::StringRef Duration) {
  int64_t secs =
      csupport_parse_duration_seconds(Duration.data(), Duration.size());
  if (secs < 0)
    return llvm::make_error<llvm::StringError>(
        "'" + Duration + "' is not a valid duration (use Ns, Nm, or Nh)",
        llvm::inconvertibleErrorCode());
  return CacheSec(secs);
}

namespace llvm {

inline Expected<CachePruningPolicy>
parseCachePruningPolicy(StringRef PolicyStr) {
  CachePruningPolicy Policy;
  auto P = PolicyStr.split(':');
  P.first = StringRef();
  P.second = PolicyStr;
  while (!P.second.empty()) {
    P = P.second.split(':');

    auto kv_split = P.first.split('=');
    StringRef Key = kv_split.first, Value = kv_split.second;
    if (Key == "prune_interval") {
      auto DurationOrErr = parseDuration(Value);
      if (!DurationOrErr)
        return DurationOrErr.takeError();
      Policy.Interval = *DurationOrErr;
    } else if (Key == "prune_after") {
      auto DurationOrErr = parseDuration(Value);
      if (!DurationOrErr)
        return DurationOrErr.takeError();
      Policy.Expiration = *DurationOrErr;
    } else if (Key == "cache_size") {
      if (Value.back() != '%')
        return make_error<StringError>("'" + Value + "' must be a percentage",
                                       inconvertibleErrorCode());
      StringRef SizeStr = Value.drop_back();
      uint64_t Size;
      if (SizeStr.getAsInteger(0, Size))
        return make_error<StringError>("'" + SizeStr + "' not an integer",
                                       inconvertibleErrorCode());
      if (Size > 100)
        return make_error<StringError>("'" + SizeStr +
                                           "' must be between 0 and 100",
                                       inconvertibleErrorCode());
      Policy.MaxSizePercentageOfAvailableSpace = Size;
    } else if (Key == "cache_size_bytes") {
      uint64_t Mult = 1;
      switch (tolower(Value.back())) {
      case 'k':
        Mult = 1024;
        Value = Value.drop_back();
        break;
      case 'm':
        Mult = 1024 * 1024;
        Value = Value.drop_back();
        break;
      case 'g':
        Mult = 1024 * 1024 * 1024;
        Value = Value.drop_back();
        break;
      }
      uint64_t Size;
      if (Value.getAsInteger(0, Size))
        return make_error<StringError>("'" + Value + "' not an integer",
                                       inconvertibleErrorCode());
      Policy.MaxSizeBytes = Size * Mult;
    } else if (Key == "cache_size_files") {
      if (Value.getAsInteger(0, Policy.MaxSizeFiles))
        return make_error<StringError>("'" + Value + "' not an integer",
                                       inconvertibleErrorCode());
    } else {
      return make_error<StringError>("Unknown key: '" + Key + "'",
                                     inconvertibleErrorCode());
    }
  }

  return Policy;
}

inline bool pruneCache(StringRef Path, CachePruningPolicy Policy,
                       ArrayRef<uptr_t<MemoryBuffer>> Files) {
  using chrono::duration_cast;
  using SysClock = system_clock_t;

  if (Path.empty())
    return false;

  bool isPathDir;
  if (sys::fs::is_directory(Path, isPathDir))
    return false;

  if (!isPathDir)
    return false;

  Policy.MaxSizePercentageOfAvailableSpace =
      std::min(Policy.MaxSizePercentageOfAvailableSpace, 100u);

  using sec = CacheSec;
  if (Policy.Expiration == sec(0) &&
      Policy.MaxSizePercentageOfAvailableSpace == 0 &&
      Policy.MaxSizeBytes == 0 && Policy.MaxSizeFiles == 0) {
    LLVM_DEBUG(dbgs() << "No pruning settings set, exit early\n");
    return false;
  }

  SmallString<128> TimestampFile(Path);
  sys::path::append(TimestampFile, "llvmcache.timestamp");
  sys::fs::file_status FileStatus;
  const auto CurrentTime = SysClock::now();
  if (auto EC = sys::fs::status(TimestampFile, FileStatus)) {
    if (EC == std::make_error_code(std::errc::no_such_file_or_directory)) {
      writeTimestampFile(TimestampFile);
    } else {
      return false;
    }
  } else {
    if (!Policy.Interval)
      return false;
    if (Policy.Interval != sec(0)) {
      const auto TimeStampModTime = FileStatus.getLastModificationTime();
      auto TimeStampAge = CurrentTime - TimeStampModTime;
      if (TimeStampAge <= *Policy.Interval) {
        LLVM_DEBUG(dbgs() << "Timestamp file too recent ("
                          << duration_cast<sec>(TimeStampAge).count()
                          << "s old), do not prune.\n");
        return false;
      }
    }
    writeTimestampFile(TimestampFile);
  }

  SmallVector<FileInfo, 32> FileInfos;
  uint64_t TotalSize = 0;

  errc_t EC;
  SmallString<128> CachePathNative;
  sys::path::native(Path, CachePathNative);
  for (sys::fs::directory_iterator File(CachePathNative, EC), FileEnd;
       File != FileEnd && !EC; File.increment(EC)) {
    StringRef filename = sys::path::filename(File->path());
    if (!filename.starts_with("llvmcache-") && !filename.starts_with("Thin-"))
      continue;

    ErrorOr<sys::fs::basic_file_status> StatusOrErr = File->status();
    if (!StatusOrErr) {
      LLVM_DEBUG(dbgs() << "Ignore " << File->path() << " (can't stat)\n");
      continue;
    }

    const auto FileAccessTime = StatusOrErr->getLastAccessedTime();
    auto FileAge = CurrentTime - FileAccessTime;
    if (Policy.Expiration != sec(0) && FileAge > Policy.Expiration) {
      LLVM_DEBUG(dbgs() << "Remove " << File->path() << " ("
                        << duration_cast<sec>(FileAge).count() << "s old)\n");
      sys::fs::remove(File->path());
      continue;
    }

    TotalSize += StatusOrErr->getSize();
    FileInfos.push_back({FileAccessTime, StatusOrErr->getSize(),
                         SmallString<256>(File->path())});
  }

  llvm::sort(FileInfos);
  auto *FileInfoIt = FileInfos.begin();
  size_t NumFiles = FileInfos.size();

  auto RemoveCacheFile = [&]() {
    sys::fs::remove(FileInfoIt->Path);
    TotalSize -= FileInfoIt->Size;
    NumFiles--;
    LLVM_DEBUG(dbgs() << " - Remove " << FileInfoIt->Path << " (size "
                      << FileInfoIt->Size << "), new occupancy is " << TotalSize
                      << "%\n");
    ++FileInfoIt;
  };

  const size_t ActualNums = Files.size();
  if (Policy.MaxSizeFiles && ActualNums > Policy.MaxSizeFiles)
    WithColor::warning()
        << "LTO cache pruning happens since the number of created files ("
        << ActualNums << ") exceeds the maximum number of files ("
        << Policy.MaxSizeFiles << "); consider adjusting --lto-cache-policy\n";

  if (Policy.MaxSizeFiles)
    while (NumFiles > Policy.MaxSizeFiles)
      RemoveCacheFile();

  if (Policy.MaxSizePercentageOfAvailableSpace > 0 || Policy.MaxSizeBytes > 0) {
    auto ErrOrSpaceInfo = sys::fs::disk_space(Path);
    if (!ErrOrSpaceInfo) {
      report_fatal_error("Can't get available size");
    }
    sys::fs::space_info SpaceInfo = ErrOrSpaceInfo.get();
    auto AvailableSpace = TotalSize + SpaceInfo.free;

    if (Policy.MaxSizePercentageOfAvailableSpace == 0)
      Policy.MaxSizePercentageOfAvailableSpace = 100;
    if (Policy.MaxSizeBytes == 0)
      Policy.MaxSizeBytes = AvailableSpace;
    auto TotalSizeTarget = std::min(
        AvailableSpace * Policy.MaxSizePercentageOfAvailableSpace / 100ull,
        Policy.MaxSizeBytes);

    LLVM_DEBUG(dbgs() << "Occupancy: " << ((100 * TotalSize) / AvailableSpace)
                      << "% target is: "
                      << Policy.MaxSizePercentageOfAvailableSpace << "%, "
                      << Policy.MaxSizeBytes << " bytes\n");

    size_t ActualSizes = 0;
    for (const auto &File : Files)
      if (File)
        ActualSizes += File->getBufferSize();

    if (ActualSizes > TotalSizeTarget)
      WithColor::warning()
          << "LTO cache pruning happens since the total size of the cache "
             "files consumed by the current link job ("
          << ActualSizes << "  bytes) exceeds maximum cache size ("
          << TotalSizeTarget
          << " bytes); consider adjusting --lto-cache-policy\n";

    while (TotalSize > TotalSizeTarget && FileInfoIt != FileInfos.end())
      RemoveCacheFile();
  }
  return true;
}

} // namespace llvm

#undef DEBUG_TYPE

#endif
