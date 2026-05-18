#ifndef NEVERC_LIB_AST_FORMATSTRINGPARSING_H
#define NEVERC_LIB_AST_FORMATSTRINGPARSING_H

#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Format/FormatString.h"
#include "neverc/Tree/Type/Type.h"

namespace neverc {

class LangOptions;

template <typename T> class UpdateOnReturn {
  T &ValueToUpdate;
  const T &ValueToCopy;

public:
  UpdateOnReturn(T &valueToUpdate, const T &valueToCopy)
      : ValueToUpdate(valueToUpdate), ValueToCopy(valueToCopy) {}

  ~UpdateOnReturn() { ValueToUpdate = ValueToCopy; }
};

namespace analyze_format_string {

OptionalAmount ParseAmount(const char *&Beg, const char *E);
OptionalAmount ParseNonPositionAmount(const char *&Beg, const char *E,
                                      unsigned &argIndex);

OptionalAmount ParsePositionAmount(FormatStringHandler &H, const char *Start,
                                   const char *&Beg, const char *E,
                                   PositionContext p);

bool ParseFieldWidth(FormatStringHandler &H, FormatSpecifier &CS,
                     const char *Start, const char *&Beg, const char *E,
                     unsigned *argIndex);

bool ParseArgPosition(FormatStringHandler &H, FormatSpecifier &CS,
                      const char *Start, const char *&Beg, const char *E);

bool ParseLengthModifier(FormatSpecifier &FS, const char *&Beg, const char *E,
                         const LangOptions &LO, bool IsScanf = false);

bool ParseUTF8InvalidSpecifier(const char *SpecifierBegin,
                               const char *FmtStrEnd, unsigned &Len);

template <typename T> class SpecifierResult {
  T FS;
  const char *Start;
  bool Stop;

public:
  SpecifierResult(bool stop = false) : Start(nullptr), Stop(stop) {}
  SpecifierResult(const char *start, const T &fs)
      : FS(fs), Start(start), Stop(false) {}

  const char *getStart() const { return Start; }
  bool shouldStop() const { return Stop; }
  bool hasValue() const { return Start != nullptr; }
  const T &getValue() const {
    assert(hasValue());
    return FS;
  }
  const T &getValue() { return FS; }
};

} // namespace analyze_format_string
} // namespace neverc

#endif
