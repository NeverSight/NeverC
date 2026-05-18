#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Config/config.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/SpecialCaseList.h"
#include <cstdlib>
#include <cstring>

using namespace neverc;

// ===----------------------------------------------------------------------===
// LangOptions: construction & runtime queries
// ===----------------------------------------------------------------------===

LangOptions::LangOptions() : LangStd(LangStandard::lang_unspecified) {
#define LANGOPT(Name, Bits, Default, Description) Name = Default;
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description) set##Name(Default);
#include "neverc/Foundation/LangOpts/LangOptions.def"
}

bool LangOptions::isNoBuiltinFunc(llvm::StringRef FuncName) const {
  const unsigned N = NoBuiltinFuncs.size();
  if (LLVM_LIKELY(N == 0))
    return false;
  const auto NameLen = FuncName.size();
  if (LLVM_UNLIKELY(NameLen == 0))
    return false;
  const char *NameData = FuncName.data();
  const char First = NameData[0];
  for (unsigned i = 0; i != N; ++i) {
    const auto &Entry = NoBuiltinFuncs[i];
    if (LLVM_LIKELY(Entry.size() != NameLen))
      continue;
    if (LLVM_LIKELY(Entry.front() != First))
      continue;
    if (LLVM_LIKELY(NameLen <= 8)) {
      uint64_t A = 0, B = 0;
      std::memcpy(&A, NameData, NameLen);
      std::memcpy(&B, Entry.data(), NameLen);
      if (A == B)
        return true;
    } else if (LLVM_LIKELY(std::memcmp(NameData, Entry.data(), NameLen) == 0)) {
      return true;
    }
  }
  return false;
}

void LangOptions::remapPathPrefix(llvm::SmallVectorImpl<char> &Path) const {
  for (const auto &Entry : MacroPrefixMap)
    if (llvm::sys::path::replace_path_prefix(Path, Entry.first, Entry.second))
      break;
}

// ===----------------------------------------------------------------------===
// Language standard defaults
// ===----------------------------------------------------------------------===

void LangOptions::setLangDefaults(LangOptions &Opts, Language Lang,
                                  const llvm::Triple &T,
                                  std::vector<std::string> &Includes,
                                  LangStandard::Kind LangStd) {
  if (Lang == Language::Asm) {
    Opts.AsmPreprocessor = 1;
  }

  if (LangStd == LangStandard::lang_unspecified)
    LangStd = getDefaultLanguageStandard(Lang, T);
  const LangStandard &Std = LangStandard::getLangStandardForKind(LangStd);
  Opts.LangStd = LangStd;
  Opts.LineComment = Std.hasLineComments();
  Opts.C99 = Std.isC99();
  Opts.C11 = Std.isC11();
  Opts.C17 = Std.isC17();
  Opts.C23 = Std.isC23();
  Opts.GNUMode = Std.isGNUMode();
  Opts.GNUCVersion = 0;
  Opts.HexFloats = Std.hasHexFloats();
  Opts.Digraphs = Std.hasDigraphs();

  Opts.Bool = Opts.C23;
}

FPOptions FPOptions::defaultWithoutTrailingStorage(const LangOptions &LO) {
  FPOptions result(LO);
  return result;
}

FPOptionsOverride FPOptions::getChangesSlow(const FPOptions &Base) const {
  FPOptions::storage_type OverrideMask = 0;
#define OPTION(NAME, TYPE, WIDTH, PREVIOUS)                                    \
  if (get##NAME() != Base.get##NAME())                                         \
    OverrideMask |= NAME##Mask;
#include "neverc/Foundation/LangOpts/FPOptions.def"
  return FPOptionsOverride(*this, OverrideMask);
}

LLVM_DUMP_METHOD void FPOptions::dump() {
#define OPTION(NAME, TYPE, WIDTH, PREVIOUS)                                    \
  llvm::errs() << "\n " #NAME " " << get##NAME();
#include "neverc/Foundation/LangOpts/FPOptions.def"
  llvm::errs() << "\n";
}

LLVM_DUMP_METHOD void FPOptionsOverride::dump() {
#define OPTION(NAME, TYPE, WIDTH, PREVIOUS)                                    \
  if (has##NAME##Override())                                                   \
    llvm::errs() << "\n " #NAME " Override is " << get##NAME##Override();
#include "neverc/Foundation/LangOpts/FPOptions.def"
  llvm::errs() << "\n";
}

llvm::StringRef neverc::languageToString(Language L) {
  switch (L) {
  case Language::Unknown:
    return "Unknown";
  case Language::Asm:
    return "Asm";
  case Language::LLVM_IR:
    return "LLVM IR";
  case Language::C:
    return "C";
  }
}

namespace {
bool hasDefaultCLanguageStandard(Language L) {
  return L == Language::C || L == Language::Asm || L == Language::LLVM_IR;
}
} // namespace

#define LANGSTANDARD(id, name, lang, desc, features)                           \
  static const LangStandard Lang_##id = {name, desc, features, Language::lang};
#include "neverc/Foundation/LangOpts/LangStandards.def"

// ===----------------------------------------------------------------------===
// LangStandard registry lookup
// ===----------------------------------------------------------------------===

const LangStandard &LangStandard::getLangStandardForKind(Kind K) {
  switch (K) {
  case lang_unspecified:
    llvm::report_fatal_error("getLangStandardForKind() on unspecified kind");
#define LANGSTANDARD(id, name, lang, desc, features)                           \
  case lang_##id:                                                              \
    return Lang_##id;
#include "neverc/Foundation/LangOpts/LangStandards.def"
  }
  llvm::report_fatal_error("Invalid language kind!");
}

LangStandard::Kind LangStandard::getLangKind(llvm::StringRef Name) {
  return llvm::StringSwitch<Kind>(Name)
#define LANGSTANDARD(id, name, lang, desc, features) .Case(name, lang_##id)
#define LANGSTANDARD_ALIAS(id, alias) .Case(alias, lang_##id)
#include "neverc/Foundation/LangOpts/LangStandards.def"
      .Default(lang_unspecified);
}

const LangStandard *LangStandard::getLangStandardForName(llvm::StringRef Name) {
  Kind K = getLangKind(Name);
  if (K == lang_unspecified)
    return nullptr;

  return &getLangStandardForKind(K);
}

LangStandard::Kind neverc::getDefaultLanguageStandard(neverc::Language Lang,
                                                      const llvm::Triple &T) {
  (void)T;
  if (hasDefaultCLanguageStandard(Lang))
    return LangStandard::lang_gnu23;
  llvm::report_fatal_error("unsupported language for default standard");
}

// ===----------------------------------------------------------------------===
// CodeGenOptions::OptRemark pattern matching
// ===----------------------------------------------------------------------===

namespace neverc {

bool CodeGenOptions::OptRemark::patternMatches(llvm::StringRef String) const {
  return hasValidPattern() && Regex->match(String);
}

CodeGenOptions::CodeGenOptions() {
#define CODEGENOPT(Name, Bits, Default) Name = Default;
#define ENUM_CODEGENOPT(Name, Type, Bits, Default) set##Name(Default);
#include "neverc/Foundation/LangOpts/CodeGenOptions.def"
}

} // namespace neverc
