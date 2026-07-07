#ifndef NEVERC_DYNCODE_HEAPARENAPASS_H
#define NEVERC_DYNCODE_HEAPARENAPASS_H

#include "neverc/DynCode/IR/StringRuntimeABI.h"
#include "neverc/DynCode/Pipeline/TargetDesc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"
#include <cstdint>

namespace neverc {
namespace dyncode {

enum class HeapFallbackMode {
  None,           ///< Arena only; OOM returns NULL.
  ExternalMalloc, ///< Keep malloc/free extern (Windows PEB walk → msvcrt).
  Mmap,           ///< Generate mmap/munmap calls (Linux/macOS syscall inline).
};

namespace HeapArenaABI {

inline constexpr uint64_t DefaultArenaThreshold = 64 * 1024;

inline constexpr uint64_t MmapHeaderSize = 16;

} // namespace HeapArenaABI

/// Rewrites malloc/free/calloc/realloc calls in dyncode to use
/// the StringRuntimePass arena allocator for small allocations and
/// an OS-level fallback for large ones.
///
/// Fallback modes:
///   - ExternalMalloc: keeps malloc/free as extern symbols resolved by
///     WinPEBImportPass to msvcrt.dll (Windows).
///   - Mmap: emits mmap/munmap calls inlined by SyscallStubPass
///     (Linux / macOS / Android).
///   - None: arena-only; OOM returns NULL.
///
/// Must run after StringRuntimePass (shares the arena) and before
/// WinPEBImportPass / SyscallStubPass (fallback symbols need resolution).
struct HeapArenaPass : public llvm::PassInfoMixin<HeapArenaPass> {
  explicit HeapArenaPass(
      uint64_t ArenaSize = StringRuntimeABI::UserArenaSize,
      HeapFallbackMode Fallback = HeapFallbackMode::None,
      DynCodeOS TargetOS = DynCodeOS::Linux,
      bool DynamicArena = true)
      : ArenaSize(ArenaSize), Fallback(Fallback), TargetOS(TargetOS),
        DynamicArena(DynamicArena) {}

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "HeapArenaPass"; }

private:
  uint64_t ArenaSize;
  HeapFallbackMode Fallback;
  DynCodeOS TargetOS;
  bool DynamicArena;
};

} // namespace dyncode
} // namespace neverc

#endif
