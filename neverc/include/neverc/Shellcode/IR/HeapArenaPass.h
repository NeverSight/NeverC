#ifndef NEVERC_SHELLCODE_HEAPARENAPASS_H
#define NEVERC_SHELLCODE_HEAPARENAPASS_H

#include "neverc/Shellcode/IR/StringRuntimeABI.h"
#include "neverc/Shellcode/Pipeline/TargetDesc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"
#include <cstdint>

namespace neverc {
namespace shellcode {

enum class HeapFallbackMode {
  None,           ///< Arena only; OOM returns NULL.
  ExternalMalloc, ///< Keep malloc/free extern (Windows PEB walk → msvcrt).
  Mmap,           ///< Generate mmap/munmap calls (Linux/macOS syscall inline).
};

namespace HeapArenaABI {

inline constexpr uint64_t DefaultArenaThreshold = 64 * 1024;

inline constexpr uint64_t MmapHeaderSize = 16;

inline constexpr int MapPrivateAnonLinux = 0x22;   // MAP_PRIVATE(0x02) | MAP_ANONYMOUS(0x20)
inline constexpr int MapPrivateAnonDarwin = 0x1002; // MAP_PRIVATE(0x02) | MAP_ANONYMOUS(0x1000)

} // namespace HeapArenaABI

/// Rewrites malloc/free/calloc/realloc calls in shellcode to use
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
      ShellcodeOS TargetOS = ShellcodeOS::Linux)
      : ArenaSize(ArenaSize), Fallback(Fallback), TargetOS(TargetOS) {}

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "HeapArenaPass"; }

private:
  uint64_t ArenaSize;
  HeapFallbackMode Fallback;
  ShellcodeOS TargetOS;
};

} // namespace shellcode
} // namespace neverc

#endif
