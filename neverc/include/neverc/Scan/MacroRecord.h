#ifndef NEVERC_SCAN_MACRORECORD_H
#define NEVERC_SCAN_MACRORECORD_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Scan/Token.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include <algorithm>
#include <cassert>

namespace neverc {

class DefMacroDirective;
class IdentifierInfo;
class PrepEngine;
class SourceManager;

class MacroRecord {
  //===--------------------------------------------------------------------===//
  // State set when the macro is defined.

  SourceLocation Location;

  SourceLocation EndLocation;

  IdentifierInfo **ParameterList = nullptr;

  const Token *ReplacementTokens = nullptr;

  unsigned NumParameters = 0;

  unsigned NumReplacementTokens = 0;

  mutable unsigned DefinitionLength;
  mutable bool IsDefinitionLengthCached : 1;

  bool IsFunctionLike : 1;

  bool IsC99Varargs : 1;

  bool IsGNUVarargs : 1;

  bool IsBuiltinMacro : 1;

  bool HasCommaPasting : 1;

  //===--------------------------------------------------------------------===//
  // State that changes as the macro is used.

  bool IsDisabled : 1;

  bool IsUsed : 1;

  bool IsAllowRedefinitionsWithoutWarning : 1;

  bool IsWarnIfUnused : 1;

  bool UsedForHeaderGuard : 1;
  mutable bool TokenContentHashValid : 1;

  mutable uint32_t TokenContentHash = 0;

  // Only the PrepEngine gets to create these.
  MacroRecord(SourceLocation DefLoc);

public:
  SourceLocation getDefinitionLoc() const { return Location; }

  void setDefinitionEndLoc(SourceLocation EndLoc) { EndLocation = EndLoc; }

  SourceLocation getDefinitionEndLoc() const { return EndLocation; }

  unsigned getDefinitionLength(const SourceManager &SM) const {
    if (IsDefinitionLengthCached)
      return DefinitionLength;
    return getDefinitionLengthSlow(SM);
  }

  bool isIdenticalTo(const MacroRecord &Other, PrepEngine &PP,
                     bool Syntactically) const;
  uint32_t computeTokenContentHash() const;

  void setIsBuiltinMacro(bool Val = true) { IsBuiltinMacro = Val; }

  void setIsUsed(bool Val) { IsUsed = Val; }

  void setIsAllowRedefinitionsWithoutWarning(bool Val) {
    IsAllowRedefinitionsWithoutWarning = Val;
  }

  void setIsWarnIfUnused(bool val) { IsWarnIfUnused = val; }

  void setParameterList(llvm::ArrayRef<IdentifierInfo *> List,
                        llvm::BumpPtrAllocator &PPAllocator) {
    assert(ParameterList == nullptr && NumParameters == 0 &&
           "Parameter list already set!");
    if (List.empty())
      return;

    NumParameters = List.size();
    ParameterList = PPAllocator.Allocate<IdentifierInfo *>(List.size());
    std::copy(List.begin(), List.end(), ParameterList);
  }

  using param_iterator = IdentifierInfo *const *;
  bool param_empty() const { return NumParameters == 0; }
  param_iterator param_begin() const { return ParameterList; }
  param_iterator param_end() const { return ParameterList + NumParameters; }
  unsigned getNumParams() const { return NumParameters; }
  llvm::ArrayRef<const IdentifierInfo *> params() const {
    return llvm::ArrayRef<const IdentifierInfo *>(ParameterList, NumParameters);
  }

  int getParameterNum(const IdentifierInfo *Arg) const {
    for (param_iterator I = param_begin(), E = param_end(); I != E; ++I)
      if (*I == Arg)
        return I - param_begin();
    return -1;
  }

  void setIsFunctionLike() { IsFunctionLike = true; }
  bool isFunctionLike() const { return IsFunctionLike; }
  bool isObjectLike() const { return !IsFunctionLike; }

  void setIsC99Varargs() { IsC99Varargs = true; }
  void setIsGNUVarargs() { IsGNUVarargs = true; }
  bool isC99Varargs() const { return IsC99Varargs; }
  bool isGNUVarargs() const { return IsGNUVarargs; }
  bool isVariadic() const { return IsC99Varargs || IsGNUVarargs; }

  bool isBuiltinMacro() const { return IsBuiltinMacro; }

  bool hasCommaPasting() const { return HasCommaPasting; }
  void setHasCommaPasting() { HasCommaPasting = true; }

  bool isUsed() const { return IsUsed; }

  bool isAllowRedefinitionsWithoutWarning() const {
    return IsAllowRedefinitionsWithoutWarning;
  }

  bool isWarnIfUnused() const { return IsWarnIfUnused; }

  unsigned getNumTokens() const { return NumReplacementTokens; }

  const Token &getReplacementToken(unsigned Tok) const {
    assert(Tok < NumReplacementTokens && "Invalid token #");
    return ReplacementTokens[Tok];
  }

  using const_tokens_iterator = const Token *;

  const_tokens_iterator tokens_begin() const { return ReplacementTokens; }
  const_tokens_iterator tokens_end() const {
    return ReplacementTokens + NumReplacementTokens;
  }
  bool tokens_empty() const { return NumReplacementTokens == 0; }
  llvm::ArrayRef<Token> tokens() const {
    return llvm::ArrayRef(ReplacementTokens, NumReplacementTokens);
  }

  llvm::MutableArrayRef<Token>
  allocateTokens(unsigned NumTokens, llvm::BumpPtrAllocator &PPAllocator) {
    assert(ReplacementTokens == nullptr && NumReplacementTokens == 0 &&
           "Token list already allocated!");
    NumReplacementTokens = NumTokens;
    Token *NewReplacementTokens = PPAllocator.Allocate<Token>(NumTokens);
    ReplacementTokens = NewReplacementTokens;
    return llvm::MutableArrayRef(NewReplacementTokens, NumTokens);
  }

  void setTokens(llvm::ArrayRef<Token> Tokens,
                 llvm::BumpPtrAllocator &PPAllocator) {
    assert(
        !IsDefinitionLengthCached &&
        "Changing replacement tokens after definition length got calculated");
    assert(ReplacementTokens == nullptr && NumReplacementTokens == 0 &&
           "Token list already set!");
    if (Tokens.empty())
      return;

    NumReplacementTokens = Tokens.size();
    Token *NewReplacementTokens = PPAllocator.Allocate<Token>(Tokens.size());
    std::copy(Tokens.begin(), Tokens.end(), NewReplacementTokens);
    ReplacementTokens = NewReplacementTokens;
  }

  bool isEnabled() const { return !IsDisabled; }

  void EnableMacro() {
    assert(IsDisabled && "Cannot enable an already-enabled macro!");
    IsDisabled = false;
  }

  void DisableMacro() {
    assert(!IsDisabled && "Cannot disable an already-disabled macro!");
    IsDisabled = true;
  }

  bool isUsedForHeaderGuard() const { return UsedForHeaderGuard; }

  void setUsedForHeaderGuard(bool Val) { UsedForHeaderGuard = Val; }

  void dump() const;

private:
  friend class PrepEngine;

  unsigned getDefinitionLengthSlow(const SourceManager &SM) const;
};

class MacroDirective {
public:
  enum Kind { MD_Define, MD_Undefine };

protected:
  MacroDirective *Previous = nullptr;

  SourceLocation Loc;

  LLVM_PREFERRED_TYPE(Kind)
  unsigned MDKind : 2;

  MacroDirective(Kind K, SourceLocation Loc) : Loc(Loc), MDKind(K) {}

public:
  Kind getKind() const { return Kind(MDKind); }

  SourceLocation getLocation() const { return Loc; }

  void setPrevious(MacroDirective *Prev) { Previous = Prev; }

  const MacroDirective *getPrevious() const { return Previous; }

  MacroDirective *getPrevious() { return Previous; }

  class DefInfo {
    DefMacroDirective *DefDirective = nullptr;
    SourceLocation UndefLoc;

  public:
    DefInfo() = default;
    DefInfo(DefMacroDirective *DefDirective, SourceLocation UndefLoc)
        : DefDirective(DefDirective), UndefLoc(UndefLoc) {}

    const DefMacroDirective *getDirective() const { return DefDirective; }
    DefMacroDirective *getDirective() { return DefDirective; }

    inline SourceLocation getLocation() const;
    inline MacroRecord *getMacroRecord();

    const MacroRecord *getMacroRecord() const {
      return const_cast<DefInfo *>(this)->getMacroRecord();
    }

    SourceLocation getUndefLocation() const { return UndefLoc; }
    bool isUndefined() const { return UndefLoc.isValid(); }

    bool isValid() const { return DefDirective != nullptr; }
    bool isInvalid() const { return !isValid(); }

    explicit operator bool() const { return isValid(); }

    inline DefInfo getPreviousDefinition();

    const DefInfo getPreviousDefinition() const {
      return const_cast<DefInfo *>(this)->getPreviousDefinition();
    }
  };

  DefInfo getDefinition();
  const DefInfo getDefinition() const {
    return const_cast<MacroDirective *>(this)->getDefinition();
  }

  bool isDefined() const {
    if (const DefInfo Def = getDefinition())
      return !Def.isUndefined();
    return false;
  }

  const MacroRecord *getMacroRecord() const {
    return getDefinition().getMacroRecord();
  }
  MacroRecord *getMacroRecord() { return getDefinition().getMacroRecord(); }

  const DefInfo findDirectiveAtLoc(SourceLocation L,
                                   const SourceManager &SM) const;

  void dump() const;

  static bool classof(const MacroDirective *) { return true; }
};

class DefMacroDirective : public MacroDirective {
  MacroRecord *Info;

public:
  DefMacroDirective(MacroRecord *MI, SourceLocation Loc)
      : MacroDirective(MD_Define, Loc), Info(MI) {
    assert(MI && "MacroRecord is null");
  }
  explicit DefMacroDirective(MacroRecord *MI)
      : DefMacroDirective(MI, MI->getDefinitionLoc()) {}

  const MacroRecord *getInfo() const { return Info; }
  MacroRecord *getInfo() { return Info; }

  static bool classof(const MacroDirective *MD) {
    return MD->getKind() == MD_Define;
  }

  static bool classof(const DefMacroDirective *) { return true; }
};

class UndefMacroDirective : public MacroDirective {
public:
  explicit UndefMacroDirective(SourceLocation UndefLoc)
      : MacroDirective(MD_Undefine, UndefLoc) {
    assert(UndefLoc.isValid() && "Invalid UndefLoc!");
  }

  static bool classof(const MacroDirective *MD) {
    return MD->getKind() == MD_Undefine;
  }

  static bool classof(const UndefMacroDirective *) { return true; }
};

inline SourceLocation MacroDirective::DefInfo::getLocation() const {
  if (isInvalid())
    return {};
  return DefDirective->getLocation();
}

inline MacroRecord *MacroDirective::DefInfo::getMacroRecord() {
  if (isInvalid())
    return nullptr;
  return DefDirective->getInfo();
}

inline MacroDirective::DefInfo
MacroDirective::DefInfo::getPreviousDefinition() {
  if (isInvalid() || DefDirective->getPrevious() == nullptr)
    return {};
  return DefDirective->getPrevious()->getDefinition();
}

class MacroDefinition {
  DefMacroDirective *LatestLocal = nullptr;

public:
  MacroDefinition() = default;
  MacroDefinition(DefMacroDirective *MD, llvm::ArrayRef<int> = {}, bool = false)
      : LatestLocal(MD) {}

  explicit operator bool() const { return getLocalDirective(); }

  MacroRecord *getMacroRecord() const {
    if (auto *MD = getLocalDirective())
      return MD->getMacroRecord();
    return nullptr;
  }

  bool isAmbiguous() const { return false; }

  DefMacroDirective *getLocalDirective() const { return LatestLocal; }

  template <typename Fn> void forAllDefinitions(Fn F) const {
    if (auto *MD = getLocalDirective())
      F(MD->getMacroRecord());
  }
};

} // namespace neverc

#endif // NEVERC_SCAN_MACROINFO_H
