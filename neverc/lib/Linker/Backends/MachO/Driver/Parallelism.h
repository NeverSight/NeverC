//===- Parallelism.h - Mach-O adaptive worker selection ---------*- C++ -*-===//

#ifndef NEVERC_LINKER_MACHO_DRIVER_PARALLELISM_H
#define NEVERC_LINKER_MACHO_DRIVER_PARALLELISM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBufferRef.h"

#include <cstdint>

namespace linker::macho {

namespace detail {

struct LinkInputWorkload {
  uint64_t Bytes = 0;
  uint64_t Files = 0;

  void account(uint64_t Size) {
    Bytes = llvm::SaturatingAdd<uint64_t>(Bytes, Size);
    Files = llvm::SaturatingAdd<uint64_t>(Files, 1);
  }

  void merge(LinkInputWorkload Other) {
    Bytes = llvm::SaturatingAdd<uint64_t>(Bytes, Other.Bytes);
    Files = llvm::SaturatingAdd<uint64_t>(Files, Other.Files);
  }
};

/// Per-link, constant-space accounting for inputs discovered incrementally.
/// Native and source-bitcode totals remain separate so LTO output can replace,
/// rather than double-charge, the source representation.
class IncrementalInputWorkload {
public:
  LinkInputWorkload recordNative(uint64_t Size) {
    Native.account(Size);
    return current();
  }

  LinkInputWorkload recordBitcode(uint64_t Size) {
    Bitcode.account(Size);
    return current();
  }

  LinkInputWorkload current() const {
    LinkInputWorkload Result = Native;
    Result.merge(Bitcode);
    return Result;
  }

  LinkInputWorkload materializedNative() const { return Native; }

  LinkInputWorkload
  replaceBitcodeWithNative(llvm::ArrayRef<uint64_t> NativeObjectSizes) {
    Bitcode = {};
    for (uint64_t Size : NativeObjectSizes)
      Native.account(Size);
    return Native;
  }

private:
  LinkInputWorkload Native;
  LinkInputWorkload Bitcode;
};

IncrementalInputWorkload &incrementalInputWorkload();

} // namespace detail

/// Record a newly materialized native or bitcode input, and start the
/// automatic pool when justified, before parsing that input.
void configureParallelismForMaterializedInput(llvm::MemoryBufferRef Buffer);

using ArchiveMemberParseObserverForTesting = void (*)(
    llvm::MemoryBufferRef Buffer, unsigned SelectedThreads, void *Context);
void setArchiveMemberParseObserverForTesting(
    ArchiveMemberParseObserverForTesting Observer, void *Context = nullptr);

/// Select the automatic pool from all native LTO outputs before any of those
/// buffers is parsed as an ObjFile.
void configureParallelismForLTONativeObjects(
    llvm::ArrayRef<uint64_t> ObjectSizes);

} // namespace linker::macho

#endif // NEVERC_LINKER_MACHO_DRIVER_PARALLELISM_H
