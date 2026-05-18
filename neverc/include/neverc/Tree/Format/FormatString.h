#ifndef NEVERC_AST_FORMATSTRING_H
#define NEVERC_AST_FORMATSTRING_H

#include "neverc/Tree/Type/CanonicalType.h"
#include <optional>

namespace neverc {

class TargetInfo;

//===----------------------------------------------------------------------===//
namespace analyze_format_string {

class OptionalFlag {
public:
  OptionalFlag(const char *Representation)
      : representation(Representation), flag(false) {}
  bool isSet() const { return flag; }
  void set() { flag = true; }
  void clear() { flag = false; }
  void setPosition(const char *position) {
    assert(position);
    flag = true;
    this->position = position;
  }
  const char *getPosition() const {
    assert(position);
    return position;
  }
  const char *toString() const { return representation; }

  // Overloaded operators for bool like qualities
  explicit operator bool() const { return flag; }
  OptionalFlag &operator=(const bool &rhs) {
    flag = rhs;
    return *this; // Return a reference to myself.
  }

private:
  const char *representation;
  const char *position;
  bool flag;
};

class LengthModifier {
public:
  enum Kind {
    None,
    AsChar,             // 'hh'
    AsShort,            // 'h'
    AsShortLong,        // 'hl' (vector element)
    AsLong,             // 'l'
    AsLongLong,         // 'll'
    AsQuad,             // 'q' (BSD, deprecated, for 64-bit integer types)
    AsIntMax,           // 'j'
    AsSizeT,            // 'z'
    AsPtrDiff,          // 't'
    AsInt32,            // 'I32' (MSVCRT, like __int32)
    AsInt3264,          // 'I'   (MSVCRT, like __int3264 from MIDL)
    AsInt64,            // 'I64' (MSVCRT, like __int64)
    AsLongDouble,       // 'L'
    AsAllocate,         // for '%as', GNU extension to C90 scanf
    AsMAllocate,        // for '%ms', GNU extension to scanf
    AsWide,             // 'w' (MSVCRT, like l but only for c, C, s, S, or Z
    AsWideChar = AsLong // for '%ls', only makes sense for printf
  };

  LengthModifier() : Position(nullptr), kind(None) {}
  LengthModifier(const char *pos, Kind k) : Position(pos), kind(k) {}

  const char *getStart() const { return Position; }

  unsigned getLength() const {
    switch (kind) {
    default:
      return 1;
    case AsLongLong:
    case AsChar:
      return 2;
    case AsInt32:
    case AsInt64:
      return 3;
    case None:
      return 0;
    }
  }

  Kind getKind() const { return kind; }
  void setKind(Kind k) { kind = k; }

  const char *toString() const;

private:
  const char *Position;
  Kind kind;
};

class ConversionSpecifier {
public:
  enum Kind {
    InvalidSpecifier = 0,
    // C99 conversion specifiers.
    cArg,
    dArg,
    DArg, // Apple extension
    iArg,
    // C23 conversion specifiers.
    bArg,
    BArg,

    IntArgBeg = dArg,
    IntArgEnd = BArg,

    oArg,
    OArg, // Apple extension
    uArg,
    UArg, // Apple extension
    xArg,
    XArg,
    UIntArgBeg = oArg,
    UIntArgEnd = XArg,

    fArg,
    FArg,
    eArg,
    EArg,
    gArg,
    GArg,
    aArg,
    AArg,
    DoubleArgBeg = fArg,
    DoubleArgEnd = AArg,

    sArg,
    pArg,
    nArg,
    PercentArg,
    CArg,
    SArg,

    // Apple extension: P specifies to os_log that the data being pointed to is
    // to be copied by os_log. The precision indicates the number of bytes to
    // copy.
    PArg,

    // ** Printf-specific **

    ZArg, // MS extension

    // GlibC specific specifiers.
    PrintErrno, // 'm'

    // ** Scanf-specific **
    ScanListArg, // '['
    ScanfConvBeg = ScanListArg,
    ScanfConvEnd = ScanListArg
  };

  ConversionSpecifier(bool isPrintf = true)
      : IsPrintf(isPrintf), Position(nullptr), EndScanList(nullptr),
        kind(InvalidSpecifier) {}

  ConversionSpecifier(bool isPrintf, const char *pos, Kind k)
      : IsPrintf(isPrintf), Position(pos), EndScanList(nullptr), kind(k) {}

  const char *getStart() const { return Position; }

  llvm::StringRef getCharacters() const {
    return llvm::StringRef(getStart(), getLength());
  }

  bool consumesDataArgument() const {
    switch (kind) {
    case PrintErrno:
      assert(IsPrintf);
      return false;
    case PercentArg:
      return false;
    case InvalidSpecifier:
      return false;
    default:
      return true;
    }
  }

  Kind getKind() const { return kind; }
  void setKind(Kind k) { kind = k; }
  unsigned getLength() const {
    return EndScanList ? EndScanList - Position : 1;
  }
  void setEndScanList(const char *pos) { EndScanList = pos; }

  bool isIntArg() const { return kind >= IntArgBeg && kind <= IntArgEnd; }
  bool isUIntArg() const { return kind >= UIntArgBeg && kind <= UIntArgEnd; }
  bool isAnyIntArg() const { return kind >= IntArgBeg && kind <= UIntArgEnd; }
  bool isDoubleArg() const {
    return kind >= DoubleArgBeg && kind <= DoubleArgEnd;
  }

  const char *toString() const;

  bool isPrintfKind() const { return IsPrintf; }

  std::optional<ConversionSpecifier> getStandardSpecifier() const;

protected:
  bool IsPrintf;
  const char *Position;
  const char *EndScanList;
  Kind kind;
};

class ArgType {
public:
  enum Kind {
    UnknownTy,
    InvalidTy,
    SpecificTy,
    CPointerTy,
    AnyCharTy,
    CStrTy,
    WCStrTy,
    WIntTy
  };

  enum MatchKind {
    /// The conversion specifier and the argument types are incompatible. For
    /// instance, "%d" and float.
    NoMatch = 0,
    /// The conversion specifier and the argument type are compatible. For
    /// instance, "%d" and int.
    Match = 1,
    /// The conversion specifier and the argument type are compatible because of
    /// default argument promotions. For instance, "%hhd" and int.
    MatchPromotion,
    /// The conversion specifier and the argument type are compatible but still
    /// seems likely to be an error. For instanace, "%hhd" and short.
    NoMatchPromotionTypeConfusion,
    /// The conversion specifier and the argument type are disallowed by the C
    /// standard, but are in practice harmless. For instance, "%p" and int*.
    NoMatchPedantic,
    /// The conversion specifier and the argument type are compatible, but still
    /// seems likely to be an error. For instance, "%hd" and _Bool.
    NoMatchTypeConfusion,
  };

private:
  const Kind K;
  QualType T;
  const char *Name = nullptr;
  bool Ptr = false;

  enum class TypeKind { DontCare, SizeT, PtrdiffT };
  TypeKind TK = TypeKind::DontCare;

public:
  ArgType(Kind K = UnknownTy, const char *N = nullptr) : K(K), Name(N) {}
  ArgType(QualType T, const char *N = nullptr) : K(SpecificTy), T(T), Name(N) {}
  ArgType(CanQualType T) : K(SpecificTy), T(T) {}

  static ArgType Invalid() { return ArgType(InvalidTy); }
  bool isValid() const { return K != InvalidTy; }

  bool isSizeT() const { return TK == TypeKind::SizeT; }

  bool isPtrdiffT() const { return TK == TypeKind::PtrdiffT; }

  static ArgType PtrTo(const ArgType &A) {
    assert(A.K >= InvalidTy && "ArgType cannot be pointer to invalid/unknown");
    ArgType Res = A;
    Res.Ptr = true;
    return Res;
  }

  static ArgType makeSizeT(const ArgType &A) {
    ArgType Res = A;
    Res.TK = TypeKind::SizeT;
    return Res;
  }

  static ArgType makePtrdiffT(const ArgType &A) {
    ArgType Res = A;
    Res.TK = TypeKind::PtrdiffT;
    return Res;
  }

  MatchKind matchesType(TreeContext &C, QualType argTy) const;

  QualType getRepresentativeType(TreeContext &C) const;

  ArgType makeVectorType(TreeContext &C, unsigned NumElts) const;

  std::string getRepresentativeTypeName(TreeContext &C) const;
};

class OptionalAmount {
public:
  enum HowSpecified { NotSpecified, Constant, Arg, Invalid };

  OptionalAmount(HowSpecified howSpecified, unsigned amount,
                 const char *amountStart, unsigned amountLength,
                 bool usesPositionalArg)
      : start(amountStart), length(amountLength), hs(howSpecified), amt(amount),
        UsesPositionalArg(usesPositionalArg), UsesDotPrefix(false) {}

  OptionalAmount(bool valid = true)
      : start(nullptr), length(0), hs(valid ? NotSpecified : Invalid), amt(0),
        UsesPositionalArg(false), UsesDotPrefix(false) {}

  explicit OptionalAmount(unsigned Amount)
      : start(nullptr), length(0), hs(Constant), amt(Amount),
        UsesPositionalArg(false), UsesDotPrefix(false) {}

  bool isInvalid() const { return hs == Invalid; }

  HowSpecified getHowSpecified() const { return hs; }
  void setHowSpecified(HowSpecified h) { hs = h; }

  bool hasDataArgument() const { return hs == Arg; }

  unsigned getArgIndex() const {
    assert(hasDataArgument());
    return amt;
  }

  unsigned getConstantAmount() const {
    assert(hs == Constant);
    return amt;
  }

  const char *getStart() const {
    // We include the . character if it is given.
    return start - UsesDotPrefix;
  }

  unsigned getConstantLength() const {
    assert(hs == Constant);
    return length + UsesDotPrefix;
  }

  ArgType getArgType(TreeContext &Ctx) const;

  void toString(llvm::raw_ostream &os) const;

  bool usesPositionalArg() const { return (bool)UsesPositionalArg; }
  unsigned getPositionalArgIndex() const {
    assert(hasDataArgument());
    return amt + 1;
  }

  bool usesDotPrefix() const { return UsesDotPrefix; }
  void setUsesDotPrefix() { UsesDotPrefix = true; }

private:
  const char *start;
  unsigned length;
  HowSpecified hs;
  unsigned amt;
  bool UsesPositionalArg : 1;
  bool UsesDotPrefix;
};

class FormatSpecifier {
protected:
  LengthModifier LM;
  OptionalAmount FieldWidth;
  ConversionSpecifier CS;
  OptionalAmount VectorNumElts;

  bool UsesPositionalArg;
  unsigned argIndex;

public:
  FormatSpecifier(bool isPrintf)
      : CS(isPrintf), VectorNumElts(false), UsesPositionalArg(false),
        argIndex(0) {}

  void setLengthModifier(LengthModifier lm) { LM = lm; }

  void setUsesPositionalArg() { UsesPositionalArg = true; }

  void setArgIndex(unsigned i) { argIndex = i; }

  unsigned getArgIndex() const { return argIndex; }

  unsigned getPositionalArgIndex() const { return argIndex + 1; }

  const LengthModifier &getLengthModifier() const { return LM; }

  const OptionalAmount &getFieldWidth() const { return FieldWidth; }

  void setVectorNumElts(const OptionalAmount &Amt) { VectorNumElts = Amt; }

  const OptionalAmount &getVectorNumElts() const { return VectorNumElts; }

  void setFieldWidth(const OptionalAmount &Amt) { FieldWidth = Amt; }

  bool usesPositionalArg() const { return UsesPositionalArg; }

  bool hasValidLengthModifier(const TargetInfo &Target,
                              const LangOptions &LO) const;

  bool hasStandardLengthModifier() const;

  std::optional<LengthModifier> getCorrectedLengthModifier() const;

  bool hasStandardConversionSpecifier(const LangOptions &LangOpt) const;

  bool hasStandardLengthConversionCombination() const;

  static bool namedTypeToLengthModifier(QualType QT, LengthModifier &LM);
};

} // namespace analyze_format_string

//===----------------------------------------------------------------------===//

namespace analyze_printf {

class PrintfConversionSpecifier
    : public analyze_format_string::ConversionSpecifier {
public:
  PrintfConversionSpecifier()
      : ConversionSpecifier(true, nullptr, InvalidSpecifier) {}

  PrintfConversionSpecifier(const char *pos, Kind k)
      : ConversionSpecifier(true, pos, k) {}

  bool isDoubleArg() const {
    return kind >= DoubleArgBeg && kind <= DoubleArgEnd;
  }

  static bool classof(const analyze_format_string::ConversionSpecifier *CS) {
    return CS->isPrintfKind();
  }
};

using analyze_format_string::ArgType;
using analyze_format_string::LengthModifier;
using analyze_format_string::OptionalAmount;
using analyze_format_string::OptionalFlag;

class PrintfSpecifier : public analyze_format_string::FormatSpecifier {
  OptionalFlag HasThousandsGrouping; // ''', POSIX extension.
  OptionalFlag IsLeftJustified;      // '-'
  OptionalFlag HasPlusPrefix;        // '+'
  OptionalFlag HasSpacePrefix;       // ' '
  OptionalFlag HasAlternativeForm;   // '#'
  OptionalFlag HasLeadingZeroes;     // '0'
  OptionalFlag IsPrivate;            // '{private}'
  OptionalFlag IsPublic;             // '{public}'
  OptionalFlag IsSensitive;          // '{sensitive}'
  OptionalAmount Precision;
  llvm::StringRef MaskType;

  ArgType getScalarArgType(TreeContext &Ctx) const;

public:
  PrintfSpecifier()
      : FormatSpecifier(/* isPrintf = */ true), HasThousandsGrouping("'"),
        IsLeftJustified("-"), HasPlusPrefix("+"), HasSpacePrefix(" "),
        HasAlternativeForm("#"), HasLeadingZeroes("0"), IsPrivate("private"),
        IsPublic("public"), IsSensitive("sensitive") {}

  static PrintfSpecifier Parse(const char *beg, const char *end);

  // Methods for incrementally constructing the PrintfSpecifier.
  void setConversionSpecifier(const PrintfConversionSpecifier &cs) { CS = cs; }
  void setHasThousandsGrouping(const char *position) {
    HasThousandsGrouping.setPosition(position);
  }
  void setIsLeftJustified(const char *position) {
    IsLeftJustified.setPosition(position);
  }
  void setHasPlusPrefix(const char *position) {
    HasPlusPrefix.setPosition(position);
  }
  void setHasSpacePrefix(const char *position) {
    HasSpacePrefix.setPosition(position);
  }
  void setHasAlternativeForm(const char *position) {
    HasAlternativeForm.setPosition(position);
  }
  void setHasLeadingZeros(const char *position) {
    HasLeadingZeroes.setPosition(position);
  }
  void setIsPrivate(const char *position) { IsPrivate.setPosition(position); }
  void setIsPublic(const char *position) { IsPublic.setPosition(position); }
  void setIsSensitive(const char *position) {
    IsSensitive.setPosition(position);
  }
  void setUsesPositionalArg() { UsesPositionalArg = true; }

  // Methods for querying the format specifier.

  const PrintfConversionSpecifier &getConversionSpecifier() const {
    return cast<PrintfConversionSpecifier>(CS);
  }

  void setPrecision(const OptionalAmount &Amt) {
    Precision = Amt;
    Precision.setUsesDotPrefix();
  }

  const OptionalAmount &getPrecision() const { return Precision; }

  bool consumesDataArgument() const {
    return getConversionSpecifier().consumesDataArgument();
  }

  ArgType getArgType(TreeContext &Ctx) const;

  const OptionalFlag &hasThousandsGrouping() const {
    return HasThousandsGrouping;
  }
  const OptionalFlag &isLeftJustified() const { return IsLeftJustified; }
  const OptionalFlag &hasPlusPrefix() const { return HasPlusPrefix; }
  const OptionalFlag &hasAlternativeForm() const { return HasAlternativeForm; }
  const OptionalFlag &hasLeadingZeros() const { return HasLeadingZeroes; }
  const OptionalFlag &hasSpacePrefix() const { return HasSpacePrefix; }
  const OptionalFlag &isPrivate() const { return IsPrivate; }
  const OptionalFlag &isPublic() const { return IsPublic; }
  const OptionalFlag &isSensitive() const { return IsSensitive; }
  bool usesPositionalArg() const { return UsesPositionalArg; }

  llvm::StringRef getMaskType() const { return MaskType; }
  void setMaskType(llvm::StringRef S) { MaskType = S; }

  bool fixType(QualType QT, const LangOptions &LangOpt, TreeContext &Ctx);

  void toString(llvm::raw_ostream &os) const;

  // Validation methods - to check if any element results in undefined behavior
  bool hasValidPlusPrefix() const;
  bool hasValidAlternativeForm() const;
  bool hasValidLeadingZeros() const;
  bool hasValidSpacePrefix() const;
  bool hasValidLeftJustified() const;
  bool hasValidThousandsGroupingPrefix() const;

  bool hasValidPrecision() const;
  bool hasValidFieldWidth() const;
};
} // namespace analyze_printf

//===----------------------------------------------------------------------===//

namespace analyze_scanf {

class ScanfConversionSpecifier
    : public analyze_format_string::ConversionSpecifier {
public:
  ScanfConversionSpecifier()
      : ConversionSpecifier(false, nullptr, InvalidSpecifier) {}

  ScanfConversionSpecifier(const char *pos, Kind k)
      : ConversionSpecifier(false, pos, k) {}

  static bool classof(const analyze_format_string::ConversionSpecifier *CS) {
    return !CS->isPrintfKind();
  }
};

using analyze_format_string::ArgType;
using analyze_format_string::LengthModifier;
using analyze_format_string::OptionalAmount;
using analyze_format_string::OptionalFlag;

class ScanfSpecifier : public analyze_format_string::FormatSpecifier {
  OptionalFlag SuppressAssignment; // '*'
public:
  ScanfSpecifier()
      : FormatSpecifier(/* isPrintf = */ false), SuppressAssignment("*") {}

  void setSuppressAssignment(const char *position) {
    SuppressAssignment.setPosition(position);
  }

  const OptionalFlag &getSuppressAssignment() const {
    return SuppressAssignment;
  }

  void setConversionSpecifier(const ScanfConversionSpecifier &cs) { CS = cs; }

  const ScanfConversionSpecifier &getConversionSpecifier() const {
    return cast<ScanfConversionSpecifier>(CS);
  }

  bool consumesDataArgument() const {
    return CS.consumesDataArgument() && !SuppressAssignment;
  }

  ArgType getArgType(TreeContext &Ctx) const;

  bool fixType(QualType QT, QualType RawQT, const LangOptions &LangOpt,
               TreeContext &Ctx);

  void toString(llvm::raw_ostream &os) const;

  static ScanfSpecifier Parse(const char *beg, const char *end);
};

} // namespace analyze_scanf

//===----------------------------------------------------------------------===//
// Parsing and processing of format strings (both fprintf and fscanf).

namespace analyze_format_string {

enum PositionContext { FieldWidthPos = 0, PrecisionPos = 1 };

class FormatStringHandler {
public:
  FormatStringHandler() {}
  virtual ~FormatStringHandler();

  virtual void HandleNullChar(const char *nullCharacter) {}

  virtual void HandlePosition(const char *startPos, unsigned posLen) {}

  virtual void HandleInvalidPosition(const char *startPos, unsigned posLen,
                                     PositionContext p) {}

  virtual void HandleZeroPosition(const char *startPos, unsigned posLen) {}

  virtual void HandleIncompleteSpecifier(const char *startSpecifier,
                                         unsigned specifierLen) {}

  // Printf-specific handlers.

  virtual bool HandleInvalidPrintfConversionSpecifier(
      const analyze_printf::PrintfSpecifier &FS, const char *startSpecifier,
      unsigned specifierLen) {
    return true;
  }

  virtual bool HandlePrintfSpecifier(const analyze_printf::PrintfSpecifier &FS,
                                     const char *startSpecifier,
                                     unsigned specifierLen,
                                     const TargetInfo &Target) {
    return true;
  }

  virtual void handleInvalidMaskType(llvm::StringRef MaskType) {}

  // Scanf-specific handlers.

  virtual bool
  HandleInvalidScanfConversionSpecifier(const analyze_scanf::ScanfSpecifier &FS,
                                        const char *startSpecifier,
                                        unsigned specifierLen) {
    return true;
  }

  virtual bool HandleScanfSpecifier(const analyze_scanf::ScanfSpecifier &FS,
                                    const char *startSpecifier,
                                    unsigned specifierLen) {
    return true;
  }

  virtual void HandleIncompleteScanList(const char *start, const char *end) {}
};

bool ParsePrintfString(FormatStringHandler &H, const char *beg, const char *end,
                       const LangOptions &LO, const TargetInfo &Target);

bool ParseScanfString(FormatStringHandler &H, const char *beg, const char *end,
                      const LangOptions &LO, const TargetInfo &Target);

bool parseFormatStringHasFormattingSpecifiers(const char *Begin,
                                              const char *End,
                                              const LangOptions &LO,
                                              const TargetInfo &Target);

} // namespace analyze_format_string
} // namespace neverc
#endif
