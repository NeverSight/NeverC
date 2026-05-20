#ifndef NEVERC_SCAN_INCLUDEGUARDOPT_H
#define NEVERC_SCAN_INCLUDEGUARDOPT_H

#include "neverc/Foundation/Core/SourceLocation.h"

namespace neverc {
class IdentifierInfo;

class IncludeGuardTracker {
  bool ReadAnyTokens;

  bool ImmediatelyAfterTopLevelIfndef;

  bool DidMacroExpansion;

  const IdentifierInfo *TheMacro;

  const IdentifierInfo *DefinedMacro;

  SourceLocation MacroLoc;
  SourceLocation DefinedLoc;

public:
  IncludeGuardTracker() {
    ReadAnyTokens = false;
    ImmediatelyAfterTopLevelIfndef = false;
    DidMacroExpansion = false;
    TheMacro = nullptr;
    DefinedMacro = nullptr;
  }

  SourceLocation getMacroLocation() const { return MacroLoc; }

  SourceLocation getDefinedLocation() const { return DefinedLoc; }

  void resetImmediatelyAfterTopLevelIfndef() {
    ImmediatelyAfterTopLevelIfndef = false;
  }

  void setDefinedMacro(IdentifierInfo *M, SourceLocation Loc) {
    DefinedMacro = M;
    DefinedLoc = Loc;
  }

  void invalidate() {
    // If we have read tokens but have no controlling macro, the state-machine
    // below can never "accept".
    ReadAnyTokens = true;
    ImmediatelyAfterTopLevelIfndef = false;
    DefinedMacro = nullptr;
    TheMacro = nullptr;
  }

  bool getHasReadAnyTokensVal() const { return ReadAnyTokens; }

  bool getImmediatelyAfterTopLevelIfndef() const {
    return ImmediatelyAfterTopLevelIfndef;
  }

  // If a token is read, remember that we have seen a side-effect in this file.
  void readToken() {
    ReadAnyTokens = true;
    ImmediatelyAfterTopLevelIfndef = false;
  }

  void setReadToken(bool Value) { ReadAnyTokens = Value; }

  void expandedMacro() { DidMacroExpansion = true; }

  void enterTopLevelIfndef(const IdentifierInfo *M, SourceLocation Loc) {
    // If the macro is already set, this is after the top-level #endif.
    if (TheMacro)
      return invalidate();

    // If we have already expanded a macro by the end of the #ifndef line, then
    // there is a macro expansion *in* the #ifndef line.  This means that the
    // condition could evaluate differently when subsequently #included.  Reject
    // this.
    if (DidMacroExpansion)
      return invalidate();

    // Remember that we're in the #if and that we have the macro.
    ReadAnyTokens = true;
    ImmediatelyAfterTopLevelIfndef = true;
    TheMacro = M;
    MacroLoc = Loc;
  }

  void enterTopLevelConditional() {
    // If a conditional directive (except #ifndef) is found at the top level,
    // there is a chunk of the file not guarded by the controlling macro.
    invalidate();
  }

  void exitTopLevelConditional() {
    // If we have a macro, that means the top of the file was ok.  Set our state
    // back to "not having read any tokens" so we can detect anything after the
    // #endif.
    if (!TheMacro)
      return invalidate();

    // At this point, we haven't "read any tokens" but we do have a controlling
    // macro.
    ReadAnyTokens = false;
    ImmediatelyAfterTopLevelIfndef = false;
  }

  const IdentifierInfo *getControllingMacroAtEndOfFile() const {
    // If we haven't read any tokens after the #endif, return the controlling
    // macro if it's valid (if it isn't, it will be null).
    if (!ReadAnyTokens)
      return TheMacro;
    return nullptr;
  }

  const IdentifierInfo *getDefinedMacro() const { return DefinedMacro; }
};

} // end namespace neverc

#endif
