#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Scan/HeaderIndexOptions.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/LexDiag.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Capacity.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>

using namespace neverc;

#define DEBUG_TYPE "file-search"

ALWAYS_ENABLED_STATISTIC(NumIncluded, "Number of attempted #includes.");
ALWAYS_ENABLED_STATISTIC(
    NumMultiIncludeFileOptzn,
    "Number of #includes skipped due to the multi-include optimization.");
ALWAYS_ENABLED_STATISTIC(NumFrameworkLookups, "Number of framework lookups.");
ALWAYS_ENABLED_STATISTIC(NumSubFrameworkLookups,
                         "Number of subframework lookups.");

// ===----------------------------------------------------------------------===
// Construction & statistics
// ===----------------------------------------------------------------------===

const IdentifierInfo *HeaderFileInfo::getControllingMacro() const {
  return ControllingMacro;
}

IncludeResolver::IncludeResolver(std::shared_ptr<HeaderIndexOptions> HSOpts,
                                 SourceManager &SourceMgr,
                                 DiagnosticsEngine &Diags,
                                 const LangOptions &LangOpts,
                                 const TargetInfo *Target)
    : HSOpts(std::move(HSOpts)), Diags(Diags),
      FileMgr(SourceMgr.getFileManager()), FrameworkMap(64) {}

void IncludeResolver::PrintStats() {
  const unsigned FISize = FileInfo.size();
  llvm::errs() << "\n*** IncludeResolver Stats:\n"
               << FISize << " files tracked.\n";
  unsigned NumOnceOnlyFiles = 0;
  const auto *FIData = FileInfo.data();
  for (unsigned i = 0; i < FISize; ++i) {
    if (LLVM_LIKELY(i + 4 < FISize))
      __builtin_prefetch(&FIData[i + 4], 0, 0);
    NumOnceOnlyFiles += (FIData[i].isPragmaOnce | FIData[i].isImport);
  }
  llvm::errs() << "  " << NumOnceOnlyFiles << " #import/#pragma once files.\n";

  llvm::errs() << "  " << NumIncluded << " #include/#include_next/#import.\n"
               << "    " << NumMultiIncludeFileOptzn
               << " #includes skipped due to the multi-include optimization.\n";

  llvm::errs() << NumFrameworkLookups << " framework lookups.\n"
               << NumSubFrameworkLookups << " subframework lookups.\n";
}

// ===----------------------------------------------------------------------===
// Search path management
// ===----------------------------------------------------------------------===

void IncludeResolver::SetSearchPaths(
    std::vector<PathEntry> dirs, unsigned int angledDirIdx,
    unsigned int systemDirIdx,
    llvm::DenseMap<unsigned int, unsigned int> searchDirToHSEntry) {
  assert(angledDirIdx <= systemDirIdx && systemDirIdx <= dirs.size() &&
         "Directory indices are unordered");
  SearchDirs = std::move(dirs);
  SearchDirsUsage.assign(SearchDirs.size(), false);
  AngledDirIdx = angledDirIdx;
  SystemDirIdx = systemDirIdx;
  SearchDirToHSEntry = std::move(searchDirToHSEntry);
  indexInitialHeaderIndexs();
}

void IncludeResolver::AddSearchPath(const PathEntry &dir, bool isAngled) {
  unsigned idx = isAngled ? SystemDirIdx : AngledDirIdx;
  SearchDirs.insert(SearchDirs.begin() + idx, dir);
  SearchDirsUsage.insert(SearchDirsUsage.begin() + idx, false);
  if (!isAngled)
    AngledDirIdx++;
  SystemDirIdx++;
}

std::vector<bool> IncludeResolver::computeUserEntryUsage() const {
  std::vector<bool> UserEntryUsage(HSOpts->UserEntries.size());
  for (unsigned I = 0, E = SearchDirsUsage.size(); I < E; ++I) {
    if (SearchDirsUsage[I]) {
      auto UserEntryIdxIt = SearchDirToHSEntry.find(I);
      if (UserEntryIdxIt != SearchDirToHSEntry.end())
        UserEntryUsage[UserEntryIdxIt->second] = true;
    }
  }
  return UserEntryUsage;
}

const HeaderIndex *IncludeResolver::CreateHeaderIndex(FileEntryRef FE) {
  // We expect the number of headermaps to be small, and almost always empty.
  // If it ever grows, use of a linear search should be re-evaluated.
  if (!HeaderIndexs.empty()) {
    for (unsigned i = 0, e = HeaderIndexs.size(); i != e; ++i)
      // Pointer equality comparison of FileEntries works because they are
      // already uniqued by inode.
      if (HeaderIndexs[i].first == FE)
        return HeaderIndexs[i].second.get();
  }

  if (std::unique_ptr<HeaderIndex> HM = HeaderIndex::Create(FE, FileMgr)) {
    HeaderIndexs.emplace_back(FE, std::move(HM));
    return HeaderIndexs.back().second.get();
  }

  return nullptr;
}

void IncludeResolver::getHeaderIndexFileNames(
    llvm::SmallVectorImpl<std::string> &Names) const {
  for (auto &HM : HeaderIndexs)
    Names.push_back(std::string(HM.first.getName()));
}

void IncludeResolver::indexInitialHeaderIndexs() {
  llvm::StringMap<unsigned, llvm::BumpPtrAllocator> Index(SearchDirs.size());

  for (unsigned i = 0; i != SearchDirs.size(); ++i) {
    auto &Dir = SearchDirs[i];

    if (!Dir.isHeaderIndex()) {
      SearchDirHeaderIndexIndex = std::move(Index);
      FirstNonHeaderIndexSearchDirIdx = i;
      break;
    }

    auto Callback = [&](llvm::StringRef Filename) {
      Index.try_emplace(Filename.lower(), i);
    };
    Dir.getHeaderIndex()->forEachKey(Callback);
  }
}

// ===----------------------------------------------------------------------===
// File lookup
// ===----------------------------------------------------------------------===

llvm::StringRef PathEntry::getName() const {
  if (isNormalDir())
    return getDirRef()->getName();
  if (isFramework())
    return getFrameworkDirRef()->getName();
  assert(isHeaderIndex() && "Unknown PathEntry");
  return getHeaderIndex()->getFileName();
}

OptionalFileEntryRef IncludeResolver::getFileAndSuggestModule(
    llvm::StringRef FileName, SourceLocation IncludeLoc,
    const DirectoryEntry *Dir, bool IsSystemHeaderDir, bool OpenFile /*=true*/,
    bool CacheFailures /*=true*/) {
  auto File = getFileMgr().getFileRef(FileName, OpenFile, CacheFailures);
  if (!File) {
    std::error_code EC = llvm::errorToErrorCode(File.takeError());
    if (EC != llvm::errc::no_such_file_or_directory &&
        EC != llvm::errc::invalid_argument &&
        EC != llvm::errc::is_a_directory && EC != llvm::errc::not_a_directory) {
      Diags.Report(IncludeLoc, diag::err_cannot_open_file)
          << FileName << EC.message();
    }
    return std::nullopt;
  }

  return *File;
}

OptionalFileEntryRef PathEntry::ResolveInclude(
    llvm::StringRef &Filename, IncludeResolver &HS, SourceLocation IncludeLoc,
    llvm::SmallVectorImpl<char> *SearchPath,
    llvm::SmallVectorImpl<char> *RelativePath,
    bool &InUserSpecifiedSystemFramework, bool &IsFrameworkFound,
    bool &IsInHeaderIndex, llvm::SmallVectorImpl<char> &MappedName,
    bool OpenFile) const {
  InUserSpecifiedSystemFramework = false;
  IsInHeaderIndex = false;
  MappedName.clear();

  llvm::SmallString<1024> TmpDir;
  if (isNormalDir()) {
    TmpDir = getDirRef()->getName();
    llvm::sys::path::append(TmpDir, Filename);
    if (SearchPath) {
      llvm::StringRef SearchPathRef(getDirRef()->getName());
      SearchPath->clear();
      SearchPath->append(SearchPathRef.begin(), SearchPathRef.end());
    }
    if (RelativePath) {
      RelativePath->clear();
      RelativePath->append(Filename.begin(), Filename.end());
    }

    return HS.getFileAndSuggestModule(TmpDir, IncludeLoc, getDir(),
                                      isSystemHeaderDirectory(), OpenFile);
  }

  if (isFramework())
    return DoFrameworkLookup(Filename, HS, SearchPath, RelativePath,
                             InUserSpecifiedSystemFramework, IsFrameworkFound);

  assert(isHeaderIndex() && "Unknown directory lookup");
  const HeaderIndex *HM = getHeaderIndex();
  llvm::SmallString<1024> Path;
  llvm::StringRef Dest = HM->lookupFilename(Filename, Path);
  if (Dest.empty())
    return std::nullopt;

  IsInHeaderIndex = true;

  auto FixupSearchPathAndFindUsableModule =
      [&](FileEntryRef File) -> OptionalFileEntryRef {
    if (SearchPath) {
      llvm::StringRef SearchPathRef(getName());
      SearchPath->clear();
      SearchPath->append(SearchPathRef.begin(), SearchPathRef.end());
    }
    if (RelativePath) {
      RelativePath->clear();
      RelativePath->append(Filename.begin(), Filename.end());
    }
    return File;
  };

  // Headermap may remap "Foo.h" → "Foo/Foo.h"; continue with the mapped name.
  if (llvm::sys::path::is_relative(Dest)) {
    MappedName.append(Dest.begin(), Dest.end());
    Filename = llvm::StringRef(MappedName.begin(), MappedName.size());
    Dest = HM->lookupFilename(Filename, Path);
  }

  if (auto Res = HS.getFileMgr().getOptionalFileRef(Dest, OpenFile)) {
    return FixupSearchPathAndFindUsableModule(*Res);
  }

  // Header maps need to be marked as used whenever the filename matches.
  // The case where the target file **exists** is handled by callee of this
  // function as part of the regular logic that applies to include search paths.
  // The case where the target file **does not exist** is handled here:
  HS.noteLookupUsage(HS.searchDirIdx(*this), IncludeLoc);
  return std::nullopt;
}

namespace {

OptionalDirectoryEntryRef
locateFrameworkRoot(FileManager &FileMgr, llvm::StringRef DirName,
                    llvm::SmallVectorImpl<std::string> &SubmodulePath) {
  assert(llvm::sys::path::extension(DirName) == ".framework" &&
         "Not a framework directory");

  // Use real path to handle symlinked framework relocations.
  auto TopFrameworkDir = FileMgr.getOptionalDirectoryRef(DirName);

  if (TopFrameworkDir)
    DirName = FileMgr.getCanonicalName(*TopFrameworkDir);
  do {
    DirName = llvm::sys::path::parent_path(DirName);
    if (DirName.empty())
      break;

    auto Dir = FileMgr.getOptionalDirectoryRef(DirName);
    if (!Dir)
      break;

    if (llvm::sys::path::extension(DirName) == ".framework") {
      SubmodulePath.push_back(std::string(llvm::sys::path::stem(DirName)));
      TopFrameworkDir = *Dir;
    }
  } while (true);

  return TopFrameworkDir;
}

} // namespace

OptionalFileEntryRef
PathEntry::DoFrameworkLookup(llvm::StringRef Filename, IncludeResolver &HS,
                             llvm::SmallVectorImpl<char> *SearchPath,
                             llvm::SmallVectorImpl<char> *RelativePath,
                             bool &InUserSpecifiedSystemFramework,
                             bool &IsFrameworkFound) const {
  FileManager &FileMgr = HS.getFileMgr();

  // Framework names must have a '/' in the filename.
  size_t SlashPos = Filename.find('/');
  if (SlashPos == llvm::StringRef::npos)
    return std::nullopt;

  // Find out if this is the home for the specified framework, by checking
  // IncludeResolver.  Possible answers are yes/no and unknown.
  FrameworkCacheEntry &CacheEntry =
      HS.LookupFrameworkCache(Filename.substr(0, SlashPos));

  if (CacheEntry.Directory && CacheEntry.Directory != getFrameworkDirRef())
    return std::nullopt;

  // FrameworkName = "/System/Library/Frameworks/"
  llvm::SmallString<1024> FrameworkName;
  FrameworkName += getFrameworkDirRef()->getName();
  if (FrameworkName.empty() || FrameworkName.back() != '/')
    FrameworkName.push_back('/');

  // FrameworkName = "/System/Library/Frameworks/Cocoa"
  llvm::StringRef ModuleName(Filename.begin(), SlashPos);
  FrameworkName += ModuleName;

  // FrameworkName = "/System/Library/Frameworks/Cocoa.framework/"
  FrameworkName += ".framework/";

  // If the cache entry was unresolved, populate it now.
  if (!CacheEntry.Directory) {
    ++NumFrameworkLookups;

    auto Dir = FileMgr.getDirectory(FrameworkName);
    if (!Dir)
      return std::nullopt;

    CacheEntry.Directory = getFrameworkDirRef();

    // If this is a user search directory, check if the framework has been
    // user-specified as a system framework.
    if (getDirCharacteristic() == SrcMgr::C_User) {
      llvm::SmallString<1024> SystemFrameworkMarker(FrameworkName);
      SystemFrameworkMarker += ".system_framework";
      if (llvm::sys::fs::exists(SystemFrameworkMarker)) {
        CacheEntry.IsUserSpecifiedSystemFramework = true;
      }
    }
  }

  InUserSpecifiedSystemFramework = CacheEntry.IsUserSpecifiedSystemFramework;
  IsFrameworkFound = CacheEntry.Directory.has_value();

  if (RelativePath) {
    RelativePath->clear();
    RelativePath->append(Filename.begin() + SlashPos + 1, Filename.end());
  }

  // Check "/System/Library/Frameworks/Cocoa.framework/Headers/file.h"
  unsigned OrigSize = FrameworkName.size();

  FrameworkName += "Headers/";

  if (SearchPath) {
    SearchPath->clear();
    // Without trailing '/'.
    SearchPath->append(FrameworkName.begin(), FrameworkName.end() - 1);
  }

  FrameworkName.append(Filename.begin() + SlashPos + 1, Filename.end());

  auto File = FileMgr.getOptionalFileRef(FrameworkName, /*OpenFile=*/true);
  if (!File) {
    // Check "/System/Library/Frameworks/Cocoa.framework/PrivateHeaders/file.h"
    constexpr llvm::StringLiteral Private("Private");
    FrameworkName.insert(FrameworkName.begin() + OrigSize, Private.begin(),
                         Private.end());
    if (SearchPath)
      SearchPath->insert(SearchPath->begin() + OrigSize, Private.begin(),
                         Private.end());

    File = FileMgr.getOptionalFileRef(FrameworkName, /*OpenFile=*/true);
  }

  if (File)
    return *File;
  return std::nullopt;
}

void IncludeResolver::cacheLookupSuccess(ResolveIncludeCacheInfo &CacheLookup,
                                         ConstSearchDirIterator HitIt,
                                         SourceLocation Loc) {
  CacheLookup.HitIt = HitIt;
  noteLookupUsage(HitIt.Idx, Loc);
}

void IncludeResolver::noteLookupUsage(unsigned HitIdx, SourceLocation Loc) {
  SearchDirsUsage[HitIdx] = true;

  auto UserEntryIdxIt = SearchDirToHSEntry.find(HitIdx);
  if (UserEntryIdxIt != SearchDirToHSEntry.end())
    Diags.Report(Loc, diag::remark_pp_search_path_usage)
        << HSOpts->UserEntries[UserEntryIdxIt->second].Path;
}

// ===----------------------------------------------------------------------===
// File info management
// ===----------------------------------------------------------------------===

namespace {

bool splitFrameworkPath(llvm::StringRef Path, bool &IsPrivateHeader,
                        llvm::SmallVectorImpl<char> &FrameworkName,
                        llvm::SmallVectorImpl<char> &IncludeSpelling) {
  using namespace llvm::sys;
  path::const_iterator I = path::begin(Path);
  path::const_iterator E = path::end(Path);
  IsPrivateHeader = false;

  // Detect different types of framework style paths:
  //
  //   ...Foo.framework/{Headers,PrivateHeaders}
  //   ...Foo.framework/Versions/{A,Current}/{Headers,PrivateHeaders}
  //   ...Foo.framework/Frameworks/Nested.framework/{Headers,PrivateHeaders}
  //   ...<other variations with 'Versions' like in the above path>
  //
  // and some other variations among these lines.
  int FoundComp = 0;
  while (I != E) {
    if (*I == "Headers") {
      ++FoundComp;
    } else if (*I == "PrivateHeaders") {
      ++FoundComp;
      IsPrivateHeader = true;
    } else if (I->ends_with(".framework")) {
      llvm::StringRef Name = I->drop_back(10); // Drop .framework
      // Need to reset the strings and counter to support nested frameworks.
      FrameworkName.clear();
      FrameworkName.append(Name.begin(), Name.end());
      IncludeSpelling.clear();
      IncludeSpelling.append(Name.begin(), Name.end());
      FoundComp = 1;
    } else if (FoundComp >= 2) {
      IncludeSpelling.push_back('/');
      IncludeSpelling.append(I->begin(), I->end());
    }
    ++I;
  }

  return !FrameworkName.empty() && FoundComp >= 2;
}

void mergeHeaderFileInfo(HeaderFileInfo &HFI, const HeaderFileInfo &OtherHFI) {
  assert(OtherHFI.External && "expected to merge external HFI");

  HFI.isImport |= OtherHFI.isImport;
  HFI.isPragmaOnce |= OtherHFI.isPragmaOnce;

  if (!HFI.ControllingMacro && !HFI.ControllingMacroID) {
    HFI.ControllingMacro = OtherHFI.ControllingMacro;
    HFI.ControllingMacroID = OtherHFI.ControllingMacroID;
  }

  HFI.DirInfo = OtherHFI.DirInfo;
  HFI.External = (!HFI.IsValid || HFI.External);
  HFI.IsValid = true;
  HFI.IndexHeaderIndexHeader = OtherHFI.IndexHeaderIndexHeader;

  if (HFI.Framework.empty())
    HFI.Framework = OtherHFI.Framework;
}

} // namespace

HeaderFileInfo &IncludeResolver::getFileInfo(FileEntryRef FE) {
  unsigned UID = FE.getUID();
  if (LLVM_UNLIKELY(UID >= FileInfo.size()))
    FileInfo.resize(UID + UID / 4 + 1);

  HeaderFileInfo *HFI = &FileInfo[UID];

  HFI->IsValid = true;
  HFI->External = false;
  return *HFI;
}

const HeaderFileInfo *
IncludeResolver::getExistingFileInfo(FileEntryRef FE, bool WantExternal) const {
  HeaderFileInfo *HFI;
  if (FE.getUID() >= FileInfo.size()) {
    return nullptr;
  } else {
    HFI = &FileInfo[FE.getUID()];
  }

  if (!HFI->IsValid || (HFI->External && !WantExternal))
    return nullptr;

  return HFI;
}

bool IncludeResolver::isFileMultipleIncludeGuarded(FileEntryRef File) const {
  // #import is not checked here — it's not a property of the file itself.
  if (auto *HFI = getExistingFileInfo(File))
    return HFI->isPragmaOnce || HFI->ControllingMacro ||
           HFI->ControllingMacroID;
  return false;
}

bool IncludeResolver::ShouldProcessInclude(PrepEngine &PP, FileEntryRef File,
                                           bool isImport,
                                           bool &IsFirstIncludeOfFile) {
  ++NumIncluded;

  IsFirstIncludeOfFile = false;

  HeaderFileInfo &FileInfo = getFileInfo(File);

  const IdentifierInfo *ControllingMacro = FileInfo.getControllingMacro();
  if (LLVM_LIKELY(ControllingMacro != nullptr)) {
    if (LLVM_LIKELY(PP.isMacroDefined(ControllingMacro))) {
      ++NumMultiIncludeFileOptzn;
      return false;
    }
  }

  if (LLVM_UNLIKELY(isImport)) {
    FileInfo.isImport = true;
    if (PP.alreadyIncluded(File))
      return false;
  } else {
    if (LLVM_UNLIKELY(FileInfo.isPragmaOnce || FileInfo.isImport))
      return false;
  }

  IsFirstIncludeOfFile = PP.markIncluded(File);

  return true;
}

size_t IncludeResolver::getTotalMemory() const {
  return SearchDirs.capacity() + llvm::capacity_in_bytes(FileInfo) +
         llvm::capacity_in_bytes(HeaderIndexs) +
         ResolveIncludeCache.getAllocator().getTotalMemory() +
         FrameworkMap.getAllocator().getTotalMemory();
}

unsigned IncludeResolver::searchDirIdx(const PathEntry &DL) const {
  return &DL - &*SearchDirs.begin();
}

llvm::StringRef
IncludeResolver::getUniqueFrameworkName(llvm::StringRef Framework) {
  return FrameworkNames.insert(Framework).first->first();
}

llvm::StringRef
IncludeResolver::getIncludeNameForHeader(const FileEntry *File) const {
  auto It = IncludeNames.find(File);
  if (It == IncludeNames.end())
    return {};
  return It->second;
}

// ===----------------------------------------------------------------------===
// Diagnostic suggestions
// ===----------------------------------------------------------------------===

std::string IncludeResolver::suggestPathToFileForDiagnostics(
    FileEntryRef File, llvm::StringRef MainFile, bool *IsAngled) const {
  return suggestPathToFileForDiagnostics(File.getName(), /*WorkingDir=*/"",
                                         MainFile, IsAngled);
}

std::string IncludeResolver::suggestPathToFileForDiagnostics(
    llvm::StringRef File, llvm::StringRef WorkingDir, llvm::StringRef MainFile,
    bool *IsAngled) const {
  using namespace llvm::sys;

  llvm::SmallString<32> FilePath = File;
  // remove_dots switches to backslashes on windows as a side-effect!
  // We always want to suggest forward slashes for includes.
  // (not remove_dots(..., posix) as that misparses windows paths).
  path::remove_dots(FilePath, /*remove_dot_dot=*/true);
  path::native(FilePath, path::Style::posix);
  File = FilePath;

  unsigned BestPrefixLength = 0;
  // Checks whether `Dir` is a strict path prefix of `File`. If so and that's
  // the longest prefix we've seen so for it, returns true and updates the
  // `BestPrefixLength` accordingly.
  auto CheckDir = [&](llvm::SmallString<32> Dir) -> bool {
    if (!WorkingDir.empty() && !path::is_absolute(Dir))
      fs::make_absolute(WorkingDir, Dir);
    path::remove_dots(Dir, /*remove_dot_dot=*/true);
    for (auto NI = path::begin(File), NE = path::end(File),
              DI = path::begin(Dir), DE = path::end(Dir);
         NI != NE; ++NI, ++DI) {
      if (DI == DE) {
        // Dir is a prefix of File, up to choice of path separators.
        unsigned PrefixLength = NI - path::begin(File);
        if (PrefixLength > BestPrefixLength) {
          BestPrefixLength = PrefixLength;
          return true;
        }
        break;
      }

      // Consider all path separators equal.
      if (NI->size() == 1 && DI->size() == 1 &&
          path::is_separator(NI->front()) && path::is_separator(DI->front()))
        continue;

      // Special case Apple .sdk folders since the search path is typically a
      // symlink like `iPhoneSimulator14.5.sdk` while the file is instead
      // located in `iPhoneSimulator.sdk` (the real folder).
      if (NI->ends_with(".sdk") && DI->ends_with(".sdk")) {
        llvm::StringRef NBasename = path::stem(*NI);
        llvm::StringRef DBasename = path::stem(*DI);
        if (DBasename.starts_with(NBasename))
          continue;
      }

      if (*NI != *DI)
        break;
    }
    return false;
  };

  bool BestPrefixIsFramework = false;
  for (const PathEntry &DL : search_dir_range()) {
    if (DL.isNormalDir()) {
      llvm::StringRef Dir = DL.getDirRef()->getName();
      if (CheckDir(Dir)) {
        if (IsAngled)
          *IsAngled = BestPrefixLength && isSystem(DL.getDirCharacteristic());
        BestPrefixIsFramework = false;
      }
    } else if (DL.isFramework()) {
      llvm::StringRef Dir = DL.getFrameworkDirRef()->getName();
      if (CheckDir(Dir)) {
        // Framework includes by convention use <>.
        if (IsAngled)
          *IsAngled = BestPrefixLength;
        BestPrefixIsFramework = true;
      }
    }
  }

  // Try to shorten include path using TUs directory, if we couldn't find any
  // suitable prefix in include search paths.
  if (!BestPrefixLength && CheckDir(path::parent_path(MainFile))) {
    if (IsAngled)
      *IsAngled = false;
    BestPrefixIsFramework = false;
  }

  // Try resolving resulting filename via reverse search in header maps,
  // key from header name is user preferred name for the include file.
  llvm::StringRef Filename = File.drop_front(BestPrefixLength);
  for (const PathEntry &DL : search_dir_range()) {
    if (!DL.isHeaderIndex())
      continue;

    llvm::StringRef SpelledFilename =
        DL.getHeaderIndex()->reverseLookupFilename(Filename);
    if (!SpelledFilename.empty()) {
      Filename = SpelledFilename;
      BestPrefixIsFramework = false;
      break;
    }
  }

  // If the best prefix is a framework path, we need to compute the proper
  // include spelling for the framework header.
  bool IsPrivateHeader;
  llvm::SmallString<128> FrameworkName, IncludeSpelling;
  if (BestPrefixIsFramework &&
      splitFrameworkPath(Filename, IsPrivateHeader, FrameworkName,
                         IncludeSpelling)) {
    Filename = IncludeSpelling;
  }
  return std::string(path::convert_to_slash(Filename).str());
}
