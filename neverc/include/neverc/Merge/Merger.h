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
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"

namespace neverc::merge {

/// Container format of the input/output objects.
enum class Format {
  ELF64LE,
  MachO64,
  COFF,
};

/// Tuning knobs for the merge.  Defaults match the LTO+parallel-codegen
/// pipeline (pure C, no C++ COMDAT/SHT_GROUP).
struct Options {
  /// Pure-C mode: skip C++ section types (SHT_GROUP, COMDAT,
  /// .gnu.linkonce.*).  Always safe for neverc's pipeline.
  bool pureC = true;

  /// Drop DWARF debug sections (.debug_*, .zdebug_*) instead of
  /// trying to re-link cross-section refs.  Matches LLD -r behavior
  /// when --strip-debug is active.
  bool dropDebugInfo = false;
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
