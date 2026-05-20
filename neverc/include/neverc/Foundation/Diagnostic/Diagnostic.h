#ifndef NEVERC_FOUNDATION_DIAGNOSTIC_H
#define NEVERC_FOUNDATION_DIAGNOSTIC_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Foundation/Diagnostic/DiagnosticIDs.h"
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace llvm {
class Error;
class raw_ostream;
} // namespace llvm

namespace neverc {

class DeclContext;
class DiagnosticBuilder;
class DiagnosticConsumer;
class IdentifierInfo;
class LangOptions;
class PrepEngine;
class SourceManager;
class StoredDiagnostic;

namespace tok {

enum TokenKind : unsigned short;

} // namespace tok

class FixItHint {
public:
  CharSourceRange RemoveRange;

  CharSourceRange InsertFromRange;

  std::string CodeToInsert;

  bool BeforePreviousInsertions = false;

  FixItHint() = default;

  bool isNull() const { return !RemoveRange.isValid(); }

  static FixItHint CreateInsertion(SourceLocation InsertionLoc,
                                   llvm::StringRef Code,
                                   bool BeforePreviousInsertions = false) {
    FixItHint Hint;
    Hint.RemoveRange =
        CharSourceRange::getCharRange(InsertionLoc, InsertionLoc);
    Hint.CodeToInsert = std::string(Code);
    Hint.BeforePreviousInsertions = BeforePreviousInsertions;
    return Hint;
  }

  static FixItHint
  CreateInsertionFromRange(SourceLocation InsertionLoc,
                           CharSourceRange FromRange,
                           bool BeforePreviousInsertions = false) {
    FixItHint Hint;
    Hint.RemoveRange =
        CharSourceRange::getCharRange(InsertionLoc, InsertionLoc);
    Hint.InsertFromRange = FromRange;
    Hint.BeforePreviousInsertions = BeforePreviousInsertions;
    return Hint;
  }

  static FixItHint CreateRemoval(CharSourceRange RemoveRange) {
    FixItHint Hint;
    Hint.RemoveRange = RemoveRange;
    return Hint;
  }
  static FixItHint CreateRemoval(SourceRange RemoveRange) {
    return CreateRemoval(CharSourceRange::getTokenRange(RemoveRange));
  }

  static FixItHint CreateReplacement(CharSourceRange RemoveRange,
                                     llvm::StringRef Code) {
    FixItHint Hint;
    Hint.RemoveRange = RemoveRange;
    Hint.CodeToInsert = std::string(Code);
    return Hint;
  }

  static FixItHint CreateReplacement(SourceRange RemoveRange,
                                     llvm::StringRef Code) {
    return CreateReplacement(CharSourceRange::getTokenRange(RemoveRange), Code);
  }
};

struct DiagnosticStorage {
  enum {
    /// The maximum number of arguments we can hold. We
    /// currently only support up to 10 arguments (%0-%9).
    ///
    /// A single diagnostic with more than that almost certainly has to
    /// be simplified anyway.
    MaxArguments = 10
  };

  unsigned char NumDiagArgs = 0;

  unsigned char DiagArgumentsKind[MaxArguments];

  uint64_t DiagArgumentsVal[MaxArguments];

  std::string DiagArgumentsStr[MaxArguments];

  llvm::SmallVector<CharSourceRange, 8> DiagRanges;

  llvm::SmallVector<FixItHint, 6> FixItHints;

  DiagnosticStorage() = default;
};

class DiagnosticsEngine : public llvm::RefCountedBase<DiagnosticsEngine> {
public:
  enum Level {
    Ignored = DiagnosticIDs::Ignored,
    Note = DiagnosticIDs::Note,
    Remark = DiagnosticIDs::Remark,
    Warning = DiagnosticIDs::Warning,
    Error = DiagnosticIDs::Error,
    Fatal = DiagnosticIDs::Fatal
  };

  enum ArgumentKind {
    /// std::string
    ak_std_string,

    /// const char *
    ak_c_string,

    /// int
    ak_sint,

    /// unsigned
    ak_uint,

    /// enum TokenKind : unsigned
    ak_tokenkind,

    /// IdentifierInfo
    ak_identifierinfo,

    /// address space
    ak_addrspace,

    /// Qualifiers
    ak_qual,

    /// QualType
    ak_qualtype,

    /// DeclarationName
    ak_declarationname,

    /// NamedDecl *
    ak_nameddecl,

    /// DeclContext *
    ak_declcontext,

    /// pair<QualType, QualType>
    ak_qualtype_pair,

    /// Attr *
    ak_attr
  };

  using ArgumentValue = std::pair<ArgumentKind, intptr_t>;

public:
  // Used by __extension__
  unsigned char AllExtensionsSilenced = 0;

  // Treat fatal errors like errors.
  bool FatalsAsError = false;

  // Suppress all diagnostics.
  bool SuppressAllDiagnostics = false;

  // Color printing is enabled.
  bool ShowColors = false;

  // Treat all warnings as errors.
  bool TreatWarningsAsErrors = false;

  // Cap of # errors emitted, 0 -> no limit.
  unsigned ErrorLimit = 0;

  // Cap on depth of constexpr evaluation backtrace stack, 0 -> no limit.
  unsigned ConstexprBacktraceLimit = 0;

  llvm::IntrusiveRefCntPtr<DiagnosticIDs> Diags;
  llvm::IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts;
  DiagnosticConsumer *Client = nullptr;
  std::unique_ptr<DiagnosticConsumer> Owner;
  SourceManager *SourceMgr = nullptr;

  class DiagState {
    llvm::DenseMap<unsigned, DiagnosticMapping> DiagMap;

  public:
    // "Global" configuration state that can actually vary between modules.

    // Ignore all warnings: -w
    LLVM_PREFERRED_TYPE(bool)
    unsigned IgnoreAllWarnings : 1;

    // Enable all warnings.
    LLVM_PREFERRED_TYPE(bool)
    unsigned EnableAllWarnings : 1;

    // Treat warnings like errors.
    LLVM_PREFERRED_TYPE(bool)
    unsigned WarningsAsErrors : 1;

    // Treat errors like fatal errors.
    LLVM_PREFERRED_TYPE(bool)
    unsigned ErrorsAsFatal : 1;

    // Suppress warnings in system headers.
    LLVM_PREFERRED_TYPE(bool)
    unsigned SuppressSystemWarnings : 1;

    // Map extensions to warnings or errors?
    diag::Severity ExtBehavior = diag::Severity::Ignored;

    DiagState()
        : IgnoreAllWarnings(false), EnableAllWarnings(false),
          WarningsAsErrors(false), ErrorsAsFatal(false),
          SuppressSystemWarnings(false) {}

    using iterator = llvm::DenseMap<unsigned, DiagnosticMapping>::iterator;
    using const_iterator =
        llvm::DenseMap<unsigned, DiagnosticMapping>::const_iterator;

    void setMapping(diag::kind Diag, DiagnosticMapping Info) {
      DiagMap[Diag] = Info;
    }

    DiagnosticMapping lookupMapping(diag::kind Diag) const {
      return DiagMap.lookup(Diag);
    }

    DiagnosticMapping &getOrAddMapping(diag::kind Diag);

    const_iterator begin() const { return DiagMap.begin(); }
    const_iterator end() const { return DiagMap.end(); }
  };

  std::list<DiagState> DiagStates;

  class DiagStateMap {
  public:
    /// Add an initial diagnostic state.
    void appendFirst(DiagState *State);

    /// Add a new latest state point.
    void append(SourceManager &SrcMgr, SourceLocation Loc, DiagState *State);

    /// Look up the diagnostic state at a given source location.
    DiagState *lookup(SourceManager &SrcMgr, SourceLocation Loc) const;

    /// Determine whether this map is empty.
    bool empty() const { return Files.empty(); }

    /// Clear out this map.
    void clear() {
      Files.clear();
      FirstDiagState = CurDiagState = nullptr;
      CurDiagStateLoc = SourceLocation();
    }

    /// Produce a debugging dump of the diagnostic state.
    LLVM_DUMP_METHOD void
    dump(SourceManager &SrcMgr,
         llvm::StringRef DiagName = llvm::StringRef()) const;

    /// Grab the most-recently-added state point.
    DiagState *getCurDiagState() const { return CurDiagState; }

    /// Get the location at which a diagnostic state was last added.
    SourceLocation getCurDiagStateLoc() const { return CurDiagStateLoc; }

  private:
    /// Represents a point in source where the diagnostic state was
    /// modified because of a pragma.
    ///
    /// 'Loc' can be null if the point represents the diagnostic state
    /// modifications done through the command-line.
    struct DiagStatePoint {
      DiagState *State;
      unsigned Offset;

      DiagStatePoint(DiagState *State, unsigned Offset)
          : State(State), Offset(Offset) {}
    };

    /// Description of the diagnostic states and state transitions for a
    /// particular FileID.
    struct File {
      /// The diagnostic state for the parent file. This is strictly redundant,
      /// as looking up the DecomposedIncludedLoc for the FileID in the Files
      /// map would give us this, but we cache it here for performance.
      File *Parent = nullptr;

      /// The offset of this file within its parent.
      unsigned ParentOffset = 0;

      /// Whether this file has any local (not imported from an AST file)
      /// diagnostic state transitions.
      bool HasLocalTransitions = false;

      /// The points within the file where the state changes. There will always
      /// be at least one of these (the state on entry to the file).
      llvm::SmallVector<DiagStatePoint, 4> StateTransitions;

      DiagState *lookup(unsigned Offset) const;
    };

    /// The diagnostic states for each file.
    mutable std::map<FileID, File> Files;

    /// The initial diagnostic state.
    DiagState *FirstDiagState;

    /// The current diagnostic state.
    DiagState *CurDiagState;

    /// The location at which the current diagnostic state was established.
    SourceLocation CurDiagStateLoc;

    /// Get the diagnostic state information for a file.
    File *getFile(SourceManager &SrcMgr, FileID ID) const;
  };

  DiagStateMap DiagStatesByLoc;

  std::vector<DiagState *> DiagStateOnPushStack;

  DiagState *GetCurDiagState() const {
    return DiagStatesByLoc.getCurDiagState();
  }

  void PushDiagStatePoint(DiagState *State, SourceLocation L);

  DiagState *GetDiagStateForLoc(SourceLocation Loc) const {
    return SourceMgr ? DiagStatesByLoc.lookup(*SourceMgr, Loc)
                     : DiagStatesByLoc.getCurDiagState();
  }

  bool ErrorOccurred;

  bool UncompilableErrorOccurred;

  bool FatalErrorOccurred;

  bool UnrecoverableErrorOccurred;

  unsigned TrapNumErrorsOccurred;
  unsigned TrapNumUnrecoverableErrorsOccurred;

  DiagnosticIDs::Level LastDiagLevel;

  unsigned NumWarnings;

  unsigned NumErrors;

  using ArgToStringFnTy = void (*)(ArgumentKind Kind, intptr_t Val,
                                   llvm::StringRef Modifier,
                                   llvm::StringRef Argument,
                                   llvm::ArrayRef<ArgumentValue> PrevArgs,
                                   llvm::SmallVectorImpl<char> &Output,
                                   void *Cookie,
                                   llvm::ArrayRef<intptr_t> QualTypeVals);

  void *ArgToStringCookie = nullptr;
  ArgToStringFnTy ArgToStringFn;

  unsigned DelayedDiagID;

  std::string DelayedDiagArg1;

  std::string DelayedDiagArg2;

  std::string DelayedDiagArg3;

  std::string FlagValue;

public:
  explicit DiagnosticsEngine(
      llvm::IntrusiveRefCntPtr<DiagnosticIDs> Diags,
      llvm::IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts,
      DiagnosticConsumer *client = nullptr, bool ShouldOwnClient = true);
  DiagnosticsEngine(const DiagnosticsEngine &) = delete;
  DiagnosticsEngine &operator=(const DiagnosticsEngine &) = delete;
  ~DiagnosticsEngine();

  friend void DiagnosticsTestHelper(DiagnosticsEngine &);
  LLVM_DUMP_METHOD void dump() const;
  LLVM_DUMP_METHOD void dump(llvm::StringRef DiagName) const;

  const llvm::IntrusiveRefCntPtr<DiagnosticIDs> &getDiagnosticIDs() const {
    return Diags;
  }

  DiagnosticOptions &getDiagnosticOptions() const { return *DiagOpts; }

  using diag_mapping_range = llvm::iterator_range<DiagState::const_iterator>;

  diag_mapping_range getDiagnosticMappings() const {
    const DiagState &DS = *GetCurDiagState();
    return diag_mapping_range(DS.begin(), DS.end());
  }

  DiagnosticConsumer *getClient() { return Client; }
  const DiagnosticConsumer *getClient() const { return Client; }

  bool ownsClient() const { return Owner != nullptr; }

  std::unique_ptr<DiagnosticConsumer> takeClient() { return std::move(Owner); }

  bool hasSourceManager() const { return SourceMgr != nullptr; }

  SourceManager &getSourceManager() const {
    assert(SourceMgr && "SourceManager not set!");
    return *SourceMgr;
  }

  void setSourceManager(SourceManager *SrcMgr) {
    assert(DiagStatesByLoc.empty() &&
           "Leftover diag state from a different SourceManager.");
    SourceMgr = SrcMgr;
  }

  //===--------------------------------------------------------------------===//
  //  DiagnosticsEngine characterization methods, used by a client to customize
  //  how diagnostics are emitted.
  //

  void pushMappings(SourceLocation Loc);

  bool popMappings(SourceLocation Loc);

  void setClient(DiagnosticConsumer *client, bool ShouldOwnClient = true);

  void setErrorLimit(unsigned Limit) { ErrorLimit = Limit; }

  void setConstexprBacktraceLimit(unsigned Limit) {
    ConstexprBacktraceLimit = Limit;
  }

  unsigned getConstexprBacktraceLimit() const {
    return ConstexprBacktraceLimit;
  }

  void setIgnoreAllWarnings(bool Val) {
    GetCurDiagState()->IgnoreAllWarnings = Val;
  }
  bool getIgnoreAllWarnings() const {
    return GetCurDiagState()->IgnoreAllWarnings;
  }

  void setEnableAllWarnings(bool Val) {
    GetCurDiagState()->EnableAllWarnings = Val;
  }
  bool getEnableAllWarnings() const {
    return GetCurDiagState()->EnableAllWarnings;
  }

  void setWarningsAsErrors(bool Val) {
    GetCurDiagState()->WarningsAsErrors = Val;
  }
  bool getWarningsAsErrors() const {
    return GetCurDiagState()->WarningsAsErrors;
  }

  void setErrorsAsFatal(bool Val) { GetCurDiagState()->ErrorsAsFatal = Val; }
  bool getErrorsAsFatal() const { return GetCurDiagState()->ErrorsAsFatal; }

  void setFatalsAsError(bool Val) { FatalsAsError = Val; }
  bool getFatalsAsError() const { return FatalsAsError; }

  void setSuppressSystemWarnings(bool Val) {
    GetCurDiagState()->SuppressSystemWarnings = Val;
  }
  bool getSuppressSystemWarnings() const {
    return GetCurDiagState()->SuppressSystemWarnings;
  }

  void setSuppressAllDiagnostics(bool Val) { SuppressAllDiagnostics = Val; }
  bool getSuppressAllDiagnostics() const { return SuppressAllDiagnostics; }

  void setShowColors(bool Val) { ShowColors = Val; }
  bool getShowColors() { return ShowColors; }

  void setLastDiagnosticIgnored(bool Ignored) {
    if (LastDiagLevel == DiagnosticIDs::Fatal)
      FatalErrorOccurred = true;
    LastDiagLevel = Ignored ? DiagnosticIDs::Ignored : DiagnosticIDs::Warning;
  }

  bool isLastDiagnosticIgnored() const {
    return LastDiagLevel == DiagnosticIDs::Ignored;
  }

  void setExtensionHandlingBehavior(diag::Severity H) {
    GetCurDiagState()->ExtBehavior = H;
  }
  diag::Severity getExtensionHandlingBehavior() const {
    return GetCurDiagState()->ExtBehavior;
  }

  void IncrementAllExtensionsSilenced() { ++AllExtensionsSilenced; }
  void DecrementAllExtensionsSilenced() { --AllExtensionsSilenced; }
  bool hasAllExtensionsSilenced() { return AllExtensionsSilenced != 0; }

  void setSeverity(diag::kind Diag, diag::Severity Map, SourceLocation Loc);

  bool setSeverityForGroup(diag::Flavor Flavor, llvm::StringRef Group,
                           diag::Severity Map,
                           SourceLocation Loc = SourceLocation());
  bool setSeverityForGroup(diag::Flavor Flavor, diag::Group Group,
                           diag::Severity Map,
                           SourceLocation Loc = SourceLocation());

  bool setDiagnosticGroupWarningAsError(llvm::StringRef Group, bool Enabled);

  bool setDiagnosticGroupErrorAsFatal(llvm::StringRef Group, bool Enabled);

  void setSeverityForAll(diag::Flavor Flavor, diag::Severity Map,
                         SourceLocation Loc = SourceLocation());

  bool hasErrorOccurred() const { return ErrorOccurred; }

  bool hasUncompilableErrorOccurred() const {
    return UncompilableErrorOccurred;
  }
  bool hasFatalErrorOccurred() const { return FatalErrorOccurred; }

  bool hasUnrecoverableErrorOccurred() const {
    return FatalErrorOccurred || UnrecoverableErrorOccurred;
  }

  unsigned getNumErrors() const { return NumErrors; }
  unsigned getNumWarnings() const { return NumWarnings; }

  void setNumWarnings(unsigned NumWarnings) { this->NumWarnings = NumWarnings; }

  template <unsigned N>
  unsigned getCustomDiagID(Level L, const char (&FormatString)[N]) {
    return Diags->getCustomDiagID((DiagnosticIDs::Level)L,
                                  llvm::StringRef(FormatString, N - 1));
  }

  void ConvertArgToString(ArgumentKind Kind, intptr_t Val,
                          llvm::StringRef Modifier, llvm::StringRef Argument,
                          llvm::ArrayRef<ArgumentValue> PrevArgs,
                          llvm::SmallVectorImpl<char> &Output,
                          llvm::ArrayRef<intptr_t> QualTypeVals) const {
    ArgToStringFn(Kind, Val, Modifier, Argument, PrevArgs, Output,
                  ArgToStringCookie, QualTypeVals);
  }

  void SetArgToStringFn(ArgToStringFnTy Fn, void *Cookie) {
    ArgToStringFn = Fn;
    ArgToStringCookie = Cookie;
  }

  void notePriorDiagnosticFrom(const DiagnosticsEngine &Other) {
    LastDiagLevel = Other.LastDiagLevel;
  }

  void Reset(bool soft = false);

  //===--------------------------------------------------------------------===//
  // DiagnosticsEngine classification and reporting interfaces.
  //

  bool isIgnored(unsigned DiagID, SourceLocation Loc) const {
    return Diags->getDiagnosticSeverity(DiagID, Loc, *this) ==
           diag::Severity::Ignored;
  }

  Level getDiagnosticLevel(unsigned DiagID, SourceLocation Loc) const {
    return (Level)Diags->getDiagnosticLevel(DiagID, Loc, *this);
  }

  inline DiagnosticBuilder Report(SourceLocation Loc, unsigned DiagID);
  inline DiagnosticBuilder Report(unsigned DiagID);

  void Report(const StoredDiagnostic &storedDiag);

  bool isDiagnosticInFlight() const {
    return CurDiagID != std::numeric_limits<unsigned>::max();
  }

  void SetDelayedDiagnostic(unsigned DiagID, llvm::StringRef Arg1 = "",
                            llvm::StringRef Arg2 = "",
                            llvm::StringRef Arg3 = "");

  void Clear() { CurDiagID = std::numeric_limits<unsigned>::max(); }

  llvm::StringRef getFlagValue() const { return FlagValue; }

private:
  // This is private state used by DiagnosticBuilder.  We put it here instead of
  // in DiagnosticBuilder in order to keep DiagnosticBuilder a small lightweight
  // object.  This implementation choice means that we can only have one
  // diagnostic "in flight" at a time, but this seems to be a reasonable
  // tradeoff to keep these objects small.  Assertions verify that only one
  // diagnostic is in flight at a time.
  friend class Diagnostic;
  friend class DiagnosticBuilder;
  friend class DiagnosticErrorTrap;
  friend class DiagnosticIDs;
  friend class PartialDiagnostic;

  void ReportDelayed();

  SourceLocation CurDiagLoc;

  unsigned CurDiagID;

  enum {
    /// The maximum number of arguments we can hold.
    ///
    /// We currently only support up to 10 arguments (%0-%9).  A single
    /// diagnostic with more than that almost certainly has to be simplified
    /// anyway.
    MaxArguments = DiagnosticStorage::MaxArguments,
  };

  DiagnosticStorage DiagStorage;

  DiagnosticMapping makeUserMapping(diag::Severity Map, SourceLocation L) {
    bool isPragma = L.isValid();
    DiagnosticMapping Mapping =
        DiagnosticMapping::Make(Map, /*IsUser=*/true, isPragma);

    // If this is a pragma mapping, then set the diagnostic mapping flags so
    // that we override command line options.
    if (isPragma) {
      Mapping.setNoWarningAsError(true);
      Mapping.setNoErrorAsFatal(true);
    }

    return Mapping;
  }

  bool ProcessDiag() { return Diags->ProcessDiag(*this); }

protected:
  friend class Sema;

  bool GenCurrentDiagnostic(bool Force = false);

  unsigned getCurrentDiagID() const { return CurDiagID; }

  SourceLocation getCurrentDiagLoc() const { return CurDiagLoc; }
};

class DiagnosticErrorTrap {
  DiagnosticsEngine &Diag;
  unsigned NumErrors;
  unsigned NumUnrecoverableErrors;

public:
  explicit DiagnosticErrorTrap(DiagnosticsEngine &Diag) : Diag(Diag) {
    reset();
  }

  bool hasErrorOccurred() const {
    return Diag.TrapNumErrorsOccurred > NumErrors;
  }

  bool hasUnrecoverableErrorOccurred() const {
    return Diag.TrapNumUnrecoverableErrorsOccurred > NumUnrecoverableErrors;
  }

  void reset() {
    NumErrors = Diag.TrapNumErrorsOccurred;
    NumUnrecoverableErrors = Diag.TrapNumUnrecoverableErrorsOccurred;
  }
};

class StreamingDiagnostic {
public:
  class DiagStorageAllocator {
    static const unsigned NumCached = 16;
    DiagnosticStorage Cached[NumCached];
    DiagnosticStorage *FreeList[NumCached];
    unsigned NumFreeListEntries;

  public:
    DiagStorageAllocator();
    ~DiagStorageAllocator();

    /// Allocate new storage.
    DiagnosticStorage *Allocate() {
      if (NumFreeListEntries == 0)
        return new DiagnosticStorage;

      DiagnosticStorage *Result = FreeList[--NumFreeListEntries];
      Result->NumDiagArgs = 0;
      Result->DiagRanges.clear();
      Result->FixItHints.clear();
      return Result;
    }

    /// Free the given storage object.
    void Deallocate(DiagnosticStorage *S) {
      if (S >= Cached && S <= Cached + NumCached) {
        FreeList[NumFreeListEntries++] = S;
        return;
      }

      delete S;
    }
  };

protected:
  mutable DiagnosticStorage *DiagStorage = nullptr;

  DiagStorageAllocator *Allocator = nullptr;

public:
  DiagnosticStorage *getStorage() const {
    if (DiagStorage)
      return DiagStorage;

    assert(Allocator);
    DiagStorage = Allocator->Allocate();
    return DiagStorage;
  }

  void freeStorage() {
    if (!DiagStorage)
      return;

    // The hot path for PartialDiagnostic is when we just used it to wrap an ID
    // (typically so we have the flexibility of passing a more complex
    // diagnostic into the callee, but that does not commonly occur).
    //
    // Split this out into a slow function for silly compilers (*cough*) which
    // can't do decent partial inlining.
    freeStorageSlow();
  }

  void freeStorageSlow() {
    if (!Allocator)
      return;
    Allocator->Deallocate(DiagStorage);
    DiagStorage = nullptr;
  }

  void AddTaggedVal(uint64_t V, DiagnosticsEngine::ArgumentKind Kind) const {
    if (!DiagStorage)
      DiagStorage = getStorage();

    assert(DiagStorage->NumDiagArgs < DiagnosticStorage::MaxArguments &&
           "Too many arguments to diagnostic!");
    DiagStorage->DiagArgumentsKind[DiagStorage->NumDiagArgs] = Kind;
    DiagStorage->DiagArgumentsVal[DiagStorage->NumDiagArgs++] = V;
  }

  void AddString(llvm::StringRef V) const {
    if (!DiagStorage)
      DiagStorage = getStorage();

    assert(DiagStorage->NumDiagArgs < DiagnosticStorage::MaxArguments &&
           "Too many arguments to diagnostic!");
    DiagStorage->DiagArgumentsKind[DiagStorage->NumDiagArgs] =
        DiagnosticsEngine::ak_std_string;
    DiagStorage->DiagArgumentsStr[DiagStorage->NumDiagArgs++] = std::string(V);
  }

  void AddSourceRange(const CharSourceRange &R) const {
    if (!DiagStorage)
      DiagStorage = getStorage();

    DiagStorage->DiagRanges.push_back(R);
  }

  void AddFixItHint(const FixItHint &Hint) const {
    if (Hint.isNull())
      return;

    if (!DiagStorage)
      DiagStorage = getStorage();

    DiagStorage->FixItHints.push_back(Hint);
  }

  operator bool() const { return true; }

protected:
  StreamingDiagnostic() = default;

  explicit StreamingDiagnostic(DiagnosticStorage *Storage)
      : DiagStorage(Storage) {}

  explicit StreamingDiagnostic(DiagStorageAllocator &Alloc)
      : Allocator(&Alloc) {}

  StreamingDiagnostic(const StreamingDiagnostic &Diag) = default;
  StreamingDiagnostic(StreamingDiagnostic &&Diag) = default;

  ~StreamingDiagnostic() { freeStorage(); }
};

//===----------------------------------------------------------------------===//
// DiagnosticBuilder
//===----------------------------------------------------------------------===//

class DiagnosticBuilder : public StreamingDiagnostic {
  friend class DiagnosticsEngine;
  friend class PartialDiagnostic;

  mutable DiagnosticsEngine *DiagObj = nullptr;

  // NOTE: This field is redundant with DiagObj (IsActive iff (DiagObj == 0)),
  // but LLVM is not currently smart enough to eliminate the null check that
  // Emit() would end up with if we used that as our status variable.
  mutable bool IsActive = false;

  mutable bool IsForceEmit = false;

  DiagnosticBuilder() = default;

  explicit DiagnosticBuilder(DiagnosticsEngine *diagObj)
      : StreamingDiagnostic(&diagObj->DiagStorage), DiagObj(diagObj),
        IsActive(true) {
    assert(diagObj && "DiagnosticBuilder requires a valid DiagnosticsEngine!");
    assert(DiagStorage &&
           "DiagnosticBuilder requires a valid DiagnosticStorage!");
    DiagStorage->NumDiagArgs = 0;
    DiagStorage->DiagRanges.clear();
    DiagStorage->FixItHints.clear();
  }

protected:
  void Clear() const {
    DiagObj = nullptr;
    IsActive = false;
    IsForceEmit = false;
  }

  bool isActive() const { return IsActive; }

  bool Emit() {
    // If this diagnostic is inactive, then its soul was stolen by the copy ctor
    // (or by a subclass, as in SemaDiagnosticBuilder).
    if (!isActive())
      return false;

    // Process the diagnostic.
    bool Result = DiagObj->GenCurrentDiagnostic(IsForceEmit);

    // This diagnostic is dead.
    Clear();

    return Result;
  }

public:
  DiagnosticBuilder(const DiagnosticBuilder &D) : StreamingDiagnostic() {
    DiagObj = D.DiagObj;
    DiagStorage = D.DiagStorage;
    IsActive = D.IsActive;
    IsForceEmit = D.IsForceEmit;
    D.Clear();
  }

  template <typename T> const DiagnosticBuilder &operator<<(const T &V) const {
    assert(isActive() && "Clients must not add to cleared diagnostic!");
    const StreamingDiagnostic &DB = *this;
    DB << V;
    return *this;
  }

  // It is necessary to limit this to rvalue reference to avoid calling this
  // function with a bitfield lvalue argument since non-const reference to
  // bitfield is not allowed.
  template <typename T,
            typename = std::enable_if_t<!std::is_lvalue_reference<T>::value>>
  const DiagnosticBuilder &operator<<(T &&V) const {
    assert(isActive() && "Clients must not add to cleared diagnostic!");
    const StreamingDiagnostic &DB = *this;
    DB << std::move(V);
    return *this;
  }

  DiagnosticBuilder &operator=(const DiagnosticBuilder &) = delete;

  ~DiagnosticBuilder() { Emit(); }

  const DiagnosticBuilder &setForceEmit() const {
    IsForceEmit = true;
    return *this;
  }

  void addFlagValue(llvm::StringRef V) const {
    DiagObj->FlagValue = std::string(V);
  }
};

struct AddFlagValue {
  llvm::StringRef Val;

  explicit AddFlagValue(llvm::StringRef V) : Val(V) {}
};

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           const AddFlagValue V) {
  DB.addFlagValue(V.Val);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             llvm::StringRef S) {
  DB.AddString(S);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const char *Str) {
  DB.AddTaggedVal(reinterpret_cast<intptr_t>(Str),
                  DiagnosticsEngine::ak_c_string);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             int I) {
  DB.AddTaggedVal(I, DiagnosticsEngine::ak_sint);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             long I) {
  DB.AddTaggedVal(I, DiagnosticsEngine::ak_sint);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             long long I) {
  DB.AddTaggedVal(I, DiagnosticsEngine::ak_sint);
  return DB;
}

// We use enable_if here to prevent that this overload is selected for
// pointers or other arguments that are implicitly convertible to bool.
template <typename T>
inline std::enable_if_t<std::is_same<T, bool>::value,
                        const StreamingDiagnostic &>
operator<<(const StreamingDiagnostic &DB, T I) {
  DB.AddTaggedVal(I, DiagnosticsEngine::ak_sint);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             unsigned I) {
  DB.AddTaggedVal(I, DiagnosticsEngine::ak_uint);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             unsigned long I) {
  DB.AddTaggedVal(I, DiagnosticsEngine::ak_uint);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             unsigned long long I) {
  DB.AddTaggedVal(I, DiagnosticsEngine::ak_uint);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             tok::TokenKind I) {
  DB.AddTaggedVal(static_cast<unsigned>(I), DiagnosticsEngine::ak_tokenkind);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const IdentifierInfo *II) {
  DB.AddTaggedVal(reinterpret_cast<intptr_t>(II),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

// Adds a DeclContext to the diagnostic. The enable_if template magic is here
// so that we only match those arguments that are (statically) DeclContexts;
// other arguments that derive from DeclContext (e.g., RecordDecls) will not
// match.
template <typename T>
inline std::enable_if_t<
    std::is_same<std::remove_const_t<T>, DeclContext>::value,
    const StreamingDiagnostic &>
operator<<(const StreamingDiagnostic &DB, T *DC) {
  DB.AddTaggedVal(reinterpret_cast<intptr_t>(DC),
                  DiagnosticsEngine::ak_declcontext);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             SourceLocation L) {
  DB.AddSourceRange(CharSourceRange::getTokenRange(L));
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             SourceRange R) {
  DB.AddSourceRange(CharSourceRange::getTokenRange(R));
  return DB;
}

inline const StreamingDiagnostic &
operator<<(const StreamingDiagnostic &DB, llvm::ArrayRef<SourceRange> Ranges) {
  for (SourceRange R : Ranges)
    DB.AddSourceRange(CharSourceRange::getTokenRange(R));
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const CharSourceRange &R) {
  DB.AddSourceRange(R);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const FixItHint &Hint) {
  DB.AddFixItHint(Hint);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             llvm::ArrayRef<FixItHint> Hints) {
  for (const FixItHint &Hint : Hints)
    DB.AddFixItHint(Hint);
  return DB;
}

inline const StreamingDiagnostic &
operator<<(const StreamingDiagnostic &DB,
           const std::optional<SourceRange> &Opt) {
  if (Opt)
    DB << *Opt;
  return DB;
}

inline const StreamingDiagnostic &
operator<<(const StreamingDiagnostic &DB,
           const std::optional<CharSourceRange> &Opt) {
  if (Opt)
    DB << *Opt;
  return DB;
}

inline const StreamingDiagnostic &
operator<<(const StreamingDiagnostic &DB, const std::optional<FixItHint> &Opt) {
  if (Opt)
    DB << *Opt;
  return DB;
}

using DiagNullabilityKind = std::pair<NullabilityKind, bool>;

const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                      DiagNullabilityKind nullability);

inline DiagnosticBuilder DiagnosticsEngine::Report(SourceLocation Loc,
                                                   unsigned DiagID) {
  assert(CurDiagID == std::numeric_limits<unsigned>::max() &&
         "Multiple diagnostics in flight at once!");
  CurDiagLoc = Loc;
  CurDiagID = DiagID;
  FlagValue.clear();
  return DiagnosticBuilder(this);
}

const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                      llvm::Error &&E);

inline DiagnosticBuilder DiagnosticsEngine::Report(unsigned DiagID) {
  return Report(SourceLocation(), DiagID);
}

//===----------------------------------------------------------------------===//
// Diagnostic
//===----------------------------------------------------------------------===//

class Diagnostic {
  const DiagnosticsEngine *DiagObj;
  std::optional<llvm::StringRef> StoredDiagMessage;

public:
  explicit Diagnostic(const DiagnosticsEngine *DO) : DiagObj(DO) {}
  Diagnostic(const DiagnosticsEngine *DO, llvm::StringRef storedDiagMessage)
      : DiagObj(DO), StoredDiagMessage(storedDiagMessage) {}

  const DiagnosticsEngine *getDiags() const { return DiagObj; }
  unsigned getID() const { return DiagObj->CurDiagID; }
  const SourceLocation &getLocation() const { return DiagObj->CurDiagLoc; }
  bool hasSourceManager() const { return DiagObj->hasSourceManager(); }
  SourceManager &getSourceManager() const {
    return DiagObj->getSourceManager();
  }

  unsigned getNumArgs() const { return DiagObj->DiagStorage.NumDiagArgs; }

  DiagnosticsEngine::ArgumentKind getArgKind(unsigned Idx) const {
    assert(Idx < getNumArgs() && "Argument index out of range!");
    return (DiagnosticsEngine::ArgumentKind)
        DiagObj->DiagStorage.DiagArgumentsKind[Idx];
  }

  const std::string &getArgStdStr(unsigned Idx) const {
    assert(getArgKind(Idx) == DiagnosticsEngine::ak_std_string &&
           "invalid argument accessor!");
    return DiagObj->DiagStorage.DiagArgumentsStr[Idx];
  }

  const char *getArgCStr(unsigned Idx) const {
    assert(getArgKind(Idx) == DiagnosticsEngine::ak_c_string &&
           "invalid argument accessor!");
    return reinterpret_cast<const char *>(
        DiagObj->DiagStorage.DiagArgumentsVal[Idx]);
  }

  int64_t getArgSInt(unsigned Idx) const {
    assert(getArgKind(Idx) == DiagnosticsEngine::ak_sint &&
           "invalid argument accessor!");
    return (int64_t)DiagObj->DiagStorage.DiagArgumentsVal[Idx];
  }

  uint64_t getArgUInt(unsigned Idx) const {
    assert(getArgKind(Idx) == DiagnosticsEngine::ak_uint &&
           "invalid argument accessor!");
    return DiagObj->DiagStorage.DiagArgumentsVal[Idx];
  }

  const IdentifierInfo *getArgIdentifier(unsigned Idx) const {
    assert(getArgKind(Idx) == DiagnosticsEngine::ak_identifierinfo &&
           "invalid argument accessor!");
    return reinterpret_cast<IdentifierInfo *>(
        DiagObj->DiagStorage.DiagArgumentsVal[Idx]);
  }

  uint64_t getRawArg(unsigned Idx) const {
    assert(getArgKind(Idx) != DiagnosticsEngine::ak_std_string &&
           "invalid argument accessor!");
    return DiagObj->DiagStorage.DiagArgumentsVal[Idx];
  }

  unsigned getNumRanges() const {
    return DiagObj->DiagStorage.DiagRanges.size();
  }

  const CharSourceRange &getRange(unsigned Idx) const {
    assert(Idx < getNumRanges() && "Invalid diagnostic range index!");
    return DiagObj->DiagStorage.DiagRanges[Idx];
  }

  llvm::ArrayRef<CharSourceRange> getRanges() const {
    return DiagObj->DiagStorage.DiagRanges;
  }

  unsigned getNumFixItHints() const {
    return DiagObj->DiagStorage.FixItHints.size();
  }

  const FixItHint &getFixItHint(unsigned Idx) const {
    assert(Idx < getNumFixItHints() && "Invalid index!");
    return DiagObj->DiagStorage.FixItHints[Idx];
  }

  llvm::ArrayRef<FixItHint> getFixItHints() const {
    return DiagObj->DiagStorage.FixItHints;
  }

  void FormatDiagnostic(llvm::SmallVectorImpl<char> &OutStr) const;

  void FormatDiagnostic(const char *DiagStr, const char *DiagEnd,
                        llvm::SmallVectorImpl<char> &OutStr) const;
};

/**
 * Represents a diagnostic in a form that can be retained until its
 * corresponding source manager is destroyed.
 */
class StoredDiagnostic {
  unsigned ID;
  DiagnosticsEngine::Level Level;
  FullSourceLoc Loc;
  std::string Message;
  std::vector<CharSourceRange> Ranges;
  std::vector<FixItHint> FixIts;

public:
  StoredDiagnostic() = default;
  StoredDiagnostic(DiagnosticsEngine::Level Level, const Diagnostic &Info);
  StoredDiagnostic(DiagnosticsEngine::Level Level, unsigned ID,
                   llvm::StringRef Message);
  StoredDiagnostic(DiagnosticsEngine::Level Level, unsigned ID,
                   llvm::StringRef Message, FullSourceLoc Loc,
                   llvm::ArrayRef<CharSourceRange> Ranges,
                   llvm::ArrayRef<FixItHint> Fixits);

  explicit operator bool() const { return !Message.empty(); }

  unsigned getID() const { return ID; }
  DiagnosticsEngine::Level getLevel() const { return Level; }
  const FullSourceLoc &getLocation() const { return Loc; }
  llvm::StringRef getMessage() const { return Message; }

  void setLocation(FullSourceLoc Loc) { this->Loc = Loc; }

  using range_iterator = std::vector<CharSourceRange>::const_iterator;

  range_iterator range_begin() const { return Ranges.begin(); }
  range_iterator range_end() const { return Ranges.end(); }
  unsigned range_size() const { return Ranges.size(); }

  llvm::ArrayRef<CharSourceRange> getRanges() const {
    return llvm::ArrayRef(Ranges);
  }

  using fixit_iterator = std::vector<FixItHint>::const_iterator;

  fixit_iterator fixit_begin() const { return FixIts.begin(); }
  fixit_iterator fixit_end() const { return FixIts.end(); }
  unsigned fixit_size() const { return FixIts.size(); }

  llvm::ArrayRef<FixItHint> getFixIts() const { return llvm::ArrayRef(FixIts); }
};

// Simple debug printing of StoredDiagnostic.
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const StoredDiagnostic &);

class DiagnosticConsumer {
protected:
  unsigned NumWarnings = 0; ///< Number of warnings reported
  unsigned NumErrors = 0;   ///< Number of errors reported

public:
  DiagnosticConsumer() = default;
  virtual ~DiagnosticConsumer();

  unsigned getNumErrors() const { return NumErrors; }
  unsigned getNumWarnings() const { return NumWarnings; }
  virtual void clear() { NumWarnings = NumErrors = 0; }

  virtual void BeginSourceFile(const LangOptions &LangOpts,
                               const PrepEngine *PP = nullptr) {}

  virtual void EndSourceFile() {}

  virtual void finish() {}

  virtual bool IncludeInDiagnosticCounts() const;

  virtual void ProcessDiagnostic(DiagnosticsEngine::Level DiagLevel,
                                 const Diagnostic &Info);
};

class IgnoringDiagConsumer : public DiagnosticConsumer {
  virtual void anchor();

  void ProcessDiagnostic(DiagnosticsEngine::Level DiagLevel,
                         const Diagnostic &Info) override {
    // Just ignore it.
  }
};

class ForwardingDiagnosticConsumer : public DiagnosticConsumer {
  DiagnosticConsumer &Target;

public:
  ForwardingDiagnosticConsumer(DiagnosticConsumer &Target) : Target(Target) {}
  ~ForwardingDiagnosticConsumer() override;

  void ProcessDiagnostic(DiagnosticsEngine::Level DiagLevel,
                         const Diagnostic &Info) override;
  void clear() override;

  bool IncludeInDiagnosticCounts() const override;
};

struct TypeDiffInfo {
  intptr_t FromType;
  intptr_t ToType;
  LLVM_PREFERRED_TYPE(bool)
  unsigned PrintFromType : 1;
};

const char ToggleHighlight = 127;

void ProcessWarningOptions(DiagnosticsEngine &Diags,
                           const DiagnosticOptions &Opts,
                           bool ReportDiags = true);
void EscapeStringForDiagnostic(llvm::StringRef Str,
                               llvm::SmallVectorImpl<char> &OutStr);
} // namespace neverc

#endif // NEVERC_FOUNDATION_DIAGNOSTIC_H
