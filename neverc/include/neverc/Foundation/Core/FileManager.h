#ifndef NEVERC_FOUNDATION_FILEMANAGER_H
#define NEVERC_FOUNDATION_FILEMANAGER_H

#include "neverc/Foundation/Core/DirectoryEntry.h"
#include "neverc/Foundation/Core/FileEntry.h"
#include "neverc/Foundation/Core/FileSystemOptions.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <ctime>
#include <map>
#include <memory>
#include <string>

namespace llvm {

class MemoryBuffer;

} // end namespace llvm

namespace neverc {

class FileManager : public llvm::RefCountedBase<FileManager> {
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;
  FileSystemOptions FileSystemOpts;
  llvm::SpecificBumpPtrAllocator<FileEntry> FilesAlloc;
  llvm::SpecificBumpPtrAllocator<DirectoryEntry> DirsAlloc;

  llvm::DenseMap<llvm::sys::fs::UniqueID, DirectoryEntry *> UniqueRealDirs;

  llvm::DenseMap<llvm::sys::fs::UniqueID, FileEntry *> UniqueRealFiles;

  llvm::SmallVector<DirectoryEntry *, 4> VirtualDirectoryEntries;
  llvm::SmallVector<FileEntry *, 4> VirtualFileEntries;

  llvm::SmallVector<FileEntry *, 0> BypassFileEntries;

  llvm::StringMap<llvm::ErrorOr<DirectoryEntry &>, llvm::BumpPtrAllocator>
      SeenDirEntries;

  llvm::StringMap<llvm::ErrorOr<FileEntryRef::MapValue>, llvm::BumpPtrAllocator>
      SeenFileEntries;

  std::unique_ptr<llvm::StringMap<llvm::ErrorOr<FileEntryRef::MapValue>>>
      SeenBypassFileEntries;

  OptionalFileEntryRef STDIN;

  llvm::DenseMap<const void *, llvm::StringRef> CanonicalNames;

  llvm::BumpPtrAllocator CanonicalNameStorage;

  unsigned NextFileUID;

  std::error_code getStatValue(llvm::StringRef Path, llvm::vfs::Status &Status,
                               bool isFile,
                               std::unique_ptr<llvm::vfs::File> *F);

  void addAncestorsAsVirtualDirs(llvm::StringRef Path);

  void fillRealPathName(FileEntry *UFE, llvm::StringRef FileName);

public:
  FileManager(const FileSystemOptions &FileSystemOpts,
              llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS = nullptr);
  ~FileManager();

  size_t getNumUniqueRealFiles() const { return UniqueRealFiles.size(); }

  llvm::Expected<DirectoryEntryRef> getDirectoryRef(llvm::StringRef DirName,
                                                    bool CacheFailure = true);

  OptionalDirectoryEntryRef getOptionalDirectoryRef(llvm::StringRef DirName,
                                                    bool CacheFailure = true) {
    return llvm::expectedToOptional(getDirectoryRef(DirName, CacheFailure));
  }

  llvm::ErrorOr<const DirectoryEntry *> getDirectory(llvm::StringRef DirName,
                                                     bool CacheFailure = true);

  llvm::ErrorOr<const FileEntry *> getFile(llvm::StringRef Filename,
                                           bool OpenFile = false,
                                           bool CacheFailure = true);

  llvm::Expected<FileEntryRef> getFileRef(llvm::StringRef Filename,
                                          bool OpenFile = false,
                                          bool CacheFailure = true);

  llvm::Expected<FileEntryRef> getSTDIN();

  OptionalFileEntryRef getOptionalFileRef(llvm::StringRef Filename,
                                          bool OpenFile = false,
                                          bool CacheFailure = true) {
    return llvm::expectedToOptional(
        getFileRef(Filename, OpenFile, CacheFailure));
  }

  FileSystemOptions &getFileSystemOpts() { return FileSystemOpts; }
  const FileSystemOptions &getFileSystemOpts() const { return FileSystemOpts; }

  llvm::vfs::FileSystem &getVirtualFileSystem() const { return *FS; }
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
  getVirtualFileSystemPtr() const {
    return FS;
  }

  void
  setVirtualFileSystem(llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS) {
    this->FS = std::move(FS);
  }

  FileEntryRef getVirtualFileRef(llvm::StringRef Filename, off_t Size,
                                 time_t ModificationTime);

  const FileEntry *getVirtualFile(llvm::StringRef Filename, off_t Size,
                                  time_t ModificationTime);

  OptionalFileEntryRef getBypassFile(FileEntryRef VFE);

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBufferForFile(FileEntryRef Entry, bool isVolatile = false,
                   bool RequiresNullTerminator = true);
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBufferForFile(llvm::StringRef Filename, bool isVolatile = false,
                   bool RequiresNullTerminator = true) {
    return getBufferForFileImpl(Filename, /*FileSize=*/-1, isVolatile,
                                RequiresNullTerminator);
  }

private:
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBufferForFileImpl(llvm::StringRef Filename, int64_t FileSize,
                       bool isVolatile, bool RequiresNullTerminator);

public:
  std::error_code getNoncachedStatValue(llvm::StringRef Path,
                                        llvm::vfs::Status &Result);

  bool FixupRelativePath(llvm::SmallVectorImpl<char> &path) const;

  bool makeAbsolutePath(llvm::SmallVectorImpl<char> &Path) const;

  void GetUniqueIDMapping(
      llvm::SmallVectorImpl<OptionalFileEntryRef> &UIDToFiles) const;

  llvm::StringRef getCanonicalName(DirectoryEntryRef Dir);

  llvm::StringRef getCanonicalName(FileEntryRef File);

private:
  llvm::StringRef getCanonicalName(const void *Entry, llvm::StringRef Name);

public:
  void PrintStats() const;
};

} // end namespace neverc

#endif // NEVERC_FOUNDATION_FILEMANAGER_H
