//===- OutputDigest.h - Shared output digest formatting ----------*- C++ -*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_FOUNDATION_CORE_OUTPUTDIGEST_H
#define NEVERC_FOUNDATION_CORE_OUTPUTDIGEST_H

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <string>

namespace neverc {

/// Formats digest bytes as lower-case hexadecimal text.
std::string outputDigestText(llvm::ArrayRef<uint8_t> Digest);

} // namespace neverc

#endif // NEVERC_FOUNDATION_CORE_OUTPUTDIGEST_H
