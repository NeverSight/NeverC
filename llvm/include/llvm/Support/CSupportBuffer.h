//===- CSupportBuffer.h - Calling a csupport buffer filler ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The one way to call a csupport routine that writes into a caller's buffer.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CSUPPORTBUFFER_H
#define LLVM_SUPPORT_CSUPPORTBUFFER_H

#include "llvm/ADT/SmallString.h"

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace llvm {

/// Runs a csupport routine that fills a buffer this owns, growing the buffer
/// until the whole output fits.
///
/// \p Fill is called as <tt>Fill(char *Buf, size_t Cap)</tt> and must follow
/// snprintf's convention: write what fits, terminate it, and return the length
/// the complete output needs, whether or not that is the length written.  The
/// C side of that contract is \c csupport_obuf_t, which every filler in
/// llvm/lib/CSupport builds its answer through.
///
/// Reporting the truncated length instead is what makes truncation invisible.
/// A caller cannot tell a complete answer from a cut one by looking at it --
/// both are just bytes of the right shape -- so the report is the only place
/// the difference can survive, and a filler that clamps it destroys the one
/// signal anybody downstream could have acted on.
template <unsigned InlineCapacity = 256, typename Filler>
SmallString<InlineCapacity> fillCSupportBuffer(Filler Fill) {
  SmallString<InlineCapacity> Out;
  Out.resize_for_overwrite(InlineCapacity);

  // A filler holds a byte back for its terminator, so an output exactly as
  // long as the buffer is one that did not fit.
  size_t Length = Fill(Out.data(), Out.size());
  if (Length >= Out.size()) {
    Out.resize_for_overwrite(Length + 1);
    Length = Fill(Out.data(), Out.size());
    assert(Length < Out.size() && "csupport filler reported unstable length");
  }

  // Clamped rather than trusted: a filler whose second answer disagrees with
  // its first has already been caught by the assertion above, and a release
  // build should still not read past what it wrote.
  Out.truncate(std::min(Length, static_cast<size_t>(Out.size())));
  return Out;
}

} // namespace llvm

#endif // LLVM_SUPPORT_CSUPPORTBUFFER_H
