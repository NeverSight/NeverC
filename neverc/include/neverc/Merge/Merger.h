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

  /// Remove local and undefined ELF symbols that no retained relocation uses,
  /// matching llvm-strip's `--strip-unneeded` safety boundary for ET_REL.
  /// The ELF merger applies this only after all symbol resolution, parallel-
  /// codegen demotion, and relocation remapping are complete; it then rebuilds
  /// `.strtab` so removed names do not remain as stale bytes.  Other formats
  /// currently ignore this option.
  bool stripUnneededSymbols = false;

  /// Merge per-function/per-variable sections into canonical names:
  /// .text.* → .text, .bss.* → .bss, .data.* → .data, .rodata.* → .rodata.
  /// Used for Android kernel modules to fold per-symbol section names.
  /// ELF-only: Mach-O keeps one __text section keyed by (segment, section) and
  /// slices it via MH_SUBSECTIONS_VIA_SYMBOLS, so there are no per-function
  /// sections to fold; COFF has no consumer today.  Both still merge
  /// same-named input sections, but ignore this canonicalization (the verifier
  /// therefore expects the unchanged input section name for those formats).
  bool mergeSections = false;

  /// Emulate the small part of the Android kernel module linker script that
  /// is a loader ABI rather than ordinary section folding.  The final ET_REL
  /// object receives an allocated (possibly empty) `__versions` section and a
  /// `.codetag.alloc_tags` range bounded by `__start_alloc_tags` /
  /// `__stop_alloc_tags`.  Input `alloc_tags` contributions are collected into
  /// that range.  Enable this for Android-kernel relocatable links, including
  /// intermediate partial links.
  bool androidKernelModule = false;

  /// Finalize an Android-kernel relocatable link for delivery.  The inputs'
  /// NeverC-only profile contract must already have been checked by the caller;
  /// finalization drops its section and symbol so the `.ko` retains no tooling
  /// fingerprint.  Intermediate partial links leave this false and preserve
  /// the contract for a later checked link.
  bool finalizeAndroidKernelModule = false;

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
  /// instead of ever emitting a valid-looking but semantically wrong .o. In
  /// Android kernel-module mode it also validates the loader-facing section
  /// and boundary-symbol contract synthesized by the merger.
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
  /// Pre-compressed ELF input sections are not supported by this in-process
  /// merger. It returns false so callers can fall back to the regular linker.
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

/// Independently audit the loader-facing contract of a delivered Android
/// kernel module without requiring its pre-merge inputs.  The caller must
/// select both Android module semantics and finalization.  This exposes the
/// native verified merger's canonical output checks to plugin paths: ET_REL
/// structure, module ABI sections/boundaries, debug removal, and relocation-
/// safe symbol pruning.
bool verifyAndroidKernelModuleImage(llvm::ArrayRef<char> Output,
                                    const Options &Opts,
                                    std::string *Err = nullptr);

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
