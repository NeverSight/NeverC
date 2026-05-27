#include "PrintPPOutputPrepObserver.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Scan/MacroRecord.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// writeMacroDefinition
// ===----------------------------------------------------------------------===

void neverc::writeMacroDefinition(const IdentifierInfo &II,
                                  const MacroRecord &MI, PrepEngine &PP,
                                  llvm::raw_ostream *OS) {
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
// PrintPPOutputPrepObserver method implementations
// ===----------------------------------------------------------------------===

void PrintPPOutputPrepObserver::WriteLineInfo(unsigned LineNo,
                                              const char *Extra,
                                              unsigned ExtraLen) {
  startNewLineIfNeeded();

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
  bool StartedNewLine = false;
  if ((RequireStartOfLine && EmittedTokensOnThisLine) ||
      EmittedDirectiveOnThisLine) {
    *OS << '\n';
    StartedNewLine = true;
    CurLine += 1;
    EmittedTokensOnThisLine = false;
    EmittedDirectiveOnThisLine = false;
  }

  if (CurLine == LineNo) {
    // Nothing to do if we are already on the correct line.
  } else if (MinimizeWhitespace && DisableLineMarkers) {
    // With -E -P -fminimize-whitespace, don't emit anything if not necessary.
  } else if (!StartedNewLine && LineNo - CurLine == 1) {
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
    NewLine += 1;
  }

  CurLine = NewLine;

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
  if ((!DumpDefines && !DirectivesOnly) ||
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
  if (!DumpDefines && !DirectivesOnly)
    return;

  MoveToLine(MacroNameTok.getLocation(), /*RequireStartOfLine=*/true);
  *OS << "#undef " << MacroNameTok.getIdentifierInfo()->getName();
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::GenWhitespaceBeforeTok(const Token &Tok,
                                                       bool RequireSpace,
                                                       bool RequireSameLine) {
  if (Tok.is(tok::eof) || Tok.isAnnotation())
    return;

  if ((!RequireSameLine || EmittedDirectiveOnThisLine) &&
      MoveToLine(Tok, /*RequireStartOfLine=*/EmittedDirectiveOnThisLine)) {
    if (MinimizeWhitespace) {
      if (Tok.is(tok::hash))
        *OS << ' ';
    } else {
      unsigned ColNo = CachedColNo
                           ? CachedColNo
                           : SM.getExpansionColumnNumber(Tok.getLocation());

      if (ColNo == 1 && Tok.hasLeadingSpace())
        ColNo = 2;

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

void PrintPPOutputPrepObserver::initLineCacheForFID(FileID FID) {
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
