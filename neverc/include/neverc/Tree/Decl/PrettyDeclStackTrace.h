#ifndef NEVERC_TREE_PRETTYDECLSTACKTRACE_H
#define NEVERC_TREE_PRETTYDECLSTACKTRACE_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "llvm/Support/PrettyStackTrace.h"

namespace neverc {

class TreeContext;
class Decl;

class PrettyDeclStackTraceEntry : public llvm::PrettyStackTraceEntry {
  TreeContext &Context;
  Decl *TheDecl;
  SourceLocation Loc;
  const char *Message;

public:
  PrettyDeclStackTraceEntry(TreeContext &Ctx, Decl *D, SourceLocation Loc,
                            const char *Msg)
      : Context(Ctx), TheDecl(D), Loc(Loc), Message(Msg) {}

  void print(llvm::raw_ostream &OS) const override;
};

} // namespace neverc

#endif
