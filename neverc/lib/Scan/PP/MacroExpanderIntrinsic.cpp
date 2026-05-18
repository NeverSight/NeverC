#include "neverc/Foundation/Attr/AttributeCommonInfo.h"
#include "neverc/Foundation/Attr/Attributes.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/MacroArgStorage.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/MSVCMacroRebuilding.h"
#include <cassert>
#include <ctime>
#include <optional>
#include <random>
#include <string>

using namespace neverc;
// ===----------------------------------------------------------------------===
// Built-in macro helpers
// ===----------------------------------------------------------------------===

namespace {
void initDateTimeMacroLocs(SourceLocation &DATELoc, SourceLocation &TIMELoc,
                           PrepEngine &PP) {
  time_t TT;
  std::tm *TM;
  if (PP.getPrepEngineOpts().SourceDateEpoch) {
    TT = *PP.getPrepEngineOpts().SourceDateEpoch;
    TM = std::gmtime(&TT);
  } else {
    TT = std::time(nullptr);
    TM = std::localtime(&TT);
  }

  static const char *const Months[] = {"Jan", "Feb", "Mar", "Apr",
                                       "May", "Jun", "Jul", "Aug",
                                       "Sep", "Oct", "Nov", "Dec"};

  {
    llvm::SmallString<32> TmpBuffer;
    llvm::raw_svector_ostream TmpStream(TmpBuffer);
    if (TM)
      TmpStream << llvm::format("\"%s %2d %4d\"", Months[TM->tm_mon],
                                TM->tm_mday, TM->tm_year + 1900);
    else
      TmpStream << "??? ?? ????";
    Token TmpTok;
    TmpTok.startToken();
    PP.WriteScratch(TmpStream.str(), TmpTok);
    DATELoc = TmpTok.getLocation();
  }

  {
    llvm::SmallString<32> TmpBuffer;
    llvm::raw_svector_ostream TmpStream(TmpBuffer);
    if (TM)
      TmpStream << llvm::format("\"%02d:%02d:%02d\"", TM->tm_hour, TM->tm_min,
                                TM->tm_sec);
    else
      TmpStream << "??:??:??";
    Token TmpTok;
    TmpTok.startToken();
    PP.WriteScratch(TmpStream.str(), TmpTok);
    TIMELoc = TmpTok.getLocation();
  }
}

bool isLangFeatureEnabled(const PrepEngine &PP, llvm::StringRef Feature) {
  const LangOptions &LangOpts = PP.getLangOpts();

  // Normalize the feature name, __foo__ becomes foo.
  if (Feature.starts_with("__") && Feature.ends_with("__") &&
      Feature.size() >= 4)
    Feature = Feature.substr(2, Feature.size() - 4);

#define FEATURE(Name, Predicate) .Case(#Name, Predicate)
  return llvm::StringSwitch<bool>(Feature)
#include "neverc/Foundation/LangOpts/Features.def"
      .Default(false);
#undef FEATURE
}

bool isLangExtEnabled(const PrepEngine &PP, llvm::StringRef Extension) {
  if (isLangFeatureEnabled(PP, Extension))
    return true;

  // If the use of an extension results in an error diagnostic, extensions are
  // effectively unavailable, so just return false here.
  if (PP.getDiagnostics().getExtensionHandlingBehavior() >=
      diag::Severity::Error)
    return false;

  const LangOptions &LangOpts = PP.getLangOpts();

  // Normalize the extension name, __foo__ becomes foo.
  if (Extension.starts_with("__") && Extension.ends_with("__") &&
      Extension.size() >= 4)
    Extension = Extension.substr(2, Extension.size() - 4);

#define EXTENSION(Name, Predicate) .Case(#Name, Predicate)
  return llvm::StringSwitch<bool>(Extension)
#include "neverc/Foundation/LangOpts/Features.def"
      .Default(false);
#undef EXTENSION
}

bool probeIncludeFile(Token &Tok, IdentifierInfo *II, PrepEngine &PP,
                      ConstSearchDirIterator LookupFrom,
                      const FileEntry *LookupFromFile) {
  SourceLocation LParenLoc = Tok.getLocation();

  if (!PP.isParsingIfOrElifDirective()) {
    PP.Diag(LParenLoc, diag::err_pp_directive_required) << II;
    assert(Tok.is(tok::identifier));
    Tok.setIdentifierInfo(II);
    return false;
  }

  // Get '('. If we don't have a '(', try to form a header-name token.
  do {
    if (PP.LexIncludePathTok(Tok))
      return false;
  } while (Tok.getKind() == tok::comment);

  if (Tok.isNot(tok::l_paren)) {
    // No '(', use end of last token.
    LParenLoc = PP.getLocForEndOfToken(LParenLoc);
    PP.Diag(LParenLoc, diag::err_pp_expected_after) << II << tok::l_paren;
    // If the next token looks like a filename or the start of one,
    // assume it is and process it as such.
    if (Tok.isNot(tok::header_name))
      return false;
  } else {
    // Save '(' location for possible missing ')' message.
    LParenLoc = Tok.getLocation();
    if (PP.LexIncludePathTok(Tok))
      return false;
  }

  if (Tok.isNot(tok::header_name)) {
    PP.Diag(Tok.getLocation(), diag::err_pp_expects_filename);
    return false;
  }

  llvm::SmallString<128> FilenameBuffer;
  bool Invalid = false;
  llvm::StringRef Filename = PP.getSpelling(Tok, FilenameBuffer, &Invalid);
  if (Invalid)
    return false;

  SourceLocation FilenameLoc = Tok.getLocation();

  PP.LexSkipComments(Tok);

  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(PP.getLocForEndOfToken(FilenameLoc), diag::err_pp_expected_after)
        << II << tok::r_paren;
    PP.Diag(LParenLoc, diag::note_matching) << tok::l_paren;
    return false;
  }

  bool isAngled = PP.extractIncludeFilename(Tok.getLocation(), Filename);
  // If extractIncludeFilename set the start ptr to null, there was an
  // error.
  if (Filename.empty())
    return false;

  OptionalFileEntryRef File = PP.ResolveInclude(
      FilenameLoc, Filename, isAngled, LookupFrom, LookupFromFile, nullptr,
      nullptr, nullptr, nullptr, nullptr);

  if (PrepObserver *Callbacks = PP.getObserver()) {
    SrcMgr::CharacteristicKind FileType = SrcMgr::C_User;
    if (File)
      FileType = PP.getIncludeResolver().getFileDirFlavor(*File);
    Callbacks->HasInclude(FilenameLoc, Filename, isAngled, File, FileType);
  }

  return File.has_value();
}
} // namespace

bool PrepEngine::CheckHasInclude(Token &Tok, IdentifierInfo *II) {
  return probeIncludeFile(Tok, II, *this, nullptr, nullptr);
}

bool PrepEngine::CheckHasIncludeNext(Token &Tok, IdentifierInfo *II) {
  ConstSearchDirIterator Lookup = nullptr;
  const FileEntry *LookupFromFile;
  std::tie(Lookup, LookupFromFile) = getIncludeNextStart(Tok);

  return probeIncludeFile(Tok, II, *this, Lookup, LookupFromFile);
}

// ===----------------------------------------------------------------------===
// Intrinsic macro expansion
// ===----------------------------------------------------------------------===

namespace {
void evaluateBuiltinCheck(
    llvm::raw_svector_ostream &OS, Token &Tok, IdentifierInfo *II,
    PrepEngine &PP, bool ExpandArgs,
    llvm::function_ref<int(Token &Tok, bool &HasLexedNextTok)> Op) {
  PP.LexWithoutExpansion(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(Tok.getLocation(), diag::err_pp_expected_after)
        << II << tok::l_paren;

    // Provide a dummy '0' value on output stream to elide further errors.
    if (!Tok.isOneOf(tok::eof, tok::eod)) {
      OS << 0;
      Tok.setKind(tok::numeric_constant);
    }
    return;
  }

  unsigned ParenDepth = 1;
  SourceLocation LParenLoc = Tok.getLocation();
  std::optional<int> Result;

  Token ResultTok;
  bool SuppressDiagnostic = false;
  while (true) {
    if (ExpandArgs)
      PP.Lex(Tok);
    else
      PP.LexWithoutExpansion(Tok);

  already_lexed:
    switch (Tok.getKind()) {
    case tok::eof:
    case tok::eod:
      // Don't provide even a dummy value if the eod or eof marker is
      // reached.  Simply provide a diagnostic.
      PP.Diag(Tok.getLocation(), diag::err_unterm_macro_invoc);
      return;

    case tok::comma:
      if (!SuppressDiagnostic) {
        PP.Diag(Tok.getLocation(), diag::err_too_many_args_in_macro_invoc);
        SuppressDiagnostic = true;
      }
      continue;

    case tok::l_paren:
      ++ParenDepth;
      if (Result)
        break;
      if (!SuppressDiagnostic) {
        PP.Diag(Tok.getLocation(), diag::err_pp_nested_paren) << II;
        SuppressDiagnostic = true;
      }
      continue;

    case tok::r_paren:
      if (--ParenDepth > 0)
        continue;

      // The last ')' has been reached; return the value if one found or
      // a diagnostic and a dummy value.
      if (Result) {
        OS << *Result;
        // For strict conformance to __has_cpp_attribute rules, use 'L'
        // suffix for dated literals.
        if (*Result > 1)
          OS << 'L';
      } else {
        OS << 0;
        if (!SuppressDiagnostic)
          PP.Diag(Tok.getLocation(), diag::err_too_few_args_in_macro_invoc);
      }
      Tok.setKind(tok::numeric_constant);
      return;

    default: {
      if (Result)
        break;

      bool HasLexedNextToken = false;
      Result = Op(Tok, HasLexedNextToken);
      ResultTok = Tok;
      if (HasLexedNextToken)
        goto already_lexed;
      continue;
    }
    }

    if (!SuppressDiagnostic) {
      if (auto Diag = PP.Diag(Tok.getLocation(), diag::err_pp_expected_after)) {
        if (IdentifierInfo *LastII = ResultTok.getIdentifierInfo())
          Diag << LastII;
        else
          Diag << ResultTok.getKind();
        Diag << tok::r_paren << ResultTok.getLocation();
      }
      PP.Diag(LParenLoc, diag::note_matching) << tok::l_paren;
      SuppressDiagnostic = true;
    }
  }
}

IdentifierInfo *expectAndConsumeIdent(Token &Tok, PrepEngine &PP,
                                      signed DiagID) {
  IdentifierInfo *II;
  if (!Tok.isAnnotation() && (II = Tok.getIdentifierInfo()))
    return II;

  PP.Diag(Tok.getLocation(), DiagID);
  return nullptr;
}

bool isTargetArchMatch(const TargetInfo &TI, const IdentifierInfo *II) {
  llvm::SmallString<64> ArchName;
  ArchName += II->getName().lower();
  ArchName += "--";
  llvm::Triple Arch(ArchName);
  const llvm::Triple &TT = TI.getTriple();
  return (Arch.getSubArch() == llvm::Triple::NoSubArch ||
          Arch.getSubArch() == TT.getSubArch()) &&
         Arch.getArch() == TT.getArch();
}

bool isTargetVendorMatch(const TargetInfo &TI, const IdentifierInfo *II) {
  llvm::StringRef VendorName = TI.getTriple().getVendorName();
  if (VendorName.empty())
    VendorName = "unknown";
  return VendorName.equals_insensitive(II->getName());
}

bool isTargetOSMatch(const TargetInfo &TI, const IdentifierInfo *II) {
  llvm::SmallString<64> OSName("unknown-unknown-");
  OSName += II->getName().lower();
  llvm::Triple OS(OSName);
  if (OS.getOS() == llvm::Triple::Darwin)
    return TI.getTriple().isOSDarwin();
  return TI.getTriple().getOS() == OS.getOS();
}

bool isTargetEnvMatch(const TargetInfo &TI, const IdentifierInfo *II) {
  llvm::SmallString<64> EnvName("---");
  EnvName += II->getName().lower();
  llvm::Triple Env(EnvName);
  if (Env.getEnvironment() == llvm::Triple::UnknownEnvironment &&
      EnvName != "---unknown")
    return false;
  return TI.getTriple().getEnvironment() == Env.getEnvironment();
}
} // namespace

void PrepEngine::ExpandIntrinsicMacro(Token &Tok) {
  IdentifierInfo *II = Tok.getIdentifierInfo();
  assert(II && "Can't be a macro without id info!");

  auto It = IntrinsicMacroMap.find(II);
  assert(It != IntrinsicMacroMap.end() && "Unregistered intrinsic macro");
  IntrinsicMacroKind Kind = It->second;

  // _Pragma / __pragma are dispatched before common setup because they
  // replace Tok by re-entering the lexer.
  if (Kind == IntrinsicMacroKind::IMK_Pragma)
    return ExpandUnderscorePragma(Tok);
  if (Kind == IntrinsicMacroKind::IMK_MsPragma)
    return ExpandMsPragma(Tok);

  ++NumBuiltinMacroExpanded;

  llvm::SmallString<128> TmpBuffer;
  llvm::raw_svector_ostream OS(TmpBuffer);

  Tok.setIdentifierInfo(nullptr);
  Tok.clearFlag(Token::NeedsCleaning);
  bool IsAtStartOfLine = Tok.isAtStartOfLine();
  bool HasLeadingSpace = Tok.hasLeadingSpace();

  switch (Kind) {
  case IntrinsicMacroKind::IMK_Line: {
    SourceLocation Loc = Tok.getLocation();
    Loc = AdvanceToTokenCharacter(Loc, 0);
    Loc = SourceMgr.getExpansionRange(Loc).getEnd();
    PresumedLoc PLoc = SourceMgr.getPresumedLoc(Loc);
    OS << (PLoc.isValid() ? PLoc.getLine() : 1);
    Tok.setKind(tok::numeric_constant);
    break;
  }

  case IntrinsicMacroKind::IMK_RandomNumeric: {
    // Use a file-scope mt19937_64 seeded once to avoid the cost of
    // constructing std::random_device on every expansion.
    static std::mt19937_64 RNG(std::random_device{}());
    std::uniform_int_distribution<uint64_t> Dist(
        std::numeric_limits<uint64_t>::min(),
        std::numeric_limits<uint64_t>::max());
    OS << Dist(RNG);
    Tok.setKind(tok::numeric_constant);
    break;
  }

  case IntrinsicMacroKind::IMK_File:
  case IntrinsicMacroKind::IMK_BaseFile:
  case IntrinsicMacroKind::IMK_FileName:
  case IntrinsicMacroKind::IMK_Function: {
    PresumedLoc PLoc = SourceMgr.getPresumedLoc(Tok.getLocation());

    if (Kind == IntrinsicMacroKind::IMK_BaseFile && PLoc.isValid()) {
      SourceLocation NextLoc = PLoc.getIncludeLoc();
      while (NextLoc.isValid()) {
        PLoc = SourceMgr.getPresumedLoc(NextLoc);
        if (PLoc.isInvalid())
          break;
        NextLoc = PLoc.getIncludeLoc();
      }
    }

    llvm::SmallString<256> FN;
    if (PLoc.isValid()) {
      if (Kind == IntrinsicMacroKind::IMK_FileName) {
        formatPathToFileName(FN, PLoc, getLangOpts(), getTargetInfo());
      } else {
        FN += PLoc.getFilename();
        formatPathForFileMacro(FN, getLangOpts(), getTargetInfo());
        if ((getLangOpts().MicrosoftExt || getLangOpts().MSVCCompat) &&
            Kind == IntrinsicMacroKind::IMK_Function) {
          llvm::SmallString<256> FNTemp =
              llvm::MSVCMacroRebuildingPass::get__FUNCTION__MarkerName();
          FNTemp += FN;
          FNTemp += "(Line:";
          FNTemp += llvm::Twine(PLoc.getLine()).str();
          FNTemp += ")";
          FN = FNTemp;
        }
      }
      SourceScanner::escapeStringLiteral(FN);
      OS << '"' << FN << '"';
    }
    Tok.setKind(tok::string_literal);
    break;
  }

  case IntrinsicMacroKind::IMK_Date: {
    Diag(Tok.getLocation(), diag::warn_pp_date_time);
    if (!DATELoc.isValid())
      initDateTimeMacroLocs(DATELoc, TIMELoc, *this);
    Tok.setKind(tok::string_literal);
    Tok.setLength(strlen("\"Mmm dd yyyy\""));
    Tok.setLocation(SourceMgr.createExpansionLoc(
        DATELoc, Tok.getLocation(), Tok.getLocation(), Tok.getLength()));
    return;
  }

  case IntrinsicMacroKind::IMK_Time: {
    Diag(Tok.getLocation(), diag::warn_pp_date_time);
    if (!TIMELoc.isValid())
      initDateTimeMacroLocs(DATELoc, TIMELoc, *this);
    Tok.setKind(tok::string_literal);
    Tok.setLength(strlen("\"hh:mm:ss\""));
    Tok.setLocation(SourceMgr.createExpansionLoc(
        TIMELoc, Tok.getLocation(), Tok.getLocation(), Tok.getLength()));
    return;
  }

  case IntrinsicMacroKind::IMK_IncludeLevel: {
    unsigned Depth = 0;
    PresumedLoc PLoc = SourceMgr.getPresumedLoc(Tok.getLocation());
    if (PLoc.isValid()) {
      PLoc = SourceMgr.getPresumedLoc(PLoc.getIncludeLoc());
      for (; PLoc.isValid(); ++Depth)
        PLoc = SourceMgr.getPresumedLoc(PLoc.getIncludeLoc());
    }
    OS << Depth;
    Tok.setKind(tok::numeric_constant);
    break;
  }

  case IntrinsicMacroKind::IMK_Timestamp: {
    Diag(Tok.getLocation(), diag::warn_pp_date_time);
    const char *Result;
    if (getPrepEngineOpts().SourceDateEpoch) {
      time_t TT = *getPrepEngineOpts().SourceDateEpoch;
      std::tm *TM = std::gmtime(&TT);
      Result = asctime(TM);
    } else {
      const FileEntry *CurFile = nullptr;
      if (LexerCore *TheLexer = getCurrentFileLexer())
        CurFile = SourceMgr.getFileEntryForID(TheLexer->getFileID());
      if (CurFile) {
        time_t TT = CurFile->getModificationTime();
        struct tm *TM = localtime(&TT);
        Result = asctime(TM);
      } else {
        Result = "??? ??? ?? ??:??:?? ????\n";
      }
    }
    OS << '"' << llvm::StringRef(Result).drop_back() << '"';
    Tok.setKind(tok::string_literal);
    break;
  }

  case IntrinsicMacroKind::IMK_FltEvalMethod: {
    OS << getTUFPEvalMethod();
    Tok.setKind(tok::numeric_constant);
    if (getLastFPEvalPragmaLocation().isValid()) {
      Diag(Tok, diag::err_illegal_use_of_flt_eval_macro);
      Diag(getLastFPEvalPragmaLocation(), diag::note_pragma_entered_here);
    }
    break;
  }

  case IntrinsicMacroKind::IMK_Counter:
    OS << CounterValue++;
    Tok.setKind(tok::numeric_constant);
    break;

  case IntrinsicMacroKind::IMK_HasFeature:
    evaluateBuiltinCheck(OS, Tok, II, *this, false,
                         [this](Token &Tok, bool &HasLexedNextToken) -> int {
                           IdentifierInfo *II = expectAndConsumeIdent(
                               Tok, *this, diag::err_feature_check_malformed);
                           return II &&
                                  isLangFeatureEnabled(*this, II->getName());
                         });
    break;

  case IntrinsicMacroKind::IMK_HasExtension:
    evaluateBuiltinCheck(OS, Tok, II, *this, false,
                         [this](Token &Tok, bool &HasLexedNextToken) -> int {
                           IdentifierInfo *II = expectAndConsumeIdent(
                               Tok, *this, diag::err_feature_check_malformed);
                           return II && isLangExtEnabled(*this, II->getName());
                         });
    break;

  case IntrinsicMacroKind::IMK_HasBuiltin:
    evaluateBuiltinCheck(
        OS, Tok, II, *this, false,
        [this](Token &Tok, bool &HasLexedNextToken) -> int {
          IdentifierInfo *II = expectAndConsumeIdent(
              Tok, *this, diag::err_feature_check_malformed);
          if (!II)
            return false;
          else if (II->getBuiltinID() != 0) {
            return Builtin::evaluateRequiredTargetFeatures(
                getBuiltinInfo().getRequiredFeatures(II->getBuiltinID()),
                getTargetInfo().getTargetOpts().FeatureMap);
            return true;
          } else if (II->getTokenID() != tok::identifier ||
                     II->hasRevertedTokenIDToIdentifier()) {
            if (II->getName().starts_with("__builtin_") ||
                II->getName().starts_with("__is_") ||
                II->getName().starts_with("__has_"))
              return true;
            return false;
          } else {
            return llvm::StringSwitch<bool>(II->getName())
                .Case("__is_target_arch", true)
                .Case("__is_target_vendor", true)
                .Case("__is_target_os", true)
                .Case("__is_target_environment", true)
                .Default(false);
          }
        });
    break;

  case IntrinsicMacroKind::IMK_HasConstexprBuiltin:
    evaluateBuiltinCheck(OS, Tok, II, *this, false,
                         [this](Token &Tok, bool &HasLexedNextToken) -> int {
                           IdentifierInfo *II = expectAndConsumeIdent(
                               Tok, *this, diag::err_feature_check_malformed);
                           if (!II)
                             return false;
                           unsigned BuiltinOp = II->getBuiltinID();
                           return BuiltinOp != 0 &&
                                  this->getBuiltinInfo().isConstantEvaluated(
                                      BuiltinOp);
                         });
    break;

  case IntrinsicMacroKind::IMK_IsIdentifier:
    evaluateBuiltinCheck(OS, Tok, II, *this, false,
                         [](Token &Tok, bool &HasLexedNextToken) -> int {
                           return Tok.is(tok::identifier);
                         });
    break;

  case IntrinsicMacroKind::IMK_HasAttribute:
    evaluateBuiltinCheck(
        OS, Tok, II, *this, true,
        [this](Token &Tok, bool &HasLexedNextToken) -> int {
          IdentifierInfo *II = expectAndConsumeIdent(
              Tok, *this, diag::err_feature_check_malformed);
          return II ? hasAttribute(AttributeCommonInfo::Syntax::AS_GNU, nullptr,
                                   II, getTargetInfo(), getLangOpts())
                    : 0;
        });
    break;

  case IntrinsicMacroKind::IMK_HasDeclspec:
    evaluateBuiltinCheck(
        OS, Tok, II, *this, true,
        [this](Token &Tok, bool &HasLexedNextToken) -> int {
          IdentifierInfo *II = expectAndConsumeIdent(
              Tok, *this, diag::err_feature_check_malformed);
          if (II) {
            const LangOptions &LangOpts = getLangOpts();
            return LangOpts.DeclSpecKeyword &&
                   hasAttribute(AttributeCommonInfo::Syntax::AS_Declspec,
                                nullptr, II, getTargetInfo(), LangOpts) != 0;
          }
          return false;
        });
    break;

  case IntrinsicMacroKind::IMK_HasCAttribute:
    evaluateBuiltinCheck(
        OS, Tok, II, *this, true,
        [&](Token &Tok, bool &HasLexedNextToken) -> int {
          IdentifierInfo *ScopeII = nullptr;
          IdentifierInfo *II = expectAndConsumeIdent(
              Tok, *this, diag::err_feature_check_malformed);
          if (!II)
            return false;

          LexWithoutExpansion(Tok);
          if (Tok.isNot(tok::coloncolon))
            HasLexedNextToken = true;
          else {
            ScopeII = II;
            Lex(Tok);
            II = expectAndConsumeIdent(Tok, *this,
                                       diag::err_feature_check_malformed);
          }

          return II ? hasAttribute(AttributeCommonInfo::Syntax::AS_C23, ScopeII,
                                   II, getTargetInfo(), getLangOpts())
                    : 0;
        });
    break;

  case IntrinsicMacroKind::IMK_HasInclude:
  case IntrinsicMacroKind::IMK_HasIncludeNext: {
    bool Value = (Kind == IntrinsicMacroKind::IMK_HasInclude)
                     ? CheckHasInclude(Tok, II)
                     : CheckHasIncludeNext(Tok, II);
    if (Tok.isNot(tok::r_paren))
      return;
    OS << (int)Value;
    Tok.setKind(tok::numeric_constant);
    break;
  }

  case IntrinsicMacroKind::IMK_HasWarning:
    evaluateBuiltinCheck(
        OS, Tok, II, *this, false,
        [this](Token &Tok, bool &HasLexedNextToken) -> int {
          std::string WarningName;
          SourceLocation StrStartLoc = Tok.getLocation();

          HasLexedNextToken = Tok.is(tok::string_literal);
          if (!CompleteStringLiteral(Tok, WarningName, "'__has_warning'",
                                     /*AllowMacroExpansion=*/false))
            return false;

          if (WarningName.size() < 3 || WarningName[0] != '-' ||
              WarningName[1] != 'W') {
            Diag(StrStartLoc, diag::warn_has_warning_invalid_option);
            return false;
          }

          llvm::SmallVector<diag::kind, 10> Diags;
          return !getDiagnostics().getDiagnosticIDs()->getDiagnosticsInGroup(
              diag::Flavor::WarningOrError, WarningName.substr(2), Diags);
        });
    break;

  case IntrinsicMacroKind::IMK_MsIdentifier: {
    SourceLocation Loc = Tok.getLocation();

    LexSkipComments(Tok);
    if (Tok.isNot(tok::l_paren)) {
      Diag(getLocForEndOfToken(Loc), diag::err_pp_expected_after)
          << II << tok::l_paren;
      if (!Tok.isAnnotation() && Tok.getIdentifierInfo())
        Tok.setKind(tok::identifier);
      return;
    }

    SourceLocation LParenLoc = Tok.getLocation();
    LexSkipComments(Tok);

    if (!Tok.isAnnotation() && Tok.getIdentifierInfo())
      Tok.setKind(tok::identifier);
    else if (Tok.is(tok::string_literal)) {
      StringLiteralParser Literal(Tok, *this,
                                  StringLiteralEvalMethod::Unevaluated);
      if (Literal.hadError)
        return;
      Tok.setIdentifierInfo(getIdentifierInfo(Literal.getString()));
      Tok.setKind(tok::identifier);
    } else {
      Diag(Tok.getLocation(), diag::err_pp_identifier_arg_not_identifier)
          << Tok.getKind();
      if (Tok.isOneOf(tok::eof, tok::eod) || Tok.isAnnotation())
        return;
    }

    Token RParen;
    LexSkipComments(RParen);
    if (RParen.isNot(tok::r_paren)) {
      Diag(getLocForEndOfToken(Tok.getLocation()), diag::err_pp_expected_after)
          << Tok.getKind() << tok::r_paren;
      Diag(LParenLoc, diag::note_matching) << tok::l_paren;
    }
    return;
  }

  case IntrinsicMacroKind::IMK_IsTargetArch:
    evaluateBuiltinCheck(OS, Tok, II, *this, false,
                         [this](Token &Tok, bool &HasLexedNextToken) -> int {
                           IdentifierInfo *II = expectAndConsumeIdent(
                               Tok, *this, diag::err_feature_check_malformed);
                           return II && isTargetArchMatch(getTargetInfo(), II);
                         });
    break;

  case IntrinsicMacroKind::IMK_IsTargetVendor:
    evaluateBuiltinCheck(OS, Tok, II, *this, false,
                         [this](Token &Tok, bool &HasLexedNextToken) -> int {
                           IdentifierInfo *II = expectAndConsumeIdent(
                               Tok, *this, diag::err_feature_check_malformed);
                           return II &&
                                  isTargetVendorMatch(getTargetInfo(), II);
                         });
    break;

  case IntrinsicMacroKind::IMK_IsTargetOS:
    evaluateBuiltinCheck(OS, Tok, II, *this, false,
                         [this](Token &Tok, bool &HasLexedNextToken) -> int {
                           IdentifierInfo *II = expectAndConsumeIdent(
                               Tok, *this, diag::err_feature_check_malformed);
                           return II && isTargetOSMatch(getTargetInfo(), II);
                         });
    break;

  case IntrinsicMacroKind::IMK_IsTargetEnvironment:
    evaluateBuiltinCheck(OS, Tok, II, *this, false,
                         [this](Token &Tok, bool &HasLexedNextToken) -> int {
                           IdentifierInfo *II = expectAndConsumeIdent(
                               Tok, *this, diag::err_feature_check_malformed);
                           return II && isTargetEnvMatch(getTargetInfo(), II);
                         });
    break;

  case IntrinsicMacroKind::IMK_Pragma:
  case IntrinsicMacroKind::IMK_MsPragma:
    llvm_unreachable("Handled above before common setup");
  }

  WriteScratch(OS.str(), Tok, Tok.getLocation(), Tok.getLocation());
  Tok.setFlagValue(Token::StartOfLine, IsAtStartOfLine);
  Tok.setFlagValue(Token::LeadingSpace, HasLeadingSpace);
}

// ===----------------------------------------------------------------------===
// Utilities
// ===----------------------------------------------------------------------===

void PrepEngine::setMacroUsed(MacroRecord *MI) {
  // If the 'used' status changed, and the macro requires 'unused' warning,
  // remove its SourceLocation from the warn-for-unused-macro locations.
  if (MI->isWarnIfUnused() && !MI->isUsed())
    WarnUnusedMacroLocs.erase(MI->getDefinitionLoc());
  MI->setIsUsed(true);
}

void PrepEngine::formatPathForFileMacro(llvm::SmallVectorImpl<char> &Path,
                                        const LangOptions &LangOpts,
                                        const TargetInfo &TI) {
  LangOpts.remapPathPrefix(Path);
  if (LangOpts.UseTargetPathSeparator) {
    if (TI.getTriple().isOSWindows())
      llvm::sys::path::remove_dots(Path, false,
                                   llvm::sys::path::Style::windows_backslash);
    else
      llvm::sys::path::remove_dots(Path, false, llvm::sys::path::Style::posix);
  }
}

void PrepEngine::formatPathToFileName(llvm::SmallVectorImpl<char> &FileName,
                                      const PresumedLoc &PLoc,
                                      const LangOptions &LangOpts,
                                      const TargetInfo &TI) {
  // Try to get the last path component, failing that return the original
  // presumed location.
  llvm::StringRef PLFileName = llvm::sys::path::filename(PLoc.getFilename());
  if (PLFileName.empty())
    PLFileName = PLoc.getFilename();
  FileName.append(PLFileName.begin(), PLFileName.end());
  formatPathForFileMacro(FileName, LangOpts, TI);
}
