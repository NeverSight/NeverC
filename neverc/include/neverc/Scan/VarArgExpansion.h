#ifndef NEVERC_SCAN_VARARGEXPANSION_H
#define NEVERC_SCAN_VARARGEXPANSION_H

#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/SmallVector.h"

namespace neverc {
class PrepEngine;

class VarArgScopeGuard {
  const PrepEngine &PP;
  IdentifierInfo *const Ident__VA_ARGS__;
  IdentifierInfo *const Ident__VA_OPT__;

public:
  VarArgScopeGuard(const PrepEngine &P)
      : PP(P), Ident__VA_ARGS__(PP.Ident__VA_ARGS__),
        Ident__VA_OPT__(PP.Ident__VA_OPT__) {
    assert(Ident__VA_ARGS__->isPoisoned() && "__VA_ARGS__ should be poisoned "
                                             "outside an ISO C variadic "
                                             "macro definition!");
    assert(Ident__VA_OPT__->isPoisoned() && "__VA_OPT__ should be poisoned!");
  }

  void enterScope() {
    Ident__VA_ARGS__->setIsPoisoned(false);
    Ident__VA_OPT__->setIsPoisoned(false);
  }

  void exitScope() {
    Ident__VA_ARGS__->setIsPoisoned(true);
    Ident__VA_OPT__->setIsPoisoned(true);
  }

  ~VarArgScopeGuard() { exitScope(); }
};

class VAOptDefinitionContext {
  llvm::SmallVector<SourceLocation, 8> UnmatchedOpeningParens;

  const IdentifierInfo *const Ident__VA_OPT__;

public:
  VAOptDefinitionContext(PrepEngine &PP)
      : Ident__VA_OPT__(PP.Ident__VA_OPT__) {}

  bool isVAOptToken(const Token &T) const {
    return Ident__VA_OPT__ && T.getIdentifierInfo() == Ident__VA_OPT__;
  }

  bool isInVAOpt() const { return UnmatchedOpeningParens.size(); }

  void sawVAOptFollowedByOpeningParens(const SourceLocation LParenLoc) {
    assert(!isInVAOpt() && "Must NOT be within VAOPT context to call this");
    UnmatchedOpeningParens.push_back(LParenLoc);
  }

  SourceLocation getUnmatchedOpeningParenLoc() const {
    assert(isInVAOpt() && "Must be within VAOPT context to call this");
    return UnmatchedOpeningParens.back();
  }

  bool sawClosingParen() {
    assert(isInVAOpt() && "Must be within VAOPT context to call this");
    UnmatchedOpeningParens.pop_back();
    return !UnmatchedOpeningParens.size();
  }

  void sawOpeningParen(SourceLocation LParenLoc) {
    assert(isInVAOpt() && "Must be within VAOPT context to call this");
    UnmatchedOpeningParens.push_back(LParenLoc);
  }

  bool isAtTopLevel() const { return UnmatchedOpeningParens.size() == 1; }
};

class VAOptExpansionContext : VAOptDefinitionContext {

  Token SyntheticEOFToken;

  // The (spelling) location of the current __VA_OPT__ in the replacement list
  // of the function-like macro being expanded.
  SourceLocation VAOptLoc;

  // NumOfTokensPriorToVAOpt : when != -1, contains the index *of* the first
  // token of the current VAOPT contents (so we know where to start eager
  // token-pasting and stringification) *within*  the substituted tokens of
  // the function-like macro's new replacement list.
  int NumOfTokensPriorToVAOpt = -1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned LeadingSpaceForStringifiedToken : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned StringifyBefore : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned CharifyBefore : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned BeginsWithPlaceholder : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned EndsWithPlaceholder : 1;

  bool hasStringifyBefore() const {
    assert(!isReset() && "Must only be called if the state has not been reset");
    return StringifyBefore;
  }

  bool isReset() const {
    return NumOfTokensPriorToVAOpt == -1 || VAOptLoc.isInvalid();
  }

public:
  VAOptExpansionContext(PrepEngine &PP)
      : VAOptDefinitionContext(PP), LeadingSpaceForStringifiedToken(false),
        StringifyBefore(false), CharifyBefore(false),
        BeginsWithPlaceholder(false), EndsWithPlaceholder(false) {
    SyntheticEOFToken.startToken();
    SyntheticEOFToken.setKind(tok::eof);
  }

  void reset() {
    VAOptLoc = SourceLocation();
    NumOfTokensPriorToVAOpt = -1;
    LeadingSpaceForStringifiedToken = false;
    StringifyBefore = false;
    CharifyBefore = false;
    BeginsWithPlaceholder = false;
    EndsWithPlaceholder = false;
  }

  const Token &getEOFTok() const { return SyntheticEOFToken; }

  void sawHashOrHashAtBefore(const bool HasLeadingSpace, const bool IsHashAt) {

    StringifyBefore = !IsHashAt;
    CharifyBefore = IsHashAt;
    LeadingSpaceForStringifiedToken = HasLeadingSpace;
  }

  void hasPlaceholderAfterHashhashAtStart() { BeginsWithPlaceholder = true; }
  void hasPlaceholderBeforeRParen() {
    if (isAtTopLevel())
      EndsWithPlaceholder = true;
  }

  bool beginsWithPlaceholder() const {
    assert(!isReset() && "Must only be called if the state has not been reset");
    return BeginsWithPlaceholder;
  }
  bool endsWithPlaceholder() const {
    assert(!isReset() && "Must only be called if the state has not been reset");
    return EndsWithPlaceholder;
  }

  bool hasCharifyBefore() const {
    assert(!isReset() && "Must only be called if the state has not been reset");
    return CharifyBefore;
  }
  bool hasStringifyOrCharifyBefore() const {
    return hasStringifyBefore() || hasCharifyBefore();
  }

  unsigned int getNumberOfTokensPriorToVAOpt() const {
    assert(!isReset() && "Must only be called if the state has not been reset");
    return NumOfTokensPriorToVAOpt;
  }

  bool getLeadingSpaceForStringifiedToken() const {
    assert(hasStringifyBefore() &&
           "Must only be called if this has been marked for stringification");
    return LeadingSpaceForStringifiedToken;
  }

  void sawVAOptFollowedByOpeningParens(const SourceLocation VAOptLoc,
                                       const unsigned int NumPriorTokens) {
    assert(VAOptLoc.isFileID() && "Must not come from a macro expansion");
    assert(isReset() && "Must only be called if the state has been reset");
    VAOptDefinitionContext::sawVAOptFollowedByOpeningParens(SourceLocation());
    this->VAOptLoc = VAOptLoc;
    NumOfTokensPriorToVAOpt = NumPriorTokens;
    assert(NumOfTokensPriorToVAOpt > -1 && "Too many prior tokens");
  }

  SourceLocation getVAOptLoc() const {
    assert(!isReset() && "Must only be called if the state has not been reset");
    assert(VAOptLoc.isValid() && "__VA_OPT__ location must be valid");
    return VAOptLoc;
  }
  using VAOptDefinitionContext::isInVAOpt;
  using VAOptDefinitionContext::isVAOptToken;
  using VAOptDefinitionContext::sawClosingParen;
  using VAOptDefinitionContext::sawOpeningParen;
};
} // end namespace neverc

#endif
