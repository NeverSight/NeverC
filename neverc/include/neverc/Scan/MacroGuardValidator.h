#ifndef NEVERC_LEX_MACROGUARDVALIDATOR_H
#define NEVERC_LEX_MACROGUARDVALIDATOR_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Scan/PrepObserver.h"
#include "llvm/ADT/SmallVector.h"

namespace neverc {
class IdentifierInfo;
class MacroDirective;
class SourceManager;
class Token;
} // namespace neverc

class MacroGuardValidator : public neverc::PrepObserver {
  neverc::SourceManager &SM;
  llvm::SmallVector<const neverc::IdentifierInfo *, 2> PendingGuardArgs;

public:
  explicit MacroGuardValidator(neverc::SourceManager &SM) : SM(SM) {}

  void addPendingGuardArg(const neverc::IdentifierInfo *II) {
    PendingGuardArgs.push_back(II);
  }

  void clearPendingGuardArgs() { PendingGuardArgs.clear(); }

  void MacroDefined(const neverc::Token &MacroNameTok,
                    const neverc::MacroDirective *MD) override;
};

#endif
