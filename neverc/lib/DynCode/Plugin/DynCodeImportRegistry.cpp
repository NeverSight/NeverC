#include "DynCodeImportRegistry.h"

namespace neverc {
namespace dyncode {

namespace {

bool isWildcard(const std::vector<std::string> &Matchers) {
  return Matchers.size() == 1 && Matchers.front() == "*";
}

} // namespace

unsigned OwnedDynCodeImportProvider::matchSpecificity(llvm::StringRef Symbol) const {
  if (isWildcard(SymbolMatchers))
    return 1;
  for (const std::string &M : SymbolMatchers)
    if (Symbol == M)
      return 2; // exact match beats a wildcard.
  return 0;
}

bool OwnedDynCodeImportProvider::acceptsTarget(NevercTargetID Target) const {
  // A zero target ID means "any target".
  if (!idNonzero({TargetID.High, TargetID.Low}))
    return true;
  return targetIDEqual(TargetID, Target);
}

bool OwnedDynCodeImportProvider::acceptsLevel(
    NevercDynCodeExecutionLevel L) const {
  return AnyLevel || Level == L;
}

llvm::Error DynCodeImportRegistry::registerImportProvider(
    OwnedDynCodeImportProvider Provider) {
  if (!idNonzero(Provider.ProviderID))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "import provider ID must be non-zero");
  if (Provider.Kind < NEVERC_DYNCODE_IMPORT_SYSCALL ||
      Provider.Kind > NEVERC_DYNCODE_IMPORT_CUSTOM)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "import provider kind is out of range");
  if (Provider.SymbolMatchers.empty())
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "import provider must declare at least one symbol matcher");
  // A wildcard must stand alone; mixing "*" with named matchers is ambiguous.
  for (const std::string &M : Provider.SymbolMatchers)
    if (M == "*" && Provider.SymbolMatchers.size() != 1)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "import provider wildcard matcher must be the only matcher");

  for (const OwnedDynCodeImportProvider &Existing : Providers)
    if (idEqual(Existing.ProviderID, Provider.ProviderID))
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "duplicate import provider ID registration");

  Providers.push_back(std::move(Provider));
  return llvm::Error::success();
}

ImportResolution
DynCodeImportRegistry::resolve(llvm::StringRef Symbol, NevercTargetID Target,
                               NevercDynCodeExecutionLevel Level) const {
  ImportResolution Result;
  unsigned BestSpecificity = 0;
  unsigned BestCount = 0;
  const OwnedDynCodeImportProvider *Best = nullptr;
  const OwnedDynCodeImportProvider *Runner = nullptr;

  for (const OwnedDynCodeImportProvider &P : Providers) {
    if (!P.acceptsTarget(Target) || !P.acceptsLevel(Level))
      continue;
    unsigned Spec = P.matchSpecificity(Symbol);
    if (Spec == 0)
      continue;
    if (Spec > BestSpecificity) {
      BestSpecificity = Spec;
      BestCount = 1;
      Best = &P;
      Runner = nullptr;
    } else if (Spec == BestSpecificity) {
      ++BestCount;
      Runner = &P;
    }
  }

  if (BestCount == 0) {
    Result.Status = ImportResolveStatus::NoProvider;
    return Result;
  }
  if (BestCount > 1) {
    Result.Status = ImportResolveStatus::Conflict;
    Result.ConflictA = Best->ProviderID;
    if (Runner)
      Result.ConflictB = Runner->ProviderID;
    return Result;
  }
  Result.Status = ImportResolveStatus::Resolved;
  Result.Provider = Best;
  return Result;
}

} // namespace dyncode
} // namespace neverc
