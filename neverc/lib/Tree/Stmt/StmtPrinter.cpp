#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Statement printing
// ===----------------------------------------------------------------------===

namespace {

class StmtPrinter : public StmtVisitor<StmtPrinter> {
  llvm::raw_ostream &OS;
  unsigned IndentLevel;
  PrinterHelper *Helper;
  PrintingPolicy Policy;
  std::string NL;

public:
  StmtPrinter(llvm::raw_ostream &os, PrinterHelper *helper,
              const PrintingPolicy &Policy, unsigned Indentation = 0,
              llvm::StringRef NL = "\n")
      : OS(os), IndentLevel(Indentation), Helper(helper), Policy(Policy),
        NL(NL) {}

  void PrintStmt(Stmt *S) { PrintStmt(S, Policy.Indentation); }

  void PrintStmt(Stmt *S, int SubIndent) {
    IndentLevel += SubIndent;
    if (S && isa<Expr>(S)) {
      // If this is an expr used in a stmt context, indent and newline it.
      Indent();
      Visit(S);
      OS << ";" << NL;
    } else if (S) {
      Visit(S);
    } else {
      Indent() << "<<<NULL STATEMENT>>>" << NL;
    }
    IndentLevel -= SubIndent;
  }

  void PrintInitStmt(Stmt *S, unsigned PrefixWidth) {
    IndentLevel += (PrefixWidth + 1) / 2;
    if (auto *DS = dyn_cast<DeclStmt>(S))
      PrintRawDeclStmt(DS);
    else
      PrintExpr(cast<Expr>(S));
    OS << "; ";
    IndentLevel -= (PrefixWidth + 1) / 2;
  }

  void PrintControlledStmt(Stmt *S) {
    if (auto *CS = dyn_cast<CompoundStmt>(S)) {
      OS << " ";
      PrintRawCompoundStmt(CS);
      OS << NL;
    } else {
      OS << NL;
      PrintStmt(S);
    }
  }

  void PrintRawCompoundStmt(CompoundStmt *S);
  void PrintRawDecl(Decl *D);
  void PrintRawDeclStmt(const DeclStmt *S);
  void PrintRawIfStmt(IfStmt *If);
  void PrintCallArgs(CallExpr *E);
  void PrintRawSEHExceptHandler(SEHExceptStmt *S);
  void PrintRawSEHFinallyStmt(SEHFinallyStmt *S);
  void PrintFPPragmas(CompoundStmt *S);

  void PrintExpr(Expr *E) {
    if (E)
      Visit(E);
    else
      OS << "<null expr>";
  }

  llvm::raw_ostream &Indent(int Delta = 0) {
    for (int i = 0, e = IndentLevel + Delta; i < e; ++i)
      OS << "  ";
    return OS;
  }

  void Visit(Stmt *S) {
    if (Helper && Helper->handledStmt(S, OS))
      return;
    else
      StmtVisitor<StmtPrinter>::Visit(S);
  }

  void VisitStmt(Stmt *) { Indent() << "<<unknown stmt type>>" << NL; }

  void VisitExpr(Expr *) { OS << "<<unknown expr type>>"; }

  // Declared only for Visit* defined in this file; other stmt kinds use
  // StmtVisitor<StmtPrinter> dispatch to VisitStmt / VisitExpr.

  void VisitAddrLabelExpr(AddrLabelExpr *Node);
  void VisitArrayInitIndexExpr(ArrayInitIndexExpr *Node);
  void VisitArrayInitLoopExpr(ArrayInitLoopExpr *Node);
  void VisitArraySubscriptExpr(ArraySubscriptExpr *Node);
  void VisitAtomicExpr(AtomicExpr *Node);
  void VisitAttributedStmt(AttributedStmt *Node);
  void VisitBinaryConditionalOperator(BinaryConditionalOperator *Node);
  void VisitBinaryOperator(BinaryOperator *Node);
  void VisitBreakStmt(BreakStmt *Node);
  void VisitCallExpr(CallExpr *Call);
  void VisitCaseStmt(CaseStmt *Node);
  void VisitCharacterLiteral(CharacterLiteral *Node);
  void VisitChooseExpr(ChooseExpr *Node);
  void VisitCompoundLiteralExpr(CompoundLiteralExpr *Node);
  void VisitCompoundStmt(CompoundStmt *Node);
  void VisitConditionalOperator(ConditionalOperator *Node);
  void VisitConstantExpr(ConstantExpr *Node);
  void VisitContinueStmt(ContinueStmt *Node);
  void VisitConvertVectorExpr(ConvertVectorExpr *Node);
  void VisitCStyleCastExpr(CStyleCastExpr *Node);
  void VisitDeclRefExpr(DeclRefExpr *Node);
  void VisitDeclStmt(DeclStmt *Node);
  void VisitDefaultStmt(DefaultStmt *Node);
  void VisitDesignatedInitExpr(DesignatedInitExpr *Node);
  void VisitDesignatedInitUpdateExpr(DesignatedInitUpdateExpr *Node);
  void VisitDoStmt(DoStmt *Node);
  void VisitExtVectorElementExpr(ExtVectorElementExpr *Node);
  void VisitFixedPointLiteral(FixedPointLiteral *Node);
  void VisitFloatingLiteral(FloatingLiteral *Node);
  void VisitForStmt(ForStmt *Node);
  void VisitGCCAsmStmt(GCCAsmStmt *Node);
  void VisitGenericSelectionExpr(GenericSelectionExpr *Node);
  void VisitGotoStmt(GotoStmt *Node);
  void VisitIfStmt(IfStmt *If);
  void VisitImaginaryLiteral(ImaginaryLiteral *Node);
  void VisitImplicitCastExpr(ImplicitCastExpr *Node);
  void VisitImplicitValueInitExpr(ImplicitValueInitExpr *Node);
  void VisitIndirectGotoStmt(IndirectGotoStmt *Node);
  void VisitInitListExpr(InitListExpr *Node);
  void VisitIntegerLiteral(IntegerLiteral *Node);
  void VisitLabelStmt(LabelStmt *Node);
  void VisitMatrixSubscriptExpr(MatrixSubscriptExpr *Node);
  void VisitMemberExpr(MemberExpr *Node);
  void VisitMSAsmStmt(MSAsmStmt *Node);
  void VisitNoInitExpr(NoInitExpr *Node);
  void VisitNullStmt(NullStmt *Node);
  void VisitOffsetOfExpr(OffsetOfExpr *Node);
  void VisitOpaqueValueExpr(OpaqueValueExpr *Node);
  void VisitParenExpr(ParenExpr *Node);
  void VisitParenListExpr(ParenListExpr *Node);
  void VisitPredefinedExpr(PredefinedExpr *Node);
  void VisitPseudoObjectExpr(PseudoObjectExpr *Node);
  void VisitRecoveryExpr(RecoveryExpr *Node);
  void VisitReturnStmt(ReturnStmt *Node);
  void VisitSEHExceptStmt(SEHExceptStmt *Node);
  void VisitSEHFinallyStmt(SEHFinallyStmt *Node);
  void VisitSEHLeaveStmt(SEHLeaveStmt *Node);
  void VisitSEHTryStmt(SEHTryStmt *Node);
  void VisitShuffleVectorExpr(ShuffleVectorExpr *Node);
  void VisitSourceLocExpr(SourceLocExpr *Node);
  void VisitStmtExpr(StmtExpr *E);
  void VisitStringLiteral(StringLiteral *Str);
  void VisitSwitchStmt(SwitchStmt *Node);
  void VisitTypoExpr(TypoExpr *);
  void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *Node);
  void VisitUnaryOperator(UnaryOperator *Node);
  void VisitVAArgExpr(VAArgExpr *Node);
  void VisitWhileStmt(WhileStmt *Node);
};

} // namespace

void StmtPrinter::PrintRawCompoundStmt(CompoundStmt *Node) {
  assert(Node && "Compound statement cannot be null");
  OS << "{" << NL;
  PrintFPPragmas(Node);
  for (auto *I : Node->body())
    PrintStmt(I);

  Indent() << "}";
}

void StmtPrinter::PrintFPPragmas(CompoundStmt *S) {
  if (!S->hasStoredFPFeatures())
    return;
  FPOptionsOverride FPO = S->getStoredFPFeatures();
  bool FEnvAccess = false;
  if (FPO.hasAllowFEnvAccessOverride()) {
    FEnvAccess = FPO.getAllowFEnvAccessOverride();
    Indent() << "#pragma STDC FENV_ACCESS " << (FEnvAccess ? "ON" : "OFF")
             << NL;
  }
  if (FPO.hasSpecifiedExceptionModeOverride()) {
    LangOptions::FPExceptionModeKind EM =
        FPO.getSpecifiedExceptionModeOverride();
    if (!FEnvAccess || EM != LangOptions::FPE_Strict) {
      Indent() << "#pragma neverc fp exceptions(";
      switch (FPO.getSpecifiedExceptionModeOverride()) {
      default:
        break;
      case LangOptions::FPE_Ignore:
        OS << "ignore";
        break;
      case LangOptions::FPE_MayTrap:
        OS << "maytrap";
        break;
      case LangOptions::FPE_Strict:
        OS << "strict";
        break;
      }
      OS << ")\n";
    }
  }
  if (FPO.hasConstRoundingModeOverride()) {
    LangOptions::RoundingMode RM = FPO.getConstRoundingModeOverride();
    Indent() << "#pragma STDC FENV_ROUND ";
    switch (RM) {
    case llvm::RoundingMode::TowardZero:
      OS << "FE_TOWARDZERO";
      break;
    case llvm::RoundingMode::NearestTiesToEven:
      OS << "FE_TONEAREST";
      break;
    case llvm::RoundingMode::TowardPositive:
      OS << "FE_UPWARD";
      break;
    case llvm::RoundingMode::TowardNegative:
      OS << "FE_DOWNWARD";
      break;
    case llvm::RoundingMode::NearestTiesToAway:
      OS << "FE_TONEARESTFROMZERO";
      break;
    case llvm::RoundingMode::Dynamic:
      OS << "FE_DYNAMIC";
      break;
    default:
      llvm_unreachable("Invalid rounding mode");
    }
    OS << NL;
  }
}

void StmtPrinter::PrintRawDecl(Decl *D) { D->print(OS, Policy, IndentLevel); }

void StmtPrinter::PrintRawDeclStmt(const DeclStmt *S) {
  llvm::SmallVector<Decl *, 2> Decls(S->decls());
  Decl::printGroup(Decls.data(), Decls.size(), OS, Policy, IndentLevel);
}

void StmtPrinter::VisitNullStmt(NullStmt *Node) { Indent() << ";" << NL; }

void StmtPrinter::VisitDeclStmt(DeclStmt *Node) {
  Indent();
  PrintRawDeclStmt(Node);
  OS << ";" << NL;
}

void StmtPrinter::VisitCompoundStmt(CompoundStmt *Node) {
  Indent();
  PrintRawCompoundStmt(Node);
  OS << "" << NL;
}

void StmtPrinter::VisitCaseStmt(CaseStmt *Node) {
  Indent(-1) << tok::getKeywordSpelling(tok::kw_case) << ' ';
  PrintExpr(Node->getLHS());
  if (Node->getRHS()) {
    OS << " ... ";
    PrintExpr(Node->getRHS());
  }
  OS << ":" << NL;

  PrintStmt(Node->getSubStmt(), 0);
}

void StmtPrinter::VisitDefaultStmt(DefaultStmt *Node) {
  Indent(-1) << tok::getKeywordSpelling(tok::kw_default) << ':' << NL;
  PrintStmt(Node->getSubStmt(), 0);
}

void StmtPrinter::VisitLabelStmt(LabelStmt *Node) {
  Indent(-1) << Node->getName() << ":" << NL;
  PrintStmt(Node->getSubStmt(), 0);
}

void StmtPrinter::VisitAttributedStmt(AttributedStmt *Node) {
  for (const auto *Attr : Node->getAttrs()) {
    Attr->printPretty(OS, Policy);
  }

  PrintStmt(Node->getSubStmt(), 0);
}

void StmtPrinter::PrintRawIfStmt(IfStmt *If) {
  OS << tok::getKeywordSpelling(tok::kw_if) << " (";
  if (If->getInit())
    PrintInitStmt(If->getInit(), 4);
  if (const DeclStmt *DS = If->getConditionVariableDeclStmt())
    PrintRawDeclStmt(DS);
  else
    PrintExpr(If->getCond());
  OS << ')';

  if (auto *CS = dyn_cast<CompoundStmt>(If->getThen())) {
    OS << ' ';
    PrintRawCompoundStmt(CS);
    OS << (If->getElse() ? " " : NL);
  } else {
    OS << NL;
    PrintStmt(If->getThen());
    if (If->getElse())
      Indent();
  }

  if (Stmt *Else = If->getElse()) {
    OS << tok::getKeywordSpelling(tok::kw_else);

    if (auto *CS = dyn_cast<CompoundStmt>(Else)) {
      OS << ' ';
      PrintRawCompoundStmt(CS);
      OS << NL;
    } else if (auto *ElseIf = dyn_cast<IfStmt>(Else)) {
      OS << ' ';
      PrintRawIfStmt(ElseIf);
    } else {
      OS << NL;
      PrintStmt(If->getElse());
    }
  }
}

void StmtPrinter::VisitIfStmt(IfStmt *If) {
  Indent();
  PrintRawIfStmt(If);
}

void StmtPrinter::VisitSwitchStmt(SwitchStmt *Node) {
  Indent() << tok::getKeywordSpelling(tok::kw_switch) << " (";
  if (Node->getInit())
    PrintInitStmt(Node->getInit(), 8);
  if (const DeclStmt *DS = Node->getConditionVariableDeclStmt())
    PrintRawDeclStmt(DS);
  else
    PrintExpr(Node->getCond());
  OS << ")";
  PrintControlledStmt(Node->getBody());
}

void StmtPrinter::VisitWhileStmt(WhileStmt *Node) {
  Indent() << tok::getKeywordSpelling(tok::kw_while) << " (";
  if (const DeclStmt *DS = Node->getConditionVariableDeclStmt())
    PrintRawDeclStmt(DS);
  else
    PrintExpr(Node->getCond());
  OS << ")" << NL;
  PrintStmt(Node->getBody());
}

void StmtPrinter::VisitDoStmt(DoStmt *Node) {
  Indent() << tok::getKeywordSpelling(tok::kw_do) << ' ';
  if (auto *CS = dyn_cast<CompoundStmt>(Node->getBody())) {
    PrintRawCompoundStmt(CS);
    OS << " ";
  } else {
    OS << NL;
    PrintStmt(Node->getBody());
    Indent();
  }

  OS << tok::getKeywordSpelling(tok::kw_while) << " (";
  PrintExpr(Node->getCond());
  OS << ");" << NL;
}

void StmtPrinter::VisitForStmt(ForStmt *Node) {
  Indent() << tok::getKeywordSpelling(tok::kw_for) << " (";
  if (Node->getInit())
    PrintInitStmt(Node->getInit(), 5);
  else
    OS << (Node->getCond() ? "; " : ";");
  if (const DeclStmt *DS = Node->getConditionVariableDeclStmt())
    PrintRawDeclStmt(DS);
  else if (Node->getCond())
    PrintExpr(Node->getCond());
  OS << ";";
  if (Node->getInc()) {
    OS << " ";
    PrintExpr(Node->getInc());
  }
  OS << ")";
  PrintControlledStmt(Node->getBody());
}

void StmtPrinter::VisitGotoStmt(GotoStmt *Node) {
  Indent() << tok::getKeywordSpelling(tok::kw_goto) << ' '
           << Node->getLabel()->getName() << ";";
  OS << NL;
}

void StmtPrinter::VisitIndirectGotoStmt(IndirectGotoStmt *Node) {
  Indent() << tok::getKeywordSpelling(tok::kw_goto) << " *";
  PrintExpr(Node->getTarget());
  OS << ";";
  OS << NL;
}

void StmtPrinter::VisitContinueStmt(ContinueStmt *Node) {
  Indent() << tok::getKeywordSpelling(tok::kw_continue) << ';';
  OS << NL;
}

void StmtPrinter::VisitBreakStmt(BreakStmt *Node) {
  Indent() << tok::getKeywordSpelling(tok::kw_break) << ';';
  OS << NL;
}

void StmtPrinter::VisitReturnStmt(ReturnStmt *Node) {
  Indent() << tok::getKeywordSpelling(tok::kw_return);
  if (Node->getRetValue()) {
    OS << " ";
    PrintExpr(Node->getRetValue());
  }
  OS << ";";
  OS << NL;
}

void StmtPrinter::VisitGCCAsmStmt(GCCAsmStmt *Node) {
  Indent() << tok::getKeywordSpelling(tok::kw_asm) << ' ';

  if (Node->isVolatile())
    OS << tok::getKeywordSpelling(tok::kw_volatile) << ' ';

  if (Node->isAsmGoto())
    OS << tok::getKeywordSpelling(tok::kw_goto) << ' ';

  OS << "(";
  VisitStringLiteral(Node->getAsmString());

  // Outputs
  if (Node->getNumOutputs() != 0 || Node->getNumInputs() != 0 ||
      Node->getNumClobbers() != 0 || Node->getNumLabels() != 0)
    OS << " : ";

  for (unsigned i = 0, e = Node->getNumOutputs(); i != e; ++i) {
    if (i != 0)
      OS << ", ";

    if (!Node->getOutputName(i).empty()) {
      OS << '[';
      OS << Node->getOutputName(i);
      OS << "] ";
    }

    VisitStringLiteral(Node->getOutputConstraintLiteral(i));
    OS << " (";
    Visit(Node->getOutputExpr(i));
    OS << ")";
  }

  // Inputs
  if (Node->getNumInputs() != 0 || Node->getNumClobbers() != 0 ||
      Node->getNumLabels() != 0)
    OS << " : ";

  for (unsigned i = 0, e = Node->getNumInputs(); i != e; ++i) {
    if (i != 0)
      OS << ", ";

    if (!Node->getInputName(i).empty()) {
      OS << '[';
      OS << Node->getInputName(i);
      OS << "] ";
    }

    VisitStringLiteral(Node->getInputConstraintLiteral(i));
    OS << " (";
    Visit(Node->getInputExpr(i));
    OS << ")";
  }

  // Clobbers
  if (Node->getNumClobbers() != 0 || Node->getNumLabels())
    OS << " : ";

  for (unsigned i = 0, e = Node->getNumClobbers(); i != e; ++i) {
    if (i != 0)
      OS << ", ";

    VisitStringLiteral(Node->getClobberStringLiteral(i));
  }

  // Labels
  if (Node->getNumLabels() != 0)
    OS << " : ";

  for (unsigned i = 0, e = Node->getNumLabels(); i != e; ++i) {
    if (i != 0)
      OS << ", ";
    OS << Node->getLabelName(i);
  }

  OS << ");";
  OS << NL;
}

void StmtPrinter::VisitMSAsmStmt(MSAsmStmt *Node) {
  Indent() << "__asm ";
  if (Node->hasBraces())
    OS << "{" << NL;
  OS << Node->getAsmString() << NL;
  if (Node->hasBraces())
    Indent() << "}" << NL;
}

void StmtPrinter::VisitSEHTryStmt(SEHTryStmt *Node) {
  Indent() << "__try ";
  PrintRawCompoundStmt(Node->getTryBlock());
  SEHExceptStmt *E = Node->getExceptHandler();
  SEHFinallyStmt *F = Node->getFinallyHandler();
  if (E)
    PrintRawSEHExceptHandler(E);
  else {
    assert(F && "Must have a finally block...");
    PrintRawSEHFinallyStmt(F);
  }
  OS << NL;
}

void StmtPrinter::PrintRawSEHFinallyStmt(SEHFinallyStmt *Node) {
  OS << "__finally ";
  PrintRawCompoundStmt(Node->getBlock());
  OS << NL;
}

void StmtPrinter::PrintRawSEHExceptHandler(SEHExceptStmt *Node) {
  OS << "__except (";
  VisitExpr(Node->getFilterExpr());
  OS << ")" << NL;
  PrintRawCompoundStmt(Node->getBlock());
  OS << NL;
}

void StmtPrinter::VisitSEHExceptStmt(SEHExceptStmt *Node) {
  Indent();
  PrintRawSEHExceptHandler(Node);
  OS << NL;
}

void StmtPrinter::VisitSEHFinallyStmt(SEHFinallyStmt *Node) {
  Indent();
  PrintRawSEHFinallyStmt(Node);
  OS << NL;
}

void StmtPrinter::VisitSEHLeaveStmt(SEHLeaveStmt *Node) {
  Indent() << "__leave;";
  OS << NL;
}

void StmtPrinter::VisitSourceLocExpr(SourceLocExpr *Node) {
  OS << Node->getBuiltinStr() << "()";
}

void StmtPrinter::VisitConstantExpr(ConstantExpr *Node) {
  PrintExpr(Node->getSubExpr());
}

void StmtPrinter::VisitDeclRefExpr(DeclRefExpr *Node) {
  Node->getNameInfo().printName(OS, Policy);
}

void StmtPrinter::VisitPredefinedExpr(PredefinedExpr *Node) {
  OS << PredefinedExpr::getIdentKindName(Node->getIdentKind());
}

void StmtPrinter::VisitCharacterLiteral(CharacterLiteral *Node) {
  CharacterLiteral::print(Node->getValue(), Node->getKind(), OS);
}

void StmtPrinter::VisitIntegerLiteral(IntegerLiteral *Node) {
  bool isSigned = Node->getType()->isSignedIntegerType();
  OS << toString(Node->getValue(), 10, isSigned);

  if (isa<BitIntType>(Node->getType())) {
    OS << (isSigned ? "wb" : "uwb");
    return;
  }

  // Emit suffixes.  Integer literals are always a builtin integer type.
  switch (Node->getType()->castAs<BuiltinType>()->getKind()) {
  default:
    llvm_unreachable("Unexpected type for integer literal!");
  case BuiltinType::Char_S:
  case BuiltinType::Char_U:
    OS << "i8";
    break;
  case BuiltinType::UChar:
    OS << "Ui8";
    break;
  case BuiltinType::SChar:
    OS << "i8";
    break;
  case BuiltinType::Short:
    OS << "i16";
    break;
  case BuiltinType::UShort:
    OS << "Ui16";
    break;
  case BuiltinType::Int:
    break; // no suffix.
  case BuiltinType::UInt:
    OS << 'U';
    break;
  case BuiltinType::Long:
    OS << 'L';
    break;
  case BuiltinType::ULong:
    OS << "UL";
    break;
  case BuiltinType::LongLong:
    OS << "LL";
    break;
  case BuiltinType::ULongLong:
    OS << "ULL";
    break;
  case BuiltinType::Int128:
    break; // no suffix.
  case BuiltinType::UInt128:
    break; // no suffix.
  case BuiltinType::WChar_S:
  case BuiltinType::WChar_U:
    break; // no suffix
  }
}

void StmtPrinter::VisitFixedPointLiteral(FixedPointLiteral *Node) {
  OS << Node->getValueAsString(/*Radix=*/10);

  switch (Node->getType()->castAs<BuiltinType>()->getKind()) {
  default:
    llvm_unreachable("Unexpected type for fixed point literal!");
  case BuiltinType::ShortFract:
    OS << "hr";
    break;
  case BuiltinType::ShortAccum:
    OS << "hk";
    break;
  case BuiltinType::UShortFract:
    OS << "uhr";
    break;
  case BuiltinType::UShortAccum:
    OS << "uhk";
    break;
  case BuiltinType::Fract:
    OS << "r";
    break;
  case BuiltinType::Accum:
    OS << "k";
    break;
  case BuiltinType::UFract:
    OS << "ur";
    break;
  case BuiltinType::UAccum:
    OS << "uk";
    break;
  case BuiltinType::LongFract:
    OS << "lr";
    break;
  case BuiltinType::LongAccum:
    OS << "lk";
    break;
  case BuiltinType::ULongFract:
    OS << "ulr";
    break;
  case BuiltinType::ULongAccum:
    OS << "ulk";
    break;
  }
}

namespace {
void printFloatingLiteral(llvm::raw_ostream &OS, FloatingLiteral *Node,
                          bool PrintSuffix) {
  llvm::SmallString<16> Str;
  Node->getValue().toString(Str);
  OS << Str;
  if (Str.find_first_not_of("-0123456789") == llvm::StringRef::npos)
    OS << '.'; // Trailing dot in order to separate from ints.

  if (!PrintSuffix)
    return;

  // Emit suffixes.  Float literals are always a builtin float type.
  switch (Node->getType()->castAs<BuiltinType>()->getKind()) {
  default:
    llvm_unreachable("Unexpected type for float literal!");
  case BuiltinType::Half:
    break;
  case BuiltinType::Double:
    break; // no suffix.
  case BuiltinType::Float16:
    OS << "F16";
    break;
  case BuiltinType::Float:
    OS << 'F';
    break;
  case BuiltinType::LongDouble:
    OS << 'L';
    break;
  case BuiltinType::Float128:
    OS << 'Q';
    break;
  }
}
} // namespace

void StmtPrinter::VisitFloatingLiteral(FloatingLiteral *Node) {
  printFloatingLiteral(OS, Node, /*PrintSuffix=*/true);
}

void StmtPrinter::VisitImaginaryLiteral(ImaginaryLiteral *Node) {
  PrintExpr(Node->getSubExpr());
  OS << "i";
}

void StmtPrinter::VisitStringLiteral(StringLiteral *Str) {
  Str->outputString(OS);
}

void StmtPrinter::VisitParenExpr(ParenExpr *Node) {
  OS << "(";
  PrintExpr(Node->getSubExpr());
  OS << ")";
}

void StmtPrinter::VisitUnaryOperator(UnaryOperator *Node) {
  if (!Node->isPostfix()) {
    OS << UnaryOperator::getOpcodeStr(Node->getOpcode());

    // Print a space if this is an "identifier operator" like __real, or if
    // it might be concatenated incorrectly like '+'.
    switch (Node->getOpcode()) {
    default:
      break;
    case UO_Real:
    case UO_Imag:
    case UO_Extension:
      OS << ' ';
      break;
    case UO_Plus:
    case UO_Minus:
      if (isa<UnaryOperator>(Node->getSubExpr()))
        OS << ' ';
      break;
    }
  }
  PrintExpr(Node->getSubExpr());

  if (Node->isPostfix())
    OS << UnaryOperator::getOpcodeStr(Node->getOpcode());
}

void StmtPrinter::VisitOffsetOfExpr(OffsetOfExpr *Node) {
  OS << "__builtin_offsetof(";
  Node->getTypeSourceInfo()->getType().print(OS, Policy);
  OS << ", ";
  bool PrintedSomething = false;
  for (unsigned i = 0, n = Node->getNumComponents(); i < n; ++i) {
    OffsetOfNode ON = Node->getComponent(i);
    if (ON.getKind() == OffsetOfNode::Array) {
      // Array node
      OS << "[";
      PrintExpr(Node->getIndexExpr(ON.getArrayExprIndex()));
      OS << "]";
      PrintedSomething = true;
      continue;
    }

    // Field or identifier node.
    IdentifierInfo *Id = ON.getFieldName();
    if (!Id)
      continue;

    if (PrintedSomething)
      OS << ".";
    else
      PrintedSomething = true;
    OS << Id->getName();
  }
  OS << ")";
}

void StmtPrinter::VisitUnaryExprOrTypeTraitExpr(
    UnaryExprOrTypeTraitExpr *Node) {
  const char *Spelling = getTraitSpelling(Node->getKind());
  if (Node->getKind() == UETT_AlignOf)
    Spelling = Policy.UnderscoreAlignof
                   ? tok::getKeywordSpelling(tok::kw__Alignof)
                   : tok::getKeywordSpelling(tok::kw___alignof);

  OS << Spelling;

  if (Node->isArgumentType()) {
    OS << '(';
    Node->getArgumentType().print(OS, Policy);
    OS << ')';
  } else {
    OS << " ";
    PrintExpr(Node->getArgumentExpr());
  }
}

void StmtPrinter::VisitGenericSelectionExpr(GenericSelectionExpr *Node) {
  OS << tok::getKeywordSpelling(tok::kw__Generic) << '(';
  if (Node->isExprPredicate())
    PrintExpr(Node->getControllingExpr());
  else
    Node->getControllingType()->getType().print(OS, Policy);

  for (const GenericSelectionExpr::Association &Assoc : Node->associations()) {
    OS << ", ";
    QualType T = Assoc.getType();
    if (T.isNull())
      OS << tok::getKeywordSpelling(tok::kw_default);
    else
      T.print(OS, Policy);
    OS << ": ";
    PrintExpr(Assoc.getAssociationExpr());
  }
  OS << ")";
}

void StmtPrinter::VisitArraySubscriptExpr(ArraySubscriptExpr *Node) {
  PrintExpr(Node->getLHS());
  OS << "[";
  PrintExpr(Node->getRHS());
  OS << "]";
}

void StmtPrinter::VisitMatrixSubscriptExpr(MatrixSubscriptExpr *Node) {
  PrintExpr(Node->getBase());
  OS << "[";
  PrintExpr(Node->getRowIdx());
  OS << "]";
  OS << "[";
  PrintExpr(Node->getColumnIdx());
  OS << "]";
}

void StmtPrinter::PrintCallArgs(CallExpr *Call) {
  for (unsigned i = 0, e = Call->getNumArgs(); i != e; ++i) {
    if (i)
      OS << ", ";
    PrintExpr(Call->getArg(i));
  }
}

void StmtPrinter::VisitCallExpr(CallExpr *Call) {
  PrintExpr(Call->getCallee());
  OS << "(";
  PrintCallArgs(Call);
  OS << ")";
}

void StmtPrinter::VisitMemberExpr(MemberExpr *Node) {
  PrintExpr(Node->getBase());

  auto *ParentMember = dyn_cast<MemberExpr>(Node->getBase());
  FieldDecl *ParentDecl =
      ParentMember ? dyn_cast<FieldDecl>(ParentMember->getMemberDecl())
                   : nullptr;

  if (!ParentDecl || !ParentDecl->isAnonymousStructOrUnion())
    OS << (Node->isArrow() ? "->" : ".");

  if (auto *FD = dyn_cast<FieldDecl>(Node->getMemberDecl()))
    if (FD->isAnonymousStructOrUnion())
      return;

  OS << Node->getMemberNameInfo();
}

void StmtPrinter::VisitExtVectorElementExpr(ExtVectorElementExpr *Node) {
  PrintExpr(Node->getBase());
  OS << ".";
  OS << Node->getAccessor().getName();
}

void StmtPrinter::VisitCStyleCastExpr(CStyleCastExpr *Node) {
  OS << '(';
  Node->getTypeAsWritten().print(OS, Policy);
  OS << ')';
  PrintExpr(Node->getSubExpr());
}

void StmtPrinter::VisitCompoundLiteralExpr(CompoundLiteralExpr *Node) {
  OS << '(';
  Node->getType().print(OS, Policy);
  OS << ')';
  PrintExpr(Node->getInitializer());
}

void StmtPrinter::VisitImplicitCastExpr(ImplicitCastExpr *Node) {
  // No need to print anything, simply forward to the subexpression.
  PrintExpr(Node->getSubExpr());
}

void StmtPrinter::VisitBinaryOperator(BinaryOperator *Node) {
  PrintExpr(Node->getLHS());
  OS << " " << BinaryOperator::getOpcodeStr(Node->getOpcode()) << " ";
  PrintExpr(Node->getRHS());
}

void StmtPrinter::VisitConditionalOperator(ConditionalOperator *Node) {
  PrintExpr(Node->getCond());
  OS << " ? ";
  PrintExpr(Node->getLHS());
  OS << " : ";
  PrintExpr(Node->getRHS());
}

// GNU extensions.

void StmtPrinter::VisitBinaryConditionalOperator(
    BinaryConditionalOperator *Node) {
  PrintExpr(Node->getCommon());
  OS << " ?: ";
  PrintExpr(Node->getFalseExpr());
}

void StmtPrinter::VisitAddrLabelExpr(AddrLabelExpr *Node) {
  OS << "&&" << Node->getLabel()->getName();
}

void StmtPrinter::VisitStmtExpr(StmtExpr *E) {
  OS << "(";
  PrintRawCompoundStmt(E->getSubStmt());
  OS << ")";
}

void StmtPrinter::VisitChooseExpr(ChooseExpr *Node) {
  OS << "__builtin_choose_expr(";
  PrintExpr(Node->getCond());
  OS << ", ";
  PrintExpr(Node->getLHS());
  OS << ", ";
  PrintExpr(Node->getRHS());
  OS << ")";
}

void StmtPrinter::VisitShuffleVectorExpr(ShuffleVectorExpr *Node) {
  OS << "__builtin_shufflevector(";
  for (unsigned i = 0, e = Node->getNumSubExprs(); i != e; ++i) {
    if (i)
      OS << ", ";
    PrintExpr(Node->getExpr(i));
  }
  OS << ")";
}

void StmtPrinter::VisitConvertVectorExpr(ConvertVectorExpr *Node) {
  OS << "__builtin_convertvector(";
  PrintExpr(Node->getSrcExpr());
  OS << ", ";
  Node->getType().print(OS, Policy);
  OS << ")";
}

void StmtPrinter::VisitInitListExpr(InitListExpr *Node) {
  if (Node->getSyntacticForm()) {
    Visit(Node->getSyntacticForm());
    return;
  }

  OS << "{";
  for (unsigned i = 0, e = Node->getNumInits(); i != e; ++i) {
    if (i)
      OS << ", ";
    if (Node->getInit(i))
      PrintExpr(Node->getInit(i));
    else
      OS << "{}";
  }
  OS << "}";
}

void StmtPrinter::VisitArrayInitLoopExpr(ArrayInitLoopExpr *Node) {
  // There's no way to express this expression in any of our supported
  // languages, so just emit something terse and (hopefully) clear.
  OS << "{";
  PrintExpr(Node->getSubExpr());
  OS << "}";
}

void StmtPrinter::VisitArrayInitIndexExpr(ArrayInitIndexExpr *Node) {
  OS << "*";
}

void StmtPrinter::VisitParenListExpr(ParenListExpr *Node) {
  OS << "(";
  for (unsigned i = 0, e = Node->getNumExprs(); i != e; ++i) {
    if (i)
      OS << ", ";
    PrintExpr(Node->getExpr(i));
  }
  OS << ")";
}

void StmtPrinter::VisitDesignatedInitExpr(DesignatedInitExpr *Node) {
  bool NeedsEquals = true;
  for (const DesignatedInitExpr::Designator &D : Node->designators()) {
    if (D.isFieldDesignator()) {
      if (D.getDotLoc().isInvalid()) {
        if (const IdentifierInfo *II = D.getFieldName()) {
          OS << II->getName() << ":";
          NeedsEquals = false;
        }
      } else {
        OS << "." << D.getFieldName()->getName();
      }
    } else {
      OS << "[";
      if (D.isArrayDesignator()) {
        PrintExpr(Node->getArrayIndex(D));
      } else {
        PrintExpr(Node->getArrayRangeStart(D));
        OS << " ... ";
        PrintExpr(Node->getArrayRangeEnd(D));
      }
      OS << "]";
    }
  }

  if (NeedsEquals)
    OS << " = ";
  else
    OS << " ";
  PrintExpr(Node->getInit());
}

void StmtPrinter::VisitDesignatedInitUpdateExpr(
    DesignatedInitUpdateExpr *Node) {
  OS << "{";
  OS << "/*base*/";
  PrintExpr(Node->getBase());
  OS << ", ";

  OS << "/*updater*/";
  PrintExpr(Node->getUpdater());
  OS << "}";
}

void StmtPrinter::VisitNoInitExpr(NoInitExpr *Node) { OS << "/*no init*/"; }

void StmtPrinter::VisitImplicitValueInitExpr(ImplicitValueInitExpr *Node) {
  OS << "/*implicit*/(";
  Node->getType().print(OS, Policy);
  OS << ')';
  if (Node->getType()->isRecordType())
    OS << "{}";
  else
    OS << 0;
}

void StmtPrinter::VisitVAArgExpr(VAArgExpr *Node) {
  OS << "__builtin_va_arg(";
  PrintExpr(Node->getSubExpr());
  OS << ", ";
  Node->getType().print(OS, Policy);
  OS << ")";
}

void StmtPrinter::VisitPseudoObjectExpr(PseudoObjectExpr *Node) {
  PrintExpr(Node->getSyntacticForm());
}

void StmtPrinter::VisitAtomicExpr(AtomicExpr *Node) {
  const char *Name = nullptr;
  switch (Node->getOp()) {
#define BUILTIN(ID, TYPE, ATTRS)
#define ATOMIC_BUILTIN(ID, TYPE, ATTRS)                                        \
  case AtomicExpr::AO##ID:                                                     \
    Name = #ID "(";                                                            \
    break;
#include "neverc/Foundation/Builtin/Builtins.def"
  }
  OS << Name;

  // AtomicExpr stores its subexpressions in a permuted order.
  PrintExpr(Node->getPtr());
  if (Node->getOp() != AtomicExpr::AO__c11_atomic_load &&
      Node->getOp() != AtomicExpr::AO__atomic_load_n &&
      Node->getOp() != AtomicExpr::AO__scoped_atomic_load_n) {
    OS << ", ";
    PrintExpr(Node->getVal1());
  }
  if (Node->getOp() == AtomicExpr::AO__atomic_exchange || Node->isCmpXChg()) {
    OS << ", ";
    PrintExpr(Node->getVal2());
  }
  if (Node->getOp() == AtomicExpr::AO__atomic_compare_exchange ||
      Node->getOp() == AtomicExpr::AO__atomic_compare_exchange_n) {
    OS << ", ";
    PrintExpr(Node->getWeak());
  }
  if (Node->getOp() != AtomicExpr::AO__c11_atomic_init) {
    OS << ", ";
    PrintExpr(Node->getOrder());
  }
  if (Node->isCmpXChg()) {
    OS << ", ";
    PrintExpr(Node->getOrderFail());
  }
  OS << ")";
}

void StmtPrinter::VisitOpaqueValueExpr(OpaqueValueExpr *Node) {
  PrintExpr(Node->getSourceExpr());
}

void StmtPrinter::VisitTypoExpr(TypoExpr *) { OS << "<<typo>>"; }

void StmtPrinter::VisitRecoveryExpr(RecoveryExpr *Node) {
  OS << "<recovery-expr>(";
  const char *Sep = "";
  for (Expr *E : Node->subExpressions()) {
    OS << Sep;
    PrintExpr(E);
    Sep = ", ";
  }
  OS << ')';
}

void Stmt::dumpPretty(const TreeContext &Context) const {
  printPretty(llvm::errs(), nullptr, PrintingPolicy{Context.getLangOpts()});
}

void Stmt::printPretty(llvm::raw_ostream &Out, PrinterHelper *Helper,
                       const PrintingPolicy &Policy, unsigned Indentation,
                       llvm::StringRef NL) const {
  StmtPrinter P(Out, Helper, Policy, Indentation, NL);
  P.Visit(const_cast<Stmt *>(this));
}

void Stmt::printPrettyControlled(llvm::raw_ostream &Out, PrinterHelper *Helper,
                                 const PrintingPolicy &Policy,
                                 unsigned Indentation,
                                 llvm::StringRef NL) const {
  StmtPrinter P(Out, Helper, Policy, Indentation, NL);
  P.PrintControlledStmt(const_cast<Stmt *>(this));
}

// Implement virtual destructor.
PrinterHelper::~PrinterHelper() = default;
