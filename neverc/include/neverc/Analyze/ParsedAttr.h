#ifndef NEVERC_SEMA_PARSEDATTR_H
#define NEVERC_SEMA_PARSEDATTR_H

#include "neverc/Analyze/Ownership.h"
#include "neverc/Foundation/Attr/AttrSubjectMatchRules.h"
#include "neverc/Foundation/Attr/AttributeCommonInfo.h"
#include "neverc/Foundation/Attr/ParsedAttrInfo.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/VersionTuple.h"
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <utility>

namespace neverc {

using llvm::SmallVectorImpl;

class TreeContext;
class Decl;
class Expr;
class IdentifierInfo;
class LangOptions;
class Sema;
class Stmt;
class TargetInfo;

struct AvailabilityChange {
  SourceLocation KeywordLoc;

  llvm::VersionTuple Version;

  SourceRange VersionRange;

  bool isValid() const { return !Version.empty(); }
};

namespace detail {
enum AvailabilitySlot {
  IntroducedSlot,
  DeprecatedSlot,
  ObsoletedSlot,
  NumAvailabilitySlots
};

struct AvailabilityData {
  AvailabilityChange Changes[NumAvailabilitySlots];
  SourceLocation StrictLoc;
  const Expr *Replacement;

  AvailabilityData(const AvailabilityChange &Introduced,
                   const AvailabilityChange &Deprecated,
                   const AvailabilityChange &Obsoleted, SourceLocation Strict,
                   const Expr *ReplaceExpr)
      : StrictLoc(Strict), Replacement(ReplaceExpr) {
    Changes[IntroducedSlot] = Introduced;
    Changes[DeprecatedSlot] = Deprecated;
    Changes[ObsoletedSlot] = Obsoleted;
  }
};

struct TypeTagForDatatypeData {
  ParsedType MatchingCType;
  unsigned LayoutCompatible : 1;
  unsigned MustBeNull : 1;
};

} // namespace detail

struct IdentifierLoc {
  SourceLocation Loc;
  IdentifierInfo *Ident;

  static IdentifierLoc *create(TreeContext &Ctx, SourceLocation Loc,
                               IdentifierInfo *Ident);
};

using ArgsUnion = llvm::PointerUnion<Expr *, IdentifierLoc *>;
using ArgsVector = llvm::SmallVector<ArgsUnion, 12U>;

class ParsedAttr final : public AttributeCommonInfo,
                         private llvm::TrailingObjects<
                             ParsedAttr, ArgsUnion, detail::AvailabilityData,
                             detail::TypeTagForDatatypeData, ParsedType> {
  friend TrailingObjects;

  size_t numTrailingObjects(OverloadToken<ArgsUnion>) const { return NumArgs; }
  size_t numTrailingObjects(OverloadToken<detail::AvailabilityData>) const {
    return IsAvailability;
  }
  size_t
  numTrailingObjects(OverloadToken<detail::TypeTagForDatatypeData>) const {
    return IsTypeTagForDatatype;
  }
  size_t numTrailingObjects(OverloadToken<ParsedType>) const {
    return HasParsedType;
  }

private:
  IdentifierInfo *MacroII = nullptr;
  SourceLocation MacroExpansionLoc;

  unsigned NumArgs : 16;

  mutable unsigned Invalid : 1;

  mutable unsigned UsedAsTypeAttr : 1;

  unsigned IsAvailability : 1;

  unsigned IsTypeTagForDatatype : 1;

  unsigned HasParsedType : 1;

  mutable unsigned HasProcessingCache : 1;

  mutable unsigned ProcessingCache : 8;

  mutable unsigned IsPragmaAttribute : 1;

  SourceLocation UnavailableLoc;

  const Expr *MessageExpr;

  const ParsedAttrInfo &Info;

  ArgsUnion *getArgsBuffer() { return getTrailingObjects<ArgsUnion>(); }
  ArgsUnion const *getArgsBuffer() const {
    return getTrailingObjects<ArgsUnion>();
  }

  detail::AvailabilityData *getAvailabilityData() {
    return getTrailingObjects<detail::AvailabilityData>();
  }
  const detail::AvailabilityData *getAvailabilityData() const {
    return getTrailingObjects<detail::AvailabilityData>();
  }

private:
  friend class AttributeFactory;
  friend class AttributePool;

  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             ArgsUnion *args, unsigned numArgs, Form formUsed)
      : AttributeCommonInfo(attrName, scopeName, attrRange, scopeLoc, formUsed),
        NumArgs(numArgs), Invalid(false), UsedAsTypeAttr(false),
        IsAvailability(false), IsTypeTagForDatatype(false),
        HasParsedType(false), HasProcessingCache(false),
        IsPragmaAttribute(false), Info(ParsedAttrInfo::get(*this)) {
    if (numArgs)
      memcpy(getArgsBuffer(), args, numArgs * sizeof(ArgsUnion));
  }

  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             IdentifierLoc *Parm, const AvailabilityChange &introduced,
             const AvailabilityChange &deprecated,
             const AvailabilityChange &obsoleted, SourceLocation unavailable,
             const Expr *messageExpr, Form formUsed, SourceLocation strict,
             const Expr *replacementExpr)
      : AttributeCommonInfo(attrName, scopeName, attrRange, scopeLoc, formUsed),
        NumArgs(1), Invalid(false), UsedAsTypeAttr(false), IsAvailability(true),
        IsTypeTagForDatatype(false), HasParsedType(false),
        HasProcessingCache(false), IsPragmaAttribute(false),
        UnavailableLoc(unavailable), MessageExpr(messageExpr),
        Info(ParsedAttrInfo::get(*this)) {
    ArgsUnion PVal(Parm);
    memcpy(getArgsBuffer(), &PVal, sizeof(ArgsUnion));
    new (getAvailabilityData()) detail::AvailabilityData(
        introduced, deprecated, obsoleted, strict, replacementExpr);
  }

  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             IdentifierLoc *ArgKind, ParsedType matchingCType,
             bool layoutCompatible, bool mustBeNull, Form formUsed)
      : AttributeCommonInfo(attrName, scopeName, attrRange, scopeLoc, formUsed),
        NumArgs(1), Invalid(false), UsedAsTypeAttr(false),
        IsAvailability(false), IsTypeTagForDatatype(true), HasParsedType(false),
        HasProcessingCache(false), IsPragmaAttribute(false),
        Info(ParsedAttrInfo::get(*this)) {
    ArgsUnion PVal(ArgKind);
    memcpy(getArgsBuffer(), &PVal, sizeof(ArgsUnion));
    detail::TypeTagForDatatypeData &ExtraData = getTypeTagForDatatypeDataSlot();
    new (&ExtraData.MatchingCType) ParsedType(matchingCType);
    ExtraData.LayoutCompatible = layoutCompatible;
    ExtraData.MustBeNull = mustBeNull;
  }

  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             ParsedType typeArg, Form formUsed)
      : AttributeCommonInfo(attrName, scopeName, attrRange, scopeLoc, formUsed),
        NumArgs(0), Invalid(false), UsedAsTypeAttr(false),
        IsAvailability(false), IsTypeTagForDatatype(false), HasParsedType(true),
        HasProcessingCache(false), IsPragmaAttribute(false),
        Info(ParsedAttrInfo::get(*this)) {
    new (&getTypeBuffer()) ParsedType(typeArg);
  }

  detail::TypeTagForDatatypeData &getTypeTagForDatatypeDataSlot() {
    return *getTrailingObjects<detail::TypeTagForDatatypeData>();
  }
  const detail::TypeTagForDatatypeData &getTypeTagForDatatypeDataSlot() const {
    return *getTrailingObjects<detail::TypeTagForDatatypeData>();
  }

  ParsedType &getTypeBuffer() { return *getTrailingObjects<ParsedType>(); }
  const ParsedType &getTypeBuffer() const {
    return *getTrailingObjects<ParsedType>();
  }

  size_t allocated_size() const;

public:
  ParsedAttr(const ParsedAttr &) = delete;
  ParsedAttr(ParsedAttr &&) = delete;
  ParsedAttr &operator=(const ParsedAttr &) = delete;
  ParsedAttr &operator=(ParsedAttr &&) = delete;
  ~ParsedAttr() = delete;

  void operator delete(void *) = delete;

  bool hasParsedType() const { return HasParsedType; }

  bool isInvalid() const { return Invalid; }
  void setInvalid(bool b = true) const { Invalid = b; }

  bool hasProcessingCache() const { return HasProcessingCache; }

  unsigned getProcessingCache() const {
    assert(hasProcessingCache());
    return ProcessingCache;
  }

  void setProcessingCache(unsigned value) const {
    ProcessingCache = value;
    HasProcessingCache = true;
  }

  bool isUsedAsTypeAttr() const { return UsedAsTypeAttr; }
  void setUsedAsTypeAttr(bool Used = true) { UsedAsTypeAttr = Used; }

  bool isPragmaAttribute() const { return IsPragmaAttribute; }

  void setIsPragmaAttribute() { IsPragmaAttribute = true; }

  unsigned getNumArgs() const { return NumArgs; }

  ArgsUnion getArg(unsigned Arg) const {
    assert(Arg < NumArgs && "Arg access out of range!");
    return getArgsBuffer()[Arg];
  }

  bool isArgExpr(unsigned Arg) const {
    return Arg < NumArgs && getArg(Arg).is<Expr *>();
  }

  Expr *getArgAsExpr(unsigned Arg) const { return getArg(Arg).get<Expr *>(); }

  bool isArgIdent(unsigned Arg) const {
    return Arg < NumArgs && getArg(Arg).is<IdentifierLoc *>();
  }

  IdentifierLoc *getArgAsIdent(unsigned Arg) const {
    return getArg(Arg).get<IdentifierLoc *>();
  }

  const AvailabilityChange &getAvailabilityIntroduced() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return getAvailabilityData()->Changes[detail::IntroducedSlot];
  }

  const AvailabilityChange &getAvailabilityDeprecated() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return getAvailabilityData()->Changes[detail::DeprecatedSlot];
  }

  const AvailabilityChange &getAvailabilityObsoleted() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return getAvailabilityData()->Changes[detail::ObsoletedSlot];
  }

  SourceLocation getStrictLoc() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return getAvailabilityData()->StrictLoc;
  }

  SourceLocation getUnavailableLoc() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return UnavailableLoc;
  }

  const Expr *getMessageExpr() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return MessageExpr;
  }

  const Expr *getReplacementExpr() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return getAvailabilityData()->Replacement;
  }

  const ParsedType &getMatchingCType() const {
    assert(getParsedKind() == AT_TypeTagForDatatype &&
           "Not a type_tag_for_datatype attribute");
    return getTypeTagForDatatypeDataSlot().MatchingCType;
  }

  bool getLayoutCompatible() const {
    assert(getParsedKind() == AT_TypeTagForDatatype &&
           "Not a type_tag_for_datatype attribute");
    return getTypeTagForDatatypeDataSlot().LayoutCompatible;
  }

  bool getMustBeNull() const {
    assert(getParsedKind() == AT_TypeTagForDatatype &&
           "Not a type_tag_for_datatype attribute");
    return getTypeTagForDatatypeDataSlot().MustBeNull;
  }

  const ParsedType &getTypeArg() const {
    assert(HasParsedType && "Not a type attribute");
    return getTypeBuffer();
  }

  void setMacroIdentifier(IdentifierInfo *MacroName, SourceLocation Loc) {
    MacroII = MacroName;
    MacroExpansionLoc = Loc;
  }

  bool hasMacroIdentifier() const { return MacroII != nullptr; }

  IdentifierInfo *getMacroIdentifier() const { return MacroII; }

  SourceLocation getMacroExpansionLoc() const {
    assert(hasMacroIdentifier() && "Can only get the macro expansion location "
                                   "if this attribute has a macro identifier.");
    return MacroExpansionLoc;
  }

  bool checkExactlyNumArgs(class Sema &S, unsigned Num) const;
  bool checkAtLeastNumArgs(class Sema &S, unsigned Num) const;
  bool checkAtMostNumArgs(class Sema &S, unsigned Num) const;

  bool isTargetSpecificAttr() const;
  bool isTypeAttr() const;
  bool isStmtAttr() const;

  bool hasCustomParsing() const;
  bool isParamExpr(size_t N) const;
  unsigned getMinArgs() const;
  unsigned getMaxArgs() const;
  unsigned getNumArgMembers() const;
  bool hasVariadicArg() const;
  bool diagnoseAppertainsTo(class Sema &S, const Decl *D) const;
  bool diagnoseAppertainsTo(class Sema &S, const Stmt *St) const;
  bool diagnoseMutualExclusion(class Sema &S, const Decl *D) const;
  // Stmt overload: mutual-exclusion rules are declaration-only in C mode.
  bool diagnoseMutualExclusion(class Sema &S, const Stmt *St) const {
    return true;
  }
  bool appliesToDecl(const Decl *D, attr::SubjectMatchRule MatchRule) const;
  void
  getMatchRules(const LangOptions &LangOpts,
                llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
                    &MatchRules) const;
  bool diagnoseLangOpts(class Sema &S) const;
  bool existsInTarget(const TargetInfo &Target) const;
  bool isKnownToGCC() const;
  bool isSupportedByPragmaAttribute() const;

  bool slidesFromDeclToDeclSpecLegacyBehavior() const;

  unsigned getSemanticSpelling() const;

  AttributeCommonInfo::Kind getKind() const {
    return AttributeCommonInfo::Kind(Info.AttrKind);
  }
  const ParsedAttrInfo &getInfo() const { return Info; }
};

class AttributePool;
class AttributeFactory {
public:
  enum {
    AvailabilityAllocSize =
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData,
                                     ParsedType>(1, 1, 0, 0),
    TypeTagForDatatypeAllocSize =
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData,
                                     ParsedType>(1, 0, 1, 0),
  };

private:
  enum {
    /// The number of free lists we want to be sure to support
    /// inline.  This is just enough that availability attributes
    /// don't surpass it.  It's actually very unlikely we'll see an
    /// attribute that needs more than that; on x86_64 you'd need 10
    /// expression arguments to overflow this.
    InlineFreeListsCapacity =
        1 + (AvailabilityAllocSize - sizeof(ParsedAttr)) / sizeof(void *)
  };

  llvm::BumpPtrAllocator Alloc;

  llvm::SmallVector<llvm::SmallVector<ParsedAttr *, 8>, InlineFreeListsCapacity>
      FreeLists;

  // The following are the private interface used by AttributePool.
  friend class AttributePool;

  void *allocate(size_t size);

  void deallocate(ParsedAttr *AL);

  void reclaimPool(AttributePool &head);

public:
  AttributeFactory();
  ~AttributeFactory();
};

class AttributePool {
  friend class AttributeFactory;
  friend class ParsedAttributes;
  AttributeFactory &Factory;
  llvm::SmallVector<ParsedAttr *> Attrs;

  void *allocate(size_t size) { return Factory.allocate(size); }

  ParsedAttr *add(ParsedAttr *attr) {
    Attrs.push_back(attr);
    return attr;
  }

  void remove(ParsedAttr *attr) {
    assert(llvm::is_contained(Attrs, attr) &&
           "Can't take attribute from a pool that doesn't own it!");
    Attrs.erase(llvm::find(Attrs, attr));
  }

  void takePool(AttributePool &pool);

public:
  AttributePool(AttributeFactory &factory) : Factory(factory) {}

  AttributePool(const AttributePool &) = delete;
  // The copy assignment operator is defined as deleted pending further
  // motivation.
  AttributePool &operator=(const AttributePool &) = delete;

  ~AttributePool() {
    if (LLVM_UNLIKELY(!Attrs.empty()))
      Factory.reclaimPool(*this);
  }

  AttributePool(AttributePool &&pool) = default;

  // The move assignment operator is defined as deleted pending further
  // motivation.
  AttributePool &operator=(AttributePool &&pool) = delete;

  AttributeFactory &getFactory() const { return Factory; }

  void clear() {
    Factory.reclaimPool(*this);
    Attrs.clear();
  }

  void takeAllFrom(AttributePool &pool) {
    takePool(pool);
    pool.Attrs.clear();
  }

  ParsedAttr *create(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     ArgsUnion *args, unsigned numArgs, ParsedAttr::Form form) {
    size_t temp =
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData,
                                     ParsedType>(numArgs, 0, 0, 0);
    (void)temp;
    void *memory = allocate(
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData,
                                     ParsedType>(numArgs, 0, 0, 0));
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       args, numArgs, form));
  }

  ParsedAttr *create(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierLoc *Param, const AvailabilityChange &introduced,
                     const AvailabilityChange &deprecated,
                     const AvailabilityChange &obsoleted,
                     SourceLocation unavailable, const Expr *MessageExpr,
                     ParsedAttr::Form form, SourceLocation strict,
                     const Expr *ReplacementExpr) {
    void *memory = allocate(AttributeFactory::AvailabilityAllocSize);
    return add(new (memory) ParsedAttr(
        attrName, attrRange, scopeName, scopeLoc, Param, introduced, deprecated,
        obsoleted, unavailable, MessageExpr, form, strict, ReplacementExpr));
  }

  ParsedAttr *
  createTypeTagForDatatype(IdentifierInfo *attrName, SourceRange attrRange,
                           IdentifierInfo *scopeName, SourceLocation scopeLoc,
                           IdentifierLoc *argumentKind,
                           ParsedType matchingCType, bool layoutCompatible,
                           bool mustBeNull, ParsedAttr::Form form) {
    void *memory = allocate(AttributeFactory::TypeTagForDatatypeAllocSize);
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       argumentKind, matchingCType,
                                       layoutCompatible, mustBeNull, form));
  }

  ParsedAttr *createTypeAttribute(IdentifierInfo *attrName,
                                  SourceRange attrRange,
                                  IdentifierInfo *scopeName,
                                  SourceLocation scopeLoc, ParsedType typeArg,
                                  ParsedAttr::Form formUsed) {
    void *memory = allocate(
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData,
                                     ParsedType>(0, 0, 0, 1));
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       typeArg, formUsed));
  }
};

class ParsedAttributesView {
  using VecTy = llvm::SmallVector<ParsedAttr *>;
  using SizeType = decltype(std::declval<VecTy>().size());

public:
  SourceRange Range;

  static const ParsedAttributesView &none() {
    static const ParsedAttributesView Attrs;
    return Attrs;
  }

  bool empty() const { return AttrList.empty(); }
  SizeType size() const { return AttrList.size(); }
  ParsedAttr &operator[](SizeType pos) { return *AttrList[pos]; }
  const ParsedAttr &operator[](SizeType pos) const { return *AttrList[pos]; }

  void addAtEnd(ParsedAttr *newAttr) {
    assert(newAttr);
    AttrList.push_back(newAttr);
  }

  void remove(ParsedAttr *ToBeRemoved) {
    assert(is_contained(AttrList, ToBeRemoved) &&
           "Cannot remove attribute that isn't in the list");
    AttrList.erase(llvm::find(AttrList, ToBeRemoved));
  }

  void clearListOnly() { AttrList.clear(); }

  struct iterator : llvm::iterator_adaptor_base<iterator, VecTy::iterator,
                                                std::random_access_iterator_tag,
                                                ParsedAttr> {
    iterator() : iterator_adaptor_base(nullptr) {}
    iterator(VecTy::iterator I) : iterator_adaptor_base(I) {}
    reference operator*() const { return **I; }
    friend class ParsedAttributesView;
  };
  struct const_iterator
      : llvm::iterator_adaptor_base<const_iterator, VecTy::const_iterator,
                                    std::random_access_iterator_tag,
                                    ParsedAttr> {
    const_iterator() : iterator_adaptor_base(nullptr) {}
    const_iterator(VecTy::const_iterator I) : iterator_adaptor_base(I) {}

    reference operator*() const { return **I; }
    friend class ParsedAttributesView;
  };

  void addAll(iterator B, iterator E) {
    AttrList.insert(AttrList.begin(), B.I, E.I);
  }

  void addAll(const_iterator B, const_iterator E) {
    AttrList.insert(AttrList.begin(), B.I, E.I);
  }

  void addAllAtEnd(iterator B, iterator E) {
    AttrList.insert(AttrList.end(), B.I, E.I);
  }

  void addAllAtEnd(const_iterator B, const_iterator E) {
    AttrList.insert(AttrList.end(), B.I, E.I);
  }

  iterator begin() { return iterator(AttrList.begin()); }
  const_iterator begin() const { return const_iterator(AttrList.begin()); }
  iterator end() { return iterator(AttrList.end()); }
  const_iterator end() const { return const_iterator(AttrList.end()); }

  ParsedAttr &front() {
    assert(!empty());
    return *AttrList.front();
  }
  const ParsedAttr &front() const {
    assert(!empty());
    return *AttrList.front();
  }
  ParsedAttr &back() {
    assert(!empty());
    return *AttrList.back();
  }
  const ParsedAttr &back() const {
    assert(!empty());
    return *AttrList.back();
  }

  bool hasAttribute(ParsedAttr::Kind K) const {
    return llvm::any_of(AttrList, [K](const ParsedAttr *AL) {
      return AL->getParsedKind() == K;
    });
  }

private:
  VecTy AttrList;
};

struct ParsedAttributeArgumentsProperties {
  ParsedAttributeArgumentsProperties(uint32_t StringLiteralBits)
      : StringLiterals(StringLiteralBits) {}
  bool isStringLiteralArg(unsigned I) const {
    // If the last bit is set, assume we have a variadic parameter
    if (I >= StringLiterals.size())
      return StringLiterals.test(StringLiterals.size() - 1);
    return StringLiterals.test(I);
  }

private:
  std::bitset<32> StringLiterals;
};

class ParsedAttributes : public ParsedAttributesView {
public:
  ParsedAttributes(AttributeFactory &factory) : pool(factory) {}
  ParsedAttributes(const ParsedAttributes &) = delete;
  ParsedAttributes &operator=(const ParsedAttributes &) = delete;

  AttributePool &getPool() const { return pool; }

  void takeAllFrom(ParsedAttributes &Other) {
    assert(&Other != this &&
           "ParsedAttributes can't take attributes from itself");
    addAll(Other.begin(), Other.end());
    Other.clearListOnly();
    pool.takeAllFrom(Other.pool);
  }

  void takeOneFrom(ParsedAttributes &Other, ParsedAttr *PA) {
    assert(&Other != this &&
           "ParsedAttributes can't take attribute from itself");
    Other.getPool().remove(PA);
    Other.remove(PA);
    getPool().add(PA);
    addAtEnd(PA);
  }

  void clear() {
    clearListOnly();
    pool.clear();
    Range = SourceRange();
  }

  ParsedAttr *addNew(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     ArgsUnion *args, unsigned numArgs, ParsedAttr::Form form) {
    ParsedAttr *attr = pool.create(attrName, attrRange, scopeName, scopeLoc,
                                   args, numArgs, form);
    addAtEnd(attr);
    return attr;
  }

  ParsedAttr *addNew(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierLoc *Param, const AvailabilityChange &introduced,
                     const AvailabilityChange &deprecated,
                     const AvailabilityChange &obsoleted,
                     SourceLocation unavailable, const Expr *MessageExpr,
                     ParsedAttr::Form form, SourceLocation strict,
                     const Expr *ReplacementExpr) {
    ParsedAttr *attr = pool.create(
        attrName, attrRange, scopeName, scopeLoc, Param, introduced, deprecated,
        obsoleted, unavailable, MessageExpr, form, strict, ReplacementExpr);
    addAtEnd(attr);
    return attr;
  }

  ParsedAttr *
  addNewTypeTagForDatatype(IdentifierInfo *attrName, SourceRange attrRange,
                           IdentifierInfo *scopeName, SourceLocation scopeLoc,
                           IdentifierLoc *argumentKind,
                           ParsedType matchingCType, bool layoutCompatible,
                           bool mustBeNull, ParsedAttr::Form form) {
    ParsedAttr *attr = pool.createTypeTagForDatatype(
        attrName, attrRange, scopeName, scopeLoc, argumentKind, matchingCType,
        layoutCompatible, mustBeNull, form);
    addAtEnd(attr);
    return attr;
  }

  ParsedAttr *addNewTypeAttr(IdentifierInfo *attrName, SourceRange attrRange,
                             IdentifierInfo *scopeName, SourceLocation scopeLoc,
                             ParsedType typeArg, ParsedAttr::Form formUsed) {
    ParsedAttr *attr = pool.createTypeAttribute(attrName, attrRange, scopeName,
                                                scopeLoc, typeArg, formUsed);
    addAtEnd(attr);
    return attr;
  }

private:
  mutable AttributePool pool;
};

void takeAndConcatenateAttrs(ParsedAttributes &First, ParsedAttributes &Second,
                             ParsedAttributes &Result);

enum AttributeArgumentNType {
  AANT_ArgumentIntOrBool,
  AANT_ArgumentIntegerConstant,
  AANT_ArgumentString,
  AANT_ArgumentIdentifier,
  AANT_ArgumentConstantExpr,
  AANT_ArgumentBuiltinFunction,
};

enum AttributeDeclKind {
  ExpectedFunction,
  ExpectedUnion,
  ExpectedVariableOrFunction,
  ExpectedFunctionOrMethod,          // C-only: same as ExpectedFunction
  ExpectedFunctionMethodOrBlock,     // C-only: same as ExpectedFunction
  ExpectedFunctionMethodOrParameter, // C-only: functions and parameters
  ExpectedVariable,
  ExpectedVariableOrField,
  ExpectedVariableFieldOrTag,
  ExpectedTag,
  ExpectedFunctionVariableOrClass, // C-only: variables and functions
  ExpectedKernelFunction,
  ExpectedFunctionWithProtoType,
};

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const ParsedAttr &At) {
  DB.AddTaggedVal(reinterpret_cast<uint64_t>(At.getAttrName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const ParsedAttr *At) {
  DB.AddTaggedVal(reinterpret_cast<uint64_t>(At->getAttrName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

template <
    typename ACI,
    std::enable_if_t<std::is_same<ACI, AttributeCommonInfo>::value, int> = 0>
inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const ACI &CI) {
  DB.AddTaggedVal(reinterpret_cast<uint64_t>(CI.getAttrName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

template <
    typename ACI,
    std::enable_if_t<std::is_same<ACI, AttributeCommonInfo>::value, int> = 0>
inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const ACI *CI) {
  DB.AddTaggedVal(reinterpret_cast<uint64_t>(CI->getAttrName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

} // namespace neverc

#endif // NEVERC_SEMA_PARSEDATTR_H
