#ifndef NEVERC_BASIC_DIRECTORYENTRY_H
#define NEVERC_BASIC_DIRECTORYENTRY_H

#include "neverc/Foundation/Core/CustomizableOptional.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorOr.h"

#include <optional>
#include <utility>

namespace neverc {
namespace FileMgr {

template <class RefTy> class MapEntryOptionalStorage;

} // end namespace FileMgr

class DirectoryEntry {
  DirectoryEntry() = default;
  DirectoryEntry(const DirectoryEntry &) = delete;
  DirectoryEntry &operator=(const DirectoryEntry &) = delete;
  friend class FileManager;
  friend class FileEntryTestHelper;

  llvm::StringRef Name;

public:
  LLVM_DEPRECATED("Use DirectoryEntryRef::getName() instead.", "")
  llvm::StringRef getName() const { return Name; }
};

class DirectoryEntryRef {
public:
  const DirectoryEntry &getDirEntry() const { return *ME->getValue(); }

  llvm::StringRef getName() const { return ME->getKey(); }

  friend llvm::hash_code hash_value(DirectoryEntryRef Ref) {
    return llvm::hash_value(&Ref.getDirEntry());
  }

  using MapEntry = llvm::StringMapEntry<llvm::ErrorOr<DirectoryEntry &>>;

  const MapEntry &getMapEntry() const { return *ME; }

  bool isSameRef(DirectoryEntryRef RHS) const { return ME == RHS.ME; }

  DirectoryEntryRef() = delete;
  explicit DirectoryEntryRef(const MapEntry &ME) : ME(&ME) {}

  operator const DirectoryEntry *() const { return &getDirEntry(); }

private:
  friend class FileMgr::MapEntryOptionalStorage<DirectoryEntryRef>;
  struct optional_none_tag {};

  // Private constructor for use by OptionalStorage.
  DirectoryEntryRef(optional_none_tag) : ME(nullptr) {}
  bool hasOptionalValue() const { return ME; }

  friend struct llvm::DenseMapInfo<DirectoryEntryRef>;
  struct dense_map_empty_tag {};
  struct dense_map_tombstone_tag {};

  // Private constructors for use by DenseMapInfo.
  DirectoryEntryRef(dense_map_empty_tag)
      : ME(llvm::DenseMapInfo<const MapEntry *>::getEmptyKey()) {}
  DirectoryEntryRef(dense_map_tombstone_tag)
      : ME(llvm::DenseMapInfo<const MapEntry *>::getTombstoneKey()) {}
  bool isSpecialDenseMapKey() const {
    return isSameRef(DirectoryEntryRef(dense_map_empty_tag())) ||
           isSameRef(DirectoryEntryRef(dense_map_tombstone_tag()));
  }

  const MapEntry *ME;
};

using OptionalDirectoryEntryRef = CustomizableOptional<DirectoryEntryRef>;

namespace FileMgr {

template <class RefTy> class MapEntryOptionalStorage {
  using optional_none_tag = typename RefTy::optional_none_tag;
  RefTy MaybeRef;

public:
  MapEntryOptionalStorage() : MaybeRef(optional_none_tag()) {}

  template <class... ArgTypes>
  explicit MapEntryOptionalStorage(std::in_place_t, ArgTypes &&...Args)
      : MaybeRef(std::forward<ArgTypes>(Args)...) {}

  void reset() { MaybeRef = optional_none_tag(); }

  bool has_value() const { return MaybeRef.hasOptionalValue(); }

  RefTy &value() & {
    assert(has_value());
    return MaybeRef;
  }
  RefTy const &value() const & {
    assert(has_value());
    return MaybeRef;
  }
  RefTy &&value() && {
    assert(has_value());
    return std::move(MaybeRef);
  }

  template <class... Args> void emplace(Args &&...args) {
    MaybeRef = RefTy(std::forward<Args>(args)...);
  }

  MapEntryOptionalStorage &operator=(RefTy Ref) {
    MaybeRef = Ref;
    return *this;
  }
};

} // end namespace FileMgr

namespace optional_detail {

template <>
class OptionalStorage<neverc::DirectoryEntryRef>
    : public neverc::FileMgr::MapEntryOptionalStorage<
          neverc::DirectoryEntryRef> {
  using StorageImpl =
      neverc::FileMgr::MapEntryOptionalStorage<neverc::DirectoryEntryRef>;

public:
  OptionalStorage() = default;

  template <class... ArgTypes>
  explicit OptionalStorage(std::in_place_t, ArgTypes &&...Args)
      : StorageImpl(std::in_place_t{}, std::forward<ArgTypes>(Args)...) {}

  OptionalStorage &operator=(neverc::DirectoryEntryRef Ref) {
    StorageImpl::operator=(Ref);
    return *this;
  }
};

static_assert(sizeof(OptionalDirectoryEntryRef) == sizeof(DirectoryEntryRef),
              "OptionalDirectoryEntryRef must avoid size overhead");

static_assert(std::is_trivially_copyable<OptionalDirectoryEntryRef>::value,
              "OptionalDirectoryEntryRef should be trivially copyable");

} // end namespace optional_detail
} // namespace neverc

namespace llvm {

template <> struct PointerLikeTypeTraits<neverc::DirectoryEntryRef> {
  static inline void *getAsVoidPointer(neverc::DirectoryEntryRef Dir) {
    return const_cast<neverc::DirectoryEntryRef::MapEntry *>(
        &Dir.getMapEntry());
  }

  static inline neverc::DirectoryEntryRef getFromVoidPointer(void *Ptr) {
    return neverc::DirectoryEntryRef(
        *reinterpret_cast<const neverc::DirectoryEntryRef::MapEntry *>(Ptr));
  }

  static constexpr int NumLowBitsAvailable = PointerLikeTypeTraits<
      const neverc::DirectoryEntryRef::MapEntry *>::NumLowBitsAvailable;
};

template <> struct DenseMapInfo<neverc::DirectoryEntryRef> {
  static inline neverc::DirectoryEntryRef getEmptyKey() {
    return neverc::DirectoryEntryRef(
        neverc::DirectoryEntryRef::dense_map_empty_tag());
  }

  static inline neverc::DirectoryEntryRef getTombstoneKey() {
    return neverc::DirectoryEntryRef(
        neverc::DirectoryEntryRef::dense_map_tombstone_tag());
  }

  static unsigned getHashValue(neverc::DirectoryEntryRef Val) {
    return hash_value(Val);
  }

  static bool isEqual(neverc::DirectoryEntryRef LHS,
                      neverc::DirectoryEntryRef RHS) {
    // Catch the easy cases: both empty, both tombstone, or the same ref.
    if (LHS.isSameRef(RHS))
      return true;

    // Confirm LHS and RHS are valid.
    if (LHS.isSpecialDenseMapKey() || RHS.isSpecialDenseMapKey())
      return false;

    // It's safe to use operator==.
    return LHS == RHS;
  }
};

} // end namespace llvm

#endif // NEVERC_BASIC_DIRECTORYENTRY_H
