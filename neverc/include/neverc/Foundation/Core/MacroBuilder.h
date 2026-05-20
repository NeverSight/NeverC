#ifndef NEVERC_FOUNDATION_MACROBUILDER_H
#define NEVERC_FOUNDATION_MACROBUILDER_H

#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"

namespace neverc {

class MacroBuilder {
  llvm::raw_ostream &Out;

public:
  MacroBuilder(llvm::raw_ostream &Output) : Out(Output) {}

  void defineMacro(const llvm::Twine &Name, const llvm::Twine &Value = "1") {
    Out << "#define " << Name << ' ' << Value << '\n';
  }

  void undefineMacro(const llvm::Twine &Name) {
    Out << "#undef " << Name << '\n';
  }

  void append(const llvm::Twine &Str) { Out << Str << '\n'; }
};

} // end namespace neverc

#endif
