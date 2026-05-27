#include "PrintPPOutputPrepObserver.h"
#include "neverc/Compiler/PrepOutputOptions.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Scan/MacroRecord.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/SmallVector.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// UnknownPragmaHandler
// ===----------------------------------------------------------------------===

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
    Callbacks->MoveToLine(PragmaTok.getLocation(), /*RequireStartOfLine=*/true);
    Callbacks->OS->write(Prefix, strlen(Prefix));
    Callbacks->setEmittedTokensOnThisLine();

    if (ShouldExpandTokens) {
      auto Toks = std::make_unique<Token[]>(1);
      Toks[0] = PragmaTok;
      PP.PushTokenStream(std::move(Toks), /*NumToks=*/1,
                         /*DisableMacroExpansion=*/false,
                         /*IsReinject=*/false);
      PP.Lex(PragmaTok);
    }

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

// ===----------------------------------------------------------------------===
// Token printing
// ===----------------------------------------------------------------------===

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

// ===----------------------------------------------------------------------===
// Macro dump (-dM mode)
// ===----------------------------------------------------------------------===

typedef std::pair<const IdentifierInfo *, MacroRecord *> id_macro_pair;
int compareMacroIDs(const id_macro_pair *LHS, const id_macro_pair *RHS) {
  return LHS->first->getName().compare(RHS->first->getName());
}

void genAllMacroDefinitions(PrepEngine &PP, llvm::raw_ostream *OS) {
  PP.IgnorePragmas();

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
    if (MI.isBuiltinMacro())
      continue;

    writeMacroDefinition(*MacrosByID[i].first, MI, PP, OS);
    *OS << '\n';
  }
}

} // anonymous namespace

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

  PP.SetCommentRetentionState(Opts.ShowComments, Opts.ShowMacroComments);

  PrintPPOutputPrepObserver *Callbacks = new PrintPPOutputPrepObserver(
      PP, OS, !Opts.ShowLineMarkers, Opts.ShowMacros,
      Opts.ShowIncludeDirectives, Opts.UseLineDirectives,
      Opts.MinimizeWhitespace, Opts.DirectivesOnly, Opts.KeepSystemIncludes);

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

  PP.InitMainInput();
  if (Opts.DirectivesOnly)
    PP.SetMacroExpansionOnlyInDirectives();

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

  printPreprocessedTokens(PP, Tok, Callbacks);
  *OS << '\n';

  PP.RemovePragmaDispatch(MicrosoftExtHandler.get());
  PP.RemovePragmaDispatch("GCC", GCCHandler.get());
  PP.RemovePragmaDispatch("neverc", NeverCHandler.get());
}
