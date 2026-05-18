//== llvm/Support/CodeGenCoverage.h ------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file This file provides rule coverage tracking for tablegen-erated CodeGen.
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CODEGENCOVERAGE_H
#define LLVM_SUPPORT_CODEGENCOVERAGE_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class MemoryBuffer;

class CodeGenCoverage {
protected:
  BitVector RuleCoverage;

public:
  using const_covered_iterator = BitVector::const_set_bits_iterator;

  CodeGenCoverage();

  void setCovered(uint64_t RuleID);
  bool isCovered(uint64_t RuleID) const;
  iterator_range<const_covered_iterator> covered() const;

  bool parse(MemoryBuffer &Buffer, StringRef BackendName);
  bool emit(StringRef FilePrefix, StringRef BackendName) const;
  void reset();
};
namespace detail {
inline sys::SmartMutex<true> &getCodeGenOutputMutex() {
  static sys::SmartMutex<true> Mutex;
  return Mutex;
}
} // namespace detail

inline CodeGenCoverage::CodeGenCoverage() = default;
inline void CodeGenCoverage::setCovered(uint64_t RuleID) {
  if (RuleCoverage.size() <= RuleID)
    RuleCoverage.resize(RuleID + 1, false);
  RuleCoverage[RuleID] = true;
}
inline bool CodeGenCoverage::isCovered(uint64_t RuleID) const {
  if (RuleCoverage.size() <= RuleID)
    return false;
  return RuleCoverage[RuleID];
}
inline iterator_range<CodeGenCoverage::const_covered_iterator>
CodeGenCoverage::covered() const {
  return RuleCoverage.set_bits();
}
inline bool CodeGenCoverage::parse(MemoryBuffer &Buffer,
                                   StringRef BackendName) {
  const char *CurPtr = Buffer.getBufferStart();
  while (CurPtr != Buffer.getBufferEnd()) {
    const char *LexedBackendName = CurPtr;
    while (*CurPtr++ != 0)
      ;
    if (CurPtr == Buffer.getBufferEnd())
      return false;
    bool IsForThisBackend = BackendName.equals(LexedBackendName);
    while (CurPtr != Buffer.getBufferEnd()) {
      if ((Buffer.getBufferEnd() - CurPtr) < 8)
        return false;
      uint64_t RuleID =
          support::endian::read64(CurPtr, llvm::endianness::native);
      CurPtr += 8;
      if (RuleID == ~0ull)
        break;
      if (IsForThisBackend)
        setCovered(RuleID);
    }
  }
  return true;
}
inline bool CodeGenCoverage::emit(StringRef CoveragePrefix,
                                  StringRef BackendName) const {
  if (!CoveragePrefix.empty() && !RuleCoverage.empty()) {
    sys::SmartScopedLock<true> Lock(detail::getCodeGenOutputMutex());
    SmallString<64> Pid;
    raw_svector_ostream(Pid) << sys::Process::getProcessId();
    SmallString<256> CoverageFilename;
    CoverageFilename += CoveragePrefix;
    CoverageFilename += Pid;
    std::error_code EC;
    sys::fs::OpenFlags OpenFlags = sys::fs::OF_Append;
    auto CoverageFile = std::unique_ptr<ToolOutputFile>(
        new ToolOutputFile(CoverageFilename, EC, OpenFlags));
    if (EC)
      return false;
    uint64_t Zero = 0;
    uint64_t InvZero = ~0ull;
    CoverageFile->os() << BackendName;
    CoverageFile->os().write((const char *)&Zero, sizeof(unsigned char));
    for (uint64_t I : RuleCoverage.set_bits())
      CoverageFile->os().write((const char *)&I, sizeof(uint64_t));
    CoverageFile->os().write((const char *)&InvZero, sizeof(uint64_t));
    CoverageFile->keep();
  }
  return true;
}
inline void CodeGenCoverage::reset() { RuleCoverage.resize(0); }

} // namespace llvm

#endif // LLVM_SUPPORT_CODEGENCOVERAGE_H
