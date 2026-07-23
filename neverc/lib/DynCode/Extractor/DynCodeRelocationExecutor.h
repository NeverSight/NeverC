#ifndef NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODERELOCATIONEXECUTOR_H
#define NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODERELOCATIONEXECUTOR_H

// The relocation executor applies an intra-image relocation worklist to a
// candidate DynCodeImage laid out at logical base 0.  It centralises the
// patch dispatch the old per-format extractors each open-coded: from a resolved
// target output offset and the site offset it computes
//
//     FinalAddr = TargetOffset + Addend
//     PCDisp    = FinalAddr - SiteOffset
//
// and drives the checked branch26 / page21 / lo12 / rel32 / rel64 encoders.
// The kind is the architecture-specific fixup form (what the format reader maps
// each native relocation type onto); anything the executor cannot patch is a
// structured error, never a silent skip.

#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace neverc {
namespace dyncode {

/// Architecture-specific fixup form a relocation lowers to.  The format reader
/// maps each native relocation type (R_AARCH64_CALL26, R_X86_64_PC32, ...) onto
/// one of these; the executor only knows how to encode these forms.
enum class DynCodeRelocApplyKind : uint32_t {
  None = 0,
  AArch64Branch26, ///< CALL26 / JUMP26
  AArch64Page21,   ///< ADR_PREL_PG_HI21[_NC]
  AArch64AddLo12,  ///< ADD_ABS_LO12_NC (shift 0)
  AArch64LdstLo12, ///< LDST{8,16,32,64,128}_ABS_LO12_NC (shift = log2 access size)
  AArch64Prel32,   ///< PREL32
  AArch64Prel64,   ///< PREL64
  X86Rel32,        ///< PC32 / PLT32
};

/// One resolved, intra-image relocation to apply.
struct DynCodeRelocationWork {
  uint64_t SiteOffset = 0;   ///< image offset of the fixup field
  uint64_t TargetOffset = 0; ///< image offset of the target (logical base 0)
  int64_t Addend = 0;
  DynCodeRelocApplyKind Kind = DynCodeRelocApplyKind::None;
  unsigned LdstShift = 0; ///< only for AArch64LdstLo12
};

/// Applies a single relocation to ``Bytes`` (the full image, logical base 0).
/// Fails on an out-of-range site or an unsupported kind.
llvm::Error applyDynCodeRelocation(llvm::MutableArrayRef<uint8_t> Bytes,
                                   const DynCodeRelocationWork &Work);

/// Applies the whole worklist to ``Image``.  The image must still be a mutable
/// candidate; on success every fixup has been encoded in place.
llvm::Error executeDynCodeRelocations(DynCodeImage &Image,
                                      llvm::ArrayRef<DynCodeRelocationWork> Work);

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODERELOCATIONEXECUTOR_H
