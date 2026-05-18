#include "neverc/Config/config.h" // C_INCLUDE_DIRS
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticFrontend.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Scan/HeaderIndex.h"
#include "neverc/Scan/HeaderIndexOptions.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/LexDiag.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

using namespace neverc;
using namespace neverc::frontend;

namespace {
struct PathEntryInfo {
  IncludeDirGroup Group;
  PathEntry Lookup;
  std::optional<unsigned> UserEntryIdx;

  PathEntryInfo(IncludeDirGroup Group, PathEntry Lookup,
                std::optional<unsigned> UserEntryIdx)
      : Group(Group), Lookup(Lookup), UserEntryIdx(UserEntryIdx) {}
};

class IncludePathBuilder {
  std::vector<PathEntryInfo> IncludePath;
  std::vector<std::pair<std::string, bool>> SystemHeaderPrefixes;
  IncludeResolver &Headers;
  bool Verbose;
  std::string IncludeSysroot;
  bool HasSysroot;

public:
  IncludePathBuilder(IncludeResolver &HS, bool verbose, llvm::StringRef sysroot)
      : Headers(HS), Verbose(verbose), IncludeSysroot(std::string(sysroot)),
        HasSysroot(!(sysroot.empty() || sysroot == "/")) {}

  /// Add the specified path to the specified group list, prefixing the sysroot
  /// if used.
  /// Returns true if the path exists, false if it was ignored.
  bool AddPath(const llvm::Twine &Path, IncludeDirGroup Group, bool isFramework,
               std::optional<unsigned> UserEntryIdx = std::nullopt);

  /// Add the specified path to the specified group list, without performing any
  /// sysroot remapping.
  /// Returns true if the path exists, false if it was ignored.
  bool AddUnmappedPath(const llvm::Twine &Path, IncludeDirGroup Group,
                       bool isFramework,
                       std::optional<unsigned> UserEntryIdx = std::nullopt);

  /// Add the specified prefix to the system header prefix list.
  void AddSystemHeaderPrefix(llvm::StringRef Prefix, bool IsSystemHeader) {
    SystemHeaderPrefixes.emplace_back(std::string(Prefix), IsSystemHeader);
  }

  /// Add paths that should always be searched.
  void AddDefaultCIncludePaths(const llvm::Triple &triple,
                               const HeaderIndexOptions &HSOpts);

  /// Returns true iff AddDefaultIncludePaths should do anything.  If this
  /// returns false, include paths should instead be handled in the driver.
  bool ShouldAddDefaultIncludePaths(const llvm::Triple &triple);

  /// Adds the default system include paths so that e.g. stdio.h is found.
  void AddDefaultIncludePaths(const LangOptions &Lang,
                              const llvm::Triple &triple,
                              const HeaderIndexOptions &HSOpts);

  /// Merges all search path lists into one list and send it to IncludeResolver.
  void Realize(const LangOptions &Lang);
};

} // end anonymous namespace.

namespace {
bool canPrefixWithSysroot(llvm::StringRef Path) {
#if defined(_WIN32)
  return !Path.empty() && llvm::sys::path::is_separator(Path[0]);
#else
  return llvm::sys::path::is_absolute(Path);
#endif
}
} // namespace

// ===----------------------------------------------------------------------===
// Path management
// ===----------------------------------------------------------------------===

bool IncludePathBuilder::AddPath(const llvm::Twine &Path, IncludeDirGroup Group,
                                 bool isFramework,
                                 std::optional<unsigned> UserEntryIdx) {
  // Add the path with sysroot prepended, if desired and this is a system header
  // group.
  if (HasSysroot) {
    llvm::SmallString<256> MappedPathStorage;
    llvm::StringRef MappedPathStr = Path.toStringRef(MappedPathStorage);
    if (canPrefixWithSysroot(MappedPathStr)) {
      return AddUnmappedPath(IncludeSysroot + Path, Group, isFramework,
                             UserEntryIdx);
    }
  }

  return AddUnmappedPath(Path, Group, isFramework, UserEntryIdx);
}

bool IncludePathBuilder::AddUnmappedPath(const llvm::Twine &Path,
                                         IncludeDirGroup Group,
                                         bool isFramework,
                                         std::optional<unsigned> UserEntryIdx) {
  assert(!Path.isTriviallyEmpty() && "can't handle empty path here");

  FileManager &FM = Headers.getFileMgr();
  llvm::SmallString<256> MappedPathStorage;
  llvm::StringRef MappedPathStr = Path.toStringRef(MappedPathStorage);

  // If use system headers while cross-compiling, emit the warning.
  if (HasSysroot && (MappedPathStr.starts_with("/usr/include") ||
                     MappedPathStr.starts_with("/usr/local/include"))) {
    Headers.getDiags().Report(diag::warn_poison_system_directories)
        << MappedPathStr;
  }

  // Compute the PathEntry type.
  SrcMgr::CharacteristicKind Type;
  if (Group == Quoted || Group == Angled || Group == IndexHeaderMap) {
    Type = SrcMgr::C_User;
  } else if (Group == ExternCSystem) {
    Type = SrcMgr::C_ExternCSystem;
  } else {
    Type = SrcMgr::C_System;
  }

  // If the directory exists, add it.
  if (auto DE = FM.getOptionalDirectoryRef(MappedPathStr)) {
    IncludePath.emplace_back(Group, PathEntry(*DE, Type, isFramework),
                             UserEntryIdx);
    return true;
  }

  // Check to see if this is an apple-style headermap (which are not allowed to
  // be frameworks).
  if (!isFramework) {
    if (auto FE = FM.getOptionalFileRef(MappedPathStr)) {
      if (const HeaderIndex *HM = Headers.CreateHeaderIndex(*FE)) {
        // It is a headermap, add it to the search path.
        IncludePath.emplace_back(
            Group, PathEntry(HM, Type, Group == IndexHeaderMap), UserEntryIdx);
        return true;
      }
    }
  }

  if (Verbose)
    llvm::errs() << "ignoring nonexistent directory \"" << MappedPathStr
                 << "\"\n";
  return false;
}

// ===----------------------------------------------------------------------===
// Default include paths
// ===----------------------------------------------------------------------===

void IncludePathBuilder::AddDefaultCIncludePaths(
    const llvm::Triple &triple, const HeaderIndexOptions &HSOpts) {
  if (!ShouldAddDefaultIncludePaths(triple))
    llvm_unreachable("Include management is handled in the driver.");

  if (HSOpts.UseStandardSystemIncludes)
    AddPath("/usr/local/include", System, false);

  // Builtin includes use #include_next directives and should be positioned
  // just prior C include dirs.
  if (HSOpts.UseBuiltinIncludes) {
    // Ignore the sys root, we *always* look for NeverC headers relative to
    // supplied path.
    llvm::SmallString<128> P = llvm::StringRef(HSOpts.ResourceDir);
    llvm::sys::path::append(P, "include");
    AddUnmappedPath(P, ExternCSystem, false);
  }

  // All remaining additions are for system include directories, early exit if
  // we aren't using them.
  if (!HSOpts.UseStandardSystemIncludes)
    return;

  // Add dirs specified via 'configure --with-c-include-dirs'.
  llvm::StringRef CIncludeDirs(C_INCLUDE_DIRS);
  if (CIncludeDirs != "") {
    llvm::SmallVector<llvm::StringRef, 5> dirs;
    CIncludeDirs.split(dirs, ":");
    for (llvm::StringRef dir : dirs)
      AddPath(dir, ExternCSystem, false);
    return;
  }

  AddPath("/usr/include", ExternCSystem, false);
}

bool IncludePathBuilder::ShouldAddDefaultIncludePaths(
    const llvm::Triple &triple) {
  switch (triple.getOS()) {
  case llvm::Triple::Linux:
  case llvm::Triple::Win32:
    return false;
  default:
    break;
  }
  return true; // Everything else uses AddDefaultIncludePaths().
}

void IncludePathBuilder::AddDefaultIncludePaths(
    const LangOptions &Lang, const llvm::Triple &triple,
    const HeaderIndexOptions &HSOpts) {
  // NB: This code path is going away. All of the logic is moving into the
  // driver which has the information necessary to do target-specific
  // selections of default include paths. Each target which moves there will be
  // exempted from this logic in ShouldAddDefaultIncludePaths() until we can
  // delete the entire pile of code.
  if (!ShouldAddDefaultIncludePaths(triple))
    return;

  // NOTE: some additional header search logic is handled in the driver for
  // Darwin.
  if (triple.isOSDarwin()) {
    if (HSOpts.UseStandardSystemIncludes) {
      AddPath("/System/Library/Frameworks", System, true);
      AddPath("/Library/Frameworks", System, true);
    }
    return;
  }

  if (!Lang.AsmPreprocessor && HSOpts.UseStandardSystemIncludes)
    AddPath("/usr/include/neverc/v1", ExternCSystem, false);

  AddDefaultCIncludePaths(triple, HSOpts);
}

namespace {
unsigned removeDuplicateSearchDirs(std::vector<PathEntryInfo> &SearchList,
                                   unsigned First, bool Verbose) {
  llvm::SmallPtrSet<const DirectoryEntry *, 8> SeenDirs;
  llvm::SmallPtrSet<const DirectoryEntry *, 8> SeenFrameworkDirs;
  llvm::SmallPtrSet<const HeaderIndex *, 8> SeenHeaderIndexs;
  unsigned NonSystemRemoved = 0;

  llvm::SmallVector<unsigned, 8> IndicesToRemove;

  for (unsigned i = First; i != SearchList.size(); ++i) {
    unsigned DirToRemove = i;

    const PathEntry &CurEntry = SearchList[i].Lookup;

    if (LLVM_LIKELY(CurEntry.isNormalDir())) {
      if (SeenDirs.insert(CurEntry.getDir()).second)
        continue;
    } else if (CurEntry.isFramework()) {
      if (SeenFrameworkDirs.insert(CurEntry.getFrameworkDir()).second)
        continue;
    } else {
      assert(CurEntry.isHeaderIndex() && "Not a headermap or normal dir?");
      if (SeenHeaderIndexs.insert(CurEntry.getHeaderIndex()).second)
        continue;
    }

    if (LLVM_UNLIKELY(CurEntry.getDirCharacteristic() != SrcMgr::C_User)) {
      unsigned FirstDir;
      for (FirstDir = First;; ++FirstDir) {
        assert(FirstDir != i && "Didn't find dupe?");
        const PathEntry &SearchEntry = SearchList[FirstDir].Lookup;
        if (SearchEntry.getLookupType() != CurEntry.getLookupType())
          continue;

        bool isSame;
        if (CurEntry.isNormalDir())
          isSame = SearchEntry.getDir() == CurEntry.getDir();
        else if (CurEntry.isFramework())
          isSame = SearchEntry.getFrameworkDir() == CurEntry.getFrameworkDir();
        else {
          assert(CurEntry.isHeaderIndex() && "Not a headermap or normal dir?");
          isSame = SearchEntry.getHeaderIndex() == CurEntry.getHeaderIndex();
        }

        if (isSame)
          break;
      }

      if (SearchList[FirstDir].Lookup.getDirCharacteristic() == SrcMgr::C_User)
        DirToRemove = FirstDir;
    }

    if (LLVM_UNLIKELY(Verbose)) {
      llvm::errs() << "ignoring duplicate directory \"" << CurEntry.getName()
                   << "\"\n";
      if (DirToRemove != i)
        llvm::errs() << "  as it is a non-system directory that duplicates "
                     << "a system directory\n";
    }
    if (DirToRemove != i)
      ++NonSystemRemoved;

    IndicesToRemove.push_back(DirToRemove);
  }

  if (!IndicesToRemove.empty()) {
    llvm::sort(IndicesToRemove);
    IndicesToRemove.erase(
        std::unique(IndicesToRemove.begin(), IndicesToRemove.end()),
        IndicesToRemove.end());
    for (int j = IndicesToRemove.size() - 1; j >= 0; --j)
      SearchList.erase(SearchList.begin() + IndicesToRemove[j]);
  }

  return NonSystemRemoved;
}

std::vector<PathEntry> extractLookups(const std::vector<PathEntryInfo> &Infos) {
  std::vector<PathEntry> Lookups;
  Lookups.reserve(Infos.size());
  llvm::transform(Infos, std::back_inserter(Lookups),
                  [](const PathEntryInfo &Info) { return Info.Lookup; });
  return Lookups;
}

llvm::DenseMap<unsigned, unsigned>
mapToUserEntries(const std::vector<PathEntryInfo> &Infos) {
  llvm::DenseMap<unsigned, unsigned> LookupsToUserEntries;
  for (unsigned I = 0, E = Infos.size(); I < E; ++I) {
    // Check whether this PathEntry maps to an IncludeResolver::UserEntry.
    if (Infos[I].UserEntryIdx)
      LookupsToUserEntries.insert({I, *Infos[I].UserEntryIdx});
  }
  return LookupsToUserEntries;
}
} // namespace

// ===----------------------------------------------------------------------===
// Realization & application
// ===----------------------------------------------------------------------===

void IncludePathBuilder::Realize(const LangOptions &Lang) {
  // Concatenate ANGLE+SYSTEM+AFTER chains together into SearchList.
  std::vector<PathEntryInfo> SearchList;
  SearchList.reserve(IncludePath.size());

  // Quoted arguments go first.
  for (auto &Include : IncludePath)
    if (Include.Group == Quoted)
      SearchList.push_back(Include);

  // Deduplicate and remember index.
  removeDuplicateSearchDirs(SearchList, 0, Verbose);
  unsigned NumQuoted = SearchList.size();

  for (auto &Include : IncludePath)
    if (Include.Group == Angled || Include.Group == IndexHeaderMap)
      SearchList.push_back(Include);

  removeDuplicateSearchDirs(SearchList, NumQuoted, Verbose);
  unsigned NumAngled = SearchList.size();

  for (auto &Include : IncludePath)
    if (Include.Group == System || Include.Group == ExternCSystem ||
        Include.Group == CSystem)
      SearchList.push_back(Include);

  for (auto &Include : IncludePath)
    if (Include.Group == After)
      SearchList.push_back(Include);

  // Remove duplicates across both the Angled and System directories.  GCC does
  // this and failing to remove duplicates across these two groups breaks
  // #include_next.
  unsigned NonSystemRemoved =
      removeDuplicateSearchDirs(SearchList, NumQuoted, Verbose);
  NumAngled -= NonSystemRemoved;

  Headers.SetSearchPaths(extractLookups(SearchList), NumQuoted, NumAngled,
                         mapToUserEntries(SearchList));

  Headers.SetSystemHeaderPrefixes(SystemHeaderPrefixes);

  // If verbose, print the list of directories that will be searched.
  if (Verbose) {
    llvm::errs() << "#include \"...\" search starts here:\n";
    for (unsigned i = 0, e = SearchList.size(); i != e; ++i) {
      if (i == NumQuoted)
        llvm::errs() << "#include <...> search starts here:\n";
      llvm::StringRef Name = SearchList[i].Lookup.getName();
      const char *Suffix;
      if (SearchList[i].Lookup.isNormalDir())
        Suffix = "";
      else if (SearchList[i].Lookup.isFramework())
        Suffix = " (framework directory)";
      else {
        assert(SearchList[i].Lookup.isHeaderIndex() && "Unknown PathEntry");
        Suffix = " (headermap)";
      }
      llvm::errs() << " " << Name << Suffix << "\n";
    }
    llvm::errs() << "End of search list.\n";
  }
}

void neverc::ApplyHeaderIndexOptions(IncludeResolver &HS,
                                     const HeaderIndexOptions &HSOpts,
                                     const LangOptions &Lang,
                                     const llvm::Triple &Triple) {
  IncludePathBuilder Init(HS, HSOpts.Verbose, HSOpts.Sysroot);

  // Add the user defined entries.
  for (unsigned i = 0, e = HSOpts.UserEntries.size(); i != e; ++i) {
    const HeaderIndexOptions::Entry &E = HSOpts.UserEntries[i];
    if (E.IgnoreSysRoot) {
      Init.AddUnmappedPath(E.Path, E.Group, E.IsFramework, i);
    } else {
      Init.AddPath(E.Path, E.Group, E.IsFramework, i);
    }
  }

  Init.AddDefaultIncludePaths(Lang, Triple, HSOpts);

  for (unsigned i = 0, e = HSOpts.SystemHeaderPrefixes.size(); i != e; ++i)
    Init.AddSystemHeaderPrefix(HSOpts.SystemHeaderPrefixes[i].Prefix,
                               HSOpts.SystemHeaderPrefixes[i].IsSystemHeader);

  Init.Realize(Lang);
}
