#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/LexDiag.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <cassert>
#include <cstddef>
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
// Header file resolution
// ===----------------------------------------------------------------------===

namespace {

bool diagMSVCSearchDivergence(DiagnosticsEngine &Diags,
                              OptionalFileEntryRef MSFE, const FileEntry *FE,
                              SourceLocation IncludeLoc) {
  if (MSFE && FE != *MSFE) {
    Diags.Report(IncludeLoc, diag::ext_pp_include_search_ms) << MSFE->getName();
    return true;
  }
  return false;
}

const char *allocateStringCopy(llvm::StringRef Str,
                               llvm::BumpPtrAllocator &Alloc) {
  assert(!Str.empty());
  char *CopyStr = Alloc.Allocate<char>(Str.size() + 1);
  memcpy(CopyStr, Str.data(), Str.size());
  CopyStr[Str.size()] = '\0';
  return CopyStr;
}

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

void diagFrameworkInclude(DiagnosticsEngine &Diags, SourceLocation IncludeLoc,
                          llvm::StringRef Includer,
                          llvm::StringRef IncludeFilename,
                          FileEntryRef IncludeFE, bool isAngled = false,
                          bool FoundByHeaderIndex = false) {
  bool IsIncluderPrivateHeader = false;
  llvm::SmallString<128> FromFramework, ToFramework;
  llvm::SmallString<128> FromIncludeSpelling, ToIncludeSpelling;
  if (!splitFrameworkPath(Includer, IsIncluderPrivateHeader, FromFramework,
                          FromIncludeSpelling))
    return;
  bool IsIncludeePrivateHeader = false;
  bool IsIncludeeInFramework =
      splitFrameworkPath(IncludeFE.getName(), IsIncludeePrivateHeader,
                         ToFramework, ToIncludeSpelling);

  if (!isAngled && !FoundByHeaderIndex) {
    llvm::SmallString<128> NewInclude("<");
    if (IsIncludeeInFramework) {
      NewInclude += ToIncludeSpelling;
      NewInclude += ">";
    } else {
      NewInclude += IncludeFilename;
      NewInclude += ">";
    }
    Diags.Report(IncludeLoc, diag::warn_quoted_include_in_framework_header)
        << IncludeFilename
        << FixItHint::CreateReplacement(IncludeLoc, NewInclude);
  }

  // Headers in Foo.framework/Headers should not include headers
  // from Foo.framework/PrivateHeaders, since this violates public/private
  // API boundaries and can cause modular dependency cycles.
  if (!IsIncluderPrivateHeader && IsIncludeeInFramework &&
      IsIncludeePrivateHeader && FromFramework == ToFramework)
    Diags.Report(IncludeLoc, diag::warn_framework_include_private_from_public)
        << IncludeFilename;
}

} // namespace

OptionalFileEntryRef IncludeResolver::ResolveInclude(
    llvm::StringRef Filename, SourceLocation IncludeLoc, bool isAngled,
    ConstSearchDirIterator FromDir, ConstSearchDirIterator *CurDirArg,
    llvm::ArrayRef<std::pair<OptionalFileEntryRef, DirectoryEntryRef>>
        Includers,
    llvm::SmallVectorImpl<char> *SearchPath,
    llvm::SmallVectorImpl<char> *RelativePath, bool *IsMapped,
    bool *IsFrameworkFound, bool SkipCache, bool OpenFile, bool CacheFailures) {
  ConstSearchDirIterator CurDirLocal = nullptr;
  ConstSearchDirIterator &CurDir = CurDirArg ? *CurDirArg : CurDirLocal;

  if (IsMapped)
    *IsMapped = false;

  if (IsFrameworkFound)
    *IsFrameworkFound = false;

  if (LLVM_UNLIKELY(llvm::sys::path::is_absolute(Filename))) {
    CurDir = nullptr;

    if (FromDir)
      return std::nullopt;

    if (SearchPath)
      SearchPath->clear();
    if (RelativePath) {
      RelativePath->clear();
      RelativePath->append(Filename.begin(), Filename.end());
    }
    return getFileAndSuggestModule(Filename, IncludeLoc, nullptr,
                                   /*IsSystemHeaderDir*/ false, OpenFile,
                                   CacheFailures);
  }

  OptionalFileEntryRef MSFE;

  // Quoted includes: search includer's directory first.
  if (!Includers.empty() && !isAngled) {
    llvm::SmallString<1024> TmpDir;
    bool First = true;
    for (const auto &IncluderAndDir : Includers) {
      OptionalFileEntryRef Includer = IncluderAndDir.first;

      TmpDir = IncluderAndDir.second.getName();
      llvm::sys::path::append(TmpDir, Filename);

      // getFileInfo refs may be invalidated by getFileAndSuggestModule,
      // so don't cache them across that call.
      bool IncluderIsSystemHeader =
          Includer && getFileInfo(*Includer).DirInfo != SrcMgr::C_User;
      if (OptionalFileEntryRef FE =
              getFileAndSuggestModule(TmpDir, IncludeLoc, IncluderAndDir.second,
                                      IncluderIsSystemHeader)) {
        if (!Includer) {
          assert(First && "only first includer can have no file");
          return FE;
        }

        // Leave CurDir unset.
        // Inherit directory classification (e.g. system) from the includer.
        //
        // Note that we only use one of FromHFI/ToHFI at once, due to potential
        // reallocation of the underlying vector potentially making the first
        // reference binding dangling.
        HeaderFileInfo &FromHFI = getFileInfo(*Includer);
        unsigned DirInfo = FromHFI.DirInfo;
        bool IndexHeaderIndexHeader = FromHFI.IndexHeaderIndexHeader;
        llvm::StringRef Framework = FromHFI.Framework;

        HeaderFileInfo &ToHFI = getFileInfo(*FE);
        ToHFI.DirInfo = DirInfo;
        ToHFI.IndexHeaderIndexHeader = IndexHeaderIndexHeader;
        ToHFI.Framework = Framework;

        if (SearchPath) {
          llvm::StringRef SearchPathRef(IncluderAndDir.second.getName());
          SearchPath->clear();
          SearchPath->append(SearchPathRef.begin(), SearchPathRef.end());
        }
        if (RelativePath) {
          RelativePath->clear();
          RelativePath->append(Filename.begin(), Filename.end());
        }
        if (First) {
          diagFrameworkInclude(Diags, IncludeLoc,
                               IncluderAndDir.second.getName(), Filename, *FE);
          return FE;
        }

        // Otherwise, we found the path via MSVC header search rules.  If
        // -Wmsvc-include is enabled, we have to keep searching to see if we
        // would've found this header in -I or -isystem directories.
        if (Diags.isIgnored(diag::ext_pp_include_search_ms, IncludeLoc)) {
          return FE;
        } else {
          MSFE = FE;
          break;
        }
      }
      First = false;
    }
  }

  CurDir = nullptr;

  // If this is a system #include, ignore the user #include locs.
  ConstSearchDirIterator It =
      isAngled ? angled_dir_begin() : search_dir_begin();

  // If this is a #include_next request, start searching after the directory the
  // file was found in.
  if (FromDir)
    It = FromDir;

  // Cache all of the lookups performed by this method.  Many headers are
  // multiply included, and the "pragma once" optimization prevents them from
  // being relex/pp'd, but they would still have to search through a
  // (potentially huge) series of SearchDirs to find it.
  ResolveIncludeCacheInfo &CacheLookup = ResolveIncludeCache[Filename];

  ConstSearchDirIterator NextIt = std::next(It);

  if (!SkipCache) {
    if (CacheLookup.StartIt == NextIt) {
      // HIT: Skip querying potentially lots of directories for this lookup.
      if (CacheLookup.HitIt)
        It = CacheLookup.HitIt;
      if (CacheLookup.MappedName) {
        Filename = CacheLookup.MappedName;
        if (IsMapped)
          *IsMapped = true;
      }
    } else {
      // MISS: This is the first query, or the previous query didn't match
      // our search start.  We will fill in our found location below, so prime
      // the start point value.
      CacheLookup.reset(/*NewStartIt=*/NextIt);

      if (It == search_dir_begin() && FirstNonHeaderIndexSearchDirIdx > 0) {
        // Handle cold misses of user includes in the presence of many header
        // maps.  We avoid searching perhaps thousands of header maps by
        // jumping directly to the correct one or jumping beyond all of them.
        auto Iter = SearchDirHeaderIndexIndex.find(Filename.lower());
        if (Iter == SearchDirHeaderIndexIndex.end())
          // Not in index => Skip to first SearchDir after initial header maps
          It = search_dir_nth(FirstNonHeaderIndexSearchDirIdx);
        else
          // In index => Start with a specific header map
          It = search_dir_nth(Iter->second);
      }
    }
  } else {
    CacheLookup.reset(/*NewStartIt=*/NextIt);
  }

  llvm::SmallString<64> MappedName;

  for (; It != search_dir_end(); ++It) {
    bool InUserSpecifiedSystemFramework = false;
    bool IsInHeaderIndex = false;
    bool IsFrameworkFoundInDir = false;
    OptionalFileEntryRef File = It->ResolveInclude(
        Filename, *this, IncludeLoc, SearchPath, RelativePath,
        InUserSpecifiedSystemFramework, IsFrameworkFoundInDir, IsInHeaderIndex,
        MappedName, OpenFile);
    if (!MappedName.empty()) {
      assert(IsInHeaderIndex && "MappedName should come from a header map");
      CacheLookup.MappedName =
          allocateStringCopy(MappedName, ResolveIncludeCache.getAllocator());
    }
    if (IsMapped)
      // A filename is mapped when a header map remapped it to a relative path
      // used in subsequent header search or to an absolute path pointing to an
      // existing file.
      *IsMapped |= (!MappedName.empty() || (IsInHeaderIndex && File));
    if (IsFrameworkFound)
      // Because we keep a filename remapped for subsequent search directory
      // lookups, ignore IsFrameworkFoundInDir after the first remapping and not
      // just for remapping in a current search directory.
      *IsFrameworkFound |= (IsFrameworkFoundInDir && !CacheLookup.MappedName);
    if (!File)
      continue;

    CurDir = It;

    IncludeNames[*File] = Filename;

    // Inherit directory classification from the directory entry.
    HeaderFileInfo &HFI = getFileInfo(*File);
    HFI.DirInfo = CurDir->getDirCharacteristic();

    // If the directory characteristic is User but this framework was
    // user-specified to be treated as a system framework, promote the
    // characteristic.
    if (HFI.DirInfo == SrcMgr::C_User && InUserSpecifiedSystemFramework)
      HFI.DirInfo = SrcMgr::C_System;

    // If the filename matches a known system header prefix, override
    // whether the file is a system header.
    for (unsigned j = SystemHeaderPrefixes.size(); j; --j) {
      if (Filename.starts_with(SystemHeaderPrefixes[j - 1].first)) {
        HFI.DirInfo = SystemHeaderPrefixes[j - 1].second ? SrcMgr::C_System
                                                         : SrcMgr::C_User;
        break;
      }
    }

    if (CurDir->isHeaderIndex() && isAngled) {
      size_t SlashPos = Filename.find('/');
      if (SlashPos != llvm::StringRef::npos)
        HFI.Framework =
            getUniqueFrameworkName(llvm::StringRef(Filename.begin(), SlashPos));
      if (CurDir->isIndexHeaderIndex())
        HFI.IndexHeaderIndexHeader = 1;
    } else if (CurDir->isFramework()) {
      size_t SlashPos = Filename.find('/');
      if (SlashPos != llvm::StringRef::npos)
        HFI.Framework =
            getUniqueFrameworkName(llvm::StringRef(Filename.begin(), SlashPos));
    }

    if (diagMSVCSearchDivergence(Diags, MSFE, &File->getFileEntry(),
                                 IncludeLoc))
      return MSFE;

    bool FoundByHeaderIndex = !IsMapped ? false : *IsMapped;
    if (!Includers.empty())
      diagFrameworkInclude(Diags, IncludeLoc,
                           Includers.front().second.getName(), Filename, *File,
                           isAngled, FoundByHeaderIndex);

    // Remember this location for the next lookup we do.
    cacheLookupSuccess(CacheLookup, It, IncludeLoc);
    return File;
  }

  // If we are including a file with a quoted include "foo.h" from inside
  // a header in a framework that is currently being built, and we couldn't
  // resolve "foo.h" any other way, change the include to <Foo/foo.h>, where
  // "Foo" is the name of the framework in which the including header was found.
  if (!Includers.empty() && Includers.front().first && !isAngled &&
      !Filename.contains('/')) {
    HeaderFileInfo &IncludingHFI = getFileInfo(*Includers.front().first);
    if (IncludingHFI.IndexHeaderIndexHeader) {
      llvm::SmallString<128> ScratchFilename;
      ScratchFilename += IncludingHFI.Framework;
      ScratchFilename += '/';
      ScratchFilename += Filename;

      OptionalFileEntryRef File =
          ResolveInclude(ScratchFilename, IncludeLoc, /*isAngled=*/true,
                         FromDir, &CurDir, Includers.front(), SearchPath,
                         RelativePath, IsMapped, /*IsFrameworkFound=*/nullptr);

      if (diagMSVCSearchDivergence(
              Diags, MSFE, File ? &File->getFileEntry() : nullptr, IncludeLoc))
        return MSFE;

      cacheLookupSuccess(ResolveIncludeCache[Filename],
                         ResolveIncludeCache[ScratchFilename].HitIt,
                         IncludeLoc);
      return File;
    }
  }

  if (diagMSVCSearchDivergence(Diags, MSFE, nullptr, IncludeLoc))
    return MSFE;

  // Otherwise, didn't find it. Remember we didn't find this.
  CacheLookup.HitIt = search_dir_end();
  return std::nullopt;
}

OptionalFileEntryRef IncludeResolver::ResolveSubframeworkHeader(
    llvm::StringRef Filename, FileEntryRef ContextFileEnt,
    llvm::SmallVectorImpl<char> *SearchPath,
    llvm::SmallVectorImpl<char> *RelativePath) {
  // Framework names must have a '/' in the filename.  Find it.
  size_t SlashPos = Filename.find('/');
  if (SlashPos == llvm::StringRef::npos)
    return std::nullopt;

  // Look up the base framework name of the ContextFileEnt.
  llvm::StringRef ContextName = ContextFileEnt.getName();

  // If the context info wasn't a framework, couldn't be a subframework.
  const unsigned DotFrameworkLen = 10;
  auto FrameworkPos = ContextName.find(".framework");
  if (FrameworkPos == llvm::StringRef::npos ||
      (ContextName[FrameworkPos + DotFrameworkLen] != '/' &&
       ContextName[FrameworkPos + DotFrameworkLen] != '\\'))
    return std::nullopt;

  llvm::SmallString<1024> FrameworkName(ContextName.data(),
                                        ContextName.data() + FrameworkPos +
                                            DotFrameworkLen + 1);

  // Append Frameworks/HIToolbox.framework/
  FrameworkName += "Frameworks/";
  FrameworkName.append(Filename.begin(), Filename.begin() + SlashPos);
  FrameworkName += ".framework/";

  auto &CacheLookup = *FrameworkMap
                           .insert(std::make_pair(Filename.substr(0, SlashPos),
                                                  FrameworkCacheEntry()))
                           .first;

  // Some other location?
  if (CacheLookup.second.Directory &&
      CacheLookup.first().size() == FrameworkName.size() &&
      memcmp(CacheLookup.first().data(), &FrameworkName[0],
             CacheLookup.first().size()) != 0)
    return std::nullopt;

  // Cache subframework.
  if (!CacheLookup.second.Directory) {
    ++NumSubFrameworkLookups;

    // If the framework dir doesn't exist, we fail.
    auto Dir = FileMgr.getOptionalDirectoryRef(FrameworkName);
    if (!Dir)
      return std::nullopt;

    // Otherwise, if it does, remember that this is the right direntry for this
    // framework.
    CacheLookup.second.Directory = Dir;
  }

  if (RelativePath) {
    RelativePath->clear();
    RelativePath->append(Filename.begin() + SlashPos + 1, Filename.end());
  }

  // Check ".../Frameworks/HIToolbox.framework/Headers/HIToolbox.h"
  llvm::SmallString<1024> HeadersFilename(FrameworkName);
  HeadersFilename += "Headers/";
  if (SearchPath) {
    SearchPath->clear();
    // Without trailing '/'.
    SearchPath->append(HeadersFilename.begin(), HeadersFilename.end() - 1);
  }

  HeadersFilename.append(Filename.begin() + SlashPos + 1, Filename.end());
  auto File = FileMgr.getOptionalFileRef(HeadersFilename, /*OpenFile=*/true);
  if (!File) {
    // Check ".../Frameworks/HIToolbox.framework/PrivateHeaders/HIToolbox.h"
    HeadersFilename = FrameworkName;
    HeadersFilename += "PrivateHeaders/";
    if (SearchPath) {
      SearchPath->clear();
      // Without trailing '/'.
      SearchPath->append(HeadersFilename.begin(), HeadersFilename.end() - 1);
    }

    HeadersFilename.append(Filename.begin() + SlashPos + 1, Filename.end());
    File = FileMgr.getOptionalFileRef(HeadersFilename, /*OpenFile=*/true);

    if (!File)
      return std::nullopt;
  }

  // Inherit directory classification from the context file.
  //
  // Note that the temporary 'DirInfo' is required here, as either call to
  // getFileInfo could resize the vector and we don't want to rely on order
  // of evaluation.
  unsigned DirInfo = getFileInfo(ContextFileEnt).DirInfo;
  getFileInfo(*File).DirInfo = DirInfo;

  return *File;
}
