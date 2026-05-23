//===-- MCAsmMacro.cpp - Assembly macro dump implementations --------------===//
//
// MCAsmMacro::dump is declared in the header but must live in a library TU when
// assertions are enabled (Windows Release builds may not define NDEBUG).
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmMacro.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void MCAsmMacroParameter::dump(raw_ostream &OS) const {
  OS << "\"" << Name << "\"";
  if (Required)
    OS << ":req";
  if (Vararg)
    OS << ":vararg";
  if (!Value.empty()) {
    OS << " = ";
    bool first = true;
    for (const AsmToken &T : Value) {
      if (!first)
        OS << ", ";
      first = false;
      OS << T.getString();
    }
  }
  OS << "\n";
}

void MCAsmMacro::dump(raw_ostream &OS) const {
  OS << "Macro " << Name << ":\n";
  OS << " Parameters:\n";
  for (const MCAsmMacroParameter &P : Parameters) {
    OS << "  ";
    P.dump(OS);
  }
  if (!Locals.empty()) {
    OS << " Locals:\n";
    for (StringRef L : Locals)
      OS << "  " << L << '\n';
  }
  OS << " (BEGIN BODY)" << Body << "(END BODY)\n";
}
#endif
