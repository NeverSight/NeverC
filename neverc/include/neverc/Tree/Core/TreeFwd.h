#ifndef NEVERC_AST_ASTFWD_H
#define NEVERC_AST_ASTFWD_H

namespace neverc {

class Decl;
#define DECL(DERIVED, BASE) class DERIVED##Decl;
#include "neverc/Tree/DeclNodes.td.h"
class Stmt;
#define STMT(DERIVED, BASE) class DERIVED;
#include "neverc/Tree/StmtNodes.td.h"
class Type;
#define TYPE(DERIVED, BASE) class DERIVED##Type;
#include "neverc/Tree/TypeNodes.td.h"
class Attr;
#define ATTR(A) class A##Attr;
#include "neverc/Foundation/AttrList.td.h"

} // end namespace neverc

#endif
