#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"

using namespace neverc;

StaticAssertDecl *StaticAssertDecl::Create(TreeContext &C, DeclContext *DC,
                                           SourceLocation StaticAssertLoc,
                                           Expr *AssertExpr, Expr *Message,
                                           SourceLocation RParenLoc,
                                           bool Failed) {
  return new (C, DC) StaticAssertDecl(DC, StaticAssertLoc, AssertExpr, Message,
                                      RParenLoc, Failed);
}

void StaticAssertDecl::anchor() {}
