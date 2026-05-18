//===- Caching.h - LLVM Local File Cache ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CachedFileStream and the localCache function, which
// simplifies caching files on the local filesystem in a directory whose
// contents are managed by a CachePruningPolicy.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CACHING_H
#define LLVM_SUPPORT_CACHING_H

#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class MemoryBuffer;

/// This class wraps an output stream for a file. Most clients should just be
/// able to return an instance of this base class from the stream callback, but
/// if a client needs to perform some action after the stream is written to,
/// that can be done by deriving from this class and overriding the destructor.
class CachedFileStream {
public:
  CachedFileStream(std::unique_ptr<raw_pwrite_stream> OS, StringRef OSPath = "")
      : OS(std::move(OS)), ObjectPathName(OSPath) {}
  std::unique_ptr<raw_pwrite_stream> OS;
  SmallString<256> ObjectPathName;
  virtual ~CachedFileStream() = default;
};

/// This type defines the callback to add a file that is generated on the fly.
///
/// Stream callbacks must be thread safe.
using AddStreamFn = std::function<Expected<std::unique_ptr<CachedFileStream>>(
    unsigned Task, const Twine &ModuleName)>;

/// This is the type of a file cache. To request an item from the cache, pass a
/// unique string as the Key. For hits, the cached file will be added to the
/// link and this function will return AddStreamFn(). For misses, the cache will
/// return a stream callback which must be called at most once to produce
/// content for the stream. The file stream produced by the stream callback will
/// add the file to the link after the stream is written to. ModuleName is the
/// unique module identifier for the bitcode module the cache is being checked
/// for.
///
/// Clients generally look like this:
///
/// if (AddStreamFn AddStream = Cache(Task, Key, ModuleName))
///   ProduceContent(AddStream);
using FileCache = std::function<Expected<AddStreamFn>(
    unsigned Task, StringRef Key, const Twine &ModuleName)>;

/// This type defines the callback to add a pre-existing file (e.g. in a cache).
///
/// Buffer callbacks must be thread safe.
using AddBufferFn = std::function<void(unsigned Task, const Twine &ModuleName,
                                       std::unique_ptr<MemoryBuffer> MB)>;

/// Create a local file system cache which uses the given cache name, temporary
/// file prefix, cache directory and file callback.  This function does not
/// immediately create the cache directory if it does not yet exist; this is
/// done lazily the first time a file is added.  The cache name appears in error
/// messages for errors during caching. The temporary file prefix is used in the
/// temporary file naming scheme used when writing files atomically.
Expected<FileCache> localCache(
    const Twine &CacheNameRef, const Twine &TempFilePrefixRef,
    const Twine &CacheDirectoryPathRef,
    AddBufferFn AddBuffer = [](size_t Task, const Twine &ModuleName,
                               std::unique_ptr<MemoryBuffer> MB) {});
inline Expected<FileCache> localCache(const Twine &CacheNameRef,
                                      const Twine &TempFilePrefixRef,
                                      const Twine &CacheDirectoryPathRef,
                                      AddBufferFn AddBuffer) {
  SmallString<64> CacheName, TempFilePrefix, CacheDirectoryPath;
  CacheNameRef.toVector(CacheName);
  TempFilePrefixRef.toVector(TempFilePrefix);
  CacheDirectoryPathRef.toVector(CacheDirectoryPath);
  return [=](unsigned Task, StringRef Key,
             const Twine &ModuleName) -> Expected<AddStreamFn> {
    SmallString<64> EntryPath;
    sys::path::append(EntryPath, CacheDirectoryPath, "llvmcache-" + Key);
    SmallString<64> ResultPath;
    Expected<sys::fs::file_t> FDOrErr = sys::fs::openNativeFileForRead(
        Twine(EntryPath), sys::fs::OF_UpdateAtime, &ResultPath);
    std::error_code EC;
    if (FDOrErr) {
      ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
          MemoryBuffer::getOpenFile(*FDOrErr, EntryPath, -1, false);
      sys::fs::closeFile(*FDOrErr);
      if (MBOrErr) {
        AddBuffer(Task, ModuleName, std::move(*MBOrErr));
        return AddStreamFn();
      }
      EC = MBOrErr.getError();
    } else {
      EC = errorToErrorCode(FDOrErr.takeError());
    }
    if (EC != errc::no_such_file_or_directory && EC != errc::permission_denied)
      return createStringError(EC, Twine("Failed to open cache file ") +
                                       EntryPath + ": " + EC.message() + "\n");
    struct CacheStream : CachedFileStream {
      AddBufferFn AddBuffer;
      sys::fs::TempFile TempFile;
      SmallString<256> ModuleName;
      unsigned Task;
      CacheStream(std::unique_ptr<raw_pwrite_stream> OS, AddBufferFn AddBuffer,
                  sys::fs::TempFile TempFile, StringRef EntryPath,
                  StringRef ModuleName, unsigned Task)
          : CachedFileStream(std::move(OS), EntryPath),
            AddBuffer(std::move(AddBuffer)), TempFile(std::move(TempFile)),
            ModuleName(ModuleName), Task(Task) {}
      ~CacheStream() {
        OS.reset();
        ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
            MemoryBuffer::getOpenFile(
                sys::fs::convertFDToNativeFile(TempFile.FD), ObjectPathName, -1,
                false);
        if (!MBOrErr)
          report_fatal_error(Twine("Failed to open new cache file ") +
                             TempFile.TmpName + ": " +
                             MBOrErr.getError().message() + "\n");
        Error E = TempFile.keep(ObjectPathName);
        E = handleErrors(std::move(E), [&](const ECError &E) -> Error {
          int ec_val = E.convertToErrorCode();
          if (ec_val != EACCES)
            return errorCodeToError(
                std::error_code(ec_val, std::generic_category()));
          auto MBCopy = MemoryBuffer::getMemBufferCopy((*MBOrErr)->getBuffer(),
                                                       ObjectPathName);
          MBOrErr = std::move(MBCopy);
          consumeError(TempFile.discard());
          return Error::success();
        });
        if (E)
          report_fatal_error(Twine("Failed to rename temporary file ") +
                             TempFile.TmpName + " to " + ObjectPathName + ": " +
                             toString(std::move(E)) + "\n");
        AddBuffer(Task, ModuleName, std::move(*MBOrErr));
      }
    };
    return [=](size_t Task, const Twine &ModuleName)
               -> Expected<std::unique_ptr<CachedFileStream>> {
      if (std::error_code EC =
              sys::fs::create_directories(CacheDirectoryPath, true))
        return createStringError(EC, Twine("can't create cache directory ") +
                                         CacheDirectoryPath + ": " +
                                         EC.message());
      SmallString<64> TempFilenameModel;
      sys::path::append(TempFilenameModel, CacheDirectoryPath,
                        TempFilePrefix + "-%%%%%%.tmp.o");
      Expected<sys::fs::TempFile> Temp = sys::fs::TempFile::create(
          TempFilenameModel, sys::fs::owner_read | sys::fs::owner_write);
      if (!Temp)
        return createStringError(errc::io_error,
                                 toString(Temp.takeError()) + ": " + CacheName +
                                     ": Can't get a temporary file");
      return std::unique_ptr<CacheStream>(new CacheStream(
          std::unique_ptr<raw_fd_ostream>(new raw_fd_ostream(Temp->FD, false)),
          AddBuffer, std::move(*Temp), EntryPath, ModuleName.str(), Task));
    };
  };
}

} // namespace llvm

#endif
