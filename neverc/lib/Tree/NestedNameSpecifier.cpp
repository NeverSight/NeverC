//===- NestedNameSpecifier.cpp - C++ nested-name-specifiers ---------------===//
#include "neverc/Tree/NestedNameSpecifier.h"
#include "neverc/Foundation/Core/LangOptions.h"
#include "neverc/Tree/Core/PrettyPrinter.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/Support/raw_ostream.h"
#include <new>

using namespace neverc;

NestedNameSpecifier *
NestedNameSpecifier::getOrCreate(const TreeContext &Context,
                                 NestedNameSpecifier *Prefix,
                                 SpecifierKind Kind, void *Spec) {
  llvm::PointerIntPair<void *, 3> Pair(Spec, Kind);
  llvm::FoldingSetNodeID ID;
  NestedNameSpecifier::Profile(ID, Prefix, Pair.getOpaqueValue());
  void *InsertPos = nullptr;
  auto &Set = Context.getNestedNameSpecifiers();
  if (NestedNameSpecifier *NNS = Set.FindNodeOrInsertPos(ID, InsertPos))
    return NNS;
  void *Mem = Context.Allocate(sizeof(NestedNameSpecifier),
                               alignof(NestedNameSpecifier));
  auto *NNS = new (Mem) NestedNameSpecifier(Kind, Spec);
  NNS->Prefix = Prefix;
  Set.InsertNode(NNS, InsertPos);
  return NNS;
}

NestedNameSpecifier *
NestedNameSpecifier::GlobalSpecifier(const TreeContext &Context) {
  return getOrCreate(Context, nullptr, Global, nullptr);
}

NestedNameSpecifier *NestedNameSpecifier::Create(const TreeContext &Context,
                                                 NestedNameSpecifier *Prefix,
                                                 IdentifierInfo *II) {
  return getOrCreate(Context, Prefix, Identifier, II);
}

NestedNameSpecifier *NestedNameSpecifier::Create(const TreeContext &Context,
                                                 NestedNameSpecifier *Prefix,
                                                 NamespaceDecl *NS) {
  return getOrCreate(Context, Prefix, Namespace, NS);
}

NestedNameSpecifier *NestedNameSpecifier::Create(const TreeContext &Context,
                                                 NestedNameSpecifier *Prefix,
                                                 NamespaceAliasDecl *Alias) {
  return getOrCreate(Context, Prefix, NamespaceAlias, Alias);
}

NestedNameSpecifier *NestedNameSpecifier::Create(const TreeContext &Context,
                                                 NestedNameSpecifier *Prefix,
                                                 bool Template,
                                                 const Type *T) {
  return getOrCreate(Context, Prefix,
                     Template ? TypeSpecWithTemplate : TypeSpec,
                     const_cast<Type *>(T));
}

void NestedNameSpecifier::print(llvm::raw_ostream &OS,
                                const PrintingPolicy &Policy) const {
  (void)Policy;
  if (getPrefix())
    getPrefix()->print(OS, Policy);
  switch (getKind()) {
  case Identifier:
    if (IdentifierInfo *II = getAsIdentifier())
      OS << II->getName();
    break;
  case Namespace:
    if (NamespaceDecl *NS = getAsNamespace())
      if (IdentifierInfo *II = NS->getIdentifier())
        OS << II->getName();
    break;
  case NamespaceAlias:
    if (NamespaceAliasDecl *NA = getAsNamespaceAlias())
      if (IdentifierInfo *II = NA->getIdentifier())
        OS << II->getName();
    break;
  case TypeSpec:
  case TypeSpecWithTemplate:
    OS << "<type>";
    break;
  case Global:
    break;
  case Super:
    OS << "__super";
    break;
  }
  OS << "::";
}

void NestedNameSpecifier::dump(const LangOptions &LO) const {
  print(llvm::errs(), PrintingPolicy(LO));
  llvm::errs() << '\n';
}

void NestedNameSpecifier::dump() const {
  LangOptions LO;
  dump(LO);
}

SourceRange NestedNameSpecifierLoc::getSourceRange() const {
  if (!Qualifier)
    return SourceRange();
  return SourceRange(getLocalBeginLoc(), getLocalEndLoc());
}

SourceLocation NestedNameSpecifierLoc::getLocalBeginLoc() const {
  return SourceLocation();
}
SourceLocation NestedNameSpecifierLoc::getLocalEndLoc() const {
  return SourceLocation();
}

void NestedNameSpecifierLocBuilder::MakeGlobal(TreeContext &Ctx,
                                               SourceLocation) {
  Context = &Ctx;
  Representation = NestedNameSpecifier::GlobalSpecifier(Ctx);
}

void NestedNameSpecifierLocBuilder::Extend(TreeContext &Ctx, IdentifierInfo *II,
                                           SourceLocation, SourceLocation) {
  Context = &Ctx;
  Representation = NestedNameSpecifier::Create(Ctx, Representation, II);
}

void NestedNameSpecifierLocBuilder::Extend(TreeContext &Ctx, NamespaceDecl *NS,
                                           SourceLocation, SourceLocation) {
  Context = &Ctx;
  Representation = NestedNameSpecifier::Create(Ctx, Representation, NS);
}

void NestedNameSpecifierLocBuilder::Adopt(NestedNameSpecifierLoc Other) {
  Representation = Other.getNestedNameSpecifier();
}

NestedNameSpecifierLoc
NestedNameSpecifierLocBuilder::getWithLocInContext(TreeContext &) const {
  return NestedNameSpecifierLoc(Representation, nullptr);
}
