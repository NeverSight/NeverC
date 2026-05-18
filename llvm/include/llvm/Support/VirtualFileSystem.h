//===- VirtualFileSystem.h - Virtual File System Layer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the virtual file system interface vfs::FileSystem.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_VIRTUALFILESYSTEM_H
#define LLVM_SUPPORT_VIRTUALFILESYSTEM_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <assert.h>
#include <optional>
#include <stdint.h>
#include <string>
#include <system_error>
#include <time.h>
#include <utility>
#include <vector>

namespace llvm {

class MemoryBuffer;
class MemoryBufferRef;
class Twine;

namespace vfs {

/// The result of a \p status operation.
class Status {
  std::string Name;
  llvm::sys::fs::UniqueID UID;
  llvm::sys::TimePoint<> MTime;
  uint32_t User;
  uint32_t Group;
  uint64_t Size;
  llvm::sys::fs::file_type Type = llvm::sys::fs::file_type::status_error;
  llvm::sys::fs::perms Perms;

public:
  // FIXME: remove when files support multiple names
  bool IsVFSMapped = false;

  /// Whether this entity has an external path different from the virtual path,
  /// and the external path is exposed by leaking it through the abstraction.
  /// For example, a RedirectingFileSystem will set this for paths where
  /// UseExternalName is true.
  ///
  /// FIXME: Currently the external path is exposed by replacing the virtual
  /// path in this Status object. Instead, we should leave the path in the
  /// Status intact (matching the requested virtual path) - see
  /// FileManager::getFileRef for how we plan to fix this.
  bool ExposesExternalVFSPath = false;

  Status() = default;
  Status(const llvm::sys::fs::file_status &Status);
  Status(const Twine &Name, llvm::sys::fs::UniqueID UID,
         llvm::sys::TimePoint<> MTime, uint32_t User, uint32_t Group,
         uint64_t Size, llvm::sys::fs::file_type Type,
         llvm::sys::fs::perms Perms);

  /// Get a copy of a Status with a different size.
  static Status copyWithNewSize(const Status &In, uint64_t NewSize);
  /// Get a copy of a Status with a different name.
  static Status copyWithNewName(const Status &In, const Twine &NewName);
  static Status copyWithNewName(const llvm::sys::fs::file_status &In,
                                const Twine &NewName);

  /// Returns the name that should be used for this file or directory.
  StringRef getName() const { return Name; }

  /// @name Status interface from llvm::sys::fs
  /// @{
  llvm::sys::fs::file_type getType() const { return Type; }
  llvm::sys::fs::perms getPermissions() const { return Perms; }
  llvm::sys::TimePoint<> getLastModificationTime() const { return MTime; }
  llvm::sys::fs::UniqueID getUniqueID() const { return UID; }
  uint32_t getUser() const { return User; }
  uint32_t getGroup() const { return Group; }
  uint64_t getSize() const { return Size; }
  /// @}
  /// @name Status queries
  /// These are static queries in llvm::sys::fs.
  /// @{
  bool equivalent(const Status &Other) const;
  bool isDirectory() const;
  bool isRegularFile() const;
  bool isOther() const;
  bool isSymlink() const;
  bool isStatusKnown() const;
  bool exists() const;
  /// @}
};

/// Represents an open file.
class File {
public:
  /// Destroy the file after closing it (if open).
  /// Sub-classes should generally call close() inside their destructors.  We
  /// cannot do that from the base class, since close is virtual.
  virtual ~File();

  /// Get the status of the file.
  virtual llvm::ErrorOr<Status> status() = 0;

  /// Get the name of the file
  virtual llvm::ErrorOr<SmallString<256>> getName() {
    if (auto Status = status())
      return SmallString<256>(Status->getName());
    else
      return Status.getError();
  }

  /// Get the contents of the file as a \p MemoryBuffer.
  virtual llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBuffer(const Twine &Name, int64_t FileSize = -1,
            bool RequiresNullTerminator = true, bool IsVolatile = false) = 0;

  /// Closes the file.
  virtual std::error_code close() = 0;

  // Get the same file with a different path.
  static ErrorOr<std::unique_ptr<File>>
  getWithPath(ErrorOr<std::unique_ptr<File>> Result, const Twine &P);

protected:
  // Set the file's underlying path.
  virtual void setPath(const Twine &Path) {}
};

/// A member of a directory, yielded by a directory_iterator.
/// Only information available on most platforms is included.
class directory_entry {
  std::string Path;
  llvm::sys::fs::file_type Type = llvm::sys::fs::file_type::type_unknown;

public:
  directory_entry() = default;
  directory_entry(std::string Path, llvm::sys::fs::file_type Type)
      : Path(std::move(Path)), Type(Type) {}

  llvm::StringRef path() const { return Path; }
  llvm::sys::fs::file_type type() const { return Type; }
};

namespace detail {

/// An interface for virtual file systems to provide an iterator over the
/// (non-recursive) contents of a directory.
struct DirIterImpl : public ThreadSafeRefCountedBase<DirIterImpl> {
  virtual ~DirIterImpl();

  /// Sets \c CurrentEntry to the next entry in the directory on success,
  /// to directory_entry() at end,  or returns a system-defined \c error_code.
  virtual std::error_code increment() = 0;

  directory_entry CurrentEntry;
};

} // namespace detail

/// An input iterator over the entries in a virtual path, similar to
/// llvm::sys::fs::directory_iterator.
class directory_iterator {
  IntrusiveRefCntPtr<detail::DirIterImpl> Impl;

public:
  directory_iterator(IntrusiveRefCntPtr<detail::DirIterImpl> I) : Impl(I) {
    assert(Impl.get() != nullptr && "requires non-null implementation");
    if (Impl->CurrentEntry.path().empty())
      Impl.reset();
  }

  /// Construct an 'end' iterator.
  directory_iterator() = default;

  /// Equivalent to operator++, with an error code.
  directory_iterator &increment(std::error_code &EC) {
    assert(Impl && "attempting to increment past end");
    EC = Impl->increment();
    if (Impl->CurrentEntry.path().empty())
      Impl.reset(); // Normalize the end iterator to Impl == nullptr.
    return *this;
  }

  const directory_entry &operator*() const { return Impl->CurrentEntry; }
  const directory_entry *operator->() const { return &Impl->CurrentEntry; }

  bool operator==(const directory_iterator &RHS) const {
    if (Impl && RHS.Impl)
      return Impl->CurrentEntry.path() == RHS.Impl->CurrentEntry.path();
    return !Impl && !RHS.Impl;
  }
  bool operator!=(const directory_iterator &RHS) const {
    return !(*this == RHS);
  }
};

class FileSystem;

namespace detail {

/// Keeps state for the recursive_directory_iterator.
struct RecDirIterState : public ThreadSafeRefCountedBase<RecDirIterState> {
  SmallVector<directory_iterator, 8> Stack;
  bool HasNoPushRequest = false;
};

} // end namespace detail

/// An input iterator over the recursive contents of a virtual path,
/// similar to llvm::sys::fs::recursive_directory_iterator.
class recursive_directory_iterator {
  FileSystem *FS;
  IntrusiveRefCntPtr<detail::RecDirIterState> State;

public:
  recursive_directory_iterator(FileSystem &FS, const Twine &Path,
                               std::error_code &EC);

  /// Construct an 'end' iterator.
  recursive_directory_iterator() = default;

  /// Equivalent to operator++, with an error code.
  recursive_directory_iterator &increment(std::error_code &EC);

  const directory_entry &operator*() const { return *State->Stack.back(); }
  const directory_entry *operator->() const { return &*State->Stack.back(); }

  bool operator==(const recursive_directory_iterator &Other) const {
    return State == Other.State; // identity
  }
  bool operator!=(const recursive_directory_iterator &RHS) const {
    return !(*this == RHS);
  }

  /// Gets the current level. Starting path is at level 0.
  int level() const {
    assert(!State->Stack.empty() &&
           "Cannot get level without any iteration state");
    return State->Stack.size() - 1;
  }

  void no_push() { State->HasNoPushRequest = true; }
};

/// The virtual file system interface.
class FileSystem : public llvm::ThreadSafeRefCountedBase<FileSystem> {
public:
  virtual ~FileSystem();

  /// Get the status of the entry at \p Path, if one exists.
  virtual llvm::ErrorOr<Status> status(const Twine &Path) = 0;

  /// Get a \p File object for the file at \p Path, if one exists.
  virtual llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) = 0;

  /// This is a convenience method that opens a file, gets its content and then
  /// closes the file.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBufferForFile(const Twine &Name, int64_t FileSize = -1,
                   bool RequiresNullTerminator = true, bool IsVolatile = false);

  /// Get a directory_iterator for \p Dir.
  /// \note The 'end' iterator is directory_iterator().
  virtual directory_iterator dir_begin(const Twine &Dir,
                                       std::error_code &EC) = 0;

  /// Set the working directory. This will affect all following operations on
  /// this file system and may propagate down for nested file systems.
  virtual std::error_code setCurrentWorkingDirectory(const Twine &Path) = 0;

  /// Get the working directory of this file system.
  virtual llvm::ErrorOr<SmallString<256>>
  getCurrentWorkingDirectory() const = 0;

  /// Gets real path of \p Path e.g. collapse all . and .. patterns, resolve
  /// symlinks. For real file system, this uses `llvm::sys::fs::real_path`.
  /// This returns errc::operation_not_permitted if not implemented by subclass.
  virtual std::error_code getRealPath(const Twine &Path,
                                      SmallVectorImpl<char> &Output) const;

  /// Check whether a file exists. Provided for convenience.
  bool exists(const Twine &Path);

  /// Is the file mounted on a local filesystem?
  virtual std::error_code isLocal(const Twine &Path, bool &Result);

  /// Make \a Path an absolute path.
  ///
  /// Makes \a Path absolute using the current directory if it is not already.
  /// An empty \a Path will result in the current directory.
  ///
  /// /absolute/path   => /absolute/path
  /// relative/../path => <current-directory>/relative/../path
  ///
  /// \param Path A path that is modified to be an absolute path.
  /// \returns success if \a path has been made absolute, otherwise a
  ///          platform-specific error_code.
  virtual std::error_code makeAbsolute(SmallVectorImpl<char> &Path) const;

  enum class PrintType { Summary, Contents, RecursiveContents };
  void print(raw_ostream &OS, PrintType Type = PrintType::Contents,
             unsigned IndentLevel = 0) const {
    printImpl(OS, Type, IndentLevel);
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const;
#endif

protected:
  virtual void printImpl(raw_ostream &OS, PrintType Type,
                         unsigned IndentLevel) const {
    printIndent(OS, IndentLevel);
    OS << "FileSystem\n";
  }

  void printIndent(raw_ostream &OS, unsigned IndentLevel) const {
    for (unsigned i = 0; i < IndentLevel; ++i)
      OS << "  ";
  }
};

/// Gets an \p vfs::FileSystem for the 'real' file system, as seen by
/// the operating system.
/// The working directory is linked to the process's working directory.
/// (This is usually thread-hostile).
/// Declared here (before SourceMgr include) so CommandLine/lcommand_lline
/// recursive includes see this when they call vfs::getRealFileSystem().
IntrusiveRefCntPtr<FileSystem> getRealFileSystem();

/// Create an \p vfs::FileSystem for the 'real' file system, as seen by
/// the operating system.
/// It has its own working directory, independent of (but initially equal to)
/// that of the process.
std::unique_ptr<FileSystem> createPhysicalFileSystem();

} // namespace vfs

} // namespace llvm

// Include after vfs::FileSystem is complete: SourceMgr pulls in WithColor ->
// CommandLine -> csupport/lcommand_lline.h, which needs a complete FileSystem.
// Must be at file scope (not inside llvm::vfs or llvm): MemoryBuffer pulls in
// Program.h and other headers that must not be nested in namespaces.
#include "llvm/Support/SourceMgr.h"

namespace llvm {

namespace vfs {

/// A file system that allows overlaying one \p AbstractFileSystem on top
/// of another.
///
/// Consists of a stack of >=1 \p FileSystem objects, which are treated as being
/// one merged file system. When there is a directory that exists in more than
/// one file system, the \p OverlayFileSystem contains a directory containing
/// the union of their contents.  The attributes (permissions, etc.) of the
/// top-most (most recently added) directory are used.  When there is a file
/// that exists in more than one file system, the file in the top-most file
/// system overrides the other(s).
class OverlayFileSystem : public FileSystem {
  using FileSystemList = SmallVector<IntrusiveRefCntPtr<FileSystem>, 1>;

  /// The stack of file systems, implemented as a list in order of
  /// their addition.
  FileSystemList FSList;

public:
  OverlayFileSystem(IntrusiveRefCntPtr<FileSystem> Base);

  /// Pushes a file system on top of the stack.
  void pushOverlay(IntrusiveRefCntPtr<FileSystem> FS);

  llvm::ErrorOr<Status> status(const Twine &Path) override;
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override;
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;
  llvm::ErrorOr<SmallString<256>> getCurrentWorkingDirectory() const override;
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;
  std::error_code isLocal(const Twine &Path, bool &Result) override;
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) const override;

  using iterator = FileSystemList::reverse_iterator;
  using const_iterator = FileSystemList::const_reverse_iterator;
  using reverse_iterator = FileSystemList::iterator;
  using const_reverse_iterator = FileSystemList::const_iterator;
  using range = iterator_range<iterator>;
  using const_range = iterator_range<const_iterator>;

  /// Get an iterator pointing to the most recently added file system.
  iterator overlays_begin() { return FSList.rbegin(); }
  const_iterator overlays_begin() const { return FSList.rbegin(); }

  /// Get an iterator pointing one-past the least recently added file system.
  iterator overlays_end() { return FSList.rend(); }
  const_iterator overlays_end() const { return FSList.rend(); }

  /// Get an iterator pointing to the least recently added file system.
  reverse_iterator overlays_rbegin() { return FSList.begin(); }
  const_reverse_iterator overlays_rbegin() const { return FSList.begin(); }

  /// Get an iterator pointing one-past the most recently added file system.
  reverse_iterator overlays_rend() { return FSList.end(); }
  const_reverse_iterator overlays_rend() const { return FSList.end(); }

  range overlays_range() { return llvm::reverse(FSList); }
  const_range overlays_range() const { return llvm::reverse(FSList); }

protected:
  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override;
};

/// By default, this delegates all calls to the underlying file system. This
/// is useful when derived file systems want to override some calls and still
/// proxy other calls.
class ProxyFileSystem : public FileSystem {
public:
  explicit ProxyFileSystem(IntrusiveRefCntPtr<FileSystem> FS)
      : FS(std::move(FS)) {}

  llvm::ErrorOr<Status> status(const Twine &Path) override {
    return FS->status(Path);
  }
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override {
    return FS->openFileForRead(Path);
  }
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override {
    return FS->dir_begin(Dir, EC);
  }
  llvm::ErrorOr<SmallString<256>> getCurrentWorkingDirectory() const override {
    return FS->getCurrentWorkingDirectory();
  }
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override {
    return FS->setCurrentWorkingDirectory(Path);
  }
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) const override {
    return FS->getRealPath(Path, Output);
  }
  std::error_code isLocal(const Twine &Path, bool &Result) override {
    return FS->isLocal(Path, Result);
  }

protected:
  FileSystem &getUnderlyingFS() const { return *FS; }

private:
  IntrusiveRefCntPtr<FileSystem> FS;

  virtual void anchor();
};

namespace detail {

class InMemoryDirectory;
class InMemoryNode;

struct NewInMemoryNodeInfo {
  llvm::sys::fs::UniqueID DirUID;
  StringRef Path;
  StringRef Name;
  time_t ModificationTime;
  std::unique_ptr<llvm::MemoryBuffer> Buffer;
  uint32_t User;
  uint32_t Group;
  llvm::sys::fs::file_type Type;
  llvm::sys::fs::perms Perms;

  Status makeStatus() const;
};

class NamedNodeOrError {
  ErrorOr<std::pair<llvm::SmallString<128>, const detail::InMemoryNode *>>
      Value;

public:
  NamedNodeOrError(llvm::SmallString<128> Name,
                   const detail::InMemoryNode *Node)
      : Value(std::make_pair(Name, Node)) {}
  NamedNodeOrError(std::error_code EC) : Value(EC) {}
  NamedNodeOrError(llvm::errc EC) : Value(EC) {}

  StringRef getName() const { return (*Value).first; }
  explicit operator bool() const { return static_cast<bool>(Value); }
  operator std::error_code() const { return Value.getError(); }
  std::error_code getError() const { return Value.getError(); }
  const detail::InMemoryNode *operator*() const { return (*Value).second; }
};

} // namespace detail

/// An in-memory file system.
class InMemoryFileSystem : public FileSystem {
  std::unique_ptr<detail::InMemoryDirectory> Root;
  SmallString<256> WorkingDirectory;
  bool UseNormalizedPaths = true;

  using MakeNodeFn = llvm::function_ref<std::unique_ptr<detail::InMemoryNode>(
      detail::NewInMemoryNodeInfo)>;

  /// Create node with \p MakeNode and add it into this filesystem at \p Path.
  bool addFile(const Twine &Path, time_t ModificationTime,
               std::unique_ptr<llvm::MemoryBuffer> Buffer, uint32_t User,
               uint32_t Group, llvm::sys::fs::file_type Type,
               llvm::sys::fs::perms Perms, MakeNodeFn MakeNode);

  /// Looks up the in-memory node for the path \p P.
  /// If \p FollowFinalSymlink is true, the returned node is guaranteed to
  /// not be a symlink and its path may differ from \p P.
  detail::NamedNodeOrError lookupNode(const Twine &P, bool FollowFinalSymlink,
                                      size_t SymlinkDepth = 0) const;

  class DirIterator;

public:
  explicit InMemoryFileSystem(bool UseNormalizedPaths = true);
  ~InMemoryFileSystem() override;

  /// Add a file containing a buffer or a directory to the VFS with a
  /// path. The VFS owns the buffer.  If present, User, Group, Type
  /// and Perms apply to the newly-created file or directory.
  /// \return true if the file or directory was successfully added,
  /// false if the file or directory already exists in the file system with
  /// different contents.
  bool addFile(
      const Twine &Path, time_t ModificationTime,
      std::unique_ptr<llvm::MemoryBuffer> Buffer, uint32_t User = 0,
      uint32_t Group = 0,
      llvm::sys::fs::file_type Type = llvm::sys::fs::file_type::regular_file,
      llvm::sys::fs::perms Perms = llvm::sys::fs::all_all);

  /// Add a hard link to a file.
  ///
  /// Here hard links are not intended to be fully equivalent to the classical
  /// filesystem. Both the hard link and the file share the same buffer and
  /// status (and thus have the same UniqueID). Because of this there is no way
  /// to distinguish between the link and the file after the link has been
  /// added.
  ///
  /// The \p Target path must be an existing file or a hardlink. The
  /// \p NewLink file must not have been added before. The \p Target
  /// path must not be a directory. The \p NewLink node is added as a hard
  /// link which points to the resolved file of \p Target node.
  /// \return true if the above condition is satisfied and hardlink was
  /// successfully created, false otherwise.
  bool addHardLink(const Twine &NewLink, const Twine &Target);

  /// Arbitrary max depth to search through symlinks. We can get into problems
  /// if a link links to a link that links back to the link, for example.
  static constexpr size_t MaxSymlinkDepth = 16;

  /// Add a symbolic link. Unlike a HardLink, because \p Target doesn't need
  /// to refer to a file (or refer to anything, as it happens). Also, an
  /// in-memory directory for \p Target isn't automatically created.
  bool addSymbolicLink(const Twine &NewLink, const Twine &Target,
                       time_t ModificationTime, uint32_t User = 0,
                       uint32_t Group = 0,
                       llvm::sys::fs::perms Perms = llvm::sys::fs::all_all);

  /// Add a buffer to the VFS with a path. The VFS does not own the buffer.
  /// If present, User, Group, Type and Perms apply to the newly-created file
  /// or directory.
  /// \return true if the file or directory was successfully added,
  /// false if the file or directory already exists in the file system with
  /// different contents.
  bool addFileNoOwn(
      const Twine &Path, time_t ModificationTime,
      const llvm::MemoryBufferRef &Buffer, uint32_t User = 0,
      uint32_t Group = 0,
      llvm::sys::fs::file_type Type = llvm::sys::fs::file_type::regular_file,
      llvm::sys::fs::perms Perms = llvm::sys::fs::all_all);

  SmallString<256> toString() const;

  /// Return true if this file system normalizes . and .. in paths.
  bool useNormalizedPaths() const { return UseNormalizedPaths; }

  llvm::ErrorOr<Status> status(const Twine &Path) override;
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override;
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;

  llvm::ErrorOr<SmallString<256>> getCurrentWorkingDirectory() const override {
    return WorkingDirectory;
  }
  /// Canonicalizes \p Path by combining with the current working
  /// directory and normalizing the path (e.g. remove dots). If the current
  /// working directory is not set, this returns errc::operation_not_permitted.
  ///
  /// This doesn't resolve symlinks as they are not supported in in-memory file
  /// system.
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) const override;
  std::error_code isLocal(const Twine &Path, bool &Result) override;
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;

protected:
  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override;
};

/// Get a globally unique ID for a virtual file or directory.
llvm::sys::fs::UniqueID getNextVirtualUniqueID();

/// Gets a \p FileSystem for a virtual file system described in YAML
/// format.
std::unique_ptr<FileSystem>
getVFSFromYAML(std::unique_ptr<llvm::MemoryBuffer> Buffer,
               llvm::SourceMgr::DiagHandlerTy DiagHandler,
               StringRef YAMLFilePath, void *DiagContext = nullptr,
               IntrusiveRefCntPtr<FileSystem> ExternalFS = getRealFileSystem());

struct YAMLVFSEntry {
  template <typename T1, typename T2>
  YAMLVFSEntry(T1 &&VPath, T2 &&RPath, bool IsDirectory = false)
      : VPath(std::forward<T1>(VPath)), RPath(std::forward<T2>(RPath)),
        IsDirectory(IsDirectory) {}
  SmallString<256> VPath;
  SmallString<256> RPath;
  bool IsDirectory = false;
};

class RedirectingFSDirIterImpl;
class RedirectingFileSystemParser;

/// A virtual file system parsed from a YAML file.
///
/// Currently, this class allows creating virtual files and directories. Virtual
/// files map to existing external files in \c ExternalFS, and virtual
/// directories may either map to existing directories in \c ExternalFS or list
/// their contents in the form of other virtual directories and/or files.
///
/// The basic structure of the parsed file is:
/// \verbatim
/// {
///   'version': <version number>,
///   <optional configuration>
///   'roots': [
///              <directory entries>
///            ]
/// }
/// \endverbatim
///
/// The roots may be absolute or relative. If relative they will be made
/// absolute against either current working directory or the directory where
/// the Overlay YAML file is located, depending on the 'root-relative'
/// configuration.
///
/// All configuration options are optional.
///   'case-sensitive': <boolean, default=(true for Posix, false for Windows)>
///   'use-external-names': <boolean, default=true>
///   'root-relative': <string, one of 'cwd' or 'overlay-dir', default='cwd'>
///   'overlay-relative': <boolean, default=false>
///   'fallthrough': <boolean, default=true, deprecated - use 'redirecting-with'
///                   instead>
///   'redirecting-with': <string, one of 'fallthrough', 'fallback', or
///                        'redirect-only', default='fallthrough'>
///
/// To clarify, 'root-relative' option will prepend the current working
/// directory, or the overlay directory to the 'roots->name' field only if
/// 'roots->name' is a relative path. On the other hand, when 'overlay-relative'
/// is set to 'true', external paths will always be prepended with the overlay
/// directory, even if external paths are not relative paths. The
/// 'root-relative' option has no interaction with the 'overlay-relative'
/// option.
///
/// Virtual directories that list their contents are represented as
/// \verbatim
/// {
///   'type': 'directory',
///   'name': <string>,
///   'contents': [ <file or directory entries> ]
/// }
/// \endverbatim
///
/// The default attributes for such virtual directories are:
/// \verbatim
/// MTime = now() when created
/// Perms = 0777
/// User = Group = 0
/// Size = 0
/// UniqueID = unspecified unique value
/// \endverbatim
///
/// When a path prefix matches such a directory, the next component in the path
/// is matched against the entries in the 'contents' array.
///
/// Re-mapped directories, on the other hand, are represented as
/// /// \verbatim
/// {
///   'type': 'directory-remap',
///   'name': <string>,
///   'use-external-name': <boolean>, # Optional
///   'external-contents': <path to external directory>
/// }
/// \endverbatim
///
/// and inherit their attributes from the external directory. When a path
/// prefix matches such an entry, the unmatched components are appended to the
/// 'external-contents' path, and the resulting path is looked up in the
/// external file system instead.
///
/// Re-mapped files are represented as
/// \verbatim
/// {
///   'type': 'file',
///   'name': <string>,
///   'use-external-name': <boolean>, # Optional
///   'external-contents': <path to external file>
/// }
/// \endverbatim
///
/// Their attributes and file contents are determined by looking up the file at
/// their 'external-contents' path in the external file system.
///
/// For 'file', 'directory' and 'directory-remap' entries the 'name' field may
/// contain multiple path components (e.g. /path/to/file). However, any
/// directory in such a path that contains more than one child must be uniquely
/// represented by a 'directory' entry.
///
/// When the 'use-external-name' field is set, calls to \a vfs::File::status()
/// give the external (remapped) filesystem name instead of the name the file
/// was accessed by. This is an intentional leak through the \a
/// RedirectingFileSystem abstraction layer. It enables clients to discover
/// (and use) the external file location when communicating with users or tools
/// that don't use the same VFS overlay.
///
/// FIXME: 'use-external-name' causes behaviour that's inconsistent with how
/// "real" filesystems behave. Maybe there should be a separate channel for
/// this information.
class RedirectingFileSystem : public vfs::FileSystem {
public:
  enum EntryKind { EK_Directory, EK_DirectoryRemap, EK_File };
  enum NameKind { NK_NotSet, NK_External, NK_Virtual };

  /// The type of redirection to perform.
  enum class RedirectKind {
    /// Lookup the redirected path first (ie. the one specified in
    /// 'external-contents') and if that fails "fallthrough" to a lookup of the
    /// originally provided path.
    Fallthrough,
    /// Lookup the provided path first and if that fails, "fallback" to a
    /// lookup of the redirected path.
    Fallback,
    /// Only lookup the redirected path, do not lookup the originally provided
    /// path.
    RedirectOnly
  };

  /// The type of relative path used by Roots.
  enum class RootRelativeKind {
    /// The roots are relative to the current working directory.
    CWD,
    /// The roots are relative to the directory where the Overlay YAML file
    // locates.
    OverlayDir
  };

  /// A single file or directory in the VFS.
  class Entry {
    EntryKind Kind;
    std::string Name;

  public:
    Entry(EntryKind K, StringRef Name) : Kind(K), Name(Name) {}
    virtual ~Entry() = default;

    StringRef getName() const { return Name; }
    EntryKind getKind() const { return Kind; }
  };

  /// A directory in the vfs with explicitly specified contents.
  class DirectoryEntry : public Entry {
    SmallVector<std::unique_ptr<Entry>, 4> Contents;
    Status S;

  public:
    /// Constructs a directory entry with explicitly specified contents.
    template <unsigned N>
    DirectoryEntry(StringRef Name,
                   SmallVector<std::unique_ptr<Entry>, N> InContents, Status S)
        : Entry(EK_Directory, Name), S(std::move(S)) {
      Contents.reserve(InContents.size());
      for (auto &E : InContents)
        Contents.push_back(std::move(E));
    }

    /// Constructs an empty directory entry.
    DirectoryEntry(StringRef Name, Status S)
        : Entry(EK_Directory, Name), S(std::move(S)) {}

    Status getStatus() { return S; }

    void addContent(std::unique_ptr<Entry> Content) {
      Contents.push_back(std::move(Content));
    }

    Entry *getLastContent() const { return Contents.back().get(); }

    using iterator = decltype(Contents)::iterator;

    iterator contents_begin() { return Contents.begin(); }
    iterator contents_end() { return Contents.end(); }

    static bool classof(const Entry *E) { return E->getKind() == EK_Directory; }
  };

  /// A file or directory in the vfs that is mapped to a file or directory in
  /// the external filesystem.
  class RemapEntry : public Entry {
    SmallString<256> ExternalContentsPath;
    NameKind UseName;

  protected:
    RemapEntry(EntryKind K, StringRef Name, StringRef ExternalContentsPath,
               NameKind UseName)
        : Entry(K, Name), ExternalContentsPath(ExternalContentsPath),
          UseName(UseName) {}

  public:
    StringRef getExternalContentsPath() const { return ExternalContentsPath; }

    /// Whether to use the external path as the name for this file or directory.
    bool useExternalName(bool GlobalUseExternalName) const {
      return UseName == NK_NotSet ? GlobalUseExternalName
                                  : (UseName == NK_External);
    }

    NameKind getUseName() const { return UseName; }

    static bool classof(const Entry *E) {
      switch (E->getKind()) {
      case EK_DirectoryRemap:
        [[fallthrough]];
      case EK_File:
        return true;
      case EK_Directory:
        return false;
      }
      llvm_unreachable("invalid entry kind");
    }
  };

  /// A directory in the vfs that maps to a directory in the external file
  /// system.
  class DirectoryRemapEntry : public RemapEntry {
  public:
    DirectoryRemapEntry(StringRef Name, StringRef ExternalContentsPath,
                        NameKind UseName)
        : RemapEntry(EK_DirectoryRemap, Name, ExternalContentsPath, UseName) {}

    static bool classof(const Entry *E) {
      return E->getKind() == EK_DirectoryRemap;
    }
  };

  /// A file in the vfs that maps to a file in the external file system.
  class FileEntry : public RemapEntry {
  public:
    FileEntry(StringRef Name, StringRef ExternalContentsPath, NameKind UseName)
        : RemapEntry(EK_File, Name, ExternalContentsPath, UseName) {}

    static bool classof(const Entry *E) { return E->getKind() == EK_File; }
  };

  /// Represents the result of a path lookup into the RedirectingFileSystem.
  struct LookupResult {
    /// Chain of parent directory entries for \c E.
    llvm::SmallVector<Entry *, 32> Parents;

    /// The entry the looked-up path corresponds to.
    Entry *E;

  private:
    /// When the found Entry is a DirectoryRemapEntry, stores the path in the
    /// external file system that the looked-up path in the virtual file system
    //  corresponds to.
    std::optional<SmallString<256>> ExternalRedirect;

  public:
    LookupResult(Entry *E, sys::path::const_iterator Start,
                 sys::path::const_iterator End);

    /// If the found Entry maps the input path to a path in the external
    /// file system (i.e. it is a FileEntry or DirectoryRemapEntry), returns
    /// that path.
    /// Returns StringRef() (null data) if no external redirect.
    StringRef getExternalRedirect() const {
      if (isa<DirectoryRemapEntry>(E))
        return StringRef(*ExternalRedirect);
      if (auto *FE = dyn_cast<FileEntry>(E))
        return FE->getExternalContentsPath();
      return StringRef();
    }

    /// Get the (canonical) path of the found entry. This uses the as-written
    /// path components from the VFS specification.
    void getPath(llvm::SmallVectorImpl<char> &Path) const;
  };

private:
  friend class RedirectingFSDirIterImpl;
  friend class RedirectingFileSystemParser;

  /// Canonicalize path by removing ".", "..", "./", components. This is
  /// a VFS request, do not bother about symlinks in the path components
  /// but canonicalize in order to perform the correct entry search.
  std::error_code makeCanonical(SmallVectorImpl<char> &Path) const;

  /// Get the File status, or error, from the underlying external file system.
  /// This returns the status with the originally requested name, while looking
  /// up the entry using the canonical path.
  ErrorOr<Status> getExternalStatus(const Twine &CanonicalPath,
                                    const Twine &OriginalPath) const;

  /// Make \a Path an absolute path.
  ///
  /// Makes \a Path absolute using the \a WorkingDir if it is not already.
  ///
  /// /absolute/path   => /absolute/path
  /// relative/../path => <WorkingDir>/relative/../path
  ///
  /// \param WorkingDir  A path that will be used as the base Dir if \a Path
  ///                    is not already absolute.
  /// \param Path A path that is modified to be an absolute path.
  /// \returns success if \a path has been made absolute, otherwise a
  ///          platform-specific error_code.
  std::error_code makeAbsolute(StringRef WorkingDir,
                               SmallVectorImpl<char> &Path) const;

  // In a RedirectingFileSystem, keys can be specified in Posix or Windows
  // style (or even a mixture of both), so this comparison helper allows
  // slashes (representing a root) to match backslashes (and vice versa).  Note
  // that, other than the root, path components should not contain slashes or
  // backslashes.
  bool pathComponentMatches(llvm::StringRef lhs, llvm::StringRef rhs) const {
    if ((CaseSensitive ? lhs.equals(rhs) : lhs.equals_insensitive(rhs)))
      return true;
    return (lhs == "/" && rhs == "\\") || (lhs == "\\" && rhs == "/");
  }

  /// The root(s) of the virtual file system.
  SmallVector<std::unique_ptr<Entry>, 4> Roots;

  /// The current working directory of the file system.
  SmallString<256> WorkingDirectory;

  /// The file system to use for external references.
  IntrusiveRefCntPtr<FileSystem> ExternalFS;

  /// This represents the directory path that the YAML file is located.
  /// This will be prefixed to each 'external-contents' if IsRelativeOverlay
  /// is set. This will also be prefixed to each 'roots->name' if RootRelative
  /// is set to RootRelativeKind::OverlayDir and the path is relative.
  SmallString<256> OverlayFileDir;

  /// @name Configuration
  /// @{

  /// Whether to perform case-sensitive comparisons.
  ///
  /// Currently, case-insensitive matching only works correctly with ASCII.
  bool CaseSensitive = is_style_posix(sys::path::Style::native);

  /// IsRelativeOverlay marks whether a OverlayFileDir path must
  /// be prefixed in every 'external-contents' when reading from YAML files.
  bool IsRelativeOverlay = false;

  /// Whether to use to use the value of 'external-contents' for the
  /// names of files.  This global value is overridable on a per-file basis.
  bool UseExternalNames = true;

  /// Determines the lookups to perform, as well as their order. See
  /// \c RedirectKind for details.
  RedirectKind Redirection = RedirectKind::Fallthrough;

  /// Determine the prefix directory if the roots are relative paths. See
  /// \c RootRelativeKind for details.
  RootRelativeKind RootRelative = RootRelativeKind::CWD;
  /// @}

  RedirectingFileSystem(IntrusiveRefCntPtr<FileSystem> ExternalFS);

  /// Looks up the path <tt>[Start, End)</tt> in \p From, possibly recursing
  /// into the contents of \p From if it is a directory. Returns a LookupResult
  /// giving the matched entry and, if that entry is a FileEntry or
  /// DirectoryRemapEntry, the path it redirects to in the external file system.
  ErrorOr<LookupResult>
  lookupPathImpl(llvm::sys::path::const_iterator Start,
                 llvm::sys::path::const_iterator End, Entry *From,
                 llvm::SmallVectorImpl<Entry *> &Entries) const;

  /// Get the status for a path with the provided \c LookupResult.
  ErrorOr<Status> status(const Twine &CanonicalPath, const Twine &OriginalPath,
                         const LookupResult &Result);

public:
  /// Looks up \p Path in \c Roots and returns a LookupResult giving the
  /// matched entry and, if the entry was a FileEntry or DirectoryRemapEntry,
  /// the path it redirects to in the external file system.
  ErrorOr<LookupResult> lookupPath(StringRef Path) const;

  /// Parses \p Buffer, which is expected to be in YAML format and
  /// returns a virtual file system representing its contents.
  static std::unique_ptr<RedirectingFileSystem>
  create(std::unique_ptr<MemoryBuffer> Buffer,
         SourceMgr::DiagHandlerTy DiagHandler, StringRef YAMLFilePath,
         void *DiagContext, IntrusiveRefCntPtr<FileSystem> ExternalFS);

  /// Redirect each of the remapped files from first to second.
  struct RemappedFile {
    SmallString<256> From, To;
  };
  static std::unique_ptr<RedirectingFileSystem>
  create(ArrayRef<RemappedFile> RemappedFiles, bool UseExternalNames,
         FileSystem &ExternalFS);

  ErrorOr<Status> status(const Twine &Path) override;
  ErrorOr<std::unique_ptr<File>> openFileForRead(const Twine &Path) override;

  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) const override;

  llvm::ErrorOr<SmallString<256>> getCurrentWorkingDirectory() const override;

  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;

  std::error_code isLocal(const Twine &Path, bool &Result) override;

  std::error_code makeAbsolute(SmallVectorImpl<char> &Path) const override;

  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;

  void setOverlayFileDir(StringRef PrefixDir);

  StringRef getOverlayFileDir() const;

  /// Sets the redirection kind to \c Fallthrough if true or \c RedirectOnly
  /// otherwise. Will removed in the future, use \c setRedirection instead.
  void setFallthrough(bool Fallthrough);

  void setRedirection(RedirectingFileSystem::RedirectKind Kind);

  SmallVector<llvm::StringRef, 4> getRoots() const;

  void printEntry(raw_ostream &OS, Entry *E, unsigned IndentLevel = 0) const;

protected:
  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override;
};

/// Collect all pairs of <virtual path, real path> entries from the
/// \p YAMLFilePath. This is used by the module dependency collector to forward
/// the entries into the reproducer output VFS YAML file.
void collectVFSFromYAML(
    std::unique_ptr<llvm::MemoryBuffer> Buffer,
    llvm::SourceMgr::DiagHandlerTy DiagHandler, StringRef YAMLFilePath,
    SmallVectorImpl<YAMLVFSEntry> &CollectedEntries,
    void *DiagContext = nullptr,
    IntrusiveRefCntPtr<FileSystem> ExternalFS = getRealFileSystem());

class YAMLVFSWriter {
  SmallVector<YAMLVFSEntry, 0> Mappings;
  int IsCaseSensitive = -1;
  int IsOverlayRelative = -1;
  int UseExternalNames = -1;
  SmallString<256> OverlayDir;

  void addEntry(StringRef VirtualPath, StringRef RealPath, bool IsDirectory);

public:
  YAMLVFSWriter() = default;

  void addFileMapping(StringRef VirtualPath, StringRef RealPath);
  void addDirectoryMapping(StringRef VirtualPath, StringRef RealPath);

  void setCaseSensitivity(bool CaseSensitive) {
    IsCaseSensitive = CaseSensitive ? 1 : 0;
  }

  void setUseExternalNames(bool UseExtNames) {
    UseExternalNames = UseExtNames ? 1 : 0;
  }

  void setOverlayDir(StringRef OverlayDirectory) {
    IsOverlayRelative = 1;
    OverlayDir.assign(OverlayDirectory.str());
  }

  ArrayRef<YAMLVFSEntry> getMappings() const { return Mappings; }

  void write(llvm::raw_ostream &OS);
};

} // namespace vfs
} // namespace llvm

// ===== Inline implementations (moved from VFS section of cpp_bridge.cpp) =====

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/YAMLTraits.h"

extern "C" {
int csupport_path_get_existing_style(const char *path, size_t len);
int csupport_path_is_traversal_component(const char *comp, size_t len);
size_t csupport_path_canonicalize(const char *path, size_t len, char *out,
                                  size_t out_cap);
}
#ifndef CSUPPORT_PATH_STYLE_POSIX
#define CSUPPORT_PATH_STYLE_POSIX 1
#endif
#ifndef CSUPPORT_PATH_STYLE_WINDOWS
#define CSUPPORT_PATH_STYLE_WINDOWS 2
#endif

#ifndef CMOVE
#define CMOVE(x) std::move(x)
#endif

namespace llvm {
namespace vfs {

using errc_t = std::error_code;
template <typename T> using uptr_t = std::unique_ptr<T>;
using SysClock = std::chrono::system_clock;
using sys::fs::file_status;
using sys::fs::file_t;
using sys::fs::file_type;
using sys::fs::kInvalidFile;
using sys::fs::perms;
using sys::fs::UniqueID;

inline Status::Status(const file_status &Status)
    : UID(Status.getUniqueID()), MTime(Status.getLastModificationTime()),
      User(Status.getUser()), Group(Status.getGroup()), Size(Status.getSize()),
      Type(Status.type()), Perms(Status.permissions()) {}

inline Status::Status(const Twine &Name, UniqueID UID, sys::TimePoint<> MTime,
                      uint32_t User, uint32_t Group, uint64_t Size,
                      file_type Type, perms Perms)
    : Name(Name.str()), UID(UID), MTime(MTime), User(User), Group(Group),
      Size(Size), Type(Type), Perms(Perms) {}

inline Status Status::copyWithNewSize(const Status &In, uint64_t NewSize) {
  return Status(In.getName(), In.getUniqueID(), In.getLastModificationTime(),
                In.getUser(), In.getGroup(), NewSize, In.getType(),
                In.getPermissions());
}

inline Status Status::copyWithNewName(const Status &In, const Twine &NewName) {
  return Status(NewName, In.getUniqueID(), In.getLastModificationTime(),
                In.getUser(), In.getGroup(), In.getSize(), In.getType(),
                In.getPermissions());
}

inline Status Status::copyWithNewName(const file_status &In,
                                      const Twine &NewName) {
  return Status(NewName, In.getUniqueID(), In.getLastModificationTime(),
                In.getUser(), In.getGroup(), In.getSize(), In.type(),
                In.permissions());
}

inline bool Status::equivalent(const Status &Other) const {
  assert(isStatusKnown() && Other.isStatusKnown());
  return getUniqueID() == Other.getUniqueID();
}

inline bool Status::isDirectory() const {
  return Type == file_type::directory_file;
}

inline bool Status::isRegularFile() const {
  return Type == file_type::regular_file;
}

inline bool Status::isOther() const {
  return exists() && !isRegularFile() && !isDirectory() && !isSymlink();
}

inline bool Status::isSymlink() const {
  return Type == file_type::symlink_file;
}

inline bool Status::isStatusKnown() const {
  return Type != file_type::status_error;
}

inline bool Status::exists() const {
  return isStatusKnown() && Type != file_type::file_not_found;
}

inline File::~File() = default;

inline FileSystem::~FileSystem() = default;

inline ErrorOr<uptr_t<MemoryBuffer>>
FileSystem::getBufferForFile(const llvm::Twine &Name, int64_t FileSize,
                             bool RequiresNullTerminator, bool IsVolatile) {
  auto F = openFileForRead(Name);
  if (!F)
    return F.getError();

  return (*F)->getBuffer(Name, FileSize, RequiresNullTerminator, IsVolatile);
}

inline errc_t FileSystem::makeAbsolute(SmallVectorImpl<char> &Path) const {
  if (llvm::sys::path::is_absolute(Path))
    return {};

  auto WorkingDir = getCurrentWorkingDirectory();
  if (!WorkingDir)
    return WorkingDir.getError();

  llvm::sys::fs::make_absolute(WorkingDir.get(), Path);
  return {};
}

inline errc_t FileSystem::getRealPath(const Twine &Path,
                                      SmallVectorImpl<char> &Output) const {
  return errc::operation_not_permitted;
}

inline errc_t FileSystem::isLocal(const Twine &Path, bool &Result) {
  return errc::operation_not_permitted;
}

inline bool FileSystem::exists(const Twine &Path) {
  auto Status = status(Path);
  return Status && Status->exists();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
inline void FileSystem::dump() const {
  print(dbgs(), PrintType::RecursiveContents);
}
#endif

#ifndef NDEBUG
inline bool isTraversalComponent(StringRef Component) {
  return csupport_path_is_traversal_component(Component.data(),
                                              Component.size()) != 0;
}

inline bool pathHasTraversal(StringRef Path) {
  using namespace llvm::sys;

  for (StringRef Comp : llvm::make_range(path::begin(Path), path::end(Path)))
    if (isTraversalComponent(Comp))
      return true;
  return false;
}
#endif

//===-----------------------------------------------------------------------===/
// RealFileSystem implementation
//===-----------------------------------------------------------------------===/

namespace detail {

/// Wrapper around a raw file descriptor.
class RealFile : public File {
  friend class RealFileSystem;

  file_t FD;
  Status S;
  SmallString<256> RealName;

  RealFile(file_t RawFD, StringRef NewName, StringRef NewRealPathName)
      : FD(RawFD), S(NewName, {}, {}, {}, {}, {},
                     llvm::sys::fs::file_type::status_error, {}),
        RealName(NewRealPathName) {
    assert(FD != kInvalidFile && "Invalid or inactive file descriptor");
  }

public:
  ~RealFile() override;

  ErrorOr<Status> status() override;
  ErrorOr<SmallString<256>> getName() override;
  ErrorOr<uptr_t<MemoryBuffer>> getBuffer(const Twine &Name, int64_t FileSize,
                                          bool RequiresNullTerminator,
                                          bool IsVolatile) override;
  errc_t close() override;
  void setPath(const Twine &Path) override;
};

} // namespace detail

namespace detail {
inline RealFile::~RealFile() { close(); }

inline ErrorOr<Status> RealFile::status() {
  assert(FD != kInvalidFile && "cannot stat closed file");
  if (!S.isStatusKnown()) {
    file_status RealStatus;
    if (errc_t EC = sys::fs::status(FD, RealStatus))
      return EC;
    S = Status::copyWithNewName(RealStatus, S.getName());
  }
  return S;
}

inline ErrorOr<SmallString<256>> RealFile::getName() {
  return RealName.empty() ? SmallString<256>(S.getName()) : RealName;
}

inline ErrorOr<uptr_t<MemoryBuffer>>
RealFile::getBuffer(const Twine &Name, int64_t FileSize,
                    bool RequiresNullTerminator, bool IsVolatile) {
  assert(FD != kInvalidFile && "cannot get buffer for closed file");
  return MemoryBuffer::getOpenFile(FD, Name, FileSize, RequiresNullTerminator,
                                   IsVolatile);
}

inline errc_t RealFile::close() {
  auto EC = sys::fs::closeFile(FD);
  FD = kInvalidFile;
  return EC;
}

inline void RealFile::setPath(const Twine &Path) {
  RealName = Path.str();
  if (auto Status = status())
    S = Status.get().copyWithNewName(Status.get(), Path);
}
} // namespace detail

namespace detail {

/// A file system according to your operating system.
/// This may be linked to the process's working directory, or maintain its own.
///
/// Currently, its own working directory is emulated by storing the path and
/// sending absolute paths to llvm::sys::fs:: functions.
/// A more principled approach would be to push this down a level, modelling
/// the working dir as an llvm::sys::fs::WorkingDir or similar.
/// This would enable the use of openat()-style functions on some platforms.
class RealFileSystem : public FileSystem {
public:
  explicit RealFileSystem(bool LinkCWDToProcess) {
    if (!LinkCWDToProcess) {
      SmallString<128> PWD, RealPWD;
      if (errc_t EC = llvm::sys::fs::current_path(PWD)) {
        WD = EC;
        WDInit = true;
      } else if (llvm::sys::fs::real_path(PWD, RealPWD)) {
        WD = WorkingDirectory{PWD, PWD};
        WDInit = true;
      } else {
        WD = WorkingDirectory{PWD, RealPWD};
        WDInit = true;
      }
    }
  }

  ErrorOr<Status> status(const Twine &Path) override;
  ErrorOr<uptr_t<File>> openFileForRead(const Twine &Path) override;
  directory_iterator dir_begin(const Twine &Dir, errc_t &EC) override;

  llvm::ErrorOr<SmallString<256>> getCurrentWorkingDirectory() const override;
  errc_t setCurrentWorkingDirectory(const Twine &Path) override;
  errc_t isLocal(const Twine &Path, bool &Result) override;
  errc_t getRealPath(const Twine &Path,
                     SmallVectorImpl<char> &Output) const override;

protected:
  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override;

private:
  // If this FS has its own working dir, use it to make Path absolute.
  // The returned twine is safe to use as long as both Storage and Path live.
  Twine adjustPath(const Twine &Path, SmallVectorImpl<char> &Storage) const {
    if (!WDInit || !WD)
      return Path;
    Path.toVector(Storage);
    sys::fs::make_absolute(WD.get().Resolved, Storage);
    return Storage;
  }

  struct WorkingDirectory {
    SmallString<128> Specified;
    SmallString<128> Resolved;
  };
  llvm::ErrorOr<WorkingDirectory> WD = WorkingDirectory{};
  bool WDInit = false;
};

} // namespace detail

namespace detail {
inline ErrorOr<Status> RealFileSystem::status(const Twine &Path) {
  SmallString<256> Storage;
  sys::fs::file_status RealStatus;
  if (errc_t EC = sys::fs::status(adjustPath(Path, Storage), RealStatus))
    return EC;
  return Status::copyWithNewName(RealStatus, Path);
}

inline ErrorOr<uptr_t<File>>
RealFileSystem::openFileForRead(const Twine &Name) {
  SmallString<256> RealName, Storage;
  Expected<file_t> FDOrErr = sys::fs::openNativeFileForRead(
      adjustPath(Name, Storage), sys::fs::OF_None, &RealName);
  if (!FDOrErr)
    return errorToErrorCode(FDOrErr.takeError());
  return uptr_t<File>(new RealFile(*FDOrErr, Name.str(), RealName.str()));
}

inline llvm::ErrorOr<SmallString<256>>
RealFileSystem::getCurrentWorkingDirectory() const {
  if (WDInit && WD)
    return SmallString<256>(WD.get().Specified);
  if (WDInit)
    return WD.getError();

  SmallString<128> Dir;
  if (errc_t EC = llvm::sys::fs::current_path(Dir))
    return EC;
  return SmallString<256>(Dir);
}

inline errc_t RealFileSystem::setCurrentWorkingDirectory(const Twine &Path) {
  if (!WDInit)
    return llvm::sys::fs::set_current_path(Path);

  SmallString<128> Absolute, Resolved, Storage;
  adjustPath(Path, Storage).toVector(Absolute);
  bool IsDir;
  if (auto Err = llvm::sys::fs::is_directory(Absolute, IsDir))
    return Err;
  if (!IsDir)
    return make_error_code(llvm::errc::not_a_directory);
  if (auto Err = llvm::sys::fs::real_path(Absolute, Resolved))
    return Err;
  WD = WorkingDirectory{Absolute, Resolved};
  WDInit = true;
  return {};
}

inline errc_t RealFileSystem::isLocal(const Twine &Path, bool &Result) {
  SmallString<256> Storage;
  return llvm::sys::fs::is_local(adjustPath(Path, Storage), Result);
}

inline errc_t RealFileSystem::getRealPath(const Twine &Path,
                                          SmallVectorImpl<char> &Output) const {
  SmallString<256> Storage;
  return llvm::sys::fs::real_path(adjustPath(Path, Storage), Output);
}

inline void RealFileSystem::printImpl(raw_ostream &OS, PrintType Type,
                                      unsigned IndentLevel) const {
  printIndent(OS, IndentLevel);
  OS << "RealFileSystem using ";
  if (WDInit)
    OS << "own";
  else
    OS << "process";
  OS << " CWD\n";
}
} // namespace detail

inline IntrusiveRefCntPtr<FileSystem> getRealFileSystem() {
  static IntrusiveRefCntPtr<FileSystem> FS(new detail::RealFileSystem(true));
  return FS;
}

inline uptr_t<FileSystem> createPhysicalFileSystem() {
  return uptr_t<detail::RealFileSystem>(new detail::RealFileSystem(false));
}

namespace detail {

class RealFSDirIter : public detail::DirIterImpl {
  llvm::sys::fs::directory_iterator Iter;

public:
  RealFSDirIter(const Twine &Path, errc_t &EC) : Iter(Path, EC) {
    if (Iter != llvm::sys::fs::directory_iterator())
      CurrentEntry = directory_entry(Iter->path(), Iter->type());
  }

  errc_t increment() override {
    errc_t EC;
    Iter.increment(EC);
    CurrentEntry = (Iter == llvm::sys::fs::directory_iterator())
                       ? directory_entry()
                       : directory_entry(Iter->path(), Iter->type());
    return EC;
  }
};

} // namespace detail

namespace detail {
inline directory_iterator RealFileSystem::dir_begin(const Twine &Dir,
                                                    errc_t &EC) {
  SmallString<128> Storage;
  return directory_iterator(IntrusiveRefCntPtr<RealFSDirIter>(
      new RealFSDirIter(adjustPath(Dir, Storage), EC)));
}
} // namespace detail

//===-----------------------------------------------------------------------===/
// OverlayFileSystem implementation
//===-----------------------------------------------------------------------===/

inline OverlayFileSystem::OverlayFileSystem(
    IntrusiveRefCntPtr<FileSystem> BaseFS) {
  FSList.push_back(BaseFS);
}

inline void OverlayFileSystem::pushOverlay(IntrusiveRefCntPtr<FileSystem> FS) {
  FSList.push_back(FS);
  // Synchronize added file systems by duplicating the working directory from
  // the first one in the list.
  FS->setCurrentWorkingDirectory(getCurrentWorkingDirectory().get());
}

inline ErrorOr<Status> OverlayFileSystem::status(const Twine &Path) {
  // FIXME: handle symlinks that cross file systems
  for (iterator I = overlays_begin(), E = overlays_end(); I != E; ++I) {
    ErrorOr<Status> Status = (*I)->status(Path);
    if (Status || Status.getError() != llvm::errc::no_such_file_or_directory)
      return Status;
  }
  return make_error_code(llvm::errc::no_such_file_or_directory);
}

inline ErrorOr<uptr_t<File>>
OverlayFileSystem::openFileForRead(const llvm::Twine &Path) {
  // FIXME: handle symlinks that cross file systems
  for (iterator I = overlays_begin(), E = overlays_end(); I != E; ++I) {
    auto Result = (*I)->openFileForRead(Path);
    if (Result || Result.getError() != llvm::errc::no_such_file_or_directory)
      return Result;
  }
  return make_error_code(llvm::errc::no_such_file_or_directory);
}

inline llvm::ErrorOr<SmallString<256>>
OverlayFileSystem::getCurrentWorkingDirectory() const {
  return FSList.front()->getCurrentWorkingDirectory();
}

inline errc_t OverlayFileSystem::setCurrentWorkingDirectory(const Twine &Path) {
  for (auto &FS : FSList)
    if (errc_t EC = FS->setCurrentWorkingDirectory(Path))
      return EC;
  return {};
}

inline errc_t OverlayFileSystem::isLocal(const Twine &Path, bool &Result) {
  for (auto &FS : FSList)
    if (FS->exists(Path))
      return FS->isLocal(Path, Result);
  return errc::no_such_file_or_directory;
}

inline errc_t
OverlayFileSystem::getRealPath(const Twine &Path,
                               SmallVectorImpl<char> &Output) const {
  for (const auto &FS : FSList)
    if (FS->exists(Path))
      return FS->getRealPath(Path, Output);
  return errc::no_such_file_or_directory;
}

inline void OverlayFileSystem::printImpl(raw_ostream &OS, PrintType Type,
                                         unsigned IndentLevel) const {
  printIndent(OS, IndentLevel);
  OS << "OverlayFileSystem\n";
  if (Type == PrintType::Summary)
    return;

  if (Type == PrintType::Contents)
    Type = PrintType::Summary;
  for (const auto &FS : overlays_range())
    FS->print(OS, Type, IndentLevel + 1);
}

inline detail::DirIterImpl::~DirIterImpl() = default;

namespace detail {

/// Combines and deduplicates directory entries across multiple file systems.
class CombiningDirIterImpl : public detail::DirIterImpl {
  using FileSystemPtr = llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>;

  /// Iterators to combine, processed in reverse order.
  SmallVector<directory_iterator, 8> IterList;
  /// The iterator currently being traversed.
  directory_iterator CurrentDirIter;
  /// The set of names already returned as entries.
  llvm::StringSet<> SeenNames;

  /// Sets \c CurrentDirIter to the next iterator in the list, or leaves it as
  /// is (at its end position) if we've already gone through them all.
  errc_t incrementIter(bool IsFirstTime) {
    while (!IterList.empty()) {
      CurrentDirIter = IterList.back();
      IterList.pop_back();
      if (CurrentDirIter != directory_iterator())
        break; // found
    }

    if (IsFirstTime && CurrentDirIter == directory_iterator())
      return errc::no_such_file_or_directory;
    return {};
  }

  errc_t incrementDirIter(bool IsFirstTime) {
    assert((IsFirstTime || CurrentDirIter != directory_iterator()) &&
           "incrementing past end");
    errc_t EC;
    if (!IsFirstTime)
      CurrentDirIter.increment(EC);
    if (!EC && CurrentDirIter == directory_iterator())
      EC = incrementIter(IsFirstTime);
    return EC;
  }

  errc_t incrementImpl(bool IsFirstTime) {
    while (true) {
      auto EC = incrementDirIter(IsFirstTime);
      if (EC || CurrentDirIter == directory_iterator()) {
        CurrentEntry = directory_entry();
        return EC;
      }
      CurrentEntry = *CurrentDirIter;
      StringRef Name = llvm::sys::path::filename(CurrentEntry.path());
      if (SeenNames.insert(Name).second)
        return EC; // name not seen before
    }
    llvm_unreachable("returned above");
  }

public:
  CombiningDirIterImpl(ArrayRef<FileSystemPtr> FileSystems, StringRef Dir,
                       errc_t &EC) {
    for (const auto &FS : FileSystems) {
      errc_t FEC;
      directory_iterator Iter = FS->dir_begin(Dir, FEC);
      if (FEC && FEC != errc::no_such_file_or_directory) {
        EC = FEC;
        return;
      }
      if (!FEC)
        IterList.push_back(Iter);
    }
    EC = incrementImpl(true);
  }

  CombiningDirIterImpl(ArrayRef<directory_iterator> DirIters, errc_t &EC)
      : IterList(DirIters.begin(), DirIters.end()) {
    EC = incrementImpl(true);
  }

  errc_t increment() override { return incrementImpl(false); }
};

} // namespace detail

inline directory_iterator OverlayFileSystem::dir_begin(const Twine &Dir,
                                                       errc_t &EC) {
  directory_iterator Combined =
      directory_iterator(IntrusiveRefCntPtr<detail::CombiningDirIterImpl>(
          new detail::CombiningDirIterImpl(FSList, Dir.str(), EC)));
  if (EC)
    return {};
  return Combined;
}

inline void ProxyFileSystem::anchor() {}

namespace detail {

enum InMemoryNodeKind {
  IME_File,
  IME_Directory,
  IME_HardLink,
  IME_SymbolicLink,
};

/// The in memory file system is a tree of Nodes. Every node can either be a
/// file, symlink, hardlink or a directory.
class InMemoryNode {
  InMemoryNodeKind Kind;
  SmallString<64> FileName;

public:
  InMemoryNode(llvm::StringRef FileName, InMemoryNodeKind Kind)
      : Kind(Kind), FileName(llvm::sys::path::filename(FileName)) {}
  virtual ~InMemoryNode() = default;

  /// Return the \p Status for this node. \p RequestedName should be the name
  /// through which the caller referred to this node. It will override
  /// \p Status::Name in the return value, to mimic the behavior of \p RealFile.
  virtual Status getStatus(const Twine &RequestedName) const = 0;

  /// Get the filename of this node (the name without the directory part).
  StringRef getFileName() const { return FileName; }
  InMemoryNodeKind getKind() const { return Kind; }
  virtual SmallString<256> toString(unsigned Indent) const = 0;
};

class InMemoryFile : public InMemoryNode {
  Status Stat;
  uptr_t<llvm::MemoryBuffer> Buffer;

public:
  InMemoryFile(Status Stat, uptr_t<llvm::MemoryBuffer> Buffer)
      : InMemoryNode(Stat.getName(), IME_File), Stat(Stat),
        Buffer(CMOVE(Buffer)) {}

  Status getStatus(const Twine &RequestedName) const override {
    return Status::copyWithNewName(Stat, RequestedName);
  }
  llvm::MemoryBuffer *getBuffer() const { return Buffer.get(); }

  SmallString<256> toString(unsigned Indent) const override {
    SmallString<256> R;
    R.assign(Indent, ' ');
    R += Stat.getName();
    R += "\n";
    return R;
  }

  static bool classof(const InMemoryNode *N) {
    return N->getKind() == IME_File;
  }
};

class InMemoryHardLink : public InMemoryNode {
  const InMemoryFile &ResolvedFile;

public:
  InMemoryHardLink(StringRef Path, const InMemoryFile &ResolvedFile)
      : InMemoryNode(Path, IME_HardLink), ResolvedFile(ResolvedFile) {}
  const InMemoryFile &getResolvedFile() const { return ResolvedFile; }

  Status getStatus(const Twine &RequestedName) const override {
    return ResolvedFile.getStatus(RequestedName);
  }

  SmallString<256> toString(unsigned Indent) const override {
    SmallString<256> R;
    R.assign(Indent, ' ');
    R += "HardLink to -> ";
    R += ResolvedFile.toString(0);
    return R;
  }

  static bool classof(const InMemoryNode *N) {
    return N->getKind() == IME_HardLink;
  }
};

class InMemorySymbolicLink : public InMemoryNode {
  SmallString<256> TargetPath;
  Status Stat;

public:
  InMemorySymbolicLink(StringRef Path, StringRef TargetPath, Status Stat)
      : InMemoryNode(Path, IME_SymbolicLink), TargetPath(TargetPath),
        Stat(Stat) {}

  SmallString<256> toString(unsigned Indent) const override {
    SmallString<256> R;
    R.assign(Indent, ' ');
    R += "SymbolicLink to -> ";
    R += TargetPath;
    return R;
  }

  Status getStatus(const Twine &RequestedName) const override {
    return Status::copyWithNewName(Stat, RequestedName);
  }

  StringRef getTargetPath() const { return TargetPath; }

  static bool classof(const InMemoryNode *N) {
    return N->getKind() == IME_SymbolicLink;
  }
};

/// Adapt a InMemoryFile for VFS' File interface.  The goal is to make
/// \p InMemoryFileAdaptor mimic as much as possible the behavior of
/// \p RealFile.
class InMemoryFileAdaptor : public File {
  const InMemoryFile &Node;
  SmallString<64> RequestedName;

public:
  explicit InMemoryFileAdaptor(const InMemoryFile &Node,
                               StringRef RequestedName)
      : Node(Node), RequestedName(RequestedName) {}

  llvm::ErrorOr<Status> status() override {
    return Node.getStatus(RequestedName);
  }

  llvm::ErrorOr<uptr_t<llvm::MemoryBuffer>>
  getBuffer(const Twine &Name, int64_t FileSize, bool RequiresNullTerminator,
            bool IsVolatile) override {
    llvm::MemoryBuffer *Buf = Node.getBuffer();
    return llvm::MemoryBuffer::getMemBuffer(
        Buf->getBuffer(), Buf->getBufferIdentifier(), RequiresNullTerminator);
  }

  errc_t close() override { return {}; }

  void setPath(const Twine &Path) override { RequestedName = Path.str(); }
};

class InMemoryDirectory : public InMemoryNode {
  Status Stat;
  llvm::StringMap<uptr_t<InMemoryNode>> Entries;

public:
  InMemoryDirectory(Status Stat)
      : InMemoryNode(Stat.getName(), IME_Directory), Stat(Stat) {}

  /// Return the \p Status for this node. \p RequestedName should be the name
  /// through which the caller referred to this node. It will override
  /// \p Status::Name in the return value, to mimic the behavior of \p RealFile.
  Status getStatus(const Twine &RequestedName) const override {
    return Status::copyWithNewName(Stat, RequestedName);
  }

  UniqueID getUniqueID() const { return Stat.getUniqueID(); }

  InMemoryNode *getChild(StringRef Name) const {
    auto I = Entries.find(Name);
    if (I != Entries.end())
      return I->second.get();
    return 0;
  }

  InMemoryNode *addChild(StringRef Name, uptr_t<InMemoryNode> Child) {
    return Entries.try_emplace(Name, CMOVE(Child)).first->second.get();
  }

  using const_iterator = decltype(Entries)::const_iterator;

  const_iterator begin() const { return Entries.begin(); }
  const_iterator end() const { return Entries.end(); }

  SmallString<256> toString(unsigned Indent) const override {
    SmallString<256> Result;
    Result.append(Indent, ' ');
    Result.append(Stat.getName());
    Result.append("\n");
    for (const auto &Entry : Entries)
      Result.append(Entry.second->toString(Indent + 2));
    return Result;
  }

  static bool classof(const InMemoryNode *N) {
    return N->getKind() == IME_Directory;
  }
};

} // namespace detail

// The UniqueID of in-memory files is derived from path and content.
// This avoids difficulties in creating exactly equivalent in-memory FSes,
// as often needed in multithreaded programs.
inline sys::fs::UniqueID getUniqueID(hash_code Hash) {
  return sys::fs::UniqueID(UINT64_MAX, uint64_t(size_t(Hash)));
}
inline sys::fs::UniqueID getFileID(sys::fs::UniqueID Parent,
                                   llvm::StringRef Name,
                                   llvm::StringRef Contents) {
  return getUniqueID(llvm::hash_combine(Parent.getFile(), Name, Contents));
}
inline sys::fs::UniqueID getDirectoryID(sys::fs::UniqueID Parent,
                                        llvm::StringRef Name) {
  return getUniqueID(llvm::hash_combine(Parent.getFile(), Name));
}

inline Status detail::NewInMemoryNodeInfo::makeStatus() const {
  UniqueID UID =
      (Type == sys::fs::file_type::directory_file)
          ? getDirectoryID(DirUID, Name)
          : getFileID(DirUID, Name, Buffer ? Buffer->getBuffer() : "");

  return Status(Path, UID, llvm::sys::toTimePoint(ModificationTime), User,
                Group, Buffer ? Buffer->getBufferSize() : 0, Type, Perms);
}

inline InMemoryFileSystem::InMemoryFileSystem(bool UseNormalizedPaths)
    : Root(new detail::InMemoryDirectory(
          Status("", getDirectoryID(llvm::sys::fs::UniqueID(), ""),
                 llvm::sys::TimePoint<>(), 0, 0, 0,
                 llvm::sys::fs::file_type::directory_file,
                 llvm::sys::fs::perms::all_all))),
      UseNormalizedPaths(UseNormalizedPaths) {}

inline InMemoryFileSystem::~InMemoryFileSystem() = default;

inline SmallString<256> InMemoryFileSystem::toString() const {
  return Root->toString(/*Indent=*/0);
}

inline bool InMemoryFileSystem::addFile(const Twine &P, time_t ModificationTime,
                                        uptr_t<llvm::MemoryBuffer> Buffer,
                                        uint32_t User, uint32_t Group,
                                        llvm::sys::fs::file_type Type,
                                        llvm::sys::fs::perms Perms,
                                        MakeNodeFn MakeNode) {
  SmallString<128> Path;
  P.toVector(Path);

  // Fix up relative paths. This just prepends the current working directory.
  auto EC = makeAbsolute(Path);
  assert(!EC);
  (void)EC;

  if (useNormalizedPaths())
    llvm::sys::path::remove_dots(Path, /*remove_dot_dot=*/true);

  if (Path.empty())
    return false;

  detail::InMemoryDirectory *Dir = Root.get();
  auto I = llvm::sys::path::begin(Path), E = sys::path::end(Path);
  const auto ResolvedUser = User;
  const auto ResolvedGroup = Group;
  const auto ResolvedType = Type;
  const auto ResolvedPerms = Perms;
  // Any intermediate directories we create should be accessible by
  // the owner, even if Perms says otherwise for the final path.
  const auto NewDirectoryPerms = ResolvedPerms | sys::fs::owner_all;
  while (true) {
    StringRef Name = *I;
    detail::InMemoryNode *Node = Dir->getChild(Name);
    ++I;
    if (!Node) {
      if (I == E) {
        // End of the path.
        Dir->addChild(Name,
                      MakeNode({Dir->getUniqueID(), Path, Name,
                                ModificationTime, CMOVE(Buffer), ResolvedUser,
                                ResolvedGroup, ResolvedType, ResolvedPerms}));
        return true;
      }

      // Create a new directory. Use the path up to here.
      Status Stat(
          StringRef(Path.str().begin(), Name.end() - Path.str().begin()),
          getDirectoryID(Dir->getUniqueID(), Name),
          llvm::sys::toTimePoint(ModificationTime), ResolvedUser, ResolvedGroup,
          0, sys::fs::file_type::directory_file, NewDirectoryPerms);
      Dir = cast<detail::InMemoryDirectory>(
          Dir->addChild(Name, uptr_t<detail::InMemoryDirectory>(
                                  new detail::InMemoryDirectory(Stat))));
      continue;
    }

    if (auto *NewDir = dyn_cast<detail::InMemoryDirectory>(Node)) {
      Dir = NewDir;
    } else {
      assert((isa<detail::InMemoryFile>(Node) ||
              isa<detail::InMemoryHardLink>(Node)) &&
             "Must be either file, hardlink or directory!");

      // Trying to insert a directory in place of a file.
      if (I != E)
        return false;

      // Return false only if the new file is different from the existing one.
      if (auto Link = dyn_cast<detail::InMemoryHardLink>(Node)) {
        return Link->getResolvedFile().getBuffer()->getBuffer() ==
               Buffer->getBuffer();
      }
      return cast<detail::InMemoryFile>(Node)->getBuffer()->getBuffer() ==
             Buffer->getBuffer();
    }
  }
}

inline bool InMemoryFileSystem::addFile(const Twine &P, time_t ModificationTime,
                                        uptr_t<llvm::MemoryBuffer> Buffer,
                                        uint32_t User, uint32_t Group,
                                        llvm::sys::fs::file_type Type,
                                        llvm::sys::fs::perms Perms) {
  return addFile(
      P, ModificationTime, CMOVE(Buffer), User, Group, Type, Perms,
      [](detail::NewInMemoryNodeInfo NNI) -> uptr_t<detail::InMemoryNode> {
        Status Stat = NNI.makeStatus();
        if (Stat.getType() == sys::fs::file_type::directory_file)
          return uptr_t<detail::InMemoryDirectory>(
              new detail::InMemoryDirectory(Stat));
        return uptr_t<detail::InMemoryFile>(
            new detail::InMemoryFile(Stat, CMOVE(NNI.Buffer)));
      });
}

inline bool InMemoryFileSystem::addFileNoOwn(
    const Twine &P, time_t ModificationTime,
    const llvm::MemoryBufferRef &Buffer, uint32_t User, uint32_t Group,
    llvm::sys::fs::file_type Type, llvm::sys::fs::perms Perms) {
  return addFile(
      P, ModificationTime, llvm::MemoryBuffer::getMemBuffer(Buffer), User,
      Group, Type, Perms,
      [](detail::NewInMemoryNodeInfo NNI) -> uptr_t<detail::InMemoryNode> {
        Status Stat = NNI.makeStatus();
        if (Stat.getType() == sys::fs::file_type::directory_file)
          return uptr_t<detail::InMemoryDirectory>(
              new detail::InMemoryDirectory(Stat));
        return uptr_t<detail::InMemoryFile>(
            new detail::InMemoryFile(Stat, CMOVE(NNI.Buffer)));
      });
}

inline detail::NamedNodeOrError
InMemoryFileSystem::lookupNode(const Twine &P, bool FollowFinalSymlink,
                               size_t SymlinkDepth) const {
  SmallString<128> Path;
  P.toVector(Path);

  // Fix up relative paths. This just prepends the current working directory.
  auto EC = makeAbsolute(Path);
  assert(!EC);
  (void)EC;

  if (useNormalizedPaths())
    llvm::sys::path::remove_dots(Path, /*remove_dot_dot=*/true);

  const detail::InMemoryDirectory *Dir = Root.get();
  if (Path.empty())
    return detail::NamedNodeOrError(Path, Dir);

  auto I = llvm::sys::path::begin(Path), E = llvm::sys::path::end(Path);
  while (true) {
    detail::InMemoryNode *Node = Dir->getChild(*I);
    ++I;
    if (!Node)
      return errc::no_such_file_or_directory;

    if (auto Symlink = dyn_cast<detail::InMemorySymbolicLink>(Node)) {
      // If we're at the end of the path, and we're not following through
      // terminal symlinks, then we're done.
      if (I == E && !FollowFinalSymlink)
        return detail::NamedNodeOrError(Path, Symlink);

      if (SymlinkDepth > InMemoryFileSystem::MaxSymlinkDepth)
        return errc::no_such_file_or_directory;

      SmallString<128> TargetPath = Symlink->getTargetPath();
      if (errc_t EC = makeAbsolute(TargetPath))
        return EC;

      // Keep going with the target. We always want to follow symlinks here
      // because we're either at the end of a path that we want to follow, or
      // not at the end of a path, in which case we need to follow the symlink
      // regardless.
      auto Target =
          lookupNode(TargetPath, /*FollowFinalSymlink=*/true, SymlinkDepth + 1);
      if (!Target || I == E)
        return Target;

      if (!isa<detail::InMemoryDirectory>(*Target))
        return errc::no_such_file_or_directory;

      // Otherwise, continue on the search in the symlinked directory.
      Dir = cast<detail::InMemoryDirectory>(*Target);
      continue;
    }

    // Return the file if it's at the end of the path.
    if (auto File = dyn_cast<detail::InMemoryFile>(Node)) {
      if (I == E)
        return detail::NamedNodeOrError(Path, File);
      return errc::no_such_file_or_directory;
    }

    // If Node is HardLink then return the resolved file.
    if (auto File = dyn_cast<detail::InMemoryHardLink>(Node)) {
      if (I == E)
        return detail::NamedNodeOrError(Path, &File->getResolvedFile());
      return errc::no_such_file_or_directory;
    }
    // Traverse directories.
    Dir = cast<detail::InMemoryDirectory>(Node);
    if (I == E)
      return detail::NamedNodeOrError(Path, Dir);
  }
}

inline bool InMemoryFileSystem::addHardLink(const Twine &NewLink,
                                            const Twine &Target) {
  auto NewLinkNode = lookupNode(NewLink, /*FollowFinalSymlink=*/false);
  // Whether symlinks in the hardlink target are followed is
  // implementation-defined in POSIX.
  // We're following symlinks here to be consistent with macOS.
  auto TargetNode = lookupNode(Target, /*FollowFinalSymlink=*/true);
  // FromPath must not have been added before. ToPath must have been added
  // before. Resolved ToPath must be a File.
  if (!TargetNode || NewLinkNode || !isa<detail::InMemoryFile>(*TargetNode))
    return false;
  return addFile(
      NewLink, 0, 0, 0, 0, sys::fs::file_type::regular_file, sys::fs::all_all,
      [&](detail::NewInMemoryNodeInfo NNI) {
        return uptr_t<detail::InMemoryHardLink>(new detail::InMemoryHardLink(
            NNI.Path.str(), *cast<detail::InMemoryFile>(*TargetNode)));
      });
}

inline bool InMemoryFileSystem::addSymbolicLink(const Twine &NewLink,
                                                const Twine &Target,
                                                time_t ModificationTime,
                                                uint32_t User, uint32_t Group,
                                                llvm::sys::fs::perms Perms) {
  auto NewLinkNode = lookupNode(NewLink, /*FollowFinalSymlink=*/false);
  if (NewLinkNode)
    return false;

  SmallString<128> NewLinkStr, TargetStr;
  NewLink.toVector(NewLinkStr);
  Target.toVector(TargetStr);

  return addFile(NewLinkStr, ModificationTime, 0, User, Group,
                 sys::fs::file_type::symlink_file, Perms,
                 [&](detail::NewInMemoryNodeInfo NNI) {
                   return uptr_t<detail::InMemorySymbolicLink>(
                       new detail::InMemorySymbolicLink(NewLinkStr, TargetStr,
                                                        NNI.makeStatus()));
                 });
}

inline llvm::ErrorOr<Status> InMemoryFileSystem::status(const Twine &Path) {
  auto Node = lookupNode(Path, /*FollowFinalSymlink=*/true);
  if (Node)
    return (*Node)->getStatus(Path);
  return Node.getError();
}

inline llvm::ErrorOr<uptr_t<File>>
InMemoryFileSystem::openFileForRead(const Twine &Path) {
  auto Node = lookupNode(Path, /*FollowFinalSymlink=*/true);
  if (!Node)
    return Node.getError();

  // When we have a file provide a heap-allocated wrapper for the memory buffer
  // to match the ownership semantics for File.
  if (auto *F = dyn_cast<detail::InMemoryFile>(*Node))
    return uptr_t<File>(new detail::InMemoryFileAdaptor(*F, Path.str()));

  // FIXME: errc::not_a_file?
  return make_error_code(llvm::errc::invalid_argument);
}

/// Adaptor from InMemoryDir::iterator to directory_iterator.
class InMemoryFileSystem::DirIterator : public detail::DirIterImpl {
  const InMemoryFileSystem *FS;
  detail::InMemoryDirectory::const_iterator I;
  detail::InMemoryDirectory::const_iterator E;
  SmallString<256> RequestedDirName;

  void setCurrentEntry() {
    if (I != E) {
      SmallString<256> Path(RequestedDirName);
      llvm::sys::path::append(Path, I->second->getFileName());
      sys::fs::file_type Type = sys::fs::file_type::type_unknown;
      switch (I->second->getKind()) {
      case detail::IME_File:
      case detail::IME_HardLink:
        Type = sys::fs::file_type::regular_file;
        break;
      case detail::IME_Directory:
        Type = sys::fs::file_type::directory_file;
        break;
      case detail::IME_SymbolicLink:
        if (auto SymlinkTarget =
                FS->lookupNode(Path, /*FollowFinalSymlink=*/true)) {
          Path = SymlinkTarget.getName();
          Type = (*SymlinkTarget)->getStatus(Path).getType();
        }
        break;
      }
      CurrentEntry = directory_entry(SmallString<256>(Path).str().str(), Type);
    } else {
      CurrentEntry = directory_entry();
    }
  }

public:
  DirIterator() = default;

  DirIterator(const InMemoryFileSystem *FS,
              const detail::InMemoryDirectory &Dir, StringRef RequestedDirName)
      : FS(FS), I(Dir.begin()), E(Dir.end()),
        RequestedDirName(RequestedDirName) {
    setCurrentEntry();
  }

  errc_t increment() override {
    ++I;
    setCurrentEntry();
    return {};
  }
};

inline directory_iterator InMemoryFileSystem::dir_begin(const Twine &Dir,
                                                        errc_t &EC) {
  auto Node = lookupNode(Dir, /*FollowFinalSymlink=*/true);
  if (!Node) {
    EC = Node.getError();
    return directory_iterator(
        IntrusiveRefCntPtr<DirIterator>(new DirIterator()));
  }

  if (auto *DirNode = dyn_cast<detail::InMemoryDirectory>(*Node))
    return directory_iterator(IntrusiveRefCntPtr<DirIterator>(
        new DirIterator(this, *DirNode, Dir.str())));

  EC = make_error_code(llvm::errc::not_a_directory);
  return directory_iterator(IntrusiveRefCntPtr<DirIterator>(new DirIterator()));
}

inline errc_t InMemoryFileSystem::setCurrentWorkingDirectory(const Twine &P) {
  SmallString<128> Path;
  P.toVector(Path);

  // Fix up relative paths. This just prepends the current working directory.
  auto EC = makeAbsolute(Path);
  assert(!EC);
  (void)EC;

  if (useNormalizedPaths())
    llvm::sys::path::remove_dots(Path, /*remove_dot_dot=*/true);

  if (!Path.empty())
    WorkingDirectory = SmallString<256>(Path);
  return {};
}

inline errc_t
InMemoryFileSystem::getRealPath(const Twine &Path,
                                SmallVectorImpl<char> &Output) const {
  auto CWD = getCurrentWorkingDirectory();
  if (!CWD || CWD->empty())
    return errc::operation_not_permitted;
  Path.toVector(Output);
  if (auto EC = makeAbsolute(Output))
    return EC;
  llvm::sys::path::remove_dots(Output, /*remove_dot_dot=*/true);
  return {};
}

inline errc_t InMemoryFileSystem::isLocal(const Twine &Path, bool &Result) {
  Result = false;
  return {};
}

inline void InMemoryFileSystem::printImpl(raw_ostream &OS,
                                          PrintType PrintContents,
                                          unsigned IndentLevel) const {
  printIndent(OS, IndentLevel);
  OS << "InMemoryFileSystem\n";
}

//===-----------------------------------------------------------------------===/
// RedirectingFileSystem implementation
//===-----------------------------------------------------------------------===/

inline llvm::sys::path::Style getExistingStyle(llvm::StringRef Path) {
  int s = csupport_path_get_existing_style(Path.data(), Path.size());
  if (s == CSUPPORT_PATH_STYLE_POSIX)
    return llvm::sys::path::Style::posix;
  if (s == CSUPPORT_PATH_STYLE_WINDOWS)
    return llvm::sys::path::Style::windows_backslash;
  return llvm::sys::path::Style::native;
}

inline llvm::SmallString<256> canonicalize(llvm::StringRef Path) {
  char buf[512];
  size_t n =
      csupport_path_canonicalize(Path.data(), Path.size(), buf, sizeof(buf));
  return llvm::SmallString<256>(StringRef(buf, n));
}

/// Whether the error and entry specify a file/directory that was not found.
inline bool isFileNotFound(errc_t EC, RedirectingFileSystem::Entry *E = 0) {
  if (E && !isa<RedirectingFileSystem::DirectoryRemapEntry>(E))
    return false;
  return EC == llvm::errc::no_such_file_or_directory;
}

inline RedirectingFileSystem::RedirectingFileSystem(
    IntrusiveRefCntPtr<FileSystem> FS)
    : ExternalFS(FS) {
  if (ExternalFS)
    if (auto ExternalWorkingDirectory =
            ExternalFS->getCurrentWorkingDirectory()) {
      WorkingDirectory = *ExternalWorkingDirectory;
    }
}

/// Directory iterator implementation for \c RedirectingFileSystem's
/// directory entries.
class RedirectingFSDirIterImpl : public detail::DirIterImpl {
  SmallString<128> Dir;
  RedirectingFileSystem::DirectoryEntry::iterator Current, End;

  errc_t incrementImpl(bool IsFirstTime) {
    assert((IsFirstTime || Current != End) && "cannot iterate past end");
    if (!IsFirstTime)
      ++Current;
    if (Current != End) {
      SmallString<128> PathStr(Dir);
      llvm::sys::path::append(PathStr, (*Current)->getName());
      sys::fs::file_type Type = sys::fs::file_type::type_unknown;
      switch ((*Current)->getKind()) {
      case RedirectingFileSystem::EK_Directory:
        __attribute__((fallthrough));
      case RedirectingFileSystem::EK_DirectoryRemap:
        Type = sys::fs::file_type::directory_file;
        break;
      case RedirectingFileSystem::EK_File:
        Type = sys::fs::file_type::regular_file;
        break;
      }
      CurrentEntry =
          directory_entry(SmallString<256>(PathStr).str().str(), Type);
    } else {
      CurrentEntry = directory_entry();
    }
    return {};
  };

public:
  RedirectingFSDirIterImpl(
      const Twine &Path, RedirectingFileSystem::DirectoryEntry::iterator Begin,
      RedirectingFileSystem::DirectoryEntry::iterator End, errc_t &EC)
      : Dir(Path.str()), Current(Begin), End(End) {
    EC = incrementImpl(/*IsFirstTime=*/true);
  }

  errc_t increment() override { return incrementImpl(/*IsFirstTime=*/false); }
};

namespace detail {
/// Directory iterator implementation for \c RedirectingFileSystem's
/// directory remap entries that maps the paths reported by the external
/// file system's directory iterator back to the virtual directory's path.
class RedirectingFSDirRemapIterImpl : public detail::DirIterImpl {
  SmallString<256> Dir;
  llvm::sys::path::Style DirStyle;
  llvm::vfs::directory_iterator ExternalIter;

public:
  RedirectingFSDirRemapIterImpl(StringRef DirPath,
                                llvm::vfs::directory_iterator ExtIter)
      : Dir(DirPath), DirStyle(getExistingStyle(Dir)), ExternalIter(ExtIter) {
    if (ExternalIter != llvm::vfs::directory_iterator())
      setCurrentEntry();
  }

  void setCurrentEntry() {
    StringRef ExternalPath = ExternalIter->path();
    llvm::sys::path::Style ExternalStyle = getExistingStyle(ExternalPath);
    StringRef File = llvm::sys::path::filename(ExternalPath, ExternalStyle);

    SmallString<128> NewPath(Dir);
    llvm::sys::path::append(NewPath, DirStyle, File);

    CurrentEntry = directory_entry(SmallString<256>(NewPath).str().str(),
                                   ExternalIter->type());
  }

  errc_t increment() override {
    errc_t EC;
    ExternalIter.increment(EC);
    if (!EC && ExternalIter != llvm::vfs::directory_iterator())
      setCurrentEntry();
    else
      CurrentEntry = directory_entry();
    return EC;
  }
};
} // namespace detail

inline llvm::ErrorOr<SmallString<256>>
RedirectingFileSystem::getCurrentWorkingDirectory() const {
  return SmallString<256>(WorkingDirectory);
}

inline errc_t
RedirectingFileSystem::setCurrentWorkingDirectory(const Twine &Path) {
  // Don't change the working directory if the path doesn't exist.
  if (!exists(Path))
    return errc::no_such_file_or_directory;

  SmallString<128> AbsolutePath;
  Path.toVector(AbsolutePath);
  if (errc_t EC = makeAbsolute(AbsolutePath))
    return EC;
  WorkingDirectory = SmallString<256>(AbsolutePath);
  return {};
}

inline errc_t RedirectingFileSystem::isLocal(const Twine &Path_, bool &Result) {
  SmallString<256> Path;
  Path_.toVector(Path);

  if (makeCanonical(Path))
    return {};

  return ExternalFS->isLocal(Path, Result);
}

inline errc_t
RedirectingFileSystem::makeAbsolute(SmallVectorImpl<char> &Path) const {
  // is_absolute(..., Style::windows_*) accepts paths with both slash types.
  if (llvm::sys::path::is_absolute(Path, llvm::sys::path::Style::posix) ||
      llvm::sys::path::is_absolute(Path,
                                   llvm::sys::path::Style::windows_backslash))
    // This covers windows absolute path with forward slash as well, as the
    // forward slashes are treated as path seperation in llvm::path
    // regardless of what path::Style is used.
    return {};

  auto WorkingDir = getCurrentWorkingDirectory();
  if (!WorkingDir)
    return WorkingDir.getError();

  return makeAbsolute(WorkingDir.get(), Path);
}

inline errc_t
RedirectingFileSystem::makeAbsolute(StringRef WorkingDir,
                                    SmallVectorImpl<char> &Path) const {
  // We can't use sys::fs::make_absolute because that assumes the path style
  // is native and there is no way to override that.  Since we know WorkingDir
  // is absolute, we can use it to determine which style we actually have and
  // append Path ourselves.
  if (!WorkingDir.empty() &&
      !sys::path::is_absolute(WorkingDir, sys::path::Style::posix) &&
      !sys::path::is_absolute(WorkingDir,
                              sys::path::Style::windows_backslash)) {
    return {};
  }
  sys::path::Style style = sys::path::Style::windows_backslash;
  if (sys::path::is_absolute(WorkingDir, sys::path::Style::posix)) {
    style = sys::path::Style::posix;
  } else {
    // Distinguish between windows_backslash and windows_slash; getExistingStyle
    // returns posix for a path with windows_slash.
    if (getExistingStyle(WorkingDir) != sys::path::Style::windows_backslash)
      style = sys::path::Style::windows_slash;
  }

  SmallString<512> Result(WorkingDir);
  if (!StringRef(Result).ends_with(sys::path::get_separator(style))) {
    Result += sys::path::get_separator(style);
  }
  Result.append(Path.data(), Path.data() + Path.size());
  Path.assign(Result.begin(), Result.end());

  return {};
}

inline directory_iterator RedirectingFileSystem::dir_begin(const Twine &Dir,
                                                           errc_t &EC) {
  SmallString<256> Path;
  Dir.toVector(Path);

  EC = makeCanonical(Path);
  if (EC)
    return {};

  ErrorOr<RedirectingFileSystem::LookupResult> Result = lookupPath(Path);
  if (!Result) {
    if (Redirection != RedirectKind::RedirectOnly &&
        isFileNotFound(Result.getError()))
      return ExternalFS->dir_begin(Path, EC);

    EC = Result.getError();
    return {};
  }

  // Use status to make sure the path exists and refers to a directory.
  ErrorOr<Status> S = status(Path, Dir, *Result);
  if (!S) {
    if (Redirection != RedirectKind::RedirectOnly &&
        isFileNotFound(S.getError(), Result->E))
      return ExternalFS->dir_begin(Dir, EC);

    EC = S.getError();
    return {};
  }

  if (!S->isDirectory()) {
    EC = errc::not_a_directory;
    return {};
  }

  // Create the appropriate directory iterator based on whether we found a
  // DirectoryRemapEntry or DirectoryEntry.
  directory_iterator RedirectIter;
  errc_t RedirectEC;
  StringRef ExtRedirectDir = Result->getExternalRedirect();
  if (ExtRedirectDir.data()) {
    auto RE = cast<RedirectingFileSystem::RemapEntry>(Result->E);
    RedirectIter = ExternalFS->dir_begin(ExtRedirectDir, RedirectEC);

    if (!RE->useExternalName(UseExternalNames)) {
      // Update the paths in the results to use the virtual directory's path.
      RedirectIter = directory_iterator(
          IntrusiveRefCntPtr<detail::RedirectingFSDirRemapIterImpl>(
              new detail::RedirectingFSDirRemapIterImpl(Path, RedirectIter)));
    }
  } else {
    auto DE = cast<DirectoryEntry>(Result->E);
    RedirectIter =
        directory_iterator(IntrusiveRefCntPtr<RedirectingFSDirIterImpl>(
            new RedirectingFSDirIterImpl(Path, DE->contents_begin(),
                                         DE->contents_end(), RedirectEC)));
  }

  if (RedirectEC) {
    if (RedirectEC != errc::no_such_file_or_directory) {
      EC = RedirectEC;
      return {};
    }
    RedirectIter = {};
  }

  if (Redirection == RedirectKind::RedirectOnly) {
    EC = RedirectEC;
    return RedirectIter;
  }

  errc_t ExternalEC;
  directory_iterator ExternalIter = ExternalFS->dir_begin(Path, ExternalEC);
  if (ExternalEC) {
    if (ExternalEC != errc::no_such_file_or_directory) {
      EC = ExternalEC;
      return {};
    }
    ExternalIter = {};
  }

  SmallVector<directory_iterator, 2> Iters;
  switch (Redirection) {
  case RedirectKind::Fallthrough:
    Iters.push_back(ExternalIter);
    Iters.push_back(RedirectIter);
    break;
  case RedirectKind::Fallback:
    Iters.push_back(RedirectIter);
    Iters.push_back(ExternalIter);
    break;
  default:
    llvm_unreachable("unhandled RedirectKind");
  }

  directory_iterator Combined{IntrusiveRefCntPtr<detail::CombiningDirIterImpl>(
      new detail::CombiningDirIterImpl(Iters, EC))};
  if (EC)
    return {};
  return Combined;
}

inline void RedirectingFileSystem::setOverlayFileDir(StringRef Dir) {
  OverlayFileDir = Dir.str();
}

inline StringRef RedirectingFileSystem::getOverlayFileDir() const {
  return OverlayFileDir;
}

inline void RedirectingFileSystem::setFallthrough(bool Fallthrough) {
  if (Fallthrough) {
    Redirection = RedirectingFileSystem::RedirectKind::Fallthrough;
  } else {
    Redirection = RedirectingFileSystem::RedirectKind::RedirectOnly;
  }
}

inline void RedirectingFileSystem::setRedirection(
    RedirectingFileSystem::RedirectKind Kind) {
  Redirection = Kind;
}

inline SmallVector<StringRef, 4> RedirectingFileSystem::getRoots() const {
  SmallVector<StringRef, 4> R;
  R.reserve(Roots.size());
  for (const auto &Root : Roots)
    R.push_back(Root->getName());
  return R;
}

inline void RedirectingFileSystem::printImpl(raw_ostream &OS, PrintType Type,
                                             unsigned IndentLevel) const {
  printIndent(OS, IndentLevel);
  OS << "RedirectingFileSystem (UseExternalNames: "
     << (UseExternalNames ? "true" : "false") << ")\n";
  if (Type == PrintType::Summary)
    return;

  for (const auto &Root : Roots)
    printEntry(OS, Root.get(), IndentLevel);

  printIndent(OS, IndentLevel);
  OS << "ExternalFS:\n";
  ExternalFS->print(OS, Type == PrintType::Contents ? PrintType::Summary : Type,
                    IndentLevel + 1);
}

inline void RedirectingFileSystem::printEntry(raw_ostream &OS,
                                              RedirectingFileSystem::Entry *E,
                                              unsigned IndentLevel) const {
  printIndent(OS, IndentLevel);
  OS << "'" << E->getName() << "'";

  switch (E->getKind()) {
  case EK_Directory: {
    auto *DE = cast<RedirectingFileSystem::DirectoryEntry>(E);

    OS << "\n";
    for (uptr_t<Entry> &SubEntry :
         llvm::make_range(DE->contents_begin(), DE->contents_end()))
      printEntry(OS, SubEntry.get(), IndentLevel + 1);
    break;
  }
  case EK_DirectoryRemap:
  case EK_File: {
    auto *RE = cast<RedirectingFileSystem::RemapEntry>(E);
    OS << " -> '" << RE->getExternalContentsPath() << "'";
    switch (RE->getUseName()) {
    case NK_NotSet:
      break;
    case NK_External:
      OS << " (UseExternalName: true)";
      break;
    case NK_Virtual:
      OS << " (UseExternalName: false)";
      break;
    }
    OS << "\n";
    break;
  }
  }
}

/// A helper class to hold the common YAML parsing state.
class RedirectingFileSystemParser {
  yaml::Stream &Stream;

  void error(yaml::Node *N, const Twine &Msg) { Stream.printError(N, Msg); }

  // false on error
  bool parseScalarString(yaml::Node *N, StringRef &Result,
                         SmallVectorImpl<char> &Storage) {
    const auto *S = dyn_cast<yaml::ScalarNode>(N);

    if (!S) {
      error(N, "expected string");
      return false;
    }
    Result = S->getValue(Storage);
    return true;
  }

  // false on error
  bool parseScalarBool(yaml::Node *N, bool &Result) {
    SmallString<5> Storage;
    StringRef Value;
    if (!parseScalarString(N, Value, Storage))
      return false;

    if (Value.equals_insensitive("true") || Value.equals_insensitive("on") ||
        Value.equals_insensitive("yes") || Value == "1") {
      Result = true;
      return true;
    } else if (Value.equals_insensitive("false") ||
               Value.equals_insensitive("off") ||
               Value.equals_insensitive("no") || Value == "0") {
      Result = false;
      return true;
    }

    error(N, "expected boolean value");
    return false;
  }

  int parseRedirectKind(yaml::Node *N) {
    SmallString<12> Storage;
    StringRef Value;
    if (!parseScalarString(N, Value, Storage))
      return -1;

    if (Value.equals_insensitive("fallthrough")) {
      return (int)RedirectingFileSystem::RedirectKind::Fallthrough;
    } else if (Value.equals_insensitive("fallback")) {
      return (int)RedirectingFileSystem::RedirectKind::Fallback;
    } else if (Value.equals_insensitive("redirect-only")) {
      return (int)RedirectingFileSystem::RedirectKind::RedirectOnly;
    }
    return -1;
  }

  int parseRootRelativeKind(yaml::Node *N) {
    SmallString<12> Storage;
    StringRef Value;
    if (!parseScalarString(N, Value, Storage))
      return -1;
    if (Value.equals_insensitive("cwd")) {
      return (int)RedirectingFileSystem::RootRelativeKind::CWD;
    } else if (Value.equals_insensitive("overlay-dir")) {
      return (int)RedirectingFileSystem::RootRelativeKind::OverlayDir;
    }
    return -1;
  }

  struct KeyStatus {
    bool Required;
    bool Seen = false;

    KeyStatus(bool Required = false) : Required(Required) {}
  };

  struct KeyStatusPair {
    StringRef first;
    KeyStatus second;
    KeyStatusPair(StringRef k, bool req = false) : first(k), second(req) {}
  };

  // false on error
  bool checkDuplicateOrUnknownKey(yaml::Node *KeyNode, StringRef Key,
                                  DenseMap<StringRef, KeyStatus> &Keys) {
    if (!Keys.count(Key)) {
      error(KeyNode, "unknown key");
      return false;
    }
    KeyStatus &S = Keys[Key];
    if (S.Seen) {
      error(KeyNode, Twine("duplicate key '") + Key + "'");
      return false;
    }
    S.Seen = true;
    return true;
  }

  // false on error
  bool checkMissingKeys(yaml::Node *Obj, DenseMap<StringRef, KeyStatus> &Keys) {
    for (const auto &I : Keys) {
      if (I.second.Required && !I.second.Seen) {
        error(Obj, Twine("missing key '") + I.first + "'");
        return false;
      }
    }
    return true;
  }

public:
  static RedirectingFileSystem::Entry *
  lookupOrCreateEntry(RedirectingFileSystem *FS, StringRef Name,
                      RedirectingFileSystem::Entry *ParentEntry = 0) {
    if (!ParentEntry) { // Look for a existent root
      for (const auto &Root : FS->Roots) {
        if (Name.equals(Root->getName())) {
          ParentEntry = Root.get();
          return ParentEntry;
        }
      }
    } else { // Advance to the next component
      auto *DE = dyn_cast<RedirectingFileSystem::DirectoryEntry>(ParentEntry);
      for (uptr_t<RedirectingFileSystem::Entry> &Content :
           llvm::make_range(DE->contents_begin(), DE->contents_end())) {
        auto *DirContent =
            dyn_cast<RedirectingFileSystem::DirectoryEntry>(Content.get());
        if (DirContent && Name.equals(Content->getName()))
          return DirContent;
      }
    }

    // ... or create a new one
    uptr_t<RedirectingFileSystem::Entry> E =
        uptr_t<RedirectingFileSystem::DirectoryEntry>(
            new RedirectingFileSystem::DirectoryEntry(
                Name,
                Status("", getNextVirtualUniqueID(), SysClock::now(), 0, 0, 0,
                       file_type::directory_file, sys::fs::all_all)));

    if (!ParentEntry) { // Add a new root to the overlay
      FS->Roots.push_back(CMOVE(E));
      ParentEntry = FS->Roots.back().get();
      return ParentEntry;
    }

    auto *DE = cast<RedirectingFileSystem::DirectoryEntry>(ParentEntry);
    DE->addContent(CMOVE(E));
    return DE->getLastContent();
  }

private:
  void uniqueOverlayTree(RedirectingFileSystem *FS,
                         RedirectingFileSystem::Entry *SrcE,
                         RedirectingFileSystem::Entry *NewParentE = 0) {
    StringRef Name = SrcE->getName();
    switch (SrcE->getKind()) {
    case RedirectingFileSystem::EK_Directory: {
      auto *DE = cast<RedirectingFileSystem::DirectoryEntry>(SrcE);
      // Empty directories could be present in the YAML as a way to
      // describe a file for a current directory after some of its subdir
      // is parsed. This only leads to redundant walks, ignore it.
      if (!Name.empty())
        NewParentE = lookupOrCreateEntry(FS, Name, NewParentE);
      for (uptr_t<RedirectingFileSystem::Entry> &SubEntry :
           llvm::make_range(DE->contents_begin(), DE->contents_end()))
        uniqueOverlayTree(FS, SubEntry.get(), NewParentE);
      break;
    }
    case RedirectingFileSystem::EK_DirectoryRemap: {
      assert(NewParentE && "Parent entry must exist");
      auto *DR = cast<RedirectingFileSystem::DirectoryRemapEntry>(SrcE);
      auto *DE = cast<RedirectingFileSystem::DirectoryEntry>(NewParentE);
      DE->addContent(uptr_t<RedirectingFileSystem::DirectoryRemapEntry>(
          new RedirectingFileSystem::DirectoryRemapEntry(
              Name, DR->getExternalContentsPath(), DR->getUseName())));
      break;
    }
    case RedirectingFileSystem::EK_File: {
      assert(NewParentE && "Parent entry must exist");
      auto *FE = cast<RedirectingFileSystem::FileEntry>(SrcE);
      auto *DE = cast<RedirectingFileSystem::DirectoryEntry>(NewParentE);
      DE->addContent(uptr_t<RedirectingFileSystem::FileEntry>(
          new RedirectingFileSystem::FileEntry(
              Name, FE->getExternalContentsPath(), FE->getUseName())));
      break;
    }
    }
  }

  uptr_t<RedirectingFileSystem::Entry>
  parseEntry(yaml::Node *N, RedirectingFileSystem *FS, bool IsRootEntry) {
    auto *M = dyn_cast<yaml::MappingNode>(N);
    if (!M) {
      error(N, "expected mapping node for file or directory entry");
      return 0;
    }

    KeyStatusPair Fields[] = {
        KeyStatusPair("name", true),
        KeyStatusPair("type", true),
        KeyStatusPair("contents", false),
        KeyStatusPair("external-contents", false),
        KeyStatusPair("use-external-name", false),
    };

    DenseMap<StringRef, KeyStatus> Keys;
    for (const auto &F : Fields)
      Keys.insert({F.first, F.second});

    enum { CF_NotSet, CF_List, CF_External } ContentsField = CF_NotSet;
    SmallVector<uptr_t<RedirectingFileSystem::Entry>, 4> EntryArrayContents;
    SmallString<256> ExternalContentsPath;
    SmallString<256> Name;
    yaml::Node *NameValueNode = 0;
    auto UseExternalName = RedirectingFileSystem::NK_NotSet;
    RedirectingFileSystem::EntryKind Kind;

    for (auto &I : *M) {
      StringRef Key;
      // Reuse the buffer for key and value, since we don't look at key after
      // parsing value.
      SmallString<256> Buffer;
      if (!parseScalarString(I.getKey(), Key, Buffer))
        return 0;

      if (!checkDuplicateOrUnknownKey(I.getKey(), Key, Keys))
        return 0;

      StringRef Value;
      if (Key == "name") {
        if (!parseScalarString(I.getValue(), Value, Buffer))
          return 0;

        NameValueNode = I.getValue();
        // Guarantee that old YAML files containing paths with ".." and "."
        // are properly canonicalized before read into the VFS.
        Name = canonicalize(Value).str();
      } else if (Key == "type") {
        if (!parseScalarString(I.getValue(), Value, Buffer))
          return 0;
        if (Value == "file")
          Kind = RedirectingFileSystem::EK_File;
        else if (Value == "directory")
          Kind = RedirectingFileSystem::EK_Directory;
        else if (Value == "directory-remap")
          Kind = RedirectingFileSystem::EK_DirectoryRemap;
        else {
          error(I.getValue(), "unknown value for 'type'");
          return 0;
        }
      } else if (Key == "contents") {
        if (ContentsField != CF_NotSet) {
          error(I.getKey(),
                "entry already has 'contents' or 'external-contents'");
          return 0;
        }
        ContentsField = CF_List;
        auto *Contents = dyn_cast<yaml::SequenceNode>(I.getValue());
        if (!Contents) {
          // FIXME: this is only for directories, what about files?
          error(I.getValue(), "expected array");
          return 0;
        }

        for (auto &I : *Contents) {
          if (uptr_t<RedirectingFileSystem::Entry> E =
                  parseEntry(&I, FS, /*IsRootEntry*/ false))
            EntryArrayContents.push_back(CMOVE(E));
          else
            return 0;
        }
      } else if (Key == "external-contents") {
        if (ContentsField != CF_NotSet) {
          error(I.getKey(),
                "entry already has 'contents' or 'external-contents'");
          return 0;
        }
        ContentsField = CF_External;
        if (!parseScalarString(I.getValue(), Value, Buffer))
          return 0;

        SmallString<256> FullPath;
        if (FS->IsRelativeOverlay) {
          FullPath = FS->getOverlayFileDir();
          assert(!FullPath.empty() &&
                 "External contents prefix directory must exist");
          llvm::sys::path::append(FullPath, Value);
        } else {
          FullPath = Value;
        }

        // Guarantee that old YAML files containing paths with ".." and "."
        // are properly canonicalized before read into the VFS.
        FullPath = canonicalize(FullPath);
        ExternalContentsPath = FullPath.str();
      } else if (Key == "use-external-name") {
        bool Val;
        if (!parseScalarBool(I.getValue(), Val))
          return 0;
        UseExternalName = Val ? RedirectingFileSystem::NK_External
                              : RedirectingFileSystem::NK_Virtual;
      } else {
        llvm_unreachable("key missing from Keys");
      }
    }

    if (Stream.failed())
      return 0;

    // check for missing keys
    if (ContentsField == CF_NotSet) {
      error(N, "missing key 'contents' or 'external-contents'");
      return 0;
    }
    if (!checkMissingKeys(N, Keys))
      return 0;

    // check invalid configuration
    if (Kind == RedirectingFileSystem::EK_Directory &&
        UseExternalName != RedirectingFileSystem::NK_NotSet) {
      error(N, "'use-external-name' is not supported for 'directory' entries");
      return 0;
    }

    if (Kind == RedirectingFileSystem::EK_DirectoryRemap &&
        ContentsField == CF_List) {
      error(N, "'contents' is not supported for 'directory-remap' entries");
      return 0;
    }

    sys::path::Style path_style = sys::path::Style::native;
    if (IsRootEntry) {
      // VFS root entries may be in either Posix or Windows style.  Figure out
      // which style we have, and use it consistently.
      if (sys::path::is_absolute(Name, sys::path::Style::posix)) {
        path_style = sys::path::Style::posix;
      } else if (sys::path::is_absolute(Name,
                                        sys::path::Style::windows_backslash)) {
        path_style = sys::path::Style::windows_backslash;
      } else {
        // Relative VFS root entries are made absolute to either the overlay
        // directory, or the current working directory, then we can determine
        // the path style from that.
        errc_t EC;
        if (FS->RootRelative ==
            RedirectingFileSystem::RootRelativeKind::OverlayDir) {
          StringRef FullPath = FS->getOverlayFileDir();
          assert(!FullPath.empty() && "Overlay file directory must exist");
          EC = FS->makeAbsolute(FullPath, Name);
          Name = canonicalize(Name);
        } else {
          EC = sys::fs::make_absolute(Name);
        }
        if (EC) {
          assert(NameValueNode && "Name presence should be checked earlier");
          error(
              NameValueNode,
              "entry with relative path at the root level is not discoverable");
          return 0;
        }
        path_style = sys::path::is_absolute(Name, sys::path::Style::posix)
                         ? sys::path::Style::posix
                         : sys::path::Style::windows_backslash;
      }
      // is::path::is_absolute(Name, sys::path::Style::windows_backslash) will
      // return true even if `Name` is using forward slashes. Distinguish
      // between windows_backslash and windows_slash.
      if (path_style == sys::path::Style::windows_backslash &&
          getExistingStyle(Name) != sys::path::Style::windows_backslash)
        path_style = sys::path::Style::windows_slash;
    }

    // Remove trailing slash(es), being careful not to remove the root path
    StringRef Trimmed = Name;
    size_t RootPathLen = sys::path::root_path(Trimmed, path_style).size();
    while (Trimmed.size() > RootPathLen &&
           sys::path::is_separator(Trimmed.back(), path_style))
      Trimmed = Trimmed.slice(0, Trimmed.size() - 1);

    // Get the last component
    StringRef LastComponent = sys::path::filename(Trimmed, path_style);

    uptr_t<RedirectingFileSystem::Entry> Result;
    switch (Kind) {
    case RedirectingFileSystem::EK_File:
      Result = uptr_t<RedirectingFileSystem::FileEntry>(
          new RedirectingFileSystem::FileEntry(
              LastComponent, ExternalContentsPath, UseExternalName));
      break;
    case RedirectingFileSystem::EK_DirectoryRemap:
      Result = uptr_t<RedirectingFileSystem::DirectoryRemapEntry>(
          new RedirectingFileSystem::DirectoryRemapEntry(
              LastComponent, ExternalContentsPath, UseExternalName));
      break;
    case RedirectingFileSystem::EK_Directory:
      Result = uptr_t<RedirectingFileSystem::DirectoryEntry>(
          new RedirectingFileSystem::DirectoryEntry(
              LastComponent, CMOVE(EntryArrayContents),
              Status("", getNextVirtualUniqueID(), SysClock::now(), 0, 0, 0,
                     file_type::directory_file, sys::fs::all_all)));
      break;
    }

    StringRef Parent = sys::path::parent_path(Trimmed, path_style);
    if (Parent.empty())
      return Result;

    // if 'name' contains multiple components, create implicit directory entries
    for (sys::path::reverse_iterator I = sys::path::rbegin(Parent, path_style),
                                     E = sys::path::rend(Parent);
         I != E; ++I) {
      SmallVector<uptr_t<RedirectingFileSystem::Entry>, 2> Entries;
      Entries.push_back(CMOVE(Result));
      Result = uptr_t<RedirectingFileSystem::DirectoryEntry>(
          new RedirectingFileSystem::DirectoryEntry(
              *I, CMOVE(Entries),
              Status("", getNextVirtualUniqueID(), SysClock::now(), 0, 0, 0,
                     file_type::directory_file, sys::fs::all_all)));
    }
    return Result;
  }

public:
  RedirectingFileSystemParser(yaml::Stream &S) : Stream(S) {}

  // false on error
  bool parse(yaml::Node *Root, RedirectingFileSystem *FS) {
    auto *Top = dyn_cast<yaml::MappingNode>(Root);
    if (!Top) {
      error(Root, "expected mapping node");
      return false;
    }

    KeyStatusPair Fields[] = {
        KeyStatusPair("version", true),
        KeyStatusPair("case-sensitive", false),
        KeyStatusPair("use-external-names", false),
        KeyStatusPair("root-relative", false),
        KeyStatusPair("overlay-relative", false),
        KeyStatusPair("fallthrough", false),
        KeyStatusPair("redirecting-with", false),
        KeyStatusPair("roots", true),
    };

    DenseMap<StringRef, KeyStatus> Keys;
    for (const auto &F : Fields)
      Keys.insert({F.first, F.second});
    SmallVector<uptr_t<RedirectingFileSystem::Entry>, 4> RootEntries;

    // Parse configuration and 'roots'
    for (auto &I : *Top) {
      SmallString<10> KeyBuffer;
      StringRef Key;
      if (!parseScalarString(I.getKey(), Key, KeyBuffer))
        return false;

      if (!checkDuplicateOrUnknownKey(I.getKey(), Key, Keys))
        return false;

      if (Key == "roots") {
        auto *Roots = dyn_cast<yaml::SequenceNode>(I.getValue());
        if (!Roots) {
          error(I.getValue(), "expected array");
          return false;
        }

        for (auto &I : *Roots) {
          if (uptr_t<RedirectingFileSystem::Entry> E =
                  parseEntry(&I, FS, /*IsRootEntry*/ true))
            RootEntries.push_back(CMOVE(E));
          else
            return false;
        }
      } else if (Key == "version") {
        StringRef VersionString;
        SmallString<4> Storage;
        if (!parseScalarString(I.getValue(), VersionString, Storage))
          return false;
        int Version;
        if (VersionString.getAsInteger<int>(10, Version)) {
          error(I.getValue(), "expected integer");
          return false;
        }
        if (Version < 0) {
          error(I.getValue(), "invalid version number");
          return false;
        }
        if (Version != 0) {
          error(I.getValue(), "version mismatch, expected 0");
          return false;
        }
      } else if (Key == "case-sensitive") {
        if (!parseScalarBool(I.getValue(), FS->CaseSensitive))
          return false;
      } else if (Key == "overlay-relative") {
        if (!parseScalarBool(I.getValue(), FS->IsRelativeOverlay))
          return false;
      } else if (Key == "use-external-names") {
        if (!parseScalarBool(I.getValue(), FS->UseExternalNames))
          return false;
      } else if (Key == "fallthrough") {
        if (Keys["redirecting-with"].Seen) {
          error(I.getValue(),
                "'fallthrough' and 'redirecting-with' are mutually exclusive");
          return false;
        }

        bool ShouldFallthrough = false;
        if (!parseScalarBool(I.getValue(), ShouldFallthrough))
          return false;

        if (ShouldFallthrough) {
          FS->Redirection = RedirectingFileSystem::RedirectKind::Fallthrough;
        } else {
          FS->Redirection = RedirectingFileSystem::RedirectKind::RedirectOnly;
        }
      } else if (Key == "redirecting-with") {
        if (Keys["fallthrough"].Seen) {
          error(I.getValue(),
                "'fallthrough' and 'redirecting-with' are mutually exclusive");
          return false;
        }

        if (int Kind = parseRedirectKind(I.getValue()); Kind >= 0) {
          FS->Redirection = (RedirectingFileSystem::RedirectKind)Kind;
        } else {
          error(I.getValue(), "expected valid redirect kind");
          return false;
        }
      } else if (Key == "root-relative") {
        if (int Kind = parseRootRelativeKind(I.getValue()); Kind >= 0) {
          FS->RootRelative = (RedirectingFileSystem::RootRelativeKind)Kind;
        } else {
          error(I.getValue(), "expected valid root-relative kind");
          return false;
        }
      } else {
        llvm_unreachable("key missing from Keys");
      }
    }

    if (Stream.failed())
      return false;

    if (!checkMissingKeys(Top, Keys))
      return false;

    // Now that we sucessefully parsed the YAML file, canonicalize the internal
    // representation to a proper directory tree so that we can search faster
    // inside the VFS.
    for (auto &E : RootEntries)
      uniqueOverlayTree(FS, E.get());

    return true;
  }
};

inline uptr_t<RedirectingFileSystem>
RedirectingFileSystem::create(uptr_t<MemoryBuffer> Buffer,
                              SourceMgr::DiagHandlerTy DiagHandler,
                              StringRef YAMLFilePath, void *DiagContext,
                              IntrusiveRefCntPtr<FileSystem> ExternalFS) {
  SourceMgr SM;
  yaml::Stream Stream(Buffer->getMemBufferRef(), SM);

  SM.setDiagHandler(DiagHandler, DiagContext);
  yaml::document_iterator DI = Stream.begin();
  yaml::Node *Root = DI->getRoot();
  if (DI == Stream.end() || !Root) {
    SM.PrintMessage(SMLoc(), SourceMgr::DK_Error, "expected root node");
    return 0;
  }

  RedirectingFileSystemParser P(Stream);

  uptr_t<RedirectingFileSystem> FS(new RedirectingFileSystem(ExternalFS));

  if (!YAMLFilePath.empty()) {
    // Use the YAML path from -ivfsoverlay to compute the dir to be prefixed
    // to each 'external-contents' path.
    //
    // Example:
    //    -ivfsoverlay dummy.cache/vfs/vfs.yaml
    // yields:
    //  FS->OverlayFileDir => /<absolute_path_to>/dummy.cache/vfs
    //
    SmallString<256> OverlayAbsDir = sys::path::parent_path(YAMLFilePath);
    auto EC = llvm::sys::fs::make_absolute(OverlayAbsDir);
    assert(!EC && "Overlay dir final path must be absolute");
    (void)EC;
    FS->setOverlayFileDir(OverlayAbsDir);
  }

  if (!P.parse(Root, FS.get()))
    return 0;

  return FS;
}

inline uptr_t<RedirectingFileSystem> RedirectingFileSystem::create(
    ArrayRef<RedirectingFileSystem::RemappedFile> RemappedFiles,
    bool UseExternalNames, FileSystem &ExternalFS) {
  uptr_t<RedirectingFileSystem> FS(new RedirectingFileSystem(&ExternalFS));
  FS->UseExternalNames = UseExternalNames;

  StringMap<RedirectingFileSystem::Entry *> Entries;

  for (auto &Mapping : llvm::reverse(RemappedFiles)) {
    SmallString<128> From = StringRef(Mapping.From);
    SmallString<128> To = StringRef(Mapping.To);
    {
      auto EC = ExternalFS.makeAbsolute(From);
      (void)EC;
      assert(!EC && "Could not make absolute path");
    }

    // Check if we've already mapped this file. The first one we see (in the
    // reverse iteration) wins.
    RedirectingFileSystem::Entry *&ToEntry = Entries[From];
    if (ToEntry)
      continue;

    // Add parent directories.
    RedirectingFileSystem::Entry *Parent = 0;
    StringRef FromDirectory = llvm::sys::path::parent_path(From);
    for (auto I = llvm::sys::path::begin(FromDirectory),
              E = llvm::sys::path::end(FromDirectory);
         I != E; ++I) {
      Parent = RedirectingFileSystemParser::lookupOrCreateEntry(FS.get(), *I,
                                                                Parent);
    }
    assert(Parent && "File without a directory?");
    {
      auto EC = ExternalFS.makeAbsolute(To);
      (void)EC;
      assert(!EC && "Could not make absolute path");
    }

    // Add the file.
    auto NewFile = uptr_t<RedirectingFileSystem::FileEntry>(
        new RedirectingFileSystem::FileEntry(
            llvm::sys::path::filename(From), To,
            UseExternalNames ? RedirectingFileSystem::NK_External
                             : RedirectingFileSystem::NK_Virtual));
    ToEntry = NewFile.get();
    cast<RedirectingFileSystem::DirectoryEntry>(Parent)->addContent(
        CMOVE(NewFile));
  }

  return FS;
}

inline RedirectingFileSystem::LookupResult::LookupResult(
    Entry *E, sys::path::const_iterator Start, sys::path::const_iterator End)
    : E(E) {
  assert(E != 0);
  // If the matched entry is a DirectoryRemapEntry, set ExternalRedirect to the
  // path of the directory it maps to in the external file system plus any
  // remaining path components in the provided iterator.
  if (auto *DRE = dyn_cast<RedirectingFileSystem::DirectoryRemapEntry>(E)) {
    SmallString<256> Redirect(DRE->getExternalContentsPath());
    sys::path::append(Redirect, Start, End,
                      getExistingStyle(DRE->getExternalContentsPath()));
    ExternalRedirect = SmallString<256>(Redirect).str().str();
  }
}

inline void RedirectingFileSystem::LookupResult::getPath(
    llvm::SmallVectorImpl<char> &Result) const {
  Result.clear();
  for (Entry *Parent : Parents)
    llvm::sys::path::append(Result, Parent->getName());
  llvm::sys::path::append(Result, E->getName());
}

inline errc_t
RedirectingFileSystem::makeCanonical(SmallVectorImpl<char> &Path) const {
  if (errc_t EC = makeAbsolute(Path))
    return EC;

  llvm::SmallString<256> CanonicalPath =
      canonicalize(StringRef(Path.data(), Path.size()));
  if (CanonicalPath.empty())
    return make_error_code(llvm::errc::invalid_argument);

  Path.assign(CanonicalPath.begin(), CanonicalPath.end());
  return {};
}

inline ErrorOr<RedirectingFileSystem::LookupResult>
RedirectingFileSystem::lookupPath(StringRef Path) const {
  sys::path::const_iterator Start = sys::path::begin(Path);
  sys::path::const_iterator End = sys::path::end(Path);
  llvm::SmallVector<Entry *, 32> Entries;
  for (const auto &Root : Roots) {
    ErrorOr<RedirectingFileSystem::LookupResult> Result =
        lookupPathImpl(Start, End, Root.get(), Entries);
    if (Result || Result.getError() != llvm::errc::no_such_file_or_directory) {
      Result->Parents = Entries;
      return Result;
    }
  }
  return make_error_code(llvm::errc::no_such_file_or_directory);
}

inline ErrorOr<RedirectingFileSystem::LookupResult>
RedirectingFileSystem::lookupPathImpl(
    sys::path::const_iterator Start, sys::path::const_iterator End,
    RedirectingFileSystem::Entry *From,
    llvm::SmallVectorImpl<Entry *> &Entries) const {
  assert(!isTraversalComponent(*Start) &&
         !isTraversalComponent(From->getName()) &&
         "Paths should not contain traversal components");

  StringRef FromName = From->getName();

  // Forward the search to the next component in case this is an empty one.
  if (!FromName.empty()) {
    if (!pathComponentMatches(*Start, FromName))
      return make_error_code(llvm::errc::no_such_file_or_directory);

    ++Start;

    if (Start == End) {
      // Match!
      return LookupResult(From, Start, End);
    }
  }

  if (isa<RedirectingFileSystem::FileEntry>(From))
    return make_error_code(llvm::errc::not_a_directory);

  if (isa<RedirectingFileSystem::DirectoryRemapEntry>(From))
    return LookupResult(From, Start, End);

  auto *DE = cast<RedirectingFileSystem::DirectoryEntry>(From);
  for (const uptr_t<RedirectingFileSystem::Entry> &DirEntry :
       llvm::make_range(DE->contents_begin(), DE->contents_end())) {
    Entries.push_back(From);
    ErrorOr<RedirectingFileSystem::LookupResult> Result =
        lookupPathImpl(Start, End, DirEntry.get(), Entries);
    if (Result || Result.getError() != llvm::errc::no_such_file_or_directory)
      return Result;
    Entries.pop_back();
  }

  return make_error_code(llvm::errc::no_such_file_or_directory);
}

inline Status getRedirectedFileStatus(const Twine &OriginalPath,
                                      bool UseExternalNames,
                                      Status ExternalStatus) {
  // The path has been mapped by some nested VFS and exposes an external path,
  // don't override it with the original path.
  if (ExternalStatus.ExposesExternalVFSPath)
    return ExternalStatus;

  Status S = ExternalStatus;
  if (!UseExternalNames)
    S = Status::copyWithNewName(S, OriginalPath);
  else
    S.ExposesExternalVFSPath = true;
  S.IsVFSMapped = true;
  return S;
}

inline ErrorOr<Status> RedirectingFileSystem::status(
    const Twine &CanonicalPath, const Twine &OriginalPath,
    const RedirectingFileSystem::LookupResult &Result) {
  StringRef ExtRedirect = Result.getExternalRedirect();
  if (ExtRedirect.data()) {
    SmallString<256> CanonicalRemappedPath(ExtRedirect.str());
    if (errc_t EC = makeCanonical(CanonicalRemappedPath))
      return EC;

    ErrorOr<Status> S = ExternalFS->status(CanonicalRemappedPath);
    if (!S)
      return S;
    S = Status::copyWithNewName(*S, ExtRedirect);
    auto *RE = cast<RedirectingFileSystem::RemapEntry>(Result.E);
    return getRedirectedFileStatus(OriginalPath,
                                   RE->useExternalName(UseExternalNames), *S);
  }

  auto *DE = cast<RedirectingFileSystem::DirectoryEntry>(Result.E);
  return Status::copyWithNewName(DE->getStatus(), CanonicalPath);
}

inline ErrorOr<Status>
RedirectingFileSystem::getExternalStatus(const Twine &CanonicalPath,
                                         const Twine &OriginalPath) const {
  auto Result = ExternalFS->status(CanonicalPath);

  // The path has been mapped by some nested VFS, don't override it with the
  // original path.
  if (!Result || Result->ExposesExternalVFSPath)
    return Result;
  return Status::copyWithNewName(Result.get(), OriginalPath);
}

inline ErrorOr<Status>
RedirectingFileSystem::status(const Twine &OriginalPath) {
  SmallString<256> CanonicalPath;
  OriginalPath.toVector(CanonicalPath);

  if (errc_t EC = makeCanonical(CanonicalPath))
    return EC;

  if (Redirection == RedirectKind::Fallback) {
    // Attempt to find the original file first, only falling back to the
    // mapped file if that fails.
    ErrorOr<Status> S = getExternalStatus(CanonicalPath, OriginalPath);
    if (S)
      return S;
  }

  ErrorOr<RedirectingFileSystem::LookupResult> Result =
      lookupPath(CanonicalPath);
  if (!Result) {
    // Was not able to map file, fallthrough to using the original path if
    // that was the specified redirection type.
    if (Redirection == RedirectKind::Fallthrough &&
        isFileNotFound(Result.getError()))
      return getExternalStatus(CanonicalPath, OriginalPath);
    return Result.getError();
  }

  ErrorOr<Status> S = status(CanonicalPath, OriginalPath, *Result);
  if (!S && Redirection == RedirectKind::Fallthrough &&
      isFileNotFound(S.getError(), Result->E)) {
    // Mapped the file but it wasn't found in the underlying filesystem,
    // fallthrough to using the original path if that was the specified
    // redirection type.
    return getExternalStatus(CanonicalPath, OriginalPath);
  }

  return S;
}

namespace detail {

/// Provide a file wrapper with an overriden status.
class FileWithFixedStatus : public File {
  uptr_t<File> InnerFile;
  Status S;

public:
  FileWithFixedStatus(uptr_t<File> InnerFile, Status S)
      : InnerFile(CMOVE(InnerFile)), S(S) {}

  ErrorOr<Status> status() override { return S; }
  ErrorOr<uptr_t<llvm::MemoryBuffer>>

  getBuffer(const Twine &Name, int64_t FileSize, bool RequiresNullTerminator,
            bool IsVolatile) override {
    return InnerFile->getBuffer(Name, FileSize, RequiresNullTerminator,
                                IsVolatile);
  }

  errc_t close() override { return InnerFile->close(); }

  void setPath(const Twine &Path) override { S = S.copyWithNewName(S, Path); }
};

} // namespace detail

inline ErrorOr<uptr_t<File>> File::getWithPath(ErrorOr<uptr_t<File>> Result,
                                               const Twine &P) {
  // See \c getRedirectedFileStatus - don't update path if it's exposing an
  // external path.
  if (!Result || (*Result)->status()->ExposesExternalVFSPath)
    return Result;

  ErrorOr<uptr_t<File>> F = CMOVE(*Result);
  auto Name = F->get()->getName();
  if (Name && Name.get() != P.str())
    F->get()->setPath(P);
  return F;
}

inline ErrorOr<uptr_t<File>>
RedirectingFileSystem::openFileForRead(const Twine &OriginalPath) {
  SmallString<256> CanonicalPath;
  OriginalPath.toVector(CanonicalPath);

  if (errc_t EC = makeCanonical(CanonicalPath))
    return EC;

  if (Redirection == RedirectKind::Fallback) {
    // Attempt to find the original file first, only falling back to the
    // mapped file if that fails.
    auto F = File::getWithPath(ExternalFS->openFileForRead(CanonicalPath),
                               OriginalPath);
    if (F)
      return F;
  }

  ErrorOr<RedirectingFileSystem::LookupResult> Result =
      lookupPath(CanonicalPath);
  if (!Result) {
    // Was not able to map file, fallthrough to using the original path if
    // that was the specified redirection type.
    if (Redirection == RedirectKind::Fallthrough &&
        isFileNotFound(Result.getError()))
      return File::getWithPath(ExternalFS->openFileForRead(CanonicalPath),
                               OriginalPath);
    return Result.getError();
  }

  StringRef ExtRedirectPath = Result->getExternalRedirect();
  if (!ExtRedirectPath.data()) // FIXME: errc::not_a_file?
    return make_error_code(llvm::errc::invalid_argument);

  StringRef ExtRedirect = ExtRedirectPath;
  SmallString<256> CanonicalRemappedPath(ExtRedirect.str());
  if (errc_t EC = makeCanonical(CanonicalRemappedPath))
    return EC;

  auto *RE = cast<RedirectingFileSystem::RemapEntry>(Result->E);

  auto ExternalFile = File::getWithPath(
      ExternalFS->openFileForRead(CanonicalRemappedPath), ExtRedirect);
  if (!ExternalFile) {
    if (Redirection == RedirectKind::Fallthrough &&
        isFileNotFound(ExternalFile.getError(), Result->E)) {
      // Mapped the file but it wasn't found in the underlying filesystem,
      // fallthrough to using the original path if that was the specified
      // redirection type.
      return File::getWithPath(ExternalFS->openFileForRead(CanonicalPath),
                               OriginalPath);
    }
    return ExternalFile;
  }

  auto ExternalStatus = (*ExternalFile)->status();
  if (!ExternalStatus)
    return ExternalStatus.getError();

  // Otherwise, the file was successfully remapped. Mark it as such. Also
  // replace the underlying path if the external name is being used.
  Status S = getRedirectedFileStatus(
      OriginalPath, RE->useExternalName(UseExternalNames), *ExternalStatus);
  return uptr_t<File>(uptr_t<detail::FileWithFixedStatus>(
      new detail::FileWithFixedStatus(CMOVE(*ExternalFile), S)));
}

inline errc_t
RedirectingFileSystem::getRealPath(const Twine &OriginalPath,
                                   SmallVectorImpl<char> &Output) const {
  SmallString<256> CanonicalPath;
  OriginalPath.toVector(CanonicalPath);

  if (errc_t EC = makeCanonical(CanonicalPath))
    return EC;

  if (Redirection == RedirectKind::Fallback) {
    // Attempt to find the original file first, only falling back to the
    // mapped file if that fails.
    auto EC = ExternalFS->getRealPath(CanonicalPath, Output);
    if (!EC)
      return EC;
  }

  ErrorOr<RedirectingFileSystem::LookupResult> Result =
      lookupPath(CanonicalPath);
  if (!Result) {
    // Was not able to map file, fallthrough to using the original path if
    // that was the specified redirection type.
    if (Redirection == RedirectKind::Fallthrough &&
        isFileNotFound(Result.getError()))
      return ExternalFS->getRealPath(CanonicalPath, Output);
    return Result.getError();
  }

  // If we found FileEntry or DirectoryRemapEntry, look up the mapped
  // path in the external file system.
  StringRef ExtRedirectRP = Result->getExternalRedirect();
  if (ExtRedirectRP.data()) {
    auto P = ExternalFS->getRealPath(ExtRedirectRP, Output);
    if (P && Redirection == RedirectKind::Fallthrough &&
        isFileNotFound(P, Result->E)) {
      // Mapped the file but it wasn't found in the underlying filesystem,
      // fallthrough to using the original path if that was the specified
      // redirection type.
      return ExternalFS->getRealPath(CanonicalPath, Output);
    }
    return P;
  }

  // We found a DirectoryEntry, which does not have a single external contents
  // path. Use the canonical virtual path.
  if (Redirection == RedirectKind::Fallthrough) {
    Result->getPath(Output);
    return {};
  }
  return llvm::errc::invalid_argument;
}

inline uptr_t<FileSystem>
getVFSFromYAML(uptr_t<MemoryBuffer> Buffer,
               SourceMgr::DiagHandlerTy DiagHandler, StringRef YAMLFilePath,
               void *DiagContext, IntrusiveRefCntPtr<FileSystem> ExternalFS) {
  return RedirectingFileSystem::create(CMOVE(Buffer), DiagHandler, YAMLFilePath,
                                       DiagContext, ExternalFS);
}

inline void getVFSEntries(RedirectingFileSystem::Entry *SrcE,
                          SmallVectorImpl<StringRef> &Path,
                          SmallVectorImpl<YAMLVFSEntry> &Entries) {
  auto Kind = SrcE->getKind();
  if (Kind == RedirectingFileSystem::EK_Directory) {
    auto *DE = dyn_cast<RedirectingFileSystem::DirectoryEntry>(SrcE);
    assert(DE && "Must be a directory");
    for (uptr_t<RedirectingFileSystem::Entry> &SubEntry :
         llvm::make_range(DE->contents_begin(), DE->contents_end())) {
      Path.push_back(SubEntry->getName());
      getVFSEntries(SubEntry.get(), Path, Entries);
      Path.pop_back();
    }
    return;
  }

  if (Kind == RedirectingFileSystem::EK_DirectoryRemap) {
    auto *DR = dyn_cast<RedirectingFileSystem::DirectoryRemapEntry>(SrcE);
    assert(DR && "Must be a directory remap");
    SmallString<128> VPath;
    for (auto &Comp : Path)
      llvm::sys::path::append(VPath, Comp);
    Entries.push_back(
        YAMLVFSEntry(VPath.c_str(), DR->getExternalContentsPath()));
    return;
  }

  assert(Kind == RedirectingFileSystem::EK_File && "Must be a EK_File");
  auto *FE = dyn_cast<RedirectingFileSystem::FileEntry>(SrcE);
  assert(FE && "Must be a file");
  SmallString<128> VPath;
  for (auto &Comp : Path)
    llvm::sys::path::append(VPath, Comp);
  Entries.push_back(YAMLVFSEntry(VPath.c_str(), FE->getExternalContentsPath()));
}

inline void collectVFSFromYAML(uptr_t<MemoryBuffer> Buffer,
                               SourceMgr::DiagHandlerTy DiagHandler,
                               StringRef YAMLFilePath,
                               SmallVectorImpl<YAMLVFSEntry> &CollectedEntries,
                               void *DiagContext,
                               IntrusiveRefCntPtr<FileSystem> ExternalFS) {
  uptr_t<RedirectingFileSystem> VFS = RedirectingFileSystem::create(
      CMOVE(Buffer), DiagHandler, YAMLFilePath, DiagContext, ExternalFS);
  if (!VFS)
    return;
  ErrorOr<RedirectingFileSystem::LookupResult> RootResult =
      VFS->lookupPath("/");
  if (!RootResult)
    return;
  SmallVector<StringRef, 16> Components;
  Components.push_back("/");
  getVFSEntries(RootResult->E, Components, CollectedEntries);
}

inline UniqueID getNextVirtualUniqueID() {
  static unsigned UID = 0;
  unsigned ID = __atomic_add_fetch(&UID, 1, __ATOMIC_SEQ_CST);
  // The following assumes that uint64_t max will never collide with a real
  // dev_t value from the OS.
  return UniqueID(UINT64_MAX, ID);
}

inline void YAMLVFSWriter::addEntry(StringRef VirtualPath, StringRef RealPath,
                                    bool IsDirectory) {
  assert(sys::path::is_absolute(VirtualPath) && "virtual path not absolute");
  assert(sys::path::is_absolute(RealPath) && "real path not absolute");
  assert(!pathHasTraversal(VirtualPath) && "path traversal is not supported");
  Mappings.emplace_back(VirtualPath, RealPath, IsDirectory);
}

inline void YAMLVFSWriter::addFileMapping(StringRef VirtualPath,
                                          StringRef RealPath) {
  addEntry(VirtualPath, RealPath, /*IsDirectory=*/false);
}

inline void YAMLVFSWriter::addDirectoryMapping(StringRef VirtualPath,
                                               StringRef RealPath) {
  addEntry(VirtualPath, RealPath, /*IsDirectory=*/true);
}

namespace detail {

class JSONWriter {
  llvm::raw_ostream &OS;
  SmallVector<StringRef, 16> DirStack;

  unsigned getDirIndent() { return 4 * DirStack.size(); }
  unsigned getFileIndent() { return 4 * (DirStack.size() + 1); }
  bool containedIn(StringRef Parent, StringRef Path);
  StringRef containedPart(StringRef Parent, StringRef Path);
  void startDirectory(StringRef Path);
  void endDirectory();
  void writeEntry(StringRef VPath, StringRef RPath);

public:
  JSONWriter(llvm::raw_ostream &OS) : OS(OS) {}

  void write(ArrayRef<YAMLVFSEntry> Entries, int UseExternalNames,
             int IsCaseSensitive, int IsOverlayRelative, StringRef OverlayDir);
};

} // namespace detail

namespace detail {
inline bool JSONWriter::containedIn(StringRef Parent, StringRef Path) {
  using namespace llvm::sys;

  // Compare each path component.
  auto IParent = path::begin(Parent), EParent = path::end(Parent);
  for (auto IChild = path::begin(Path), EChild = path::end(Path);
       IParent != EParent && IChild != EChild; ++IParent, ++IChild) {
    if (*IParent != *IChild)
      return false;
  }
  // Have we exhausted the parent path?
  return IParent == EParent;
}

inline StringRef JSONWriter::containedPart(StringRef Parent, StringRef Path) {
  assert(!Parent.empty());
  assert(containedIn(Parent, Path));
  return Path.slice(Parent.size() + 1, StringRef::npos);
}

inline void JSONWriter::startDirectory(StringRef Path) {
  StringRef Name =
      DirStack.empty() ? Path : containedPart(DirStack.back(), Path);
  DirStack.push_back(Path);
  unsigned Indent = getDirIndent();
  OS.indent(Indent) << "{\n";
  OS.indent(Indent + 2) << "'type': 'directory',\n";
  OS.indent(Indent + 2) << "'name': \"" << llvm::yaml::escape(Name) << "\",\n";
  OS.indent(Indent + 2) << "'contents': [\n";
}

inline void JSONWriter::endDirectory() {
  unsigned Indent = getDirIndent();
  OS.indent(Indent + 2) << "]\n";
  OS.indent(Indent) << "}";

  DirStack.pop_back();
}

inline void JSONWriter::writeEntry(StringRef VPath, StringRef RPath) {
  unsigned Indent = getFileIndent();
  OS.indent(Indent) << "{\n";
  OS.indent(Indent + 2) << "'type': 'file',\n";
  OS.indent(Indent + 2) << "'name': \"" << llvm::yaml::escape(VPath) << "\",\n";
  OS.indent(Indent + 2) << "'external-contents': \""
                        << llvm::yaml::escape(RPath) << "\"\n";
  OS.indent(Indent) << "}";
}

inline void JSONWriter::write(ArrayRef<YAMLVFSEntry> Entries,
                              int UseExternalNames, int IsCaseSensitive,
                              int IsOverlayRelative, StringRef OverlayDir) {
  using namespace llvm::sys;

  OS << "{\n"
        "  'version': 0,\n";
  if (IsCaseSensitive >= 0)
    OS << "  'case-sensitive': '" << (IsCaseSensitive ? "true" : "false")
       << "',\n";
  if (UseExternalNames >= 0)
    OS << "  'use-external-names': '" << (UseExternalNames ? "true" : "false")
       << "',\n";
  bool UseOverlayRelative = false;
  if (IsOverlayRelative >= 0) {
    UseOverlayRelative = (IsOverlayRelative != 0);
    OS << "  'overlay-relative': '" << (UseOverlayRelative ? "true" : "false")
       << "',\n";
  }
  OS << "  'roots': [\n";

  if (!Entries.empty()) {
    const YAMLVFSEntry &Entry = Entries.front();

    startDirectory(Entry.IsDirectory ? StringRef(Entry.VPath)
                                     : path::parent_path(Entry.VPath));

    StringRef RPath = Entry.RPath;
    if (UseOverlayRelative) {
      unsigned OverlayDirLen = OverlayDir.size();
      assert(RPath.substr(0, OverlayDirLen) == OverlayDir &&
             "Overlay dir must be contained in RPath");
      RPath = RPath.slice(OverlayDirLen, RPath.size());
    }

    bool IsCurrentDirEmpty = true;
    if (!Entry.IsDirectory) {
      writeEntry(path::filename(Entry.VPath), RPath);
      IsCurrentDirEmpty = false;
    }

    for (const auto &Entry : Entries.slice(1)) {
      StringRef Dir = Entry.IsDirectory ? StringRef(Entry.VPath)
                                        : path::parent_path(Entry.VPath);
      if (Dir == DirStack.back()) {
        if (!IsCurrentDirEmpty) {
          OS << ",\n";
        }
      } else {
        bool IsDirPoppedFromStack = false;
        while (!DirStack.empty() && !containedIn(DirStack.back(), Dir)) {
          OS << "\n";
          endDirectory();
          IsDirPoppedFromStack = true;
        }
        if (IsDirPoppedFromStack || !IsCurrentDirEmpty) {
          OS << ",\n";
        }
        startDirectory(Dir);
        IsCurrentDirEmpty = true;
      }
      StringRef RPath = Entry.RPath;
      if (UseOverlayRelative) {
        unsigned OverlayDirLen = OverlayDir.size();
        assert(RPath.substr(0, OverlayDirLen) == OverlayDir &&
               "Overlay dir must be contained in RPath");
        RPath = RPath.slice(OverlayDirLen, RPath.size());
      }
      if (!Entry.IsDirectory) {
        writeEntry(path::filename(Entry.VPath), RPath);
        IsCurrentDirEmpty = false;
      }
    }

    while (!DirStack.empty()) {
      OS << "\n";
      endDirectory();
    }
    OS << "\n";
  }

  OS << "  ]\n"
     << "}\n";
}
} // namespace detail

inline void YAMLVFSWriter::write(llvm::raw_ostream &OS) {
  llvm::sort(Mappings, [](const YAMLVFSEntry &LHS, const YAMLVFSEntry &RHS) {
    return LHS.VPath < RHS.VPath;
  });

  detail::JSONWriter(OS).write(Mappings, UseExternalNames, IsCaseSensitive,
                               IsOverlayRelative, OverlayDir);
}

inline recursive_directory_iterator::recursive_directory_iterator(
    FileSystem &FS_, const Twine &Path, errc_t &EC)
    : FS(&FS_) {
  directory_iterator I = FS->dir_begin(Path, EC);
  if (I != directory_iterator()) {
    State = IntrusiveRefCntPtr<detail::RecDirIterState>(
        new detail::RecDirIterState());
    State->Stack.push_back(I);
  }
}

inline vfs::recursive_directory_iterator &
recursive_directory_iterator::increment(errc_t &EC) {
  assert(FS && State && !State->Stack.empty() && "incrementing past end");
  assert(!State->Stack.back()->path().empty() && "non-canonical end iterator");
  vfs::directory_iterator End;

  if (State->HasNoPushRequest)
    State->HasNoPushRequest = false;
  else {
    if (State->Stack.back()->type() == sys::fs::file_type::directory_file) {
      vfs::directory_iterator I =
          FS->dir_begin(State->Stack.back()->path(), EC);
      if (I != End) {
        State->Stack.push_back(I);
        return *this;
      }
    }
  }

  while (!State->Stack.empty() && State->Stack.back().increment(EC) == End)
    State->Stack.pop_back();

  if (State->Stack.empty())
    State.reset();

  return *this;
}

} // namespace vfs
} // namespace llvm

#endif // LLVM_SUPPORT_VIRTUALFILESYSTEM_H
