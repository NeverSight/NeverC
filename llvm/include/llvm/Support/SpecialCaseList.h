//===-- SpecialCaseList.h - special case list for sanitizers ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// This file implements a Special Case List for code sanitizers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SPECIALCASELIST_H
#define LLVM_SUPPORT_SPECIALCASELIST_H

#include "csupport/cpp_compat_stl.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class MemoryBuffer;
class StringRef;

namespace vfs {
class FileSystem;
}

/// This is a utility class used to parse user-provided text files with
/// "special case lists" for code sanitizers. Such files are used to
/// define an "ABI list" for DataFlowSanitizer and allow/exclusion lists for
/// sanitizers like AddressSanitizer or UndefinedBehaviorSanitizer.
///
/// Empty lines and lines starting with "#" are ignored. Sections are defined
/// using a '[section_name]' header and can be used to specify sanitizers the
/// entries below it apply to. Section names are globs, and
/// entries without a section header match all sections (e.g. an '[*]' header
/// is assumed.)
/// The remaining lines should have the form:
///   prefix:glob_pattern[=category]
/// If category is not specified, it is assumed to be empty string.
/// Definitions of "prefix" and "category" are sanitizer-specific. For example,
/// sanitizer exclusion support prefixes "src", "mainfile", "fun" and "global".
/// "glob_pattern" defines source files, main files, functions or globals which
/// shouldn't be instrumented.
/// Examples of categories:
///   "functional": used in DFSan to list functions with pure functional
///                 semantics.
///   "init": used in ASan exclusion list to disable initialization-order bugs
///           detection for certain globals or source files.
/// Full special case list file example:
/// ---
/// [address]
/// # Excluded items:
/// fun:*_ZN4base6subtle*
/// global:*global_with_bad_access_or_initialization*
/// global:*global_with_initialization_issues*=init
/// type:*Namespace::ClassName*=init
/// src:file_with_tricky_code.cc
/// src:ignore-global-initializers-issues.cc=init
/// mainfile:main_file.cc
///
/// [dataflow]
/// # Functions with pure functional semantics:
/// fun:cos=functional
/// fun:sin=functional
/// ---
class SpecialCaseList {
public:
  /// Parses the special case list entries from files. On failure, returns
  /// 0 and writes an error message to string.
  static std::unique_ptr<SpecialCaseList> create(ArrayRef<StringRef> Paths,
                                                 llvm::vfs::FileSystem &FS,
                                                 SmallVectorImpl<char> &Error);
  static std::unique_ptr<SpecialCaseList> create(const MemoryBuffer *MB,
                                                 SmallVectorImpl<char> &Error);
  /// Parses the special case list entries from files. On failure, reports a
  /// fatal error.
  static std::unique_ptr<SpecialCaseList>
  createOrDie(ArrayRef<StringRef> Paths, llvm::vfs::FileSystem &FS);

  ~SpecialCaseList();

  /// Returns true, if special case list contains a line
  /// \code
  ///   @Prefix:<E>=@Category
  /// \endcode
  /// where @Query satisfies the glob <E> in a given @Section.
  bool inSection(StringRef Section, StringRef Prefix, StringRef Query,
                 StringRef Category = StringRef()) const;

  /// Returns the line number corresponding to the special case list entry if
  /// the special case list contains a line
  /// \code
  ///   @Prefix:<E>=@Category
  /// \endcode
  /// where @Query satisfies the glob <E> in a given @Section.
  /// Returns zero if there is no exclusion entry corresponding to this
  /// expression.
  unsigned inSectionBlame(StringRef Section, StringRef Prefix, StringRef Query,
                          StringRef Category = StringRef()) const;

protected:
  // Implementations of the create*() functions that can also be used by derived
  // classes.
  bool createInternal(ArrayRef<StringRef> Paths, vfs::FileSystem &VFS,
                      SmallVectorImpl<char> &Error);
  bool createInternal(const MemoryBuffer *MB, SmallVectorImpl<char> &Error);

  SpecialCaseList() = default;
  SpecialCaseList(SpecialCaseList const &) = delete;
  SpecialCaseList &operator=(SpecialCaseList const &) = delete;

  /// Represents a set of globs and their line numbers
  class Matcher {
  public:
    Error insert(StringRef Pattern, unsigned LineNumber, bool UseRegex);
    // Returns the line number in the source file that this query matches to.
    // Returns zero if no match is found.
    unsigned match(StringRef Query) const;

  private:
    StringMap<std::pair<GlobPattern, unsigned>> Globs;
    std::vector<std::pair<std::unique_ptr<Regex>, unsigned>> RegExes;
  };

  using SectionEntries = StringMap<StringMap<Matcher>>;

  struct Section {
    Section(std::unique_ptr<Matcher> M) : SectionMatcher(std::move(M)) {};
    Section() : Section(std::make_unique<Matcher>()) {}

    std::unique_ptr<Matcher> SectionMatcher;
    SectionEntries Entries;
  };

  StringMap<Section> Sections;

  Expected<Section *> addSection(StringRef SectionStr, unsigned LineNo,
                                 bool UseGlobs = true);

  /// Parses just-constructed SpecialCaseList entries from a memory buffer.
  bool parse(const MemoryBuffer *MB, SmallVectorImpl<char> &Error);

  // Helper method for derived classes to search by Prefix, Query, and Category
  // once they have already resolved a section entry.
  unsigned inSectionBlame(const SectionEntries &Entries, StringRef Prefix,
                          StringRef Query, StringRef Category) const;
};

} // namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

namespace llvm {

inline Error SpecialCaseList::Matcher::insert(StringRef Pattern,
                                              unsigned LineNumber,
                                              bool UseGlobs) {
  if (Pattern.empty())
    return createStringError(errc::invalid_argument,
                             Twine("Supplied ") +
                                 (UseGlobs ? "glob" : "regex") + " was blank");

  if (!UseGlobs) {
    SmallString<256> Regexp;
    {
      auto PatStr = Pattern.str();
      size_t prev = 0;
      for (size_t pos = 0; pos < PatStr.size(); ++pos) {
        if (PatStr[pos] == '*') {
          Regexp += StringRef(PatStr.data() + prev, pos - prev);
          Regexp += ".*";
          prev = pos + 1;
        }
      }
      Regexp += StringRef(PatStr.data() + prev, PatStr.size() - prev);
    }
    {
      SmallString<256> Tmp;
      Tmp += "^(";
      Tmp += StringRef(Regexp);
      Tmp += ")$";
      Regexp = Tmp;
    }

    // Check that the regexp is valid.
    Regex CheckRE(Regexp);
    SmallString<256> REError;
    if (!CheckRE.isValid(REError))
      return createStringError(errc::invalid_argument,
                               Twine(StringRef(REError)));

    RegExes.push_back({uptr_t<Regex>(new Regex(CMOVE(CheckRE))), LineNumber});

    return Error::success();
  }

  auto [It, DidEmplace] = Globs.try_emplace(Pattern);
  if (DidEmplace) {
    // We must be sure to use the string in the map rather than the provided
    // reference which could be destroyed before match() is called
    Pattern = It->getKey();
    auto &Pair = It->getValue();
    if (auto Err = GlobPattern::create(Pattern, /*MaxSubPatterns=*/1024)
                       .moveInto(Pair.first))
      return Err;
    Pair.second = LineNumber;
  }
  return Error::success();
}

inline unsigned SpecialCaseList::Matcher::match(StringRef Query) const {
  for (const auto &[Pattern, Pair] : Globs)
    if (Pair.first.match(Query))
      return Pair.second;
  for (const auto &[Regex, LineNumber] : RegExes)
    if (Regex->match(Query))
      return LineNumber;
  return 0;
}

// TODO: Refactor this to return Expected<...>
inline uptr_t<SpecialCaseList>
SpecialCaseList::create(ArrayRef<StringRef> Paths, llvm::vfs::FileSystem &FS,
                        SmallVectorImpl<char> &Error) {
  uptr_t<SpecialCaseList> SCL(new SpecialCaseList());
  if (SCL->createInternal(Paths, FS, Error))
    return SCL;
  return 0;
}

inline uptr_t<SpecialCaseList>
SpecialCaseList::create(const MemoryBuffer *MB, SmallVectorImpl<char> &Error) {
  uptr_t<SpecialCaseList> SCL(new SpecialCaseList());
  if (SCL->createInternal(MB, Error))
    return SCL;
  return 0;
}

inline uptr_t<SpecialCaseList>
SpecialCaseList::createOrDie(ArrayRef<StringRef> Paths,
                             llvm::vfs::FileSystem &FS) {
  SmallString<256> Error;
  if (auto SCL = create(Paths, FS, Error))
    return SCL;
  report_fatal_error(Twine(StringRef(Error)));
}

inline bool SpecialCaseList::createInternal(ArrayRef<StringRef> Paths,
                                            vfs::FileSystem &VFS,
                                            SmallVectorImpl<char> &Error) {
  for (const auto &Path : Paths) {
    ErrorOr<uptr_t<MemoryBuffer>> FileOrErr = VFS.getBufferForFile(Path);
    if (errc_t EC = FileOrErr.getError()) {
      SmallString<256> Msg;
      raw_svector_ostream OS(Msg);
      OS << "can't open file '" << Path << "': " << EC.message();
      Error.assign(Msg.begin(), Msg.end());
      return false;
    }
    SmallString<256> ParseError;
    if (!parse(FileOrErr.get().get(), ParseError)) {
      SmallString<256> Msg;
      raw_svector_ostream OS(Msg);
      OS << "error parsing file '" << Path << "': " << StringRef(ParseError);
      Error.assign(Msg.begin(), Msg.end());
      return false;
    }
  }
  return true;
}

inline bool SpecialCaseList::createInternal(const MemoryBuffer *MB,
                                            SmallVectorImpl<char> &Error) {
  if (!parse(MB, Error))
    return false;
  return true;
}

inline Expected<SpecialCaseList::Section *>
SpecialCaseList::addSection(StringRef SectionStr, unsigned LineNo,
                            bool UseGlobs) {
  auto [It, DidEmplace] = Sections.try_emplace(SectionStr);
  auto &Section = It->getValue();
  if (DidEmplace)
    if (auto Err = Section.SectionMatcher->insert(SectionStr, LineNo, UseGlobs))
      return createStringError(errc::invalid_argument,
                               "malformed section at line " + Twine(LineNo) +
                                   ": '" + SectionStr +
                                   "': " + toString(CMOVE(Err)));
  return &Section;
}

inline bool SpecialCaseList::parse(const MemoryBuffer *MB,
                                   SmallVectorImpl<char> &Error) {
  Section *CurrentSection;
  if (auto Err = addSection("*", 1).moveInto(CurrentSection)) {
    auto ErrStr = toString(CMOVE(Err));
    Error.assign(ErrStr.begin(), ErrStr.end());
    return false;
  }

  // In https://reviews.llvm.org/D154014 we added glob support and planned to
  // remove regex support in patterns. We temporarily support the original
  // behavior using regexes if "#!special-case-list-v1" is the first line of the
  // file. For more details, see
  // https://discourse.llvm.org/t/use-glob-instead-of-regex-for-specialcaselists/71666
  bool UseGlobs = !MB->getBuffer().starts_with("#!special-case-list-v1\n");

  for (line_iterator LineIt(*MB, /*SkipBlanks=*/true, /*CommentMarker=*/'#');
       !LineIt.is_at_eof(); LineIt++) {
    unsigned LineNo = LineIt.line_number();
    StringRef Line = LineIt->trim();
    if (Line.empty())
      continue;

    // Save section names
    if (Line.starts_with("[")) {
      if (!Line.ends_with("]")) {
        SmallString<256> Msg;
        raw_svector_ostream OS(Msg);
        OS << "malformed section header on line " << LineNo << ": " << Line;
        Error.assign(Msg.begin(), Msg.end());
        return false;
      }

      if (auto Err = addSection(Line.drop_front().drop_back(), LineNo, UseGlobs)
                         .moveInto(CurrentSection)) {
        auto ErrStr = toString(CMOVE(Err));
        Error.assign(ErrStr.begin(), ErrStr.end());
        return false;
      }
      continue;
    }

    auto [Prefix, Postfix] = Line.split(":");
    if (Postfix.empty()) {
      SmallString<256> Msg;
      raw_svector_ostream OS(Msg);
      OS << "malformed line " << LineNo << ": '" << Line << "'";
      Error.assign(Msg.begin(), Msg.end());
      return false;
    }

    auto [Pattern, Category] = Postfix.split("=");
    auto &Entry = CurrentSection->Entries[Prefix][Category];
    if (auto Err = Entry.insert(Pattern, LineNo, UseGlobs)) {
      SmallString<256> Msg;
      raw_svector_ostream OS(Msg);
      OS << "malformed " << (UseGlobs ? "glob" : "regex") << " in line "
         << LineNo << ": '" << Pattern << "': " << toString(CMOVE(Err));
      Error.assign(Msg.begin(), Msg.end());
      return false;
    }
  }
  return true;
}

inline SpecialCaseList::~SpecialCaseList() = default;

inline bool SpecialCaseList::inSection(StringRef Section, StringRef Prefix,
                                       StringRef Query,
                                       StringRef Category) const {
  return inSectionBlame(Section, Prefix, Query, Category);
}

inline unsigned SpecialCaseList::inSectionBlame(StringRef Section,
                                                StringRef Prefix,
                                                StringRef Query,
                                                StringRef Category) const {
  for (const auto &It : Sections) {
    const auto &S = It.getValue();
    if (S.SectionMatcher->match(Section)) {
      unsigned Blame = inSectionBlame(S.Entries, Prefix, Query, Category);
      if (Blame)
        return Blame;
    }
  }
  return 0;
}

inline unsigned SpecialCaseList::inSectionBlame(const SectionEntries &Entries,
                                                StringRef Prefix,
                                                StringRef Query,
                                                StringRef Category) const {
  SectionEntries::const_iterator I = Entries.find(Prefix);
  if (I == Entries.end())
    return 0;
  StringMap<Matcher>::const_iterator II = I->second.find(Category);
  if (II == I->second.end())
    return 0;

  return II->getValue().match(Query);
}

} // namespace llvm

#endif // LLVM_SUPPORT_SPECIALCASELIST_H
