#ifndef NEVERC_SCAN_TOKEN_H
#define NEVERC_SCAN_TOKEN_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>

namespace neverc {

class IdentifierInfo;

class Token {
  SourceLocation::UIntTy Loc;

  SourceLocation::UIntTy UintData;

  //     This is a pointer to a Decl.
  void *PtrData;

  tok::TokenKind Kind;

  unsigned short Flags;

public:
  // Various flags set per token:
  enum TokenFlags {
    StartOfLine = 0x01,   // At start of line or only after whitespace
                          // (considering the line after macro expansion).
    LeadingSpace = 0x02,  // Whitespace exists before this token (considering
                          // whitespace after macro expansion).
    DisableExpand = 0x04, // This identifier may never be macro expanded.
    NeedsCleaning = 0x08, // Contained an escaped newline or trigraph.
    LeadingEmptyMacro = 0x10, // Empty macro exists before this token.
    HasUCN = 0x40,            // This identifier contains a UCN.
    IgnoredComma = 0x80, // This comma is not a macro argument separator (MS).
    StringifiedInMacro = 0x100, // This string or character literal is formed by
                                // macro stringizing or charizing operator.
    CommaAfterElided = 0x200, // The comma following this token was elided (MS).
    IsReinjected = 0x800,     // A phase 4 token that was produced before and
                              // re-added, e.g. via PushTokenStream. Annotation
                              // tokens are *not* reinjected.
  };

  tok::TokenKind getKind() const { return Kind; }
  void setKind(tok::TokenKind K) { Kind = K; }

  bool is(tok::TokenKind K) const { return Kind == K; }
  bool isNot(tok::TokenKind K) const { return Kind != K; }
  bool isOneOf(tok::TokenKind K1, tok::TokenKind K2) const {
    return is(K1) || is(K2);
  }
  template <typename... Ts> bool isOneOf(tok::TokenKind K1, Ts... Ks) const {
    return is(K1) || isOneOf(Ks...);
  }

  bool isAnyIdentifier() const { return tok::isAnyIdentifier(getKind()); }

  bool isLiteral() const { return tok::isLiteral(getKind()); }

  bool isAnnotation() const { return tok::isAnnotation(getKind()); }

  bool isRegularKeywordAttribute() const {
    return tok::isRegularKeywordAttribute(getKind());
  }

  SourceLocation getLocation() const {
    return SourceLocation::getFromRawEncoding(Loc);
  }
  unsigned getLength() const {
    assert(!isAnnotation() && "Annotation tokens have no length field");
    return UintData;
  }

  void setLocation(SourceLocation L) { Loc = L.getRawEncoding(); }
  void setLength(unsigned Len) {
    assert(!isAnnotation() && "Annotation tokens have no length field");
    UintData = Len;
  }

  SourceLocation getAnnotationEndLoc() const {
    assert(isAnnotation() && "Used AnnotEndLocID on non-annotation token");
    return SourceLocation::getFromRawEncoding(UintData ? UintData : Loc);
  }
  void setAnnotationEndLoc(SourceLocation L) {
    assert(isAnnotation() && "Used AnnotEndLocID on non-annotation token");
    UintData = L.getRawEncoding();
  }

  SourceLocation getLastLoc() const {
    return isAnnotation() ? getAnnotationEndLoc() : getLocation();
  }

  SourceLocation getEndLoc() const {
    return isAnnotation() ? getAnnotationEndLoc()
                          : getLocation().getLocWithOffset(getLength());
  }

  SourceRange getAnnotationRange() const {
    return SourceRange(getLocation(), getAnnotationEndLoc());
  }
  void setAnnotationRange(SourceRange R) {
    setLocation(R.getBegin());
    setAnnotationEndLoc(R.getEnd());
  }

  const char *getName() const { return tok::getTokenName(Kind); }

  void startToken() {
    Kind = tok::unknown;
    Flags = 0;
    PtrData = nullptr;
    UintData = 0;
    Loc = SourceLocation().getRawEncoding();
  }

  /// Lightweight reset for the scanner hot path: only clears Flags and
  /// PtrData.  The caller guarantees that Kind, UintData (length), and
  /// Loc are always overwritten by emitToken before the token is used.
  /// InitFlags allows pre-computed flags (StartOfLine, LeadingSpace, etc.)
  /// to be written in one store instead of separate setFlag calls.
  void startTokenFast(unsigned short InitFlags = 0) {
    Flags = InitFlags;
    PtrData = nullptr;
  }

  bool hasPtrData() const { return PtrData != nullptr; }

  IdentifierInfo *getIdentifierInfo() const {
    assert(isNot(tok::raw_identifier) &&
           "getIdentifierInfo() on a tok::raw_identifier token!");
    assert(!isAnnotation() && "getIdentifierInfo() on an annotation token!");
    if (isLiteral())
      return nullptr;
    if (is(tok::eof))
      return nullptr;
    return (IdentifierInfo *)PtrData;
  }
  void setIdentifierInfo(IdentifierInfo *II) { PtrData = (void *)II; }

  const void *getEofData() const {
    assert(is(tok::eof));
    return reinterpret_cast<const void *>(PtrData);
  }
  void setEofData(const void *D) {
    assert(is(tok::eof));
    assert(!PtrData);
    PtrData = const_cast<void *>(D);
  }

  llvm::StringRef getRawIdentifier() const {
    assert(is(tok::raw_identifier));
    return llvm::StringRef(reinterpret_cast<const char *>(PtrData),
                           getLength());
  }
  void setRawIdentifierData(const char *Ptr) {
    assert(is(tok::raw_identifier));
    PtrData = const_cast<char *>(Ptr);
  }

  const char *getLiteralData() const {
    assert(isLiteral() && "Cannot get literal data of non-literal");
    return reinterpret_cast<const char *>(PtrData);
  }
  void setLiteralData(const char *Ptr) {
    assert(isLiteral() && "Cannot set literal data of non-literal");
    PtrData = const_cast<char *>(Ptr);
  }

  void *getAnnotationValue() const {
    assert(isAnnotation() && "Used AnnotVal on non-annotation token");
    return PtrData;
  }
  void setAnnotationValue(void *val) {
    assert(isAnnotation() && "Used AnnotVal on non-annotation token");
    PtrData = val;
  }

  void setFlag(TokenFlags Flag) { Flags |= Flag; }

  bool getFlag(TokenFlags Flag) const { return (Flags & Flag) != 0; }

  void clearFlag(TokenFlags Flag) { Flags &= ~Flag; }

  unsigned getFlags() const { return Flags; }

  void setFlagValue(TokenFlags Flag, bool Val) {
    if (Val)
      setFlag(Flag);
    else
      clearFlag(Flag);
  }

  bool isAtStartOfLine() const { return getFlag(StartOfLine); }

  bool hasLeadingSpace() const { return getFlag(LeadingSpace); }

  bool isExpandDisabled() const { return getFlag(DisableExpand); }

  bool needsCleaning() const { return getFlag(NeedsCleaning); }

  bool hasLeadingEmptyMacro() const { return getFlag(LeadingEmptyMacro); }

  bool hasUCN() const { return getFlag(HasUCN); }

  bool stringifiedInMacro() const { return getFlag(StringifiedInMacro); }

  bool commaAfterElided() const { return getFlag(CommaAfterElided); }
};

struct PPConditionalInfo {
  SourceLocation IfLoc;

  bool WasSkipping;

  bool FoundNonSkip;

  bool FoundElse;
};

} // end namespace neverc

#endif // NEVERC_SCAN_TOKEN_H
