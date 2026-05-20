#ifndef NEVERC_SCAN_MACROARGSTORAGE_H
#define NEVERC_SCAN_MACROARGSTORAGE_H

#include "neverc/Scan/Token.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/TrailingObjects.h"
#include <vector>

namespace neverc {
class MacroRecord;
class PrepEngine;
class SourceLocation;

class MacroArgStorage final
    : private llvm::TrailingObjects<MacroArgStorage, Token> {

  friend TrailingObjects;
  unsigned NumUnexpArgTokens;

  bool VarargsElided;

  std::vector<std::vector<Token>> PreExpArgTokens;

  MacroArgStorage *ArgCache;

  unsigned NumMacroArgStorage;

  mutable llvm::SmallVector<unsigned, 8> ArgStartOffsets;
  mutable unsigned ArgAccessCount = 0;

  MacroArgStorage(unsigned NumToks, bool varargsElided,
                  unsigned MacroArgStorage)
      : NumUnexpArgTokens(NumToks), VarargsElided(varargsElided),
        ArgCache(nullptr), NumMacroArgStorage(MacroArgStorage) {}
  ~MacroArgStorage() = default;

public:
  static MacroArgStorage *create(const MacroRecord *MI,
                                 llvm::ArrayRef<Token> UnexpArgTokens,
                                 bool VarargsElided, PrepEngine &PP);

  void destroy(PrepEngine &PP);

  bool ArgNeedsPreexpansion(const Token *ArgTok, PrepEngine &PP) const;

  const Token *getUnexpArgument(unsigned Arg) const;

  static unsigned getArgLength(const Token *ArgPtr);

  const std::vector<Token> &getPreExpArgument(unsigned Arg, PrepEngine &PP);

  unsigned getNumMacroArguments() const { return NumMacroArgStorage; }

  bool isVarargsElidedUse() const { return VarargsElided; }

  bool invokedWithVariadicArgument(const MacroRecord *const MI, PrepEngine &PP);

  static Token EscapeArgToLiteral(const Token *ArgToks, PrepEngine &PP,
                                  bool Charify,
                                  SourceLocation ExpansionLocStart,
                                  SourceLocation ExpansionLocEnd);

  MacroArgStorage *deallocate();
};

} // end namespace neverc

#endif
