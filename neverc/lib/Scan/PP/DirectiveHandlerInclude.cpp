#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/DirectoryEntry.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/VarArgExpansion.h"
#include "neverc/Scan/LexDiag.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>

#ifdef __AVX2__
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

using namespace neverc;

namespace {
bool detectIncludeCaseDiff(llvm::StringRef Include) {
  if (::llvm::sys::path::begin(Include)->equals_insensitive("boost"))
    return true;

  static constexpr size_t MaxStdHeaderNameLen = 18u;
  if (Include.size() > MaxStdHeaderNameLen)
    return false;

  llvm::SmallString<32> LowerInclude{Include};
  char *Data = LowerInclude.data();
  size_t Len = LowerInclude.size();
  size_t I = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
  if (Len >= 16) {
    uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(Data));
    uint8x16_t Hi = vcgtq_u8(V, vdupq_n_u8(0x7f));
    if (vmaxvq_u8(Hi) != 0)
      return false;
    uint8x16_t IsUpper =
        vandq_u8(vcgeq_u8(V, vdupq_n_u8('A')), vcleq_u8(V, vdupq_n_u8('Z')));
    uint8x16_t ToLower = vandq_u8(IsUpper, vdupq_n_u8('A' ^ 'a'));
    V = veorq_u8(V, ToLower);
    uint8x16_t IsBSlash = vceqq_u8(V, vdupq_n_u8('\\'));
    V = vbslq_u8(IsBSlash, vdupq_n_u8('/'), V);
    vst1q_u8(reinterpret_cast<uint8_t *>(Data), V);
    I = 16;
  }
#elif defined(__AVX2__)
  if (Len >= 16) {
    __m128i V = _mm_loadu_si128((const __m128i *)Data);
    __m128i HiBits = _mm_and_si128(V, _mm_set1_epi8((char)0x80));
    if (_mm_movemask_epi8(HiBits) != 0)
      return false;
    const __m128i CaseBit = _mm_set1_epi8('A' ^ 'a');
    __m128i Biased = _mm_xor_si128(V, _mm_set1_epi8((char)0x80u));
    __m128i IsUpper = _mm_and_si128(
        _mm_cmpgt_epi8(Biased, _mm_set1_epi8((char)(('A' - 1) ^ 0x80u))),
        _mm_cmpgt_epi8(_mm_set1_epi8((char)(('Z' + 1) ^ 0x80u)), Biased));
    V = _mm_xor_si128(V, _mm_and_si128(IsUpper, CaseBit));
    __m128i IsBSlash = _mm_cmpeq_epi8(V, _mm_set1_epi8('\\'));
    V = _mm_blendv_epi8(V, _mm_set1_epi8('/'), IsBSlash);
    _mm_storeu_si128((__m128i *)Data, V);
    I = 16;
  }
#elif defined(__SSE2__)
  if (Len >= 16) {
    __m128i V = _mm_loadu_si128((const __m128i *)Data);
    __m128i HiBits = _mm_and_si128(V, _mm_set1_epi8((char)0x80));
    if (_mm_movemask_epi8(HiBits) != 0)
      return false;
    __m128i IsUpper = _mm_and_si128(_mm_cmpgt_epi8(V, _mm_set1_epi8('A' - 1)),
                                    _mm_cmpgt_epi8(_mm_set1_epi8('Z' + 1), V));
    __m128i ToLower = _mm_and_si128(IsUpper, _mm_set1_epi8('A' ^ 'a'));
    V = _mm_xor_si128(V, ToLower);
    __m128i IsBSlash = _mm_cmpeq_epi8(V, _mm_set1_epi8('\\'));
    V = _mm_or_si128(_mm_andnot_si128(IsBSlash, V),
                     _mm_and_si128(IsBSlash, _mm_set1_epi8('/')));
    _mm_storeu_si128((__m128i *)Data, V);
    I = 16;
  }
#endif
  for (; I < Len; ++I) {
    char &Ch = Data[I];
    if (static_cast<unsigned char>(Ch) > 0x7f)
      return false;
    Ch |= ('A' ^ 'a') * (Ch >= 'A' && Ch <= 'Z');
    if (::llvm::sys::path::is_separator(Ch))
      Ch = '/';
  }

  static const llvm::StringSet<> KnownHeaders = [] {
    llvm::StringSet<> S;
    for (llvm::StringRef H : {
             "assert.h",
             "complex.h",
             "ctype.h",
             "errno.h",
             "fenv.h",
             "float.h",
             "inttypes.h",
             "iso646.h",
             "limits.h",
             "locale.h",
             "math.h",
             "setjmp.h",
             "signal.h",
             "stdalign.h",
             "stdarg.h",
             "stdatomic.h",
             "stdbool.h",
             "stdckdint.h",
             "stddef.h",
             "stdint.h",
             "stdio.h",
             "stdlib.h",
             "stdnoreturn.h",
             "string.h",
             "tgmath.h",
             "threads.h",
             "time.h",
             "uchar.h",
             "wchar.h",
             "wctype.h",
             "cassert",
             "ccomplex",
             "cctype",
             "cerrno",
             "cfenv",
             "cfloat",
             "cinttypes",
             "ciso646",
             "climits",
             "clocale",
             "cmath",
             "csetjmp",
             "csignal",
             "cstdalign",
             "cstdarg",
             "cstdbool",
             "cstddef",
             "cstdint",
             "cstdio",
             "cstdlib",
             "cstring",
             "ctgmath",
             "ctime",
             "cuchar",
             "cwchar",
             "cwctype",
             "algorithm",
             "fstream",
             "list",
             "regex",
             "thread",
             "array",
             "functional",
             "locale",
             "scoped_allocator",
             "tuple",
             "atomic",
             "future",
             "map",
             "set",
             "type_traits",
             "bitset",
             "initializer_list",
             "memory",
             "shared_mutex",
             "typeindex",
             "chrono",
             "iomanip",
             "mutex",
             "sstream",
             "typeinfo",
             "codecvt",
             "ios",
             "new",
             "stack",
             "unordered_map",
             "complex",
             "iosfwd",
             "numeric",
             "stdexcept",
             "unordered_set",
             "condition_variable",
             "iostream",
             "ostream",
             "streambuf",
             "utility",
             "deque",
             "istream",
             "queue",
             "string",
             "valarray",
             "exception",
             "iterator",
             "random",
             "strstream",
             "vector",
             "forward_list",
             "limits",
             "ratio",
             "system_error",
             "aio.h",
             "arpa/inet.h",
             "cpio.h",
             "dirent.h",
             "dlfcn.h",
             "fcntl.h",
             "fmtmsg.h",
             "fnmatch.h",
             "ftw.h",
             "glob.h",
             "grp.h",
             "iconv.h",
             "langinfo.h",
             "libgen.h",
             "monetary.h",
             "mqueue.h",
             "ndbm.h",
             "net/if.h",
             "netdb.h",
             "netinet/in.h",
             "netinet/tcp.h",
             "nl_types.h",
             "poll.h",
             "pthread.h",
             "pwd.h",
             "regex.h",
             "sched.h",
             "search.h",
             "semaphore.h",
             "spawn.h",
             "strings.h",
             "stropts.h",
             "sys/ipc.h",
             "sys/mman.h",
             "sys/msg.h",
             "sys/resource.h",
             "sys/select.h",
             "sys/sem.h",
             "sys/shm.h",
             "sys/socket.h",
             "sys/stat.h",
             "sys/statvfs.h",
             "sys/time.h",
             "sys/times.h",
             "sys/types.h",
             "sys/uio.h",
             "sys/un.h",
             "sys/utsname.h",
             "sys/wait.h",
             "syslog.h",
             "tar.h",
             "termios.h",
             "trace.h",
             "ulimit.h",
             "unistd.h",
             "utime.h",
             "utmpx.h",
             "wordexp.h",
         })
      S.insert(H);
    return S;
  }();
  return KnownHeaders.count(LowerInclude);
}
} // namespace

// ===----------------------------------------------------------------------===
// Include directive handling
// ===----------------------------------------------------------------------===

bool PrepEngine::extractIncludeFilename(SourceLocation Loc,
                                        llvm::StringRef &Buffer) {
  assert(!Buffer.empty() && "Can't have tokens with empty spellings!");

  // Make sure the filename is <x> or "x".
  bool isAngled;
  if (Buffer[0] == '<') {
    if (Buffer.back() != '>') {
      Diag(Loc, diag::err_pp_expects_filename);
      Buffer = llvm::StringRef();
      return true;
    }
    isAngled = true;
  } else if (Buffer[0] == '"') {
    if (Buffer.back() != '"') {
      Diag(Loc, diag::err_pp_expects_filename);
      Buffer = llvm::StringRef();
      return true;
    }
    isAngled = false;
  } else {
    Diag(Loc, diag::err_pp_expects_filename);
    Buffer = llvm::StringRef();
    return true;
  }

  // Diagnose #include "" as invalid.
  if (Buffer.size() <= 2) {
    Diag(Loc, diag::err_pp_empty_filename);
    Buffer = llvm::StringRef();
    return true;
  }

  // Skip the brackets.
  Buffer = Buffer.substr(1, Buffer.size() - 2);
  return isAngled;
}

void PrepEngine::InjectAnnotation(SourceRange Range, tok::TokenKind Kind,
                                  void *AnnotationVal) {
  auto Tok = std::make_unique<Token[]>(1);
  Tok[0].startToken();
  Tok[0].setKind(Kind);
  Tok[0].setLocation(Range.getBegin());
  Tok[0].setAnnotationEndLoc(Range.getEnd());
  Tok[0].setAnnotationValue(AnnotationVal);
  PushTokenStream(std::move(Tok), 1, true, /*IsReinject*/ false);
}

// Given a vector of path components and a string containing the real
// path to the file, build a properly-cased replacement in the vector,
// and return true if the replacement should be suggested.
namespace {
bool collapsePathSegments(llvm::SmallVectorImpl<llvm::StringRef> &Components,
                          llvm::StringRef RealPathName,
                          llvm::sys::path::Style Separator) {
  auto RealPathComponentIter = llvm::sys::path::rbegin(RealPathName);
  auto RealPathComponentEnd = llvm::sys::path::rend(RealPathName);
  int Cnt = 0;
  bool SuggestReplacement = false;

  auto IsSep = [Separator](llvm::StringRef Component) {
    return Component.size() == 1 &&
           llvm::sys::path::is_separator(Component[0], Separator);
  };

  // Below is a best-effort to handle ".." in paths. It is admittedly
  // not 100% correct in the presence of symlinks.
  for (auto &Component : llvm::reverse(Components)) {
    if ("." == Component) {
    } else if (".." == Component) {
      ++Cnt;
    } else if (Cnt) {
      --Cnt;
    } else if (RealPathComponentIter != RealPathComponentEnd) {
      if (!IsSep(Component) && !IsSep(*RealPathComponentIter) &&
          Component != *RealPathComponentIter) {
        // If these non-separator path components differ by more than just case,
        // then we may be looking at symlinked paths. Bail on this diagnostic to
        // avoid noisy false positives.
        SuggestReplacement =
            RealPathComponentIter->equals_insensitive(Component);
        if (!SuggestReplacement)
          break;
        Component = *RealPathComponentIter;
      }
      ++RealPathComponentIter;
    }
  }
  return SuggestReplacement;
}
} // namespace

std::pair<ConstSearchDirIterator, const FileEntry *>
PrepEngine::getIncludeNextStart(const Token &IncludeNextTok) const {
  // #include_next is like #include, except that we start searching after
  // the current found directory.  If we can't do this, issue a
  // diagnostic.
  ConstSearchDirIterator Lookup = CurDirLookup;
  const FileEntry *LookupFromFile = nullptr;

  if (isInPrimaryFile() && LangOpts.IsHeaderFile) {
    // If the main file is a header (e.g. -xc-header), allow include_next.
    // Handle it as a normal include below and do not complain about
    // include_next.
  } else if (isInPrimaryFile()) {
    Lookup = nullptr;
    Diag(IncludeNextTok, diag::pp_include_next_in_primary);
  } else if (!Lookup) {
    // The current file was not found by walking the include path. Either it
    // is the primary file (handled above), or it was found by absolute path,
    // or it was found relative to such a file.
    Diag(IncludeNextTok, diag::pp_include_next_absolute_path);
  } else {
    // Start looking up in the next directory.
    ++Lookup;
  }

  return {Lookup, LookupFromFile};
}

// ===----------------------------------------------------------------------===
// Include & import processing
// ===----------------------------------------------------------------------===

void PrepEngine::ExecInclude(SourceLocation HashLoc, Token &IncludeTok,
                             ConstSearchDirIterator LookupFrom,
                             const FileEntry *LookupFromFile) {
  Token FilenameTok;
  if (LexIncludePathTok(FilenameTok))
    return;

  if (FilenameTok.isNot(tok::header_name)) {
    Diag(FilenameTok.getLocation(), diag::err_pp_expects_filename);
    if (FilenameTok.isNot(tok::eod))
      DiscardDirectiveLine();
    return;
  }

  // Verify nothing follows the filename (empty macro expansions are allowed).
  SourceLocation EndLoc =
      VerifyDirectiveEnd(IncludeTok.getIdentifierInfo()->getNameStart(), true);

  ExecHeaderImport(HashLoc, IncludeTok, FilenameTok, EndLoc, LookupFrom,
                   LookupFromFile);
}

OptionalFileEntryRef PrepEngine::FindIncludeTarget(
    ConstSearchDirIterator *CurDir, llvm::StringRef &Filename,
    SourceLocation FilenameLoc, CharSourceRange FilenameRange,
    const Token &FilenameTok, bool &IsFrameworkFound, bool &IsMapped,
    ConstSearchDirIterator LookupFrom, const FileEntry *LookupFromFile,
    llvm::StringRef &ResolveIncludename,
    llvm::SmallVectorImpl<char> &RelativePath,
    llvm::SmallVectorImpl<char> &SearchPath, bool isAngled) {
  OptionalFileEntryRef File = ResolveInclude(
      FilenameLoc, ResolveIncludename, isAngled, LookupFrom, LookupFromFile,
      CurDir, Callbacks ? &SearchPath : nullptr,
      Callbacks ? &RelativePath : nullptr, &IsMapped, &IsFrameworkFound);
  if (File)
    return File;

  // Give the clients a chance to silently skip this include.
  if (Callbacks && Callbacks->FileNotFound(Filename))
    return std::nullopt;

  if (SuppressIncludeNotFoundError)
    return std::nullopt;

  // If the file could not be located and it was included via angle
  // brackets, we can attempt a lookup as though it were a quoted path to
  // provide the user with a possible fixit.
  if (isAngled) {
    OptionalFileEntryRef File = ResolveInclude(
        FilenameLoc, ResolveIncludename, false, LookupFrom, LookupFromFile,
        CurDir, Callbacks ? &SearchPath : nullptr,
        Callbacks ? &RelativePath : nullptr, &IsMapped,
        /*IsFrameworkFound=*/nullptr);
    if (File) {
      Diag(FilenameTok, diag::err_pp_file_not_found_angled_include_not_fatal)
          << Filename << false
          << FixItHint::CreateReplacement(FilenameRange,
                                          ("\"" + Filename + "\"").str());
      return File;
    }
  }

  llvm::StringRef OriginalFilename = Filename;
  if (LangOpts.SpellChecking) {
    // A heuristic to correct a typo file name by removing leading and
    // trailing non-isAlphanumeric characters.
    auto CorrectTypoFilename = [](llvm::StringRef Filename) {
      Filename = Filename.drop_until(isAlphanumeric);
      while (!Filename.empty() && !isAlphanumeric(Filename.back())) {
        Filename = Filename.drop_back();
      }
      return Filename;
    };
    llvm::StringRef TypoCorrectionName = CorrectTypoFilename(Filename);
    llvm::StringRef TypoCorrectionLookupName =
        CorrectTypoFilename(ResolveIncludename);

    OptionalFileEntryRef File = ResolveInclude(
        FilenameLoc, TypoCorrectionLookupName, isAngled, LookupFrom,
        LookupFromFile, CurDir, Callbacks ? &SearchPath : nullptr,
        Callbacks ? &RelativePath : nullptr, &IsMapped,
        /*IsFrameworkFound=*/nullptr);
    if (File) {
      auto Hint =
          isAngled
              ? FixItHint::CreateReplacement(
                    FilenameRange, ("<" + TypoCorrectionName + ">").str())
              : FixItHint::CreateReplacement(
                    FilenameRange, ("\"" + TypoCorrectionName + "\"").str());
      Diag(FilenameTok, diag::err_pp_file_not_found_typo_not_fatal)
          << OriginalFilename << TypoCorrectionName << Hint;
      // We found the file, so set the Filename to the name after typo
      // correction.
      Filename = TypoCorrectionName;
      ResolveIncludename = TypoCorrectionLookupName;
      return File;
    }
  }

  // If the file is still not found, just go with the vanilla diagnostic
  assert(!File && "expected missing file");
  Diag(FilenameTok, diag::err_pp_file_not_found)
      << OriginalFilename << FilenameRange;
  if (IsFrameworkFound) {
    size_t SlashPos = OriginalFilename.find('/');
    assert(SlashPos != llvm::StringRef::npos &&
           "Include with framework name should have '/' in the filename");
    llvm::StringRef FrameworkName = OriginalFilename.substr(0, SlashPos);
    FrameworkCacheEntry &CacheEntry =
        HeaderInfo.LookupFrameworkCache(FrameworkName);
    assert(CacheEntry.Directory && "Found framework should be in cache");
    Diag(FilenameTok, diag::note_pp_framework_without_header)
        << OriginalFilename.substr(SlashPos + 1) << FrameworkName
        << CacheEntry.Directory->getName();
  }

  return std::nullopt;
}

void PrepEngine::ExecHeaderImport(SourceLocation HashLoc, Token &IncludeTok,
                                  Token &FilenameTok, SourceLocation EndLoc,
                                  ConstSearchDirIterator LookupFrom,
                                  const FileEntry *LookupFromFile) {
  llvm::SmallString<128> FilenameBuffer;
  llvm::StringRef Filename = getSpelling(FilenameTok, FilenameBuffer);
  SourceLocation CharEnd = FilenameTok.getEndLoc();

  CharSourceRange FilenameRange =
      CharSourceRange::getCharRange(FilenameTok.getLocation(), CharEnd);
  llvm::StringRef OriginalFilename = Filename;
  bool isAngled = extractIncludeFilename(FilenameTok.getLocation(), Filename);

  // If extractIncludeFilename set the start ptr to null, there was an
  // error.
  if (Filename.empty())
    return;

  SourceLocation StartLoc = HashLoc;

  if (PragmaAssumeNonNullLoc.isValid()) {
    Diag(StartLoc, diag::err_pp_include_in_assume_nonnull) << false;
    Diag(PragmaAssumeNonNullLoc, diag::note_pragma_entered_here);

    // Immediately leave the pragma.
    PragmaAssumeNonNullLoc = SourceLocation();
  }

  if (HeaderInfo.HasIncludeAliasMap()) {
    // Map the filename with the brackets still attached.  If the name doesn't
    // map to anything, fall back on the filename we've already gotten the
    // spelling for.
    llvm::StringRef NewName =
        HeaderInfo.MapHeaderToIncludeAlias(OriginalFilename);
    if (!NewName.empty())
      Filename = NewName;
  }

  // Search include directories.
  bool IsMapped = false;
  bool IsFrameworkFound = false;
  ConstSearchDirIterator CurDir = nullptr;
  llvm::SmallString<1024> SearchPath;
  llvm::SmallString<1024> RelativePath;
  // We get the raw path only if we have 'Callbacks' to which we later pass
  // the path.
  SourceLocation FilenameLoc = FilenameTok.getLocation();
  llvm::StringRef ResolveIncludename = Filename;

  // Normalize slashes when compiling with -fms-extensions on non-Windows. This
  // is unnecessary on Windows since the filesystem there handles backslashes.
  llvm::SmallString<128> NormalizedPath;
  llvm::sys::path::Style BackslashStyle = llvm::sys::path::Style::native;
  if (is_style_posix(BackslashStyle) && LangOpts.MicrosoftExt) {
    NormalizedPath = Filename.str();
    llvm::sys::path::native(NormalizedPath);
    ResolveIncludename = NormalizedPath;
    BackslashStyle = llvm::sys::path::Style::windows;
  }

  OptionalFileEntryRef File = FindIncludeTarget(
      &CurDir, Filename, FilenameLoc, FilenameRange, FilenameTok,
      IsFrameworkFound, IsMapped, LookupFrom, LookupFromFile,
      ResolveIncludename, RelativePath, SearchPath, isAngled);

  // Should we enter the source file? Set to Skip if either the source file is
  // known to have no effect beyond its effect on module visibility -- that is,
  // if it's got an include guard that is already defined, set to Import if it
  // is a modular header we've already built and should import.

  // Modular headers: replacing #include with import is only allowed in limited
  // contexts (e.g. global module fragment), not throughout named-module code.

  enum { Enter, Import, Skip, IncludeLimitReached } Action = Enter;

  // If we've reached the max allowed include depth, it is usually due to an
  // include cycle. Don't enter already processed files again as it can lead to
  // reaching the max allowed include depth again.
  if (Action == Enter && HasReachedMaxIncludeDepth && File &&
      alreadyIncluded(*File))
    Action = IncludeLimitReached;

  // The #included file will be considered to be a system header if either it is
  // in a system include directory, or if the #includer is a system include
  // header.
  SrcMgr::CharacteristicKind FileCharacter =
      SourceMgr.getFileCharacteristic(FilenameTok.getLocation());
  if (File)
    FileCharacter = std::max(HeaderInfo.getFileDirFlavor(*File), FileCharacter);

  // If this is a '#import' or an import-declaration, don't re-enter the file.
  //
  bool EnterOnce =
      IncludeTok.getIdentifierInfo()->getPPKeywordID() == tok::pp_import;

  bool IsFirstIncludeOfFile = false;

  // Ask HeaderInfo if we should enter this #include file.  If not, #including
  // this file will have no effect.
  if (Action == Enter && File &&
      !HeaderInfo.ShouldProcessInclude(*this, *File, EnterOnce,
                                       IsFirstIncludeOfFile)) {
    Action = Skip;
  }

  if (Callbacks) {
    Callbacks->InclusionDirective(HashLoc, IncludeTok, ResolveIncludename,
                                  isAngled, FilenameRange, File, SearchPath,
                                  RelativePath, FileCharacter);
    if (Action == Skip && File)
      Callbacks->FileSkipped(*File, FilenameTok, FileCharacter);
  }

  if (!File)
    return;

  // Issue a diagnostic if the name of the file on disk has a different case
  // than the one we're about to open.
  const bool CheckIncludePathPortability =
      !IsMapped && !File->getFileEntry().tryGetRealPathName().empty();

  if (CheckIncludePathPortability) {
    llvm::StringRef Name = ResolveIncludename;
    llvm::StringRef NameWithoriginalSlashes = Filename;
#if defined(_WIN32)
    // Skip UNC prefix if present. (tryGetRealPathName() always
    // returns a path with the prefix skipped.)
    bool NameWasUNC = Name.consume_front("\\\\?\\");
    NameWithoriginalSlashes.consume_front("\\\\?\\");
#endif
    llvm::StringRef RealPathName = File->getFileEntry().tryGetRealPathName();
    llvm::SmallVector<llvm::StringRef, 16> Components(
        llvm::sys::path::begin(Name), llvm::sys::path::end(Name));
#if defined(_WIN32)
    // -Wnonportable-include-path is designed to diagnose includes using
    // case even on systems with a case-insensitive file system.
    // On Windows, RealPathName always starts with an upper-case drive
    // letter for absolute paths, but Name might start with either
    // case depending on if `cd c:\foo` or `cd C:\foo` was used in the shell.
    // ("foo" will always have on-disk case, no matter which case was
    // used in the cd command). To not emit this warning solely for
    // the drive letter, whose case is dependent on if `cd` is used
    // with upper- or lower-case drive letters, always consider the
    // given drive letter case as correct for the purpose of this warning.
    llvm::SmallString<128> FixedDriveRealPath;
    if (llvm::sys::path::is_absolute(Name) &&
        llvm::sys::path::is_absolute(RealPathName) &&
        toLowercase(Name[0]) == toLowercase(RealPathName[0]) &&
        isLowercase(Name[0]) != isLowercase(RealPathName[0])) {
      assert(Components.size() >= 3 && "should have drive, backslash, name");
      assert(Components[0].size() == 2 && "should start with drive");
      assert(Components[0][1] == ':' && "should have colon");
      FixedDriveRealPath = (Name.substr(0, 1) + RealPathName.substr(1)).str();
      RealPathName = FixedDriveRealPath;
    }
#endif

    if (collapsePathSegments(Components, RealPathName, BackslashStyle)) {
      llvm::SmallString<128> Path;
      Path.reserve(Name.size() + 2);
      Path.push_back(isAngled ? '<' : '"');

      const auto IsSep = [BackslashStyle](char c) {
        return llvm::sys::path::is_separator(c, BackslashStyle);
      };

      for (auto Component : Components) {
        // On POSIX, Components will contain a single '/' as first element
        // exactly if Name is an absolute path.
        // On Windows, it will contain "C:" followed by '\' for absolute paths.
        // The drive letter is optional for absolute paths on Windows, but
        // NeverC currently cannot process absolute paths in #include lines that
        // don't have a drive.
        // If the first entry in Components is a directory separator,
        // then the code at the bottom of this loop that keeps the original
        // directory separator style copies it. If the second entry is
        // a directory separator (the C:\ case), then that separator already
        // got copied when the C: was processed and we want to skip that entry.
        if (!(Component.size() == 1 && IsSep(Component[0])))
          Path.append(Component);
        else if (Path.size() != 1)
          continue;

        // Append the separator(s) the user used, or the close quote
        if (Path.size() > NameWithoriginalSlashes.size()) {
          Path.push_back(isAngled ? '>' : '"');
          continue;
        }
        assert(IsSep(NameWithoriginalSlashes[Path.size() - 1]));
        do
          Path.push_back(NameWithoriginalSlashes[Path.size() - 1]);
        while (Path.size() <= NameWithoriginalSlashes.size() &&
               IsSep(NameWithoriginalSlashes[Path.size() - 1]));
      }

#if defined(_WIN32)
      // Restore UNC prefix if it was there.
      if (NameWasUNC)
        Path = (Path.substr(0, 1) + "\\\\?\\" + Path.substr(1)).str();
#endif

      // For user files and known standard headers, issue a diagnostic.
      // For other system headers, don't. They can be controlled separately.
      auto DiagId =
          (FileCharacter == SrcMgr::C_User || detectIncludeCaseDiff(Name))
              ? diag::pp_nonportable_path
              : diag::pp_nonportable_system_path;
      Diag(FilenameTok, DiagId)
          << Path << FixItHint::CreateReplacement(FilenameRange, Path);
    }
  }

  switch (Action) {
  case Skip:
  case IncludeLimitReached:
  case Import:
    return;

  case Enter:
    break;
  }
  if (IncludeMacroStack.size() == MaxAllowedIncludeStackDepth - 1) {
    Diag(FilenameTok, diag::err_pp_include_too_deep);
    HasReachedMaxIncludeDepth = true;
    return;
  }

  // Look up the file, create a File ID for it.
  SourceLocation IncludePos = FilenameTok.getLocation();
  // If the filename string was the result of macro expansions, set the include
  // position on the file where it will be included and after the expansions.
  if (IncludePos.isMacroID())
    IncludePos = SourceMgr.getExpansionRange(IncludePos).getEnd();
  FileID FID = SourceMgr.createFileID(*File, IncludePos, FileCharacter);
  if (!FID.isValid()) {
    return;
  }

  // If all is good, enter the new file!
  if (PushSourceFile(FID, CurDir, FilenameTok.getLocation(),
                     IsFirstIncludeOfFile))
    return;

  // Determine if we're switching to building a new submodule, and which one.
}

void PrepEngine::ExecIncludeNext(SourceLocation HashLoc,
                                 Token &IncludeNextTok) {
  Diag(IncludeNextTok, diag::ext_pp_include_next_directive);

  ConstSearchDirIterator Lookup = nullptr;
  const FileEntry *LookupFromFile;
  std::tie(Lookup, LookupFromFile) = getIncludeNextStart(IncludeNextTok);

  return ExecInclude(HashLoc, IncludeNextTok, Lookup, LookupFromFile);
}

void PrepEngine::ExecMsImport(Token &Tok) {
  // The Microsoft #import directive takes a type library and generates header
  // files from it, and includes those.  This is beyond the scope of what NeverC
  // does, so we ignore it and error out.  However, #import can optionally have
  // trailing attributes that span multiple lines.  We're going to eat those
  // so we can continue processing from there.
  Diag(Tok, diag::err_pp_import_directive_ms);

  // Read tokens until we get to the end of the directive.  Note that the
  // directive can be split over multiple lines using the backslash character.
  DiscardDirectiveLine();
}

void PrepEngine::ExecImport(SourceLocation HashLoc, Token &ImportTok) {
  if (LangOpts.MSVCCompat)
    return ExecMsImport(ImportTok);
  Diag(ImportTok, diag::ext_pp_import_directive);
  return ExecInclude(HashLoc, ImportTok);
}

void PrepEngine::ExecIncludeMacros(SourceLocation HashLoc,
                                   Token &IncludeMacrosTok) {
  // This directive should only occur in the predefines buffer.  If not, emit an
  // error and reject it.
  SourceLocation Loc = IncludeMacrosTok.getLocation();
  if (SourceMgr.getBufferName(Loc) != "<built-in>") {
    Diag(IncludeMacrosTok.getLocation(),
         diag::pp_include_macros_out_of_predefines);
    DiscardDirectiveLine();
    return;
  }

  // Treat this as a normal #include for checking purposes.  If this is
  // successful, it will push a new lexer onto the include stack.
  ExecInclude(HashLoc, IncludeMacrosTok);

  Token TmpTok;
  do {
    Lex(TmpTok);
    assert(TmpTok.isNot(tok::eof) && "Didn't find end of -imacros!");
  } while (TmpTok.isNot(tok::hashhash));
}
