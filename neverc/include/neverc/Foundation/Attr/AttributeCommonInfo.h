#ifndef NEVERC_BASIC_ATTRIBUTECOMMONINFO_H
#define NEVERC_BASIC_ATTRIBUTECOMMONINFO_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/TokenKinds.h"

namespace neverc {

class IdentifierInfo;

class AttributeCommonInfo {
public:
  enum Syntax {
    /// __attribute__((...))
    AS_GNU = 1,

    /// [[...]]
    AS_Bracket,

    /// [[...]]
    AS_C23,

    /// __declspec(...)
    AS_Declspec,

    /// [uuid("...")] class Foo
    AS_Microsoft,

    /// __ptr16, alignas(...), etc.
    AS_Keyword,

    /// #pragma ...
    AS_Pragma,

    // Note TableGen depends on the order above.  Do not add or change the order
    // without adding related code to TableGen/NeverCAttrEmitter.cpp.
    /// Context-sensitive version of a keyword attribute.
    AS_ContextSensitiveKeyword,

    /// The attibute has no source code manifestation and is only created
    /// implicitly.
    AS_Implicit
  };
  enum Kind {
#define PARSED_ATTR(NAME) AT_##NAME,
#include "neverc/Analyze/AttrParsedAttrList.td.h"
#undef PARSED_ATTR
    NoSemaHandlerAttribute,
    IgnoredAttribute,
    UnknownAttribute,
  };

private:
  const IdentifierInfo *AttrName = nullptr;
  const IdentifierInfo *ScopeName = nullptr;
  SourceRange AttrRange;
  const SourceLocation ScopeLoc;
  // Corresponds to the Kind enum.
  LLVM_PREFERRED_TYPE(Kind)
  unsigned AttrKind : 16;
  LLVM_PREFERRED_TYPE(Syntax)
  unsigned SyntaxUsed : 4;
  LLVM_PREFERRED_TYPE(bool)
  unsigned SpellingIndex : 4;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsAlignas : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsRegularKeywordAttribute : 1;

protected:
  static constexpr unsigned SpellingNotCalculated = 0xf;

public:
  class Form {
  public:
    constexpr Form(Syntax SyntaxUsed, unsigned SpellingIndex, bool IsAlignas,
                   bool IsRegularKeywordAttribute)
        : SyntaxUsed(SyntaxUsed), SpellingIndex(SpellingIndex),
          IsAlignas(IsAlignas),
          IsRegularKeywordAttribute(IsRegularKeywordAttribute) {}
    constexpr Form(tok::TokenKind Tok)
        : SyntaxUsed(AS_Keyword), SpellingIndex(SpellingNotCalculated),
          IsAlignas(Tok == tok::kw_alignas),
          IsRegularKeywordAttribute(tok::isRegularKeywordAttribute(Tok)) {}

    Syntax getSyntax() const { return Syntax(SyntaxUsed); }
    unsigned getSpellingIndex() const { return SpellingIndex; }
    bool isAlignas() const { return IsAlignas; }
    bool isRegularKeywordAttribute() const { return IsRegularKeywordAttribute; }

    static Form GNU() { return AS_GNU; }
    static Form Bracket() { return AS_Bracket; }
    static Form C23() { return AS_C23; }
    static Form Declspec() { return AS_Declspec; }
    static Form Microsoft() { return AS_Microsoft; }
    static Form Keyword(bool IsAlignas, bool IsRegularKeywordAttribute) {
      return Form(AS_Keyword, SpellingNotCalculated, IsAlignas,
                  IsRegularKeywordAttribute);
    }
    static Form Pragma() { return AS_Pragma; }
    static Form ContextSensitiveKeyword() { return AS_ContextSensitiveKeyword; }
    static Form Implicit() { return AS_Implicit; }

  private:
    constexpr Form(Syntax SyntaxUsed)
        : SyntaxUsed(SyntaxUsed), SpellingIndex(SpellingNotCalculated),
          IsAlignas(0), IsRegularKeywordAttribute(0) {}

    LLVM_PREFERRED_TYPE(Syntax)
    unsigned SyntaxUsed : 4;
    unsigned SpellingIndex : 4;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsAlignas : 1;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsRegularKeywordAttribute : 1;
  };

  AttributeCommonInfo(const IdentifierInfo *AttrName,
                      const IdentifierInfo *ScopeName, SourceRange AttrRange,
                      SourceLocation ScopeLoc, Kind AttrKind, Form FormUsed)
      : AttrName(AttrName), ScopeName(ScopeName), AttrRange(AttrRange),
        ScopeLoc(ScopeLoc), AttrKind(AttrKind),
        SyntaxUsed(FormUsed.getSyntax()),
        SpellingIndex(FormUsed.getSpellingIndex()),
        IsAlignas(FormUsed.isAlignas()),
        IsRegularKeywordAttribute(FormUsed.isRegularKeywordAttribute()) {
    assert(SyntaxUsed >= AS_GNU && SyntaxUsed <= AS_Implicit &&
           "Invalid syntax!");
  }

  AttributeCommonInfo(const IdentifierInfo *AttrName,
                      const IdentifierInfo *ScopeName, SourceRange AttrRange,
                      SourceLocation ScopeLoc, Form FormUsed)
      : AttributeCommonInfo(
            AttrName, ScopeName, AttrRange, ScopeLoc,
            getParsedKind(AttrName, ScopeName, FormUsed.getSyntax()),
            FormUsed) {}

  AttributeCommonInfo(const IdentifierInfo *AttrName, SourceRange AttrRange,
                      Form FormUsed)
      : AttributeCommonInfo(AttrName, nullptr, AttrRange, SourceLocation(),
                            FormUsed) {}

  AttributeCommonInfo(SourceRange AttrRange, Kind K, Form FormUsed)
      : AttributeCommonInfo(nullptr, nullptr, AttrRange, SourceLocation(), K,
                            FormUsed) {}

  AttributeCommonInfo(AttributeCommonInfo &&) = default;
  AttributeCommonInfo(const AttributeCommonInfo &) = default;

  Kind getParsedKind() const { return Kind(AttrKind); }
  Syntax getSyntax() const { return Syntax(SyntaxUsed); }
  Form getForm() const {
    return Form(getSyntax(), SpellingIndex, IsAlignas,
                IsRegularKeywordAttribute);
  }
  const IdentifierInfo *getAttrName() const { return AttrName; }
  void setAttrName(const IdentifierInfo *AttrNameII) { AttrName = AttrNameII; }
  SourceLocation getLoc() const { return AttrRange.getBegin(); }
  SourceRange getRange() const { return AttrRange; }
  void setRange(SourceRange R) { AttrRange = R; }

  bool hasScope() const { return ScopeName; }
  const IdentifierInfo *getScopeName() const { return ScopeName; }
  SourceLocation getScopeLoc() const { return ScopeLoc; }

  std::string getNormalizedFullName() const;

  bool isDeclspecAttribute() const { return SyntaxUsed == AS_Declspec; }
  bool isMicrosoftAttribute() const { return SyntaxUsed == AS_Microsoft; }

  bool isGNUScope() const;
  bool isNeverCScope() const;

  bool isBracketAttribute() const {
    return SyntaxUsed == AS_Bracket || IsAlignas;
  }

  bool isC23Attribute() const { return SyntaxUsed == AS_C23; }

  bool isAlignas() const {
    // The IsAlignas member variable is only true with the `alignas` keyword
    // but not `_Alignas`. The following expression works around this so it
    // returns true for `alignas` or `_Alignas` while still returning false
    // for `__attribute__((aligned))`.
    return (getParsedKind() == AT_Aligned && isKeywordAttribute());
  }

  bool isStandardAttributeSyntax() const {
    return isBracketAttribute() || isC23Attribute();
  }

  bool isGNUAttribute() const { return SyntaxUsed == AS_GNU; }

  bool isKeywordAttribute() const {
    return SyntaxUsed == AS_Keyword || SyntaxUsed == AS_ContextSensitiveKeyword;
  }

  bool isRegularKeywordAttribute() const { return IsRegularKeywordAttribute; }

  bool isContextSensitiveKeywordAttribute() const {
    return SyntaxUsed == AS_ContextSensitiveKeyword;
  }

  unsigned getAttributeSpellingListIndex() const {
    assert((isAttributeSpellingListCalculated() || AttrName) &&
           "Spelling cannot be found");
    return isAttributeSpellingListCalculated()
               ? SpellingIndex
               : calculateAttributeSpellingListIndex();
  }
  void setAttributeSpellingListIndex(unsigned V) { SpellingIndex = V; }

  static bool isBracketAttributeSyntax(Syntax S) {
    return S == AS_Bracket || S == AS_C23;
  }

  static Kind getParsedKind(const IdentifierInfo *Name,
                            const IdentifierInfo *Scope, Syntax SyntaxUsed);

private:
  unsigned calculateAttributeSpellingListIndex() const;

protected:
  bool isAttributeSpellingListCalculated() const {
    return SpellingIndex != SpellingNotCalculated;
  }
};
} // namespace neverc

#endif // NEVERC_BASIC_ATTRIBUTECOMMONINFO_H
