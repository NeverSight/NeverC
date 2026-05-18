//===- llvm/Object/BuildID.h - Build ID -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares a library for handling Build IDs and using them to find
/// debug info.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_OBJECT_BUILDID_H
#define LLVM_DEBUGINFO_OBJECT_BUILDID_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
namespace object {

/// A build ID in binary form.
typedef SmallVector<uint8_t, 10> BuildID;

/// A reference to a BuildID in binary form.
typedef ArrayRef<uint8_t> BuildIDRef;

} // end namespace object
} // end namespace llvm

#endif // LLVM_DEBUGINFO_OBJECT_BUILDID_H
