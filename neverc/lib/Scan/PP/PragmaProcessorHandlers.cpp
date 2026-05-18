#include "neverc/Foundation/Diagnostic/CLWarnings.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/MacroGuardValidator.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepOptions.h"
#include "neverc/Scan/LexDiag.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Timer.h"
#include <cassert>
#include <memory>
#include <optional>
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
namespace {

struct MacroGuardHandler : public PragmaDispatch {
  MacroGuardHandler() : PragmaDispatch("macro_arg_guard") {}
  MacroGuardValidator *Validator = nullptr;

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &PragmaTok) override {
    if (!Validator) {
      auto OwnedValidator =
          std::make_unique<MacroGuardValidator>(PP.getSourceManager());
      Validator = OwnedValidator.get();
      PP.addObserver(std::move(OwnedValidator));
    }

    Validator->clearPendingGuardArgs();

    Token Tok;
    PP.Lex(Tok);
    while (Tok.isNot(tok::eod)) {
      if (const auto *II = Tok.getIdentifierInfo())
        Validator->addPendingGuardArg(II);
      PP.Lex(Tok);
    }
  }
};

struct PragmaOnceHandler : public PragmaDispatch {
  PragmaOnceHandler() : PragmaDispatch("once") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &OnceTok) override {
    PP.VerifyDirectiveEnd("pragma once");
    PP.ExecPragmaOnce(OnceTok);
  }
};

struct PragmaMarkHandler : public PragmaDispatch {
  PragmaMarkHandler() : PragmaDispatch("mark") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &MarkTok) override {
    PP.ExecPragmaMark(MarkTok);
  }
};

struct PragmaPoisonHandler : public PragmaDispatch {
  PragmaPoisonHandler() : PragmaDispatch("poison") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &PoisonTok) override {
    PP.ExecPragmaPoison();
  }
};

struct PragmaSystemHeaderHandler : public PragmaDispatch {
  PragmaSystemHeaderHandler() : PragmaDispatch("system_header") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &SHToken) override {
    PP.ExecPragmaSysHeader(SHToken);
    PP.VerifyDirectiveEnd(tok::getPPKeywordSpelling(tok::pp_pragma));
  }
};

struct PragmaDependencyHandler : public PragmaDispatch {
  PragmaDependencyHandler() : PragmaDispatch("dependency") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &DepToken) override {
    PP.ExecPragmaDep(DepToken);
  }
};

struct PragmaDebugHandler : public PragmaDispatch {
  PragmaDebugHandler() : PragmaDispatch("__debug") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &DebugToken) override {
    Token Tok;
    PP.LexWithoutExpansion(Tok);
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok, diag::warn_pragma_debug_missing_command);
      return;
    }
    IdentifierInfo *II = Tok.getIdentifierInfo();

    if (II->isStr("assert")) {
      llvm_unreachable("This is an assertion!");
    } else if (II->isStr("crash")) {
      llvm::Timer T("crash", "pragma crash");
      llvm::TimeRegion R(&T);
      LLVM_BUILTIN_TRAP;
    } else if (II->isStr("parser_crash")) {
      Token Crasher;
      Crasher.startToken();
      Crasher.setKind(tok::annot_pragma_parser_crash);
      Crasher.setAnnotationRange(SourceRange(Tok.getLocation()));
      PP.InjectToken(Crasher, /*IsReinject*/ false);
    } else if (II->isStr("dump")) {
      Token DumpAnnot;
      DumpAnnot.startToken();
      DumpAnnot.setKind(tok::annot_pragma_dump);
      DumpAnnot.setAnnotationRange(SourceRange(Tok.getLocation()));
      PP.InjectToken(DumpAnnot, /*IsReinject*/ false);
    } else if (II->isStr("diag_mapping")) {
      Token DiagName;
      PP.LexWithoutExpansion(DiagName);
      if (DiagName.is(tok::eod))
        PP.getDiagnostics().dump();
      else if (DiagName.is(tok::string_literal)) {
        StringLiteralParser Literal(DiagName, PP,
                                    StringLiteralEvalMethod::Unevaluated);
        if (Literal.hadError)
          return;
        PP.getDiagnostics().dump(Literal.getString());
      } else {
        PP.Diag(DiagName, diag::warn_pragma_debug_missing_argument)
            << II->getName();
      }
    } else if (II->isStr("llvm_fatal_error")) {
      llvm::report_fatal_error("#pragma neverc __debug llvm_fatal_error");
    } else if (II->isStr("llvm_unreachable")) {
      llvm_unreachable("#pragma neverc __debug llvm_unreachable");
    } else if (II->isStr("macro")) {
      Token MacroName;
      PP.LexWithoutExpansion(MacroName);
      auto *MacroII = MacroName.getIdentifierInfo();
      if (MacroII)
        PP.traceMacroRecord(MacroII);
      else
        PP.Diag(MacroName, diag::warn_pragma_debug_missing_argument)
            << II->getName();
    } else if (II->isStr("overflow_stack")) {
      DebugOverflowStack();
    } else if (II->isStr("sloc_usage")) {
      // An optional integer literal argument specifies the number of files to
      // specifically report information about.
      std::optional<unsigned> MaxNotes;
      Token ArgToken;
      PP.Lex(ArgToken);
      uint64_t Value;
      if (ArgToken.is(tok::numeric_constant) &&
          PP.parseBasicInteger(ArgToken, Value)) {
        MaxNotes = Value;
      } else if (ArgToken.isNot(tok::eod)) {
        PP.Diag(ArgToken, diag::warn_pragma_debug_unexpected_argument);
      }

      PP.Diag(Tok, diag::remark_sloc_usage);
      PP.getSourceManager().noteSLocAddressSpaceUsage(PP.getDiagnostics(),
                                                      MaxNotes);
    } else {
      PP.Diag(Tok, diag::warn_pragma_debug_unexpected_command) << II->getName();
    }

    PrepObserver *Callbacks = PP.getObserver();
    if (Callbacks)
      Callbacks->PragmaDebug(Tok.getLocation(), II->getName());
  }

// Disable MSVC warning about runtime stack overflow.
#ifdef _MSC_VER
#pragma warning(disable : 4717)
#endif
  static void DebugOverflowStack(void (*P)() = nullptr) {
    void (*volatile Self)(void (*P)()) = DebugOverflowStack;
    Self(reinterpret_cast<void (*)()>(Self));
  }
#ifdef _MSC_VER
#pragma warning(default : 4717)
#endif
};

struct PragmaUnsafeBufferUsageHandler : public PragmaDispatch {
  PragmaUnsafeBufferUsageHandler() : PragmaDispatch("unsafe_buffer_usage") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override {
    Token Tok;

    PP.LexWithoutExpansion(Tok);
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok, diag::err_pp_pragma_unsafe_buffer_usage_syntax);
      return;
    }

    IdentifierInfo *II = Tok.getIdentifierInfo();
    SourceLocation Loc = Tok.getLocation();

    if (II->isStr("begin")) {
      if (PP.enterOrExitSafeBufferOptOutRegion(true, Loc))
        PP.Diag(Loc, diag::err_pp_double_begin_pragma_unsafe_buffer_usage);
    } else if (II->isStr("end")) {
      if (PP.enterOrExitSafeBufferOptOutRegion(false, Loc))
        PP.Diag(Loc,
                diag::err_pp_unmatched_end_begin_pragma_unsafe_buffer_usage);
    } else
      PP.Diag(Tok, diag::err_pp_pragma_unsafe_buffer_usage_syntax);
  }
};

struct PragmaDiagnosticHandler : public PragmaDispatch {
private:
  const char *Namespace;

public:
  explicit PragmaDiagnosticHandler(const char *NS)
      : PragmaDispatch("diagnostic"), Namespace(NS) {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &DiagToken) override {
    SourceLocation DiagLoc = DiagToken.getLocation();
    Token Tok;
    PP.LexWithoutExpansion(Tok);
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok, diag::warn_pragma_diagnostic_invalid);
      return;
    }
    IdentifierInfo *II = Tok.getIdentifierInfo();
    PrepObserver *Callbacks = PP.getObserver();

    // Get the next token, which is either an EOD or a string literal. We lex
    // it now so that we can early return if the previous token was push or pop.
    PP.LexWithoutExpansion(Tok);

    if (II->isStr("pop")) {
      if (!PP.getDiagnostics().popMappings(DiagLoc))
        PP.Diag(Tok, diag::warn_pragma_diagnostic_cannot_pop);
      else if (Callbacks)
        Callbacks->PragmaDiagnosticPop(DiagLoc, Namespace);

      if (Tok.isNot(tok::eod))
        PP.Diag(Tok.getLocation(), diag::warn_pragma_diagnostic_invalid_token);
      return;
    } else if (II->isStr("push")) {
      PP.getDiagnostics().pushMappings(DiagLoc);
      if (Callbacks)
        Callbacks->PragmaDiagnosticPush(DiagLoc, Namespace);

      if (Tok.isNot(tok::eod))
        PP.Diag(Tok.getLocation(), diag::warn_pragma_diagnostic_invalid_token);
      return;
    }

    diag::Severity SV = llvm::StringSwitch<diag::Severity>(II->getName())
                            .Case("ignored", diag::Severity::Ignored)
                            .Case("warning", diag::Severity::Warning)
                            .Case("error", diag::Severity::Error)
                            .Case("fatal", diag::Severity::Fatal)
                            .Default(diag::Severity());

    if (SV == diag::Severity()) {
      PP.Diag(Tok, diag::warn_pragma_diagnostic_invalid);
      return;
    }

    // At this point, we expect a string literal.
    SourceLocation StringLoc = Tok.getLocation();
    std::string WarningName;
    if (!PP.CompleteStringLiteral(Tok, WarningName, "pragma diagnostic",
                                  /*AllowMacroExpansion=*/false))
      return;

    if (Tok.isNot(tok::eod)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_diagnostic_invalid_token);
      return;
    }

    if (WarningName.size() < 3 || WarningName[0] != '-' ||
        (WarningName[1] != 'W' && WarningName[1] != 'R')) {
      PP.Diag(StringLoc, diag::warn_pragma_diagnostic_invalid_option);
      return;
    }

    diag::Flavor Flavor = WarningName[1] == 'W' ? diag::Flavor::WarningOrError
                                                : diag::Flavor::Remark;
    llvm::StringRef Group = llvm::StringRef(WarningName).substr(2);
    bool unknownDiag = false;
    if (Group == "everything") {
      // Special handling for pragma neverc diagnostic ... "-Weverything".
      // There is no formal group named "everything", so there has to be a
      // special case for it.
      PP.getDiagnostics().setSeverityForAll(Flavor, SV, DiagLoc);
    } else
      unknownDiag =
          PP.getDiagnostics().setSeverityForGroup(Flavor, Group, SV, DiagLoc);
    if (unknownDiag)
      PP.Diag(StringLoc, diag::warn_pragma_diagnostic_unknown_warning)
          << WarningName;
    else if (Callbacks)
      Callbacks->PragmaDiagnostic(DiagLoc, Namespace, SV, WarningName);
  }
};

struct PragmaWarningHandler : public PragmaDispatch {
  PragmaWarningHandler() : PragmaDispatch("warning") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &Tok) override {
    // Parse things like:
    // warning(push, 1)
    // warning(pop)
    // warning(disable : 1 2 3 ; error : 4 5 6 ; suppress : 7 8 9)
    SourceLocation DiagLoc = Tok.getLocation();
    PrepObserver *Callbacks = PP.getObserver();

    PP.Lex(Tok);
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok, diag::warn_pragma_warning_expected) << "(";
      return;
    }

    PP.Lex(Tok);
    IdentifierInfo *II = Tok.getIdentifierInfo();

    if (II && II->isStr("push")) {
      // #pragma warning( push[ ,n ] )
      int Level = -1;
      PP.Lex(Tok);
      if (Tok.is(tok::comma)) {
        PP.Lex(Tok);
        uint64_t Value;
        if (Tok.is(tok::numeric_constant) && PP.parseBasicInteger(Tok, Value))
          Level = int(Value);
        if (Level < 0 || Level > 4) {
          PP.Diag(Tok, diag::warn_pragma_warning_push_level);
          return;
        }
      }
      PP.getDiagnostics().pushMappings(DiagLoc);
      if (Callbacks)
        Callbacks->PragmaWarningPush(DiagLoc, Level);
    } else if (II && II->isStr("pop")) {
      // #pragma warning( pop )
      PP.Lex(Tok);
      if (!PP.getDiagnostics().popMappings(DiagLoc))
        PP.Diag(Tok, diag::warn_pragma_diagnostic_cannot_pop);
      else if (Callbacks)
        Callbacks->PragmaWarningPop(DiagLoc);
    } else {
      // #pragma warning( warning-specifier : warning-number-list
      //                  [; warning-specifier : warning-number-list...] )
      while (true) {
        II = Tok.getIdentifierInfo();
        if (!II && !Tok.is(tok::numeric_constant)) {
          PP.Diag(Tok, diag::warn_pragma_warning_spec_invalid);
          return;
        }

        // Figure out which warning specifier this is.
        bool SpecifierValid;
        PrepObserver::PragmaWarningSpecifier Specifier;
        if (II) {
          int SpecifierInt = llvm::StringSwitch<int>(II->getName())
                                 .Case("default", PrepObserver::PWS_Default)
                                 .Case("disable", PrepObserver::PWS_Disable)
                                 .Case("error", PrepObserver::PWS_Error)
                                 .Case("once", PrepObserver::PWS_Once)
                                 .Case("suppress", PrepObserver::PWS_Suppress)
                                 .Default(-1);
          if ((SpecifierValid = SpecifierInt != -1))
            Specifier =
                static_cast<PrepObserver::PragmaWarningSpecifier>(SpecifierInt);

          // If we read a correct specifier, snatch next token (that should be
          // ":", checked later).
          if (SpecifierValid)
            PP.Lex(Tok);
        } else {
          // Token is a numeric constant. It should be either 1, 2, 3 or 4.
          uint64_t Value;
          if (PP.parseBasicInteger(Tok, Value)) {
            if ((SpecifierValid = (Value >= 1) && (Value <= 4)))
              Specifier = static_cast<PrepObserver::PragmaWarningSpecifier>(
                  PrepObserver::PWS_Level1 + Value - 1);
          } else
            SpecifierValid = false;
          // Next token already snatched by parseBasicInteger.
        }

        if (!SpecifierValid) {
          PP.Diag(Tok, diag::warn_pragma_warning_spec_invalid);
          return;
        }
        if (Tok.isNot(tok::colon)) {
          PP.Diag(Tok, diag::warn_pragma_warning_expected) << ":";
          return;
        }

        // Collect the warning ids.
        llvm::SmallVector<int, 4> Ids;
        PP.Lex(Tok);
        while (Tok.is(tok::numeric_constant)) {
          uint64_t Value;
          if (!PP.parseBasicInteger(Tok, Value) || Value == 0 ||
              Value > INT_MAX) {
            PP.Diag(Tok, diag::warn_pragma_warning_expected_number);
            return;
          }
          Ids.push_back(int(Value));
        }

        // Only act on disable for now.
        diag::Severity SV = diag::Severity();
        if (Specifier == PrepObserver::PWS_Disable)
          SV = diag::Severity::Ignored;
        if (SV != diag::Severity())
          for (int Id : Ids) {
            if (auto Group = diagGroupFromCLWarningID(Id)) {
              bool unknownDiag = PP.getDiagnostics().setSeverityForGroup(
                  diag::Flavor::WarningOrError, *Group, SV, DiagLoc);
              assert(!unknownDiag &&
                     "wd table should only contain known diags");
              (void)unknownDiag;
            }
          }

        if (Callbacks)
          Callbacks->PragmaWarning(DiagLoc, Specifier, Ids);

        // Parse the next specifier if there is a semicolon.
        if (Tok.isNot(tok::semi))
          break;
        PP.Lex(Tok);
      }
    }

    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok, diag::warn_pragma_warning_expected) << ")";
      return;
    }

    PP.Lex(Tok);
    if (Tok.isNot(tok::eod))
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol) << "pragma warning";
  }
};

struct PragmaExecCharsetHandler : public PragmaDispatch {
  PragmaExecCharsetHandler() : PragmaDispatch("execution_character_set") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &Tok) override {
    // Parse things like:
    // execution_character_set(push, "UTF-8")
    // execution_character_set(pop)
    SourceLocation DiagLoc = Tok.getLocation();
    PrepObserver *Callbacks = PP.getObserver();

    PP.Lex(Tok);
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok, diag::warn_pragma_exec_charset_expected) << "(";
      return;
    }

    PP.Lex(Tok);
    IdentifierInfo *II = Tok.getIdentifierInfo();

    if (II && II->isStr("push")) {
      // #pragma execution_character_set( push[ , string ] )
      PP.Lex(Tok);
      if (Tok.is(tok::comma)) {
        PP.Lex(Tok);

        std::string ExecCharset;
        if (!PP.CompleteStringLiteral(Tok, ExecCharset,
                                      "pragma execution_character_set",
                                      /*AllowMacroExpansion=*/false))
          return;

        // MSVC supports either of these, but nothing else.
        if (ExecCharset != "UTF-8" && ExecCharset != "utf-8") {
          PP.Diag(Tok, diag::warn_pragma_exec_charset_push_invalid)
              << ExecCharset;
          return;
        }
      }
      if (Callbacks)
        Callbacks->PragmaExecCharsetPush(DiagLoc, "UTF-8");
    } else if (II && II->isStr("pop")) {
      // #pragma execution_character_set( pop )
      PP.Lex(Tok);
      if (Callbacks)
        Callbacks->PragmaExecCharsetPop(DiagLoc);
    } else {
      PP.Diag(Tok, diag::warn_pragma_exec_charset_spec_invalid);
      return;
    }

    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok, diag::warn_pragma_exec_charset_expected) << ")";
      return;
    }

    PP.Lex(Tok);
    if (Tok.isNot(tok::eod))
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol)
          << "pragma execution_character_set";
  }
};

struct PragmaIncludeAliasHandler : public PragmaDispatch {
  PragmaIncludeAliasHandler() : PragmaDispatch("include_alias") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &IncludeAliasTok) override {
    PP.ExecPragmaAlias(IncludeAliasTok);
  }
};

struct PragmaMessageHandler : public PragmaDispatch {
private:
  const PrepObserver::PragmaMessageKind Kind;
  const llvm::StringRef Namespace;

  static const char *PragmaKind(PrepObserver::PragmaMessageKind Kind,
                                bool PragmaNameOnly = false) {
    switch (Kind) {
    case PrepObserver::PMK_Message:
      return PragmaNameOnly ? "message" : "pragma message";
    case PrepObserver::PMK_Warning:
      return PragmaNameOnly ? "warning" : "pragma warning";
    case PrepObserver::PMK_Error:
      return PragmaNameOnly ? "error" : "pragma error";
    }
    llvm_unreachable("Unknown PragmaMessageKind!");
  }

public:
  PragmaMessageHandler(PrepObserver::PragmaMessageKind Kind,
                       llvm::StringRef Namespace = llvm::StringRef())
      : PragmaDispatch(PragmaKind(Kind, true)), Kind(Kind),
        Namespace(Namespace) {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &Tok) override {
    SourceLocation MessageLoc = Tok.getLocation();
    PP.Lex(Tok);
    bool ExpectClosingParen = false;
    switch (Tok.getKind()) {
    case tok::l_paren:
      // We have a MSVC style pragma message.
      ExpectClosingParen = true;
      // Read the string.
      PP.Lex(Tok);
      break;
    case tok::string_literal:
      // We have a GCC style pragma message, and we just read the string.
      break;
    default:
      PP.Diag(MessageLoc, diag::err_pragma_message_malformed) << Kind;
      return;
    }

    std::string MessageString;
    if (!PP.CompleteStringLiteral(Tok, MessageString, PragmaKind(Kind),
                                  /*AllowMacroExpansion=*/true))
      return;

    if (ExpectClosingParen) {
      if (Tok.isNot(tok::r_paren)) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_message_malformed) << Kind;
        return;
      }
      PP.Lex(Tok); // eat the r_paren.
    }

    if (Tok.isNot(tok::eod)) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_message_malformed) << Kind;
      return;
    }

    // Output the message.
    PP.Diag(MessageLoc, (Kind == PrepObserver::PMK_Error)
                            ? diag::err_pragma_message
                            : diag::warn_pragma_message)
        << MessageString;

    // If the pragma is lexically sound, notify any interested PrepObserver.
    if (PrepObserver *Callbacks = PP.getObserver())
      Callbacks->PragmaMessage(MessageLoc, Namespace, Kind, MessageString);
  }
};

struct PragmaPushMacroHandler : public PragmaDispatch {
  PragmaPushMacroHandler() : PragmaDispatch("push_macro") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &PushMacroTok) override {
    PP.ExecPragmaPush(PushMacroTok);
  }
};

struct PragmaPopMacroHandler : public PragmaDispatch {
  PragmaPopMacroHandler() : PragmaDispatch("pop_macro") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &PopMacroTok) override {
    PP.ExecPragmaPop(PopMacroTok);
  }
};

struct PragmaAssumeNonNullHandler : public PragmaDispatch {
  PragmaAssumeNonNullHandler() : PragmaDispatch("assume_nonnull") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &NameTok) override {
    SourceLocation Loc = NameTok.getLocation();
    bool IsBegin;

    Token Tok;

    // Lex the 'begin' or 'end'.
    PP.LexWithoutExpansion(Tok);
    const IdentifierInfo *BeginEnd = Tok.getIdentifierInfo();
    if (BeginEnd && BeginEnd->isStr("begin")) {
      IsBegin = true;
    } else if (BeginEnd && BeginEnd->isStr("end")) {
      IsBegin = false;
    } else {
      PP.Diag(Tok.getLocation(), diag::err_pp_assume_nonnull_syntax);
      return;
    }

    // Verify that this is followed by EOD.
    PP.LexWithoutExpansion(Tok);
    if (Tok.isNot(tok::eod))
      PP.Diag(Tok, diag::ext_pp_extra_tokens_at_eol)
          << tok::getPPKeywordSpelling(tok::pp_pragma);

    // The start location of the active audit.
    SourceLocation BeginLoc = PP.getPragmaAssumeNonNullLoc();

    // The start location we want after processing this.
    SourceLocation NewLoc;
    PrepObserver *Callbacks = PP.getObserver();

    if (IsBegin) {
      // Complain about attempts to re-enter an audit.
      if (BeginLoc.isValid()) {
        PP.Diag(Loc, diag::err_pp_double_begin_of_assume_nonnull);
        PP.Diag(BeginLoc, diag::note_pragma_entered_here);
      }
      NewLoc = Loc;
      if (Callbacks)
        Callbacks->PragmaAssumeNonNullBegin(NewLoc);
    } else {
      // Complain about attempts to leave an audit that doesn't exist.
      if (!BeginLoc.isValid()) {
        PP.Diag(Loc, diag::err_pp_unmatched_end_of_assume_nonnull);
        return;
      }
      NewLoc = SourceLocation();
      if (Callbacks)
        Callbacks->PragmaAssumeNonNullEnd(NewLoc);
    }

    PP.setPragmaAssumeNonNullLoc(NewLoc);
  }
};

struct PragmaRegionHandler : public PragmaDispatch {
  PragmaRegionHandler(const char *pragma) : PragmaDispatch(pragma) {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &NameTok) override {
    // #pragma region: endregion matches can be verified
    // __pragma(region): no sense, but ignored by msvc
    // _Pragma is not valid for MSVC, but there isn't any point
    // to handle a _Pragma differently.
  }
};

struct PragmaManagedHandler : public EmptyPragmaDispatch {
  PragmaManagedHandler(const char *pragma) : EmptyPragmaDispatch(pragma) {}
};

IdentifierInfo *parseMacroAnnotationArg(PrepEngine &PP, Token &Tok,
                                        const char *Pragma,
                                        std::string &MessageString) {
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(Tok, diag::err_expected) << "(";
    return nullptr;
  }

  PP.LexWithoutExpansion(Tok);
  if (!Tok.is(tok::identifier)) {
    PP.Diag(Tok, diag::err_expected) << tok::identifier;
    return nullptr;
  }
  IdentifierInfo *II = Tok.getIdentifierInfo();

  if (!II->hasMacroDefinition()) {
    PP.Diag(Tok, diag::err_pp_visibility_non_macro) << II;
    return nullptr;
  }

  PP.Lex(Tok);
  if (Tok.is(tok::comma)) {
    PP.Lex(Tok);
    if (!PP.CompleteStringLiteral(Tok, MessageString, Pragma,
                                  /*AllowMacroExpansion=*/true))
      return nullptr;
  }

  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(Tok, diag::err_expected) << ")";
    return nullptr;
  }
  return II;
}

struct PragmaDeprecatedHandler : public PragmaDispatch {
  PragmaDeprecatedHandler() : PragmaDispatch("deprecated") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &Tok) override {
    std::string MessageString;

    if (IdentifierInfo *II = parseMacroAnnotationArg(
            PP, Tok, "#pragma neverc deprecated", MessageString)) {
      II->setIsDeprecatedMacro(true);
      PP.addMacroDeprecationMsg(II, std::move(MessageString),
                                Tok.getLocation());
    }
  }
};

struct PragmaRestrictExpansionHandler : public PragmaDispatch {
  PragmaRestrictExpansionHandler() : PragmaDispatch("restrict_expansion") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &Tok) override {
    std::string MessageString;

    if (IdentifierInfo *II = parseMacroAnnotationArg(
            PP, Tok, "#pragma neverc restrict_expansion", MessageString)) {
      II->setIsRestrictExpansion(true);
      PP.addRestrictExpansionMsg(II, std::move(MessageString),
                                 Tok.getLocation());
    }
  }
};

struct PragmaFinalHandler : public PragmaDispatch {
  PragmaFinalHandler() : PragmaDispatch("final") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &Tok) override {
    PP.Lex(Tok);
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok, diag::err_expected) << "(";
      return;
    }

    PP.LexWithoutExpansion(Tok);
    if (!Tok.is(tok::identifier)) {
      PP.Diag(Tok, diag::err_expected) << tok::identifier;
      return;
    }
    IdentifierInfo *II = Tok.getIdentifierInfo();

    if (!II->hasMacroDefinition()) {
      PP.Diag(Tok, diag::err_pp_visibility_non_macro) << II;
      return;
    }

    PP.Lex(Tok);
    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok, diag::err_expected) << ")";
      return;
    }
    II->setIsFinal(true);
    PP.addFinalLoc(II, Tok.getLocation());
  }
};

} // namespace

void PrepEngine::InitBuiltinPragmas() {
  AddPragmaDispatch(new MacroGuardHandler());
  AddPragmaDispatch(new PragmaOnceHandler());
  AddPragmaDispatch(new PragmaMarkHandler());
  AddPragmaDispatch(new PragmaPushMacroHandler());
  AddPragmaDispatch(new PragmaPopMacroHandler());
  AddPragmaDispatch(new PragmaMessageHandler(PrepObserver::PMK_Message));

  // #pragma GCC ...
  AddPragmaDispatch("GCC", new PragmaPoisonHandler());
  AddPragmaDispatch("GCC", new PragmaSystemHeaderHandler());
  AddPragmaDispatch("GCC", new PragmaDependencyHandler());
  AddPragmaDispatch("GCC", new PragmaDiagnosticHandler("GCC"));
  AddPragmaDispatch("GCC",
                    new PragmaMessageHandler(PrepObserver::PMK_Warning, "GCC"));
  AddPragmaDispatch("GCC",
                    new PragmaMessageHandler(PrepObserver::PMK_Error, "GCC"));
  // #pragma neverc ... (primary namespace)
  AddPragmaDispatch("neverc", new PragmaPoisonHandler());
  AddPragmaDispatch("neverc", new PragmaSystemHeaderHandler());
  AddPragmaDispatch("neverc", new PragmaDebugHandler());
  AddPragmaDispatch("neverc", new PragmaDependencyHandler());
  AddPragmaDispatch("neverc", new PragmaDiagnosticHandler("neverc"));
  AddPragmaDispatch("neverc", new PragmaAssumeNonNullHandler());
  AddPragmaDispatch("neverc", new PragmaDeprecatedHandler());
  AddPragmaDispatch("neverc", new PragmaRestrictExpansionHandler());
  AddPragmaDispatch("neverc", new PragmaFinalHandler());
  AddPragmaDispatch("neverc", new PragmaUnsafeBufferUsageHandler);

  // Add region pragmas.
  AddPragmaDispatch(new PragmaRegionHandler("region"));
  AddPragmaDispatch(new PragmaRegionHandler("endregion"));

  // MS extensions.
  if (LangOpts.MicrosoftExt) {
    AddPragmaDispatch(new PragmaWarningHandler());
    AddPragmaDispatch(new PragmaExecCharsetHandler());
    AddPragmaDispatch(new PragmaIncludeAliasHandler());
    AddPragmaDispatch(new PragmaSystemHeaderHandler());
    AddPragmaDispatch(new PragmaManagedHandler("managed"));
    AddPragmaDispatch(new PragmaManagedHandler("unmanaged"));
  }
}

void PrepEngine::IgnorePragmas() {
  AddPragmaDispatch(new EmptyPragmaDispatch());
  AddPragmaDispatch("GCC", new EmptyPragmaDispatch());
  AddPragmaDispatch("neverc", new EmptyPragmaDispatch());
}
