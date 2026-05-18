#include "neverc/Compiler/DiagnosticRenderer.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.h"
#include "neverc/Scan/SourceScanner.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>

using namespace neverc;

DiagnosticRenderer::DiagnosticRenderer(const LangOptions &LangOpts,
                                       DiagnosticOptions *DiagOpts)
    : LangOpts(LangOpts), DiagOpts(DiagOpts), LastLevel() {}

DiagnosticRenderer::~DiagnosticRenderer() = default;

namespace {

void mergeFixits(llvm::ArrayRef<FixItHint> FixItHints,
                 llvm::SmallVectorImpl<FixItHint> &MergedFixits) {
  MergedFixits.append(FixItHints.begin(), FixItHints.end());
}

} // namespace

// ===----------------------------------------------------------------------===
// Diagnostic emission
// ===----------------------------------------------------------------------===

void DiagnosticRenderer::emitDiagnostic(FullSourceLoc Loc,
                                        DiagnosticsEngine::Level Level,
                                        llvm::StringRef Message,
                                        llvm::ArrayRef<CharSourceRange> Ranges,
                                        llvm::ArrayRef<FixItHint> FixItHints,
                                        DiagOrStoredDiag D) {
  assert(Loc.hasManager() || Loc.isInvalid());

  beginDiagnostic(D, Level);

  if (!Loc.isValid()) {
    emitDiagnosticMessage(Loc, PresumedLoc(), Level, Message, Ranges, D);
  } else {
    llvm::SmallVector<CharSourceRange, 20> MutableRanges(Ranges.begin(),
                                                         Ranges.end());

    llvm::SmallVector<FixItHint, 8> MergedFixits;
    if (!FixItHints.empty()) {
      mergeFixits(FixItHints, MergedFixits);
      FixItHints = MergedFixits;
    }

    for (const auto &Hint : FixItHints)
      if (Hint.RemoveRange.isValid())
        MutableRanges.push_back(Hint.RemoveRange);

    FullSourceLoc UnexpandedLoc = Loc;
    Loc = Loc.getFileLoc();
    PresumedLoc PLoc = Loc.getPresumedLoc(DiagOpts->ShowPresumedLoc);

    emitIncludeStack(Loc, PLoc, Level);
    emitDiagnosticMessage(Loc, PLoc, Level, Message, Ranges, D);
    emitCaret(Loc, Level, MutableRanges, FixItHints);

    if (UnexpandedLoc.isValid() && UnexpandedLoc.isMacroID()) {
      emitMacroExpansions(UnexpandedLoc, Level, MutableRanges, FixItHints);
    }
  }

  LastLoc = Loc;
  LastLevel = Level;

  endDiagnostic(D, Level);
}

void DiagnosticRenderer::emitStoredDiagnostic(StoredDiagnostic &Diag) {
  emitDiagnostic(Diag.getLocation(), Diag.getLevel(), Diag.getMessage(),
                 Diag.getRanges(), Diag.getFixIts(), &Diag);
}

void DiagnosticRenderer::emitBasicNote(llvm::StringRef Message) {
  emitDiagnosticMessage(FullSourceLoc(), PresumedLoc(), DiagnosticsEngine::Note,
                        Message, std::nullopt, DiagOrStoredDiag());
}

void DiagnosticRenderer::emitIncludeStack(FullSourceLoc Loc, PresumedLoc PLoc,
                                          DiagnosticsEngine::Level Level) {
  FullSourceLoc IncludeLoc =
      PLoc.isInvalid() ? FullSourceLoc()
                       : FullSourceLoc(PLoc.getIncludeLoc(), Loc.getManager());

  if (LastIncludeLoc == IncludeLoc)
    return;

  LastIncludeLoc = IncludeLoc;

  if (!DiagOpts->ShowNoteIncludeStack && Level == DiagnosticsEngine::Note)
    return;

  if (IncludeLoc.isValid())
    emitIncludeStackRecursively(IncludeLoc);
}

void DiagnosticRenderer::emitIncludeStackRecursively(FullSourceLoc Loc) {
  if (Loc.isInvalid())
    return;

  PresumedLoc PLoc = Loc.getPresumedLoc(DiagOpts->ShowPresumedLoc);
  if (PLoc.isInvalid())
    return;

  emitIncludeStackRecursively(
      FullSourceLoc(PLoc.getIncludeLoc(), Loc.getManager()));

  emitIncludeLocation(Loc, PLoc);
}

// ===----------------------------------------------------------------------===
// Macro expansion helpers
// ===----------------------------------------------------------------------===

namespace {

SourceLocation retrieveMacroLocation(
    SourceLocation Loc, FileID MacroFileID, FileID CaretFileID,
    const llvm::SmallVectorImpl<FileID> &CommonArgExpansions, bool IsBegin,
    const SourceManager *SM, bool &IsTokenRange) {
  assert(SM->getFileID(Loc) == MacroFileID);
  if (MacroFileID == CaretFileID)
    return Loc;
  if (!Loc.isMacroID())
    return {};

  CharSourceRange MacroRange, MacroArgRange;

  if (SM->isMacroArgExpansion(Loc)) {
    if (llvm::is_sorted(CommonArgExpansions) &&
        std::binary_search(CommonArgExpansions.begin(),
                           CommonArgExpansions.end(), MacroFileID))
      MacroRange =
          CharSourceRange(SM->getImmediateSpellingLoc(Loc), IsTokenRange);
    MacroArgRange = SM->getImmediateExpansionRange(Loc);
  } else {
    MacroRange = SM->getImmediateExpansionRange(Loc);
    MacroArgRange =
        CharSourceRange(SM->getImmediateSpellingLoc(Loc), IsTokenRange);
  }

  SourceLocation MacroLocation =
      IsBegin ? MacroRange.getBegin() : MacroRange.getEnd();
  if (MacroLocation.isValid()) {
    MacroFileID = SM->getFileID(MacroLocation);
    bool TokenRange = IsBegin ? IsTokenRange : MacroRange.isTokenRange();
    MacroLocation =
        retrieveMacroLocation(MacroLocation, MacroFileID, CaretFileID,
                              CommonArgExpansions, IsBegin, SM, TokenRange);
    if (MacroLocation.isValid()) {
      IsTokenRange = TokenRange;
      return MacroLocation;
    }
  }

  // If we moved the end of the range to an expansion location, we now have
  // a range of the same kind as the expansion range.
  if (!IsBegin)
    IsTokenRange = MacroArgRange.isTokenRange();

  SourceLocation MacroArgLocation =
      IsBegin ? MacroArgRange.getBegin() : MacroArgRange.getEnd();
  MacroFileID = SM->getFileID(MacroArgLocation);
  return retrieveMacroLocation(MacroArgLocation, MacroFileID, CaretFileID,
                               CommonArgExpansions, IsBegin, SM, IsTokenRange);
}

void getMacroArgExpansionFileIDs(SourceLocation Loc,
                                 llvm::SmallVectorImpl<FileID> &IDs,
                                 bool IsBegin, const SourceManager *SM) {
  while (Loc.isMacroID()) {
    if (SM->isMacroArgExpansion(Loc)) {
      IDs.push_back(SM->getFileID(Loc));
      Loc = SM->getImmediateSpellingLoc(Loc);
    } else {
      auto ExpRange = SM->getImmediateExpansionRange(Loc);
      Loc = IsBegin ? ExpRange.getBegin() : ExpRange.getEnd();
    }
  }
}

void computeCommonMacroArgExpansionFileIDs(
    SourceLocation Begin, SourceLocation End, const SourceManager *SM,
    llvm::SmallVectorImpl<FileID> &CommonArgExpansions) {
  llvm::SmallVector<FileID, 4> BeginArgExpansions;
  llvm::SmallVector<FileID, 4> EndArgExpansions;
  getMacroArgExpansionFileIDs(Begin, BeginArgExpansions, /*IsBegin=*/true, SM);
  getMacroArgExpansionFileIDs(End, EndArgExpansions, /*IsBegin=*/false, SM);
  llvm::sort(BeginArgExpansions);
  llvm::sort(EndArgExpansions);
  std::set_intersection(BeginArgExpansions.begin(), BeginArgExpansions.end(),
                        EndArgExpansions.begin(), EndArgExpansions.end(),
                        std::back_inserter(CommonArgExpansions));
}

/// Map diagnostic ranges to spelling locations by walking macro caller chains.
/// Two locations are part of the same macro expansion iff their FileID matches.
void mapDiagnosticRanges(
    FullSourceLoc CaretLoc, llvm::ArrayRef<CharSourceRange> Ranges,
    llvm::SmallVectorImpl<CharSourceRange> &SpellingRanges) {
  FileID CaretLocFileID = CaretLoc.getFileID();

  const SourceManager *SM = &CaretLoc.getManager();

  for (const auto &Range : Ranges) {
    if (Range.isInvalid())
      continue;

    SourceLocation Begin = Range.getBegin(), End = Range.getEnd();
    bool IsTokenRange = Range.isTokenRange();

    FileID BeginFileID = SM->getFileID(Begin);
    FileID EndFileID = SM->getFileID(End);

    llvm::SmallDenseMap<FileID, SourceLocation> BeginLocsMap;
    while (Begin.isMacroID() && BeginFileID != EndFileID) {
      BeginLocsMap[BeginFileID] = Begin;
      Begin = SM->getImmediateExpansionRange(Begin).getBegin();
      BeginFileID = SM->getFileID(Begin);
    }

    if (BeginFileID != EndFileID) {
      while (End.isMacroID() && !BeginLocsMap.contains(EndFileID)) {
        auto Exp = SM->getImmediateExpansionRange(End);
        IsTokenRange = Exp.isTokenRange();
        End = Exp.getEnd();
        EndFileID = SM->getFileID(End);
      }
      if (End.isMacroID()) {
        Begin = BeginLocsMap[EndFileID];
        BeginFileID = EndFileID;
      }
    }

    // There is a chance that begin or end is invalid here, for example if
    // specific compile error is reported.
    // It is possible that the FileID's do not match, if one comes from an
    // included file. In this case we can not produce a meaningful source range.
    if (Begin.isInvalid() || End.isInvalid() || BeginFileID != EndFileID)
      continue;

    llvm::SmallVector<FileID, 4> CommonArgExpansions;
    computeCommonMacroArgExpansionFileIDs(Begin, End, SM, CommonArgExpansions);
    Begin = retrieveMacroLocation(Begin, BeginFileID, CaretLocFileID,
                                  CommonArgExpansions, /*IsBegin=*/true, SM,
                                  IsTokenRange);
    End = retrieveMacroLocation(End, BeginFileID, CaretLocFileID,
                                CommonArgExpansions, /*IsBegin=*/false, SM,
                                IsTokenRange);
    if (Begin.isInvalid() || End.isInvalid())
      continue;

    Begin = SM->getSpellingLoc(Begin);
    End = SM->getSpellingLoc(End);

    SpellingRanges.push_back(
        CharSourceRange(SourceRange(Begin, End), IsTokenRange));
  }
}

} // namespace

void DiagnosticRenderer::emitCaret(FullSourceLoc Loc,
                                   DiagnosticsEngine::Level Level,
                                   llvm::ArrayRef<CharSourceRange> Ranges,
                                   llvm::ArrayRef<FixItHint> Hints) {
  llvm::SmallVector<CharSourceRange, 4> SpellingRanges;
  mapDiagnosticRanges(Loc, Ranges, SpellingRanges);
  emitCodeContext(Loc, Level, SpellingRanges, Hints);
}

void DiagnosticRenderer::emitSingleMacroExpansion(
    FullSourceLoc Loc, DiagnosticsEngine::Level Level,
    llvm::ArrayRef<CharSourceRange> Ranges) {
  FullSourceLoc SpellingLoc = Loc.getSpellingLoc();

  llvm::SmallVector<CharSourceRange, 4> SpellingRanges;
  mapDiagnosticRanges(Loc, Ranges, SpellingRanges);

  llvm::SmallString<100> MessageStorage;
  llvm::raw_svector_ostream Message(MessageStorage);
  llvm::StringRef MacroName =
      SourceScanner::getImmediateMacroNameForDiagnostics(Loc, Loc.getManager(),
                                                         LangOpts);
  if (MacroName.empty())
    Message << "expanded from here";
  else
    Message << "expanded from macro '" << MacroName << "'";

  emitDiagnostic(SpellingLoc, DiagnosticsEngine::Note, Message.str(),
                 SpellingRanges, std::nullopt);
}

namespace {

bool checkLocForMacroArgExpansion(SourceLocation Loc, const SourceManager &SM,
                                  SourceLocation ArgumentLoc) {
  SourceLocation MacroLoc;
  if (SM.isMacroArgExpansion(Loc, &MacroLoc)) {
    if (ArgumentLoc == MacroLoc)
      return true;
  }

  return false;
}

bool checkRangeForMacroArgExpansion(CharSourceRange Range,
                                    const SourceManager &SM,
                                    SourceLocation ArgumentLoc) {
  SourceLocation BegLoc = Range.getBegin(), EndLoc = Range.getEnd();
  while (BegLoc != EndLoc) {
    if (!checkLocForMacroArgExpansion(BegLoc, SM, ArgumentLoc))
      return false;
    BegLoc.getLocWithOffset(1);
  }

  return checkLocForMacroArgExpansion(BegLoc, SM, ArgumentLoc);
}

bool checkRangesForMacroArgExpansion(FullSourceLoc Loc,
                                     llvm::ArrayRef<CharSourceRange> Ranges) {
  assert(Loc.isMacroID() && "Must be a macro expansion!");

  llvm::SmallVector<CharSourceRange, 4> SpellingRanges;
  mapDiagnosticRanges(Loc, Ranges, SpellingRanges);

  unsigned ValidCount =
      llvm::count_if(Ranges, [](const auto &R) { return R.isValid(); });

  if (ValidCount > SpellingRanges.size())
    return false;

  FullSourceLoc ArgumentLoc;
  if (!Loc.isMacroArgExpansion(&ArgumentLoc))
    return false;

  for (const auto &Range : SpellingRanges)
    if (!checkRangeForMacroArgExpansion(Range, Loc.getManager(), ArgumentLoc))
      return false;

  return true;
}

} // namespace

// ===----------------------------------------------------------------------===
// Macro backtrace
// ===----------------------------------------------------------------------===

void DiagnosticRenderer::emitMacroExpansions(
    FullSourceLoc Loc, DiagnosticsEngine::Level Level,
    llvm::ArrayRef<CharSourceRange> Ranges, llvm::ArrayRef<FixItHint> Hints) {
  assert(Loc.isValid() && "must have a valid source location here");
  const SourceManager &SM = Loc.getManager();
  SourceLocation L = Loc;

  llvm::SmallVector<SourceLocation, 8> LocationStack;
  unsigned IgnoredEnd = 0;
  while (L.isMacroID()) {
    // If this is the expansion of a macro argument, point the caret at the
    // use of the argument in the definition of the macro, not the expansion.
    if (SM.isMacroArgExpansion(L))
      LocationStack.push_back(SM.getImmediateExpansionRange(L).getBegin());
    else
      LocationStack.push_back(L);

    if (checkRangesForMacroArgExpansion(FullSourceLoc(L, SM), Ranges))
      IgnoredEnd = LocationStack.size();

    L = SM.getImmediateMacroCallerLoc(L);

    // Once the location no longer points into a macro, try stepping through
    // the last found location.  This sometimes produces additional useful
    // backtraces.
    if (L.isFileID())
      L = SM.getImmediateMacroCallerLoc(LocationStack.back());
    assert(L.isValid() && "must have a valid source location here");
  }

  LocationStack.erase(LocationStack.begin(),
                      LocationStack.begin() + IgnoredEnd);

  unsigned MacroDepth = LocationStack.size();
  unsigned MacroLimit = DiagOpts->MacroBacktraceLimit;
  if (MacroDepth <= MacroLimit || MacroLimit == 0) {
    for (auto I = LocationStack.rbegin(), E = LocationStack.rend(); I != E; ++I)
      emitSingleMacroExpansion(FullSourceLoc(*I, SM), Level, Ranges);
    return;
  }

  unsigned MacroStartMessages = MacroLimit / 2;
  unsigned MacroEndMessages = MacroLimit / 2 + MacroLimit % 2;

  for (auto I = LocationStack.rbegin(),
            E = LocationStack.rbegin() + MacroStartMessages;
       I != E; ++I)
    emitSingleMacroExpansion(FullSourceLoc(*I, SM), Level, Ranges);

  llvm::SmallString<200> MessageStorage;
  llvm::raw_svector_ostream Message(MessageStorage);
  Message << "(skipping " << (MacroDepth - MacroLimit)
          << " expansions in backtrace; use -fmacro-backtrace-limit=0 to "
             "see all)";
  emitBasicNote(Message.str());

  for (auto I = LocationStack.rend() - MacroEndMessages,
            E = LocationStack.rend();
       I != E; ++I)
    emitSingleMacroExpansion(FullSourceLoc(*I, SM), Level, Ranges);
}

DiagnosticNoteRenderer::~DiagnosticNoteRenderer() = default;

void DiagnosticNoteRenderer::emitIncludeLocation(FullSourceLoc Loc,
                                                 PresumedLoc PLoc) {
  llvm::SmallString<200> MessageStorage;
  llvm::raw_svector_ostream Message(MessageStorage);
  Message << "in file included from " << PLoc.getFilename() << ':'
          << PLoc.getLine() << ":";
  emitNote(Loc, Message.str());
}
