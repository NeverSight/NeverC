#ifndef NEVERC_LIB_COMPILER_PREPROCESSOR_PRINTPPOUTPUTPREPOBSERVER_H
#define NEVERC_LIB_COMPILER_PREPROCESSOR_PRINTPPOUTPUTPREPOBSERVER_H

#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Scan/PasteGuard.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepObserver.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace neverc {

void writeMacroDefinition(const IdentifierInfo &II, const MacroRecord &MI,
                          PrepEngine &PP, llvm::raw_ostream *OS);

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

  void initLineCacheForFID(FileID FID);

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

} // namespace neverc

#endif // NEVERC_LIB_COMPILER_PREPROCESSOR_PRINTPPOUTPUTPREPOBSERVER_H
