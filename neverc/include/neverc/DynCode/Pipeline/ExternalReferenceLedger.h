#ifndef NEVERC_DYNCODE_EXTERNALREFERENCELEDGER_H
#define NEVERC_DYNCODE_EXTERNALREFERENCELEDGER_H

// Volume 6 task 8: the external-reference ledger is the typed record of every
// symbol a dyncode translation unit still refers to but does not define.  It is
// the single source of truth the (builtin and plugin) ImportProviders update and
// the final verifier consults: request-allowed externals only mean "some
// provider may handle it", never "an unresolved relocation may survive into the
// flat image".  Each reference ends up either eliminated in IR, resolved to an
// in-image definition, converted to a declared runtime-resolver contract, or --
// if none of those -- left Unresolved so the final verifier fails.

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
class Module;
} // namespace llvm

namespace neverc {
namespace dyncode {

// Whether the external names a callable (function declaration) or a data object
// (global variable declaration).
enum class ExternalReferenceKind : uint8_t {
  Function = 0,
  Data,
};

// How an external reference has been (or has not yet been) dealt with.  The
// initial scan records everything as Unresolved; providers move each reference
// to one of the resolved states and record which provider claimed it.
enum class ExternalDisposition : uint8_t {
  Unresolved = 0,   // still an undefined external -- must not survive.
  EliminatedInIR,   // rewritten/inlined away; no longer referenced.
  ResolvedInternal, // now satisfied by an in-image definition.
  RuntimeContract,  // lowered to a declared runtime resolver contract.
};

struct ExternalReference {
  std::string Symbol;
  ExternalReferenceKind Kind = ExternalReferenceKind::Function;
  // Number of call/use sites observed at scan time (functions count call sites,
  // data counts uses); purely informational provenance for diagnostics.
  uint32_t UseSites = 0;
  ExternalDisposition Disposition = ExternalDisposition::Unresolved;
  // The stable ID of the provider that claimed this reference, empty when none
  // has.  Recorded so a second provider claiming the same symbol is a conflict.
  std::string ProviderID;
};

// Ordered, deduplicated ledger of external references in a module.  The order
// is the module declaration order so the ledger digest is deterministic.
class ExternalReferenceLedger {
public:
  // Scan a module: every declared (i.e. body-less / initializer-less) function
  // or global variable with a name is an external reference.  llvm intrinsics
  // and llvm.* metadata globals are excluded -- they are lowered by the backend,
  // never by an ImportProvider.
  static ExternalReferenceLedger scan(const llvm::Module &M);

  llvm::ArrayRef<ExternalReference> entries() const { return Entries; }
  bool empty() const { return Entries.empty(); }
  size_t size() const { return Entries.size(); }

  // Returns the reference for Symbol, or nullptr if the ledger has none.
  ExternalReference *find(llvm::StringRef Symbol);
  const ExternalReference *find(llvm::StringRef Symbol) const;

  // Record that ProviderID handled Symbol with disposition D.  Returns false
  // (and leaves the ledger unchanged) when: the symbol is unknown, D is
  // Unresolved, or the symbol was already claimed by a *different* provider.  A
  // provider re-claiming its own symbol with a resolved disposition is
  // idempotent and succeeds.
  bool claim(llvm::StringRef Symbol, llvm::StringRef ProviderID,
             ExternalDisposition D);

  // The symbols still Unresolved, in ledger order.
  std::vector<llvm::StringRef> unresolved() const;
  bool allResolved() const;

private:
  std::vector<ExternalReference> Entries;
};

llvm::StringRef externalDispositionName(ExternalDisposition D);
llvm::StringRef externalReferenceKindName(ExternalReferenceKind K);

} // namespace dyncode
} // namespace neverc

#endif
