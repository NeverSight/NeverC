#ifndef NEVERC_TREE_TREEDIAG_H
#define NEVERC_TREE_TREEDIAG_H

#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/DiagnosticAST.h"
#include "neverc/Tree/Type/Type.h"

namespace neverc {
void FormatASTNodeDiagnosticArgument(
    DiagnosticsEngine::ArgumentKind Kind, intptr_t Val,
    llvm::StringRef Modifier, llvm::StringRef Argument,
    llvm::ArrayRef<DiagnosticsEngine::ArgumentValue> PrevArgs,
    llvm::SmallVectorImpl<char> &Output, void *Cookie,
    llvm::ArrayRef<intptr_t> QualTypeVals);

QualType desugarForDiagnostic(TreeContext &Context, QualType QT,
                              bool &ShouldAKA);
} // end namespace neverc

#endif
