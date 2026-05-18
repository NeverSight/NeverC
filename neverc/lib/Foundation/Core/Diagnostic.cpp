#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/Stack.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Diagnostic/AllDiagnostics.h"
#include "neverc/Foundation/Diagnostic/CLWarnings.h"
#include "neverc/Foundation/Diagnostic/DiagnosticCategories.h"
#include "neverc/Foundation/Diagnostic/DiagnosticIDs.h"
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.h"
#include "neverc/Foundation/Diagnostic/PartialDiagnostic.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Unicode.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifdef __AVX2__
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h> // _AddressOfReturnAddress
#endif

using namespace neverc;

// ===----------------------------------------------------------------------===
// Streaming diagnostic operators
// ===----------------------------------------------------------------------===

const StreamingDiagnostic &neverc::operator<<(const StreamingDiagnostic &DB,
                                              DiagNullabilityKind nullability) {
  DB.AddString(
      ("'" +
       getNullabilitySpelling(nullability.first,
                              /*isContextSensitive=*/nullability.second) +
       "'")
          .str());
  return DB;
}

const StreamingDiagnostic &neverc::operator<<(const StreamingDiagnostic &DB,
                                              llvm::Error &&E) {
  DB.AddString(toString(std::move(E)));
  return DB;
}

// ===----------------------------------------------------------------------===
// DiagnosticsEngine
// ===----------------------------------------------------------------------===

namespace {
void dummyArgToStringFn(
    DiagnosticsEngine::ArgumentKind AK, intptr_t QT, llvm::StringRef Modifier,
    llvm::StringRef Argument,
    llvm::ArrayRef<DiagnosticsEngine::ArgumentValue> PrevArgs,
    llvm::SmallVectorImpl<char> &Output, void *Cookie,
    llvm::ArrayRef<intptr_t> QualTypeVals) {
  llvm::StringRef Str = "<can't format argument>";
  Output.append(Str.begin(), Str.end());
}
} // namespace

DiagnosticsEngine::DiagnosticsEngine(
    llvm::IntrusiveRefCntPtr<DiagnosticIDs> diags,
    llvm::IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts,
    DiagnosticConsumer *client, bool ShouldOwnClient)
    : Diags(std::move(diags)), DiagOpts(std::move(DiagOpts)) {
  setClient(client, ShouldOwnClient);
  ArgToStringFn = dummyArgToStringFn;

  Reset();
}

DiagnosticsEngine::~DiagnosticsEngine() {
  // If we own the diagnostic client, destroy it first so that it can access the
  // engine from its destructor.
  setClient(nullptr);
}

void DiagnosticsEngine::dump() const { DiagStatesByLoc.dump(*SourceMgr); }

void DiagnosticsEngine::dump(llvm::StringRef DiagName) const {
  DiagStatesByLoc.dump(*SourceMgr, DiagName);
}

void DiagnosticsEngine::setClient(DiagnosticConsumer *client,
                                  bool ShouldOwnClient) {
  Owner.reset(ShouldOwnClient ? client : nullptr);
  Client = client;
}

void DiagnosticsEngine::pushMappings(SourceLocation Loc) {
  DiagStateOnPushStack.push_back(GetCurDiagState());
}

bool DiagnosticsEngine::popMappings(SourceLocation Loc) {
  if (DiagStateOnPushStack.empty())
    return false;

  if (DiagStateOnPushStack.back() != GetCurDiagState()) {
    // State changed at some point between push/pop.
    PushDiagStatePoint(DiagStateOnPushStack.back(), Loc);
  }
  DiagStateOnPushStack.pop_back();
  return true;
}

void DiagnosticsEngine::Reset(bool soft /*=false*/) {
  ErrorOccurred = false;
  UncompilableErrorOccurred = false;
  FatalErrorOccurred = false;
  UnrecoverableErrorOccurred = false;

  NumWarnings = 0;
  NumErrors = 0;
  TrapNumErrorsOccurred = 0;
  TrapNumUnrecoverableErrorsOccurred = 0;

  CurDiagID = std::numeric_limits<unsigned>::max();
  LastDiagLevel = DiagnosticIDs::Ignored;
  DelayedDiagID = 0;

  if (!soft) {
    // Clear state related to #pragma diagnostic.
    DiagStates.clear();
    DiagStatesByLoc.clear();
    DiagStateOnPushStack.clear();

    DiagStates.emplace_back();
    DiagStatesByLoc.appendFirst(&DiagStates.back());
  }
}

void DiagnosticsEngine::SetDelayedDiagnostic(unsigned DiagID,
                                             llvm::StringRef Arg1,
                                             llvm::StringRef Arg2,
                                             llvm::StringRef Arg3) {
  if (DelayedDiagID)
    return;

  DelayedDiagID = DiagID;
  DelayedDiagArg1 = Arg1.str();
  DelayedDiagArg2 = Arg2.str();
  DelayedDiagArg3 = Arg3.str();
}

void DiagnosticsEngine::ReportDelayed() {
  unsigned ID = DelayedDiagID;
  DelayedDiagID = 0;
  Report(ID) << DelayedDiagArg1 << DelayedDiagArg2 << DelayedDiagArg3;
}

DiagnosticMapping &
DiagnosticsEngine::DiagState::getOrAddMapping(diag::kind Diag) {
  std::pair<iterator, bool> Result =
      DiagMap.insert(std::make_pair(Diag, DiagnosticMapping()));

  if (Result.second)
    Result.first->second = DiagnosticIDs::getDefaultMapping(Diag);

  return Result.first->second;
}

void DiagnosticsEngine::DiagStateMap::appendFirst(DiagState *State) {
  assert(Files.empty() && "not first");
  FirstDiagState = CurDiagState = State;
  CurDiagStateLoc = SourceLocation();
}

void DiagnosticsEngine::DiagStateMap::append(SourceManager &SrcMgr,
                                             SourceLocation Loc,
                                             DiagState *State) {
  CurDiagState = State;
  CurDiagStateLoc = Loc;

  std::pair<FileID, unsigned> Decomp = SrcMgr.getDecomposedLoc(Loc);
  unsigned Offset = Decomp.second;
  for (File *F = getFile(SrcMgr, Decomp.first); F;
       Offset = F->ParentOffset, F = F->Parent) {
    F->HasLocalTransitions = true;
    auto &Last = F->StateTransitions.back();
    assert(Last.Offset <= Offset && "state transitions added out of order");

    if (Last.Offset == Offset) {
      if (Last.State == State)
        break;
      Last.State = State;
      continue;
    }

    F->StateTransitions.push_back({State, Offset});
  }
}

DiagnosticsEngine::DiagState *
DiagnosticsEngine::DiagStateMap::lookup(SourceManager &SrcMgr,
                                        SourceLocation Loc) const {
  // Common case: we have not seen any diagnostic pragmas.
  if (Files.empty())
    return FirstDiagState;

  std::pair<FileID, unsigned> Decomp = SrcMgr.getDecomposedLoc(Loc);
  const File *F = getFile(SrcMgr, Decomp.first);
  return F->lookup(Decomp.second);
}

DiagnosticsEngine::DiagState *
DiagnosticsEngine::DiagStateMap::File::lookup(unsigned Offset) const {
  auto OnePastIt =
      llvm::partition_point(StateTransitions, [=](const DiagStatePoint &P) {
        return P.Offset <= Offset;
      });
  assert(OnePastIt != StateTransitions.begin() && "missing initial state");
  return OnePastIt[-1].State;
}

DiagnosticsEngine::DiagStateMap::File *
DiagnosticsEngine::DiagStateMap::getFile(SourceManager &SrcMgr,
                                         FileID ID) const {
  auto Range = Files.equal_range(ID);
  if (Range.first != Range.second)
    return &Range.first->second;
  auto &F = Files.insert(Range.first, std::make_pair(ID, File()))->second;

  // We created a new File; look up the diagnostic state at the start of it and
  // initialize it.
  if (ID.isValid()) {
    std::pair<FileID, unsigned> Decomp = SrcMgr.getDecomposedIncludedLoc(ID);
    F.Parent = getFile(SrcMgr, Decomp.first);
    F.ParentOffset = Decomp.second;
    F.StateTransitions.push_back({F.Parent->lookup(Decomp.second), 0});
  } else {
    // This is the (imaginary) root file into which we pretend all top-level
    // files are included; it descends from the initial state.
    //
    F.StateTransitions.push_back({FirstDiagState, 0});
  }
  return &F;
}

void DiagnosticsEngine::DiagStateMap::dump(SourceManager &SrcMgr,
                                           llvm::StringRef DiagName) const {
  llvm::errs() << "diagnostic state at ";
  CurDiagStateLoc.print(llvm::errs(), SrcMgr);
  llvm::errs() << ": " << CurDiagState << "\n";

  for (auto &F : Files) {
    FileID ID = F.first;
    File &File = F.second;

    bool PrintedOuterHeading = false;
    auto PrintOuterHeading = [&] {
      if (PrintedOuterHeading)
        return;
      PrintedOuterHeading = true;

      llvm::errs() << "File " << &File << " <FileID " << ID.getHashValue()
                   << ">: " << SrcMgr.getBufferOrFake(ID).getBufferIdentifier();

      if (F.second.Parent) {
        std::pair<FileID, unsigned> Decomp =
            SrcMgr.getDecomposedIncludedLoc(ID);
        assert(File.ParentOffset == Decomp.second);
        llvm::errs() << " parent " << File.Parent << " <FileID "
                     << Decomp.first.getHashValue() << "> ";
        SrcMgr.getLocForStartOfFile(Decomp.first)
            .getLocWithOffset(Decomp.second)
            .print(llvm::errs(), SrcMgr);
      }
      if (File.HasLocalTransitions)
        llvm::errs() << " has_local_transitions";
      llvm::errs() << "\n";
    };

    if (DiagName.empty())
      PrintOuterHeading();

    for (DiagStatePoint &Transition : File.StateTransitions) {
      bool PrintedInnerHeading = false;
      auto PrintInnerHeading = [&] {
        if (PrintedInnerHeading)
          return;
        PrintedInnerHeading = true;

        PrintOuterHeading();
        llvm::errs() << "  ";
        SrcMgr.getLocForStartOfFile(ID)
            .getLocWithOffset(Transition.Offset)
            .print(llvm::errs(), SrcMgr);
        llvm::errs() << ": state " << Transition.State << ":\n";
      };

      if (DiagName.empty())
        PrintInnerHeading();

      for (auto &Mapping : *Transition.State) {
        llvm::StringRef Option =
            DiagnosticIDs::getWarningOptionForDiag(Mapping.first);
        if (!DiagName.empty() && DiagName != Option)
          continue;

        PrintInnerHeading();
        llvm::errs() << "    ";
        if (Option.empty())
          llvm::errs() << "<unknown " << Mapping.first << ">";
        else
          llvm::errs() << Option;
        llvm::errs() << ": ";

        switch (Mapping.second.getSeverity()) {
        case diag::Severity::Ignored:
          llvm::errs() << "ignored";
          break;
        case diag::Severity::Remark:
          llvm::errs() << "remark";
          break;
        case diag::Severity::Warning:
          llvm::errs() << "warning";
          break;
        case diag::Severity::Error:
          llvm::errs() << "error";
          break;
        case diag::Severity::Fatal:
          llvm::errs() << "fatal";
          break;
        }

        if (!Mapping.second.isUser())
          llvm::errs() << " default";
        if (Mapping.second.isPragma())
          llvm::errs() << " pragma";
        if (Mapping.second.hasNoWarningAsError())
          llvm::errs() << " no-error";
        if (Mapping.second.hasNoErrorAsFatal())
          llvm::errs() << " no-fatal";
        if (Mapping.second.wasUpgradedFromWarning())
          llvm::errs() << " overruled";
        llvm::errs() << "\n";
      }
    }
  }
}

void DiagnosticsEngine::PushDiagStatePoint(DiagState *State,
                                           SourceLocation Loc) {
  assert(Loc.isValid() && "Adding invalid loc point");
  DiagStatesByLoc.append(*SourceMgr, Loc, State);
}

void DiagnosticsEngine::setSeverity(diag::kind Diag, diag::Severity Map,
                                    SourceLocation L) {
  assert(Diag < diag::DIAG_UPPER_LIMIT && "Can only map builtin diagnostics");
  assert((Diags->isBuiltinWarningOrExtension(Diag) ||
          (Map == diag::Severity::Fatal || Map == diag::Severity::Error)) &&
         "Cannot map errors into warnings!");
  assert((L.isInvalid() || SourceMgr) && "No SourceMgr for valid location");

  // Don't allow a mapping to a warning override an error/fatal mapping.
  bool WasUpgradedFromWarning = false;
  if (Map == diag::Severity::Warning) {
    DiagnosticMapping &Info = GetCurDiagState()->getOrAddMapping(Diag);
    if (Info.getSeverity() == diag::Severity::Error ||
        Info.getSeverity() == diag::Severity::Fatal) {
      Map = Info.getSeverity();
      WasUpgradedFromWarning = true;
    }
  }
  DiagnosticMapping Mapping = makeUserMapping(Map, L);
  Mapping.setUpgradedFromWarning(WasUpgradedFromWarning);

  // Make sure we propagate the NoWarningAsError flag from an existing
  // mapping (which may be the default mapping).
  DiagnosticMapping &Info = GetCurDiagState()->getOrAddMapping(Diag);
  Mapping.setNoWarningAsError(Info.hasNoWarningAsError() ||
                              Mapping.hasNoWarningAsError());

  // Common case; setting all the diagnostics of a group in one place.
  if ((L.isInvalid() || L == DiagStatesByLoc.getCurDiagStateLoc()) &&
      DiagStatesByLoc.getCurDiagState()) {
    DiagStatesByLoc.getCurDiagState()->setMapping(Diag, Mapping);
    return;
  }

  // A diagnostic pragma occurred, create a new DiagState initialized with
  // the current one and a new DiagStatePoint to record at which location
  // the new state became active.
  DiagStates.push_back(*GetCurDiagState());
  DiagStates.back().setMapping(Diag, Mapping);
  PushDiagStatePoint(&DiagStates.back(), L);
}

bool DiagnosticsEngine::setSeverityForGroup(diag::Flavor Flavor,
                                            llvm::StringRef Group,
                                            diag::Severity Map,
                                            SourceLocation Loc) {
  llvm::SmallVector<diag::kind, 256> GroupDiags;
  if (Diags->getDiagnosticsInGroup(Flavor, Group, GroupDiags))
    return true;

  for (diag::kind Diag : GroupDiags)
    setSeverity(Diag, Map, Loc);

  return false;
}

bool DiagnosticsEngine::setSeverityForGroup(diag::Flavor Flavor,
                                            diag::Group Group,
                                            diag::Severity Map,
                                            SourceLocation Loc) {
  return setSeverityForGroup(Flavor, Diags->getWarningOptionForGroup(Group),
                             Map, Loc);
}

bool DiagnosticsEngine::setDiagnosticGroupWarningAsError(llvm::StringRef Group,
                                                         bool Enabled) {
  // If we are enabling this feature, just set the diagnostic mappings to map to
  // errors.
  if (Enabled)
    return setSeverityForGroup(diag::Flavor::WarningOrError, Group,
                               diag::Severity::Error);

  // Otherwise, we want to set the diagnostic mapping's "no Werror" bit, and
  // potentially downgrade anything already mapped to be a warning.

  llvm::SmallVector<diag::kind, 8> GroupDiags;
  if (Diags->getDiagnosticsInGroup(diag::Flavor::WarningOrError, Group,
                                   GroupDiags))
    return true;

  for (diag::kind Diag : GroupDiags) {
    DiagnosticMapping &Info = GetCurDiagState()->getOrAddMapping(Diag);

    if (Info.getSeverity() == diag::Severity::Error ||
        Info.getSeverity() == diag::Severity::Fatal)
      Info.setSeverity(diag::Severity::Warning);

    Info.setNoWarningAsError(true);
  }

  return false;
}

bool DiagnosticsEngine::setDiagnosticGroupErrorAsFatal(llvm::StringRef Group,
                                                       bool Enabled) {
  if (Enabled)
    return setSeverityForGroup(diag::Flavor::WarningOrError, Group,
                               diag::Severity::Fatal);

  // Otherwise, we want to set the diagnostic mapping's "no Wfatal-errors" bit,
  // and potentially downgrade anything already mapped to be a fatal error.

  llvm::SmallVector<diag::kind, 8> GroupDiags;
  if (Diags->getDiagnosticsInGroup(diag::Flavor::WarningOrError, Group,
                                   GroupDiags))
    return true;

  for (diag::kind Diag : GroupDiags) {
    DiagnosticMapping &Info = GetCurDiagState()->getOrAddMapping(Diag);

    if (Info.getSeverity() == diag::Severity::Fatal)
      Info.setSeverity(diag::Severity::Error);

    Info.setNoErrorAsFatal(true);
  }

  return false;
}

void DiagnosticsEngine::setSeverityForAll(diag::Flavor Flavor,
                                          diag::Severity Map,
                                          SourceLocation Loc) {
  std::vector<diag::kind> AllDiags;
  DiagnosticIDs::getAllDiagnostics(Flavor, AllDiags);

  for (diag::kind Diag : AllDiags)
    if (Diags->isBuiltinWarningOrExtension(Diag))
      setSeverity(Diag, Map, Loc);
}

void DiagnosticsEngine::Report(const StoredDiagnostic &storedDiag) {
  assert(CurDiagID == std::numeric_limits<unsigned>::max() &&
         "Multiple diagnostics in flight at once!");

  CurDiagLoc = storedDiag.getLocation();
  CurDiagID = storedDiag.getID();
  DiagStorage.NumDiagArgs = 0;

  DiagStorage.DiagRanges.clear();
  DiagStorage.DiagRanges.append(storedDiag.range_begin(),
                                storedDiag.range_end());

  DiagStorage.FixItHints.clear();
  DiagStorage.FixItHints.append(storedDiag.fixit_begin(),
                                storedDiag.fixit_end());

  assert(Client && "DiagnosticConsumer not set!");
  Level DiagLevel = storedDiag.getLevel();
  Diagnostic Info(this, storedDiag.getMessage());
  Client->ProcessDiagnostic(DiagLevel, Info);
  if (Client->IncludeInDiagnosticCounts()) {
    if (DiagLevel == DiagnosticsEngine::Warning)
      ++NumWarnings;
  }

  CurDiagID = std::numeric_limits<unsigned>::max();
}

bool DiagnosticsEngine::GenCurrentDiagnostic(bool Force) {
  assert(getClient() && "DiagnosticClient not set!");

  bool Emitted;
  if (Force) {
    Diagnostic Info(this);

    // Figure out the diagnostic level of this message.
    DiagnosticIDs::Level DiagLevel =
        Diags->getDiagnosticLevel(Info.getID(), Info.getLocation(), *this);

    Emitted = (DiagLevel != DiagnosticIDs::Ignored);
    if (Emitted) {
      Diags->GenDiag(*this, DiagLevel);
    }
  } else {

    Emitted = ProcessDiag();
  }

  // Clear out the current diagnostic object.
  Clear();

  // If there was a delayed diagnostic, emit it now.
  if (!Force && DelayedDiagID)
    ReportDelayed();

  return Emitted;
}

// ===----------------------------------------------------------------------===
// DiagnosticConsumer
// ===----------------------------------------------------------------------===

DiagnosticConsumer::~DiagnosticConsumer() = default;

void DiagnosticConsumer::ProcessDiagnostic(DiagnosticsEngine::Level DiagLevel,
                                           const Diagnostic &Info) {
  if (!IncludeInDiagnosticCounts())
    return;

  if (DiagLevel == DiagnosticsEngine::Warning)
    ++NumWarnings;
  else if (DiagLevel >= DiagnosticsEngine::Error)
    ++NumErrors;
}

// ===----------------------------------------------------------------------===
// Format string processing
// ===----------------------------------------------------------------------===

namespace {

template <std::size_t StrLen>
bool matchesModifier(const char *Modifier, unsigned ModifierLen,
                     const char (&Str)[StrLen]) {
  return StrLen - 1 == ModifierLen && memcmp(Modifier, Str, StrLen - 1) == 0;
}

const char *advanceFormatTo(const char *I, const char *E, char Target) {
  unsigned Depth = 0;

  for (; I != E; ++I) {
    if (Depth == 0 && *I == Target)
      return I;
    if (Depth != 0 && *I == '}')
      Depth--;

    if (*I == '%') {
      I++;
      if (I == E)
        break;

      // Escaped characters get implicitly skipped here.

      // Format specifier.
      if (!isDigit(*I) && !isPunctuation(*I)) {
        for (I++; I != E && !isDigit(*I) && *I != '{'; I++)
          ;
        if (I == E)
          break;
        if (*I == '{')
          Depth++;
      }
    }
  }
  return E;
}

void formatSelectModifier(const Diagnostic &DInfo, unsigned ValNo,
                          const char *Argument, unsigned ArgumentLen,
                          llvm::SmallVectorImpl<char> &OutStr) {
  const char *ArgumentEnd = Argument + ArgumentLen;

  // Skip over 'ValNo' |'s.
  while (ValNo) {
    const char *NextVal = advanceFormatTo(Argument, ArgumentEnd, '|');
    assert(NextVal != ArgumentEnd &&
           "Value for integer select modifier was"
           " larger than the number of options in the diagnostic string!");
    Argument = NextVal + 1; // Skip this string.
    --ValNo;
  }
  const char *EndPtr = advanceFormatTo(Argument, ArgumentEnd, '|');

  // Recursively format the result of the select clause into the output string.
  DInfo.FormatDiagnostic(Argument, EndPtr, OutStr);
}

void formatIntegerSModifier(unsigned ValNo,
                            llvm::SmallVectorImpl<char> &OutStr) {
  if (ValNo != 1)
    OutStr.push_back('s');
}

void formatOrdinalModifier(unsigned ValNo,
                           llvm::SmallVectorImpl<char> &OutStr) {
  assert(ValNo != 0 && "ValNo must be strictly positive!");

  llvm::raw_svector_ostream Out(OutStr);

  // We could use text forms for the first N ordinals, but the numeric
  // forms are actually nicer in diagnostics because they stand out.
  Out << ValNo << llvm::getOrdinalSuffix(ValNo);
}

unsigned parsePluralNumber(const char *&Start, const char *End) {
  unsigned Val = 0;
  while (Start != End && *Start >= '0' && *Start <= '9') {
    Val *= 10;
    Val += *Start - '0';
    ++Start;
  }
  return Val;
}

bool checkPluralRange(unsigned Val, const char *&Start, const char *End) {
  if (*Start != '[') {
    unsigned Ref = parsePluralNumber(Start, End);
    return Ref == Val;
  }

  ++Start;
  unsigned Low = parsePluralNumber(Start, End);
  assert(*Start == ',' && "Bad plural expression syntax: expected ,");
  ++Start;
  unsigned High = parsePluralNumber(Start, End);
  assert(*Start == ']' && "Bad plural expression syntax: expected )");
  ++Start;
  return Low <= Val && Val <= High;
}

bool evaluatePluralExpr(unsigned ValNo, const char *Start, const char *End) {
  // Empty condition?
  if (*Start == ':')
    return true;

  while (true) {
    char C = *Start;
    if (C == '%') {
      // Modulo expression
      ++Start;
      unsigned Arg = parsePluralNumber(Start, End);
      assert(*Start == '=' && "Bad plural expression syntax: expected =");
      ++Start;
      unsigned ValMod = ValNo % Arg;
      if (checkPluralRange(ValMod, Start, End))
        return true;
    } else {
      assert((C == '[' || (C >= '0' && C <= '9')) &&
             "Bad plural expression syntax: unexpected character");
      // Range expression
      if (checkPluralRange(ValNo, Start, End))
        return true;
    }

    // Scan for next or-expr part.
    Start = std::find(Start, End, ',');
    if (Start == End)
      break;
    ++Start;
  }
  return false;
}

void formatPluralModifier(const Diagnostic &DInfo, unsigned ValNo,
                          const char *Argument, unsigned ArgumentLen,
                          llvm::SmallVectorImpl<char> &OutStr) {
  const char *ArgumentEnd = Argument + ArgumentLen;
  while (true) {
    assert(Argument < ArgumentEnd && "Plural expression didn't match.");
    const char *ExprEnd = Argument;
    while (*ExprEnd != ':') {
      assert(ExprEnd != ArgumentEnd && "Plural missing expression end");
      ++ExprEnd;
    }
    if (evaluatePluralExpr(ValNo, Argument, ExprEnd)) {
      Argument = ExprEnd + 1;
      ExprEnd = advanceFormatTo(Argument, ArgumentEnd, '|');

      // Recursively format the result of the plural clause into the
      // output string.
      DInfo.FormatDiagnostic(Argument, ExprEnd, OutStr);
      return;
    }
    Argument = advanceFormatTo(Argument, ArgumentEnd - 1, '|') + 1;
  }
}

const char *describeTokenForDiagnostic(tok::TokenKind Kind) {
  switch (Kind) {
  case tok::identifier:
    return "identifier";
  default:
    return nullptr;
  }
}

} // namespace

__attribute__((cold, noinline)) void
Diagnostic::FormatDiagnostic(llvm::SmallVectorImpl<char> &OutStr) const {
  if (StoredDiagMessage.has_value()) {
    OutStr.append(StoredDiagMessage->begin(), StoredDiagMessage->end());
    return;
  }

  llvm::StringRef Diag =
      getDiags()->getDiagnosticIDs()->getDescription(getID());

  FormatDiagnostic(Diag.begin(), Diag.end(), OutStr);
}

void neverc::EscapeStringForDiagnostic(llvm::StringRef Str,
                                       llvm::SmallVectorImpl<char> &OutStr) {
  OutStr.reserve(OutStr.size() + Str.size());
  auto *Begin = reinterpret_cast<const unsigned char *>(Str.data());
  llvm::raw_svector_ostream OutStream(OutStr);
  const unsigned char *End = Begin + Str.size();

#ifdef __AVX512BW__
  {
    while (Begin + 64 <= End) {
      __m512i V = _mm512_loadu_si512(Begin);
      __mmask64 HiBit = _mm512_movepi8_mask(V);
      __mmask64 Control = _mm512_cmplt_epu8_mask(V, _mm512_set1_epi8(' '));
      if (LLVM_LIKELY((HiBit | Control) == 0)) {
        OutStr.append(Begin, Begin + 64);
        Begin += 64;
        continue;
      }
      break;
    }
  }
#endif
#if defined(__AVX512BW__) || defined(__AVX2__)
  {
    const __m256i Hi7F = _mm256_set1_epi8(0x7F);
    const __m256i LoSpace = _mm256_set1_epi8(' ' - 1);
    while (Begin + 32 <= End) {
      __m256i V = _mm256_loadu_si256((const __m256i *)Begin);
      __m256i IsAscii = _mm256_cmpeq_epi8(_mm256_and_si256(V, Hi7F), V);
      __m256i IsPrintableRange = _mm256_cmpgt_epi8(V, LoSpace);
      __m256i Fast = _mm256_and_si256(IsAscii, IsPrintableRange);
      if (LLVM_LIKELY(_mm256_movemask_epi8(Fast) == -1)) {
        OutStr.append(Begin, Begin + 32);
        Begin += 32;
        continue;
      }
      break;
    }
  }
#elif defined(__SSE2__)
  {
    const __m128i Hi7F = _mm_set1_epi8(0x7F);
    const __m128i LoSpace = _mm_set1_epi8(' ' - 1);
    while (Begin + 16 <= End) {
      __m128i V = _mm_loadu_si128((const __m128i *)Begin);
      __m128i IsAscii = _mm_cmpeq_epi8(_mm_and_si128(V, Hi7F), V);
      __m128i IsPrintableRange = _mm_cmpgt_epi8(V, LoSpace);
      __m128i Fast = _mm_and_si128(IsAscii, IsPrintableRange);
      if (LLVM_LIKELY(_mm_movemask_epi8(Fast) == 0xFFFF)) {
        OutStr.append(Begin, Begin + 16);
        Begin += 16;
        continue;
      }
      break;
    }
  }
#elif defined(__aarch64__) && defined(__ARM_NEON)
  {
    while (Begin + 16 <= End) {
      uint8x16_t V = vld1q_u8(Begin);
      uint8x16_t IsHiBit = vcgtq_u8(V, vdupq_n_u8(0x7F));
      uint8x16_t IsControl = vcltq_u8(V, vdupq_n_u8(' '));
      uint8x16_t NeedEscape = vorrq_u8(IsHiBit, IsControl);
      if (LLVM_LIKELY(vmaxvq_u8(NeedEscape) == 0)) {
        OutStr.append(Begin, Begin + 16);
        Begin += 16;
        continue;
      }
      break;
    }
  }
#endif

  while (Begin != End) {
    if (isPrintable(*Begin) || isWhitespace(*Begin)) {
      OutStream << *Begin;
      ++Begin;
      continue;
    }
    if (llvm::isLegalUTF8Sequence(Begin, End)) {
      llvm::UTF32 CodepointValue;
      llvm::UTF32 *CpPtr = &CodepointValue;
      const unsigned char *CodepointBegin = Begin;
      const unsigned char *CodepointEnd =
          Begin + llvm::getNumBytesForUTF8(*Begin);
      llvm::ConversionResult Res = llvm::ConvertUTF8toUTF32(
          &Begin, CodepointEnd, &CpPtr, CpPtr + 1, llvm::strictConversion);
      (void)Res;
      assert(
          llvm::conversionOK == Res &&
          "the sequence is legal UTF-8 but we couldn't convert it to UTF-32");
      assert(Begin == CodepointEnd &&
             "we must be further along in the string now");
      if (llvm::sys::unicode::isPrintable(CodepointValue) ||
          llvm::sys::unicode::isFormatting(CodepointValue)) {
        OutStr.append(CodepointBegin, CodepointEnd);
        continue;
      }
      // Unprintable code point.
      OutStream << "<U+" << llvm::format_hex_no_prefix(CodepointValue, 4, true)
                << ">";
      continue;
    }
    // Invalid code unit.
    OutStream << "<" << llvm::format_hex_no_prefix(*Begin, 2, true) << ">";
    ++Begin;
  }
}

__attribute__((cold, noinline)) void
Diagnostic::FormatDiagnostic(const char *DiagStr, const char *DiagEnd,
                             llvm::SmallVectorImpl<char> &OutStr) const {
  // When the diagnostic string is only "%0", the entire string is being given
  // by an outside source.  Remove unprintable characters from this string
  // and skip all the other string processing.
  if (DiagEnd - DiagStr == 2 &&
      llvm::StringRef(DiagStr, DiagEnd - DiagStr).equals("%0") &&
      getArgKind(0) == DiagnosticsEngine::ak_std_string) {
    const std::string &S = getArgStdStr(0);
    EscapeStringForDiagnostic(S, OutStr);
    return;
  }

  llvm::SmallVector<DiagnosticsEngine::ArgumentValue, 8> FormattedArgs;

  llvm::SmallVector<intptr_t, 2> QualTypeVals;
  llvm::SmallString<64> Tree;

  for (unsigned i = 0, e = getNumArgs(); i < e; ++i)
    if (getArgKind(i) == DiagnosticsEngine::ak_qualtype)
      QualTypeVals.push_back(getRawArg(i));

  while (DiagStr != DiagEnd) {
    if (DiagStr[0] != '%') {
      const char *StrEnd = DiagStr;
#if defined(__AVX2__)
      {
        const __m256i VPct = _mm256_set1_epi8('%');
        while (StrEnd + 32 <= DiagEnd) {
          __m256i V = _mm256_loadu_si256((const __m256i *)StrEnd);
          unsigned Mask = static_cast<unsigned>(
              _mm256_movemask_epi8(_mm256_cmpeq_epi8(V, VPct)));
          if (Mask != 0) {
            StrEnd += llvm::countr_zero(Mask);
            goto found_pct;
          }
          StrEnd += 32;
        }
      }
#elif defined(__SSE2__)
      {
        const __m128i VPct = _mm_set1_epi8('%');
        while (StrEnd + 16 <= DiagEnd) {
          __m128i V = _mm_loadu_si128((const __m128i *)StrEnd);
          unsigned Mask = _mm_movemask_epi8(_mm_cmpeq_epi8(V, VPct));
          if (Mask != 0) {
            StrEnd += __builtin_ctz(Mask);
            goto found_pct;
          }
          StrEnd += 16;
        }
      }
#elif defined(__aarch64__) && defined(__ARM_NEON)
      {
        const uint8x16_t VPct = vdupq_n_u8('%');
        while (StrEnd + 16 <= DiagEnd) {
          uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(StrEnd));
          uint8x16_t Hit = vceqq_u8(V, VPct);
          if (vmaxvq_u8(Hit) != 0) {
            uint64x2_t As64 = vreinterpretq_u64_u8(Hit);
            uint64_t Lo = vgetq_lane_u64(As64, 0);
            if (Lo) {
              StrEnd += __builtin_ctzll(Lo) >> 3;
              goto found_pct;
            }
            StrEnd += 8 + (__builtin_ctzll(vgetq_lane_u64(As64, 1)) >> 3);
            goto found_pct;
          }
          StrEnd += 16;
        }
      }
#endif
      while (StrEnd != DiagEnd && *StrEnd != '%')
        ++StrEnd;
    found_pct:
      OutStr.append(DiagStr, StrEnd);
      DiagStr = StrEnd;
      continue;
    } else if (isPunctuation(DiagStr[1])) {
      OutStr.push_back(DiagStr[1]); // %% -> %.
      DiagStr += 2;
      continue;
    }

    // Skip the %.
    ++DiagStr;

    // This must be a placeholder for a diagnostic argument.  The format for a
    // placeholder is one of "%0", "%modifier0", or "%modifier{arguments}0".
    // The digit is a number from 0-9 indicating which argument this comes from.
    // The modifier is a string of digits from the set [-a-z]+, arguments is a
    // brace enclosed string.
    const char *Modifier = nullptr, *Argument = nullptr;
    unsigned ModifierLen = 0, ArgumentLen = 0;
    if (!isDigit(DiagStr[0])) {
      Modifier = DiagStr;
      while (DiagStr[0] == '-' || (DiagStr[0] >= 'a' && DiagStr[0] <= 'z'))
        ++DiagStr;
      ModifierLen = DiagStr - Modifier;

      // If we have an argument, get it next.
      if (DiagStr[0] == '{') {
        ++DiagStr; // Skip {.
        Argument = DiagStr;

        DiagStr = advanceFormatTo(DiagStr, DiagEnd, '}');
        assert(DiagStr != DiagEnd && "Mismatched {}'s in diagnostic string!");
        ArgumentLen = DiagStr - Argument;
        ++DiagStr; // Skip }.
      }
    }

    assert(isDigit(*DiagStr) && "Invalid format for argument in diagnostic");
    unsigned ArgNo = *DiagStr++ - '0';

    // Only used for type diffing.
    unsigned ArgNo2 = ArgNo;

    DiagnosticsEngine::ArgumentKind Kind = getArgKind(ArgNo);
    if (matchesModifier(Modifier, ModifierLen, "diff")) {
      assert(*DiagStr == ',' && isDigit(*(DiagStr + 1)) &&
             "Invalid format for diff modifier");
      ++DiagStr; // Comma.
      ArgNo2 = *DiagStr++ - '0';
      DiagnosticsEngine::ArgumentKind Kind2 = getArgKind(ArgNo2);
      if (Kind == DiagnosticsEngine::ak_qualtype &&
          Kind2 == DiagnosticsEngine::ak_qualtype)
        Kind = DiagnosticsEngine::ak_qualtype_pair;
      else {
        // %diff only supports QualTypes.  For other kinds of arguments,
        // use the default printing.  For example, if the modifier is:
        //   "%diff{compare $ to $|other text}1,2"
        // treat it as:
        //   "compare %1 to %2"
        const char *ArgumentEnd = Argument + ArgumentLen;
        const char *Pipe = advanceFormatTo(Argument, ArgumentEnd, '|');
        assert(advanceFormatTo(Pipe + 1, ArgumentEnd, '|') == ArgumentEnd &&
               "Found too many '|'s in a %diff modifier!");
        const char *FirstDollar = advanceFormatTo(Argument, Pipe, '$');
        const char *SecondDollar = advanceFormatTo(FirstDollar + 1, Pipe, '$');
        const char ArgStr1[] = {'%', static_cast<char>('0' + ArgNo)};
        const char ArgStr2[] = {'%', static_cast<char>('0' + ArgNo2)};
        FormatDiagnostic(Argument, FirstDollar, OutStr);
        FormatDiagnostic(ArgStr1, ArgStr1 + 2, OutStr);
        FormatDiagnostic(FirstDollar + 1, SecondDollar, OutStr);
        FormatDiagnostic(ArgStr2, ArgStr2 + 2, OutStr);
        FormatDiagnostic(SecondDollar + 1, Pipe, OutStr);
        continue;
      }
    }

    switch (Kind) {
    // ---- STRINGS ----
    case DiagnosticsEngine::ak_std_string: {
      const std::string &S = getArgStdStr(ArgNo);
      assert(ModifierLen == 0 && "No modifiers for strings yet");
      EscapeStringForDiagnostic(S, OutStr);
      break;
    }
    case DiagnosticsEngine::ak_c_string: {
      const char *S = getArgCStr(ArgNo);
      assert(ModifierLen == 0 && "No modifiers for strings yet");

      // Don't crash if get passed a null pointer by accident.
      if (!S)
        S = "(null)";
      EscapeStringForDiagnostic(S, OutStr);
      break;
    }
    // ---- INTEGERS ----
    case DiagnosticsEngine::ak_sint: {
      int64_t Val = getArgSInt(ArgNo);

      if (matchesModifier(Modifier, ModifierLen, "select")) {
        formatSelectModifier(*this, (unsigned)Val, Argument, ArgumentLen,
                             OutStr);
      } else if (matchesModifier(Modifier, ModifierLen, "s")) {
        formatIntegerSModifier(Val, OutStr);
      } else if (matchesModifier(Modifier, ModifierLen, "plural")) {
        formatPluralModifier(*this, (unsigned)Val, Argument, ArgumentLen,
                             OutStr);
      } else if (matchesModifier(Modifier, ModifierLen, "ordinal")) {
        formatOrdinalModifier((unsigned)Val, OutStr);
      } else {
        assert(ModifierLen == 0 && "Unknown integer modifier");
        llvm::raw_svector_ostream(OutStr) << Val;
      }
      break;
    }
    case DiagnosticsEngine::ak_uint: {
      uint64_t Val = getArgUInt(ArgNo);

      if (matchesModifier(Modifier, ModifierLen, "select")) {
        formatSelectModifier(*this, Val, Argument, ArgumentLen, OutStr);
      } else if (matchesModifier(Modifier, ModifierLen, "s")) {
        formatIntegerSModifier(Val, OutStr);
      } else if (matchesModifier(Modifier, ModifierLen, "plural")) {
        formatPluralModifier(*this, (unsigned)Val, Argument, ArgumentLen,
                             OutStr);
      } else if (matchesModifier(Modifier, ModifierLen, "ordinal")) {
        formatOrdinalModifier(Val, OutStr);
      } else {
        assert(ModifierLen == 0 && "Unknown integer modifier");
        llvm::raw_svector_ostream(OutStr) << Val;
      }
      break;
    }
    // ---- TOKEN SPELLINGS ----
    case DiagnosticsEngine::ak_tokenkind: {
      tok::TokenKind Kind = static_cast<tok::TokenKind>(getRawArg(ArgNo));
      assert(ModifierLen == 0 && "No modifiers for token kinds yet");

      llvm::raw_svector_ostream Out(OutStr);
      if (const char *S = tok::getPunctuatorSpelling(Kind))
        // Quoted token spelling for punctuators.
        Out << '\'' << S << '\'';
      else if ((S = tok::getKeywordSpelling(Kind)))
        // Unquoted token spelling for keywords.
        Out << S;
      else if ((S = describeTokenForDiagnostic(Kind)))
        // Unquoted translatable token name.
        Out << S;
      else if ((S = tok::getTokenName(Kind)))
        // Debug name, shouldn't appear in user-facing diagnostics.
        Out << '<' << S << '>';
      else
        Out << "(null)";
      break;
    }
    // ---- NAMES and TYPES ----
    case DiagnosticsEngine::ak_identifierinfo: {
      const IdentifierInfo *II = getArgIdentifier(ArgNo);
      assert(ModifierLen == 0 && "No modifiers for strings yet");

      // Don't crash if get passed a null pointer by accident.
      if (!II) {
        const char *S = "(null)";
        OutStr.append(S, S + strlen(S));
        continue;
      }

      llvm::raw_svector_ostream(OutStr) << '\'' << II->getName() << '\'';
      break;
    }
    case DiagnosticsEngine::ak_addrspace:
    case DiagnosticsEngine::ak_qual:
    case DiagnosticsEngine::ak_qualtype:
    case DiagnosticsEngine::ak_declarationname:
    case DiagnosticsEngine::ak_nameddecl:
    case DiagnosticsEngine::ak_declcontext:
    case DiagnosticsEngine::ak_attr:
      getDiags()->ConvertArgToString(Kind, getRawArg(ArgNo),
                                     llvm::StringRef(Modifier, ModifierLen),
                                     llvm::StringRef(Argument, ArgumentLen),
                                     FormattedArgs, OutStr, QualTypeVals);
      break;
    case DiagnosticsEngine::ak_qualtype_pair: {
      TypeDiffInfo TDT;
      TDT.FromType = getRawArg(ArgNo);
      TDT.ToType = getRawArg(ArgNo2);
      intptr_t val = reinterpret_cast<intptr_t>(&TDT);

      const char *ArgumentEnd = Argument + ArgumentLen;
      const char *Pipe = advanceFormatTo(Argument, ArgumentEnd, '|');
      const char *FirstDollar = advanceFormatTo(Argument, ArgumentEnd, '$');
      const char *SecondDollar =
          advanceFormatTo(FirstDollar + 1, ArgumentEnd, '$');

      // Append before text
      FormatDiagnostic(Argument, FirstDollar, OutStr);

      // Append first type
      TDT.PrintFromType = true;
      getDiags()->ConvertArgToString(Kind, val,
                                     llvm::StringRef(Modifier, ModifierLen),
                                     llvm::StringRef(Argument, ArgumentLen),
                                     FormattedArgs, OutStr, QualTypeVals);
      FormattedArgs.push_back(
          std::make_pair(DiagnosticsEngine::ak_qualtype, TDT.FromType));

      // Append middle text
      FormatDiagnostic(FirstDollar + 1, SecondDollar, OutStr);

      // Append second type
      TDT.PrintFromType = false;
      getDiags()->ConvertArgToString(Kind, val,
                                     llvm::StringRef(Modifier, ModifierLen),
                                     llvm::StringRef(Argument, ArgumentLen),
                                     FormattedArgs, OutStr, QualTypeVals);
      FormattedArgs.push_back(
          std::make_pair(DiagnosticsEngine::ak_qualtype, TDT.ToType));

      // Append end text
      FormatDiagnostic(SecondDollar + 1, Pipe, OutStr);
      break;
    }
    }

    // Remember this argument info for subsequent formatting operations.  Turn
    // std::strings into a null terminated string to make it be the same case as
    // all the other ones.
    if (Kind == DiagnosticsEngine::ak_qualtype_pair)
      continue;
    else if (Kind != DiagnosticsEngine::ak_std_string)
      FormattedArgs.push_back(std::make_pair(Kind, getRawArg(ArgNo)));
    else
      FormattedArgs.push_back(
          std::make_pair(DiagnosticsEngine::ak_c_string,
                         (intptr_t)getArgStdStr(ArgNo).c_str()));
  }

  // Append the type tree to the end of the diagnostics.
  OutStr.append(Tree.begin(), Tree.end());
}

StoredDiagnostic::StoredDiagnostic(DiagnosticsEngine::Level Level, unsigned ID,
                                   llvm::StringRef Message)
    : ID(ID), Level(Level), Message(Message) {}

StoredDiagnostic::StoredDiagnostic(DiagnosticsEngine::Level Level,
                                   const Diagnostic &Info)
    : ID(Info.getID()), Level(Level) {
  assert(
      (Info.getLocation().isInvalid() || Info.hasSourceManager()) &&
      "Valid source location without setting a source manager for diagnostic");
  if (Info.getLocation().isValid())
    Loc = FullSourceLoc(Info.getLocation(), Info.getSourceManager());
  llvm::SmallString<64> Message;
  Info.FormatDiagnostic(Message);
  this->Message.assign(Message.begin(), Message.end());
  this->Ranges.assign(Info.getRanges().begin(), Info.getRanges().end());
  this->FixIts.assign(Info.getFixItHints().begin(), Info.getFixItHints().end());
}

StoredDiagnostic::StoredDiagnostic(DiagnosticsEngine::Level Level, unsigned ID,
                                   llvm::StringRef Message, FullSourceLoc Loc,
                                   llvm::ArrayRef<CharSourceRange> Ranges,
                                   llvm::ArrayRef<FixItHint> FixIts)
    : ID(ID), Level(Level), Loc(Loc), Message(Message),
      Ranges(Ranges.begin(), Ranges.end()),
      FixIts(FixIts.begin(), FixIts.end()) {}

llvm::raw_ostream &neverc::operator<<(llvm::raw_ostream &OS,
                                      const StoredDiagnostic &SD) {
  if (SD.getLocation().hasManager())
    OS << SD.getLocation().printToString(SD.getLocation().getManager()) << ": ";
  OS << SD.getMessage();
  return OS;
}

bool DiagnosticConsumer::IncludeInDiagnosticCounts() const { return true; }

void IgnoringDiagConsumer::anchor() {}

ForwardingDiagnosticConsumer::~ForwardingDiagnosticConsumer() = default;

void ForwardingDiagnosticConsumer::ProcessDiagnostic(
    DiagnosticsEngine::Level DiagLevel, const Diagnostic &Info) {
  Target.ProcessDiagnostic(DiagLevel, Info);
}

void ForwardingDiagnosticConsumer::clear() {
  DiagnosticConsumer::clear();
  Target.clear();
}

bool ForwardingDiagnosticConsumer::IncludeInDiagnosticCounts() const {
  return Target.IncludeInDiagnosticCounts();
}

PartialDiagnostic::DiagStorageAllocator::DiagStorageAllocator() {
  for (unsigned I = 0; I != NumCached; ++I)
    FreeList[I] = Cached + I;
  NumFreeListEntries = NumCached;
}

PartialDiagnostic::DiagStorageAllocator::~DiagStorageAllocator() {
  // Don't assert if we are in a CrashRecovery context, as this invariant may
  // be invalidated during a crash.
  assert((NumFreeListEntries == NumCached ||
          llvm::CrashRecoveryContext::isRecoveringFromCrash()) &&
         "A partial is on the lam");
}

// ===----------------------------------------------------------------------===
// Stack safety
// ===----------------------------------------------------------------------===

namespace {
LLVM_THREAD_LOCAL void *BottomOfStack = nullptr;

void *getStackPointer() {
#if __GNUC__ || __has_builtin(__builtin_frame_address)
  return __builtin_frame_address(0);
#elif defined(_MSC_VER)
  return _AddressOfReturnAddress();
#else
  char CharOnStack = 0;
  char *volatile Ptr = &CharOnStack;
  return Ptr;
#endif
}
} // namespace

void neverc::noteBottomOfStack() {
  if (!BottomOfStack)
    BottomOfStack = getStackPointer();
}

bool neverc::isStackNearlyExhausted() {
  constexpr size_t SufficientStack = 256 << 10;
  if (!BottomOfStack)
    return false;

  intptr_t StackDiff = (intptr_t)getStackPointer() - (intptr_t)BottomOfStack;
  size_t StackUsage = (size_t)std::abs(StackDiff);

  if (StackUsage > DesiredStackSize)
    return false;

  return StackUsage >= DesiredStackSize - SufficientStack;
}

__attribute__((cold, noinline)) void
neverc::runWithSufficientStackSpaceSlow(llvm::function_ref<void()> Diag,
                                        llvm::function_ref<void()> Fn) {
  llvm::CrashRecoveryContext CRC;
  CRC.RunSafelyOnThread(
      [&] {
        noteBottomOfStack();
        Diag();
        Fn();
      },
      DesiredStackSize);
}

// ===----------------------------------------------------------------------===
// Diagnostic info tables & lookup
// ===----------------------------------------------------------------------===

namespace {

struct StaticDiagInfoRec;

// Store the descriptions in a separate table to avoid pointers that need to
// be relocated, and also decrease the amount of data needed on 64-bit
// platforms. See "How To Write Shared Libraries" by Ulrich Drepper.
struct StaticDiagInfoDescriptionStringTable {
#define DIAG(ENUM, CLASS, DEFAULT_SEVERITY, DESC, GROUP, NOWERROR,             \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  char ENUM##_desc[sizeof(DESC)];
  // clang-format off
#include "neverc/Foundation/DiagnosticCommonKinds.td.h"
#include "neverc/Foundation/DiagnosticDriverKinds.td.h"
#include "neverc/Foundation/DiagnosticFrontendKinds.td.h"

#include "neverc/Foundation/DiagnosticLexKinds.td.h"
#include "neverc/Foundation/DiagnosticParseKinds.td.h"
#include "neverc/Foundation/DiagnosticASTKinds.td.h"
#include "neverc/Foundation/DiagnosticSemaKinds.td.h"
  // clang-format on
#undef DIAG
};

const StaticDiagInfoDescriptionStringTable StaticDiagInfoDescriptions = {
#define DIAG(ENUM, CLASS, DEFAULT_SEVERITY, DESC, GROUP, NOWERROR,             \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  DESC,
// clang-format off
#include "neverc/Foundation/DiagnosticCommonKinds.td.h"
#include "neverc/Foundation/DiagnosticDriverKinds.td.h"
#include "neverc/Foundation/DiagnosticFrontendKinds.td.h"

#include "neverc/Foundation/DiagnosticLexKinds.td.h"
#include "neverc/Foundation/DiagnosticParseKinds.td.h"
#include "neverc/Foundation/DiagnosticASTKinds.td.h"
#include "neverc/Foundation/DiagnosticSemaKinds.td.h"
// clang-format on
#undef DIAG
};

extern const StaticDiagInfoRec StaticDiagInfo[];

// Stored separately from StaticDiagInfoRec to pack better.  Otherwise,
// StaticDiagInfoRec would have extra padding on 64-bit platforms.
const uint32_t StaticDiagInfoDescriptionOffsets[] = {
#define DIAG(ENUM, CLASS, DEFAULT_SEVERITY, DESC, GROUP, NOWERROR,             \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  offsetof(StaticDiagInfoDescriptionStringTable, ENUM##_desc),
// clang-format off
#include "neverc/Foundation/DiagnosticCommonKinds.td.h"
#include "neverc/Foundation/DiagnosticDriverKinds.td.h"
#include "neverc/Foundation/DiagnosticFrontendKinds.td.h"

#include "neverc/Foundation/DiagnosticLexKinds.td.h"
#include "neverc/Foundation/DiagnosticParseKinds.td.h"
#include "neverc/Foundation/DiagnosticASTKinds.td.h"
#include "neverc/Foundation/DiagnosticSemaKinds.td.h"
// clang-format on
#undef DIAG
};

// Diagnostic classes.
enum {
  CLASS_NOTE = 0x01,
  CLASS_REMARK = 0x02,
  CLASS_WARNING = 0x03,
  CLASS_EXTENSION = 0x04,
  CLASS_ERROR = 0x05
};

struct StaticDiagInfoRec {
  uint16_t DiagID;
  uint8_t DefaultSeverity : 3;
  uint8_t Class : 3;
  uint8_t Category : 6;
  uint8_t WarnNoWerror : 1;
  uint8_t WarnShowInSystemHeader : 1;
  uint8_t WarnShowInSystemMacro : 1;

  uint16_t OptionGroupIndex : 15;
  uint16_t Deferrable : 1;

  uint16_t DescriptionLen;

  unsigned getOptionGroupIndex() const { return OptionGroupIndex; }

  llvm::StringRef getDescription() const {
    size_t MyIndex = this - &StaticDiagInfo[0];
    uint32_t StringOffset = StaticDiagInfoDescriptionOffsets[MyIndex];
    const char *Table =
        reinterpret_cast<const char *>(&StaticDiagInfoDescriptions);
    return llvm::StringRef(&Table[StringOffset], DescriptionLen);
  }

  diag::Flavor getFlavor() const {
    return Class == CLASS_REMARK ? diag::Flavor::Remark
                                 : diag::Flavor::WarningOrError;
  }

  bool operator<(const StaticDiagInfoRec &RHS) const {
    return DiagID < RHS.DiagID;
  }
};

#define STRINGIFY_NAME(NAME) #NAME
#define VALIDATE_DIAG_SIZE(NAME)                                               \
  static_assert(                                                               \
      static_cast<unsigned>(diag::NUM_BUILTIN_##NAME##_DIAGNOSTICS) <          \
          static_cast<unsigned>(diag::DIAG_START_##NAME) +                     \
              static_cast<unsigned>(diag::DIAG_SIZE_##NAME),                   \
      STRINGIFY_NAME(                                                          \
          DIAG_SIZE_##NAME) " is insufficient to contain all "                 \
                            "diagnostics, it may need to be made larger in "   \
                            "DiagnosticIDs.h.");
VALIDATE_DIAG_SIZE(COMMON)
VALIDATE_DIAG_SIZE(DRIVER)
VALIDATE_DIAG_SIZE(FRONTEND)
VALIDATE_DIAG_SIZE(LEX)
VALIDATE_DIAG_SIZE(PARSE)
VALIDATE_DIAG_SIZE(AST)
VALIDATE_DIAG_SIZE(SEMA)
#undef VALIDATE_DIAG_SIZE
#undef STRINGIFY_NAME

const StaticDiagInfoRec StaticDiagInfo[] = {
// clang-format off
#define DIAG(ENUM, CLASS, DEFAULT_SEVERITY, DESC, GROUP, NOWERROR,              \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  {                                                                            \
      diag::ENUM,                                                              \
      DEFAULT_SEVERITY,                                                        \
      CLASS,                                                                   \
      CATEGORY,                                                                \
      NOWERROR,                                                                \
      SHOWINSYSHEADER,                                                         \
      SHOWINSYSMACRO,                                                          \
      GROUP,                                                                   \
	    DEFERRABLE,                                                              \
      STR_SIZE(DESC, uint16_t)},
#include "neverc/Foundation/DiagnosticCommonKinds.td.h"
#include "neverc/Foundation/DiagnosticDriverKinds.td.h"
#include "neverc/Foundation/DiagnosticFrontendKinds.td.h"

#include "neverc/Foundation/DiagnosticLexKinds.td.h"
#include "neverc/Foundation/DiagnosticParseKinds.td.h"
#include "neverc/Foundation/DiagnosticASTKinds.td.h"
#include "neverc/Foundation/DiagnosticSemaKinds.td.h"
// clang-format on
#undef DIAG
};

const unsigned StaticDiagInfoSize = std::size(StaticDiagInfo);

const StaticDiagInfoRec *FindDiagRecord(unsigned DiagID) {
  // Out of bounds diag. Can't be in the table.
  using namespace diag;
  if (DiagID >= DIAG_UPPER_LIMIT || DiagID <= DIAG_START_COMMON)
    return nullptr;

  // Compute the index of the requested diagnostic in the static table.
  // 1. Add the number of diagnostics in each category preceding the
  //    diagnostic and of the category the diagnostic is in. This gives us
  //    the offset of the category in the table.
  // 2. Subtract the number of IDs in each category from our ID. This gives us
  //    the offset of the diagnostic in the category.
  // This is cheaper than a binary search on the table as it doesn't touch
  // memory at all.
  unsigned Offset = 0;
  unsigned ID = DiagID - DIAG_START_COMMON - 1;
#define CATEGORY(NAME, PREV)                                                   \
  if (DiagID > DIAG_START_##NAME) {                                            \
    Offset += NUM_BUILTIN_##PREV##_DIAGNOSTICS - DIAG_START_##PREV - 1;        \
    ID -= DIAG_START_##NAME - DIAG_START_##PREV;                               \
  }
  CATEGORY(DRIVER, COMMON)
  CATEGORY(FRONTEND, DRIVER)
  CATEGORY(LEX, FRONTEND)
  CATEGORY(PARSE, LEX)
  CATEGORY(AST, PARSE)
  CATEGORY(SEMA, AST)
#undef CATEGORY

  // Avoid out of bounds reads.
  if (ID + Offset >= StaticDiagInfoSize)
    return nullptr;

  assert(ID < StaticDiagInfoSize && Offset < StaticDiagInfoSize);

  const StaticDiagInfoRec *Found = &StaticDiagInfo[ID + Offset];
  // If the diag id doesn't match we found a different diag, abort. This can
  // happen when this function is called with an ID that points into a hole in
  // the diagID space.
  if (Found->DiagID != DiagID)
    return nullptr;
  return Found;
}

} // namespace

// ===----------------------------------------------------------------------===
// DiagnosticIDs
// ===----------------------------------------------------------------------===

DiagnosticMapping DiagnosticIDs::getDefaultMapping(unsigned DiagID) {
  DiagnosticMapping Info = DiagnosticMapping::Make(
      diag::Severity::Fatal, /*IsUser=*/false, /*IsPragma=*/false);

  if (const StaticDiagInfoRec *StaticInfo = FindDiagRecord(DiagID)) {
    Info.setSeverity((diag::Severity)StaticInfo->DefaultSeverity);

    if (StaticInfo->WarnNoWerror) {
      assert(Info.getSeverity() == diag::Severity::Warning &&
             "Unexpected mapping with no-Werror bit!");
      Info.setNoWarningAsError(true);
    }
  }

  return Info;
}

unsigned DiagnosticIDs::getCategoryNumberForDiag(unsigned DiagID) {
  if (const StaticDiagInfoRec *Info = FindDiagRecord(DiagID))
    return Info->Category;
  return 0;
}

namespace {
struct StaticDiagCategoryRec {
  const char *NameStr;
  uint8_t NameLen;

  llvm::StringRef getName() const { return llvm::StringRef(NameStr, NameLen); }
};

const StaticDiagCategoryRec CategoryNameTable[] = {
#define GET_CATEGORY_TABLE
#define CATEGORY(X, ENUM) {X, STR_SIZE(X, uint8_t)},
#include "neverc/Foundation/DiagnosticGroups.td.h"
#undef GET_CATEGORY_TABLE
    {nullptr, 0}};
} // namespace

unsigned DiagnosticIDs::getNumberOfCategories() {
  return std::size(CategoryNameTable) - 1;
}

llvm::StringRef DiagnosticIDs::getCategoryNameFromID(unsigned CategoryID) {
  if (CategoryID >= getNumberOfCategories())
    return llvm::StringRef();
  return CategoryNameTable[CategoryID].getName();
}

bool DiagnosticIDs::isDeferrable(unsigned DiagID) {
  if (const StaticDiagInfoRec *Info = FindDiagRecord(DiagID))
    return Info->Deferrable;
  return false;
}

namespace {
unsigned queryBuiltinDiagClass(unsigned DiagID) {
  if (const StaticDiagInfoRec *Info = FindDiagRecord(DiagID))
    return Info->Class;
  return ~0U;
}
} // namespace

namespace neverc {
namespace diag {
class CustomDiagInfo {
  typedef std::pair<DiagnosticIDs::Level, std::string> DiagDesc;
  std::vector<DiagDesc> DiagInfo;
  std::map<DiagDesc, unsigned> DiagIDs;

public:
  llvm::StringRef getDescription(unsigned DiagID) const {
    assert(DiagID - DIAG_UPPER_LIMIT < DiagInfo.size() &&
           "Invalid diagnostic ID");
    return DiagInfo[DiagID - DIAG_UPPER_LIMIT].second;
  }

  DiagnosticIDs::Level getLevel(unsigned DiagID) const {
    assert(DiagID - DIAG_UPPER_LIMIT < DiagInfo.size() &&
           "Invalid diagnostic ID");
    return DiagInfo[DiagID - DIAG_UPPER_LIMIT].first;
  }

  unsigned getOrCreateDiagID(DiagnosticIDs::Level L, llvm::StringRef Message,
                             DiagnosticIDs &Diags) {
    DiagDesc D(L, std::string(Message));
    std::map<DiagDesc, unsigned>::iterator I = DiagIDs.lower_bound(D);
    if (I != DiagIDs.end() && I->first == D)
      return I->second;

    // If not, assign a new ID.
    unsigned ID = DiagInfo.size() + DIAG_UPPER_LIMIT;
    DiagIDs.insert(std::make_pair(D, ID));
    DiagInfo.push_back(D);
    return ID;
  }
};

} // namespace diag
} // namespace neverc

DiagnosticIDs::DiagnosticIDs() {}

DiagnosticIDs::~DiagnosticIDs() {}

unsigned DiagnosticIDs::getCustomDiagID(Level L, llvm::StringRef FormatString) {
  if (!CustomDiagInfo)
    CustomDiagInfo.reset(new diag::CustomDiagInfo());
  return CustomDiagInfo->getOrCreateDiagID(L, FormatString, *this);
}

bool DiagnosticIDs::isBuiltinWarningOrExtension(unsigned DiagID) {
  return DiagID < diag::DIAG_UPPER_LIMIT &&
         queryBuiltinDiagClass(DiagID) != CLASS_ERROR;
}

bool DiagnosticIDs::isBuiltinNote(unsigned DiagID) {
  return DiagID < diag::DIAG_UPPER_LIMIT &&
         queryBuiltinDiagClass(DiagID) == CLASS_NOTE;
}

bool DiagnosticIDs::isBuiltinExtensionDiag(unsigned DiagID,
                                           bool &EnabledByDefault) {
  if (DiagID >= diag::DIAG_UPPER_LIMIT ||
      queryBuiltinDiagClass(DiagID) != CLASS_EXTENSION)
    return false;

  EnabledByDefault =
      getDefaultMapping(DiagID).getSeverity() != diag::Severity::Ignored;
  return true;
}

bool DiagnosticIDs::isDefaultMappingAsError(unsigned DiagID) {
  if (DiagID >= diag::DIAG_UPPER_LIMIT)
    return false;

  return getDefaultMapping(DiagID).getSeverity() >= diag::Severity::Error;
}

llvm::StringRef DiagnosticIDs::getDescription(unsigned DiagID) const {
  if (const StaticDiagInfoRec *Info = FindDiagRecord(DiagID))
    return Info->getDescription();
  assert(CustomDiagInfo && "Invalid CustomDiagInfo");
  return CustomDiagInfo->getDescription(DiagID);
}

namespace {
DiagnosticIDs::Level toLevel(diag::Severity SV) {
  switch (SV) {
  case diag::Severity::Ignored:
    return DiagnosticIDs::Ignored;
  case diag::Severity::Remark:
    return DiagnosticIDs::Remark;
  case diag::Severity::Warning:
    return DiagnosticIDs::Warning;
  case diag::Severity::Error:
    return DiagnosticIDs::Error;
  case diag::Severity::Fatal:
    return DiagnosticIDs::Fatal;
  }
  llvm_unreachable("unexpected severity");
}
} // namespace

DiagnosticIDs::Level
DiagnosticIDs::getDiagnosticLevel(unsigned DiagID, SourceLocation Loc,
                                  const DiagnosticsEngine &Diag) const {
  if (DiagID >= diag::DIAG_UPPER_LIMIT) {
    assert(CustomDiagInfo && "Invalid CustomDiagInfo");
    return CustomDiagInfo->getLevel(DiagID);
  }

  unsigned DiagClass = queryBuiltinDiagClass(DiagID);
  if (DiagClass == CLASS_NOTE)
    return DiagnosticIDs::Note;
  return toLevel(getDiagnosticSeverity(DiagID, Loc, Diag));
}

diag::Severity
DiagnosticIDs::getDiagnosticSeverity(unsigned DiagID, SourceLocation Loc,
                                     const DiagnosticsEngine &Diag) const {
  assert(queryBuiltinDiagClass(DiagID) != CLASS_NOTE);

  DiagnosticsEngine::DiagState *State = Diag.GetDiagStateForLoc(Loc);

  // Under -w, WARNING-class diagnostics are always ignored regardless of any
  // mapping or promotion. Skip the expensive DenseMap lookup entirely.
  if (State->IgnoreAllWarnings &&
      queryBuiltinDiagClass(DiagID) == CLASS_WARNING)
    return diag::Severity::Ignored;

  // Specific non-error diagnostics may be mapped to various levels from ignored
  // to error.  Errors can only be mapped to fatal.
  diag::Severity Result = diag::Severity::Fatal;
  DiagnosticMapping &Mapping = State->getOrAddMapping((diag::kind)DiagID);

  if (Mapping.getSeverity() != diag::Severity())
    Result = Mapping.getSeverity();

  // Upgrade ignored diagnostics if -Weverything is enabled.
  if (State->EnableAllWarnings && Result == diag::Severity::Ignored &&
      !Mapping.isUser() && queryBuiltinDiagClass(DiagID) != CLASS_REMARK)
    Result = diag::Severity::Warning;

  // Ignore -pedantic diagnostics inside __extension__ blocks.
  // (The diagnostics controlled by -pedantic are the extension diagnostics
  // that are not enabled by default.)
  bool EnabledByDefault = false;
  bool IsExtensionDiag = isBuiltinExtensionDiag(DiagID, EnabledByDefault);
  if (Diag.AllExtensionsSilenced && IsExtensionDiag && !EnabledByDefault)
    return diag::Severity::Ignored;

  // For extension diagnostics that haven't been explicitly mapped, check if we
  // should upgrade the diagnostic.
  if (IsExtensionDiag && !Mapping.isUser())
    Result = std::max(Result, State->ExtBehavior);

  // At this point, ignored errors can no longer be upgraded.
  if (Result == diag::Severity::Ignored)
    return Result;

  // Honor -w: this disables all messages which are not Error/Fatal by
  // default (disregarding attempts to upgrade severity from Warning to Error),
  // as well as disabling all messages which are currently mapped to Warning
  // (whether by default or downgraded from Error via e.g. -Wno-error or #pragma
  // diagnostic.)
  if (State->IgnoreAllWarnings) {
    if (Result == diag::Severity::Warning ||
        (Result >= diag::Severity::Error &&
         !isDefaultMappingAsError((diag::kind)DiagID)))
      return diag::Severity::Ignored;
  }

  // If -Werror is enabled, map warnings to errors unless explicitly disabled.
  if (Result == diag::Severity::Warning) {
    if (State->WarningsAsErrors && !Mapping.hasNoWarningAsError())
      Result = diag::Severity::Error;
  }

  // If -Wfatal-errors is enabled, map errors to fatal unless explicitly
  // disabled.
  if (Result == diag::Severity::Error) {
    if (State->ErrorsAsFatal && !Mapping.hasNoErrorAsFatal())
      Result = diag::Severity::Fatal;
  }

  // If explicitly requested, map fatal errors to errors.
  if (Result == diag::Severity::Fatal &&
      Diag.CurDiagID != diag::fatal_too_many_errors && Diag.FatalsAsError)
    Result = diag::Severity::Error;

  // Custom diagnostics always are emitted in system headers.
  bool ShowInSystemHeader =
      !FindDiagRecord(DiagID) || FindDiagRecord(DiagID)->WarnShowInSystemHeader;

  // If we are in a system header, we ignore it. We look at the diagnostic class
  // because we also want to ignore extensions and warnings in -Werror and
  // -pedantic-errors modes, which *map* warnings/extensions to errors.
  if (State->SuppressSystemWarnings && !ShowInSystemHeader && Loc.isValid() &&
      Diag.getSourceManager().isInSystemHeader(
          Diag.getSourceManager().getExpansionLoc(Loc)))
    return diag::Severity::Ignored;

  // We also ignore warnings due to system macros
  bool ShowInSystemMacro =
      !FindDiagRecord(DiagID) || FindDiagRecord(DiagID)->WarnShowInSystemMacro;
  if (State->SuppressSystemWarnings && !ShowInSystemMacro && Loc.isValid() &&
      Diag.getSourceManager().isInSystemMacro(Loc))
    return diag::Severity::Ignored;

  return Result;
}

#define GET_DIAG_ARRAYS
#include "neverc/Foundation/DiagnosticGroups.td.h"
#undef GET_DIAG_ARRAYS

namespace {
struct WarningOption {
  uint16_t NameOffset;
  uint16_t Members;
  uint16_t SubGroups;
  llvm::StringRef Documentation;

  llvm::StringRef getName() const {
    return llvm::StringRef(DiagGroupNames + NameOffset + 1,
                           DiagGroupNames[NameOffset]);
  }
};

const WarningOption OptionTable[] = {
#define DIAG_ENTRY(GroupName, FlagNameOffset, Members, SubGroups, Docs)        \
  {FlagNameOffset, Members, SubGroups, Docs},
#include "neverc/Foundation/DiagnosticGroups.td.h"
#undef DIAG_ENTRY
};
} // namespace

llvm::StringRef
DiagnosticIDs::getWarningOptionDocumentation(diag::Group Group) {
  return OptionTable[static_cast<int>(Group)].Documentation;
}

llvm::StringRef DiagnosticIDs::getWarningOptionForGroup(diag::Group Group) {
  return OptionTable[static_cast<int>(Group)].getName();
}

std::optional<diag::Group>
DiagnosticIDs::getGroupForWarningOption(llvm::StringRef Name) {
  const auto *Found = llvm::partition_point(
      OptionTable, [=](const WarningOption &O) { return O.getName() < Name; });
  if (Found == std::end(OptionTable) || Found->getName() != Name)
    return std::nullopt;
  return static_cast<diag::Group>(Found - OptionTable);
}

std::optional<diag::Group> DiagnosticIDs::getGroupForDiag(unsigned DiagID) {
  if (const StaticDiagInfoRec *Info = FindDiagRecord(DiagID))
    return static_cast<diag::Group>(Info->getOptionGroupIndex());
  return std::nullopt;
}

llvm::StringRef DiagnosticIDs::getWarningOptionForDiag(unsigned DiagID) {
  if (auto G = getGroupForDiag(DiagID))
    return getWarningOptionForGroup(*G);
  return llvm::StringRef();
}

std::vector<std::string> DiagnosticIDs::getDiagnosticFlags() {
  std::vector<std::string> Res{"-W", "-Wno-"};
  for (size_t I = 1; DiagGroupNames[I] != '\0';) {
    std::string Diag(DiagGroupNames + I + 1, DiagGroupNames[I]);
    I += DiagGroupNames[I] + 1;
    Res.push_back("-W" + Diag);
    Res.push_back("-Wno-" + Diag);
  }

  return Res;
}

namespace {
bool getDiagnosticsInGroup(diag::Flavor Flavor, const WarningOption *Group,
                           llvm::SmallVectorImpl<diag::kind> &Diags) {
  // An empty group is considered to be a warning group: we have empty groups
  // for GCC compatibility, and GCC does not have remarks.
  if (!Group->Members && !Group->SubGroups)
    return Flavor == diag::Flavor::Remark;

  bool NotFound = true;

  // Add the members of the option diagnostic set.
  const int16_t *Member = DiagArrays + Group->Members;
  for (; *Member != -1; ++Member) {
    if (FindDiagRecord(*Member)->getFlavor() == Flavor) {
      NotFound = false;
      Diags.push_back(*Member);
    }
  }

  // Add the members of the subgroups.
  const int16_t *SubGroups = DiagSubGroups + Group->SubGroups;
  for (; *SubGroups != (int16_t)-1; ++SubGroups)
    NotFound &=
        getDiagnosticsInGroup(Flavor, &OptionTable[(short)*SubGroups], Diags);

  return NotFound;
}
} // namespace

bool DiagnosticIDs::getDiagnosticsInGroup(
    diag::Flavor Flavor, llvm::StringRef Group,
    llvm::SmallVectorImpl<diag::kind> &Diags) const {
  if (std::optional<diag::Group> G = getGroupForWarningOption(Group))
    return ::getDiagnosticsInGroup(
        Flavor, &OptionTable[static_cast<unsigned>(*G)], Diags);
  return true;
}

void DiagnosticIDs::getAllDiagnostics(diag::Flavor Flavor,
                                      std::vector<diag::kind> &Diags) {
  for (unsigned i = 0; i != StaticDiagInfoSize; ++i)
    if (StaticDiagInfo[i].getFlavor() == Flavor)
      Diags.push_back(StaticDiagInfo[i].DiagID);
}

llvm::StringRef DiagnosticIDs::getNearestOption(diag::Flavor Flavor,
                                                llvm::StringRef Group) {
  llvm::StringRef Best;
  unsigned BestDistance = Group.size() + 1; // Maximum threshold.
  for (const WarningOption &O : OptionTable) {
    // Don't suggest ignored warning flags.
    if (!O.Members && !O.SubGroups)
      continue;

    unsigned Distance = O.getName().edit_distance(Group, true, BestDistance);
    if (Distance > BestDistance)
      continue;

    // Don't suggest groups that are not of this kind.
    llvm::SmallVector<diag::kind, 8> Diags;
    if (::getDiagnosticsInGroup(Flavor, &O, Diags) || Diags.empty())
      continue;

    if (Distance == BestDistance) {
      // Two matches with the same distance, don't prefer one over the other.
      Best = "";
    } else if (Distance < BestDistance) {
      // This is a better match.
      Best = O.getName();
      BestDistance = Distance;
    }
  }

  return Best;
}

bool DiagnosticIDs::ProcessDiag(DiagnosticsEngine &Diag) const {
  Diagnostic Info(&Diag);

  assert(Diag.getClient() && "DiagnosticClient not set!");

  // Figure out the diagnostic level of this message.
  unsigned DiagID = Info.getID();
  DiagnosticIDs::Level DiagLevel =
      getDiagnosticLevel(DiagID, Info.getLocation(), Diag);

  // Update counts for DiagnosticErrorTrap even if a fatal error occurred
  // or diagnostics are suppressed.
  if (DiagLevel >= DiagnosticIDs::Error) {
    ++Diag.TrapNumErrorsOccurred;
    if (isUnrecoverable(DiagID))
      ++Diag.TrapNumUnrecoverableErrorsOccurred;
  }

  if (Diag.SuppressAllDiagnostics)
    return false;

  if (DiagLevel != DiagnosticIDs::Note) {
    // Record that a fatal error occurred only when we see a second
    // non-note diagnostic. This allows notes to be attached to the
    // fatal error, but suppresses any diagnostics that follow those
    // notes.
    if (Diag.LastDiagLevel == DiagnosticIDs::Fatal)
      Diag.FatalErrorOccurred = true;

    Diag.LastDiagLevel = DiagLevel;
  }

  // If a fatal error has already been emitted, silence all subsequent
  // diagnostics.
  if (Diag.FatalErrorOccurred) {
    if (DiagLevel >= DiagnosticIDs::Error &&
        Diag.Client->IncludeInDiagnosticCounts()) {
      ++Diag.NumErrors;
    }

    return false;
  }

  // If the client doesn't care about this message, don't issue it.  If this is
  // a note and the last real diagnostic was ignored, ignore it too.
  if (DiagLevel == DiagnosticIDs::Ignored ||
      (DiagLevel == DiagnosticIDs::Note &&
       Diag.LastDiagLevel == DiagnosticIDs::Ignored))
    return false;

  if (DiagLevel >= DiagnosticIDs::Error) {
    if (isUnrecoverable(DiagID))
      Diag.UnrecoverableErrorOccurred = true;

    // Warnings which have been upgraded to errors do not prevent compilation.
    if (isDefaultMappingAsError(DiagID))
      Diag.UncompilableErrorOccurred = true;

    Diag.ErrorOccurred = true;
    if (Diag.Client->IncludeInDiagnosticCounts()) {
      ++Diag.NumErrors;
    }

    // If we've emitted a lot of errors, emit a fatal error instead of it to
    // stop a flood of bogus errors.
    if (Diag.ErrorLimit && Diag.NumErrors > Diag.ErrorLimit &&
        DiagLevel == DiagnosticIDs::Error) {
      Diag.SetDelayedDiagnostic(diag::fatal_too_many_errors);
      return false;
    }
  }

  // Make sure we set FatalErrorOccurred to ensure that the notes from the
  // diagnostic that caused `fatal_too_many_errors` won't be emitted.
  if (Diag.CurDiagID == diag::fatal_too_many_errors)
    Diag.FatalErrorOccurred = true;
  // Finally, report it.
  GenDiag(Diag, DiagLevel);
  return true;
}

__attribute__((cold, noinline)) void
DiagnosticIDs::GenDiag(DiagnosticsEngine &Diag, Level DiagLevel) const {
  Diagnostic Info(&Diag);
  assert(DiagLevel != DiagnosticIDs::Ignored &&
         "Cannot emit ignored diagnostics!");

  Diag.Client->ProcessDiagnostic((DiagnosticsEngine::Level)DiagLevel, Info);
  if (Diag.Client->IncludeInDiagnosticCounts()) {
    if (DiagLevel == DiagnosticIDs::Warning)
      ++Diag.NumWarnings;
  }

  Diag.CurDiagID = ~0U;
}

bool DiagnosticIDs::isUnrecoverable(unsigned DiagID) const {
  if (DiagID >= diag::DIAG_UPPER_LIMIT) {
    assert(CustomDiagInfo && "Invalid CustomDiagInfo");
    // Custom diagnostics.
    return CustomDiagInfo->getLevel(DiagID) >= DiagnosticIDs::Error;
  }

  // Only errors may be unrecoverable.
  if (queryBuiltinDiagClass(DiagID) < CLASS_ERROR)
    return false;

  if (DiagID == diag::err_unavailable ||
      DiagID == diag::err_unavailable_message)
    return false;

  return true;
}
namespace {
void diagnoseUnknownDiagOption(DiagnosticsEngine &Diags, diag::Flavor Flavor,
                               llvm::StringRef Prefix, llvm::StringRef Opt) {
  llvm::StringRef Suggestion = DiagnosticIDs::getNearestOption(Flavor, Opt);
  Diags.Report(diag::warn_unknown_diag_option)
      << (Flavor == diag::Flavor::WarningOrError ? 0 : 1)
      << (Prefix.str() += std::string(Opt)) << !Suggestion.empty()
      << (Prefix.str() += std::string(Suggestion));
}
} // namespace

// ===----------------------------------------------------------------------===
// Warning option processing
// ===----------------------------------------------------------------------===

void neverc::ProcessWarningOptions(DiagnosticsEngine &Diags,
                                   const DiagnosticOptions &Opts,
                                   bool ReportDiags) {
  Diags.setSuppressSystemWarnings(true); // Default to -Wno-system-headers
  Diags.setIgnoreAllWarnings(Opts.IgnoreWarnings);

  Diags.setShowColors(Opts.ShowColors);

  if (Opts.ErrorLimit)
    Diags.setErrorLimit(Opts.ErrorLimit);
  if (Opts.ConstexprBacktraceLimit)
    Diags.setConstexprBacktraceLimit(Opts.ConstexprBacktraceLimit);

  // If -pedantic or -pedantic-errors was specified, then we want to map all
  // extension diagnostics onto WARNING or ERROR unless the user has futz'd
  // around with them explicitly.
  if (Opts.PedanticErrors)
    Diags.setExtensionHandlingBehavior(diag::Severity::Error);
  else if (Opts.Pedantic)
    Diags.setExtensionHandlingBehavior(diag::Severity::Warning);
  else
    Diags.setExtensionHandlingBehavior(diag::Severity::Ignored);

  llvm::SmallVector<diag::kind, 10> _Diags;
  const llvm::IntrusiveRefCntPtr<DiagnosticIDs> DiagIDs =
      Diags.getDiagnosticIDs();
  // We parse the warning options twice.  The first pass sets diagnostic state,
  // while the second pass reports warnings/errors.  This has the effect that
  // we follow the more canonical "last option wins" paradigm when there are
  // conflicting options.
  for (unsigned Report = 0, ReportEnd = 2; Report != ReportEnd; ++Report) {
    bool SetDiagnostic = (Report == 0);

    // If we've set the diagnostic state and are not reporting diagnostics then
    // we're done.
    if (!SetDiagnostic && !ReportDiags)
      break;

    for (unsigned i = 0, e = Opts.Warnings.size(); i != e; ++i) {
      const auto Flavor = diag::Flavor::WarningOrError;
      llvm::StringRef Opt = Opts.Warnings[i];
      llvm::StringRef OrigOpt = Opts.Warnings[i];

      // Treat -Wformat=0 as an alias for -Wno-format.
      if (Opt == "format=0")
        Opt = "no-format";

      bool isPositive = !Opt.consume_front("no-");

      // Figure out how this option affects the warning.  If -Wfoo, map the
      // diagnostic to a warning, if -Wno-foo, map it to ignore.
      diag::Severity Mapping =
          isPositive ? diag::Severity::Warning : diag::Severity::Ignored;

      // -Wsystem-headers is a special case, not driven by the option table.  It
      // cannot be controlled with -Werror.
      if (Opt == "system-headers") {
        if (SetDiagnostic)
          Diags.setSuppressSystemWarnings(!isPositive);
        continue;
      }

      // -Weverything is a special case as well.  It implicitly enables all
      // warnings, including ones not explicitly in a warning group.
      if (Opt == "everything") {
        if (SetDiagnostic) {
          if (isPositive) {
            Diags.setEnableAllWarnings(true);
          } else {
            Diags.setEnableAllWarnings(false);
            Diags.setSeverityForAll(Flavor, diag::Severity::Ignored);
          }
        }
        continue;
      }

      // -Werror/-Wno-error is a special case, not controlled by the option
      // table. It also has the "specifier" form of -Werror=foo. GCC supports
      // the deprecated -Werror-implicit-function-declaration which is used by
      // a few projects.
#ifdef _WIN32
      if (Opt == "error")
        isPositive = false;
#endif
      if (Diags.TreatWarningsAsErrors) {
        isPositive = true;
        Diags.setWarningsAsErrors(isPositive);
      }
      if (Opt.starts_with("error")) {
        llvm::StringRef Specifier;
        if (Opt.size() > 5) { // Specifier must be present.
          if (Opt[5] != '=' &&
              Opt.substr(5) != "-implicit-function-declaration") {
            if (Report)
              Diags.Report(diag::warn_unknown_warning_specifier)
                  << "-Werror" << ("-W" + OrigOpt.str());
            continue;
          }
          Specifier = Opt.substr(6);
        }

        if (Specifier.empty()) {
          if (SetDiagnostic)
            Diags.setWarningsAsErrors(isPositive);
          continue;
        }

        if (SetDiagnostic) {
          Diags.setDiagnosticGroupWarningAsError(Specifier, isPositive);
        } else if (DiagIDs->getDiagnosticsInGroup(Flavor, Specifier, _Diags)) {
          diagnoseUnknownDiagOption(Diags, Flavor, "-Werror=", Specifier);
        }
        continue;
      }

      // -Wfatal-errors is yet another special case.
      if (Opt.starts_with("fatal-errors")) {
        llvm::StringRef Specifier;
        if (Opt.size() != 12) {
          if ((Opt[12] != '=' && Opt[12] != '-') || Opt.size() == 13) {
            if (Report)
              Diags.Report(diag::warn_unknown_warning_specifier)
                  << "-Wfatal-errors" << ("-W" + OrigOpt.str());
            continue;
          }
          Specifier = Opt.substr(13);
        }

        if (Specifier.empty()) {
          if (SetDiagnostic)
            Diags.setErrorsAsFatal(isPositive);
          continue;
        }

        if (SetDiagnostic) {
          Diags.setDiagnosticGroupErrorAsFatal(Specifier, isPositive);
        } else if (DiagIDs->getDiagnosticsInGroup(Flavor, Specifier, _Diags)) {
          diagnoseUnknownDiagOption(Diags, Flavor,
                                    "-Wfatal-errors=", Specifier);
        }
        continue;
      }

      if (Report) {
        if (DiagIDs->getDiagnosticsInGroup(Flavor, Opt, _Diags))
          diagnoseUnknownDiagOption(Diags, Flavor, isPositive ? "-W" : "-Wno-",
                                    Opt);
      } else {
        Diags.setSeverityForGroup(Flavor, Opt, Mapping);
      }
    }

    for (llvm::StringRef Opt : Opts.Remarks) {
      const auto Flavor = diag::Flavor::Remark;

      bool IsPositive = !Opt.starts_with("no-");
      if (!IsPositive)
        Opt = Opt.substr(3);

      auto Severity =
          IsPositive ? diag::Severity::Remark : diag::Severity::Ignored;

      // -Reverything sets the state of all remarks. Note that all remarks are
      // in remark groups, so we don't need a separate 'all remarks enabled'
      // flag.
      if (Opt == "everything") {
        if (SetDiagnostic)
          Diags.setSeverityForAll(Flavor, Severity);
        continue;
      }

      if (Report) {
        if (DiagIDs->getDiagnosticsInGroup(Flavor, Opt, _Diags))
          diagnoseUnknownDiagOption(Diags, Flavor, IsPositive ? "-R" : "-Rno-",
                                    Opt);
      } else {
        Diags.setSeverityForGroup(Flavor, Opt,
                                  IsPositive ? diag::Severity::Remark
                                             : diag::Severity::Ignored);
      }
    }
  }
}

std::optional<diag::Group>
neverc::diagGroupFromCLWarningID(unsigned CLWarningID) {
  switch (CLWarningID) {
  case 4005:
    return diag::Group::MacroRedefined;
  case 4018:
    return diag::Group::SignCompare;
  case 4100:
    return diag::Group::UnusedParameter;
  case 4996:
    return diag::Group::DeprecatedDeclarations;
  }
  return {};
}
