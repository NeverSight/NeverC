//===- VersionTuple.h - Version Number Handling -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the llvm::VersionTuple class, which represents a version in
/// the form major[.minor[.subminor]].
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_VERSIONTUPLE_H
#define LLVM_SUPPORT_VERSIONTUPLE_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"

extern "C" {
int csupport_version_format(char *buf, size_t buflen, unsigned major,
                            int has_minor, unsigned minor, int has_subminor,
                            unsigned subminor, int has_build, unsigned build);
int csupport_version_try_parse(const char *str, size_t len, unsigned *major,
                               unsigned *minor, unsigned *micro,
                               unsigned *build, int *has_minor,
                               int *has_subminor, int *has_build);
}

namespace llvm {
template <typename HasherT, llvm::endianness Endianness> class HashBuilder;
class raw_ostream;
class StringRef;

/// Represents a version number in the form major[.minor[.subminor[.build]]].
class VersionTuple {
  unsigned Major : 32;

  unsigned Minor : 31;
  unsigned HasMinor : 1;

  unsigned Subminor : 31;
  unsigned HasSubminor : 1;

  unsigned Build : 31;
  unsigned HasBuild : 1;

public:
  constexpr VersionTuple()
      : Major(0), Minor(0), HasMinor(false), Subminor(0), HasSubminor(false),
        Build(0), HasBuild(false) {}

  explicit constexpr VersionTuple(unsigned Major)
      : Major(Major), Minor(0), HasMinor(false), Subminor(0),
        HasSubminor(false), Build(0), HasBuild(false) {}

  explicit constexpr VersionTuple(unsigned Major, unsigned Minor)
      : Major(Major), Minor(Minor), HasMinor(true), Subminor(0),
        HasSubminor(false), Build(0), HasBuild(false) {}

  explicit constexpr VersionTuple(unsigned Major, unsigned Minor,
                                  unsigned Subminor)
      : Major(Major), Minor(Minor), HasMinor(true), Subminor(Subminor),
        HasSubminor(true), Build(0), HasBuild(false) {}

  explicit constexpr VersionTuple(unsigned Major, unsigned Minor,
                                  unsigned Subminor, unsigned Build)
      : Major(Major), Minor(Minor), HasMinor(true), Subminor(Subminor),
        HasSubminor(true), Build(Build), HasBuild(true) {}

  /// Determine whether this version information is empty
  /// (e.g., all version components are zero).
  bool empty() const {
    return Major == 0 && Minor == 0 && Subminor == 0 && Build == 0;
  }

  /// Retrieve the major version number.
  unsigned getMajor() const { return Major; }

  /// Retrieve the minor version number (0 if not provided).
  unsigned getMinor() const { return Minor; }
  bool hasMinor() const { return HasMinor; }

  /// Retrieve the subminor version number (0 if not provided).
  unsigned getSubminor() const { return Subminor; }
  bool hasSubminor() const { return HasSubminor; }

  /// Retrieve the build version number (0 if not provided).
  unsigned getBuild() const { return Build; }
  bool hasBuild() const { return HasBuild; }

  /// Return a version tuple that contains only the first 3 version components.
  VersionTuple withoutBuild() const {
    if (HasBuild)
      return VersionTuple(Major, Minor, Subminor);
    return *this;
  }

  /// Return a version tuple that contains a different major version but
  /// everything else is the same.
  VersionTuple withMajorReplaced(unsigned NewMajor) const {
    return VersionTuple(NewMajor, Minor, Subminor, Build);
  }

  /// Return a version tuple that contains only components that are non-zero.
  VersionTuple normalize() const {
    VersionTuple Result = *this;
    if (Result.Build == 0) {
      Result.HasBuild = false;
      if (Result.Subminor == 0) {
        Result.HasSubminor = false;
        if (Result.Minor == 0)
          Result.HasMinor = false;
      }
    }
    return Result;
  }

  /// Determine if two version numbers are equivalent. If not
  /// provided, minor and subminor version numbers are considered to be zero.
  friend bool operator==(const VersionTuple &X, const VersionTuple &Y) {
    return X.Major == Y.Major && X.Minor == Y.Minor &&
           X.Subminor == Y.Subminor && X.Build == Y.Build;
  }

  /// Determine if two version numbers are not equivalent.
  ///
  /// If not provided, minor and subminor version numbers are considered to be
  /// zero.
  friend bool operator!=(const VersionTuple &X, const VersionTuple &Y) {
    return !(X == Y);
  }

  /// Determine whether one version number precedes another.
  ///
  /// If not provided, minor and subminor version numbers are considered to be
  /// zero.
  friend bool operator<(const VersionTuple &X, const VersionTuple &Y) {
    if (X.Major != Y.Major)
      return X.Major < Y.Major;
    if (X.Minor != Y.Minor)
      return X.Minor < Y.Minor;
    if (X.Subminor != Y.Subminor)
      return X.Subminor < Y.Subminor;
    return X.Build < Y.Build;
  }

  /// Determine whether one version number follows another.
  ///
  /// If not provided, minor and subminor version numbers are considered to be
  /// zero.
  friend bool operator>(const VersionTuple &X, const VersionTuple &Y) {
    return Y < X;
  }

  /// Determine whether one version number precedes or is
  /// equivalent to another.
  ///
  /// If not provided, minor and subminor version numbers are considered to be
  /// zero.
  friend bool operator<=(const VersionTuple &X, const VersionTuple &Y) {
    return !(Y < X);
  }

  /// Determine whether one version number follows or is
  /// equivalent to another.
  ///
  /// If not provided, minor and subminor version numbers are considered to be
  /// zero.
  friend bool operator>=(const VersionTuple &X, const VersionTuple &Y) {
    return !(X < Y);
  }

  friend hash_code hash_value(const VersionTuple &VT) {
    return hash_combine(VT.Major, VT.Minor, VT.Subminor, VT.Build);
  }

  template <typename HasherT, llvm::endianness Endianness>
  friend void addHash(HashBuilder<HasherT, Endianness> &HBuilder,
                      const VersionTuple &VT) {
    HBuilder.add(VT.Major, VT.Minor, VT.Subminor, VT.Build);
  }

  /// Retrieve a string representation of the version number.
  inline SmallString<64> getAsString() const {
    char buf[64];
    csupport_version_format(buf, sizeof(buf), getMajor(), hasMinor(),
                            getMinor(), hasSubminor(), getSubminor(),
                            hasBuild(), getBuild());
    return SmallString<64>(buf);
  }

  /// Try to parse the given string as a version number.
  /// \returns \c true if the string does not match the regular expression
  ///   [0-9]+(\.[0-9]+){0,3}
  inline bool tryParse(StringRef input) {
    unsigned major = 0, minor = 0, micro = 0, build = 0;
    int has_minor = 0, has_subminor = 0, has_build = 0;
    if (csupport_version_try_parse(input.data(), input.size(), &major, &minor,
                                   &micro, &build, &has_minor, &has_subminor,
                                   &has_build))
      return true;
    if (has_build)
      *this = VersionTuple(major, minor, micro, build);
    else if (has_subminor)
      *this = VersionTuple(major, minor, micro);
    else if (has_minor)
      *this = VersionTuple(major, minor);
    else
      *this = VersionTuple(major);
    return false;
  }
};

/// Print a version number.
inline raw_ostream &operator<<(raw_ostream &Out, const VersionTuple &V) {
  Out << V.getAsString();
  return Out;
}

// Provide DenseMapInfo for version tuples.
template <> struct DenseMapInfo<VersionTuple> {
  static inline VersionTuple getEmptyKey() { return VersionTuple(0x7FFFFFFF); }
  static inline VersionTuple getTombstoneKey() {
    return VersionTuple(0x7FFFFFFE);
  }
  static unsigned getHashValue(const VersionTuple &Value) {
    unsigned Result = Value.getMajor();
    if (Value.hasMinor())
      Result = detail::combineHashValue(Result, Value.getMinor());
    if (Value.hasSubminor())
      Result = detail::combineHashValue(Result, Value.getSubminor());
    if (Value.hasBuild())
      Result = detail::combineHashValue(Result, Value.getBuild());

    return Result;
  }

  static bool isEqual(const VersionTuple &LHS, const VersionTuple &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm
#endif // LLVM_SUPPORT_VERSIONTUPLE_H
