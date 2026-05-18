#ifndef NEVERC_BASIC_PARSEDATTRINFO_H
#define NEVERC_BASIC_PARSEDATTRINFO_H

#include "neverc/Foundation/Attr/AttrSubjectMatchRules.h"
#include "neverc/Foundation/Attr/AttributeCommonInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include <climits>

namespace neverc {

class Decl;
class LangOptions;
class ParsedAttr;
class Sema;
class Stmt;
class TargetInfo;

struct ParsedAttrInfo {
  LLVM_PREFERRED_TYPE(AttributeCommonInfo::Kind)
  unsigned AttrKind : 16;
  unsigned NumArgs : 4;
  unsigned OptArgs : 4;
  unsigned NumArgMembers : 4;
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasCustomParsing : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsTargetSpecific : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsType : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsStmt : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsKnownToGCC : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsSupportedByPragmaAttribute : 1;
  struct Spelling {
    AttributeCommonInfo::Syntax Syntax;
    const char *NormalizedFullName;
  };
  llvm::ArrayRef<Spelling> Spellings;
  // The names of the known arguments of this attribute.
  llvm::ArrayRef<const char *> ArgNames;

protected:
  constexpr ParsedAttrInfo(AttributeCommonInfo::Kind AttrKind =
                               AttributeCommonInfo::NoSemaHandlerAttribute)
      : AttrKind(AttrKind), NumArgs(0), OptArgs(0), NumArgMembers(0),
        HasCustomParsing(0), IsTargetSpecific(0), IsType(0), IsStmt(0),
        IsKnownToGCC(0), IsSupportedByPragmaAttribute(0) {}

  constexpr ParsedAttrInfo(AttributeCommonInfo::Kind AttrKind, unsigned NumArgs,
                           unsigned OptArgs, unsigned NumArgMembers,
                           unsigned HasCustomParsing, unsigned IsTargetSpecific,
                           unsigned IsType, unsigned IsStmt,
                           unsigned IsKnownToGCC,
                           unsigned IsSupportedByPragmaAttribute,
                           llvm::ArrayRef<Spelling> Spellings,
                           llvm::ArrayRef<const char *> ArgNames)
      : AttrKind(AttrKind), NumArgs(NumArgs), OptArgs(OptArgs),
        NumArgMembers(NumArgMembers), HasCustomParsing(HasCustomParsing),
        IsTargetSpecific(IsTargetSpecific), IsType(IsType), IsStmt(IsStmt),
        IsKnownToGCC(IsKnownToGCC),
        IsSupportedByPragmaAttribute(IsSupportedByPragmaAttribute),
        Spellings(Spellings), ArgNames(ArgNames) {}

public:
  virtual ~ParsedAttrInfo() = default;

  bool hasSpelling(AttributeCommonInfo::Syntax Syntax,
                   llvm::StringRef Name) const {
    return llvm::any_of(Spellings, [&](const Spelling &S) {
      return (S.Syntax == Syntax && S.NormalizedFullName == Name);
    });
  }

  virtual bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                                    const Decl *D) const {
    return true;
  }
  virtual bool diagAppertainsToStmt(Sema &S, const ParsedAttr &Attr,
                                    const Stmt *St) const {
    return true;
  }
  virtual bool diagMutualExclusion(Sema &S, const ParsedAttr &A,
                                   const Decl *D) const {
    return true;
  }
  virtual bool acceptsLangOpts(const LangOptions &LO) const { return true; }

  virtual bool existsInTarget(const TargetInfo &Target) const { return true; }

  virtual bool spellingExistsInTarget(const TargetInfo &Target,
                                      const unsigned SpellingListIndex) const {
    return true;
  }

  virtual unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const {
    return UINT_MAX;
  }
  virtual bool isParamExpr(size_t N) const { return false; }
  virtual void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>> &Rules,
      const LangOptions &LangOpts) const {}

  enum AttrHandling { NotHandled, AttributeApplied, AttributeNotApplied };
  virtual AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                           const ParsedAttr &Attr) const {
    return NotHandled;
  }

  static const ParsedAttrInfo &get(const AttributeCommonInfo &A);
  static llvm::ArrayRef<const ParsedAttrInfo *> getAllBuiltin();
};

} // namespace neverc

#endif // NEVERC_BASIC_PARSEDATTRINFO_H
