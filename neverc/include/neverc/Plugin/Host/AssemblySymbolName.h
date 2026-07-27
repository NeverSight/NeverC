#ifndef NEVERC_PLUGIN_HOST_ASSEMBLYSYMBOLNAME_H
#define NEVERC_PLUGIN_HOST_ASSEMBLYSYMBOLNAME_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"
#include <string>

namespace neverc::plugin {

// Whether a name can be written into assembly text, and how to spell it.
//
// Two places hand names to the assembler -- the built-in object writer and the
// built-in assembly printer -- and both face the same question, so the answer
// lives here rather than in whichever of them was written first. The printer
// used to write every name bare, which is fine until one holds a character the
// assembler reads as syntax: '@' introduces a relocation variant, '-'
// subtracts, and real manglings bring more. MSVC spells `void f(int)` as
// "?f@@YAXH@Z", Objective-C names a method "-[Class method]", and identifiers
// may hold UTF-8.
//
// Quoting carries all of those; the assembler takes a quoted name anywhere a
// bare one is allowed. A quote or a backslash is the one thing it cannot carry,
// because the lexer treats a backslash as an escape while the parser hands back
// the raw bytes between the quotes, so the two disagree and no spelling
// survives the trip. A NUL ends the lexer's input no matter where it sits, so a
// name holding one comes back truncated at that byte. Those are refused rather
// than written out wrong.

inline bool expressibleName(llvm::StringRef Name) {
  if (Name.empty())
    return false;
  return llvm::none_of(Name, [](char C) {
    return C == '"' || C == '\\' || C == '\n' || C == '\r' || C == '\0';
  });
}

inline std::string assemblyName(llvm::StringRef Name) {
  const bool Bare = !Name.empty() && !llvm::isDigit(Name.front()) &&
                    llvm::all_of(Name, [](char C) {
                      return llvm::isAlnum(C) || C == '_' || C == '.' ||
                             C == '$';
                    });
  if (Bare)
    return Name.str();
  return ("\"" + Name + "\"").str();
}

// Whether the assembler would take \p Name for a label it invented itself.
//
// Each format reserves a prefix for those, and the assembler classifies a name
// by that prefix before anything else has a say: a local symbol spelled this
// way is left out of the symbol table, and a global one is refused with
// "non-local symbol required". Quoting does not change the answer, because the
// lexer strips the quotes before the name is classified -- so a name reaching
// here cannot be written at all, and the local case is the dangerous one,
// since the symbol simply disappears with nothing said.
//
// This asks about names the caller was handed. A writer's own scratch labels
// are spelled with the prefix on purpose, to be dropped exactly this way.
inline bool isPrivateLabelName(llvm::StringRef Name,
                               const llvm::Triple &Target) {
  return Name.starts_with(Target.isOSBinFormatMachO() ? "L" : ".L");
}

} // namespace neverc::plugin

#endif
