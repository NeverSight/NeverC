#ifndef NEVERC_LEX_PATHENTRY_H
#define NEVERC_LEX_PATHENTRY_H

#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"

namespace neverc {
class HeaderIndex;
class IncludeResolver;

class PathEntry {
public:
  enum LookupType_t { LT_NormalDir, LT_Framework, LT_HeaderIndex };

private:
  union Storage {
    DirectoryEntryRef Dir;
    const HeaderIndex *Map;

    Storage(DirectoryEntryRef Dir) : Dir(Dir) {}
    Storage(const HeaderIndex *Map) : Map(Map) {}
  } u;

  LLVM_PREFERRED_TYPE(SrcMgr::CharacteristicKind)
  unsigned DirCharacteristic : 3;

  LLVM_PREFERRED_TYPE(LookupType_t)
  unsigned LookupType : 2;

  LLVM_PREFERRED_TYPE(bool)
  unsigned IsIndexHeaderIndex : 1;

public:
  PathEntry(DirectoryEntryRef Dir, SrcMgr::CharacteristicKind DT,
            bool isFramework)
      : u(Dir), DirCharacteristic(DT),
        LookupType(isFramework ? LT_Framework : LT_NormalDir),
        IsIndexHeaderIndex(false) {}

  PathEntry(const HeaderIndex *Map, SrcMgr::CharacteristicKind DT,
            bool isIndexHeaderIndex)
      : u(Map), DirCharacteristic(DT), LookupType(LT_HeaderIndex),
        IsIndexHeaderIndex(isIndexHeaderIndex) {}

  LookupType_t getLookupType() const { return (LookupType_t)LookupType; }

  llvm::StringRef getName() const;

  const DirectoryEntry *getDir() const {
    return isNormalDir() ? &u.Dir.getDirEntry() : nullptr;
  }

  OptionalDirectoryEntryRef getDirRef() const {
    return isNormalDir() ? OptionalDirectoryEntryRef(u.Dir) : std::nullopt;
  }

  const DirectoryEntry *getFrameworkDir() const {
    return isFramework() ? &u.Dir.getDirEntry() : nullptr;
  }

  OptionalDirectoryEntryRef getFrameworkDirRef() const {
    return isFramework() ? OptionalDirectoryEntryRef(u.Dir) : std::nullopt;
  }

  const HeaderIndex *getHeaderIndex() const {
    return isHeaderIndex() ? u.Map : nullptr;
  }

  bool isNormalDir() const { return getLookupType() == LT_NormalDir; }

  bool isFramework() const { return getLookupType() == LT_Framework; }

  bool isHeaderIndex() const { return getLookupType() == LT_HeaderIndex; }

  SrcMgr::CharacteristicKind getDirCharacteristic() const {
    return (SrcMgr::CharacteristicKind)DirCharacteristic;
  }

  bool isSystemHeaderDirectory() const {
    return getDirCharacteristic() != SrcMgr::C_User;
  }

  bool isIndexHeaderIndex() const {
    return isHeaderIndex() && IsIndexHeaderIndex;
  }

  OptionalFileEntryRef
  ResolveInclude(llvm::StringRef &Filename, IncludeResolver &HS,
                 SourceLocation IncludeLoc,
                 llvm::SmallVectorImpl<char> *SearchPath,
                 llvm::SmallVectorImpl<char> *RelativePath,
                 bool &InUserSpecifiedSystemFramework, bool &IsFrameworkFound,
                 bool &IsInHeaderIndex, llvm::SmallVectorImpl<char> &MappedName,
                 bool OpenFile = true) const;

private:
  OptionalFileEntryRef
  DoFrameworkLookup(llvm::StringRef Filename, IncludeResolver &HS,
                    llvm::SmallVectorImpl<char> *SearchPath,
                    llvm::SmallVectorImpl<char> *RelativePath,
                    bool &InUserSpecifiedSystemFramework,
                    bool &IsFrameworkFound) const;
};

} // end namespace neverc

#endif
