//===- CSEConfigBase.h - A CSEConfig interface ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CSECONFIGBASE_H
#define LLVM_CODEGEN_CSECONFIGBASE_H

namespace llvm {
// Class representing some configuration that can be done during CSEInfo
// analysis.
class CSEConfigBase {
public:
  virtual ~CSEConfigBase() = default;
  // Hook for defining which Generic instructions should be CSEd.
  // GISelCSEInfo currently only calls this hook when dealing with generic
  // opcodes.
  virtual bool shouldCSEOpc(unsigned Opc) { return false; }
};

} // namespace llvm

#endif // LLVM_CODEGEN_CSECONFIGBASE_H
