//===- Merger.h - Relocatable object merger (LLD -r algorithms) --------===//
//
// Public API for merging relocatable object files (.o) into a single .o.
// The merge algorithms are ported from the LLD linker's `-r` (partial
// link / relocatable) code paths, optimized for the pure-C pipeline:
//
//   ELF   ← LLD ELF backend (OutputSections, Symbols, Relocations,
//            ElfImageEmitter) with C++ COMDAT/SHT_GROUP removed
//   MachO ← LLD MachO backend (ConcatOutputSection, SymbolTable,
//            MachOImageEmitter) with __cstring dedup removed
//   COFF  ← LLD COFF backend section merge + symbol resolution
//
// This library is shared by:
//   1) The linker backends (ELF/MachO -r mode → fast relocatable merge)
//   2) The parallel codegen pipeline (partition .o merge)
//
// .eh_frame / __compact_unwind / __unwind_info are preserved as regular
// sections (no CIE/FDE dedup — matches LLD -r behavior).
//
//===------------------------------------------------------------------===//

#ifndef NEVERC_MERGE_MERGER_H
#define NEVERC_MERGE_MERGER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

namespace neverc::merge {

/// Marker that parallel codegen appends (followed by a per-module hash, e.g.
/// "helper.__pcg1a2b3c4d") when it externalizes a module-local symbol so that
/// cross-partition references resolve.  This name suffix is the *only* signal
/// that a symbol is a parallel-codegen artifact — no object format carries a
/// flag for it — so the codegen that appends it and every merger that detects
/// it must share this single definition instead of hardcoding the literal.
inline constexpr llvm::StringLiteral PcgSymbolMarker = ".__pcg";

/// Container format of the input/output objects.
enum class Format {
  ELF64LE,
  MachO64,
  COFF,
};

/// Semantic role of the object image being merged.
///
/// A normal relocatable object carries DWARF cross-section references as
/// relocations on ELF/COFF. A standalone Split-DWARF object deliberately has
/// no relocations: its `.debug_*.dwo` references are literal offsets and must
/// be rebased when section contributions are concatenated.
enum class ArtifactKind {
  RelocatableObject,
  SplitDwarf,
};

/// Tuning knobs for the merge.  Defaults match the LTO+parallel-codegen
/// pipeline (pure C, no C++ COMDAT/SHT_GROUP).
struct Options {
  ArtifactKind artifact = ArtifactKind::RelocatableObject;

  /// Pure-C mode: skip C++ section types (SHT_GROUP, COMDAT,
  /// .gnu.linkonce.*).  Always safe for neverc's pipeline.
  bool pureC = true;

  /// Drop DWARF debug sections (.debug_*, .zdebug_*) instead of
  /// trying to re-link cross-section refs.  Matches LLD -r behavior
  /// when --strip-debug is active.
  bool dropDebugInfo = false;

  /// Merge per-function/per-variable sections into canonical names:
  /// .text.* → .text, .bss.* → .bss, .data.* → .data, .rodata.* → .rodata.
  /// Used for Android kernel modules to fold per-symbol section names.
  /// ELF-only: Mach-O keeps one __text section keyed by (segment, section) and
  /// slices it via MH_SUBSECTIONS_VIA_SYMBOLS, so there are no per-function
  /// sections to fold; COFF has no consumer today.  Both still merge
  /// same-named input sections, but ignore this canonicalization (the verifier
  /// therefore expects the unchanged input section name for those formats).
  bool mergeSections = false;

  /// Sections to preserve from merging (exact match).  Only consulted
  /// when mergeSections is true.
  llvm::SmallVector<llvm::StringRef, 8> preservedSections;

  /// Self-verify the produced object before returning success.  When on,
  /// the merger re-parses its own output with an *independent* raw reader
  /// (no shared offset math) and content-anchors every uniquely-named
  /// defined symbol back to the input bytes it came from.  Any divergence
  /// — e.g. the historical "all symbol values collapse to 0" bug — makes
  /// the merge fail (return false) *before* writing, so callers fall back
  /// to the proven path (serial codegen / a real linker) or error loudly
  /// instead of ever emitting a valid-looking but semantically wrong .o.
  /// Cost is one extra O(output) pass; merge is a tiny fraction of link
  /// time, so this is on by default.  Enforced for all three formats
  /// (ELF/COFF/MachO) by the format-specific content anchors in
  /// Verify/MergerVerify.cpp; the Mach-O check additionally skips byte windows
  /// that overlap a relocation site, because the Mach-O merger rewrites those
  /// bytes in place (so they legitimately differ from the input).
  bool verify = true;

  /// Compress final ELF `.debug_*` sections after every contribution has been
  /// merged and (for SplitDwarf) rebased. Partition codegen must emit
  /// uncompressed sections; concatenating independent compressed frames would
  /// make every contribution after the first invisible.
  ///
  /// COFF intentionally ignores this setting, matching LLVM's object writer.
  llvm::DebugCompressionType debugCompression =
      llvm::DebugCompressionType::None;
};

/// Merge \p Buffers (each is a complete .o image) into a single .o of
/// format \p Fmt, writing the result to \p OS.  Returns true on success,
/// false on any parse/encode error; callers should fall back to serial
/// codegen on failure.
bool mergeObjects(llvm::ArrayRef<llvm::SmallVector<char, 0>> Buffers,
                  llvm::raw_pwrite_stream &OS, Format Fmt,
                  const Options &Opts = {});

/// Zero-copy overload: accepts raw buffer views (e.g. from LLD's
/// MemoryBufferRef) so the linker's -r path avoids data copies.
bool mergeObjects(llvm::ArrayRef<llvm::StringRef> Buffers,
                  llvm::raw_pwrite_stream &OS, Format Fmt,
                  const Options &Opts = {});

/// Independently re-validate that \p Output is a faithful merge of \p Inputs
/// for format \p Fmt.  This is the same check `mergeObjects` runs internally
/// when `Options::verify` is set, exposed so the linker's -r path and the
/// test suite can audit an already-produced object.  Returns true when the
/// merge is semantically sound; on false, \p Err (when non-null) receives a
/// human-readable reason.  A return of true is *conservative*: ambiguous
/// cases (e.g. a symbol name that is not unique in the output) are skipped
/// rather than guessed, so a true never hides a definite corruption but a
/// false is always a real one.
bool verifyMerge(llvm::ArrayRef<llvm::StringRef> Inputs,
                 llvm::ArrayRef<char> Output, Format Fmt,
                 const Options &Opts = {}, std::string *Err = nullptr);
bool verifyMerge(llvm::ArrayRef<llvm::SmallVector<char, 0>> Inputs,
                 llvm::ArrayRef<char> Output, Format Fmt,
                 const Options &Opts = {}, std::string *Err = nullptr);

/// Validate a completed DWARF 5 split-debug pair. Every skeleton CU in the
/// main object must have exactly one split CU with the same DWO ID, and neither
/// side may contain duplicate IDs or malformed unit DIEs.
bool verifySplitDwarfPair(llvm::ArrayRef<char> Object,
                          llvm::ArrayRef<char> SplitDwarf, Format Fmt,
                          std::string *Err = nullptr);

// Per-format helpers — exported so the existing call sites in
// ParallelCodeGenMerge.cpp / BackendUtil.cpp can keep their direct
// invocations during the migration window.  New code should prefer
// `mergeObjects` above.
bool mergeELF64LEObjects(llvm::ArrayRef<llvm::SmallVector<char, 0>> Buffers,
                         llvm::raw_pwrite_stream &OS, const Options &Opts = {});
bool mergeELF64LEObjects(llvm::ArrayRef<llvm::StringRef> Buffers,
                         llvm::raw_pwrite_stream &OS, const Options &Opts = {});

bool mergeMachO64Objects(llvm::ArrayRef<llvm::SmallVector<char, 0>> Buffers,
                         llvm::raw_pwrite_stream &OS, const Options &Opts = {});
bool mergeMachO64Objects(llvm::ArrayRef<llvm::StringRef> Buffers,
                         llvm::raw_pwrite_stream &OS, const Options &Opts = {});

bool mergeCOFFObjects(llvm::ArrayRef<llvm::SmallVector<char, 0>> Buffers,
                      llvm::raw_pwrite_stream &OS, const Options &Opts = {});
bool mergeCOFFObjects(llvm::ArrayRef<llvm::StringRef> Buffers,
                      llvm::raw_pwrite_stream &OS, const Options &Opts = {});

} // namespace neverc::merge

#endif // NEVERC_MERGE_MERGER_H
