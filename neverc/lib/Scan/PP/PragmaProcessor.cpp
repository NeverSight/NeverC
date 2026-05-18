#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/MacroGuardValidator.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/LexDiag.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <memory>
#include <string>
#include <utility>

#ifdef __AVX2__
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

using namespace neverc;
// ===----------------------------------------------------------------------===
// PragmaDispatch & PragmaRegistry
// ===----------------------------------------------------------------------===

PragmaDispatch::~PragmaDispatch() = default;

EmptyPragmaDispatch::EmptyPragmaDispatch(llvm::StringRef Name)
    : PragmaDispatch(Name) {}

void EmptyPragmaDispatch::ProcessPragma(PrepEngine &PP,
                                        PragmaIntroducer Introducer,
                                        Token &FirstToken) {}

// ===----------------------------------------------------------------------===
// PragmaRegistry
// ===----------------------------------------------------------------------===

PragmaDispatch *PragmaRegistry::FindHandler(llvm::StringRef Name,
                                            bool IgnoreNull) const {
  if (auto I = Handlers.find(Name); I != Handlers.end())
    return I->getValue().get();
  if (IgnoreNull)
    return nullptr;
  if (auto I = Handlers.find(llvm::StringRef()); I != Handlers.end())
    return I->getValue().get();
  return nullptr;
}

void PragmaRegistry::AddPragma(PragmaDispatch *Handler) {
  assert(!Handlers.contains(Handler->getName()) &&
         "A handler with this name is already registered in this namespace");
  Handlers[Handler->getName()].reset(Handler);
}

void PragmaRegistry::RemovePragmaDispatch(PragmaDispatch *Handler) {
  auto I = Handlers.find(Handler->getName());
  assert(I != Handlers.end() && "Handler not registered in this namespace");
  I->getValue().release();
  Handlers.erase(I);
}

void PragmaRegistry::ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                                   Token &Tok) {
  PP.LexWithoutExpansion(Tok);

  PragmaDispatch *Handler =
      FindHandler(Tok.getIdentifierInfo() ? Tok.getIdentifierInfo()->getName()
                                          : llvm::StringRef(),
                  /*IgnoreNull=*/false);
  if (!Handler) {
#ifndef _WIN32
    PP.Diag(Tok, diag::warn_pragma_ignored);
#endif
    return;
  }

  Handler->ProcessPragma(PP, Introducer, Tok);
}

// ===----------------------------------------------------------------------===
// Pragma directive handling
// ===----------------------------------------------------------------------===

namespace {
struct TokenCollector {
  PrepEngine &Self;
  bool Collect;
  llvm::SmallVector<Token, 3> Tokens;
  Token &Tok;

  void lex() {
    if (Collect)
      Tokens.push_back(Tok);
    Self.Lex(Tok);
  }

  void revert() {
    assert(Collect && "did not collect tokens");
    assert(!Tokens.empty() && "collected unexpected number of tokens");

    auto Toks = std::make_unique<Token[]>(Tokens.size());
    std::copy(Tokens.begin() + 1, Tokens.end(), Toks.get());
    Toks[Tokens.size() - 1] = Tok;
    Self.PushTokenStream(std::move(Toks), Tokens.size(),
                         /*DisableMacroExpansion*/ true,
                         /*IsReinject*/ true);

    Tok = *Tokens.begin();
  }
};
} // namespace

// ===----------------------------------------------------------------------===
// PrepEngine pragma execution
// ===----------------------------------------------------------------------===

void PrepEngine::ExecPragma(PragmaIntroducer Introducer) {
  if (Callbacks)
    Callbacks->PragmaDirective(Introducer.Loc, Introducer.Kind);

  if (!PragmasEnabled)
    return;

  ++NumPragma;

  Token Tok;
  PragmaDispatchs->ProcessPragma(*this, Introducer, Tok);

  if ((CurExpansionLexer && CurExpansionLexer->isParsingDirective()) ||
      (CurPPLexer && CurPPLexer->ParsingDirective))
    DiscardDirectiveLine();
}

void PrepEngine::ExpandUnderscorePragma(Token &Tok) {
  // _Pragma operators are executed after full macro replacement completes,
  // in two situations:
  //  1) on the final output token sequence of preprocessing, and
  //  2) on token sequences formed as the macro-replaced token sequence of a
  //     macro argument
  //
  // Case #2 appears to be a wording bug: only _Pragmas that would survive to
  // the end of phase 4 should actually be executed. Discussion on the WG14
  // mailing list suggests that a _Pragma operator is notionally checked early,
  // but only pragmas that survive to the end of phase 4 should be executed.
  //
  // In Case #2, we check the syntax now, but then put the tokens back into the
  // token stream for later consumption.

  TokenCollector Toks = {*this, InMacroArgPreExpansion, {}, Tok};

  // Remember the pragma token location.
  SourceLocation PragmaLoc = Tok.getLocation();

  // Read the '('.
  Toks.lex();
  if (Tok.isNot(tok::l_paren)) {
    Diag(PragmaLoc, diag::err__Pragma_malformed);
    return;
  }

  // Read the '"..."'.
  Toks.lex();
  if (!tok::isStringLiteral(Tok.getKind())) {
    Diag(PragmaLoc, diag::err__Pragma_malformed);
    // Skip bad tokens, and the ')', if present.
    if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::eof))
      Lex(Tok);
    while (Tok.isNot(tok::r_paren) && !Tok.isAtStartOfLine() &&
           Tok.isNot(tok::eof))
      Lex(Tok);
    if (Tok.is(tok::r_paren))
      Lex(Tok);
    return;
  }

  // Remember the string.
  Token StrTok = Tok;

  // Read the ')'.
  Toks.lex();
  if (Tok.isNot(tok::r_paren)) {
    Diag(PragmaLoc, diag::err__Pragma_malformed);
    return;
  }

  // If we're expanding a macro argument, put the tokens back.
  if (InMacroArgPreExpansion) {
    Toks.revert();
    return;
  }

  SourceLocation RParenLoc = Tok.getLocation();
  bool Invalid = false;
  llvm::SmallString<64> StrVal;
  StrVal.resize(StrTok.getLength());
  llvm::StringRef StrValRef = getSpelling(StrTok, StrVal, &Invalid);
  if (Invalid) {
    Diag(PragmaLoc, diag::err__Pragma_malformed);
    return;
  }

  assert(StrValRef.size() <= StrVal.size());

  // If the token was spelled somewhere else, copy it.
  if (StrValRef.begin() != StrVal.begin())
    StrVal.assign(StrValRef);
  // Truncate if necessary.
  else if (StrValRef.size() != StrVal.size())
    StrVal.resize(StrValRef.size());

  // Destringize the _Pragma argument.
  prepare_PragmaString(StrVal);

  // Plop the string (including the newline and trailing null) into a buffer
  // where we can lex it.
  Token TmpTok;
  TmpTok.startToken();
  WriteScratch(StrVal, TmpTok);
  SourceLocation TokLoc = TmpTok.getLocation();

  SourceScanner *TL = SourceScanner::CreatePragmaScanner(
      TokLoc, PragmaLoc, RParenLoc, StrVal.size(), *this);

  PushLexer(TL, nullptr);

  // With everything set up, lex this as a #pragma directive.
  ExecPragma({PIK__Pragma, PragmaLoc});

  // Finally, return whatever came after the pragma directive.
  return Lex(Tok);
}

void neverc::prepare_PragmaString(llvm::SmallVectorImpl<char> &StrVal) {
  char First = StrVal[0];
  if (First == 'L' || First == 'U' || (First == 'u' && StrVal[1] != '8'))
    StrVal.erase(StrVal.begin());
  else if (First == 'u')
    StrVal.erase(StrVal.begin(), StrVal.begin() + 2);

  if (StrVal[0] == 'R') {
    assert(StrVal[1] == '"' && StrVal[StrVal.size() - 1] == '"' &&
           "Invalid raw string token!");

    unsigned NumDChars = 0;
    while (StrVal[2 + NumDChars] != '(') {
      assert(NumDChars < (StrVal.size() - 5) / 2 &&
             "Invalid raw string token!");
      ++NumDChars;
    }
    assert(StrVal[StrVal.size() - 2 - NumDChars] == ')');

    StrVal.erase(StrVal.begin(), StrVal.begin() + 2 + NumDChars);
    StrVal.erase(StrVal.end() - 1 - NumDChars, StrVal.end());
  } else {
    assert(StrVal[0] == '"' && StrVal[StrVal.size() - 1] == '"' &&
           "Invalid string token!");

    size_t SrcSize = StrVal.size() - 1;
    unsigned ResultPos = 1;
    size_t i = 1;

#if defined(__SSE2__)
    {
      const __m128i Backslash = _mm_set1_epi8('\\');
      for (; i + 16 <= SrcSize; i += 16) {
        __m128i V = _mm_loadu_si128((const __m128i *)(&StrVal[i]));
        unsigned Mask = _mm_movemask_epi8(_mm_cmpeq_epi8(V, Backslash));
        if (LLVM_LIKELY(Mask == 0)) {
          std::memmove(&StrVal[ResultPos], &StrVal[i], 16);
          ResultPos += 16;
        } else {
          for (unsigned j = 0; j < 16 && i + j < SrcSize; ++j) {
            if (StrVal[i + j] == '\\' && i + j + 1 < SrcSize &&
                (StrVal[i + j + 1] == '\\' || StrVal[i + j + 1] == '"'))
              ++j;
            StrVal[ResultPos++] = StrVal[i + j];
          }
          i += 16;
          goto scalar_tail;
        }
      }
    }
  scalar_tail:
#elif defined(__aarch64__) && defined(__ARM_NEON)
    {
      const uint8x16_t Backslash = vdupq_n_u8('\\');
      for (; i + 16 <= SrcSize; i += 16) {
        uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(&StrVal[i]));
        uint8x16_t Hit = vceqq_u8(V, Backslash);
        if (LLVM_LIKELY(vmaxvq_u8(Hit) == 0)) {
          std::memmove(&StrVal[ResultPos], &StrVal[i], 16);
          ResultPos += 16;
        } else {
          for (unsigned j = 0; j < 16 && i + j < SrcSize; ++j) {
            if (StrVal[i + j] == '\\' && i + j + 1 < SrcSize &&
                (StrVal[i + j + 1] == '\\' || StrVal[i + j + 1] == '"'))
              ++j;
            StrVal[ResultPos++] = StrVal[i + j];
          }
          i += 16;
          break;
        }
      }
    }
#endif
    for (; i < SrcSize; ++i) {
      if (StrVal[i] == '\\' && i + 1 < SrcSize &&
          (StrVal[i + 1] == '\\' || StrVal[i + 1] == '"'))
        ++i;
      StrVal[ResultPos++] = StrVal[i];
    }
    StrVal.erase(StrVal.begin() + ResultPos, StrVal.end() - 1);
  }

  StrVal[0] = ' ';
  StrVal[StrVal.size() - 1] = '\n';
}

void PrepEngine::ExpandMsPragma(Token &Tok) {
  // During macro pre-expansion, check the syntax now but put the tokens back
  // into the token stream for later consumption. Same as
  // ExpandUnderscorePragma.
  TokenCollector Toks = {*this, InMacroArgPreExpansion, {}, Tok};

  // Remember the pragma token location.
  SourceLocation PragmaLoc = Tok.getLocation();

  // Read the '('.
  Toks.lex();
  if (Tok.isNot(tok::l_paren)) {
    Diag(PragmaLoc, diag::err__Pragma_malformed);
    return;
  }
  llvm::SmallVector<Token, 32> PragmaToks;
  int NumParens = 0;
  Toks.lex();
  while (Tok.isNot(tok::eof)) {
    PragmaToks.push_back(Tok);
    if (Tok.is(tok::l_paren))
      NumParens++;
    else if (Tok.is(tok::r_paren) && NumParens-- == 0)
      break;
    Toks.lex();
  }

  if (Tok.is(tok::eof)) {
    Diag(PragmaLoc, diag::err_unterminated___pragma);
    return;
  }

  // If we're expanding a macro argument, put the tokens back.
  if (InMacroArgPreExpansion) {
    Toks.revert();
    return;
  }

  PragmaToks.front().setFlag(Token::LeadingSpace);

  // Replace the ')' with an EOD to mark the end of the pragma.
  PragmaToks.back().setKind(tok::eod);

  auto TokArray = std::make_unique<Token[]>(PragmaToks.size());
  std::copy(PragmaToks.begin(), PragmaToks.end(), TokArray.get());

  // Push the tokens onto the stack.
  PushTokenStream(std::move(TokArray), PragmaToks.size(), true,
                  /*IsReinject*/ false);

  // With everything set up, lex this as a #pragma directive.
  ExecPragma({PIK___pragma, PragmaLoc});

  // Finally, return whatever came after the pragma directive.
  return Lex(Tok);
}

void PrepEngine::ExecPragmaOnce(Token &OnceTok) {
  if (isInPrimaryFile() && !getLangOpts().IsHeaderFile) {
    Diag(OnceTok, diag::pp_pragma_once_in_main_file);
    return;
  }

  // Mark the file as a once-only file now.
  HeaderInfo.MarkFileIncludeOnce(*getCurrentFileLexer()->getFileEntry());
}

void PrepEngine::ExecPragmaMark(Token &MarkTok) {
  assert(CurPPLexer && "No current lexer?");

  llvm::SmallString<64> Buffer;
  CurLexer->drainDirectiveLine(&Buffer);
  if (Callbacks)
    Callbacks->PragmaMark(MarkTok.getLocation(), Buffer);
}

void PrepEngine::ExecPragmaPoison() {
  Token Tok;

  while (true) {
    // Read the next token to poison.  While doing this, pretend that we are
    // skipping while reading the identifier to poison.
    // This avoids errors on code like:
    //   #pragma GCC poison X
    //   #pragma GCC poison X
    if (CurPPLexer)
      CurPPLexer->LexingRawMode = true;
    LexWithoutExpansion(Tok);
    if (CurPPLexer)
      CurPPLexer->LexingRawMode = false;

    // If we reached the end of line, we're done.
    if (Tok.is(tok::eod))
      return;

    // Can only poison identifiers.
    if (Tok.isNot(tok::raw_identifier)) {
      Diag(Tok, diag::err_pp_invalid_poison);
      return;
    }

    // Look up the identifier info for the token.  We disabled identifier lookup
    // by saying we're skipping contents, so we need to do this manually.
    IdentifierInfo *II = ResolveRawIdent(Tok);

    // Already poisoned.
    if (II->isPoisoned())
      continue;

    // If this is a macro identifier, emit a warning.
    if (isMacroDefined(II))
      Diag(Tok, diag::pp_poisoning_existing_macro);

    // Finally, poison it!
    II->setIsPoisoned();
  }
}

void PrepEngine::ExecPragmaSysHeader(Token &SysHeaderTok) {
  if (isInPrimaryFile()) {
    Diag(SysHeaderTok, diag::pp_pragma_sysheader_in_main_file);
    return;
  }

  LexerCore *TheLexer = getCurrentFileLexer();

  // Mark the file as a system header.
  HeaderInfo.MarkFileSystemHeader(*TheLexer->getFileEntry());

  PresumedLoc PLoc = SourceMgr.getPresumedLoc(SysHeaderTok.getLocation());
  if (PLoc.isInvalid())
    return;

  unsigned FilenameID = SourceMgr.getLineTableFilenameID(PLoc.getFilename());

  // Notify the client, if desired, that we are in a new source file.
  if (Callbacks)
    Callbacks->FileChanged(SysHeaderTok.getLocation(),
                           PrepObserver::SystemHeaderPragma, SrcMgr::C_System);

  SourceMgr.AddLineNote(SysHeaderTok.getLocation(), PLoc.getLine() + 1,
                        FilenameID, /*IsEntry=*/false, /*IsExit=*/false,
                        SrcMgr::C_System);
}

void PrepEngine::ExecPragmaDep(Token &DependencyTok) {
  Token FilenameTok;
  if (LexIncludePathTok(FilenameTok, /*AllowConcatenation*/ false))
    return;

  // If the next token wasn't a header-name, diagnose the error.
  if (FilenameTok.isNot(tok::header_name)) {
    Diag(FilenameTok.getLocation(), diag::err_pp_expects_filename);
    return;
  }

  // Reserve a buffer to get the spelling.
  llvm::SmallString<128> FilenameBuffer;
  bool Invalid = false;
  llvm::StringRef Filename = getSpelling(FilenameTok, FilenameBuffer, &Invalid);
  if (Invalid)
    return;

  bool isAngled = extractIncludeFilename(FilenameTok.getLocation(), Filename);
  // If extractIncludeFilename set the start ptr to null, there was an
  // error.
  if (Filename.empty())
    return;

  // Search include directories for this file.
  OptionalFileEntryRef File =
      ResolveInclude(FilenameTok.getLocation(), Filename, isAngled, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
  if (!File) {
    if (!SuppressIncludeNotFoundError)
      Diag(FilenameTok, diag::err_pp_file_not_found) << Filename;
    return;
  }

  OptionalFileEntryRef CurFile = getCurrentFileLexer()->getFileEntry();

  // If this file is older than the file it depends on, emit a diagnostic.
  if (CurFile && CurFile->getModificationTime() < File->getModificationTime()) {
    // Lex tokens at the end of the message and include them in the message.
    std::string Message;
    Lex(DependencyTok);
    while (DependencyTok.isNot(tok::eod)) {
      Message += getSpelling(DependencyTok) + " ";
      Lex(DependencyTok);
    }

    // Remove the trailing ' ' if present.
    if (!Message.empty())
      Message.erase(Message.end() - 1);
    Diag(FilenameTok, diag::pp_out_of_date_dependency) << Message;
  }
}

IdentifierInfo *PrepEngine::ParsePragmaPushPop(Token &Tok) {
  // Remember the pragma token location.
  Token PragmaTok = Tok;

  // Read the '('.
  Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    Diag(PragmaTok.getLocation(), diag::err_pragma_push_pop_macro_malformed)
        << getSpelling(PragmaTok);
    return nullptr;
  }

  // Read the macro name string.
  Lex(Tok);
  if (Tok.isNot(tok::string_literal)) {
    Diag(PragmaTok.getLocation(), diag::err_pragma_push_pop_macro_malformed)
        << getSpelling(PragmaTok);
    return nullptr;
  }

  // Remember the macro string.
  std::string StrVal = getSpelling(Tok);

  // Read the ')'.
  Lex(Tok);
  if (Tok.isNot(tok::r_paren)) {
    Diag(PragmaTok.getLocation(), diag::err_pragma_push_pop_macro_malformed)
        << getSpelling(PragmaTok);
    return nullptr;
  }

  assert(StrVal[0] == '"' && StrVal[StrVal.size() - 1] == '"' &&
         "Invalid string token!");
  Token MacroTok;
  MacroTok.startToken();
  MacroTok.setKind(tok::raw_identifier);
  WriteScratch(llvm::StringRef(&StrVal[1], StrVal.size() - 2), MacroTok);
  return ResolveRawIdent(MacroTok);
}

void PrepEngine::ExecPragmaPush(Token &PushMacroTok) {
  // Parse the pragma directive and get the macro IdentifierInfo*.
  IdentifierInfo *IdentInfo = ParsePragmaPushPop(PushMacroTok);
  if (!IdentInfo)
    return;
  MacroRecord *MI = getMacroRecord(IdentInfo);

  if (MI) {
    // Allow the original MacroRecord to be redefined later.
    MI->setIsAllowRedefinitionsWithoutWarning(true);
  }

  // Push the cloned MacroRecord so we can retrieve it later.
  PragmaPushMacroRecord[IdentInfo].push_back(MI);
}

void PrepEngine::ExecPragmaPop(Token &PopMacroTok) {
  SourceLocation MessageLoc = PopMacroTok.getLocation();

  // Parse the pragma directive and get the macro IdentifierInfo*.
  IdentifierInfo *IdentInfo = ParsePragmaPushPop(PopMacroTok);
  if (!IdentInfo)
    return;

  // Find the vector<MacroRecord*> associated with the macro.
  llvm::DenseMap<IdentifierInfo *, std::vector<MacroRecord *>>::iterator iter =
      PragmaPushMacroRecord.find(IdentInfo);
  if (iter != PragmaPushMacroRecord.end()) {
    // Forget the MacroRecord currently associated with IdentInfo.
    if (MacroRecord *MI = getMacroRecord(IdentInfo)) {
      if (MI->isWarnIfUnused())
        WarnUnusedMacroLocs.erase(MI->getDefinitionLoc());
      appendMacroDirective(IdentInfo, AllocUndefDirective(MessageLoc));
    }
    MacroRecord *MacroToReInstall = iter->second.back();

    if (MacroToReInstall)
      // Reinstall the previously pushed macro.
      appendDefMacroDirective(IdentInfo, MacroToReInstall, MessageLoc);

    // Pop PragmaPushMacroRecord stack.
    iter->second.pop_back();
    if (iter->second.empty())
      PragmaPushMacroRecord.erase(iter);
  } else {
    Diag(MessageLoc, diag::warn_pragma_pop_macro_no_push)
        << IdentInfo->getName();
  }
}

void PrepEngine::ExecPragmaAlias(Token &Tok) {
  // We will either get a quoted filename or a bracketed filename, and we
  // have to track which we got.  The first filename is the source name,
  // and the second name is the mapped filename.  If the first is quoted,
  // the second must be as well (cannot mix and match quotes and brackets).
  Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::warn_pragma_include_alias_expected) << "(";
    return;
  }

  // We expect either a quoted string literal, or a bracketed name
  Token SourceFilenameTok;
  if (LexIncludePathTok(SourceFilenameTok))
    return;

  llvm::StringRef SourceFileName;
  llvm::SmallString<128> FileNameBuffer;
  if (SourceFilenameTok.is(tok::header_name)) {
    SourceFileName = getSpelling(SourceFilenameTok, FileNameBuffer);
  } else {
    Diag(Tok, diag::warn_pragma_include_alias_expected_filename);
    return;
  }
  FileNameBuffer.clear();

  // Now we expect a comma, followed by another include name
  Lex(Tok);
  if (Tok.isNot(tok::comma)) {
    Diag(Tok, diag::warn_pragma_include_alias_expected) << ",";
    return;
  }

  Token ReplaceFilenameTok;
  if (LexIncludePathTok(ReplaceFilenameTok))
    return;

  llvm::StringRef ReplaceFileName;
  if (ReplaceFilenameTok.is(tok::header_name)) {
    ReplaceFileName = getSpelling(ReplaceFilenameTok, FileNameBuffer);
  } else {
    Diag(Tok, diag::warn_pragma_include_alias_expected_filename);
    return;
  }

  // Finally, we expect the closing paren
  Lex(Tok);
  if (Tok.isNot(tok::r_paren)) {
    Diag(Tok, diag::warn_pragma_include_alias_expected) << ")";
    return;
  }

  // Now that we have the source and target filenames, we need to make sure
  // they're both of the same type (angled vs non-angled)
  llvm::StringRef OriginalSource = SourceFileName;

  bool SourceIsAngled =
      extractIncludeFilename(SourceFilenameTok.getLocation(), SourceFileName);
  bool ReplaceIsAngled =
      extractIncludeFilename(ReplaceFilenameTok.getLocation(), ReplaceFileName);
  if (!SourceFileName.empty() && !ReplaceFileName.empty() &&
      (SourceIsAngled != ReplaceIsAngled)) {
    unsigned int DiagID;
    if (SourceIsAngled)
      DiagID = diag::warn_pragma_include_alias_mismatch_angle;
    else
      DiagID = diag::warn_pragma_include_alias_mismatch_quote;

    Diag(SourceFilenameTok.getLocation(), DiagID)
        << SourceFileName << ReplaceFileName;

    return;
  }

  // Now we can let the include handler know about this mapping
  getIncludeResolver().AddIncludeAlias(OriginalSource, ReplaceFileName);
}

void PrepEngine::AddPragmaDispatch(llvm::StringRef Namespace,
                                   PragmaDispatch *Handler) {
  PragmaRegistry *InsertNS = PragmaDispatchs.get();

  // If this is specified to be in a namespace, step down into it.
  if (!Namespace.empty()) {
    // If there is already a pragma handler with the name of this namespace,
    // we either have an error (directive with the same name as a namespace) or
    // we already have the namespace to insert into.
    if (PragmaDispatch *Existing = PragmaDispatchs->FindHandler(Namespace)) {
      InsertNS = Existing->getIfNamespace();
      assert(InsertNS != nullptr && "Cannot have a pragma namespace and pragma"
                                    " handler with the same name!");
    } else {
      // Otherwise, this namespace doesn't exist yet, create and insert the
      // handler for it.
      InsertNS = new PragmaRegistry(Namespace);
      PragmaDispatchs->AddPragma(InsertNS);
    }
  }
  assert(!InsertNS->FindHandler(Handler->getName()) &&
         "Pragma handler already exists for this identifier!");
  InsertNS->AddPragma(Handler);
}

void PrepEngine::RemovePragmaDispatch(llvm::StringRef Namespace,
                                      PragmaDispatch *Handler) {
  PragmaRegistry *NS = PragmaDispatchs.get();

  // If this is specified to be in a namespace, step down into it.
  if (!Namespace.empty()) {
    PragmaDispatch *Existing = PragmaDispatchs->FindHandler(Namespace);
    assert(Existing && "Namespace containing handler does not exist!");

    NS = Existing->getIfNamespace();
    assert(NS && "Invalid namespace, registered as a regular pragma handler!");
  }

  NS->RemovePragmaDispatch(Handler);

  // If this is a non-default namespace and it is now empty, remove it.
  if (NS != PragmaDispatchs.get() && NS->IsEmpty()) {
    PragmaDispatchs->RemovePragmaDispatch(NS);
    delete NS;
  }
}

bool PrepEngine::LexOnOffSwitch(tok::OnOffSwitch &Result) {
  Token Tok;
  LexWithoutExpansion(Tok);

  if (Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::ext_on_off_switch_syntax);
    return true;
  }
  IdentifierInfo *II = Tok.getIdentifierInfo();
  if (II->isStr("ON"))
    Result = tok::OOS_ON;
  else if (II->isStr("OFF"))
    Result = tok::OOS_OFF;
  else if (II->isStr("DEFAULT"))
    Result = tok::OOS_DEFAULT;
  else {
    Diag(Tok, diag::ext_on_off_switch_syntax);
    return true;
  }

  // Verify that this is followed by EOD.
  LexWithoutExpansion(Tok);
  if (Tok.isNot(tok::eod))
    Diag(Tok, diag::ext_pragma_syntax_eod);
  return false;
}
