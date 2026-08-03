#ifndef NEVERC_FOUNDATION_LANGSTANDARD_H
#define NEVERC_FOUNDATION_LANGSTANDARD_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {
class Triple;
}

namespace neverc {

enum class Language : uint8_t {
  Unknown,

  Asm,

  LLVM_IR,

  ///@{ Languages that the frontend can parse and compile.
  C,
  CXX,
  ///@}
};
llvm::StringRef languageToString(Language L);

enum LangFeatures {
  LineComment = (1 << 0),
  C99 = (1 << 1),
  C11 = (1 << 2),
  C17 = (1 << 3),
  C23 = (1 << 4),
  Digraphs = (1 << 5),
  GNUMode = (1 << 6),
  HexFloat = (1 << 7),
  CPlusPlus = (1 << 8),
  CPlusPlus11 = (1 << 9),
  CPlusPlus14 = (1 << 10),
  CPlusPlus17 = (1 << 11),
  CPlusPlus20 = (1 << 12)
};

struct LangStandard {
  enum Kind {
#define LANGSTANDARD(id, name, lang, desc, features) lang_##id,
#include "neverc/Foundation/LangOpts/LangStandards.def"
    lang_unspecified
  };

  const char *ShortName;
  const char *Description;
  unsigned Flags;
  neverc::Language Language;

public:
  const char *getName() const { return ShortName; }

  const char *getDescription() const { return Description; }

  neverc::Language getLanguage() const { return Language; }

  bool hasLineComments() const { return Flags & LineComment; }

  bool isC99() const { return Flags & C99; }

  bool isC11() const { return Flags & C11; }

  bool isC17() const { return Flags & C17; }

  bool isC23() const { return Flags & C23; }

  bool isCPlusPlus() const { return Flags & CPlusPlus; }

  bool isCPlusPlus11() const { return Flags & CPlusPlus11; }

  bool isCPlusPlus14() const { return Flags & CPlusPlus14; }

  bool isCPlusPlus17() const { return Flags & CPlusPlus17; }

  bool isCPlusPlus20() const { return Flags & CPlusPlus20; }

  bool hasDigraphs() const { return Flags & Digraphs; }

  bool isGNUMode() const { return Flags & GNUMode; }

  bool hasHexFloats() const { return Flags & HexFloat; }

  static Kind getLangKind(llvm::StringRef Name);
  static const LangStandard &getLangStandardForKind(Kind K);
  static const LangStandard *getLangStandardForName(llvm::StringRef Name);
};

LangStandard::Kind getDefaultLanguageStandard(neverc::Language Lang,
                                              const llvm::Triple &T);

} // end namespace neverc

#endif
