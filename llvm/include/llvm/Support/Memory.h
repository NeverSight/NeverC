//===- llvm/Support/Memory.h - Memory Support -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys::Memory class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MEMORY_H
#define LLVM_SUPPORT_MEMORY_H

#include "csupport/lmemory.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/raw_ostream.h"
#include <assert.h>

namespace llvm {

namespace sys {

class MemoryBlock {
public:
  MemoryBlock() : Address(nullptr), AllocatedSize(0) {}
  MemoryBlock(void *addr, size_t allocatedSize)
      : Address(addr), AllocatedSize(allocatedSize) {}
  void *base() const { return Address; }
  size_t allocatedSize() const { return AllocatedSize; }

private:
  void *Address;
  size_t AllocatedSize;
  unsigned Flags = 0;
  friend class Memory;
};

class Memory {
public:
  enum ProtectionFlags {
    MF_READ = 0x1000000,
    MF_WRITE = 0x2000000,
    MF_EXEC = 0x4000000,
    MF_RWE_MASK = 0x7000000,
    MF_HUGE_HINT = 0x0000001
  };

  static MemoryBlock allocateMappedMemory(size_t NumBytes,
                                          const MemoryBlock *const NearBlock,
                                          unsigned Flags, int &EC) {
    size_t outSize = 0;
    void *addr = csupport_mmap_alloc_mapped(
        NumBytes, NearBlock ? NearBlock->base() : nullptr,
        NearBlock ? NearBlock->allocatedSize() : 0, Flags, &outSize, &EC);
    if (!addr && EC)
      return MemoryBlock();
    MemoryBlock Result;
    Result.Address = addr;
    Result.AllocatedSize = outSize;
    Result.Flags = Flags;
    return Result;
  }

  static int releaseMappedMemory(MemoryBlock &M) {
    int ec = csupport_mmap_release(M.Address, M.AllocatedSize);
    if (ec == 0) {
      M.Address = nullptr;
      M.AllocatedSize = 0;
    }
    return ec;
  }

  static int protectMappedMemory(const MemoryBlock &M, unsigned Flags) {
    return csupport_mmap_protect(M.Address, M.AllocatedSize, Flags);
  }

  static void InvalidateInstructionCache(const void *Addr, size_t Len) {
    csupport_invalidate_icache(Addr, Len);
  }
};

/// Owning version of MemoryBlock.
class OwningMemoryBlock {
public:
  OwningMemoryBlock() = default;
  explicit OwningMemoryBlock(MemoryBlock M) : M(M) {}
  OwningMemoryBlock(OwningMemoryBlock &&Other) {
    M = Other.M;
    Other.M = MemoryBlock();
  }
  OwningMemoryBlock &operator=(OwningMemoryBlock &&Other) {
    M = Other.M;
    Other.M = MemoryBlock();
    return *this;
  }
  ~OwningMemoryBlock() {
    if (M.base())
      Memory::releaseMappedMemory(M);
  }
  void *base() const { return M.base(); }
  /// The size as it was allocated. This is always greater or equal to the
  /// size that was originally requested.
  size_t allocatedSize() const { return M.allocatedSize(); }
  MemoryBlock getMemoryBlock() const { return M; }
  int release() {
    int ec = 0;
    if (M.base()) {
      ec = Memory::releaseMappedMemory(M);
      M = MemoryBlock();
    }
    return ec;
  }

private:
  MemoryBlock M;
};

#ifndef NDEBUG
/// Debugging output for Memory::ProtectionFlags.
inline raw_ostream &operator<<(raw_ostream &OS,
                               const Memory::ProtectionFlags &PF) {
  assert((PF & ~(Memory::MF_READ | Memory::MF_WRITE | Memory::MF_EXEC)) == 0 &&
         "Unrecognized flags");
  return OS << (PF & Memory::MF_READ ? 'R' : '-')
            << (PF & Memory::MF_WRITE ? 'W' : '-')
            << (PF & Memory::MF_EXEC ? 'X' : '-');
}

/// Debugging output for MemoryBlock.
inline raw_ostream &operator<<(raw_ostream &OS, const MemoryBlock &MB) {
  return OS << "[ " << MB.base() << " .. "
            << (void *)((char *)MB.base() + MB.allocatedSize()) << " ] ("
            << MB.allocatedSize() << " bytes)";
}
#endif // ifndef NDEBUG
} // end namespace sys
} // end namespace llvm

#endif
