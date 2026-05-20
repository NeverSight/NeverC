#ifndef NEVERC_ANALYZE_DELAYEDDIAGNOSTIC_H
#define NEVERC_ANALYZE_DELAYEDDIAGNOSTIC_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Tree/Decl/DeclBase.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cstddef>

namespace neverc {

namespace sema {

class DelayedDiagnostic {
public:
  bool Triggered = false;

  SourceLocation Loc;

  void Destroy();

  static DelayedDiagnostic makeAvailability(AvailabilityResult AR,
                                            llvm::ArrayRef<SourceLocation> Locs,
                                            const NamedDecl *ReferringDecl,
                                            const NamedDecl *OffendingDecl,
                                            llvm::StringRef Msg);

  const NamedDecl *getAvailabilityReferringDecl() const {
    return ReferringDecl;
  }

  const NamedDecl *getAvailabilityOffendingDecl() const {
    return OffendingDecl;
  }

  llvm::StringRef getAvailabilityMessage() const {
    return llvm::StringRef(Message, MessageLen);
  }

  llvm::ArrayRef<SourceLocation> getAvailabilitySelectorLocs() const {
    return llvm::ArrayRef(&Loc, 1);
  }

  AvailabilityResult getAvailabilityResult() const { return AR; }

private:
  const NamedDecl *ReferringDecl = nullptr;
  const NamedDecl *OffendingDecl = nullptr;
  const char *Message = nullptr;
  size_t MessageLen = 0;
  AvailabilityResult AR = AR_Available;
};

class DelayedDiagnosticPool {
  const DelayedDiagnosticPool *Parent;
  llvm::SmallVector<DelayedDiagnostic, 4> Diagnostics;

public:
  DelayedDiagnosticPool(const DelayedDiagnosticPool *parent) : Parent(parent) {}

  DelayedDiagnosticPool(const DelayedDiagnosticPool &) = delete;
  DelayedDiagnosticPool &operator=(const DelayedDiagnosticPool &) = delete;

  DelayedDiagnosticPool(DelayedDiagnosticPool &&Other)
      : Parent(Other.Parent), Diagnostics(std::move(Other.Diagnostics)) {
    Other.Diagnostics.clear();
  }

  DelayedDiagnosticPool &operator=(DelayedDiagnosticPool &&Other) {
    Parent = Other.Parent;
    Diagnostics = std::move(Other.Diagnostics);
    Other.Diagnostics.clear();
    return *this;
  }

  ~DelayedDiagnosticPool() {
    for (llvm::SmallVectorImpl<DelayedDiagnostic>::iterator
             i = Diagnostics.begin(),
             e = Diagnostics.end();
         i != e; ++i)
      i->Destroy();
  }

  const DelayedDiagnosticPool *getParent() const { return Parent; }

  void add(const DelayedDiagnostic &diag) { Diagnostics.push_back(diag); }

  void steal(DelayedDiagnosticPool &pool) {
    if (pool.Diagnostics.empty())
      return;

    if (Diagnostics.empty()) {
      Diagnostics = std::move(pool.Diagnostics);
    } else {
      Diagnostics.append(pool.pool_begin(), pool.pool_end());
    }
    pool.Diagnostics.clear();
  }

  using pool_iterator =
      llvm::SmallVectorImpl<DelayedDiagnostic>::const_iterator;

  pool_iterator pool_begin() const { return Diagnostics.begin(); }
  pool_iterator pool_end() const { return Diagnostics.end(); }
  bool pool_empty() const { return Diagnostics.empty(); }
};

} // namespace sema

} // namespace neverc

#endif // NEVERC_ANALYZE_DELAYEDDIAGNOSTIC_H
