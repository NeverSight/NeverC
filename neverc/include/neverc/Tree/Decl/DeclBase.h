#ifndef NEVERC_TREE_DECLBASE_H
#define NEVERC_TREE_DECLBASE_H

#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Tree/Core/AttrIterator.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/VersionTuple.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

namespace neverc {

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

class TreeContext;
class TreeMutationListener;
class Attr;
class DeclContext;
class FunctionDecl;
class FunctionType;
enum class Linkage : unsigned char;
class NamedDecl;
struct PrintingPolicy;
class RecordDecl;
class SourceManager;
class Stmt;
class StoredDeclsMap;
class TranslationUnitDecl;

enum AvailabilityResult {
  AR_Available = 0,
  AR_NotYetIntroduced,
  AR_Deprecated,
  AR_Unavailable
};

class alignas(8) Decl {
public:
  enum Kind {
#define DECL(DERIVED, BASE) DERIVED,
#define ABSTRACT_DECL(DECL)
#define DECL_RANGE(BASE, START, END) first##BASE = START, last##BASE = END,
#define LAST_DECL_RANGE(BASE, START, END) first##BASE = START, last##BASE = END
#include "neverc/Tree/DeclNodes.td.h"
  };

  struct EmptyShell {};

  enum IdentifierNamespace {
    /// Labels, declared with 'x:' and referenced with 'goto x'.
    IDNS_Label = 0x0001,

    /// Tags, declared with 'struct foo;' and referenced with
    /// 'struct foo'.  All tags are also types.  This is what
    /// elaborated-type-specifiers look for in C.
    /// This also contains names that conflict with tags in the
    /// same scope but that are otherwise ordinary names (indirect
    /// field declarations).
    IDNS_Tag = 0x0002,

    /// Types, declared with 'struct foo', typedefs, etc.
    IDNS_Type = 0x0004,

    /// Members, declared with object declarations within tag
    /// definitions.  These can only be found by "qualified"
    /// lookup in member expressions.
    IDNS_Member = 0x0008,

    /// Ordinary names.  In C, everything that's not a label, tag,
    /// member, or function-local extern ends up here.
    IDNS_Ordinary = 0x0020,

    /// This declaration is a function-local extern declaration of a
    /// variable or function. This may also be IDNS_Ordinary if it
    /// has been declared outside any function.
    IDNS_LocalExtern = 0x0800,
  };

protected:
  Decl *NextInContext = nullptr;

private:
  friend class DeclContext;

  struct MultipleDC {
    DeclContext *SemanticDC;
    DeclContext *LexicalDC;
  };

  llvm::PointerUnion<DeclContext *, MultipleDC *> DeclCtx;

  bool isInSemaDC() const { return DeclCtx.is<DeclContext *>(); }
  bool isOutOfSemaDC() const { return DeclCtx.is<MultipleDC *>(); }

  MultipleDC *getMultipleDC() const { return DeclCtx.get<MultipleDC *>(); }

  DeclContext *getSemanticDC() const { return DeclCtx.get<DeclContext *>(); }

  SourceLocation Loc;

  LLVM_PREFERRED_TYPE(Kind)
  unsigned DeclKind : 7;

  LLVM_PREFERRED_TYPE(bool)
  unsigned InvalidDecl : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned HasAttrs : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned Implicit : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned Used : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned Referenced : 1;

  static bool StatisticsEnabled;

protected:
  friend class LinkageComputer;
  friend class RecordDecl;
  template <typename decl_type> friend class Redeclarable;

  // NOTE: MSVC treats enums as signed, avoid using the AccessSpecifier enum
  LLVM_PREFERRED_TYPE(AccessSpecifier)
  unsigned Access : 2;

  LLVM_PREFERRED_TYPE(IdentifierNamespace)
  unsigned IdentifierNamespace : 14;

  LLVM_PREFERRED_TYPE(Linkage)
  mutable unsigned CacheValidAndLinkage : 3;

  void *operator new(std::size_t Size, const TreeContext &Ctx,
                     DeclContext *Parent, std::size_t Extra = 0);

public:
  Decl() = delete;
  Decl(const Decl &) = delete;
  Decl(Decl &&) = delete;
  Decl &operator=(const Decl &) = delete;
  Decl &operator=(Decl &&) = delete;

protected:
  Decl(Kind DK, DeclContext *DC, SourceLocation L)
      : DeclCtx(DC), Loc(L), DeclKind(DK), InvalidDecl(false), HasAttrs(false),
        Implicit(false), Used(false), Referenced(false), Access(AS_none),
        IdentifierNamespace(getIdentifierNamespaceForKind(DK)),
        CacheValidAndLinkage(llvm::to_underlying(Linkage::Invalid)) {
    if (StatisticsEnabled)
      add(DK);
  }

  Decl(Kind DK, EmptyShell Empty)
      : DeclKind(DK), InvalidDecl(false), HasAttrs(false), Implicit(false),
        Used(false), Referenced(false), Access(AS_none),
        IdentifierNamespace(getIdentifierNamespaceForKind(DK)),
        CacheValidAndLinkage(llvm::to_underlying(Linkage::Invalid)) {
    if (StatisticsEnabled)
      add(DK);
  }

  virtual ~Decl();

  Linkage getCachedLinkage() const {
    return static_cast<Linkage>(CacheValidAndLinkage);
  }

  void setCachedLinkage(Linkage L) const {
    CacheValidAndLinkage = llvm::to_underlying(L);
  }

  bool hasCachedLinkage() const { return CacheValidAndLinkage; }

public:
  virtual SourceRange getSourceRange() const LLVM_READONLY {
    return SourceRange(getLocation(), getLocation());
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getSourceRange().getBegin();
  }

  SourceLocation getEndLoc() const LLVM_READONLY {
    return getSourceRange().getEnd();
  }

  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }

  Kind getKind() const { return static_cast<Kind>(DeclKind); }
  const char *getDeclKindName() const;

  Decl *getNextDeclInContext() { return NextInContext; }
  const Decl *getNextDeclInContext() const { return NextInContext; }

  DeclContext *getDeclContext() {
    if (isInSemaDC())
      return getSemanticDC();
    return getMultipleDC()->SemanticDC;
  }
  const DeclContext *getDeclContext() const {
    return const_cast<Decl *>(this)->getDeclContext();
  }

  DeclContext *getNonTransparentDeclContext();
  const DeclContext *getNonTransparentDeclContext() const {
    return const_cast<Decl *>(this)->getNonTransparentDeclContext();
  }

  TranslationUnitDecl *getTranslationUnitDecl();
  const TranslationUnitDecl *getTranslationUnitDecl() const {
    return const_cast<Decl *>(this)->getTranslationUnitDecl();
  }

  // Return true if this is a FileContext Decl.
  bool isFileContextDecl() const;

  TreeContext &getTreeContext() const LLVM_READONLY;

  const LangOptions &getLangOpts() const LLVM_READONLY;

  void setAccess(AccessSpecifier AS) { Access = AS; }

  AccessSpecifier getAccess() const { return AccessSpecifier(Access); }

  bool hasAttrs() const { return HasAttrs; }

  void setAttrs(const AttrVec &Attrs) {
    return setAttrsImpl(Attrs, getTreeContext());
  }

  AttrVec &getAttrs() {
    return const_cast<AttrVec &>(const_cast<const Decl *>(this)->getAttrs());
  }

  const AttrVec &getAttrs() const;
  void dropAttrs();
  void addAttr(Attr *A);

  using attr_iterator = AttrVec::const_iterator;
  using attr_range = llvm::iterator_range<attr_iterator>;

  attr_range attrs() const { return attr_range(attr_begin(), attr_end()); }

  attr_iterator attr_begin() const {
    return hasAttrs() ? getAttrs().begin() : nullptr;
  }
  attr_iterator attr_end() const {
    return hasAttrs() ? getAttrs().end() : nullptr;
  }

  template <typename T> void dropAttr() {
    if (!HasAttrs)
      return;

    AttrVec &Vec = getAttrs();
    llvm::erase_if(Vec, [](Attr *A) { return isa<T>(A); });

    if (Vec.empty())
      HasAttrs = false;
  }

  template <typename T>
  llvm::iterator_range<specific_attr_iterator<T>> specific_attrs() const {
    return llvm::make_range(specific_attr_begin<T>(), specific_attr_end<T>());
  }

  template <typename T> specific_attr_iterator<T> specific_attr_begin() const {
    return specific_attr_iterator<T>(attr_begin());
  }

  template <typename T> specific_attr_iterator<T> specific_attr_end() const {
    return specific_attr_iterator<T>(attr_end());
  }

  template <typename T> T *getAttr() const {
    return hasAttrs() ? getSpecificAttr<T>(getAttrs()) : nullptr;
  }

  template <typename T> bool hasAttr() const {
    return hasAttrs() && hasSpecificAttr<T>(getAttrs());
  }

  unsigned getMaxAlignment() const;

  void setInvalidDecl(bool Invalid = true);
  bool isInvalidDecl() const { return (bool)InvalidDecl; }

  bool isImplicit() const { return Implicit; }
  void setImplicit(bool I = true) { Implicit = I; }

  bool isUsed(bool CheckUsedAttr = true) const;

  void setIsUsed() { getCanonicalDecl()->Used = true; }

  void markUsed(TreeContext &C);

  bool isReferenced() const;

  bool isThisDeclarationReferenced() const { return Referenced; }

  void setReferenced(bool R = true) { Referenced = R; }

  bool hasDefiningAttr() const;

  const Attr *getDefiningAttr() const;

public:
  AvailabilityResult
  getAvailability(std::string *Message = nullptr,
                  llvm::VersionTuple EnclosingVersion = llvm::VersionTuple(),
                  llvm::StringRef *RealizedPlatform = nullptr) const;

  llvm::VersionTuple getVersionIntroduced() const;

  bool isDeprecated(std::string *Message = nullptr) const {
    return getAvailability(Message) == AR_Deprecated;
  }

  bool isUnavailable(std::string *Message = nullptr) const {
    return getAvailability(Message) == AR_Unavailable;
  }

  bool isWeakImported() const;

  bool canBeWeakImported(bool &IsDefinition) const;

  unsigned getIdentifierNamespace() const { return IdentifierNamespace; }

  bool isInIdentifierNamespace(unsigned NS) const {
    return getIdentifierNamespace() & NS;
  }

  static unsigned getIdentifierNamespaceForKind(Kind DK);

  bool hasTagIdentifierNamespace() const {
    return isTagIdentifierNamespace(getIdentifierNamespace());
  }

  static bool isTagIdentifierNamespace(unsigned NS) {
    return NS == (IDNS_Tag | IDNS_Type);
  }

  DeclContext *getLexicalDeclContext() {
    if (isInSemaDC())
      return getSemanticDC();
    return getMultipleDC()->LexicalDC;
  }
  const DeclContext *getLexicalDeclContext() const {
    return const_cast<Decl *>(this)->getLexicalDeclContext();
  }

  void setDeclContext(DeclContext *DC);

  void setLexicalDeclContext(DeclContext *DC);

  bool isDefinedOutsideFunctionOrMethod() const {
    return getParentFunctionOrMethod() == nullptr;
  }

  const DeclContext *
  getParentFunctionOrMethod(bool LexicalParent = false) const;
  DeclContext *getParentFunctionOrMethod(bool LexicalParent = false) {
    return const_cast<DeclContext *>(
        const_cast<const Decl *>(this)->getParentFunctionOrMethod(
            LexicalParent));
  }

  virtual Decl *getCanonicalDecl() { return this; }
  const Decl *getCanonicalDecl() const {
    return const_cast<Decl *>(this)->getCanonicalDecl();
  }

  bool isCanonicalDecl() const { return getCanonicalDecl() == this; }

protected:
  virtual Decl *getNextRedeclarationImpl() { return this; }

  virtual Decl *getPreviousDeclImpl() { return nullptr; }

  virtual Decl *getMostRecentDeclImpl() { return this; }

public:
  class redecl_iterator {
    /// Current - The current declaration.
    Decl *Current = nullptr;
    Decl *Starter;

  public:
    using value_type = Decl *;
    using reference = const value_type &;
    using pointer = const value_type *;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;

    redecl_iterator() = default;
    explicit redecl_iterator(Decl *C) : Current(C), Starter(C) {}

    reference operator*() const { return Current; }
    value_type operator->() const { return Current; }

    redecl_iterator &operator++() {
      assert(Current && "Advancing while iterator has reached end");
      // Get either previous decl or latest decl.
      Decl *Next = Current->getNextRedeclarationImpl();
      assert(Next && "Should return next redeclaration or itself, never null!");
      Current = (Next != Starter) ? Next : nullptr;
      return *this;
    }

    redecl_iterator operator++(int) {
      redecl_iterator tmp(*this);
      ++(*this);
      return tmp;
    }

    friend bool operator==(redecl_iterator x, redecl_iterator y) {
      return x.Current == y.Current;
    }

    friend bool operator!=(redecl_iterator x, redecl_iterator y) {
      return x.Current != y.Current;
    }
  };

  using redecl_range = llvm::iterator_range<redecl_iterator>;

  redecl_range redecls() const {
    return redecl_range(redecls_begin(), redecls_end());
  }

  redecl_iterator redecls_begin() const {
    return redecl_iterator(const_cast<Decl *>(this));
  }

  redecl_iterator redecls_end() const { return redecl_iterator(); }

  Decl *getPreviousDecl() { return getPreviousDeclImpl(); }

  const Decl *getPreviousDecl() const {
    return const_cast<Decl *>(this)->getPreviousDeclImpl();
  }

  bool isFirstDecl() const { return getPreviousDecl() == nullptr; }

  Decl *getMostRecentDecl() { return getMostRecentDeclImpl(); }

  const Decl *getMostRecentDecl() const {
    return const_cast<Decl *>(this)->getMostRecentDeclImpl();
  }

  virtual Stmt *getBody() const { return nullptr; }

  virtual bool hasBody() const { return getBody() != nullptr; }

  SourceLocation getBodyRBrace() const;

  // global temp stats (until we have a per-module visitor)
  static void add(Kind k);
  static void EnableStatistics();
  static void PrintStats();

  FunctionDecl *getAsFunction() LLVM_READONLY;

  const FunctionDecl *getAsFunction() const {
    return const_cast<Decl *>(this)->getAsFunction();
  }

  void setLocalExternDecl() {
    Decl *Prev = getPreviousDecl();
    IdentifierNamespace &= ~IDNS_Ordinary;

    assert((IdentifierNamespace & ~IDNS_Tag) == 0 &&
           "namespace is not ordinary");

    IdentifierNamespace |= IDNS_LocalExtern;
    if (Prev && Prev->getIdentifierNamespace() & IDNS_Ordinary)
      IdentifierNamespace |= IDNS_Ordinary;
  }

  bool isLocalExternDecl() const {
    return IdentifierNamespace & IDNS_LocalExtern;
  }

  void clearIdentifierNamespace() { IdentifierNamespace = 0; }

  static bool classofKind(Kind K) { return true; }
  static DeclContext *castToDeclContext(const Decl *);
  static Decl *castFromDeclContext(const DeclContext *);

  void print(llvm::raw_ostream &Out, unsigned Indentation = 0) const;
  void print(llvm::raw_ostream &Out, const PrintingPolicy &Policy,
             unsigned Indentation = 0) const;
  static void printGroup(Decl **Begin, unsigned NumDecls,
                         llvm::raw_ostream &Out, const PrintingPolicy &Policy,
                         unsigned Indentation = 0);

  // Debuggers don't usually respect default arguments.
  void dump() const;

  void dump(llvm::raw_ostream &Out) const;

  int64_t getID() const;

  const FunctionType *getFunctionType() const;

  // Looks through the Decl's underlying type to determine if it's a
  // function pointer type.
  bool isFunctionPointerType() const;

private:
  void setAttrsImpl(const AttrVec &Attrs, TreeContext &Ctx);
  void setDeclContextsImpl(DeclContext *SemaDC, DeclContext *LexicalDC,
                           TreeContext &Ctx);

protected:
  TreeMutationListener *getTreeMutationListener() const;
};

inline bool declaresSameEntity(const Decl *D1, const Decl *D2) {
  if (!D1 || !D2)
    return false;

  if (D1 == D2)
    return true;

  return D1->getCanonicalDecl() == D2->getCanonicalDecl();
}

class PrettyStackTraceDecl : public llvm::PrettyStackTraceEntry {
  const Decl *TheDecl;
  SourceLocation Loc;
  SourceManager &SM;
  const char *Message;

public:
  PrettyStackTraceDecl(const Decl *theDecl, SourceLocation L, SourceManager &sm,
                       const char *Msg)
      : TheDecl(theDecl), Loc(L), SM(sm), Message(Msg) {}

  void print(llvm::raw_ostream &OS) const override;
};
} // namespace neverc

// Required to determine the layout of the PointerUnion<NamedDecl*> before
// seeing the NamedDecl definition being first used in DeclListNode::operator*.
namespace llvm {
template <> struct PointerLikeTypeTraits<::neverc::NamedDecl *> {
  static inline void *getAsVoidPointer(::neverc::NamedDecl *P) { return P; }
  static inline ::neverc::NamedDecl *getFromVoidPointer(void *P) {
    return static_cast<::neverc::NamedDecl *>(P);
  }
  static constexpr int NumLowBitsAvailable = 3;
};
} // namespace llvm

namespace neverc {
class DeclListNode {
  friend class TreeContext; // allocate, deallocate nodes.
  friend class StoredDeclsList;

public:
  using Decls = llvm::PointerUnion<NamedDecl *, DeclListNode *>;
  class iterator {
    friend class DeclContextLookupResult;
    friend class StoredDeclsList;

    Decls Ptr;
    iterator(Decls Node) : Ptr(Node) {}

  public:
    using difference_type = ptrdiff_t;
    using value_type = NamedDecl *;
    using pointer = void;
    using reference = value_type;
    using iterator_category = std::forward_iterator_tag;

    iterator() = default;

    reference operator*() const {
      assert(Ptr && "dereferencing end() iterator");
      if (DeclListNode *CurNode = Ptr.dyn_cast<DeclListNode *>())
        return CurNode->D;
      return Ptr.get<NamedDecl *>();
    }
    void operator->() const {} // Unsupported.
    bool operator==(const iterator &X) const { return Ptr == X.Ptr; }
    bool operator!=(const iterator &X) const { return Ptr != X.Ptr; }
    inline iterator &operator++() { // ++It
      assert(!Ptr.isNull() && "Advancing empty iterator");

      if (DeclListNode *CurNode = Ptr.dyn_cast<DeclListNode *>())
        Ptr = CurNode->Rest;
      else
        Ptr = nullptr;
      return *this;
    }
    iterator operator++(int) { // It++
      iterator temp = *this;
      ++(*this);
      return temp;
    }
    // Enables the pattern for (iterator I =..., E = I.end(); I != E; ++I)
    iterator end() { return iterator(); }
  };

private:
  NamedDecl *D = nullptr;
  Decls Rest = nullptr;
  DeclListNode(NamedDecl *ND) : D(ND) {}
};

class DeclContextLookupResult {
  using Decls = DeclListNode::Decls;

  Decls Result;

public:
  DeclContextLookupResult() = default;
  DeclContextLookupResult(Decls Result) : Result(Result) {}

  using iterator = DeclListNode::iterator;
  using const_iterator = iterator;
  using reference = iterator::reference;

  iterator begin() { return iterator(Result); }
  iterator end() { return iterator(); }
  const_iterator begin() const {
    return const_cast<DeclContextLookupResult *>(this)->begin();
  }
  const_iterator end() const { return iterator(); }

  bool empty() const { return Result.isNull(); }
  bool isSingleResult() const { return Result.dyn_cast<NamedDecl *>(); }
  reference front() const { return *begin(); }

  // Find the first declaration of the given type in the list. Note that this
  // is not in general the earliest-declared declaration, and should only be
  // used when it's not possible for there to be more than one match or where
  // it doesn't matter which one is found.
  template <class T> T *find_first() const {
    for (auto *D : *this)
      if (T *Decl = dyn_cast<T>(D))
        return Decl;

    return nullptr;
  }
};

class DeclContext {

  // We use uint64_t in the bit-fields below since some bit-fields
  // cross the unsigned boundary and this breaks the packing.

  class DeclContextBitfields {
    friend class DeclContext;
    /// DeclKind - This indicates which class this is.
    LLVM_PREFERRED_TYPE(Decl::Kind)
    uint64_t DeclKind : 7;

    /// If \c true, this context may have local lexical declarations
    /// that are missing from the lookup table.
    LLVM_PREFERRED_TYPE(bool)
    mutable uint64_t HasLazyLocalLexicalLookups : 1;

    /// If \c true, lookups should only return identifier from
    /// DeclContext scope (for example TranslationUnit). Used in
    /// LookupQualifiedName()
    LLVM_PREFERRED_TYPE(bool)
    mutable uint64_t UseQualifiedLookup : 1;
  };

  enum { NumDeclContextBits = 13 };

  class TagDeclBitfields {
    friend class TagDecl;
    /// For the bits in DeclContextBitfields
    LLVM_PREFERRED_TYPE(DeclContextBitfields)
    uint64_t : NumDeclContextBits;

    /// The TagKind enum.
    LLVM_PREFERRED_TYPE(TagTypeKind)
    uint64_t TagDeclKind : 3;

    /// True if this is a definition ("struct foo {};"), false if it is a
    /// declaration ("struct foo;").  It is not considered a definition
    /// until the definition has been fully processed.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t IsCompleteDefinition : 1;

    /// True if this is currently being defined.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t IsBeingDefined : 1;

    /// True if this tag declaration is "embedded" (i.e., defined or declared
    /// for the very first time) in the syntax of a declarator.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t IsEmbeddedInDeclarator : 1;

    /// True if this tag is free standing, e.g. "struct foo;".
    LLVM_PREFERRED_TYPE(bool)
    uint64_t IsFreeStanding : 1;

    /// Has the full definition of this type been required by a use somewhere in
    /// the TU.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t IsCompleteDefinitionRequired : 1;

    /// Whether this tag is a definition which was demoted due to
    /// a module merge.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t IsThisDeclarationADemotedDefinition : 1;
  };

  enum { NumTagDeclBits = NumDeclContextBits + 10 };

  class EnumDeclBitfields {
    friend class EnumDecl;
    /// For the bits in TagDeclBitfields.
    LLVM_PREFERRED_TYPE(TagDeclBitfields)
    uint64_t : NumTagDeclBits;

    /// Width in bits required to store all the non-negative
    /// enumerators of this enum.
    uint64_t NumPositiveBits : 8;

    /// Width in bits required to store all the negative
    /// enumerators of this enum.
    uint64_t NumNegativeBits : 8;

    /// True if this is an enumeration with fixed underlying type.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t IsFixed : 1;
  };

  enum { NumEnumDeclBits = NumTagDeclBits + 19 };

  class RecordDeclBitfields {
    friend class RecordDecl;
    /// For the bits in TagDeclBitfields.
    LLVM_PREFERRED_TYPE(TagDeclBitfields)
    uint64_t : NumTagDeclBits;

    /// This is true if this struct ends with a flexible
    /// array member (e.g. int X[]) or if this union contains a struct that
    /// does. If so, this cannot be contained in arrays or other structs as a
    /// member.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t HasFlexibleArrayMember : 1;

    /// Whether this is the type of an anonymous struct or union.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t AnonymousStructOrUnion : 1;

    /// This is true if struct has at least one member of
    /// 'volatile' type.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t HasVolatileMember : 1;

    /// Basic properties of non-trivial C structs.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t NonTrivialToPrimitiveDefaultInitialize : 1;
    LLVM_PREFERRED_TYPE(bool)
    uint64_t NonTrivialToPrimitiveCopy : 1;
    LLVM_PREFERRED_TYPE(bool)
    uint64_t NonTrivialToPrimitiveDestroy : 1;

    /// The following bits indicate whether this is or contains a C union that
    /// is non-trivial to default-initialize, destruct, or copy. These bits
    /// imply the associated basic non-triviality predicates declared above.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t HasNonTrivialToPrimitiveDefaultInitializeCUnion : 1;
    LLVM_PREFERRED_TYPE(bool)
    uint64_t HasNonTrivialToPrimitiveDestructCUnion : 1;
    LLVM_PREFERRED_TYPE(bool)
    uint64_t HasNonTrivialToPrimitiveCopyCUnion : 1;

    /// Indicates whether this struct has had its field layout randomized.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t IsRandomized : 1;
  };

  enum { NumRecordDeclBits = NumTagDeclBits + 11 };

  class FunctionDeclBitfields {
    friend class FunctionDecl;
    /// For the bits in DeclContextBitfields.
    LLVM_PREFERRED_TYPE(DeclContextBitfields)
    uint64_t : NumDeclContextBits;

    LLVM_PREFERRED_TYPE(StorageClass)
    uint64_t SClass : 3;
    LLVM_PREFERRED_TYPE(bool)
    uint64_t IsInline : 1;
    LLVM_PREFERRED_TYPE(bool)
    uint64_t IsInlineSpecified : 1;

    LLVM_PREFERRED_TYPE(bool)
    uint64_t HasInheritedPrototype : 1;
    LLVM_PREFERRED_TYPE(bool)
    uint64_t HasWrittenPrototype : 1;

    LLVM_PREFERRED_TYPE(bool)
    uint64_t HasImplicitReturnZero : 1;

    /// Kind of contexpr specifier as defined by ConstexprSpecKind.
    LLVM_PREFERRED_TYPE(ConstexprSpecKind)
    uint64_t ConstexprKind : 2;

    /// Indicates if the function uses __try.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t UsesSEHTry : 1;

    /// Indicates if the function declaration will
    /// have a body, once we're done parsing it.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t WillHaveBody : 1;

    /// Indicates that this function is a multiversioned
    /// function using attribute 'target'.
    LLVM_PREFERRED_TYPE(bool)
    uint64_t IsMultiVersion : 1;

    /// Indicates if the function uses Floating Point Constrained Intrinsics
    LLVM_PREFERRED_TYPE(bool)
    uint64_t UsesFPIntrin : 1;
  };

  enum { NumFunctionDeclBits = NumDeclContextBits + 15 };

  mutable StoredDeclsMap *LookupPtr = nullptr;

protected:
  union {
    DeclContextBitfields DeclContextBits;
    TagDeclBitfields TagDeclBits;
    EnumDeclBitfields EnumDeclBits;
    RecordDeclBitfields RecordDeclBits;
    FunctionDeclBitfields FunctionDeclBits;

    static_assert(sizeof(DeclContextBitfields) <= 8,
                  "DeclContextBitfields is larger than 8 bytes!");
    static_assert(sizeof(TagDeclBitfields) <= 8,
                  "TagDeclBitfields is larger than 8 bytes!");
    static_assert(sizeof(EnumDeclBitfields) <= 8,
                  "EnumDeclBitfields is larger than 8 bytes!");
    static_assert(sizeof(RecordDeclBitfields) <= 8,
                  "RecordDeclBitfields is larger than 8 bytes!");
    static_assert(sizeof(FunctionDeclBitfields) <= 8,
                  "FunctionDeclBitfields is larger than 8 bytes!");
  };

  mutable Decl *FirstDecl = nullptr;

  mutable Decl *LastDecl = nullptr;

  static std::pair<Decl *, Decl *> FormDeclChain(llvm::ArrayRef<Decl *> Decls,
                                                 bool FieldsAlreadyLoaded);

  DeclContext(Decl::Kind K);

public:
  ~DeclContext();

  // For use when debugging; hasValidDeclKind() will always return true for
  // a correctly constructed object within its lifetime.
  bool hasValidDeclKind() const;

  Decl::Kind getDeclKind() const {
    return static_cast<Decl::Kind>(DeclContextBits.DeclKind);
  }

  const char *getDeclKindName() const;

  DeclContext *getParent() { return cast<Decl>(this)->getDeclContext(); }
  const DeclContext *getParent() const {
    return const_cast<DeclContext *>(this)->getParent();
  }

  DeclContext *getLexicalParent() {
    return cast<Decl>(this)->getLexicalDeclContext();
  }
  const DeclContext *getLexicalParent() const {
    return const_cast<DeclContext *>(this)->getLexicalParent();
  }

  TreeContext &getParentTreeContext() const {
    return cast<Decl>(this)->getTreeContext();
  }

  bool isFunctionOrMethod() const {
    switch (getDeclKind()) {
    case Decl::Function:
      return true;
    default:
      return false;
    }
  }

  bool isLookupContext() const { return !isFunctionOrMethod(); }

  bool isFileContext() const { return getDeclKind() == Decl::TranslationUnit; }

  bool isTranslationUnit() const {
    return getDeclKind() == Decl::TranslationUnit;
  }

  bool isRecord() const { return getDeclKind() == Decl::Record; }

  bool isTransparentContext() const;

  bool Equals(const DeclContext *DC) const {
    return DC && this->getPrimaryContext() == DC->getPrimaryContext();
  }

  bool Encloses(const DeclContext *DC) const;

  // Retrieve the nearest context that is not a transparent context.
  DeclContext *getNonTransparentContext();
  const DeclContext *getNonTransparentContext() const {
    return const_cast<DeclContext *>(this)->getNonTransparentContext();
  }

  DeclContext *getPrimaryContext();
  const DeclContext *getPrimaryContext() const {
    return const_cast<DeclContext *>(this)->getPrimaryContext();
  }

  DeclContext *getRedeclContext();
  const DeclContext *getRedeclContext() const {
    return const_cast<DeclContext *>(this)->getRedeclContext();
  }

  void collectAllContexts(llvm::SmallVectorImpl<DeclContext *> &Contexts);

  class decl_iterator {
    /// Current - The current declaration.
    Decl *Current = nullptr;

  public:
    using value_type = Decl *;
    using reference = const value_type &;
    using pointer = const value_type *;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;

    decl_iterator() = default;
    explicit decl_iterator(Decl *C) : Current(C) {}

    reference operator*() const { return Current; }

    // This doesn't meet the iterator requirements, but it's convenient
    value_type operator->() const { return Current; }

    decl_iterator &operator++() {
      Current = Current->getNextDeclInContext();
      return *this;
    }

    decl_iterator operator++(int) {
      decl_iterator tmp(*this);
      ++(*this);
      return tmp;
    }

    friend bool operator==(decl_iterator x, decl_iterator y) {
      return x.Current == y.Current;
    }

    friend bool operator!=(decl_iterator x, decl_iterator y) {
      return x.Current != y.Current;
    }
  };

  using decl_range = llvm::iterator_range<decl_iterator>;

  decl_range decls() const { return decl_range(decls_begin(), decls_end()); }
  decl_iterator decls_begin() const;
  decl_iterator decls_end() const { return decl_iterator(); }
  bool decls_empty() const;

  decl_range noload_decls() const {
    return decl_range(noload_decls_begin(), noload_decls_end());
  }
  decl_iterator noload_decls_begin() const { return decl_iterator(FirstDecl); }
  decl_iterator noload_decls_end() const { return decl_iterator(); }

  template <typename SpecificDecl> class specific_decl_iterator {
    /// Current - The current, underlying declaration iterator, which
    /// will either be NULL or will point to a declaration of
    /// type SpecificDecl.
    DeclContext::decl_iterator Current;

    /// SkipToNextDecl - Advances the current position up to the next
    /// declaration of type SpecificDecl that also meets the criteria
    /// required by Acceptable.
    void SkipToNextDecl() {
      while (*Current && !isa<SpecificDecl>(*Current))
        ++Current;
    }

  public:
    using value_type = SpecificDecl *;
    using reference = void;
    using pointer = void;
    using difference_type =
        std::iterator_traits<DeclContext::decl_iterator>::difference_type;
    using iterator_category = std::forward_iterator_tag;

    specific_decl_iterator() = default;

    /// specific_decl_iterator - Construct a new iterator over a
    /// subset of the declarations the range [C,
    /// end-of-declarations). If A is non-NULL, it is a pointer to a
    /// member function of SpecificDecl that should return true for
    /// all of the SpecificDecl instances that will be in the subset
    /// of iterators.
    explicit specific_decl_iterator(DeclContext::decl_iterator C) : Current(C) {
      SkipToNextDecl();
    }

    value_type operator*() const { return cast<SpecificDecl>(*Current); }

    // This doesn't meet the iterator requirements, but it's convenient
    value_type operator->() const { return **this; }

    specific_decl_iterator &operator++() {
      ++Current;
      SkipToNextDecl();
      return *this;
    }

    specific_decl_iterator operator++(int) {
      specific_decl_iterator tmp(*this);
      ++(*this);
      return tmp;
    }

    friend bool operator==(const specific_decl_iterator &x,
                           const specific_decl_iterator &y) {
      return x.Current == y.Current;
    }

    friend bool operator!=(const specific_decl_iterator &x,
                           const specific_decl_iterator &y) {
      return x.Current != y.Current;
    }
  };

  template <typename SpecificDecl, bool (SpecificDecl::*Acceptable)() const>
  class filtered_decl_iterator {
    /// Current - The current, underlying declaration iterator, which
    /// will either be NULL or will point to a declaration of
    /// type SpecificDecl.
    DeclContext::decl_iterator Current;

    /// SkipToNextDecl - Advances the current position up to the next
    /// declaration of type SpecificDecl that also meets the criteria
    /// required by Acceptable.
    void SkipToNextDecl() {
      while (*Current &&
             (!isa<SpecificDecl>(*Current) ||
              (Acceptable && !(cast<SpecificDecl>(*Current)->*Acceptable)())))
        ++Current;
    }

  public:
    using value_type = SpecificDecl *;
    using reference = void;
    using pointer = void;
    using difference_type =
        std::iterator_traits<DeclContext::decl_iterator>::difference_type;
    using iterator_category = std::forward_iterator_tag;

    filtered_decl_iterator() = default;

    /// filtered_decl_iterator - Construct a new iterator over a
    /// subset of the declarations the range [C,
    /// end-of-declarations). If A is non-NULL, it is a pointer to a
    /// member function of SpecificDecl that should return true for
    /// all of the SpecificDecl instances that will be in the subset
    /// of iterators.
    explicit filtered_decl_iterator(DeclContext::decl_iterator C) : Current(C) {
      SkipToNextDecl();
    }

    value_type operator*() const { return cast<SpecificDecl>(*Current); }
    value_type operator->() const { return cast<SpecificDecl>(*Current); }

    filtered_decl_iterator &operator++() {
      ++Current;
      SkipToNextDecl();
      return *this;
    }

    filtered_decl_iterator operator++(int) {
      filtered_decl_iterator tmp(*this);
      ++(*this);
      return tmp;
    }

    friend bool operator==(const filtered_decl_iterator &x,
                           const filtered_decl_iterator &y) {
      return x.Current == y.Current;
    }

    friend bool operator!=(const filtered_decl_iterator &x,
                           const filtered_decl_iterator &y) {
      return x.Current != y.Current;
    }
  };

  void addDecl(Decl *D);

  void addHiddenDecl(Decl *D);

  void removeDecl(Decl *D);

  bool containsDecl(Decl *D) const;

  bool containsDeclAndLoad(Decl *D) const;

  using lookup_result = DeclContextLookupResult;
  using lookup_iterator = lookup_result::iterator;

  lookup_result lookup(DeclarationName Name) const;

  void localUncachedLookup(DeclarationName Name,
                           llvm::SmallVectorImpl<NamedDecl *> &Results);

  void makeDeclVisibleInContext(NamedDecl *D);

  class all_lookups_iterator;

  using lookups_range = llvm::iterator_range<all_lookups_iterator>;

  lookups_range lookups() const;
  all_lookups_iterator lookups_begin() const;
  all_lookups_iterator lookups_end() const;

  // Low-level accessors

  StoredDeclsMap *getLookupPtr() const { return LookupPtr; }

  StoredDeclsMap *buildLookup();

  bool isDeclInLexicalTraversal(const Decl *D) const {
    return D && (D->NextInContext || D == FirstDecl || D == LastDecl);
  }

  void setUseQualifiedLookup(bool use = true) const {
    DeclContextBits.UseQualifiedLookup = use;
  }

  bool shouldUseQualifiedLookup() const {
    return DeclContextBits.UseQualifiedLookup;
  }

  static bool classof(const Decl *D);
  static bool classof(const DeclContext *D) { return true; }

  void dumpAsDecl() const;
  void dumpAsDecl(const TreeContext *Ctx) const;
  void dumpDeclContext() const;
  void dumpLookups() const;
  void dumpLookups(llvm::raw_ostream &OS, bool DumpDecls = false) const;

private:
  bool hasLazyLocalLexicalLookups() const {
    return DeclContextBits.HasLazyLocalLexicalLookups;
  }

  void setHasLazyLocalLexicalLookups(bool HasLLLL = true) const {
    DeclContextBits.HasLazyLocalLexicalLookups = HasLLLL;
  }

  StoredDeclsMap *CreateStoredDeclsMap(TreeContext &C) const;

  void loadLazyLocalLexicalLookups();
  void buildLookupImpl(DeclContext *DCtx);
  void makeDeclVisibleInContextWithFlags(NamedDecl *D, bool Rediscoverable);
  void makeDeclVisibleInContextImpl(NamedDecl *D);
};

// Specialization selected when ToTy is not a known subclass of DeclContext.
template <class ToTy,
          bool IsKnownSubtype = ::std::is_base_of<DeclContext, ToTy>::value>
struct cast_convert_decl_context {
  static const ToTy *doit(const DeclContext *Val) {
    return static_cast<const ToTy *>(Decl::castFromDeclContext(Val));
  }

  static ToTy *doit(DeclContext *Val) {
    return static_cast<ToTy *>(Decl::castFromDeclContext(Val));
  }
};

// Specialization selected when ToTy is a known subclass of DeclContext.
template <class ToTy> struct cast_convert_decl_context<ToTy, true> {
  static const ToTy *doit(const DeclContext *Val) {
    return static_cast<const ToTy *>(Val);
  }

  static ToTy *doit(DeclContext *Val) { return static_cast<ToTy *>(Val); }
};

} // namespace neverc

namespace llvm {

template <typename To> struct isa_impl<To, ::neverc::DeclContext> {
  static bool doit(const ::neverc::DeclContext &Val) {
    return To::classofKind(Val.getDeclKind());
  }
};

template <class ToTy>
struct cast_convert_val<ToTy, const ::neverc::DeclContext,
                        const ::neverc::DeclContext> {
  static const ToTy &doit(const ::neverc::DeclContext &Val) {
    return *::neverc::cast_convert_decl_context<ToTy>::doit(&Val);
  }
};

template <class ToTy>
struct cast_convert_val<ToTy, ::neverc::DeclContext, ::neverc::DeclContext> {
  static ToTy &doit(::neverc::DeclContext &Val) {
    return *::neverc::cast_convert_decl_context<ToTy>::doit(&Val);
  }
};

template <class ToTy>
struct cast_convert_val<ToTy, const ::neverc::DeclContext *,
                        const ::neverc::DeclContext *> {
  static const ToTy *doit(const ::neverc::DeclContext *Val) {
    return ::neverc::cast_convert_decl_context<ToTy>::doit(Val);
  }
};

template <class ToTy>
struct cast_convert_val<ToTy, ::neverc::DeclContext *,
                        ::neverc::DeclContext *> {
  static ToTy *doit(::neverc::DeclContext *Val) {
    return ::neverc::cast_convert_decl_context<ToTy>::doit(Val);
  }
};

template <class FromTy>
struct cast_convert_val<::neverc::DeclContext, FromTy, FromTy> {
  static ::neverc::DeclContext &doit(const FromTy &Val) {
    return *FromTy::castToDeclContext(&Val);
  }
};

template <class FromTy>
struct cast_convert_val<::neverc::DeclContext, FromTy *, FromTy *> {
  static ::neverc::DeclContext *doit(const FromTy *Val) {
    return FromTy::castToDeclContext(Val);
  }
};

template <class FromTy>
struct cast_convert_val<const ::neverc::DeclContext, FromTy, FromTy> {
  static const ::neverc::DeclContext &doit(const FromTy &Val) {
    return *FromTy::castToDeclContext(&Val);
  }
};

template <class FromTy>
struct cast_convert_val<const ::neverc::DeclContext, FromTy *, FromTy *> {
  static const ::neverc::DeclContext *doit(const FromTy *Val) {
    return FromTy::castToDeclContext(Val);
  }
};

} // namespace llvm

#endif // NEVERC_TREE_DECLBASE_H
