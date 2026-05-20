#ifndef NEVERC_LIB_EMIT_STMT_VARBYPASSDETECTOR_H
#define NEVERC_LIB_EMIT_STMT_VARBYPASSDETECTOR_H

#include "neverc/Tree/Decl/Decl.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

namespace neverc {

class Decl;
class Stmt;
class VarDecl;

namespace Emit {

class VarBypassDetector {
  // Scope information. Contains a parent scope and related variable
  // declaration.
  llvm::SmallVector<std::pair<unsigned, const VarDecl *>, 48> Scopes;
  // List of jumps with scopes.
  llvm::SmallVector<std::pair<const Stmt *, unsigned>, 16> FromScopes;
  // Lookup map to find scope for destinations.
  llvm::DenseMap<const Stmt *, unsigned> ToScopes;
  // Set of variables which were bypassed by some jump.
  llvm::SmallPtrSet<const VarDecl *, 8> Bypasses;
  // If true assume that all variables are being bypassed.
  bool AlwaysBypassed = false;

public:
  void Init(const Stmt *Body);

  bool IsBypassed(const VarDecl *D) const {
    return AlwaysBypassed || Bypasses.contains(D);
  }

private:
  bool FormScopeInformation(const Decl *D, unsigned &ParentScope);
  bool FormScopeInformation(const Stmt *S, unsigned &origParentScope);
  void Detect();
  void Detect(unsigned From, unsigned To);
};
} // namespace Emit
} // namespace neverc

#endif
