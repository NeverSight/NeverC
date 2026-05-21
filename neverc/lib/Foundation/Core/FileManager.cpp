#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/MakeSupport.h"
#include "neverc/Foundation/Core/PrettyStackTrace.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/SourceManagerInternals.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Capacity.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef __AVX512BW__
#include <immintrin.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

using namespace neverc;

#define DEBUG_TYPE "file-search"

ALWAYS_ENABLED_STATISTIC(NumDirLookups, "Number of directory lookups.");
ALWAYS_ENABLED_STATISTIC(NumFileLookups, "Number of file lookups.");
ALWAYS_ENABLED_STATISTIC(NumDirCacheMisses,
                         "Number of directory cache misses.");
ALWAYS_ENABLED_STATISTIC(NumFileCacheMisses, "Number of file cache misses.");

// ===----------------------------------------------------------------------===
// FileManager — directory & file lookup
// ===----------------------------------------------------------------------===

FileManager::FileManager(const FileSystemOptions &FSO,
                         llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
    : FS(std::move(FS)), FileSystemOpts(FSO), SeenDirEntries(256),
      SeenFileEntries(256), NextFileUID(0) {
  // If the caller doesn't provide a virtual file system, just grab the real
  // file system.
  if (!this->FS)
    this->FS = llvm::vfs::getRealFileSystem();
}

FileManager::~FileManager() = default;

namespace {
llvm::Expected<DirectoryEntryRef> getDirectoryFromFile(FileManager &FileMgr,
                                                       llvm::StringRef Filename,
                                                       bool CacheFailure) {
  if (Filename.empty())
    return llvm::errorCodeToError(
        make_error_code(std::errc::no_such_file_or_directory));

  if (llvm::sys::path::is_separator(Filename[Filename.size() - 1]))
    return llvm::errorCodeToError(make_error_code(std::errc::is_a_directory));

  llvm::StringRef DirName = llvm::sys::path::parent_path(Filename);
  // Use the current directory if file has no path component.
  if (DirName.empty())
    DirName = ".";

  return FileMgr.getDirectoryRef(DirName, CacheFailure);
}
} // namespace

void FileManager::addAncestorsAsVirtualDirs(llvm::StringRef Path) {
  llvm::StringRef DirName = llvm::sys::path::parent_path(Path);
  if (DirName.empty())
    DirName = ".";

  auto &NamedDirEnt =
      *SeenDirEntries.insert({DirName, std::errc::no_such_file_or_directory})
           .first;

  // When caching a virtual directory, we always cache its ancestors
  // at the same time.  Therefore, if DirName is already in the cache,
  // we don't need to recurse as its ancestors must also already be in
  // the cache (or it's a known non-virtual directory).
  if (NamedDirEnt.second)
    return;

  // Add the virtual directory to the cache.
  auto *UDE = new (DirsAlloc.Allocate()) DirectoryEntry();
  UDE->Name = NamedDirEnt.first();
  NamedDirEnt.second = *UDE;
  VirtualDirectoryEntries.push_back(UDE);

  // Recursively add the other ancestors.
  addAncestorsAsVirtualDirs(DirName);
}

llvm::Expected<DirectoryEntryRef>
FileManager::getDirectoryRef(llvm::StringRef DirName, bool CacheFailure) {
  // stat doesn't like trailing separators except for root directory.
  // At least, on Win32 MSVCRT, stat() cannot strip trailing '/'.
  // (though it can strip '\\')
  if (DirName.size() > 1 && DirName != llvm::sys::path::root_path(DirName) &&
      llvm::sys::path::is_separator(DirName.back()))
    DirName = DirName.substr(0, DirName.size() - 1);
  std::optional<std::string> DirNameStr;
  if (is_style_windows(llvm::sys::path::Style::native)) {
    // Fixing a problem with "neverc C:test.c" on Windows.
    // Stat("C:") does not recognize "C:" as a valid directory
    if (DirName.size() > 1 && DirName.back() == ':' &&
        DirName.equals_insensitive(llvm::sys::path::root_name(DirName))) {
      DirNameStr = DirName.str() + '.';
      DirName = *DirNameStr;
    }
  }

  ++NumDirLookups;

  // See if there was already an entry in the map.  Note that the map
  // contains both virtual and real directories.
  auto SeenDirInsertResult =
      SeenDirEntries.insert({DirName, std::errc::no_such_file_or_directory});
  if (!SeenDirInsertResult.second) {
    if (SeenDirInsertResult.first->second)
      return DirectoryEntryRef(*SeenDirInsertResult.first);
    return llvm::errorCodeToError(SeenDirInsertResult.first->second.getError());
  }

  // We've not seen this before. Fill it in.
  ++NumDirCacheMisses;
  auto &NamedDirEnt = *SeenDirInsertResult.first;
  assert(!NamedDirEnt.second && "should be newly-created");

  llvm::StringRef InterndDirName = NamedDirEnt.first();

  llvm::vfs::Status Status;
  auto statError =
      getStatValue(InterndDirName, Status, false, nullptr /*directory lookup*/);
  if (statError) {
    // There's no real directory at the given path.
    if (CacheFailure)
      NamedDirEnt.second = statError;
    else
      SeenDirEntries.erase(DirName);
    return llvm::errorCodeToError(statError);
  }

  // It exists.  See if we have already opened a directory with the
  // same inode (this occurs on Unix-like systems when one dir is
  // symlinked to another, for example) or the same path (on
  // Windows).
  DirectoryEntry *&UDE = UniqueRealDirs[Status.getUniqueID()];

  if (!UDE) {
    // We don't have this directory yet, add it.  We use the string
    // key from the SeenDirEntries map as the string.
    UDE = new (DirsAlloc.Allocate()) DirectoryEntry();
    UDE->Name = InterndDirName;
  }
  NamedDirEnt.second = *UDE;

  return DirectoryEntryRef(NamedDirEnt);
}

llvm::ErrorOr<const DirectoryEntry *>
FileManager::getDirectory(llvm::StringRef DirName, bool CacheFailure) {
  auto Result = getDirectoryRef(DirName, CacheFailure);
  if (Result)
    return &Result->getDirEntry();
  return llvm::errorToErrorCode(Result.takeError());
}

llvm::ErrorOr<const FileEntry *> FileManager::getFile(llvm::StringRef Filename,
                                                      bool openFile,
                                                      bool CacheFailure) {
  auto Result = getFileRef(Filename, openFile, CacheFailure);
  if (Result)
    return &Result->getFileEntry();
  return llvm::errorToErrorCode(Result.takeError());
}

llvm::Expected<FileEntryRef> FileManager::getFileRef(llvm::StringRef Filename,
                                                     bool openFile,
                                                     bool CacheFailure) {
  ++NumFileLookups;

  // See if there is already an entry in the map.
  auto SeenFileInsertResult =
      SeenFileEntries.insert({Filename, std::errc::no_such_file_or_directory});
  if (!SeenFileInsertResult.second) {
    if (!SeenFileInsertResult.first->second)
      return llvm::errorCodeToError(
          SeenFileInsertResult.first->second.getError());
    return FileEntryRef(*SeenFileInsertResult.first);
  }

  // We've not seen this before. Fill it in.
  ++NumFileCacheMisses;
  auto *NamedFileEnt = &*SeenFileInsertResult.first;
  assert(!NamedFileEnt->second && "should be newly-created");

  llvm::StringRef InterndFileName = NamedFileEnt->first();

  // Look up the directory for the file.  When looking up something like
  // sys/foo.h we'll discover all of the search directories that have a 'sys'
  // subdirectory.  This will let us avoid having to waste time on known-to-fail
  // searches when we go to find sys/bar.h, because all the search directories
  // without a 'sys' subdir will get a cached failure result.
  auto DirInfoOrErr = getDirectoryFromFile(*this, Filename, CacheFailure);
  if (!DirInfoOrErr) { // Directory doesn't exist, file can't exist.
    std::error_code Err = errorToErrorCode(DirInfoOrErr.takeError());
    if (CacheFailure)
      NamedFileEnt->second = Err;
    else
      SeenFileEntries.erase(Filename);

    return llvm::errorCodeToError(Err);
  }
  DirectoryEntryRef DirInfo = *DirInfoOrErr;

  std::unique_ptr<llvm::vfs::File> F;
  llvm::vfs::Status Status;
  auto statError =
      getStatValue(InterndFileName, Status, true, openFile ? &F : nullptr);
  if (statError) {
    // There's no real file at the given path.
    if (CacheFailure)
      NamedFileEnt->second = statError;
    else
      SeenFileEntries.erase(Filename);

    return llvm::errorCodeToError(statError);
  }

  assert((openFile || !F) && "undesired open file");

  // It exists.  See if we have already opened a file with the same inode.
  // This occurs when one dir is symlinked to another, for example.
  FileEntry *&UFE = UniqueRealFiles[Status.getUniqueID()];
  bool ReusingEntry = UFE != nullptr;
  if (!UFE)
    UFE = new (FilesAlloc.Allocate()) FileEntry();

  if (!Status.ExposesExternalVFSPath || Status.getName() == Filename) {
    // Use the requested name. Set the FileEntry.
    NamedFileEnt->second = FileEntryRef::MapValue(*UFE, DirInfo);
  } else {
    // Name mismatch. We need a redirect. First grab the actual entry we want
    // to return.
    //
    // This redirection logic intentionally leaks the external name of a
    // redirected file that uses 'use-external-name' in \a
    // vfs::RedirectionFileSystem. This allows NeverC to report the external
    // name to users (in diagnostics) and to tools that don't have access to
    // the VFS (in debug info and dependency '.d' files).
    //
    auto &Redirection =
        *SeenFileEntries
             .insert({Status.getName(), FileEntryRef::MapValue(*UFE, DirInfo)})
             .first;
    assert(Redirection.second->V.is<FileEntry *>() &&
           "filename redirected to a non-canonical filename?");
    assert(Redirection.second->V.get<FileEntry *>() == UFE &&
           "filename from getStatValue() refers to wrong file");

    // Cache the redirection in the previously-inserted entry, still available
    // in the tentative return value.
    NamedFileEnt->second = FileEntryRef::MapValue(Redirection, DirInfo);
  }

  FileEntryRef ReturnedRef(*NamedFileEnt);
  if (ReusingEntry) { // Already have an entry with this inode, return it.

    if (&DirInfo.getDirEntry() != UFE->Dir && Status.IsVFSMapped)
      UFE->Dir = &DirInfo.getDirEntry();

    UFE->LastRef = ReturnedRef;

    return ReturnedRef;
  }

  // Otherwise, we don't have this file yet, add it.
  UFE->LastRef = ReturnedRef;
  UFE->Size = Status.getSize();
  UFE->ModTime = llvm::sys::toTimeT(Status.getLastModificationTime());
  UFE->Dir = &DirInfo.getDirEntry();
  UFE->UID = NextFileUID++;
  UFE->UniqueID = Status.getUniqueID();
  UFE->IsNamedPipe = Status.getType() == llvm::sys::fs::file_type::fifo_file;
  UFE->File = std::move(F);

  if (UFE->File) {
    if (auto PathName = UFE->File->getName())
      fillRealPathName(UFE, *PathName);
  } else if (!openFile) {
    // We should still fill the path even if we aren't opening the file.
    fillRealPathName(UFE, InterndFileName);
  }
  return ReturnedRef;
}

llvm::Expected<FileEntryRef> FileManager::getSTDIN() {
  // Only read stdin once.
  if (STDIN)
    return *STDIN;

  std::unique_ptr<llvm::MemoryBuffer> Content;
  if (auto ContentOrError = llvm::MemoryBuffer::getSTDIN())
    Content = std::move(*ContentOrError);
  else
    return llvm::errorCodeToError(ContentOrError.getError());

  STDIN = getVirtualFileRef(Content->getBufferIdentifier(),
                            Content->getBufferSize(), 0);
  FileEntry &FE = const_cast<FileEntry &>(STDIN->getFileEntry());
  FE.Content = std::move(Content);
  FE.IsNamedPipe = true;
  return *STDIN;
}

const FileEntry *FileManager::getVirtualFile(llvm::StringRef Filename,
                                             off_t Size,
                                             time_t ModificationTime) {
  return &getVirtualFileRef(Filename, Size, ModificationTime).getFileEntry();
}

FileEntryRef FileManager::getVirtualFileRef(llvm::StringRef Filename,
                                            off_t Size,
                                            time_t ModificationTime) {
  ++NumFileLookups;

  // See if there is already an entry in the map for an existing file.
  auto &NamedFileEnt =
      *SeenFileEntries.insert({Filename, std::errc::no_such_file_or_directory})
           .first;
  if (NamedFileEnt.second) {
    FileEntryRef::MapValue Value = *NamedFileEnt.second;
    if (LLVM_LIKELY(Value.V.is<FileEntry *>()))
      return FileEntryRef(NamedFileEnt);
    return FileEntryRef(*Value.V.get<const FileEntryRef::MapEntry *>());
  }

  // We've not seen this before, or the file is cached as non-existent.
  ++NumFileCacheMisses;
  addAncestorsAsVirtualDirs(Filename);
  FileEntry *UFE = nullptr;

  // Now that all ancestors of Filename are in the cache, the
  // following call is guaranteed to find the DirectoryEntry from the
  // cache. A virtual file can also have an empty filename, that could come
  // from a source location preprocessor directive with an empty filename as
  // an example, so we need to pretend it has a name to ensure a valid directory
  // entry can be returned.
  auto DirInfo = expectedToOptional(getDirectoryFromFile(
      *this, Filename.empty() ? "." : Filename, /*CacheFailure=*/true));
  assert(DirInfo &&
         "The directory of a virtual file should already be in the cache.");

  // If the file exists on disk, prefer the real entry over the virtual one.
  llvm::vfs::Status Status;
  const char *InterndFileName = NamedFileEnt.first().data();
  if (!getStatValue(InterndFileName, Status, true, nullptr)) {
    Status = llvm::vfs::Status(Status.getName(), Status.getUniqueID(),
                               llvm::sys::toTimePoint(ModificationTime),
                               Status.getUser(), Status.getGroup(), Size,
                               Status.getType(), Status.getPermissions());

    auto &RealFE = UniqueRealFiles[Status.getUniqueID()];
    if (RealFE) {
      // If we had already opened this file, close it now so we don't
      // leak the descriptor. We're not going to use the file
      // descriptor anyway, since this is a virtual file.
      if (RealFE->File)
        RealFE->closeFile();
      NamedFileEnt.second = FileEntryRef::MapValue(*RealFE, *DirInfo);
      return FileEntryRef(NamedFileEnt);
    }
    // File exists, but no entry - create it.
    RealFE = new (FilesAlloc.Allocate()) FileEntry();
    RealFE->UniqueID = Status.getUniqueID();
    RealFE->IsNamedPipe =
        Status.getType() == llvm::sys::fs::file_type::fifo_file;
    fillRealPathName(RealFE, Status.getName());

    UFE = RealFE;
  } else {
    // File does not exist, create a virtual entry.
    UFE = new (FilesAlloc.Allocate()) FileEntry();
    VirtualFileEntries.push_back(UFE);
  }

  NamedFileEnt.second = FileEntryRef::MapValue(*UFE, *DirInfo);
  UFE->LastRef = FileEntryRef(NamedFileEnt);
  UFE->Size = Size;
  UFE->ModTime = ModificationTime;
  UFE->Dir = &DirInfo->getDirEntry();
  UFE->UID = NextFileUID++;
  UFE->File.reset();
  return FileEntryRef(NamedFileEnt);
}

OptionalFileEntryRef FileManager::getBypassFile(FileEntryRef VF) {
  // Stat of the file and return nullptr if it doesn't exist.
  llvm::vfs::Status Status;
  if (getStatValue(VF.getName(), Status, /*isFile=*/true, /*F=*/nullptr))
    return std::nullopt;

  if (!SeenBypassFileEntries)
    SeenBypassFileEntries = std::make_unique<
        llvm::StringMap<llvm::ErrorOr<FileEntryRef::MapValue>>>();

  // If we've already bypassed just use the existing one.
  auto Insertion = SeenBypassFileEntries->insert(
      {VF.getName(), std::errc::no_such_file_or_directory});
  if (!Insertion.second)
    return FileEntryRef(*Insertion.first);

  // Fill in the new entry from the stat.
  FileEntry *BFE = new (FilesAlloc.Allocate()) FileEntry();
  BypassFileEntries.push_back(BFE);
  Insertion.first->second = FileEntryRef::MapValue(*BFE, VF.getDir());
  BFE->LastRef = FileEntryRef(*Insertion.first);
  BFE->Size = Status.getSize();
  BFE->Dir = VF.getFileEntry().Dir;
  BFE->ModTime = llvm::sys::toTimeT(Status.getLastModificationTime());
  BFE->UID = NextFileUID++;

  // Save the entry in the bypass table and return.
  return FileEntryRef(*Insertion.first);
}

bool FileManager::FixupRelativePath(llvm::SmallVectorImpl<char> &path) const {
  llvm::StringRef pathRef(path.data(), path.size());

  if (FileSystemOpts.WorkingDir.empty() ||
      llvm::sys::path::is_absolute(pathRef))
    return false;

  llvm::SmallString<128> NewPath(FileSystemOpts.WorkingDir);
  llvm::sys::path::append(NewPath, pathRef);
  path = NewPath;
  return true;
}

bool FileManager::makeAbsolutePath(llvm::SmallVectorImpl<char> &Path) const {
  bool Changed = FixupRelativePath(Path);

  if (!llvm::sys::path::is_absolute(
          llvm::StringRef(Path.data(), Path.size()))) {
    FS->makeAbsolute(Path);
    Changed = true;
  }

  return Changed;
}

void FileManager::fillRealPathName(FileEntry *UFE, llvm::StringRef FileName) {
  llvm::SmallString<128> AbsPath(FileName);
  makeAbsolutePath(AbsPath);
  llvm::sys::path::remove_dots(AbsPath, /*remove_dot_dot=*/true);
  UFE->RealPathName = std::string(AbsPath.str());
}

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
FileManager::getBufferForFile(FileEntryRef FE, bool isVolatile,
                              bool RequiresNullTerminator) {
  const FileEntry *Entry = &FE.getFileEntry();
  // If the content is living on the file entry, return a reference to it.
  if (Entry->Content)
    return llvm::MemoryBuffer::getMemBuffer(Entry->Content->getMemBufferRef());

  uint64_t FileSize = Entry->getSize();
  // If there's a high enough chance that the file have changed since we
  // got its size, force a stat before opening it.
  if (isVolatile || Entry->isNamedPipe())
    FileSize = -1;

  llvm::StringRef Filename = FE.getName();
  // If the file is already open, use the open file descriptor.
  if (Entry->File) {
    auto Result = Entry->File->getBuffer(Filename, FileSize,
                                         RequiresNullTerminator, isVolatile);
    Entry->closeFile();
    return Result;
  }

  // Otherwise, open the file.
  return getBufferForFileImpl(Filename, FileSize, isVolatile,
                              RequiresNullTerminator);
}

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
FileManager::getBufferForFileImpl(llvm::StringRef Filename, int64_t FileSize,
                                  bool isVolatile,
                                  bool RequiresNullTerminator) {
  if (FileSystemOpts.WorkingDir.empty())
    return FS->getBufferForFile(Filename, FileSize, RequiresNullTerminator,
                                isVolatile);

  llvm::SmallString<128> FilePath(Filename);
  FixupRelativePath(FilePath);
  return FS->getBufferForFile(FilePath, FileSize, RequiresNullTerminator,
                              isVolatile);
}

static std::error_code statVFS(llvm::StringRef Path, llvm::vfs::Status &Status,
                               bool isFile, std::unique_ptr<llvm::vfs::File> *F,
                               llvm::vfs::FileSystem &FS) {
  bool isForDir = !isFile;
  std::error_code RetCode;

  if (isForDir || !F) {
    llvm::ErrorOr<llvm::vfs::Status> StatusOrErr = FS.status(Path);
    if (!StatusOrErr)
      RetCode = StatusOrErr.getError();
    else
      Status = *StatusOrErr;
  } else {
    auto OwnedFile = FS.openFileForRead(Path);
    if (!OwnedFile) {
      RetCode = OwnedFile.getError();
    } else {
      llvm::ErrorOr<llvm::vfs::Status> StatusOrErr = (*OwnedFile)->status();
      if (StatusOrErr) {
        Status = *StatusOrErr;
        *F = std::move(*OwnedFile);
      } else {
        *F = nullptr;
        RetCode = StatusOrErr.getError();
      }
    }
  }

  if (RetCode)
    return RetCode;

  if (Status.isDirectory() != isForDir) {
    if (F)
      *F = nullptr;
    return std::make_error_code(Status.isDirectory()
                                    ? std::errc::is_a_directory
                                    : std::errc::not_a_directory);
  }

  return std::error_code();
}

std::error_code FileManager::getStatValue(llvm::StringRef Path,
                                          llvm::vfs::Status &Status,
                                          bool isFile,
                                          std::unique_ptr<llvm::vfs::File> *F) {
  if (FileSystemOpts.WorkingDir.empty())
    return statVFS(Path, Status, isFile, F, *FS);

  llvm::SmallString<128> FilePath(Path);
  FixupRelativePath(FilePath);

  return statVFS(FilePath.c_str(), Status, isFile, F, *FS);
}

std::error_code FileManager::getNoncachedStatValue(llvm::StringRef Path,
                                                   llvm::vfs::Status &Result) {
  llvm::SmallString<128> FilePath(Path);
  FixupRelativePath(FilePath);

  llvm::ErrorOr<llvm::vfs::Status> S = FS->status(FilePath.c_str());
  if (!S)
    return S.getError();
  Result = *S;
  return std::error_code();
}

void FileManager::GetUniqueIDMapping(
    llvm::SmallVectorImpl<OptionalFileEntryRef> &UIDToFiles) const {
  UIDToFiles.clear();
  UIDToFiles.resize(NextFileUID);

  for (const auto &Entry : SeenFileEntries) {
    // Only return files that exist and are not redirected.
    if (!Entry.getValue() || !Entry.getValue()->V.is<FileEntry *>())
      continue;
    FileEntryRef FE(Entry);
    // Add this file if it's the first one with the UID, or if its name is
    // better than the existing one.
    OptionalFileEntryRef &ExistingFE = UIDToFiles[FE.getUID()];
    if (!ExistingFE || FE.getName() < ExistingFE->getName())
      ExistingFE = FE;
  }
}

llvm::StringRef FileManager::getCanonicalName(DirectoryEntryRef Dir) {
  return getCanonicalName(Dir, Dir.getName());
}

llvm::StringRef FileManager::getCanonicalName(FileEntryRef File) {
  return getCanonicalName(File, File.getName());
}

llvm::StringRef FileManager::getCanonicalName(const void *Entry,
                                              llvm::StringRef Name) {
  llvm::DenseMap<const void *, llvm::StringRef>::iterator Known =
      CanonicalNames.find(Entry);
  if (Known != CanonicalNames.end())
    return Known->second;

  // Name comes from FileEntry/DirectoryEntry::getName(), so it is safe to
  // store it in the DenseMap below.
  llvm::StringRef CanonicalName(Name);

  llvm::SmallString<256> AbsPathBuf;
  llvm::SmallString<256> RealPathBuf;
  if (!FS->getRealPath(Name, RealPathBuf)) {
    if (is_style_windows(llvm::sys::path::Style::native)) {
      // For Windows paths, only use the real path if it doesn't resolve
      // a substitute drive, as those are used to avoid MAX_PATH issues.
      AbsPathBuf = Name;
      if (!FS->makeAbsolute(AbsPathBuf)) {
        if (llvm::sys::path::root_name(RealPathBuf) ==
            llvm::sys::path::root_name(AbsPathBuf)) {
          CanonicalName = RealPathBuf.str().copy(CanonicalNameStorage);
        } else {
          // Fallback to using the absolute path.
          // Simplifying /../ is semantically valid on Windows even in the
          // presence of symbolic links.
          llvm::sys::path::remove_dots(AbsPathBuf, /*remove_dot_dot=*/true);
          CanonicalName = AbsPathBuf.str().copy(CanonicalNameStorage);
        }
      }
    } else {
      CanonicalName = RealPathBuf.str().copy(CanonicalNameStorage);
    }
  }

  CanonicalNames.insert({Entry, CanonicalName});
  return CanonicalName;
}

void FileManager::PrintStats() const {
  llvm::errs() << "\n*** File Manager Stats:\n";
  llvm::errs() << UniqueRealFiles.size() << " real files found, "
               << UniqueRealDirs.size() << " real dirs found.\n";
  llvm::errs() << VirtualFileEntries.size() << " virtual files found, "
               << VirtualDirectoryEntries.size() << " virtual dirs found.\n";
  llvm::errs() << NumDirLookups << " dir lookups, " << NumDirCacheMisses
               << " dir cache misses.\n";
  llvm::errs() << NumFileLookups << " file lookups, " << NumFileCacheMisses
               << " file cache misses.\n";
}

void neverc::quoteMakeTarget(llvm::StringRef Target,
                             llvm::SmallVectorImpl<char> &Res) {
  for (unsigned i = 0, e = Target.size(); i != e; ++i) {
    switch (Target[i]) {
    case ' ':
    case '\t':
      for (int j = i - 1; j >= 0 && Target[j] == '\\'; --j)
        Res.push_back('\\');
      Res.push_back('\\');
      break;
    case '$':
      Res.push_back('$');
      break;
    case '#':
      Res.push_back('\\');
      break;
    default:
      break;
    }

    Res.push_back(Target[i]);
  }
}

FileEntry::FileEntry() : UniqueID(0, 0) {}

FileEntry::~FileEntry() = default;

void FileEntry::closeFile() const { File.reset(); }

// ===----------------------------------------------------------------------===
// ContentCache
// ===----------------------------------------------------------------------===

using namespace SrcMgr;
using llvm::MemoryBuffer;

unsigned ContentCache::getSizeBytesMapped() const {
  return Buffer ? Buffer->getBufferSize() : 0;
}

llvm::MemoryBuffer::BufferKind ContentCache::getMemoryBufferKind() const {
  if (Buffer == nullptr) {
    assert(0 && "Buffer should never be null");
    return llvm::MemoryBuffer::MemoryBuffer_Malloc;
  }
  return Buffer->getBufferKind();
}

unsigned ContentCache::getSize() const {
  return Buffer ? (unsigned)Buffer->getBufferSize()
                : (unsigned)ContentsEntry->getSize();
}

const char *ContentCache::getInvalidBOM(llvm::StringRef BufStr) {
  // If the buffer is valid, check to see if it has a UTF Byte Order Mark
  // (BOM).  We only support UTF-8 with and without a BOM right now.  See
  // http://en.wikipedia.org/wiki/Byte_order_mark for more information.
  const char *InvalidBOM =
      llvm::StringSwitch<const char *>(BufStr)
          .StartsWith(llvm::StringLiteral::withInnerNUL("\x00\x00\xFE\xFF"),
                      "UTF-32 (BE)")
          .StartsWith(llvm::StringLiteral::withInnerNUL("\xFF\xFE\x00\x00"),
                      "UTF-32 (LE)")
          .StartsWith("\xFE\xFF", "UTF-16 (BE)")
          .StartsWith("\xFF\xFE", "UTF-16 (LE)")
          .StartsWith("\x2B\x2F\x76", "UTF-7")
          .StartsWith("\xF7\x64\x4C", "UTF-1")
          .StartsWith("\xDD\x73\x66\x73", "UTF-EBCDIC")
          .StartsWith("\x0E\xFE\xFF", "SCSU")
          .StartsWith("\xFB\xEE\x28", "BOCU-1")
          .StartsWith("\x84\x31\x95\x33", "GB-18030")
          .Default(nullptr);

  return InvalidBOM;
}

bool ContentCache::checkBufUTF16(llvm::StringRef BufStr) {
  const char *Result = llvm::StringSwitch<const char *>(BufStr)
                           .StartsWith("\xFE\xFF", "UTF-16 (BE)")
                           .StartsWith("\xFF\xFE", "UTF-16 (LE)")
                           .Default(nullptr);
  if (Result) {
    return true;
  }
  return false;
}

bool ContentCache::checkBufUTF32(llvm::StringRef BufStr) {
  const char *Result =
      llvm::StringSwitch<const char *>(BufStr)
          .StartsWith(llvm::StringLiteral::withInnerNUL("\x00\x00\xFE\xFF"),
                      "UTF-32 (BE)")
          .StartsWith(llvm::StringLiteral::withInnerNUL("\xFF\xFE\x00\x00"),
                      "UTF-32 (LE)")
          .Default(nullptr);
  if (Result) {
    return true;
  }
  return false;
}

std::optional<llvm::MemoryBufferRef>
ContentCache::getBufferOrNone(DiagnosticsEngine &Diag, FileManager &FM,
                              SourceLocation Loc) const {
  // Lazily create the Buffer for ContentCaches that wrap files.  If we already
  // computed it, just return what we have.
  if (IsBufferInvalid)
    return std::nullopt;
  if (Buffer)
    return Buffer->getMemBufferRef();
  if (!ContentsEntry)
    return std::nullopt;

  // Start with the assumption that the buffer is invalid to simplify early
  // return paths.
  IsBufferInvalid = true;

  auto BufferOrError = FM.getBufferForFile(*ContentsEntry, IsFileVolatile);

  // If we were unable to open the file, then we are in an inconsistent
  // situation where the content cache referenced a file which no longer
  // exists. Most likely, we were using a stat cache with an invalid entry but
  // the file could also have been removed during processing. Since we can't
  // really deal with this situation, just create an empty buffer.
  if (!BufferOrError) {
    if (Diag.isDiagnosticInFlight())
      Diag.SetDelayedDiagnostic(diag::err_cannot_open_file,
                                ContentsEntry->getName(),
                                BufferOrError.getError().message());
    else
      Diag.Report(Loc, diag::err_cannot_open_file)
          << ContentsEntry->getName() << BufferOrError.getError().message();

    return std::nullopt;
  }

  Buffer = std::move(*BufferOrError);

  // Check that the file's size fits in an 'unsigned' (with room for a
  // past-the-end value). This is deeply regrettable, but various parts of
  // NeverC (including elsewhere in this file!) use 'unsigned' to represent file
  // offsets, line numbers, string literal lengths, and so on, and fail
  // miserably on large source files.
  //
  // Note: ContentsEntry could be a named pipe, in which case
  // ContentsEntry::getSize() could have the wrong size. Use
  // MemoryBuffer::getBufferSize() instead.
  if (Buffer->getBufferSize() >= std::numeric_limits<unsigned>::max()) {
    if (Diag.isDiagnosticInFlight())
      Diag.SetDelayedDiagnostic(diag::err_file_too_large,
                                ContentsEntry->getName());
    else
      Diag.Report(Loc, diag::err_file_too_large) << ContentsEntry->getName();

    return std::nullopt;
  }

  // Unless this is a named pipe (in which case we can handle a mismatch),
  // check that the file's size is the same as in the file entry (which may
  // have come from a stat cache).
  if (!ContentsEntry->isNamedPipe() &&
      Buffer->getBufferSize() != (size_t)ContentsEntry->getSize()) {
    if (Diag.isDiagnosticInFlight())
      Diag.SetDelayedDiagnostic(diag::err_file_modified,
                                ContentsEntry->getName());
    else
      Diag.Report(Loc, diag::err_file_modified) << ContentsEntry->getName();

    return std::nullopt;
  }

  // If the buffer is valid, check to see if it has a UTF Byte Order Mark
  // (BOM).  We only support UTF-8 with and without a BOM right now.  See
  // http://en.wikipedia.org/wiki/Byte_order_mark for more information.
  llvm::StringRef BufStr = Buffer->getBuffer();
  const char *InvalidBOM = getInvalidBOM(BufStr);

  auto moveBuffer = [this](llvm::StringRef StrUtf8) {
    auto NewBuf =
        llvm::WritableMemoryBuffer::getNewUninitMemBuffer(StrUtf8.size());
    if (NewBuf) {
      memcpy(NewBuf->getBufferStart(), StrUtf8.data(), StrUtf8.size());
      Buffer = std::move(NewBuf);
    }
  };

  // [MSVC Compatibility] Trying to convert UTF16 to UTF8.
  llvm::SmallString<4096> StrUtf8;
  if (InvalidBOM && checkBufUTF16(BufStr)) {
    if (llvm::convertUTF16ToUTF8String(
            llvm::ArrayRef<char>(BufStr.data(), BufStr.size()), StrUtf8)) {
      moveBuffer(StrUtf8);
      BufStr = Buffer->getBuffer();

      // Verify it again.
      InvalidBOM = getInvalidBOM(BufStr);
    }
  } else {
#ifdef _WIN32
    // Trying to convert GBK to UTF8.
    if (llvm::convertGBKToUTF8String(BufStr, StrUtf8)) {
      moveBuffer(StrUtf8);
      BufStr = Buffer->getBuffer();

      // Verify it again.
      InvalidBOM = getInvalidBOM(BufStr);
    }
#endif
  }

  if (InvalidBOM) {
    Diag.Report(Loc, diag::err_unsupported_bom)
        << InvalidBOM << ContentsEntry->getName();
    return std::nullopt;
  }

  // Buffer has been validated.
  IsBufferInvalid = false;
  return Buffer->getMemBufferRef();
}

// ===----------------------------------------------------------------------===
// LineTableInfo & SourceManager initialization
// ===----------------------------------------------------------------------===

unsigned LineTableInfo::getLineTableFilenameID(llvm::StringRef Name) {
  auto IterBool = FilenameIDs.try_emplace(Name, FilenamesByID.size());
  if (IterBool.second)
    FilenamesByID.push_back(&*IterBool.first);
  return IterBool.first->second;
}

void LineTableInfo::AddLineNote(FileID FID, unsigned Offset, unsigned LineNo,
                                int FilenameID, unsigned EntryExit,
                                SrcMgr::CharacteristicKind FileKind) {
  std::vector<LineEntry> &Entries = LineEntries[FID];

  assert((Entries.empty() || Entries.back().FileOffset < Offset) &&
         "Adding line entries out of order!");

  unsigned IncludeOffset = 0;
  if (EntryExit == 1) {
    // Push #include
    IncludeOffset = Offset - 1;
  } else {
    const auto *PrevEntry = Entries.empty() ? nullptr : &Entries.back();
    if (EntryExit == 2) {
      // Pop #include
      assert(
          PrevEntry && PrevEntry->IncludeOffset &&
          "DirectiveHandler should have caught case when popping empty include "
          "stack");
      PrevEntry = FindNearestLineEntry(FID, PrevEntry->IncludeOffset);
    }
    if (PrevEntry) {
      IncludeOffset = PrevEntry->IncludeOffset;
      if (FilenameID == -1) {
        // An unspecified FilenameID means use the previous (or containing)
        // filename if available, or the main source file otherwise.
        FilenameID = PrevEntry->FilenameID;
      }
    }
  }

  Entries.push_back(
      LineEntry::get(Offset, LineNo, FilenameID, FileKind, IncludeOffset));
}

const LineEntry *LineTableInfo::FindNearestLineEntry(FileID FID,
                                                     unsigned Offset) {
  const std::vector<LineEntry> &Entries = LineEntries[FID];
  assert(!Entries.empty() && "No #line entries for this FID after all!");

  // It is very common for the query to be after the last #line, check this
  // first.
  if (Entries.back().FileOffset <= Offset)
    return &Entries.back();

  // Do a binary search to find the maximal element that is still before Offset.
  std::vector<LineEntry>::const_iterator I = llvm::upper_bound(Entries, Offset);
  if (I == Entries.begin())
    return nullptr;
  return &*--I;
}

void LineTableInfo::AddEntry(FileID FID,
                             const std::vector<LineEntry> &Entries) {
  LineEntries[FID] = Entries;
}

unsigned SourceManager::getLineTableFilenameID(llvm::StringRef Name) {
  return getLineTable().getLineTableFilenameID(Name);
}

void SourceManager::AddLineNote(SourceLocation Loc, unsigned LineNo,
                                int FilenameID, bool IsFileEntry,
                                bool IsFileExit,
                                SrcMgr::CharacteristicKind FileKind) {
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);

  bool Invalid = false;
  const SLocEntry &Entry = getSLocEntry(LocInfo.first, &Invalid);
  if (!Entry.isFile() || Invalid)
    return;

  const SrcMgr::FileInfo &FileInfo = Entry.getFile();

  // Remember that this file has #line directives now if it doesn't already.
  const_cast<SrcMgr::FileInfo &>(FileInfo).setHasLineDirectives();

  (void)getLineTable();

  unsigned EntryExit = 0;
  if (IsFileEntry)
    EntryExit = 1;
  else if (IsFileExit)
    EntryExit = 2;

  LineTable->AddLineNote(LocInfo.first, LocInfo.second, LineNo, FilenameID,
                         EntryExit, FileKind);
}

LineTableInfo &SourceManager::getLineTable() {
  if (!LineTable)
    LineTable.reset(new LineTableInfo());
  return *LineTable;
}

SourceManager::SourceManager(DiagnosticsEngine &Diag, FileManager &FileMgr,
                             bool UserFilesAreVolatile)
    : Diag(Diag), FileMgr(FileMgr), UserFilesAreVolatile(UserFilesAreVolatile) {
  clearIDTables();
  Diag.setSourceManager(this);
}

SourceManager::~SourceManager() {
  // Delete FileEntry objects corresponding to content caches.  Since the actual
  // content cache objects are bump pointer allocated, we just have to run the
  // dtors, but we call the deallocate method for completeness.
  for (unsigned i = 0, e = MemBufferInfos.size(); i != e; ++i) {
    if (MemBufferInfos[i]) {
      MemBufferInfos[i]->~ContentCache();
      ContentCacheAlloc.Deallocate(MemBufferInfos[i]);
    }
  }
  for (auto I = FileInfos.begin(), E = FileInfos.end(); I != E; ++I) {
    if (I->second) {
      I->second->~ContentCache();
      ContentCacheAlloc.Deallocate(I->second);
    }
  }
}

void SourceManager::clearIDTables() {
  MainFileID = FileID();
  LocalSLocEntryTable.clear();
  LoadedSLocEntryTable.clear();
  SLocEntryLoaded.clear();
  SLocEntryOffsetLoaded.clear();
  LastLineNoFileIDQuery = FileID();
  LastLineNoContentCache = nullptr;
  LastFileIDLookup = FileID();

  if (LineTable)
    LineTable->clear();

  // Use up FileID #0 as an invalid expansion.
  NextLocalOffset = 0;
  CurrentLoadedOffset = MaxLoadedOffset;
  createExpansionLoc(SourceLocation(), SourceLocation(), SourceLocation(), 1);
}

bool SourceManager::isMainFile(const FileEntry &SourceFile) {
  assert(MainFileID.isValid() && "expected initialized SourceManager");
  if (auto *FE = getFileEntryForID(MainFileID))
    return FE->getUID() == SourceFile.getUID();
  return false;
}

void SourceManager::initializeForReplay(const SourceManager &Old) {
  assert(MainFileID.isInvalid() && "expected uninitialized SourceManager");

  auto CloneContentCache = [&](const ContentCache *Cache) -> ContentCache * {
    auto *Clone = new (ContentCacheAlloc.Allocate<ContentCache>()) ContentCache;
    Clone->OrigEntry = Cache->OrigEntry;
    Clone->ContentsEntry = Cache->ContentsEntry;
    Clone->BufferOverridden = Cache->BufferOverridden;
    Clone->IsFileVolatile = Cache->IsFileVolatile;
    Clone->IsTransient = Cache->IsTransient;
    Clone->setUnownedBuffer(Cache->getBufferIfLoaded());
    return Clone;
  };

  // Ensure all SLocEntries are loaded from the external source.
  for (unsigned I = 0, N = Old.LoadedSLocEntryTable.size(); I != N; ++I)
    if (!Old.SLocEntryLoaded[I])
      Old.loadSLocEntry(I, nullptr);

  // Inherit any content cache data from the old source manager.
  for (auto &FileInfo : Old.FileInfos) {
    SrcMgr::ContentCache *&Slot = FileInfos[FileInfo.first];
    if (Slot)
      continue;
    Slot = CloneContentCache(FileInfo.second);
  }
}

ContentCache &SourceManager::getOrCreateContentCache(FileEntryRef FileEnt,
                                                     bool isSystemFile) {
  // Do we already have information about this file?
  ContentCache *&Entry = FileInfos[FileEnt];
  if (Entry)
    return *Entry;

  // Nope, create a new Cache entry.
  Entry = ContentCacheAlloc.Allocate<ContentCache>();

  if (OverriddenFilesInfo) {
    // If the file contents are overridden with contents from another file,
    // pass that file to ContentCache. The reported source file name always
    // matches FileEnt (NeverC keeps the original name).
    auto overI = OverriddenFilesInfo->OverriddenFiles.find(FileEnt);
    if (overI == OverriddenFilesInfo->OverriddenFiles.end())
      new (Entry) ContentCache(FileEnt);
    else
      new (Entry) ContentCache(FileEnt, overI->second);
  } else {
    new (Entry) ContentCache(FileEnt);
  }

  Entry->IsFileVolatile = UserFilesAreVolatile && !isSystemFile;
  Entry->IsTransient = FilesAreTransient;
  Entry->BufferOverridden |= FileEnt.isNamedPipe();

  return *Entry;
}

ContentCache &SourceManager::createMemBufferContentCache(
    std::unique_ptr<llvm::MemoryBuffer> Buffer) {
  // Add a new ContentCache to the MemBufferInfos list and return it.
  ContentCache *Entry = ContentCacheAlloc.Allocate<ContentCache>();
  new (Entry) ContentCache();
  MemBufferInfos.push_back(Entry);
  Entry->setBuffer(std::move(Buffer));
  return *Entry;
}

const SrcMgr::SLocEntry &SourceManager::loadSLocEntry(unsigned Index,
                                                      bool *Invalid) const {
  assert(!SLocEntryLoaded[Index]);
  if (ExternalSLocEntries->ReadSLocEntry(-(static_cast<int>(Index) + 2))) {
    if (Invalid)
      *Invalid = true;
    // If the file of the SLocEntry changed we could still have loaded it.
    if (!SLocEntryLoaded[Index]) {
      // Try to recover; create a SLocEntry so the rest of NeverC can handle it.
      if (!FakeSLocEntryForRecovery)
        FakeSLocEntryForRecovery = std::make_unique<SLocEntry>(SLocEntry::get(
            0, FileInfo::get(SourceLocation(), getFakeContentCacheForRecovery(),
                             SrcMgr::C_User, "")));
      return *FakeSLocEntryForRecovery;
    }
  }

  return LoadedSLocEntryTable[Index];
}

std::pair<int, SourceLocation::UIntTy>
SourceManager::AllocateLoadedSLocEntries(unsigned NumSLocEntries,
                                         SourceLocation::UIntTy TotalSize) {
  assert(ExternalSLocEntries && "Don't have an external sloc source");
  // Make sure we're not about to run out of source locations.
  if (CurrentLoadedOffset < TotalSize ||
      CurrentLoadedOffset - TotalSize < NextLocalOffset) {
    return std::make_pair(0, 0);
  }
  LoadedSLocEntryTable.resize(LoadedSLocEntryTable.size() + NumSLocEntries);
  SLocEntryLoaded.resize(LoadedSLocEntryTable.size());
  SLocEntryOffsetLoaded.resize(LoadedSLocEntryTable.size());
  CurrentLoadedOffset -= TotalSize;
  int BaseID = -int(LoadedSLocEntryTable.size()) - 1;
  LoadedSLocEntryAllocBegin.push_back(FileID::get(BaseID));
  return std::make_pair(BaseID, CurrentLoadedOffset);
}

llvm::MemoryBufferRef SourceManager::getFakeBufferForRecovery() const {
  if (!FakeBufferForRecovery)
    FakeBufferForRecovery =
        llvm::MemoryBuffer::getMemBuffer("<<<INVALID BUFFER>>");

  return *FakeBufferForRecovery;
}

SrcMgr::ContentCache &SourceManager::getFakeContentCacheForRecovery() const {
  if (!FakeContentCacheForRecovery) {
    FakeContentCacheForRecovery = std::make_unique<SrcMgr::ContentCache>();
    FakeContentCacheForRecovery->setUnownedBuffer(getFakeBufferForRecovery());
  }
  return *FakeContentCacheForRecovery;
}

FileID SourceManager::getPreviousFileID(FileID FID) const {
  if (FID.isInvalid())
    return FileID();

  int ID = FID.ID;
  if (ID == -1)
    return FileID();

  if (ID > 0) {
    if (ID - 1 == 0)
      return FileID();
  } else if (unsigned(-(ID - 1) - 2) >= LoadedSLocEntryTable.size()) {
    return FileID();
  }

  return FileID::get(ID - 1);
}

FileID SourceManager::getNextFileID(FileID FID) const {
  if (FID.isInvalid())
    return FileID();

  int ID = FID.ID;
  if (ID > 0) {
    if (unsigned(ID + 1) >= local_sloc_entry_size())
      return FileID();
  } else if (ID + 1 >= -1) {
    return FileID();
  }

  return FileID::get(ID + 1);
}

FileID SourceManager::createFileID(FileEntryRef SourceFile,
                                   SourceLocation IncludePos,
                                   SrcMgr::CharacteristicKind FileCharacter,
                                   int LoadedID,
                                   SourceLocation::UIntTy LoadedOffset) {
  SrcMgr::ContentCache &IR =
      getOrCreateContentCache(SourceFile, isSystem(FileCharacter));

  // If this is a named pipe, immediately load the buffer to ensure subsequent
  // calls to ContentCache::getSize() are accurate.
  if (IR.ContentsEntry->isNamedPipe())
    (void)IR.getBufferOrNone(Diag, getFileManager(), SourceLocation());

  return createFileIDImpl(IR, SourceFile.getName(), IncludePos, FileCharacter,
                          LoadedID, LoadedOffset);
}

FileID SourceManager::createFileID(std::unique_ptr<llvm::MemoryBuffer> Buffer,
                                   SrcMgr::CharacteristicKind FileCharacter,
                                   int LoadedID,
                                   SourceLocation::UIntTy LoadedOffset,
                                   SourceLocation IncludeLoc) {
  llvm::StringRef Name = Buffer->getBufferIdentifier();
  return createFileIDImpl(createMemBufferContentCache(std::move(Buffer)), Name,
                          IncludeLoc, FileCharacter, LoadedID, LoadedOffset);
}

FileID SourceManager::createFileID(const llvm::MemoryBufferRef &Buffer,
                                   SrcMgr::CharacteristicKind FileCharacter,
                                   int LoadedID,
                                   SourceLocation::UIntTy LoadedOffset,
                                   SourceLocation IncludeLoc) {
  return createFileID(llvm::MemoryBuffer::getMemBuffer(Buffer), FileCharacter,
                      LoadedID, LoadedOffset, IncludeLoc);
}

FileID
SourceManager::getOrCreateFileID(FileEntryRef SourceFile,
                                 SrcMgr::CharacteristicKind FileCharacter) {
  FileID ID = translateFile(SourceFile);
  return ID.isValid()
             ? ID
             : createFileID(SourceFile, SourceLocation(), FileCharacter);
}

FileID SourceManager::createFileIDImpl(ContentCache &File,
                                       llvm::StringRef Filename,
                                       SourceLocation IncludePos,
                                       SrcMgr::CharacteristicKind FileCharacter,
                                       int LoadedID,
                                       SourceLocation::UIntTy LoadedOffset) {
  if (LoadedID < 0) {
    assert(LoadedID != -1 && "Loading sentinel FileID");
    unsigned Index = unsigned(-LoadedID) - 2;
    assert(Index < LoadedSLocEntryTable.size() && "FileID out of range");
    assert(!SLocEntryLoaded[Index] && "FileID already loaded");
    LoadedSLocEntryTable[Index] = SLocEntry::get(
        LoadedOffset, FileInfo::get(IncludePos, File, FileCharacter, Filename));
    SLocEntryLoaded[Index] = SLocEntryOffsetLoaded[Index] = true;
    return FileID::get(LoadedID);
  }
  unsigned FileSize = File.getSize();
  if (!(NextLocalOffset + FileSize + 1 > NextLocalOffset &&
        NextLocalOffset + FileSize + 1 <= CurrentLoadedOffset)) {
    Diag.Report(IncludePos, diag::err_sloc_space_too_large);
    noteSLocAddressSpaceUsage(Diag);
    return FileID();
  }
  LocalSLocEntryTable.push_back(
      SLocEntry::get(NextLocalOffset,
                     FileInfo::get(IncludePos, File, FileCharacter, Filename)));
  // We do a +1 here because we want a SourceLocation that means "the end of the
  // file", e.g. for the "no newline at the end of the file" diagnostic.
  NextLocalOffset += FileSize + 1;

  // Set LastFileIDLookup to the newly created file.  The next getFileID call is
  // almost guaranteed to be from that file.
  FileID FID = FileID::get(LocalSLocEntryTable.size() - 1);
  return LastFileIDLookup = FID;
}

SourceLocation SourceManager::createMacroArgExpansionLoc(
    SourceLocation SpellingLoc, SourceLocation ExpansionLoc, unsigned Length) {
  ExpansionInfo Info =
      ExpansionInfo::createForMacroArg(SpellingLoc, ExpansionLoc);
  return createExpansionLocImpl(Info, Length);
}

SourceLocation SourceManager::createExpansionLoc(
    SourceLocation SpellingLoc, SourceLocation ExpansionLocStart,
    SourceLocation ExpansionLocEnd, unsigned Length, bool ExpansionIsTokenRange,
    int LoadedID, SourceLocation::UIntTy LoadedOffset) {
  ExpansionInfo Info = ExpansionInfo::create(
      SpellingLoc, ExpansionLocStart, ExpansionLocEnd, ExpansionIsTokenRange);
  return createExpansionLocImpl(Info, Length, LoadedID, LoadedOffset);
}

SourceLocation SourceManager::createTokenSplitLoc(SourceLocation Spelling,
                                                  SourceLocation TokenStart,
                                                  SourceLocation TokenEnd) {
  assert(getFileID(TokenStart) == getFileID(TokenEnd) &&
         "token spans multiple files");
  return createExpansionLocImpl(
      ExpansionInfo::createForTokenSplit(Spelling, TokenStart, TokenEnd),
      TokenEnd.getOffset() - TokenStart.getOffset());
}

SourceLocation
SourceManager::createExpansionLocImpl(const ExpansionInfo &Info,
                                      unsigned Length, int LoadedID,
                                      SourceLocation::UIntTy LoadedOffset) {
  if (LoadedID < 0) {
    assert(LoadedID != -1 && "Loading sentinel FileID");
    unsigned Index = unsigned(-LoadedID) - 2;
    assert(Index < LoadedSLocEntryTable.size() && "FileID out of range");
    assert(!SLocEntryLoaded[Index] && "FileID already loaded");
    LoadedSLocEntryTable[Index] = SLocEntry::get(LoadedOffset, Info);
    SLocEntryLoaded[Index] = SLocEntryOffsetLoaded[Index] = true;
    return SourceLocation::getMacroLoc(LoadedOffset);
  }
  LocalSLocEntryTable.push_back(SLocEntry::get(NextLocalOffset, Info));
#ifndef _WIN32
  if (NextLocalOffset + Length + 1 <= NextLocalOffset ||
      NextLocalOffset + Length + 1 > CurrentLoadedOffset) {
    Diag.Report(SourceLocation(), diag::err_sloc_space_too_large);
    llvm::report_fatal_error("ran out of source locations");
  }
#endif
  // See createFileID for that +1.
  NextLocalOffset += Length + 1;
  return SourceLocation::getMacroLoc(NextLocalOffset - (Length + 1));
}

std::optional<llvm::MemoryBufferRef>
SourceManager::getMemoryBufferForFileOrNone(FileEntryRef File) {
  SrcMgr::ContentCache &IR = getOrCreateContentCache(File);
  return IR.getBufferOrNone(Diag, getFileManager(), SourceLocation());
}

// ===----------------------------------------------------------------------===
// SourceManager — buffer & content override
// ===----------------------------------------------------------------------===

void SourceManager::overrideFileContents(
    FileEntryRef SourceFile, std::unique_ptr<llvm::MemoryBuffer> Buffer) {
  SrcMgr::ContentCache &IR = getOrCreateContentCache(SourceFile);

  IR.setBuffer(std::move(Buffer));
  IR.BufferOverridden = true;

  getOverriddenFilesInfo().OverriddenFilesWithBuffer.insert(SourceFile);
}

void SourceManager::overrideFileContents(const FileEntry *SourceFile,
                                         FileEntryRef NewFile) {
  assert(SourceFile->getSize() == NewFile.getSize() &&
         "Different sizes, use the FileManager to create a virtual file with "
         "the correct size");
  assert(FileInfos.find_as(SourceFile) == FileInfos.end() &&
         "This function should be called at the initialization stage, before "
         "any parsing occurs.");
  // FileEntryRef is not default-constructible.
  auto Pair = getOverriddenFilesInfo().OverriddenFiles.insert(
      std::make_pair(SourceFile, NewFile));
  if (!Pair.second)
    Pair.first->second = NewFile;
}

OptionalFileEntryRef
SourceManager::bypassFileContentsOverride(FileEntryRef File) {
  assert(isFileOverridden(&File.getFileEntry()));
  OptionalFileEntryRef BypassFile = FileMgr.getBypassFile(File);

  // If the file can't be found in the FS, give up.
  if (!BypassFile)
    return std::nullopt;

  (void)getOrCreateContentCache(*BypassFile);
  return BypassFile;
}

void SourceManager::setFileIsTransient(FileEntryRef File) {
  getOrCreateContentCache(File).IsTransient = true;
}

std::optional<llvm::StringRef>
SourceManager::getNonBuiltinFilenameForID(FileID FID) const {
  if (const SrcMgr::SLocEntry *Entry = getSLocEntryForFile(FID))
    if (Entry->getFile().getContentCache().OrigEntry)
      return Entry->getFile().getName();
  return std::nullopt;
}

llvm::StringRef SourceManager::getBufferData(FileID FID, bool *Invalid) const {
  auto B = getBufferDataOrNone(FID);
  if (Invalid)
    *Invalid = !B;
  return B ? *B : "<<<<<INVALID SOURCE LOCATION>>>>>";
}

std::optional<llvm::StringRef>
SourceManager::getBufferDataIfLoaded(FileID FID) const {
  if (const SrcMgr::SLocEntry *Entry = getSLocEntryForFile(FID))
    return Entry->getFile().getContentCache().getBufferDataIfLoaded();
  return std::nullopt;
}

std::optional<llvm::StringRef>
SourceManager::getBufferDataOrNone(FileID FID) const {
  if (const SrcMgr::SLocEntry *Entry = getSLocEntryForFile(FID))
    if (auto B = Entry->getFile().getContentCache().getBufferOrNone(
            Diag, getFileManager(), SourceLocation()))
      return B->getBuffer();
  return std::nullopt;
}

__attribute__((hot)) FileID
SourceManager::getFileIDSlow(SourceLocation::UIntTy SLocOffset) const {
  if (!SLocOffset)
    return FileID::get(0);

  // Now it is time to search for the correct file. See where the SLocOffset
  // sits in the global view and consult local or loaded buffers for it.
  if (SLocOffset < NextLocalOffset)
    return getFileIDLocal(SLocOffset);
  return getFileIDLoaded(SLocOffset);
}

__attribute__((hot)) FileID
SourceManager::getFileIDLocal(SourceLocation::UIntTy SLocOffset) const {
  assert(SLocOffset < NextLocalOffset && "Bad function choice");

  // After the first and second level caches, I see two common sorts of
  // behavior: 1) a lot of searched FileID's are "near" the cached file
  // location or are "near" the cached expansion location. 2) others are just
  // completely random and may be a very long way away.
  //
  // To handle this, we do a linear search for up to 8 steps to catch #1 quickly
  // then we fall back to a less cache efficient, but more scalable, binary
  // search to find the location.

  // See if this is near the file point - worst case we start scanning from the
  // most newly created FileID.

  // LessIndex - This is the lower bound of the range that we're searching.
  // We know that the offset corresponding to the FileID is less than
  // SLocOffset.
  unsigned LessIndex = 0;
  // upper bound of the search range.
  unsigned GreaterIndex = LocalSLocEntryTable.size();
  if (LastFileIDLookup.ID >= 0) {
    // Use the LastFileIDLookup to prune the search space.
    if (LocalSLocEntryTable[LastFileIDLookup.ID].getOffset() < SLocOffset)
      LessIndex = LastFileIDLookup.ID;
    else
      GreaterIndex = LastFileIDLookup.ID;
  }

  unsigned NumProbes = 0;
  while (true) {
    --GreaterIndex;
    assert(GreaterIndex < LocalSLocEntryTable.size());
    if (LocalSLocEntryTable[GreaterIndex].getOffset() <= SLocOffset) {
      FileID Res = FileID::get(int(GreaterIndex));
      // Remember it.  We have good locality across FileID lookups.
      LastFileIDLookup = Res;
      NumLinearScans += NumProbes + 1;
      return Res;
    }
    if (++NumProbes == 8)
      break;
  }

  NumProbes = 0;
  while (true) {
    unsigned MiddleIndex = (GreaterIndex - LessIndex) / 2 + LessIndex;
    unsigned Q1 = (MiddleIndex - LessIndex) / 2 + LessIndex;
    unsigned Q3 = (GreaterIndex - MiddleIndex) / 2 + MiddleIndex;
    __builtin_prefetch(&LocalSLocEntryTable[Q1], 0, 3);
    __builtin_prefetch(&LocalSLocEntryTable[Q3], 0, 3);
    SourceLocation::UIntTy MidOffset =
        getLocalSLocEntry(MiddleIndex).getOffset();

    ++NumProbes;

    // If the offset of the midpoint is too large, chop the high side of the
    // range to the midpoint.
    if (MidOffset > SLocOffset) {
      GreaterIndex = MiddleIndex;
      continue;
    }

    // If the middle index contains the value, succeed and return.
    if (MiddleIndex + 1 == LocalSLocEntryTable.size() ||
        SLocOffset < getLocalSLocEntry(MiddleIndex + 1).getOffset()) {
      FileID Res = FileID::get(MiddleIndex);

      // Remember it.  We have good locality across FileID lookups.
      LastFileIDLookup = Res;
      NumBinaryProbes += NumProbes;
      return Res;
    }

    // Otherwise, move the low-side up to the middle index.
    LessIndex = MiddleIndex;
  }
}

FileID SourceManager::getFileIDLoaded(SourceLocation::UIntTy SLocOffset) const {
  if (SLocOffset < CurrentLoadedOffset) {
    assert(0 && "Invalid SLocOffset or bad function choice");
    return FileID();
  }

  return FileID::get(ExternalSLocEntries->getSLocEntryID(SLocOffset));
}

SourceLocation
SourceManager::getExpansionLocSlowCase(SourceLocation Loc) const {
  do {
    // Note: If Loc indicates an offset into a token that came from a macro
    // expansion (e.g. the 5th character of the token) we do not want to add
    // this offset when going to the expansion location.  The expansion
    // location is the macro invocation, which the offset has nothing to do
    // with.  This is unlike when we get the spelling loc, because the offset
    // directly correspond to the token whose spelling we're inspecting.
    Loc = getSLocEntry(getFileID(Loc)).getExpansion().getExpansionLocStart();
  } while (!Loc.isFileID());

  return Loc;
}

SourceLocation SourceManager::getSpellingLocSlowCase(SourceLocation Loc) const {
  do {
    std::pair<FileID, unsigned> LocInfo = getDecomposedLoc(Loc);
    Loc = getSLocEntry(LocInfo.first).getExpansion().getSpellingLoc();
    Loc = Loc.getLocWithOffset(LocInfo.second);
  } while (!Loc.isFileID());
  return Loc;
}

SourceLocation SourceManager::getFileLocSlowCase(SourceLocation Loc) const {
  do {
    if (isMacroArgExpansion(Loc))
      Loc = getImmediateSpellingLoc(Loc);
    else
      Loc = getImmediateExpansionRange(Loc).getBegin();
  } while (!Loc.isFileID());
  return Loc;
}

// ===----------------------------------------------------------------------===
// SourceManager — location decomposition
// ===----------------------------------------------------------------------===

std::pair<FileID, unsigned> SourceManager::getDecomposedExpansionLocSlowCase(
    const SrcMgr::SLocEntry *E) const {
  // If this is an expansion record, walk through all the expansion points.
  FileID FID;
  SourceLocation Loc;
  unsigned Offset;
  do {
    Loc = E->getExpansion().getExpansionLocStart();

    FID = getFileID(Loc);
    E = &getSLocEntry(FID);
    Offset = Loc.getOffset() - E->getOffset();
  } while (!Loc.isFileID());

  return std::make_pair(FID, Offset);
}

std::pair<FileID, unsigned>
SourceManager::getDecomposedSpellingLocSlowCase(const SrcMgr::SLocEntry *E,
                                                unsigned Offset) const {
  // If this is an expansion record, walk through all the expansion points.
  FileID FID;
  SourceLocation Loc;
  do {
    Loc = E->getExpansion().getSpellingLoc();
    Loc = Loc.getLocWithOffset(Offset);

    FID = getFileID(Loc);
    E = &getSLocEntry(FID);
    Offset = Loc.getOffset() - E->getOffset();
  } while (!Loc.isFileID());

  return std::make_pair(FID, Offset);
}

SourceLocation
SourceManager::getImmediateSpellingLoc(SourceLocation Loc) const {
  if (Loc.isFileID())
    return Loc;
  std::pair<FileID, unsigned> LocInfo = getDecomposedLoc(Loc);
  Loc = getSLocEntry(LocInfo.first).getExpansion().getSpellingLoc();
  return Loc.getLocWithOffset(LocInfo.second);
}

llvm::StringRef SourceManager::getFilename(SourceLocation SpellingLoc) const {
  if (OptionalFileEntryRef F = getFileEntryRefForID(getFileID(SpellingLoc)))
    return F->getName();
  return llvm::StringRef();
}

CharSourceRange
SourceManager::getImmediateExpansionRange(SourceLocation Loc) const {
  assert(Loc.isMacroID() && "Not a macro expansion loc!");
  const ExpansionInfo &Expansion = getSLocEntry(getFileID(Loc)).getExpansion();
  return Expansion.getExpansionLocRange();
}

SourceLocation SourceManager::getTopMacroCallerLoc(SourceLocation Loc) const {
  while (isMacroArgExpansion(Loc))
    Loc = getImmediateSpellingLoc(Loc);
  return Loc;
}

CharSourceRange SourceManager::getExpansionRange(SourceLocation Loc) const {
  if (Loc.isFileID())
    return CharSourceRange(SourceRange(Loc, Loc), true);

  CharSourceRange Res = getImmediateExpansionRange(Loc);

  // Fully resolve the start and end locations to their ultimate expansion
  // points.
  while (!Res.getBegin().isFileID())
    Res.setBegin(getImmediateExpansionRange(Res.getBegin()).getBegin());
  while (!Res.getEnd().isFileID()) {
    CharSourceRange EndRange = getImmediateExpansionRange(Res.getEnd());
    Res.setEnd(EndRange.getEnd());
    Res.setTokenRange(EndRange.isTokenRange());
  }
  return Res;
}

bool SourceManager::isMacroArgExpansion(SourceLocation Loc,
                                        SourceLocation *StartLoc) const {
  if (!Loc.isMacroID())
    return false;

  FileID FID = getFileID(Loc);
  const SrcMgr::ExpansionInfo &Expansion = getSLocEntry(FID).getExpansion();
  if (!Expansion.isMacroArgExpansion())
    return false;

  if (StartLoc)
    *StartLoc = Expansion.getExpansionLocStart();
  return true;
}

bool SourceManager::isMacroBodyExpansion(SourceLocation Loc) const {
  if (!Loc.isMacroID())
    return false;

  FileID FID = getFileID(Loc);
  const SrcMgr::ExpansionInfo &Expansion = getSLocEntry(FID).getExpansion();
  return Expansion.isMacroBodyExpansion();
}

bool SourceManager::isAtStartOfImmediateMacroExpansion(
    SourceLocation Loc, SourceLocation *MacroBegin) const {
  assert(Loc.isValid() && Loc.isMacroID() && "Expected a valid macro loc");

  std::pair<FileID, unsigned> DecompLoc = getDecomposedLoc(Loc);
  if (DecompLoc.second > 0)
    return false; // Does not point at the start of expansion range.

  bool Invalid = false;
  const SrcMgr::ExpansionInfo &ExpInfo =
      getSLocEntry(DecompLoc.first, &Invalid).getExpansion();
  if (Invalid)
    return false;
  SourceLocation ExpLoc = ExpInfo.getExpansionLocStart();

  if (ExpInfo.isMacroArgExpansion()) {
    // For macro argument expansions, check if the previous FileID is part of
    // the same argument expansion, in which case this Loc is not at the
    // beginning of the expansion.
    FileID PrevFID = getPreviousFileID(DecompLoc.first);
    if (!PrevFID.isInvalid()) {
      const SrcMgr::SLocEntry &PrevEntry = getSLocEntry(PrevFID, &Invalid);
      if (Invalid)
        return false;
      if (PrevEntry.isExpansion() &&
          PrevEntry.getExpansion().getExpansionLocStart() == ExpLoc)
        return false;
    }
  }

  if (MacroBegin)
    *MacroBegin = ExpLoc;
  return true;
}

bool SourceManager::isAtEndOfImmediateMacroExpansion(
    SourceLocation Loc, SourceLocation *MacroEnd) const {
  assert(Loc.isValid() && Loc.isMacroID() && "Expected a valid macro loc");

  FileID FID = getFileID(Loc);
  SourceLocation NextLoc = Loc.getLocWithOffset(1);
  if (isInFileID(NextLoc, FID))
    return false; // Does not point at the end of expansion range.

  bool Invalid = false;
  const SrcMgr::ExpansionInfo &ExpInfo =
      getSLocEntry(FID, &Invalid).getExpansion();
  if (Invalid)
    return false;

  if (ExpInfo.isMacroArgExpansion()) {
    // For macro argument expansions, check if the next FileID is part of the
    // same argument expansion, in which case this Loc is not at the end of the
    // expansion.
    FileID NextFID = getNextFileID(FID);
    if (!NextFID.isInvalid()) {
      const SrcMgr::SLocEntry &NextEntry = getSLocEntry(NextFID, &Invalid);
      if (Invalid)
        return false;
      if (NextEntry.isExpansion() &&
          NextEntry.getExpansion().getExpansionLocStart() ==
              ExpInfo.getExpansionLocStart())
        return false;
    }
  }

  if (MacroEnd)
    *MacroEnd = ExpInfo.getExpansionLocEnd();
  return true;
}

const char *SourceManager::getCharacterData(SourceLocation SL,
                                            bool *Invalid) const {
  // Note that this is a hot function in the getSpelling() path, which is
  // heavily used by -E mode.
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(SL);

  // Note that calling 'getBuffer()' may lazily page in a source file.
  bool CharDataInvalid = false;
  const SLocEntry &Entry = getSLocEntry(LocInfo.first, &CharDataInvalid);
  if (CharDataInvalid || !Entry.isFile()) {
    if (Invalid)
      *Invalid = true;

    return "<<<<INVALID BUFFER>>>>";
  }
  std::optional<llvm::MemoryBufferRef> Buffer =
      Entry.getFile().getContentCache().getBufferOrNone(Diag, getFileManager(),
                                                        SourceLocation());
  if (Invalid)
    *Invalid = !Buffer;
  return Buffer ? Buffer->getBufferStart() + LocInfo.second
                : "<<<<INVALID BUFFER>>>>";
}

// ===----------------------------------------------------------------------===
// SourceManager — line & column numbers
// ===----------------------------------------------------------------------===

unsigned SourceManager::getColumnNumber(FileID FID, unsigned FilePos,
                                        bool *Invalid) const {
  if (LastLineNoFileIDQuery == FID && LastLineNoContentCache &&
      LastLineNoContentCache->SourceLineCache &&
      LastLineNoResult < LastLineNoContentCache->SourceLineCache.size()) {
    const unsigned *SourceLineCache =
        LastLineNoContentCache->SourceLineCache.begin();
    unsigned LineStart = SourceLineCache[LastLineNoResult - 1];
    unsigned LineEnd = SourceLineCache[LastLineNoResult];
    if (FilePos >= LineStart && FilePos < LineEnd) {
      if (LLVM_UNLIKELY(FilePos + 1 == LineEnd && FilePos > LineStart)) {
        auto MemBuf = getBufferOrNone(FID);
        if (MemBuf) {
          const char *Buf = MemBuf->getBufferStart();
          if (Buf[FilePos - 1] == '\r' || Buf[FilePos - 1] == '\n')
            --FilePos;
        }
      }
      if (Invalid)
        *Invalid = false;
      return FilePos - LineStart + 1;
    }
  }

  std::optional<llvm::MemoryBufferRef> MemBuf = getBufferOrNone(FID);
  if (Invalid)
    *Invalid = !MemBuf;

  if (!MemBuf)
    return 1;

  if (FilePos > MemBuf->getBufferSize()) {
    if (Invalid)
      *Invalid = true;
    return 1;
  }

  const char *Buf = MemBuf->getBufferStart();
  unsigned LineStart = FilePos;
  while (LineStart && Buf[LineStart - 1] != '\n' && Buf[LineStart - 1] != '\r')
    --LineStart;
  return FilePos - LineStart + 1;
}

// isInvalid - Return the result of calling loc.isInvalid(), and
// if Invalid is not null, set its value to same.
template <typename LocType> static bool isInvalid(LocType Loc, bool *Invalid) {
  bool MyInvalid = Loc.isInvalid();
  if (Invalid)
    *Invalid = MyInvalid;
  return MyInvalid;
}

unsigned SourceManager::getSpellingColumnNumber(SourceLocation Loc,
                                                bool *Invalid) const {
  if (isInvalid(Loc, Invalid))
    return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(Loc);
  return getColumnNumber(LocInfo.first, LocInfo.second, Invalid);
}

unsigned SourceManager::getExpansionColumnNumber(SourceLocation Loc,
                                                 bool *Invalid) const {
  if (isInvalid(Loc, Invalid))
    return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);
  return getColumnNumber(LocInfo.first, LocInfo.second, Invalid);
}

unsigned SourceManager::getPresumedColumnNumber(SourceLocation Loc,
                                                bool *Invalid) const {
  PresumedLoc PLoc = getPresumedLoc(Loc);
  if (isInvalid(PLoc, Invalid))
    return 0;
  return PLoc.getColumn();
}

// Check if mutli-byte word x has bytes between m and n, included. This may also
// catch bytes equal to n + 1.
// The returned value holds a 0x80 at each byte position that holds a match.
// see http://graphics.stanford.edu/~seander/bithacks.html#HasBetweenInWord
namespace {
template <class T>
constexpr inline T likelyhasbetween(T x, unsigned char m, unsigned char n) {
  return ((x - ~static_cast<T>(0) / 255 * (n + 1)) & ~x &
          ((x & ~static_cast<T>(0) / 255 * 127) +
           (~static_cast<T>(0) / 255 * (127 - (m - 1))))) &
         ~static_cast<T>(0) / 255 * 128;
}
} // namespace

LineOffsetMapping LineOffsetMapping::get(llvm::MemoryBufferRef Buffer,
                                         llvm::BumpPtrAllocator &Alloc) {
  unsigned BufSize = Buffer.getBufferSize();
  llvm::SmallVector<unsigned, 256> LineOffsets;
  LineOffsets.reserve(BufSize / 32 + 2);
  LineOffsets.push_back(0);

  const unsigned char *Start = (const unsigned char *)Buffer.getBufferStart();
  const unsigned char *End = (const unsigned char *)Buffer.getBufferEnd();
  const unsigned char *Buf = Start;
  bool PendingCR = false;

#ifdef __AVX512BW__
  {
    const __m512i NLV = _mm512_set1_epi8('\n');
    const __m512i CRV = _mm512_set1_epi8('\r');
    while (Buf + 64 <= End) {
      __m512i V = _mm512_loadu_si512(Buf);
      __mmask64 NLMask = _mm512_cmpeq_epi8_mask(V, NLV);
      __mmask64 CRMask = _mm512_cmpeq_epi8_mask(V, CRV);
      uint64_t Combined = NLMask | CRMask;
      if (Combined == 0) {
        PendingCR = false;
        Buf += 64;
        continue;
      }
      while (Combined != 0) {
        unsigned Pos = _tzcnt_u64(Combined);
        Combined &= Combined - 1;
        unsigned char Byte = Buf[Pos];
        if (PendingCR && Pos == 0 && Byte == '\n') {
          LineOffsets.back() += 1;
          PendingCR = false;
          continue;
        }
        PendingCR = false;
        if (Byte == '\r' && Pos + 1 < 64 && Buf[Pos + 1] == '\n') {
          Combined &= ~(1ULL << (Pos + 1));
          LineOffsets.push_back((Buf + Pos + 2) - Start);
        } else {
          if (Byte == '\r' && Pos == 63)
            PendingCR = true;
          LineOffsets.push_back((Buf + Pos + 1) - Start);
        }
      }
      Buf += 64;
    }
  }
#endif
#if defined(__AVX512BW__) || defined(__AVX2__)
  {
    const __m256i NLV = _mm256_set1_epi8('\n');
    const __m256i CRV = _mm256_set1_epi8('\r');
    while (Buf + 32 <= End) {
      __m256i V = _mm256_loadu_si256((const __m256i *)Buf);
      unsigned NLMask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(V, NLV));
      unsigned CRMask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(V, CRV));
      unsigned Combined = NLMask | CRMask;
      if (Combined == 0) {
        PendingCR = false;
        Buf += 32;
        continue;
      }
      while (Combined != 0) {
        unsigned Pos = llvm::countr_zero(Combined);
        Combined &= Combined - 1;
        unsigned char Byte = Buf[Pos];
        if (PendingCR && Pos == 0 && Byte == '\n') {
          LineOffsets.back() += 1;
          PendingCR = false;
          continue;
        }
        PendingCR = false;
        if (Byte == '\r' && Pos + 1 < 32 && Buf[Pos + 1] == '\n') {
          Combined &= ~(1u << (Pos + 1));
          LineOffsets.push_back((Buf + Pos + 2) - Start);
        } else {
          if (Byte == '\r' && Pos == 31)
            PendingCR = true;
          LineOffsets.push_back((Buf + Pos + 1) - Start);
        }
      }
      Buf += 32;
    }
  }
#elif defined(__SSE2__)
  const __m128i NLV = _mm_set1_epi8('\n');
  const __m128i CRV = _mm_set1_epi8('\r');
  while (Buf + 16 <= End) {
    __m128i V = _mm_loadu_si128((const __m128i *)Buf);
    int NLMask = _mm_movemask_epi8(_mm_cmpeq_epi8(V, NLV));
    int CRMask = _mm_movemask_epi8(_mm_cmpeq_epi8(V, CRV));
    int Combined = NLMask | CRMask;
    if (Combined == 0) {
      PendingCR = false;
      Buf += 16;
      continue;
    }
    while (Combined != 0) {
      unsigned Pos = llvm::countr_zero<unsigned>(Combined);
      Combined &= Combined - 1;
      unsigned char Byte = Buf[Pos];
      if (PendingCR && Pos == 0 && Byte == '\n') {
        LineOffsets.back() += 1;
        PendingCR = false;
        continue;
      }
      PendingCR = false;
      if (Byte == '\r' && Pos + 1 < 16 && Buf[Pos + 1] == '\n') {
        Combined &= ~(1 << (Pos + 1));
        LineOffsets.push_back((Buf + Pos + 2) - Start);
      } else {
        if (Byte == '\r' && Pos == 15)
          PendingCR = true;
        LineOffsets.push_back((Buf + Pos + 1) - Start);
      }
    }
    Buf += 16;
  }
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    const uint8x16_t NLV = vdupq_n_u8('\n');
    const uint8x16_t CRV = vdupq_n_u8('\r');
    auto extractNL = [&](const unsigned char *Base,
                         uint8x16_t V) __attribute__((always_inline)) {
      uint8x16_t NLHit = vceqq_u8(V, NLV);
      uint8x16_t CRHit = vceqq_u8(V, CRV);
      if (LLVM_LIKELY(vmaxvq_u8(CRHit) == 0)) {
        uint64x2_t As64 = vreinterpretq_u64_u8(NLHit);
        uint64_t Lo = vgetq_lane_u64(As64, 0);
        uint64_t Hi = vgetq_lane_u64(As64, 1);
        while (Lo) {
          unsigned Pos = static_cast<unsigned>(__builtin_ctzll(Lo)) >> 3;
          LineOffsets.push_back(static_cast<unsigned>(Base + Pos + 1 - Start));
          Lo ^= 0xFFULL << (Pos * 8);
        }
        while (Hi) {
          unsigned Pos = 8u + (static_cast<unsigned>(__builtin_ctzll(Hi)) >> 3);
          LineOffsets.push_back(static_cast<unsigned>(Base + Pos + 1 - Start));
          Hi ^= 0xFFULL << ((Pos - 8u) * 8);
        }
      } else {
        for (unsigned I = 0; I < 16; ++I) {
          unsigned char Byte = Base[I];
          if (PendingCR && Byte == '\n') {
            LineOffsets.back() += 1;
            PendingCR = false;
            continue;
          }
          PendingCR = false;
          if (Byte == '\n') {
            LineOffsets.push_back(static_cast<unsigned>(Base + I + 1 - Start));
          } else if (Byte == '\r') {
            if (I + 1 < 16 && Base[I + 1] == '\n')
              ++I;
            else if (I == 15)
              PendingCR = true;
            LineOffsets.push_back(static_cast<unsigned>(Base + I + 1 - Start));
          }
        }
      }
    };
    while (Buf + 64 <= End) {
      uint8x16_t V0 = vld1q_u8(Buf);
      uint8x16_t V1 = vld1q_u8(Buf + 16);
      uint8x16_t V2 = vld1q_u8(Buf + 32);
      uint8x16_t V3 = vld1q_u8(Buf + 48);
      uint8x16_t Any =
          vorrq_u8(vorrq_u8(vorrq_u8(vceqq_u8(V0, NLV), vceqq_u8(V0, CRV)),
                            vorrq_u8(vceqq_u8(V1, NLV), vceqq_u8(V1, CRV))),
                   vorrq_u8(vorrq_u8(vceqq_u8(V2, NLV), vceqq_u8(V2, CRV)),
                            vorrq_u8(vceqq_u8(V3, NLV), vceqq_u8(V3, CRV))));
      if (vmaxvq_u8(Any) == 0) {
        PendingCR = false;
        Buf += 64;
        continue;
      }
      extractNL(Buf, V0);
      extractNL(Buf + 16, V1);
      extractNL(Buf + 32, V2);
      extractNL(Buf + 48, V3);
      Buf += 64;
    }
    while (Buf + 16 <= End) {
      uint8x16_t V = vld1q_u8(Buf);
      if (vmaxvq_u8(vorrq_u8(vceqq_u8(V, NLV), vceqq_u8(V, CRV))) == 0) {
        PendingCR = false;
        Buf += 16;
        continue;
      }
      extractNL(Buf, V);
      Buf += 16;
    }
  }
#else
  uint64_t Word;
  if ((unsigned long)(End - Start) > sizeof(Word)) {
    do {
      Word = llvm::support::endian::read64(Buf, llvm::endianness::little);
      auto Mask = likelyhasbetween(Word, '\n', '\r');
      if (!Mask) {
        Buf += sizeof(Word);
        continue;
      }
      unsigned N = llvm::countr_zero(Mask) - 7;
      Word >>= N;
      Buf += N / 8 + 1;
      unsigned char Byte = Word;
      switch (Byte) {
      case '\r':
        if (*Buf == '\n')
          ++Buf;
        [[fallthrough]];
      case '\n':
        LineOffsets.push_back(Buf - Start);
      };
    } while (Buf < End - sizeof(Word) - 1);
  }
#endif

  while (Buf < End) {
    if (PendingCR && *Buf == '\n') {
      LineOffsets.back() += 1;
      PendingCR = false;
      ++Buf;
      continue;
    }
    PendingCR = false;
    if (*Buf == '\n') {
      LineOffsets.push_back(Buf - Start + 1);
    } else if (*Buf == '\r') {
      if (Buf + 1 < End && Buf[1] == '\n')
        ++Buf;
      LineOffsets.push_back(Buf - Start + 1);
    }
    ++Buf;
  }

  return LineOffsetMapping(LineOffsets, Alloc);
}

LineOffsetMapping::LineOffsetMapping(llvm::ArrayRef<unsigned> LineOffsets,
                                     llvm::BumpPtrAllocator &Alloc)
    : Storage(Alloc.Allocate<unsigned>(LineOffsets.size() + 1)) {
  Storage[0] = LineOffsets.size();
  std::copy(LineOffsets.begin(), LineOffsets.end(), Storage + 1);
}

unsigned SourceManager::getLineNumber(FileID FID, unsigned FilePos,
                                      bool *Invalid) const {
  if (FID.isInvalid()) {
    if (Invalid)
      *Invalid = true;
    return 1;
  }

  const ContentCache *Content;
  if (LastLineNoFileIDQuery == FID)
    Content = LastLineNoContentCache;
  else {
    bool MyInvalid = false;
    const SLocEntry &Entry = getSLocEntry(FID, &MyInvalid);
    if (MyInvalid || !Entry.isFile()) {
      if (Invalid)
        *Invalid = true;
      return 1;
    }

    Content = &Entry.getFile().getContentCache();
  }

  // If this is the first use of line information for this buffer, compute the
  // SourceLineCache for it on demand.
  if (!Content->SourceLineCache) {
    std::optional<llvm::MemoryBufferRef> Buffer =
        Content->getBufferOrNone(Diag, getFileManager(), SourceLocation());
    if (Invalid)
      *Invalid = !Buffer;
    if (!Buffer)
      return 1;

    Content->SourceLineCache =
        LineOffsetMapping::get(*Buffer, ContentCacheAlloc);
  } else if (Invalid)
    *Invalid = false;

  // Okay, we know we have a line number table.  Do a binary search to find the
  // line number that this character position lands on.
  const unsigned *SourceLineCache = Content->SourceLineCache.begin();
  const unsigned *SourceLineCacheStart = SourceLineCache;
  const unsigned *SourceLineCacheEnd = Content->SourceLineCache.end();

  unsigned QueriedFilePos = FilePos + 1;

  if (LastLineNoFileIDQuery == FID) {
    if (QueriedFilePos >= LastLineNoFilePos) {
      SourceLineCache = SourceLineCache + LastLineNoResult - 1;

      // Galloping search: 1, 2, 4, 8, 16, 32 ... stride doubling
      // converges to O(log d) where d = distance from last query.
      unsigned Stride = 1;
      while (SourceLineCache + Stride < SourceLineCacheEnd &&
             SourceLineCache[Stride] <= QueriedFilePos) {
        Stride <<= 1;
      }
      SourceLineCacheEnd =
          std::min(SourceLineCache + Stride, SourceLineCacheEnd);
      if (Stride > 1)
        SourceLineCache += Stride >> 1;
    } else {
      if (LastLineNoResult < Content->SourceLineCache.size())
        SourceLineCacheEnd = SourceLineCache + LastLineNoResult + 1;
    }
  }

  // Branchless binary search (Eytzinger-style lower_bound variant).
  unsigned Len = SourceLineCacheEnd - SourceLineCache;
  while (Len > 8) {
    unsigned Half = Len >> 1;
    // cmov instead of branch — better on modern superscalar pipelines.
    SourceLineCache += (SourceLineCache[Half] < QueriedFilePos) ? Half : 0;
    Len -= Half;
  }
  const unsigned *Pos = SourceLineCache;
  while (Pos < SourceLineCache + Len && *Pos < QueriedFilePos)
    ++Pos;
  unsigned LineNo = Pos - SourceLineCacheStart;

  LastLineNoFileIDQuery = FID;
  LastLineNoContentCache = Content;
  LastLineNoFilePos = QueriedFilePos;
  LastLineNoResult = LineNo;
  return LineNo;
}

unsigned SourceManager::getSpellingLineNumber(SourceLocation Loc,
                                              bool *Invalid) const {
  if (isInvalid(Loc, Invalid))
    return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(Loc);
  return getLineNumber(LocInfo.first, LocInfo.second);
}
unsigned SourceManager::getExpansionLineNumber(SourceLocation Loc,
                                               bool *Invalid) const {
  if (isInvalid(Loc, Invalid))
    return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);
  return getLineNumber(LocInfo.first, LocInfo.second);
}
unsigned SourceManager::getPresumedLineNumber(SourceLocation Loc,
                                              bool *Invalid) const {
  PresumedLoc PLoc = getPresumedLoc(Loc);
  if (isInvalid(PLoc, Invalid))
    return 0;
  return PLoc.getLine();
}

SrcMgr::CharacteristicKind
SourceManager::getFileCharacteristic(SourceLocation Loc) const {
  assert(Loc.isValid() && "Can't get file characteristic of invalid loc!");
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);
  const SLocEntry *SEntry = getSLocEntryForFile(LocInfo.first);
  if (!SEntry)
    return C_User;

  const SrcMgr::FileInfo &FI = SEntry->getFile();

  // If there are no #line directives in this file, just return the whole-file
  // state.
  if (!FI.hasLineDirectives())
    return FI.getFileCharacteristic();

  assert(LineTable && "Can't have linetable entries without a LineTable!");
  // See if there is a #line directive before the location.
  const LineEntry *Entry =
      LineTable->FindNearestLineEntry(LocInfo.first, LocInfo.second);

  // If this is before the first line marker, use the file characteristic.
  if (!Entry)
    return FI.getFileCharacteristic();

  return Entry->FileKind;
}

llvm::StringRef SourceManager::getBufferName(SourceLocation Loc,
                                             bool *Invalid) const {
  if (isInvalid(Loc, Invalid))
    return "<invalid loc>";

  auto B = getBufferOrNone(getFileID(Loc));
  if (Invalid)
    *Invalid = !B;
  return B ? B->getBufferIdentifier() : "<invalid buffer>";
}

PresumedLoc SourceManager::getPresumedLoc(SourceLocation Loc,
                                          bool UseLineDirectives) const {
  if (Loc.isInvalid())
    return PresumedLoc();

  // Presumed locations are always for expansion points.
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);

  bool Invalid = false;
  const SLocEntry &Entry = getSLocEntry(LocInfo.first, &Invalid);
  if (Invalid || !Entry.isFile())
    return PresumedLoc();

  const SrcMgr::FileInfo &FI = Entry.getFile();
  const SrcMgr::ContentCache *C = &FI.getContentCache();

  // To get the source name, first consult the FileEntry (if one exists)
  // before the MemBuffer as this will avoid unnecessarily paging in the
  // MemBuffer.
  FileID FID = LocInfo.first;
  llvm::StringRef Filename;
  if (C->OrigEntry)
    Filename = C->OrigEntry->getName();
  else if (auto Buffer = C->getBufferOrNone(Diag, getFileManager()))
    Filename = Buffer->getBufferIdentifier();

  unsigned LineNo = getLineNumber(LocInfo.first, LocInfo.second, &Invalid);
  if (Invalid)
    return PresumedLoc();
  unsigned ColNo = getColumnNumber(LocInfo.first, LocInfo.second, &Invalid);
  if (Invalid)
    return PresumedLoc();

  SourceLocation IncludeLoc = FI.getIncludeLoc();

  // If we have #line directives in this file, update and overwrite the physical
  // location info if appropriate.
  if (UseLineDirectives && FI.hasLineDirectives()) {
    assert(LineTable && "Can't have linetable entries without a LineTable!");
    // See if there is a #line directive before this.  If so, get it.
    if (const LineEntry *Entry =
            LineTable->FindNearestLineEntry(LocInfo.first, LocInfo.second)) {
      // If the LineEntry indicates a filename, use it.
      if (Entry->FilenameID != -1) {
        Filename = LineTable->getFilename(Entry->FilenameID);
        // The contents of files referenced by #line are not in the
        // SourceManager
        FID = FileID::get(0);
      }

      // Use the line number specified by the LineEntry.  This line number may
      // be multiple lines down from the line entry.  Add the difference in
      // physical line numbers from the query point and the line marker to the
      // total.
      unsigned MarkerLineNo = getLineNumber(LocInfo.first, Entry->FileOffset);
      LineNo = Entry->LineNo + (LineNo - MarkerLineNo - 1);

      // Note that column numbers are not molested by line markers.

      // Handle virtual #include manipulation.
      if (Entry->IncludeOffset) {
        IncludeLoc = getLocForStartOfFile(LocInfo.first);
        IncludeLoc = IncludeLoc.getLocWithOffset(Entry->IncludeOffset);
      }
    }
  }

  return PresumedLoc(Filename.data(), FID, LineNo, ColNo, IncludeLoc);
}

bool SourceManager::isInMainFile(SourceLocation Loc) const {
  if (Loc.isInvalid())
    return false;

  // Presumed locations are always for expansion points.
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);

  const SLocEntry *Entry = getSLocEntryForFile(LocInfo.first);
  if (!Entry)
    return false;

  const SrcMgr::FileInfo &FI = Entry->getFile();

  if (FI.hasLineDirectives())
    if (const LineEntry *Entry =
            LineTable->FindNearestLineEntry(LocInfo.first, LocInfo.second))
      if (Entry->IncludeOffset)
        return false;

  return FI.getIncludeLoc().isInvalid();
}

unsigned SourceManager::getFileIDSize(FileID FID) const {
  bool Invalid = false;
  const SrcMgr::SLocEntry &Entry = getSLocEntry(FID, &Invalid);
  if (Invalid)
    return 0;

  int ID = FID.ID;
  SourceLocation::UIntTy NextOffset;
  if ((ID > 0 && unsigned(ID + 1) == local_sloc_entry_size()))
    NextOffset = getNextLocalOffset();
  else if (ID + 1 == -1)
    NextOffset = MaxLoadedOffset;
  else
    NextOffset = getSLocEntry(FileID::get(ID + 1)).getOffset();

  return NextOffset - Entry.getOffset() - 1;
}

SourceLocation SourceManager::translateFileLineCol(const FileEntry *SourceFile,
                                                   unsigned Line,
                                                   unsigned Col) const {
  assert(SourceFile && "Null source file!");
  assert(Line && Col && "Line and column should start from 1!");

  FileID FirstFID = translateFile(SourceFile);
  return translateLineCol(FirstFID, Line, Col);
}

FileID SourceManager::translateFile(const FileEntry *SourceFile) const {
  assert(SourceFile && "Null source file!");

  // First, check the main file ID, since it is common to look for a
  // location in the main file.
  if (MainFileID.isValid()) {
    bool Invalid = false;
    const SLocEntry &MainSLoc = getSLocEntry(MainFileID, &Invalid);
    if (Invalid)
      return FileID();

    if (MainSLoc.isFile()) {
      if (MainSLoc.getFile().getContentCache().OrigEntry == SourceFile)
        return MainFileID;
    }
  }

  // The location we're looking for isn't in the main file; look
  // through all of the local source locations.
  for (unsigned I = 0, N = local_sloc_entry_size(); I != N; ++I) {
    const SLocEntry &SLoc = getLocalSLocEntry(I);
    if (SLoc.isFile() &&
        SLoc.getFile().getContentCache().OrigEntry == SourceFile)
      return FileID::get(I);
  }

  // If that still didn't help, try the modules.
  for (unsigned I = 0, N = loaded_sloc_entry_size(); I != N; ++I) {
    const SLocEntry &SLoc = getLoadedSLocEntry(I);
    if (SLoc.isFile() &&
        SLoc.getFile().getContentCache().OrigEntry == SourceFile)
      return FileID::get(-int(I) - 2);
  }

  return FileID();
}

SourceLocation SourceManager::translateLineCol(FileID FID, unsigned Line,
                                               unsigned Col) const {
  // Lines are used as a one-based index into a zero-based array. This assert
  // checks for possible buffer underruns.
  assert(Line && Col && "Line and column should start from 1!");

  if (FID.isInvalid())
    return SourceLocation();

  bool Invalid = false;
  const SLocEntry &Entry = getSLocEntry(FID, &Invalid);
  if (Invalid)
    return SourceLocation();

  if (!Entry.isFile())
    return SourceLocation();

  SourceLocation FileLoc = SourceLocation::getFileLoc(Entry.getOffset());

  if (Line == 1 && Col == 1)
    return FileLoc;

  const ContentCache *Content = &Entry.getFile().getContentCache();

  // If this is the first use of line information for this buffer, compute the
  // SourceLineCache for it on demand.
  std::optional<llvm::MemoryBufferRef> Buffer =
      Content->getBufferOrNone(Diag, getFileManager());
  if (!Buffer)
    return SourceLocation();
  if (!Content->SourceLineCache)
    Content->SourceLineCache =
        LineOffsetMapping::get(*Buffer, ContentCacheAlloc);

  if (Line > Content->SourceLineCache.size()) {
    unsigned Size = Buffer->getBufferSize();
    if (Size > 0)
      --Size;
    return FileLoc.getLocWithOffset(Size);
  }

  unsigned FilePos = Content->SourceLineCache[Line - 1];
  const char *Buf = Buffer->getBufferStart() + FilePos;
  unsigned BufLength = Buffer->getBufferSize() - FilePos;
  if (BufLength == 0)
    return FileLoc.getLocWithOffset(FilePos);

  unsigned i = 0;

  while (i < BufLength - 1 && i < Col - 1 && Buf[i] != '\n' && Buf[i] != '\r')
    ++i;
  return FileLoc.getLocWithOffset(FilePos + i);
}

void SourceManager::computeMacroArgsCache(MacroArgsMap &MacroArgsCache,
                                          FileID FID) const {
  assert(FID.isValid());

  // Initially no macro argument chunk is present.
  MacroArgsCache.insert(std::make_pair(0, SourceLocation()));

  int ID = FID.ID;
  while (true) {
    ++ID;
    // Stop if there are no more FileIDs to check.
    if (ID > 0) {
      if (unsigned(ID) >= local_sloc_entry_size())
        return;
    } else if (ID == -1) {
      return;
    }

    bool Invalid = false;
    const SrcMgr::SLocEntry &Entry = getSLocEntryByID(ID, &Invalid);
    if (Invalid)
      return;
    if (Entry.isFile()) {
      auto &File = Entry.getFile();

      SourceLocation IncludeLoc = File.getIncludeLoc();
      bool IncludedInFID =
          (IncludeLoc.isValid() && isInFileID(IncludeLoc, FID)) ||
          // Predefined header doesn't have a valid include location in main
          // file, but any files created by it should still be skipped when
          // computing macro args expanded in the main file.
          (FID == MainFileID && Entry.getFile().getName() == "<built-in>");
      if (IncludedInFID) {
        // Skip the files/macros of the #include'd file, we only care about
        // macros that lexed macro arguments from our file.
        if (Entry.getFile().NumCreatedFIDs)
          ID += Entry.getFile().NumCreatedFIDs - 1 /*because of next ++ID*/;
        continue;
      }
      // If file was included but not from FID, there is no more files/macros
      // that may be "contained" in this file.
      if (IncludeLoc.isValid())
        return;
      continue;
    }

    const ExpansionInfo &ExpInfo = Entry.getExpansion();

    if (ExpInfo.getExpansionLocStart().isFileID()) {
      if (!isInFileID(ExpInfo.getExpansionLocStart(), FID))
        return; // No more files/macros that may be "contained" in this file.
    }

    if (!ExpInfo.isMacroArgExpansion())
      continue;

    associateFileChunkWithMacroArgExp(
        MacroArgsCache, FID, ExpInfo.getSpellingLoc(),
        SourceLocation::getMacroLoc(Entry.getOffset()),
        getFileIDSize(FileID::get(ID)));
  }
}

void SourceManager::associateFileChunkWithMacroArgExp(
    MacroArgsMap &MacroArgsCache, FileID FID, SourceLocation SpellLoc,
    SourceLocation ExpansionLoc, unsigned ExpansionLength) const {
  if (!SpellLoc.isFileID()) {
    SourceLocation::UIntTy SpellBeginOffs = SpellLoc.getOffset();
    SourceLocation::UIntTy SpellEndOffs = SpellBeginOffs + ExpansionLength;

    // The spelling range for this macro argument expansion can span multiple
    // consecutive FileID entries. Go through each entry contained in the
    // spelling range and if one is itself a macro argument expansion, recurse
    // and associate the file chunk that it represents.

    FileID SpellFID; // Current FileID in the spelling range.
    unsigned SpellRelativeOffs;
    std::tie(SpellFID, SpellRelativeOffs) = getDecomposedLoc(SpellLoc);
    while (true) {
      const SLocEntry &Entry = getSLocEntry(SpellFID);
      SourceLocation::UIntTy SpellFIDBeginOffs = Entry.getOffset();
      unsigned SpellFIDSize = getFileIDSize(SpellFID);
      SourceLocation::UIntTy SpellFIDEndOffs = SpellFIDBeginOffs + SpellFIDSize;
      const ExpansionInfo &Info = Entry.getExpansion();
      if (Info.isMacroArgExpansion()) {
        unsigned CurrSpellLength;
        if (SpellFIDEndOffs < SpellEndOffs)
          CurrSpellLength = SpellFIDSize - SpellRelativeOffs;
        else
          CurrSpellLength = ExpansionLength;
        associateFileChunkWithMacroArgExp(
            MacroArgsCache, FID,
            Info.getSpellingLoc().getLocWithOffset(SpellRelativeOffs),
            ExpansionLoc, CurrSpellLength);
      }

      if (SpellFIDEndOffs >= SpellEndOffs)
        return; // we covered all FileID entries in the spelling range.

      // Move to the next FileID entry in the spelling range.
      unsigned advance = SpellFIDSize - SpellRelativeOffs + 1;
      ExpansionLoc = ExpansionLoc.getLocWithOffset(advance);
      ExpansionLength -= advance;
      ++SpellFID.ID;
      SpellRelativeOffs = 0;
    }
  }

  assert(SpellLoc.isFileID());

  unsigned BeginOffs;
  if (!isInFileID(SpellLoc, FID, &BeginOffs))
    return;

  unsigned EndOffs = BeginOffs + ExpansionLength;

  // Add a new chunk for this macro argument. A previous macro argument chunk
  // may have been lexed again, so e.g. if the map is
  //     0   -> SourceLocation()
  //     100 -> Expanded loc #1
  //     110 -> SourceLocation()
  // and we found a new macro FileID that lexed from offset 105 with length 3,
  // the new map will be:
  //     0   -> SourceLocation()
  //     100 -> Expanded loc #1
  //     105 -> Expanded loc #2
  //     108 -> Expanded loc #1
  //     110 -> SourceLocation()
  //
  // Since re-lexed macro chunks will always be the same size or less of
  // previous chunks, we only need to find where the ending of the new macro
  // chunk is mapped to and update the map with new begin/end mappings.

  MacroArgsMap::iterator I = MacroArgsCache.upper_bound(EndOffs);
  --I;
  SourceLocation EndOffsMappedLoc = I->second;
  MacroArgsCache[BeginOffs] = ExpansionLoc;
  MacroArgsCache[EndOffs] = EndOffsMappedLoc;
}

SourceLocation
SourceManager::getMacroArgExpandedLocation(SourceLocation Loc) const {
  if (Loc.isInvalid() || !Loc.isFileID())
    return Loc;

  FileID FID;
  unsigned Offset;
  std::tie(FID, Offset) = getDecomposedLoc(Loc);
  if (FID.isInvalid())
    return Loc;

  std::unique_ptr<MacroArgsMap> &MacroArgsCache = MacroArgsCacheMap[FID];
  if (!MacroArgsCache) {
    MacroArgsCache = std::make_unique<MacroArgsMap>();
    computeMacroArgsCache(*MacroArgsCache, FID);
  }

  assert(!MacroArgsCache->empty());
  MacroArgsMap::iterator I = MacroArgsCache->upper_bound(Offset);
  // In case every element in MacroArgsCache is greater than Offset we can't
  // decrement the iterator.
  if (I == MacroArgsCache->begin())
    return Loc;

  --I;

  SourceLocation::UIntTy MacroArgBeginOffs = I->first;
  SourceLocation MacroArgExpandedLoc = I->second;
  if (MacroArgExpandedLoc.isValid())
    return MacroArgExpandedLoc.getLocWithOffset(Offset - MacroArgBeginOffs);

  return Loc;
}

std::pair<FileID, unsigned>
SourceManager::getDecomposedIncludedLoc(FileID FID) const {
  if (FID.isInvalid())
    return std::make_pair(FileID(), 0);

  // Uses IncludedLocMap to retrieve/cache the decomposed loc.

  using DecompTy = std::pair<FileID, unsigned>;
  auto InsertOp = IncludedLocMap.try_emplace(FID);
  DecompTy &DecompLoc = InsertOp.first->second;
  if (!InsertOp.second)
    return DecompLoc; // already in map.

  SourceLocation UpperLoc;
  bool Invalid = false;
  const SrcMgr::SLocEntry &Entry = getSLocEntry(FID, &Invalid);
  if (!Invalid) {
    if (Entry.isExpansion())
      UpperLoc = Entry.getExpansion().getExpansionLocStart();
    else
      UpperLoc = Entry.getFile().getIncludeLoc();
  }

  if (UpperLoc.isValid())
    DecompLoc = getDecomposedLoc(UpperLoc);

  return DecompLoc;
}

bool SourceManager::isInTheSameTranslationUnitImpl(
    const std::pair<FileID, unsigned> &LOffs,
    const std::pair<FileID, unsigned> &ROffs) const {
  // If one is local while the other is loaded.
  if (isLoadedFileID(LOffs.first) != isLoadedFileID(ROffs.first))
    return false;

  if (isLoadedFileID(LOffs.first) && isLoadedFileID(ROffs.first)) {
    auto FindSLocEntryAlloc = [this](FileID FID) {
      // Loaded FileIDs are negative, we store the lowest FileID from each
      // allocation, later allocations have lower FileIDs.
      return llvm::lower_bound(LoadedSLocEntryAllocBegin, FID,
                               std::greater<FileID>{});
    };

    // If both are loaded from different AST files.
    if (FindSLocEntryAlloc(LOffs.first) != FindSLocEntryAlloc(ROffs.first))
      return false;
  }

  return true;
}

namespace {
bool MoveUpTranslationUnitIncludeHierarchy(std::pair<FileID, unsigned> &Loc,
                                           const SourceManager &SM) {
  std::pair<FileID, unsigned> UpperLoc = SM.getDecomposedIncludedLoc(Loc.first);
  if (UpperLoc.first.isInvalid() ||
      !SM.isInTheSameTranslationUnitImpl(UpperLoc, Loc))
    return true; // We reached the top.

  Loc = UpperLoc;
  return false;
}
} // namespace

InBeforeInTUCacheEntry &SourceManager::getInBeforeInTUCache(FileID LFID,
                                                            FileID RFID) const {
  // This is a magic number for limiting the cache size.  It was experimentally
  // derived from a small project (where the cache filled out to ~250 items).
  // We can make it larger if necessary.
  enum { MagicCacheSize = 300 };
  IsBeforeInTUCacheKey Key(LFID, RFID);

  // If the cache size isn't too large, do a lookup and if necessary default
  // construct an entry.  We can then return it to the caller for direct
  // use.  When they update the value, the cache will get automatically
  // updated as well.
  if (IBTUCache.size() < MagicCacheSize)
    return IBTUCache.try_emplace(Key, LFID, RFID).first->second;

  // Otherwise, do a lookup that will not construct a new value.
  InBeforeInTUCache::iterator I = IBTUCache.find(Key);
  if (I != IBTUCache.end())
    return I->second;

  // Fall back to the overflow value.
  IBTUCacheOverflow.setQueryFIDs(LFID, RFID);
  return IBTUCacheOverflow;
}

// ===----------------------------------------------------------------------===
// SourceManager — ordering & translation unit
// ===----------------------------------------------------------------------===

bool SourceManager::isBeforeInTranslationUnit(SourceLocation LHS,
                                              SourceLocation RHS) const {
  assert(LHS.isValid() && RHS.isValid() && "Passed invalid source location!");
  if (LHS == RHS)
    return false;

  std::pair<FileID, unsigned> LOffs = getDecomposedLoc(LHS);
  std::pair<FileID, unsigned> ROffs = getDecomposedLoc(RHS);

  // getDecomposedLoc may have failed to return a valid FileID.
  if (LOffs.first.isInvalid() || ROffs.first.isInvalid())
    return LOffs.first.isInvalid() && !ROffs.first.isInvalid();

  std::pair<bool, bool> InSameTU = isInTheSameTranslationUnit(LOffs, ROffs);
  if (InSameTU.first)
    return InSameTU.second;
  return LOffs.first < ROffs.first;
}

std::pair<bool, bool> SourceManager::isInTheSameTranslationUnit(
    std::pair<FileID, unsigned> &LOffs,
    std::pair<FileID, unsigned> &ROffs) const {
  // If the source locations are not in the same TU, return early.
  if (!isInTheSameTranslationUnitImpl(LOffs, ROffs))
    return std::make_pair(false, false);

  // If the source locations are in the same file, just compare offsets.
  if (LOffs.first == ROffs.first)
    return std::make_pair(true, LOffs.second < ROffs.second);

  // If we are comparing a source location with multiple locations in the same
  // file, we get a big win by caching the result.
  InBeforeInTUCacheEntry &IsBeforeInTUCache =
      getInBeforeInTUCache(LOffs.first, ROffs.first);

  // If we are comparing a source location with multiple locations in the same
  // file, we get a big win by caching the result.
  if (IsBeforeInTUCache.isCacheValid())
    return std::make_pair(
        true, IsBeforeInTUCache.getCachedResult(LOffs.second, ROffs.second));

  // Okay, we missed in the cache, we'll compute the answer and populate it.
  // We need to find the common ancestor. The only way of doing this is to
  // build the complete include chain for one and then walking up the chain
  // of the other looking for a match.

  // A location within a FileID on the path up from LOffs to the main file.
  struct Entry {
    std::pair<FileID, unsigned> DecomposedLoc; // FileID redundant, but clearer.
    FileID ChildFID; // Used for breaking ties. Invalid for the initial loc.
  };
  llvm::SmallDenseMap<FileID, Entry, 16> LChain;

  FileID LChild;
  do {
    LChain.try_emplace(LOffs.first, Entry{LOffs, LChild});
    // We catch the case where LOffs is in a file included by ROffs and
    // quit early. The other way round unfortunately remains suboptimal.
    if (LOffs.first == ROffs.first)
      break;
    LChild = LOffs.first;
  } while (!MoveUpTranslationUnitIncludeHierarchy(LOffs, *this));

  FileID RChild;
  do {
    auto LIt = LChain.find(ROffs.first);
    if (LIt != LChain.end()) {
      // Compare the locations within the common file and cache them.
      LOffs = LIt->second.DecomposedLoc;
      LChild = LIt->second.ChildFID;
      // The relative order of LChild and RChild is a tiebreaker when
      // - locs expand to the same location (occurs in macro arg expansion)
      // - one loc is a parent of the other (we consider the parent as "first")
      // For the parent entry to be first, its invalid child file ID must
      // compare smaller to the valid child file ID of the other entry.
      // However loaded FileIDs are <0, so we perform *unsigned* comparison!
      // This changes the relative order of local vs loaded FileIDs, but it
      // doesn't matter as these are never mixed in macro expansion.
      unsigned LChildID = LChild.ID;
      unsigned RChildID = RChild.ID;
      assert(((LOffs.second != ROffs.second) ||
              (LChildID == 0 || RChildID == 0) ||
              isInSameSLocAddrSpace(getComposedLoc(LChild, 0),
                                    getComposedLoc(RChild, 0), nullptr)) &&
             "Mixed local/loaded FileIDs with same include location?");
      IsBeforeInTUCache.setCommonLoc(LOffs.first, LOffs.second, ROffs.second,
                                     LChildID < RChildID);
      return std::make_pair(
          true, IsBeforeInTUCache.getCachedResult(LOffs.second, ROffs.second));
    }
    RChild = ROffs.first;
  } while (!MoveUpTranslationUnitIncludeHierarchy(ROffs, *this));

  // If we found no match, the location is either in a built-ins buffer or
  // associated with global inline asm. PR5662 and PR22576 are examples.

  llvm::StringRef LB = getBufferOrFake(LOffs.first).getBufferIdentifier();
  llvm::StringRef RB = getBufferOrFake(ROffs.first).getBufferIdentifier();

  bool LIsBuiltins = LB == "<built-in>";
  bool RIsBuiltins = RB == "<built-in>";
  // Sort built-in before non-built-in.
  if (LIsBuiltins || RIsBuiltins) {
    if (LIsBuiltins != RIsBuiltins)
      return std::make_pair(true, LIsBuiltins);
    // Both are in built-in buffers, but from different files. We just claim
    // that lower IDs come first.
    return std::make_pair(true, LOffs.first < ROffs.first);
  }

  bool LIsAsm = LB == "<inline asm>";
  bool RIsAsm = RB == "<inline asm>";
  // Sort assembler after built-ins, but before the rest.
  if (LIsAsm || RIsAsm) {
    if (LIsAsm != RIsAsm)
      return std::make_pair(true, RIsAsm);
    assert(LOffs.first == ROffs.first);
    return std::make_pair(true, false);
  }

  bool LIsScratch = LB == "<scratch space>";
  bool RIsScratch = RB == "<scratch space>";
  // Sort scratch after inline asm, but before the rest.
  if (LIsScratch || RIsScratch) {
    if (LIsScratch != RIsScratch)
      return std::make_pair(true, LIsScratch);
    return std::make_pair(true, LOffs.second < ROffs.second);
  }

  return std::make_pair(true, LOffs.first < ROffs.first);
}

// ===----------------------------------------------------------------------===
// SourceManager — diagnostics & stats
// ===----------------------------------------------------------------------===

void SourceManager::PrintStats() const {
  llvm::errs() << "\n*** Source Manager Stats:\n";
  llvm::errs() << FileInfos.size() << " files mapped, " << MemBufferInfos.size()
               << " mem buffers mapped.\n";
  llvm::errs() << LocalSLocEntryTable.size() << " local SLocEntries allocated ("
               << llvm::capacity_in_bytes(LocalSLocEntryTable)
               << " bytes of capacity), " << NextLocalOffset
               << "B of SLoc address space used.\n";
  llvm::errs() << LoadedSLocEntryTable.size()
               << " loaded SLocEntries allocated ("
               << llvm::capacity_in_bytes(LoadedSLocEntryTable)
               << " bytes of capacity), "
               << MaxLoadedOffset - CurrentLoadedOffset
               << "B of SLoc address space used.\n";

  unsigned NumLineNumsComputed = 0;
  unsigned NumFileBytesMapped = 0;
  for (fileinfo_iterator I = fileinfo_begin(), E = fileinfo_end(); I != E;
       ++I) {
    NumLineNumsComputed += bool(I->second->SourceLineCache);
    NumFileBytesMapped += I->second->getSizeBytesMapped();
  }
  unsigned NumMacroArgsComputed = MacroArgsCacheMap.size();

  llvm::errs() << NumFileBytesMapped << " bytes of files mapped, "
               << NumLineNumsComputed << " files with line #'s computed, "
               << NumMacroArgsComputed << " files with macro args computed.\n";
  llvm::errs() << "FileID scans: " << NumLinearScans << " linear, "
               << NumBinaryProbes << " binary.\n";
}

LLVM_DUMP_METHOD void SourceManager::dump() const {
  llvm::raw_ostream &out = llvm::errs();

  auto DumpSLocEntry = [&](int ID, const SrcMgr::SLocEntry &Entry,
                           std::optional<SourceLocation::UIntTy> NextStart) {
    out << "SLocEntry <FileID " << ID << "> "
        << (Entry.isFile() ? "file" : "expansion") << " <SourceLocation "
        << Entry.getOffset() << ":";
    if (NextStart)
      out << *NextStart << ">\n";
    else
      out << "???\?>\n";
    if (Entry.isFile()) {
      auto &FI = Entry.getFile();
      if (FI.NumCreatedFIDs)
        out << "  covers <FileID " << ID << ":" << int(ID + FI.NumCreatedFIDs)
            << ">\n";
      if (FI.getIncludeLoc().isValid())
        out << "  included from " << FI.getIncludeLoc().getOffset() << "\n";
      auto &CC = FI.getContentCache();
      out << "  for " << (CC.OrigEntry ? CC.OrigEntry->getName() : "<none>")
          << "\n";
      if (CC.BufferOverridden)
        out << "  contents overridden\n";
      if (CC.ContentsEntry != CC.OrigEntry) {
        out << "  contents from "
            << (CC.ContentsEntry ? CC.ContentsEntry->getName() : "<none>")
            << "\n";
      }
    } else {
      auto &EI = Entry.getExpansion();
      out << "  spelling from " << EI.getSpellingLoc().getOffset() << "\n";
      out << "  macro " << (EI.isMacroArgExpansion() ? "arg" : "body")
          << " range <" << EI.getExpansionLocStart().getOffset() << ":"
          << EI.getExpansionLocEnd().getOffset() << ">\n";
    }
  };

  // Dump local SLocEntries.
  for (unsigned ID = 0, NumIDs = LocalSLocEntryTable.size(); ID != NumIDs;
       ++ID) {
    DumpSLocEntry(ID, LocalSLocEntryTable[ID],
                  ID == NumIDs - 1 ? NextLocalOffset
                                   : LocalSLocEntryTable[ID + 1].getOffset());
  }
  // Dump loaded SLocEntries.
  std::optional<SourceLocation::UIntTy> NextStart;
  for (unsigned Index = 0; Index != LoadedSLocEntryTable.size(); ++Index) {
    int ID = -(int)Index - 2;
    if (SLocEntryLoaded[Index]) {
      DumpSLocEntry(ID, LoadedSLocEntryTable[Index], NextStart);
      NextStart = LoadedSLocEntryTable[Index].getOffset();
    } else {
      NextStart = std::nullopt;
    }
  }
}

void SourceManager::noteSLocAddressSpaceUsage(
    DiagnosticsEngine &Diag, std::optional<unsigned> MaxNotes) const {
  struct Info {
    // A location where this file was entered.
    SourceLocation Loc;
    // Number of times this FileEntry was entered.
    unsigned Inclusions = 0;
    // Size usage from the file itself.
    uint64_t DirectSize = 0;
    // Total size usage from the file and its macro expansions.
    uint64_t TotalSize = 0;
  };
  using UsageMap = llvm::MapVector<const FileEntry *, Info>;

  UsageMap Usage;
  uint64_t CountedSize = 0;

  auto AddUsageForFileID = [&](FileID ID) {
    // The +1 here is because getFileIDSize doesn't include the extra byte for
    // the one-past-the-end location.
    unsigned Size = getFileIDSize(ID) + 1;

    // Find the file that used this address space, either directly or by
    // macro expansion.
    SourceLocation FileStart = getFileLoc(getComposedLoc(ID, 0));
    FileID FileLocID = getFileID(FileStart);
    const FileEntry *Entry = getFileEntryForID(FileLocID);

    Info &EntryInfo = Usage[Entry];
    if (EntryInfo.Loc.isInvalid())
      EntryInfo.Loc = FileStart;
    if (ID == FileLocID) {
      ++EntryInfo.Inclusions;
      EntryInfo.DirectSize += Size;
    }
    EntryInfo.TotalSize += Size;
    CountedSize += Size;
  };

  // Loaded SLocEntries have indexes counting downwards from -2.
  for (size_t Index = 0; Index != LoadedSLocEntryTable.size(); ++Index) {
    AddUsageForFileID(FileID::get(-2 - Index));
  }
  // Local SLocEntries have indexes counting upwards from 0.
  for (size_t Index = 0; Index != LocalSLocEntryTable.size(); ++Index) {
    AddUsageForFileID(FileID::get(Index));
  }

  // Sort the usage by size from largest to smallest. Break ties by raw source
  // location.
  auto SortedUsage = Usage.takeVector();
  auto Cmp = [](const UsageMap::value_type &A, const UsageMap::value_type &B) {
    return A.second.TotalSize > B.second.TotalSize ||
           (A.second.TotalSize == B.second.TotalSize &&
            A.second.Loc < B.second.Loc);
  };
  auto SortedEnd = SortedUsage.end();
  if (MaxNotes && SortedUsage.size() > *MaxNotes) {
    SortedEnd = SortedUsage.begin() + *MaxNotes;
    std::nth_element(SortedUsage.begin(), SortedEnd, SortedUsage.end(), Cmp);
  }
  std::sort(SortedUsage.begin(), SortedEnd, Cmp);

  // Produce note on sloc address space usage total.
  uint64_t LocalUsage = NextLocalOffset;
  uint64_t LoadedUsage = MaxLoadedOffset - CurrentLoadedOffset;
  int UsagePercent = static_cast<int>(100.0 * double(LocalUsage + LoadedUsage) /
                                      MaxLoadedOffset);
  Diag.Report(SourceLocation(), diag::note_total_sloc_usage)
      << LocalUsage << LoadedUsage << (LocalUsage + LoadedUsage)
      << UsagePercent;

  // Produce notes on sloc address space usage for each file with a high usage.
  uint64_t ReportedSize = 0;
  for (auto &[Entry, FileInfo] :
       llvm::make_range(SortedUsage.begin(), SortedEnd)) {
    Diag.Report(FileInfo.Loc, diag::note_file_sloc_usage)
        << FileInfo.Inclusions << FileInfo.DirectSize
        << (FileInfo.TotalSize - FileInfo.DirectSize);
    ReportedSize += FileInfo.TotalSize;
  }

  // Describe any remaining usage not reported in the per-file usage.
  if (ReportedSize != CountedSize) {
    Diag.Report(SourceLocation(), diag::note_file_misc_sloc_usage)
        << (SortedUsage.end() - SortedEnd) << CountedSize - ReportedSize;
  }
}

ExternalSLocEntrySource::~ExternalSLocEntrySource() = default;

SourceManager::MemoryBufferSizes SourceManager::getMemoryBufferSizes() const {
  size_t malloc_bytes = 0;
  size_t mmap_bytes = 0;

  for (unsigned i = 0, e = MemBufferInfos.size(); i != e; ++i)
    if (size_t sized_mapped = MemBufferInfos[i]->getSizeBytesMapped())
      switch (MemBufferInfos[i]->getMemoryBufferKind()) {
      case llvm::MemoryBuffer::MemoryBuffer_MMap:
        mmap_bytes += sized_mapped;
        break;
      case llvm::MemoryBuffer::MemoryBuffer_Malloc:
        malloc_bytes += sized_mapped;
        break;
      }

  return MemoryBufferSizes(malloc_bytes, mmap_bytes);
}

size_t SourceManager::getDataStructureSizes() const {
  size_t size = llvm::capacity_in_bytes(MemBufferInfos) +
                llvm::capacity_in_bytes(LocalSLocEntryTable) +
                llvm::capacity_in_bytes(LoadedSLocEntryTable) +
                llvm::capacity_in_bytes(SLocEntryLoaded) +
                llvm::capacity_in_bytes(FileInfos);

  if (OverriddenFilesInfo)
    size += llvm::capacity_in_bytes(OverriddenFilesInfo->OverriddenFiles);

  return size;
}

SourceManagerForFile::SourceManagerForFile(llvm::StringRef FileName,
                                           llvm::StringRef Content) {
  // This is referenced by `FileMgr` and will be released by `FileMgr` when it
  // is deleted.
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
      new llvm::vfs::InMemoryFileSystem);
  InMemoryFileSystem->addFile(
      FileName, 0,
      llvm::MemoryBuffer::getMemBuffer(Content, FileName,
                                       /*RequiresNullTerminator=*/false));
  // This is passed to `SM` as reference, so the pointer has to be referenced
  // in `Environment` so that `FileMgr` can out-live this function scope.
  FileMgr =
      std::make_unique<FileManager>(FileSystemOptions(), InMemoryFileSystem);
  // This is passed to `SM` as reference, so the pointer has to be referenced
  // by `Environment` due to the same reason above.
  Diagnostics = std::make_unique<DiagnosticsEngine>(
      llvm::IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs),
      new DiagnosticOptions);
  SourceMgr = std::make_unique<SourceManager>(*Diagnostics, *FileMgr);
  FileEntryRef FE = llvm::cantFail(FileMgr->getFileRef(FileName));
  FileID ID =
      SourceMgr->createFileID(FE, SourceLocation(), neverc::SrcMgr::C_User);
  assert(ID.isValid());
  SourceMgr->setMainFileID(ID);
}

void PrettyStackTraceLoc::print(llvm::raw_ostream &OS) const {
  if (Loc.isValid()) {
    Loc.print(OS, SM);
    OS << ": ";
  }
  OS << Message << '\n';
}

static_assert(std::is_trivially_destructible_v<SourceLocation>,
              "SourceLocation must be trivially destructible because it is "
              "used in unions");

static_assert(std::is_trivially_destructible_v<SourceRange>,
              "SourceRange must be trivially destructible because it is "
              "used in unions");

unsigned SourceLocation::getHashValue() const {
  return llvm::DenseMapInfo<UIntTy>::getHashValue(ID);
}

void llvm::FoldingSetTrait<SourceLocation>::Profile(
    const SourceLocation &X, llvm::FoldingSetNodeID &ID) {
  ID.AddInteger(X.ID);
}

void SourceLocation::print(llvm::raw_ostream &OS,
                           const SourceManager &SM) const {
  if (!isValid()) {
    OS << "<invalid loc>";
    return;
  }

  if (isFileID()) {
    PresumedLoc PLoc = SM.getPresumedLoc(*this);

    if (PLoc.isInvalid()) {
      OS << "<invalid>";
      return;
    }
    OS << PLoc.getFilename() << ':' << PLoc.getLine() << ':'
       << PLoc.getColumn();
    return;
  }

  SM.getExpansionLoc(*this).print(OS, SM);

  OS << " <Spelling=";
  SM.getSpellingLoc(*this).print(OS, SM);
  OS << '>';
}

LLVM_DUMP_METHOD std::string
SourceLocation::printToString(const SourceManager &SM) const {
  std::string S;
  llvm::raw_string_ostream OS(S);
  print(OS, SM);
  return S;
}

LLVM_DUMP_METHOD void SourceLocation::dump(const SourceManager &SM) const {
  print(llvm::errs(), SM);
  llvm::errs() << '\n';
}

LLVM_DUMP_METHOD void SourceRange::dump(const SourceManager &SM) const {
  print(llvm::errs(), SM);
  llvm::errs() << '\n';
}

namespace {
PresumedLoc genLocationDelta(llvm::raw_ostream &OS, const SourceManager &SM,
                             SourceLocation Loc, PresumedLoc Previous) {
  if (Loc.isFileID()) {

    PresumedLoc PLoc = SM.getPresumedLoc(Loc);

    if (PLoc.isInvalid()) {
      OS << "<invalid sloc>";
      return Previous;
    }

    if (Previous.isInvalid() ||
        strcmp(PLoc.getFilename(), Previous.getFilename()) != 0) {
      OS << PLoc.getFilename() << ':' << PLoc.getLine() << ':'
         << PLoc.getColumn();
    } else if (Previous.isInvalid() || PLoc.getLine() != Previous.getLine()) {
      OS << "line" << ':' << PLoc.getLine() << ':' << PLoc.getColumn();
    } else {
      OS << "col" << ':' << PLoc.getColumn();
    }
    return PLoc;
  }
  auto PrintedLoc = genLocationDelta(OS, SM, SM.getExpansionLoc(Loc), Previous);

  OS << " <Spelling=";
  PrintedLoc = genLocationDelta(OS, SM, SM.getSpellingLoc(Loc), PrintedLoc);
  OS << '>';
  return PrintedLoc;
}
} // namespace

void SourceRange::print(llvm::raw_ostream &OS, const SourceManager &SM) const {

  OS << '<';
  auto PrintedLoc = genLocationDelta(OS, SM, B, {});
  if (B != E) {
    OS << ", ";
    genLocationDelta(OS, SM, E, PrintedLoc);
  }
  OS << '>';
}

LLVM_DUMP_METHOD std::string
SourceRange::printToString(const SourceManager &SM) const {
  std::string S;
  llvm::raw_string_ostream OS(S);
  print(OS, SM);
  return S;
}

FileID FullSourceLoc::getFileID() const {
  assert(isValid());
  return SrcMgr->getFileID(*this);
}

FullSourceLoc FullSourceLoc::getExpansionLoc() const {
  assert(isValid());
  return FullSourceLoc(SrcMgr->getExpansionLoc(*this), *SrcMgr);
}

std::pair<FileID, unsigned> FullSourceLoc::getDecomposedExpansionLoc() const {
  return SrcMgr->getDecomposedExpansionLoc(*this);
}

FullSourceLoc FullSourceLoc::getSpellingLoc() const {
  assert(isValid());
  return FullSourceLoc(SrcMgr->getSpellingLoc(*this), *SrcMgr);
}

FullSourceLoc FullSourceLoc::getFileLoc() const {
  assert(isValid());
  return FullSourceLoc(SrcMgr->getFileLoc(*this), *SrcMgr);
}

PresumedLoc FullSourceLoc::getPresumedLoc(bool UseLineDirectives) const {
  if (!isValid())
    return PresumedLoc();

  return SrcMgr->getPresumedLoc(*this, UseLineDirectives);
}

bool FullSourceLoc::isMacroArgExpansion(FullSourceLoc *StartLoc) const {
  assert(isValid());
  return SrcMgr->isMacroArgExpansion(*this, StartLoc);
}

FullSourceLoc FullSourceLoc::getImmediateMacroCallerLoc() const {
  assert(isValid());
  return FullSourceLoc(SrcMgr->getImmediateMacroCallerLoc(*this), *SrcMgr);
}

unsigned FullSourceLoc::getFileOffset() const {
  assert(isValid());
  return SrcMgr->getFileOffset(*this);
}

unsigned FullSourceLoc::getLineNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getLineNumber(getFileID(), getFileOffset(), Invalid);
}

unsigned FullSourceLoc::getColumnNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getColumnNumber(getFileID(), getFileOffset(), Invalid);
}

const FileEntry *FullSourceLoc::getFileEntry() const {
  assert(isValid());
  return SrcMgr->getFileEntryForID(getFileID());
}

OptionalFileEntryRef FullSourceLoc::getFileEntryRef() const {
  assert(isValid());
  return SrcMgr->getFileEntryRefForID(getFileID());
}

unsigned FullSourceLoc::getExpansionLineNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getExpansionLineNumber(*this, Invalid);
}

unsigned FullSourceLoc::getExpansionColumnNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getExpansionColumnNumber(*this, Invalid);
}

unsigned FullSourceLoc::getSpellingLineNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getSpellingLineNumber(*this, Invalid);
}

unsigned FullSourceLoc::getSpellingColumnNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getSpellingColumnNumber(*this, Invalid);
}

bool FullSourceLoc::isInSystemHeader() const {
  assert(isValid());
  return SrcMgr->isInSystemHeader(*this);
}

bool FullSourceLoc::isBeforeInTranslationUnitThan(SourceLocation Loc) const {
  assert(isValid());
  return SrcMgr->isBeforeInTranslationUnit(*this, Loc);
}

LLVM_DUMP_METHOD void FullSourceLoc::dump() const {
  SourceLocation::dump(*SrcMgr);
}

const char *FullSourceLoc::getCharacterData(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getCharacterData(*this, Invalid);
}

llvm::StringRef FullSourceLoc::getBufferData(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getBufferData(SrcMgr->getFileID(*this), Invalid);
}

std::pair<FileID, unsigned> FullSourceLoc::getDecomposedLoc() const {
  return SrcMgr->getDecomposedLoc(*this);
}
