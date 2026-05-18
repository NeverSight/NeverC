#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Foundation/Core/PragmaKinds.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PragmaDispatch.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/Token.h"
#include "neverc/Syntax/ParseDiag.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringSwitch.h"
#include <optional>
using namespace neverc;

namespace {

struct PragmaAlignHandler : public PragmaDispatch {
  explicit PragmaAlignHandler() : PragmaDispatch("align") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

struct PragmaGCCVisibilityHandler : public PragmaDispatch {
  explicit PragmaGCCVisibilityHandler() : PragmaDispatch("visibility") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

struct PragmaOptionsHandler : public PragmaDispatch {
  explicit PragmaOptionsHandler() : PragmaDispatch("options") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

struct PragmaPackHandler : public PragmaDispatch {
  explicit PragmaPackHandler() : PragmaDispatch("pack") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

struct PragmaSectionHandler : public PragmaDispatch {
  explicit PragmaSectionHandler(Sema &S)
      : PragmaDispatch("section"), Actions(S) {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;

private:
  Sema &Actions;
};

struct PragmaMSStructHandler : public PragmaDispatch {
  explicit PragmaMSStructHandler() : PragmaDispatch("ms_struct") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

struct PragmaUnusedHandler : public PragmaDispatch {
  PragmaUnusedHandler() : PragmaDispatch("unused") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

struct PragmaWeakHandler : public PragmaDispatch {
  explicit PragmaWeakHandler() : PragmaDispatch("weak") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

struct PragmaFPContractHandler : public PragmaDispatch {
  PragmaFPContractHandler() : PragmaDispatch("FP_CONTRACT") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

// Pragma STDC implementations.

struct PragmaSTDC_FENV_ACCESSHandler : public PragmaDispatch {
  PragmaSTDC_FENV_ACCESSHandler() : PragmaDispatch("FENV_ACCESS") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &Tok) override {
    Token PragmaName = Tok;
    if (!PP.getTargetInfo().hasStrictFP() && !PP.getLangOpts().ExpStrictFP) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_fp_ignored)
          << PragmaName.getIdentifierInfo()->getName();
      return;
    }
    tok::OnOffSwitch OOS;
    if (PP.LexOnOffSwitch(OOS))
      return;

    llvm::MutableArrayRef<Token> Toks(
        PP.getPrepEngineAllocator().Allocate<Token>(1), 1);
    Toks[0].startToken();
    Toks[0].setKind(tok::annot_pragma_fenv_access);
    Toks[0].setLocation(Tok.getLocation());
    Toks[0].setAnnotationEndLoc(Tok.getLocation());
    Toks[0].setAnnotationValue(
        reinterpret_cast<void *>(static_cast<uintptr_t>(OOS)));
    PP.PushTokenStream(Toks, /*DisableMacroExpansion=*/true,
                       /*IsReinject=*/false);
  }
};

struct PragmaSTDC_CX_LIMITED_RANGEHandler : public PragmaDispatch {
  PragmaSTDC_CX_LIMITED_RANGEHandler() : PragmaDispatch("CX_LIMITED_RANGE") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &Tok) override {
    tok::OnOffSwitch OOS;
    if (PP.LexOnOffSwitch(OOS))
      return;

    llvm::MutableArrayRef<Token> Toks(
        PP.getPrepEngineAllocator().Allocate<Token>(1), 1);

    Toks[0].startToken();
    Toks[0].setKind(tok::annot_pragma_cx_limited_range);
    Toks[0].setLocation(Tok.getLocation());
    Toks[0].setAnnotationEndLoc(Tok.getLocation());
    Toks[0].setAnnotationValue(
        reinterpret_cast<void *>(static_cast<uintptr_t>(OOS)));
    PP.PushTokenStream(Toks, /*DisableMacroExpansion=*/true,
                       /*IsReinject=*/false);
  }
};

struct PragmaSTDC_FENV_ROUNDHandler : public PragmaDispatch {
  PragmaSTDC_FENV_ROUNDHandler() : PragmaDispatch("FENV_ROUND") {}

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &Tok) override;
};

struct PragmaSTDC_UnknownHandler : public PragmaDispatch {
  PragmaSTDC_UnknownHandler() = default;

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &UnknownTok) override {
    // C99 6.10.6p2, unknown forms are not allowed.
    PP.Diag(UnknownTok, diag::ext_stdc_pragma_ignored);
  }
};

struct PragmaFPHandler : public PragmaDispatch {
  PragmaFPHandler() : PragmaDispatch("fp") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

struct PragmaCommentHandler : public PragmaDispatch {
  PragmaCommentHandler(Sema &Actions)
      : PragmaDispatch("comment"), Actions(Actions) {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;

private:
  Sema &Actions;
};

struct PragmaDetectMismatchHandler : public PragmaDispatch {
  PragmaDetectMismatchHandler(Sema &Actions)
      : PragmaDispatch("detect_mismatch"), Actions(Actions) {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;

private:
  Sema &Actions;
};

struct PragmaFloatControlHandler : public PragmaDispatch {
  PragmaFloatControlHandler(Sema &Actions) : PragmaDispatch("float_control") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

struct PragmaMSPragma : public PragmaDispatch {
  explicit PragmaMSPragma(const char *name) : PragmaDispatch(name) {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

struct PragmaOptimizeHandler : public PragmaDispatch {
  PragmaOptimizeHandler(Sema &S) : PragmaDispatch("optimize"), Actions(S) {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;

private:
  Sema &Actions;
};

struct PragmaMSRuntimeChecksHandler : public EmptyPragmaDispatch {
  PragmaMSRuntimeChecksHandler() : EmptyPragmaDispatch("runtime_checks") {}
};

struct PragmaMSIntrinsicHandler : public PragmaDispatch {
  PragmaMSIntrinsicHandler() : PragmaDispatch("intrinsic") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

// "\#pragma fenv_access (on)".
struct PragmaMSFenvAccessHandler : public PragmaDispatch {
  PragmaMSFenvAccessHandler() : PragmaDispatch("fenv_access") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override {
    llvm::StringRef PragmaName = FirstToken.getIdentifierInfo()->getName();
    if (!PP.getTargetInfo().hasStrictFP() && !PP.getLangOpts().ExpStrictFP) {
      PP.Diag(FirstToken.getLocation(), diag::warn_pragma_fp_ignored)
          << PragmaName;
      return;
    }

    Token Tok;
    PP.Lex(Tok);
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_lparen)
          << PragmaName;
      return;
    }
    PP.Lex(Tok); // Consume the l_paren.
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_ms_fenv_access);
      return;
    }
    const IdentifierInfo *II = Tok.getIdentifierInfo();
    tok::OnOffSwitch OOS;
    if (II->isStr("on")) {
      OOS = tok::OOS_ON;
      PP.Lex(Tok);
    } else if (II->isStr("off")) {
      OOS = tok::OOS_OFF;
      PP.Lex(Tok);
    } else {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_ms_fenv_access);
      return;
    }
    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_rparen)
          << PragmaName;
      return;
    }
    PP.Lex(Tok); // Consume the r_paren.

    if (Tok.isNot(tok::eod)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
          << PragmaName;
      return;
    }

    llvm::MutableArrayRef<Token> Toks(
        PP.getPrepEngineAllocator().Allocate<Token>(1), 1);
    Toks[0].startToken();
    Toks[0].setKind(tok::annot_pragma_fenv_access_ms);
    Toks[0].setLocation(FirstToken.getLocation());
    Toks[0].setAnnotationEndLoc(Tok.getLocation());
    Toks[0].setAnnotationValue(
        reinterpret_cast<void *>(static_cast<uintptr_t>(OOS)));
    PP.PushTokenStream(Toks, /*DisableMacroExpansion=*/true,
                       /*IsReinject=*/false);
  }
};

struct PragmaAttributeHandler : public PragmaDispatch {
  PragmaAttributeHandler(AttributeFactory &AttrFactory)
      : PragmaDispatch("attribute"), AttributesForPragmaAttribute(AttrFactory) {
  }
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;

  ParsedAttributes AttributesForPragmaAttribute;
};

struct PragmaMaxTokensHereHandler : public PragmaDispatch {
  PragmaMaxTokensHereHandler() : PragmaDispatch("max_tokens_here") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

struct PragmaMaxTokensTotalHandler : public PragmaDispatch {
  PragmaMaxTokensTotalHandler() : PragmaDispatch("max_tokens_total") {}
  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

void markAsReinjectedForRelexing(llvm::MutableArrayRef<neverc::Token> Toks) {
  for (auto &T : Toks)
    T.setFlag(neverc::Token::IsReinjected);
}
} // end namespace

// ===----------------------------------------------------------------------===
// Pragma handler registration
// ===----------------------------------------------------------------------===

void Parser::initializePragmaHandlers() {
  AlignHandler = std::make_unique<PragmaAlignHandler>();
  PP.AddPragmaDispatch(AlignHandler.get());

  GCCVisibilityHandler = std::make_unique<PragmaGCCVisibilityHandler>();
  PP.AddPragmaDispatch("GCC", GCCVisibilityHandler.get());

  OptionsHandler = std::make_unique<PragmaOptionsHandler>();
  PP.AddPragmaDispatch(OptionsHandler.get());

  PackHandler = std::make_unique<PragmaPackHandler>();
  PP.AddPragmaDispatch(PackHandler.get());

  MSStructHandler = std::make_unique<PragmaMSStructHandler>();
  PP.AddPragmaDispatch(MSStructHandler.get());

  UnusedHandler = std::make_unique<PragmaUnusedHandler>();
  PP.AddPragmaDispatch(UnusedHandler.get());

  WeakHandler = std::make_unique<PragmaWeakHandler>();
  PP.AddPragmaDispatch(WeakHandler.get());

  FPContractHandler = std::make_unique<PragmaFPContractHandler>();
  PP.AddPragmaDispatch("STDC", FPContractHandler.get());

  STDCFenvAccessHandler = std::make_unique<PragmaSTDC_FENV_ACCESSHandler>();
  PP.AddPragmaDispatch("STDC", STDCFenvAccessHandler.get());

  STDCFenvRoundHandler = std::make_unique<PragmaSTDC_FENV_ROUNDHandler>();
  PP.AddPragmaDispatch("STDC", STDCFenvRoundHandler.get());

  STDCCXLIMITHandler = std::make_unique<PragmaSTDC_CX_LIMITED_RANGEHandler>();
  PP.AddPragmaDispatch("STDC", STDCCXLIMITHandler.get());

  STDCUnknownHandler = std::make_unique<PragmaSTDC_UnknownHandler>();
  PP.AddPragmaDispatch("STDC", STDCUnknownHandler.get());

  PCSectionHandler = std::make_unique<PragmaSectionHandler>(Actions);
  PP.AddPragmaDispatch("neverc", PCSectionHandler.get());

  if (getLangOpts().MicrosoftExt ||
      getTargetInfo().getTriple().isOSBinFormatELF()) {
    MSCommentHandler = std::make_unique<PragmaCommentHandler>(Actions);
    PP.AddPragmaDispatch(MSCommentHandler.get());
  }

  FloatControlHandler = std::make_unique<PragmaFloatControlHandler>(Actions);
  PP.AddPragmaDispatch(FloatControlHandler.get());
  if (getLangOpts().MicrosoftExt) {
    MSDetectMismatchHandler =
        std::make_unique<PragmaDetectMismatchHandler>(Actions);
    PP.AddPragmaDispatch(MSDetectMismatchHandler.get());
    MSInitSeg = std::make_unique<PragmaMSPragma>("init_seg");
    PP.AddPragmaDispatch(MSInitSeg.get());
    MSDataSeg = std::make_unique<PragmaMSPragma>("data_seg");
    PP.AddPragmaDispatch(MSDataSeg.get());
    MSBSSSeg = std::make_unique<PragmaMSPragma>("bss_seg");
    PP.AddPragmaDispatch(MSBSSSeg.get());
    MSConstSeg = std::make_unique<PragmaMSPragma>("const_seg");
    PP.AddPragmaDispatch(MSConstSeg.get());
    MSCodeSeg = std::make_unique<PragmaMSPragma>("code_seg");
    PP.AddPragmaDispatch(MSCodeSeg.get());
    MSSection = std::make_unique<PragmaMSPragma>("section");
    PP.AddPragmaDispatch(MSSection.get());
    MSStrictGuardStackCheck =
        std::make_unique<PragmaMSPragma>("strict_gs_check");
    PP.AddPragmaDispatch(MSStrictGuardStackCheck.get());
    MSFunction = std::make_unique<PragmaMSPragma>("function");
    PP.AddPragmaDispatch(MSFunction.get());
    MSAllocText = std::make_unique<PragmaMSPragma>("alloc_text");
    PP.AddPragmaDispatch(MSAllocText.get());
    MSOptimize = std::make_unique<PragmaMSPragma>("optimize");
    PP.AddPragmaDispatch(MSOptimize.get());
    MSRuntimeChecks = std::make_unique<PragmaMSRuntimeChecksHandler>();
    PP.AddPragmaDispatch(MSRuntimeChecks.get());
    MSIntrinsic = std::make_unique<PragmaMSIntrinsicHandler>();
    PP.AddPragmaDispatch(MSIntrinsic.get());
    MSFenvAccess = std::make_unique<PragmaMSFenvAccessHandler>();
    PP.AddPragmaDispatch(MSFenvAccess.get());
  }

  OptimizeHandler = std::make_unique<PragmaOptimizeHandler>(Actions);
  PP.AddPragmaDispatch("neverc", OptimizeHandler.get());

  FPHandler = std::make_unique<PragmaFPHandler>();
  PP.AddPragmaDispatch("neverc", FPHandler.get());

  AttributePragmaHandler =
      std::make_unique<PragmaAttributeHandler>(AttrFactory);
  PP.AddPragmaDispatch("neverc", AttributePragmaHandler.get());

  MaxTokensHerePragmaHandler = std::make_unique<PragmaMaxTokensHereHandler>();
  PP.AddPragmaDispatch("neverc", MaxTokensHerePragmaHandler.get());

  MaxTokensTotalPragmaHandler = std::make_unique<PragmaMaxTokensTotalHandler>();
  PP.AddPragmaDispatch("neverc", MaxTokensTotalPragmaHandler.get());
}

void Parser::resetPragmaHandlers() {
  // Remove the pragma handlers we installed.
  PP.RemovePragmaDispatch(AlignHandler.get());
  AlignHandler.reset();
  PP.RemovePragmaDispatch("GCC", GCCVisibilityHandler.get());
  GCCVisibilityHandler.reset();
  PP.RemovePragmaDispatch(OptionsHandler.get());
  OptionsHandler.reset();
  PP.RemovePragmaDispatch(PackHandler.get());
  PackHandler.reset();
  PP.RemovePragmaDispatch(MSStructHandler.get());
  MSStructHandler.reset();
  PP.RemovePragmaDispatch(UnusedHandler.get());
  UnusedHandler.reset();
  PP.RemovePragmaDispatch(WeakHandler.get());
  WeakHandler.reset();

  if (getLangOpts().MicrosoftExt ||
      getTargetInfo().getTriple().isOSBinFormatELF()) {
    PP.RemovePragmaDispatch(MSCommentHandler.get());
    MSCommentHandler.reset();
  }

  PP.RemovePragmaDispatch("neverc", PCSectionHandler.get());
  PCSectionHandler.reset();

  PP.RemovePragmaDispatch(FloatControlHandler.get());
  FloatControlHandler.reset();
  if (getLangOpts().MicrosoftExt) {
    PP.RemovePragmaDispatch(MSDetectMismatchHandler.get());
    MSDetectMismatchHandler.reset();
    PP.RemovePragmaDispatch(MSInitSeg.get());
    MSInitSeg.reset();
    PP.RemovePragmaDispatch(MSDataSeg.get());
    MSDataSeg.reset();
    PP.RemovePragmaDispatch(MSBSSSeg.get());
    MSBSSSeg.reset();
    PP.RemovePragmaDispatch(MSConstSeg.get());
    MSConstSeg.reset();
    PP.RemovePragmaDispatch(MSCodeSeg.get());
    MSCodeSeg.reset();
    PP.RemovePragmaDispatch(MSSection.get());
    MSSection.reset();
    PP.RemovePragmaDispatch(MSStrictGuardStackCheck.get());
    MSStrictGuardStackCheck.reset();
    PP.RemovePragmaDispatch(MSFunction.get());
    MSFunction.reset();
    PP.RemovePragmaDispatch(MSAllocText.get());
    MSAllocText.reset();
    PP.RemovePragmaDispatch(MSRuntimeChecks.get());
    MSRuntimeChecks.reset();
    PP.RemovePragmaDispatch(MSIntrinsic.get());
    MSIntrinsic.reset();
    PP.RemovePragmaDispatch(MSOptimize.get());
    MSOptimize.reset();
    PP.RemovePragmaDispatch(MSFenvAccess.get());
    MSFenvAccess.reset();
  }

  PP.RemovePragmaDispatch("STDC", FPContractHandler.get());
  FPContractHandler.reset();

  PP.RemovePragmaDispatch("STDC", STDCFenvAccessHandler.get());
  STDCFenvAccessHandler.reset();

  PP.RemovePragmaDispatch("STDC", STDCFenvRoundHandler.get());
  STDCFenvRoundHandler.reset();

  PP.RemovePragmaDispatch("STDC", STDCCXLIMITHandler.get());
  STDCCXLIMITHandler.reset();

  PP.RemovePragmaDispatch("STDC", STDCUnknownHandler.get());
  STDCUnknownHandler.reset();

  PP.RemovePragmaDispatch("neverc", OptimizeHandler.get());
  OptimizeHandler.reset();

  PP.RemovePragmaDispatch("neverc", FPHandler.get());
  FPHandler.reset();

  PP.RemovePragmaDispatch("neverc", AttributePragmaHandler.get());
  AttributePragmaHandler.reset();

  PP.RemovePragmaDispatch("neverc", MaxTokensHerePragmaHandler.get());
  MaxTokensHerePragmaHandler.reset();

  PP.RemovePragmaDispatch("neverc", MaxTokensTotalPragmaHandler.get());
  MaxTokensTotalPragmaHandler.reset();
}

// ===----------------------------------------------------------------------===
// Parser-level pragma processing
// ===----------------------------------------------------------------------===

void Parser::ProcessPragmaUnused() {
  assert(Tok.is(tok::annot_pragma_unused));
  SourceLocation UnusedLoc = ConsumeAnnotationToken();
  Actions.OnPragmaUnused(Tok, getCurScope(), UnusedLoc);
  ConsumeToken(); // The argument token.
}

void Parser::ProcessPragmaVisibility() {
  assert(Tok.is(tok::annot_pragma_vis));
  const IdentifierInfo *VisType =
      static_cast<IdentifierInfo *>(Tok.getAnnotationValue());
  SourceLocation VisLoc = ConsumeAnnotationToken();
  Actions.OnPragmaVisibility(VisType, VisLoc);
}

void Parser::ProcessPragmaPack() {
  assert(Tok.is(tok::annot_pragma_pack));
  Sema::PragmaPackInfo *Info =
      static_cast<Sema::PragmaPackInfo *>(Tok.getAnnotationValue());
  SourceLocation PragmaLoc = Tok.getLocation();
  ExprResult Alignment;
  if (Info->Alignment.is(tok::numeric_constant)) {
    Alignment = Actions.OnNumericConstant(Info->Alignment);
    if (Alignment.isInvalid()) {
      ConsumeAnnotationToken();
      return;
    }
  }
  Actions.OnPragmaPack(PragmaLoc, Info->Action, Info->SlotLabel,
                       Alignment.get());
  // Consume the token after processing the pragma to enable pragma-specific
  // #include warnings.
  ConsumeAnnotationToken();
}

void Parser::ProcessPragmaMSStruct() {
  assert(Tok.is(tok::annot_pragma_msstruct));
  PragmaMSStructKind Kind = static_cast<PragmaMSStructKind>(
      reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));
  Actions.OnPragmaMSStruct(Kind);
  ConsumeAnnotationToken();
}

void Parser::ProcessPragmaAlign() {
  assert(Tok.is(tok::annot_pragma_align));
  Sema::PragmaOptionsAlignKind Kind = static_cast<Sema::PragmaOptionsAlignKind>(
      reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));
  Actions.OnPragmaOptionsAlign(Kind, Tok.getLocation());
  // Consume the token after processing the pragma to enable pragma-specific
  // #include warnings.
  ConsumeAnnotationToken();
}

void Parser::ProcessPragmaDump() {
  assert(Tok.is(tok::annot_pragma_dump));
  ConsumeAnnotationToken();
  if (Tok.is(tok::eod)) {
    PP.Diag(Tok, diag::warn_pragma_debug_missing_argument) << "dump";
  } else if (NextToken().is(tok::eod)) {
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok, diag::warn_pragma_debug_unexpected_argument);
      ConsumeAnyToken();
      RequireToken(tok::eod);
      return;
    }
    IdentifierInfo *II = Tok.getIdentifierInfo();
    Actions.OnPragmaDump(getCurScope(), Tok.getLocation(), II);
    ConsumeToken();
  } else {
    SourceLocation StartLoc = Tok.getLocation();
    EnterExpressionEvaluationContext Ctx(
        Actions, Sema::ExpressionEvaluationContext::Unevaluated);
    ExprResult E = ParseExpression();
    if (!E.isUsable() || E.get()->containsErrors()) {
      // Diagnostics were emitted during parsing. No action needed.
    } else if (E.get()->getDependence() != ExprDependence::None) {
      PP.Diag(StartLoc, diag::warn_pragma_debug_dependent_argument)
          << E.get()->isTypeDependent()
          << SourceRange(StartLoc, Tok.getLocation());
    } else {
      Actions.OnPragmaDump(E.get());
    }
    SkipUntil(tok::eod, StopBeforeMatch);
  }
  RequireToken(tok::eod);
}

void Parser::ProcessPragmaWeak() {
  assert(Tok.is(tok::annot_pragma_weak));
  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.OnPragmaWeakID(Tok.getIdentifierInfo(), PragmaLoc, Tok.getLocation());
  ConsumeToken(); // The weak name.
}

void Parser::ProcessPragmaWeakAlias() {
  assert(Tok.is(tok::annot_pragma_weakalias));
  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  IdentifierInfo *WeakName = Tok.getIdentifierInfo();
  SourceLocation WeakNameLoc = Tok.getLocation();
  ConsumeToken();
  IdentifierInfo *AliasName = Tok.getIdentifierInfo();
  SourceLocation AliasNameLoc = Tok.getLocation();
  ConsumeToken();
  Actions.OnPragmaWeakAlias(WeakName, AliasName, PragmaLoc, WeakNameLoc,
                            AliasNameLoc);
}

void Parser::ProcessPragmaFPContract() {
  assert(Tok.is(tok::annot_pragma_fp_contract));
  tok::OnOffSwitch OOS = static_cast<tok::OnOffSwitch>(
      reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));

  LangOptions::FPModeKind FPC;
  switch (OOS) {
  case tok::OOS_ON:
    FPC = LangOptions::FPM_On;
    break;
  case tok::OOS_OFF:
    FPC = LangOptions::FPM_Off;
    break;
  case tok::OOS_DEFAULT:
    FPC = getLangOpts().getDefaultFPContractMode();
    break;
  }

  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.OnPragmaFPContract(PragmaLoc, FPC);
}

void Parser::ProcessPragmaFloatControl() {
  assert(Tok.is(tok::annot_pragma_float_control));

  // The value that is held on the PragmaFloatControlStack encodes
  // the PragmaFloatControl kind and the MSStackAction kind
  // into a single 32-bit word. The MsStackAction is the high 16 bits
  // and the FloatControl is the lower 16 bits. Use shift and bit-and
  // to decode the parts.
  uintptr_t Value = reinterpret_cast<uintptr_t>(Tok.getAnnotationValue());
  Sema::PragmaMsStackAction Action =
      static_cast<Sema::PragmaMsStackAction>((Value >> 16) & 0xFFFF);
  PragmaFloatControlKind Kind = PragmaFloatControlKind(Value & 0xFFFF);
  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.OnPragmaFloatControl(PragmaLoc, Action, Kind);
}

void Parser::ProcessPragmaFEnvAccess() {
  assert(Tok.is(tok::annot_pragma_fenv_access) ||
         Tok.is(tok::annot_pragma_fenv_access_ms));
  tok::OnOffSwitch OOS = static_cast<tok::OnOffSwitch>(
      reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));

  bool IsEnabled;
  switch (OOS) {
  case tok::OOS_ON:
    IsEnabled = true;
    break;
  case tok::OOS_OFF:
    IsEnabled = false;
    break;
  case tok::OOS_DEFAULT:
    IsEnabled = false;
    break;
  }

  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.OnPragmaFEnvAccess(PragmaLoc, IsEnabled);
}

void Parser::ProcessPragmaFEnvRound() {
  assert(Tok.is(tok::annot_pragma_fenv_round));
  auto RM = static_cast<llvm::RoundingMode>(
      reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));

  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.OnPragmaFEnvRound(PragmaLoc, RM);
}

void Parser::ProcessPragmaCXLimitedRange() {
  assert(Tok.is(tok::annot_pragma_cx_limited_range));
  tok::OnOffSwitch OOS = static_cast<tok::OnOffSwitch>(
      reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));

  LangOptions::ComplexRangeKind Range;
  switch (OOS) {
  case tok::OOS_ON:
    Range = LangOptions::CX_Limited;
    break;
  case tok::OOS_OFF:
    Range = LangOptions::CX_Full;
    break;
  case tok::OOS_DEFAULT:
    // According to ISO C99 standard chapter 7.3.4, the default value
    // for the pragma is ``off'. -fcx-limited-range and -fcx-improved-range
    // control the default value of these pragmas.
    Range = getLangOpts().getComplexRange();
    break;
  }

  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.OnPragmaCXLimitedRange(PragmaLoc, Range);
}

void Parser::ProcessPragmaMSPragma() {
  assert(Tok.is(tok::annot_pragma_ms_pragma));
  // Grab the tokens out of the annotation and enter them into the stream.
  auto TheTokens =
      (std::pair<std::unique_ptr<Token[]>, size_t> *)Tok.getAnnotationValue();
  PP.PushTokenStream(std::move(TheTokens->first), TheTokens->second, true,
                     /*IsReinject=*/true);
  SourceLocation PragmaLocation = ConsumeAnnotationToken();
  assert(Tok.isAnyIdentifier());
  llvm::StringRef PragmaName = Tok.getIdentifierInfo()->getName();
  PP.Lex(Tok); // pragma kind

  // Figure out which #pragma we're dealing with.  The switch has no default
  // because lex shouldn't emit the annotation token for unrecognized pragmas.
  typedef bool (Parser::*PragmaHandler)(llvm::StringRef, SourceLocation);
  PragmaHandler Handler =
      llvm::StringSwitch<PragmaHandler>(PragmaName)
          .Case("data_seg", &Parser::ProcessPragmaMSSegment)
          .Case("bss_seg", &Parser::ProcessPragmaMSSegment)
          .Case("const_seg", &Parser::ProcessPragmaMSSegment)
          .Case("code_seg", &Parser::ProcessPragmaMSSegment)
          .Case("section", &Parser::ProcessPragmaMSSection)
          .Case("init_seg", &Parser::ProcessPragmaMSInitSeg)
          .Case("strict_gs_check",
                &Parser::ProcessPragmaMSStrictGuardStackCheck)
          .Case("function", &Parser::ProcessPragmaMSFunction)
          .Case("alloc_text", &Parser::ProcessPragmaMSAllocText)
          .Case("optimize", &Parser::ProcessPragmaMSOptimize);

  if (!(this->*Handler)(PragmaName, PragmaLocation)) {
    // Pragma handling failed, and has been diagnosed.  Slurp up the tokens
    // until eof (really end of line) to prevent follow-on errors.
    while (Tok.isNot(tok::eof))
      PP.Lex(Tok);
    PP.Lex(Tok);
  }
}

bool Parser::ProcessPragmaMSSection(llvm::StringRef PragmaName,
                                    SourceLocation PragmaLocation) {
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_lparen) << PragmaName;
    return false;
  }
  PP.Lex(Tok); // (
  // Parsing code for pragma section
  if (Tok.isNot(tok::string_literal)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_section_name)
        << PragmaName;
    return false;
  }
  ExprResult StringResult = ParseStringLiteralExpression();
  if (StringResult.isInvalid())
    return false; // Already diagnosed.
  StringLiteral *SegmentName = cast<StringLiteral>(StringResult.get());
  if (SegmentName->getCharByteWidth() != 1) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_non_wide_string)
        << PragmaName;
    return false;
  }
  int SectionFlags = TreeContext::PSF_Read;
  bool SectionFlagsAreDefault = true;
  while (Tok.is(tok::comma)) {
    PP.Lex(Tok); // ,
    // Ignore "long" and "short".
    // They are undocumented, but widely used, section attributes which appear
    // to do nothing.
    if (Tok.is(tok::kw_long) || Tok.is(tok::kw_short)) {
      PP.Lex(Tok); // long/short
      continue;
    }

    if (!Tok.isAnyIdentifier()) {
      PP.Diag(PragmaLocation, diag::warn_pragma_expected_action_or_r_paren)
          << PragmaName;
      return false;
    }
    TreeContext::PragmaSectionFlag Flag =
        llvm::StringSwitch<TreeContext::PragmaSectionFlag>(
            Tok.getIdentifierInfo()->getName())
            .Case("read", TreeContext::PSF_Read)
            .Case("write", TreeContext::PSF_Write)
            .Case("execute", TreeContext::PSF_Execute)
            .Case("shared", TreeContext::PSF_Invalid)
            .Case("nopage", TreeContext::PSF_Invalid)
            .Case("nocache", TreeContext::PSF_Invalid)
            .Case("discard", TreeContext::PSF_Invalid)
            .Case("remove", TreeContext::PSF_Invalid)
            .Default(TreeContext::PSF_None);
    if (Flag == TreeContext::PSF_None || Flag == TreeContext::PSF_Invalid) {
      PP.Diag(PragmaLocation, Flag == TreeContext::PSF_None
                                  ? diag::warn_pragma_invalid_specific_action
                                  : diag::warn_pragma_unsupported_action)
          << PragmaName << Tok.getIdentifierInfo()->getName();
      return false;
    }
    SectionFlags |= Flag;
    SectionFlagsAreDefault = false;
    PP.Lex(Tok); // Identifier
  }
  // If no section attributes are specified, the section will be marked as
  // read/write.
  if (SectionFlagsAreDefault)
    SectionFlags |= TreeContext::PSF_Write;
  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_rparen) << PragmaName;
    return false;
  }
  PP.Lex(Tok); // )
  if (Tok.isNot(tok::eof)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_extra_tokens_at_eol)
        << PragmaName;
    return false;
  }
  PP.Lex(Tok); // eof
  Actions.OnPragmaMSSection(PragmaLocation, SectionFlags, SegmentName);
  return true;
}

bool Parser::ProcessPragmaMSSegment(llvm::StringRef PragmaName,
                                    SourceLocation PragmaLocation) {
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_lparen) << PragmaName;
    return false;
  }
  PP.Lex(Tok); // (
  Sema::PragmaMsStackAction Action = Sema::PSK_Reset;
  llvm::StringRef SlotLabel;
  if (Tok.isAnyIdentifier()) {
    llvm::StringRef PushPop = Tok.getIdentifierInfo()->getName();
    if (PushPop == "push")
      Action = Sema::PSK_Push;
    else if (PushPop == "pop")
      Action = Sema::PSK_Pop;
    else {
      PP.Diag(PragmaLocation,
              diag::warn_pragma_expected_section_push_pop_or_name)
          << PragmaName;
      return false;
    }
    if (Action != Sema::PSK_Reset) {
      PP.Lex(Tok); // push | pop
      if (Tok.is(tok::comma)) {
        PP.Lex(Tok); // ,
        // If we've got a comma, we either need a label or a string.
        if (Tok.isAnyIdentifier()) {
          SlotLabel = Tok.getIdentifierInfo()->getName();
          PP.Lex(Tok); // identifier
          if (Tok.is(tok::comma))
            PP.Lex(Tok);
          else if (Tok.isNot(tok::r_paren)) {
            PP.Diag(PragmaLocation, diag::warn_pragma_expected_punc)
                << PragmaName;
            return false;
          }
        }
      } else if (Tok.isNot(tok::r_paren)) {
        PP.Diag(PragmaLocation, diag::warn_pragma_expected_punc) << PragmaName;
        return false;
      }
    }
  }
  // Grab the string literal for our section name.
  StringLiteral *SegmentName = nullptr;
  if (Tok.isNot(tok::r_paren)) {
    if (Tok.isNot(tok::string_literal)) {
      unsigned DiagID =
          Action != Sema::PSK_Reset
              ? !SlotLabel.empty()
                    ? diag::warn_pragma_expected_section_name
                    : diag::warn_pragma_expected_section_label_or_name
              : diag::warn_pragma_expected_section_push_pop_or_name;
      PP.Diag(PragmaLocation, DiagID) << PragmaName;
      return false;
    }
    ExprResult StringResult = ParseStringLiteralExpression();
    if (StringResult.isInvalid())
      return false; // Already diagnosed.
    SegmentName = cast<StringLiteral>(StringResult.get());
    if (SegmentName->getCharByteWidth() != 1) {
      PP.Diag(PragmaLocation, diag::warn_pragma_expected_non_wide_string)
          << PragmaName;
      return false;
    }
    // Setting section "" has no effect
    if (SegmentName->getLength())
      Action = (Sema::PragmaMsStackAction)(Action | Sema::PSK_Set);
  }
  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_rparen) << PragmaName;
    return false;
  }
  PP.Lex(Tok); // )
  if (Tok.isNot(tok::eof)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_extra_tokens_at_eol)
        << PragmaName;
    return false;
  }
  PP.Lex(Tok); // eof
  Actions.OnPragmaMSSeg(PragmaLocation, Action, SlotLabel, SegmentName,
                        PragmaName);
  return true;
}

// #pragma init_seg({ compiler | lib | user | "section-name" [, func-name]} )
bool Parser::ProcessPragmaMSInitSeg(llvm::StringRef PragmaName,
                                    SourceLocation PragmaLocation) {
  if (getTargetInfo().getTriple().getEnvironment() != llvm::Triple::MSVC) {
    PP.Diag(PragmaLocation, diag::warn_pragma_init_seg_unsupported_target);
    return false;
  }

  if (RequireToken(tok::l_paren, diag::warn_pragma_expected_lparen, PragmaName))
    return false;

  // Parse either the known section names or the string section name.
  StringLiteral *SegmentName = nullptr;
  if (Tok.isAnyIdentifier()) {
    auto *II = Tok.getIdentifierInfo();
    llvm::StringRef Section = llvm::StringSwitch<llvm::StringRef>(II->getName())
                                  .Case("compiler", "\".CRT$XCC\"")
                                  .Case("lib", "\".CRT$XCL\"")
                                  .Case("user", "\".CRT$XCU\"")
                                  .Default("");

    if (!Section.empty()) {
      // Pretend the user wrote the appropriate string literal here.
      Token Toks[1];
      Toks[0].startToken();
      Toks[0].setKind(tok::string_literal);
      Toks[0].setLocation(Tok.getLocation());
      Toks[0].setLiteralData(Section.data());
      Toks[0].setLength(Section.size());
      SegmentName = cast<StringLiteral>(Actions.OnStringLiteral(Toks).get());
      PP.Lex(Tok);
    }
  } else if (Tok.is(tok::string_literal)) {
    ExprResult StringResult = ParseStringLiteralExpression();
    if (StringResult.isInvalid())
      return false;
    SegmentName = cast<StringLiteral>(StringResult.get());
    if (SegmentName->getCharByteWidth() != 1) {
      PP.Diag(PragmaLocation, diag::warn_pragma_expected_non_wide_string)
          << PragmaName;
      return false;
    }
  }

  if (!SegmentName) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_init_seg) << PragmaName;
    return false;
  }

  if (Tok.is(tok::comma)) {
    PP.Lex(Tok);
    if (!Tok.isAnyIdentifier()) {
      PP.Diag(PragmaLocation, diag::warn_pragma_expected_identifier)
          << PragmaName;
      return false;
    }
    PP.Lex(Tok);
  }

  if (RequireToken(tok::r_paren, diag::warn_pragma_expected_rparen,
                   PragmaName) ||
      RequireToken(tok::eof, diag::warn_pragma_extra_tokens_at_eol, PragmaName))
    return false;

  Actions.OnPragmaMSInitSeg(PragmaLocation, SegmentName);
  return true;
}

// #pragma strict_gs_check(pop)
// #pragma strict_gs_check(push, "on" | "off")
// #pragma strict_gs_check("on" | "off")
bool Parser::ProcessPragmaMSStrictGuardStackCheck(
    llvm::StringRef PragmaName, SourceLocation PragmaLocation) {
  if (RequireToken(tok::l_paren, diag::warn_pragma_expected_lparen, PragmaName))
    return false;

  Sema::PragmaMsStackAction Action = Sema::PSK_Set;
  if (Tok.is(tok::identifier)) {
    llvm::StringRef PushPop = Tok.getIdentifierInfo()->getName();
    if (PushPop == "push") {
      PP.Lex(Tok);
      Action = Sema::PSK_Push;
      if (RequireToken(tok::comma, diag::warn_pragma_expected_punc, PragmaName))
        return false;
    } else if (PushPop == "pop") {
      PP.Lex(Tok);
      Action = Sema::PSK_Pop;
    }
  }

  bool Value = false;
  if (Action & Sema::PSK_Push || Action & Sema::PSK_Set) {
    const IdentifierInfo *II = Tok.getIdentifierInfo();
    if (II && II->isStr("off")) {
      PP.Lex(Tok);
      Value = false;
    } else if (II && II->isStr("on")) {
      PP.Lex(Tok);
      Value = true;
    } else {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_invalid_action)
          << PragmaName;
      return false;
    }
  }

  // Finish the pragma: ')' $
  if (RequireToken(tok::r_paren, diag::warn_pragma_expected_rparen, PragmaName))
    return false;

  if (RequireToken(tok::eof, diag::warn_pragma_extra_tokens_at_eol, PragmaName))
    return false;

  Actions.OnPragmaMSStrictGuardStackCheck(PragmaLocation, Action, Value);
  return true;
}

bool Parser::ProcessPragmaMSAllocText(llvm::StringRef PragmaName,
                                      SourceLocation PragmaLocation) {
  Token FirstTok = Tok;
  if (RequireToken(tok::l_paren, diag::warn_pragma_expected_lparen, PragmaName))
    return false;

  llvm::StringRef Section;
  if (Tok.is(tok::string_literal)) {
    ExprResult StringResult = ParseStringLiteralExpression();
    if (StringResult.isInvalid())
      return false; // Already diagnosed.
    StringLiteral *SegmentName = cast<StringLiteral>(StringResult.get());
    if (SegmentName->getCharByteWidth() != 1) {
      PP.Diag(PragmaLocation, diag::warn_pragma_expected_non_wide_string)
          << PragmaName;
      return false;
    }
    Section = SegmentName->getString();
  } else if (Tok.is(tok::identifier)) {
    Section = Tok.getIdentifierInfo()->getName();
    PP.Lex(Tok);
  } else {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_section_name)
        << PragmaName;
    return false;
  }

  if (RequireToken(tok::comma, diag::warn_pragma_expected_comma, PragmaName))
    return false;

  llvm::SmallVector<std::tuple<IdentifierInfo *, SourceLocation>> Functions;
  while (true) {
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_identifier)
          << PragmaName;
      return false;
    }

    IdentifierInfo *II = Tok.getIdentifierInfo();
    Functions.emplace_back(II, Tok.getLocation());

    PP.Lex(Tok);
    if (Tok.isNot(tok::comma))
      break;
    PP.Lex(Tok);
  }

  if (RequireToken(tok::r_paren, diag::warn_pragma_expected_rparen,
                   PragmaName) ||
      RequireToken(tok::eof, diag::warn_pragma_extra_tokens_at_eol, PragmaName))
    return false;

  Actions.OnPragmaMSAllocText(FirstTok.getLocation(), Section, Functions);
  return true;
}

namespace {
struct PragmaAttributeInfo {
  enum ActionType { Push, Pop, Attribute };
  ParsedAttributes &Attributes;
  ActionType Action;
  const IdentifierInfo *Namespace = nullptr;
  llvm::ArrayRef<Token> Tokens;

  PragmaAttributeInfo(ParsedAttributes &Attributes) : Attributes(Attributes) {}
};

#include "neverc/Syntax/AttrSubMatchRulesParserStringSwitches.td.h"

} // end anonymous namespace

// ===----------------------------------------------------------------------===
// Attribute subject matching
// ===----------------------------------------------------------------------===

namespace {

llvm::StringRef extractIdentName(const Token &Tok) {
  if (Tok.is(tok::identifier))
    return Tok.getIdentifierInfo()->getName();
  const char *S = tok::getKeywordSpelling(Tok.getKind());
  if (!S)
    return "";
  return S;
}

bool isAbstractAttrMatcherRule(attr::SubjectMatchRule Rule) {
  using namespace attr;
  switch (Rule) {
#define ATTR_MATCH_RULE(Value, Spelling, IsAbstract)                           \
  case Value:                                                                  \
    return IsAbstract;
#include "neverc/Foundation/AttrSubMatchRulesList.td.h"
  }
  return false;
}

void diagnoseExpectedAttributeSubjectSubRule(Parser &PRef,
                                             attr::SubjectMatchRule PrimaryRule,
                                             llvm::StringRef PrimaryRuleName,
                                             SourceLocation SubRuleLoc) {
  auto Diagnostic =
      PRef.Diag(SubRuleLoc,
                diag::err_pragma_attribute_expected_subject_sub_identifier)
      << PrimaryRuleName;
  if (const char *SubRules = validAttributeSubjectMatchSubRules(PrimaryRule))
    Diagnostic << /*SubRulesSupported=*/1 << SubRules;
  else
    Diagnostic << /*SubRulesSupported=*/0;
}

void diagnoseUnknownAttributeSubjectSubRule(Parser &PRef,
                                            attr::SubjectMatchRule PrimaryRule,
                                            llvm::StringRef PrimaryRuleName,
                                            llvm::StringRef SubRuleName,
                                            SourceLocation SubRuleLoc) {

  auto Diagnostic =
      PRef.Diag(SubRuleLoc, diag::err_pragma_attribute_unknown_subject_sub_rule)
      << SubRuleName << PrimaryRuleName;
  if (const char *SubRules = validAttributeSubjectMatchSubRules(PrimaryRule))
    Diagnostic << /*SubRulesSupported=*/1 << SubRules;
  else
    Diagnostic << /*SubRulesSupported=*/0;
}

} // namespace

bool Parser::ParsePragmaAttributeSubjectMatchRuleSet(
    attr::ParsedSubjectMatchRuleSet &SubjectMatchRules, SourceLocation &AnyLoc,
    SourceLocation &LastMatchRuleEndLoc) {
  bool IsAny = false;
  BalancedDelimiterTracker AnyParens(*this, tok::l_paren);
  if (extractIdentName(Tok) == "any") {
    AnyLoc = ConsumeToken();
    IsAny = true;
    if (AnyParens.expectAndConsume())
      return true;
  }

  do {
    // Parse the subject matcher rule.
    llvm::StringRef Name = extractIdentName(Tok);
    if (Name.empty()) {
      Diag(Tok, diag::err_pragma_attribute_expected_subject_identifier);
      return true;
    }
    std::pair<std::optional<attr::SubjectMatchRule>,
              std::optional<attr::SubjectMatchRule> (*)(llvm::StringRef, bool)>
        Rule = isAttributeSubjectMatchRule(Name);
    if (!Rule.first) {
      Diag(Tok, diag::err_pragma_attribute_unknown_subject_rule) << Name;
      return true;
    }
    attr::SubjectMatchRule PrimaryRule = *Rule.first;
    SourceLocation RuleLoc = ConsumeToken();

    BalancedDelimiterTracker Parens(*this, tok::l_paren);
    if (isAbstractAttrMatcherRule(PrimaryRule)) {
      if (Parens.expectAndConsume())
        return true;
    } else if (Parens.consumeOpen()) {
      if (!SubjectMatchRules
               .insert(
                   std::make_pair(PrimaryRule, SourceRange(RuleLoc, RuleLoc)))
               .second)
        Diag(RuleLoc, diag::err_pragma_attribute_duplicate_subject)
            << Name
            << FixItHint::CreateRemoval(SourceRange(
                   RuleLoc, Tok.is(tok::comma) ? Tok.getLocation() : RuleLoc));
      LastMatchRuleEndLoc = RuleLoc;
      continue;
    }

    // Parse the sub-rules.
    llvm::StringRef SubRuleName = extractIdentName(Tok);
    if (SubRuleName.empty()) {
      diagnoseExpectedAttributeSubjectSubRule(*this, PrimaryRule, Name,
                                              Tok.getLocation());
      return true;
    }
    attr::SubjectMatchRule SubRule;
    if (SubRuleName == "unless") {
      SourceLocation SubRuleLoc = ConsumeToken();
      BalancedDelimiterTracker Parens(*this, tok::l_paren);
      if (Parens.expectAndConsume())
        return true;
      SubRuleName = extractIdentName(Tok);
      if (SubRuleName.empty()) {
        diagnoseExpectedAttributeSubjectSubRule(*this, PrimaryRule, Name,
                                                SubRuleLoc);
        return true;
      }
      auto SubRuleOrNone = Rule.second(SubRuleName, /*IsUnless=*/true);
      if (!SubRuleOrNone) {
        std::string SubRuleUnlessName = "unless(" + SubRuleName.str() + ")";
        diagnoseUnknownAttributeSubjectSubRule(*this, PrimaryRule, Name,
                                               SubRuleUnlessName, SubRuleLoc);
        return true;
      }
      SubRule = *SubRuleOrNone;
      ConsumeToken();
      if (Parens.consumeClose())
        return true;
    } else {
      auto SubRuleOrNone = Rule.second(SubRuleName, /*IsUnless=*/false);
      if (!SubRuleOrNone) {
        diagnoseUnknownAttributeSubjectSubRule(*this, PrimaryRule, Name,
                                               SubRuleName, Tok.getLocation());
        return true;
      }
      SubRule = *SubRuleOrNone;
      ConsumeToken();
    }
    SourceLocation RuleEndLoc = Tok.getLocation();
    LastMatchRuleEndLoc = RuleEndLoc;
    if (Parens.consumeClose())
      return true;
    if (!SubjectMatchRules
             .insert(std::make_pair(SubRule, SourceRange(RuleLoc, RuleEndLoc)))
             .second) {
      Diag(RuleLoc, diag::err_pragma_attribute_duplicate_subject)
          << attr::getSubjectMatchRuleSpelling(SubRule)
          << FixItHint::CreateRemoval(SourceRange(
                 RuleLoc, Tok.is(tok::comma) ? Tok.getLocation() : RuleEndLoc));
      continue;
    }
  } while (IsAny && TryConsumeToken(tok::comma));

  if (IsAny)
    if (AnyParens.consumeClose())
      return true;

  return false;
}

namespace {

enum class MissingAttributeSubjectRulesRecoveryPoint {
  Comma,
  ApplyTo,
  Equals,
  Any,
  None,
};

MissingAttributeSubjectRulesRecoveryPoint
getAttributeSubjectRulesRecoveryPointForToken(const Token &Tok) {
  if (const auto *II = Tok.getIdentifierInfo()) {
    if (II->isStr("apply_to"))
      return MissingAttributeSubjectRulesRecoveryPoint::ApplyTo;
    if (II->isStr("any"))
      return MissingAttributeSubjectRulesRecoveryPoint::Any;
  }
  if (Tok.is(tok::equal))
    return MissingAttributeSubjectRulesRecoveryPoint::Equals;
  return MissingAttributeSubjectRulesRecoveryPoint::None;
}

DiagnosticBuilder createExpectedAttributeSubjectRulesTokenDiagnostic(
    unsigned DiagID, ParsedAttributes &Attrs,
    MissingAttributeSubjectRulesRecoveryPoint Point, Parser &PRef) {
  SourceLocation Loc = PRef.getEndOfPreviousToken();
  if (Loc.isInvalid())
    Loc = PRef.getCurToken().getLocation();
  auto Diagnostic = PRef.Diag(Loc, DiagID);
  std::string FixIt;
  MissingAttributeSubjectRulesRecoveryPoint EndPoint =
      getAttributeSubjectRulesRecoveryPointForToken(PRef.getCurToken());
  if (Point == MissingAttributeSubjectRulesRecoveryPoint::Comma)
    FixIt = ", ";
  if (Point <= MissingAttributeSubjectRulesRecoveryPoint::ApplyTo &&
      EndPoint > MissingAttributeSubjectRulesRecoveryPoint::ApplyTo)
    FixIt += "apply_to";
  if (Point <= MissingAttributeSubjectRulesRecoveryPoint::Equals &&
      EndPoint > MissingAttributeSubjectRulesRecoveryPoint::Equals)
    FixIt += " = ";
  SourceRange FixItRange(Loc);
  if (EndPoint == MissingAttributeSubjectRulesRecoveryPoint::None) {
    // Gather the subject match rules that are supported by the attribute.
    // Add all the possible rules initially.
    llvm::BitVector IsMatchRuleAvailable(attr::SubjectMatchRule_Last + 1, true);
    // Remove the ones that are not supported by any of the attributes.
    for (const ParsedAttr &Attribute : Attrs) {
      llvm::SmallVector<std::pair<attr::SubjectMatchRule, bool>, 4> MatchRules;
      Attribute.getMatchRules(PRef.getLangOpts(), MatchRules);
      llvm::BitVector IsSupported(attr::SubjectMatchRule_Last + 1);
      for (const auto &Rule : MatchRules) {
        // Ensure that the missing rule is reported in the fix-it only when it's
        // supported in the current language mode.
        if (!Rule.second)
          continue;
        IsSupported[Rule.first] = true;
      }
      IsMatchRuleAvailable &= IsSupported;
    }
    if (IsMatchRuleAvailable.count() == 0) {
      return Diagnostic;
    }
    FixIt += "any(";
    bool NeedsComma = false;
    for (unsigned I = 0; I <= attr::SubjectMatchRule_Last; I++) {
      if (!IsMatchRuleAvailable[I])
        continue;
      if (NeedsComma)
        FixIt += ", ";
      else
        NeedsComma = true;
      FixIt += attr::getSubjectMatchRuleSpelling(
          static_cast<attr::SubjectMatchRule>(I));
    }
    FixIt += ")";
    // Check if we need to remove the range
    PRef.SkipUntil(tok::eof, Parser::StopBeforeMatch);
    FixItRange.setEnd(PRef.getCurToken().getLocation());
  }
  if (FixItRange.getBegin() == FixItRange.getEnd())
    Diagnostic << FixItHint::CreateInsertion(FixItRange.getBegin(), FixIt);
  else
    Diagnostic << FixItHint::CreateReplacement(
        CharSourceRange::getCharRange(FixItRange), FixIt);
  return Diagnostic;
}

} // end anonymous namespace

void Parser::ProcessPragmaAttribute() {
  assert(Tok.is(tok::annot_pragma_attribute) &&
         "Expected #pragma attribute annotation token");
  SourceLocation PragmaLoc = Tok.getLocation();
  auto *Info = static_cast<PragmaAttributeInfo *>(Tok.getAnnotationValue());
  if (Info->Action == PragmaAttributeInfo::Pop) {
    ConsumeAnnotationToken();
    Actions.OnPragmaAttributePop(PragmaLoc, Info->Namespace);
    return;
  }
  // Parse the actual attribute with its arguments.
  assert((Info->Action == PragmaAttributeInfo::Push ||
          Info->Action == PragmaAttributeInfo::Attribute) &&
         "Unexpected #pragma attribute command");

  if (Info->Action == PragmaAttributeInfo::Push && Info->Tokens.empty()) {
    ConsumeAnnotationToken();
    Actions.OnPragmaAttributeEmptyPush(PragmaLoc, Info->Namespace);
    return;
  }

  PP.PushTokenStream(Info->Tokens, /*DisableMacroExpansion=*/false,
                     /*IsReinject=*/false);
  ConsumeAnnotationToken();

  ParsedAttributes &Attrs = Info->Attributes;
  Attrs.clearListOnly();

  auto SkipToEnd = [this]() {
    SkipUntil(tok::eof, StopBeforeMatch);
    ConsumeToken();
  };

  if ((Tok.is(tok::l_square) && NextToken().is(tok::l_square)) ||
      Tok.isRegularKeywordAttribute()) {
    // Parse the [[...]] (bracket) attribute.
    ParseBracketAttributeSpecifier(Attrs);
  } else if (Tok.is(tok::kw___attribute)) {
    ConsumeToken();
    if (RequireToken(tok::l_paren, diag::err_expected_lparen_after,
                     "attribute"))
      return SkipToEnd();
    if (RequireToken(tok::l_paren, diag::err_expected_lparen_after, "("))
      return SkipToEnd();

    // Parse the comma-separated list of attributes.
    do {
      if (Tok.isNot(tok::identifier)) {
        Diag(Tok, diag::err_pragma_attribute_expected_attribute_name);
        SkipToEnd();
        return;
      }
      IdentifierInfo *AttrName = Tok.getIdentifierInfo();
      SourceLocation AttrNameLoc = ConsumeToken();

      if (Tok.isNot(tok::l_paren))
        Attrs.addNew(AttrName, AttrNameLoc, nullptr, AttrNameLoc, nullptr, 0,
                     ParsedAttr::Form::GNU());
      else
        ParseGNUAttributeArgs(AttrName, AttrNameLoc, Attrs, /*EndLoc=*/nullptr,
                              /*ScopeName=*/nullptr,
                              /*ScopeLoc=*/SourceLocation(),
                              ParsedAttr::Form::GNU(),
                              /*Declarator=*/nullptr);
    } while (TryConsumeToken(tok::comma));

    if (RequireToken(tok::r_paren))
      return SkipToEnd();
    if (RequireToken(tok::r_paren))
      return SkipToEnd();
  } else if (Tok.is(tok::kw___declspec)) {
    ParseMicrosoftDeclSpecs(Attrs);
  } else {
    Diag(Tok, diag::err_pragma_attribute_expected_attribute_syntax);
    if (Tok.getIdentifierInfo()) {
      // If we suspect that this is an attribute suggest the use of
      // '__attribute__'.
      if (ParsedAttr::getParsedKind(
              Tok.getIdentifierInfo(), /*ScopeName=*/nullptr,
              ParsedAttr::AS_GNU) != ParsedAttr::UnknownAttribute) {
        SourceLocation InsertStartLoc = Tok.getLocation();
        ConsumeToken();
        if (Tok.is(tok::l_paren)) {
          ConsumeAnyToken();
          SkipUntil(tok::r_paren, StopBeforeMatch);
          if (Tok.isNot(tok::r_paren))
            return SkipToEnd();
        }
        Diag(Tok, diag::note_pragma_attribute_use_attribute_kw)
            << FixItHint::CreateInsertion(InsertStartLoc, "__attribute__((")
            << FixItHint::CreateInsertion(Tok.getEndLoc(), "))");
      }
    }
    SkipToEnd();
    return;
  }

  if (Attrs.empty() || Attrs.begin()->isInvalid()) {
    SkipToEnd();
    return;
  }

  for (const ParsedAttr &Attribute : Attrs) {
    if (!Attribute.isSupportedByPragmaAttribute()) {
      Diag(PragmaLoc, diag::err_pragma_attribute_unsupported_attribute)
          << Attribute;
      SkipToEnd();
      return;
    }
  }

  // Parse the subject-list.
  if (!TryConsumeToken(tok::comma)) {
    createExpectedAttributeSubjectRulesTokenDiagnostic(
        diag::err_expected, Attrs,
        MissingAttributeSubjectRulesRecoveryPoint::Comma, *this)
        << tok::comma;
    SkipToEnd();
    return;
  }

  if (Tok.isNot(tok::identifier)) {
    createExpectedAttributeSubjectRulesTokenDiagnostic(
        diag::err_pragma_attribute_invalid_subject_set_specifier, Attrs,
        MissingAttributeSubjectRulesRecoveryPoint::ApplyTo, *this);
    SkipToEnd();
    return;
  }
  const IdentifierInfo *II = Tok.getIdentifierInfo();
  if (!II->isStr("apply_to")) {
    createExpectedAttributeSubjectRulesTokenDiagnostic(
        diag::err_pragma_attribute_invalid_subject_set_specifier, Attrs,
        MissingAttributeSubjectRulesRecoveryPoint::ApplyTo, *this);
    SkipToEnd();
    return;
  }
  ConsumeToken();

  if (!TryConsumeToken(tok::equal)) {
    createExpectedAttributeSubjectRulesTokenDiagnostic(
        diag::err_expected, Attrs,
        MissingAttributeSubjectRulesRecoveryPoint::Equals, *this)
        << tok::equal;
    SkipToEnd();
    return;
  }

  attr::ParsedSubjectMatchRuleSet SubjectMatchRules;
  SourceLocation AnyLoc, LastMatchRuleEndLoc;
  if (ParsePragmaAttributeSubjectMatchRuleSet(SubjectMatchRules, AnyLoc,
                                              LastMatchRuleEndLoc)) {
    SkipToEnd();
    return;
  }

  // Tokens following an ill-formed attribute will remain in the token stream
  // and must be removed.
  if (Tok.isNot(tok::eof)) {
    Diag(Tok, diag::err_pragma_attribute_extra_tokens_after_attribute);
    SkipToEnd();
    return;
  }

  // Consume the eof terminator token.
  ConsumeToken();

  // Handle a mixed push/attribute by desurging to a push, then an attribute.
  if (Info->Action == PragmaAttributeInfo::Push)
    Actions.OnPragmaAttributeEmptyPush(PragmaLoc, Info->Namespace);

  for (ParsedAttr &Attribute : Attrs) {
    Actions.OnPragmaAttributeAttribute(Attribute, PragmaLoc, SubjectMatchRules);
  }
}

// ===----------------------------------------------------------------------===
// Preprocessor-level pragma handlers
// ===----------------------------------------------------------------------===

// #pragma GCC visibility comes in two variants:
//   'push' '(' [visibility] ')'
//   'pop'
void PragmaGCCVisibilityHandler::ProcessPragma(PrepEngine &PP,
                                               PragmaIntroducer Introducer,
                                               Token &VisTok) {
  SourceLocation VisLoc = VisTok.getLocation();

  Token Tok;
  PP.LexWithoutExpansion(Tok);

  const IdentifierInfo *PushPop = Tok.getIdentifierInfo();

  const IdentifierInfo *VisType;
  if (PushPop && PushPop->isStr("pop")) {
    VisType = nullptr;
  } else if (PushPop && PushPop->isStr("push")) {
    PP.LexWithoutExpansion(Tok);
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_lparen)
          << "visibility";
      return;
    }
    PP.LexWithoutExpansion(Tok);
    VisType = Tok.getIdentifierInfo();
    if (!VisType) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_identifier)
          << "visibility";
      return;
    }
    PP.LexWithoutExpansion(Tok);
    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_rparen)
          << "visibility";
      return;
    }
  } else {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_identifier)
        << "visibility";
    return;
  }
  SourceLocation EndLoc = Tok.getLocation();
  PP.LexWithoutExpansion(Tok);
  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "visibility";
    return;
  }

  auto Toks = std::make_unique<Token[]>(1);
  Toks[0].startToken();
  Toks[0].setKind(tok::annot_pragma_vis);
  Toks[0].setLocation(VisLoc);
  Toks[0].setAnnotationEndLoc(EndLoc);
  Toks[0].setAnnotationValue(
      const_cast<void *>(static_cast<const void *>(VisType)));
  PP.PushTokenStream(std::move(Toks), 1, /*DisableMacroExpansion=*/true,
                     /*IsReinject=*/false);
}

// #pragma pack(...) comes in the following delicious flavors:
//   pack '(' [integer] ')'
//   pack '(' 'show' ')'
//   pack '(' ('push' | 'pop') [',' identifier] [, integer] ')'
void PragmaPackHandler::ProcessPragma(PrepEngine &PP,
                                      PragmaIntroducer Introducer,
                                      Token &PackTok) {
  SourceLocation PackLoc = PackTok.getLocation();

  Token Tok;
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_lparen) << "pack";
    return;
  }

  Sema::PragmaMsStackAction Action = Sema::PSK_Reset;
  llvm::StringRef SlotLabel;
  Token Alignment;
  Alignment.startToken();
  PP.Lex(Tok);
  if (Tok.is(tok::numeric_constant)) {
    Alignment = Tok;

    PP.Lex(Tok);

    // In MSVC/gcc, #pragma pack(4) sets the alignment without affecting
    // the push/pop stack.
    Action = Sema::PSK_Set;
  } else if (Tok.is(tok::identifier)) {
    const IdentifierInfo *II = Tok.getIdentifierInfo();
    if (II->isStr("show")) {
      Action = Sema::PSK_Show;
      PP.Lex(Tok);
    } else {
      if (II->isStr("push")) {
        Action = Sema::PSK_Push;
      } else if (II->isStr("pop")) {
        Action = Sema::PSK_Pop;
      } else {
        PP.Diag(Tok.getLocation(), diag::warn_pragma_invalid_action) << "pack";
        return;
      }
      PP.Lex(Tok);

      if (Tok.is(tok::comma)) {
        PP.Lex(Tok);

        if (Tok.is(tok::numeric_constant)) {
          Action = (Sema::PragmaMsStackAction)(Action | Sema::PSK_Set);
          Alignment = Tok;

          PP.Lex(Tok);
        } else if (Tok.is(tok::identifier)) {
          SlotLabel = Tok.getIdentifierInfo()->getName();
          PP.Lex(Tok);

          if (Tok.is(tok::comma)) {
            PP.Lex(Tok);

            if (Tok.isNot(tok::numeric_constant)) {
              PP.Diag(Tok.getLocation(), diag::warn_pragma_pack_malformed);
              return;
            }

            Action = (Sema::PragmaMsStackAction)(Action | Sema::PSK_Set);
            Alignment = Tok;

            PP.Lex(Tok);
          }
        } else {
          PP.Diag(Tok.getLocation(), diag::warn_pragma_pack_malformed);
          return;
        }
      }
    }
  }

  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_rparen) << "pack";
    return;
  }

  SourceLocation RParenLoc = Tok.getLocation();
  PP.Lex(Tok);
  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol) << "pack";
    return;
  }

  Sema::PragmaPackInfo *Info =
      PP.getPrepEngineAllocator().Allocate<Sema::PragmaPackInfo>(1);
  Info->Action = Action;
  Info->SlotLabel = SlotLabel;
  Info->Alignment = Alignment;

  llvm::MutableArrayRef<Token> Toks(
      PP.getPrepEngineAllocator().Allocate<Token>(1), 1);
  Toks[0].startToken();
  Toks[0].setKind(tok::annot_pragma_pack);
  Toks[0].setLocation(PackLoc);
  Toks[0].setAnnotationEndLoc(RParenLoc);
  Toks[0].setAnnotationValue(static_cast<void *>(Info));
  PP.PushTokenStream(Toks, /*DisableMacroExpansion=*/true,
                     /*IsReinject=*/false);
}

// #pragma ms_struct on
// #pragma ms_struct off
void PragmaMSStructHandler::ProcessPragma(PrepEngine &PP,
                                          PragmaIntroducer Introducer,
                                          Token &MSStructTok) {
  PragmaMSStructKind Kind = PMSST_OFF;

  Token Tok;
  PP.Lex(Tok);
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_ms_struct);
    return;
  }
  SourceLocation EndLoc = Tok.getLocation();
  const IdentifierInfo *II = Tok.getIdentifierInfo();
  if (II->isStr("on")) {
    Kind = PMSST_ON;
    PP.Lex(Tok);
  } else if (II->isStr("off") || II->isStr("reset"))
    PP.Lex(Tok);
  else {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_ms_struct);
    return;
  }

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "ms_struct";
    return;
  }

  llvm::MutableArrayRef<Token> Toks(
      PP.getPrepEngineAllocator().Allocate<Token>(1), 1);
  Toks[0].startToken();
  Toks[0].setKind(tok::annot_pragma_msstruct);
  Toks[0].setLocation(MSStructTok.getLocation());
  Toks[0].setAnnotationEndLoc(EndLoc);
  Toks[0].setAnnotationValue(
      reinterpret_cast<void *>(static_cast<uintptr_t>(Kind)));
  PP.PushTokenStream(Toks, /*DisableMacroExpansion=*/true,
                     /*IsReinject=*/false);
}

// #pragma neverc section bss="abc" data="" rodata="def" text="" relro=""
void PragmaSectionHandler::ProcessPragma(PrepEngine &PP,
                                         PragmaIntroducer Introducer,
                                         Token &FirstToken) {

  Token Tok;
  auto SecKind = Sema::PragmaSectionKind::PCSK_Invalid;

  PP.Lex(Tok); // eat 'section'
  while (Tok.isNot(tok::eod)) {
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_expected_section_kind)
          << "neverc section";
      return;
    }

    const IdentifierInfo *SecType = Tok.getIdentifierInfo();
    if (SecType->isStr("bss"))
      SecKind = Sema::PragmaSectionKind::PCSK_BSS;
    else if (SecType->isStr("data"))
      SecKind = Sema::PragmaSectionKind::PCSK_Data;
    else if (SecType->isStr("rodata"))
      SecKind = Sema::PragmaSectionKind::PCSK_Rodata;
    else if (SecType->isStr("relro"))
      SecKind = Sema::PragmaSectionKind::PCSK_Relro;
    else if (SecType->isStr("text"))
      SecKind = Sema::PragmaSectionKind::PCSK_Text;
    else {
      PP.Diag(Tok.getLocation(), diag::err_pragma_expected_section_kind)
          << "neverc section";
      return;
    }

    SourceLocation PragmaLocation = Tok.getLocation();
    PP.Lex(Tok); // eat ['bss'|'data'|'rodata'|'text']
    if (Tok.isNot(tok::equal)) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_section_expected_equal)
          << SecKind;
      return;
    }

    std::string SecName;
    if (!PP.LexStringContent(Tok, SecName, "pragma neverc section", false))
      return;

    Actions.OnPragmaSection(PragmaLocation,
                            (SecName.size()
                                 ? Sema::PragmaSectionAction::PCSA_Set
                                 : Sema::PragmaSectionAction::PCSA_Clear),
                            SecKind, SecName);
  }
}

// #pragma 'align' '=' {'native','packed','reset'}
// #pragma 'options' 'align' '=' {'native','packed','reset'}
// (natural and power are accepted as aliases for native)
namespace {
void injectAlignPragmaAnnotation(PrepEngine &PP, Token &FirstTok,
                                 bool IsOptions) {
  Token Tok;

  if (IsOptions) {
    PP.Lex(Tok);
    if (Tok.isNot(tok::identifier) ||
        !Tok.getIdentifierInfo()->isStr("align")) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_options_expected_align);
      return;
    }
  }

  PP.Lex(Tok);
  if (Tok.isNot(tok::equal)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_align_expected_equal)
        << IsOptions;
    return;
  }

  PP.Lex(Tok);
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_identifier)
        << (IsOptions ? "options" : "align");
    return;
  }

  Sema::PragmaOptionsAlignKind Kind = Sema::POAK_Native;
  const IdentifierInfo *II = Tok.getIdentifierInfo();
  if (II->isStr("native") || II->isStr("natural") || II->isStr("power"))
    Kind = Sema::POAK_Native;
  else if (II->isStr("packed"))
    Kind = Sema::POAK_Packed;
  else if (II->isStr("reset"))
    Kind = Sema::POAK_Reset;
  else {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_align_invalid_option)
        << IsOptions;
    return;
  }

  SourceLocation EndLoc = Tok.getLocation();
  PP.Lex(Tok);
  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << (IsOptions ? "options" : "align");
    return;
  }

  llvm::MutableArrayRef<Token> Toks(
      PP.getPrepEngineAllocator().Allocate<Token>(1), 1);
  Toks[0].startToken();
  Toks[0].setKind(tok::annot_pragma_align);
  Toks[0].setLocation(FirstTok.getLocation());
  Toks[0].setAnnotationEndLoc(EndLoc);
  Toks[0].setAnnotationValue(
      reinterpret_cast<void *>(static_cast<uintptr_t>(Kind)));
  PP.PushTokenStream(Toks, /*DisableMacroExpansion=*/true,
                     /*IsReinject=*/false);
}
} // namespace

void PragmaAlignHandler::ProcessPragma(PrepEngine &PP,
                                       PragmaIntroducer Introducer,
                                       Token &AlignTok) {
  injectAlignPragmaAnnotation(PP, AlignTok, /*IsOptions=*/false);
}

void PragmaOptionsHandler::ProcessPragma(PrepEngine &PP,
                                         PragmaIntroducer Introducer,
                                         Token &OptionsTok) {
  injectAlignPragmaAnnotation(PP, OptionsTok, /*IsOptions=*/true);
}

// #pragma unused(identifier)
void PragmaUnusedHandler::ProcessPragma(PrepEngine &PP,
                                        PragmaIntroducer Introducer,
                                        Token &UnusedTok) {
  SourceLocation UnusedLoc = UnusedTok.getLocation();

  // Lex the left '('.
  Token Tok;
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_lparen) << "unused";
    return;
  }

  // Lex the declaration reference(s).
  llvm::SmallVector<Token, 5> Identifiers;
  SourceLocation RParenLoc;
  bool LexID = true;

  while (true) {
    PP.Lex(Tok);

    if (LexID) {
      if (Tok.is(tok::identifier)) {
        Identifiers.push_back(Tok);
        LexID = false;
        continue;
      }

      // Illegal token!
      PP.Diag(Tok.getLocation(), diag::warn_pragma_unused_expected_var);
      return;
    }

    // We are execting a ')' or a ','.
    if (Tok.is(tok::comma)) {
      LexID = true;
      continue;
    }

    if (Tok.is(tok::r_paren)) {
      RParenLoc = Tok.getLocation();
      break;
    }

    // Illegal token!
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_punc) << "unused";
    return;
  }

  PP.Lex(Tok);
  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "unused";
    return;
  }

  // Verify that we have a location for the right parenthesis.
  assert(RParenLoc.isValid() && "Valid '#pragma unused' must have ')'");
  assert(!Identifiers.empty() && "Valid '#pragma unused' must have arguments");

  // For each identifier token, insert into the token stream a
  // annot_pragma_unused token followed by the identifier token.
  // This allows us to cache a "#pragma unused" that occurs inside an inline
  // function body.

  llvm::MutableArrayRef<Token> Toks(
      PP.getPrepEngineAllocator().Allocate<Token>(2 * Identifiers.size()),
      2 * Identifiers.size());
  for (unsigned i = 0; i != Identifiers.size(); i++) {
    Token &pragmaUnusedTok = Toks[2 * i], &idTok = Toks[2 * i + 1];
    pragmaUnusedTok.startToken();
    pragmaUnusedTok.setKind(tok::annot_pragma_unused);
    pragmaUnusedTok.setLocation(UnusedLoc);
    idTok = Identifiers[i];
  }
  PP.PushTokenStream(Toks, /*DisableMacroExpansion=*/true,
                     /*IsReinject=*/false);
}

// #pragma weak identifier
// #pragma weak identifier '=' identifier
void PragmaWeakHandler::ProcessPragma(PrepEngine &PP,
                                      PragmaIntroducer Introducer,
                                      Token &WeakTok) {
  SourceLocation WeakLoc = WeakTok.getLocation();

  Token Tok;
  PP.Lex(Tok);
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_identifier) << "weak";
    return;
  }

  Token WeakName = Tok;
  bool HasAlias = false;
  Token AliasName;

  PP.Lex(Tok);
  if (Tok.is(tok::equal)) {
    HasAlias = true;
    PP.Lex(Tok);
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_identifier)
          << "weak";
      return;
    }
    AliasName = Tok;
    PP.Lex(Tok);
  }

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol) << "weak";
    return;
  }

  if (HasAlias) {
    llvm::MutableArrayRef<Token> Toks(
        PP.getPrepEngineAllocator().Allocate<Token>(3), 3);
    Token &pragmaUnusedTok = Toks[0];
    pragmaUnusedTok.startToken();
    pragmaUnusedTok.setKind(tok::annot_pragma_weakalias);
    pragmaUnusedTok.setLocation(WeakLoc);
    pragmaUnusedTok.setAnnotationEndLoc(AliasName.getLocation());
    Toks[1] = WeakName;
    Toks[2] = AliasName;
    PP.PushTokenStream(Toks, /*DisableMacroExpansion=*/true,
                       /*IsReinject=*/false);
  } else {
    llvm::MutableArrayRef<Token> Toks(
        PP.getPrepEngineAllocator().Allocate<Token>(2), 2);
    Token &pragmaUnusedTok = Toks[0];
    pragmaUnusedTok.startToken();
    pragmaUnusedTok.setKind(tok::annot_pragma_weak);
    pragmaUnusedTok.setLocation(WeakLoc);
    pragmaUnusedTok.setAnnotationEndLoc(WeakLoc);
    Toks[1] = WeakName;
    PP.PushTokenStream(Toks, /*DisableMacroExpansion=*/true,
                       /*IsReinject=*/false);
  }
}

void PragmaFPContractHandler::ProcessPragma(PrepEngine &PP,
                                            PragmaIntroducer Introducer,
                                            Token &Tok) {
  tok::OnOffSwitch OOS;
  if (PP.LexOnOffSwitch(OOS))
    return;

  llvm::MutableArrayRef<Token> Toks(
      PP.getPrepEngineAllocator().Allocate<Token>(1), 1);
  Toks[0].startToken();
  Toks[0].setKind(tok::annot_pragma_fp_contract);
  Toks[0].setLocation(Tok.getLocation());
  Toks[0].setAnnotationEndLoc(Tok.getLocation());
  Toks[0].setAnnotationValue(
      reinterpret_cast<void *>(static_cast<uintptr_t>(OOS)));
  PP.PushTokenStream(Toks, /*DisableMacroExpansion=*/true,
                     /*IsReinject=*/false);
}

void PragmaMSPragma::ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                                   Token &Tok) {
  Token EoF, AnnotTok;
  EoF.startToken();
  EoF.setKind(tok::eof);
  AnnotTok.startToken();
  AnnotTok.setKind(tok::annot_pragma_ms_pragma);
  AnnotTok.setLocation(Tok.getLocation());
  AnnotTok.setAnnotationEndLoc(Tok.getLocation());
  llvm::SmallVector<Token, 8> TokenVector;
  // Suck up all of the tokens before the eod.
  for (; Tok.isNot(tok::eod); PP.Lex(Tok)) {
    TokenVector.push_back(Tok);
    AnnotTok.setAnnotationEndLoc(Tok.getLocation());
  }
  // Add a sentinel EoF token to the end of the list.
  TokenVector.push_back(EoF);
  // We must allocate this array with new because PushTokenStream is going to
  // delete it later.
  markAsReinjectedForRelexing(TokenVector);
  auto TokenArray = std::make_unique<Token[]>(TokenVector.size());
  std::copy(TokenVector.begin(), TokenVector.end(), TokenArray.get());
  auto Value = new (PP.getPrepEngineAllocator())
      std::pair<std::unique_ptr<Token[]>, size_t>(std::move(TokenArray),
                                                  TokenVector.size());
  AnnotTok.setAnnotationValue(Value);
  PP.InjectToken(AnnotTok, /*IsReinject*/ false);
}

// 'keyword' can be: precise, except, push, pop
// 'setting' can be: on, off
void PragmaFloatControlHandler::ProcessPragma(PrepEngine &PP,
                                              PragmaIntroducer Introducer,
                                              Token &Tok) {
  Sema::PragmaMsStackAction Action = Sema::PSK_Set;
  SourceLocation FloatControlLoc = Tok.getLocation();
  Token PragmaName = Tok;
  if (!PP.getTargetInfo().hasStrictFP() && !PP.getLangOpts().ExpStrictFP) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_fp_ignored)
        << PragmaName.getIdentifierInfo()->getName();
    return;
  }
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(FloatControlLoc, diag::err_expected) << tok::l_paren;
    return;
  }

  // Read the identifier.
  PP.Lex(Tok);
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
    return;
  }

  // Verify that this is one of the float control options.
  IdentifierInfo *II = Tok.getIdentifierInfo();
  PragmaFloatControlKind Kind =
      llvm::StringSwitch<PragmaFloatControlKind>(II->getName())
          .Case("precise", PFC_Precise)
          .Case("except", PFC_Except)
          .Case("push", PFC_Push)
          .Case("pop", PFC_Pop)
          .Default(PFC_Unknown);
  PP.Lex(Tok); // the identifier
  if (Kind == PFC_Unknown) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
    return;
  } else if (Kind == PFC_Push || Kind == PFC_Pop) {
    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
      return;
    }
    PP.Lex(Tok); // Eat the r_paren
    Action = (Kind == PFC_Pop) ? Sema::PSK_Pop : Sema::PSK_Push;
  } else {
    if (Tok.is(tok::r_paren))
      // Selecting Precise or Except
      PP.Lex(Tok); // the r_paren
    else if (Tok.isNot(tok::comma)) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
      return;
    } else {
      PP.Lex(Tok); // ,
      if (!Tok.isAnyIdentifier()) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
        return;
      }
      llvm::StringRef PushOnOff = Tok.getIdentifierInfo()->getName();
      if (PushOnOff == "on")
        // Kind is set correctly
        ;
      else if (PushOnOff == "off") {
        if (Kind == PFC_Precise)
          Kind = PFC_NoPrecise;
        if (Kind == PFC_Except)
          Kind = PFC_NoExcept;
      } else if (PushOnOff == "push") {
        Action = Sema::PSK_Push_Set;
      } else {
        PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
        return;
      }
      PP.Lex(Tok); // the identifier
      if (Tok.is(tok::comma)) {
        PP.Lex(Tok); // ,
        if (!Tok.isAnyIdentifier()) {
          PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
          return;
        }
        llvm::StringRef ExpectedPush = Tok.getIdentifierInfo()->getName();
        if (ExpectedPush == "push") {
          Action = Sema::PSK_Push_Set;
        } else {
          PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
          return;
        }
        PP.Lex(Tok); // the push identifier
      }
      if (Tok.isNot(tok::r_paren)) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
        return;
      }
      PP.Lex(Tok); // the r_paren
    }
  }
  SourceLocation EndLoc = Tok.getLocation();
  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "float_control";
    return;
  }

  // Note: there is no accomodation for PP callback for this pragma.

  // Enter the annotation.
  auto TokenArray = std::make_unique<Token[]>(1);
  TokenArray[0].startToken();
  TokenArray[0].setKind(tok::annot_pragma_float_control);
  TokenArray[0].setLocation(FloatControlLoc);
  TokenArray[0].setAnnotationEndLoc(EndLoc);
  // Create an encoding of Action and Value by shifting the Action into
  // the high 16 bits then union with the Kind.
  TokenArray[0].setAnnotationValue(reinterpret_cast<void *>(
      static_cast<uintptr_t>((Action << 16) | (Kind & 0xFFFF))));
  PP.PushTokenStream(std::move(TokenArray), 1,
                     /*DisableMacroExpansion=*/false, /*IsReinject=*/false);
}

void PragmaDetectMismatchHandler::ProcessPragma(PrepEngine &PP,
                                                PragmaIntroducer Introducer,
                                                Token &Tok) {
  SourceLocation DetectMismatchLoc = Tok.getLocation();
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(DetectMismatchLoc, diag::err_expected) << tok::l_paren;
    return;
  }

  // Read the name to embed, which must be a string literal.
  std::string NameString;
  if (!PP.LexStringContent(Tok, NameString, "pragma detect_mismatch",
                           /*AllowMacroExpansion=*/true))
    return;

  // Read the comma followed by a second string literal.
  std::string ValueString;
  if (Tok.isNot(tok::comma)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_detect_mismatch_malformed);
    return;
  }

  if (!PP.LexStringContent(Tok, ValueString, "pragma detect_mismatch",
                           /*AllowMacroExpansion=*/true))
    return;

  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(Tok.getLocation(), diag::err_expected) << tok::r_paren;
    return;
  }
  PP.Lex(Tok); // Eat the r_paren.

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_detect_mismatch_malformed);
    return;
  }

  // If the pragma is lexically sound, notify any interested PrepEngineObserver.
  if (PP.getObserver())
    PP.getObserver()->PragmaDetectMismatch(DetectMismatchLoc, NameString,
                                           ValueString);

  Actions.OnPragmaDetectMismatch(DetectMismatchLoc, NameString, ValueString);
}

void PragmaCommentHandler::ProcessPragma(PrepEngine &PP,
                                         PragmaIntroducer Introducer,
                                         Token &Tok) {
  SourceLocation CommentLoc = Tok.getLocation();
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(CommentLoc, diag::err_pragma_comment_malformed);
    return;
  }

  // Read the identifier.
  PP.Lex(Tok);
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(CommentLoc, diag::err_pragma_comment_malformed);
    return;
  }

  // Verify that this is one of the 5 explicitly listed options.
  IdentifierInfo *II = Tok.getIdentifierInfo();
  PragmaMSCommentKind Kind =
      llvm::StringSwitch<PragmaMSCommentKind>(II->getName())
          .Case("linker", PCK_Linker)
          .Case("lib", PCK_Lib)
          .Case("compiler", PCK_Compiler)
          .Case("exestr", PCK_ExeStr)
          .Case("user", PCK_User)
          .Default(PCK_Unknown);
  if (Kind == PCK_Unknown) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_comment_unknown_kind);
    return;
  }

  if (PP.getTargetInfo().getTriple().isOSBinFormatELF() && Kind != PCK_Lib) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_comment_ignored)
        << II->getName();
    return;
  }

  // Read the optional string if present.
  PP.Lex(Tok);
  std::string ArgumentString;
  if (Tok.is(tok::comma) &&
      !PP.LexStringContent(Tok, ArgumentString, "pragma comment",
                           /*AllowMacroExpansion=*/true))
    return;

  // The MSDN docs say that "lib" and "linker" require a string and have a short
  // list of linker options they support, but in practice MSVC doesn't
  // issue a diagnostic.  Therefore neither does NeverC.

  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_comment_malformed);
    return;
  }
  PP.Lex(Tok); // eat the r_paren.

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_comment_malformed);
    return;
  }

  // If the pragma is lexically sound, notify any interested PrepEngineObserver.
  if (PP.getObserver())
    PP.getObserver()->PragmaComment(CommentLoc, II, ArgumentString);

  Actions.OnPragmaMSComment(CommentLoc, Kind, ArgumentString);
}

// #pragma neverc optimize off
// #pragma neverc optimize on
void PragmaOptimizeHandler::ProcessPragma(PrepEngine &PP,
                                          PragmaIntroducer Introducer,
                                          Token &FirstToken) {
  Token Tok;
  PP.Lex(Tok);
  if (Tok.is(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_missing_argument)
        << "neverc optimize" << /*Expected=*/true << "'on' or 'off'";
    return;
  }
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_optimize_invalid_argument)
        << PP.getSpelling(Tok);
    return;
  }
  const IdentifierInfo *II = Tok.getIdentifierInfo();
  // The only accepted values are 'on' or 'off'.
  bool IsOn = false;
  if (II->isStr("on")) {
    IsOn = true;
  } else if (!II->isStr("off")) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_optimize_invalid_argument)
        << PP.getSpelling(Tok);
    return;
  }
  PP.Lex(Tok);

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_optimize_extra_argument)
        << PP.getSpelling(Tok);
    return;
  }

  Actions.OnPragmaOptimize(IsOn, FirstToken.getLocation());
}

namespace {
struct TokFPAnnotValue {
  enum FlagValues { On, Off, Fast };

  std::optional<LangOptions::FPModeKind> ContractValue;
  std::optional<LangOptions::FPModeKind> ReassociateValue;
  std::optional<LangOptions::FPModeKind> ReciprocalValue;
  std::optional<LangOptions::FPExceptionModeKind> ExceptionsValue;
  std::optional<LangOptions::FPEvalMethodKind> EvalMethodValue;
};
} // end anonymous namespace

void PragmaFPHandler::ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                                    Token &Tok) {
  // fp
  Token PragmaName = Tok;
  llvm::SmallVector<Token, 1> TokenList;

  PP.Lex(Tok);
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_fp_invalid_option)
        << /*MissingOption=*/true << "";
    return;
  }

  auto *AnnotValue = new (PP.getPrepEngineAllocator()) TokFPAnnotValue;
  while (Tok.is(tok::identifier)) {
    IdentifierInfo *OptionInfo = Tok.getIdentifierInfo();

    auto FlagKind =
        llvm::StringSwitch<std::optional<PragmaFPKind>>(OptionInfo->getName())
            .Case("contract", PFK_Contract)
            .Case("reassociate", PFK_Reassociate)
            .Case("exceptions", PFK_Exceptions)
            .Case("eval_method", PFK_EvalMethod)
            .Case("reciprocal", PFK_Reciprocal)
            .Default(std::nullopt);
    if (!FlagKind) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_fp_invalid_option)
          << /*MissingOption=*/false << OptionInfo;
      return;
    }
    PP.Lex(Tok);

    // Read '('
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok.getLocation(), diag::err_expected) << tok::l_paren;
      return;
    }
    PP.Lex(Tok);
    bool isEvalMethodDouble =
        Tok.is(tok::kw_double) && FlagKind == PFK_EvalMethod;

    // Don't diagnose if we have an eval_metod pragma with "double" kind.
    if (Tok.isNot(tok::identifier) && !isEvalMethodDouble) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_fp_invalid_argument)
          << PP.getSpelling(Tok) << OptionInfo->getName()
          << static_cast<int>(*FlagKind);
      return;
    }
    const IdentifierInfo *II = Tok.getIdentifierInfo();

    if (FlagKind == PFK_Contract) {
      AnnotValue->ContractValue =
          llvm::StringSwitch<std::optional<LangOptions::FPModeKind>>(
              II->getName())
              .Case("on", LangOptions::FPModeKind::FPM_On)
              .Case("off", LangOptions::FPModeKind::FPM_Off)
              .Case("fast", LangOptions::FPModeKind::FPM_Fast)
              .Default(std::nullopt);
      if (!AnnotValue->ContractValue) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_fp_invalid_argument)
            << PP.getSpelling(Tok) << OptionInfo->getName() << *FlagKind;
        return;
      }
    } else if (FlagKind == PFK_Reassociate || FlagKind == PFK_Reciprocal) {
      auto &Value = FlagKind == PFK_Reassociate ? AnnotValue->ReassociateValue
                                                : AnnotValue->ReciprocalValue;
      Value = llvm::StringSwitch<std::optional<LangOptions::FPModeKind>>(
                  II->getName())
                  .Case("on", LangOptions::FPModeKind::FPM_On)
                  .Case("off", LangOptions::FPModeKind::FPM_Off)
                  .Default(std::nullopt);
      if (!Value) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_fp_invalid_argument)
            << PP.getSpelling(Tok) << OptionInfo->getName() << *FlagKind;
        return;
      }
    } else if (FlagKind == PFK_Exceptions) {
      AnnotValue->ExceptionsValue =
          llvm::StringSwitch<std::optional<LangOptions::FPExceptionModeKind>>(
              II->getName())
              .Case("ignore", LangOptions::FPE_Ignore)
              .Case("maytrap", LangOptions::FPE_MayTrap)
              .Case("strict", LangOptions::FPE_Strict)
              .Default(std::nullopt);
      if (!AnnotValue->ExceptionsValue) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_fp_invalid_argument)
            << PP.getSpelling(Tok) << OptionInfo->getName() << *FlagKind;
        return;
      }
    } else if (FlagKind == PFK_EvalMethod) {
      AnnotValue->EvalMethodValue =
          llvm::StringSwitch<std::optional<LangOptions::FPEvalMethodKind>>(
              II->getName())
              .Case("source", LangOptions::FPEvalMethodKind::FEM_Source)
              .Case("double", LangOptions::FPEvalMethodKind::FEM_Double)
              .Case("extended", LangOptions::FPEvalMethodKind::FEM_Extended)
              .Default(std::nullopt);
      if (!AnnotValue->EvalMethodValue) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_fp_invalid_argument)
            << PP.getSpelling(Tok) << OptionInfo->getName() << *FlagKind;
        return;
      }
    }
    PP.Lex(Tok);

    // Read ')'
    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok.getLocation(), diag::err_expected) << tok::r_paren;
      return;
    }
    PP.Lex(Tok);
  }

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "neverc fp";
    return;
  }

  Token FPTok;
  FPTok.startToken();
  FPTok.setKind(tok::annot_pragma_fp);
  FPTok.setLocation(PragmaName.getLocation());
  FPTok.setAnnotationEndLoc(PragmaName.getLocation());
  FPTok.setAnnotationValue(reinterpret_cast<void *>(AnnotValue));
  TokenList.push_back(FPTok);

  auto TokenArray = std::make_unique<Token[]>(TokenList.size());
  std::copy(TokenList.begin(), TokenList.end(), TokenArray.get());

  PP.PushTokenStream(std::move(TokenArray), TokenList.size(),
                     /*DisableMacroExpansion=*/false, /*IsReinject=*/false);
}

void PragmaSTDC_FENV_ROUNDHandler::ProcessPragma(PrepEngine &PP,
                                                 PragmaIntroducer Introducer,
                                                 Token &Tok) {
  Token PragmaName = Tok;
  llvm::SmallVector<Token, 1> TokenList;
  if (!PP.getTargetInfo().hasStrictFP() && !PP.getLangOpts().ExpStrictFP) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_fp_ignored)
        << PragmaName.getIdentifierInfo()->getName();
    return;
  }

  PP.Lex(Tok);
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_identifier)
        << PragmaName.getIdentifierInfo()->getName();
    return;
  }
  IdentifierInfo *II = Tok.getIdentifierInfo();

  auto RM =
      llvm::StringSwitch<llvm::RoundingMode>(II->getName())
          .Case("FE_TOWARDZERO", llvm::RoundingMode::TowardZero)
          .Case("FE_TONEAREST", llvm::RoundingMode::NearestTiesToEven)
          .Case("FE_UPWARD", llvm::RoundingMode::TowardPositive)
          .Case("FE_DOWNWARD", llvm::RoundingMode::TowardNegative)
          .Case("FE_TONEARESTFROMZERO", llvm::RoundingMode::NearestTiesToAway)
          .Case("FE_DYNAMIC", llvm::RoundingMode::Dynamic)
          .Default(llvm::RoundingMode::Invalid);
  if (RM == llvm::RoundingMode::Invalid) {
    PP.Diag(Tok.getLocation(), diag::warn_stdc_unknown_rounding_mode);
    return;
  }
  PP.Lex(Tok);

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "STDC FENV_ROUND";
    return;
  }

  // Until the pragma is fully implemented, issue a warning.
  PP.Diag(Tok.getLocation(), diag::warn_stdc_fenv_round_not_supported);

  llvm::MutableArrayRef<Token> Toks(
      PP.getPrepEngineAllocator().Allocate<Token>(1), 1);
  Toks[0].startToken();
  Toks[0].setKind(tok::annot_pragma_fenv_round);
  Toks[0].setLocation(Tok.getLocation());
  Toks[0].setAnnotationEndLoc(Tok.getLocation());
  Toks[0].setAnnotationValue(
      reinterpret_cast<void *>(static_cast<uintptr_t>(RM)));
  PP.PushTokenStream(Toks, /*DisableMacroExpansion=*/true,
                     /*IsReinject=*/false);
}

void Parser::ProcessPragmaFP() {
  assert(Tok.is(tok::annot_pragma_fp));
  auto *AnnotValue =
      reinterpret_cast<TokFPAnnotValue *>(Tok.getAnnotationValue());

  if (AnnotValue->ReassociateValue)
    Actions.OnPragmaFPValueChangingOption(Tok.getLocation(), PFK_Reassociate,
                                          *AnnotValue->ReassociateValue ==
                                              LangOptions::FPModeKind::FPM_On);

  if (AnnotValue->ReciprocalValue)
    Actions.OnPragmaFPValueChangingOption(Tok.getLocation(), PFK_Reciprocal,
                                          *AnnotValue->ReciprocalValue ==
                                              LangOptions::FPModeKind::FPM_On);

  if (AnnotValue->ContractValue)
    Actions.OnPragmaFPContract(Tok.getLocation(), *AnnotValue->ContractValue);
  if (AnnotValue->ExceptionsValue)
    Actions.OnPragmaFPExceptions(Tok.getLocation(),
                                 *AnnotValue->ExceptionsValue);
  if (AnnotValue->EvalMethodValue)
    Actions.OnPragmaFPEvalMethod(Tok.getLocation(),
                                 *AnnotValue->EvalMethodValue);
  ConsumeAnnotationToken();
}

void PragmaMSIntrinsicHandler::ProcessPragma(PrepEngine &PP,
                                             PragmaIntroducer Introducer,
                                             Token &Tok) {
  PP.Lex(Tok);

  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_lparen)
        << "intrinsic";
    return;
  }
  PP.Lex(Tok);

  bool SuggestIntrinH = !PP.isMacroDefined("__INTRIN_H");

  while (Tok.is(tok::identifier)) {
    IdentifierInfo *II = Tok.getIdentifierInfo();
    if (!II->getBuiltinID())
      PP.Diag(Tok.getLocation(), diag::warn_pragma_intrinsic_builtin)
          << II << SuggestIntrinH;

    PP.Lex(Tok);
    if (Tok.isNot(tok::comma))
      break;
    PP.Lex(Tok);
  }

  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_rparen)
        << "intrinsic";
    return;
  }
  PP.Lex(Tok);

  if (Tok.isNot(tok::eod))
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "intrinsic";
}

bool Parser::ProcessPragmaMSFunction(llvm::StringRef PragmaName,
                                     SourceLocation PragmaLocation) {
  Token FirstTok = Tok;

  if (RequireToken(tok::l_paren, diag::warn_pragma_expected_lparen, PragmaName))
    return false;

  bool SuggestIntrinH = !PP.isMacroDefined("__INTRIN_H");

  llvm::SmallVector<llvm::StringRef> NoBuiltins;
  while (Tok.is(tok::identifier)) {
    IdentifierInfo *II = Tok.getIdentifierInfo();
    if (!II->getBuiltinID())
      PP.Diag(Tok.getLocation(), diag::warn_pragma_intrinsic_builtin)
          << II << SuggestIntrinH;
    else
      NoBuiltins.emplace_back(II->getName());

    PP.Lex(Tok);
    if (Tok.isNot(tok::comma))
      break;
    PP.Lex(Tok); // ,
  }

  if (RequireToken(tok::r_paren, diag::warn_pragma_expected_rparen,
                   PragmaName) ||
      RequireToken(tok::eof, diag::warn_pragma_extra_tokens_at_eol, PragmaName))
    return false;

  Actions.OnPragmaMSFunction(FirstTok.getLocation(), NoBuiltins);
  return true;
}

// #pragma optimize("gsty", on|off)
bool Parser::ProcessPragmaMSOptimize(llvm::StringRef PragmaName,
                                     SourceLocation PragmaLocation) {
  Token FirstTok = Tok;
  if (RequireToken(tok::l_paren, diag::warn_pragma_expected_lparen, PragmaName))
    return false;

  if (Tok.isNot(tok::string_literal)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_string) << PragmaName;
    return false;
  }
  ExprResult StringResult = ParseStringLiteralExpression();
  if (StringResult.isInvalid())
    return false; // Already diagnosed.
  StringLiteral *OptimizationList = cast<StringLiteral>(StringResult.get());
  if (OptimizationList->getCharByteWidth() != 1) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_non_wide_string)
        << PragmaName;
    return false;
  }

  if (RequireToken(tok::comma, diag::warn_pragma_expected_comma, PragmaName))
    return false;

  if (Tok.is(tok::eof) || Tok.is(tok::r_paren)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_missing_argument)
        << PragmaName << /*Expected=*/true << "'on' or 'off'";
    return false;
  }
  IdentifierInfo *II = Tok.getIdentifierInfo();
  if (!II || (!II->isStr("on") && !II->isStr("off"))) {
    PP.Diag(PragmaLocation, diag::warn_pragma_invalid_argument)
        << PP.getSpelling(Tok) << PragmaName << /*Expected=*/true
        << "'on' or 'off'";
    return false;
  }
  bool IsOn = II->isStr("on");
  PP.Lex(Tok);

  if (RequireToken(tok::r_paren, diag::warn_pragma_expected_rparen, PragmaName))
    return false;

  for (char C : OptimizationList->getString()) {
    if (C == 'g' || C == 's' || C == 't' || C == 'y')
      continue;
    PP.Diag(PragmaLocation, diag::warn_pragma_invalid_argument)
        << OptimizationList->getString() << PragmaName << /*Expected=*/true
        << "\"\", \"g\", \"s\", \"t\", \"y\", or any combination of those";
    return false;
  }

  if (RequireToken(tok::eof, diag::warn_pragma_extra_tokens_at_eol, PragmaName))
    return false;

  Actions.OnPragmaMSOptimize(FirstTok.getLocation(), IsOn);
  return true;
}

void PragmaAttributeHandler::ProcessPragma(PrepEngine &PP,
                                           PragmaIntroducer Introducer,
                                           Token &FirstToken) {
  Token Tok;
  PP.Lex(Tok);
  auto *Info = new (PP.getPrepEngineAllocator())
      PragmaAttributeInfo(AttributesForPragmaAttribute);

  // Parse the optional namespace followed by a period.
  if (Tok.is(tok::identifier)) {
    IdentifierInfo *II = Tok.getIdentifierInfo();
    if (!II->isStr("push") && !II->isStr("pop")) {
      Info->Namespace = II;
      PP.Lex(Tok);

      if (!Tok.is(tok::period)) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_attribute_expected_period)
            << II;
        return;
      }
      PP.Lex(Tok);
    }
  }

  if (!Tok.isOneOf(tok::identifier, tok::l_paren)) {
    PP.Diag(Tok.getLocation(),
            diag::err_pragma_attribute_expected_push_pop_paren);
    return;
  }

  // Determine what action this pragma attribute represents.
  if (Tok.is(tok::l_paren)) {
    if (Info->Namespace) {
      PP.Diag(Tok.getLocation(),
              diag::err_pragma_attribute_namespace_on_attribute);
      PP.Diag(Tok.getLocation(),
              diag::note_pragma_attribute_namespace_on_attribute);
      return;
    }
    Info->Action = PragmaAttributeInfo::Attribute;
  } else {
    const IdentifierInfo *II = Tok.getIdentifierInfo();
    if (II->isStr("push"))
      Info->Action = PragmaAttributeInfo::Push;
    else if (II->isStr("pop"))
      Info->Action = PragmaAttributeInfo::Pop;
    else {
      PP.Diag(Tok.getLocation(), diag::err_pragma_attribute_invalid_argument)
          << PP.getSpelling(Tok);
      return;
    }

    PP.Lex(Tok);
  }

  // Parse the actual attribute.
  if ((Info->Action == PragmaAttributeInfo::Push && Tok.isNot(tok::eod)) ||
      Info->Action == PragmaAttributeInfo::Attribute) {
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok.getLocation(), diag::err_expected) << tok::l_paren;
      return;
    }
    PP.Lex(Tok);

    // Lex the attribute tokens.
    llvm::SmallVector<Token, 16> AttributeTokens;
    int OpenParens = 1;
    while (Tok.isNot(tok::eod)) {
      if (Tok.is(tok::l_paren))
        OpenParens++;
      else if (Tok.is(tok::r_paren)) {
        OpenParens--;
        if (OpenParens == 0)
          break;
      }

      AttributeTokens.push_back(Tok);
      PP.Lex(Tok);
    }

    if (AttributeTokens.empty()) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_attribute_expected_attribute);
      return;
    }
    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok.getLocation(), diag::err_expected) << tok::r_paren;
      return;
    }
    SourceLocation EndLoc = Tok.getLocation();
    PP.Lex(Tok);

    // Terminate the attribute for parsing.
    Token EOFTok;
    EOFTok.startToken();
    EOFTok.setKind(tok::eof);
    EOFTok.setLocation(EndLoc);
    AttributeTokens.push_back(EOFTok);

    markAsReinjectedForRelexing(AttributeTokens);
    Info->Tokens =
        llvm::ArrayRef(AttributeTokens).copy(PP.getPrepEngineAllocator());
  }

  if (Tok.isNot(tok::eod))
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "neverc attribute";

  // Generate the annotated pragma token.
  auto TokenArray = std::make_unique<Token[]>(1);
  TokenArray[0].startToken();
  TokenArray[0].setKind(tok::annot_pragma_attribute);
  TokenArray[0].setLocation(FirstToken.getLocation());
  TokenArray[0].setAnnotationEndLoc(FirstToken.getLocation());
  TokenArray[0].setAnnotationValue(static_cast<void *>(Info));
  PP.PushTokenStream(std::move(TokenArray), 1,
                     /*DisableMacroExpansion=*/false, /*IsReinject=*/false);
}

// Handle '#pragma neverc max_tokens 12345'.
void PragmaMaxTokensHereHandler::ProcessPragma(PrepEngine &PP,
                                               PragmaIntroducer Introducer,
                                               Token &Tok) {
  PP.Lex(Tok);
  if (Tok.is(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_missing_argument)
        << "neverc max_tokens_here" << /*Expected=*/true << "integer";
    return;
  }

  SourceLocation Loc = Tok.getLocation();
  uint64_t MaxTokens;
  if (Tok.isNot(tok::numeric_constant) ||
      !PP.parseBasicInteger(Tok, MaxTokens)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_expected_integer)
        << "neverc max_tokens_here";
    return;
  }

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "neverc max_tokens_here";
    return;
  }

  if (PP.getTokenCount() > MaxTokens) {
    PP.Diag(Loc, diag::warn_max_tokens)
        << PP.getTokenCount() << (unsigned)MaxTokens;
  }
}

// Handle '#pragma neverc max_tokens_total 12345'.
void PragmaMaxTokensTotalHandler::ProcessPragma(PrepEngine &PP,
                                                PragmaIntroducer Introducer,
                                                Token &Tok) {
  PP.Lex(Tok);
  if (Tok.is(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_missing_argument)
        << "neverc max_tokens_total" << /*Expected=*/true << "integer";
    return;
  }

  SourceLocation Loc = Tok.getLocation();
  uint64_t MaxTokens;
  if (Tok.isNot(tok::numeric_constant) ||
      !PP.parseBasicInteger(Tok, MaxTokens)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_expected_integer)
        << "neverc max_tokens_total";
    return;
  }

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "neverc max_tokens_total";
    return;
  }

  PP.overrideMaxTokens(MaxTokens, Loc);
}
