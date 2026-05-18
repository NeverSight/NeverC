#ifndef NEVERC_FRONTEND_PREPOUTPUTOPTIONS_H
#define NEVERC_FRONTEND_PREPOUTPUTOPTIONS_H

#include <llvm/Support/Compiler.h>

namespace neverc {

class PrepOutputOptions {
public:
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowCPP : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowComments : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowLineMarkers : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseLineDirectives : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowMacroComments : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowMacros : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowIncludeDirectives : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned MinimizeWhitespace : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned DirectivesOnly : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned KeepSystemIncludes : 1;

public:
  PrepOutputOptions() {
    ShowCPP = 0;
    ShowComments = 0;
    ShowLineMarkers = 1;
    UseLineDirectives = 0;
    ShowMacroComments = 0;
    ShowMacros = 0;
    ShowIncludeDirectives = 0;
    MinimizeWhitespace = 0;
    DirectivesOnly = 0;
    KeepSystemIncludes = 0;
  }
};

} // end namespace neverc

#endif
