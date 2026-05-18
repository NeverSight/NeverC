#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/MacroRecord.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <optional>

using namespace neverc;

// ===----------------------------------------------------------------------===
// File lexer queries
// ===----------------------------------------------------------------------===

bool PrepEngine::isInPrimaryFile() const {
  if (IsFileLexer())
    return IncludeMacroStack.empty();

  assert(IsFileLexer(IncludeMacroStack[0]) &&
         "Top level include stack isn't our primary lexer?");
  return llvm::none_of(
      llvm::drop_begin(IncludeMacroStack),
      [&](const LexerFrame &ISI) -> bool { return IsFileLexer(ISI); });
}

LexerCore *PrepEngine::getCurrentFileLexer() const {
  if (IsFileLexer())
    return CurPPLexer;

  for (const LexerFrame &ISI : llvm::reverse(IncludeMacroStack)) {
    if (IsFileLexer(ISI))
      return ISI.ThePPLexer;
  }
  return nullptr;
}

// ===----------------------------------------------------------------------===
// Lexer stack management
// ===----------------------------------------------------------------------===

bool PrepEngine::PushSourceFile(FileID FID, ConstSearchDirIterator CurDir,
                                SourceLocation Loc, bool IsFirstIncludeOfFile) {
  assert(!CurExpansionLexer && "Cannot #include a file inside a macro!");
  ++NumEnteredSourceFiles;

  if (MaxIncludeStackDepth < IncludeMacroStack.size())
    MaxIncludeStackDepth = IncludeMacroStack.size();

  std::optional<llvm::MemoryBufferRef> InputFile =
      getSourceManager().getBufferOrNone(FID, Loc);
  if (LLVM_UNLIKELY(!InputFile)) {
    SourceLocation FileStart = SourceMgr.getLocForStartOfFile(FID);
    Diag(Loc, diag::err_pp_error_opening_file)
        << SourceMgr.getBufferName(FileStart) << "";
    return true;
  }

  auto *TheLexer =
      new SourceScanner(FID, *InputFile, *this, IsFirstIncludeOfFile);

  PushLexer(TheLexer, CurDir);
  return false;
}

void PrepEngine::PushLexer(SourceScanner *TheLexer,
                           ConstSearchDirIterator CurDir) {
  LexerCore *PrevPPLexer = CurPPLexer;

  if (CurPPLexer || CurExpansionLexer)
    SaveLexerFrame();

  CurLexer.reset(TheLexer);
  CurPPLexer = TheLexer;
  CurDirLookup = CurDir;
  CurLexerCallback = DispatchFile;

  if (Callbacks && !CurLexer->Is_PragmaLexer) {
    SrcMgr::CharacteristicKind FileType =
        SourceMgr.getFileCharacteristic(CurLexer->getFileLoc());

    FileID PrevFID;
    SourceLocation EnterLoc;
    if (PrevPPLexer) {
      PrevFID = PrevPPLexer->getFileID();
      EnterLoc = PrevPPLexer->getSourceLocation();
    }
    Callbacks->FileChanged(CurLexer->getFileLoc(), PrepObserver::EnterFile,
                           FileType, PrevFID);
    Callbacks->LexedFileChanged(CurLexer->getFileID(),
                                PrepObserver::LexedFileChangeReason::EnterFile,
                                FileType, PrevFID, EnterLoc);
  }
}

__attribute__((hot)) void PrepEngine::PushMacroContext(Token &Tok,
                                                       SourceLocation ILEnd,
                                                       MacroRecord *Macro,
                                                       MacroArgStorage *Args) {
  std::unique_ptr<ExpansionLexer> TokLexer;
  if (LLVM_LIKELY(!ExpansionLexerCache.empty())) {
    TokLexer = std::move(ExpansionLexerCache.back());
    ExpansionLexerCache.pop_back();
    TokLexer->Init(Tok, ILEnd, Macro, Args);
  } else {
    TokLexer = std::make_unique<ExpansionLexer>(Tok, ILEnd, Macro, Args, *this);
  }

  SaveLexerFrame();
  CurDirLookup = nullptr;
  CurExpansionLexer = std::move(TokLexer);
  CurLexerCallback = DispatchExpansion;
}

__attribute__((hot)) void
PrepEngine::PushTokenStream(const Token *Toks, unsigned NumToks,
                            bool DisableMacroExpansion, bool OwnsTokens,
                            bool IsReinject) {
  if (LLVM_UNLIKELY(CurLexerCallback == DispatchCache)) {
    if (CachedLexPos < CachedTokens.size()) {
      assert(IsReinject && "new tokens in the middle of cached stream");
      CachedTokens.insert(CachedTokens.begin() + CachedLexPos, Toks,
                          Toks + NumToks);
      if (OwnsTokens)
        delete[] Toks;
      return;
    }

    DisableCaching();
    PushTokenStream(Toks, NumToks, DisableMacroExpansion, OwnsTokens,
                    IsReinject);
    EnableCaching();
    return;
  }

  std::unique_ptr<ExpansionLexer> TokLexer;
  if (LLVM_LIKELY(!ExpansionLexerCache.empty())) {
    TokLexer = std::move(ExpansionLexerCache.back());
    ExpansionLexerCache.pop_back();
    TokLexer->Init(Toks, NumToks, DisableMacroExpansion, OwnsTokens,
                   IsReinject);
  } else {
    TokLexer = std::make_unique<ExpansionLexer>(
        Toks, NumToks, DisableMacroExpansion, OwnsTokens, IsReinject, *this);
  }

  SaveLexerFrame();
  CurDirLookup = nullptr;
  CurExpansionLexer = std::move(TokLexer);
  CurLexerCallback = DispatchExpansion;
}

void PrepEngine::CarryLineFlags(Token &Result) {
  if (CurExpansionLexer) {
    CurExpansionLexer->carryLineFlags(Result);
    return;
  }
  if (CurLexer) {
    CurLexer->carryLineFlags(Result);
    return;
  }
}

const char *PrepEngine::computeEffectiveEndPos() {
  const char *EndPos = CurLexer->BufferEnd;
  if (EndPos != CurLexer->BufferStart &&
      (EndPos[-1] == '\n' || EndPos[-1] == '\r')) {
    --EndPos;

    // Handle \n\r and \r\n:
    if (EndPos != CurLexer->BufferStart &&
        (EndPos[-1] == '\n' || EndPos[-1] == '\r') && EndPos[-1] != EndPos[0])
      --EndPos;
  }

  return EndPos;
}

// ===----------------------------------------------------------------------===
// End-of-file handling
// ===----------------------------------------------------------------------===

void PrepEngine::checkHeaderGuardAtEOF() {
  if (!CurPPLexer)
    return;
  const IdentifierInfo *ControllingMacro =
      CurPPLexer->MIOpt.getControllingMacroAtEndOfFile();
  if (!ControllingMacro)
    return;
  OptionalFileEntryRef FE = CurPPLexer->getFileEntry();
  if (!FE)
    return;

  HeaderInfo.SetFileControllingMacro(*FE, ControllingMacro);
  if (MacroRecord *MI =
          getMacroRecord(const_cast<IdentifierInfo *>(ControllingMacro)))
    MI->setUsedForHeaderGuard(true);

  const IdentifierInfo *DefinedMacro = CurPPLexer->MIOpt.getDefinedMacro();
  if (!DefinedMacro || isMacroDefined(ControllingMacro) ||
      DefinedMacro == ControllingMacro || !CurLexer->isFirstTimeLexingFile())
    return;

  const llvm::StringRef ControllingMacroName = ControllingMacro->getName();
  const llvm::StringRef DefinedMacroName = DefinedMacro->getName();
  const size_t MaxHalfLength =
      std::max(ControllingMacroName.size(), DefinedMacroName.size()) / 2;
  const size_t LenDiff =
      ControllingMacroName.size() > DefinedMacroName.size()
          ? ControllingMacroName.size() - DefinedMacroName.size()
          : DefinedMacroName.size() - ControllingMacroName.size();
  if (LenDiff > MaxHalfLength)
    return;

  const unsigned ED =
      ControllingMacroName.edit_distance(DefinedMacroName, true, MaxHalfLength);
  if (ED > MaxHalfLength)
    return;

  Diag(CurPPLexer->MIOpt.getMacroLocation(), diag::warn_header_guard)
      << CurPPLexer->MIOpt.getMacroLocation() << ControllingMacro;
  Diag(CurPPLexer->MIOpt.getDefinedLocation(), diag::note_header_guard)
      << CurPPLexer->MIOpt.getDefinedLocation() << DefinedMacro
      << ControllingMacro
      << FixItHint::CreateReplacement(CurPPLexer->MIOpt.getDefinedLocation(),
                                      ControllingMacro->getName());
}

bool PrepEngine::FinalizeSourceEnd(Token &Result, bool isEndOfMacro) {
  assert(!CurExpansionLexer && "Ending a file when currently in a macro!");

  SourceLocation UnclosedSafeBufferOptOutLoc;

  if (IncludeMacroStack.empty() &&
      isPPInSafeBufferOptOutRegion(UnclosedSafeBufferOptOutLoc)) {
    Diag(UnclosedSafeBufferOptOutLoc,
         diag::err_pp_unclosed_pragma_unsafe_buffer_usage);
  }

  checkHeaderGuardAtEOF();

  if (PragmaAssumeNonNullLoc.isValid() && !isEndOfMacro &&
      !(CurLexer && CurLexer->Is_PragmaLexer)) {
    Diag(PragmaAssumeNonNullLoc, diag::err_pp_eof_in_assume_nonnull);
    PragmaAssumeNonNullLoc = SourceLocation();
  }

  if (!IncludeMacroStack.empty()) {
    if (!isEndOfMacro && CurPPLexer &&
        (SourceMgr.getIncludeLoc(CurPPLexer->getFileID()).isValid() ||
         (PredefinesFileID.isValid() &&
          CurPPLexer->getFileID() == PredefinesFileID))) {
      unsigned NumFIDs = SourceMgr.local_sloc_entry_size() -
                         CurPPLexer->getInitialNumSLocEntries() +
                         1 /*#include'd file*/;
      SourceMgr.setNumCreatedFIDsForFileID(CurPPLexer->getFileID(), NumFIDs);
    }

    FileID ExitedFID;
    if (!isEndOfMacro && CurPPLexer)
      ExitedFID = CurPPLexer->getFileID();

    PopLexerLevel();
    CarryLineFlags(Result);

    if (Callbacks && !isEndOfMacro && CurPPLexer) {
      SourceLocation Loc = CurPPLexer->getSourceLocation();
      SrcMgr::CharacteristicKind FileType =
          SourceMgr.getFileCharacteristic(Loc);
      Callbacks->FileChanged(Loc, PrepObserver::ExitFile, FileType, ExitedFID);
      Callbacks->LexedFileChanged(CurPPLexer->getFileID(),
                                  PrepObserver::LexedFileChangeReason::ExitFile,
                                  FileType, ExitedFID, Loc);
    }

    return false;
  }

  assert(CurLexer && "Got EOF but no current lexer set!");
  const char *EndPos = computeEffectiveEndPos();
  Result.startToken();
  CurLexer->BufferPtr = EndPos;
  CurLexer->emitToken(Result, EndPos, tok::eof);

  CurLexer.reset();
  CurPPLexer = nullptr;

  for (const auto &Loc : WarnUnusedMacroLocs)
    Diag(Loc, diag::pp_macro_not_used);

  return true;
}

__attribute__((hot)) bool PrepEngine::FinalizeExpansion(Token &Result) {
  assert(CurExpansionLexer && !CurPPLexer &&
         "Ending a macro when currently in a #include file!");

  if (LLVM_UNLIKELY(!MacroExpandingLexersStack.empty() &&
                    MacroExpandingLexersStack.back().first ==
                        CurExpansionLexer.get()))
    unstashLastLexerTokens();

  static constexpr size_t MaxCacheSize = 16;
  if (LLVM_LIKELY(ExpansionLexerCache.size() < MaxCacheSize))
    ExpansionLexerCache.push_back(std::move(CurExpansionLexer));
  else
    CurExpansionLexer.reset();

  return FinalizeSourceEnd(Result, true);
}

void PrepEngine::PopLexerLevel() {
  assert(!IncludeMacroStack.empty() && "Ran out of stack entries to load");

  if (CurExpansionLexer) {
    static constexpr size_t MaxCacheSize = 16;
    if (LLVM_LIKELY(ExpansionLexerCache.size() < MaxCacheSize))
      ExpansionLexerCache.push_back(std::move(CurExpansionLexer));
    else
      CurExpansionLexer.reset();
  }

  RestoreLexerFrame();
}

void PrepEngine::SkipPastedComment(Token &Tok) {
  assert(CurExpansionLexer && !CurPPLexer &&
         "Pasted comment can only be formed from macro");
  LexerCore *FoundLexer = nullptr;
  bool LexerWasInPPMode = false;
  for (const LexerFrame &ISI : llvm::reverse(IncludeMacroStack)) {
    if (ISI.ThePPLexer == nullptr)
      continue;

    FoundLexer = ISI.ThePPLexer;
    FoundLexer->LexingRawMode = true;
    LexerWasInPPMode = FoundLexer->ParsingDirective;
    FoundLexer->ParsingDirective = true;
    break;
  }

  if (!FinalizeExpansion(Tok))
    Lex(Tok);

  while (Tok.isNot(tok::eod) && Tok.isNot(tok::eof))
    Lex(Tok);

  if (Tok.is(tok::eod)) {
    assert(FoundLexer && "Can't get end of line without an active lexer");
    FoundLexer->LexingRawMode = false;

    if (LexerWasInPPMode)
      return;

    FoundLexer->ParsingDirective = false;
    return Lex(Tok);
  }

  assert(!FoundLexer && "Scanner should return EOD before EOF in PP mode");
}
