#ifndef NEVERC_AST_DECLCONTEXTINTERNALS_H
#define NEVERC_AST_DECLCONTEXTINTERNALS_H

#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclBase.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include <cassert>

namespace neverc {

class StoredDeclsList {
  using Decls = DeclListNode::Decls;

  using DeclsAndHasExternalTy = llvm::PointerIntPair<Decls, 1, bool>;

  DeclsAndHasExternalTy Data;

  template <typename Fn> void erase_if(Fn ShouldErase) {
    Decls List = Data.getPointer();
    if (!List)
      return;
    TreeContext &C = getTreeContext();
    DeclListNode::Decls NewHead = nullptr;
    DeclListNode::Decls *NewLast = nullptr;
    DeclListNode::Decls *NewTail = &NewHead;
    while (true) {
      if (!ShouldErase(*DeclListNode::iterator(List))) {
        NewLast = NewTail;
        *NewTail = List;
        if (auto *Node = List.dyn_cast<DeclListNode *>()) {
          NewTail = &Node->Rest;
          List = Node->Rest;
        } else {
          break;
        }
      } else if (DeclListNode *N = List.dyn_cast<DeclListNode *>()) {
        List = N->Rest;
        C.DeallocateDeclListNode(N);
      } else {
        // We're discarding the last declaration in the list. The last node we
        // want to keep (if any) will be of the form DeclListNode(D, <rest>);
        // replace it with just D.
        if (NewLast) {
          DeclListNode *Node = NewLast->get<DeclListNode *>();
          *NewLast = Node->D;
          C.DeallocateDeclListNode(Node);
        }
        break;
      }
    }
    Data.setPointer(NewHead);

    assert(llvm::none_of(getLookupResult(), ShouldErase) && "Still exists!");
  }

  void erase(NamedDecl *ND) {
    erase_if([ND](NamedDecl *D) { return D == ND; });
  }

public:
  StoredDeclsList() = default;

  StoredDeclsList(StoredDeclsList &&RHS) : Data(RHS.Data) {
    RHS.Data.setPointer(nullptr);
    RHS.Data.setInt(false);
  }

  void MaybeDeallocList() {
    if (isNull())
      return;
    // If this is a list-form, free the list.
    TreeContext &C = getTreeContext();
    Decls List = Data.getPointer();
    while (DeclListNode *ToDealloc = List.dyn_cast<DeclListNode *>()) {
      List = ToDealloc->Rest;
      C.DeallocateDeclListNode(ToDealloc);
    }
  }

  ~StoredDeclsList() { MaybeDeallocList(); }

  StoredDeclsList &operator=(StoredDeclsList &&RHS) {
    MaybeDeallocList();

    Data = RHS.Data;
    RHS.Data.setPointer(nullptr);
    RHS.Data.setInt(false);
    return *this;
  }

  bool isNull() const { return Data.getPointer().isNull(); }

  TreeContext &getTreeContext() {
    assert(!isNull() && "No TreeContext.");
    if (NamedDecl *ND = getAsDecl())
      return ND->getTreeContext();
    return getAsList()->D->getTreeContext();
  }

  DeclsAndHasExternalTy getAsListAndHasExternal() const { return Data; }

  NamedDecl *getAsDecl() const {
    return getAsListAndHasExternal().getPointer().dyn_cast<NamedDecl *>();
  }

  DeclListNode *getAsList() const {
    return getAsListAndHasExternal().getPointer().dyn_cast<DeclListNode *>();
  }

  void remove(NamedDecl *D) {
    assert(!isNull() && "removing from empty list");
    erase(D);
  }

  void removeExternalDecls() { Data.setInt(false); }

  void replaceExternalDecls(llvm::ArrayRef<NamedDecl *> Decls) {
    erase_if([Decls](NamedDecl *ND) {
      for (NamedDecl *D : Decls)
        if (D->declarationReplaces(ND, /*IsKnownNewer=*/false))
          return true;
      return false;
    });

    Data.setInt(false);

    if (Decls.empty())
      return;

    TreeContext &C = Decls.front()->getTreeContext();
    DeclListNode::Decls DeclsAsList = Decls.back();
    for (size_t I = Decls.size() - 1; I != 0; --I) {
      DeclListNode *Node = C.AllocateDeclListNode(Decls[I - 1]);
      Node->Rest = DeclsAsList;
      DeclsAsList = Node;
    }

    DeclListNode::Decls Head = Data.getPointer();
    if (Head.isNull()) {
      Data.setPointer(DeclsAsList);
      return;
    }

    // Find the end of the existing list.
    DeclListNode::Decls *Tail = &Head;
    while (DeclListNode *Node = Tail->dyn_cast<DeclListNode *>())
      Tail = &Node->Rest;

    // Append the Decls.
    DeclListNode *Node = C.AllocateDeclListNode(Tail->get<NamedDecl *>());
    Node->Rest = DeclsAsList;
    *Tail = Node;
    Data.setPointer(Head);
  }

  DeclContext::lookup_result getLookupResult() const {
    return DeclContext::lookup_result(Data.getPointer());
  }

  void addOrReplaceDecl(NamedDecl *D) {
    const bool IsKnownNewer = true;

    if (isNull()) {
      Data.setPointer(D);
      return;
    }

    // Most decls only have one entry in their list, special case it.
    if (NamedDecl *OldD = getAsDecl()) {
      if (D->declarationReplaces(OldD, IsKnownNewer)) {
        Data.setPointer(D);
        return;
      }

      // Add D after OldD.
      TreeContext &C = D->getTreeContext();
      DeclListNode *Node = C.AllocateDeclListNode(OldD);
      Node->Rest = D;
      Data.setPointer(Node);
      return;
    }

    assert(!llvm::is_contained(getLookupResult(), D) && "Already exists!");
    // Determine if this declaration is actually a redeclaration.
    for (DeclListNode *N = getAsList(); /*return in loop*/;
         N = N->Rest.dyn_cast<DeclListNode *>()) {
      if (D->declarationReplaces(N->D, IsKnownNewer)) {
        N->D = D;
        return;
      }
      if (auto *ND = N->Rest.dyn_cast<NamedDecl *>()) {
        if (D->declarationReplaces(ND, IsKnownNewer)) {
          N->Rest = D;
          return;
        }

        // Add D after ND.
        TreeContext &C = D->getTreeContext();
        DeclListNode *Node = C.AllocateDeclListNode(ND);
        N->Rest = Node;
        Node->Rest = D;
        return;
      }
    }
  }

  LLVM_DUMP_METHOD void dump() const {
    Decls D = Data.getPointer();
    if (!D) {
      llvm::errs() << "<null>\n";
      return;
    }

    while (true) {
      if (auto *Node = D.dyn_cast<DeclListNode *>()) {
        llvm::errs() << '[' << Node->D << "] -> ";
        D = Node->Rest;
      } else {
        llvm::errs() << '[' << D.get<NamedDecl *>() << "]\n";
        return;
      }
    }
  }
};

class StoredDeclsMap
    : public llvm::SmallDenseMap<DeclarationName, StoredDeclsList, 4> {
  friend class TreeContext; // walks the chain deleting these
  friend class DeclContext;

  llvm::PointerIntPair<StoredDeclsMap *, 1> Previous;

public:
  static void DestroyAll(StoredDeclsMap *Map, bool Dependent);
};

class DependentStoredDeclsMap : public StoredDeclsMap {
public:
  DependentStoredDeclsMap() = default;
};

} // namespace neverc

#endif // NEVERC_AST_DECLCONTEXTINTERNALS_H
