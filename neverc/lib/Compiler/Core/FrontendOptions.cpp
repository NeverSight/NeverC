#include "neverc/Compiler/FrontendOptions.h"
#include "neverc/Foundation/LangOpts/LangStandard.h"
#include "llvm/ADT/StringSwitch.h"

using namespace neverc;

InputKind FrontendOptions::getInputKindForExtension(llvm::StringRef Extension) {
  return llvm::StringSwitch<InputKind>(Extension)
      .Case("c", Language::C)
      .Case("nc", Language::C)
      .Cases("S", "s", Language::Asm)
      .Case("i", InputKind(Language::C).getPreprocessed())
      .Cases("ll", "bc", Language::LLVM_IR)
      .Default(Language::Unknown);
}
