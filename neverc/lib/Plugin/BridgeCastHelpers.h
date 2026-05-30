#ifndef NEVERC_LIB_PLUGIN_BRIDGECASTHELPERS_H
#define NEVERC_LIB_PLUGIN_BRIDGECASTHELPERS_H

#include "neverc/Plugin/NevercPluginAPI.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace neverc {
namespace plugin {

// ===----------------------------------------------------------------------===
//  Cast helpers -- reinterpret_cast between opaque C handles and LLVM types
// ===----------------------------------------------------------------------===

static inline llvm::Module *unwrap(NevercModuleRef M) {
  return reinterpret_cast<llvm::Module *>(M);
}
static inline NevercModuleRef wrap(llvm::Module *M) {
  return reinterpret_cast<NevercModuleRef>(M);
}
static inline llvm::Value *unwrapV(NevercValueRef V) {
  return reinterpret_cast<llvm::Value *>(V);
}
static inline NevercValueRef wrapV(llvm::Value *V) {
  return reinterpret_cast<NevercValueRef>(V);
}
static inline llvm::BasicBlock *unwrapBB(NevercBasicBlockRef BB) {
  return reinterpret_cast<llvm::BasicBlock *>(BB);
}
static inline NevercBasicBlockRef wrapBB(llvm::BasicBlock *BB) {
  return reinterpret_cast<NevercBasicBlockRef>(BB);
}
static inline llvm::Type *unwrapTy(NevercTypeRef T) {
  return reinterpret_cast<llvm::Type *>(T);
}
static inline NevercTypeRef wrapTy(llvm::Type *T) {
  return reinterpret_cast<NevercTypeRef>(T);
}
static inline llvm::LLVMContext *unwrapCtx(NevercContextRef C) {
  return reinterpret_cast<llvm::LLVMContext *>(C);
}
static inline NevercContextRef wrapCtx(llvm::LLVMContext *C) {
  return reinterpret_cast<NevercContextRef>(C);
}
static inline llvm::IRBuilder<> *unwrapB(NevercBuilderRef B) {
  return reinterpret_cast<llvm::IRBuilder<> *>(B);
}
static inline NevercBuilderRef wrapB(llvm::IRBuilder<> *B) {
  return reinterpret_cast<NevercBuilderRef>(B);
}
static inline llvm::Metadata *unwrapMD(NevercMetadataRef MD) {
  return reinterpret_cast<llvm::Metadata *>(MD);
}
static inline NevercMetadataRef wrapMD(llvm::Metadata *MD) {
  return reinterpret_cast<NevercMetadataRef>(MD);
}
static inline llvm::MachineFunction *unwrapMF(NevercMachineFuncRef MF) {
  return reinterpret_cast<llvm::MachineFunction *>(MF);
}
static inline llvm::MachineBasicBlock *unwrapMBB(NevercMachineBBRef MBB) {
  return reinterpret_cast<llvm::MachineBasicBlock *>(MBB);
}
static inline NevercMachineBBRef wrapMBB(llvm::MachineBasicBlock *MBB) {
  return reinterpret_cast<NevercMachineBBRef>(MBB);
}
static inline llvm::MachineInstr *unwrapMI(NevercMachineInstrRef MI) {
  return reinterpret_cast<llvm::MachineInstr *>(MI);
}
static inline NevercMachineInstrRef wrapMI(llvm::MachineInstr *MI) {
  return reinterpret_cast<NevercMachineInstrRef>(MI);
}
static inline llvm::NamedMDNode *unwrapNMD(NevercNamedMDRef NMD) {
  return reinterpret_cast<llvm::NamedMDNode *>(NMD);
}
static inline NevercNamedMDRef wrapNMD(llvm::NamedMDNode *NMD) {
  return reinterpret_cast<NevercNamedMDRef>(NMD);
}
static inline NevercUseRef wrapUse(llvm::Use *U) {
  return reinterpret_cast<NevercUseRef>(U);
}
static inline llvm::Use *unwrapUse(NevercUseRef U) {
  return reinterpret_cast<llvm::Use *>(U);
}
static inline llvm::Comdat *unwrapComdat(NevercComdatRef C) {
  return reinterpret_cast<llvm::Comdat *>(C);
}
static inline NevercComdatRef wrapComdat(llvm::Comdat *C) {
  return reinterpret_cast<NevercComdatRef>(C);
}

// ===----------------------------------------------------------------------===
//  Numeric utility functions
// ===----------------------------------------------------------------------===

static inline bool exceedsSizeT(uint64_t V) {
#if SIZE_MAX < UINT64_MAX
  return V > SIZE_MAX;
#else
  (void)V;
  return false;
#endif
}

static inline size_t clampToSizeT(uint64_t V) {
#if SIZE_MAX < UINT64_MAX
  return V > SIZE_MAX ? SIZE_MAX : static_cast<size_t>(V);
#else
  return static_cast<size_t>(V);
#endif
}

static inline size_t checkedArraySize(uint64_t Count, uint64_t ElemSize) {
  if (LLVM_UNLIKELY(exceedsSizeT(Count) || exceedsSizeT(ElemSize)))
    return 0;
  size_t C = static_cast<size_t>(Count);
  size_t E = static_cast<size_t>(ElemSize);
  if (LLVM_UNLIKELY(C != 0 && E > SIZE_MAX / C))
    return 0;
  return C * E;
}

// ===----------------------------------------------------------------------===
//  Character classification and case conversion
// ===----------------------------------------------------------------------===

static inline bool bridgeIsWhitespace(unsigned char C) {
  return C == ' ' || C == '\t' || C == '\n' || C == '\r' || C == '\f' ||
         C == '\v';
}

static inline const char *nameStr(const char *Name) {
  return Name ? Name : "";
}

static inline unsigned char asciiToLower(unsigned char C) {
  unsigned Diff = static_cast<unsigned>(C) - 'A';
  unsigned Mask = (Diff < 26U) ? 0x20U : 0U;
  return static_cast<unsigned char>(C | Mask);
}

static inline unsigned char asciiToUpper(unsigned char C) {
  unsigned Diff = static_cast<unsigned>(C) - 'a';
  unsigned Mask = (Diff < 26U) ? 0x20U : 0U;
  return static_cast<unsigned char>(C & ~Mask);
}

// ===----------------------------------------------------------------------===
//  Arena -- shared definition so all bridge files can allocate from arenas
// ===----------------------------------------------------------------------===

struct ArenaImpl {
  llvm::BumpPtrAllocator Alloc;
};

static inline ArenaImpl *unwrapArena(NevercArenaRef A) {
  return reinterpret_cast<ArenaImpl *>(A);
}
static inline NevercArenaRef wrapArena(ArenaImpl *A) {
  return reinterpret_cast<NevercArenaRef>(A);
}

// ===----------------------------------------------------------------------===
//  Shared state -- defined in HostAPIBridge.cpp
// ===----------------------------------------------------------------------===

extern bool gShellcodeModeEnabled;

// ===----------------------------------------------------------------------===
//  Shared bridge functions -- defined in HostAPIBridge.cpp, used across files
// ===----------------------------------------------------------------------===

void *bridgeAlloc(uint64_t Size);
void *bridgeRealloc(void *Ptr, uint64_t Size);
void bridgeFree(void *Ptr);

void bridgeDiagNote(const char *Msg);
void bridgeDiagWarning(const char *Msg);
void bridgeDiagError(const char *Msg);

const char *bridgePluginGetArg(const char *Key);

int bridgeStrIEqual(const char *A, const char *B);

// ===----------------------------------------------------------------------===
//  Populate functions -- each sub-file fills its portion of the vtable
// ===----------------------------------------------------------------------===

void populateIRBridge(NevercHostAPI &API);
void populateIRBuilderBridge(NevercHostAPI &API);
void populateMIRBridge(NevercHostAPI &API);
void populateStringBridge(NevercHostAPI &API);
void populateDataStructuresBridge(NevercHostAPI &API);
void populateAnalysisBridge(NevercHostAPI &API);
void populateLinkerBridge(NevercHostAPI &API);

} // namespace plugin
} // namespace neverc

#endif
