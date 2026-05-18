#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include <optional>
using namespace neverc;

// ===----------------------------------------------------------------------===
// Pragma & attribute processing
// ===----------------------------------------------------------------------===

Sema::PragmaStackSentinelRAII::PragmaStackSentinelRAII(
    Sema &S, llvm::StringRef SlotLabel, bool ShouldAct)
    : S(S), SlotLabel(SlotLabel), ShouldAct(ShouldAct) {
  if (ShouldAct) {
    S.DataSegStack.SentinelAction(PSK_Push, SlotLabel);
    S.BSSSegStack.SentinelAction(PSK_Push, SlotLabel);
    S.ConstSegStack.SentinelAction(PSK_Push, SlotLabel);
    S.CodeSegStack.SentinelAction(PSK_Push, SlotLabel);
    S.StrictGuardStackCheckStack.SentinelAction(PSK_Push, SlotLabel);
  }
}

Sema::PragmaStackSentinelRAII::~PragmaStackSentinelRAII() {
  if (ShouldAct) {
    S.DataSegStack.SentinelAction(PSK_Pop, SlotLabel);
    S.BSSSegStack.SentinelAction(PSK_Pop, SlotLabel);
    S.ConstSegStack.SentinelAction(PSK_Pop, SlotLabel);
    S.CodeSegStack.SentinelAction(PSK_Pop, SlotLabel);
    S.StrictGuardStackCheckStack.SentinelAction(PSK_Pop, SlotLabel);
  }
}

void Sema::AddAlignmentAttributesForRecord(RecordDecl *RD) {
  AlignPackInfo InfoVal = AlignPackStack.CurrentValue;
  if (!InfoVal.IsPackSet())
    return;

  RD->addAttr(MaxFieldAlignmentAttr::CreateImplicit(
      Context, InfoVal.getPackNumber() * 8));
}

void Sema::AddMsStructLayoutForRecord(RecordDecl *RD) {
  if (MSStructPragmaOn)
    RD->addAttr(MSStructAttr::CreateImplicit(Context));
}

void Sema::OnPragmaOptionsAlign(PragmaOptionsAlignKind Kind,
                                SourceLocation PragmaLoc) {
  PragmaMsStackAction Action = Sema::PSK_Reset;
  AlignPackInfo::Mode ModeVal = AlignPackInfo::Native;

  switch (Kind) {
  case POAK_Native:
    Action = Sema::PSK_Push_Set;
    break;
  case POAK_Packed:
    Action = Sema::PSK_Push_Set;
    ModeVal = AlignPackInfo::Packed;
    break;
  case POAK_Reset:
    Action = Sema::PSK_Pop;
    if (AlignPackStack.Stack.empty()) {
      if (AlignPackStack.CurrentValue.getAlignMode() != AlignPackInfo::Native ||
          AlignPackStack.CurrentValue.IsPackAttr()) {
        Action = Sema::PSK_Reset;
      } else {
        Diag(PragmaLoc, diag::warn_pragma_options_align_reset_failed)
            << "stack empty";
        return;
      }
    }
    break;
  }

  AlignPackInfo Info(ModeVal);

  AlignPackStack.Act(PragmaLoc, Action, llvm::StringRef(), Info);
}

void Sema::OnPragmaSection(SourceLocation PragmaLoc, PragmaSectionAction Action,
                           PragmaSectionKind SecKind, llvm::StringRef SecName) {
  PragmaSection *CSec;
  int SectionFlags = TreeContext::PSF_Read;
  switch (SecKind) {
  case PragmaSectionKind::PCSK_BSS:
    CSec = &PragmaBSSSection;
    SectionFlags |= TreeContext::PSF_Write | TreeContext::PSF_ZeroInit;
    break;
  case PragmaSectionKind::PCSK_Data:
    CSec = &PragmaDataSection;
    SectionFlags |= TreeContext::PSF_Write;
    break;
  case PragmaSectionKind::PCSK_Rodata:
    CSec = &PragmaRodataSection;
    break;
  case PragmaSectionKind::PCSK_Relro:
    CSec = &PragmaRelroSection;
    break;
  case PragmaSectionKind::PCSK_Text:
    CSec = &PragmaTextSection;
    SectionFlags |= TreeContext::PSF_Execute;
    break;
  default:
    llvm_unreachable("invalid section kind");
  }

  if (Action == PragmaSectionAction::PCSA_Clear) {
    CSec->Valid = false;
    return;
  }

  if (llvm::Error E = isValidSectionSpecifier(SecName)) {
    Diag(PragmaLoc, diag::err_pragma_section_invalid_for_target)
        << toString(std::move(E));
    CSec->Valid = false;
    return;
  }

  if (UnifySection(SecName, SectionFlags, PragmaLoc))
    return;

  CSec->Valid = true;
  CSec->SectionName = std::string(SecName);
  CSec->PragmaLocation = PragmaLoc;
}

void Sema::OnPragmaPack(SourceLocation PragmaLoc, PragmaMsStackAction Action,
                        llvm::StringRef SlotLabel, Expr *alignment) {
  const AlignPackInfo CurVal = AlignPackStack.CurrentValue;
  Expr *Alignment = static_cast<Expr *>(alignment);

  // If specified then alignment must be a "small" power of two.
  unsigned AlignmentVal = 0;
  AlignPackInfo::Mode ModeVal = CurVal.getAlignMode();

  if (Alignment) {
    std::optional<llvm::APSInt> Val;
    Val = Alignment->getIntegerConstantExpr(Context);

    // pack(0) is like pack(), which just works out since that is what
    // we use 0 for in PackAttr.
    if (!Val || !(*Val == 0 || Val->isPowerOf2()) || Val->getZExtValue() > 16) {
      Diag(PragmaLoc, diag::warn_pragma_pack_invalid_alignment);
      return; // Ignore
    }

    AlignmentVal = (unsigned)Val->getZExtValue();
  }

  if (Action == Sema::PSK_Show) {
    // Show the current alignment, making sure to show the right value
    // for the default.
    AlignmentVal = CurVal.IsPackSet() ? CurVal.getPackNumber() : 8;
    Diag(PragmaLoc, diag::warn_pragma_pack_show) << AlignmentVal;
  }

  // MSDN PrepEngine Reference > Pragma Directives > pack:
  // "#pragma pack(pop, identifier, n) is undefined"
  if (Action & Sema::PSK_Pop) {
    if (Alignment && !SlotLabel.empty())
      Diag(PragmaLoc, diag::warn_pragma_pack_pop_identifier_and_alignment);
    if (AlignPackStack.Stack.empty()) {
      assert(CurVal.getAlignMode() == AlignPackInfo::Native &&
             "Empty pack stack can only be at Native alignment mode.");
      Diag(PragmaLoc, diag::warn_pragma_pop_failed) << "pack" << "stack empty";
    }
  }

  AlignPackInfo Info(ModeVal, AlignmentVal);

  AlignPackStack.Act(PragmaLoc, Action, SlotLabel, Info);
}

bool Sema::ConstantFoldAttrArgs(const AttributeCommonInfo &CI,
                                llvm::MutableArrayRef<Expr *> Args) {
  llvm::SmallVector<PartialDiagnosticAt, 8> Notes;
  for (unsigned Idx = 0; Idx < Args.size(); Idx++) {
    Expr *&E = Args.begin()[Idx];
    assert(E && "error are handled before");
    if (E->getType()->isArrayType())
      E = ImpCastExprToType(E, Context.getPointerType(E->getType()),
                            neverc::CK_ArrayToPointerDecay)
              .get();
    if (E->getType()->isFunctionType())
      E = ImplicitCastExpr::Create(Context,
                                   Context.getPointerType(E->getType()),
                                   neverc::CK_FunctionToPointerDecay, E,
                                   VK_PRValue, FPOptionsOverride());
    if (E->isLValue())
      E = ImplicitCastExpr::Create(Context, E->getType(),
                                   neverc::CK_LValueToRValue, E, VK_PRValue,
                                   FPOptionsOverride());

    Expr::EvalResult Eval;
    Notes.clear();
    Eval.Diag = &Notes;

    bool Result = E->EvaluateAsConstantExpr(Eval, Context);

    /// Result means the expression can be folded to a constant.
    /// Note.empty() means the expression is a valid constant expression in the
    /// current language mode.
    if (!Result || !Notes.empty()) {
      Diag(E->getBeginLoc(), diag::err_attribute_argument_n_type)
          << CI << (Idx + 1) << AANT_ArgumentConstantExpr;
      for (auto &Note : Notes)
        Diag(Note.first, Note.second);
      return false;
    }
    assert(Eval.Val.hasValue());
    E = ConstantExpr::Create(Context, E, Eval.Val);
  }

  return true;
}

void Sema::DiagnoseUnterminatedPragmaAlignPack() {
  if (AlignPackStack.Stack.empty())
    return;
  bool IsInnermost = true;
  for (const auto &StackSlot : llvm::reverse(AlignPackStack.Stack)) {
    Diag(StackSlot.PragmaPushLocation, diag::warn_pragma_pack_no_pop_eof);
    // The user might have already reset the alignment, so suggest replacing
    // the reset with a pop.
    if (IsInnermost &&
        AlignPackStack.CurrentValue == AlignPackStack.DefaultValue) {
      auto DB = Diag(AlignPackStack.CurrentPragmaLocation,
                     diag::note_pragma_pack_pop_instead_reset);
      SourceLocation FixItLoc =
          SourceScanner::locateAfterToken(AlignPackStack.CurrentPragmaLocation,
                                          tok::l_paren, SourceMgr, LangOpts,
                                          /*SkipTrailing=*/false);
      if (FixItLoc.isValid())
        DB << FixItHint::CreateInsertion(FixItLoc, "pop");
    }
    IsInnermost = false;
  }
}

void Sema::OnPragmaMSStruct(PragmaMSStructKind Kind) {
  MSStructPragmaOn = (Kind == PMSST_ON);
}

void Sema::OnPragmaMSComment(SourceLocation CommentLoc,
                             PragmaMSCommentKind Kind, llvm::StringRef Arg) {
  auto *PCD = PragmaCommentDecl::Create(
      Context, Context.getTranslationUnitDecl(), CommentLoc, Kind, Arg);
  Context.getTranslationUnitDecl()->addDecl(PCD);
  Consumer.ProcessTopLevelDecl(DeclGroupRef(PCD));
}

void Sema::OnPragmaDetectMismatch(SourceLocation Loc, llvm::StringRef Name,
                                  llvm::StringRef Value) {
  auto *PDMD = PragmaDetectMismatchDecl::Create(
      Context, Context.getTranslationUnitDecl(), Loc, Name, Value);
  Context.getTranslationUnitDecl()->addDecl(PDMD);
  Consumer.ProcessTopLevelDecl(DeclGroupRef(PDMD));
}

void Sema::OnPragmaFPEvalMethod(SourceLocation Loc,
                                LangOptions::FPEvalMethodKind Value) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  switch (Value) {
  default:
    llvm_unreachable("invalid pragma eval_method kind");
  case LangOptions::FEM_Source:
    NewFPFeatures.setFPEvalMethodOverride(LangOptions::FEM_Source);
    break;
  case LangOptions::FEM_Double:
    NewFPFeatures.setFPEvalMethodOverride(LangOptions::FEM_Double);
    break;
  case LangOptions::FEM_Extended:
    NewFPFeatures.setFPEvalMethodOverride(LangOptions::FEM_Extended);
    break;
  }
  if (getLangOpts().ApproxFunc)
    Diag(Loc, diag::err_setting_eval_method_used_in_unsafe_context) << 0 << 0;
  if (getLangOpts().AllowFPReassoc)
    Diag(Loc, diag::err_setting_eval_method_used_in_unsafe_context) << 0 << 1;
  if (getLangOpts().AllowRecip)
    Diag(Loc, diag::err_setting_eval_method_used_in_unsafe_context) << 0 << 2;
  FpPragmaStack.Act(Loc, PSK_Set, llvm::StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
  PP.setCurrentFPEvalMethod(Loc, Value);
}

void Sema::OnPragmaFloatControl(SourceLocation Loc, PragmaMsStackAction Action,
                                PragmaFloatControlKind Value) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  if ((Action == PSK_Push_Set || Action == PSK_Push || Action == PSK_Pop) &&
      !CurContext->getRedeclContext()->isFileContext()) {
    // Push and pop can only occur at file scope, or within a language linkage
    // declaration.
    Diag(Loc, diag::err_pragma_fc_pp_scope);
    return;
  }
  switch (Value) {
  default:
    llvm_unreachable("invalid pragma float_control kind");
  case PFC_Precise:
    NewFPFeatures.setFPPreciseEnabled(true);
    FpPragmaStack.Act(Loc, Action, llvm::StringRef(), NewFPFeatures);
    break;
  case PFC_NoPrecise:
    if (CurFPFeatures.getExceptionMode() == LangOptions::FPE_Strict)
      Diag(Loc, diag::err_pragma_fc_noprecise_requires_noexcept);
    else if (CurFPFeatures.getAllowFEnvAccess())
      Diag(Loc, diag::err_pragma_fc_noprecise_requires_nofenv);
    else
      NewFPFeatures.setFPPreciseEnabled(false);
    FpPragmaStack.Act(Loc, Action, llvm::StringRef(), NewFPFeatures);
    break;
  case PFC_Except:
    if (!isPreciseFPEnabled())
      Diag(Loc, diag::err_pragma_fc_except_requires_precise);
    else
      NewFPFeatures.setSpecifiedExceptionModeOverride(LangOptions::FPE_Strict);
    FpPragmaStack.Act(Loc, Action, llvm::StringRef(), NewFPFeatures);
    break;
  case PFC_NoExcept:
    NewFPFeatures.setSpecifiedExceptionModeOverride(LangOptions::FPE_Ignore);
    FpPragmaStack.Act(Loc, Action, llvm::StringRef(), NewFPFeatures);
    break;
  case PFC_Push:
    FpPragmaStack.Act(Loc, Sema::PSK_Push_Set, llvm::StringRef(),
                      NewFPFeatures);
    break;
  case PFC_Pop:
    if (FpPragmaStack.Stack.empty()) {
      Diag(Loc, diag::warn_pragma_pop_failed) << "float_control"
                                              << "stack empty";
      return;
    }
    FpPragmaStack.Act(Loc, Action, llvm::StringRef(), NewFPFeatures);
    NewFPFeatures = FpPragmaStack.CurrentValue;
    break;
  }
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

bool Sema::UnifySection(llvm::StringRef SectionName, int SectionFlags,
                        NamedDecl *Decl) {
  SourceLocation PragmaLocation;
  if (auto A = Decl->getAttr<SectionAttr>())
    if (A->isImplicit())
      PragmaLocation = A->getLocation();
  auto SectionIt = Context.SectionInfos.find(SectionName);
  if (SectionIt == Context.SectionInfos.end()) {
    Context.SectionInfos[SectionName] =
        TreeContext::SectionInfo(Decl, PragmaLocation, SectionFlags);
    return false;
  }
  // A pre-declared section takes precedence w/o diagnostic.
  const auto &Section = SectionIt->second;
  if (Section.SectionFlags == SectionFlags ||
      ((SectionFlags & TreeContext::PSF_Implicit) &&
       !(Section.SectionFlags & TreeContext::PSF_Implicit)))
    return false;
  return true;
}

bool Sema::UnifySection(llvm::StringRef SectionName, int SectionFlags,
                        SourceLocation PragmaSectionLocation) {
  auto SectionIt = Context.SectionInfos.find(SectionName);
  if (SectionIt != Context.SectionInfos.end()) {
    const auto &Section = SectionIt->second;
    if (Section.SectionFlags == SectionFlags)
      return false;
    if (!(Section.SectionFlags & TreeContext::PSF_Implicit)) {
      Diag(PragmaSectionLocation, diag::err_section_conflict)
          << "this" << Section;
      if (Section.Decl)
        Diag(Section.Decl->getLocation(), diag::note_declared_at)
            << Section.Decl->getName();
      if (Section.PragmaSectionLocation.isValid())
        Diag(Section.PragmaSectionLocation, diag::note_pragma_entered_here);
      return true;
    }
  }
  Context.SectionInfos[SectionName] =
      TreeContext::SectionInfo(nullptr, PragmaSectionLocation, SectionFlags);
  return false;
}

void Sema::OnPragmaMSSeg(SourceLocation PragmaLocation,
                         PragmaMsStackAction Action,
                         llvm::StringRef StackSlotLabel,
                         StringLiteral *SegmentName,
                         llvm::StringRef PragmaName) {
  PragmaStack<StringLiteral *> *Stack =
      llvm::StringSwitch<PragmaStack<StringLiteral *> *>(PragmaName)
          .Case("data_seg", &DataSegStack)
          .Case("bss_seg", &BSSSegStack)
          .Case("const_seg", &ConstSegStack)
          .Case("code_seg", &CodeSegStack);
  if (Action & PSK_Pop && Stack->Stack.empty())
    Diag(PragmaLocation, diag::warn_pragma_pop_failed)
        << PragmaName << "stack empty";
  if (SegmentName) {
    if (!checkSectionName(SegmentName->getBeginLoc(), SegmentName->getString()))
      return;

    if (SegmentName->getString() == ".drectve")
      Diag(PragmaLocation, diag::warn_attribute_section_drectve) << PragmaName;
  }

  Stack->Act(PragmaLocation, Action, StackSlotLabel, SegmentName);
}

void Sema::OnPragmaMSStrictGuardStackCheck(SourceLocation PragmaLocation,
                                           PragmaMsStackAction Action,
                                           bool Value) {
  if (Action & PSK_Pop && StrictGuardStackCheckStack.Stack.empty())
    Diag(PragmaLocation, diag::warn_pragma_pop_failed) << "strict_gs_check"
                                                       << "stack empty";

  StrictGuardStackCheckStack.Act(PragmaLocation, Action, llvm::StringRef(),
                                 Value);
}

void Sema::OnPragmaMSSection(SourceLocation PragmaLocation, int SectionFlags,
                             StringLiteral *SegmentName) {
  UnifySection(SegmentName->getString(), SectionFlags, PragmaLocation);
}

void Sema::OnPragmaMSInitSeg(SourceLocation PragmaLocation,
                             StringLiteral *SegmentName) {
  // There's no stack to maintain, so we just have a current section.  When we
  // see the default section, reset our current section back to null so we stop
  // tacking on unnecessary attributes.
  CurInitSeg = SegmentName->getString() == ".CRT$XCU" ? nullptr : SegmentName;
  CurInitSegLoc = PragmaLocation;
}

void Sema::OnPragmaMSAllocText(
    SourceLocation PragmaLocation, llvm::StringRef Section,
    const llvm::SmallVector<std::tuple<IdentifierInfo *, SourceLocation>>
        &Functions) {
  if (!CurContext->getRedeclContext()->isFileContext()) {
    Diag(PragmaLocation, diag::err_pragma_expected_file_scope) << "alloc_text";
    return;
  }

  for (auto &Function : Functions) {
    IdentifierInfo *II;
    SourceLocation Loc;
    std::tie(II, Loc) = Function;
#ifndef _WIN32
    DeclarationName DN(II);
    NamedDecl *ND = LookupSingleName(TUScope, DN, Loc, ResolveOrdinary);
    if (!ND) {
      Diag(Loc, diag::err_undeclared_use) << II->getName();
      return;
    }

    auto *FD = dyn_cast<FunctionDecl>(ND->getCanonicalDecl());
    if (!FD) {
      Diag(Loc, diag::err_pragma_alloc_text_not_function);
      return;
    }

#endif
    FunctionToSectionMap[II->getName()] = std::make_tuple(Section, Loc);
  }
}

void Sema::OnPragmaUnused(const Token &IdTok, Scope *curScope,
                          SourceLocation PragmaLoc) {

  IdentifierInfo *Name = IdTok.getIdentifierInfo();
  LookupResult Lookup(*this, Name, IdTok.getLocation(), ResolveOrdinary);
  LookupParsedName(Lookup, curScope, true);

  if (Lookup.empty()) {
    Diag(PragmaLoc, diag::warn_pragma_unused_undeclared_var)
        << Name << SourceRange(IdTok.getLocation());
    return;
  }

  VarDecl *VD = Lookup.getAsSingle<VarDecl>();
  if (!VD) {
    Diag(PragmaLoc, diag::warn_pragma_unused_expected_var_arg)
        << Name << SourceRange(IdTok.getLocation());
    return;
  }

  // Warn if this was used before being marked unused.
  if (VD->isUsed())
    Diag(PragmaLoc, diag::warn_used_but_marked_unused) << Name;

  VD->addAttr(UnusedAttr::CreateImplicit(Context, IdTok.getLocation(),
                                         UnusedAttr::GNU_unused));
}

namespace {

std::optional<attr::SubjectMatchRule>
getParentAttrMatcherRule(attr::SubjectMatchRule Rule) {
  using namespace attr;
  switch (Rule) {
  default:
    return std::nullopt;
#define ATTR_MATCH_RULE(Value, Spelling, IsAbstract)
#define ATTR_MATCH_SUB_RULE(Value, Spelling, IsAbstract, Parent, IsNegated)    \
  case Value:                                                                  \
    return Parent;
#include "neverc/Foundation/AttrSubMatchRulesList.td.h"
  }
}

bool isNegatedAttrMatcherSubRule(attr::SubjectMatchRule Rule) {
  using namespace attr;
  switch (Rule) {
  default:
    return false;
#define ATTR_MATCH_RULE(Value, Spelling, IsAbstract)
#define ATTR_MATCH_SUB_RULE(Value, Spelling, IsAbstract, Parent, IsNegated)    \
  case Value:                                                                  \
    return IsNegated;
#include "neverc/Foundation/AttrSubMatchRulesList.td.h"
  }
}

CharSourceRange replacementRangeForListElement(const Sema &S,
                                               SourceRange Range) {
  // Make sure that the ',' is removed as well.
  SourceLocation AfterCommaLoc = SourceScanner::locateAfterToken(
      Range.getEnd(), tok::comma, S.getSourceManager(), S.getLangOpts(),
      /*SkipTrailingWhitespaceAndNewLine=*/false);
  if (AfterCommaLoc.isValid())
    return CharSourceRange::getCharRange(Range.getBegin(), AfterCommaLoc);
  else
    return CharSourceRange::getTokenRange(Range);
}

std::string
attrMatcherRuleListToString(llvm::ArrayRef<attr::SubjectMatchRule> Rules) {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  for (const auto &I : llvm::enumerate(Rules)) {
    if (I.index())
      OS << (I.index() == Rules.size() - 1 ? ", and " : ", ");
    OS << "'" << attr::getSubjectMatchRuleSpelling(I.value()) << "'";
  }
  return Result;
}

} // end anonymous namespace

void Sema::OnPragmaAttributeAttribute(ParsedAttr &Attribute,
                                      SourceLocation PragmaLoc,
                                      attr::ParsedSubjectMatchRuleSet Rules) {
  Attribute.setIsPragmaAttribute();
  llvm::SmallVector<attr::SubjectMatchRule, 4> SubjectMatchRules;
  // Gather the subject match rules that are supported by the attribute.
  llvm::SmallVector<std::pair<attr::SubjectMatchRule, bool>, 4>
      StrictSubjectMatchRuleSet;
  Attribute.getMatchRules(LangOpts, StrictSubjectMatchRuleSet);

  // Figure out which subject matching rules are valid.
  if (StrictSubjectMatchRuleSet.empty()) {
    // Check for contradicting match rules. Contradicting match rules are
    // either:
    //  - a top-level rule and one of its sub-rules. E.g. variable and
    //    variable(is_parameter).
    //  - a sub-rule and a sibling that's negated. E.g.
    //    variable(is_thread_local) and variable(unless(is_parameter))
    llvm::SmallDenseMap<int, std::pair<int, SourceRange>, 2>
        RulesToFirstSpecifiedNegatedSubRule;
    for (const auto &Rule : Rules) {
      attr::SubjectMatchRule MatchRule = attr::SubjectMatchRule(Rule.first);
      std::optional<attr::SubjectMatchRule> ParentRule =
          getParentAttrMatcherRule(MatchRule);
      if (!ParentRule)
        continue;
      auto It = Rules.find(*ParentRule);
      if (It != Rules.end()) {
        // A sub-rule contradicts a parent rule.
        Diag(Rule.second.getBegin(),
             diag::err_pragma_attribute_matcher_subrule_contradicts_rule)
            << attr::getSubjectMatchRuleSpelling(MatchRule)
            << attr::getSubjectMatchRuleSpelling(*ParentRule) << It->second
            << FixItHint::CreateRemoval(
                   replacementRangeForListElement(*this, Rule.second));
        // Keep going without removing this rule as it won't change the set of
        // declarations that receive the attribute.
        continue;
      }
      if (isNegatedAttrMatcherSubRule(MatchRule))
        RulesToFirstSpecifiedNegatedSubRule.insert(
            std::make_pair(*ParentRule, Rule));
    }
    bool IgnoreNegatedSubRules = false;
    for (const auto &Rule : Rules) {
      attr::SubjectMatchRule MatchRule = attr::SubjectMatchRule(Rule.first);
      std::optional<attr::SubjectMatchRule> ParentRule =
          getParentAttrMatcherRule(MatchRule);
      if (!ParentRule)
        continue;
      auto It = RulesToFirstSpecifiedNegatedSubRule.find(*ParentRule);
      if (It != RulesToFirstSpecifiedNegatedSubRule.end() &&
          It->second != Rule) {
        // Negated sub-rule contradicts another sub-rule.
        Diag(
            It->second.second.getBegin(),
            diag::
                err_pragma_attribute_matcher_negated_subrule_contradicts_subrule)
            << attr::getSubjectMatchRuleSpelling(
                   attr::SubjectMatchRule(It->second.first))
            << attr::getSubjectMatchRuleSpelling(MatchRule) << Rule.second
            << FixItHint::CreateRemoval(
                   replacementRangeForListElement(*this, It->second.second));
        // Keep going but ignore all of the negated sub-rules.
        IgnoreNegatedSubRules = true;
        RulesToFirstSpecifiedNegatedSubRule.erase(It);
      }
    }

    if (!IgnoreNegatedSubRules) {
      for (const auto &Rule : Rules)
        SubjectMatchRules.push_back(attr::SubjectMatchRule(Rule.first));
    } else {
      for (const auto &Rule : Rules) {
        if (!isNegatedAttrMatcherSubRule(attr::SubjectMatchRule(Rule.first)))
          SubjectMatchRules.push_back(attr::SubjectMatchRule(Rule.first));
      }
    }
    Rules.clear();
  } else {
    // Each rule in Rules must be a strict subset of the attribute's
    // SubjectMatch rules.  I.e. we're allowed to use
    // `apply_to=variables(is_global)` on an attrubute with SubjectList<[Var]>,
    // but should not allow `apply_to=variables` on an attribute which has
    // `SubjectList<[GlobalVar]>`.
    for (const auto &StrictRule : StrictSubjectMatchRuleSet) {
      // First, check for exact match.
      if (Rules.erase(StrictRule.first)) {
        // Add the rule to the set of attribute receivers only if it's supported
        // in the current language mode.
        if (StrictRule.second)
          SubjectMatchRules.push_back(StrictRule.first);
      }
    }
    // Check remaining rules for subset matches.
    auto RulesToCheck = Rules;
    for (const auto &Rule : RulesToCheck) {
      attr::SubjectMatchRule MatchRule = attr::SubjectMatchRule(Rule.first);
      if (auto ParentRule = getParentAttrMatcherRule(MatchRule)) {
        if (llvm::any_of(StrictSubjectMatchRuleSet,
                         [ParentRule](const auto &StrictRule) {
                           return StrictRule.first == *ParentRule &&
                                  StrictRule.second; // IsEnabled
                         })) {
          SubjectMatchRules.push_back(MatchRule);
          Rules.erase(MatchRule);
        }
      }
    }
  }

  if (!Rules.empty()) {
    auto Diagnostic =
        Diag(PragmaLoc, diag::err_pragma_attribute_invalid_matchers)
        << Attribute;
    llvm::SmallVector<attr::SubjectMatchRule, 2> ExtraRules;
    for (const auto &Rule : Rules) {
      ExtraRules.push_back(attr::SubjectMatchRule(Rule.first));
      Diagnostic << FixItHint::CreateRemoval(
          replacementRangeForListElement(*this, Rule.second));
    }
    Diagnostic << attrMatcherRuleListToString(ExtraRules);
  }

  if (PragmaAttributeStack.empty()) {
    Diag(PragmaLoc, diag::err_pragma_attr_attr_no_push);
    return;
  }

  PragmaAttributeStack.back().Entries.push_back(
      {PragmaLoc, &Attribute, std::move(SubjectMatchRules), /*IsUsed=*/false});
}

void Sema::OnPragmaAttributeEmptyPush(SourceLocation PragmaLoc,
                                      const IdentifierInfo *Namespace) {
  PragmaAttributeStack.emplace_back();
  PragmaAttributeStack.back().Loc = PragmaLoc;
  PragmaAttributeStack.back().Namespace = Namespace;
}

void Sema::OnPragmaAttributePop(SourceLocation PragmaLoc,
                                const IdentifierInfo *Namespace) {
  if (PragmaAttributeStack.empty()) {
    Diag(PragmaLoc, diag::err_pragma_attribute_stack_mismatch) << 1;
    return;
  }

  // Dig back through the stack trying to find the most recently pushed group
  // that in Namespace. Note that this works fine if no namespace is present,
  // think of push/pops without namespaces as having an implicit "nullptr"
  // namespace.
  for (size_t Index = PragmaAttributeStack.size(); Index;) {
    --Index;
    if (PragmaAttributeStack[Index].Namespace == Namespace) {
      for (const PragmaAttributeEntry &Entry :
           PragmaAttributeStack[Index].Entries) {
        if (!Entry.IsUsed) {
          assert(Entry.Attribute && "Expected an attribute");
          Diag(Entry.Attribute->getLoc(), diag::warn_pragma_attribute_unused)
              << *Entry.Attribute;
          Diag(PragmaLoc, diag::note_pragma_attribute_region_ends_here);
        }
      }
      PragmaAttributeStack.erase(PragmaAttributeStack.begin() + Index);
      return;
    }
  }

  if (Namespace)
    Diag(PragmaLoc, diag::err_pragma_attribute_stack_mismatch)
        << 0 << Namespace->getName();
  else
    Diag(PragmaLoc, diag::err_pragma_attribute_stack_mismatch) << 1;
}

void Sema::AddPragmaAttributes(Scope *S, Decl *D) {
  if (PragmaAttributeStack.empty())
    return;
  for (auto &Group : PragmaAttributeStack) {
    for (auto &Entry : Group.Entries) {
      ParsedAttr *Attribute = Entry.Attribute;
      assert(Attribute && "Expected an attribute");
      assert(Attribute->isPragmaAttribute() &&
             "expected #pragma neverc attribute");

      // Ensure that the attribute can be applied to the given declaration.
      bool Applies = false;
      for (const auto &Rule : Entry.MatchRules) {
        if (Attribute->appliesToDecl(D, Rule)) {
          Applies = true;
          break;
        }
      }
      if (!Applies)
        continue;
      Entry.IsUsed = true;
      PragmaAttributeCurrentTargetDecl = D;
      ParsedAttributesView Attrs;
      Attrs.addAtEnd(Attribute);
      ProcessDeclAttributeList(S, D, Attrs);
      PragmaAttributeCurrentTargetDecl = nullptr;
    }
  }
}

void Sema::PrintPragmaAttributeInstantiationPoint() {
  assert(PragmaAttributeCurrentTargetDecl && "Expected an active declaration");
  Diags.Report(PragmaAttributeCurrentTargetDecl->getBeginLoc(),
               diag::note_pragma_attribute_applied_decl_here);
}

void Sema::DiagnoseUnterminatedPragmaAttribute() {
  if (PragmaAttributeStack.empty())
    return;
  Diag(PragmaAttributeStack.back().Loc, diag::err_pragma_attribute_no_pop_eof);
}

void Sema::OnPragmaOptimize(bool On, SourceLocation PragmaLoc) {
  if (On)
    OptimizeOffPragmaLocation = SourceLocation();
  else
    OptimizeOffPragmaLocation = PragmaLoc;
}

void Sema::OnPragmaMSOptimize(SourceLocation Loc, bool IsOn) {
  if (!CurContext->getRedeclContext()->isFileContext()) {
    Diag(Loc, diag::err_pragma_expected_file_scope) << "optimize";
    return;
  }

  MSPragmaOptimizeIsOn = IsOn;
}

void Sema::OnPragmaMSFunction(
    SourceLocation Loc,
    const llvm::SmallVectorImpl<llvm::StringRef> &NoBuiltins) {
  if (!CurContext->getRedeclContext()->isFileContext()) {
    Diag(Loc, diag::err_pragma_expected_file_scope) << "function";
    return;
  }

  MSFunctionNoBuiltins.insert(NoBuiltins.begin(), NoBuiltins.end());
}

void Sema::AddRangeBasedOptnone(FunctionDecl *FD) {
  // In the future, check other pragmas if they're implemented (e.g. pragma
  // optimize 0 will probably map to this functionality too).
  if (OptimizeOffPragmaLocation.isValid())
    AddOptnoneAttributeIfNoConflicts(FD, OptimizeOffPragmaLocation);
}

void Sema::AddSectionMSAllocText(FunctionDecl *FD) {
  if (!FD->getIdentifier())
    return;

  llvm::StringRef Name = FD->getName();
  auto It = FunctionToSectionMap.find(Name);
  if (It != FunctionToSectionMap.end()) {
    llvm::StringRef Section;
    SourceLocation Loc;
    std::tie(Section, Loc) = It->second;

    if (!FD->hasAttr<SectionAttr>())
      FD->addAttr(SectionAttr::CreateImplicit(Context, Section));
  }
}

void Sema::ModifyFnAttributesMSPragmaOptimize(FunctionDecl *FD) {
  // Don't modify the function attributes if it's "on". "on" resets the
  // optimizations to the ones listed on the command line
  if (!MSPragmaOptimizeIsOn)
    AddOptnoneAttributeIfNoConflicts(FD, FD->getBeginLoc());
}

void Sema::AddOptnoneAttributeIfNoConflicts(FunctionDecl *FD,
                                            SourceLocation Loc) {
  // Don't add a conflicting attribute. No diagnostic is needed.
  if (FD->hasAttr<MinSizeAttr>() || FD->hasAttr<AlwaysInlineAttr>())
    return;

  // Add attributes only if required. Optnone requires noinline as well, but if
  // either is already present then don't bother adding them.
  if (!FD->hasAttr<OptimizeNoneAttr>())
    FD->addAttr(OptimizeNoneAttr::CreateImplicit(Context, Loc));
  if (!FD->hasAttr<NoInlineAttr>())
    FD->addAttr(NoInlineAttr::CreateImplicit(Context, Loc));
}

void Sema::AddImplicitMSFunctionNoBuiltinAttr(FunctionDecl *FD) {
  if (MSFunctionNoBuiltins.empty())
    return;
  llvm::SmallVector<llvm::StringRef> V(MSFunctionNoBuiltins.begin(),
                                       MSFunctionNoBuiltins.end());
  FD->addAttr(NoBuiltinAttr::CreateImplicit(Context, V.data(), V.size()));
}

using VisStack = std::vector<std::pair<unsigned, SourceLocation>>;
enum : unsigned { NoVisibility = ~0U };

void Sema::AddPushedVisibilityAttribute(Decl *D) {
  if (!VisContext)
    return;

  NamedDecl *ND = dyn_cast<NamedDecl>(D);
  if (ND && ND->getExplicitVisibility(NamedDecl::VisibilityForValue))
    return;

  VisStack *Stack = static_cast<VisStack *>(VisContext);
  unsigned rawType = Stack->back().first;
  if (rawType == NoVisibility)
    return;

  VisibilityAttr::VisibilityType type = (VisibilityAttr::VisibilityType)rawType;
  SourceLocation loc = Stack->back().second;

  D->addAttr(VisibilityAttr::CreateImplicit(Context, type, loc));
}

void Sema::FreeVisContext() {
  delete static_cast<VisStack *>(VisContext);
  VisContext = nullptr;
}

namespace {
void enterPragmaVisibility(Sema &S, unsigned type, SourceLocation loc) {
  // Put visibility on stack.
  if (!S.VisContext)
    S.VisContext = new VisStack;

  VisStack *Stack = static_cast<VisStack *>(S.VisContext);
  Stack->push_back(std::make_pair(type, loc));
}
} // namespace

void Sema::OnPragmaVisibility(const IdentifierInfo *VisType,
                              SourceLocation PragmaLoc) {
  if (VisType) {
    // Compute visibility to use.
    VisibilityAttr::VisibilityType T;
    if (!VisibilityAttr::ConvertStrToVisibilityType(VisType->getName(), T)) {
      Diag(PragmaLoc, diag::warn_attribute_unknown_visibility) << VisType;
      return;
    }
    enterPragmaVisibility(*this, T, PragmaLoc);
  } else {
    PopPragmaVisibility(PragmaLoc);
  }
}

void Sema::OnPragmaFPContract(SourceLocation Loc, LangOptions::FPModeKind FPC) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  switch (FPC) {
  case LangOptions::FPM_On:
    NewFPFeatures.setAllowFPContractWithinStatement();
    break;
  case LangOptions::FPM_Fast:
    NewFPFeatures.setAllowFPContractAcrossStatement();
    break;
  case LangOptions::FPM_Off:
    NewFPFeatures.setDisallowFPContract();
    break;
  case LangOptions::FPM_FastHonorPragmas:
    llvm_unreachable("Should not happen");
  }
  FpPragmaStack.Act(Loc, Sema::PSK_Set, llvm::StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::OnPragmaFPValueChangingOption(SourceLocation Loc, PragmaFPKind Kind,
                                         bool IsEnabled) {
  if (IsEnabled) {
    // For value unsafe context, combining this pragma with eval method
    // setting is not recommended. See comment in function FixupInvocation#506.
    int Reason = -1;
    if (getLangOpts().getFPEvalMethod() != LangOptions::FEM_UnsetOnCommandLine)
      // Eval method set using the option 'ffp-eval-method'.
      Reason = 1;
    if (PP.getLastFPEvalPragmaLocation().isValid())
      // Eval method set using the '#pragma neverc fp eval_method'.
      // We could have both an option and a pragma used to the set the eval
      // method. The pragma overrides the option in the command line. The Reason
      // of the diagnostic is overriden too.
      Reason = 0;
    if (Reason != -1)
      Diag(Loc, diag::err_setting_eval_method_used_in_unsafe_context)
          << Reason << (Kind == PFK_Reassociate ? 4 : 5);
  }

  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  switch (Kind) {
  case PFK_Reassociate:
    NewFPFeatures.setAllowFPReassociateOverride(IsEnabled);
    break;
  case PFK_Reciprocal:
    NewFPFeatures.setAllowReciprocalOverride(IsEnabled);
    break;
  default:
    llvm_unreachable("unhandled value changing pragma fp");
  }

  FpPragmaStack.Act(Loc, PSK_Set, llvm::StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::OnPragmaFEnvRound(SourceLocation Loc, llvm::RoundingMode FPR) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  NewFPFeatures.setConstRoundingModeOverride(FPR);
  FpPragmaStack.Act(Loc, PSK_Set, llvm::StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::setExceptionMode(SourceLocation Loc,
                            LangOptions::FPExceptionModeKind FPE) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  NewFPFeatures.setSpecifiedExceptionModeOverride(FPE);
  FpPragmaStack.Act(Loc, PSK_Set, llvm::StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::OnPragmaFEnvAccess(SourceLocation Loc, bool IsEnabled) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  if (IsEnabled) {
    // Verify Microsoft restriction:
    // You can't enable fenv_access unless precise semantics are enabled.
    // Precise semantics can be enabled either by the float_control
    // pragma, or by using the /fp:precise or /fp:strict compiler options
    if (!isPreciseFPEnabled())
      Diag(Loc, diag::err_pragma_fenv_requires_precise);
  }
  NewFPFeatures.setAllowFEnvAccessOverride(IsEnabled);
  NewFPFeatures.setRoundingMathOverride(IsEnabled);
  FpPragmaStack.Act(Loc, PSK_Set, llvm::StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::OnPragmaCXLimitedRange(SourceLocation Loc,
                                  LangOptions::ComplexRangeKind Range) {
  FPOptionsOverride NewFPFeatures = CurFPFeatureOverrides();
  NewFPFeatures.setComplexRangeOverride(Range);
  FpPragmaStack.Act(Loc, PSK_Set, llvm::StringRef(), NewFPFeatures);
  CurFPFeatures = NewFPFeatures.applyOverrides(getLangOpts());
}

void Sema::OnPragmaFPExceptions(SourceLocation Loc,
                                LangOptions::FPExceptionModeKind FPE) {
  setExceptionMode(Loc, FPE);
}

void Sema::PopPragmaVisibility(SourceLocation EndLoc) {
  if (!VisContext) {
    Diag(EndLoc, diag::err_pragma_pop_visibility_mismatch);
    return;
  }

  VisStack *Stack = static_cast<VisStack *>(VisContext);

  const std::pair<unsigned, SourceLocation> *Back = &Stack->back();
  bool StartsWithPragma = Back->first != NoVisibility;
  if (!StartsWithPragma) {
    Diag(EndLoc, diag::err_pragma_pop_visibility_mismatch);
    return;
  }

  Stack->pop_back();
  // To simplify the implementation, never keep around an empty stack.
  if (Stack->empty())
    FreeVisContext();
}

namespace {
template <typename Ty>
bool checkCommonAttributeFeatures(Sema &S, const Ty *Node,
                                  const ParsedAttr &A) {
  // We bail on unknown and ignored attributes because those are handled
  // as part of the target-specific handling logic.
  if (A.getKind() == ParsedAttr::UnknownAttribute)
    return false;
  // Check whether the attribute requires specific language extensions to be
  // enabled.
  if (!A.diagnoseLangOpts(S))
    return true;
  // Check whether the attribute appertains to the given subject.
  if (!A.diagnoseAppertainsTo(S, Node))
    return true;
  // Check whether the attribute is mutually exclusive with other attributes
  // that have already been applied to the declaration.
  if (!A.diagnoseMutualExclusion(S, Node))
    return true;
  // Check whether the attribute exists in the target architecture.
  if (S.CheckAttrTarget(A))
    return true;

  if (A.hasCustomParsing())
    return false;

  if (A.getMinArgs() == A.getMaxArgs()) {
    // If there are no optional arguments, then checking for the argument
    // count is trivial.
    if (!A.checkExactlyNumArgs(S, A.getMinArgs()))
      return true;
  } else {
    // There are optional arguments, so checking is slightly more involved.
    if (A.getMinArgs() && !A.checkAtLeastNumArgs(S, A.getMinArgs()))
      return true;
    else if (!A.hasVariadicArg() && A.getMaxArgs() &&
             !A.checkAtMostNumArgs(S, A.getMaxArgs()))
      return true;
  }

  return false;
}
} // namespace

bool Sema::checkCommonAttributeFeatures(const Decl *D, const ParsedAttr &A) {
  return ::checkCommonAttributeFeatures(*this, D, A);
}
bool Sema::checkCommonAttributeFeatures(const Stmt *S, const ParsedAttr &A) {
  return ::checkCommonAttributeFeatures(*this, S, A);
}
