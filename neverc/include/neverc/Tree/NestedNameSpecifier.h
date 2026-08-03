//===- NestedNameSpecifier.h - C++ nested-name-specifiers -------*- C++ -*-===//
#ifndef NEVERC_TREE_NESTEDNAMESPECIFIER_H
#define NEVERC_TREE_NESTEDNAMESPECIFIER_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace neverc {

class IdentifierInfo;
class LangOptions;
class NamespaceAliasDecl;
class NamespaceDecl;
class NestedNameSpecifier;
struct PrintingPolicy;
class TreeContext;
class Type;

/// Represents a C++ nested name specifier, e.g. \c std:: or \c Foo::Bar::.
class NestedNameSpecifier : public llvm::FoldingSetNode {
  friend class TreeContext;

public:
  enum SpecifierKind {
    Identifier,
    Namespace,
    NamespaceAlias,
    TypeSpec,
    TypeSpecWithTemplate,
    Global,
    Super
  };

private:
  NestedNameSpecifier *Prefix = nullptr;
  llvm::PointerIntPair<void *, 3> SpecifierAndKind;

  NestedNameSpecifier(SpecifierKind Kind, void *Spec)
      : SpecifierAndKind(Spec, Kind) {}

  static NestedNameSpecifier *getOrCreate(const TreeContext &Context,
                                          NestedNameSpecifier *Prefix,
                                          SpecifierKind Kind, void *Spec);

public:
  NestedNameSpecifier(const NestedNameSpecifier &) = delete;
  NestedNameSpecifier &operator=(const NestedNameSpecifier &) = delete;

  SpecifierKind getKind() const {
    return static_cast<SpecifierKind>(SpecifierAndKind.getInt());
  }

  NestedNameSpecifier *getPrefix() const { return Prefix; }

  IdentifierInfo *getAsIdentifier() const {
    if (getKind() == Identifier)
      return static_cast<IdentifierInfo *>(SpecifierAndKind.getPointer());
    return nullptr;
  }

  NamespaceDecl *getAsNamespace() const {
    if (getKind() == Namespace)
      return static_cast<NamespaceDecl *>(SpecifierAndKind.getPointer());
    return nullptr;
  }

  NamespaceAliasDecl *getAsNamespaceAlias() const {
    if (getKind() == NamespaceAlias)
      return static_cast<NamespaceAliasDecl *>(SpecifierAndKind.getPointer());
    return nullptr;
  }

  const Type *getAsType() const {
    if (getKind() == TypeSpec || getKind() == TypeSpecWithTemplate)
      return static_cast<const Type *>(SpecifierAndKind.getPointer());
    return nullptr;
  }

  static NestedNameSpecifier *GlobalSpecifier(const TreeContext &Context);
  static NestedNameSpecifier *Create(const TreeContext &Context,
                                     NestedNameSpecifier *Prefix,
                                     IdentifierInfo *II);
  static NestedNameSpecifier *Create(const TreeContext &Context,
                                     NestedNameSpecifier *Prefix,
                                     NamespaceDecl *NS);
  static NestedNameSpecifier *Create(const TreeContext &Context,
                                     NestedNameSpecifier *Prefix,
                                     NamespaceAliasDecl *Alias);
  static NestedNameSpecifier *Create(const TreeContext &Context,
                                     NestedNameSpecifier *Prefix, bool Template,
                                     const Type *T);

  void Profile(llvm::FoldingSetNodeID &ID) const {
    Profile(ID, Prefix, SpecifierAndKind.getOpaqueValue());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, NestedNameSpecifier *Prefix,
                      uintptr_t SpecAndKind) {
    ID.AddPointer(Prefix);
    ID.AddInteger(SpecAndKind);
  }

  void print(llvm::raw_ostream &OS, const PrintingPolicy &Policy) const;
  void dump(const LangOptions &LO) const;
  void dump() const;
};

/// A nested-name-specifier with source-location information.
class NestedNameSpecifierLoc {
  NestedNameSpecifier *Qualifier = nullptr;
  void *Data = nullptr;

public:
  NestedNameSpecifierLoc() = default;
  NestedNameSpecifierLoc(NestedNameSpecifier *Qualifier, void *Data)
      : Qualifier(Qualifier), Data(Data) {}

  NestedNameSpecifier *getNestedNameSpecifier() const { return Qualifier; }
  void *getOpaqueData() const { return Data; }
  bool hasQualifier() const { return Qualifier != nullptr; }
  explicit operator bool() const { return hasQualifier(); }

  SourceRange getSourceRange() const LLVM_READONLY;
  SourceLocation getBeginLoc() const { return getSourceRange().getBegin(); }
  SourceLocation getEndLoc() const { return getSourceRange().getEnd(); }
  SourceLocation getLocalBeginLoc() const;
  SourceLocation getLocalEndLoc() const;
};

/// Builder for NestedNameSpecifierLoc.
class NestedNameSpecifierLocBuilder {
  NestedNameSpecifier *Representation = nullptr;
  TreeContext *Context = nullptr;

public:
  NestedNameSpecifierLocBuilder() = default;
  void clear() { Representation = nullptr; }
  NestedNameSpecifier *getRepresentation() const { return Representation; }
  NestedNameSpecifierLoc getWithLocInContext(TreeContext &Ctx) const;
  void MakeGlobal(TreeContext &Ctx, SourceLocation ColonColonLoc);
  void Extend(TreeContext &Ctx, IdentifierInfo *II, SourceLocation IdLoc,
              SourceLocation ColonColonLoc);
  void Extend(TreeContext &Ctx, NamespaceDecl *NS, SourceLocation NSLoc,
              SourceLocation ColonColonLoc);
  void Adopt(NestedNameSpecifierLoc Other);
};

} // namespace neverc

#endif
