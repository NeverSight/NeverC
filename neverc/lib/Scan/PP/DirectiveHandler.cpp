#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/VarArgExpansion.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
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

namespace {
enum ConditionalBranchDiag { CBD_Elif, CBD_Elifdef, CBD_Elifndef };
} // namespace

// ===----------------------------------------------------------------------===
// Directive dispatch
// ===----------------------------------------------------------------------===

class PrepEngine::ResetMacroExpansionHelper {
public:
  ResetMacroExpansionHelper(PrepEngine *pp)
      : PP(pp), save(pp->DisableMacroExpansion) {
    if (pp->MacroExpansionInDirectivesOverride)
      pp->DisableMacroExpansion = false;
  }

  ~ResetMacroExpansionHelper() { PP->DisableMacroExpansion = save; }

private:
  PrepEngine *PP;
  bool save;
};

// ===----------------------------------------------------------------------===
// Directive dispatch & conditional compilation
// ===----------------------------------------------------------------------===

__attribute__((hot)) void PrepEngine::DispatchDirective(Token &Result) {
  CurPPLexer->ParsingDirective = true;
  if (CurLexer)
    CurLexer->SetKeepWhitespaceMode(false);

  bool ImmediatelyAfterTopLevelIfndef =
      CurPPLexer->MIOpt.getImmediatelyAfterTopLevelIfndef();
  CurPPLexer->MIOpt.resetImmediatelyAfterTopLevelIfndef();

  ++NumDirectives;

  bool ReadAnyTokensBeforeDirective =
      CurPPLexer->MIOpt.getHasReadAnyTokensVal();

  Token SavedHash = Result;
  LexWithoutExpansion(Result);

  if (InMacroArgStorage) {
    if (IdentifierInfo *II = Result.getIdentifierInfo()) {
      switch (II->getPPKeywordID()) {
      case tok::pp_include:
      case tok::pp_import:
      case tok::pp_include_next:
      case tok::pp___include_macros:
      case tok::pp_pragma:
        Diag(Result, diag::err_embedded_directive) << II->getName();
        Diag(*ArgMacro, diag::note_macro_expansion_here)
            << ArgMacro->getIdentifierInfo();
        DiscardDirectiveLine();
        return;
      default:
        break;
      }
    }
    Diag(Result, diag::ext_embedded_directive);
  }

  ResetMacroExpansionHelper helper(this);

  switch (Result.getKind()) {
  case tok::eod:
    CurPPLexer->MIOpt.setReadToken(ReadAnyTokensBeforeDirective);
    return;
  case tok::numeric_constant:
    if (getLangOpts().AsmPreprocessor &&
        SourceMgr.getFileID(SavedHash.getLocation()) != getPredefinesFileID())
      break;
    return ExecDigitDir(Result);
  default:
    IdentifierInfo *II = Result.getIdentifierInfo();
    if (!II)
      break;

    switch (II->getPPKeywordID()) {
    default:
      break;
    case tok::pp_if:
      return ExecIf(Result, SavedHash, ReadAnyTokensBeforeDirective);
    case tok::pp_ifdef:
      return ExecIfdef(Result, SavedHash, false, true /*not valid for miopt*/);
    case tok::pp_ifndef:
      return ExecIfdef(Result, SavedHash, true, ReadAnyTokensBeforeDirective);
    case tok::pp_elif:
    case tok::pp_elifdef:
    case tok::pp_elifndef:
      return ExecElifFamily(Result, SavedHash, II->getPPKeywordID());

    case tok::pp_else:
      return ExecElse(Result, SavedHash);
    case tok::pp_endif:
      return ExecEndif(Result);

    case tok::pp_include:
      return ExecInclude(SavedHash.getLocation(), Result);
    case tok::pp___include_macros:
      return ExecIncludeMacros(SavedHash.getLocation(), Result);

    case tok::pp_define:
      return ExecDefine(Result, ImmediatelyAfterTopLevelIfndef);
    case tok::pp_undef:
      return ExecUndef();

    case tok::pp_line:
      return ExecLineDir();

    case tok::pp_error:
      return ExecUserDiag(Result, false);

    case tok::pp_pragma:
      return ExecPragma({PIK_HashPragma, SavedHash.getLocation()});

    case tok::pp_import:
      return ExecImport(SavedHash.getLocation(), Result);
    case tok::pp_include_next:
      return ExecIncludeNext(SavedHash.getLocation(), Result);

    case tok::pp_warning:
      Diag(Result, LangOpts.C23 ? diag::warn_c23_compat_warning_directive
                                : diag::ext_pp_warning_directive);

      return ExecUserDiag(Result, true);
    case tok::pp_ident:
      return ExecIdentSCCS(Result);
    case tok::pp_sccs:
      return ExecIdentSCCS(Result);
    case tok::pp_assert:
      break;
    case tok::pp_unassert:
      break;

    case tok::pp___public_macro:
      break;

    case tok::pp___private_macro:
      break;
    }
    break;
  }

  if (getLangOpts().AsmPreprocessor) {
    auto Toks = std::make_unique<Token[]>(2);
    Toks[0] = SavedHash;
    Toks[1] = Result;

    if (Result.is(tok::hashhash))
      Toks[1].setKind(tok::unknown);

    PushTokenStream(std::move(Toks), 2, false, /*IsReinject*/ false);
    return;
  }

  Diag(Result, diag::err_pp_invalid_directive) << 0;
  DiscardDirectiveLine();
}

namespace {
bool extractLineNumber(Token &DigitTok, unsigned &Val, unsigned DiagID,
                       PrepEngine &PP, bool IsGNULineDirective = false) {
  if (DigitTok.isNot(tok::numeric_constant)) {
    PP.Diag(DigitTok, DiagID);

    if (DigitTok.isNot(tok::eod))
      PP.DiscardDirectiveLine();
    return true;
  }

  llvm::SmallString<64> IntegerBuffer;
  IntegerBuffer.resize(DigitTok.getLength());
  const char *DigitTokBegin = &IntegerBuffer[0];
  bool Invalid = false;
  unsigned ActualLength = PP.getSpelling(DigitTok, DigitTokBegin, &Invalid);
  if (Invalid)
    return true;

  Val = 0;
  for (unsigned i = 0; i != ActualLength; ++i) {
    if (DigitTokBegin[i] == '\'')
      continue;

    if (!isDigit(DigitTokBegin[i])) {
      PP.Diag(PP.AdvanceToTokenCharacter(DigitTok.getLocation(), i),
              diag::err_pp_line_digit_sequence)
          << IsGNULineDirective;
      PP.DiscardDirectiveLine();
      return true;
    }

    unsigned NextVal = Val * 10 + (DigitTokBegin[i] - '0');
    if (NextVal < Val) { // overflow.
      PP.Diag(DigitTok, DiagID);
      PP.DiscardDirectiveLine();
      return true;
    }
    Val = NextVal;
  }

  if (DigitTokBegin[0] == '0' && Val)
    PP.Diag(DigitTok.getLocation(), diag::warn_pp_line_decimal)
        << IsGNULineDirective;

  return false;
}
} // namespace

void PrepEngine::ExecLineDir() {
  // Read the line # and string argument (tokens are macro-expanded).
  Token DigitTok;
  Lex(DigitTok);

  // Validate the number and convert it to an unsigned.
  unsigned LineNo;
  if (extractLineNumber(DigitTok, LineNo, diag::err_pp_line_requires_integer,
                        *this))
    return;

  if (LineNo == 0)
    Diag(DigitTok, diag::ext_pp_line_zero);

  // Line number must fit in 31 bits (C99+) or 15 bits (C90).
  unsigned LineLimit = LangOpts.C99 ? 2147483648U : 32768U;
  if (LineNo >= LineLimit)
    Diag(DigitTok, diag::ext_pp_line_too_big) << LineLimit;

  int FilenameID = -1;
  Token StrTok;
  Lex(StrTok);

  // If the StrTok is "eod", then it wasn't present.  Otherwise, it must be a
  // string followed by eod.
  if (StrTok.is(tok::eod))
    ; // ok
  else if (StrTok.isNot(tok::string_literal)) {
    Diag(StrTok, diag::err_pp_line_invalid_filename);
    DiscardDirectiveLine();
    return;
  } else {
    StringLiteralParser Literal(StrTok, *this);
    assert(Literal.isOrdinary() && "Didn't allow wide strings in");
    if (Literal.hadError) {
      DiscardDirectiveLine();
      return;
    }
    FilenameID = SourceMgr.getLineTableFilenameID(Literal.getString());

    // Verify that there is nothing after the string, other than EOD.  Because
    // of C99 6.10.4p5, macros that expand to empty tokens are ok.
    VerifyDirectiveEnd(tok::getPPKeywordSpelling(tok::pp_line), true);
  }

  // Take the file kind of the file containing the #line directive. #line
  // directives are often used for generated sources from the same codebase, so
  // the new file should generally be classified the same way as the current
  // file. This is visible in GCC's pre-processed output, which rewrites #line
  // to GNU line markers.
  SrcMgr::CharacteristicKind FileKind =
      SourceMgr.getFileCharacteristic(DigitTok.getLocation());

  SourceMgr.AddLineNote(DigitTok.getLocation(), LineNo, FilenameID, false,
                        false, FileKind);

  if (Callbacks)
    Callbacks->FileChanged(CurPPLexer->getSourceLocation(),
                           PrepObserver::RenameFile, FileKind);
}

namespace {
bool extractLineMarkerFlags(bool &IsFileEntry, bool &IsFileExit,
                            SrcMgr::CharacteristicKind &FileKind,
                            PrepEngine &PP) {
  unsigned FlagVal;
  Token FlagTok;
  PP.Lex(FlagTok);
  if (FlagTok.is(tok::eod))
    return false;
  if (extractLineNumber(FlagTok, FlagVal, diag::err_pp_linemarker_invalid_flag,
                        PP))
    return true;

  if (FlagVal == 1) {
    IsFileEntry = true;

    PP.Lex(FlagTok);
    if (FlagTok.is(tok::eod))
      return false;
    if (extractLineNumber(FlagTok, FlagVal,
                          diag::err_pp_linemarker_invalid_flag, PP))
      return true;
  } else if (FlagVal == 2) {
    IsFileExit = true;

    SourceManager &SM = PP.getSourceManager();
    // If we are leaving the current presumed file, check to make sure the
    // presumed include stack isn't empty!
    FileID CurFileID =
        SM.getDecomposedExpansionLoc(FlagTok.getLocation()).first;
    PresumedLoc PLoc = SM.getPresumedLoc(FlagTok.getLocation());
    if (PLoc.isInvalid())
      return true;

    // If there is no include loc (main file) or if the include loc is in a
    // different physical file, then we aren't in a "1" line marker flag region.
    SourceLocation IncLoc = PLoc.getIncludeLoc();
    if (IncLoc.isInvalid() ||
        SM.getDecomposedExpansionLoc(IncLoc).first != CurFileID) {
      PP.Diag(FlagTok, diag::err_pp_linemarker_invalid_pop);
      PP.DiscardDirectiveLine();
      return true;
    }

    PP.Lex(FlagTok);
    if (FlagTok.is(tok::eod))
      return false;
    if (extractLineNumber(FlagTok, FlagVal,
                          diag::err_pp_linemarker_invalid_flag, PP))
      return true;
  }

  // We must have 3 if there are still flags.
  if (FlagVal != 3) {
    PP.Diag(FlagTok, diag::err_pp_linemarker_invalid_flag);
    PP.DiscardDirectiveLine();
    return true;
  }

  FileKind = SrcMgr::C_System;

  PP.Lex(FlagTok);
  if (FlagTok.is(tok::eod))
    return false;
  if (extractLineNumber(FlagTok, FlagVal, diag::err_pp_linemarker_invalid_flag,
                        PP))
    return true;

  // We must have 4 if there is yet another flag.
  if (FlagVal != 4) {
    PP.Diag(FlagTok, diag::err_pp_linemarker_invalid_flag);
    PP.DiscardDirectiveLine();
    return true;
  }

  FileKind = SrcMgr::C_ExternCSystem;

  PP.Lex(FlagTok);
  if (FlagTok.is(tok::eod))
    return false;

  // There are no more valid flags here.
  PP.Diag(FlagTok, diag::err_pp_linemarker_invalid_flag);
  PP.DiscardDirectiveLine();
  return true;
}
} // namespace

void PrepEngine::ExecDigitDir(Token &DigitTok) {
  // Validate the number and convert it to an unsigned.  GNU does not have a
  // line # limit other than it fit in 32-bits.
  unsigned LineNo;
  if (extractLineNumber(DigitTok, LineNo,
                        diag::err_pp_linemarker_requires_integer, *this, true))
    return;

  Token StrTok;
  Lex(StrTok);

  bool IsFileEntry = false, IsFileExit = false;
  int FilenameID = -1;
  SrcMgr::CharacteristicKind FileKind = SrcMgr::C_User;

  // If the StrTok is "eod", then it wasn't present.  Otherwise, it must be a
  // string followed by eod.
  if (StrTok.is(tok::eod)) {
    Diag(StrTok, diag::ext_pp_gnu_line_directive);
    // Treat this like "#line NN", which doesn't change file characteristics.
    FileKind = SourceMgr.getFileCharacteristic(DigitTok.getLocation());
  } else if (StrTok.isNot(tok::string_literal)) {
    Diag(StrTok, diag::err_pp_linemarker_invalid_filename);
    DiscardDirectiveLine();
    return;
  } else {
    StringLiteralParser Literal(StrTok, *this);
    assert(Literal.isOrdinary() && "Didn't allow wide strings in");
    if (Literal.hadError) {
      DiscardDirectiveLine();
      return;
    }

    // If a filename was present, read any flags that are present.
    if (extractLineMarkerFlags(IsFileEntry, IsFileExit, FileKind, *this))
      return;
    if (!SourceMgr.isWrittenInBuiltinFile(DigitTok.getLocation()) &&
        !SourceMgr.isWrittenInCommandLineFile(DigitTok.getLocation()))
      Diag(StrTok, diag::ext_pp_gnu_line_directive);

    // Exiting to an empty string means pop to the including file, so leave
    // FilenameID as -1 in that case.
    if (!(IsFileExit && Literal.getString().empty()))
      FilenameID = SourceMgr.getLineTableFilenameID(Literal.getString());
  }
  SourceMgr.AddLineNote(DigitTok.getLocation(), LineNo, FilenameID, IsFileEntry,
                        IsFileExit, FileKind);

  // If the preprocessor has callbacks installed, notify them of the #line
  // change.  This is used so that the line marker comes out in -E mode for
  // example.
  if (Callbacks) {
    PrepObserver::FileChangeReason Reason = PrepObserver::RenameFile;
    if (IsFileEntry)
      Reason = PrepObserver::EnterFile;
    else if (IsFileExit)
      Reason = PrepObserver::ExitFile;

    Callbacks->FileChanged(CurPPLexer->getSourceLocation(), Reason, FileKind);
  }
}

void PrepEngine::ExecUserDiag(Token &Tok, bool isWarning) {
  // Read the rest of the line raw.  We do this because we don't want macros
  // to be expanded and we don't require that the tokens be valid preprocessing
  // tokens.  For example, this is allowed: "#warning `   'foo".  GCC does
  // collapse multiple consecutive white space between tokens, but this isn't
  // specified by the standard.
  llvm::SmallString<128> Message;
  CurLexer->drainDirectiveLine(&Message);

  // Find the first non-whitespace character, so that we can make the
  // diagnostic more succinct.
  llvm::StringRef Msg = Message.str().ltrim(' ');

  if (isWarning)
    Diag(Tok, diag::pp_hash_warning) << Msg;
  else
    Diag(Tok, diag::err_pp_hash_error) << Msg;
}

void PrepEngine::ExecIdentSCCS(Token &Tok) {
  // Yes, this directive is an extension.
  Diag(Tok, diag::ext_pp_ident_directive);

  // Read the string argument.
  Token StrTok;
  Lex(StrTok);

  // If the token kind isn't a string, it's a malformed directive.
  if (StrTok.isNot(tok::string_literal) &&
      StrTok.isNot(tok::wide_string_literal)) {
    Diag(StrTok, diag::err_pp_malformed_ident);
    if (StrTok.isNot(tok::eod))
      DiscardDirectiveLine();
    return;
  }

  // Verify that there is nothing after the string, other than EOD.
  VerifyDirectiveEnd(tok::getPPKeywordSpelling(tok::pp_ident));

  if (Callbacks) {
    bool Invalid = false;
    std::string Str = getSpelling(StrTok, &Invalid);
    if (!Invalid)
      Callbacks->Ident(Tok.getLocation(), Str);
  }
}

// ===----------------------------------------------------------------------===
// Conditional directive handling
// ===----------------------------------------------------------------------===

void PrepEngine::ExecIfdef(Token &Result, const Token &HashToken, bool isIfndef,
                           bool ReadAnyTokensBeforeDirective) {
  ++NumIf;
  Token DirectiveTok = Result;

  Token MacroNameTok;
  ValidateMacroNameTok(MacroNameTok);

  // Error reading macro name?  If so, diagnostic already issued.
  if (MacroNameTok.is(tok::eod)) {
    // Skip code until we get to #endif.  This helps with recovery by not
    // emitting an error when the #endif is reached.
    SkipExcludedBlock(HashToken.getLocation(), DirectiveTok.getLocation(),
                      /*Foundnonskip*/ false, /*FoundElse*/ false);
    return;
  }

  checkMacroWarnings(MacroNameTok);
  VerifyDirectiveEnd(isIfndef ? tok::getPPKeywordSpelling(tok::pp_ifndef)
                              : tok::getPPKeywordSpelling(tok::pp_ifdef));

  IdentifierInfo *MII = MacroNameTok.getIdentifierInfo();
  auto MD = getMacroDefinition(MII);
  MacroRecord *MI = MD.getMacroRecord();

  if (CurPPLexer->getConditionalStackDepth() == 0) {
    // If the start of a top-level #ifdef and if the macro is not defined,
    // inform MIOpt that this might be the start of a proper include guard.
    // Otherwise it is some other form of unknown conditional which we can't
    // handle.
    if (!ReadAnyTokensBeforeDirective && !MI) {
      assert(isIfndef && "#ifdef shouldn't reach here");
      CurPPLexer->MIOpt.enterTopLevelIfndef(MII, MacroNameTok.getLocation());
    } else
      CurPPLexer->MIOpt.enterTopLevelConditional();
  }

  // If there is a macro, process it.
  if (MI) // Mark it used.
    setMacroUsed(MI);

  if (Callbacks) {
    if (isIfndef)
      Callbacks->Ifndef(DirectiveTok.getLocation(), MacroNameTok, MD);
    else
      Callbacks->Ifdef(DirectiveTok.getLocation(), MacroNameTok, MD);
  }

  // Should we include the stuff contained by this directive?
  if (!MI == isIfndef) {
    // Yes, remember that we are inside a conditional, then lex the next token.
    CurPPLexer->pushConditionalLevel(DirectiveTok.getLocation(),
                                     /*wasskip*/ false, /*foundnonskip*/ true,
                                     /*foundelse*/ false);
  } else {
    // No, skip the contents of this block.
    SkipExcludedBlock(HashToken.getLocation(), DirectiveTok.getLocation(),
                      /*Foundnonskip*/ false,
                      /*FoundElse*/ false);
  }
}

void PrepEngine::ExecIf(Token &IfToken, const Token &HashToken,
                        bool ReadAnyTokensBeforeDirective) {
  ++NumIf;

  IdentifierInfo *IfNDefMacro = nullptr;
  const CondEvalOutcome DER = EvalCondExpr(IfNDefMacro);
  const bool ConditionalTrue = DER.Conditional;
  if (!CurPPLexer)
    return;

  if (CurPPLexer->getConditionalStackDepth() == 0) {
    if (!ReadAnyTokensBeforeDirective && IfNDefMacro && ConditionalTrue)
      CurPPLexer->MIOpt.enterTopLevelIfndef(IfNDefMacro, IfToken.getLocation());
    else
      CurPPLexer->MIOpt.enterTopLevelConditional();
  }

  if (Callbacks)
    Callbacks->If(
        IfToken.getLocation(), DER.ExprRange,
        (ConditionalTrue ? PrepObserver::CVK_True : PrepObserver::CVK_False));

  if (ConditionalTrue) {
    CurPPLexer->pushConditionalLevel(IfToken.getLocation(), /*wasskip*/ false,
                                     /*foundnonskip*/ true,
                                     /*foundelse*/ false);
  } else {
    SkipExcludedBlock(HashToken.getLocation(), IfToken.getLocation(),
                      /*Foundnonskip*/ false,
                      /*FoundElse*/ false);
  }
}

void PrepEngine::ExecEndif(Token &EndifToken) {
  ++NumEndif;

  VerifyDirectiveEnd(tok::getPPKeywordSpelling(tok::pp_endif));

  PPConditionalInfo CondInfo;
  if (CurPPLexer->popConditionalLevel(CondInfo)) {
    Diag(EndifToken, diag::err_pp_endif_without_if);
    return;
  }

  if (CurPPLexer->getConditionalStackDepth() == 0)
    CurPPLexer->MIOpt.exitTopLevelConditional();

  assert(!CondInfo.WasSkipping && !CurPPLexer->LexingRawMode &&
         "This code should only be reachable in the non-skipping case!");

  if (Callbacks)
    Callbacks->Endif(EndifToken.getLocation(), CondInfo.IfLoc);
}

void PrepEngine::ExecElse(Token &Result, const Token &HashToken) {
  ++NumElse;

  VerifyDirectiveEnd(tok::getPPKeywordSpelling(tok::pp_else));

  PPConditionalInfo CI;
  if (CurPPLexer->popConditionalLevel(CI)) {
    Diag(Result, diag::pp_err_else_without_if);
    return;
  }

  if (CurPPLexer->getConditionalStackDepth() == 0)
    CurPPLexer->MIOpt.enterTopLevelConditional();

  if (CI.FoundElse)
    Diag(Result, diag::pp_err_else_after_else);

  if (Callbacks)
    Callbacks->Else(Result.getLocation(), CI.IfLoc);

  SkipExcludedBlock(HashToken.getLocation(), CI.IfLoc,
                    /*Foundnonskip*/ true,
                    /*FoundElse*/ true, Result.getLocation());
}
void PrepEngine::ExecElifFamily(Token &ElifToken, const Token &HashToken,
                                tok::PPKeywordKind Kind) {
  ConditionalBranchDiag DirKind = Kind == tok::pp_elif      ? CBD_Elif
                                  : Kind == tok::pp_elifdef ? CBD_Elifdef
                                                            : CBD_Elifndef;
  ++NumElse;

  switch (DirKind) {
  case CBD_Elifdef:
  case CBD_Elifndef: {
    unsigned DiagID = LangOpts.C23 ? diag::warn_c23_compat_pp_directive
                                   : diag::ext_c23_pp_directive;
    Diag(ElifToken, DiagID) << DirKind;
    break;
  }
  default:
    break;
  }

  SourceRange ConditionRange = DiscardDirectiveLine();

  PPConditionalInfo CI;
  if (CurPPLexer->popConditionalLevel(CI)) {
    Diag(ElifToken, diag::pp_err_elif_without_if) << DirKind;
    return;
  }

  if (CurPPLexer->getConditionalStackDepth() == 0)
    CurPPLexer->MIOpt.enterTopLevelConditional();

  if (CI.FoundElse)
    Diag(ElifToken, diag::pp_err_elif_after_else) << DirKind;

  if (Callbacks) {
    switch (Kind) {
    case tok::pp_elif:
      Callbacks->Elif(ElifToken.getLocation(), ConditionRange,
                      PrepObserver::CVK_NotEvaluated, CI.IfLoc);
      break;
    case tok::pp_elifdef:
      Callbacks->Elifdef(ElifToken.getLocation(), ConditionRange, CI.IfLoc);
      break;
    case tok::pp_elifndef:
      Callbacks->Elifndef(ElifToken.getLocation(), ConditionRange, CI.IfLoc);
      break;
    default:
      assert(false && "unexpected directive kind");
      break;
    }
  }

  SkipExcludedBlock(HashToken.getLocation(), CI.IfLoc, /*Foundnonskip*/ true,
                    /*FoundElse*/ CI.FoundElse, ElifToken.getLocation());
}
