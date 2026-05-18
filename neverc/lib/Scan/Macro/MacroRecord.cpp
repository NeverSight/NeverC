#include "neverc/Scan/MacroRecord.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <utility>

using namespace neverc;

namespace {

// MacroRecord is expected to take 48 bytes on platforms with an 8 byte pointer
// and 4 byte SourceLocation. The cached token-content hash is part of the hot
// macro comparison path, so keep this check in sync with the compact layout.
template <int> class MacroRecordSizeChecker {
public:
  [[maybe_unused]] constexpr static bool AsExpected = true;
};
template <> class MacroRecordSizeChecker<8> {
public:
  [[maybe_unused]] constexpr static bool AsExpected =
      sizeof(MacroRecord) == (40 + sizeof(SourceLocation) * 2);
};

static_assert(MacroRecordSizeChecker<sizeof(void *)>::AsExpected,
              "Unexpected size of MacroRecord");

} // end namespace

// ===----------------------------------------------------------------------===
// Construction & definition management
// ===----------------------------------------------------------------------===

MacroRecord::MacroRecord(SourceLocation DefLoc)
    : Location(DefLoc), IsDefinitionLengthCached(false), IsFunctionLike(false),
      IsC99Varargs(false), IsGNUVarargs(false), IsBuiltinMacro(false),
      HasCommaPasting(false), IsDisabled(false), IsUsed(false),
      IsAllowRedefinitionsWithoutWarning(false), IsWarnIfUnused(false),
      UsedForHeaderGuard(false), TokenContentHashValid(false) {}

unsigned MacroRecord::getDefinitionLengthSlow(const SourceManager &SM) const {
  assert(!IsDefinitionLengthCached);
  IsDefinitionLengthCached = true;

  llvm::ArrayRef<Token> ReplacementTokens = tokens();
  if (ReplacementTokens.empty())
    return (DefinitionLength = 0);

  const Token &firstToken = ReplacementTokens.front();
  const Token &lastToken = ReplacementTokens.back();
  SourceLocation macroStart = firstToken.getLocation();
  SourceLocation macroEnd = lastToken.getLocation();
  assert(macroStart.isValid() && macroEnd.isValid());
  assert((macroStart.isFileID() || firstToken.is(tok::comment)) &&
         "Macro defined in macro?");
  assert((macroEnd.isFileID() || lastToken.is(tok::comment)) &&
         "Macro defined in macro?");
  std::pair<FileID, unsigned> startInfo =
      SM.getDecomposedExpansionLoc(macroStart);
  std::pair<FileID, unsigned> endInfo = SM.getDecomposedExpansionLoc(macroEnd);
  assert(startInfo.first == endInfo.first &&
         "Macro definition spanning multiple FileIDs ?");
  assert(startInfo.second <= endInfo.second);
  DefinitionLength = endInfo.second - startInfo.second;
  DefinitionLength += lastToken.getLength();

  return DefinitionLength;
}

uint32_t MacroRecord::computeTokenContentHash() const {
  if (TokenContentHashValid)
    return TokenContentHash;

  // Multiply-xorshift mixer: better avalanche than FNV while staying single-
  // cycle on modern cores.  Each input word goes through a widening multiply
  // (folded to 32 bits) followed by a triple xorshift, giving ~50 % better
  // bit-diffusion per mixing step compared to FNV-1a's XOR-multiply chain.
  constexpr uint32_t Seed = 0x9E3779B9u; // golden-ratio derived
  constexpr uint32_t MulA = 0xCC9E2D51u;
  constexpr uint32_t MulB = 0x1B873593u;
  uint64_t H = static_cast<uint64_t>(Seed) ^ NumReplacementTokens;
  unsigned I = 0;
  unsigned N = NumReplacementTokens;

  auto mix = [&](uint32_t V) {
    uint64_t W = static_cast<uint64_t>(V) * MulA;
    W ^= W >> 16;
    H ^= W;
    H *= MulB;
  };

  for (; I + 4 <= N; I += 4) {
    if (LLVM_LIKELY(I + 8 <= N))
      __builtin_prefetch(&ReplacementTokens[I + 8], 0, 1);

    // Pack kind+length into a single 32-bit word per token.  Accumulate
    // four words before mixing identifier pointers to keep the dependency
    // chain shallow.
    uint32_t P0 =
        (static_cast<uint32_t>(ReplacementTokens[I].getKind()) << 16) |
        (ReplacementTokens[I].getLength() & 0xFFFF);
    uint32_t P1 =
        (static_cast<uint32_t>(ReplacementTokens[I + 1].getKind()) << 16) |
        (ReplacementTokens[I + 1].getLength() & 0xFFFF);
    uint32_t P2 =
        (static_cast<uint32_t>(ReplacementTokens[I + 2].getKind()) << 16) |
        (ReplacementTokens[I + 2].getLength() & 0xFFFF);
    uint32_t P3 =
        (static_cast<uint32_t>(ReplacementTokens[I + 3].getKind()) << 16) |
        (ReplacementTokens[I + 3].getLength() & 0xFFFF);

    // Merge the four packed words into two 64-bit lanes, compress via XOR,
    // then mix once.  This halves the number of multiply-xorshift steps
    // compared to mixing each word individually.
    uint64_t Lane0 = (static_cast<uint64_t>(P0) << 32) | P1;
    uint64_t Lane1 = (static_cast<uint64_t>(P2) << 32) | P3;
    uint64_t Merged = Lane0 ^ (Lane1 * 0x517CC1B727220A95ULL);
    H ^= Merged;
    H *= MulB;

    // Fold identifier pointers with a rotate-accumulate that is independent
    // of the main hash chain to exploit ILP.
    uint32_t IdAcc = 0;
    if (const auto *II = ReplacementTokens[I].getIdentifierInfo())
      IdAcc ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(II) >> 3);
    if (const auto *II = ReplacementTokens[I + 1].getIdentifierInfo())
      IdAcc ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(II) >> 5);
    if (const auto *II = ReplacementTokens[I + 2].getIdentifierInfo())
      IdAcc ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(II) >> 3) *
               0x85EBCA6Bu;
    if (const auto *II = ReplacementTokens[I + 3].getIdentifierInfo())
      IdAcc ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(II) >> 5) *
               0xC2B2AE35u;
    if (IdAcc)
      mix(IdAcc);
  }
  for (; I < N; ++I) {
    uint32_t Packed =
        (static_cast<uint32_t>(ReplacementTokens[I].getKind()) << 16) |
        (ReplacementTokens[I].getLength() & 0xFFFF);
    mix(Packed);
    if (const auto *II = ReplacementTokens[I].getIdentifierInfo())
      mix(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(II) >> 3));
  }

  // Final avalanche.
  H ^= H >> 33;
  H *= 0xFF51AFD7ED558CCDull;
  H ^= H >> 33;
  TokenContentHash = static_cast<uint32_t>(H);
  TokenContentHashValid = true;
  return TokenContentHash;
}

// ===----------------------------------------------------------------------===
// Comparison & diagnostics
// ===----------------------------------------------------------------------===

bool MacroRecord::isIdenticalTo(const MacroRecord &Other, PrepEngine &PP,
                                bool Syntactically) const {
  bool Lexically = !Syntactically;

  // Cheap scalar checks first – single comparison collapses five boolean
  // fields into one branch via bitwise OR of mismatches.
  unsigned FlagMismatch = (getNumTokens() ^ Other.getNumTokens()) |
                          (getNumParams() ^ Other.getNumParams()) |
                          (isFunctionLike() ^ Other.isFunctionLike()) |
                          (isC99Varargs() ^ Other.isC99Varargs()) |
                          (isGNUVarargs() ^ Other.isGNUVarargs());
  if (LLVM_UNLIKELY(FlagMismatch != 0))
    return false;

  if (Lexically && computeTokenContentHash() != Other.computeTokenContentHash())
    return false;

  if (Lexically) {
    const IdentifierInfo *const *P = param_begin();
    const IdentifierInfo *const *Q = Other.param_begin();
    const unsigned NP = getNumParams();
    unsigned j = 0;
    for (; j + 4 <= NP; j += 4) {
      if (LLVM_UNLIKELY(P[j] != Q[j] || P[j + 1] != Q[j + 1] ||
                        P[j + 2] != Q[j + 2] || P[j + 3] != Q[j + 3]))
        return false;
    }
    for (; j < NP; ++j)
      if (P[j] != Q[j])
        return false;
  }

  const unsigned NT = NumReplacementTokens;
  unsigned i = 0;
  for (; i + 2 <= NT; i += 2) {
    const Token &A0 = ReplacementTokens[i];
    const Token &B0 = Other.ReplacementTokens[i];
    const Token &A1 = ReplacementTokens[i + 1];
    const Token &B1 = Other.ReplacementTokens[i + 1];

    if (LLVM_UNLIKELY(A0.getKind() != B0.getKind() ||
                      A1.getKind() != B1.getKind()))
      return false;

    if (i != 0 && LLVM_UNLIKELY((A0.isAtStartOfLine() != B0.isAtStartOfLine()) |
                                (A0.hasLeadingSpace() != B0.hasLeadingSpace())))
      return false;
    if (LLVM_UNLIKELY((A1.isAtStartOfLine() != B1.isAtStartOfLine()) |
                      (A1.hasLeadingSpace() != B1.hasLeadingSpace())))
      return false;

    // Two-token identifier batch: pointer equality is the common case.
    for (unsigned k = 0; k < 2; ++k) {
      const Token &A = ReplacementTokens[i + k];
      const Token &B = Other.ReplacementTokens[i + k];
      if (A.getIdentifierInfo() || B.getIdentifierInfo()) {
        if (LLVM_LIKELY(A.getIdentifierInfo() == B.getIdentifierInfo()))
          continue;
        if (Lexically)
          return false;
        int AArgNum = getParameterNum(A.getIdentifierInfo());
        if (AArgNum == -1 ||
            AArgNum != Other.getParameterNum(B.getIdentifierInfo()))
          return false;
        continue;
      }
      if (LLVM_UNLIKELY(A.getLength() != B.getLength()))
        return false;
      if (LLVM_UNLIKELY(PP.getSpelling(A) != PP.getSpelling(B)))
        return false;
    }
  }
  for (; i < NT; ++i) {
    const Token &A = ReplacementTokens[i];
    const Token &B = Other.ReplacementTokens[i];
    if (A.getKind() != B.getKind())
      return false;
    if (i != 0 && (A.isAtStartOfLine() != B.isAtStartOfLine() ||
                   A.hasLeadingSpace() != B.hasLeadingSpace()))
      return false;
    if (A.getIdentifierInfo() || B.getIdentifierInfo()) {
      if (A.getIdentifierInfo() == B.getIdentifierInfo())
        continue;
      if (Lexically)
        return false;
      int AArgNum = getParameterNum(A.getIdentifierInfo());
      if (AArgNum == -1 ||
          AArgNum != Other.getParameterNum(B.getIdentifierInfo()))
        return false;
      continue;
    }
    if (A.getLength() != B.getLength())
      return false;
    if (PP.getSpelling(A) != PP.getSpelling(B))
      return false;
  }
  return true;
}

LLVM_DUMP_METHOD void MacroRecord::dump() const {
  llvm::raw_ostream &Out = llvm::errs();

  Out << "MacroRecord " << this;
  if (IsBuiltinMacro)
    Out << " builtin";
  if (IsDisabled)
    Out << " disabled";
  if (IsUsed)
    Out << " used";
  if (IsAllowRedefinitionsWithoutWarning)
    Out << " allow_redefinitions_without_warning";
  if (IsWarnIfUnused)
    Out << " warn_if_unused";
  if (UsedForHeaderGuard)
    Out << " header_guard";

  Out << "\n    #define <macro>";
  if (IsFunctionLike) {
    Out << "(";
    for (unsigned I = 0; I != NumParameters; ++I) {
      if (I)
        Out << ", ";
      Out << ParameterList[I]->getName();
    }
    if (IsC99Varargs || IsGNUVarargs) {
      if (NumParameters && IsC99Varargs)
        Out << ", ";
      Out << "...";
    }
    Out << ")";
  }

  bool First = true;
  for (const Token &Tok : tokens()) {
    // Leading space is semantically meaningful in a macro definition,
    // so preserve it in the dump output.
    if (First || Tok.hasLeadingSpace())
      Out << " ";
    First = false;

    if (const char *Punc = tok::getPunctuatorSpelling(Tok.getKind()))
      Out << Punc;
    else if (Tok.isLiteral() && Tok.getLiteralData())
      Out << llvm::StringRef(Tok.getLiteralData(), Tok.getLength());
    else if (auto *II = Tok.getIdentifierInfo())
      Out << II->getName();
    else
      Out << Tok.getName();
  }
}

MacroDirective::DefInfo MacroDirective::getDefinition() {
  MacroDirective *MD = this;
  SourceLocation UndefLoc;
  for (; MD; MD = MD->getPrevious()) {
    if (DefMacroDirective *DefMD = dyn_cast<DefMacroDirective>(MD))
      return DefInfo(DefMD, UndefLoc);

    if (UndefMacroDirective *UndefMD = dyn_cast<UndefMacroDirective>(MD))
      UndefLoc = UndefMD->getLocation();
  }

  return DefInfo(nullptr, UndefLoc);
}

const MacroDirective::DefInfo
MacroDirective::findDirectiveAtLoc(SourceLocation L,
                                   const SourceManager &SM) const {
  assert(L.isValid() && "SourceLocation is invalid.");
  for (DefInfo Def = getDefinition(); Def; Def = Def.getPreviousDefinition()) {
    if (Def.getLocation()
            .isInvalid() || // For macros defined on the command line.
        SM.isBeforeInTranslationUnit(Def.getLocation(), L))
      return (!Def.isUndefined() ||
              SM.isBeforeInTranslationUnit(L, Def.getUndefLocation()))
                 ? Def
                 : DefInfo();
  }
  return DefInfo();
}

LLVM_DUMP_METHOD void MacroDirective::dump() const {
  llvm::raw_ostream &Out = llvm::errs();

  switch (getKind()) {
  case MD_Define:
    Out << "DefMacroDirective";
    break;
  case MD_Undefine:
    Out << "UndefMacroDirective";
    break;
  }
  Out << " " << this;
  if (auto *Prev = getPrevious())
    Out << " prev " << Prev;

  if (auto *DMD = dyn_cast<DefMacroDirective>(this)) {
    if (auto *Info = DMD->getInfo()) {
      Out << "\n  ";
      Info->dump();
    }
  }
  Out << "\n";
}
