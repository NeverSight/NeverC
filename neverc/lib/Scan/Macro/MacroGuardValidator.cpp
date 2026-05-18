#include "neverc/Scan/MacroGuardValidator.h"
#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Scan/MacroRecord.h"
#include "neverc/Scan/Token.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/raw_ostream.h"

using namespace neverc;

void MacroGuardValidator::MacroDefined(const Token &MacroNameTok,
                                       const MacroDirective *MD) {
  auto MI = MD->getMacroRecord();
  if (LLVM_LIKELY(MI->tokens_empty()))
    return;

  if (LLVM_LIKELY(PendingGuardArgs.empty()))
    return;

  const auto Params = MI->params();
  const unsigned NumTokens = MI->getNumTokens();
  const unsigned NumParams = Params.size();

  llvm::DenseSet<const IdentifierInfo *> ParamSet;
  ParamSet.reserve(NumParams);
  for (const auto *P : Params)
    ParamSet.insert(P);

  for (const auto *II : PendingGuardArgs) {
    if (LLVM_UNLIKELY(!ParamSet.contains(II))) {
      const auto *MacroNameII = MacroNameTok.getIdentifierInfo();
      assert(MacroNameII);
      llvm::errs() << "[WARNING] Can't find argument '" << II->getName()
                   << "' at macro '" << MacroNameII->getName() << "'("
                   << MacroNameTok.getLocation().printToString(SM) << ")\n";
      continue;
    }

    for (unsigned TokIdx = 0U; TokIdx < NumTokens; ++TokIdx) {
      const Token &CurTok = MI->getReplacementToken(TokIdx);
      if (LLVM_LIKELY(CurTok.getIdentifierInfo() != II))
        continue;
      if (TokIdx > 0 && TokIdx < NumTokens - 1 &&
          MI->getReplacementToken(TokIdx - 1).is(tok::l_paren) &&
          MI->getReplacementToken(TokIdx + 1).is(tok::r_paren))
        continue;

      llvm::errs() << "[WARNING] In " << CurTok.getLocation().printToString(SM)
                   << ": macro argument '" << II->getName()
                   << "' is not enclosed by parenthesis\n";
    }
  }

  PendingGuardArgs.clear();
}
