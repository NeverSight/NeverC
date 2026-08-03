#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Tree/Core/PrettyPrinter.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace neverc;

int DeclarationName::compare(DeclarationName LHS, DeclarationName RHS) {
  if (LLVM_LIKELY(LHS.Ptr == RHS.Ptr))
    return 0;

  const IdentifierInfo *LII = LHS.getAsIdentifierInfo();
  const IdentifierInfo *RII = RHS.getAsIdentifierInfo();
  if (!LII)
    return RII ? -1 : 0;
  if (!RII)
    return 1;
  return LII->getName().compare(RII->getName());
}

void DeclarationName::print(llvm::raw_ostream &OS,
                            const PrintingPolicy &) const {
  switch (getNameKind()) {
  case Identifier:
    if (const IdentifierInfo *II = getAsIdentifierInfo())
      OS << II->getName();
    break;
  case CXXConstructorName:
    OS << "<constructor>";
    break;
  case CXXDestructorName:
    OS << "<destructor>";
    break;
  case CXXConversionFunctionName:
    OS << "<conversion>";
    break;
  case CXXOperatorName: {
    const char *Sp = getOperatorSpelling(getCXXOverloadedOperator());
    OS << "operator" << (Sp ? Sp : "?");
    break;
  }
  case CXXLiteralOperatorName:
    OS << "operator""";
    break;
  case CXXUsingDirective:
    OS << "<using-directive>";
    break;
  }
}

llvm::raw_ostream &neverc::operator<<(llvm::raw_ostream &OS,
                                      DeclarationName N) {
  N.print(OS, PrintingPolicy{LangOptions()});
  return OS;
}

std::string DeclarationName::getAsString() const {
  llvm::SmallString<64> Buf;
  llvm::raw_svector_ostream OS(Buf);
  OS << *this;
  return std::string(Buf);
}

LLVM_DUMP_METHOD void DeclarationName::dump() const {
  llvm::errs() << *this << '\n';
}

std::string DeclarationNameInfo::getAsString() const {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  OS << *this;
  return Result;
}

llvm::raw_ostream &neverc::operator<<(llvm::raw_ostream &OS,
                                      DeclarationNameInfo DNInfo) {
  DNInfo.printName(OS, PrintingPolicy{LangOptions()});
  return OS;
}

void DeclarationNameInfo::printName(llvm::raw_ostream &OS,
                                    PrintingPolicy Policy) const {
  Name.print(OS, Policy);
}


const char *DeclarationName::getOperatorSpelling(OverloadedOperatorKind Op) {
  switch (Op) {
  case OO_None: return "";
  case OO_New: return "new";
  case OO_Delete: return "delete";
  case OO_Array_New: return "new[]";
  case OO_Array_Delete: return "delete[]";
  case OO_Plus: return "+";
  case OO_Minus: return "-";
  case OO_Star: return "*";
  case OO_Slash: return "/";
  case OO_Percent: return "%";
  case OO_Caret: return "^";
  case OO_Amp: return "&";
  case OO_Pipe: return "|";
  case OO_Tilde: return "~";
  case OO_Exclaim: return "!";
  case OO_Equal: return "=";
  case OO_Less: return "<";
  case OO_Greater: return ">";
  case OO_PlusEqual: return "+=";
  case OO_MinusEqual: return "-=";
  case OO_StarEqual: return "*=";
  case OO_SlashEqual: return "/=";
  case OO_PercentEqual: return "%=";
  case OO_Caretequal: return "^=";
  case OO_Ampequal: return "&=";
  case OO_Pipeequal: return "|=";
  case OO_LessLess: return "<<";
  case OO_GreaterGreater: return ">>";
  case OO_LessLessequal: return "<<=";
  case OO_GreaterGreaterequal: return ">>=";
  case OO_EqualEqual: return "==";
  case OO_Exclaimequal: return "!=";
  case OO_Lessequal: return "<=";
  case OO_Greaterequal: return ">=";
  case OO_Spaceship: return "<=>";
  case OO_AmpAmp: return "&&";
  case OO_PipePipe: return "||";
  case OO_PlusPlus: return "++";
  case OO_MinusMinus: return "--";
  case OO_Comma: return ",";
  case OO_ArrowStar: return "->*";
  case OO_Arrow: return "->";
  case OO_Call: return "()";
  case OO_Subscript: return "[]";
  case OO_Conditional: return "?";
  case OO_Coawait: return "co_await";
  case NUM_OVERLOADED_OPERATORS: return "";
  }
  return "";
}
