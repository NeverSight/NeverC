#ifndef NEVERC_BASIC_IDENTIFIERTABLE_H
#define NEVERC_BASIC_IDENTIFIERTABLE_H

#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Diagnostic/DiagnosticIDs.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace neverc {

class IdentifierInfo;
class LangOptions;

enum class ReservedIdentifierStatus {
  NotReserved = 0,
  StartsWithUnderscoreAtGlobalScope,
  StartsWithUnderscoreAndIsExternC,
  StartsWithDoubleUnderscore,
  StartsWithUnderscoreFollowedByCapitalLetter,
  ContainsDoubleUnderscore,
};

inline bool isReservedAtGlobalScope(ReservedIdentifierStatus Status) {
  return Status != ReservedIdentifierStatus::NotReserved;
}

inline bool isReservedInAllContexts(ReservedIdentifierStatus Status) {
  return Status != ReservedIdentifierStatus::NotReserved &&
         Status !=
             ReservedIdentifierStatus::StartsWithUnderscoreAtGlobalScope &&
         Status != ReservedIdentifierStatus::StartsWithUnderscoreAndIsExternC;
}

enum { IdentifierInfoAlignment = 8 };

static constexpr int BuiltinIDBits = 16;

static constexpr int FirstInterestingIdentifierID = 1;
static constexpr int LastInterestingIdentifierID =
    FirstInterestingIdentifierID + tok::NUM_INTERESTING_IDENTIFIERS - 2;
static constexpr int FirstBuiltinID = LastInterestingIdentifierID + 1;

class alignas(IdentifierInfoAlignment) IdentifierInfo {
  friend class IdentifierTable;

  // Front-end token ID or tok::identifier.
  LLVM_PREFERRED_TYPE(tok::TokenKind)
  unsigned TokenID : 9;

  unsigned BuiltinOrExtraID : BuiltinIDBits;

  // True if there is a #define for this.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasMacro : 1;

  // True if there was a #define for this.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HadMacro : 1;

  // True if the identifier is a language extension.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsExtension : 1;

  // True if the identifier is a keyword in a newer or proposed Standard.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFutureCompatKeyword : 1;

  // True if the identifier is poisoned.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsPoisoned : 1;

  // Internal bit set by the member function RecomputeNeedsIdentifierProcessing.
  // See comment about RecomputeNeedsIdentifierProcessing for more info.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NeedsIdentifierProcessing : 1;

  // True if revertTokenIDToIdentifier was called.
  LLVM_PREFERRED_TYPE(bool)
  unsigned RevertedTokenID : 1;

  // True if this is a deprecated macro.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsDeprecatedMacro : 1;

  // True if this macro is unsafe in headers.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsRestrictExpansion : 1;

  // True if this macro is final.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFinal : 1;

  // 22 bits left in a 64-bit word.

  // Managed by the language front-end.
  void *FETokenInfo = nullptr;

  llvm::StringMapEntry<IdentifierInfo *> *Entry = nullptr;

  IdentifierInfo()
      : TokenID(tok::identifier), BuiltinOrExtraID(0), HasMacro(false),
        HadMacro(false), IsExtension(false), IsFutureCompatKeyword(false),
        IsPoisoned(false), NeedsIdentifierProcessing(false),
        RevertedTokenID(false), IsDeprecatedMacro(false),
        IsRestrictExpansion(false), IsFinal(false) {}

public:
  IdentifierInfo(const IdentifierInfo &) = delete;
  IdentifierInfo &operator=(const IdentifierInfo &) = delete;
  IdentifierInfo(IdentifierInfo &&) = delete;
  IdentifierInfo &operator=(IdentifierInfo &&) = delete;

  template <std::size_t StrLen> bool isStr(const char (&Str)[StrLen]) const {
    return getLength() == StrLen - 1 &&
           memcmp(getNameStart(), Str, StrLen - 1) == 0;
  }

  bool isStr(llvm::StringRef Str) const {
    llvm::StringRef ThisStr(getNameStart(), getLength());
    return ThisStr == Str;
  }

  const char *getNameStart() const { return Entry->getKeyData(); }

  unsigned getLength() const { return Entry->getKeyLength(); }

  llvm::StringRef getName() const {
    return llvm::StringRef(getNameStart(), getLength());
  }

  bool hasMacroDefinition() const { return HasMacro; }
  void setHasMacroDefinition(bool Val) {
    if (HasMacro == Val)
      return;

    HasMacro = Val;
    if (Val) {
      NeedsIdentifierProcessing = true;
      HadMacro = true;
    } else {
      // If this is a final macro, make the deprecation and header unsafe bits
      // stick around after the undefinition so they apply to any redefinitions.
      if (!IsFinal) {
        // Because calling the setters of these calls recomputes, just set them
        // manually to avoid recomputing a bunch of times.
        IsDeprecatedMacro = false;
        IsRestrictExpansion = false;
      }
      RecomputeNeedsIdentifierProcessing();
    }
  }
  bool hadMacroDefinition() const { return HadMacro; }

  bool isDeprecatedMacro() const { return IsDeprecatedMacro; }

  void setIsDeprecatedMacro(bool Val) {
    if (IsDeprecatedMacro == Val)
      return;
    IsDeprecatedMacro = Val;
    if (Val)
      NeedsIdentifierProcessing = true;
    else
      RecomputeNeedsIdentifierProcessing();
  }

  bool isRestrictExpansion() const { return IsRestrictExpansion; }

  void setIsRestrictExpansion(bool Val) {
    if (IsRestrictExpansion == Val)
      return;
    IsRestrictExpansion = Val;
    if (Val)
      NeedsIdentifierProcessing = true;
    else
      RecomputeNeedsIdentifierProcessing();
  }

  bool isFinal() const { return IsFinal; }

  void setIsFinal(bool Val) { IsFinal = Val; }

  tok::TokenKind getTokenID() const { return (tok::TokenKind)TokenID; }

  bool hasRevertedTokenIDToIdentifier() const { return RevertedTokenID; }

  void revertTokenIDToIdentifier() {
    assert(TokenID != tok::identifier && "Already at tok::identifier");
    TokenID = tok::identifier;
    RevertedTokenID = true;
  }
  void revertIdentifierToTokenID(tok::TokenKind TK) {
    assert(TokenID == tok::identifier && "Should be at tok::identifier");
    TokenID = TK;
    RevertedTokenID = false;
  }

  tok::PPKeywordKind getPPKeywordID() const;

  unsigned getBuiltinID() const {
    if (BuiltinOrExtraID >= FirstBuiltinID)
      return 1 + (BuiltinOrExtraID - FirstBuiltinID);
    else
      return 0;
  }
  void setBuiltinID(unsigned ID) {
    assert(ID != 0);
    BuiltinOrExtraID = FirstBuiltinID + (ID - 1);
    assert(getBuiltinID() == ID && "ID too large for field!");
  }
  void clearBuiltinID() { BuiltinOrExtraID = 0; }

  tok::InterestingIdentifierKind getInterestingIdentifierID() const {
    if (BuiltinOrExtraID >= FirstInterestingIdentifierID &&
        BuiltinOrExtraID <= LastInterestingIdentifierID)
      return tok::InterestingIdentifierKind(
          1 + (BuiltinOrExtraID - FirstInterestingIdentifierID));
    else
      return tok::not_interesting;
  }
  void setInterestingIdentifierID(unsigned ID) {
    assert(ID != tok::not_interesting);
    BuiltinOrExtraID = FirstInterestingIdentifierID + (ID - 1);
    assert(getInterestingIdentifierID() == ID && "ID too large for field!");
  }

  unsigned getRawBuiltinOrExtraID() const { return BuiltinOrExtraID; }
  void setRawBuiltinOrExtraID(unsigned ID) { BuiltinOrExtraID = ID; }

  bool isExtensionToken() const { return IsExtension; }
  void setIsExtensionToken(bool Val) {
    IsExtension = Val;
    if (Val)
      NeedsIdentifierProcessing = true;
    else
      RecomputeNeedsIdentifierProcessing();
  }

  bool isFutureCompatKeyword() const { return IsFutureCompatKeyword; }
  void setIsFutureCompatKeyword(bool Val) {
    IsFutureCompatKeyword = Val;
    if (Val)
      NeedsIdentifierProcessing = true;
    else
      RecomputeNeedsIdentifierProcessing();
  }

  void setIsPoisoned(bool Value = true) {
    IsPoisoned = Value;
    if (Value)
      NeedsIdentifierProcessing = true;
    else
      RecomputeNeedsIdentifierProcessing();
  }

  bool isPoisoned() const { return IsPoisoned; }

  bool isKeyword(const LangOptions &LangOpts) const;

  void *getFETokenInfo() const { return FETokenInfo; }
  void setFETokenInfo(void *T) { FETokenInfo = T; }

  bool needsIdentifierProcessing() const { return NeedsIdentifierProcessing; }

  ReservedIdentifierStatus isReserved(const LangOptions &LangOpts) const;

  llvm::StringRef deuglifiedName() const;
  bool isPlaceholder() const {
    return getLength() == 1 && getNameStart()[0] == '_';
  }

  bool operator<(const IdentifierInfo &RHS) const {
    return getName() < RHS.getName();
  }

private:
  void RecomputeNeedsIdentifierProcessing() {
    NeedsIdentifierProcessing = isPoisoned() || hasMacroDefinition() ||
                                isExtensionToken() || isFutureCompatKeyword();
  }
};

class PoisonIdentifierRAIIObject {
  IdentifierInfo *const II;
  const bool OldValue;

public:
  PoisonIdentifierRAIIObject(IdentifierInfo *II, bool NewValue)
      : II(II), OldValue(II ? II->isPoisoned() : false) {
    if (II)
      II->setIsPoisoned(NewValue);
  }

  ~PoisonIdentifierRAIIObject() {
    if (II)
      II->setIsPoisoned(OldValue);
  }
};

class IdentifierIterator {
protected:
  IdentifierIterator() = default;

public:
  IdentifierIterator(const IdentifierIterator &) = delete;
  IdentifierIterator &operator=(const IdentifierIterator &) = delete;

  virtual ~IdentifierIterator();

  virtual llvm::StringRef Next() = 0;
};

class IdentifierInfoLookup {
public:
  virtual ~IdentifierInfoLookup();

  virtual IdentifierInfo *get(llvm::StringRef Name) = 0;

  virtual IdentifierIterator *getIdentifiers();
};

class IdentifierTable {
  // Shark shows that using MallocAllocator is *much* slower than using this
  // BumpPtrAllocator!
  using HashTableTy = llvm::StringMap<IdentifierInfo *, llvm::BumpPtrAllocator>;
  HashTableTy HashTable;

  IdentifierInfoLookup *ExternalLookup;

public:
  explicit IdentifierTable(IdentifierInfoLookup *ExternalLookup = nullptr);

  explicit IdentifierTable(const LangOptions &LangOpts,
                           IdentifierInfoLookup *ExternalLookup = nullptr);

  void setExternalIdentifierLookup(IdentifierInfoLookup *IILookup) {
    ExternalLookup = IILookup;
  }

  IdentifierInfoLookup *getExternalIdentifierLookup() const {
    return ExternalLookup;
  }

  llvm::BumpPtrAllocator &getAllocator() { return HashTable.getAllocator(); }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  IdentifierInfo &get(llvm::StringRef Name) {
    auto It = HashTable.find(Name);
    if (LLVM_LIKELY(It != HashTable.end()))
      return *It->second;
    return getOrCreateIdentifier(Name);
  }

  LLVM_ATTRIBUTE_NOINLINE IdentifierInfo &
  getOrCreateIdentifier(llvm::StringRef Name);

  IdentifierInfo &get(llvm::StringRef Name, tok::TokenKind TokenCode) {
    IdentifierInfo &II = get(Name);
    II.TokenID = TokenCode;
    assert(II.TokenID == (unsigned)TokenCode && "TokenCode too large");
    return II;
  }

  IdentifierInfo &getOwn(llvm::StringRef Name) {
    auto &Entry = *HashTable.insert(std::make_pair(Name, nullptr)).first;

    IdentifierInfo *&II = Entry.second;
    if (II)
      return *II;

    // Lookups failed, make a new IdentifierInfo.
    void *Mem = getAllocator().Allocate<IdentifierInfo>();
    II = new (Mem) IdentifierInfo();

    // Make sure getName() knows how to find the IdentifierInfo
    // contents.
    II->Entry = &Entry;

    return *II;
  }

  using iterator = HashTableTy::const_iterator;
  using const_iterator = HashTableTy::const_iterator;

  iterator begin() const { return HashTable.begin(); }
  iterator end() const { return HashTable.end(); }
  unsigned size() const { return HashTable.size(); }

  iterator find(llvm::StringRef Name) const { return HashTable.find(Name); }

  void PrintStats() const;

  void AddKeywords(const LangOptions &LangOpts);

  diag::kind getFutureCompatDiagKind(const IdentifierInfo &II,
                                     const LangOptions &LangOpts);
};

} // namespace neverc

#endif // NEVERC_BASIC_IDENTIFIERTABLE_H
