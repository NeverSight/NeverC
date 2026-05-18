#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclGroup.h"
#include <cassert>

using namespace neverc;

DeclGroup *DeclGroup::Create(TreeContext &C, Decl **Decls, unsigned NumDecls) {
  assert(NumDecls > 1 && "Invalid DeclGroup");
  unsigned Size = totalSizeToAlloc<Decl *>(NumDecls);
  void *Mem = C.Allocate(Size, alignof(DeclGroup));
  new (Mem) DeclGroup(NumDecls, Decls);
  return static_cast<DeclGroup *>(Mem);
}

DeclGroup::DeclGroup(unsigned numdecls, Decl **decls) : NumDecls(numdecls) {
  assert(numdecls > 0);
  assert(decls);
  std::uninitialized_copy(decls, decls + numdecls,
                          getTrailingObjects<Decl *>());
}

bool TreeConsumer::ProcessTopLevelDecl(DeclGroupRef D) { return true; }

void TreeConsumer::ProcessInterestingDecl(DeclGroupRef D) {
  ProcessTopLevelDecl(D);
}
