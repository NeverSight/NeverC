#ifndef NEVERC_ANALYZE_TYPOCORRECTION_H
#define NEVERC_ANALYZE_TYPOCORRECTION_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Diagnostic/PartialDiagnostic.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace neverc {

using llvm::dyn_cast_or_null;

class IdentifierInfo;
class LangOptions;

class TypoCorrection {
public:
  // "Distance" for unusable corrections
  static const unsigned InvalidDistance = std::numeric_limits<unsigned>::max();

  // The largest distance still considered valid (larger edit distances are
  // mapped to InvalidDistance by getEditDistance).
  static const unsigned MaximumDistance = 10000U;

  static const unsigned CharDistanceWeight = 100U;
  static const unsigned CallbackDistanceWeight = 150U;

  TypoCorrection(const DeclarationName &Name, NamedDecl *NameDecl,
                 unsigned CharDistance = 0)
      : CorrectionName(Name), CharDistance(CharDistance) {
    if (NameDecl)
      CorrectionDecls.push_back(NameDecl);
  }

  TypoCorrection(NamedDecl *Name, unsigned CharDistance = 0)
      : CorrectionName(Name->getDeclName()), CharDistance(CharDistance) {
    if (Name)
      CorrectionDecls.push_back(Name);
  }

  TypoCorrection(DeclarationName Name, unsigned CharDistance = 0)
      : CorrectionName(Name), CharDistance(CharDistance) {}

  TypoCorrection() = default;

  DeclarationName getCorrection() const { return CorrectionName; }

  IdentifierInfo *getCorrectionAsIdentifierInfo() const {
    return CorrectionName.getAsIdentifierInfo();
  }

  void setCallbackDistance(unsigned ED) { CallbackDistance = ED; }

  static unsigned NormalizeEditDistance(unsigned ED) {
    if (ED > MaximumDistance)
      return InvalidDistance;
    return (ED + CharDistanceWeight / 2) / CharDistanceWeight;
  }

  unsigned getEditDistance(bool Normalized = true) const {
    if (CharDistance > MaximumDistance || CallbackDistance > MaximumDistance)
      return InvalidDistance;
    unsigned ED = CharDistance * CharDistanceWeight +
                  CallbackDistance * CallbackDistanceWeight;
    if (ED > MaximumDistance)
      return InvalidDistance;
    return Normalized ? NormalizeEditDistance(ED) : ED;
  }

  NamedDecl *getFoundDecl() const {
    return hasCorrectionDecl() ? *(CorrectionDecls.begin()) : nullptr;
  }

  NamedDecl *getCorrectionDecl() const {
    auto *D = getFoundDecl();
    return D ? D->getUnderlyingDecl() : nullptr;
  }
  template <class DeclClass> DeclClass *getCorrectionDeclAs() const {
    return dyn_cast_or_null<DeclClass>(getCorrectionDecl());
  }

  void ClearCorrectionDecls() { CorrectionDecls.clear(); }

  void setCorrectionDecl(NamedDecl *CDecl) {
    CorrectionDecls.clear();
    addCorrectionDecl(CDecl);
  }

  void setCorrectionDecls(llvm::ArrayRef<NamedDecl *> Decls) {
    CorrectionDecls.clear();
    CorrectionDecls.insert(CorrectionDecls.begin(), Decls.begin(), Decls.end());
  }

  void addCorrectionDecl(NamedDecl *CDecl);

  std::string getAsString(const LangOptions &LO) const;

  std::string getQuoted(const LangOptions &LO) const {
    return "'" + getAsString(LO) + "'";
  }

  explicit operator bool() const { return bool(CorrectionName); }

  void makeKeyword() {
    CorrectionDecls.clear();
    CorrectionDecls.push_back(nullptr);
  }

  bool isKeyword() const {
    return !CorrectionDecls.empty() && CorrectionDecls.front() == nullptr;
  }

  template <std::size_t StrLen>
  bool isKeyword(const char (&Str)[StrLen]) const {
    return isKeyword() && getCorrectionAsIdentifierInfo()->isStr(Str);
  }

  bool isResolved() const { return !CorrectionDecls.empty(); }

  bool isOverloaded() const { return CorrectionDecls.size() > 1; }

  void setCorrectionRange(const DeclarationNameInfo &TypoName);

  SourceRange getCorrectionRange() const { return CorrectionRange; }

  using decl_iterator = llvm::SmallVectorImpl<NamedDecl *>::iterator;

  decl_iterator begin() {
    return isKeyword() ? CorrectionDecls.end() : CorrectionDecls.begin();
  }

  decl_iterator end() { return CorrectionDecls.end(); }

  using const_decl_iterator =
      llvm::SmallVectorImpl<NamedDecl *>::const_iterator;

  const_decl_iterator begin() const {
    return isKeyword() ? CorrectionDecls.end() : CorrectionDecls.begin();
  }

  const_decl_iterator end() const { return CorrectionDecls.end(); }

  bool requiresImport() const { return RequiresImport; }
  void setRequiresImport(bool Req) { RequiresImport = Req; }

  void addExtraDiagnostic(PartialDiagnostic PD) {
    ExtraDiagnostics.push_back(std::move(PD));
  }
  llvm::ArrayRef<PartialDiagnostic> getExtraDiagnostics() const {
    return ExtraDiagnostics;
  }

private:
  bool hasCorrectionDecl() const {
    return (!isKeyword() && !CorrectionDecls.empty());
  }

  DeclarationName CorrectionName;
  llvm::SmallVector<NamedDecl *, 1> CorrectionDecls;
  unsigned CharDistance = 0;
  unsigned CallbackDistance = 0;
  SourceRange CorrectionRange;
  bool RequiresImport = false;

  std::vector<PartialDiagnostic> ExtraDiagnostics;
};

class CorrectionCandidateCallback {
public:
  static const unsigned InvalidDistance = TypoCorrection::InvalidDistance;

  explicit CorrectionCandidateCallback(IdentifierInfo *Typo = nullptr)
      : Typo(Typo) {}

  virtual ~CorrectionCandidateCallback() = default;

  virtual bool ValidateCandidate(const TypoCorrection &candidate);

  virtual unsigned RankCandidate(const TypoCorrection &candidate) {
    return (!MatchesTypo(candidate) && ValidateCandidate(candidate))
               ? 0
               : InvalidDistance;
  }

  virtual std::unique_ptr<CorrectionCandidateCallback> clone() = 0;

  void setTypoName(IdentifierInfo *II) { Typo = II; }

  bool WantTypeSpecifiers = true;
  bool WantExpressionKeywords = true;
  bool WantFunctionLikeCasts = true;
  bool WantRemainingKeywords = true;
  bool IsAddressOfOperand = false;

protected:
  bool MatchesTypo(const TypoCorrection &candidate) {
    return Typo && candidate.isResolved() && !candidate.requiresImport() &&
           candidate.getCorrectionAsIdentifierInfo() == Typo;
  }

  IdentifierInfo *Typo;
};

class DefaultFilterCCC final : public CorrectionCandidateCallback {
public:
  explicit DefaultFilterCCC(IdentifierInfo *Typo = nullptr)
      : CorrectionCandidateCallback(Typo) {}

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<DefaultFilterCCC>(*this);
  }
};

template <class C>
class DeclFilterCCC final : public CorrectionCandidateCallback {
public:
  bool ValidateCandidate(const TypoCorrection &candidate) override {
    return candidate.getCorrectionDeclAs<C>();
  }
  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<DeclFilterCCC>(*this);
  }
};

class FunctionCallFilterCCC : public CorrectionCandidateCallback {
public:
  explicit FunctionCallFilterCCC(unsigned NumArgs);

  bool ValidateCandidate(const TypoCorrection &candidate) override;
  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<FunctionCallFilterCCC>(*this);
  }

private:
  unsigned NumArgs;
};

class NoTypoCorrectionCCC final : public CorrectionCandidateCallback {
public:
  NoTypoCorrectionCCC() {
    WantTypeSpecifiers = false;
    WantExpressionKeywords = false;
    WantFunctionLikeCasts = false;
    WantRemainingKeywords = false;
  }

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    return false;
  }
  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<NoTypoCorrectionCCC>(*this);
  }
};

} // namespace neverc

#endif // NEVERC_ANALYZE_TYPOCORRECTION_H
