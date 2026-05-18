//===- GCStrategy.cpp - Garbage Collector Description ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// NeverC is a C-only compiler, so no function ever carries a `gc("...")`
// attribute and no GC strategy is ever requested. We keep the GCRegistry
// instantiation and a stub `getGCStrategy` so the Verifier / GCMetadata
// plumbing still link, but they always report "unsupported GC".
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/GCStrategy.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

LLVM_INSTANTIATE_REGISTRY(GCRegistry)

GCStrategy::GCStrategy() = default;

std::unique_ptr<GCStrategy> llvm::getGCStrategy(const StringRef Name) {
  report_fatal_error(
      Twine(std::string("unsupported GC: ") + Name.str() +
            " (NeverC is C-only and registers no GC strategies)"));
}
