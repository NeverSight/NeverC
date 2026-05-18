#ifndef NEVERC_BASIC_FILEENTRY_H
#define NEVERC_BASIC_FILEENTRY_H

#include "neverc/Foundation/Core/CustomizableOptional.h"
#include "neverc/Foundation/Core/DirectoryEntry.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem/UniqueID.h"

#include <optional>
#include <utility>

namespace llvm {

class MemoryBuffer;

namespace vfs {

class File;

} // namespace vfs
} // namespace llvm

namespace neverc {

class FileEntryRef;

namespace optional_detail {

template <> class OptionalStorage<neverc::FileEntryRef>;

} // namespace optional_detail

class FileEntry;

class FileEntryRef {
public:
  llvm::StringRef getName() const { return getBaseMapEntry().first(); }

  llvm::StringRef getNameAsRequested() const { return ME->first(); }

  const FileEntry &getFileEntry() const {
    return *getBaseMapEntry().second->V.get<FileEntry *>();
  }
  DirectoryEntryRef getDir() const { return ME->second->Dir; }

  inline off_t getSize() const;
  inline unsigned getUID() const;
  inline const llvm::sys::fs::UniqueID &getUniqueID() const;
  inline time_t getModificationTime() const;
  inline bool isNamedPipe() const;
  inline void closeFile() const;

  friend bool operator==(const FileEntryRef &LHS, const FileEntryRef &RHS) {
    return &LHS.getFileEntry() == &RHS.getFileEntry();
  }
  friend bool operator==(const FileEntry *LHS, const FileEntryRef &RHS) {
    return LHS == &RHS.getFileEntry();
  }
  friend bool operator==(const FileEntryRef &LHS, const FileEntry *RHS) {
    return &LHS.getFileEntry() == RHS;
  }
  friend bool operator!=(const FileEntryRef &LHS, const FileEntryRef &RHS) {
    return !(LHS == RHS);
  }
  friend bool operator!=(const FileEntry *LHS, const FileEntryRef &RHS) {
    return !(LHS == RHS);
  }
  friend bool operator!=(const FileEntryRef &LHS, const FileEntry *RHS) {
    return !(LHS == RHS);
  }

  friend llvm::hash_code hash_value(FileEntryRef Ref) {
    return llvm::hash_value(&Ref.getFileEntry());
  }

  struct MapValue;

  using MapEntry = llvm::StringMapEntry<llvm::ErrorOr<MapValue>>;

  struct MapValue {
    /// The pointer at another MapEntry is used when the FileManager should
    /// silently forward from one name to another, which occurs in Redirecting
    /// VFSs that use external names. In that case, the \c FileEntryRef
    /// returned by the \c FileManager will have the external name, and not the
    /// name that was used to lookup the file.
    llvm::PointerUnion<FileEntry *, const MapEntry *> V;

    /// Directory the file was found in.
    DirectoryEntryRef Dir;

    MapValue() = delete;
    MapValue(FileEntry &FE, DirectoryEntryRef Dir) : V(&FE), Dir(Dir) {}
    MapValue(MapEntry &ME, DirectoryEntryRef Dir) : V(&ME), Dir(Dir) {}
  };

  bool isSameRef(const FileEntryRef &RHS) const { return ME == RHS.ME; }

  operator const FileEntry *() const { return &getFileEntry(); }

  FileEntryRef() = delete;
  explicit FileEntryRef(const MapEntry &ME) : ME(&ME) {
    assert(ME.second && "Expected payload");
    assert(ME.second->V && "Expected non-null");
  }

  const neverc::FileEntryRef::MapEntry &getMapEntry() const { return *ME; }

  const MapEntry &getBaseMapEntry() const {
    const MapEntry *Base = ME;
    while (const auto *Next = Base->second->V.dyn_cast<const MapEntry *>())
      Base = Next;
    return *Base;
  }

private:
  friend class FileMgr::MapEntryOptionalStorage<FileEntryRef>;
  struct optional_none_tag {};

  // Private constructor for use by OptionalStorage.
  FileEntryRef(optional_none_tag) : ME(nullptr) {}
  bool hasOptionalValue() const { return ME; }

  friend struct llvm::DenseMapInfo<FileEntryRef>;
  struct dense_map_empty_tag {};
  struct dense_map_tombstone_tag {};

  // Private constructors for use by DenseMapInfo.
  FileEntryRef(dense_map_empty_tag)
      : ME(llvm::DenseMapInfo<const MapEntry *>::getEmptyKey()) {}
  FileEntryRef(dense_map_tombstone_tag)
      : ME(llvm::DenseMapInfo<const MapEntry *>::getTombstoneKey()) {}
  bool isSpecialDenseMapKey() const {
    return isSameRef(FileEntryRef(dense_map_empty_tag())) ||
           isSameRef(FileEntryRef(dense_map_tombstone_tag()));
  }

  const MapEntry *ME;
};

static_assert(sizeof(FileEntryRef) == sizeof(const FileEntry *),
              "FileEntryRef must avoid size overhead");

static_assert(std::is_trivially_copyable<FileEntryRef>::value,
              "FileEntryRef must be trivially copyable");

using OptionalFileEntryRef = CustomizableOptional<FileEntryRef>;

namespace optional_detail {

template <>
class OptionalStorage<neverc::FileEntryRef>
    : public neverc::FileMgr::MapEntryOptionalStorage<neverc::FileEntryRef> {
  using StorageImpl =
      neverc::FileMgr::MapEntryOptionalStorage<neverc::FileEntryRef>;

public:
  OptionalStorage() = default;

  template <class... ArgTypes>
  explicit OptionalStorage(std::in_place_t, ArgTypes &&...Args)
      : StorageImpl(std::in_place_t{}, std::forward<ArgTypes>(Args)...) {}

  OptionalStorage &operator=(neverc::FileEntryRef Ref) {
    StorageImpl::operator=(Ref);
    return *this;
  }
};

static_assert(sizeof(OptionalFileEntryRef) == sizeof(FileEntryRef),
              "OptionalFileEntryRef must avoid size overhead");

static_assert(std::is_trivially_copyable<OptionalFileEntryRef>::value,
              "OptionalFileEntryRef should be trivially copyable");

} // end namespace optional_detail
} // namespace neverc

namespace llvm {

template <> struct DenseMapInfo<neverc::FileEntryRef> {
  static inline neverc::FileEntryRef getEmptyKey() {
    return neverc::FileEntryRef(neverc::FileEntryRef::dense_map_empty_tag());
  }

  static inline neverc::FileEntryRef getTombstoneKey() {
    return neverc::FileEntryRef(
        neverc::FileEntryRef::dense_map_tombstone_tag());
  }

  static unsigned getHashValue(neverc::FileEntryRef Val) {
    return hash_value(Val);
  }

  static bool isEqual(neverc::FileEntryRef LHS, neverc::FileEntryRef RHS) {
    // Catch the easy cases: both empty, both tombstone, or the same ref.
    if (LHS.isSameRef(RHS))
      return true;

    // Confirm LHS and RHS are valid.
    if (LHS.isSpecialDenseMapKey() || RHS.isSpecialDenseMapKey())
      return false;

    // It's safe to use operator==.
    return LHS == RHS;
  }

  static unsigned getHashValue(const neverc::FileEntry *Val) {
    return llvm::hash_value(Val);
  }
  static bool isEqual(const neverc::FileEntry *LHS, neverc::FileEntryRef RHS) {
    if (RHS.isSpecialDenseMapKey())
      return false;
    return LHS == RHS;
  }
};

} // end namespace llvm

namespace neverc {

inline bool operator==(const FileEntry *LHS, const OptionalFileEntryRef &RHS) {
  return LHS == (RHS ? &RHS->getFileEntry() : nullptr);
}
inline bool operator==(const OptionalFileEntryRef &LHS, const FileEntry *RHS) {
  return (LHS ? &LHS->getFileEntry() : nullptr) == RHS;
}
inline bool operator!=(const FileEntry *LHS, const OptionalFileEntryRef &RHS) {
  return !(LHS == RHS);
}
inline bool operator!=(const OptionalFileEntryRef &LHS, const FileEntry *RHS) {
  return !(LHS == RHS);
}

class FileEntry {
  friend class FileManager;
  friend class FileEntryTestHelper;
  FileEntry();
  FileEntry(const FileEntry &) = delete;
  FileEntry &operator=(const FileEntry &) = delete;

  std::string RealPathName;            // Real path to the file; could be empty.
  off_t Size = 0;                      // File size in bytes.
  time_t ModTime = 0;                  // Modification time of file.
  const DirectoryEntry *Dir = nullptr; // Directory file lives in.
  llvm::sys::fs::UniqueID UniqueID;
  unsigned UID = 0; // A unique (small) ID for the file.
  bool IsNamedPipe = false;

  mutable std::unique_ptr<llvm::vfs::File> File;

  std::unique_ptr<llvm::MemoryBuffer> Content;

  // Optional only to allow delayed construction (FileEntryRef has no
  // default constructor). Always has a value in practice.
  OptionalFileEntryRef LastRef;

public:
  ~FileEntry();
  LLVM_DEPRECATED("Use FileEntryRef::getName() instead.", "")
  llvm::StringRef getName() const { return LastRef->getName(); }

  llvm::StringRef tryGetRealPathName() const { return RealPathName; }
  off_t getSize() const { return Size; }
  unsigned getUID() const { return UID; }
  const llvm::sys::fs::UniqueID &getUniqueID() const { return UniqueID; }
  time_t getModificationTime() const { return ModTime; }

  const DirectoryEntry *getDir() const { return Dir; }

  bool isNamedPipe() const { return IsNamedPipe; }

  void closeFile() const;
};

off_t FileEntryRef::getSize() const { return getFileEntry().getSize(); }

unsigned FileEntryRef::getUID() const { return getFileEntry().getUID(); }

const llvm::sys::fs::UniqueID &FileEntryRef::getUniqueID() const {
  return getFileEntry().getUniqueID();
}

time_t FileEntryRef::getModificationTime() const {
  return getFileEntry().getModificationTime();
}

bool FileEntryRef::isNamedPipe() const { return getFileEntry().isNamedPipe(); }

void FileEntryRef::closeFile() const { getFileEntry().closeFile(); }

} // end namespace neverc

#endif // NEVERC_BASIC_FILEENTRY_H
