#include "neverc/Scan/PrepEngine.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/ExpansionLexer.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/LexerCore.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/MacroArgStorage.h"
#include "neverc/Scan/MacroRecord.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "neverc/Scan/PrepOptions.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Scan/TokenScratch.h"
#include "neverc/Scan/LexDiag.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Capacity.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Construction & initialization
// ===----------------------------------------------------------------------===

PrepEngine::PrepEngine(std::shared_ptr<PrepOptions> PPOpts,
                       DiagnosticsEngine &diags, const LangOptions &opts,
                       SourceManager &SM, IncludeResolver &Headers,
                       IdentifierInfoLookup *IILookup, bool OwnsHeaders)
    : PPOpts(std::move(PPOpts)), Diags(&diags),
      LangOpts(const_cast<LangOptions &>(opts)), FileMgr(Headers.getFileMgr()),
      SourceMgr(SM), ScratchBuf(new TokenScratch(SourceMgr)),
      HeaderInfo(Headers), Identifiers(IILookup),
      PragmaDispatchs(new PragmaRegistry(llvm::StringRef())) {
  OwnsIncludeResolver = OwnsHeaders;
  BuiltinInfo = std::make_unique<Builtin::Context>();

  IncludeMacroStack.reserve(64);

  (Ident__VA_ARGS__ = getIdentifierInfo("__VA_ARGS__"))->setIsPoisoned();
  RecordPoisonReason(Ident__VA_ARGS__, diag::ext_pp_bad_vaargs_use);
  (Ident__VA_OPT__ = getIdentifierInfo("__VA_OPT__"))->setIsPoisoned();
  RecordPoisonReason(Ident__VA_OPT__, diag::ext_pp_bad_vaopt_use);

  InitBuiltinPragmas();

  InitIntrinsicMacros();

  MaxTokens = LangOpts.MaxTokens;
}

PrepEngine::~PrepEngine() {
  assert(BacktrackPositions.empty() && "EnableBacktrack/Backtrack imbalance!");

  CurExpansionLexer.reset();

  for (MacroArgStorage *ArgList = MacroArgCache; ArgList;)
    ArgList = ArgList->deallocate();

  if (OwnsIncludeResolver)
    delete &HeaderInfo;
}

void PrepEngine::Initialize(const TargetInfo &Target) {
  assert((!this->Target || this->Target == &Target) &&
         "Invalid override of target information");
  this->Target = &Target;

  BuiltinInfo->InitializeTarget(Target);

  Identifiers.AddKeywords(LangOpts);

  auto TargetFPEval = Target.getFPEvalMethod();
  setTUFPEvalMethod(TargetFPEval);

  auto LangFPEval = LangOpts.getFPEvalMethod();
  if (LangFPEval == LangOptions::FEM_UnsetOnCommandLine)
    setCurrentFPEvalMethod(SourceLocation(), TargetFPEval);
  else
    setCurrentFPEvalMethod(SourceLocation(), LangFPEval);
}

// ===----------------------------------------------------------------------===
// Debug & diagnostics
// ===----------------------------------------------------------------------===

void PrepEngine::DumpToken(const Token &Tok, bool ShowFlags) const {
  llvm::errs() << tok::getTokenName(Tok.getKind());

  if (!Tok.isAnnotation())
    llvm::errs() << " '" << getSpelling(Tok) << "'";

  if (!ShowFlags)
    return;

  llvm::errs() << "\t";
  if (Tok.isAtStartOfLine())
    llvm::errs() << " [StartOfLine]";
  if (Tok.hasLeadingSpace())
    llvm::errs() << " [LeadingSpace]";
  if (Tok.isExpandDisabled())
    llvm::errs() << " [ExpandDisabled]";
  if (Tok.needsCleaning()) {
    const char *Start = SourceMgr.getCharacterData(Tok.getLocation());
    llvm::errs() << " [UnClean='" << llvm::StringRef(Start, Tok.getLength())
                 << "']";
  }

  llvm::errs() << "\tLoc=<";
  DumpLoc(Tok.getLocation());
  llvm::errs() << ">";
}

void PrepEngine::DumpLoc(SourceLocation Loc) const {
  Loc.print(llvm::errs(), SourceMgr);
}

void PrepEngine::DumpMacroTokens(const MacroRecord &MI) const {
  llvm::errs() << "MACRO: ";
  for (unsigned i = 0, e = MI.getNumTokens(); i != e; ++i) {
    DumpToken(MI.getReplacementToken(i));
    llvm::errs() << "  ";
  }
  llvm::errs() << "\n";
}

void PrepEngine::DumpStats() {
  llvm::errs() << "\n*** PrepEngine Stats:\n";
  llvm::errs() << NumDirectives << " directives found:\n";
  llvm::errs() << "  " << NumDefined << " #define.\n";
  llvm::errs() << "  " << NumUndefined << " #undef.\n";
  llvm::errs() << "  #include/#include_next/#import:\n";
  llvm::errs() << "    " << NumEnteredSourceFiles << " source files entered.\n";
  llvm::errs() << "    " << MaxIncludeStackDepth
               << " max include stack depth\n";
  llvm::errs() << "  " << NumIf << " #if/#ifndef/#ifdef.\n";
  llvm::errs() << "  " << NumElse << " #else/#elif/#elifdef/#elifndef.\n";
  llvm::errs() << "  " << NumEndif << " #endif.\n";
  llvm::errs() << "  " << NumPragma << " #pragma.\n";
  llvm::errs() << NumSkipped << " #if/#ifndef#ifdef regions skipped\n";

  llvm::errs() << NumMacroExpanded << "/" << NumFnMacroExpanded << "/"
               << NumBuiltinMacroExpanded << " obj/fn/builtin macros expanded, "
               << NumFastMacroExpanded << " on the fast path.\n";
  llvm::errs() << (NumFastTokenPaste + NumTokenPaste)
               << " token paste (##) operations performed, "
               << NumFastTokenPaste << " on the fast path.\n";

  llvm::errs() << "\nPrepEngine Memory: " << getTotalMemory() << "B total";

  llvm::errs() << "\n  BumpPtr: " << BP.getTotalMemory();
  llvm::errs() << "\n  Macro Expanded Tokens: "
               << llvm::capacity_in_bytes(MacroExpandedTokens);
  llvm::errs() << "\n  Predefines Buffer: " << Predefines.capacity();
  llvm::errs() << "\n  Macros: " << llvm::capacity_in_bytes(Macros);
  llvm::errs() << "\n  #pragma push_macro Info: "
               << llvm::capacity_in_bytes(PragmaPushMacroRecord);
  llvm::errs() << "\n  Poison Reasons: "
               << llvm::capacity_in_bytes(PoisonReasons);
  llvm::errs() << "\n  Comment Handlers: "
               << llvm::capacity_in_bytes(CommentHandlers) << "\n";
}

PrepEngine::macro_iterator
PrepEngine::macro_begin(bool IncludeExternalMacros) const {
  return Macros.begin();
}

size_t PrepEngine::getTotalMemory() const {
  return BP.getTotalMemory() + llvm::capacity_in_bytes(MacroExpandedTokens) +
         Predefines.capacity() /* Predefines buffer. */
         + llvm::capacity_in_bytes(Macros) +
         llvm::capacity_in_bytes(PragmaPushMacroRecord) +
         llvm::capacity_in_bytes(PoisonReasons) +
         llvm::capacity_in_bytes(CommentHandlers);
}

PrepEngine::macro_iterator
PrepEngine::macro_end(bool IncludeExternalMacros) const {
  return Macros.end();
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
bool tokensMatchMacroBody(const MacroRecord *MI,
                          llvm::ArrayRef<TokenValue> Tokens) {
  const unsigned N = MI->getNumTokens();
  if (Tokens.size() != N)
    return false;
  if (LLVM_UNLIKELY(N == 0))
    return true;

  const auto *Src = Tokens.begin();
  const auto *Def = MI->tokens_begin();
  unsigned i = 0;
  for (; i + 4 <= N; i += 4) {
    if (LLVM_UNLIKELY(Src[i] != Def[i] || Src[i + 1] != Def[i + 1] ||
                      Src[i + 2] != Def[i + 2] || Src[i + 3] != Def[i + 3]))
      return false;
  }
  for (; i < N; ++i) {
    if (LLVM_UNLIKELY(Src[i] != Def[i]))
      return false;
  }
  return true;
}
} // namespace

llvm::StringRef
PrepEngine::getLastMacroWithSpelling(SourceLocation Loc,
                                     llvm::ArrayRef<TokenValue> Tokens) const {
  SourceLocation BestLocation;
  llvm::StringRef BestSpelling;
  const unsigned WantTokens = Tokens.size();

  for (auto I = macro_begin(), E = macro_end(); I != E; ++I) {
    const MacroDirective::DefInfo Def =
        I->second.findDirectiveAtLoc(Loc, SourceMgr);
    if (!Def)
      continue;
    const auto *MI = Def.getMacroRecord();
    if (!MI || !MI->isObjectLike())
      continue;
    if (MI->getNumTokens() != WantTokens)
      continue;
    if (!tokensMatchMacroBody(MI, Tokens))
      continue;
    SourceLocation Location = Def.getLocation();
    if (BestLocation.isInvalid() ||
        (Location.isValid() &&
         SourceMgr.isBeforeInTranslationUnit(BestLocation, Location))) {
      BestLocation = Location;
      BestSpelling = I->first->getName();
    }
  }
  return BestSpelling;
}

// ===----------------------------------------------------------------------===
// Token dispatch & lexing
// ===----------------------------------------------------------------------===

void PrepEngine::refreshDispatch() {
  if (CurLexer)
    CurLexerCallback = DispatchFile;
  else if (CurExpansionLexer)
    CurLexerCallback = DispatchExpansion;
  else
    CurLexerCallback = DispatchCache;
}

llvm::StringRef PrepEngine::getSpelling(const Token &Tok,
                                        llvm::SmallVectorImpl<char> &Buffer,
                                        bool *Invalid) const {
  if (LLVM_LIKELY(Tok.isNot(tok::raw_identifier) && !Tok.hasUCN())) {
    if (const IdentifierInfo *II = Tok.getIdentifierInfo())
      return II->getName();
  }

  if (Tok.needsCleaning())
    Buffer.resize(Tok.getLength());

  const char *Ptr = Buffer.data();
  unsigned Len = getSpelling(Tok, Ptr, Invalid);
  return llvm::StringRef(Ptr, Len);
}

void PrepEngine::WriteScratch(llvm::StringRef Str, Token &Tok,
                              SourceLocation ExpansionLocStart,
                              SourceLocation ExpansionLocEnd) {
  Tok.setLength(Str.size());

  const char *DestPtr;
  SourceLocation Loc = ScratchBuf->getToken(Str.data(), Str.size(), DestPtr);

  if (ExpansionLocStart.isValid())
    Loc = SourceMgr.createExpansionLoc(Loc, ExpansionLocStart, ExpansionLocEnd,
                                       Str.size());
  Tok.setLocation(Loc);

  if (Tok.is(tok::raw_identifier))
    Tok.setRawIdentifierData(DestPtr);
  else if (Tok.isLiteral())
    Tok.setLiteralData(DestPtr);
}

SourceLocation PrepEngine::SplitTokenLoc(SourceLocation Loc, unsigned Length) {
  auto &SM = getSourceManager();
  SourceLocation SpellingLoc = SM.getSpellingLoc(Loc);
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(SpellingLoc);
  bool Invalid = false;
  llvm::StringRef Buffer = SM.getBufferData(LocInfo.first, &Invalid);
  if (Invalid)
    return SourceLocation();

  const char *DestPtr;
  SourceLocation Spelling =
      ScratchBuf->getToken(Buffer.data() + LocInfo.second, Length, DestPtr);
  return SM.createTokenSplitLoc(Spelling, Loc, Loc.getLocWithOffset(Length));
}

void PrepEngine::InitMainInput() {
  assert(NumEnteredSourceFiles == 0 && "Cannot reenter the main file!");
  FileID MainFileID = SourceMgr.getMainFileID();

  if (!SourceMgr.isLoadedFileID(MainFileID)) {
    PushSourceFile(MainFileID, nullptr, SourceLocation());
    if (OptionalFileEntryRef FE = SourceMgr.getFileEntryRefForID(MainFileID))
      markIncluded(*FE);
  }

  std::unique_ptr<llvm::MemoryBuffer> SB =
      llvm::MemoryBuffer::getMemBufferCopy(Predefines, "<built-in>");
  assert(SB && "Cannot create predefined source buffer");
  FileID FID = SourceMgr.createFileID(std::move(SB));
  assert(FID.isValid() && "Could not create FileID for predefines?");
  setPredefinesFileID(FID);

  PushSourceFile(FID, nullptr, SourceLocation());
}

void PrepEngine::FiniMainInput() {
  if (Callbacks)
    Callbacks->EndOfMainFile();
}

__attribute__((noinline, cold)) IdentifierInfo *
PrepEngine::ResolveRawIdentSlow(Token &Identifier) const {
  assert(!Identifier.getRawIdentifier().empty() && "No raw identifier data!");

  IdentifierInfo *II;
  llvm::SmallString<128> IdentifierBuffer;
  llvm::StringRef CleanedStr = getSpelling(Identifier, IdentifierBuffer);

  if (Identifier.hasUCN()) {
    llvm::SmallString<64> UCNIdentifierBuffer;
    expandUCNs(UCNIdentifierBuffer, CleanedStr);
    II = getIdentifierInfo(UCNIdentifierBuffer);
  } else {
    II = getIdentifierInfo(CleanedStr);
  }

  Identifier.setIdentifierInfo(II);
  Identifier.setKind(II->getTokenID());

  return II;
}

void PrepEngine::RecordPoisonReason(IdentifierInfo *II, unsigned DiagID) {
  PoisonReasons[II] = DiagID;
}

__attribute__((noinline, cold)) void
PrepEngine::DiagPoisonedIdent(Token &Identifier) {
  assert(Identifier.getIdentifierInfo() &&
         "Can't handle identifiers without identifier info!");
  llvm::DenseMap<IdentifierInfo *, unsigned>::const_iterator it =
      PoisonReasons.find(Identifier.getIdentifierInfo());
  if (it == PoisonReasons.end())
    Diag(Identifier, diag::err_pp_used_poisoned_id);
  else
    Diag(Identifier, it->second) << Identifier.getIdentifierInfo();
}

__attribute__((hot)) bool PrepEngine::ResolveIdentifier(Token &Identifier) {
  assert(Identifier.getIdentifierInfo() &&
         "Can't handle identifiers without identifier info!");

  IdentifierInfo &II = *Identifier.getIdentifierInfo();

  if (LLVM_UNLIKELY(LangOpts.MicrosoftExt && II.getLength() == 24 &&
                    II.getName() == "DEPRECATE_DDK_FUNCTIONS")) {
    LangOpts.Kernel = true;
  }

  if (LLVM_UNLIKELY(II.isPoisoned() && CurPPLexer)) {
    DiagPoisonedIdent(Identifier);
  }

  if (const MacroDefinition MD = getMacroDefinition(&II)) {
    const auto *MI = MD.getMacroRecord();
    assert(MI && "macro definition with no macro info?");
    if (LLVM_LIKELY(!DisableMacroExpansion)) {
      if (LLVM_LIKELY(!Identifier.isExpandDisabled() && MI->isEnabled())) {
        if (!MI->isFunctionLike() || ProbeLeftParen())
          return BeginMacroExpansion(Identifier, MD);
      } else {
        Identifier.setFlag(Token::DisableExpand);
        if (MI->isObjectLike() || ProbeLeftParen())
          Diag(Identifier, diag::pp_disabled_macro_expansion);
      }
    }
  }

  if (LLVM_UNLIKELY(II.isFutureCompatKeyword()) && !DisableMacroExpansion) {
    Diag(Identifier,
         getIdentifierTable().getFutureCompatDiagKind(II, getLangOpts()))
        << II.getName();
    II.setIsFutureCompatKeyword(false);
  }

  if (LLVM_UNLIKELY(II.isExtensionToken()) && !DisableMacroExpansion)
    Diag(Identifier, diag::ext_token_used);

  return true;
}

LLVM_ATTRIBUTE_NOINLINE
void PrepEngine::LexSlow(Token &Result) {
  ++LexLevel;

  if (LLVM_LIKELY(CurLexerCallback == &DispatchFile)) {
    while (true) {
      if (LLVM_LIKELY(CurLexer->Lex(Result)))
        break;
      if (LLVM_UNLIKELY(CurLexerCallback != &DispatchFile)) {
        while (!CurLexerCallback(*this, Result))
          ;
        break;
      }
    }
  } else {
    while (!CurLexerCallback(*this, Result))
      ;
  }

  unsigned CurLevel = --LexLevel;
  bool NotReinjected = !Result.getFlag(Token::IsReinjected);

  if (LLVM_LIKELY(CurLevel == 0) && LLVM_LIKELY(NotReinjected))
    ++TokenCount;

  if (LLVM_UNLIKELY(OnToken) && NotReinjected &&
      (LLVM_LIKELY(CurLevel == 0) || LLVM_UNLIKELY(PreprocessToken)))
    OnToken(Result);
}

void PrepEngine::ConsumeAllTokens(std::vector<Token> *Tokens) {
  Token Tok;
  if (Tokens) {
    Tokens->reserve(1u << 17);
    for (;;) {
      Lex(Tok);
      auto K = Tok.getKind();
      if (LLVM_UNLIKELY(K == tok::unknown || K == tok::eof || K == tok::eod))
        return;
      Tokens->emplace_back(Tok);

      if (LLVM_UNLIKELY(Tokens->size() == Tokens->capacity()))
        Tokens->reserve(Tokens->capacity() + (Tokens->capacity() >> 1));
    }
  } else {
    for (;;) {
      Lex(Tok);
      auto K = Tok.getKind();
      if (LLVM_UNLIKELY(K == tok::unknown || K == tok::eof || K == tok::eod))
        return;
    }
  }
}

bool PrepEngine::LexIncludePathTok(Token &FilenameTok,
                                   bool AllowMacroExpansion) {
  if (CurPPLexer)
    CurPPLexer->LexIncludePath(FilenameTok);
  else
    Lex(FilenameTok);

  llvm::SmallString<256> FilenameBuffer;
  if (FilenameTok.is(tok::less) && AllowMacroExpansion) {
    bool StartOfLine = FilenameTok.isAtStartOfLine();
    bool LeadingSpace = FilenameTok.hasLeadingSpace();
    bool LeadingEmptyMacro = FilenameTok.hasLeadingEmptyMacro();

    SourceLocation Start = FilenameTok.getLocation();
    SourceLocation End;
    FilenameBuffer.push_back('<');

    for (;;) {
      Lex(FilenameTok);
      auto K = FilenameTok.getKind();
      if (LLVM_UNLIKELY(K == tok::greater))
        break;
      if (LLVM_UNLIKELY(K == tok::eod || K == tok::eof)) {
        Diag(FilenameTok.getLocation(), diag::err_expected) << tok::greater;
        Diag(Start, diag::note_matching) << tok::less;
        return true;
      }

      End = FilenameTok.getLocation();

      if (FilenameTok.hasLeadingSpace())
        FilenameBuffer.push_back(' ');

      size_t PreAppendSize = FilenameBuffer.size();
      unsigned TokLen = FilenameTok.getLength();
      FilenameBuffer.resize(PreAppendSize + TokLen);

      const char *BufPtr = &FilenameBuffer[PreAppendSize];
      unsigned ActualLen = getSpelling(FilenameTok, BufPtr);

      if (LLVM_UNLIKELY(BufPtr != &FilenameBuffer[PreAppendSize]))
        std::memcpy(&FilenameBuffer[PreAppendSize], BufPtr, ActualLen);

      if (LLVM_UNLIKELY(TokLen != ActualLen))
        FilenameBuffer.resize(PreAppendSize + ActualLen);
    }

    FilenameTok.startToken();
    FilenameTok.setKind(tok::header_name);
    FilenameTok.setFlagValue(Token::StartOfLine, StartOfLine);
    FilenameTok.setFlagValue(Token::LeadingSpace, LeadingSpace);
    FilenameTok.setFlagValue(Token::LeadingEmptyMacro, LeadingEmptyMacro);
    WriteScratch(FilenameBuffer, FilenameTok, Start, End);
  } else if (FilenameTok.is(tok::string_literal) && AllowMacroExpansion) {
    llvm::StringRef Str = getSpelling(FilenameTok, FilenameBuffer);
    if (Str.size() >= 2 && Str.front() == '"' && Str.back() == '"')
      FilenameTok.setKind(tok::header_name);
  }

  return false;
}

bool PrepEngine::CompleteStringLiteral(Token &Result, std::string &String,
                                       const char *DiagnosticTag,
                                       bool AllowMacroExpansion) {
  if (Result.isNot(tok::string_literal)) {
    Diag(Result, diag::err_expected_string_literal)
        << /*Source='in...'*/ 0 << DiagnosticTag;
    return false;
  }

  llvm::SmallVector<Token, 4> StrToks;
  do {
    StrToks.push_back(Result);

    if (AllowMacroExpansion)
      Lex(Result);
    else
      LexWithoutExpansion(Result);
  } while (Result.is(tok::string_literal));

  StringLiteralParser Literal(StrToks, *this);
  assert(Literal.isOrdinary() && "Didn't allow wide strings in");

  if (Literal.hadError)
    return false;

  String = std::string(Literal.getString());
  return true;
}

bool PrepEngine::parseBasicInteger(Token &Tok, uint64_t &Value) {
  assert(Tok.is(tok::numeric_constant));
  llvm::SmallString<8> IntegerBuffer;
  bool NumberInvalid = false;
  llvm::StringRef Spelling = getSpelling(Tok, IntegerBuffer, &NumberInvalid);
  if (NumberInvalid)
    return false;
  NumericLiteralParser Literal(Spelling, Tok.getLocation(), getSourceManager(),
                               getLangOpts(), getTargetInfo(),
                               getDiagnostics());
  if (Literal.hadError || !Literal.isIntegerLiteral())
    return false;
  llvm::APInt APVal(64, 0);
  if (Literal.getIntegerValue(APVal))
    return false;
  Lex(Tok);
  Value = APVal.getLimitedValue();
  return true;
}

// ===----------------------------------------------------------------------===
// Comment handling & macro warnings
// ===----------------------------------------------------------------------===

void PrepEngine::addCommentHandler(CommentHandler *Handler) {
  assert(Handler && "NULL comment handler");
  assert(!llvm::is_contained(CommentHandlers, Handler) &&
         "Comment handler already registered");
  CommentHandlers.push_back(Handler);
}

void PrepEngine::removeCommentHandler(CommentHandler *Handler) {
  auto Pos = llvm::find(CommentHandlers, Handler);
  assert(Pos != CommentHandlers.end() && "Comment handler not registered");
  CommentHandlers.erase(Pos);
}

bool PrepEngine::DispatchComment(Token &result, SourceRange Comment) {
  const auto N = CommentHandlers.size();
  if (LLVM_LIKELY(N == 0))
    return false;

  bool AnyPending = false;
  for (size_t i = 0; i < N; ++i)
    AnyPending |= CommentHandlers[i]->DispatchComment(*this, Comment);

  if (!AnyPending || getCommentRetentionState())
    return false;
  Lex(result);
  return true;
}

void PrepEngine::warnMacroDeprecation(const Token &Identifier) const {
  const MacroAnnotations &A =
      getMacroAnnotations(Identifier.getIdentifierInfo());
  assert(A.DeprecationInfo &&
         "Macro deprecation warning without recorded annotation!");
  const MacroAnnotationInfo &Info = *A.DeprecationInfo;
  if (Info.Message.empty())
    Diag(Identifier, diag::warn_pragma_deprecated_macro_use)
        << Identifier.getIdentifierInfo() << 0;
  else
    Diag(Identifier, diag::warn_pragma_deprecated_macro_use)
        << Identifier.getIdentifierInfo() << 1 << Info.Message;
  Diag(Info.Location, diag::note_pp_macro_annotation) << 0;
}

void PrepEngine::warnRestrictExpansion(const Token &Identifier) const {
  const MacroAnnotations &A =
      getMacroAnnotations(Identifier.getIdentifierInfo());
  assert(A.RestrictExpansionInfo &&
         "Macro restricted expansion warning without recorded annotation!");
  const MacroAnnotationInfo &Info = *A.RestrictExpansionInfo;
  if (Info.Message.empty())
    Diag(Identifier, diag::warn_pragma_restrict_expansion_macro_use)
        << Identifier.getIdentifierInfo() << 0;
  else
    Diag(Identifier, diag::warn_pragma_restrict_expansion_macro_use)
        << Identifier.getIdentifierInfo() << 1 << Info.Message;
  Diag(Info.Location, diag::note_pp_macro_annotation) << 1;
}

void PrepEngine::warnFinalMacro(const Token &Identifier, bool IsUndef) const {
  const MacroAnnotations &A =
      getMacroAnnotations(Identifier.getIdentifierInfo());
  assert(A.FinalAnnotationLoc &&
         "Final macro warning without recorded annotation!");

  Diag(Identifier, diag::warn_pragma_final_macro)
      << Identifier.getIdentifierInfo() << (IsUndef ? 0 : 1);
  Diag(*A.FinalAnnotationLoc, diag::note_pp_macro_annotation) << 2;
}

// ===----------------------------------------------------------------------===
// Safe buffer opt-out regions
// ===----------------------------------------------------------------------===

bool PrepEngine::isSafeBufferOptOut(const SourceManager &SourceMgr,
                                    const SourceLocation &Loc) const {
  if (LLVM_LIKELY(SafeBufferOptOutMap.empty()))
    return false;

  auto It = llvm::partition_point(
      SafeBufferOptOutMap,
      [&SourceMgr,
       &Loc](const std::pair<SourceLocation, SourceLocation> &Region) {
        return SourceMgr.isBeforeInTranslationUnit(Region.second, Loc);
      });

  if (It != SafeBufferOptOutMap.end())
    return SourceMgr.isBeforeInTranslationUnit(It->first, Loc);

  const auto &Last = SafeBufferOptOutMap.back();
  if (Last.first == Last.second)
    return SourceMgr.isBeforeInTranslationUnit(Last.first, Loc);
  return false;
}

bool PrepEngine::enterOrExitSafeBufferOptOutRegion(bool isEnter,
                                                   const SourceLocation &Loc) {
  if (isEnter) {
    if (isPPInSafeBufferOptOutRegion())
      return true; // invalid enter action
    InSafeBufferOptOutRegion = true;
    CurrentSafeBufferOptOutStart = Loc;

    if (!SafeBufferOptOutMap.empty()) {
      [[maybe_unused]] auto *PrevRegion = &SafeBufferOptOutMap.back();
      assert(PrevRegion->first != PrevRegion->second &&
             "Previous opt-out region must be closed first.");
    }
    // start == end marks an open (unclosed) region.
    SafeBufferOptOutMap.emplace_back(Loc, Loc);
  } else {
    if (!isPPInSafeBufferOptOutRegion())
      return true; // invalid enter action
    InSafeBufferOptOutRegion = false;

    assert(!SafeBufferOptOutMap.empty() &&
           "Misordered safe buffer opt-out regions");
    auto *CurrRegion = &SafeBufferOptOutMap.back();
    assert(CurrRegion->first == CurrRegion->second && "Region already closed");
    CurrRegion->second = Loc;
  }
  return false;
}

bool PrepEngine::isPPInSafeBufferOptOutRegion() {
  return InSafeBufferOptOutRegion;
}
bool PrepEngine::isPPInSafeBufferOptOutRegion(SourceLocation &StartLoc) {
  StartLoc = CurrentSafeBufferOptOutStart;
  return InSafeBufferOptOutRegion;
}

CommentHandler::~CommentHandler() = default;

EmptylineHandler::~EmptylineHandler() = default;
