//===- MicrosoftStructLayout.h - Microsoft C record layout --------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_TREE_TYPE_MICROSOFTSTRUCTLAYOUT_H
#define NEVERC_TREE_TYPE_MICROSOFTSTRUCTLAYOUT_H

#include "neverc/Tree/Core/CharUnits.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>

namespace neverc {
class RecordDecl;
class TreeContext;

namespace detail {
struct MicrosoftStructLayout {
  CharUnits Size, Alignment, RequiredAlignment, UnadjustedAlignment;
  llvm::SmallVector<uint64_t, 16> FieldOffsets;
};

MicrosoftStructLayout computeMicrosoftStructLayout(const TreeContext &Context,
                                                   const RecordDecl *Record);
} // namespace detail
} // namespace neverc

#endif
