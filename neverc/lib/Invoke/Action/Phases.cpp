#include "neverc/Invoke/Phases.h"
#include "llvm/Support/ErrorHandling.h"

using namespace neverc::driver;

const char *phases::getPhaseName(ID Id) {
  switch (Id) {
  case Preprocess:
    return "preprocessor";
  case Compile:
    return "compiler";
  case Backend:
    return "backend";
  case Assemble:
    return "assembler";
  case Link:
    return "linker";
  }

  llvm_unreachable("Invalid phase id.");
}
