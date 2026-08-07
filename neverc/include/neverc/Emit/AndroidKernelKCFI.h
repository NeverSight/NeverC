#ifndef NEVERC_EMIT_ANDROIDKERNELKCFI_H
#define NEVERC_EMIT_ANDROIDKERNELKCFI_H

#include "llvm/IR/PassManager.h"
#include <cstdint>
#include <optional>

namespace llvm {
class Function;
class Module;
} // namespace llvm

namespace neverc::Emit::AndroidKernel {

/// Read and validate the module's selected Android kernel KCFI mode. A missing
/// flag denotes a non-Android module; malformed values fail closed.
std::optional<unsigned> getKCFIMode(const llvm::Module &M);

/// Attach the profile-neutral pair of source-level KCFI IDs to a function.
/// Repeated attachment is accepted only when both IDs agree.
void setKCFITypePair(llvm::Function &F, uint32_t Standard, uint32_t Normalized);

/// Attach the already profile-selected source-level KCFI ID. Repeated
/// attachment is accepted only when the selected IDs agree.
void setKCFIType(llvm::Function &F, uint32_t TypeID);

/// Materialize selected !kcfi_type metadata as the i32 prefix consumed by the
/// Android arm64 module loader and KCFI indirect-call checks. This operation is
/// idempotent and is a no-op for modules without an Android KCFI mode flag.
void finalizeKCFIPrefixes(llvm::Module &M);

class FinalizeKCFIPrefixesPass
    : public llvm::PassInfoMixin<FinalizeKCFIPrefixesPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

} // namespace neverc::Emit::AndroidKernel

#endif // NEVERC_EMIT_ANDROIDKERNELKCFI_H
