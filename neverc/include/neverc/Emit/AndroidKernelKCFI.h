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

/// Serialized values 0..2 are part of the Android-kernel IR contract.  The
/// frontend-only Unspecified state must be resolved before a module is emitted.
enum class KCFIMode : uint8_t {
  Disabled = 0,
  Classic = 1,
  Normalized = 2,
  Unspecified = 3,
};

constexpr bool isConcrete(KCFIMode Mode) {
  return Mode != KCFIMode::Unspecified;
}

/// Read and validate the module's selected Android kernel KCFI mode. A missing
/// flag denotes a non-Android module; malformed values fail closed.
std::optional<KCFIMode> getKCFIMode(const llvm::Module &M);

/// Read and validate the opaque Android kernel profile contract carried by the
/// module.  The compiler deliberately assigns no version semantics to it.
std::optional<uint32_t> getProfile(const llvm::Module &M);

/// The complete compiler-visible Android kernel contract.  Keeping the fields
/// together makes every backend/provider boundary preserve new invariants as a
/// unit instead of duplicating parallel checks.
struct Contract {
  KCFIMode Mode;
  uint32_t Profile;

  bool operator==(const Contract &Other) const {
    return Mode == Other.Mode && Profile == Other.Profile;
  }
  bool operator!=(const Contract &Other) const { return !(*this == Other); }
};

/// Return the complete contract only when both invariants are present.  A
/// caller requiring Android mode treats nullopt (including a partial contract)
/// as a fail-closed boundary violation.
std::optional<Contract> getContract(const llvm::Module &M);

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

/// Apply the mandatory Android-kernel function attributes and prepare KCFI
/// type metadata for every definition currently present in the module.  This
/// must be rerun after a provider or late runtime linker adds definitions.
class KernelFunctionAttrsPass
    : public llvm::PassInfoMixin<KernelFunctionAttrsPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

} // namespace neverc::Emit::AndroidKernel

#endif // NEVERC_EMIT_ANDROIDKERNELKCFI_H
