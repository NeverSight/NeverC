//===- MCComponentProvider.cpp - Per-context MC component seam ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCComponentProvider.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCObjectWriter.h"

using namespace llvm;

MCComponentProvider::~MCComponentProvider() = default;

Expected<std::unique_ptr<MCInstPrinter>>
MCComponentProvider::provideInstPrinter(
    MCContext &, std::unique_ptr<MCInstPrinter> Fallback) {
  return std::move(Fallback);
}

Expected<std::unique_ptr<MCCodeEmitter>>
MCComponentProvider::provideCodeEmitter(
    MCContext &, std::unique_ptr<MCCodeEmitter> Fallback) {
  return std::move(Fallback);
}

Expected<std::unique_ptr<MCAsmBackend>>
MCComponentProvider::provideAsmBackend(
    MCContext &, std::unique_ptr<MCAsmBackend> Fallback) {
  return std::move(Fallback);
}

Expected<std::unique_ptr<MCObjectWriter>>
MCComponentProvider::provideObjectWriter(
    MCContext &, std::unique_ptr<MCObjectWriter> Fallback) {
  return std::move(Fallback);
}
