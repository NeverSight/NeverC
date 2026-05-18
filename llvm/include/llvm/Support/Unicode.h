//===- llvm/Support/Unicode.h - Unicode character properties  -*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines functions that allow querying certain properties of Unicode
// characters.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_UNICODE_H
#define LLVM_SUPPORT_UNICODE_H

#include "csupport/lunicode_lname_lto_lcodepoint.h"
#include "llvm/ADT/SmallString.h"
#include <optional>
#include <string>

namespace llvm {
class StringRef;

namespace sys {
namespace unicode {

enum ColumnWidthErrors {
  ErrorInvalidUTF8 = -2,
  ErrorNonPrintableCharacter = -1
};

/// Determines if a character is likely to be displayed correctly on the
/// terminal. Exact implementation would have to depend on the specific
/// terminal, so we define the semantic that should be suitable for generic case
/// of a terminal capable to output Unicode characters.
///
extern "C" {
bool csupport_unicode_is_printable(int ucs);
bool csupport_unicode_is_formatting(int ucs);
int csupport_unicode_column_width_utf8_raw(const char *data, size_t len);
}

/// Printable codepoints are those in the categories L, M, N, P, S and Zs
/// \return true if the character is considered printable.
inline bool isPrintable(int UCS) { return csupport_unicode_is_printable(UCS); }

// Formatting codepoints are codepoints in the Cf category.
inline bool isFormatting(int UCS) {
  return csupport_unicode_is_formatting(UCS);
}

/// Gets the number of positions the UTF8-encoded \p Text is likely to occupy
/// when output on a terminal ("character width"). This depends on the
/// implementation of the terminal, and there's no standard definition of
/// character width.
///
/// The implementation defines it in a way that is expected to be compatible
/// with a generic Unicode-capable terminal.
///
/// \return Character width:
///   * ErrorNonPrintableCharacter (-1) if \p Text contains non-printable
///     characters (as identified by isPrintable);
///   * 0 for each non-spacing and enclosing combining mark;
///   * 2 for each CJK character excluding halfwidth forms;
///   * 1 for each of the remaining characters.
inline int columnWidthUTF8(StringRef Text) {
  return csupport_unicode_column_width_utf8_raw(Text.data(), Text.size());
}

/// Fold input unicode character according the Simple unicode case folding
/// rules.
extern "C" int csupport_fold_char_simple(int C);
inline int foldCharSimple(int C) { return csupport_fold_char_simple(C); }

/// Maps the name or the alias of a Unicode character to its associated
/// codepoints.
/// The names and aliases are derived from UnicodeData.txt and NameAliases.txt
/// For compatibility with the semantics of named character escape sequences in
/// C++, this mapping does an exact match sensitive to casing and spacing.
/// \return The codepoint of the corresponding character, if any.
/// Returns UINT32_MAX if no match found.
char32_t nameToCodepointStrict(StringRef Name);

struct LooseMatchingResult {
  char32_t CodePoint;
  SmallString<64> Name;
};

/// Returns result with CodePoint == UINT32_MAX if no match found.
LooseMatchingResult nameToCodepointLooseMatching(StringRef Name);

struct MatchForCodepointName {
  SmallString<64> Name;
  uint32_t Distance = 0;
  char32_t Value = 0;
};

SmallVector<MatchForCodepointName>
nearestMatchesForCodepointName(StringRef Pattern, std::size_t MaxMatchesCount);

extern "C" {
extern const char *csupport_unicode_name_to_cp_dict;
extern const uint8_t *csupport_unicode_name_to_cp_index;
extern const size_t csupport_unicode_name_to_cp_index_size;
extern const size_t csupport_unicode_name_to_cp_largest_name_size;
}

inline const char *UnicodeNameToCodepointDict =
    csupport_unicode_name_to_cp_dict;
inline const uint8_t *UnicodeNameToCodepointIndex =
    csupport_unicode_name_to_cp_index;
inline const size_t UnicodeNameToCodepointIndexSize =
    csupport_unicode_name_to_cp_index_size;
inline const size_t UnicodeNameToCodepointLargestNameSize =
    csupport_unicode_name_to_cp_largest_name_size;

} // namespace unicode
} // namespace sys
} // namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

namespace llvm {
namespace sys {
namespace unicode {

extern const char *UnicodeNameToCodepointDict;
extern const uint8_t *UnicodeNameToCodepointIndex;
extern const size_t UnicodeNameToCodepointIndexSize;
extern const size_t UnicodeNameToCodepointLargestNameSize;

using BufferType = SmallString<64>;

struct Node {
  bool IsRoot = false;
  char32_t Value = 0xFFFFFFFF;
  uint32_t ChildrenOffset = 0;
  bool HasSibling = false;
  uint32_t Size = 0;
  StringRef Name;
  const Node *Parent = 0;

  bool isValid() const { return !Name.empty() || Value == 0xFFFFFFFF; }
  bool hasChildren() const { return ChildrenOffset != 0 || IsRoot; }

  SmallString<64> fullName() const {
    SmallString<64> S;
    const Node *N = this;
    while (N) {
      for (auto it = N->Name.end(); it != N->Name.begin();) {
        --it;
        S.push_back(*it);
      }
      N = N->Parent;
    }
    for (size_t lo = 0, hi = S.size() - 1; lo < hi; ++lo, --hi) {
      char t = S[lo];
      S[lo] = S[hi];
      S[hi] = t;
    }
    return S;
  }
};

inline static Node createRoot() {
  Node N;
  N.IsRoot = true;
  N.ChildrenOffset = 1;
  N.Size = 1;
  return N;
}

inline static Node readNode(uint32_t Offset, const Node *Parent = 0) {
  if (Offset == 0)
    return createRoot();
  csupport_unicode_node_t cn = csupport_unicode_read_node(
      UnicodeNameToCodepointIndex, UnicodeNameToCodepointIndexSize,
      UnicodeNameToCodepointDict, Offset);
  Node N;
  N.Parent = Parent;
  N.Name = StringRef(cn.name_data, cn.name_len);
  N.Value = cn.value;
  N.ChildrenOffset = cn.children_offset;
  N.HasSibling = cn.has_sibling != 0;
  N.Size = cn.size;
  return N;
}

struct CompareResult {
  Node N;
  bool Matches;
  uint32_t Value;
};

inline static CompareResult compareNode(uint32_t Offset, StringRef Name,
                                        bool Strict, BufferType &Buffer) {
  char rev_buf[512];
  size_t rev_buf_len = 0;
  uint32_t val = csupport_unicode_compare_node(
      UnicodeNameToCodepointIndex, UnicodeNameToCodepointIndexSize,
      UnicodeNameToCodepointDict, Offset, Name.data(), Name.size(), Strict, 0,
      rev_buf, sizeof(rev_buf), &rev_buf_len);
  CompareResult CR;
  CR.N = readNode(Offset);
  CR.Matches = (val != UINT32_MAX);
  CR.Value = val;
  if (CR.Matches && rev_buf_len > 0)
    Buffer.append(rev_buf, rev_buf + rev_buf_len);
  return CR;
}

/* Hangul data tables and constants now in UnicodeNameToCodepoint.c */

inline static char32_t nameToHangulCodePoint(StringRef Name, bool Strict,
                                             BufferType &Buffer) {
  Buffer.clear();
  char buf[256];
  size_t buf_len = 0;
  uint32_t cp = csupport_unicode_name_to_hangul(
      Name.data(), Name.size(), Strict, buf, sizeof(buf), &buf_len);
  if (cp != UINT32_MAX && !Strict && buf_len > 0)
    Buffer.append(buf, buf + buf_len);
  return cp;
}

/* GeneratedNamesDataTable now in UnicodeNameToCodepoint.c */

inline static char32_t nameToGeneratedCodePoint(StringRef Name, bool Strict,
                                                BufferType &Buffer) {
  Buffer.clear();
  char buf[256];
  size_t buf_len = 0;
  uint32_t cp = csupport_unicode_name_to_generated(
      Name.data(), Name.size(), Strict, buf, sizeof(buf), &buf_len);
  if (cp != UINT32_MAX && !Strict && buf_len > 0)
    Buffer.append(buf, buf + buf_len);
  return cp;
}

inline static char32_t nameToCodepoint(StringRef Name, bool Strict,
                                       BufferType &Buffer) {
  if (Name.empty())
    return UINT32_MAX;

  char32_t Res = nameToHangulCodePoint(Name, Strict, Buffer);
  if (Res == UINT32_MAX)
    Res = nameToGeneratedCodePoint(Name, Strict, Buffer);
  if (Res != UINT32_MAX)
    return Res;

  Buffer.clear();
  CompareResult CR = compareNode(0, Name, Strict, Buffer);
  if (CR.Matches) {
    for (size_t lo = 0, hi = Buffer.size() - 1; lo < hi; ++lo, --hi) {
      char t = Buffer[lo];
      Buffer[lo] = Buffer[hi];
      Buffer[hi] = t;
    }
    uint32_t FoundValue = CR.Value;
    if (!Strict && FoundValue == 0x116c && Name.contains_insensitive("O-E")) {
      Buffer = "HANGUL JUNGSEONG O-E";
      FoundValue = 0x1180;
    }
    return FoundValue;
  }
  return UINT32_MAX;
}

inline char32_t nameToCodepointStrict(StringRef Name) {
  BufferType Buffer;
  return nameToCodepoint(Name, true, Buffer);
}

inline LooseMatchingResult nameToCodepointLooseMatching(StringRef Name) {
  BufferType Buffer;
  char32_t cp = nameToCodepoint(Name, false, Buffer);
  return LooseMatchingResult{cp, Buffer};
}

// Find the unicode character whose editing distance to Pattern
// is shortest, using the Wagner–Fischer algorithm.
inline llvm::SmallVector<MatchForCodepointName>
nearestMatchesForCodepointName(StringRef Pattern, size_t MaxMatchesCount) {
  // We maintain a fixed size vector of matches,
  // sorted by distance
  // The worst match (with the biggest distance) are discarded when new elements
  // are added.
  size_t LargestEditDistance = 0;
  llvm::SmallVector<MatchForCodepointName> Matches;
  Matches.reserve(MaxMatchesCount + 1);

  auto Insert = [&](const Node &Node, uint32_t Distance,
                    char32_t Value) -> bool {
    if (Distance > LargestEditDistance) {
      if (Matches.size() == MaxMatchesCount)
        return false;
      LargestEditDistance = Distance;
    }
    // To avoid allocations, the creation of the name is delayed
    // as much as possible.
    SmallString<64> Name;
    auto GetName = [&]() -> SmallString<64> {
      if (Name.empty())
        Name = Node.fullName();
      return Name;
    };

    auto It =
        llvm::lower_bound(Matches, Distance,
                          [&](const MatchForCodepointName &a, size_t Distance) {
                            if (Distance == a.Distance)
                              return a.Name < GetName();
                            return a.Distance < Distance;
                          });
    if (It == Matches.end() && Matches.size() == MaxMatchesCount)
      return false;

    MatchForCodepointName M{GetName(), Distance, Value};
    Matches.insert(It, M);
    if (Matches.size() > MaxMatchesCount)
      Matches.pop_back();
    return true;
  };

  // We ignore case, space, hyphens, etc,
  // in both the search pattern and the prospective names.
  auto Normalize = [](StringRef Name) {
    SmallString<128> Out;
    Out.reserve(Name.size());
    for (char C : Name) {
      if (isAlnum(C))
        Out.push_back(toUpper(C));
    }
    return SmallString<128>(Out);
  };
  auto NormalizedName = Normalize(Pattern);

  // Allocate a matrix big enough for longest names.
  const size_t Columns =
      BRIDGE_MIN(NormalizedName.size(), UnicodeNameToCodepointLargestNameSize) +
      1;

  LLVM_ATTRIBUTE_UNUSED static size_t Rows =
      UnicodeNameToCodepointLargestNameSize + 1;

  SmallVector<char, 4096> Distances(
      Columns * (UnicodeNameToCodepointLargestNameSize + 1), 0);

  auto Get = [&Distances, Columns](size_t Column, size_t Row) -> char & {
    assert(Column < Columns);
    assert(Row < Rows);
    return Distances[Row * Columns + Column];
  };

  for (size_t I = 0; I < Columns; I++)
    Get(I, 0) = I;

  // Visit the childrens,
  // Filling (and overriding) the matrix for the name fragment of each node
  // iteratively. CompleteName is used to collect the actual name of potential
  // match, respecting case and spacing.
  auto VisitNode = [&](const Node &N, size_t Row, auto &VisitNode) -> void {
    size_t J = 0;
    for (; J < N.Name.size(); J++) {
      if (!isAlnum(N.Name[J]))
        continue;

      Get(0, Row) = Row;

      for (size_t I = 1; I < Columns; I++) {
        const int Delete = Get(I - 1, Row) + 1;
        const int Insert = Get(I, Row - 1) + 1;

        const int Replace =
            Get(I - 1, Row - 1) + (NormalizedName[I - 1] != N.Name[J] ? 1 : 0);

        Get(I, Row) = BRIDGE_MIN(Insert, BRIDGE_MIN(Delete, Replace));
      }

      Row++;
    }

    unsigned Cost = Get(Columns - 1, Row - 1);
    if (N.Value != 0xFFFFFFFF) {
      Insert(N, Cost, N.Value);
    }

    if (N.hasChildren()) {
      auto ChildOffset = N.ChildrenOffset;
      for (;;) {
        Node C = readNode(ChildOffset, &N);
        ChildOffset += C.Size;
        if (!C.isValid())
          break;
        VisitNode(C, Row, VisitNode);
        if (!C.HasSibling)
          break;
      }
    }
  };

  Node Root = createRoot();
  VisitNode(Root, 1, VisitNode);
  return Matches;
}

} // namespace unicode

} // namespace sys
} // namespace llvm

#endif
