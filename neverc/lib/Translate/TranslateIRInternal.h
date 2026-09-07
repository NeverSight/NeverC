#ifndef NEVERC_TRANSLATE_TRANSLATEIRINTERNAL_H
#define NEVERC_TRANSLATE_TRANSLATEIRINTERNAL_H

#include "TranslateIR.h"
#include "llvm/ADT/ArrayRef.h"
#include <set>

namespace neverc::translate::detail {
// Project declarations contribute only to name/type resolution. Their bodies
// are never treated as definitions; final project verification supplies none.
bool verifyProjectDefinitions(const Module &M,
                              const VerificationContext &Context,
                              llvm::ArrayRef<Function> FunctionDeclarations,
                              llvm::ArrayRef<Variable> GlobalDeclarations,
                              Diagnostics &D);
void emitProjectFiles(const Module &M,
                      const std::set<std::string> &PublicRecords,
                      const std::set<std::string> &ExternalGlobals,
                      EmittedSource &Header, EmittedSource &Source);
} // namespace neverc::translate::detail
#endif
