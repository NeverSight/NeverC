//===- MCComponentProvider.h - Per-context MC component seam ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCCOMPONENTPROVIDER_H
#define LLVM_MC_MCCOMPONENTPROVIDER_H

#include "llvm/Support/Error.h"
#include <memory>

namespace llvm {

class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstPrinter;
class MCObjectWriter;

/// A non-owning, per-MCContext customization point for components created by
/// LLVMTargetMachine. Each callback receives the target's fully constructed
/// fallback and may return it unchanged, wrap it, or replace it.
class MCComponentProvider {
public:
  virtual ~MCComponentProvider();

  virtual Expected<std::unique_ptr<MCInstPrinter>>
  provideInstPrinter(MCContext &Context,
                     std::unique_ptr<MCInstPrinter> Fallback);

  virtual Expected<std::unique_ptr<MCCodeEmitter>>
  provideCodeEmitter(MCContext &Context,
                     std::unique_ptr<MCCodeEmitter> Fallback);

  virtual Expected<std::unique_ptr<MCAsmBackend>>
  provideAsmBackend(MCContext &Context,
                    std::unique_ptr<MCAsmBackend> Fallback);

  virtual Expected<std::unique_ptr<MCObjectWriter>>
  provideObjectWriter(MCContext &Context,
                      std::unique_ptr<MCObjectWriter> Fallback);
};

} // end namespace llvm

#endif // LLVM_MC_MCCOMPONENTPROVIDER_H
