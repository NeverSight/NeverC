//===- LineIterator.h - Iterator to read a text buffer's lines --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_LINEITERATOR_H
#define LLVM_SUPPORT_LINEITERATOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <iterator>

extern "C" {
size_t csupport_next_line(const char **pos, const char *end,
                          char comment_marker, int skip_blanks,
                          int *line_number);
int csupport_is_at_line_end(const char *pos);
}

namespace llvm {

/// A forward iterator which reads text lines from a buffer.
///
/// This class provides a forward iterator interface for reading one line at
/// a time from a buffer. When default constructed the iterator will be the
/// "end" iterator.
///
/// The iterator is aware of what line number it is currently processing. It
/// strips blank lines by default, and comment lines given a comment-starting
/// character.
///
/// Note that this iterator requires the buffer to be nul terminated.
class line_iterator {
  MemoryBufferRef Buffer; // default-constructed (null data) = end iterator
  bool HasBuffer = false;
  char CommentMarker = '\0';
  bool SkipBlanks = true;

  unsigned LineNumber = 1;
  StringRef CurrentLine;

public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = StringRef;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type *;
  using reference = value_type &;

  /// Default construct an "end" iterator.
  line_iterator() = default;

  /// Construct a new iterator around an unowned memory buffer.
  explicit inline line_iterator(const MemoryBufferRef &Buffer,
                                bool SkipBlanks = true,
                                char CommentMarker = '\0')
      : Buffer(Buffer), HasBuffer(Buffer.getBufferSize() != 0),
        CommentMarker(CommentMarker), SkipBlanks(SkipBlanks),
        CurrentLine(Buffer.getBufferSize() ? Buffer.getBufferStart() : 0, 0) {
    if (Buffer.getBufferSize()) {
      if (SkipBlanks || !csupport_is_at_line_end(Buffer.getBufferStart()))
        advance();
    }
  }

  /// Construct a new iterator around some memory buffer.
  explicit inline line_iterator(const MemoryBuffer &Buffer,
                                bool SkipBlanks = true,
                                char CommentMarker = '\0')
      : line_iterator(Buffer.getMemBufferRef(), SkipBlanks, CommentMarker) {}

  /// Return true if we've reached EOF or are an "end" iterator.
  bool is_at_eof() const { return !HasBuffer; }

  /// Return true if we're an "end" iterator or have reached EOF.
  bool is_at_end() const { return is_at_eof(); }

  /// Return the current line number. May return any number at EOF.
  int64_t line_number() const { return LineNumber; }

  /// Advance to the next (non-empty, non-comment) line.
  line_iterator &operator++() {
    advance();
    return *this;
  }
  line_iterator operator++(int) {
    line_iterator tmp(*this);
    advance();
    return tmp;
  }

  /// Get the current line as a \c StringRef.
  StringRef operator*() const { return CurrentLine; }
  const StringRef *operator->() const { return &CurrentLine; }

  friend bool operator==(const line_iterator &LHS, const line_iterator &RHS) {
    return LHS.HasBuffer == RHS.HasBuffer &&
           LHS.CurrentLine.begin() == RHS.CurrentLine.begin();
  }

  friend bool operator!=(const line_iterator &LHS, const line_iterator &RHS) {
    return !(LHS == RHS);
  }

private:
  /// Advance the iterator to the next line.
  inline void advance() {
    assert(HasBuffer && "Cannot advance past the end!");
    const char *Pos = CurrentLine.end();
    int LN = LineNumber;
    const char *End = Buffer.getBufferEnd();
    size_t Length =
        csupport_next_line(&Pos, End, CommentMarker, SkipBlanks, &LN);
    LineNumber = LN;
    if (Length == 0 && Pos >= End) {
      HasBuffer = false;
      CurrentLine = StringRef();
      return;
    }
    CurrentLine = StringRef(Pos, Length);
  }
};
} // namespace llvm

#endif
