//===-- X86WinCOFFTargetStreamer.cpp ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "X86MCTargetDesc.h"
#include "X86TargetStreamer.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

namespace {

class X86WinCOFFAsmTargetStreamer : public X86TargetStreamer {
  formatted_raw_ostream &OS;

public:
  X86WinCOFFAsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                              MCInstPrinter &InstPrinter)
      : X86TargetStreamer(S), OS(OS) {}
};

class X86WinCOFFTargetStreamer : public X86TargetStreamer {
public:
  X86WinCOFFTargetStreamer(MCStreamer &S) : X86TargetStreamer(S) {}
};

} // end anonymous namespace

MCTargetStreamer *llvm::createX86AsmTargetStreamer(MCStreamer &S,
                                                   formatted_raw_ostream &OS,
                                                   MCInstPrinter *InstPrinter,
                                                   bool IsVerboseAsm) {
  return new X86WinCOFFAsmTargetStreamer(S, OS, *InstPrinter);
}

MCTargetStreamer *
llvm::createX86ObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  if (!STI.getTargetTriple().isOSBinFormatCOFF())
    return nullptr;
  return new X86WinCOFFTargetStreamer(S);
}
