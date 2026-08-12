//===- OutputPublicationFlags.h - Output publication status ----*- C++ -*-===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_FOUNDATION_CORE_OUTPUTPUBLICATIONFLAGS_H
#define NEVERC_FOUNDATION_CORE_OUTPUTPUBLICATIONFLAGS_H

#include <cstdint>

namespace neverc {

/// Status shared by single-file and bundle output transactions.
enum OutputPublicationFlag : uint64_t {
  OutputPublished = UINT64_C(1),
  OutputDurable = UINT64_C(2),
  OutputMayBePartial = UINT64_C(4),
  OutputRecoveryRequired = UINT64_C(8),
  OutputDurabilityUnconfirmed = UINT64_C(16),
};

} // namespace neverc

#endif // NEVERC_FOUNDATION_CORE_OUTPUTPUBLICATIONFLAGS_H
