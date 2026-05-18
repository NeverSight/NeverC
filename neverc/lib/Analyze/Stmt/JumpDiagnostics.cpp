#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "llvm/ADT/BitVector.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// Jump scope checking
// ===----------------------------------------------------------------------===

namespace {

class JumpScopeChecker {
  Sema &S;

  const bool Permissive;

  struct GotoScope {
    /// ParentScope - The index in ScopeMap of the parent scope.  This is 0 for
    /// the parent scope is the function body.
    unsigned ParentScope;

    /// InDiag - The note to emit if there is a jump into this scope.
    unsigned InDiag;

    /// OutDiag - The note to emit if there is an indirect jump out
    /// of this scope.  Direct jumps always clean up their current scope
    /// in an orderly way.
    unsigned OutDiag;

    /// Loc - Location to emit the diagnostic.
    SourceLocation Loc;

    GotoScope(unsigned parentScope, unsigned InDiag, unsigned OutDiag,
              SourceLocation L)
        : ParentScope(parentScope), InDiag(InDiag), OutDiag(OutDiag), Loc(L) {}
  };

  llvm::SmallVector<GotoScope, 48> Scopes;
  llvm::DenseMap<Stmt *, unsigned> LabelAndGotoScopes;
  llvm::SmallVector<Stmt *, 16> Jumps;

  llvm::SmallVector<Stmt *, 4> IndirectJumps;
  llvm::SmallVector<LabelDecl *, 4> IndirectJumpTargets;
  llvm::SmallVector<AttributedStmt *, 4> MustTailStmts;

public:
  JumpScopeChecker(Stmt *Body, Sema &S);

private:
  void FormScopeInformation(Decl *D, unsigned &ParentScope);
  void FormScopeInformation(CompoundLiteralExpr *CLE, unsigned &ParentScope);
  void FormScopeInformation(Stmt *S, unsigned &origParentScope);

  void VerifyJumps();
  void VerifyIndirectJumps();
  void VerifyMustTailStmts();
  void NoteJumpIntoScopes(llvm::ArrayRef<unsigned> ToScopes);
  void DiagnoseIndirectOrAsmJump(Stmt *IG, unsigned IGScope, LabelDecl *Target,
                                 unsigned TargetScope);
  void CheckJump(Stmt *From, Stmt *To, SourceLocation DiagLoc,
                 unsigned JumpDiag, unsigned JumpDiagWarning);
  void CheckGotoStmt(GotoStmt *GS);
  const Attr *GetMustTailAttr(AttributedStmt *AS);

  unsigned GetDeepestCommonScope(unsigned A, unsigned B);
};
} // end anonymous namespace

#define CHECK_PERMISSIVE(x) (assert(Permissive || !(x)), (Permissive && (x)))

JumpScopeChecker::JumpScopeChecker(Stmt *Body, Sema &s)
    : S(s), Permissive(s.hasAnyUnrecoverableErrorsInThisFunction()) {
  // Add a scope entry for function scope.
  Scopes.push_back(GotoScope(~0U, ~0U, ~0U, SourceLocation()));

  // Build information for the top level compound statement, so that we have a
  // defined scope record for every "goto" and label.
  unsigned BodyParentScope = 0;
  FormScopeInformation(Body, BodyParentScope);

  // Check that all jumps we saw are kosher.
  VerifyJumps();
  VerifyIndirectJumps();
  VerifyMustTailStmts();
}

unsigned JumpScopeChecker::GetDeepestCommonScope(unsigned A, unsigned B) {
  while (A != B) {
    // Inner scopes are created after outer scopes and therefore have
    // higher indices.
    if (A < B) {
      assert(Scopes[B].ParentScope < B);
      B = Scopes[B].ParentScope;
    } else {
      assert(Scopes[A].ParentScope < A);
      A = Scopes[A].ParentScope;
    }
  }
  return A;
}

typedef std::pair<unsigned, unsigned> ScopePair;

namespace {
ScopePair getDiagForGotoScopeDecl(Sema &S, const Decl *D) {
  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    unsigned InDiag = 0;
    unsigned OutDiag = 0;

    if (VD->getType()->isVariablyModifiedType())
      InDiag = diag::note_protected_by_vla;

    if (VD->hasAttr<CleanupAttr>())
      return ScopePair(diag::note_protected_by_cleanup,
                       diag::note_exits_cleanup);

    if (VD->hasLocalStorage()) {
      switch (VD->getType().isDestructedType()) {
      case QualType::DK_nontrivial_c_struct:
        return ScopePair(diag::note_protected_by_non_trivial_c_struct_init,
                         diag::note_exits_dtor);

      case QualType::DK_none:
        break;
      }
    }

    return ScopePair(InDiag, OutDiag);
  }

  if (const TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(D)) {
    if (TD->getUnderlyingType()->isVariablyModifiedType())
      return ScopePair(isa<TypedefDecl>(TD)
                           ? diag::note_protected_by_vla_typedef
                           : diag::note_protected_by_vla_type_alias,
                       0);
  }

  return ScopePair(0U, 0U);
}
} // namespace

void JumpScopeChecker::FormScopeInformation(Decl *D, unsigned &ParentScope) {
  // If this decl causes a new scope, push and switch to it.
  std::pair<unsigned, unsigned> Diags = getDiagForGotoScopeDecl(S, D);
  if (Diags.first || Diags.second) {
    Scopes.push_back(
        GotoScope(ParentScope, Diags.first, Diags.second, D->getLocation()));
    ParentScope = Scopes.size() - 1;
  }

  // If the decl has an initializer, walk it with the potentially new
  // scope we just installed.
  if (VarDecl *VD = dyn_cast<VarDecl>(D))
    if (Expr *Init = VD->getInit())
      FormScopeInformation(Init, ParentScope);
}

void JumpScopeChecker::FormScopeInformation(CompoundLiteralExpr *CLE,
                                            unsigned &ParentScope) {
  unsigned InDiag = diag::note_enters_compound_literal_scope;
  unsigned OutDiag = diag::note_exits_compound_literal_scope;
  Scopes.push_back(GotoScope(ParentScope, InDiag, OutDiag, CLE->getExprLoc()));
  ParentScope = Scopes.size() - 1;
}

void JumpScopeChecker::FormScopeInformation(Stmt *S,
                                            unsigned &origParentScope) {
  // If this is a statement, rather than an expression, scopes within it don't
  // propagate out into the enclosing scope.
  unsigned independentParentScope = origParentScope;
  unsigned &ParentScope =
      ((isa<Expr>(S) && !isa<StmtExpr>(S)) ? origParentScope
                                           : independentParentScope);

  unsigned StmtsToSkip = 0u;

  // If we found a label, remember that it is in ParentScope scope.
  switch (S->getStmtClass()) {
  case Stmt::AddrLabelExprClass:
    IndirectJumpTargets.push_back(cast<AddrLabelExpr>(S)->getLabel());
    break;

  case Stmt::IndirectGotoStmtClass:
    // "goto *&&lbl;" is a special case which we treat as equivalent
    // to a normal goto.  In addition, we don't calculate scope in the
    // operand (to avoid recording the address-of-label use), which
    // works only because of the restricted set of expressions which
    // we detect as constant targets.
    if (cast<IndirectGotoStmt>(S)->getConstantTarget())
      goto RecordJumpScope;

    LabelAndGotoScopes[S] = ParentScope;
    IndirectJumps.push_back(S);
    break;

  case Stmt::SwitchStmtClass:
    // Evaluate the switch init and condition variable before the switch body.
    if (Stmt *Init = cast<SwitchStmt>(S)->getInit()) {
      FormScopeInformation(Init, ParentScope);
      ++StmtsToSkip;
    }
    if (VarDecl *Var = cast<SwitchStmt>(S)->getConditionVariable()) {
      FormScopeInformation(Var, ParentScope);
      ++StmtsToSkip;
    }
    goto RecordJumpScope;

  case Stmt::GCCAsmStmtClass:
    if (!cast<GCCAsmStmt>(S)->isAsmGoto())
      break;
    [[fallthrough]];

  case Stmt::GotoStmtClass:
  RecordJumpScope:
    // Remember both what scope a goto is in as well as the fact that we have
    // it.  This makes the second scan not have to walk the AST again.
    LabelAndGotoScopes[S] = ParentScope;
    Jumps.push_back(S);
    break;

  case Stmt::IfStmtClass:
    break;

  case Stmt::SEHTryStmtClass: {
    SEHTryStmt *TS = cast<SEHTryStmt>(S);
    {
      unsigned NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(ParentScope, diag::note_protected_by_seh_try,
                                 diag::note_exits_seh_try,
                                 TS->getSourceRange().getBegin()));
      if (Stmt *TryBlock = TS->getTryBlock())
        FormScopeInformation(TryBlock, NewParentScope);
    }

    // Jump from __except or __finally into the __try are not allowed either.
    if (SEHExceptStmt *Except = TS->getExceptHandler()) {
      unsigned NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(
          ParentScope, diag::note_protected_by_seh_except,
          diag::note_exits_seh_except, Except->getSourceRange().getBegin()));
      FormScopeInformation(Except->getBlock(), NewParentScope);
    } else if (SEHFinallyStmt *Finally = TS->getFinallyHandler()) {
      unsigned NewParentScope = Scopes.size();
      Scopes.push_back(GotoScope(
          ParentScope, diag::note_protected_by_seh_finally,
          diag::note_exits_seh_finally, Finally->getSourceRange().getBegin()));
      FormScopeInformation(Finally->getBlock(), NewParentScope);
    }

    return;
  }

  case Stmt::DeclStmtClass: {
    // If this is a declstmt with a VLA definition, it defines a scope from here
    // to the end of the containing context.
    DeclStmt *DS = cast<DeclStmt>(S);
    // The decl statement creates a scope if any of the decls in it are VLAs
    // or have the cleanup attribute.
    for (auto *I : DS->decls())
      FormScopeInformation(I, origParentScope);
    return;
  }

  case Stmt::StmtExprClass: {
    // [GNU]
    // Jumping into a statement expression with goto or using
    // a switch statement outside the statement expression with
    // a case or default label inside the statement expression is not permitted.
    // Jumping out of a statement expression is permitted.
    StmtExpr *SE = cast<StmtExpr>(S);
    unsigned NewParentScope = Scopes.size();
    Scopes.push_back(GotoScope(ParentScope,
                               diag::note_enters_statement_expression,
                               /*OutDiag=*/0, SE->getBeginLoc()));
    FormScopeInformation(SE->getSubStmt(), NewParentScope);
    return;
  }

  case Stmt::ExprWithCleanupsClass: {
    ExprWithCleanups *EWC = cast<ExprWithCleanups>(S);
    for (unsigned i = 0, e = EWC->getNumObjects(); i != e; ++i) {
      if (auto *CLE = EWC->getObject(i))
        FormScopeInformation(CLE, origParentScope);
    }
    break;
  }

  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
  case Stmt::LabelStmtClass:
    LabelAndGotoScopes[S] = ParentScope;
    break;

  case Stmt::AttributedStmtClass: {
    AttributedStmt *AS = cast<AttributedStmt>(S);
    if (GetMustTailAttr(AS)) {
      LabelAndGotoScopes[AS] = ParentScope;
      MustTailStmts.push_back(AS);
    }
    break;
  }

  default:
    break;
  }

  for (Stmt *SubStmt : S->children()) {
    if (!SubStmt)
      continue;
    if (StmtsToSkip) {
      --StmtsToSkip;
      continue;
    }

    // Cases, labels, and defaults aren't "scope parents".  It's also
    // important to handle these iteratively instead of recursively in
    // order to avoid blowing out the stack.
    while (true) {
      Stmt *Next;
      if (SwitchCase *SC = dyn_cast<SwitchCase>(SubStmt))
        Next = SC->getSubStmt();
      else if (LabelStmt *LS = dyn_cast<LabelStmt>(SubStmt))
        Next = LS->getSubStmt();
      else
        break;

      LabelAndGotoScopes[SubStmt] = ParentScope;
      SubStmt = Next;
    }

    // Recursively walk the AST.
    FormScopeInformation(SubStmt, ParentScope);
  }
}

void JumpScopeChecker::VerifyJumps() {
  while (!Jumps.empty()) {
    Stmt *Jump = Jumps.pop_back_val();

    // With a goto,
    if (GotoStmt *GS = dyn_cast<GotoStmt>(Jump)) {
      // The label may not have a statement if it's coming from inline MS ASM.
      if (GS->getLabel()->getStmt()) {
        CheckJump(GS, GS->getLabel()->getStmt(), GS->getGotoLoc(),
                  diag::err_goto_into_protected_scope,
                  diag::ext_goto_into_protected_scope);
      }
      CheckGotoStmt(GS);
      continue;
    }

    // If an asm goto jumps to a different scope, things like destructors or
    // initializers might not be run which may be suprising to users. Perhaps
    // this behavior can be changed in the future, but today NeverC will not
    // generate such code. Produce a diagnostic instead. See also the
    // discussion here: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=110728.
    if (auto *G = dyn_cast<GCCAsmStmt>(Jump)) {
      for (AddrLabelExpr *L : G->labels()) {
        LabelDecl *LD = L->getLabel();
        unsigned JumpScope = LabelAndGotoScopes[G];
        unsigned TargetScope = LabelAndGotoScopes[LD->getStmt()];
        if (JumpScope != TargetScope)
          DiagnoseIndirectOrAsmJump(G, JumpScope, LD, TargetScope);
      }
      continue;
    }

    // We only get indirect gotos here when they have a constant target.
    if (IndirectGotoStmt *IGS = dyn_cast<IndirectGotoStmt>(Jump)) {
      LabelDecl *Target = IGS->getConstantTarget();
      CheckJump(IGS, Target->getStmt(), IGS->getGotoLoc(),
                diag::err_goto_into_protected_scope,
                diag::ext_goto_into_protected_scope);
      continue;
    }

    SwitchStmt *SS = cast<SwitchStmt>(Jump);
    for (SwitchCase *SC = SS->getSwitchCaseList(); SC;
         SC = SC->getNextSwitchCase()) {
      if (CHECK_PERMISSIVE(!LabelAndGotoScopes.contains(SC)))
        continue;
    }
  }
}

void JumpScopeChecker::VerifyIndirectJumps() {
  if (IndirectJumps.empty())
    return;
  if (IndirectJumpTargets.empty()) {
    S.Diag(IndirectJumps[0]->getBeginLoc(),
           diag::err_indirect_goto_without_addrlabel);
    return;
  }
  // One representative per scope with an indirect goto.
  using JumpScope = std::pair<unsigned, Stmt *>;
  llvm::SmallVector<JumpScope, 32> JumpScopes;
  {
    llvm::DenseMap<unsigned, Stmt *> JumpScopesMap;
    for (Stmt *IG : IndirectJumps) {
      if (CHECK_PERMISSIVE(!LabelAndGotoScopes.contains(IG)))
        continue;
      unsigned IGScope = LabelAndGotoScopes[IG];
      if (!JumpScopesMap.contains(IGScope))
        JumpScopesMap[IGScope] = IG;
    }
    JumpScopes.reserve(JumpScopesMap.size());
    for (auto &Pair : JumpScopesMap)
      JumpScopes.emplace_back(Pair);
  }

  // Collect a single representative of every scope containing a
  // label whose address was taken somewhere in the function.
  llvm::DenseMap<unsigned, LabelDecl *> TargetScopes;
  for (LabelDecl *TheLabel : IndirectJumpTargets) {
    if (CHECK_PERMISSIVE(!LabelAndGotoScopes.contains(TheLabel->getStmt())))
      continue;
    unsigned LabelScope = LabelAndGotoScopes[TheLabel->getStmt()];
    if (!TargetScopes.contains(LabelScope))
      TargetScopes[LabelScope] = TheLabel;
  }

  // For each target scope, make sure it's trivially reachable from
  // every scope containing a jump site.
  //
  // A path between scopes always consists of exitting zero or more
  // scopes, then entering zero or more scopes.  We build a set of
  // of scopes S from which the target scope can be trivially
  // entered, then verify that every jump scope can be trivially
  // exitted to reach a scope in S.
  llvm::BitVector Reachable(Scopes.size(), false);
  for (auto [TargetScope, TargetLabel] : TargetScopes) {
    Reachable.reset();

    // Mark all the enclosing scopes from which you can safely jump
    // into the target scope.  'Min' will end up being the index of
    // the shallowest such scope.
    unsigned Min = TargetScope;
    while (true) {
      Reachable.set(Min);

      // Don't go beyond the outermost scope.
      if (Min == 0)
        break;

      // Stop if we can't trivially enter the current scope.
      if (Scopes[Min].InDiag)
        break;

      Min = Scopes[Min].ParentScope;
    }

    // Walk through all the jump sites, checking that they can trivially
    // reach this label scope.
    for (auto [JumpScope, JumpStmt] : JumpScopes) {
      unsigned Scope = JumpScope;
      // Walk out the "scope chain" for this scope, looking for a scope
      // we've marked reachable.  For well-formed code this amortizes
      // to O(JumpScopes.size() / Scopes.size()):  we only iterate
      // when we see something unmarked, and in well-formed code we
      // mark everything we iterate past.
      bool IsReachable = false;
      while (true) {
        if (Reachable.test(Scope)) {
          // If we find something reachable, mark all the scopes we just
          // walked through as reachable.
          for (unsigned S = JumpScope; S != Scope; S = Scopes[S].ParentScope)
            Reachable.set(S);
          IsReachable = true;
          break;
        }

        // Don't walk out if we've reached the top-level scope or we've
        // gotten shallower than the shallowest reachable scope.
        if (Scope == 0 || Scope < Min)
          break;

        // Don't walk out through an out-diagnostic.
        if (Scopes[Scope].OutDiag)
          break;

        Scope = Scopes[Scope].ParentScope;
      }

      // Only diagnose if we didn't find something.
      if (IsReachable)
        continue;

      DiagnoseIndirectOrAsmJump(JumpStmt, JumpScope, TargetLabel, TargetScope);
    }
  }
}

namespace {
bool isMicrosoftJumpWarning(unsigned JumpDiag, unsigned InDiagNote) {
  return (JumpDiag == diag::err_goto_into_protected_scope &&
          InDiagNote == diag::note_protected_by_variable_init);
}

void diagnoseIndirectOrAsmJumpStmt(Sema &S, Stmt *Jump, LabelDecl *Target,
                                   bool &Diagnosed) {
  if (Diagnosed)
    return;
  bool IsAsmGoto = isa<GCCAsmStmt>(Jump);
  S.Diag(Jump->getBeginLoc(), diag::err_indirect_goto_in_protected_scope)
      << IsAsmGoto;
  S.Diag(Target->getStmt()->getIdentLoc(), diag::note_indirect_goto_target)
      << IsAsmGoto;
  Diagnosed = true;
}
} // namespace

void JumpScopeChecker::NoteJumpIntoScopes(llvm::ArrayRef<unsigned> ToScopes) {
  if (CHECK_PERMISSIVE(ToScopes.empty()))
    return;
  for (unsigned I = 0, E = ToScopes.size(); I != E; ++I)
    if (Scopes[ToScopes[I]].InDiag)
      S.Diag(Scopes[ToScopes[I]].Loc, Scopes[ToScopes[I]].InDiag);
}

void JumpScopeChecker::DiagnoseIndirectOrAsmJump(Stmt *Jump, unsigned JumpScope,
                                                 LabelDecl *Target,
                                                 unsigned TargetScope) {
  if (CHECK_PERMISSIVE(JumpScope == TargetScope))
    return;

  unsigned Common = GetDeepestCommonScope(JumpScope, TargetScope);
  bool Diagnosed = false;

  // Walk out the scope chain until we reach the common ancestor.
  for (unsigned I = JumpScope; I != Common; I = Scopes[I].ParentScope)
    if (Scopes[I].OutDiag) {
      diagnoseIndirectOrAsmJumpStmt(S, Jump, Target, Diagnosed);
      S.Diag(Scopes[I].Loc, Scopes[I].OutDiag);
    }

  // Now walk into the scopes containing the label whose address was taken.
  for (unsigned I = TargetScope; I != Common; I = Scopes[I].ParentScope)
    if (Scopes[I].InDiag) {
      diagnoseIndirectOrAsmJumpStmt(S, Jump, Target, Diagnosed);
      S.Diag(Scopes[I].Loc, Scopes[I].InDiag);
    }
}

void JumpScopeChecker::CheckJump(Stmt *From, Stmt *To, SourceLocation DiagLoc,
                                 unsigned JumpDiagError,
                                 unsigned JumpDiagWarning) {
  if (CHECK_PERMISSIVE(!LabelAndGotoScopes.contains(From)))
    return;
  if (CHECK_PERMISSIVE(!LabelAndGotoScopes.contains(To)))
    return;

  unsigned FromScope = LabelAndGotoScopes[From];
  unsigned ToScope = LabelAndGotoScopes[To];

  // Common case: exactly the same scope, which is fine.
  if (FromScope == ToScope)
    return;

  // Warn on gotos out of __finally blocks.
  if (isa<GotoStmt>(From) || isa<IndirectGotoStmt>(From)) {
    // If FromScope > ToScope, FromScope is more nested and the jump goes to a
    // less nested scope.  Check if it crosses a __finally along the way.
    for (unsigned I = FromScope; I > ToScope; I = Scopes[I].ParentScope) {
      if (Scopes[I].InDiag == diag::note_protected_by_seh_finally) {
        S.Diag(From->getBeginLoc(), diag::warn_jump_out_of_seh_finally);
        break;
      }
    }
  }

  unsigned CommonScope = GetDeepestCommonScope(FromScope, ToScope);

  // It's okay to jump out from a nested scope.
  if (CommonScope == ToScope)
    return;

  llvm::SmallVector<unsigned, 10> ToScopesError;
  llvm::SmallVector<unsigned, 10> ToScopesWarning;
  for (unsigned I = ToScope; I != CommonScope; I = Scopes[I].ParentScope) {
    if (S.getLangOpts().MSVCCompat && JumpDiagWarning != 0 &&
        isMicrosoftJumpWarning(JumpDiagError, Scopes[I].InDiag))
      ToScopesWarning.push_back(I);
    else if (Scopes[I].InDiag)
      ToScopesError.push_back(I);
  }

#ifndef _WIN32
  if (!ToScopesWarning.empty()) {
    S.Diag(DiagLoc, JumpDiagWarning);
    NoteJumpIntoScopes(ToScopesWarning);
    assert(isa<LabelStmt>(To));
    LabelStmt *Label = cast<LabelStmt>(To);
    Label->setSideEntry(true);
  }
#endif

  if (!ToScopesError.empty()) {
    S.Diag(DiagLoc, JumpDiagError);
    NoteJumpIntoScopes(ToScopesError);
  }
}

void JumpScopeChecker::CheckGotoStmt(GotoStmt *GS) {
  if (GS->getLabel()->isMSAsmLabel()) {
    S.Diag(GS->getGotoLoc(), diag::err_goto_ms_asm_label)
        << GS->getLabel()->getIdentifier();
    S.Diag(GS->getLabel()->getLocation(), diag::note_goto_ms_asm_label)
        << GS->getLabel()->getIdentifier();
  }
}

void JumpScopeChecker::VerifyMustTailStmts() {
  for (AttributedStmt *AS : MustTailStmts) {
    for (unsigned I = LabelAndGotoScopes[AS]; I; I = Scopes[I].ParentScope) {
      if (Scopes[I].OutDiag) {
        S.Diag(AS->getBeginLoc(), diag::err_musttail_scope);
        S.Diag(Scopes[I].Loc, Scopes[I].OutDiag);
      }
    }
  }
}

const Attr *JumpScopeChecker::GetMustTailAttr(AttributedStmt *AS) {
  llvm::ArrayRef<const Attr *> Attrs = AS->getAttrs();
  const auto *Iter =
      llvm::find_if(Attrs, [](const Attr *A) { return isa<MustTailAttr>(A); });
  return Iter != Attrs.end() ? *Iter : nullptr;
}

void Sema::DiagnoseInvalidJumps(Stmt *Body) {
  (void)JumpScopeChecker(Body, *this);
}
