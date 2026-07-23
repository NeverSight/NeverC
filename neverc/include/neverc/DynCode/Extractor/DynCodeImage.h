#ifndef NEVERC_DYNCODE_EXTRACTOR_DYNCODEIMAGE_H
#define NEVERC_DYNCODE_EXTRACTOR_DYNCODEIMAGE_H

// Volume 6 task 10: the candidate dyncode image and its bounded bytes builder.
//
// A DynCodeImage owns the candidate code bytes plus the extraction metadata
// (entry, alignment, padding) and a small state machine mirroring the C ABI
// NevercDynCodeImageState.  Every mutation goes through checked read/write/
// insert/append/resize/replace-range helpers whose arithmetic is overflow- and
// budget-checked; there is no raw ``uint8_t** + Len + Capacity`` path.  Each
// republish bumps the image generation so a stale image handle is detectable.

#include "neverc/DynCode/Extractor/DynCodeExtractionPlan.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <optional>
#include <string>

namespace neverc {
namespace dyncode {

enum class DynCodeImageState : uint32_t {
  Candidate = 1,
  Verified = 2,
  Committed = 3,
  Aborted = 4,
  FailedPartial = 5,
};

class DynCodeImage {
public:
  DynCodeImage() = default;

  /// Optional hard cap on the image size (the request MaxLength).  Any mutation
  /// that would exceed it fails.
  void setBudget(std::optional<uint64_t> MaxLength) { Budget = MaxLength; }
  std::optional<uint64_t> budget() const { return Budget; }

  DynCodeImageState state() const { return State; }
  uint64_t generation() const { return Generation; }
  uint64_t size() const { return Bytes.size(); }
  llvm::ArrayRef<uint8_t> bytes() const { return Bytes; }

  uint64_t entryOffset() const { return EntryOff; }
  llvm::StringRef entrySymbol() const { return EntrySym; }
  uint64_t outputAlignment() const { return Alignment; }
  uint64_t paddingSize() const { return PaddingSize; }

  void setEntry(uint64_t Offset, llvm::StringRef Symbol) {
    EntryOff = Offset;
    EntrySym = Symbol.str();
  }
  llvm::Error setOutputAlignment(uint64_t Align);
  void setPaddingSize(uint64_t Size) { PaddingSize = Size; }

  // --- bounded, checked bytes builder -------------------------------------

  /// Reads [Offset, Offset+Length); fails if it runs past the end.
  llvm::Expected<llvm::ArrayRef<uint8_t>> read(uint64_t Offset,
                                               uint64_t Length) const;
  /// Overwrites [Offset, Offset+Data.size()); the range must already exist.
  llvm::Error write(uint64_t Offset, llvm::ArrayRef<uint8_t> Data);
  /// Appends Data to the end (budget-checked).
  llvm::Error append(llvm::ArrayRef<uint8_t> Data);
  /// Inserts Data at Offset (0..size), shifting the tail (budget-checked).
  llvm::Error insert(uint64_t Offset, llvm::ArrayRef<uint8_t> Data);
  /// Grows/shrinks the image to NewSize (budget-checked); new bytes are Fill.
  llvm::Error resize(uint64_t NewSize, uint8_t Fill = 0);
  /// Replaces [Offset, Offset+Length) with Data (bounds- and budget-checked).
  llvm::Error replaceRange(uint64_t Offset, uint64_t Length,
                           llvm::ArrayRef<uint8_t> Data);

  /// Bumps the generation; existing image handles from before are now stale.
  void republish() { ++Generation; }

  /// Deterministic SHA-256 of the current bytes.
  std::array<uint8_t, 32> digest() const;

  // --- state transitions ---------------------------------------------------

  llvm::Error markVerified();
  llvm::Error markCommitted();
  llvm::Error markAborted();
  void markFailedPartial() { State = DynCodeImageState::FailedPartial; }

private:
  llvm::Error ensureMutable() const;

  llvm::SmallVector<uint8_t, 0> Bytes;
  DynCodeImageState State = DynCodeImageState::Candidate;
  uint64_t Generation = 1;
  uint64_t EntryOff = 0;
  std::string EntrySym;
  uint64_t Alignment = 1;
  uint64_t PaddingSize = 0;
  std::optional<uint64_t> Budget;
};

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_DYNCODE_EXTRACTOR_DYNCODEIMAGE_H
