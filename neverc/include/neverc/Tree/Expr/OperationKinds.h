#ifndef NEVERC_AST_OPERATIONKINDS_H
#define NEVERC_AST_OPERATIONKINDS_H

namespace neverc {

enum CastKind {
#define CAST_OPERATION(Name) CK_##Name,
#include "neverc/Tree/Expr/OperationKinds.def"
};

enum BinaryOperatorKind {
#define BINARY_OPERATION(Name, Spelling) BO_##Name,
#include "neverc/Tree/Expr/OperationKinds.def"
};

enum UnaryOperatorKind {
#define UNARY_OPERATION(Name, Spelling) UO_##Name,
#include "neverc/Tree/Expr/OperationKinds.def"
};

} // end namespace neverc

#endif
