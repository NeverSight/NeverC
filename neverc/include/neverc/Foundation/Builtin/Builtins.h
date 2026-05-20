#ifndef NEVERC_FOUNDATION_BUILTINS_H
#define NEVERC_FOUNDATION_BUILTINS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <cstring>

// MSVC defines 'alloca' as an object-like macro, which interferes with our
// builtins.
#undef alloca

namespace neverc {
class TargetInfo;
class IdentifierTable;
class LangOptions;

enum LanguageID : uint16_t {
  GNU_LANG = 0x1,
  C_LANG = 0x2,
  MS_LANG = 0x10,
  ALL_LANGUAGES = C_LANG,
  ALL_GNU_LANGUAGES = ALL_LANGUAGES | GNU_LANG,
  ALL_MS_LANGUAGES = ALL_LANGUAGES | MS_LANG
};

struct HeaderDesc {
  enum HeaderID : uint16_t {
#define HEADER(ID, NAME) ID,
#include "neverc/Foundation/Builtin/BuiltinHeaders.def"
#undef HEADER
  } ID;

  constexpr HeaderDesc(HeaderID ID) : ID(ID) {}

  const char *getName() const;
};

namespace Builtin {
enum ID {
  NotBuiltin = 0, // This is not a builtin function.
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "neverc/Foundation/Builtin/Builtins.def"
  FirstTSBuiltin
};

struct Info {
  llvm::StringLiteral Name;
  const char *Type, *Attributes;
  const char *Features;
  HeaderDesc Header;
  LanguageID Langs;
};

class Context {
  llvm::ArrayRef<Info> TSRecords;

public:
  Context() = default;

  void InitializeTarget(const TargetInfo &Target);

  void initializeBuiltins(IdentifierTable &Table, const LangOptions &LangOpts);

  llvm::StringRef getName(unsigned ID) const { return getRecord(ID).Name; }

  const char *getTypeString(unsigned ID) const { return getRecord(ID).Type; }

  bool isTSBuiltin(unsigned ID) const { return ID >= Builtin::FirstTSBuiltin; }

  bool isPure(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'U') != nullptr;
  }

  bool isConst(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'c') != nullptr;
  }

  bool isNoThrow(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'n') != nullptr;
  }

  bool isNoReturn(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'r') != nullptr;
  }

  bool isReturnsTwice(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'j') != nullptr;
  }

  bool isUnevaluated(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'u') != nullptr;
  }

  bool isLibFunction(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'F') != nullptr;
  }

  bool isPredefinedLibFunction(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'f') != nullptr;
  }

  bool isHeaderDependentFunction(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'h') != nullptr;
  }

  bool isPredefinedRuntimeFunction(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'i') != nullptr;
  }

  bool isDirectlyAddressable(unsigned ID) const {
    return isPredefinedLibFunction(ID);
  }

  bool hasCustomTypechecking(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 't') != nullptr;
  }

  bool allowTypeMismatch(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'T') != nullptr ||
           hasCustomTypechecking(ID);
  }

  bool hasPtrArgsOrResult(unsigned ID) const {
    return strchr(getRecord(ID).Type, '*') != nullptr;
  }

  bool hasReferenceArgsOrResult(unsigned ID) const {
    return strchr(getRecord(ID).Type, '&') != nullptr ||
           strchr(getRecord(ID).Type, 'A') != nullptr;
  }

  const char *getHeaderName(unsigned ID) const {
    return getRecord(ID).Header.getName();
  }

  bool isPrintfLike(unsigned ID, unsigned &FormatIdx, bool &HasVAListArg);

  bool isScanfLike(unsigned ID, unsigned &FormatIdx, bool &HasVAListArg);

  bool performsCallback(unsigned ID,
                        llvm::SmallVectorImpl<int> &Encoding) const;

  bool isConstWithoutErrnoAndExceptions(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'e') != nullptr;
  }

  bool isConstWithoutExceptions(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'g') != nullptr;
  }

  const char *getRequiredFeatures(unsigned ID) const {
    return getRecord(ID).Features;
  }

  unsigned getRequiredVectorWidth(unsigned ID) const;

  static bool isBuiltinFunc(llvm::StringRef Name);

  bool canBeRedeclared(unsigned ID) const;

  bool isConstantEvaluated(unsigned ID) const {
    return strchr(getRecord(ID).Attributes, 'E') != nullptr;
  }

private:
  const Info &getRecord(unsigned ID) const;

  bool isLike(unsigned ID, unsigned &FormatIdx, bool &HasVAListArg,
              const char *Fmt) const;
};

bool evaluateRequiredTargetFeatures(
    llvm::StringRef RequiredFatures,
    const llvm::StringMap<bool> &TargetFetureMap);

} // namespace Builtin

} // end namespace neverc
#endif
