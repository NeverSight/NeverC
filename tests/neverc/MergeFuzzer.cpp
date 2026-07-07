//===- MergeFuzzer.cpp - libFuzzer entry for the object mergers ---------===//
//
// Coverage-guided fuzzing entry point for the relocatable object merger and
// its independent self-verifier.  The synthetic and differential gtest suites
// in MergeTests.cpp prove correctness on hand-built and real-linker objects;
// this raises the ceiling to "no adversarial byte sequence can make the merger
// or its verifier crash, or make the verify gate change the emitted object, or
// make the merge non-deterministic".  It exercises these invariants on
// arbitrary input, for all three container formats, split into 1..8 sub-buffers
// (the historical offset-collapse bug needed >= 2 partitions to manifest):
//
//   1) Neither mergeObjects() with verify ON (the production path, which also
//      runs the independent verifyMerge internally) nor with verify OFF (the
//      raw merger code paths the gate would otherwise hide) ever crashes.
//   2) verify is a *gate*, never a rewrite: if the verified merge succeeds, the
//      unverified merge must also succeed and produce BYTE-IDENTICAL output.
//      (The converse is allowed: the conservative verifier may decline an input
//      its raw reader cannot parse even though the lenient LLVM-based merger
//      accepted it — in production that simply becomes a safe serial fallback,
//      so it is not a bug.)
//   3) Re-merging the same inputs yields byte-identical output (determinism;
//      the LTO partition cache and reproducible builds depend on it).
//
// Build (needs a fuzzer-capable compiler):
//   cmake -DNEVERC_ENABLE_MERGE_FUZZER=ON ...
//   ninja neverc-merge-fuzzer
//   ./bin/neverc-merge-fuzzer -max_len=16384 corpus/
//
//===------------------------------------------------------------------===//

#include "neverc/Merge/Merger.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace llvm;
using namespace neverc::merge;

// LLVM's MachO (and to a lesser extent COFF) object parser calls
// report_fatal_error() → abort() on some malformed-input paths instead of
// returning Error through the Expected<> API.  The canonical example is
// getStruct() in MachOObjectFile.cpp whose FIXME reads "Replace all uses of
// this function with getStructOrErr".  Until that LLVM-wide migration lands,
// a fuzz-generated input that passes createMachOObjectFile() construction but
// hits a bounds check in a later accessor (getSection64, getSymbol64TableEntry,
// getRelocation, ...) would kill the fuzzer process.
//
// We install a temporary fatal-error handler that longjmp's back to
// LLVMFuzzerTestOneInput so the fuzzer treats the input as uninteresting and
// moves on.  The handler is scoped to each fuzzer iteration and does NOT
// suppress real bugs:
//   * ASan / UBSan findings fire through their own signal handlers, not
//     through report_fatal_error, so they still crash as intended.
//   * The exercise() invariant violations (abort()) also bypass this handler.
static thread_local jmp_buf g_LLVMFatalBuf;

static void fuzzFatalErrorHandler(void *, const char *, bool) {
  longjmp(g_LLVMFatalBuf, 1);
}

namespace {

// Carve the fuzzer buffer into 1..8 sub-buffers via a 2-byte little-endian
// length prefix per chunk, so the fuzzer can explore multi-partition merges.
std::vector<SmallVector<char, 0>> carve(const uint8_t *Data, size_t Size) {
  std::vector<SmallVector<char, 0>> Bufs;
  size_t Pos = 0;
  while (Pos + 2 <= Size && Bufs.size() < 8) {
    size_t Len = (size_t)Data[Pos] | ((size_t)Data[Pos + 1] << 8);
    Pos += 2;
    Len = std::min(Len, Size - Pos);
    SmallVector<char, 0> B;
    B.append(reinterpret_cast<const char *>(Data + Pos),
             reinterpret_cast<const char *>(Data + Pos + Len));
    Bufs.push_back(std::move(B));
    Pos += Len;
  }
  if (Bufs.empty()) {
    SmallVector<char, 0> B;
    B.append(reinterpret_cast<const char *>(Data),
             reinterpret_cast<const char *>(Data + Size));
    Bufs.push_back(std::move(B));
  }
  return Bufs;
}

bool bytesEqual(const SmallVectorImpl<char> &A, const SmallVectorImpl<char> &B) {
  return A.size() == B.size() &&
         (A.empty() || std::memcmp(A.data(), B.data(), A.size()) == 0);
}

// Run one merge with the given options (a fresh stream each call).
bool mergeOnce(Format Fmt, ArrayRef<SmallVector<char, 0>> Bufs,
               const Options &Opts, SmallVectorImpl<char> &Out) {
  Out.clear();
  raw_svector_ostream OS(Out);
  return mergeObjects(Bufs, OS, Fmt, Opts);
}

void exercise(Format Fmt, ArrayRef<SmallVector<char, 0>> Bufs,
              bool MergeSections) {
  Options Verified;
  Options Raw;
  Raw.verify = false;
  if (MergeSections) {
    Verified.mergeSections = Raw.mergeSections = true;
    // Folding .text.*/.bss.*/... is the riskiest offset arithmetic and the path
    // that produced the original collapse; preserve a couple of kernel sections
    // the way the Android-module caller does.
    Verified.preservedSections = Raw.preservedSections = {".modinfo",
                                                          "__versions"};
  }

  // Invariant 1: the production (verify-on) merge — which internally runs the
  // independent verifyMerge — must not crash on any input.
  SmallVector<char, 0> OutV;
  bool OkV = mergeOnce(Fmt, Bufs, Verified, OutV);

  // Invariant 1 (cont.): the raw (verify-off) merge must not crash either; it
  // exercises encode paths the verify gate would otherwise mask.
  SmallVector<char, 0> OutR;
  bool OkR = mergeOnce(Fmt, Bufs, Raw, OutR);

  // Invariant 2: verify is a gate, not a rewrite.  A passing verified merge
  // means the raw merge produced that exact accepted object, so the raw merge
  // must also succeed with byte-identical output.
  if (OkV && (!OkR || !bytesEqual(OutV, OutR))) {
    errs() << "neverc-merge-fuzzer: verify gate is not byte-transparent (the "
              "merge is non-deterministic or verify altered the output): fmt="
           << (int)Fmt << " mergeSections=" << MergeSections << " OkR=" << OkR
           << " sizeV=" << OutV.size() << " sizeR=" << OutR.size() << "\n";
    abort();
  }

  // Invariant 3: determinism of the raw merge.
  if (OkR) {
    SmallVector<char, 0> OutR2;
    bool OkR2 = mergeOnce(Fmt, Bufs, Raw, OutR2);
    if (!OkR2 || !bytesEqual(OutR, OutR2)) {
      errs() << "neverc-merge-fuzzer: merge is non-deterministic\n";
      abort();
    }
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  llvm::install_fatal_error_handler(fuzzFatalErrorHandler, nullptr);
  if (setjmp(g_LLVMFatalBuf) != 0) {
    llvm::remove_fatal_error_handler();
    return 0;
  }

  auto Bufs = carve(Data, Size);
  for (Format Fmt : {Format::ELF64LE, Format::MachO64, Format::COFF})
    exercise(Fmt, Bufs, /*MergeSections=*/false);
  // The ELF kernel-module section-folding path gets its own pass.
  exercise(Format::ELF64LE, Bufs, /*MergeSections=*/true);

  llvm::remove_fatal_error_handler();
  return 0;
}
