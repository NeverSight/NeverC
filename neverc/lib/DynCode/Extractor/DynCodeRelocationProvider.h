#ifndef NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODERELOCATIONPROVIDER_H
#define NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODERELOCATIONPROVIDER_H

// The builtin dyncode relocation providers.
//
// A relocation provider maps a native relocation type (as recovered from an
// ObjectGraph relocation's "NCRL" extension) onto the architecture-specific
// fixup form the intra-image relocation executor knows how to encode, folding
// in any per-format addend/field-bias convention so the shared executor
// reproduces the exact bytes the old per-format ELF/COFF/Mach-O extractors
// emitted.  This is the format-agnostic replacement for the three open-coded
// relocation switches.
//
// The mapping also classifies relocations the extractor must refuse
// (GOT-indirect, absolute, unsupported) so an unresolved reference is a
// structured error, never a silent skip.

#include "Extractor/DynCodeRelocationExecutor.h"
#include "neverc/DynCode/Extractor/DynCodeExtractionPlan.h"
#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "neverc/DynCode/Extractor/DynCodeReport.h"
#include "neverc/DynCode/Pipeline/TargetDesc.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <optional>

namespace neverc {
namespace plugin {
struct PluginObjectExtension;
}
namespace dyncode {

/// How the relocation provider classifies a native relocation type.
enum class DynCodeRelocationClass : uint32_t {
  /// Resolvable inside the extracted image; apply with the returned fixup form.
  IntraImage,
  /// GOT/PLT indirection: dyncode has no GOT, so this is a hard error.
  ExternalGOT,
  /// Absolute address needing a loader (e.g. R_*_UNSIGNED / ADDR64).
  ExternalAbsolute,
  /// Unknown or otherwise unsupported relocation type.
  Unsupported,
};

/// The result of mapping a native relocation type on a target.
struct DynCodeRelocationMapping {
  DynCodeRelocationClass Class = DynCodeRelocationClass::Unsupported;
  DynCodeRelocApplyKind Kind = DynCodeRelocApplyKind::None;
  unsigned LdstShift = 0;
  /// Added to the relocation's own addend before the executor computes the
  /// displacement.  ELF carries the -4 end-relative correction in the addend
  /// itself; COFF and Mach-O do not, so their PC-relative forms fold it here.
  int64_t AddendAdjust = 0;
};

/// Reads the raw native relocation type out of the "NCRL" extension blob, or
/// nothing when the blob is missing or malformed.  The layout itself lives in
/// Plugin/Host/BuiltinObjectExtension.h, beside the reader that writes it.
std::optional<uint64_t>
decodeNativeRelocationType(const plugin::PluginObjectExtension &Ext);

/// Maps a native relocation type on the given target onto an intra-image apply
/// plan, or classifies it as an external/unsupported form.
DynCodeRelocationMapping mapDynCodeRelocation(const TargetDesc &Target,
                                              uint64_t NativeType);

/// Resolves the plan's relocation worklist through the builtin providers,
/// applies it to the candidate image via the relocation executor, and records
/// the outcome in the report.  Any unresolved / external / unsupported
/// relocation whose site is inside the image is a structured error (dyncode
/// must be fully resolved) -- never a silent skip or a running counter.
llvm::Error resolveAndApplyDynCodeRelocations(const DynCodeExtractionPlan &Plan,
                                              const TargetDesc &Target,
                                              DynCodeImage &Image,
                                              DynCodeReport &Report);

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_EXTRACTOR_DYNCODERELOCATIONPROVIDER_H
