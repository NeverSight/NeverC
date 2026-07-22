#include "neverc/DynCode/Pipeline/ExternalReferenceLedger.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace dyncode {

namespace {

// llvm.* globals/functions (intrinsics, module-level metadata arrays like
// llvm.global_ctors, llvm.used, ...) are never handled by an ImportProvider:
// intrinsics are lowered by the backend and llvm.* metadata is consumed by the
// host pipeline.  They must not enter the external ledger.
bool isReserved(const GlobalValue &GV) {
  return GV.getName().starts_with("llvm.");
}

uint32_t countUseSites(const GlobalValue &GV) {
  uint32_t Sites = 0;
  for (const Use &U : GV.uses()) {
    (void)U;
    ++Sites;
  }
  return Sites;
}

} // namespace

ExternalReferenceLedger ExternalReferenceLedger::scan(const Module &M) {
  ExternalReferenceLedger Ledger;
  for (const Function &F : M) {
    if (!F.isDeclaration() || F.isIntrinsic() || isReserved(F))
      continue;
    if (!F.hasName())
      continue;
    ExternalReference Ref;
    Ref.Symbol = F.getName().str();
    Ref.Kind = ExternalReferenceKind::Function;
    Ref.UseSites = countUseSites(F);
    Ledger.Entries.push_back(std::move(Ref));
  }
  for (const GlobalVariable &G : M.globals()) {
    if (!G.isDeclaration() || isReserved(G))
      continue;
    if (!G.hasName())
      continue;
    ExternalReference Ref;
    Ref.Symbol = G.getName().str();
    Ref.Kind = ExternalReferenceKind::Data;
    Ref.UseSites = countUseSites(G);
    Ledger.Entries.push_back(std::move(Ref));
  }
  return Ledger;
}

ExternalReference *ExternalReferenceLedger::find(StringRef Symbol) {
  for (ExternalReference &Ref : Entries)
    if (Ref.Symbol == Symbol)
      return &Ref;
  return nullptr;
}

const ExternalReference *ExternalReferenceLedger::find(StringRef Symbol) const {
  for (const ExternalReference &Ref : Entries)
    if (Ref.Symbol == Symbol)
      return &Ref;
  return nullptr;
}

bool ExternalReferenceLedger::claim(StringRef Symbol, StringRef ProviderID,
                                    ExternalDisposition D) {
  if (D == ExternalDisposition::Unresolved)
    return false;
  ExternalReference *Ref = find(Symbol);
  if (!Ref)
    return false;
  // A second, different provider claiming the same symbol is a conflict; the
  // host must report it at route freeze rather than letting load order win.
  if (!Ref->ProviderID.empty() && Ref->ProviderID != ProviderID)
    return false;
  Ref->ProviderID = ProviderID.str();
  Ref->Disposition = D;
  return true;
}

std::vector<StringRef> ExternalReferenceLedger::unresolved() const {
  std::vector<StringRef> Out;
  for (const ExternalReference &Ref : Entries)
    if (Ref.Disposition == ExternalDisposition::Unresolved)
      Out.push_back(Ref.Symbol);
  return Out;
}

bool ExternalReferenceLedger::allResolved() const {
  for (const ExternalReference &Ref : Entries)
    if (Ref.Disposition == ExternalDisposition::Unresolved)
      return false;
  return true;
}

StringRef externalDispositionName(ExternalDisposition D) {
  switch (D) {
  case ExternalDisposition::Unresolved:
    return "unresolved";
  case ExternalDisposition::EliminatedInIR:
    return "eliminated-in-ir";
  case ExternalDisposition::ResolvedInternal:
    return "resolved-internal";
  case ExternalDisposition::RuntimeContract:
    return "runtime-contract";
  }
  return "unknown";
}

StringRef externalReferenceKindName(ExternalReferenceKind K) {
  switch (K) {
  case ExternalReferenceKind::Function:
    return "function";
  case ExternalReferenceKind::Data:
    return "data";
  }
  return "unknown";
}

} // namespace dyncode
} // namespace neverc
