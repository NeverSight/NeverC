#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
using namespace neverc;

namespace {
class StmtProfiler : public ConstStmtVisitor<StmtProfiler> {
protected:
  llvm::FoldingSetNodeID &ID;
  bool Canonical;

public:
  StmtProfiler(llvm::FoldingSetNodeID &ID, bool Canonical)
      : ID(ID), Canonical(Canonical) {}

  virtual ~StmtProfiler() {}

  void VisitStmt(const Stmt *S);

  void VisitStmtNoChildren(const Stmt *S) {
    HandleStmtClass(S->getStmtClass());
  }

  virtual void HandleStmtClass(Stmt::StmtClass SC) = 0;

  // Only Visit* with out-of-line definitions below; other stmt classes use
  // ConstStmtVisitor<StmtProfiler> default dispatch.

  void VisitDeclStmt(const DeclStmt *S);
  void VisitLabelStmt(const LabelStmt *S);
  void VisitIfStmt(const IfStmt *S);
  void VisitSwitchStmt(const SwitchStmt *S);
  void VisitWhileStmt(const WhileStmt *S);
  void VisitGotoStmt(const GotoStmt *S);
  void VisitGCCAsmStmt(const GCCAsmStmt *S);
  void VisitExpr(const Expr *S);
  void VisitDeclRefExpr(const DeclRefExpr *S);
  void VisitPredefinedExpr(const PredefinedExpr *S);
  void VisitIntegerLiteral(const IntegerLiteral *S);
  void VisitFixedPointLiteral(const FixedPointLiteral *S);
  void VisitCharacterLiteral(const CharacterLiteral *S);
  void VisitFloatingLiteral(const FloatingLiteral *S);
  void VisitStringLiteral(const StringLiteral *S);
  void VisitUnaryOperator(const UnaryOperator *S);
  void VisitOffsetOfExpr(const OffsetOfExpr *S);
  void VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *S);
  void VisitMemberExpr(const MemberExpr *S);
  void VisitCompoundLiteralExpr(const CompoundLiteralExpr *S);
  void VisitImplicitCastExpr(const ImplicitCastExpr *S);
  void VisitExplicitCastExpr(const ExplicitCastExpr *S);
  void VisitBinaryOperator(const BinaryOperator *S);
  void VisitAddrLabelExpr(const AddrLabelExpr *S);
  void VisitInitListExpr(const InitListExpr *S);
  void VisitDesignatedInitExpr(const DesignatedInitExpr *S);
  void VisitDesignatedInitUpdateExpr(const DesignatedInitUpdateExpr *S);
  void VisitNoInitExpr(const NoInitExpr *S);
  void VisitExtVectorElementExpr(const ExtVectorElementExpr *S);
  void VisitGenericSelectionExpr(const GenericSelectionExpr *S);
  void VisitPseudoObjectExpr(const PseudoObjectExpr *S);
  void VisitAtomicExpr(const AtomicExpr *S);
  virtual void VisitDecl(const Decl *D) = 0;

  virtual void VisitType(QualType T) = 0;

  virtual void VisitName(DeclarationName Name, bool TreatAsDecl = false) = 0;

  virtual void VisitIdentifierInfo(IdentifierInfo *II) = 0;
};

class StmtProfilerWithPointers : public StmtProfiler {
  const TreeContext &Context;

public:
  StmtProfilerWithPointers(llvm::FoldingSetNodeID &ID,
                           const TreeContext &Context, bool Canonical)
      : StmtProfiler(ID, Canonical), Context(Context) {}

private:
  void HandleStmtClass(Stmt::StmtClass SC) override { ID.AddInteger(SC); }

  void VisitDecl(const Decl *D) override {
    ID.AddInteger(D ? D->getKind() : 0);

    if (Canonical && D) {
      if (const ParmVarDecl *Parm = dyn_cast<ParmVarDecl>(D)) {
        VisitType(Parm->getType());
        ID.AddInteger(Parm->getFunctionScopeDepth());
        ID.AddInteger(Parm->getFunctionScopeIndex());
        return;
      }
    }

    ID.AddPointer(D ? D->getCanonicalDecl() : nullptr);
  }

  void VisitType(QualType T) override {
    if (Canonical && !T.isNull())
      T = Context.getCanonicalType(T);

    ID.AddPointer(T.getAsOpaquePtr());
  }

  void VisitName(DeclarationName Name, bool /*TreatAsDecl*/) override {
    ID.AddPointer(Name.getAsOpaquePtr());
  }

  void VisitIdentifierInfo(IdentifierInfo *II) override { ID.AddPointer(II); }
};

} // namespace

__attribute__((hot)) void StmtProfiler::VisitStmt(const Stmt *S) {
  assert(S && "Requires non-null Stmt pointer");

  VisitStmtNoChildren(S);

  for (const Stmt *SubStmt : S->children()) {
    if (LLVM_LIKELY(SubStmt != nullptr))
      Visit(SubStmt);
    else
      ID.AddInteger(0);
  }
}

void StmtProfiler::VisitDeclStmt(const DeclStmt *S) {
  VisitStmt(S);
  for (const auto *D : S->decls())
    VisitDecl(D);
}

void StmtProfiler::VisitLabelStmt(const LabelStmt *S) {
  VisitStmt(S);
  VisitDecl(S->getDecl());
}

void StmtProfiler::VisitIfStmt(const IfStmt *S) {
  VisitStmt(S);
  VisitDecl(S->getConditionVariable());
}

void StmtProfiler::VisitSwitchStmt(const SwitchStmt *S) {
  VisitStmt(S);
  VisitDecl(S->getConditionVariable());
}

void StmtProfiler::VisitWhileStmt(const WhileStmt *S) {
  VisitStmt(S);
  VisitDecl(S->getConditionVariable());
}

void StmtProfiler::VisitGotoStmt(const GotoStmt *S) {
  VisitStmt(S);
  VisitDecl(S->getLabel());
}

void StmtProfiler::VisitGCCAsmStmt(const GCCAsmStmt *S) {
  VisitStmt(S);
  ID.AddBoolean(S->isVolatile());
  ID.AddBoolean(S->isSimple());
  VisitStringLiteral(S->getAsmString());
  ID.AddInteger(S->getNumOutputs());
  for (unsigned I = 0, N = S->getNumOutputs(); I != N; ++I) {
    ID.AddString(S->getOutputName(I));
    VisitStringLiteral(S->getOutputConstraintLiteral(I));
  }
  ID.AddInteger(S->getNumInputs());
  for (unsigned I = 0, N = S->getNumInputs(); I != N; ++I) {
    ID.AddString(S->getInputName(I));
    VisitStringLiteral(S->getInputConstraintLiteral(I));
  }
  ID.AddInteger(S->getNumClobbers());
  for (unsigned I = 0, N = S->getNumClobbers(); I != N; ++I)
    VisitStringLiteral(S->getClobberStringLiteral(I));
  ID.AddInteger(S->getNumLabels());
  for (auto *L : S->labels())
    VisitDecl(L->getLabel());
}

void StmtProfiler::VisitExpr(const Expr *S) { VisitStmt(S); }

void StmtProfiler::VisitDeclRefExpr(const DeclRefExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getDecl());
  if (!Canonical)
    ID.AddBoolean(false);
}

void StmtProfiler::VisitPredefinedExpr(const PredefinedExpr *S) {
  VisitExpr(S);
  ID.AddInteger(llvm::to_underlying(S->getIdentKind()));
}

void StmtProfiler::VisitIntegerLiteral(const IntegerLiteral *S) {
  VisitExpr(S);
  S->getValue().Profile(ID);

  QualType T = S->getType();
  if (Canonical)
    T = T.getCanonicalType();
  ID.AddInteger(T->getTypeClass());
  if (auto BitIntT = T->getAs<BitIntType>())
    BitIntT->Profile(ID);
  else
    ID.AddInteger(T->castAs<BuiltinType>()->getKind());
}

void StmtProfiler::VisitFixedPointLiteral(const FixedPointLiteral *S) {
  VisitExpr(S);
  S->getValue().Profile(ID);
  ID.AddInteger(S->getType()->castAs<BuiltinType>()->getKind());
}

void StmtProfiler::VisitCharacterLiteral(const CharacterLiteral *S) {
  VisitExpr(S);
  ID.AddInteger(llvm::to_underlying(S->getKind()));
  ID.AddInteger(S->getValue());
}

void StmtProfiler::VisitFloatingLiteral(const FloatingLiteral *S) {
  VisitExpr(S);
  S->getValue().Profile(ID);
  ID.AddBoolean(S->isExact());
  ID.AddInteger(S->getType()->castAs<BuiltinType>()->getKind());
}

void StmtProfiler::VisitStringLiteral(const StringLiteral *S) {
  VisitExpr(S);
  ID.AddString(S->getBytes());
  ID.AddInteger(llvm::to_underlying(S->getKind()));
}

void StmtProfiler::VisitUnaryOperator(const UnaryOperator *S) {
  VisitExpr(S);
  ID.AddInteger(S->getOpcode());
}

void StmtProfiler::VisitOffsetOfExpr(const OffsetOfExpr *S) {
  VisitType(S->getTypeSourceInfo()->getType());
  unsigned n = S->getNumComponents();
  for (unsigned i = 0; i < n; ++i) {
    const OffsetOfNode &ON = S->getComponent(i);
    ID.AddInteger(ON.getKind());
    switch (ON.getKind()) {
    case OffsetOfNode::Array:
      // Expressions handled below.
      break;

    case OffsetOfNode::Field:
      VisitDecl(ON.getField());
      break;

    case OffsetOfNode::Identifier:
      VisitIdentifierInfo(ON.getFieldName());
      break;
    }
  }

  VisitExpr(S);
}

void StmtProfiler::VisitUnaryExprOrTypeTraitExpr(
    const UnaryExprOrTypeTraitExpr *S) {
  VisitExpr(S);
  ID.AddInteger(S->getKind());
  if (S->isArgumentType())
    VisitType(S->getArgumentType());
}

void StmtProfiler::VisitMemberExpr(const MemberExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getMemberDecl());
  ID.AddBoolean(S->isArrow());
}

void StmtProfiler::VisitCompoundLiteralExpr(const CompoundLiteralExpr *S) {
  VisitExpr(S);
  ID.AddBoolean(S->isFileScope());
}

void StmtProfiler::VisitImplicitCastExpr(const ImplicitCastExpr *S) {
  VisitCastExpr(S);
  ID.AddInteger(S->getValueKind());
}

void StmtProfiler::VisitExplicitCastExpr(const ExplicitCastExpr *S) {
  VisitCastExpr(S);
  VisitType(S->getTypeAsWritten());
}

void StmtProfiler::VisitBinaryOperator(const BinaryOperator *S) {
  VisitExpr(S);
  ID.AddInteger(S->getOpcode());
}

void StmtProfiler::VisitAddrLabelExpr(const AddrLabelExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getLabel());
}

void StmtProfiler::VisitInitListExpr(const InitListExpr *S) {
  if (S->getSyntacticForm()) {
    VisitInitListExpr(S->getSyntacticForm());
    return;
  }

  VisitExpr(S);
}

void StmtProfiler::VisitDesignatedInitExpr(const DesignatedInitExpr *S) {
  VisitExpr(S);
  ID.AddBoolean(S->usesGNUSyntax());
  for (const DesignatedInitExpr::Designator &D : S->designators()) {
    if (D.isFieldDesignator()) {
      ID.AddInteger(0);
      VisitName(D.getFieldName());
      continue;
    }

    if (D.isArrayDesignator()) {
      ID.AddInteger(1);
    } else {
      assert(D.isArrayRangeDesignator());
      ID.AddInteger(2);
    }
    ID.AddInteger(D.getArrayIndex());
  }
}

void StmtProfiler::VisitDesignatedInitUpdateExpr(
    const DesignatedInitUpdateExpr *S) {
  llvm_unreachable("Unexpected DesignatedInitUpdateExpr in syntactic form of "
                   "initializer");
}

void StmtProfiler::VisitNoInitExpr(const NoInitExpr *S) {
  llvm_unreachable("Unexpected NoInitExpr in syntactic form of initializer");
}

void StmtProfiler::VisitExtVectorElementExpr(const ExtVectorElementExpr *S) {
  VisitExpr(S);
  VisitName(&S->getAccessor());
}

void StmtProfiler::VisitGenericSelectionExpr(const GenericSelectionExpr *S) {
  VisitExpr(S);
  for (const GenericSelectionExpr::ConstAssociation Assoc : S->associations()) {
    QualType T = Assoc.getType();
    if (T.isNull())
      ID.AddPointer(nullptr);
    else
      VisitType(T);
    VisitExpr(Assoc.getAssociationExpr());
  }
}

void StmtProfiler::VisitPseudoObjectExpr(const PseudoObjectExpr *S) {
  VisitExpr(S);
  for (PseudoObjectExpr::const_semantics_iterator i = S->semantics_begin(),
                                                  e = S->semantics_end();
       i != e; ++i)
    if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(*i))
      Visit(OVE->getSourceExpr());
}

void StmtProfiler::VisitAtomicExpr(const AtomicExpr *S) {
  VisitExpr(S);
  ID.AddInteger(S->getOp());
}

void Stmt::Profile(llvm::FoldingSetNodeID &ID, const TreeContext &Context,
                   bool Canonical) const {
  StmtProfilerWithPointers Profiler(ID, Context, Canonical);
  Profiler.Visit(this);
}
