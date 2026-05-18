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
  if (const IdentifierInfo *II = getAsIdentifierInfo())
    OS << II->getName();
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
