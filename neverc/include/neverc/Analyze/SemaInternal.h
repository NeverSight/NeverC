#ifndef NEVERC_ANALYZE_SEMAINTERNAL_H
#define NEVERC_ANALYZE_SEMAINTERNAL_H

#include "neverc/Analyze/Lookup.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Analyze/SemaDiag.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include <cstdint>

namespace neverc {

inline PartialDiagnostic Sema::PDiag(unsigned DiagID) {
  return PartialDiagnostic(DiagID, Context.getDiagAllocator());
}

StringLiteral *getNeverCStringLiteral(Expr *E);

void encodeNeverCStringWideCodeUnitsToUtf8(StringLiteral *SL,
                                           unsigned WidthInBits,
                                           llvm::SmallVectorImpl<char> &Out);

StringLiteral *foldNeverCStringWideLiteralToUtf8(Sema &S, StringLiteral *SL);

ExprResult buildNeverCStringRuntimeCall(Sema &S, Scope *Sc, SourceLocation Loc,
                                        llvm::StringRef FunctionName,
                                        MultiExprArg Args,
                                        SourceLocation EndLoc);

ExprResult buildNeverCStringEncryptedLiteral(Sema &S, Scope *Sc, Expr *Base,
                                             StringLiteral *SL,
                                             SourceLocation LParenLoc,
                                             SourceLocation RParenLoc);

/// If \p E is a call to `__neverc_string_decrypt_literal(enc, len, key)`,
/// return the CallExpr; otherwise return nullptr.
const CallExpr *getDecryptLiteralCall(const Expr *E);

inline bool
FTIHasSingleVoidParameter(const DeclaratorChunk::FunctionTypeInfo &FTI) {
  return FTI.NumParams == 1 && !FTI.isVariadic &&
         FTI.Params[0].Ident == nullptr && FTI.Params[0].Param &&
         cast<ParmVarDecl>(FTI.Params[0].Param)->getType()->isVoidType();
}

inline bool
FTIHasNonVoidParameters(const DeclaratorChunk::FunctionTypeInfo &FTI) {
  // Assume FTI is well-formed.
  return FTI.NumParams && !FTIHasSingleVoidParameter(FTI);
}

inline InheritableAttr *getDLLAttr(Decl *D) {
  assert(!(D->hasAttr<DLLImportAttr>() && D->hasAttr<DLLExportAttr>()) &&
         "A declaration cannot be both dllimport and dllexport.");
  if (auto *Import = D->getAttr<DLLImportAttr>())
    return Import;
  if (auto *Export = D->getAttr<DLLExportAttr>())
    return Export;
  return nullptr;
}

class TypoCorrectionConsumer {
  typedef llvm::SmallVector<TypoCorrection, 1> TypoResultList;
  typedef llvm::StringMap<TypoResultList> TypoResultsMap;
  typedef std::map<unsigned, TypoResultsMap> TypoEditDistanceMap;

public:
  TypoCorrectionConsumer(Sema &SemaRef, const DeclarationNameInfo &TypoName,
                         neverc::ResolveNameKind LookupKind, Scope *S,
                         std::unique_ptr<CorrectionCandidateCallback> CCC,
                         DeclContext *MemberContext)
      : Typo(TypoName.getName().getAsIdentifierInfo()), CurrentTCIndex(0),
        SemaRef(SemaRef), S(S), CorrectionValidator(std::move(CCC)),
        MemberContext(MemberContext), Result(SemaRef, TypoName, LookupKind) {
    Result.suppressDiagnostics();
    // Arrange for ValidatedCorrections[0] to always be an empty correction.
    ValidatedCorrections.push_back(TypoCorrection());
  }

  // Methods for adding potential corrections to the consumer.
  void FoundDecl(NamedDecl *ND, NamedDecl *Hiding);
  void FoundName(llvm::StringRef Name);
  void addKeywordResult(llvm::StringRef Keyword);
  void addCorrection(TypoCorrection Correction);

  bool empty() const {
    return CorrectionResults.empty() && ValidatedCorrections.size() == 1;
  }

  unsigned getBestEditDistance(bool Normalized) {
    if (CorrectionResults.empty())
      return (std::numeric_limits<unsigned>::max)();

    unsigned BestED = CorrectionResults.begin()->first;
    return Normalized ? TypoCorrection::NormalizeEditDistance(BestED) : BestED;
  }

  const TypoCorrection &getNextCorrection();

  void resetCorrectionStream() { CurrentTCIndex = 0; }

  CorrectionCandidateCallback *getCorrectionValidator() const {
    return CorrectionValidator.get();
  }

private:
  void addName(llvm::StringRef Name, NamedDecl *ND, bool isKeyword = false);

  bool resolveCorrection(TypoCorrection &Candidate);

  IdentifierInfo *Typo;

  TypoEditDistanceMap CorrectionResults;

  llvm::SmallVector<TypoCorrection, 4> ValidatedCorrections;
  size_t CurrentTCIndex;

  Sema &SemaRef;
  Scope *S;
  std::unique_ptr<CorrectionCandidateCallback> CorrectionValidator;
  DeclContext *MemberContext;
  LookupResult Result;
};

inline Sema::TypoExprState::TypoExprState() {}

inline Sema::TypoExprState::TypoExprState(TypoExprState &&other) noexcept {
  *this = std::move(other);
}

inline Sema::TypoExprState &
Sema::TypoExprState::operator=(Sema::TypoExprState &&other) noexcept {
  Consumer = std::move(other.Consumer);
  DiagHandler = std::move(other.DiagHandler);
  RecoveryHandler = std::move(other.RecoveryHandler);
  return *this;
}

} // end namespace neverc

#endif
