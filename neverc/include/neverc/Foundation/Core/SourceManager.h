#ifndef NEVERC_FOUNDATION_SOURCEMANAGER_H
#define NEVERC_FOUNDATION_SOURCEMANAGER_H

#include "neverc/Foundation/Core/FileEntry.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/PagedVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cassert>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace neverc {

class FileManager;
class LineTableInfo;
class SourceManager;

namespace SrcMgr {

enum CharacteristicKind { C_User, C_System, C_ExternCSystem };

inline bool isSystem(CharacteristicKind CK) { return CK != C_User; }

class LineOffsetMapping {
public:
  explicit operator bool() const { return Storage; }
  unsigned size() const {
    assert(Storage);
    return Storage[0];
  }
  llvm::ArrayRef<unsigned> getLines() const {
    assert(Storage);
    return llvm::ArrayRef<unsigned>(Storage + 1, Storage + 1 + size());
  }
  const unsigned *begin() const { return getLines().begin(); }
  const unsigned *end() const { return getLines().end(); }
  const unsigned &operator[](int I) const { return getLines()[I]; }

  static LineOffsetMapping get(llvm::MemoryBufferRef Buffer,
                               llvm::BumpPtrAllocator &Alloc);

  LineOffsetMapping() = default;
  LineOffsetMapping(llvm::ArrayRef<unsigned> LineOffsets,
                    llvm::BumpPtrAllocator &Alloc);

private:
  unsigned *Storage = nullptr;
};

class alignas(8) ContentCache {
  mutable std::unique_ptr<llvm::MemoryBuffer> Buffer;

public:
  OptionalFileEntryRef OrigEntry;

  OptionalFileEntryRef ContentsEntry;

  llvm::StringRef Filename;

  mutable LineOffsetMapping SourceLineCache;

  LLVM_PREFERRED_TYPE(bool)
  unsigned BufferOverridden : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFileVolatile : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned IsTransient : 1;

  LLVM_PREFERRED_TYPE(bool)
  mutable unsigned IsBufferInvalid : 1;

  ContentCache()
      : OrigEntry(std::nullopt), ContentsEntry(std::nullopt),
        BufferOverridden(false), IsFileVolatile(false), IsTransient(false),
        IsBufferInvalid(false) {}

  ContentCache(FileEntryRef Ent) : ContentCache(Ent, Ent) {}

  ContentCache(FileEntryRef Ent, FileEntryRef contentEnt)
      : OrigEntry(Ent), ContentsEntry(contentEnt), BufferOverridden(false),
        IsFileVolatile(false), IsTransient(false), IsBufferInvalid(false) {}

  ContentCache(const ContentCache &RHS)
      : BufferOverridden(false), IsFileVolatile(false), IsTransient(false),
        IsBufferInvalid(false) {
    OrigEntry = RHS.OrigEntry;
    ContentsEntry = RHS.ContentsEntry;

    assert(!RHS.Buffer && !RHS.SourceLineCache &&
           "Passed ContentCache object cannot own a buffer.");
  }

  ContentCache &operator=(const ContentCache &RHS) = delete;

  std::optional<llvm::MemoryBufferRef>
  getBufferOrNone(DiagnosticsEngine &Diag, FileManager &FM,
                  SourceLocation Loc = SourceLocation()) const;

  unsigned getSize() const;

  unsigned getSizeBytesMapped() const;

  llvm::MemoryBuffer::BufferKind getMemoryBufferKind() const;

  std::optional<llvm::MemoryBufferRef> getBufferIfLoaded() const {
    if (Buffer)
      return Buffer->getMemBufferRef();
    return std::nullopt;
  }

  std::optional<llvm::StringRef> getBufferDataIfLoaded() const {
    if (Buffer)
      return Buffer->getBuffer();
    return std::nullopt;
  }

  void setBuffer(std::unique_ptr<llvm::MemoryBuffer> B) {
    IsBufferInvalid = false;
    Buffer = std::move(B);
  }

  void setUnownedBuffer(std::optional<llvm::MemoryBufferRef> B) {
    assert(!Buffer && "Expected to be called right after construction");
    if (B)
      setBuffer(llvm::MemoryBuffer::getMemBuffer(*B));
  }

  // If BufStr has an invalid BOM, returns the BOM name; otherwise, returns
  // nullptr
  static const char *getInvalidBOM(llvm::StringRef BufStr);

  // Check buffer is UTF16
  static bool checkBufUTF16(llvm::StringRef BufStr);

  // Check buffer is UTF32
  static bool checkBufUTF32(llvm::StringRef BufStr);
};

// Assert that the \c ContentCache objects will always be 8-byte aligned so
// that we can pack 3 bits of integer into pointers to such objects.
static_assert(alignof(ContentCache) >= 8,
              "ContentCache must be 8-byte aligned.");

class FileInfo {
  friend class neverc::SourceManager;

  SourceLocation IncludeLoc;

  unsigned NumCreatedFIDs : 31;

  LLVM_PREFERRED_TYPE(bool)
  unsigned HasLineDirectives : 1;

  llvm::PointerIntPair<const ContentCache *, 3, CharacteristicKind>
      ContentAndKind;

public:
  static FileInfo get(SourceLocation IL, ContentCache &Con,
                      CharacteristicKind FileCharacter,
                      llvm::StringRef Filename) {
    FileInfo X;
    X.IncludeLoc = IL;
    X.NumCreatedFIDs = 0;
    X.HasLineDirectives = false;
    X.ContentAndKind.setPointer(&Con);
    X.ContentAndKind.setInt(FileCharacter);
    Con.Filename = Filename;
    return X;
  }

  SourceLocation getIncludeLoc() const { return IncludeLoc; }

  const ContentCache &getContentCache() const {
    return *ContentAndKind.getPointer();
  }

  CharacteristicKind getFileCharacteristic() const {
    return ContentAndKind.getInt();
  }

  bool hasLineDirectives() const { return HasLineDirectives; }

  void setHasLineDirectives() { HasLineDirectives = true; }

  llvm::StringRef getName() const { return getContentCache().Filename; }
};

class ExpansionInfo {
  // Really these are all SourceLocations.

  SourceLocation SpellingLoc;

  SourceLocation ExpansionLocStart, ExpansionLocEnd;

  bool ExpansionIsTokenRange;

public:
  SourceLocation getSpellingLoc() const {
    return SpellingLoc.isInvalid() ? getExpansionLocStart() : SpellingLoc;
  }

  SourceLocation getExpansionLocStart() const { return ExpansionLocStart; }

  SourceLocation getExpansionLocEnd() const {
    return ExpansionLocEnd.isInvalid() ? getExpansionLocStart()
                                       : ExpansionLocEnd;
  }

  bool isExpansionTokenRange() const { return ExpansionIsTokenRange; }

  CharSourceRange getExpansionLocRange() const {
    return CharSourceRange(
        SourceRange(getExpansionLocStart(), getExpansionLocEnd()),
        isExpansionTokenRange());
  }

  bool isMacroArgExpansion() const {
    // Note that this needs to return false for default constructed objects.
    return getExpansionLocStart().isValid() && ExpansionLocEnd.isInvalid();
  }

  bool isMacroBodyExpansion() const {
    return getExpansionLocStart().isValid() && ExpansionLocEnd.isValid();
  }

  bool isFunctionMacroExpansion() const {
    return getExpansionLocStart().isValid() &&
           getExpansionLocStart() != getExpansionLocEnd();
  }

  static ExpansionInfo create(SourceLocation SpellingLoc, SourceLocation Start,
                              SourceLocation End,
                              bool ExpansionIsTokenRange = true) {
    ExpansionInfo X;
    X.SpellingLoc = SpellingLoc;
    X.ExpansionLocStart = Start;
    X.ExpansionLocEnd = End;
    X.ExpansionIsTokenRange = ExpansionIsTokenRange;
    return X;
  }

  static ExpansionInfo createForMacroArg(SourceLocation SpellingLoc,
                                         SourceLocation ExpansionLoc) {
    // We store an intentionally invalid source location for the end of the
    // expansion range to mark that this is a macro argument location rather
    // than a normal one.
    return create(SpellingLoc, ExpansionLoc, SourceLocation());
  }

  static ExpansionInfo createForTokenSplit(SourceLocation SpellingLoc,
                                           SourceLocation Start,
                                           SourceLocation End) {
    return create(SpellingLoc, Start, End, false);
  }
};

// Assert that the \c FileInfo objects are no bigger than \c ExpansionInfo
// objects. This controls the size of \c SLocEntry, of which we have one for
// each macro expansion. The number of (unloaded) macro expansions can be
// very large. Any other fields needed in FileInfo should go in ContentCache.
static_assert(sizeof(FileInfo) <= sizeof(ExpansionInfo),
              "FileInfo must be no larger than ExpansionInfo.");

class SLocEntry {
  static constexpr int OffsetBits = 8 * sizeof(SourceLocation::UIntTy) - 1;
  SourceLocation::UIntTy Offset : OffsetBits;
  LLVM_PREFERRED_TYPE(bool)
  SourceLocation::UIntTy IsExpansion : 1;
  union {
    FileInfo File;
    ExpansionInfo Expansion;
  };

public:
  SLocEntry() : Offset(), IsExpansion(), File() {}

  SourceLocation::UIntTy getOffset() const { return Offset; }

  bool isExpansion() const { return IsExpansion; }
  bool isFile() const { return !isExpansion(); }

  const FileInfo &getFile() const {
    assert(isFile() && "Not a file SLocEntry!");
    return File;
  }

  const ExpansionInfo &getExpansion() const {
    assert(isExpansion() && "Not a macro expansion SLocEntry!");
    return Expansion;
  }

  static SLocEntry getOffsetOnly(SourceLocation::UIntTy Offset) {
    assert(!(Offset & (1ULL << OffsetBits)) && "Offset is too large");
    SLocEntry E;
    E.Offset = Offset;
    return E;
  }

  static SLocEntry get(SourceLocation::UIntTy Offset, const FileInfo &FI) {
    assert(!(Offset & (1ULL << OffsetBits)) && "Offset is too large");
    SLocEntry E;
    E.Offset = Offset;
    E.IsExpansion = false;
    E.File = FI;
    return E;
  }

  static SLocEntry get(SourceLocation::UIntTy Offset,
                       const ExpansionInfo &Expansion) {
    assert(!(Offset & (1ULL << OffsetBits)) && "Offset is too large");
    SLocEntry E;
    E.Offset = Offset;
    E.IsExpansion = true;
    new (&E.Expansion) ExpansionInfo(Expansion);
    return E;
  }
};

} // namespace SrcMgr

class ExternalSLocEntrySource {
public:
  virtual ~ExternalSLocEntrySource();

  virtual bool ReadSLocEntry(int ID) = 0;

  virtual int getSLocEntryID(SourceLocation::UIntTy SLocOffset) = 0;
};

class InBeforeInTUCacheEntry {
  FileID LQueryFID, RQueryFID;

  bool LChildBeforeRChild;

  FileID CommonFID;

  unsigned LCommonOffset, RCommonOffset;

public:
  InBeforeInTUCacheEntry() = default;
  InBeforeInTUCacheEntry(FileID L, FileID R) : LQueryFID(L), RQueryFID(R) {
    assert(L != R);
  }

  bool isCacheValid() const { return CommonFID.isValid(); }

  bool getCachedResult(unsigned LOffset, unsigned ROffset) const {
    // If one of the query files is the common file, use the offset.  Otherwise,
    // use the #include loc in the common file.
    if (LQueryFID != CommonFID)
      LOffset = LCommonOffset;
    if (RQueryFID != CommonFID)
      ROffset = RCommonOffset;

    // It is common for multiple macro expansions to be "included" from the same
    // location (expansion location), in which case use the order of the FileIDs
    // to determine which came first. This will also take care the case where
    // one of the locations points at the inclusion/expansion point of the other
    // in which case its FileID will come before the other.
    if (LOffset == ROffset)
      return LChildBeforeRChild;

    return LOffset < ROffset;
  }

  void setQueryFIDs(FileID LHS, FileID RHS) {
    assert(LHS != RHS);
    if (LQueryFID != LHS || RQueryFID != RHS) {
      LQueryFID = LHS;
      RQueryFID = RHS;
      CommonFID = FileID();
    }
  }

  void setCommonLoc(FileID commonFID, unsigned lCommonOffset,
                    unsigned rCommonOffset, bool LParentBeforeRParent) {
    CommonFID = commonFID;
    LCommonOffset = lCommonOffset;
    RCommonOffset = rCommonOffset;
    LChildBeforeRChild = LParentBeforeRParent;
  }
};

class SourceManager : public llvm::RefCountedBase<SourceManager> {
  DiagnosticsEngine &Diag;

  FileManager &FileMgr;

  mutable llvm::BumpPtrAllocator ContentCacheAlloc;

  llvm::DenseMap<FileEntryRef, SrcMgr::ContentCache *> FileInfos;

  bool UserFilesAreVolatile;

  bool FilesAreTransient = false;

  struct OverriddenFilesInfoTy {
    /// Files that have been overridden with the contents from another
    /// file.
    llvm::DenseMap<const FileEntry *, FileEntryRef> OverriddenFiles;

    /// Files that were overridden with a memory buffer.
    llvm::SmallPtrSet<const FileEntry *, 4> OverriddenFilesWithBuffer;
  };

  std::unique_ptr<OverriddenFilesInfoTy> OverriddenFilesInfo;

  OverriddenFilesInfoTy &getOverriddenFilesInfo() {
    if (!OverriddenFilesInfo)
      OverriddenFilesInfo.reset(new OverriddenFilesInfoTy);
    return *OverriddenFilesInfo;
  }

  std::vector<SrcMgr::ContentCache *> MemBufferInfos;

  llvm::SmallVector<SrcMgr::SLocEntry, 0> LocalSLocEntryTable;

  llvm::PagedVector<SrcMgr::SLocEntry> LoadedSLocEntryTable;

  llvm::SmallVector<FileID, 0> LoadedSLocEntryAllocBegin;

  SourceLocation::UIntTy NextLocalOffset;

  SourceLocation::UIntTy CurrentLoadedOffset;

  static const SourceLocation::UIntTy MaxLoadedOffset =
      1ULL << (8 * sizeof(SourceLocation::UIntTy) - 1);

  llvm::BitVector SLocEntryLoaded;

  llvm::BitVector SLocEntryOffsetLoaded;

  ExternalSLocEntrySource *ExternalSLocEntries = nullptr;

  mutable FileID LastFileIDLookup;

  std::unique_ptr<LineTableInfo> LineTable;

  mutable FileID LastLineNoFileIDQuery;
  mutable const SrcMgr::ContentCache *LastLineNoContentCache;
  mutable unsigned LastLineNoFilePos;
  mutable unsigned LastLineNoResult;

  FileID MainFileID;

  // Statistics for -print-stats.
  mutable unsigned NumLinearScans = 0;
  mutable unsigned NumBinaryProbes = 0;

  mutable llvm::DenseMap<FileID, std::pair<FileID, unsigned>> IncludedLocMap;

  using IsBeforeInTUCacheKey = std::pair<FileID, FileID>;

  using InBeforeInTUCache =
      llvm::DenseMap<IsBeforeInTUCacheKey, InBeforeInTUCacheEntry>;

  mutable InBeforeInTUCache IBTUCache;
  mutable InBeforeInTUCacheEntry IBTUCacheOverflow;

  InBeforeInTUCacheEntry &getInBeforeInTUCache(FileID LFID, FileID RFID) const;

  // Cache for the "fake" buffer used for error-recovery purposes.
  mutable std::unique_ptr<llvm::MemoryBuffer> FakeBufferForRecovery;

  mutable std::unique_ptr<SrcMgr::ContentCache> FakeContentCacheForRecovery;

  mutable std::unique_ptr<SrcMgr::SLocEntry> FakeSLocEntryForRecovery;

  using MacroArgsMap = std::map<unsigned, SourceLocation>;

  mutable llvm::DenseMap<FileID, std::unique_ptr<MacroArgsMap>>
      MacroArgsCacheMap;

public:
  SourceManager(DiagnosticsEngine &Diag, FileManager &FileMgr,
                bool UserFilesAreVolatile = false);
  explicit SourceManager(const SourceManager &) = delete;
  SourceManager &operator=(const SourceManager &) = delete;
  ~SourceManager();

  void clearIDTables();

  void initializeForReplay(const SourceManager &Old);

  DiagnosticsEngine &getDiagnostics() const { return Diag; }

  FileManager &getFileManager() const { return FileMgr; }

  bool userFilesAreVolatile() const { return UserFilesAreVolatile; }

  //===--------------------------------------------------------------------===//
  // MainFileID creation and querying methods.
  //===--------------------------------------------------------------------===//

  FileID getMainFileID() const { return MainFileID; }

  void setMainFileID(FileID FID) { MainFileID = FID; }

  bool isMainFile(const FileEntry &SourceFile);

  //===--------------------------------------------------------------------===//
  // Methods to create new FileID's and macro expansions.
  //===--------------------------------------------------------------------===//

  FileID createFileID(FileEntryRef SourceFile, SourceLocation IncludePos,
                      SrcMgr::CharacteristicKind FileCharacter,
                      int LoadedID = 0,
                      SourceLocation::UIntTy LoadedOffset = 0);

  FileID createFileID(std::unique_ptr<llvm::MemoryBuffer> Buffer,
                      SrcMgr::CharacteristicKind FileCharacter = SrcMgr::C_User,
                      int LoadedID = 0, SourceLocation::UIntTy LoadedOffset = 0,
                      SourceLocation IncludeLoc = SourceLocation());

  FileID createFileID(const llvm::MemoryBufferRef &Buffer,
                      SrcMgr::CharacteristicKind FileCharacter = SrcMgr::C_User,
                      int LoadedID = 0, SourceLocation::UIntTy LoadedOffset = 0,
                      SourceLocation IncludeLoc = SourceLocation());

  FileID getOrCreateFileID(FileEntryRef SourceFile,
                           SrcMgr::CharacteristicKind FileCharacter);

  SourceLocation createMacroArgExpansionLoc(SourceLocation SpellingLoc,
                                            SourceLocation ExpansionLoc,
                                            unsigned Length);

  SourceLocation createExpansionLoc(SourceLocation SpellingLoc,
                                    SourceLocation ExpansionLocStart,
                                    SourceLocation ExpansionLocEnd,
                                    unsigned Length,
                                    bool ExpansionIsTokenRange = true,
                                    int LoadedID = 0,
                                    SourceLocation::UIntTy LoadedOffset = 0);

  SourceLocation createTokenSplitLoc(SourceLocation SpellingLoc,
                                     SourceLocation TokenStart,
                                     SourceLocation TokenEnd);

  std::optional<llvm::MemoryBufferRef>
  getMemoryBufferForFileOrNone(FileEntryRef File);

  llvm::MemoryBufferRef getMemoryBufferForFileOrFake(FileEntryRef File) {
    if (auto B = getMemoryBufferForFileOrNone(File))
      return *B;
    return getFakeBufferForRecovery();
  }

  void overrideFileContents(FileEntryRef SourceFile,
                            const llvm::MemoryBufferRef &Buffer) {
    overrideFileContents(SourceFile, llvm::MemoryBuffer::getMemBuffer(Buffer));
  }

  void overrideFileContents(FileEntryRef SourceFile,
                            std::unique_ptr<llvm::MemoryBuffer> Buffer);

  void overrideFileContents(const FileEntry *SourceFile, FileEntryRef NewFile);

  bool isFileOverridden(const FileEntry *File) const {
    if (OverriddenFilesInfo) {
      if (OverriddenFilesInfo->OverriddenFilesWithBuffer.contains(File))
        return true;
      if (OverriddenFilesInfo->OverriddenFiles.contains(File))
        return true;
    }
    return false;
  }

  OptionalFileEntryRef bypassFileContentsOverride(FileEntryRef File);

  void setFileIsTransient(FileEntryRef SourceFile);

  void setAllFilesAreTransient(bool Transient) {
    FilesAreTransient = Transient;
  }

  //===--------------------------------------------------------------------===//
  // FileID manipulation methods.
  //===--------------------------------------------------------------------===//

  std::optional<llvm::MemoryBufferRef>
  getBufferOrNone(FileID FID, SourceLocation Loc = SourceLocation()) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return Entry->getFile().getContentCache().getBufferOrNone(
          Diag, getFileManager(), Loc);
    return std::nullopt;
  }

  llvm::MemoryBufferRef
  getBufferOrFake(FileID FID, SourceLocation Loc = SourceLocation()) const {
    if (auto B = getBufferOrNone(FID, Loc))
      return *B;
    return getFakeBufferForRecovery();
  }

  const FileEntry *getFileEntryForID(FileID FID) const {
    if (auto FE = getFileEntryRefForID(FID))
      return *FE;
    return nullptr;
  }

  OptionalFileEntryRef getFileEntryRefForID(FileID FID) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return Entry->getFile().getContentCache().OrigEntry;
    return std::nullopt;
  }

  std::optional<llvm::StringRef> getNonBuiltinFilenameForID(FileID FID) const;

  const FileEntry *
  getFileEntryForSLocEntry(const SrcMgr::SLocEntry &SLocEntry) const {
    if (auto FE = SLocEntry.getFile().getContentCache().OrigEntry)
      return *FE;
    return nullptr;
  }

  llvm::StringRef getBufferData(FileID FID, bool *Invalid = nullptr) const;

  std::optional<llvm::StringRef> getBufferDataOrNone(FileID FID) const;

  std::optional<llvm::StringRef> getBufferDataIfLoaded(FileID FID) const;

  unsigned getNumCreatedFIDsForFileID(FileID FID) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return Entry->getFile().NumCreatedFIDs;
    return 0;
  }

  void setNumCreatedFIDsForFileID(FileID FID, unsigned NumFIDs,
                                  bool Force = false) const {
    auto *Entry = getSLocEntryForFile(FID);
    if (!Entry)
      return;
    assert((Force || Entry->getFile().NumCreatedFIDs == 0) && "Already set!");
    const_cast<SrcMgr::FileInfo &>(Entry->getFile()).NumCreatedFIDs = NumFIDs;
  }

  //===--------------------------------------------------------------------===//
  // SourceLocation manipulation methods.
  //===--------------------------------------------------------------------===//

  FileID getFileID(SourceLocation SpellingLoc) const {
    return getFileID(SpellingLoc.getOffset());
  }

  llvm::StringRef getFilename(SourceLocation SpellingLoc) const;

  SourceLocation getLocForStartOfFile(FileID FID) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return SourceLocation::getFileLoc(Entry->getOffset());
    return SourceLocation();
  }

  SourceLocation getLocForEndOfFile(FileID FID) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return SourceLocation::getFileLoc(Entry->getOffset() +
                                        getFileIDSize(FID));
    return SourceLocation();
  }

  SourceLocation getIncludeLoc(FileID FID) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return Entry->getFile().getIncludeLoc();
    return SourceLocation();
  }

  SourceLocation getExpansionLoc(SourceLocation Loc) const {
    // Handle the non-mapped case inline, defer to out of line code to handle
    // expansions.
    if (Loc.isFileID())
      return Loc;
    return getExpansionLocSlowCase(Loc);
  }

  SourceLocation getFileLoc(SourceLocation Loc) const {
    if (Loc.isFileID())
      return Loc;
    return getFileLocSlowCase(Loc);
  }

  CharSourceRange getImmediateExpansionRange(SourceLocation Loc) const;

  CharSourceRange getExpansionRange(SourceLocation Loc) const;

  CharSourceRange getExpansionRange(SourceRange Range) const {
    SourceLocation Begin = getExpansionRange(Range.getBegin()).getBegin();
    CharSourceRange End = getExpansionRange(Range.getEnd());
    return CharSourceRange(SourceRange(Begin, End.getEnd()),
                           End.isTokenRange());
  }

  CharSourceRange getExpansionRange(CharSourceRange Range) const {
    CharSourceRange Expansion = getExpansionRange(Range.getAsRange());
    if (Expansion.getEnd() == Range.getEnd())
      Expansion.setTokenRange(Range.isTokenRange());
    return Expansion;
  }

  SourceLocation getSpellingLoc(SourceLocation Loc) const {
    // Handle the non-mapped case inline, defer to out of line code to handle
    // expansions.
    if (Loc.isFileID())
      return Loc;
    return getSpellingLocSlowCase(Loc);
  }

  SourceLocation getImmediateSpellingLoc(SourceLocation Loc) const;

  SourceLocation getComposedLoc(FileID FID, unsigned Offset) const {
    auto *Entry = getSLocEntryOrNull(FID);
    if (!Entry)
      return SourceLocation();

    SourceLocation::UIntTy GlobalOffset = Entry->getOffset() + Offset;
    return Entry->isFile() ? SourceLocation::getFileLoc(GlobalOffset)
                           : SourceLocation::getMacroLoc(GlobalOffset);
  }

  std::pair<FileID, unsigned> getDecomposedLoc(SourceLocation Loc) const {
    FileID FID = getFileID(Loc);
    auto *Entry = getSLocEntryOrNull(FID);
    if (!Entry)
      return std::make_pair(FileID(), 0);
    return std::make_pair(FID, Loc.getOffset() - Entry->getOffset());
  }

  std::pair<FileID, unsigned>
  getDecomposedExpansionLoc(SourceLocation Loc) const {
    FileID FID = getFileID(Loc);
    auto *E = getSLocEntryOrNull(FID);
    if (!E)
      return std::make_pair(FileID(), 0);

    unsigned Offset = Loc.getOffset() - E->getOffset();
    if (Loc.isFileID())
      return std::make_pair(FID, Offset);

    return getDecomposedExpansionLocSlowCase(E);
  }

  std::pair<FileID, unsigned>
  getDecomposedSpellingLoc(SourceLocation Loc) const {
    FileID FID = getFileID(Loc);
    auto *E = getSLocEntryOrNull(FID);
    if (!E)
      return std::make_pair(FileID(), 0);

    unsigned Offset = Loc.getOffset() - E->getOffset();
    if (Loc.isFileID())
      return std::make_pair(FID, Offset);
    return getDecomposedSpellingLocSlowCase(E, Offset);
  }

  std::pair<FileID, unsigned> getDecomposedIncludedLoc(FileID FID) const;

  unsigned getFileOffset(SourceLocation SpellingLoc) const {
    return getDecomposedLoc(SpellingLoc).second;
  }

  bool isMacroArgExpansion(SourceLocation Loc,
                           SourceLocation *StartLoc = nullptr) const;

  bool isMacroBodyExpansion(SourceLocation Loc) const;

  bool isAtStartOfImmediateMacroExpansion(
      SourceLocation Loc, SourceLocation *MacroBegin = nullptr) const;

  bool
  isAtEndOfImmediateMacroExpansion(SourceLocation Loc,
                                   SourceLocation *MacroEnd = nullptr) const;

  bool
  isInSLocAddrSpace(SourceLocation Loc, SourceLocation Start, unsigned Length,
                    SourceLocation::UIntTy *RelativeOffset = nullptr) const {
    assert(((Start.getOffset() < NextLocalOffset &&
             Start.getOffset() + Length <= NextLocalOffset) ||
            (Start.getOffset() >= CurrentLoadedOffset &&
             Start.getOffset() + Length < MaxLoadedOffset)) &&
           "Chunk is not valid SLoc address space");
    SourceLocation::UIntTy LocOffs = Loc.getOffset();
    SourceLocation::UIntTy BeginOffs = Start.getOffset();
    SourceLocation::UIntTy EndOffs = BeginOffs + Length;
    if (LocOffs >= BeginOffs && LocOffs < EndOffs) {
      if (RelativeOffset)
        *RelativeOffset = LocOffs - BeginOffs;
      return true;
    }

    return false;
  }

  bool isInSameSLocAddrSpace(SourceLocation LHS, SourceLocation RHS,
                             SourceLocation::IntTy *RelativeOffset) const {
    SourceLocation::UIntTy LHSOffs = LHS.getOffset(), RHSOffs = RHS.getOffset();
    bool LHSLoaded = LHSOffs >= CurrentLoadedOffset;
    bool RHSLoaded = RHSOffs >= CurrentLoadedOffset;

    if (LHSLoaded == RHSLoaded) {
      if (RelativeOffset)
        *RelativeOffset = RHSOffs - LHSOffs;
      return true;
    }

    return false;
  }

  //===--------------------------------------------------------------------===//
  // Queries about the code at a SourceLocation.
  //===--------------------------------------------------------------------===//

  const char *getCharacterData(SourceLocation SL,
                               bool *Invalid = nullptr) const;

  unsigned getColumnNumber(FileID FID, unsigned FilePos,
                           bool *Invalid = nullptr) const;
  unsigned getSpellingColumnNumber(SourceLocation Loc,
                                   bool *Invalid = nullptr) const;
  unsigned getExpansionColumnNumber(SourceLocation Loc,
                                    bool *Invalid = nullptr) const;
  unsigned getPresumedColumnNumber(SourceLocation Loc,
                                   bool *Invalid = nullptr) const;

  unsigned getLineNumber(FileID FID, unsigned FilePos,
                         bool *Invalid = nullptr) const;
  unsigned getSpellingLineNumber(SourceLocation Loc,
                                 bool *Invalid = nullptr) const;
  unsigned getExpansionLineNumber(SourceLocation Loc,
                                  bool *Invalid = nullptr) const;
  unsigned getPresumedLineNumber(SourceLocation Loc,
                                 bool *Invalid = nullptr) const;

  llvm::StringRef getBufferName(SourceLocation Loc,
                                bool *Invalid = nullptr) const;

  SrcMgr::CharacteristicKind getFileCharacteristic(SourceLocation Loc) const;

  PresumedLoc getPresumedLoc(SourceLocation Loc,
                             bool UseLineDirectives = true) const;

  bool isInMainFile(SourceLocation Loc) const;

  bool isWrittenInSameFile(SourceLocation Loc1, SourceLocation Loc2) const {
    return getFileID(Loc1) == getFileID(Loc2);
  }

  bool isWrittenInMainFile(SourceLocation Loc) const {
    return getFileID(Loc) == getMainFileID();
  }

  bool isWrittenInBuiltinFile(SourceLocation Loc) const {
    PresumedLoc Presumed = getPresumedLoc(Loc);
    if (Presumed.isInvalid())
      return false;
    llvm::StringRef Filename(Presumed.getFilename());
    return Filename.equals("<built-in>");
  }

  bool isWrittenInCommandLineFile(SourceLocation Loc) const {
    PresumedLoc Presumed = getPresumedLoc(Loc);
    if (Presumed.isInvalid())
      return false;
    llvm::StringRef Filename(Presumed.getFilename());
    return Filename.equals("<command line>");
  }

  bool isWrittenInScratchSpace(SourceLocation Loc) const {
    PresumedLoc Presumed = getPresumedLoc(Loc);
    if (Presumed.isInvalid())
      return false;
    llvm::StringRef Filename(Presumed.getFilename());
    return Filename.equals("<scratch space>");
  }

  bool isInSystemHeader(SourceLocation Loc) const {
    if (Loc.isInvalid())
      return false;
    return isSystem(getFileCharacteristic(Loc));
  }

  bool isInExternCSystemHeader(SourceLocation Loc) const {
    return getFileCharacteristic(Loc) == SrcMgr::C_ExternCSystem;
  }

  bool isInSystemMacro(SourceLocation loc) const {
    if (!loc.isMacroID())
      return false;

    // This happens when the macro is the result of a paste, in that case
    // its spelling is the scratch memory, so we take the parent context.
    // There can be several level of token pasting.
    if (isWrittenInScratchSpace(getSpellingLoc(loc))) {
      do {
        loc = getImmediateMacroCallerLoc(loc);
      } while (isWrittenInScratchSpace(getSpellingLoc(loc)));
      return isInSystemMacro(loc);
    }

    return isInSystemHeader(getSpellingLoc(loc));
  }

  unsigned getFileIDSize(FileID FID) const;

  bool isInFileID(SourceLocation Loc, FileID FID,
                  unsigned *RelativeOffset = nullptr) const {
    SourceLocation::UIntTy Offs = Loc.getOffset();
    if (isOffsetInFileID(FID, Offs)) {
      if (RelativeOffset)
        *RelativeOffset = Offs - getSLocEntry(FID).getOffset();
      return true;
    }

    return false;
  }

  //===--------------------------------------------------------------------===//
  // Line Table Manipulation Routines
  //===--------------------------------------------------------------------===//

  unsigned getLineTableFilenameID(llvm::StringRef Str);

  void AddLineNote(SourceLocation Loc, unsigned LineNo, int FilenameID,
                   bool IsFileEntry, bool IsFileExit,
                   SrcMgr::CharacteristicKind FileKind);

  bool hasLineTable() const { return LineTable != nullptr; }

  LineTableInfo &getLineTable();

  //===--------------------------------------------------------------------===//
  // Queries for performance analysis.
  //===--------------------------------------------------------------------===//

  size_t getContentCacheSize() const {
    return ContentCacheAlloc.getTotalMemory();
  }

  struct MemoryBufferSizes {
    const size_t malloc_bytes;
    const size_t mmap_bytes;

    MemoryBufferSizes(size_t malloc_bytes, size_t mmap_bytes)
        : malloc_bytes(malloc_bytes), mmap_bytes(mmap_bytes) {}
  };

  MemoryBufferSizes getMemoryBufferSizes() const;

  size_t getDataStructureSizes() const;

  //===--------------------------------------------------------------------===//
  // Other miscellaneous methods.
  //===--------------------------------------------------------------------===//

  SourceLocation translateFileLineCol(const FileEntry *SourceFile,
                                      unsigned Line, unsigned Col) const;

  FileID translateFile(const FileEntry *SourceFile) const;
  FileID translateFile(FileEntryRef SourceFile) const {
    return translateFile(&SourceFile.getFileEntry());
  }

  SourceLocation translateLineCol(FileID FID, unsigned Line,
                                  unsigned Col) const;

  SourceLocation getMacroArgExpandedLocation(SourceLocation Loc) const;

  bool isBeforeInTranslationUnit(SourceLocation LHS, SourceLocation RHS) const;

  std::pair<bool, bool>
  isInTheSameTranslationUnit(std::pair<FileID, unsigned> &LOffs,
                             std::pair<FileID, unsigned> &ROffs) const;

  bool isInTheSameTranslationUnitImpl(
      const std::pair<FileID, unsigned> &LOffs,
      const std::pair<FileID, unsigned> &ROffs) const;

  bool isBeforeInSLocAddrSpace(SourceLocation LHS, SourceLocation RHS) const {
    return isBeforeInSLocAddrSpace(LHS, RHS.getOffset());
  }

  bool isBeforeInSLocAddrSpace(SourceLocation LHS,
                               SourceLocation::UIntTy RHS) const {
    SourceLocation::UIntTy LHSOffset = LHS.getOffset();
    bool LHSLoaded = LHSOffset >= CurrentLoadedOffset;
    bool RHSLoaded = RHS >= CurrentLoadedOffset;
    if (LHSLoaded == RHSLoaded)
      return LHSOffset < RHS;

    return LHSLoaded;
  }

  bool isPointWithin(SourceLocation Location, SourceLocation Start,
                     SourceLocation End) const {
    return Location == Start || Location == End ||
           (isBeforeInTranslationUnit(Start, Location) &&
            isBeforeInTranslationUnit(Location, End));
  }

  // Iterators over FileInfos.
  using fileinfo_iterator =
      llvm::DenseMap<FileEntryRef, SrcMgr::ContentCache *>::const_iterator;

  fileinfo_iterator fileinfo_begin() const { return FileInfos.begin(); }
  fileinfo_iterator fileinfo_end() const { return FileInfos.end(); }
  bool hasFileInfo(const FileEntry *File) const {
    return FileInfos.find_as(File) != FileInfos.end();
  }

  void PrintStats() const;

  void dump() const;

  // Produce notes describing the current source location address space usage.
  void noteSLocAddressSpaceUsage(DiagnosticsEngine &Diag,
                                 std::optional<unsigned> MaxNotes = 32) const;

  unsigned local_sloc_entry_size() const { return LocalSLocEntryTable.size(); }

  const SrcMgr::SLocEntry &getLocalSLocEntry(unsigned Index) const {
    assert(Index < LocalSLocEntryTable.size() && "Invalid index");
    return LocalSLocEntryTable[Index];
  }

  unsigned loaded_sloc_entry_size() const {
    return LoadedSLocEntryTable.size();
  }

  const SrcMgr::SLocEntry &getLoadedSLocEntry(unsigned Index,
                                              bool *Invalid = nullptr) const {
    assert(Index < LoadedSLocEntryTable.size() && "Invalid index");
    if (SLocEntryLoaded[Index])
      return LoadedSLocEntryTable[Index];
    return loadSLocEntry(Index, Invalid);
  }

  const SrcMgr::SLocEntry &getSLocEntry(FileID FID,
                                        bool *Invalid = nullptr) const {
    if (FID.ID == 0 || FID.ID == -1) {
      if (Invalid)
        *Invalid = true;
      return LocalSLocEntryTable[0];
    }
    return getSLocEntryByID(FID.ID, Invalid);
  }

  SourceLocation::UIntTy getNextLocalOffset() const { return NextLocalOffset; }

  void setExternalSLocEntrySource(ExternalSLocEntrySource *Source) {
    assert(LoadedSLocEntryTable.empty() &&
           "Invalidating existing loaded entries");
    ExternalSLocEntries = Source;
  }

  std::pair<int, SourceLocation::UIntTy>
  AllocateLoadedSLocEntries(unsigned NumSLocEntries,
                            SourceLocation::UIntTy TotalSize);

  bool isLoadedSourceLocation(SourceLocation Loc) const {
    return isLoadedOffset(Loc.getOffset());
  }

  bool isLocalSourceLocation(SourceLocation Loc) const {
    return isLocalOffset(Loc.getOffset());
  }

  bool isLoadedFileID(FileID FID) const {
    assert(FID.ID != -1 && "Using FileID sentinel value");
    return FID.ID < 0;
  }

  bool isLocalFileID(FileID FID) const { return !isLoadedFileID(FID); }

  SourceLocation getImmediateMacroCallerLoc(SourceLocation Loc) const {
    if (!Loc.isMacroID())
      return Loc;

    // When we have the location of (part of) an expanded parameter, its
    // spelling location points to the argument as expanded in the macro call,
    // and therefore is used to locate the macro caller.
    if (isMacroArgExpansion(Loc))
      return getImmediateSpellingLoc(Loc);

    // Otherwise, the caller of the macro is located where this macro is
    // expanded (while the spelling is part of the macro definition).
    return getImmediateExpansionRange(Loc).getBegin();
  }

  SourceLocation getTopMacroCallerLoc(SourceLocation Loc) const;

private:
  llvm::MemoryBufferRef getFakeBufferForRecovery() const;
  SrcMgr::ContentCache &getFakeContentCacheForRecovery() const;

  const SrcMgr::SLocEntry &loadSLocEntry(unsigned Index, bool *Invalid) const;

  const SrcMgr::SLocEntry *getSLocEntryOrNull(FileID FID) const {
    bool Invalid = false;
    const SrcMgr::SLocEntry &Entry = getSLocEntry(FID, &Invalid);
    return Invalid ? nullptr : &Entry;
  }

  const SrcMgr::SLocEntry *getSLocEntryForFile(FileID FID) const {
    if (auto *Entry = getSLocEntryOrNull(FID))
      if (Entry->isFile())
        return Entry;
    return nullptr;
  }

  const SrcMgr::SLocEntry &getSLocEntryByID(int ID,
                                            bool *Invalid = nullptr) const {
    assert(ID != -1 && "Using FileID sentinel value");
    if (ID < 0)
      return getLoadedSLocEntryByID(ID, Invalid);
    return getLocalSLocEntry(static_cast<unsigned>(ID));
  }

  const SrcMgr::SLocEntry &
  getLoadedSLocEntryByID(int ID, bool *Invalid = nullptr) const {
    return getLoadedSLocEntry(static_cast<unsigned>(-ID - 2), Invalid);
  }

  FileID getFileID(SourceLocation::UIntTy SLocOffset) const {
    // If our one-entry cache covers this offset, just return it.
    if (isOffsetInFileID(LastFileIDLookup, SLocOffset))
      return LastFileIDLookup;

    return getFileIDSlow(SLocOffset);
  }

  bool isLocalOffset(SourceLocation::UIntTy SLocOffset) const {
    return SLocOffset < CurrentLoadedOffset;
  }

  bool isLoadedOffset(SourceLocation::UIntTy SLocOffset) const {
    return SLocOffset >= CurrentLoadedOffset;
  }

  SourceLocation
  createExpansionLocImpl(const SrcMgr::ExpansionInfo &Expansion,
                         unsigned Length, int LoadedID = 0,
                         SourceLocation::UIntTy LoadedOffset = 0);

  inline bool isOffsetInFileID(FileID FID,
                               SourceLocation::UIntTy SLocOffset) const {
    const SrcMgr::SLocEntry &Entry = getSLocEntry(FID);
    // If the entry is after the offset, it can't contain it.
    if (SLocOffset < Entry.getOffset())
      return false;

    // If this is the very last entry then it does.
    if (FID.ID == -2)
      return true;

    // If it is the last local entry, then it does if the location is local.
    if (FID.ID + 1 == static_cast<int>(LocalSLocEntryTable.size()))
      return SLocOffset < NextLocalOffset;

    // Otherwise, the entry after it has to not include it. This works for both
    // local and loaded entries.
    return SLocOffset < getSLocEntryByID(FID.ID + 1).getOffset();
  }

  FileID getPreviousFileID(FileID FID) const;

  FileID getNextFileID(FileID FID) const;

  FileID createFileIDImpl(SrcMgr::ContentCache &File, llvm::StringRef Filename,
                          SourceLocation IncludePos,
                          SrcMgr::CharacteristicKind DirCharacter, int LoadedID,
                          SourceLocation::UIntTy LoadedOffset);

  SrcMgr::ContentCache &getOrCreateContentCache(FileEntryRef SourceFile,
                                                bool isSystemFile = false);

  SrcMgr::ContentCache &
  createMemBufferContentCache(std::unique_ptr<llvm::MemoryBuffer> Buf);

  FileID getFileIDSlow(SourceLocation::UIntTy SLocOffset) const;
  FileID getFileIDLocal(SourceLocation::UIntTy SLocOffset) const;
  FileID getFileIDLoaded(SourceLocation::UIntTy SLocOffset) const;

  SourceLocation getExpansionLocSlowCase(SourceLocation Loc) const;
  SourceLocation getSpellingLocSlowCase(SourceLocation Loc) const;
  SourceLocation getFileLocSlowCase(SourceLocation Loc) const;

  std::pair<FileID, unsigned>
  getDecomposedExpansionLocSlowCase(const SrcMgr::SLocEntry *E) const;
  std::pair<FileID, unsigned>
  getDecomposedSpellingLocSlowCase(const SrcMgr::SLocEntry *E,
                                   unsigned Offset) const;
  void computeMacroArgsCache(MacroArgsMap &MacroArgsCache, FileID FID) const;
  void associateFileChunkWithMacroArgExp(MacroArgsMap &MacroArgsCache,
                                         FileID FID, SourceLocation SpellLoc,
                                         SourceLocation ExpansionLoc,
                                         unsigned ExpansionLength) const;
};

template <typename T> class BeforeThanCompare;

template <> class BeforeThanCompare<SourceLocation> {
  SourceManager &SM;

public:
  explicit BeforeThanCompare(SourceManager &SM) : SM(SM) {}

  bool operator()(SourceLocation LHS, SourceLocation RHS) const {
    return SM.isBeforeInTranslationUnit(LHS, RHS);
  }
};

template <> class BeforeThanCompare<SourceRange> {
  SourceManager &SM;

public:
  explicit BeforeThanCompare(SourceManager &SM) : SM(SM) {}

  bool operator()(SourceRange LHS, SourceRange RHS) const {
    return SM.isBeforeInTranslationUnit(LHS.getBegin(), RHS.getBegin());
  }
};

class SourceManagerForFile {
public:
  SourceManagerForFile(llvm::StringRef FileName, llvm::StringRef Content);

  SourceManager &get() {
    assert(SourceMgr);
    return *SourceMgr;
  }

private:
  // The order of these fields are important - they should be in the same order
  // as they are created in `createSourceManagerForFile` so that they can be
  // deleted in the reverse order as they are created.
  std::unique_ptr<FileManager> FileMgr;
  std::unique_ptr<DiagnosticsEngine> Diagnostics;
  std::unique_ptr<SourceManager> SourceMgr;
};

} // namespace neverc

#endif // NEVERC_FOUNDATION_SOURCEMANAGER_H
