#include "neverc/Compiler/PrepOutputOptions.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Scan/MacroRecord.h"
#include "neverc/Scan/PasteGuard.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepObserver.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// Helpers
// ===----------------------------------------------------------------------===

namespace {
void writeMacroDefinition(const IdentifierInfo &II, const MacroRecord &MI,
                          PrepEngine &PP, llvm::raw_ostream *OS) {
  *OS << "#define " << II.getName();

  if (MI.isFunctionLike()) {
    *OS << '(';
    if (!MI.param_empty()) {
      auto AI = MI.param_begin();
      auto E = MI.param_end();
      auto Last = E - 1;
      for (; AI != Last; ++AI) {
        *OS << (*AI)->getName() << ',';
      }

      if ((*Last)->getName() == "__VA_ARGS__")
        *OS << "...";
      else
        *OS << (*Last)->getName();
    }

    if (MI.isGNUVarargs())
      *OS << "...";

    *OS << ')';
  }

  // GCC always emits a space, even if the macro body is empty.  However, do not
  // want to emit two spaces if the first token has a leading space.
  if (MI.tokens_empty() || !MI.tokens_begin()->hasLeadingSpace())
    *OS << ' ';

  llvm::SmallString<128> SpellingBuffer;
  for (const auto &T : MI.tokens()) {
    if (T.hasLeadingSpace())
      *OS << ' ';

    *OS << PP.getSpelling(T, SpellingBuffer);
  }
}

// ===----------------------------------------------------------------------===
// PrintPPOutputPrepObserver
// ===----------------------------------------------------------------------===

class PrintPPOutputPrepObserver : public PrepObserver {
  PrepEngine &PP;
  SourceManager &SM;
  PasteGuard ConcatInfo;

public:
  llvm::raw_ostream *OS;

private:
  unsigned CurLine;

  bool EmittedTokensOnThisLine;
  bool EmittedDirectiveOnThisLine;
  SrcMgr::CharacteristicKind FileType;
  llvm::SmallString<512> CurFilename;
  bool Initialized;
  bool DisableLineMarkers;
  bool DumpDefines;
  bool DumpIncludeDirectives;
  bool UseLineDirectives;
  bool IsFirstFileEntered;
  bool MinimizeWhitespace;
  bool DirectivesOnly;
  bool KeepSystemIncludes;
  llvm::raw_ostream *OrigOS;
  std::unique_ptr<llvm::raw_null_ostream> NullOS;

  Token PrevTok;
  Token PrevPrevTok;
  unsigned CachedColNo = 0;

  const unsigned *LineTableData = nullptr;
  unsigned LineTableSize = 0;
  unsigned LineTableSLocBase = 0;
  unsigned LineTableSLocEnd = 0;
  unsigned LineTableIdx = 0;

public:
  PrintPPOutputPrepObserver(PrepEngine &pp, llvm::raw_ostream *os,
                            bool lineMarkers, bool defines,
                            bool DumpIncludeDirectives, bool UseLineDirectives,
                            bool MinimizeWhitespace, bool DirectivesOnly,
                            bool KeepSystemIncludes)
      : PP(pp), SM(PP.getSourceManager()), ConcatInfo(PP), OS(os),
        DisableLineMarkers(lineMarkers), DumpDefines(defines),
        DumpIncludeDirectives(DumpIncludeDirectives),
        UseLineDirectives(UseLineDirectives),
        MinimizeWhitespace(MinimizeWhitespace), DirectivesOnly(DirectivesOnly),
        KeepSystemIncludes(KeepSystemIncludes), OrigOS(os) {
    CurLine = 0;
    CurFilename += "<uninit>";
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
    FileType = SrcMgr::C_User;
    Initialized = false;
    IsFirstFileEntered = false;
    if (KeepSystemIncludes)
      NullOS = std::make_unique<llvm::raw_null_ostream>();

    PrevTok.startToken();
    PrevPrevTok.startToken();
  }

  bool isMinimizeWhitespace() const { return MinimizeWhitespace; }

  void setEmittedTokensOnThisLine() { EmittedTokensOnThisLine = true; }
  bool hasEmittedTokensOnThisLine() const { return EmittedTokensOnThisLine; }

  void setEmittedDirectiveOnThisLine() { EmittedDirectiveOnThisLine = true; }
  bool hasEmittedDirectiveOnThisLine() const {
    return EmittedDirectiveOnThisLine;
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  void advanceLinesForFastCopy(unsigned NewLines, const Token &Tok) {
    CurLine += NewLines;
    EmittedTokensOnThisLine = true;
    EmittedDirectiveOnThisLine = false;
    if (LineTableData)
      LineTableIdx += NewLines;
    PrevPrevTok = PrevTok;
    PrevTok = Tok;
  }

  void startNewLineIfNeeded();

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override;
  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          llvm::StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange,
                          OptionalFileEntryRef File, llvm::StringRef SearchPath,
                          llvm::StringRef RelativePath,
                          SrcMgr::CharacteristicKind FileType) override;
  void Ident(SourceLocation Loc, llvm::StringRef str) override;
  void PragmaMessage(SourceLocation Loc, llvm::StringRef Namespace,
                     PragmaMessageKind Kind, llvm::StringRef Str) override;
  void PragmaDebug(SourceLocation Loc, llvm::StringRef DebugType) override;
  void PragmaDiagnosticPush(SourceLocation Loc,
                            llvm::StringRef Namespace) override;
  void PragmaDiagnosticPop(SourceLocation Loc,
                           llvm::StringRef Namespace) override;
  void PragmaDiagnostic(SourceLocation Loc, llvm::StringRef Namespace,
                        diag::Severity Map, llvm::StringRef Str) override;
  void PragmaWarning(SourceLocation Loc, PragmaWarningSpecifier WarningSpec,
                     llvm::ArrayRef<int> Ids) override;
  void PragmaWarningPush(SourceLocation Loc, int Level) override;
  void PragmaWarningPop(SourceLocation Loc) override;
  void PragmaExecCharsetPush(SourceLocation Loc, llvm::StringRef Str) override;
  void PragmaExecCharsetPop(SourceLocation Loc) override;
  void PragmaAssumeNonNullBegin(SourceLocation Loc) override;
  void PragmaAssumeNonNullEnd(SourceLocation Loc) override;

  void GenWhitespaceBeforeTok(const Token &Tok, bool RequireSpace,
                              bool RequireSameLine);

  bool tryFastLineAdvance(const Token &Tok) {
    if (LLVM_UNLIKELY(!LineTableData))
      return false;
    SourceLocation Loc = Tok.getLocation();
    if (!Loc.isFileID())
      return false;
    unsigned Raw = Loc.getRawEncoding();
    if (Raw < LineTableSLocBase || Raw >= LineTableSLocEnd)
      return false;
    unsigned FO = Raw - LineTableSLocBase;
    unsigned I = LineTableIdx;
    if (LLVM_UNLIKELY(FO < LineTableData[I]))
      return false;
    while (I + 1 < LineTableSize && LineTableData[I + 1] <= FO)
      ++I;
    LineTableIdx = I;
    unsigned TargetLine = I + 1;
    if (TargetLine != CurLine + 1)
      return false;
    unsigned ColNo = FO - LineTableData[I] + 1;
    *OS << '\n';
    CurLine = TargetLine;
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
    if (ColNo > 1) {
      static const char Spaces[65] = "                                "
                                     "                                ";
      unsigned N = ColNo - 1;
      while (N > 0) {
        unsigned Batch = N < 64 ? N : 64;
        OS->write(Spaces, Batch);
        N -= Batch;
      }
    }
    PrevPrevTok = PrevTok;
    PrevTok = Tok;
    return true;
  }

  void initLineCacheForFID(FileID FID) {
    bool Invalid = false;
    const SrcMgr::SLocEntry &E = SM.getSLocEntry(FID, &Invalid);
    if (Invalid || !E.isFile()) {
      LineTableData = nullptr;
      return;
    }
    const SrcMgr::FileInfo &FI = E.getFile();
    if (FI.hasLineDirectives()) {
      LineTableData = nullptr;
      return;
    }
    const SrcMgr::ContentCache &CC = FI.getContentCache();
    if (!CC.SourceLineCache || CC.SourceLineCache.size() == 0) {
      LineTableData = nullptr;
      return;
    }
    LineTableData = CC.SourceLineCache.begin();
    LineTableSize = CC.SourceLineCache.size();
    LineTableSLocBase = E.getOffset();
    LineTableIdx = 0;
    if (auto B = SM.getBufferOrNone(FID))
      LineTableSLocEnd = LineTableSLocBase + B->getBufferSize() + 1;
    else
      LineTableSLocEnd = LineTableSLocBase;
  }

  bool MoveToLine(const Token &Tok, bool RequireStartOfLine) {
    SourceLocation Loc = Tok.getLocation();
    if (LLVM_UNLIKELY(Loc.isInvalid())) {
      CachedColNo = 0;
      return MoveToLine(CurLine, RequireStartOfLine);
    }

    if (LLVM_LIKELY(Loc.isFileID() && LineTableData)) {
      unsigned Raw = Loc.getRawEncoding();
      if (LLVM_LIKELY(Raw >= LineTableSLocBase && Raw < LineTableSLocEnd)) {
        unsigned FO = Raw - LineTableSLocBase;
        unsigned I = LineTableIdx;
        if (LLVM_UNLIKELY(FO < LineTableData[I]))
          I = 0;
        while (I + 1 < LineTableSize && LineTableData[I + 1] <= FO)
          ++I;
        LineTableIdx = I;
        unsigned TargetLine = I + 1;
        CachedColNo = FO - LineTableData[I] + 1;
        bool IsFirstInFile = Tok.isAtStartOfLine() && TargetLine == 1;
        return MoveToLine(TargetLine, RequireStartOfLine) || IsFirstInFile;
      }
    }

    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    if (PLoc.isValid()) {
      CachedColNo = PLoc.getColumn();
      unsigned TargetLine = PLoc.getLine();
      if (LLVM_UNLIKELY(!LineTableData) && Loc.isFileID()) {
        auto [FID, Off] = SM.getDecomposedLoc(Loc);
        initLineCacheForFID(FID);
        if (LineTableData && TargetLine > 0)
          LineTableIdx = TargetLine - 1;
      }
      bool IsFirstInFile = Tok.isAtStartOfLine() && TargetLine == 1;
      return MoveToLine(TargetLine, RequireStartOfLine) || IsFirstInFile;
    }
    CachedColNo = 0;
    return MoveToLine(CurLine, RequireStartOfLine);
  }

  bool MoveToLine(SourceLocation Loc, bool RequireStartOfLine) {
    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    unsigned TargetLine = PLoc.isValid() ? PLoc.getLine() : CurLine;
    return MoveToLine(TargetLine, RequireStartOfLine);
  }
  bool MoveToLine(unsigned LineNo, bool RequireStartOfLine);

  bool avoidConcat(const Token &PrevPrevTok, const Token &PrevTok,
                   const Token &Tok) {
    return ConcatInfo.avoidConcat(PrevPrevTok, PrevTok, Tok);
  }
  void WriteLineInfo(unsigned LineNo, const char *Extra = nullptr,
                     unsigned ExtraLen = 0);
  bool LineMarkersAreDisabled() const { return DisableLineMarkers; }
  void GenNewlinesInToken(const char *TokStr, unsigned Len);

  void MacroDefined(const Token &MacroNameTok,
                    const MacroDirective *MD) override;

  void MacroUndefined(const Token &MacroNameTok, const MacroDefinition &MD,
                      const MacroDirective *Undef) override;
};
} // end anonymous namespace

void PrintPPOutputPrepObserver::WriteLineInfo(unsigned LineNo,
                                              const char *Extra,
                                              unsigned ExtraLen) {
  startNewLineIfNeeded();

  // Emit #line directives or GNU line markers depending on what mode we're in.
  if (UseLineDirectives) {
    *OS << "#line" << ' ' << LineNo << ' ' << '"';
    OS->write_escaped(CurFilename);
    *OS << '"';
  } else {
    *OS << '#' << ' ' << LineNo << ' ' << '"';
    OS->write_escaped(CurFilename);
    *OS << '"';

    if (ExtraLen)
      OS->write(Extra, ExtraLen);

    if (FileType == SrcMgr::C_System)
      OS->write(" 3", 2);
    else if (FileType == SrcMgr::C_ExternCSystem)
      OS->write(" 3 4", 4);
  }
  *OS << '\n';
}

bool PrintPPOutputPrepObserver::MoveToLine(unsigned LineNo,
                                           bool RequireStartOfLine) {
  // If it is required to start a new line or finish the current, insert
  // vertical whitespace now and take it into account when moving to the
  // expected line.
  bool StartedNewLine = false;
  if ((RequireStartOfLine && EmittedTokensOnThisLine) ||
      EmittedDirectiveOnThisLine) {
    *OS << '\n';
    StartedNewLine = true;
    CurLine += 1;
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
  }

  // If this line is "close enough" to the original line, just print newlines,
  // otherwise print a #line directive.
  if (CurLine == LineNo) {
    // Nothing to do if we are already on the correct line.
  } else if (MinimizeWhitespace && DisableLineMarkers) {
    // With -E -P -fminimize-whitespace, don't emit anything if not necessary.
  } else if (!StartedNewLine && LineNo - CurLine == 1) {
    // Printing a single line has priority over printing a #line directive, even
    // when minimizing whitespace which otherwise would print #line directives
    // for every single line.
    *OS << '\n';
    StartedNewLine = true;
  } else if (!DisableLineMarkers) {
    if (LineNo - CurLine <= 8) {
      const char *NewLines = "\n\n\n\n\n\n\n\n";
      OS->write(NewLines, LineNo - CurLine);
    } else {
      WriteLineInfo(LineNo, nullptr, 0);
    }
    StartedNewLine = true;
  } else if (EmittedTokensOnThisLine) {
    // If we are not on the correct line and don't need to be line-correct,
    // at least ensure we start on a new line.
    *OS << '\n';
    StartedNewLine = true;
  }

  if (StartedNewLine) {
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
  }

  CurLine = LineNo;
  return StartedNewLine;
}

void PrintPPOutputPrepObserver::startNewLineIfNeeded() {
  if (EmittedTokensOnThisLine || EmittedDirectiveOnThisLine) {
    *OS << '\n';
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
  }
}

void PrintPPOutputPrepObserver::FileChanged(
    SourceLocation Loc, FileChangeReason Reason,
    SrcMgr::CharacteristicKind NewFileType, FileID PrevFID) {
  // Unless we are exiting a #include, make sure to skip ahead to the line the
  // #include directive was at.
  SourceManager &SourceMgr = SM;

  PresumedLoc UserLoc = SourceMgr.getPresumedLoc(Loc);
  if (UserLoc.isInvalid())
    return;

  unsigned NewLine = UserLoc.getLine();

  if (Reason == PrepObserver::EnterFile) {
    SourceLocation IncludeLoc = UserLoc.getIncludeLoc();
    if (IncludeLoc.isValid())
      MoveToLine(IncludeLoc, /*RequireStartOfLine=*/false);
  } else if (Reason == PrepObserver::SystemHeaderPragma) {
    // GCC emits the # directive for this directive on the line AFTER the
    // directive and emits a bunch of spaces that aren't needed. This is because
    // otherwise we will emit a line marker for THIS line, which requires an
    // extra blank line after the directive to avoid making all following lines
    // off by one. We can do better by simply incrementing NewLine here.
    NewLine += 1;
  }

  CurLine = NewLine;

  // In KeepSystemIncludes mode, redirect OS as needed.
  if (KeepSystemIncludes && (isSystem(FileType) != isSystem(NewFileType)))
    OS = isSystem(FileType) ? OrigOS : NullOS.get();

  CurFilename.clear();
  CurFilename += UserLoc.getFilename();
  FileType = NewFileType;

  if (DisableLineMarkers) {
    if (!MinimizeWhitespace)
      startNewLineIfNeeded();
    return;
  }

  if (!Initialized) {
    WriteLineInfo(CurLine);
    Initialized = true;
  }

  // Do not emit an enter marker for the main file (which we expect is the first
  // entered file). This matches gcc, and improves compatibility with some tools
  // which track the # line markers as a way to determine when the preprocessed
  // output is in the context of the main file.
  if (Reason == PrepObserver::EnterFile && !IsFirstFileEntered) {
    IsFirstFileEntered = true;
    return;
  }

  switch (Reason) {
  case PrepObserver::EnterFile:
    WriteLineInfo(CurLine, " 1", 2);
    break;
  case PrepObserver::ExitFile:
    WriteLineInfo(CurLine, " 2", 2);
    break;
  case PrepObserver::SystemHeaderPragma:
  case PrepObserver::RenameFile:
    WriteLineInfo(CurLine);
    break;
  }
}

void PrintPPOutputPrepObserver::InclusionDirective(
    SourceLocation HashLoc, const Token &IncludeTok, llvm::StringRef FileName,
    bool IsAngled, CharSourceRange FilenameRange, OptionalFileEntryRef File,
    llvm::StringRef SearchPath, llvm::StringRef RelativePath,
    SrcMgr::CharacteristicKind FileType) {
  // In -dI mode, dump #include directives prior to dumping their content or
  // interpretation. Similar for -fkeep-system-includes.
  if (DumpIncludeDirectives || (KeepSystemIncludes && isSystem(FileType))) {
    MoveToLine(HashLoc, /*RequireStartOfLine=*/true);
    const std::string TokenText = PP.getSpelling(IncludeTok);
    assert(!TokenText.empty());
    *OS << "#" << TokenText << " " << (IsAngled ? '<' : '"') << FileName
        << (IsAngled ? '>' : '"') << " /* NeverC -E "
        << (DumpIncludeDirectives ? "-dI" : "-fkeep-system-includes") << " */";
    setEmittedDirectiveOnThisLine();
  }
}

void PrintPPOutputPrepObserver::Ident(SourceLocation Loc, llvm::StringRef S) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);

  OS->write("#ident ", strlen("#ident "));
  OS->write(S.begin(), S.size());
  setEmittedTokensOnThisLine();
}

void PrintPPOutputPrepObserver::MacroDefined(const Token &MacroNameTok,
                                             const MacroDirective *MD) {
  const MacroRecord *MI = MD->getMacroRecord();
  // Print out macro definitions in -dD mode and when we have -fdirectives-only.
  if ((!DumpDefines && !DirectivesOnly) ||
      // Ignore __FILE__ etc.
      MI->isBuiltinMacro())
    return;

  SourceLocation DefLoc = MI->getDefinitionLoc();
  if (DirectivesOnly && !MI->isUsed()) {
    SourceManager &SM = PP.getSourceManager();
    if (SM.isWrittenInBuiltinFile(DefLoc) ||
        SM.isWrittenInCommandLineFile(DefLoc))
      return;
  }
  MoveToLine(DefLoc, /*RequireStartOfLine=*/true);
  writeMacroDefinition(*MacroNameTok.getIdentifierInfo(), *MI, PP, OS);
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::MacroUndefined(const Token &MacroNameTok,
                                               const MacroDefinition &MD,
                                               const MacroDirective *Undef) {
  // Print out macro definitions in -dD mode and when we have -fdirectives-only.
  if (!DumpDefines && !DirectivesOnly)
    return;

  MoveToLine(MacroNameTok.getLocation(), /*RequireStartOfLine=*/true);
  *OS << "#undef " << MacroNameTok.getIdentifierInfo()->getName();
  setEmittedDirectiveOnThisLine();
}

void outputPrintable(llvm::raw_ostream *OS, llvm::StringRef Str) {
  for (unsigned char Char : Str) {
    if (isPrintable(Char) && Char != '\\' && Char != '"')
      *OS << (char)Char;
    else // Output anything hard as an octal escape.
      *OS << '\\' << (char)('0' + ((Char >> 6) & 7))
          << (char)('0' + ((Char >> 3) & 7)) << (char)('0' + ((Char >> 0) & 7));
  }
}

void PrintPPOutputPrepObserver::PragmaMessage(SourceLocation Loc,
                                              llvm::StringRef Namespace,
                                              PragmaMessageKind Kind,
                                              llvm::StringRef Str) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma ";
  if (!Namespace.empty())
    *OS << Namespace << ' ';
  switch (Kind) {
  case PMK_Message:
    *OS << "message(\"";
    break;
  case PMK_Warning:
    *OS << "warning \"";
    break;
  case PMK_Error:
    *OS << "error \"";
    break;
  }

  outputPrintable(OS, Str);
  *OS << '"';
  if (Kind == PMK_Message)
    *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaDebug(SourceLocation Loc,
                                            llvm::StringRef DebugType) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);

  *OS << "#pragma neverc __debug ";
  *OS << DebugType;

  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaDiagnosticPush(
    SourceLocation Loc, llvm::StringRef Namespace) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma " << Namespace << " diagnostic push";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaDiagnosticPop(SourceLocation Loc,
                                                    llvm::StringRef Namespace) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma " << Namespace << " diagnostic pop";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaDiagnostic(SourceLocation Loc,
                                                 llvm::StringRef Namespace,
                                                 diag::Severity Map,
                                                 llvm::StringRef Str) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma " << Namespace << " diagnostic ";
  switch (Map) {
  case diag::Severity::Remark:
    *OS << "remark";
    break;
  case diag::Severity::Warning:
    *OS << "warning";
    break;
  case diag::Severity::Error:
    *OS << "error";
    break;
  case diag::Severity::Ignored:
    *OS << "ignored";
    break;
  case diag::Severity::Fatal:
    *OS << "fatal";
    break;
  }
  *OS << " \"" << Str << '"';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaWarning(
    SourceLocation Loc, PragmaWarningSpecifier WarningSpec,
    llvm::ArrayRef<int> Ids) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);

  *OS << "#pragma warning(";
  switch (WarningSpec) {
  case PWS_Default:
    *OS << "default";
    break;
  case PWS_Disable:
    *OS << "disable";
    break;
  case PWS_Error:
    *OS << "error";
    break;
  case PWS_Once:
    *OS << "once";
    break;
  case PWS_Suppress:
    *OS << "suppress";
    break;
  case PWS_Level1:
    *OS << '1';
    break;
  case PWS_Level2:
    *OS << '2';
    break;
  case PWS_Level3:
    *OS << '3';
    break;
  case PWS_Level4:
    *OS << '4';
    break;
  }
  *OS << ':';

  for (llvm::ArrayRef<int>::iterator I = Ids.begin(), E = Ids.end(); I != E;
       ++I)
    *OS << ' ' << *I;
  *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaWarningPush(SourceLocation Loc,
                                                  int Level) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma warning(push";
  if (Level >= 0)
    *OS << ", " << Level;
  *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaWarningPop(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma warning(pop)";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaExecCharsetPush(SourceLocation Loc,
                                                      llvm::StringRef Str) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma character_execution_set(push";
  if (!Str.empty())
    *OS << ", " << Str;
  *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaExecCharsetPop(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma character_execution_set(pop)";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaAssumeNonNullBegin(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma neverc assume_nonnull begin";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaAssumeNonNullEnd(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma neverc assume_nonnull end";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::GenWhitespaceBeforeTok(const Token &Tok,
                                                       bool RequireSpace,
                                                       bool RequireSameLine) {
  // These tokens are not expanded to anything and don't need whitespace before
  // them.
  if (Tok.is(tok::eof) || Tok.isAnnotation())
    return;

  // EmittedDirectiveOnThisLine takes priority over RequireSameLine.
  if ((!RequireSameLine || EmittedDirectiveOnThisLine) &&
      MoveToLine(Tok, /*RequireStartOfLine=*/EmittedDirectiveOnThisLine)) {
    if (MinimizeWhitespace) {
      // Avoid interpreting hash as a directive under -fpreprocessed.
      if (Tok.is(tok::hash))
        *OS << ' ';
    } else {
      unsigned ColNo = CachedColNo
                           ? CachedColNo
                           : SM.getExpansionColumnNumber(Tok.getLocation());

      // The first token on a line can have a column number of 1, yet still
      // expect leading white space, if a macro expansion in column 1 starts
      // with an empty macro argument, or an empty nested macro expansion. In
      // this case, move the token to column 2.
      if (ColNo == 1 && Tok.hasLeadingSpace())
        ColNo = 2;

      // This hack prevents stuff like:
      // #define HASH #
      // HASH define foo bar
      // From having the # character end up at column 1, which makes it so it
      // is not handled as a #define next time through the preprocessor if in
      // -fpreprocessed mode.
      if (ColNo <= 1 && Tok.is(tok::hash))
        *OS << ' ';

      if (ColNo > 1) {
        static const char Spaces[65] = "                                "
                                       "                                ";
        unsigned N = ColNo - 1;
        while (N > 0) {
          unsigned Batch = N < 64 ? N : 64;
          OS->write(Spaces, Batch);
          N -= Batch;
        }
      }
    }
  } else {
    // Insert whitespace between the previous and next token if either
    // - The caller requires it
    // - The input had whitespace between them and we are not in
    //   whitespace-minimization mode
    // - The whitespace is necessary to keep the tokens apart and there is not
    //   already a newline between them
    if (RequireSpace || (!MinimizeWhitespace && Tok.hasLeadingSpace()) ||
        ((EmittedTokensOnThisLine || EmittedDirectiveOnThisLine) &&
         avoidConcat(PrevPrevTok, PrevTok, Tok)))
      *OS << ' ';
  }

  PrevPrevTok = PrevTok;
  PrevTok = Tok;
}

void PrintPPOutputPrepObserver::GenNewlinesInToken(const char *TokStr,
                                                   unsigned Len) {
  unsigned NumNewlines = 0;
  for (; Len; --Len, ++TokStr) {
    if (*TokStr != '\n' && *TokStr != '\r')
      continue;

    ++NumNewlines;

    // If we have \n\r or \r\n, skip both and count as one line.
    if (Len != 1 && (TokStr[1] == '\n' || TokStr[1] == '\r') &&
        TokStr[0] != TokStr[1]) {
      ++TokStr;
      --Len;
    }
  }

  if (NumNewlines == 0)
    return;

  CurLine += NumNewlines;
}

namespace {
struct UnknownPragmaHandler : public PragmaDispatch {
  const char *Prefix;
  PrintPPOutputPrepObserver *Callbacks;

  bool ShouldExpandTokens;

  UnknownPragmaHandler(const char *prefix, PrintPPOutputPrepObserver *callbacks,
                       bool RequireTokenExpansion)
      : Prefix(prefix), Callbacks(callbacks),
        ShouldExpandTokens(RequireTokenExpansion) {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &PragmaTok) override {
    // Figure out what line we went to and insert the appropriate number of
    // newline characters.
    Callbacks->MoveToLine(PragmaTok.getLocation(), /*RequireStartOfLine=*/true);
    Callbacks->OS->write(Prefix, strlen(Prefix));
    Callbacks->setEmittedTokensOnThisLine();

    if (ShouldExpandTokens) {
      // The first token does not have expanded macros. Expand them, if
      // required.
      auto Toks = std::make_unique<Token[]>(1);
      Toks[0] = PragmaTok;
      PP.PushTokenStream(std::move(Toks), /*NumToks=*/1,
                         /*DisableMacroExpansion=*/false,
                         /*IsReinject=*/false);
      PP.Lex(PragmaTok);
    }

    // Read and print all of the pragma tokens.
    bool IsFirst = true;
    while (PragmaTok.isNot(tok::eod)) {
      Callbacks->GenWhitespaceBeforeTok(PragmaTok, /*RequireSpace=*/IsFirst,
                                        /*RequireSameLine=*/true);
      IsFirst = false;
      std::string TokSpell = PP.getSpelling(PragmaTok);
      Callbacks->OS->write(&TokSpell[0], TokSpell.size());
      Callbacks->setEmittedTokensOnThisLine();

      if (ShouldExpandTokens)
        PP.Lex(PragmaTok);
      else
        PP.LexWithoutExpansion(PragmaTok);
    }
    Callbacks->setEmittedDirectiveOnThisLine();
  }
};

void printPreprocessedTokens(PrepEngine &PP, Token &Tok,
                             PrintPPOutputPrepObserver *Callbacks) {
  bool DropComments =
      PP.getLangOpts().TraditionalCPP && !PP.getCommentRetentionState();

  SourceManager &SM = PP.getSourceManager();

  bool IsStartOfLine = false;
  char Buffer[256];

  const char *SrcBase = nullptr;
  unsigned SLocBase = 0;
  unsigned SLocEnd = 0;
  const char *CopyEnd = nullptr;

  auto setupSrcBuf = [&](SourceLocation Loc) {
    auto [FID, Off] = SM.getDecomposedLoc(Loc);
    if (auto Buf = SM.getBufferOrNone(FID)) {
      bool Invalid = false;
      const SrcMgr::SLocEntry &E = SM.getSLocEntry(FID, &Invalid);
      if (!Invalid && E.isFile()) {
        SrcBase = Buf->getBufferStart();
        SLocBase = E.getOffset();
        SLocEnd = SLocBase + Buf->getBufferSize() + 1;
        CopyEnd = SrcBase + Off + Tok.getLength();
        return;
      }
    }
    CopyEnd = nullptr;
  };

  const char *DeferredStart = nullptr;

  auto flushDeferred = [&]() __attribute__((always_inline)) {
    if (DeferredStart && DeferredStart < CopyEnd)
      Callbacks->OS->write(DeferredStart, CopyEnd - DeferredStart);
    DeferredStart = nullptr;
  };

  while (true) {
    IsStartOfLine = IsStartOfLine || Tok.isAtStartOfLine();

    // --- Fast path: file-location token, deferred source range copy ---
    // Instead of writing each token's source range individually, we
    // accumulate a contiguous range [DeferredStart, CopyEnd) and flush
    // it only when the fast path breaks.  This batches ~20 per-token
    // writes into one large write per contiguous run.
    if (LLVM_LIKELY(CopyEnd)) {
      SourceLocation Loc = Tok.getLocation();
      if (LLVM_LIKELY(Loc.isFileID())) {
        unsigned Raw = Loc.getRawEncoding();
        if (LLVM_LIKELY(Raw >= SLocBase && Raw < SLocEnd)) {
          tok::TokenKind K = Tok.getKind();
          if (LLVM_LIKELY(K != tok::eof && !tok::isAnnotation(K) &&
                          K != tok::eod && !Tok.needsCleaning() &&
                          !(DropComments && K == tok::comment))) {
            const char *TokStart = SrcBase + (Raw - SLocBase);
            if (LLVM_LIKELY(TokStart >= CopyEnd)) {
              ptrdiff_t Gap = TokStart - CopyEnd;
              bool SafeGap;
              unsigned GapNewLines = 0;
              if (LLVM_LIKELY(Gap <= 1)) {
                SafeGap = (Gap == 0) | (*CopyEnd == ' ');
              } else {
                SafeGap = true;
                const char *G = CopyEnd;
                while (G < TokStart) {
                  char GC = *G;
                  if (GC == '\n') {
                    GapNewLines++;
                  } else if (LLVM_UNLIKELY(GC != ' ' && GC != '\t' &&
                                           GC != '\r')) {
                    SafeGap = false;
                    break;
                  }
                  ++G;
                }
              }
              if (LLVM_LIKELY(SafeGap)) {
                if (!DeferredStart)
                  DeferredStart = CopyEnd;
                CopyEnd = TokStart + Tok.getLength();
                if (LLVM_LIKELY(GapNewLines == 0)) {
                  Callbacks->setEmittedTokensOnThisLine();
                } else {
                  Callbacks->advanceLinesForFastCopy(GapNewLines, Tok);
                }
                if (LLVM_UNLIKELY(K == tok::comment || K == tok::unknown))
                  Callbacks->GenNewlinesInToken(TokStart, Tok.getLength());
                if (LLVM_UNLIKELY(K == tok::comment && Tok.getLength() >= 2 &&
                                  TokStart[0] == '/' && TokStart[1] == '/'))
                  Callbacks->setEmittedDirectiveOnThisLine();
                IsStartOfLine = false;
                PP.Lex(Tok);
                continue;
              }
            }
          }
        }
      }
      flushDeferred();
      CopyEnd = nullptr;
    }

    // --- Standard path ---
    if (IsStartOfLine && Callbacks->tryFastLineAdvance(Tok))
      ; // Line advance handled inline, skip GenWhitespace
    else
      Callbacks->GenWhitespaceBeforeTok(Tok, /*RequireSpace=*/false,
                                        /*RequireSameLine=*/!IsStartOfLine);

    if (DropComments && Tok.is(tok::comment)) {
      PP.Lex(Tok);
      continue;
    } else if (Tok.is(tok::eod)) {
      PP.Lex(Tok);
      IsStartOfLine = true;
      continue;
    } else if (Tok.isAnnotation()) {
      PP.Lex(Tok);
      continue;
    } else if (IdentifierInfo *II = Tok.getIdentifierInfo()) {
      *Callbacks->OS << II->getName();
    } else if (Tok.isLiteral() && !Tok.needsCleaning() &&
               Tok.getLiteralData()) {
      Callbacks->OS->write(Tok.getLiteralData(), Tok.getLength());
    } else if (const char *Punct = tok::getPunctuatorSpelling(Tok.getKind())) {
      Callbacks->OS->write(Punct, Tok.getLength());
    } else if (Tok.getLength() < std::size(Buffer)) {
      const char *TokPtr = Buffer;
      unsigned Len = PP.getSpelling(Tok, TokPtr);
      Callbacks->OS->write(TokPtr, Len);

      if (Tok.getKind() == tok::comment || Tok.getKind() == tok::unknown)
        Callbacks->GenNewlinesInToken(TokPtr, Len);
      if (Tok.is(tok::comment) && Len >= 2 && TokPtr[0] == '/' &&
          TokPtr[1] == '/') {
        Callbacks->setEmittedDirectiveOnThisLine();
      }
    } else {
      std::string S = PP.getSpelling(Tok);
      Callbacks->OS->write(S.data(), S.size());

      if (Tok.getKind() == tok::comment || Tok.getKind() == tok::unknown)
        Callbacks->GenNewlinesInToken(S.data(), S.size());
      if (Tok.is(tok::comment) && S.size() >= 2 && S[0] == '/' && S[1] == '/') {
        Callbacks->setEmittedDirectiveOnThisLine();
      }
    }
    Callbacks->setEmittedTokensOnThisLine();
    IsStartOfLine = false;

    // After standard-path output, set up CopyEnd for the fast path.
    {
      SourceLocation Loc = Tok.getLocation();
      if (Loc.isFileID() && !Tok.needsCleaning()) {
        unsigned Raw = Loc.getRawEncoding();
        if (Raw >= SLocBase && Raw < SLocEnd) {
          CopyEnd = SrcBase + (Raw - SLocBase) + Tok.getLength();
        } else {
          setupSrcBuf(Loc);
        }
      } else {
        CopyEnd = nullptr;
      }
    }

    if (Tok.is(tok::eof)) {
      flushDeferred();
      break;
    }

    PP.Lex(Tok);
  }
}

typedef std::pair<const IdentifierInfo *, MacroRecord *> id_macro_pair;
int compareMacroIDs(const id_macro_pair *LHS, const id_macro_pair *RHS) {
  return LHS->first->getName().compare(RHS->first->getName());
}

void genAllMacroDefinitions(PrepEngine &PP, llvm::raw_ostream *OS) {
  // Ignore unknown pragmas.
  PP.IgnorePragmas();

  // -dM mode just scans and ignores all tokens in the files, then dumps out
  // the macro table at the end.
  PP.InitMainInput();

  Token Tok;
  do
    PP.Lex(Tok);
  while (Tok.isNot(tok::eof));

  llvm::SmallVector<id_macro_pair, 128> MacrosByID;
  for (PrepEngine::macro_iterator I = PP.macro_begin(), E = PP.macro_end();
       I != E; ++I) {
    auto *MD = I->second.getLatest();
    if (MD && MD->isDefined())
      MacrosByID.push_back(id_macro_pair(I->first, MD->getMacroRecord()));
  }
  llvm::array_pod_sort(MacrosByID.begin(), MacrosByID.end(), compareMacroIDs);

  for (unsigned i = 0, e = MacrosByID.size(); i != e; ++i) {
    MacroRecord &MI = *MacrosByID[i].second;
    // Ignore computed macros like __LINE__ and friends.
    if (MI.isBuiltinMacro())
      continue;

    writeMacroDefinition(*MacrosByID[i].first, MI, PP, OS);
    *OS << '\n';
  }
}
} // namespace

// ===----------------------------------------------------------------------===
// Entry point
// ===----------------------------------------------------------------------===

void neverc::DoPrintPreprocessedInput(PrepEngine &PP, llvm::raw_ostream *OS,
                                      const PrepOutputOptions &Opts) {
  // Show macros with no output is handled specially.
  if (!Opts.ShowCPP) {
    assert(Opts.ShowMacros && "Not yet implemented!");
    genAllMacroDefinitions(PP, OS);
    return;
  }

  // Inform the preprocessor whether we want it to retain comments or not, due
  // to -C or -CC.
  PP.SetCommentRetentionState(Opts.ShowComments, Opts.ShowMacroComments);

  PrintPPOutputPrepObserver *Callbacks = new PrintPPOutputPrepObserver(
      PP, OS, !Opts.ShowLineMarkers, Opts.ShowMacros,
      Opts.ShowIncludeDirectives, Opts.UseLineDirectives,
      Opts.MinimizeWhitespace, Opts.DirectivesOnly, Opts.KeepSystemIncludes);

  // Expand macros in pragmas with -fms-extensions.  The assumption is that
  // the majority of pragmas in such a file will be Microsoft pragmas.
  // Remember the handlers we will add so that we can remove them later.
  bool MSExt = PP.getLangOpts().MicrosoftExt;
  auto MicrosoftExtHandler =
      std::make_unique<UnknownPragmaHandler>("#pragma", Callbacks, MSExt);
  auto GCCHandler =
      std::make_unique<UnknownPragmaHandler>("#pragma GCC", Callbacks, MSExt);
  auto NeverCHandler = std::make_unique<UnknownPragmaHandler>("#pragma neverc",
                                                              Callbacks, MSExt);

  PP.AddPragmaDispatch(MicrosoftExtHandler.get());
  PP.AddPragmaDispatch("GCC", GCCHandler.get());
  PP.AddPragmaDispatch("neverc", NeverCHandler.get());

  PP.addObserver(std::unique_ptr<PrepObserver>(Callbacks));

  // After we have configured the preprocessor, enter the main file.
  PP.InitMainInput();
  if (Opts.DirectivesOnly)
    PP.SetMacroExpansionOnlyInDirectives();

  // Consume all of the tokens that come from the predefines buffer.  Those
  // should not be emitted into the output and are guaranteed to be at the
  // start.
  const SourceManager &SourceMgr = PP.getSourceManager();
  Token Tok;
  do {
    PP.Lex(Tok);
    if (Tok.is(tok::eof) || !Tok.getLocation().isFileID())
      break;

    PresumedLoc PLoc = SourceMgr.getPresumedLoc(Tok.getLocation());
    if (PLoc.isInvalid())
      break;

    if (strcmp(PLoc.getFilename(), "<built-in>"))
      break;
  } while (true);

  // Read all the preprocessed tokens, printing them out to the stream.
  printPreprocessedTokens(PP, Tok, Callbacks);
  *OS << '\n';

  // Remove the handlers we just added to leave the preprocessor in a sane state
  // so that it can be reused (for example by a neverc::Parser instance).
  PP.RemovePragmaDispatch(MicrosoftExtHandler.get());
  PP.RemovePragmaDispatch("GCC", GCCHandler.get());
  PP.RemovePragmaDispatch("neverc", NeverCHandler.get());
}
