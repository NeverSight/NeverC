#ifndef NEVERC_LEX_INCLUDERESOLVER_H
#define NEVERC_LEX_INCLUDERESOLVER_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Scan/HeaderIndex.h"
#include "neverc/Scan/PathEntry.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class Triple;

} // namespace llvm

namespace neverc {

class DiagnosticsEngine;
class DirectoryEntry;
class FileEntry;
class FileManager;
class IncludeResolver;
class HeaderIndexOptions;

// Compatibility alias for code that still uses the old name.

class IdentifierInfo;
class LangOptions;
class PrepEngine;
class TargetInfo;

struct HeaderFileInfo {
  LLVM_PREFERRED_TYPE(bool)
  unsigned isImport : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned isPragmaOnce : 1;

  LLVM_PREFERRED_TYPE(SrcMgr::CharacteristicKind)
  unsigned DirInfo : 3;

  LLVM_PREFERRED_TYPE(bool)
  unsigned External : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned Resolved : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned IndexHeaderIndexHeader : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned IsValid : 1;

  unsigned ControllingMacroID = 0;

  const IdentifierInfo *ControllingMacro = nullptr;

  llvm::StringRef Framework;

  HeaderFileInfo()
      : isImport(false), isPragmaOnce(false), DirInfo(SrcMgr::C_User),
        External(false), Resolved(false), IndexHeaderIndexHeader(false),
        IsValid(false) {}

  const IdentifierInfo *getControllingMacro() const;
};

struct FrameworkCacheEntry {
  OptionalDirectoryEntryRef Directory;

  bool IsUserSpecifiedSystemFramework;
};

namespace detail {
template <bool Const, typename T>
using Qualified = std::conditional_t<Const, const T, T>;

template <bool IsConst>
struct SearchDirIteratorImpl
    : llvm::iterator_facade_base<SearchDirIteratorImpl<IsConst>,
                                 std::forward_iterator_tag,
                                 Qualified<IsConst, PathEntry>> {
  template <typename Enable = std::enable_if<IsConst, bool>>
  SearchDirIteratorImpl(const SearchDirIteratorImpl<false> &Other)
      : HS(Other.HS), Idx(Other.Idx) {}

  SearchDirIteratorImpl(const SearchDirIteratorImpl &) = default;

  SearchDirIteratorImpl &operator=(const SearchDirIteratorImpl &) = default;

  bool operator==(const SearchDirIteratorImpl &RHS) const {
    return HS == RHS.HS && Idx == RHS.Idx;
  }

  SearchDirIteratorImpl &operator++() {
    assert(*this && "Invalid iterator.");
    ++Idx;
    return *this;
  }

  Qualified<IsConst, PathEntry> &operator*() const {
    assert(*this && "Invalid iterator.");
    return HS->SearchDirs[Idx];
  }

  SearchDirIteratorImpl(std::nullptr_t) : HS(nullptr), Idx(0) {}

  explicit operator bool() const { return HS != nullptr; }

private:
  Qualified<IsConst, IncludeResolver> *HS;

  size_t Idx;

  SearchDirIteratorImpl(Qualified<IsConst, IncludeResolver> &HS, size_t Idx)
      : HS(&HS), Idx(Idx) {}

  friend IncludeResolver;

  friend SearchDirIteratorImpl<!IsConst>;
};
} // namespace detail

using ConstSearchDirIterator = detail::SearchDirIteratorImpl<true>;
using SearchDirIterator = detail::SearchDirIteratorImpl<false>;

using ConstSearchDirRange = llvm::iterator_range<ConstSearchDirIterator>;
using SearchDirRange = llvm::iterator_range<SearchDirIterator>;

class IncludeResolver {
  friend class PathEntry;

  friend ConstSearchDirIterator;
  friend SearchDirIterator;

  std::shared_ptr<HeaderIndexOptions> HSOpts;

  llvm::DenseMap<unsigned, unsigned> SearchDirToHSEntry;

  DiagnosticsEngine &Diags;
  FileManager &FileMgr;

  std::vector<PathEntry> SearchDirs;
  std::vector<bool> SearchDirsUsage;
  unsigned AngledDirIdx = 0;
  unsigned SystemDirIdx = 0;

  llvm::StringMap<unsigned, llvm::BumpPtrAllocator> SearchDirHeaderIndexIndex;

  unsigned FirstNonHeaderIndexSearchDirIdx = 0;

  std::vector<std::pair<std::string, bool>> SystemHeaderPrefixes;

  mutable std::vector<HeaderFileInfo> FileInfo;

  struct ResolveIncludeCacheInfo {
    ConstSearchDirIterator StartIt = nullptr;
    ConstSearchDirIterator HitIt = nullptr;
    const char *MappedName = nullptr;

    ResolveIncludeCacheInfo() = default;

    void reset(ConstSearchDirIterator NewStartIt) {
      StartIt = NewStartIt;
      MappedName = nullptr;
    }
  };
  llvm::StringMap<ResolveIncludeCacheInfo, llvm::BumpPtrAllocator>
      ResolveIncludeCache;

  llvm::StringMap<FrameworkCacheEntry, llvm::BumpPtrAllocator> FrameworkMap;

  using IncludeAliasMap = llvm::StringMap<std::string, llvm::BumpPtrAllocator>;
  struct IncludeAliasMapDeleter {
    void operator()(IncludeAliasMap *P) const { ::delete P; }
  };
  std::unique_ptr<IncludeAliasMap, IncludeAliasMapDeleter> IncludeAliases;

  std::vector<std::pair<FileEntryRef, std::unique_ptr<HeaderIndex>>>
      HeaderIndexs;

  // A map of discovered headers with their associated include file name.
  llvm::DenseMap<const FileEntry *, llvm::SmallString<64>> IncludeNames;

  llvm::StringSet<llvm::BumpPtrAllocator> FrameworkNames;

  void indexInitialHeaderIndexs();

public:
  IncludeResolver(std::shared_ptr<HeaderIndexOptions> HSOpts,
                  SourceManager &SourceMgr, DiagnosticsEngine &Diags,
                  const LangOptions &LangOpts, const TargetInfo *Target);
  IncludeResolver(const IncludeResolver &) = delete;
  IncludeResolver &operator=(const IncludeResolver &) = delete;

  HeaderIndexOptions &getHeaderIdxOpts() const { return *HSOpts; }

  FileManager &getFileMgr() const { return FileMgr; }

  DiagnosticsEngine &getDiags() const { return Diags; }

  void SetSearchPaths(std::vector<PathEntry> dirs, unsigned angledDirIdx,
                      unsigned systemDirIdx,
                      llvm::DenseMap<unsigned, unsigned> searchDirToHSEntry);

  void AddSearchPath(const PathEntry &dir, bool isAngled);

  void AddSystemSearchPath(const PathEntry &dir) {
    SearchDirs.push_back(dir);
    SearchDirsUsage.push_back(false);
  }

  void SetSystemHeaderPrefixes(llvm::ArrayRef<std::pair<std::string, bool>> P) {
    SystemHeaderPrefixes.assign(P.begin(), P.end());
  }

  bool HasIncludeAliasMap() const { return (bool)IncludeAliases; }

  void AddIncludeAlias(llvm::StringRef Source, llvm::StringRef Dest) {
    if (!IncludeAliases)
      IncludeAliases.reset(::new IncludeAliasMap());
    (*IncludeAliases)[Source] = std::string(Dest);
  }

  llvm::StringRef MapHeaderToIncludeAlias(llvm::StringRef Source) {
    assert(IncludeAliases && "Trying to map headers when there's no map");

    // Do any filename replacements before anything else
    IncludeAliasMap::const_iterator Iter = IncludeAliases->find(Source);
    if (Iter != IncludeAliases->end())
      return Iter->second;
    return {};
  }

  void ClearFileInfo() { FileInfo.clear(); }

  OptionalFileEntryRef ResolveInclude(
      llvm::StringRef Filename, SourceLocation IncludeLoc, bool isAngled,
      ConstSearchDirIterator FromDir, ConstSearchDirIterator *CurDir,
      llvm::ArrayRef<std::pair<OptionalFileEntryRef, DirectoryEntryRef>>
          Includers,
      llvm::SmallVectorImpl<char> *SearchPath,
      llvm::SmallVectorImpl<char> *RelativePath, bool *IsMapped,
      bool *IsFrameworkFound, bool SkipCache = false, bool OpenFile = true,
      bool CacheFailures = true);

  OptionalFileEntryRef
  ResolveSubframeworkHeader(llvm::StringRef Filename,
                            FileEntryRef ContextFileEnt,
                            llvm::SmallVectorImpl<char> *SearchPath,
                            llvm::SmallVectorImpl<char> *RelativePath);

  FrameworkCacheEntry &LookupFrameworkCache(llvm::StringRef FWName) {
    return FrameworkMap[FWName];
  }

  bool ShouldProcessInclude(PrepEngine &PP, FileEntryRef File, bool isImport,
                            bool &IsFirstIncludeOfFile);

  SrcMgr::CharacteristicKind getFileDirFlavor(FileEntryRef File) {
    return (SrcMgr::CharacteristicKind)getFileInfo(File).DirInfo;
  }

  void MarkFileIncludeOnce(FileEntryRef File) {
    HeaderFileInfo &FI = getFileInfo(File);
    FI.isPragmaOnce = true;
  }

  void MarkFileSystemHeader(FileEntryRef File) {
    getFileInfo(File).DirInfo = SrcMgr::C_System;
  }

  void SetFileControllingMacro(FileEntryRef File,
                               const IdentifierInfo *ControllingMacro) {
    getFileInfo(File).ControllingMacro = ControllingMacro;
  }

  bool isFileMultipleIncludeGuarded(FileEntryRef File) const;

  bool hasFileBeenImported(FileEntryRef File) const {
    const HeaderFileInfo *FI = getExistingFileInfo(File);
    return FI && FI->isImport;
  }

  std::vector<bool> computeUserEntryUsage() const;

  const HeaderIndex *CreateHeaderIndex(FileEntryRef FE);

  void getHeaderIndexFileNames(llvm::SmallVectorImpl<std::string> &Names) const;

private:
  OptionalFileEntryRef
  getFileAndSuggestModule(llvm::StringRef FileName, SourceLocation IncludeLoc,
                          const DirectoryEntry *Dir, bool IsSystemHeaderDir,
                          bool OpenFile = true, bool CacheFailures = true);

  void cacheLookupSuccess(ResolveIncludeCacheInfo &CacheLookup,
                          ConstSearchDirIterator HitIt,
                          SourceLocation IncludeLoc);

  void noteLookupUsage(unsigned HitIdx, SourceLocation IncludeLoc);

public:
  unsigned header_file_size() const { return FileInfo.size(); }

  HeaderFileInfo &getFileInfo(FileEntryRef FE);

  const HeaderFileInfo *getExistingFileInfo(FileEntryRef FE,
                                            bool WantExternal = true) const;

  SearchDirIterator search_dir_begin() { return {*this, 0}; }
  SearchDirIterator search_dir_end() { return {*this, SearchDirs.size()}; }
  SearchDirRange search_dir_range() {
    return {search_dir_begin(), search_dir_end()};
  }

  ConstSearchDirIterator search_dir_begin() const { return quoted_dir_begin(); }
  ConstSearchDirIterator search_dir_nth(size_t n) const {
    assert(n < SearchDirs.size());
    return {*this, n};
  }
  ConstSearchDirIterator search_dir_end() const { return system_dir_end(); }
  ConstSearchDirRange search_dir_range() const {
    return {search_dir_begin(), search_dir_end()};
  }

  unsigned search_dir_size() const { return SearchDirs.size(); }

  ConstSearchDirIterator quoted_dir_begin() const { return {*this, 0}; }
  ConstSearchDirIterator quoted_dir_end() const { return angled_dir_begin(); }

  ConstSearchDirIterator angled_dir_begin() const {
    return {*this, AngledDirIdx};
  }
  ConstSearchDirIterator angled_dir_end() const { return system_dir_begin(); }

  ConstSearchDirIterator system_dir_begin() const {
    return {*this, SystemDirIdx};
  }
  ConstSearchDirIterator system_dir_end() const {
    return {*this, SearchDirs.size()};
  }

  unsigned searchDirIdx(const PathEntry &DL) const;

  llvm::StringRef getUniqueFrameworkName(llvm::StringRef Framework);

  llvm::StringRef getIncludeNameForHeader(const FileEntry *File) const;

  std::string suggestPathToFileForDiagnostics(FileEntryRef File,
                                              llvm::StringRef MainFile,
                                              bool *IsAngled = nullptr) const;

  std::string suggestPathToFileForDiagnostics(llvm::StringRef File,
                                              llvm::StringRef WorkingDir,
                                              llvm::StringRef MainFile,
                                              bool *IsAngled = nullptr) const;

  void PrintStats();

  size_t getTotalMemory() const;
};

void ApplyHeaderIndexOptions(IncludeResolver &HS,
                             const HeaderIndexOptions &HSOpts,
                             const LangOptions &Lang,
                             const llvm::Triple &triple);

} // namespace neverc

#endif // NEVERC_LEX_INCLUDERESOLVER_H
