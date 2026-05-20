#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Attr/AttrSubjectMatchRules.h"
#include "neverc/Foundation/Attr/Attributes.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Diagnostic/DiagnosticLex.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/LangOpts/TypeTraits.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Identifier table implementation
// ===----------------------------------------------------------------------===

static_assert(2 * LargestBuiltinID < (2 << (BuiltinIDBits - 1)),
              "Insufficient BuiltinID bits");

IdentifierIterator::~IdentifierIterator() = default;

IdentifierInfoLookup::~IdentifierInfoLookup() = default;

namespace {

class EmptyLookupIterator : public IdentifierIterator {
public:
  llvm::StringRef Next() override { return llvm::StringRef(); }
};

} // namespace

IdentifierIterator *IdentifierInfoLookup::getIdentifiers() {
  return new EmptyLookupIterator();
}

IdentifierTable::IdentifierTable(IdentifierInfoLookup *ExternalLookup)
    : HashTable(16384), // Start with space for 16K identifiers to avoid early
                        // rehashing.
      ExternalLookup(ExternalLookup) {}

IdentifierTable::IdentifierTable(const LangOptions &LangOpts,
                                 IdentifierInfoLookup *ExternalLookup)
    : IdentifierTable(ExternalLookup) {
  AddKeywords(LangOpts);
}

LLVM_ATTRIBUTE_NOINLINE
IdentifierInfo &IdentifierTable::getOrCreateIdentifier(llvm::StringRef Name) {
  auto &Entry = *HashTable.try_emplace(Name, nullptr).first;

  IdentifierInfo *&II = Entry.second;
  if (II)
    return *II;

  if (ExternalLookup) {
    II = ExternalLookup->get(Name);
    if (II)
      return *II;
  }

  void *Mem = getAllocator().Allocate<IdentifierInfo>();
  II = new (Mem) IdentifierInfo();
  II->Entry = &Entry;
  return *II;
}

namespace {

enum TokenKey : unsigned {
  KEYC99 = 0x1,
  KEYGNU = 0x8,
  KEYMS = 0x10,
  BOOLSUPPORT = 0x20,
  KEYC23 = 0x400,
  KEYNOMS18 = 0x800,
  WCHARSUPPORT = 0x2000,
  CHAR8SUPPORT = 0x8000,
  KEYMSCOMPAT = 0x400000,
  KEYFIXEDPOINT = 0x4000000,
  KEYNEVERC = 0x8000000,
  KEYMAX = KEYNEVERC,
  KEYALL = (KEYMAX | (KEYMAX - 1)) & ~KEYNOMS18 & ~KEYNEVERC & ~0x10000 &
           ~0x100 & ~0x80
};

enum KeywordStatus {
  KS_Unknown,   // Not yet calculated. Used when figuring out the status.
  KS_Disabled,  // Disabled
  KS_Future,    // Is a keyword in future standard
  KS_Extension, // Is an extension
  KS_Enabled,   // Enabled
};

} // namespace

namespace {
KeywordStatus resolveKeywordFlag(const LangOptions &LangOpts, TokenKey Flag) {
  assert((Flag & ~(Flag - 1)) == Flag && "Expected single bit");

  switch (Flag) {
  case KEYC99:
    return LangOpts.C99 ? KS_Enabled : KS_Future;
  case KEYC23:
    return LangOpts.C23 ? KS_Enabled : KS_Future;
  case KEYGNU:
    return LangOpts.GNUKeywords ? KS_Extension : KS_Unknown;
  case KEYMS:
    return LangOpts.MicrosoftExt ? KS_Extension : KS_Unknown;
  case BOOLSUPPORT:
    return LangOpts.Bool ? KS_Enabled : KS_Future;
  case WCHARSUPPORT:
    return LangOpts.WChar ? KS_Enabled : KS_Unknown;
  case CHAR8SUPPORT:
    return LangOpts.Char8 ? KS_Enabled : KS_Unknown;
  case KEYMSCOMPAT:
    return LangOpts.MSVCCompat ? KS_Enabled : KS_Unknown;
  case KEYNOMS18:
    return KS_Unknown;
  case KEYFIXEDPOINT:
    return LangOpts.FixedPoint ? KS_Enabled : KS_Disabled;
  case KEYNEVERC:
    return LangOpts.NeverCTypes ? KS_Enabled : KS_Disabled;
  default:
    return KS_Unknown;
  }
}

KeywordStatus computeKeywordState(const LangOptions &LangOpts, unsigned Flags) {
  if (Flags == KEYALL)
    return KS_Enabled;
  if (LangOpts.MSVCCompat && (Flags & KEYNOMS18) &&
      !LangOpts.isCompatibleWithMSVC(LangOptions::MSVC2015))
    return KS_Disabled;

  KeywordStatus Best = KS_Unknown;

  while (Flags != 0) {
    unsigned Bit = llvm::countr_zero(Flags);
    unsigned CurFlag = 1u << Bit;
    Flags ^= CurFlag;
    KeywordStatus S =
        resolveKeywordFlag(LangOpts, static_cast<TokenKey>(CurFlag));
    if (LLVM_LIKELY(S > Best)) {
      Best = S;
      if (Best == KS_Enabled)
        return KS_Enabled;
    }
  }

  return Best == KS_Unknown ? KS_Disabled : Best;
}
} // namespace

namespace {
void enrollKeyword(llvm::StringRef Keyword, tok::TokenKind TokenCode,
                   unsigned Flags, const LangOptions &LangOpts,
                   IdentifierTable &Table) {
  KeywordStatus State = computeKeywordState(LangOpts, Flags);
  if (State == KS_Disabled)
    return;

  IdentifierInfo &Info =
      Table.get(Keyword, State == KS_Future ? tok::identifier : TokenCode);
  Info.setIsExtensionToken(State == KS_Extension);
  Info.setIsFutureCompatKeyword(State == KS_Future);
}

void enrollNotableIdentifier(llvm::StringRef Name,
                             tok::InterestingIdentifierKind BTID,
                             IdentifierTable &Table) {
  if (BTID != tok::not_interesting) {
    IdentifierInfo &Info = Table.get(Name, tok::identifier);
    Info.setInterestingIdentifierID(BTID);
  }
}
} // namespace

void IdentifierTable::AddKeywords(const LangOptions &LangOpts) {
#define KEYWORD(NAME, FLAGS)                                                   \
  enrollKeyword(llvm::StringRef(#NAME), tok::kw_##NAME, FLAGS, LangOpts, *this);
#define ALIAS(NAME, TOK, FLAGS)                                                \
  enrollKeyword(llvm::StringRef(NAME), tok::kw_##TOK, FLAGS, LangOpts, *this);
#define INTERESTING_IDENTIFIER(NAME)                                           \
  enrollNotableIdentifier(llvm::StringRef(#NAME), tok::NAME, *this);

#define TESTING_KEYWORD(NAME, FLAGS)
#include "neverc/Foundation/Core/TokenKinds.def"

  if (LangOpts.DeclSpecKeyword)
    enrollKeyword("__declspec", tok::kw___declspec, KEYALL, LangOpts, *this);
}

namespace {
KeywordStatus queryTokenKeywordState(const LangOptions &LangOpts,
                                     tok::TokenKind K) {
  switch (K) {
#define KEYWORD(NAME, FLAGS)                                                   \
  case tok::kw_##NAME:                                                         \
    return computeKeywordState(LangOpts, FLAGS);
#include "neverc/Foundation/Core/TokenKinds.def"
  default:
    return KS_Disabled;
  }
}
} // namespace

bool IdentifierInfo::isKeyword(const LangOptions &LangOpts) const {
  switch (queryTokenKeywordState(LangOpts, getTokenID())) {
  case KS_Enabled:
  case KS_Extension:
    return true;
  default:
    return false;
  }
}

ReservedIdentifierStatus
IdentifierInfo::isReserved(const LangOptions &LangOpts) const {
  llvm::StringRef Name = getName();
  const unsigned Len = Name.size();
  if (LLVM_LIKELY(Len <= 1))
    return ReservedIdentifierStatus::NotReserved;

  const unsigned char C0 = static_cast<unsigned char>(Name[0]);
  const unsigned char C1 = static_cast<unsigned char>(Name[1]);

  if (LLVM_UNLIKELY(C0 == '_')) {
    unsigned IsUnderscore = C1 == '_';
    unsigned IsUpper = (C1 - 'A') < 26u;

    if (IsUnderscore)
      return ReservedIdentifierStatus::StartsWithDoubleUnderscore;
    if (IsUpper)
      return ReservedIdentifierStatus::
          StartsWithUnderscoreFollowedByCapitalLetter;
    return ReservedIdentifierStatus::StartsWithUnderscoreAtGlobalScope;
  }

  return ReservedIdentifierStatus::NotReserved;
}

llvm::StringRef IdentifierInfo::deuglifiedName() const {
  llvm::StringRef Name = getName();
  if (Name.size() >= 2 && Name.front() == '_' &&
      (Name[1] == '_' || (Name[1] >= 'A' && Name[1] <= 'Z')))
    return Name.ltrim('_');
  return Name;
}

tok::PPKeywordKind IdentifierInfo::getPPKeywordID() const {
  unsigned Len = getLength();
  if (LLVM_UNLIKELY(Len < 2))
    return tok::pp_not_keyword;
  const char *Name = getNameStart();

  if (LLVM_LIKELY(Len <= 8)) {
    uint64_t Key = 0;
    std::memcpy(&Key, Name, Len);

    switch (Key) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define PP_KEY2(a, b) (uint64_t(a) | (uint64_t(b) << 8))
#define PP_KEY4(a, b, c, d)                                                    \
  (PP_KEY2(a, b) | (uint64_t(c) << 16) | (uint64_t(d) << 24))
#define PP_KEY5(a, b, c, d, e) (PP_KEY4(a, b, c, d) | (uint64_t(e) << 32))
#define PP_KEY6(a, b, c, d, e, f) (PP_KEY5(a, b, c, d, e) | (uint64_t(f) << 40))
#define PP_KEY7(a, b, c, d, e, f, g)                                           \
  (PP_KEY6(a, b, c, d, e, f) | (uint64_t(g) << 48))
#define PP_KEY8(a, b, c, d, e, f, g, h)                                        \
  (PP_KEY7(a, b, c, d, e, f, g) | (uint64_t(h) << 56))
#else
#define PP_KEY2(a, b) ((uint64_t(a) << 56) | (uint64_t(b) << 48))
#define PP_KEY4(a, b, c, d)                                                    \
  ((uint64_t(a) << 56) | (uint64_t(b) << 48) | (uint64_t(c) << 40) |           \
   (uint64_t(d) << 32))
#define PP_KEY5(a, b, c, d, e) (PP_KEY4(a, b, c, d) | (uint64_t(e) << 24))
#define PP_KEY6(a, b, c, d, e, f) (PP_KEY5(a, b, c, d, e) | (uint64_t(f) << 16))
#define PP_KEY7(a, b, c, d, e, f, g)                                           \
  (PP_KEY6(a, b, c, d, e, f) | (uint64_t(g) << 8))
#define PP_KEY8(a, b, c, d, e, f, g, h)                                        \
  (PP_KEY7(a, b, c, d, e, f, g) | uint64_t(h))
#endif
    case PP_KEY2('i', 'f'):
      return Len == 2 ? tok::pp_if : tok::pp_not_keyword;
    case PP_KEY4('e', 'l', 'i', 'f'):
      return Len == 4 ? tok::pp_elif : tok::pp_not_keyword;
    case PP_KEY4('e', 'l', 's', 'e'):
      return Len == 4 ? tok::pp_else : tok::pp_not_keyword;
    case PP_KEY4('l', 'i', 'n', 'e'):
      return Len == 4 ? tok::pp_line : tok::pp_not_keyword;
    case PP_KEY4('s', 'c', 'c', 's'):
      return Len == 4 ? tok::pp_sccs : tok::pp_not_keyword;
    case PP_KEY5('e', 'n', 'd', 'i', 'f'):
      return Len == 5 ? tok::pp_endif : tok::pp_not_keyword;
    case PP_KEY5('e', 'r', 'r', 'o', 'r'):
      return Len == 5 ? tok::pp_error : tok::pp_not_keyword;
    case PP_KEY5('i', 'd', 'e', 'n', 't'):
      return Len == 5 ? tok::pp_ident : tok::pp_not_keyword;
    case PP_KEY5('i', 'f', 'd', 'e', 'f'):
      return Len == 5 ? tok::pp_ifdef : tok::pp_not_keyword;
    case PP_KEY5('u', 'n', 'd', 'e', 'f'):
      return Len == 5 ? tok::pp_undef : tok::pp_not_keyword;
    case PP_KEY6('a', 's', 's', 'e', 'r', 't'):
      return Len == 6 ? tok::pp_assert : tok::pp_not_keyword;
    case PP_KEY6('d', 'e', 'f', 'i', 'n', 'e'):
      return Len == 6 ? tok::pp_define : tok::pp_not_keyword;
    case PP_KEY6('i', 'f', 'n', 'd', 'e', 'f'):
      return Len == 6 ? tok::pp_ifndef : tok::pp_not_keyword;
    case PP_KEY6('i', 'm', 'p', 'o', 'r', 't'):
      return Len == 6 ? tok::pp_import : tok::pp_not_keyword;
    case PP_KEY6('p', 'r', 'a', 'g', 'm', 'a'):
      return Len == 6 ? tok::pp_pragma : tok::pp_not_keyword;
    case PP_KEY7('d', 'e', 'f', 'i', 'n', 'e', 'd'):
      return Len == 7 ? tok::pp_defined : tok::pp_not_keyword;
    case PP_KEY7('e', 'l', 'i', 'f', 'd', 'e', 'f'):
      return Len == 7 ? tok::pp_elifdef : tok::pp_not_keyword;
    case PP_KEY7('i', 'n', 'c', 'l', 'u', 'd', 'e'):
      return Len == 7 ? tok::pp_include : tok::pp_not_keyword;
    case PP_KEY7('w', 'a', 'r', 'n', 'i', 'n', 'g'):
      return Len == 7 ? tok::pp_warning : tok::pp_not_keyword;
    case PP_KEY8('e', 'l', 'i', 'f', 'n', 'd', 'e', 'f'):
      return Len == 8 ? tok::pp_elifndef : tok::pp_not_keyword;
    case PP_KEY8('u', 'n', 'a', 's', 's', 'e', 'r', 't'):
      return Len == 8 ? tok::pp_unassert : tok::pp_not_keyword;
#undef PP_KEY2
#undef PP_KEY4
#undef PP_KEY5
#undef PP_KEY6
#undef PP_KEY7
#undef PP_KEY8
    default:
      return tok::pp_not_keyword;
    }
  }

#define HASH(LEN, FIRST, THIRD)                                                \
  (LEN << 5) + (((FIRST - 'a') + (THIRD - 'a')) & 31)
#define CASE(LEN, FIRST, THIRD, NAME)                                          \
  case HASH(LEN, FIRST, THIRD):                                                \
    return memcmp(Name, #NAME, LEN) ? tok::pp_not_keyword : tok::pp_##NAME

  switch (HASH(Len, Name[0], Name[2])) {
  default:
    return tok::pp_not_keyword;
    CASE(12, 'i', 'c', include_next);
    CASE(14, '_', 'p', __public_macro);
    CASE(15, '_', 'p', __private_macro);
    CASE(16, '_', 'i', __include_macros);
#undef CASE
#undef HASH
  }
}

void IdentifierTable::PrintStats() const {
  unsigned NumBuckets = HashTable.getNumBuckets();
  unsigned NumIdentifiers = HashTable.getNumItems();
  unsigned NumEmptyBuckets = NumBuckets - NumIdentifiers;
  uint64_t TotalLen = 0;
  unsigned MaxLen = 0;
  unsigned LenHistogram[16] = {};

  for (const auto &Entry : HashTable) {
    unsigned Len = Entry.getKeyLength();
    TotalLen += Len;
    if (LLVM_UNLIKELY(Len > MaxLen))
      MaxLen = Len;
    unsigned Bucket = Len < 15 ? Len : 15;
    ++LenHistogram[Bucket];
  }

  double Density = NumBuckets ? NumIdentifiers / (double)NumBuckets : 0.0;
  double AvgLen = NumIdentifiers ? TotalLen / (double)NumIdentifiers : 0.0;

  fprintf(stderr, "\n*** Identifier Table Stats:\n");
  fprintf(stderr, "# Identifiers:   %u\n", NumIdentifiers);
  fprintf(stderr, "# Empty Buckets: %u\n", NumEmptyBuckets);
  fprintf(stderr, "Hash density (#identifiers per bucket): %f\n", Density);
  fprintf(stderr, "Ave identifier length: %f\n", AvgLen);
  fprintf(stderr, "Max identifier length: %u\n", MaxLen);
  fprintf(stderr, "Length distribution: ");
  for (unsigned i = 0; i < 15; ++i)
    fprintf(stderr, "[%u]=%u ", i, LenHistogram[i]);
  fprintf(stderr, "[15+]=%u\n", LenHistogram[15]);

  HashTable.getAllocator().PrintStats();
}

llvm::StringRef neverc::getNullabilitySpelling(NullabilityKind kind,
                                               bool isContextSensitive) {
  switch (kind) {
  case NullabilityKind::NonNull:
    return isContextSensitive ? "nonnull" : "_Nonnull";

  case NullabilityKind::Nullable:
    return isContextSensitive ? "nullable" : "_Nullable";

  case NullabilityKind::Unspecified:
    return isContextSensitive ? "null_unspecified" : "_Null_unspecified";
  }
  return isContextSensitive ? "null_unspecified" : "_Null_unspecified";
}

llvm::raw_ostream &neverc::operator<<(llvm::raw_ostream &OS,
                                      NullabilityKind NK) {
  switch (NK) {
  case NullabilityKind::NonNull:
    return OS << "NonNull";
  case NullabilityKind::Nullable:
    return OS << "Nullable";
  case NullabilityKind::Unspecified:
    return OS << "Unspecified";
  }
  return OS << "Unspecified";
}

diag::kind
IdentifierTable::getFutureCompatDiagKind(const IdentifierInfo &II,
                                         const LangOptions &LangOpts) {
  assert(II.isFutureCompatKeyword() && "diagnostic should not be needed");

  unsigned Flags = llvm::StringSwitch<unsigned>(II.getName())
#define KEYWORD(NAME, FLAGS) .Case(#NAME, FLAGS)
#include "neverc/Foundation/Core/TokenKinds.def"
#undef KEYWORD
      ;

  if ((Flags & KEYC99) == KEYC99)
    return diag::warn_c99_keyword;
  if ((Flags & KEYC23) == KEYC23)
    return diag::warn_c23_keyword;

  return diag::warn_c99_keyword;
}

namespace {
void canonicalizeAttrScope(llvm::StringRef &ScopeName) {
  if (ScopeName == "__gnu__")
    ScopeName = "gnu";
  else if (ScopeName == "_NeverC" || ScopeName == "clang")
    ScopeName = "neverc";
}

llvm::StringRef stripAttrUnderscores(llvm::StringRef Raw) {
  if (Raw.size() >= 4 && Raw.starts_with("__") && Raw.ends_with("__"))
    return Raw.substr(2, Raw.size() - 4);
  return Raw;
}

bool needsUnderscoreStripping(AttributeCommonInfo::Syntax SyntaxUsed,
                              llvm::StringRef CanonicalScopeName) {
  return SyntaxUsed == AttributeCommonInfo::AS_GNU ||
         (AttributeCommonInfo::isBracketAttributeSyntax(SyntaxUsed) &&
          (CanonicalScopeName.empty() || CanonicalScopeName == "gnu" ||
           CanonicalScopeName == "neverc"));
}
} // namespace

namespace {
int checkAttributeSupport(AttributeCommonInfo::Syntax Syntax,
                          llvm::StringRef Name, llvm::StringRef ScopeName,
                          const TargetInfo &Target,
                          const LangOptions &LangOpts) {

#include "neverc/Foundation/AttrHasAttributeImpl.td.h"

  return 0;
}
} // namespace

int neverc::hasAttribute(AttributeCommonInfo::Syntax Syntax,
                         const IdentifierInfo *Scope,
                         const IdentifierInfo *Attr, const TargetInfo &Target,
                         const LangOptions &LangOpts) {
  llvm::StringRef Name = stripAttrUnderscores(Attr->getName());

  llvm::StringRef ScopeName = Scope ? Scope->getName() : "";
  canonicalizeAttrScope(ScopeName);

  return checkAttributeSupport(Syntax, Name, ScopeName, Target, LangOpts);
}

const char *attr::getSubjectMatchRuleSpelling(attr::SubjectMatchRule Rule) {
  switch (Rule) {
#define ATTR_MATCH_RULE(NAME, SPELLING, IsAbstract)                            \
  case attr::NAME:                                                             \
    return SPELLING;
#include "neverc/Foundation/AttrSubMatchRulesList.td.h"
  }
  return "";
}

namespace {
llvm::StringRef resolveAttrScopeName(const IdentifierInfo *Scope,
                                     AttributeCommonInfo::Syntax SyntaxUsed) {
  if (!Scope)
    return "";

  llvm::StringRef ScopeName = Scope->getName();
  if (AttributeCommonInfo::isBracketAttributeSyntax(SyntaxUsed))
    canonicalizeAttrScope(ScopeName);
  return ScopeName;
}

llvm::StringRef resolveAttrName(const IdentifierInfo *Name,
                                llvm::StringRef CanonicalScopeName,
                                AttributeCommonInfo::Syntax SyntaxUsed) {
  llvm::StringRef AttrName = Name->getName();
  if (needsUnderscoreStripping(SyntaxUsed, CanonicalScopeName))
    AttrName = stripAttrUnderscores(AttrName);
  return AttrName;
}
} // namespace

namespace {
struct CanonicalAttrSpelling {
  llvm::StringRef Scope;
  llvm::StringRef Name;
};

CanonicalAttrSpelling
canonicalizeAttrSpelling(const IdentifierInfo *AttrName,
                         const IdentifierInfo *Scope,
                         AttributeCommonInfo::Syntax SyntaxUsed) {
  llvm::StringRef ScopeStr = resolveAttrScopeName(Scope, SyntaxUsed);
  llvm::StringRef NameStr = resolveAttrName(AttrName, ScopeStr, SyntaxUsed);
  return {ScopeStr, NameStr};
}
} // namespace

bool AttributeCommonInfo::isGNUScope() const {
  return ScopeName && (ScopeName->isStr("gnu") || ScopeName->isStr("__gnu__"));
}

bool AttributeCommonInfo::isNeverCScope() const {
  return ScopeName &&
         (ScopeName->isStr("neverc") || ScopeName->isStr("_NeverC"));
}

#include "neverc/Analyze/AttrParsedAttrKinds.td.h"

namespace {
llvm::SmallString<64>
buildAttrFullName(const IdentifierInfo *Name, const IdentifierInfo *Scope,
                  AttributeCommonInfo::Syntax SyntaxUsed) {
  const auto [ScopeStr, NameStr] =
      canonicalizeAttrSpelling(Name, Scope, SyntaxUsed);

  llvm::SmallString<64> FullName = ScopeStr;
  if (!ScopeStr.empty()) {
    assert(AttributeCommonInfo::isBracketAttributeSyntax(SyntaxUsed));
    FullName += "::";
  }
  FullName += NameStr;

  return FullName;
}
} // namespace

AttributeCommonInfo::Kind
AttributeCommonInfo::getParsedKind(const IdentifierInfo *Name,
                                   const IdentifierInfo *ScopeName,
                                   Syntax SyntaxUsed) {
  return ::getAttrKind(buildAttrFullName(Name, ScopeName, SyntaxUsed),
                       SyntaxUsed);
}

std::string AttributeCommonInfo::getNormalizedFullName() const {
  return static_cast<std::string>(
      buildAttrFullName(getAttrName(), getScopeName(), getSyntax()));
}

unsigned AttributeCommonInfo::calculateAttributeSpellingListIndex() const {
  auto Syntax = static_cast<AttributeCommonInfo::Syntax>(getSyntax());
  const auto [Scope, Name] =
      canonicalizeAttrSpelling(getAttrName(), getScopeName(), Syntax);

#include "neverc/Analyze/AttrSpellingListIndex.td.h"
}

namespace neverc {
namespace Builtin {

class TargetFeatures {
  struct FeatureListStatus {
    bool HasFeatures;
    llvm::StringRef CurFeaturesList;
  };

  const llvm::StringMap<bool> &CallerFeatureMap;

  FeatureListStatus getAndFeatures(llvm::StringRef FeatureList) {
    int InParentheses = 0;
    bool HasFeatures = true;
    size_t SubexpressionStart = 0;
    for (size_t i = 0, e = FeatureList.size(); i < e; ++i) {
      char CurrentToken = FeatureList[i];
      switch (CurrentToken) {
      default:
        break;
      case '(':
        if (InParentheses == 0)
          SubexpressionStart = i + 1;
        ++InParentheses;
        break;
      case ')':
        --InParentheses;
        assert(InParentheses >= 0 && "Parentheses are not in pair");
        [[fallthrough]];
      case '|':
      case ',':
        if (InParentheses == 0) {
          if (HasFeatures && i != SubexpressionStart) {
            llvm::StringRef F = FeatureList.slice(SubexpressionStart, i);
            HasFeatures = CurrentToken == ')' ? hasRequiredFeatures(F)
                                              : CallerFeatureMap.lookup(F);
          }
          SubexpressionStart = i + 1;
          if (CurrentToken == '|') {
            return {HasFeatures, FeatureList.substr(SubexpressionStart)};
          }
        }
        break;
      }
    }
    assert(InParentheses == 0 && "Parentheses are not in pair");
    if (HasFeatures && SubexpressionStart != FeatureList.size())
      HasFeatures =
          CallerFeatureMap.lookup(FeatureList.substr(SubexpressionStart));
    return {HasFeatures, llvm::StringRef()};
  }

public:
  bool hasRequiredFeatures(llvm::StringRef FeatureList) {
    FeatureListStatus FS = {false, FeatureList};
    while (!FS.HasFeatures && !FS.CurFeaturesList.empty())
      FS = getAndFeatures(FS.CurFeaturesList);
    return FS.HasFeatures;
  }

  TargetFeatures(const llvm::StringMap<bool> &CallerFeatureMap)
      : CallerFeatureMap(CallerFeatureMap) {}
};

} // namespace Builtin
} // namespace neverc

const char *HeaderDesc::getName() const {
  switch (ID) {
#define HEADER(ID, NAME)                                                       \
  case ID:                                                                     \
    return NAME;
#include "neverc/Foundation/Builtin/BuiltinHeaders.def"
#undef HEADER
  };
  return "";
}

namespace {
constexpr Builtin::Info BuiltinInfo[] = {
    {"not a builtin function", nullptr, nullptr, nullptr, HeaderDesc::NO_HEADER,
     ALL_LANGUAGES},
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define LANGBUILTIN(ID, TYPE, ATTRS, LANGS)                                    \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, LANGS},
#define LIBBUILTIN(ID, TYPE, ATTRS, HEADER, LANGS)                             \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::HEADER, LANGS},
#include "neverc/Foundation/Builtin/Builtins.def"
};
} // namespace

const Builtin::Info &Builtin::Context::getRecord(unsigned ID) const {
  if (ID < Builtin::FirstTSBuiltin)
    return BuiltinInfo[ID];
  assert(((ID - Builtin::FirstTSBuiltin) < TSRecords.size()) &&
         "Invalid builtin ID!");
  return TSRecords[ID - Builtin::FirstTSBuiltin];
}

void Builtin::Context::InitializeTarget(const TargetInfo &Target) {
  assert(TSRecords.empty() && "Already initialized target?");
  TSRecords = Target.getTargetBuiltins();
}

bool Builtin::Context::isBuiltinFunc(llvm::StringRef FuncName) {
  bool IsUnderscored = FuncName.starts_with("__");
  for (unsigned i = Builtin::NotBuiltin + 1; i != Builtin::FirstTSBuiltin;
       ++i) {
    llvm::StringRef Name = BuiltinInfo[i].Name;
    bool EntryIsUnderscored = Name.starts_with("__");
    if (IsUnderscored != EntryIsUnderscored)
      continue;
    if (FuncName.equals(Name))
      return strchr(BuiltinInfo[i].Attributes, 'f') != nullptr;
  }
  return false;
}

namespace {
bool isBuiltinActive(const Builtin::Info &BI, const LangOptions &LangOpts) {
  if (LangOpts.NoBuiltin && strchr(BI.Attributes, 'f') != nullptr)
    return false;
  if (LangOpts.NoMathBuiltin && BI.Header.ID == HeaderDesc::MATH_H)
    return false;
  if (!LangOpts.GNUMode && (BI.Langs & GNU_LANG))
    return false;
  if (!LangOpts.MicrosoftExt && (BI.Langs & MS_LANG))
    return false;
  return true;
}
} // namespace

void Builtin::Context::initializeBuiltins(IdentifierTable &Table,
                                          const LangOptions &LangOpts) {
  for (unsigned i = Builtin::NotBuiltin + 1; i != Builtin::FirstTSBuiltin; ++i)
    if (isBuiltinActive(BuiltinInfo[i], LangOpts))
      Table.get(BuiltinInfo[i].Name).setBuiltinID(i);

  for (unsigned i = 0, e = TSRecords.size(); i != e; ++i)
    if (isBuiltinActive(TSRecords[i], LangOpts))
      Table.get(TSRecords[i].Name).setBuiltinID(i + Builtin::FirstTSBuiltin);

  for (llvm::StringRef Name : LangOpts.NoBuiltinFuncs) {
    auto NameIt = Table.find(Name);
    if (NameIt != Table.end()) {
      unsigned ID = NameIt->second->getBuiltinID();
      if (ID != Builtin::NotBuiltin && isPredefinedLibFunction(ID))
        NameIt->second->clearBuiltinID();
    }
  }
}

unsigned Builtin::Context::getRequiredVectorWidth(unsigned ID) const {
  const char *WidthPos = ::strchr(getRecord(ID).Attributes, 'V');
  if (!WidthPos)
    return 0;

  ++WidthPos;
  assert(*WidthPos == ':' &&
         "Vector width specifier must be followed by a ':'");
  ++WidthPos;

  char *EndPos;
  unsigned Width = ::strtol(WidthPos, &EndPos, 10);
  assert(*EndPos == ':' && "Vector width specific must end with a ':'");
  return Width;
}

bool Builtin::Context::isLike(unsigned ID, unsigned &FormatIdx,
                              bool &HasVAListArg, const char *Fmt) const {
  assert(Fmt && "Not passed a format string");
  assert(::strlen(Fmt) == 2 && "Format string needs to be two characters long");
  assert(::toupper(Fmt[0]) == Fmt[1] &&
         "Format string is not in the form \"xX\"");

  const char *Like = ::strpbrk(getRecord(ID).Attributes, Fmt);
  if (!Like)
    return false;

  HasVAListArg = (*Like == Fmt[1]);

  ++Like;
  assert(*Like == ':' && "Format specifier must be followed by a ':'");
  ++Like;

  assert(::strchr(Like, ':') && "Format specifier must end with a ':'");
  FormatIdx = ::strtol(Like, nullptr, 10);
  return true;
}

bool Builtin::Context::isPrintfLike(unsigned ID, unsigned &FormatIdx,
                                    bool &HasVAListArg) {
  return isLike(ID, FormatIdx, HasVAListArg, "pP");
}

bool Builtin::Context::isScanfLike(unsigned ID, unsigned &FormatIdx,
                                   bool &HasVAListArg) {
  return isLike(ID, FormatIdx, HasVAListArg, "sS");
}

bool Builtin::Context::performsCallback(
    unsigned ID, llvm::SmallVectorImpl<int> &Encoding) const {
  const char *CalleePos = ::strchr(getRecord(ID).Attributes, 'C');
  if (!CalleePos)
    return false;

  ++CalleePos;
  assert(*CalleePos == '<' &&
         "Callback callee specifier must be followed by a '<'");
  ++CalleePos;

  char *EndPos;
  int CalleeIdx = ::strtol(CalleePos, &EndPos, 10);
  assert(CalleeIdx >= 0 && "Callee index is supposed to be positive!");
  Encoding.push_back(CalleeIdx);

  while (*EndPos == ',') {
    const char *PayloadPos = EndPos + 1;

    int PayloadIdx = ::strtol(PayloadPos, &EndPos, 10);
    Encoding.push_back(PayloadIdx);
  }

  assert(*EndPos == '>' && "Callback callee specifier must end with a '>'");
  return true;
}

bool Builtin::Context::canBeRedeclared(unsigned ID) const {
  return ID == Builtin::NotBuiltin || ID == Builtin::BI__va_start ||
         ID == Builtin::BI__builtin_assume_aligned ||
         (!hasReferenceArgsOrResult(ID) && !hasCustomTypechecking(ID));
}

bool Builtin::evaluateRequiredTargetFeatures(
    llvm::StringRef RequiredFeatures,
    const llvm::StringMap<bool> &TargetFetureMap) {
  if (RequiredFeatures.empty())
    return true;
  assert(!RequiredFeatures.contains(' ') && "Space in feature list");

  TargetFeatures TF(TargetFetureMap);
  return TF.hasRequiredFeatures(RequiredFeatures);
}
namespace {
const char *const TokNames[] = {
#define TOK(X) #X,
#define KEYWORD(X, Y) #X,
#include "neverc/Foundation/Core/TokenKinds.def"
    nullptr};
} // namespace

const char *tok::getTokenName(TokenKind Kind) {
  if (Kind < tok::NUM_TOKENS)
    return TokNames[Kind];
  return "<unknown>";
}

const char *tok::getPunctuatorSpelling(TokenKind Kind) {
  switch (Kind) {
#define PUNCTUATOR(X, Y)                                                       \
  case X:                                                                      \
    return Y;
#include "neverc/Foundation/Core/TokenKinds.def"
  default:
    break;
  }
  return nullptr;
}

const char *tok::getKeywordSpelling(TokenKind Kind) {
  switch (Kind) {
#define KEYWORD(X, Y)                                                          \
  case kw_##X:                                                                 \
    return #X;
#include "neverc/Foundation/Core/TokenKinds.def"
  default:
    break;
  }
  return nullptr;
}

const char *tok::getPPKeywordSpelling(tok::PPKeywordKind Kind) {
  switch (Kind) {
#define PPKEYWORD(x)                                                           \
  case tok::pp_##x:                                                            \
    return #x;
#include "neverc/Foundation/Core/TokenKinds.def"
  default:
    break;
  }
  return nullptr;
}

bool tok::isAnnotation(TokenKind Kind) {
  switch (Kind) {
#define ANNOTATION(X)                                                          \
  case annot_##X:                                                              \
    return true;
#include "neverc/Foundation/Core/TokenKinds.def"
  default:
    break;
  }
  return false;
}

bool tok::isPragmaAnnotation(TokenKind Kind) {
  switch (Kind) {
#define PRAGMA_ANNOTATION(X)                                                   \
  case annot_##X:                                                              \
    return true;
#include "neverc/Foundation/Core/TokenKinds.def"
  default:
    break;
  }
  return false;
}

namespace {
constexpr const char *UnaryExprOrTypeTraitSpellings[] = {
#define UNARY_EXPR_OR_TYPE_TRAIT(Spelling, Name, Key) #Spelling,
#include "neverc/Foundation/Core/TokenKinds.def"
};
} // namespace

const char *neverc::getTraitSpelling(UnaryExprOrTypeTrait T) {
  assert(T <= UETT_Last && "invalid enum value!");
  return UnaryExprOrTypeTraitSpellings[T];
}

namespace neverc {
namespace charinfo {
const uint16_t InfoTable[256] = {
    // 0 NUL         1 SOH         2 STX         3 ETX
    // 4 EOT         5 ENQ         6 ACK         7 BEL
    0, 0, 0, 0, 0, 0, 0, 0,
    // 8 BS          9 HT         10 NL         11 VT
    // 12 NP         13 CR         14 SO         15 SI
    0, CHAR_HORZ_WS, CHAR_VERT_WS, CHAR_HORZ_WS, CHAR_HORZ_WS, CHAR_VERT_WS, 0,
    0,
    // 16 DLE        17 DC1        18 DC2        19 DC3
    // 20 DC4        21 NAK        22 SYN        23 ETB
    0, 0, 0, 0, 0, 0, 0, 0,
    // 24 CAN        25 EM         26 SUB        27 ESC
    // 28 FS         29 GS         30 RS         31 US
    0, 0, 0, 0, 0, 0, 0, 0,
    // 32 SP         33  !         34  "         35  #
    // 36  $         37  %         38  &         39  '
    CHAR_SPACE, CHAR_RAWDEL, CHAR_RAWDEL, CHAR_RAWDEL, CHAR_PUNCT, CHAR_RAWDEL,
    CHAR_RAWDEL, CHAR_RAWDEL,
    // 40  (         41  )         42  *         43  +
    // 44  ,         45  -         46  .         47  /
    CHAR_PUNCT, CHAR_PUNCT, CHAR_RAWDEL, CHAR_RAWDEL, CHAR_RAWDEL, CHAR_RAWDEL,
    CHAR_PERIOD, CHAR_RAWDEL,
    // 48  0         49  1         50  2         51  3
    // 52  4         53  5         54  6         55  7
    CHAR_DIGIT, CHAR_DIGIT, CHAR_DIGIT, CHAR_DIGIT, CHAR_DIGIT, CHAR_DIGIT,
    CHAR_DIGIT, CHAR_DIGIT,
    // 56  8         57  9         58  :         59  ;
    // 60  <         61  =         62  >         63  ?
    CHAR_DIGIT, CHAR_DIGIT, CHAR_RAWDEL, CHAR_RAWDEL, CHAR_RAWDEL, CHAR_RAWDEL,
    CHAR_RAWDEL, CHAR_RAWDEL,
    // 64  @         65  A         66  B         67  C
    // 68  D         69  E         70  F         71  G
    CHAR_PUNCT, CHAR_XUPPER, CHAR_XUPPER, CHAR_XUPPER, CHAR_XUPPER, CHAR_XUPPER,
    CHAR_XUPPER, CHAR_UPPER,
    // 72  H         73  I         74  J         75  K
    // 76  L         77  M         78  N         79  O
    CHAR_UPPER, CHAR_UPPER, CHAR_UPPER, CHAR_UPPER, CHAR_UPPER, CHAR_UPPER,
    CHAR_UPPER, CHAR_UPPER,
    // 80  P         81  Q         82  R         83  S
    // 84  T         85  U         86  V         87  W
    CHAR_UPPER, CHAR_UPPER, CHAR_UPPER, CHAR_UPPER, CHAR_UPPER, CHAR_UPPER,
    CHAR_UPPER, CHAR_UPPER,
    // 88  X         89  Y         90  Z         91  [
    // 92  \         93  ]         94  ^         95  _
    CHAR_UPPER, CHAR_UPPER, CHAR_UPPER, CHAR_RAWDEL, CHAR_PUNCT, CHAR_RAWDEL,
    CHAR_RAWDEL, CHAR_UNDER,
    // 96  `         97  a         98  b         99  c
    // 100  d       101  e        102  f        103  g
    CHAR_PUNCT, CHAR_XLOWER, CHAR_XLOWER, CHAR_XLOWER, CHAR_XLOWER, CHAR_XLOWER,
    CHAR_XLOWER, CHAR_LOWER,
    // 104  h       105  i        106  j        107  k
    // 108  l       109  m        110  n        111  o
    CHAR_LOWER, CHAR_LOWER, CHAR_LOWER, CHAR_LOWER, CHAR_LOWER, CHAR_LOWER,
    CHAR_LOWER, CHAR_LOWER,
    // 112  p       113  q        114  r        115  s
    // 116  t       117  u        118  v        119  w
    CHAR_LOWER, CHAR_LOWER, CHAR_LOWER, CHAR_LOWER, CHAR_LOWER, CHAR_LOWER,
    CHAR_LOWER, CHAR_LOWER,
    // 120  x       121  y        122  z        123  {
    // 124  |       125  }        126  ~        127 DEL
    CHAR_LOWER, CHAR_LOWER, CHAR_LOWER, CHAR_RAWDEL, CHAR_RAWDEL, CHAR_RAWDEL,
    CHAR_RAWDEL, 0};
} // namespace charinfo
} // namespace neverc
