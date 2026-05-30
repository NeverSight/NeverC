#include "HostAPIBridge.h"
#include "neverc/Plugin/PluginLoader.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/xxhash.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace llvm;

namespace neverc {
namespace plugin {

static bool gShellcodeModeEnabled = false;

// ===----------------------------------------------------------------------===
//  Cast helpers
// ===----------------------------------------------------------------------===

static inline Module *unwrap(NevercModuleRef M) {
  return reinterpret_cast<Module *>(M);
}
static inline NevercModuleRef wrap(Module *M) {
  return reinterpret_cast<NevercModuleRef>(M);
}
static inline Value *unwrapV(NevercValueRef V) {
  return reinterpret_cast<Value *>(V);
}
static inline NevercValueRef wrapV(Value *V) {
  return reinterpret_cast<NevercValueRef>(V);
}
static inline BasicBlock *unwrapBB(NevercBasicBlockRef BB) {
  return reinterpret_cast<BasicBlock *>(BB);
}
static inline NevercBasicBlockRef wrapBB(BasicBlock *BB) {
  return reinterpret_cast<NevercBasicBlockRef>(BB);
}
static inline Type *unwrapTy(NevercTypeRef T) {
  return reinterpret_cast<Type *>(T);
}
static inline NevercTypeRef wrapTy(Type *T) {
  return reinterpret_cast<NevercTypeRef>(T);
}
static inline LLVMContext *unwrapCtx(NevercContextRef C) {
  return reinterpret_cast<LLVMContext *>(C);
}
static inline NevercContextRef wrapCtx(LLVMContext *C) {
  return reinterpret_cast<NevercContextRef>(C);
}
static inline IRBuilder<> *unwrapB(NevercBuilderRef B) {
  return reinterpret_cast<IRBuilder<> *>(B);
}
static inline NevercBuilderRef wrapB(IRBuilder<> *B) {
  return reinterpret_cast<NevercBuilderRef>(B);
}
static inline Metadata *unwrapMD(NevercMetadataRef MD) {
  return reinterpret_cast<Metadata *>(MD);
}
static inline NevercMetadataRef wrapMD(Metadata *MD) {
  return reinterpret_cast<NevercMetadataRef>(MD);
}
static inline MachineFunction *unwrapMF(NevercMachineFuncRef MF) {
  return reinterpret_cast<MachineFunction *>(MF);
}
static inline MachineBasicBlock *unwrapMBB(NevercMachineBBRef MBB) {
  return reinterpret_cast<MachineBasicBlock *>(MBB);
}
static inline NevercMachineBBRef wrapMBB(MachineBasicBlock *MBB) {
  return reinterpret_cast<NevercMachineBBRef>(MBB);
}
static inline MachineInstr *unwrapMI(NevercMachineInstrRef MI) {
  return reinterpret_cast<MachineInstr *>(MI);
}
static inline NevercMachineInstrRef wrapMI(MachineInstr *MI) {
  return reinterpret_cast<NevercMachineInstrRef>(MI);
}
static inline NamedMDNode *unwrapNMD(NevercNamedMDRef NMD) {
  return reinterpret_cast<NamedMDNode *>(NMD);
}
static inline NevercNamedMDRef wrapNMD(NamedMDNode *NMD) {
  return reinterpret_cast<NevercNamedMDRef>(NMD);
}

// ===----------------------------------------------------------------------===
//  Memory -- route through host process allocator
//  When NEVERC_ENABLE_MIMALLOC=ON the neverc binary links mimalloc with
//  MI_OVERRIDE=ON + whole-archive, so ::malloc/::realloc/::free are
//  transparently replaced by mi_malloc/mi_realloc/mi_free at the process
//  level.  Plugin allocations through these pointers thus go through the
//  host's mimalloc heap -- no CRT boundary crossing on Windows.
// ===----------------------------------------------------------------------===

// True on 32-bit when V exceeds SIZE_MAX.  Always false on 64-bit
// (SIZE_MAX == UINT64_MAX) -- the compiler eliminates the call entirely.
static inline bool exceedsSizeT(uint64_t V) {
#if SIZE_MAX < UINT64_MAX
  return V > SIZE_MAX;
#else
  (void)V;
  return false;
#endif
}

// Clamp uint64_t to size_t.  On 64-bit this is a no-op cast.
static inline size_t clampToSizeT(uint64_t V) {
#if SIZE_MAX < UINT64_MAX
  return V > SIZE_MAX ? SIZE_MAX : static_cast<size_t>(V);
#else
  return static_cast<size_t>(V);
#endif
}

// Overflow-safe Count * ElemSize -> size_t.  Returns 0 on overflow; the
// caller must distinguish overflow (Count!=0 && ElemSize!=0) from a genuine
// zero-size request.
static size_t checkedArraySize(uint64_t Count, uint64_t ElemSize) {
  if (LLVM_UNLIKELY(exceedsSizeT(Count) || exceedsSizeT(ElemSize)))
    return 0;
  size_t C = static_cast<size_t>(Count);
  size_t E = static_cast<size_t>(ElemSize);
  if (LLVM_UNLIKELY(C != 0 && E > SIZE_MAX / C))
    return 0;
  return C * E;
}

static void *bridgeAlloc(uint64_t Size) {
  if (LLVM_UNLIKELY(exceedsSizeT(Size)))
    return nullptr;
  return ::malloc(static_cast<size_t>(Size));
}

static void *bridgeRealloc(void *Ptr, uint64_t Size) {
  if (LLVM_UNLIKELY(exceedsSizeT(Size)))
    return nullptr;
  return ::realloc(Ptr, static_cast<size_t>(Size));
}

static void bridgeFree(void *Ptr) { ::free(Ptr); }

// ===----------------------------------------------------------------------===
//  Diagnostics -- simple stderr output for now
// ===----------------------------------------------------------------------===

static void bridgeDiagNote(const char *Msg) {
  WithColor::note(errs(), "neverc-plugin") << (Msg ? Msg : "") << "\n";
}
static void bridgeDiagWarning(const char *Msg) {
  WithColor::warning(errs(), "neverc-plugin") << (Msg ? Msg : "") << "\n";
}
static void bridgeDiagError(const char *Msg) {
  WithColor::error(errs(), "neverc-plugin") << (Msg ? Msg : "") << "\n";
}

// ===----------------------------------------------------------------------===
//  Context
// ===----------------------------------------------------------------------===

static NevercContextRef bridgeModuleGetContext(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return nullptr;
  return wrapCtx(&unwrap(M)->getContext());
}

// ===----------------------------------------------------------------------===
//  Module iteration
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeModuleGetFirstFunction(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return nullptr;
  auto *Mod = unwrap(M);
  if (LLVM_UNLIKELY(Mod->empty()))
    return nullptr;
  return wrapV(&*Mod->begin());
}

static NevercValueRef bridgeModuleGetLastFunction(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return nullptr;
  auto *Mod = unwrap(M);
  if (LLVM_UNLIKELY(Mod->empty()))
    return nullptr;
  return wrapV(&*Mod->rbegin());
}

static NevercValueRef bridgeModuleGetNextFunction(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || !Fn->getParent()))
    return nullptr;
  auto It = std::next(Fn->getIterator());
  if (It == Fn->getParent()->end())
    return nullptr;
  return wrapV(&*It);
}

// Defined function iterators -- skip declarations at the C++ level so the
// plugin pays one vtable call per defined function instead of two (GetNext +
// IsDeclaration).  For modules with many declarations (system headers, libc)
// this eliminates the per-declaration vtable overhead entirely.

static NevercValueRef bridgeModuleGetFirstDefinedFunction(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return nullptr;
  for (auto &F : *unwrap(M))
    if (!F.isDeclaration())
      return wrapV(&F);
  return nullptr;
}

static NevercValueRef
bridgeModuleGetNextDefinedFunction(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || !Fn->getParent()))
    return nullptr;
  auto It = std::next(Fn->getIterator());
  auto End = Fn->getParent()->end();
  while (It != End) {
    if (!It->isDeclaration())
      return wrapV(&*It);
    ++It;
  }
  return nullptr;
}

static NevercValueRef bridgeModuleGetNamedFunction(NevercModuleRef M,
                                                   const char *Name) {
  if (LLVM_UNLIKELY(!M || !Name))
    return nullptr;
  auto *Fn = unwrap(M)->getFunction(Name);
  return Fn ? wrapV(Fn) : nullptr;
}

static NevercValueRef bridgeModuleGetFirstGlobal(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return nullptr;
  auto *Mod = unwrap(M);
  if (LLVM_UNLIKELY(Mod->global_empty()))
    return nullptr;
  return wrapV(&*Mod->global_begin());
}

static NevercValueRef bridgeModuleGetNextGlobal(NevercValueRef G) {
  if (LLVM_UNLIKELY(!G))
    return nullptr;
  auto *GV = dyn_cast<GlobalVariable>(unwrapV(G));
  if (LLVM_UNLIKELY(!GV || !GV->getParent()))
    return nullptr;
  auto It = std::next(GV->getIterator());
  if (It == GV->getParent()->global_end())
    return nullptr;
  return wrapV(&*It);
}

static NevercValueRef bridgeModuleAddFunction(NevercModuleRef M,
                                              const char *Name,
                                              NevercTypeRef FnTy) {
  if (LLVM_UNLIKELY(!M || !Name || !FnTy))
    return nullptr;
  auto *Mod = unwrap(M);
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  if (LLVM_UNLIKELY(!FT))
    return nullptr;
  auto *Fn = Function::Create(FT, GlobalValue::ExternalLinkage, Name, Mod);
  if (gShellcodeModeEnabled)
    Fn->addFnAttr("no-stack-arg-probe");
  return wrapV(Fn);
}

// ===----------------------------------------------------------------------===
//  Value ops
// ===----------------------------------------------------------------------===

static const char *bridgeValueGetName(NevercValueRef V) {
  if (LLVM_UNLIKELY(!V))
    return "";
  auto Name = unwrapV(V)->getName();
  return Name.data() ? Name.data() : "";
}

static NevercTypeRef bridgeValueGetType(NevercValueRef V) {
  if (LLVM_UNLIKELY(!V))
    return nullptr;
  return wrapTy(unwrapV(V)->getType());
}

static void bridgeValueReplaceAllUsesWith(NevercValueRef Old,
                                          NevercValueRef New) {
  if (LLVM_UNLIKELY(!Old || !New))
    return;
  Value *O = unwrapV(Old);
  Value *N = unwrapV(New);
  if (O->getType() != N->getType()) {
    WithColor::warning(errs(), "neverc-plugin")
        << "ValueReplaceAllUsesWith: type mismatch; ignoring\n";
    return;
  }
  O->replaceAllUsesWith(N);
}

static void bridgeValueEraseFromParent(NevercValueRef V) {
  if (LLVM_UNLIKELY(!V))
    return;
  Value *Val = unwrapV(V);
  if (!Val->use_empty()) {
    WithColor::warning(errs(), "neverc-plugin")
        << "erasing value '" << Val->getName()
        << "' that still has uses; replacing with poison\n";
    Val->replaceAllUsesWith(PoisonValue::get(Val->getType()));
  }
  if (auto *I = dyn_cast<Instruction>(Val))
    I->eraseFromParent();
  else if (auto *GV = dyn_cast<GlobalValue>(Val))
    GV->eraseFromParent();
}

// ===----------------------------------------------------------------------===
//  Function ops
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef bridgeFunctionGetFirstBB(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->empty()))
    return nullptr;
  return wrapBB(&Fn->front());
}

static NevercBasicBlockRef bridgeFunctionGetLastBB(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->empty()))
    return nullptr;
  return wrapBB(&Fn->back());
}

static NevercBasicBlockRef bridgeFunctionGetNextBB(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return nullptr;
  auto *Block = unwrapBB(BB);
  if (LLVM_UNLIKELY(!Block->getParent()))
    return nullptr;
  auto It = std::next(Block->getIterator());
  if (It == Block->getParent()->end())
    return nullptr;
  return wrapBB(&*It);
}

static unsigned bridgeFunctionGetArgCount(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  return Fn ? Fn->arg_size() : 0;
}

static NevercValueRef bridgeFunctionGetArg(NevercValueRef F, unsigned Idx) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Idx >= Fn->arg_size()))
    return nullptr;
  return wrapV(Fn->getArg(Idx));
}

static int bridgeFunctionIsDeclaration(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return 1;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  return !Fn || Fn->isDeclaration();
}

// ===----------------------------------------------------------------------===
//  BasicBlock ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBBGetFirstInst(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return nullptr;
  auto *Block = unwrapBB(BB);
  if (LLVM_UNLIKELY(Block->empty()))
    return nullptr;
  return wrapV(&Block->front());
}

static NevercValueRef bridgeBBGetLastInst(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return nullptr;
  auto *Block = unwrapBB(BB);
  if (LLVM_UNLIKELY(Block->empty()))
    return nullptr;
  return wrapV(&Block->back());
}

static NevercValueRef bridgeBBGetNextInst(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (LLVM_UNLIKELY(!Inst || !Inst->getParent()))
    return nullptr;
  auto It = std::next(Inst->getIterator());
  if (It == Inst->getParent()->end())
    return nullptr;
  return wrapV(&*It);
}

static NevercValueRef bridgeBBGetTerminator(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return nullptr;
  auto *T = unwrapBB(BB)->getTerminator();
  return T ? wrapV(T) : nullptr;
}

static NevercValueRef bridgeBBGetParentFunction(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return nullptr;
  return wrapV(unwrapBB(BB)->getParent());
}

static NevercBasicBlockRef bridgeBBCreate(NevercContextRef C, const char *Name,
                                          NevercValueRef F) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  auto *Fn = F ? dyn_cast<Function>(unwrapV(F)) : nullptr;
  return wrapBB(BasicBlock::Create(*unwrapCtx(C), Name ? Name : "", Fn));
}

// ===----------------------------------------------------------------------===
//  Instruction ops
// ===----------------------------------------------------------------------===

static unsigned bridgeInstGetOpcode(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return Inst ? Inst->getOpcode() : 0;
}

static const char *bridgeInstGetOpcodeName(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return "";
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (LLVM_UNLIKELY(!Inst))
    return "";
  const char *Name = Inst->getOpcodeName();
  if (LLVM_UNLIKELY(!Name || Name[0] == '<'))
    return "";
  return Name;
}

static const char *bridgeInstOpcodeToName(unsigned Opcode) {
  const char *Name = Instruction::getOpcodeName(Opcode);
  if (LLVM_UNLIKELY(!Name || Name[0] == '<'))
    return "";
  return Name;
}

static unsigned bridgeInstGetNumOperands(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return Inst ? Inst->getNumOperands() : 0;
}

static NevercValueRef bridgeInstGetOperand(NevercValueRef I, unsigned Idx) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (LLVM_UNLIKELY(!Inst || Idx >= Inst->getNumOperands()))
    return nullptr;
  return wrapV(Inst->getOperand(Idx));
}

static void bridgeInstSetOperand(NevercValueRef I, unsigned Idx,
                                 NevercValueRef V) {
  if (LLVM_UNLIKELY(!I || !V))
    return;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (LLVM_UNLIKELY(!Inst || Idx >= Inst->getNumOperands()))
    return;
  Inst->setOperand(Idx, unwrapV(V));
}

static void bridgeInstEraseFromParent(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (LLVM_UNLIKELY(!Inst))
    return;
  if (!Inst->use_empty()) {
    WithColor::warning(errs(), "neverc-plugin")
        << "erasing instruction that still has uses; replacing with poison\n";
    if (!Inst->getType()->isVoidTy())
      Inst->replaceAllUsesWith(PoisonValue::get(Inst->getType()));
  }
  Inst->eraseFromParent();
}

static NevercValueRef bridgeInstClone(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return Inst ? wrapV(Inst->clone()) : nullptr;
}

static NevercBasicBlockRef bridgeInstGetParentBB(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return Inst ? wrapBB(Inst->getParent()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  Type ops
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeTypeGetInt1(NevercContextRef C) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapTy(Type::getInt1Ty(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetInt8(NevercContextRef C) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapTy(Type::getInt8Ty(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetInt16(NevercContextRef C) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapTy(Type::getInt16Ty(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetInt32(NevercContextRef C) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapTy(Type::getInt32Ty(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetInt64(NevercContextRef C) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapTy(Type::getInt64Ty(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetIntN(NevercContextRef C, unsigned NumBits) {
  if (LLVM_UNLIKELY(!C || NumBits == 0))
    return nullptr;
  return wrapTy(Type::getIntNTy(*unwrapCtx(C), NumBits));
}
static NevercTypeRef bridgeTypeGetFloat(NevercContextRef C) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapTy(Type::getFloatTy(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetDouble(NevercContextRef C) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapTy(Type::getDoubleTy(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetVoid(NevercContextRef C) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapTy(Type::getVoidTy(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetPtr(NevercContextRef C) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapTy(PointerType::get(*unwrapCtx(C), 0));
}
static NevercTypeRef bridgeTypeGetArray(NevercTypeRef ElemTy, uint64_t Count) {
  if (LLVM_UNLIKELY(!ElemTy))
    return nullptr;
  return wrapTy(ArrayType::get(unwrapTy(ElemTy), Count));
}
static NevercTypeRef bridgeTypeGetFunction(NevercTypeRef RetTy,
                                           NevercTypeRef *ParamTys,
                                           unsigned ParamCount, int IsVarArg) {
  if (LLVM_UNLIKELY(!RetTy || (ParamCount > 0 && !ParamTys)))
    return nullptr;
  SmallVector<Type *, 8> Params;
  for (unsigned I = 0; I < ParamCount; ++I) {
    if (LLVM_UNLIKELY(!ParamTys[I]))
      return nullptr;
    Params.push_back(unwrapTy(ParamTys[I]));
  }
  return wrapTy(FunctionType::get(unwrapTy(RetTy), Params, IsVarArg != 0));
}
static int bridgeTypeIsInteger(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T))
    return 0;
  return unwrapTy(T)->isIntegerTy();
}
static int bridgeTypeIsFloat(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T))
    return 0;
  return unwrapTy(T)->isFloatingPointTy();
}
static int bridgeTypeIsPointer(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T))
    return 0;
  return unwrapTy(T)->isPointerTy();
}
static int bridgeTypeIsVoid(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T))
    return 0;
  return unwrapTy(T)->isVoidTy();
}
static unsigned bridgeTypeGetIntWidth(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T || !unwrapTy(T)->isIntegerTy()))
    return 0;
  return unwrapTy(T)->getIntegerBitWidth();
}

// ===----------------------------------------------------------------------===
//  Constant creation
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeConstInt(NevercTypeRef IntTy, uint64_t Val,
                                     int SignExtend) {
  if (LLVM_UNLIKELY(!IntTy))
    return nullptr;
  Type *T = unwrapTy(IntTy);
  if (LLVM_UNLIKELY(!T->isIntegerTy()))
    return nullptr;
  return wrapV(ConstantInt::get(T, Val, SignExtend != 0));
}

static NevercValueRef bridgeConstFloat(NevercTypeRef FloatTy, double Val) {
  if (LLVM_UNLIKELY(!FloatTy))
    return nullptr;
  Type *T = unwrapTy(FloatTy);
  if (LLVM_UNLIKELY(!T->isFloatingPointTy()))
    return nullptr;
  return wrapV(ConstantFP::get(T, Val));
}

static NevercValueRef bridgeConstNull(NevercTypeRef Ty) {
  if (LLVM_UNLIKELY(!Ty))
    return nullptr;
  return wrapV(Constant::getNullValue(unwrapTy(Ty)));
}

static NevercValueRef bridgeConstUndef(NevercTypeRef Ty) {
  if (LLVM_UNLIKELY(!Ty))
    return nullptr;
  return wrapV(UndefValue::get(unwrapTy(Ty)));
}

static NevercValueRef bridgeConstString(NevercContextRef C, const char *Str,
                                        uint32_t Len,
                                        int DontNullTerminate) {
  if (LLVM_UNLIKELY(!C || !Str))
    return nullptr;
  return wrapV(ConstantDataArray::getString(
      *unwrapCtx(C), StringRef(Str, Len), DontNullTerminate == 0));
}

// ===----------------------------------------------------------------------===
//  IRBuilder
// ===----------------------------------------------------------------------===

static NevercBuilderRef bridgeBuilderCreate(NevercContextRef C) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapB(new IRBuilder<>(*unwrapCtx(C)));
}

static void bridgeBuilderDispose(NevercBuilderRef B) {
  if (B)
    delete unwrapB(B);
}

static void bridgeBuilderSetInsertPoint(NevercBuilderRef B,
                                        NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!B || !BB))
    return;
  unwrapB(B)->SetInsertPoint(unwrapBB(BB));
}

static void bridgeBuilderSetInsertPointBefore(NevercBuilderRef B,
                                              NevercValueRef Inst) {
  if (LLVM_UNLIKELY(!B || !Inst))
    return;
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  if (LLVM_UNLIKELY(!I || !I->getParent()))
    return;
  unwrapB(B)->SetInsertPoint(I->getIterator());
}

static NevercValueRef bridgeBuildAdd(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateAdd(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildSub(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateSub(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildMul(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateMul(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildUDiv(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateUDiv(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildSDiv(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateSDiv(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildAnd(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateAnd(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildOr(NevercBuilderRef B, NevercValueRef LHS,
                                    NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateOr(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildXor(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateXor(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildShl(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateShl(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildLShr(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateLShr(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildAShr(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateAShr(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildICmp(NevercBuilderRef B, unsigned Pred,
                                      NevercValueRef LHS, NevercValueRef RHS,
                                      const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateICmp(static_cast<CmpInst::Predicate>(Pred),
                                      unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildFCmp(NevercBuilderRef B, unsigned Pred,
                                      NevercValueRef LHS, NevercValueRef RHS,
                                      const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(unwrapB(B)->CreateFCmp(static_cast<CmpInst::Predicate>(Pred),
                                      unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildBr(NevercBuilderRef B,
                                    NevercBasicBlockRef Dest) {
  if (LLVM_UNLIKELY(!B || !Dest))
    return nullptr;
  return wrapV(unwrapB(B)->CreateBr(unwrapBB(Dest)));
}
static NevercValueRef bridgeBuildCondBr(NevercBuilderRef B,
                                        NevercValueRef Cond,
                                        NevercBasicBlockRef Then,
                                        NevercBasicBlockRef Else) {
  if (LLVM_UNLIKELY(!B || !Cond || !Then || !Else))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateCondBr(unwrapV(Cond), unwrapBB(Then), unwrapBB(Else)));
}
static NevercValueRef bridgeBuildRet(NevercBuilderRef B, NevercValueRef V) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateRet(unwrapV(V)));
}
static NevercValueRef bridgeBuildRetVoid(NevercBuilderRef B) {
  if (LLVM_UNLIKELY(!B))
    return nullptr;
  return wrapV(unwrapB(B)->CreateRetVoid());
}

static NevercValueRef bridgeBuildCall(NevercBuilderRef B, NevercTypeRef FnTy,
                                      NevercValueRef Fn, NevercValueRef *Args,
                                      unsigned ArgCount, const char *Name) {
  if (LLVM_UNLIKELY(!B || !FnTy || !Fn || (ArgCount > 0 && !Args)))
    return nullptr;
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  if (LLVM_UNLIKELY(!FT))
    return nullptr;
  SmallVector<Value *, 8> ArgVec;
  for (unsigned I = 0; I < ArgCount; ++I) {
    if (LLVM_UNLIKELY(!Args[I]))
      return nullptr;
    ArgVec.push_back(unwrapV(Args[I]));
  }
  return wrapV(unwrapB(B)->CreateCall(FT, unwrapV(Fn), ArgVec, Name ? Name : ""));
}

static NevercValueRef bridgeBuildGEP(NevercBuilderRef B, NevercTypeRef Ty,
                                     NevercValueRef Ptr,
                                     NevercValueRef *Indices,
                                     unsigned NumIndices, const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty || !Ptr || (NumIndices > 0 && !Indices)))
    return nullptr;
  SmallVector<Value *, 4> Idxs;
  for (unsigned I = 0; I < NumIndices; ++I) {
    if (LLVM_UNLIKELY(!Indices[I]))
      return nullptr;
    Idxs.push_back(unwrapV(Indices[I]));
  }
  return wrapV(
      unwrapB(B)->CreateGEP(unwrapTy(Ty), unwrapV(Ptr), Idxs, Name ? Name : ""));
}

static NevercValueRef bridgeBuildLoad(NevercBuilderRef B, NevercTypeRef Ty,
                                      NevercValueRef Ptr, const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty || !Ptr))
    return nullptr;
  return wrapV(unwrapB(B)->CreateLoad(unwrapTy(Ty), unwrapV(Ptr), Name ? Name : ""));
}

static NevercValueRef bridgeBuildStore(NevercBuilderRef B, NevercValueRef Val,
                                       NevercValueRef Ptr) {
  if (LLVM_UNLIKELY(!B || !Val || !Ptr))
    return nullptr;
  return wrapV(unwrapB(B)->CreateStore(unwrapV(Val), unwrapV(Ptr)));
}

static NevercValueRef bridgeBuildAlloca(NevercBuilderRef B, NevercTypeRef Ty,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty))
    return nullptr;
  return wrapV(unwrapB(B)->CreateAlloca(unwrapTy(Ty), nullptr, Name ? Name : ""));
}

static NevercValueRef bridgeBuildBitCast(NevercBuilderRef B, NevercValueRef V,
                                         NevercTypeRef DestTy,
                                         const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(unwrapB(B)->CreateBitCast(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}
static NevercValueRef bridgeBuildIntToPtr(NevercBuilderRef B, NevercValueRef V,
                                          NevercTypeRef DestTy,
                                          const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateIntToPtr(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}
static NevercValueRef bridgeBuildPtrToInt(NevercBuilderRef B, NevercValueRef V,
                                          NevercTypeRef DestTy,
                                          const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreatePtrToInt(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}
static NevercValueRef bridgeBuildZExt(NevercBuilderRef B, NevercValueRef V,
                                      NevercTypeRef DestTy, const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(unwrapB(B)->CreateZExt(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}
static NevercValueRef bridgeBuildSExt(NevercBuilderRef B, NevercValueRef V,
                                      NevercTypeRef DestTy, const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(unwrapB(B)->CreateSExt(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}
static NevercValueRef bridgeBuildTrunc(NevercBuilderRef B, NevercValueRef V,
                                       NevercTypeRef DestTy,
                                       const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(unwrapB(B)->CreateTrunc(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

static NevercValueRef bridgeBuildSelect(NevercBuilderRef B, NevercValueRef Cond,
                                        NevercValueRef Then,
                                        NevercValueRef Else,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !Cond || !Then || !Else))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSelect(unwrapV(Cond), unwrapV(Then), unwrapV(Else), Name ? Name : ""));
}

static NevercValueRef bridgeBuildPhi(NevercBuilderRef B, NevercTypeRef Ty,
                                     const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty))
    return nullptr;
  return wrapV(unwrapB(B)->CreatePHI(unwrapTy(Ty), 2, Name ? Name : ""));
}

static void bridgePhiAddIncoming(NevercValueRef Phi, NevercValueRef *Values,
                                 NevercBasicBlockRef *Blocks, unsigned Count) {
  if (LLVM_UNLIKELY(!Phi || !Values || !Blocks || Count == 0))
    return;
  auto *PN = dyn_cast<PHINode>(unwrapV(Phi));
  if (LLVM_UNLIKELY(!PN))
    return;
  for (unsigned I = 0; I < Count; ++I) {
    if (LLVM_UNLIKELY(!Values[I] || !Blocks[I]))
      continue;
    PN->addIncoming(unwrapV(Values[I]), unwrapBB(Blocks[I]));
  }
}

// ===----------------------------------------------------------------------===
//  Metadata
// ===----------------------------------------------------------------------===

static NevercMetadataRef bridgeMDStringCreate(NevercContextRef C,
                                              const char *Str, uint32_t Len) {
  if (LLVM_UNLIKELY(!C || !Str))
    return nullptr;
  return wrapMD(MDString::get(*unwrapCtx(C), StringRef(Str, Len)));
}

static NevercMetadataRef bridgeMDNodeCreate(NevercContextRef C,
                                            NevercMetadataRef *Vals,
                                            unsigned Count) {
  if (LLVM_UNLIKELY(!C || (Count > 0 && !Vals)))
    return nullptr;
  SmallVector<Metadata *, 4> Ops;
  for (unsigned I = 0; I < Count; ++I) {
    if (LLVM_UNLIKELY(!Vals[I]))
      return nullptr;
    Ops.push_back(unwrapMD(Vals[I]));
  }
  return wrapMD(MDNode::get(*unwrapCtx(C), Ops));
}

static void bridgeInstSetMetadata(NevercValueRef Inst, unsigned KindID,
                                  NevercMetadataRef MD) {
  if (LLVM_UNLIKELY(!Inst || !MD))
    return;
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  auto *Node = dyn_cast<MDNode>(unwrapMD(MD));
  if (I && Node)
    I->setMetadata(KindID, Node);
}

static NevercMetadataRef bridgeInstGetMetadata(NevercValueRef Inst,
                                               unsigned KindID) {
  if (LLVM_UNLIKELY(!Inst))
    return nullptr;
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *N = I->getMetadata(KindID);
  return N ? wrapMD(N) : nullptr;
}

static unsigned bridgeMDKindGetID(NevercContextRef C, const char *Name) {
  if (LLVM_UNLIKELY(!C || !Name))
    return 0;
  return unwrapCtx(C)->getMDKindID(Name);
}

// ===----------------------------------------------------------------------===
//  MIR ops
// ===----------------------------------------------------------------------===

static NevercMachineBBRef bridgeMFuncGetFirstBB(NevercMachineFuncRef MF) {
  if (LLVM_UNLIKELY(!MF))
    return nullptr;
  auto *Func = unwrapMF(MF);
  if (LLVM_UNLIKELY(Func->empty()))
    return nullptr;
  return wrapMBB(&Func->front());
}

static NevercMachineBBRef bridgeMFuncGetNextBB(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *Block = unwrapMBB(MBB);
  if (LLVM_UNLIKELY(!Block->getParent()))
    return nullptr;
  auto It = std::next(Block->getIterator());
  if (It == Block->getParent()->end())
    return nullptr;
  return wrapMBB(&*It);
}

static NevercMachineInstrRef bridgeMBBGetFirstInst(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *Block = unwrapMBB(MBB);
  if (LLVM_UNLIKELY(Block->empty()))
    return nullptr;
  return wrapMI(&Block->front());
}

static NevercMachineInstrRef bridgeMBBGetNextInst(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return nullptr;
  auto *Inst = unwrapMI(MI);
  if (LLVM_UNLIKELY(!Inst->getParent()))
    return nullptr;
  auto It = std::next(Inst->getIterator());
  if (It == Inst->getParent()->end())
    return nullptr;
  return wrapMI(&*It);
}

static unsigned bridgeMInstGetOpcode(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->getOpcode();
}

static unsigned bridgeMInstGetNumOperands(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->getNumOperands();
}

static void bridgeMInstEraseFromParent(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return;
  unwrapMI(MI)->eraseFromParent();
}

// ===----------------------------------------------------------------------===
//  Module info
// ===----------------------------------------------------------------------===

static const char *bridgeModuleGetDataLayout(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return "";
  return unwrap(M)->getDataLayoutStr().c_str();
}

static const char *bridgeModuleGetTargetTriple(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return "";
  return unwrap(M)->getTargetTriple().c_str();
}

static void bridgeModulePrint(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return;
  unwrap(M)->print(errs(), nullptr);
}

// ===----------------------------------------------------------------------===
//  Value debug
// ===----------------------------------------------------------------------===

static void bridgeValueDump(NevercValueRef V) {
  if (LLVM_UNLIKELY(!V))
    return;
  unwrapV(V)->print(errs());
  errs() << "\n";
}

static unsigned bridgeValueGetNumUses(NevercValueRef V) {
  if (LLVM_UNLIKELY(!V))
    return 0;
  return unwrapV(V)->getNumUses();
}

// ===----------------------------------------------------------------------===
//  Function linkage & calling convention
// ===----------------------------------------------------------------------===

static unsigned bridgeFunctionGetLinkage(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  return Fn ? static_cast<unsigned>(Fn->getLinkage()) : 0;
}

static void bridgeFunctionSetLinkage(NevercValueRef F, unsigned Linkage) {
  if (LLVM_UNLIKELY(!F))
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (Fn)
    Fn->setLinkage(static_cast<GlobalValue::LinkageTypes>(Linkage));
}

static unsigned bridgeFunctionGetCallingConv(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  return Fn ? static_cast<unsigned>(Fn->getCallingConv()) : 0;
}

static void bridgeFunctionSetCallingConv(NevercValueRef F, unsigned CC) {
  if (LLVM_UNLIKELY(!F))
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (Fn)
    Fn->setCallingConv(static_cast<CallingConv::ID>(CC));
}

// ===----------------------------------------------------------------------===
//  Instruction positioning
// ===----------------------------------------------------------------------===

static void bridgeInstMoveBefore(NevercValueRef I, NevercValueRef Before) {
  if (LLVM_UNLIKELY(!I || !Before))
    return;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  auto *BeforeInst = dyn_cast<Instruction>(unwrapV(Before));
  if (LLVM_UNLIKELY(!Inst || !BeforeInst || !BeforeInst->getParent()))
    return;
  Inst->moveBefore(BeforeInst);
}

// ===----------------------------------------------------------------------===
//  Module set ops
// ===----------------------------------------------------------------------===

static void bridgeModuleSetDataLayout(NevercModuleRef M, const char *DL) {
  if (LLVM_UNLIKELY(!M || !DL))
    return;
  unwrap(M)->setDataLayout(DL);
}

static void bridgeModuleSetTargetTriple(NevercModuleRef M, const char *Triple) {
  if (LLVM_UNLIKELY(!M || !Triple))
    return;
  unwrap(M)->setTargetTriple(Triple);
}

// ===----------------------------------------------------------------------===
//  Value user iteration
// ===----------------------------------------------------------------------===

static inline NevercUseRef wrapUse(Use *U) {
  return reinterpret_cast<NevercUseRef>(U);
}
static inline Use *unwrapUse(NevercUseRef U) {
  return reinterpret_cast<Use *>(U);
}

static NevercUseRef bridgeValueGetFirstUse(NevercValueRef V) {
  if (LLVM_UNLIKELY(!V))
    return nullptr;
  auto *Val = unwrapV(V);
  if (Val->use_empty())
    return nullptr;
  return wrapUse(&*Val->use_begin());
}

static NevercUseRef bridgeUseGetNext(NevercUseRef U) {
  if (LLVM_UNLIKELY(!U))
    return nullptr;
  Use *Next = unwrapUse(U)->getNext();
  return Next ? wrapUse(Next) : nullptr;
}

static NevercValueRef bridgeUseGetUser(NevercUseRef U) {
  if (LLVM_UNLIKELY(!U))
    return nullptr;
  return wrapV(unwrapUse(U)->getUser());
}

// ===----------------------------------------------------------------------===
//  Function string attributes
// ===----------------------------------------------------------------------===

static void bridgeFunctionAddStringAttr(NevercValueRef F, const char *Kind,
                                        const char *Val) {
  if (LLVM_UNLIKELY(!F || !Kind))
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return;
  Fn->addFnAttr(Kind, Val ? Val : "");
}

static int bridgeFunctionHasStringAttr(NevercValueRef F, const char *Kind) {
  if (LLVM_UNLIKELY(!F || !Kind))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return 0;
  return Fn->hasFnAttribute(Kind);
}

static const char *bridgeFunctionGetStringAttr(NevercValueRef F,
                                               const char *Kind) {
  if (LLVM_UNLIKELY(!F || !Kind))
    return "";
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || !Fn->hasFnAttribute(Kind)))
    return "";
  auto Val = Fn->getFnAttribute(Kind).getValueAsString();
  return Val.data() ? Val.data() : "";
}

static void bridgeFunctionRemoveStringAttr(NevercValueRef F,
                                           const char *Kind) {
  if (LLVM_UNLIKELY(!F || !Kind))
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (Fn)
    Fn->removeFnAttr(Kind);
}

// ===----------------------------------------------------------------------===
//  BasicBlock removal
// ===----------------------------------------------------------------------===

static void bridgeBBRemoveFromParent(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return;
  unwrapBB(BB)->removeFromParent();
}

static void bridgeBBEraseFromParent(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return;
  BasicBlock *Block = unwrapBB(BB);
  if (!Block->use_empty()) {
    WithColor::warning(errs(), "neverc-plugin")
        << "erasing BB '" << Block->getName()
        << "' that still has predecessor references; "
           "replacing with unreachable\n";
    auto *Replacement =
        BasicBlock::Create(Block->getContext(), "", Block->getParent());
    new UnreachableInst(Block->getContext(), Replacement);
    Block->replaceAllUsesWith(Replacement);
  }
  Block->eraseFromParent();
}

// ===----------------------------------------------------------------------===
//  Value kind queries
// ===----------------------------------------------------------------------===

static int bridgeValueIsFunction(NevercValueRef V) {
  return V && isa<Function>(unwrapV(V));
}
static int bridgeValueIsGlobalVariable(NevercValueRef V) {
  return V && isa<GlobalVariable>(unwrapV(V));
}
static int bridgeValueIsInstruction(NevercValueRef V) {
  return V && isa<Instruction>(unwrapV(V));
}
static int bridgeValueIsConstant(NevercValueRef V) {
  return V && isa<Constant>(unwrapV(V));
}
static int bridgeValueIsArgument(NevercValueRef V) {
  return V && isa<Argument>(unwrapV(V));
}
static int bridgeValueIsBasicBlock(NevercValueRef V) {
  return V && isa<BasicBlock>(unwrapV(V));
}

// ===----------------------------------------------------------------------===
//  Struct type ops
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeTypeGetStruct(NevercContextRef C,
                                         NevercTypeRef *ElemTys,
                                         unsigned ElemCount, int IsPacked) {
  if (LLVM_UNLIKELY(!C || (ElemCount > 0 && !ElemTys)))
    return nullptr;
  SmallVector<Type *, 8> Elems;
  for (unsigned I = 0; I < ElemCount; ++I) {
    if (LLVM_UNLIKELY(!ElemTys[I]))
      return nullptr;
    Elems.push_back(unwrapTy(ElemTys[I]));
  }
  return wrapTy(StructType::get(*unwrapCtx(C), Elems, IsPacked != 0));
}

static NevercTypeRef bridgeTypeGetNamedStruct(NevercContextRef C,
                                              const char *Name) {
  if (LLVM_UNLIKELY(!C || !Name))
    return nullptr;
  return wrapTy(StructType::create(*unwrapCtx(C), Name));
}

static void bridgeStructSetBody(NevercTypeRef StructTy,
                                NevercTypeRef *ElemTys, unsigned ElemCount,
                                int IsPacked) {
  if (LLVM_UNLIKELY(!StructTy || (ElemCount > 0 && !ElemTys)))
    return;
  auto *ST = dyn_cast<StructType>(unwrapTy(StructTy));
  if (LLVM_UNLIKELY(!ST))
    return;
  if (!ST->isOpaque()) {
    WithColor::warning(errs(), "neverc-plugin")
        << "StructSetBody called on already-defined struct '"
        << (ST->hasName() ? ST->getName() : "<anonymous>") << "'; ignoring\n";
    return;
  }
  SmallVector<Type *, 8> Elems;
  for (unsigned I = 0; I < ElemCount; ++I) {
    if (LLVM_UNLIKELY(!ElemTys[I]))
      return;
    Elems.push_back(unwrapTy(ElemTys[I]));
  }
  ST->setBody(Elems, IsPacked != 0);
}

static unsigned bridgeStructGetNumElements(NevercTypeRef StructTy) {
  if (LLVM_UNLIKELY(!StructTy))
    return 0;
  auto *ST = dyn_cast<StructType>(unwrapTy(StructTy));
  return ST ? ST->getNumElements() : 0;
}

static NevercTypeRef bridgeStructGetElementType(NevercTypeRef StructTy,
                                                unsigned Idx) {
  if (LLVM_UNLIKELY(!StructTy))
    return nullptr;
  auto *ST = dyn_cast<StructType>(unwrapTy(StructTy));
  if (LLVM_UNLIKELY(!ST || Idx >= ST->getNumElements()))
    return nullptr;
  return wrapTy(ST->getElementType(Idx));
}

static int bridgeTypeIsStruct(NevercTypeRef T) {
  return T && unwrapTy(T)->isStructTy();
}

// ===----------------------------------------------------------------------===
//  GlobalVariable ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeModuleAddGlobal(NevercModuleRef M,
                                            NevercTypeRef Ty, int IsConstant,
                                            const char *Name) {
  if (LLVM_UNLIKELY(!M || !Ty || !Name))
    return nullptr;
  auto *GV = new GlobalVariable(*unwrap(M), unwrapTy(Ty), IsConstant != 0,
                                GlobalValue::ExternalLinkage, nullptr, Name);
  return wrapV(GV);
}

static NevercValueRef bridgeGlobalGetInitializer(NevercValueRef GV) {
  if (LLVM_UNLIKELY(!GV))
    return nullptr;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (LLVM_UNLIKELY(!G || !G->hasInitializer()))
    return nullptr;
  return wrapV(G->getInitializer());
}

static void bridgeGlobalSetInitializer(NevercValueRef GV,
                                       NevercValueRef Init) {
  if (LLVM_UNLIKELY(!GV))
    return;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (LLVM_UNLIKELY(!G))
    return;
  if (!Init) {
    G->setInitializer(nullptr);
    return;
  }
  auto *C = dyn_cast<Constant>(unwrapV(Init));
  if (LLVM_UNLIKELY(!C)) {
    WithColor::warning(errs(), "neverc-plugin")
        << "GlobalSetInitializer called with non-constant value; ignoring\n";
    return;
  }
  G->setInitializer(C);
}

static int bridgeGlobalHasInitializer(NevercValueRef GV) {
  if (LLVM_UNLIKELY(!GV))
    return 0;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  return G && G->hasInitializer();
}

static int bridgeGlobalIsConstant(NevercValueRef GV) {
  if (LLVM_UNLIKELY(!GV))
    return 0;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  return G && G->isConstant();
}

static void bridgeGlobalSetConstant(NevercValueRef GV, int IsConstant) {
  if (LLVM_UNLIKELY(!GV))
    return;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (G)
    G->setConstant(IsConstant != 0);
}

// ===----------------------------------------------------------------------===
//  Module named-global lookup
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeModuleGetNamedGlobal(NevercModuleRef M,
                                                 const char *Name) {
  if (LLVM_UNLIKELY(!M || !Name))
    return nullptr;
  auto *GV = unwrap(M)->getGlobalVariable(Name, true);
  return GV ? wrapV(GV) : nullptr;
}

// ===----------------------------------------------------------------------===
//  Instruction insertion
// ===----------------------------------------------------------------------===

static void bridgeInstInsertBefore(NevercValueRef Inst,
                                   NevercValueRef Before) {
  if (LLVM_UNLIKELY(!Inst || !Before))
    return;
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  auto *BeforeI = dyn_cast<Instruction>(unwrapV(Before));
  if (LLVM_UNLIKELY(!I || !BeforeI || !BeforeI->getParent()))
    return;
  I->insertBefore(BeforeI);
}

static void bridgeInstInsertAfter(NevercValueRef Inst,
                                  NevercValueRef After) {
  if (LLVM_UNLIKELY(!Inst || !After))
    return;
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  auto *AfterI = dyn_cast<Instruction>(unwrapV(After));
  if (LLVM_UNLIKELY(!I || !AfterI || !AfterI->getParent()))
    return;
  I->insertAfter(AfterI);
}

// ===----------------------------------------------------------------------===
//  MIR name & operand access
// ===----------------------------------------------------------------------===

static const char *bridgeMFuncGetName(NevercMachineFuncRef MF) {
  if (LLVM_UNLIKELY(!MF))
    return "";
  auto Name = unwrapMF(MF)->getName();
  return Name.data() ? Name.data() : "";
}

static int64_t bridgeMInstGetOperandImm(NevercMachineInstrRef MI,
                                        unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isImm())
    return 0;
  return Inst->getOperand(Idx).getImm();
}

static int bridgeMInstGetOperandIsReg(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands())
    return 0;
  return Inst->getOperand(Idx).isReg();
}

static unsigned bridgeMInstGetOperandReg(NevercMachineInstrRef MI,
                                         unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).getReg().id();
}

static int bridgeMInstGetOperandIsImm(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands())
    return 0;
  return Inst->getOperand(Idx).isImm();
}

// ===----------------------------------------------------------------------===
//  Batch operand-kind collection.  Single vtable call returns the operand
//  kind for every position in OutKinds (caller-allocated, sized via
//  MInstGetNumOperands).  Eliminates the 3x vtable hop per operand pattern.
// ===----------------------------------------------------------------------===

static uint8_t machineOperandKindOf(const llvm::MachineOperand &Op) {
  using llvm::MachineOperand;
  switch (Op.getType()) {
  case MachineOperand::MO_Register:
    return NEVERC_MIR_OP_REG;
  case MachineOperand::MO_Immediate:
    return NEVERC_MIR_OP_IMM;
  case MachineOperand::MO_CImmediate:
    return NEVERC_MIR_OP_IMM;
  case MachineOperand::MO_FPImmediate:
    return NEVERC_MIR_OP_FPIMM;
  case MachineOperand::MO_MachineBasicBlock:
    return NEVERC_MIR_OP_MBB;
  case MachineOperand::MO_FrameIndex:
    return NEVERC_MIR_OP_FRAMEIDX;
  case MachineOperand::MO_GlobalAddress:
    return NEVERC_MIR_OP_GLOBAL;
  case MachineOperand::MO_ExternalSymbol:
    return NEVERC_MIR_OP_EXTSYM;
  case MachineOperand::MO_Metadata:
    return NEVERC_MIR_OP_METADATA;
  case MachineOperand::MO_RegisterMask:
  case MachineOperand::MO_RegisterLiveOut:
    return NEVERC_MIR_OP_REGMASK;
  case MachineOperand::MO_BlockAddress:
    return NEVERC_MIR_OP_BLOCKADDR;
  default:
    return NEVERC_MIR_OP_OTHER;
  }
}

static unsigned bridgeMInstCollectOperandKinds(NevercMachineInstrRef MI,
                                               uint8_t *OutKinds) {
  if (LLVM_UNLIKELY(!MI || !OutKinds))
    return 0;
  auto *Inst = unwrapMI(MI);
  unsigned N = Inst->getNumOperands();
  for (unsigned I = 0; I < N; ++I)
    OutKinds[I] = machineOperandKindOf(Inst->getOperand(I));
  return N;
}

// ===----------------------------------------------------------------------===
//  MBB navigation
// ===----------------------------------------------------------------------===

static unsigned bridgeMBBGetNumber(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return 0;
  return unwrapMBB(MBB)->getNumber();
}

static unsigned bridgeMBBGetSuccCount(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return 0;
  return unwrapMBB(MBB)->succ_size();
}

static unsigned bridgeMBBGetPredCount(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return 0;
  return unwrapMBB(MBB)->pred_size();
}

// ===----------------------------------------------------------------------===
//  Function/global removal from module
// ===----------------------------------------------------------------------===

static void bridgeModuleRemoveFunction(NevercModuleRef M, NevercValueRef F) {
  if (LLVM_UNLIKELY(!M || !F))
    return;
  auto *Mod = unwrap(M);
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->getParent() != Mod))
    return;
  if (!Fn->use_empty()) {
    WithColor::warning(errs(), "neverc-plugin")
        << "removing function '" << Fn->getName()
        << "' that still has " << Fn->getNumUses()
        << " uses; replacing with poison\n";
    Fn->replaceAllUsesWith(PoisonValue::get(Fn->getType()));
  }
  Fn->eraseFromParent();
}

static void bridgeModuleRemoveGlobal(NevercModuleRef M, NevercValueRef GV) {
  if (LLVM_UNLIKELY(!M || !GV))
    return;
  auto *Mod = unwrap(M);
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (LLVM_UNLIKELY(!G || G->getParent() != Mod))
    return;
  if (!G->use_empty()) {
    WithColor::warning(errs(), "neverc-plugin")
        << "removing global '" << G->getName()
        << "' that still has " << G->getNumUses()
        << " uses; replacing with poison\n";
    G->replaceAllUsesWith(PoisonValue::get(G->getType()));
  }
  G->eraseFromParent();
}

// ===----------------------------------------------------------------------===
//  Module convenience counters
// ===----------------------------------------------------------------------===

static unsigned bridgeModuleGetFunctionCount(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return 0;
  return unwrap(M)->size();
}

static unsigned bridgeModuleGetGlobalCount(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return 0;
  return unwrap(M)->global_size();
}

static NevercTypeRef bridgeValueGetTypeAsFunction(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return nullptr;
  return wrapTy(Fn->getFunctionType());
}

// ===----------------------------------------------------------------------===
//  Function return type
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeFunctionGetReturnType(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return nullptr;
  return wrapTy(Fn->getReturnType());
}

// ===----------------------------------------------------------------------===
//  Instruction queries
// ===----------------------------------------------------------------------===

static int bridgeInstIsTerminator(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return Inst && Inst->isTerminator();
}

// ===----------------------------------------------------------------------===
//  Unary builder ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildNeg(NevercBuilderRef B, NevercValueRef V,
                                     const char *Name) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateNeg(unwrapV(V), Name ? Name : ""));
}

static NevercValueRef bridgeBuildNot(NevercBuilderRef B, NevercValueRef V,
                                     const char *Name) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateNot(unwrapV(V), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFNeg(NevercBuilderRef B, NevercValueRef V,
                                      const char *Name) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateFNeg(unwrapV(V), Name ? Name : ""));
}

static NevercValueRef bridgeBuildUnreachable(NevercBuilderRef B) {
  if (LLVM_UNLIKELY(!B))
    return nullptr;
  return wrapV(unwrapB(B)->CreateUnreachable());
}

// ===----------------------------------------------------------------------===
//  Switch instruction
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildSwitch(NevercBuilderRef B, NevercValueRef V,
                                        NevercBasicBlockRef DefaultBB,
                                        unsigned NumCases) {
  if (LLVM_UNLIKELY(!B || !V || !DefaultBB))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSwitch(unwrapV(V), unwrapBB(DefaultBB), NumCases));
}

static void bridgeSwitchAddCase(NevercValueRef Switch, NevercValueRef OnVal,
                                NevercBasicBlockRef Dest) {
  if (LLVM_UNLIKELY(!Switch || !OnVal || !Dest))
    return;
  auto *SI = dyn_cast<SwitchInst>(unwrapV(Switch));
  if (LLVM_UNLIKELY(!SI))
    return;
  auto *CI = dyn_cast<ConstantInt>(unwrapV(OnVal));
  if (LLVM_UNLIKELY(!CI)) {
    WithColor::warning(errs(), "neverc-plugin")
        << "SwitchAddCase: case value is not ConstantInt; ignoring\n";
    return;
  }
  SI->addCase(CI, unwrapBB(Dest));
}

// ===----------------------------------------------------------------------===
//  Constant pointer null
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeConstPointerNull(NevercTypeRef PtrTy) {
  if (LLVM_UNLIKELY(!PtrTy))
    return nullptr;
  auto *PT = dyn_cast<PointerType>(unwrapTy(PtrTy));
  if (LLVM_UNLIKELY(!PT))
    return nullptr;
  return wrapV(ConstantPointerNull::get(PT));
}

// ===----------------------------------------------------------------------===
//  Remainder ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildURem(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateURem(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildSRem(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSRem(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  FP cast ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildFPToSI(NevercBuilderRef B, NevercValueRef V,
                                        NevercTypeRef DestTy,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFPToSI(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

static NevercValueRef bridgeBuildSIToFP(NevercBuilderRef B, NevercValueRef V,
                                        NevercTypeRef DestTy,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSIToFP(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFPToUI(NevercBuilderRef B, NevercValueRef V,
                                        NevercTypeRef DestTy,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFPToUI(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

static NevercValueRef bridgeBuildUIToFP(NevercBuilderRef B, NevercValueRef V,
                                        NevercTypeRef DestTy,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateUIToFP(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Register stubs -- safe no-ops so the vtable is never null for these slots.
//  PluginLoader replaces them with real registrars during RegisterPasses and
//  restores NoOps afterward.
// ===----------------------------------------------------------------------===

static void bridgeNoOpRegisterModulePass(void *, NevercHookPoint,
                                         NevercModulePassFn, void *,
                                         const char *) {}
static void bridgeNoOpRegisterMachinePass(void *, NevercHookPoint,
                                          NevercMachinePassFn, void *,
                                          const char *) {}
static void bridgeNoOpRegisterBinaryPass(void *, NevercHookPoint,
                                         NevercBinaryPassFn, void *,
                                         const char *) {}
static void bridgeNoOpRegisterLinkerPass(void *, NevercHookPoint,
                                         NevercLinkerPassFn, void *,
                                         const char *) {}

// ===----------------------------------------------------------------------===
//  Binary buffer ops
// ===----------------------------------------------------------------------===

static int bridgeBinaryResize(uint8_t **Data, uint64_t *Len,
                               uint64_t *Capacity, uint64_t NewLen) {
  if (LLVM_UNLIKELY(!Data || !Len || !Capacity))
    return 0;
  if (NewLen <= *Capacity) {
    *Len = NewLen;
    return 1;
  }
  uint64_t NewCap;
  if (NewLen > (UINT64_MAX / 2))
    NewCap = NewLen;
  else
    NewCap = NewLen * 2;
  if (NewCap < 64)
    NewCap = 64;
  uint8_t *New = static_cast<uint8_t *>(bridgeRealloc(*Data, NewCap));
  if (LLVM_UNLIKELY(!New))
    return 0;
  *Data = New;
  *Len = NewLen;
  *Capacity = NewCap;
  return 1;
}

// ===----------------------------------------------------------------------===
//  Instruction kind queries
// ===----------------------------------------------------------------------===

static int bridgeInstIsCall(NevercValueRef I) {
  return I && isa<CallInst>(unwrapV(I));
}
static int bridgeInstIsBranch(NevercValueRef I) {
  return I && isa<BranchInst>(unwrapV(I));
}
static int bridgeInstIsLoad(NevercValueRef I) {
  return I && isa<LoadInst>(unwrapV(I));
}
static int bridgeInstIsStore(NevercValueRef I) {
  return I && isa<StoreInst>(unwrapV(I));
}
static int bridgeInstIsAlloca(NevercValueRef I) {
  return I && isa<AllocaInst>(unwrapV(I));
}
static int bridgeInstIsPHI(NevercValueRef I) {
  return I && isa<PHINode>(unwrapV(I));
}
static int bridgeInstIsGEP(NevercValueRef I) {
  return I && isa<GetElementPtrInst>(unwrapV(I));
}
static int bridgeInstIsCast(NevercValueRef I) {
  return I && isa<CastInst>(unwrapV(I));
}
static int bridgeInstIsBinaryOp(NevercValueRef I) {
  return I && isa<BinaryOperator>(unwrapV(I));
}
static int bridgeInstIsUnaryOp(NevercValueRef I) {
  return I && isa<UnaryOperator>(unwrapV(I));
}
static int bridgeInstIsSwitch(NevercValueRef I) {
  return I && isa<SwitchInst>(unwrapV(I));
}
static int bridgeInstIsReturn(NevercValueRef I) {
  return I && isa<ReturnInst>(unwrapV(I));
}
static int bridgeInstIsSelect(NevercValueRef I) {
  return I && isa<SelectInst>(unwrapV(I));
}

// ===----------------------------------------------------------------------===
//  CallInst ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeCallGetCalledOperand(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  return CB ? wrapV(CB->getCalledOperand()) : nullptr;
}

static unsigned bridgeCallGetNumArgs(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  return CB ? CB->arg_size() : 0;
}

static NevercValueRef bridgeCallGetArg(NevercValueRef I, unsigned Idx) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  if (LLVM_UNLIKELY(!CB || Idx >= CB->arg_size()))
    return nullptr;
  return wrapV(CB->getArgOperand(Idx));
}

static NevercTypeRef bridgeCallGetFunctionType(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  return CB ? wrapTy(CB->getFunctionType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  FunctionType / ArrayType access
// ===----------------------------------------------------------------------===

static unsigned bridgeFuncTypeGetParamCount(NevercTypeRef FnTy) {
  if (LLVM_UNLIKELY(!FnTy))
    return 0;
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  return FT ? FT->getNumParams() : 0;
}

static NevercTypeRef bridgeFuncTypeGetParamType(NevercTypeRef FnTy,
                                                unsigned Idx) {
  if (LLVM_UNLIKELY(!FnTy))
    return nullptr;
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  if (LLVM_UNLIKELY(!FT || Idx >= FT->getNumParams()))
    return nullptr;
  return wrapTy(FT->getParamType(Idx));
}

static NevercTypeRef bridgeFuncTypeGetReturnType(NevercTypeRef FnTy) {
  if (LLVM_UNLIKELY(!FnTy))
    return nullptr;
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  return FT ? wrapTy(FT->getReturnType()) : nullptr;
}

static int bridgeFuncTypeIsVarArg(NevercTypeRef FnTy) {
  if (LLVM_UNLIKELY(!FnTy))
    return 0;
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  return FT && FT->isVarArg();
}

static int bridgeTypeIsFunctionTy(NevercTypeRef T) {
  return T && unwrapTy(T)->isFunctionTy();
}

static int bridgeTypeIsArrayTy(NevercTypeRef T) {
  return T && unwrapTy(T)->isArrayTy();
}

static uint64_t bridgeTypeGetArrayLength(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T))
    return 0;
  auto *AT = dyn_cast<ArrayType>(unwrapTy(T));
  return AT ? AT->getNumElements() : 0;
}

static NevercTypeRef bridgeTypeGetArrayElementType(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T))
    return nullptr;
  auto *AT = dyn_cast<ArrayType>(unwrapTy(T));
  return AT ? wrapTy(AT->getElementType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  BranchInst ops
// ===----------------------------------------------------------------------===

static int bridgeBrIsConditional(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *BI = dyn_cast<BranchInst>(unwrapV(I));
  return BI && BI->isConditional();
}

static NevercValueRef bridgeBrGetCondition(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *BI = dyn_cast<BranchInst>(unwrapV(I));
  if (LLVM_UNLIKELY(!BI || !BI->isConditional()))
    return nullptr;
  return wrapV(BI->getCondition());
}

static NevercBasicBlockRef bridgeBrGetSuccessor(NevercValueRef I,
                                                unsigned Idx) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *BI = dyn_cast<BranchInst>(unwrapV(I));
  if (LLVM_UNLIKELY(!BI || Idx >= BI->getNumSuccessors()))
    return nullptr;
  return wrapBB(BI->getSuccessor(Idx));
}

static unsigned bridgeBrGetNumSuccessors(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *BI = dyn_cast<BranchInst>(unwrapV(I));
  return BI ? BI->getNumSuccessors() : 0;
}

// ===----------------------------------------------------------------------===
//  Load/Store operand access
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeLoadGetPointerOperand(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *LI = dyn_cast<LoadInst>(unwrapV(I));
  return LI ? wrapV(LI->getPointerOperand()) : nullptr;
}

static NevercValueRef bridgeStoreGetValueOperand(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *SI = dyn_cast<StoreInst>(unwrapV(I));
  return SI ? wrapV(SI->getValueOperand()) : nullptr;
}

static NevercValueRef bridgeStoreGetPointerOperand(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *SI = dyn_cast<StoreInst>(unwrapV(I));
  return SI ? wrapV(SI->getPointerOperand()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  GEP access
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeGEPGetPointerOperand(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *GEP = dyn_cast<GetElementPtrInst>(unwrapV(I));
  return GEP ? wrapV(GEP->getPointerOperand()) : nullptr;
}

static unsigned bridgeGEPGetNumIndices(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *GEP = dyn_cast<GetElementPtrInst>(unwrapV(I));
  return GEP ? GEP->getNumIndices() : 0;
}

// ===----------------------------------------------------------------------===
//  BasicBlock predecessor/successor count
// ===----------------------------------------------------------------------===

static unsigned bridgeBBGetPredCount(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return 0;
  return std::distance(pred_begin(unwrapBB(BB)), pred_end(unwrapBB(BB)));
}

static unsigned bridgeBBGetSuccCount(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return 0;
  return std::distance(succ_begin(unwrapBB(BB)), succ_end(unwrapBB(BB)));
}

// ===----------------------------------------------------------------------===
//  Floating-point arithmetic
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildFAdd(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFAdd(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFSub(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFSub(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFMul(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFMul(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFDiv(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFDiv(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFRem(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (LLVM_UNLIKELY(!B || !LHS || !RHS))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFRem(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Aggregate value ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildExtractValue(NevercBuilderRef B,
                                              NevercValueRef Agg, unsigned Idx,
                                              const char *Name) {
  if (LLVM_UNLIKELY(!B || !Agg))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateExtractValue(unwrapV(Agg), Idx, Name ? Name : ""));
}

static NevercValueRef bridgeBuildInsertValue(NevercBuilderRef B,
                                             NevercValueRef Agg,
                                             NevercValueRef Val, unsigned Idx,
                                             const char *Name) {
  if (LLVM_UNLIKELY(!B || !Agg || !Val))
    return nullptr;
  return wrapV(unwrapB(B)->CreateInsertValue(unwrapV(Agg), unwrapV(Val), Idx,
                                             Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Constant aggregate creation
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeConstArray(NevercTypeRef ElemTy,
                                       NevercValueRef *Vals, unsigned Count) {
  if (LLVM_UNLIKELY(!ElemTy || (Count > 0 && !Vals)))
    return nullptr;
  SmallVector<Constant *, 8> Elems;
  for (unsigned I = 0; I < Count; ++I) {
    auto *C = dyn_cast_or_null<Constant>(unwrapV(Vals[I]));
    if (LLVM_UNLIKELY(!C))
      return nullptr;
    Elems.push_back(C);
  }
  return wrapV(ConstantArray::get(ArrayType::get(unwrapTy(ElemTy), Count),
                                  Elems));
}

static NevercValueRef bridgeConstStruct(NevercContextRef C,
                                        NevercValueRef *Vals, unsigned Count,
                                        int IsPacked) {
  if (LLVM_UNLIKELY(!C || (Count > 0 && !Vals)))
    return nullptr;
  SmallVector<Constant *, 8> Elems;
  for (unsigned I = 0; I < Count; ++I) {
    auto *CV = dyn_cast_or_null<Constant>(unwrapV(Vals[I]));
    if (LLVM_UNLIKELY(!CV))
      return nullptr;
    Elems.push_back(CV);
  }
  return wrapV(ConstantStruct::getAnon(*unwrapCtx(C), Elems, IsPacked != 0));
}

static NevercValueRef bridgeConstNamedStruct(NevercTypeRef StructTy,
                                             NevercValueRef *Vals,
                                             unsigned Count) {
  if (LLVM_UNLIKELY(!StructTy || (Count > 0 && !Vals)))
    return nullptr;
  auto *ST = dyn_cast<StructType>(unwrapTy(StructTy));
  if (LLVM_UNLIKELY(!ST))
    return nullptr;
  if (ST->isOpaque()) {
    WithColor::warning(errs(), "neverc-plugin")
        << "ConstNamedStruct: struct '"
        << (ST->hasName() ? ST->getName() : "<anonymous>")
        << "' is opaque (body not defined)\n";
    return nullptr;
  }
  if (ST->getNumElements() != Count) {
    WithColor::warning(errs(), "neverc-plugin")
        << "ConstNamedStruct: element count " << Count
        << " does not match struct '"
        << (ST->hasName() ? ST->getName() : "<anonymous>")
        << "' (expects " << ST->getNumElements() << ")\n";
    return nullptr;
  }
  SmallVector<Constant *, 8> Elems;
  for (unsigned I = 0; I < Count; ++I) {
    auto *CV = dyn_cast_or_null<Constant>(unwrapV(Vals[I]));
    if (LLVM_UNLIKELY(!CV))
      return nullptr;
    Elems.push_back(CV);
  }
  return wrapV(ConstantStruct::get(ST, Elems));
}

// ===----------------------------------------------------------------------===
//  Global string pointer convenience
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildGlobalStringPtr(NevercBuilderRef B,
                                                 const char *Str,
                                                 const char *Name) {
  if (LLVM_UNLIKELY(!B || !Str))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateGlobalStringPtr(Str, Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Value name mutation
// ===----------------------------------------------------------------------===

static void bridgeValueSetName(NevercValueRef V, const char *Name) {
  if (LLVM_UNLIKELY(!V))
    return;
  unwrapV(V)->setName(Name ? Name : "");
}

// ===----------------------------------------------------------------------===
//  GlobalVariable linkage
// ===----------------------------------------------------------------------===

static unsigned bridgeGlobalGetLinkage(NevercValueRef GV) {
  if (LLVM_UNLIKELY(!GV))
    return 0;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  return G ? static_cast<unsigned>(G->getLinkage()) : 0;
}

static void bridgeGlobalSetLinkage(NevercValueRef GV, unsigned Linkage) {
  if (LLVM_UNLIKELY(!GV))
    return;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (G)
    G->setLinkage(static_cast<GlobalValue::LinkageTypes>(Linkage));
}

// ===----------------------------------------------------------------------===
//  Inbounds GEP
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildInBoundsGEP(NevercBuilderRef B,
                                             NevercTypeRef Ty,
                                             NevercValueRef Ptr,
                                             NevercValueRef *Indices,
                                             unsigned NumIndices,
                                             const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty || !Ptr || (NumIndices > 0 && !Indices)))
    return nullptr;
  SmallVector<Value *, 4> Idxs;
  for (unsigned I = 0; I < NumIndices; ++I) {
    if (LLVM_UNLIKELY(!Indices[I]))
      return nullptr;
    Idxs.push_back(unwrapV(Indices[I]));
  }
  return wrapV(unwrapB(B)->CreateInBoundsGEP(unwrapTy(Ty), unwrapV(Ptr), Idxs,
                                             Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Module verification
// ===----------------------------------------------------------------------===

static int bridgeModuleVerify(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return 1;
  SmallString<256> Err;
  raw_svector_ostream OS(Err);
  if (verifyModule(*unwrap(M), &OS)) {
    WithColor::error(errs(), "neverc-plugin")
        << "module verification failed:\n"
        << Err << "\n";
    return 1;
  }
  return 0;
}

// ===----------------------------------------------------------------------===
//  Terminator successor navigation
// ===----------------------------------------------------------------------===

static unsigned bridgeInstGetNumSuccessors(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (LLVM_UNLIKELY(!Inst || !Inst->isTerminator()))
    return 0;
  return Inst->getNumSuccessors();
}

static NevercBasicBlockRef bridgeInstGetSuccessor(NevercValueRef I,
                                                  unsigned Idx) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (LLVM_UNLIKELY(!Inst || !Inst->isTerminator() ||
                    Idx >= Inst->getNumSuccessors()))
    return nullptr;
  return wrapBB(Inst->getSuccessor(Idx));
}

// ===----------------------------------------------------------------------===
//  AllocaInst source element type
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeAllocaGetAllocatedType(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *AI = dyn_cast<AllocaInst>(unwrapV(I));
  return AI ? wrapTy(AI->getAllocatedType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  Global variable alignment
// ===----------------------------------------------------------------------===

static unsigned bridgeGlobalGetAlignment(NevercValueRef GV) {
  if (LLVM_UNLIKELY(!GV))
    return 0;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (LLVM_UNLIKELY(!G))
    return 0;
  MaybeAlign A = G->getAlign();
  return A ? A->value() : 0;
}

static void bridgeGlobalSetAlignment(NevercValueRef GV, unsigned Align) {
  if (LLVM_UNLIKELY(!GV))
    return;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (G)
    G->setAlignment(MaybeAlign(Align));
}

// ===----------------------------------------------------------------------===
//  BasicBlock predecessor/successor by index
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef bridgeBBGetPredecessor(NevercBasicBlockRef BB,
                                                  unsigned Idx) {
  if (LLVM_UNLIKELY(!BB))
    return nullptr;
  auto *Block = unwrapBB(BB);
  unsigned Count = 0;
  for (auto *Pred : predecessors(Block)) {
    if (Count == Idx)
      return wrapBB(Pred);
    Count++;
  }
  return nullptr;
}

static NevercBasicBlockRef bridgeBBGetSuccessorBB(NevercBasicBlockRef BB,
                                                  unsigned Idx) {
  if (LLVM_UNLIKELY(!BB))
    return nullptr;
  auto *Block = unwrapBB(BB);
  unsigned Count = 0;
  for (auto *Succ : successors(Block)) {
    if (Count == Idx)
      return wrapBB(Succ);
    Count++;
  }
  return nullptr;
}

// ===----------------------------------------------------------------------===
//  Value identity
// ===----------------------------------------------------------------------===

static int bridgeValueIsNull(NevercValueRef V) {
  if (!V)
    return 1;
  auto *C = dyn_cast<Constant>(unwrapV(V));
  return C && C->isNullValue();
}

static int bridgeValueIsSameAs(NevercValueRef A, NevercValueRef B) {
  if (!A || !B)
    return !A && !B;
  return unwrapV(A) == unwrapV(B);
}

// ===----------------------------------------------------------------------===
//  PHI node access
// ===----------------------------------------------------------------------===

static unsigned bridgePhiGetNumIncoming(NevercValueRef Phi) {
  if (LLVM_UNLIKELY(!Phi))
    return 0;
  auto *PN = dyn_cast<PHINode>(unwrapV(Phi));
  return PN ? PN->getNumIncomingValues() : 0;
}

static NevercValueRef bridgePhiGetIncomingValue(NevercValueRef Phi,
                                                unsigned Idx) {
  if (LLVM_UNLIKELY(!Phi))
    return nullptr;
  auto *PN = dyn_cast<PHINode>(unwrapV(Phi));
  if (LLVM_UNLIKELY(!PN || Idx >= PN->getNumIncomingValues()))
    return nullptr;
  return wrapV(PN->getIncomingValue(Idx));
}

static NevercBasicBlockRef bridgePhiGetIncomingBlock(NevercValueRef Phi,
                                                     unsigned Idx) {
  if (LLVM_UNLIKELY(!Phi))
    return nullptr;
  auto *PN = dyn_cast<PHINode>(unwrapV(Phi));
  if (LLVM_UNLIKELY(!PN || Idx >= PN->getNumIncomingValues()))
    return nullptr;
  return wrapBB(PN->getIncomingBlock(Idx));
}

// ===----------------------------------------------------------------------===
//  SelectInst operand access
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeSelectGetCondition(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *SI = dyn_cast<SelectInst>(unwrapV(I));
  return SI ? wrapV(SI->getCondition()) : nullptr;
}

static NevercValueRef bridgeSelectGetTrueValue(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *SI = dyn_cast<SelectInst>(unwrapV(I));
  return SI ? wrapV(SI->getTrueValue()) : nullptr;
}

static NevercValueRef bridgeSelectGetFalseValue(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *SI = dyn_cast<SelectInst>(unwrapV(I));
  return SI ? wrapV(SI->getFalseValue()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  ReturnInst operand access
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeReturnGetValue(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *RI = dyn_cast<ReturnInst>(unwrapV(I));
  return (RI && RI->getReturnValue()) ? wrapV(RI->getReturnValue()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  CastInst type access
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeCastGetSrcTy(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *CI = dyn_cast<CastInst>(unwrapV(I));
  return CI ? wrapTy(CI->getSrcTy()) : nullptr;
}

static NevercTypeRef bridgeCastGetDestTy(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *CI = dyn_cast<CastInst>(unwrapV(I));
  return CI ? wrapTy(CI->getDestTy()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  GEP source element type
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeGEPGetSourceElementType(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *GEP = dyn_cast<GetElementPtrInst>(unwrapV(I));
  return GEP ? wrapTy(GEP->getSourceElementType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  ICmp/FCmp predicate access
// ===----------------------------------------------------------------------===

static unsigned bridgeCmpGetPredicate(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *CI = dyn_cast<CmpInst>(unwrapV(I));
  return CI ? static_cast<unsigned>(CI->getPredicate()) : 0;
}

static int bridgeInstIsICmp(NevercValueRef I) {
  return I && isa<ICmpInst>(unwrapV(I));
}

static int bridgeInstIsFCmp(NevercValueRef I) {
  return I && isa<FCmpInst>(unwrapV(I));
}

// ===----------------------------------------------------------------------===
//  BasicBlock name
// ===----------------------------------------------------------------------===

static const char *bridgeBBGetName(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return "";
  auto Name = unwrapBB(BB)->getName();
  return Name.data() ? Name.data() : "";
}

// ===----------------------------------------------------------------------===
//  Function BasicBlock count
// ===----------------------------------------------------------------------===

static unsigned bridgeFunctionGetBBCount(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  return Fn ? Fn->size() : 0;
}

// ===----------------------------------------------------------------------===
//  Reverse iteration
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeInstGetPrevInst(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (LLVM_UNLIKELY(!Inst || !Inst->getParent()))
    return nullptr;
  auto It = Inst->getIterator();
  if (It == Inst->getParent()->begin())
    return nullptr;
  return wrapV(&*std::prev(It));
}

static NevercBasicBlockRef bridgeFunctionGetPrevBB(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return nullptr;
  auto *Block = unwrapBB(BB);
  if (LLVM_UNLIKELY(!Block->getParent()))
    return nullptr;
  auto It = Block->getIterator();
  if (It == Block->getParent()->begin())
    return nullptr;
  return wrapBB(&*std::prev(It));
}

// ===----------------------------------------------------------------------===
//  StructType name
// ===----------------------------------------------------------------------===

static const char *bridgeTypeGetStructName(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T))
    return nullptr;
  auto *ST = dyn_cast<StructType>(unwrapTy(T));
  if (LLVM_UNLIKELY(!ST || !ST->hasName()))
    return nullptr;
  return ST->getName().data();
}

// ===----------------------------------------------------------------------===
//  SwitchInst inspection
// ===----------------------------------------------------------------------===

static unsigned bridgeSwitchGetNumCases(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *SI = dyn_cast<SwitchInst>(unwrapV(I));
  return SI ? SI->getNumCases() : 0;
}

static NevercValueRef bridgeSwitchGetCaseValue(NevercValueRef I, unsigned Idx) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *SI = dyn_cast<SwitchInst>(unwrapV(I));
  if (LLVM_UNLIKELY(!SI || Idx >= SI->getNumCases()))
    return nullptr;
  auto It = SI->case_begin();
  std::advance(It, Idx);
  return wrapV(It->getCaseValue());
}

static NevercBasicBlockRef bridgeSwitchGetCaseSuccessor(NevercValueRef I,
                                                        unsigned Idx) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *SI = dyn_cast<SwitchInst>(unwrapV(I));
  if (LLVM_UNLIKELY(!SI || Idx >= SI->getNumCases()))
    return nullptr;
  auto It = SI->case_begin();
  std::advance(It, Idx);
  return wrapBB(It->getCaseSuccessor());
}

static NevercBasicBlockRef bridgeSwitchGetDefaultDest(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *SI = dyn_cast<SwitchInst>(unwrapV(I));
  return SI ? wrapBB(SI->getDefaultDest()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  MIR BasicBlock navigation by index
// ===----------------------------------------------------------------------===

static NevercMachineBBRef bridgeMBBGetSuccessor(NevercMachineBBRef MBB,
                                                unsigned Idx) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *Block = unwrapMBB(MBB);
  if (Idx >= Block->succ_size())
    return nullptr;
  auto It = Block->succ_begin();
  std::advance(It, Idx);
  return wrapMBB(*It);
}

static NevercMachineBBRef bridgeMBBGetPredecessor(NevercMachineBBRef MBB,
                                                  unsigned Idx) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *Block = unwrapMBB(MBB);
  if (Idx >= Block->pred_size())
    return nullptr;
  auto It = Block->pred_begin();
  std::advance(It, Idx);
  return wrapMBB(*It);
}

// ===----------------------------------------------------------------------===
//  Module source file name
// ===----------------------------------------------------------------------===

static const char *bridgeModuleGetSourceFileName(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return "";
  return unwrap(M)->getSourceFileName().c_str();
}

// ===----------------------------------------------------------------------===
//  InvokeInst support
// ===----------------------------------------------------------------------===

static int bridgeInstIsInvoke(NevercValueRef I) {
  return I && isa<InvokeInst>(unwrapV(I));
}

static NevercBasicBlockRef bridgeInvokeGetNormalDest(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *II = dyn_cast<InvokeInst>(unwrapV(I));
  return II ? wrapBB(II->getNormalDest()) : nullptr;
}

static NevercBasicBlockRef bridgeInvokeGetUnwindDest(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *II = dyn_cast<InvokeInst>(unwrapV(I));
  return II ? wrapBB(II->getUnwindDest()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  BasicBlock splitting
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef bridgeBBSplitBefore(NevercBasicBlockRef BB,
                                                NevercValueRef Inst) {
  if (LLVM_UNLIKELY(!BB || !Inst))
    return nullptr;
  auto *Block = unwrapBB(BB);
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  if (LLVM_UNLIKELY(!I || I->getParent() != Block))
    return nullptr;
  // splitBasicBlock asserts the BB has a terminator and I != end().
  if (LLVM_UNLIKELY(!Block->getTerminator())) {
    WithColor::warning(errs(), "neverc-plugin")
        << "BBSplitBefore called on BB without terminator; ignoring\n";
    return nullptr;
  }
  auto *NewBB = Block->splitBasicBlock(I->getIterator());
  return wrapBB(NewBB);
}

// ===----------------------------------------------------------------------===
//  IRBuilder insert-point query
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef bridgeBuilderGetInsertBlock(NevercBuilderRef B) {
  if (LLVM_UNLIKELY(!B))
    return nullptr;
  auto *BB = unwrapB(B)->GetInsertBlock();
  return BB ? wrapBB(BB) : nullptr;
}

// ===----------------------------------------------------------------------===
//  ConstantInt value extraction
// ===----------------------------------------------------------------------===

static uint64_t bridgeConstIntGetZExtValue(NevercValueRef V) {
  if (LLVM_UNLIKELY(!V))
    return 0;
  auto *CI = dyn_cast<ConstantInt>(unwrapV(V));
  return CI ? CI->getZExtValue() : 0;
}

static int64_t bridgeConstIntGetSExtValue(NevercValueRef V) {
  if (LLVM_UNLIKELY(!V))
    return 0;
  auto *CI = dyn_cast<ConstantInt>(unwrapV(V));
  return CI ? CI->getSExtValue() : 0;
}

static int bridgeValueIsConstantInt(NevercValueRef V) {
  return V && isa<ConstantInt>(unwrapV(V));
}

// ===----------------------------------------------------------------------===
//  GlobalVariable value type
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeGlobalGetValueType(NevercValueRef GV) {
  if (LLVM_UNLIKELY(!GV))
    return nullptr;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  return G ? wrapTy(G->getValueType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  Function body removal
// ===----------------------------------------------------------------------===

static void bridgeFunctionDeleteBody(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (Fn)
    Fn->deleteBody();
}

// ===----------------------------------------------------------------------===
//  Value undef check
// ===----------------------------------------------------------------------===

static int bridgeValueIsUndef(NevercValueRef V) {
  return V && isa<UndefValue>(unwrapV(V));
}

// ===----------------------------------------------------------------------===
//  Instruction operand as BasicBlock
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef bridgeInstGetOperandAsBB(NevercValueRef I,
                                                    unsigned Idx) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (LLVM_UNLIKELY(!Inst || Idx >= Inst->getNumOperands()))
    return nullptr;
  auto *BB = dyn_cast<BasicBlock>(Inst->getOperand(Idx));
  return BB ? wrapBB(BB) : nullptr;
}

// ===----------------------------------------------------------------------===
//  FP precision cast ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildFPExt(NevercBuilderRef B, NevercValueRef V,
                                       NevercTypeRef DestTy,
                                       const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFPExt(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFPTrunc(NevercBuilderRef B, NevercValueRef V,
                                         NevercTypeRef DestTy,
                                         const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(unwrapB(B)->CreateFPTrunc(unwrapV(V), unwrapTy(DestTy),
                                         Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  LoadInst source type
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeLoadGetType(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *LI = dyn_cast<LoadInst>(unwrapV(I));
  return LI ? wrapTy(LI->getType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  GEP inbounds query
// ===----------------------------------------------------------------------===

static int bridgeGEPIsInBounds(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *GEP = dyn_cast<GetElementPtrInst>(unwrapV(I));
  return GEP && GEP->isInBounds();
}

// ===----------------------------------------------------------------------===
//  CallBase convenience (matches call, invoke, callbr)
// ===----------------------------------------------------------------------===

static int bridgeInstIsCallLike(NevercValueRef I) {
  return I && isa<CallBase>(unwrapV(I));
}

// ===----------------------------------------------------------------------===
//  Load/Store alignment & volatile
// ===----------------------------------------------------------------------===

static unsigned bridgeLoadGetAlignment(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *LI = dyn_cast<LoadInst>(unwrapV(I));
  return LI ? LI->getAlign().value() : 0;
}

static unsigned bridgeStoreGetAlignment(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *SI = dyn_cast<StoreInst>(unwrapV(I));
  return SI ? SI->getAlign().value() : 0;
}

static int bridgeLoadIsVolatile(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *LI = dyn_cast<LoadInst>(unwrapV(I));
  return LI && LI->isVolatile();
}

static int bridgeStoreIsVolatile(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *SI = dyn_cast<StoreInst>(unwrapV(I));
  return SI && SI->isVolatile();
}

// ===----------------------------------------------------------------------===
//  Builder: insert before terminator
// ===----------------------------------------------------------------------===

static void bridgeBuilderSetInsertPointBeforeTerminator(NevercBuilderRef B,
                                                        NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!B || !BB))
    return;
  auto *Block = unwrapBB(BB);
  auto *Term = Block->getTerminator();
  if (Term)
    unwrapB(B)->SetInsertPoint(Term->getIterator());
  else
    unwrapB(B)->SetInsertPoint(Block);
}

// ===----------------------------------------------------------------------===
//  Intrinsic detection
// ===----------------------------------------------------------------------===

static int bridgeInstIsIntrinsic(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  if (LLVM_UNLIKELY(!CB))
    return 0;
  auto *Callee = CB->getCalledFunction();
  return Callee && Callee->isIntrinsic();
}

static unsigned bridgeCallGetIntrinsicID(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  if (LLVM_UNLIKELY(!CB))
    return 0;
  auto *Callee = CB->getCalledFunction();
  if (!Callee || !Callee->isIntrinsic())
    return 0;
  return Callee->getIntrinsicID();
}

// ===----------------------------------------------------------------------===
//  Aligned Load/Store builders
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildAlignedLoad(NevercBuilderRef B,
                                             NevercTypeRef Ty,
                                             NevercValueRef Ptr,
                                             unsigned AlignVal, int IsVolatile,
                                             const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty || !Ptr))
    return nullptr;
  return wrapV(unwrapB(B)->CreateAlignedLoad(
      unwrapTy(Ty), unwrapV(Ptr), MaybeAlign(AlignVal), IsVolatile != 0,
      Name ? Name : ""));
}

static NevercValueRef bridgeBuildAlignedStore(NevercBuilderRef B,
                                              NevercValueRef Val,
                                              NevercValueRef Ptr,
                                              unsigned AlignVal,
                                              int IsVolatile) {
  if (LLVM_UNLIKELY(!B || !Val || !Ptr))
    return nullptr;
  return wrapV(unwrapB(B)->CreateAlignedStore(
      unwrapV(Val), unwrapV(Ptr), MaybeAlign(AlignVal), IsVolatile != 0));
}

// ===----------------------------------------------------------------------===
//  Debug location
// ===----------------------------------------------------------------------===

static void bridgeInstCopyDebugLoc(NevercValueRef Dst, NevercValueRef Src) {
  if (LLVM_UNLIKELY(!Dst || !Src))
    return;
  auto *DstI = dyn_cast<Instruction>(unwrapV(Dst));
  auto *SrcI = dyn_cast<Instruction>(unwrapV(Src));
  if (DstI && SrcI)
    DstI->setDebugLoc(SrcI->getDebugLoc());
}

static int bridgeInstHasDebugLoc(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return Inst && Inst->getDebugLoc();
}

static unsigned bridgeInstGetDebugLocLine(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (LLVM_UNLIKELY(!Inst))
    return 0;
  const auto &DL = Inst->getDebugLoc();
  return DL ? DL.getLine() : 0;
}

static unsigned bridgeInstGetDebugLocCol(NevercValueRef I) {
  if (LLVM_UNLIKELY(!I))
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (LLVM_UNLIKELY(!Inst))
    return 0;
  const auto &DL = Inst->getDebugLoc();
  return DL ? DL.getCol() : 0;
}

// ===----------------------------------------------------------------------===
//  GlobalObject section (works on both Function and GlobalVariable)
// ===----------------------------------------------------------------------===

static const char *bridgeGlobalGetSection(NevercValueRef GV) {
  if (LLVM_UNLIKELY(!GV))
    return "";
  auto *GO = dyn_cast<GlobalObject>(unwrapV(GV));
  if (LLVM_UNLIKELY(!GO))
    return "";
  StringRef Sec = GO->getSection();
  return Sec.data() ? Sec.data() : "";
}

static void bridgeGlobalSetSection(NevercValueRef GV, const char *Section) {
  if (LLVM_UNLIKELY(!GV))
    return;
  auto *GO = dyn_cast<GlobalObject>(unwrapV(GV));
  if (GO)
    GO->setSection(Section ? Section : "");
}

// ===----------------------------------------------------------------------===
//  Function parameter attributes
// ===----------------------------------------------------------------------===

static void bridgeFunctionAddParamAttr(NevercValueRef F, unsigned ParamIdx,
                                       const char *Kind, const char *Val) {
  if (LLVM_UNLIKELY(!F || !Kind))
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || ParamIdx >= Fn->arg_size()))
    return;
  Fn->addParamAttr(ParamIdx, Attribute::get(Fn->getContext(), Kind,
                                            Val ? Val : ""));
}

static int bridgeFunctionHasParamAttr(NevercValueRef F, unsigned ParamIdx,
                                      const char *Kind) {
  if (LLVM_UNLIKELY(!F || !Kind))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || ParamIdx >= Fn->arg_size()))
    return 0;
  return Fn->getAttributes().hasParamAttr(ParamIdx, Kind);
}

// ===----------------------------------------------------------------------===
//  BasicBlock instruction count
// ===----------------------------------------------------------------------===

static unsigned bridgeBBGetInstCount(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return 0;
  return unwrapBB(BB)->size();
}

// ===----------------------------------------------------------------------===
//  Module alias iteration
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeModuleGetFirstAlias(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return nullptr;
  auto *Mod = unwrap(M);
  if (LLVM_UNLIKELY(Mod->alias_empty()))
    return nullptr;
  return wrapV(&*Mod->alias_begin());
}

static NevercValueRef bridgeModuleGetNextAlias(NevercValueRef A) {
  if (LLVM_UNLIKELY(!A))
    return nullptr;
  auto *GA = dyn_cast<GlobalAlias>(unwrapV(A));
  if (LLVM_UNLIKELY(!GA || !GA->getParent()))
    return nullptr;
  auto It = std::next(GA->getIterator());
  if (It == GA->getParent()->alias_end())
    return nullptr;
  return wrapV(&*It);
}

static NevercValueRef bridgeModuleGetLastAlias(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return nullptr;
  auto *Mod = unwrap(M);
  if (LLVM_UNLIKELY(Mod->alias_empty()))
    return nullptr;
  return wrapV(&*std::prev(Mod->alias_end()));
}

static NevercValueRef bridgeModuleGetPrevAlias(NevercValueRef A) {
  if (LLVM_UNLIKELY(!A))
    return nullptr;
  auto *GA = dyn_cast<GlobalAlias>(unwrapV(A));
  if (LLVM_UNLIKELY(!GA || !GA->getParent()))
    return nullptr;
  auto It = GA->getIterator();
  if (It == GA->getParent()->alias_begin())
    return nullptr;
  return wrapV(&*std::prev(It));
}

static NevercValueRef bridgeModuleGetPrevFunction(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || !Fn->getParent()))
    return nullptr;
  auto It = Fn->getIterator();
  if (It == Fn->getParent()->begin())
    return nullptr;
  return wrapV(&*std::prev(It));
}

// ===----------------------------------------------------------------------===
//  GEP index access
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeGEPGetIndex(NevercValueRef I, unsigned Idx) {
  if (LLVM_UNLIKELY(!I))
    return nullptr;
  auto *GEP = dyn_cast<GetElementPtrInst>(unwrapV(I));
  if (LLVM_UNLIKELY(!GEP || Idx >= GEP->getNumIndices()))
    return nullptr;
  return wrapV(GEP->getOperand(Idx + 1));
}

// ===----------------------------------------------------------------------===
//  Reverse global iteration
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeModuleGetLastGlobal(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return nullptr;
  auto *Mod = unwrap(M);
  if (LLVM_UNLIKELY(Mod->global_empty()))
    return nullptr;
  return wrapV(&*std::prev(Mod->global_end()));
}

static NevercValueRef bridgeModuleGetPrevGlobal(NevercValueRef G) {
  if (LLVM_UNLIKELY(!G))
    return nullptr;
  auto *GV = dyn_cast<GlobalVariable>(unwrapV(G));
  if (LLVM_UNLIKELY(!GV || !GV->getParent()))
    return nullptr;
  auto It = GV->getIterator();
  if (It == GV->getParent()->global_begin())
    return nullptr;
  return wrapV(&*std::prev(It));
}

// ===----------------------------------------------------------------------===
//  Text representation -- returns host-allocated string, caller must Free()
// ===----------------------------------------------------------------------===

static char *bridgeValuePrintToString(NevercValueRef V) {
  if (LLVM_UNLIKELY(!V))
    return nullptr;
  SmallString<256> Buf;
  raw_svector_ostream OS(Buf);
  unwrapV(V)->print(OS, /*IsForDebug=*/true);
  char *Result = static_cast<char *>(bridgeAlloc(Buf.size() + 1));
  if (LLVM_UNLIKELY(!Result))
    return nullptr;
  std::memcpy(Result, Buf.data(), Buf.size());
  Result[Buf.size()] = '\0';
  return Result;
}

static char *bridgeTypePrintToString(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T))
    return nullptr;
  SmallString<64> Buf;
  raw_svector_ostream OS(Buf);
  unwrapTy(T)->print(OS);
  char *Result = static_cast<char *>(bridgeAlloc(Buf.size() + 1));
  if (LLVM_UNLIKELY(!Result))
    return nullptr;
  std::memcpy(Result, Buf.data(), Buf.size());
  Result[Buf.size()] = '\0';
  return Result;
}

// ===----------------------------------------------------------------------===
//  Additional Value kind queries
// ===----------------------------------------------------------------------===

static int bridgeValueIsConstantExpr(NevercValueRef V) {
  return V && isa<ConstantExpr>(unwrapV(V));
}

static int bridgeValueIsAlias(NevercValueRef V) {
  return V && isa<GlobalAlias>(unwrapV(V));
}

// ===----------------------------------------------------------------------===
//  Compilation mode queries
//  The shellcode library publishes its current state via
//  setShellcodeModeState() so that the Plugin lib does not need to depend
//  on Shellcode (which itself depends on Plugin).
// ===----------------------------------------------------------------------===

namespace {
struct ShellcodeModeState {
  bool Enabled = false;
  SmallString<64> EntrySymbol;
};
ShellcodeModeState &shellcodeModeStorage() {
  static ShellcodeModeState S;
  return S;
}
} // namespace

void setShellcodeModeState(bool Enabled, llvm::StringRef EntrySymbol) {
  auto &S = shellcodeModeStorage();
  S.Enabled = Enabled;
  S.EntrySymbol = EntrySymbol;
  gShellcodeModeEnabled = Enabled;
}

static int bridgeHostIsShellcodeMode(void) {
  return gShellcodeModeEnabled;
}

static const char *bridgeHostGetShellcodeEntrySymbol(void) {
  auto &S = shellcodeModeStorage();
  if (!S.Enabled)
    return "";
  return S.EntrySymbol.c_str();
}

// ===----------------------------------------------------------------------===
//  Plugin argument storage
//  Populated by setPluginArgs() before plugins are loaded.  Bridge functions
//  look up into this map so plugins can query -fplugin-pass-arg=key=value.
// ===----------------------------------------------------------------------===

namespace {
struct PluginArgStorage {
  StringMap<SmallString<32>> Args;
};
PluginArgStorage &pluginArgStorage() {
  static PluginArgStorage S;
  return S;
}
} // namespace

void setPluginArgs(const std::vector<std::string> &RawArgs) {
  auto &Store = pluginArgStorage();
  Store.Args.clear();
  for (const auto &A : RawArgs) {
    auto [Key, Val] = StringRef(A).split('=');
    Store.Args[Key] = Val;
  }
}

static const char *bridgePluginGetArg(const char *Key) {
  if (LLVM_UNLIKELY(!Key))
    return nullptr;
  auto &Args = pluginArgStorage().Args;
  auto It = Args.find(Key);
  if (It == Args.end())
    return nullptr;
  return It->second.c_str();
}

static int bridgePluginHasArg(const char *Key) {
  if (LLVM_UNLIKELY(!Key))
    return 0;
  return pluginArgStorage().Args.count(Key) != 0;
}

static unsigned bridgePluginGetArgCount(void) {
  return static_cast<unsigned>(pluginArgStorage().Args.size());
}

// ===----------------------------------------------------------------------===
//  Intrinsic lookup
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeIntrinsicGetDeclaration(NevercModuleRef M,
                                                     unsigned IntrinsicID,
                                                     NevercTypeRef *OverloadTys,
                                                     unsigned NumTys) {
  if (LLVM_UNLIKELY(!M || IntrinsicID == 0))
    return nullptr;
  auto *Mod = unwrap(M);
  SmallVector<Type *, 4> Tys;
  for (unsigned I = 0; I < NumTys; ++I) {
    if (LLVM_UNLIKELY(!OverloadTys || !OverloadTys[I]))
      return nullptr;
    Tys.push_back(unwrapTy(OverloadTys[I]));
  }
  auto *Fn = Intrinsic::getDeclaration(
      Mod, static_cast<Intrinsic::ID>(IntrinsicID), Tys);
  return Fn ? wrapV(Fn) : nullptr;
}

static char *bridgeIntrinsicGetName(unsigned IntrinsicID) {
  if (IntrinsicID == 0)
    return nullptr;
  StringRef Name = Intrinsic::getName(static_cast<Intrinsic::ID>(IntrinsicID));
  char *Buf = static_cast<char *>(bridgeAlloc(Name.size() + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  std::memcpy(Buf, Name.data(), Name.size());
  Buf[Name.size()] = '\0';
  return Buf;
}

static int bridgeIntrinsicIsOverloaded(unsigned IntrinsicID) {
  if (IntrinsicID == 0)
    return 0;
  return Intrinsic::isOverloaded(static_cast<Intrinsic::ID>(IntrinsicID));
}

static unsigned bridgeIntrinsicLookupByName(const char *Name) {
  if (LLVM_UNLIKELY(!Name))
    return 0;
  auto ID = Function::lookupIntrinsicID(Name);
  return static_cast<unsigned>(ID);
}

// ===----------------------------------------------------------------------===
//  NamedMetadata
// ===----------------------------------------------------------------------===

static NevercNamedMDRef bridgeModuleGetNamedMetadata(NevercModuleRef M,
                                                      const char *Name) {
  if (LLVM_UNLIKELY(!M || !Name))
    return nullptr;
  return wrapNMD(unwrap(M)->getNamedMetadata(Name));
}

static NevercNamedMDRef bridgeModuleGetOrInsertNamedMetadata(NevercModuleRef M,
                                                              const char *Name) {
  if (LLVM_UNLIKELY(!M || !Name))
    return nullptr;
  return wrapNMD(unwrap(M)->getOrInsertNamedMetadata(Name));
}

static unsigned bridgeNamedMDGetNumOperands(NevercNamedMDRef NMD) {
  if (LLVM_UNLIKELY(!NMD))
    return 0;
  return unwrapNMD(NMD)->getNumOperands();
}

static NevercMetadataRef bridgeNamedMDGetOperand(NevercNamedMDRef NMD,
                                                  unsigned Idx) {
  if (LLVM_UNLIKELY(!NMD))
    return nullptr;
  auto *Node = unwrapNMD(NMD);
  if (Idx >= Node->getNumOperands())
    return nullptr;
  return wrapMD(Node->getOperand(Idx));
}

static void bridgeNamedMDAddOperand(NevercNamedMDRef NMD,
                                     NevercMetadataRef MD) {
  if (LLVM_UNLIKELY(!NMD || !MD))
    return;
  auto *Node = dyn_cast<MDNode>(unwrapMD(MD));
  if (LLVM_UNLIKELY(!Node)) {
    WithColor::warning(errs(), "neverc-plugin")
        << "NamedMDAddOperand: expected MDNode, got other Metadata kind\n";
    return;
  }
  unwrapNMD(NMD)->addOperand(Node);
}

// ===----------------------------------------------------------------------===
//  MetadataAsValue / ValueAsMetadata conversions
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeMetadataAsValue(NevercContextRef C,
                                             NevercMetadataRef MD) {
  if (LLVM_UNLIKELY(!C || !MD))
    return nullptr;
  return wrapV(MetadataAsValue::get(*unwrapCtx(C), unwrapMD(MD)));
}

static NevercMetadataRef bridgeValueAsMetadata(NevercValueRef V) {
  if (LLVM_UNLIKELY(!V))
    return nullptr;
  Value *Val = unwrapV(V);
  if (auto *MAV = dyn_cast<MetadataAsValue>(Val))
    return wrapMD(MAV->getMetadata());
  if (auto *C = dyn_cast<Constant>(Val))
    return wrapMD(ConstantAsMetadata::get(C));
  return wrapMD(ValueAsMetadata::get(Val));
}

// ===----------------------------------------------------------------------===
//  BasicBlock reordering
// ===----------------------------------------------------------------------===

static void bridgeBBMoveAfter(NevercBasicBlockRef BB,
                               NevercBasicBlockRef AfterBB) {
  if (LLVM_UNLIKELY(!BB || !AfterBB))
    return;
  auto *Block = unwrapBB(BB);
  auto *After = unwrapBB(AfterBB);
  if (Block->getParent() != After->getParent())
    return;
  Block->moveAfter(After);
}

static void bridgeBBMoveBefore(NevercBasicBlockRef BB,
                                NevercBasicBlockRef BeforeBB) {
  if (LLVM_UNLIKELY(!BB || !BeforeBB))
    return;
  auto *Block = unwrapBB(BB);
  auto *Before = unwrapBB(BeforeBB);
  if (Block->getParent() != Before->getParent())
    return;
  Block->moveBefore(Before);
}

// ===----------------------------------------------------------------------===
//  Memory intrinsic builders (memcpy, memset, memmove)
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildMemCpy(NevercBuilderRef B,
                                         NevercValueRef Dst, unsigned DstAlign,
                                         NevercValueRef Src, unsigned SrcAlign,
                                         NevercValueRef Len, int IsVolatile) {
  if (LLVM_UNLIKELY(!B || !Dst || !Src || !Len))
    return nullptr;
  auto *Builder = unwrapB(B);
  return wrapV(Builder->CreateMemCpy(
      unwrapV(Dst), MaybeAlign(DstAlign),
      unwrapV(Src), MaybeAlign(SrcAlign),
      unwrapV(Len), IsVolatile != 0));
}

static NevercValueRef bridgeBuildMemSet(NevercBuilderRef B,
                                         NevercValueRef Dst, unsigned DstAlign,
                                         NevercValueRef Val,
                                         NevercValueRef Len, int IsVolatile) {
  if (LLVM_UNLIKELY(!B || !Dst || !Val || !Len))
    return nullptr;
  auto *Builder = unwrapB(B);
  return wrapV(Builder->CreateMemSet(
      unwrapV(Dst), unwrapV(Val),
      unwrapV(Len), MaybeAlign(DstAlign), IsVolatile != 0));
}

static NevercValueRef bridgeBuildMemMove(NevercBuilderRef B,
                                          NevercValueRef Dst, unsigned DstAlign,
                                          NevercValueRef Src, unsigned SrcAlign,
                                          NevercValueRef Len, int IsVolatile) {
  if (LLVM_UNLIKELY(!B || !Dst || !Src || !Len))
    return nullptr;
  auto *Builder = unwrapB(B);
  return wrapV(Builder->CreateMemMove(
      unwrapV(Dst), MaybeAlign(DstAlign),
      unwrapV(Src), MaybeAlign(SrcAlign),
      unwrapV(Len), IsVolatile != 0));
}

// ===----------------------------------------------------------------------===
//  DataLayout queries
// ===----------------------------------------------------------------------===

static uint64_t bridgeTypeSizeInBits(NevercModuleRef M, NevercTypeRef T) {
  if (LLVM_UNLIKELY(!M || !T))
    return 0;
  auto *Ty = unwrapTy(T);
  if (LLVM_UNLIKELY(!Ty->isSized()))
    return 0;
  return unwrap(M)->getDataLayout().getTypeSizeInBits(Ty);
}

static uint64_t bridgeTypeAllocSize(NevercModuleRef M, NevercTypeRef T) {
  if (LLVM_UNLIKELY(!M || !T))
    return 0;
  auto *Ty = unwrapTy(T);
  if (LLVM_UNLIKELY(!Ty->isSized()))
    return 0;
  return unwrap(M)->getDataLayout().getTypeAllocSize(Ty).getFixedValue();
}

static uint64_t bridgeTypeStoreSize(NevercModuleRef M, NevercTypeRef T) {
  if (LLVM_UNLIKELY(!M || !T))
    return 0;
  auto *Ty = unwrapTy(T);
  if (LLVM_UNLIKELY(!Ty->isSized()))
    return 0;
  return unwrap(M)->getDataLayout().getTypeStoreSize(Ty).getFixedValue();
}

static unsigned bridgeTypeABIAlignment(NevercModuleRef M, NevercTypeRef T) {
  if (LLVM_UNLIKELY(!M || !T))
    return 0;
  auto *Ty = unwrapTy(T);
  if (LLVM_UNLIKELY(!Ty->isSized()))
    return 0;
  return unwrap(M)->getDataLayout().getABITypeAlign(Ty).value();
}

static unsigned bridgeTypePrefAlignment(NevercModuleRef M, NevercTypeRef T) {
  if (LLVM_UNLIKELY(!M || !T))
    return 0;
  auto *Ty = unwrapTy(T);
  if (LLVM_UNLIKELY(!Ty->isSized()))
    return 0;
  return unwrap(M)->getDataLayout().getPrefTypeAlign(Ty).value();
}

static unsigned bridgePointerSizeInBits(NevercModuleRef M,
                                        unsigned AddrSpace) {
  if (LLVM_UNLIKELY(!M))
    return 0;
  return unwrap(M)->getDataLayout().getPointerSizeInBits(AddrSpace);
}

// ===----------------------------------------------------------------------===
//  PoisonValue
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeConstPoison(NevercTypeRef Ty) {
  if (LLVM_UNLIKELY(!Ty))
    return nullptr;
  return wrapV(PoisonValue::get(unwrapTy(Ty)));
}

static int bridgeValueIsPoison(NevercValueRef V) {
  return V && isa<PoisonValue>(unwrapV(V));
}

// ===----------------------------------------------------------------------===
//  Address-space-aware pointer types
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeTypeGetPtrInAddrSpace(NevercContextRef C,
                                                  unsigned AddrSpace) {
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  return wrapTy(PointerType::get(*unwrapCtx(C), AddrSpace));
}

static unsigned bridgeTypeGetPointerAddrSpace(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T))
    return 0;
  auto *PT = dyn_cast<PointerType>(unwrapTy(T));
  return PT ? PT->getAddressSpace() : 0;
}

// ===----------------------------------------------------------------------===
//  Vector types
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeTypeGetFixedVector(NevercTypeRef ElemTy,
                                               unsigned Count) {
  if (LLVM_UNLIKELY(!ElemTy || Count == 0))
    return nullptr;
  return wrapTy(FixedVectorType::get(unwrapTy(ElemTy), Count));
}

static int bridgeTypeIsVector(NevercTypeRef T) {
  return T && unwrapTy(T)->isVectorTy();
}

static unsigned bridgeTypeGetVectorNumElements(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T))
    return 0;
  auto *VT = dyn_cast<FixedVectorType>(unwrapTy(T));
  return VT ? VT->getNumElements() : 0;
}

static NevercTypeRef bridgeTypeGetVectorElementType(NevercTypeRef T) {
  if (LLVM_UNLIKELY(!T))
    return nullptr;
  auto *VT = dyn_cast<VectorType>(unwrapTy(T));
  return VT ? wrapTy(VT->getElementType()) : nullptr;
}

static NevercValueRef bridgeConstVector(NevercValueRef *Vals,
                                         unsigned Count) {
  if (LLVM_UNLIKELY(!Vals || Count == 0))
    return nullptr;
  SmallVector<Constant *, 8> Elems;
  for (unsigned I = 0; I < Count; ++I) {
    if (LLVM_UNLIKELY(!Vals[I]))
      return nullptr;
    auto *C = dyn_cast<Constant>(unwrapV(Vals[I]));
    if (LLVM_UNLIKELY(!C))
      return nullptr;
    Elems.push_back(C);
  }
  return wrapV(ConstantVector::get(Elems));
}

// ===----------------------------------------------------------------------===
//  Vector element operations
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildExtractElement(NevercBuilderRef B,
                                                NevercValueRef Vec,
                                                NevercValueRef Idx,
                                                const char *Name) {
  if (LLVM_UNLIKELY(!B || !Vec || !Idx))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateExtractElement(unwrapV(Vec), unwrapV(Idx),
                                       Name ? Name : ""));
}

static NevercValueRef bridgeBuildInsertElement(NevercBuilderRef B,
                                               NevercValueRef Vec,
                                               NevercValueRef Val,
                                               NevercValueRef Idx,
                                               const char *Name) {
  if (LLVM_UNLIKELY(!B || !Vec || !Val || !Idx))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateInsertElement(unwrapV(Vec), unwrapV(Val),
                                      unwrapV(Idx), Name ? Name : ""));
}

static NevercValueRef bridgeBuildShuffleVector(NevercBuilderRef B,
                                               NevercValueRef V1,
                                               NevercValueRef V2,
                                               int *MaskVals,
                                               unsigned MaskLen,
                                               const char *Name) {
  if (LLVM_UNLIKELY(!B || !V1 || !V2 || !MaskVals || MaskLen == 0))
    return nullptr;
  SmallVector<int, 16> Mask(MaskVals, MaskVals + MaskLen);
  return wrapV(
      unwrapB(B)->CreateShuffleVector(unwrapV(V1), unwrapV(V2), Mask,
                                      Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Freeze instruction
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildFreeze(NevercBuilderRef B, NevercValueRef V,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateFreeze(unwrapV(V), Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  AddrSpaceCast
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildAddrSpaceCast(NevercBuilderRef B,
                                               NevercValueRef V,
                                               NevercTypeRef DestTy,
                                               const char *Name) {
  if (LLVM_UNLIKELY(!B || !V || !DestTy))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateAddrSpaceCast(unwrapV(V), unwrapTy(DestTy),
                                      Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Aggregate zero initializer
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeConstZeroInitializer(NevercTypeRef Ty) {
  if (LLVM_UNLIKELY(!Ty))
    return nullptr;
  return wrapV(ConstantAggregateZero::get(unwrapTy(Ty)));
}

// ===----------------------------------------------------------------------===
//  GlobalValue unnamed_addr
// ===----------------------------------------------------------------------===

static int bridgeGlobalHasUnnamedAddr(NevercValueRef GV) {
  if (LLVM_UNLIKELY(!GV))
    return 0;
  auto *G = dyn_cast<GlobalValue>(unwrapV(GV));
  return G && G->hasGlobalUnnamedAddr();
}

static void bridgeGlobalSetUnnamedAddr(NevercValueRef GV,
                                       int HasUnnamedAddr) {
  if (LLVM_UNLIKELY(!GV))
    return;
  auto *G = dyn_cast<GlobalValue>(unwrapV(GV));
  if (G)
    G->setUnnamedAddr(HasUnnamedAddr ? GlobalValue::UnnamedAddr::Global
                                     : GlobalValue::UnnamedAddr::None);
}

// ===----------------------------------------------------------------------===
//  String utilities
// ===----------------------------------------------------------------------===

// Two-digit lookup: "00010203...9899" -- halves the number of divisions in
// integer-to-string conversion compared to single-digit extraction.
static const char kDigitPairs[201] =
    "00010203040506070809"
    "10111213141516171819"
    "20212223242526272829"
    "30313233343536373839"
    "40414243444546474849"
    "50515253545556575859"
    "60616263646566676869"
    "70717273747576777879"
    "80818283848586878889"
    "90919293949596979899";

static uint64_t bridgeStrLen(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return 0;
  return std::strlen(S);
}

static char *bridgeStrDup(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *Buf = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  std::memcpy(Buf, S, Len + 1);
  return Buf;
}

static char *bridgeStrConcat(const char *A, const char *B) {
  size_t LenA = A ? std::strlen(A) : 0;
  size_t LenB = B ? std::strlen(B) : 0;
  if (LLVM_UNLIKELY(LenB > SIZE_MAX - 1 || LenA > SIZE_MAX - 1 - LenB))
    return nullptr;
  size_t TotalLen = LenA + LenB;
  char *Buf = static_cast<char *>(bridgeAlloc(TotalLen + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (LenA)
    std::memcpy(Buf, A, LenA);
  if (LenB)
    std::memcpy(Buf + LenA, B, LenB);
  Buf[TotalLen] = '\0';
  return Buf;
}

static int bridgeStrEqual(const char *A, const char *B) {
  if (A == B)
    return 1;
  if (LLVM_UNLIKELY(!A || !B))
    return 0;
  return std::strcmp(A, B) == 0;
}

static char *bridgeIntToStr(int64_t Val) {
  char Tmp[21];
  char *End = Tmp + 20;
  *End = '\0';
  bool Neg = Val < 0;
  uint64_t U = Neg ? -static_cast<uint64_t>(Val) : static_cast<uint64_t>(Val);

  char *P = End;
  while (U >= 100) {
    unsigned Idx = static_cast<unsigned>(U % 100) * 2;
    U /= 100;
    *--P = kDigitPairs[Idx + 1];
    *--P = kDigitPairs[Idx];
  }
  if (U >= 10) {
    unsigned Idx = static_cast<unsigned>(U) * 2;
    *--P = kDigitPairs[Idx + 1];
    *--P = kDigitPairs[Idx];
  } else {
    *--P = static_cast<char>('0' + U);
  }
  if (Neg)
    *--P = '-';

  size_t Len = static_cast<size_t>(End - P);
  char *Buf = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  std::memcpy(Buf, P, Len + 1);
  return Buf;
}

static char *bridgeUIntToStr(uint64_t Val) {
  char Tmp[21];
  char *End = Tmp + 20;
  *End = '\0';

  char *P = End;
  while (Val >= 100) {
    unsigned Idx = static_cast<unsigned>(Val % 100) * 2;
    Val /= 100;
    *--P = kDigitPairs[Idx + 1];
    *--P = kDigitPairs[Idx];
  }
  if (Val >= 10) {
    unsigned Idx = static_cast<unsigned>(Val) * 2;
    *--P = kDigitPairs[Idx + 1];
    *--P = kDigitPairs[Idx];
  } else {
    *--P = static_cast<char>('0' + Val);
  }

  size_t Len = static_cast<size_t>(End - P);
  char *Buf = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  std::memcpy(Buf, P, Len + 1);
  return Buf;
}

static char *bridgeStrFormatV(const char *Fmt, va_list Args) {
  if (LLVM_UNLIKELY(!Fmt))
    return nullptr;
  char Stack[1024];
  va_list ArgsCopy;
  va_copy(ArgsCopy, Args);
  int Len = std::vsnprintf(Stack, sizeof(Stack), Fmt, ArgsCopy);
  va_end(ArgsCopy);
  if (LLVM_UNLIKELY(Len < 0))
    return nullptr;
  size_t Need = static_cast<size_t>(Len) + 1;
  char *Buf = static_cast<char *>(bridgeAlloc(Need));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (Need <= sizeof(Stack)) {
    std::memcpy(Buf, Stack, Need);
  } else {
    va_list Args2;
    va_copy(Args2, Args);
    std::vsnprintf(Buf, Need, Fmt, Args2);
    va_end(Args2);
  }
  return Buf;
}

static char *bridgeStrFormat(const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  char *Result = bridgeStrFormatV(Fmt, Args);
  va_end(Args);
  return Result;
}

// ===----------------------------------------------------------------------===
//  Raw memory utilities
// ===----------------------------------------------------------------------===

static void bridgeMemCopy(void *Dst, const void *Src, uint64_t Len) {
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  if (LLVM_LIKELY(Dst && Src && Len))
    std::memcpy(Dst, Src, static_cast<size_t>(Len));
}

static void bridgeMemSet(void *Dst, int Val, uint64_t Len) {
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  if (LLVM_LIKELY(Dst && Len))
    std::memset(Dst, Val, static_cast<size_t>(Len));
}

static void bridgeMemMove(void *Dst, const void *Src, uint64_t Len) {
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  if (LLVM_LIKELY(Dst && Src && Len))
    std::memmove(Dst, Src, static_cast<size_t>(Len));
}

// ===----------------------------------------------------------------------===
//  Byte-reverse copy.  Two supported modes:
//    1. Dst == Src           -- in-place reversal (meet-in-the-middle swap)
//    2. Dst and Src disjoint -- forward reverse-copy
//  Partial overlap (Dst != Src but ranges intersect) is undefined behaviour
//  per the API contract and treated as a no-op here, matching how the libc
//  memcpy contract handles overlapping regions.  Caller must use a separate
//  buffer for that case.
// ===----------------------------------------------------------------------===

static void bridgeMemReverse(void *Dst, const void *Src, uint64_t Len) {
  if (LLVM_UNLIKELY(!Dst || !Src || Len < 2))
    return;
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  auto *D = static_cast<unsigned char *>(Dst);
  const auto *S = static_cast<const unsigned char *>(Src);
  size_t N = static_cast<size_t>(Len);
  if (D == S) {
    for (size_t I = 0, J = N - 1; I < J; ++I, --J) {
      unsigned char T = D[I];
      D[I] = D[J];
      D[J] = T;
    }
    return;
  }
  // Reject partial overlap (UB per contract).  Pointer comparison across
  // distinct objects is implementation-defined but works on every platform
  // we ship for, and a false positive here just costs the caller the data
  // they would have read anyway.
  if (LLVM_UNLIKELY(D + N > S && S + N > D))
    return;
  for (size_t I = 0; I < N; ++I)
    D[I] = S[N - 1 - I];
}

static int bridgeMemCompare(const void *A, const void *B, uint64_t Len) {
  if (LLVM_UNLIKELY(!A || !B || !Len))
    return 0;
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return 0;
  return std::memcmp(A, B, static_cast<size_t>(Len));
}

// ===----------------------------------------------------------------------===
//  Formatted diagnostics (printf-style, one-call format + emit)
// ===----------------------------------------------------------------------===

static void bridgeDiagV(void (*Emit)(const char *), const char *Fmt,
                        va_list Args) {
  if (LLVM_UNLIKELY(!Fmt))
    return;
  char Stack[1024];
  va_list ArgsCopy;
  va_copy(ArgsCopy, Args);
  int Len = std::vsnprintf(Stack, sizeof(Stack), Fmt, ArgsCopy);
  va_end(ArgsCopy);
  if (LLVM_UNLIKELY(Len < 0))
    return;
  size_t Need = static_cast<size_t>(Len) + 1;
  if (Need <= sizeof(Stack)) {
    Emit(Stack);
    return;
  }
  char *Heap = static_cast<char *>(bridgeAlloc(Need));
  if (LLVM_UNLIKELY(!Heap))
    return;
  va_list Args2;
  va_copy(Args2, Args);
  std::vsnprintf(Heap, Need, Fmt, Args2);
  va_end(Args2);
  Emit(Heap);
  bridgeFree(Heap);
}

static void bridgeDiagNoteV(const char *Fmt, va_list Args) {
  bridgeDiagV(bridgeDiagNote, Fmt, Args);
}

static void bridgeDiagWarningV(const char *Fmt, va_list Args) {
  bridgeDiagV(bridgeDiagWarning, Fmt, Args);
}

static void bridgeDiagErrorV(const char *Fmt, va_list Args) {
  bridgeDiagV(bridgeDiagError, Fmt, Args);
}

static void bridgeDiagNoteF(const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  bridgeDiagNoteV(Fmt, Args);
  va_end(Args);
}

static void bridgeDiagWarningF(const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  bridgeDiagWarningV(Fmt, Args);
  va_end(Args);
}

static void bridgeDiagErrorF(const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  bridgeDiagErrorV(Fmt, Args);
  va_end(Args);
}

// ===----------------------------------------------------------------------===
//  Zero-initialized allocation
// ===----------------------------------------------------------------------===

static void *bridgeAllocZeroed(uint64_t Count, uint64_t ElemSize) {
  if (LLVM_UNLIKELY(exceedsSizeT(Count) || exceedsSizeT(ElemSize)))
    return nullptr;
  if (LLVM_UNLIKELY(ElemSize && Count > SIZE_MAX / ElemSize))
    return nullptr;
  return ::calloc(static_cast<size_t>(Count), static_cast<size_t>(ElemSize));
}

// ===----------------------------------------------------------------------===
//  Extended string operations
// ===----------------------------------------------------------------------===

static int bridgeStrStartsWith(const char *S, const char *Prefix) {
  if (LLVM_UNLIKELY(!S || !Prefix))
    return 0;
  for (;; ++S, ++Prefix) {
    if (*Prefix == '\0')
      return 1;
    if (*S != *Prefix)
      return 0;
  }
}

static int bridgeStrEndsWith(const char *S, const char *Suffix) {
  if (LLVM_UNLIKELY(!S || !Suffix))
    return 0;
  size_t SLen = std::strlen(S);
  size_t XLen = std::strlen(Suffix);
  if (XLen > SLen)
    return 0;
  return std::memcmp(S + SLen - XLen, Suffix, XLen) == 0;
}

static int bridgeStrContains(const char *Haystack, const char *Needle) {
  if (LLVM_UNLIKELY(!Haystack))
    return 0;
  if (LLVM_UNLIKELY(!Needle || !*Needle))
    return 1;
  return std::strstr(Haystack, Needle) != nullptr;
}

static int bridgeStrCompare(const char *A, const char *B) {
  if (A == B)
    return 0;
  if (LLVM_UNLIKELY(!A))
    return -1;
  if (LLVM_UNLIKELY(!B))
    return 1;
  return std::strcmp(A, B);
}

static inline bool bridgeIsWhitespace(unsigned char C) {
  return C == ' ' || C == '\t' || C == '\n' || C == '\r' || C == '\f' ||
         C == '\v';
}

static int bridgeStrToInt64(const char *S, int64_t *Out) {
  if (LLVM_UNLIKELY(!S || !Out))
    return 0;
  char *End = nullptr;
  errno = 0;
  long long Val = std::strtoll(S, &End, 10);
  if (errno != 0 || End == S || *End != '\0')
    return 0;
  *Out = static_cast<int64_t>(Val);
  return 1;
}

static int bridgeStrToUInt64(const char *S, uint64_t *Out) {
  if (LLVM_UNLIKELY(!S || !Out))
    return 0;
  const char *P = S;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*P)))
    ++P;
  if (*P == '-')
    return 0;
  char *End = nullptr;
  errno = 0;
  unsigned long long Val = std::strtoull(P, &End, 10);
  if (errno != 0 || End == P || *End != '\0')
    return 0;
  *Out = static_cast<uint64_t>(Val);
  return 1;
}

static char *bridgeStrSubstring(const char *S, uint64_t Start, uint64_t Len) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  size_t SLen = std::strlen(S);
  if (Start >= SLen)
    Len = 0;
  else if (Len > SLen - Start)
    Len = SLen - Start;
  char *Buf = static_cast<char *>(bridgeAlloc(static_cast<size_t>(Len) + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (Len)
    std::memcpy(Buf, S + Start, static_cast<size_t>(Len));
  Buf[Len] = '\0';
  return Buf;
}

static char *bridgeStrTrim(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*S)))
    ++S;
  size_t Len = std::strlen(S);
  while (Len > 0 && bridgeIsWhitespace(static_cast<unsigned char>(S[Len - 1])))
    --Len;
  char *Buf = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (Len)
    std::memcpy(Buf, S, Len);
  Buf[Len] = '\0';
  return Buf;
}

// ===----------------------------------------------------------------------===
//  Convenience zero-fill
// ===----------------------------------------------------------------------===

static void bridgeMemZero(void *Dst, uint64_t Len) {
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  if (LLVM_LIKELY(Dst && Len))
    std::memset(Dst, 0, static_cast<size_t>(Len));
}

// ===----------------------------------------------------------------------===
//  Character search
// ===----------------------------------------------------------------------===

// Reject C values that collapse to '\0' after the implicit (char) truncation
// performed by std::strchr/strrchr -- otherwise StrFindChar(s, 256) would
// "find" the null terminator and report strlen(s) instead of NotFound.
static uint64_t bridgeStrFindChar(const char *S, int C) {
  if (LLVM_UNLIKELY(!S))
    return UINT64_MAX;
  unsigned char Needle = static_cast<unsigned char>(C);
  if (LLVM_UNLIKELY(Needle == 0))
    return UINT64_MAX;
  const char *P = std::strchr(S, Needle);
  if (!P)
    return UINT64_MAX;
  return static_cast<uint64_t>(P - S);
}

static uint64_t bridgeStrFindLastChar(const char *S, int C) {
  if (LLVM_UNLIKELY(!S))
    return UINT64_MAX;
  unsigned char Needle = static_cast<unsigned char>(C);
  if (LLVM_UNLIKELY(Needle == 0))
    return UINT64_MAX;
  const char *P = std::strrchr(S, Needle);
  if (!P)
    return UINT64_MAX;
  return static_cast<uint64_t>(P - S);
}

// ===----------------------------------------------------------------------===
//  Overflow-safe array reallocation
// ===----------------------------------------------------------------------===

static void *bridgeReallocArray(void *Ptr, uint64_t Count, uint64_t ElemSize) {
  size_t Total = checkedArraySize(Count, ElemSize);
  if (LLVM_UNLIKELY(Total == 0 && Count != 0 && ElemSize != 0))
    return nullptr;
  if (Total == 0)
    Total = 1;
  return bridgeRealloc(Ptr, Total);
}

// ===----------------------------------------------------------------------===
//  Batch collection
// ===----------------------------------------------------------------------===

static void bridgeModuleCollectFunctions(NevercModuleRef M,
                                         NevercValueRef *Out) {
  if (LLVM_UNLIKELY(!M || !Out))
    return;
  unsigned Idx = 0;
  for (auto &F : *unwrap(M))
    Out[Idx++] = wrapV(&F);
}

static void bridgeModuleCollectGlobals(NevercModuleRef M,
                                       NevercValueRef *Out) {
  if (LLVM_UNLIKELY(!M || !Out))
    return;
  unsigned Idx = 0;
  for (auto &G : unwrap(M)->globals())
    Out[Idx++] = wrapV(&G);
}

static unsigned bridgeModuleGetAliasCount(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return 0;
  return unwrap(M)->alias_size();
}

static void bridgeModuleCollectAliases(NevercModuleRef M,
                                       NevercValueRef *Out) {
  if (LLVM_UNLIKELY(!M || !Out))
    return;
  unsigned Idx = 0;
  for (auto &A : unwrap(M)->aliases())
    Out[Idx++] = wrapV(&A);
}

static NevercValueRef *
bridgeModuleCollectAllFunctions(NevercModuleRef M, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  size_t Count = Mod->size();
  if (LLVM_UNLIKELY(Count == 0 || Count > UINT_MAX))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(
      bridgeAlloc(static_cast<uint64_t>(Count) * sizeof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &F : *Mod)
    Buf[Idx++] = wrapV(&F);
  *OutCount = Idx;
  return Buf;
}

static NevercValueRef *
bridgeModuleCollectAllGlobals(NevercModuleRef M, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  size_t Count = Mod->global_size();
  if (LLVM_UNLIKELY(Count == 0 || Count > UINT_MAX))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(
      bridgeAlloc(static_cast<uint64_t>(Count) * sizeof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &G : Mod->globals())
    Buf[Idx++] = wrapV(&G);
  *OutCount = Idx;
  return Buf;
}

static NevercValueRef *
bridgeModuleCollectAllInstructions(NevercModuleRef M, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);

  // Two-pass: count via BB.size() (O(1) per BB), then exact alloc + fill.
  // BB.size() is O(1) in modern LLVM, so the count pass only touches
  // Function and BasicBlock nodes -- never instruction nodes.  This beats
  // geometric growth because it eliminates all realloc copies.
  size_t Total = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      Total += BB.size();
  }
  if (LLVM_UNLIKELY(Total == 0 || Total > UINT_MAX))
    return nullptr;

  auto *Buf = static_cast<NevercValueRef *>(
      bridgeAlloc(static_cast<uint64_t>(Total) * sizeof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;

  unsigned Idx = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      for (auto &I : BB)
        Buf[Idx++] = wrapV(&I);
  }

  *OutCount = Idx;
  return Buf;
}

// ===----------------------------------------------------------------------===
//  StrJoin -- concatenate an array of strings with a separator
// ===----------------------------------------------------------------------===

static char *bridgeStrJoin(const char *const *Strings, unsigned Count,
                           const char *Sep) {
  if (Count == 0) {
    char *Empty = static_cast<char *>(bridgeAlloc(1));
    if (Empty)
      Empty[0] = '\0';
    return Empty;
  }
  if (LLVM_UNLIKELY(!Strings))
    return nullptr;

  size_t SepLen = (Sep && *Sep) ? std::strlen(Sep) : 0;

  size_t StackLens[64];
  size_t *Lens = StackLens;
  if (LLVM_UNLIKELY(Count > 64)) {
    Lens = static_cast<size_t *>(
        bridgeAlloc(static_cast<uint64_t>(Count) * sizeof(size_t)));
    if (LLVM_UNLIKELY(!Lens))
      return nullptr;
  }

  size_t Total = 0;
  for (unsigned I = 0; I < Count; ++I) {
    Lens[I] = Strings[I] ? std::strlen(Strings[I]) : 0;
    // Two-step overflow guard: catch the case where Lens[I] itself is
    // pathologically close to SIZE_MAX before computing SIZE_MAX-1-Lens[I],
    // which would otherwise wrap around for Lens[I] >= SIZE_MAX-1.
    if (LLVM_UNLIKELY(Lens[I] > SIZE_MAX - 1 ||
                      Total > SIZE_MAX - 1 - Lens[I])) {
      if (Lens != StackLens)
        bridgeFree(Lens);
      return nullptr;
    }
    Total += Lens[I];
    if (I > 0 && SepLen) {
      if (LLVM_UNLIKELY(SepLen > SIZE_MAX - 1 ||
                        Total > SIZE_MAX - 1 - SepLen)) {
        if (Lens != StackLens)
          bridgeFree(Lens);
        return nullptr;
      }
      Total += SepLen;
    }
  }

  char *Buf = static_cast<char *>(bridgeAlloc(Total + 1));
  if (LLVM_UNLIKELY(!Buf)) {
    if (Lens != StackLens)
      bridgeFree(Lens);
    return nullptr;
  }

  char *Dst = Buf;
  for (unsigned I = 0; I < Count; ++I) {
    if (I > 0 && SepLen) {
      std::memcpy(Dst, Sep, SepLen);
      Dst += SepLen;
    }
    if (Lens[I]) {
      std::memcpy(Dst, Strings[I], Lens[I]);
      Dst += Lens[I];
    }
  }
  *Dst = '\0';

  if (Lens != StackLens)
    bridgeFree(Lens);
  return Buf;
}

// ===----------------------------------------------------------------------===
//  StrSplit -- split a string by delimiter into an array of strings
// ===----------------------------------------------------------------------===

static char **bridgeStrSplit(const char *S, const char *Delim,
                             unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!S || !OutCount))
    return nullptr;

  if (LLVM_UNLIKELY(!Delim || !*Delim)) {
    char **Arr = static_cast<char **>(bridgeAlloc(sizeof(char *)));
    if (LLVM_UNLIKELY(!Arr))
      return nullptr;
    Arr[0] = bridgeStrDup(S);
    if (LLVM_UNLIKELY(!Arr[0])) {
      bridgeFree(Arr);
      return nullptr;
    }
    *OutCount = 1;
    return Arr;
  }

  size_t DelimLen = std::strlen(Delim);

  // Single pass: collect delimiter hit offsets into a stack-local buffer,
  // spilling to the heap only when there are many hits.  This avoids
  // scanning the string twice (count pass + split pass).
  size_t StackHits[64];
  size_t *Hits = StackHits;
  size_t HitCap = sizeof(StackHits) / sizeof(StackHits[0]);
  size_t HitCount = 0;

  const char *P = S;
  while ((P = std::strstr(P, Delim)) != nullptr) {
    if (HitCount == HitCap) {
      if (HitCap > (SIZE_MAX / sizeof(size_t)) / 2) {
        if (Hits != StackHits)
          bridgeFree(Hits);
        return nullptr;
      }
      size_t NewCap = HitCap * 2;
      size_t *NewBuf;
      if (Hits == StackHits) {
        NewBuf =
            static_cast<size_t *>(bridgeAlloc(NewCap * sizeof(size_t)));
        if (LLVM_LIKELY(NewBuf))
          std::memcpy(NewBuf, StackHits, HitCount * sizeof(size_t));
      } else {
        NewBuf =
            static_cast<size_t *>(bridgeRealloc(Hits, NewCap * sizeof(size_t)));
      }
      if (LLVM_UNLIKELY(!NewBuf)) {
        if (Hits != StackHits)
          bridgeFree(Hits);
        return nullptr;
      }
      Hits = NewBuf;
      HitCap = NewCap;
    }
    Hits[HitCount++] = static_cast<size_t>(P - S);
    P += DelimLen;
  }

  if (LLVM_UNLIKELY(HitCount >= UINT_MAX)) {
    if (Hits != StackHits)
      bridgeFree(Hits);
    return nullptr;
  }
  unsigned Parts = static_cast<unsigned>(HitCount) + 1;
  char **Arr = static_cast<char **>(
      bridgeAlloc(static_cast<uint64_t>(Parts) * sizeof(char *)));
  if (LLVM_UNLIKELY(!Arr)) {
    if (Hits != StackHits)
      bridgeFree(Hits);
    return nullptr;
  }

  // Build parts using recorded offsets -- no second strstr scan.
  size_t Prev = 0;
  unsigned Idx = 0;
  for (size_t I = 0; I < HitCount; ++I) {
    size_t Span = Hits[I] - Prev;
    char *Part = static_cast<char *>(bridgeAlloc(Span + 1));
    if (LLVM_UNLIKELY(!Part)) {
      for (unsigned J = 0; J < Idx; ++J)
        bridgeFree(Arr[J]);
      bridgeFree(Arr);
      if (Hits != StackHits)
        bridgeFree(Hits);
      return nullptr;
    }
    if (Span)
      std::memcpy(Part, S + Prev, Span);
    Part[Span] = '\0';
    Arr[Idx++] = Part;
    Prev = Hits[I] + DelimLen;
  }

  if (Hits != StackHits)
    bridgeFree(Hits);

  // Tail after the last delimiter.
  Arr[Idx] = bridgeStrDup(S + Prev);
  if (LLVM_UNLIKELY(!Arr[Idx])) {
    for (unsigned J = 0; J < Idx; ++J)
      bridgeFree(Arr[J]);
    bridgeFree(Arr);
    return nullptr;
  }
  *OutCount = Parts;
  return Arr;
}

// ===----------------------------------------------------------------------===
//  StrHash -- xxh3 64-bit hash (SIMD-accelerated when available)
// ===----------------------------------------------------------------------===

static uint64_t bridgeStrHash(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return 0;
  return llvm::xxh3_64bits(StringRef(S));
}

static void bridgeFunctionCollectBBs(NevercValueRef F,
                                     NevercBasicBlockRef *Out) {
  if (LLVM_UNLIKELY(!F || !Out))
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return;
  unsigned Idx = 0;
  for (auto &BB : *Fn)
    Out[Idx++] = wrapBB(&BB);
}

static void bridgeBBCollectInstructions(NevercBasicBlockRef BB,
                                        NevercValueRef *Out) {
  if (LLVM_UNLIKELY(!BB || !Out))
    return;
  unsigned Idx = 0;
  for (auto &Inst : *unwrapBB(BB))
    Out[Idx++] = wrapV(&Inst);
}

// ===----------------------------------------------------------------------===
//  Batch opcode collection -- direct-iterate without wrap/unwrap of every
//  Instruction handle.  Cheaper than collect-then-foreach because the
//  call to InstGetOpcode does an unwrap each time; we avoid that hop.
// ===----------------------------------------------------------------------===

static unsigned bridgeBBCollectOpcodes(NevercBasicBlockRef BB,
                                       unsigned *OutOpcodes) {
  if (LLVM_UNLIKELY(!BB || !OutOpcodes))
    return 0;
  unsigned Idx = 0;
  for (auto &Inst : *unwrapBB(BB))
    OutOpcodes[Idx++] = Inst.getOpcode();
  return Idx;
}

static unsigned *bridgeModuleCollectAllOpcodes(NevercModuleRef M,
                                               unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);

  // Same exact-count approach as bridgeModuleCollectAllInstructions:
  // BB::size() is O(1) so the count pass touches only Function and BB
  // nodes; the fill pass walks instructions exactly once.
  size_t Total = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      Total += BB.size();
  }
  if (LLVM_UNLIKELY(Total == 0 || Total > UINT_MAX))
    return nullptr;

  auto *Buf = static_cast<unsigned *>(
      bridgeAlloc(static_cast<uint64_t>(Total) * sizeof(unsigned)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;

  size_t Idx = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      for (auto &Inst : BB)
        Buf[Idx++] = Inst.getOpcode();
  }

  *OutCount = static_cast<unsigned>(Idx);
  return Buf;
}

static unsigned bridgeFunctionGetInstructionCount(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return 0;
  unsigned Count = 0;
  for (const auto &BB : *Fn)
    Count += BB.size();
  return Count;
}

// ===----------------------------------------------------------------------===
//  DominatorTree -- on-demand CFG analysis
// ===----------------------------------------------------------------------===

static NevercDomTreeRef bridgeFunctionBuildDomTree(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  auto *DT = new (std::nothrow) DominatorTree();
  if (LLVM_UNLIKELY(!DT))
    return nullptr;
  DT->recalculate(*Fn);
  return reinterpret_cast<NevercDomTreeRef>(DT);
}

static void bridgeDomTreeDestroy(NevercDomTreeRef DT) {
  if (LLVM_UNLIKELY(!DT))
    return;
  delete reinterpret_cast<DominatorTree *>(DT);
}

static int bridgeDomTreeDominates(NevercDomTreeRef DT,
                                  NevercBasicBlockRef A,
                                  NevercBasicBlockRef B) {
  if (LLVM_UNLIKELY(!DT || !A || !B))
    return 0;
  return reinterpret_cast<DominatorTree *>(DT)->dominates(unwrapBB(A),
                                                          unwrapBB(B));
}

static int bridgeDomTreeProperlyDominates(NevercDomTreeRef DT,
                                          NevercBasicBlockRef A,
                                          NevercBasicBlockRef B) {
  if (LLVM_UNLIKELY(!DT || !A || !B))
    return 0;
  return reinterpret_cast<DominatorTree *>(DT)->properlyDominates(
      unwrapBB(A), unwrapBB(B));
}

static NevercBasicBlockRef bridgeDomTreeGetIDom(NevercDomTreeRef DT,
                                                NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!DT || !BB))
    return nullptr;
  auto *Tree = reinterpret_cast<DominatorTree *>(DT);
  auto *Node = Tree->getNode(unwrapBB(BB));
  if (LLVM_UNLIKELY(!Node))
    return nullptr;
  auto *IDomNode = Node->getIDom();
  return IDomNode ? wrapBB(IDomNode->getBlock()) : nullptr;
}

static int bridgeDomTreeIsReachable(NevercDomTreeRef DT,
                                    NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!DT || !BB))
    return 0;
  return reinterpret_cast<DominatorTree *>(DT)->isReachableFromEntry(
      unwrapBB(BB));
}

static unsigned bridgeDomTreeGetDepth(NevercDomTreeRef DT,
                                      NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!DT || !BB))
    return 0;
  auto *Node =
      reinterpret_cast<DominatorTree *>(DT)->getNode(unwrapBB(BB));
  return LLVM_LIKELY(Node) ? Node->getLevel() : 0;
}

// ===----------------------------------------------------------------------===
//  LoopInfo -- on-demand loop nest analysis
//  Bundles DominatorTree + LoopInfo so the plugin never manages the DomTree
//  dependency.  The DomTree is kept alive because LoopInfo may reference it.
// ===----------------------------------------------------------------------===

namespace {
struct LoopInfoState {
  DominatorTree DT;
  LoopInfo LI;

  explicit LoopInfoState(Function &F) {
    DT.recalculate(F);
    LI.analyze(DT);
  }
};
} // namespace

static NevercLoopInfoRef bridgeFunctionBuildLoopInfo(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  auto *State = new (std::nothrow) LoopInfoState(*Fn);
  if (LLVM_UNLIKELY(!State))
    return nullptr;
  return reinterpret_cast<NevercLoopInfoRef>(State);
}

static void bridgeLoopInfoDestroy(NevercLoopInfoRef LI) {
  if (LLVM_UNLIKELY(!LI))
    return;
  delete reinterpret_cast<LoopInfoState *>(LI);
}

static NevercLoopRef bridgeLoopInfoGetLoopFor(NevercLoopInfoRef LI,
                                              NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!LI || !BB))
    return nullptr;
  auto *State = reinterpret_cast<LoopInfoState *>(LI);
  auto *L = State->LI.getLoopFor(unwrapBB(BB));
  return reinterpret_cast<NevercLoopRef>(L);
}

static unsigned bridgeLoopInfoGetTopLevelLoopCount(NevercLoopInfoRef LI) {
  if (LLVM_UNLIKELY(!LI))
    return 0;
  auto *State = reinterpret_cast<LoopInfoState *>(LI);
  return static_cast<unsigned>(State->LI.end() - State->LI.begin());
}

static NevercBasicBlockRef bridgeLoopGetHeader(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return nullptr;
  return wrapBB(reinterpret_cast<Loop *>(L)->getHeader());
}

static unsigned bridgeLoopGetDepth(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return 0;
  return reinterpret_cast<Loop *>(L)->getLoopDepth();
}

static NevercLoopRef bridgeLoopGetParentLoop(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return nullptr;
  return reinterpret_cast<NevercLoopRef>(
      reinterpret_cast<Loop *>(L)->getParentLoop());
}

static int bridgeLoopContains(NevercLoopRef L, NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!L || !BB))
    return 0;
  return reinterpret_cast<Loop *>(L)->contains(unwrapBB(BB));
}

static unsigned bridgeLoopGetNumBlocks(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return 0;
  return reinterpret_cast<Loop *>(L)->getNumBlocks();
}

static unsigned bridgeLoopGetNumSubLoops(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return 0;
  return reinterpret_cast<Loop *>(L)->getSubLoops().size();
}

static int bridgeLoopIsInnermost(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return 0;
  return reinterpret_cast<Loop *>(L)->isInnermost();
}

// ===----------------------------------------------------------------------===
//  PostDominatorTree -- reverse-CFG dominance analysis
// ===----------------------------------------------------------------------===

static NevercPostDomTreeRef bridgeFunctionBuildPostDomTree(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  auto *PDT = new (std::nothrow) PostDominatorTree();
  if (LLVM_UNLIKELY(!PDT))
    return nullptr;
  PDT->recalculate(*Fn);
  return reinterpret_cast<NevercPostDomTreeRef>(PDT);
}

static void bridgePostDomTreeDestroy(NevercPostDomTreeRef PDT) {
  if (LLVM_UNLIKELY(!PDT))
    return;
  delete reinterpret_cast<PostDominatorTree *>(PDT);
}

static int bridgePostDomTreeDominates(NevercPostDomTreeRef PDT,
                                      NevercBasicBlockRef A,
                                      NevercBasicBlockRef B) {
  if (LLVM_UNLIKELY(!PDT || !A || !B))
    return 0;
  return reinterpret_cast<PostDominatorTree *>(PDT)->dominates(unwrapBB(A),
                                                               unwrapBB(B));
}

static int bridgePostDomTreeProperlyDominates(NevercPostDomTreeRef PDT,
                                              NevercBasicBlockRef A,
                                              NevercBasicBlockRef B) {
  if (LLVM_UNLIKELY(!PDT || !A || !B))
    return 0;
  return reinterpret_cast<PostDominatorTree *>(PDT)->properlyDominates(
      unwrapBB(A), unwrapBB(B));
}

static NevercBasicBlockRef bridgePostDomTreeGetIPDom(NevercPostDomTreeRef PDT,
                                                     NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!PDT || !BB))
    return nullptr;
  auto *Tree = reinterpret_cast<PostDominatorTree *>(PDT);
  auto *Node = Tree->getNode(unwrapBB(BB));
  if (LLVM_UNLIKELY(!Node))
    return nullptr;
  auto *IPDomNode = Node->getIDom();
  return IPDomNode ? wrapBB(IPDomNode->getBlock()) : nullptr;
}

static unsigned bridgePostDomTreeGetDepth(NevercPostDomTreeRef PDT,
                                          NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!PDT || !BB))
    return 0;
  auto *Node =
      reinterpret_cast<PostDominatorTree *>(PDT)->getNode(unwrapBB(BB));
  return LLVM_LIKELY(Node) ? Node->getLevel() : 0;
}

// ===----------------------------------------------------------------------===
//  Function cloning
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeFunctionClone(NevercValueRef F,
                                          const char *NewName) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  ValueToValueMapTy VMap;
  Function *Clone = CloneFunction(Fn, VMap);
  if (LLVM_UNLIKELY(!Clone))
    return nullptr;
  if (NewName && *NewName)
    Clone->setName(NewName);
  Clone->setLinkage(GlobalValue::InternalLinkage);
  return wrapV(Clone);
}

// ===----------------------------------------------------------------------===
//  SCEV -- on-demand ScalarEvolution analysis
//  Bundles all dependencies (TLI, AC, DT, LI, SE) into one state object.
//  SE is heap-allocated after DT/LI are populated to avoid referencing
//  uninitialized analysis results.
// ===----------------------------------------------------------------------===

namespace {
struct SCEVState {
  TargetLibraryInfoImpl TLIImpl;
  TargetLibraryInfo TLI;
  AssumptionCache AC;
  DominatorTree DT;
  LoopInfo LI;
  std::unique_ptr<ScalarEvolution> SE;

  explicit SCEVState(Function &F)
      : TLIImpl(Triple(F.getParent()->getTargetTriple())), TLI(TLIImpl),
        AC(F) {
    DT.recalculate(F);
    LI.analyze(DT);
    SE = std::make_unique<ScalarEvolution>(F, TLI, AC, DT, LI);
  }
};
} // namespace

static NevercSCEVInfoRef bridgeFunctionBuildSCEV(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  auto *State = new (std::nothrow) SCEVState(*Fn);
  if (LLVM_UNLIKELY(!State))
    return nullptr;
  return reinterpret_cast<NevercSCEVInfoRef>(State);
}

static void bridgeSCEVInfoDestroy(NevercSCEVInfoRef SI) {
  if (LLVM_UNLIKELY(!SI))
    return;
  delete reinterpret_cast<SCEVState *>(SI);
}

static unsigned bridgeSCEVGetTripCount(NevercSCEVInfoRef SI,
                                       NevercBasicBlockRef LoopHeader) {
  if (LLVM_UNLIKELY(!SI || !LoopHeader))
    return 0;
  auto *State = reinterpret_cast<SCEVState *>(SI);
  auto *L = State->LI.getLoopFor(unwrapBB(LoopHeader));
  if (LLVM_UNLIKELY(!L || L->getHeader() != unwrapBB(LoopHeader)))
    return 0;
  return State->SE->getSmallConstantTripCount(L);
}

static unsigned bridgeSCEVGetMaxTripCount(NevercSCEVInfoRef SI,
                                          NevercBasicBlockRef LoopHeader) {
  if (LLVM_UNLIKELY(!SI || !LoopHeader))
    return 0;
  auto *State = reinterpret_cast<SCEVState *>(SI);
  auto *L = State->LI.getLoopFor(unwrapBB(LoopHeader));
  if (LLVM_UNLIKELY(!L || L->getHeader() != unwrapBB(LoopHeader)))
    return 0;
  return State->SE->getSmallConstantMaxTripCount(L);
}

// ===----------------------------------------------------------------------===
//  CallGraph -- on-demand module-wide call graph with SCC recursion cache
// ===----------------------------------------------------------------------===

namespace {
struct CallGraphState {
  CallGraph CG;
  SmallPtrSet<Function *, 32> RecursiveFns;

  explicit CallGraphState(Module &M) : CG(M) {
    for (scc_iterator<CallGraph *> I = scc_begin(&CG), E = scc_end(&CG);
         I != E; ++I) {
      if (!I.hasCycle())
        continue;
      for (CallGraphNode *Node : *I) {
        if (Function *Fn = Node->getFunction())
          RecursiveFns.insert(Fn);
      }
    }
  }
};
} // namespace

static NevercCallGraphRef bridgeModuleBuildCallGraph(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return nullptr;
  auto *State = new (std::nothrow) CallGraphState(*unwrap(M));
  if (LLVM_UNLIKELY(!State))
    return nullptr;
  return reinterpret_cast<NevercCallGraphRef>(State);
}

static void bridgeCallGraphDestroy(NevercCallGraphRef CG) {
  if (LLVM_UNLIKELY(!CG))
    return;
  delete reinterpret_cast<CallGraphState *>(CG);
}

static unsigned bridgeCallGraphGetCalleeCount(NevercCallGraphRef CG,
                                              NevercValueRef F) {
  if (LLVM_UNLIKELY(!CG || !F))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return 0;
  auto *State = reinterpret_cast<CallGraphState *>(CG);
  CallGraphNode *Node = State->CG[Fn];
  if (LLVM_UNLIKELY(!Node))
    return 0;
  SmallPtrSet<Function *, 16> Seen;
  for (const auto &CR : *Node) {
    if (Function *Callee = CR.second->getFunction())
      Seen.insert(Callee);
  }
  return static_cast<unsigned>(Seen.size());
}

static NevercValueRef *bridgeCallGraphCollectCallees(NevercCallGraphRef CG,
                                                     NevercValueRef F,
                                                     unsigned *OutCount) {
  if (LLVM_UNLIKELY(!OutCount))
    return nullptr;
  *OutCount = 0;
  if (LLVM_UNLIKELY(!CG || !F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return nullptr;
  auto *State = reinterpret_cast<CallGraphState *>(CG);
  CallGraphNode *Node = State->CG[Fn];
  if (LLVM_UNLIKELY(!Node))
    return nullptr;
  SmallPtrSet<Function *, 16> Seen;
  for (const auto &CR : *Node) {
    if (Function *Callee = CR.second->getFunction())
      Seen.insert(Callee);
  }
  if (Seen.empty())
    return nullptr;
  auto *Out = static_cast<NevercValueRef *>(
      bridgeAlloc(static_cast<uint64_t>(Seen.size()) * sizeof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Out))
    return nullptr;
  unsigned Idx = 0;
  for (Function *Callee : Seen)
    Out[Idx++] = wrapV(Callee);
  *OutCount = Idx;
  return Out;
}

static int bridgeCallGraphIsRecursive(NevercCallGraphRef CG,
                                      NevercValueRef F) {
  if (LLVM_UNLIKELY(!CG || !F))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return 0;
  return reinterpret_cast<CallGraphState *>(CG)->RecursiveFns.count(Fn) != 0;
}

// ===----------------------------------------------------------------------===
//  CFG mutation helpers
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef bridgeSplitEdge(NevercBasicBlockRef From,
                                           NevercBasicBlockRef To) {
  if (LLVM_UNLIKELY(!From || !To))
    return nullptr;
  BasicBlock *New =
      llvm::SplitEdge(unwrapBB(From), unwrapBB(To));
  return New ? wrapBB(New) : nullptr;
}

static int bridgeMergeBlockIntoPredecessor(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return 0;
  return llvm::MergeBlockIntoPredecessor(unwrapBB(BB)) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  Loop invariant query
// ===----------------------------------------------------------------------===

static int bridgeLoopIsLoopInvariant(NevercLoopRef L, NevercValueRef V) {
  if (LLVM_UNLIKELY(!L || !V))
    return 0;
  return reinterpret_cast<Loop *>(L)->isLoopInvariant(unwrapV(V)) ? 1 : 0;
}

static void bridgeMFuncCollectBBs(NevercMachineFuncRef MF,
                                  NevercMachineBBRef *Out) {
  if (LLVM_UNLIKELY(!MF || !Out))
    return;
  unsigned Idx = 0;
  for (auto &MBB : *unwrapMF(MF))
    Out[Idx++] = wrapMBB(&MBB);
}

static void bridgeMBBCollectInstructions(NevercMachineBBRef MBB,
                                         NevercMachineInstrRef *Out) {
  if (LLVM_UNLIKELY(!MBB || !Out))
    return;
  unsigned Idx = 0;
  for (auto &MI : *unwrapMBB(MBB))
    Out[Idx++] = wrapMI(&MI);
}

// ===----------------------------------------------------------------------===
//  NSW/NUW arithmetic
// ===----------------------------------------------------------------------===

#define BRIDGE_BINOP_FLAG(NAME, METHOD)                                        \
  static NevercValueRef bridge##NAME(NevercBuilderRef B, NevercValueRef LHS,   \
                                     NevercValueRef RHS, const char *Name) {   \
    if (LLVM_UNLIKELY(!B || !LHS || !RHS))                                     \
      return nullptr;                                                          \
    const char *Nm = Name ? Name : "";                                         \
    return wrapV(unwrapB(B)->METHOD(unwrapV(LHS), unwrapV(RHS), Nm));          \
  }

BRIDGE_BINOP_FLAG(BuildNSWAdd, CreateNSWAdd)
BRIDGE_BINOP_FLAG(BuildNUWAdd, CreateNUWAdd)
BRIDGE_BINOP_FLAG(BuildNSWSub, CreateNSWSub)
BRIDGE_BINOP_FLAG(BuildNUWSub, CreateNUWSub)
BRIDGE_BINOP_FLAG(BuildNSWMul, CreateNSWMul)
BRIDGE_BINOP_FLAG(BuildNUWMul, CreateNUWMul)
BRIDGE_BINOP_FLAG(BuildExactSDiv, CreateExactSDiv)
BRIDGE_BINOP_FLAG(BuildExactUDiv, CreateExactUDiv)
#undef BRIDGE_BINOP_FLAG

static NevercValueRef bridgeBuildNSWNeg(NevercBuilderRef B, NevercValueRef V,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !V))
    return nullptr;
  return wrapV(unwrapB(B)->CreateNSWNeg(unwrapV(V), Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Exception handling
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildInvoke(NevercBuilderRef B, NevercTypeRef FnTy,
                                        NevercValueRef Fn,
                                        NevercValueRef *Args, unsigned ArgCount,
                                        NevercBasicBlockRef NormalDest,
                                        NevercBasicBlockRef UnwindDest,
                                        const char *Name) {
  if (LLVM_UNLIKELY(!B || !FnTy || !Fn || !NormalDest || !UnwindDest ||
                    (ArgCount > 0 && !Args)))
    return nullptr;
  auto *FTy = dyn_cast<FunctionType>(unwrapTy(FnTy));
  if (LLVM_UNLIKELY(!FTy))
    return nullptr;
  SmallVector<Value *, 8> ArgVec;
  for (unsigned I = 0; I < ArgCount; ++I) {
    if (LLVM_UNLIKELY(!Args[I]))
      return nullptr;
    ArgVec.push_back(unwrapV(Args[I]));
  }
  return wrapV(unwrapB(B)->CreateInvoke(FTy, unwrapV(Fn), unwrapBB(NormalDest),
                                        unwrapBB(UnwindDest), ArgVec,
                                        Name ? Name : ""));
}

static NevercValueRef bridgeBuildLandingPad(NevercBuilderRef B,
                                            NevercTypeRef Ty,
                                            unsigned NumClauses,
                                            const char *Name) {
  if (LLVM_UNLIKELY(!B || !Ty))
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateLandingPad(unwrapTy(Ty), NumClauses, Name ? Name : ""));
}

static void bridgeLandingPadAddClause(NevercValueRef LPad,
                                      NevercValueRef ClauseVal) {
  if (LLVM_UNLIKELY(!LPad || !ClauseVal))
    return;
  auto *LP = dyn_cast<LandingPadInst>(unwrapV(LPad));
  // LandingPadInst::addClause() requires a Constant; use dyn_cast (not cast)
  // so a plugin passing a non-constant clause is rejected, not a host abort.
  auto *Clause = dyn_cast<Constant>(unwrapV(ClauseVal));
  if (LP && Clause)
    LP->addClause(Clause);
}

static void bridgeLandingPadSetCleanup(NevercValueRef LPad, int IsCleanup) {
  if (LLVM_UNLIKELY(!LPad))
    return;
  auto *LP = dyn_cast<LandingPadInst>(unwrapV(LPad));
  if (LP)
    LP->setCleanup(IsCleanup != 0);
}

static NevercValueRef bridgeBuildResume(NevercBuilderRef B,
                                        NevercValueRef Val) {
  if (LLVM_UNLIKELY(!B || !Val))
    return nullptr;
  return wrapV(unwrapB(B)->CreateResume(unwrapV(Val)));
}

// ===----------------------------------------------------------------------===
//  Thread-local globals
// ===----------------------------------------------------------------------===

static int bridgeGlobalIsThreadLocal(NevercValueRef GV) {
  if (LLVM_UNLIKELY(!GV))
    return 0;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  return G && G->isThreadLocal();
}

static void bridgeGlobalSetThreadLocal(NevercValueRef GV, int IsThreadLocal) {
  if (LLVM_UNLIKELY(!GV))
    return;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (G)
    G->setThreadLocal(IsThreadLocal != 0);
}

// ===----------------------------------------------------------------------===
//  Function enum attributes
// ===----------------------------------------------------------------------===

static void bridgeFunctionAddEnumAttr(NevercValueRef F, unsigned AttrKind) {
  if (LLVM_UNLIKELY(!F))
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (Fn && AttrKind < Attribute::EndAttrKinds)
    Fn->addFnAttr(static_cast<Attribute::AttrKind>(AttrKind));
}

static int bridgeFunctionHasEnumAttr(NevercValueRef F, unsigned AttrKind) {
  if (LLVM_UNLIKELY(!F))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || AttrKind >= Attribute::EndAttrKinds))
    return 0;
  return Fn->hasFnAttribute(static_cast<Attribute::AttrKind>(AttrKind));
}

static void bridgeFunctionRemoveEnumAttr(NevercValueRef F, unsigned AttrKind) {
  if (LLVM_UNLIKELY(!F))
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (Fn && AttrKind < Attribute::EndAttrKinds)
    Fn->removeFnAttr(static_cast<Attribute::AttrKind>(AttrKind));
}

// ===----------------------------------------------------------------------===
//  COMDAT
// ===----------------------------------------------------------------------===

static inline Comdat *unwrapComdat(NevercComdatRef C) {
  return reinterpret_cast<Comdat *>(C);
}
static inline NevercComdatRef wrapComdat(Comdat *C) {
  return reinterpret_cast<NevercComdatRef>(C);
}

static NevercComdatRef bridgeModuleGetOrInsertComdat(NevercModuleRef M,
                                                      const char *Name) {
  if (LLVM_UNLIKELY(!M || !Name))
    return nullptr;
  return wrapComdat(unwrap(M)->getOrInsertComdat(Name));
}

static unsigned bridgeComdatGetSelectionKind(NevercComdatRef C) {
  if (LLVM_UNLIKELY(!C))
    return 0;
  return static_cast<unsigned>(unwrapComdat(C)->getSelectionKind());
}

static void bridgeComdatSetSelectionKind(NevercComdatRef C, unsigned Kind) {
  if (LLVM_UNLIKELY(!C || Kind > NEVERC_COMDAT_SAME_SIZE))
    return;
  unwrapComdat(C)->setSelectionKind(static_cast<Comdat::SelectionKind>(Kind));
}

static void bridgeGlobalSetComdat(NevercValueRef GV, NevercComdatRef C) {
  if (LLVM_UNLIKELY(!GV))
    return;
  auto *GO = dyn_cast<GlobalObject>(unwrapV(GV));
  if (GO)
    GO->setComdat(unwrapComdat(C));
}

static NevercComdatRef bridgeGlobalGetComdat(NevercValueRef GV) {
  if (LLVM_UNLIKELY(!GV))
    return nullptr;
  auto *GO = dyn_cast<GlobalObject>(unwrapV(GV));
  if (LLVM_UNLIKELY(!GO))
    return nullptr;
  return wrapComdat(const_cast<Comdat *>(GO->getComdat()));
}

// ===----------------------------------------------------------------------===
//  InstMoveAfter
// ===----------------------------------------------------------------------===

static void bridgeInstMoveAfter(NevercValueRef I, NevercValueRef After) {
  if (LLVM_UNLIKELY(!I || !After))
    return;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  auto *AfterInst = dyn_cast<Instruction>(unwrapV(After));
  if (Inst && AfterInst)
    Inst->moveAfter(AfterInst);
}

// ===----------------------------------------------------------------------===
//  ConstGetAggregateElement
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeConstGetAggregateElement(NevercValueRef Agg,
                                                      unsigned Idx) {
  if (LLVM_UNLIKELY(!Agg))
    return nullptr;
  auto *C = dyn_cast<Constant>(unwrapV(Agg));
  if (LLVM_UNLIKELY(!C))
    return nullptr;
  // Constant::getAggregateElement() asserts the value is an aggregate or
  // vector constant.  A plugin may legitimately probe a scalar initializer
  // (e.g. `int g = 0;`), so mirror that precondition here and return null
  // rather than aborting the host compiler.
  Type *Ty = C->getType();
  if (LLVM_UNLIKELY(!Ty->isAggregateType() && !Ty->isVectorTy()))
    return nullptr;
  Constant *Elem = C->getAggregateElement(Idx);
  return Elem ? wrapV(Elem) : nullptr;
}

// ===----------------------------------------------------------------------===
//  MIR extended navigation
// ===----------------------------------------------------------------------===

static NevercMachineInstrRef bridgeMBBGetLastInst(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *BB = unwrapMBB(MBB);
  if (LLVM_UNLIKELY(BB->empty()))
    return nullptr;
  return wrapMI(&BB->back());
}

static NevercMachineInstrRef bridgeMBBGetPrevInst(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return nullptr;
  auto *Inst = unwrapMI(MI);
  auto *MBB = Inst->getParent();
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto It = Inst->getIterator();
  if (LLVM_UNLIKELY(It == MBB->begin()))
    return nullptr;
  return wrapMI(&*std::prev(It));
}

static NevercMachineBBRef bridgeMFuncGetLastBB(NevercMachineFuncRef MF) {
  if (LLVM_UNLIKELY(!MF))
    return nullptr;
  auto *Func = unwrapMF(MF);
  if (LLVM_UNLIKELY(Func->empty()))
    return nullptr;
  return wrapMBB(&Func->back());
}

static NevercMachineBBRef bridgeMFuncGetPrevBB(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return nullptr;
  auto *BB = unwrapMBB(MBB);
  auto *MF = BB->getParent();
  if (LLVM_UNLIKELY(!MF))
    return nullptr;
  auto It = BB->getIterator();
  if (It == MF->begin())
    return nullptr;
  return wrapMBB(&*std::prev(It));
}

// ===----------------------------------------------------------------------===
//  MIR operand mutation
// ===----------------------------------------------------------------------===

static void bridgeMInstSetOperandReg(NevercMachineInstrRef MI, unsigned Idx,
                                     unsigned Reg) {
  if (LLVM_UNLIKELY(!MI))
    return;
  auto *Inst = unwrapMI(MI);
  if (Idx < Inst->getNumOperands() && Inst->getOperand(Idx).isReg())
    Inst->getOperand(Idx).setReg(Register(Reg));
}

static void bridgeMInstSetOperandImm(NevercMachineInstrRef MI, unsigned Idx,
                                     int64_t Val) {
  if (LLVM_UNLIKELY(!MI))
    return;
  auto *Inst = unwrapMI(MI);
  if (Idx < Inst->getNumOperands() && Inst->getOperand(Idx).isImm())
    Inst->getOperand(Idx).setImm(Val);
}

// ===----------------------------------------------------------------------===
//  MIR instruction flags & properties
// ===----------------------------------------------------------------------===

static unsigned bridgeMInstGetFlags(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->getFlags();
}

static void bridgeMInstSetFlags(NevercMachineInstrRef MI, unsigned Flags) {
  if (LLVM_UNLIKELY(!MI))
    return;
  unwrapMI(MI)->setFlags(Flags);
}

static int bridgeMInstIsBranch(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->isBranch();
}

static int bridgeMInstIsCall(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->isCall();
}

static int bridgeMInstIsReturn(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->isReturn();
}

static int bridgeMInstIsTerminator(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->isTerminator();
}

static int bridgeMInstIsMoveImmediate(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->isMoveImmediate();
}

static int bridgeMInstHasDelaySlot(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  return unwrapMI(MI)->hasDelaySlot();
}

static const char *bridgeMInstGetDesc(NevercMachineInstrRef MI) {
  if (LLVM_UNLIKELY(!MI))
    return "";
  auto *Inst = unwrapMI(MI);
  auto *MBB = Inst->getParent();
  if (LLVM_UNLIKELY(!MBB))
    return "";
  auto *MF = MBB->getParent();
  if (LLVM_UNLIKELY(!MF))
    return "";
  const auto *TII = MF->getSubtarget().getInstrInfo();
  if (LLVM_UNLIKELY(!TII))
    return "";
  return TII->getName(Inst->getOpcode()).data();
}

// ===----------------------------------------------------------------------===
//  MIR register queries
// ===----------------------------------------------------------------------===

static int bridgeMInstOperandIsVirtReg(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).getReg().isVirtual();
}

static int bridgeMInstOperandIsPhysReg(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).getReg().isPhysical();
}

static int bridgeMInstOperandIsDef(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).isDef();
}

static int bridgeMInstOperandIsUse(NevercMachineInstrRef MI, unsigned Idx) {
  if (LLVM_UNLIKELY(!MI))
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).isUse();
}

// ===----------------------------------------------------------------------===
//  MIR instruction movement & counting
// ===----------------------------------------------------------------------===

static void bridgeMInstMoveBefore(NevercMachineInstrRef MI,
                                  NevercMachineInstrRef Before) {
  if (LLVM_UNLIKELY(!MI || !Before))
    return;
  auto *Inst = unwrapMI(MI);
  auto *BeforeInst = unwrapMI(Before);
  auto *DstMBB = BeforeInst->getParent();
  auto *SrcMBB = Inst->getParent();
  if (DstMBB && SrcMBB)
    DstMBB->splice(BeforeInst->getIterator(), SrcMBB, Inst->getIterator());
}

static unsigned bridgeMBBGetInstCount(NevercMachineBBRef MBB) {
  if (LLVM_UNLIKELY(!MBB))
    return 0;
  return static_cast<unsigned>(unwrapMBB(MBB)->size());
}

static unsigned bridgeMFuncGetBBCount(NevercMachineFuncRef MF) {
  if (LLVM_UNLIKELY(!MF))
    return 0;
  return static_cast<unsigned>(unwrapMF(MF)->size());
}

// ===----------------------------------------------------------------------===
//  Linker API stubs
//  These are populated with real implementations when the linker backends
//  set up their per-invocation context.  During compilation (non-linking)
//  they return safe defaults (NULL / 0 / empty string).
// ===----------------------------------------------------------------------===

// These forward to the linker backend accessor table installed via
// setLinkerBackend() for the duration of a LINK_* hook.  With no backend
// installed (the common, non-linking path) or a missing accessor they return
// safe defaults, so plugins that probe the linker API outside a link run get
// well-defined empty results instead of crashing.

static NevercLinkerSymbolRef bridgeLinkGetFirstSymbol(void) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetFirstSymbol ? B->GetFirstSymbol() : nullptr;
}
static NevercLinkerSymbolRef bridgeLinkGetNextSymbol(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetNextSymbol ? B->GetNextSymbol(S) : nullptr;
}
static NevercLinkerSymbolRef bridgeLinkFindSymbol(const char *Name) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->FindSymbol ? B->FindSymbol(Name) : nullptr;
}
static const char *bridgeLinkSymbolGetName(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolGetName ? B->SymbolGetName(S) : "";
}
static uint64_t bridgeLinkSymbolGetValue(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolGetValue ? B->SymbolGetValue(S) : 0;
}
static uint64_t bridgeLinkSymbolGetSize(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolGetSize ? B->SymbolGetSize(S) : 0;
}
static int bridgeLinkSymbolIsDefined(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolIsDefined ? B->SymbolIsDefined(S) : 0;
}
static int bridgeLinkSymbolIsLocal(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolIsLocal ? B->SymbolIsLocal(S) : 0;
}
static int bridgeLinkSymbolIsHidden(NevercLinkerSymbolRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SymbolIsHidden ? B->SymbolIsHidden(S) : 0;
}
static void bridgeLinkSymbolSetVisibilityHidden(NevercLinkerSymbolRef S,
                                                int IsHidden) {
  const NevercLinkerBackend *B = getLinkerBackend();
  if (B && B->SymbolSetVisibilityHidden)
    B->SymbolSetVisibilityHidden(S, IsHidden);
}
static NevercLinkerSectionRef bridgeLinkGetFirstSection(void) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetFirstSection ? B->GetFirstSection() : nullptr;
}
static NevercLinkerSectionRef
bridgeLinkGetNextSection(NevercLinkerSectionRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetNextSection ? B->GetNextSection(S) : nullptr;
}
static NevercLinkerSectionRef bridgeLinkFindSection(const char *Name) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->FindSection ? B->FindSection(Name) : nullptr;
}
static const char *bridgeLinkSectionGetName(NevercLinkerSectionRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SectionGetName ? B->SectionGetName(S) : "";
}
static uint64_t bridgeLinkSectionGetSize(NevercLinkerSectionRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SectionGetSize ? B->SectionGetSize(S) : 0;
}
static uint64_t bridgeLinkSectionGetAlignment(NevercLinkerSectionRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SectionGetAlignment ? B->SectionGetAlignment(S) : 0;
}
static unsigned bridgeLinkSectionGetFlags(NevercLinkerSectionRef S) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->SectionGetFlags ? B->SectionGetFlags(S) : 0;
}
static const char *bridgeLinkGetOutputPath(void) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetOutputPath ? B->GetOutputPath() : "";
}
static unsigned bridgeLinkGetOutputFormat(void) {
  const NevercLinkerBackend *B = getLinkerBackend();
  return B && B->GetOutputFormat ? B->GetOutputFormat()
                                 : NEVERC_LINK_FORMAT_UNKNOWN;
}

static const char *bridgeLinkGetOutputFormatName() {
  const NevercLinkerBackend *B = getLinkerBackend();
  unsigned Fmt = B && B->GetOutputFormat ? B->GetOutputFormat()
                                         : NEVERC_LINK_FORMAT_UNKNOWN;
  switch (Fmt) {
  case NEVERC_LINK_FORMAT_ELF:   return "ELF";
  case NEVERC_LINK_FORMAT_COFF:  return "COFF";
  case NEVERC_LINK_FORMAT_MACHO: return "Mach-O";
  default:                       return "unknown";
  }
}

// ===----------------------------------------------------------------------===
//  String search, replacement, case conversion, memory duplicate
// ===----------------------------------------------------------------------===

static uint64_t bridgeStrFindStr(const char *Haystack, const char *Needle) {
  if (LLVM_UNLIKELY(!Haystack || !Needle))
    return UINT64_MAX;
  const char *P = std::strstr(Haystack, Needle);
  return P ? static_cast<uint64_t>(P - Haystack) : UINT64_MAX;
}

static const char *bridgeHookPointGetName(unsigned Hook) {
  switch (Hook) {
  case NEVERC_HOOK_PRE_OPT:                 return "PRE_OPT";
  case NEVERC_HOOK_POST_OPT:                return "POST_OPT";
  case NEVERC_HOOK_PIPELINE_START:          return "PIPELINE_START";
  case NEVERC_HOOK_PIPELINE_LAST:           return "PIPELINE_LAST";
  case NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT:  return "BEFORE_CODEGEN_PREEMIT";
  case NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR: return "AFTER_CODEGEN_FINAL_MIR";
  case NEVERC_HOOK_SC_BEFORE_PREP:          return "SC_BEFORE_PREP";
  case NEVERC_HOOK_SC_AFTER_PREP:           return "SC_AFTER_PREP";
  case NEVERC_HOOK_SC_BEFORE_INLINING:      return "SC_BEFORE_INLINING";
  case NEVERC_HOOK_SC_AFTER_INLINING:       return "SC_AFTER_INLINING";
  case NEVERC_HOOK_SC_AFTER_STACKIFY:       return "SC_AFTER_STACKIFY";
  case NEVERC_HOOK_SC_AFTER_FINAL_IR:       return "SC_AFTER_FINAL_IR";
  case NEVERC_HOOK_SC_BEFORE_PREEMIT:       return "SC_BEFORE_PREEMIT";
  case NEVERC_HOOK_SC_AFTER_PREEMIT:        return "SC_AFTER_PREEMIT";
  case NEVERC_HOOK_SC_AFTER_FINAL_MIR:      return "SC_AFTER_FINAL_MIR";
  case NEVERC_HOOK_SC_POST_EXTRACT:         return "SC_POST_EXTRACT";
  case NEVERC_HOOK_SC_POST_FINALIZE:        return "SC_POST_FINALIZE";
  case NEVERC_HOOK_LTO_PRE_OPT:             return "LTO_PRE_OPT";
  case NEVERC_HOOK_LTO_POST_OPT:            return "LTO_POST_OPT";
  case NEVERC_HOOK_LINK_PRE_LAYOUT:         return "LINK_PRE_LAYOUT";
  case NEVERC_HOOK_LINK_POST_LAYOUT:        return "LINK_POST_LAYOUT";
  case NEVERC_HOOK_LINK_POST_EMIT:          return "LINK_POST_EMIT";
  default:                                  return "<unknown>";
  }
}

static void *bridgeMemDup(const void *Src, uint64_t Len) {
  if (LLVM_UNLIKELY(!Src || Len == 0))
    return nullptr;
  void *Dst = bridgeAlloc(Len);
  if (LLVM_LIKELY(Dst))
    std::memcpy(Dst, Src, static_cast<size_t>(Len));
  return Dst;
}

static char *bridgeStrReplace(const char *S, const char *Old,
                              const char *New) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  if (LLVM_UNLIKELY(!Old || !New || Old[0] == '\0'))
    return bridgeStrDup(S);

  const char *Pos = std::strstr(S, Old);
  if (!Pos)
    return bridgeStrDup(S);

  size_t PrefixLen = static_cast<size_t>(Pos - S);
  size_t OldLen = std::strlen(Old);
  size_t NewLen = std::strlen(New);
  size_t SuffixLen = std::strlen(Pos + OldLen);
  if (LLVM_UNLIKELY(PrefixLen > SIZE_MAX - 1 ||
                    NewLen > SIZE_MAX - 1 - PrefixLen))
    return nullptr;
  size_t ResultLen = PrefixLen + NewLen;
  if (LLVM_UNLIKELY(ResultLen > SIZE_MAX - 1 ||
                    SuffixLen > SIZE_MAX - 1 - ResultLen))
    return nullptr;
  ResultLen += SuffixLen;

  char *Result = static_cast<char *>(bridgeAlloc(ResultLen + 1));
  if (LLVM_UNLIKELY(!Result))
    return nullptr;
  if (PrefixLen)
    std::memcpy(Result, S, PrefixLen);
  if (NewLen)
    std::memcpy(Result + PrefixLen, New, NewLen);
  if (SuffixLen)
    std::memcpy(Result + PrefixLen + NewLen, Pos + OldLen, SuffixLen);
  Result[ResultLen] = '\0';
  return Result;
}

static char *bridgeStrReplaceAll(const char *S, const char *Old,
                                 const char *New) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  if (LLVM_UNLIKELY(!Old || !New || Old[0] == '\0'))
    return bridgeStrDup(S);

  size_t OldLen = std::strlen(Old);
  size_t NewLen = std::strlen(New);
  size_t SLen = std::strlen(S);

  // Single scan: collect hit offsets into a small stack-local buffer,
  // spilling to the heap only when there are many matches.
  size_t StackBuf[64];
  size_t *Hits = StackBuf;
  size_t HitCap = sizeof(StackBuf) / sizeof(StackBuf[0]);
  size_t HitCount = 0;

  const char *P = S;
  while ((P = std::strstr(P, Old)) != nullptr) {
    if (LLVM_UNLIKELY(HitCount == HitCap)) {
      if (HitCap > (SIZE_MAX / sizeof(size_t)) / 2) {
        if (Hits != StackBuf)
          bridgeFree(Hits);
        return nullptr;
      }
      size_t NewCap = HitCap * 2;
      size_t *NewBuf;
      if (Hits == StackBuf) {
        NewBuf = static_cast<size_t *>(
            bridgeAlloc(NewCap * sizeof(size_t)));
        if (LLVM_LIKELY(NewBuf))
          std::memcpy(NewBuf, StackBuf, HitCount * sizeof(size_t));
      } else {
        NewBuf = static_cast<size_t *>(
            bridgeRealloc(Hits, NewCap * sizeof(size_t)));
      }
      if (LLVM_UNLIKELY(!NewBuf)) {
        if (Hits != StackBuf)
          bridgeFree(Hits);
        return nullptr;
      }
      Hits = NewBuf;
      HitCap = NewCap;
    }
    Hits[HitCount++] = static_cast<size_t>(P - S);
    P += OldLen;
  }

  if (HitCount == 0) {
    if (Hits != StackBuf)
      bridgeFree(Hits);
    return bridgeStrDup(S);
  }

  size_t Removed = HitCount * OldLen;
  size_t Added = 0;
  if (NewLen != 0) {
    if (LLVM_UNLIKELY(HitCount > SIZE_MAX / NewLen)) {
      if (Hits != StackBuf)
        bridgeFree(Hits);
      return nullptr;
    }
    Added = HitCount * NewLen;
  }
  size_t BaseLen = SLen - Removed;
  // Two-step guard: BaseLen could be 0 or large; SIZE_MAX-BaseLen-1
  // wraps around if BaseLen == SIZE_MAX (impossible in practice but
  // matches the unsigned-underflow-safe pattern used elsewhere).
  if (LLVM_UNLIKELY(BaseLen > SIZE_MAX - 1 ||
                    Added > SIZE_MAX - 1 - BaseLen)) {
    if (Hits != StackBuf)
      bridgeFree(Hits);
    return nullptr;
  }
  size_t ResultLen = BaseLen + Added;

  char *Result = static_cast<char *>(bridgeAlloc(ResultLen + 1));
  if (LLVM_UNLIKELY(!Result)) {
    if (Hits != StackBuf)
      bridgeFree(Hits);
    return nullptr;
  }

  // Build the result in one pass using recorded offsets.
  char *Dst = Result;
  size_t Prev = 0;
  for (size_t I = 0; I < HitCount; ++I) {
    size_t Off = Hits[I];
    size_t Span = Off - Prev;
    if (Span)
      std::memcpy(Dst, S + Prev, Span);
    Dst += Span;
    if (NewLen)
      std::memcpy(Dst, New, NewLen);
    Dst += NewLen;
    Prev = Off + OldLen;
  }
  size_t Tail = SLen - Prev;
  std::memcpy(Dst, S + Prev, Tail);
  Dst[Tail] = '\0';

  if (Hits != StackBuf)
    bridgeFree(Hits);
  return Result;
}

static char *bridgeStrToLower(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *R = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!R))
    return nullptr;
  for (size_t I = 0; I < Len; ++I) {
    unsigned C = static_cast<unsigned char>(S[I]);
    unsigned CaseBit = (C - 'A' <= unsigned('Z' - 'A')) ? 0x20u : 0u;
    R[I] = static_cast<char>(C | CaseBit);
  }
  R[Len] = '\0';
  return R;
}

static char *bridgeStrToUpper(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *R = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!R))
    return nullptr;
  for (size_t I = 0; I < Len; ++I) {
    unsigned C = static_cast<unsigned char>(S[I]);
    unsigned CaseBit = (C - 'a' <= unsigned('z' - 'a')) ? 0x20u : 0u;
    R[I] = static_cast<char>(C & ~CaseBit);
  }
  R[Len] = '\0';
  return R;
}

// ===----------------------------------------------------------------------===
//  Bounded string duplication (strndup equivalent)
// ===----------------------------------------------------------------------===

static char *bridgeStrNDup(const char *S, uint64_t MaxLen) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  size_t Cap = (MaxLen > static_cast<uint64_t>(SIZE_MAX - 1))
                   ? (SIZE_MAX - 1)
                   : static_cast<size_t>(MaxLen);
  const void *Nul = Cap ? std::memchr(S, '\0', Cap) : nullptr;
  size_t CopyLen =
      Nul ? static_cast<size_t>(static_cast<const char *>(Nul) - S) : Cap;
  char *Buf = static_cast<char *>(bridgeAlloc(CopyLen + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (CopyLen)
    std::memcpy(Buf, S, CopyLen);
  Buf[CopyLen] = '\0';
  return Buf;
}

// ===----------------------------------------------------------------------===
//  Character occurrence count
//
//  Normalize C to unsigned char before searching: std::strchr converts the
//  needle through (char), so values like 256, 512, ... silently collapse to
//  '\0' on 8-bit-char platforms and trigger an infinite loop on the null
//  terminator.  Reject the post-truncation zero, not just the literal 0.
// ===----------------------------------------------------------------------===

static uint64_t bridgeStrCountChar(const char *S, int C) {
  if (LLVM_UNLIKELY(!S))
    return 0;
  unsigned char Needle = static_cast<unsigned char>(C);
  if (LLVM_UNLIKELY(Needle == 0))
    return 0;
  uint64_t Count = 0;
  for (const char *P = S; (P = std::strchr(P, Needle)) != nullptr; ++P)
    ++Count;
  return Count;
}

// ===----------------------------------------------------------------------===
//  Prefix-skip helper -- returns S+strlen(Prefix) when S starts with Prefix,
//  or nullptr otherwise.  Replaces the StrStartsWith + magic-offset idiom.
// ===----------------------------------------------------------------------===

static const char *bridgeStrAfterPrefix(const char *S, const char *Prefix) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  if (LLVM_UNLIKELY(!Prefix || !*Prefix))
    return S;
  const char *P = S;
  const char *Q = Prefix;
  for (;; ++P, ++Q) {
    if (*Q == '\0')
      return P;
    if (*P != *Q)
      return nullptr;
  }
}

// ===----------------------------------------------------------------------===
//  Byte-needle search (memmem equivalent).  Returns offset, not pointer,
//  so the result survives Haystack relocations such as BinaryResize.
// ===----------------------------------------------------------------------===

static uint64_t bridgeMemFind(const void *Haystack, uint64_t HaystackLen,
                              const void *Needle, uint64_t NeedleLen) {
  if (LLVM_UNLIKELY(!Haystack || !Needle))
    return UINT64_MAX;
  if (LLVM_UNLIKELY(NeedleLen == 0 || NeedleLen > HaystackLen))
    return UINT64_MAX;
  if (LLVM_UNLIKELY(exceedsSizeT(HaystackLen)))
    return UINT64_MAX;

  const auto *H = static_cast<const unsigned char *>(Haystack);
  const auto *N = static_cast<const unsigned char *>(Needle);

  // Single-byte needle: defer to libc memchr (typically SIMD-accelerated).
  if (NeedleLen == 1) {
    const void *P = std::memchr(H, N[0], static_cast<size_t>(HaystackLen));
    if (!P)
      return UINT64_MAX;
    return static_cast<uint64_t>(static_cast<const unsigned char *>(P) - H);
  }

  // Two-Way / Boyer-Moore-Horspool would be faster for large inputs, but
  // pass authors typically scan short binary blobs, so a naive memchr-anchored
  // sweep wins on simplicity and stays branch-prediction-friendly.
  uint64_t Last = HaystackLen - NeedleLen;
  uint64_t I = 0;
  while (I <= Last) {
    const void *Hit = std::memchr(H + I, N[0],
                                  static_cast<size_t>(Last - I + 1));
    if (!Hit)
      return UINT64_MAX;
    uint64_t Off = static_cast<uint64_t>(
        static_cast<const unsigned char *>(Hit) - H);
    if (std::memcmp(H + Off, N, static_cast<size_t>(NeedleLen)) == 0)
      return Off;
    I = Off + 1;
  }
  return UINT64_MAX;
}

// ===----------------------------------------------------------------------===
//  Byte occurrence count -- delegates to libc memchr in a tight loop so the
//  hot path stays SIMD-accelerated on platforms where memchr is vectorized.
// ===----------------------------------------------------------------------===

static uint64_t bridgeMemCount(const void *Haystack, uint64_t HaystackLen,
                               int Byte) {
  if (LLVM_UNLIKELY(!Haystack || HaystackLen == 0))
    return 0;
  if (LLVM_UNLIKELY(exceedsSizeT(HaystackLen)))
    return 0;
  unsigned char Needle = static_cast<unsigned char>(Byte);
  const auto *H = static_cast<const unsigned char *>(Haystack);
  size_t Remaining = static_cast<size_t>(HaystackLen);
  uint64_t Count = 0;
  while (Remaining > 0) {
    const void *Hit = std::memchr(H, Needle, Remaining);
    if (!Hit)
      break;
    ++Count;
    size_t Skip = static_cast<size_t>(
                      static_cast<const unsigned char *>(Hit) - H) +
                  1;
    H += Skip;
    Remaining -= Skip;
  }
  return Count;
}

// ===----------------------------------------------------------------------===
//  Single-byte search returning an offset.  Thin wrapper around memchr
//  that avoids the caller constructing a 1-byte needle array.
// ===----------------------------------------------------------------------===

static uint64_t bridgeMemFindByte(const void *Haystack, uint64_t HaystackLen,
                                  uint8_t Byte) {
  if (LLVM_UNLIKELY(!Haystack || HaystackLen == 0))
    return UINT64_MAX;
  if (LLVM_UNLIKELY(exceedsSizeT(HaystackLen)))
    return UINT64_MAX;
  const void *P =
      std::memchr(Haystack, Byte, static_cast<size_t>(HaystackLen));
  if (!P)
    return UINT64_MAX;
  return static_cast<uint64_t>(static_cast<const unsigned char *>(P) -
                               static_cast<const unsigned char *>(Haystack));
}

// ===----------------------------------------------------------------------===
//  Character class scanning (strspn / strcspn equivalents)
//
//  Build a 256-bit bitset from the class on entry then walk S with one
//  branch per byte.  Faster than the libc strspn/strcspn variants on
//  short character classes that revisit the class for every byte.
// ===----------------------------------------------------------------------===

namespace {
struct ByteClass {
  // 256 bits packed into 4x uint64_t.  Built once per call; queried by
  // ((Bits[B >> 6] >> (B & 63)) & 1).
  uint64_t Bits[4] = {0, 0, 0, 0};

  void add(unsigned char B) { Bits[B >> 6] |= (uint64_t{1} << (B & 63)); }
  bool contains(unsigned char B) const {
    return (Bits[B >> 6] >> (B & 63)) & uint64_t{1};
  }
  void buildFrom(const char *Class) {
    for (const unsigned char *P = reinterpret_cast<const unsigned char *>(
             Class);
         *P; ++P)
      add(*P);
  }
};
} // anonymous namespace

static uint64_t bridgeStrSpan(const char *S, const char *Accept) {
  if (LLVM_UNLIKELY(!S))
    return 0;
  if (LLVM_UNLIKELY(!Accept || !*Accept))
    return 0;
  ByteClass C;
  C.buildFrom(Accept);
  const unsigned char *P = reinterpret_cast<const unsigned char *>(S);
  const unsigned char *Start = P;
  while (*P && C.contains(*P))
    ++P;
  return static_cast<uint64_t>(P - Start);
}

static uint64_t bridgeStrCSpn(const char *S, const char *Reject) {
  if (LLVM_UNLIKELY(!S))
    return 0;
  const unsigned char *P = reinterpret_cast<const unsigned char *>(S);
  const unsigned char *Start = P;
  if (LLVM_UNLIKELY(!Reject || !*Reject)) {
    while (*P)
      ++P;
    return static_cast<uint64_t>(P - Start);
  }
  ByteClass C;
  C.buildFrom(Reject);
  while (*P && !C.contains(*P))
    ++P;
  return static_cast<uint64_t>(P - Start);
}

// ===----------------------------------------------------------------------===
//  ASCII case-insensitive comparison
//
//  Lowercase only ASCII letters in [A-Z]; non-ASCII bytes pass through as
//  raw uint8.  Branchless lowercasing via subtract-shift-AND mask.
// ===----------------------------------------------------------------------===

static inline unsigned char asciiToLower(unsigned char C) {
  // (C - 'A' < 26U) ? C | 0x20 : C, branchless variant.
  unsigned Diff = static_cast<unsigned>(C) - 'A';
  unsigned Mask = (Diff < 26U) ? 0x20U : 0U;
  return static_cast<unsigned char>(C | Mask);
}

static int bridgeStrICompare(const char *A, const char *B) {
  if (LLVM_UNLIKELY(!A || !B))
    return (A == B) ? 0 : (A ? 1 : -1);
  const auto *PA = reinterpret_cast<const unsigned char *>(A);
  const auto *PB = reinterpret_cast<const unsigned char *>(B);
  for (;; ++PA, ++PB) {
    unsigned char CA = asciiToLower(*PA);
    unsigned char CB = asciiToLower(*PB);
    if (CA != CB)
      return (CA < CB) ? -1 : 1;
    if (CA == 0)
      return 0;
  }
}

static int bridgeStrIEqual(const char *A, const char *B) {
  if (LLVM_UNLIKELY(!A || !B))
    return (A == B) ? 1 : 0;
  if (A == B)
    return 1;
  const auto *PA = reinterpret_cast<const unsigned char *>(A);
  const auto *PB = reinterpret_cast<const unsigned char *>(B);
  for (;; ++PA, ++PB) {
    unsigned char CA = asciiToLower(*PA);
    unsigned char CB = asciiToLower(*PB);
    if (CA != CB)
      return 0;
    if (CA == 0)
      return 1;
  }
}

// ===----------------------------------------------------------------------===
//  Path manipulation -- recognize both '/' and '\\' as separators in a
//  single pass.  Zero allocation; offsets are returned so the caller can
//  combine with StrSubstring/StrDup only when an owned copy is actually
//  needed.
// ===----------------------------------------------------------------------===

static uint64_t bridgePathBaseNameOffset(const char *Path) {
  if (LLVM_UNLIKELY(!Path))
    return 0;
  uint64_t LastSep = UINT64_MAX;
  for (const char *P = Path; *P; ++P) {
    if (*P == '/' || *P == '\\')
      LastSep = static_cast<uint64_t>(P - Path);
  }
  return LastSep == UINT64_MAX ? 0 : LastSep + 1;
}

static uint64_t bridgePathExtOffset(const char *Path) {
  if (LLVM_UNLIKELY(!Path))
    return UINT64_MAX;
  uint64_t BaseStart = 0;
  uint64_t LastDot = UINT64_MAX;
  for (const char *P = Path; *P; ++P) {
    char C = *P;
    if (C == '/' || C == '\\') {
      BaseStart = static_cast<uint64_t>(P - Path) + 1;
      LastDot = UINT64_MAX;
    } else if (C == '.') {
      LastDot = static_cast<uint64_t>(P - Path);
    }
  }
  // Reject "no dot", "trailing dot", and leading-dot dotfiles
  // ("/.bashrc" -> LastDot == BaseStart -> no extension).
  if (LastDot == UINT64_MAX || LastDot == BaseStart)
    return UINT64_MAX;
  if (Path[LastDot + 1] == '\0')
    return UINT64_MAX;
  return LastDot;
}

// ===----------------------------------------------------------------------===
//  One-call defined function collection
// ===----------------------------------------------------------------------===

static NevercValueRef *
bridgeModuleCollectDefinedFunctions(NevercModuleRef M, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);

  // Two-pass: exact count then exact allocation.  The arena variant uses
  // Mod->size() as an upper bound (over-allocation is free for arenas),
  // but the heap variant must allocate precisely since the caller Frees
  // the buffer -- over-allocating wastes memory for modules where
  // declarations vastly outnumber definitions.
  unsigned Count = 0;
  for (auto &F : *Mod)
    if (!F.isDeclaration())
      ++Count;
  if (LLVM_UNLIKELY(Count == 0 ||
                    Count > SIZE_MAX / sizeof(NevercValueRef)))
    return nullptr;

  auto *Buf = static_cast<NevercValueRef *>(
      bridgeAlloc(static_cast<uint64_t>(Count) * sizeof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &F : *Mod)
    if (!F.isDeclaration())
      Buf[Idx++] = wrapV(&F);
  *OutCount = Count;
  return Buf;
}

// ===----------------------------------------------------------------------===
//  Sort / BSearch -- routed through host to avoid cross-CRT calls
// ===----------------------------------------------------------------------===

static void bridgeSort(void *Base, uint64_t NumElements, uint64_t ElemSize,
                       int (*Cmp)(const void *, const void *)) {
  if (LLVM_UNLIKELY(!Base || !Cmp || NumElements <= 1 || ElemSize == 0))
    return;
  if (LLVM_UNLIKELY(exceedsSizeT(NumElements) || exceedsSizeT(ElemSize)))
    return;
  std::qsort(Base, static_cast<size_t>(NumElements),
             static_cast<size_t>(ElemSize), Cmp);
}

static const void *bridgeBSearch(const void *Key, const void *Base,
                                 uint64_t NumElements, uint64_t ElemSize,
                                 int (*Cmp)(const void *, const void *)) {
  if (LLVM_UNLIKELY(!Key || !Base || !Cmp || NumElements == 0 || ElemSize == 0))
    return nullptr;
  if (LLVM_UNLIKELY(exceedsSizeT(NumElements) || exceedsSizeT(ElemSize)))
    return nullptr;
  return std::bsearch(Key, Base, static_cast<size_t>(NumElements),
                      static_cast<size_t>(ElemSize), Cmp);
}

// ===----------------------------------------------------------------------===
//  SortCtx / BSearchCtx -- context-aware sort and binary search
//  The 3-arg comparator int(*)(a, b, ctx) lets plugin callbacks access the
//  host API vtable or pass state without hand-rolling CRT-like helpers.
//  Implemented via a thread-local thunk: SortCtx stores the user comparator
//  and context in TLS, calls std::qsort with a 2-arg shim that forwards to
//  the 3-arg function.  The TLS access is a single memory load on x86/ARM64
//  and introduces zero per-call heap allocation.
// ===----------------------------------------------------------------------===

namespace {

struct SortCtxTLS {
  int (*Cmp)(const void *, const void *, void *);
  void *Ctx;
};

static thread_local SortCtxTLS TheSortCtx;

static int sortCtxThunk(const void *A, const void *B) {
  return TheSortCtx.Cmp(A, B, TheSortCtx.Ctx);
}

struct SortCtxGuard {
  SortCtxTLS Saved;
  SortCtxGuard(int (*Cmp)(const void *, const void *, void *), void *Ctx)
      : Saved(TheSortCtx) {
    TheSortCtx.Cmp = Cmp;
    TheSortCtx.Ctx = Ctx;
  }
  ~SortCtxGuard() { TheSortCtx = Saved; }
  SortCtxGuard(const SortCtxGuard &) = delete;
  SortCtxGuard &operator=(const SortCtxGuard &) = delete;
};

} // namespace

static void bridgeSortCtx(void *Base, uint64_t NumElements, uint64_t ElemSize,
                          int (*Cmp)(const void *, const void *, void *),
                          void *Ctx) {
  if (LLVM_UNLIKELY(!Base || !Cmp || NumElements <= 1 || ElemSize == 0))
    return;
  if (LLVM_UNLIKELY(exceedsSizeT(NumElements) || exceedsSizeT(ElemSize)))
    return;
  SortCtxGuard G(Cmp, Ctx);
  std::qsort(Base, static_cast<size_t>(NumElements),
             static_cast<size_t>(ElemSize), sortCtxThunk);
}

static const void *
bridgeBSearchCtx(const void *Key, const void *Base, uint64_t NumElements,
                 uint64_t ElemSize,
                 int (*Cmp)(const void *, const void *, void *), void *Ctx) {
  if (LLVM_UNLIKELY(!Key || !Base || !Cmp || NumElements == 0 || ElemSize == 0))
    return nullptr;
  if (LLVM_UNLIKELY(exceedsSizeT(NumElements) || exceedsSizeT(ElemSize)))
    return nullptr;
  SortCtxGuard G(Cmp, Ctx);
  return std::bsearch(Key, Base, static_cast<size_t>(NumElements),
                      static_cast<size_t>(ElemSize), sortCtxThunk);
}

// ===----------------------------------------------------------------------===
//  StrFormatBuf -- snprintf to caller-owned buffer (zero allocation)
// ===----------------------------------------------------------------------===

static int bridgeStrFormatBufV(char *Buf, uint64_t BufSize, const char *Fmt,
                               va_list Args) {
  if (LLVM_UNLIKELY(!Fmt))
    return -1;
  if (LLVM_UNLIKELY(!Buf && BufSize > 0))
    BufSize = 0;
  size_t Sz = clampToSizeT(BufSize);
  return std::vsnprintf(Buf, Sz, Fmt, Args);
}

static int bridgeStrFormatBuf(char *Buf, uint64_t BufSize, const char *Fmt,
                              ...) {
  va_list Args;
  va_start(Args, Fmt);
  int Ret = bridgeStrFormatBufV(Buf, BufSize, Fmt, Args);
  va_end(Args);
  return Ret;
}

// ===----------------------------------------------------------------------===
//  Bounded string compare
// ===----------------------------------------------------------------------===

static int bridgeStrNCompare(const char *A, const char *B, uint64_t MaxLen) {
  if (A == B || MaxLen == 0)
    return 0;
  if (LLVM_UNLIKELY(!A))
    return -1;
  if (LLVM_UNLIKELY(!B))
    return 1;
  size_t N = clampToSizeT(MaxLen);
  return std::strncmp(A, B, N);
}

// ===----------------------------------------------------------------------===
//  StrCopyBuf -- strlcpy semantics (always null-terminates, zero allocation)
// ===----------------------------------------------------------------------===

static uint64_t bridgeStrCopyBuf(char *Buf, uint64_t BufSize,
                                 const char *Src) {
  if (LLVM_UNLIKELY(!Src))
    return 0;
  size_t SrcLen = std::strlen(Src);
  if (BufSize > 0 && LLVM_LIKELY(Buf != nullptr)) {
    size_t CopyLen = (SrcLen < BufSize) ? SrcLen : BufSize - 1;
    std::memcpy(Buf, Src, CopyLen);
    Buf[CopyLen] = '\0';
  }
  return static_cast<uint64_t>(SrcLen);
}

// ===----------------------------------------------------------------------===
//  DynArray -- opaque growable array
//  Geometric 2x growth, contiguous buffer, cache-friendly iteration.
//  Two allocations: a small header (ElemSize/Count/Capacity/DataPtr) and the
//  data buffer itself.  Both go through the host allocator.
// ===----------------------------------------------------------------------===

namespace {
struct DynArrayImpl {
  uint64_t ElemSize;
  unsigned Count;
  unsigned Capacity;
  char *Data;
};
} // namespace

static inline DynArrayImpl *unwrapDA(NevercDynArrayRef A) {
  return reinterpret_cast<DynArrayImpl *>(A);
}
static inline NevercDynArrayRef wrapDA(DynArrayImpl *A) {
  return reinterpret_cast<NevercDynArrayRef>(A);
}

static int dynArrayGrowTo(DynArrayImpl *A, unsigned MinCapacity) {
  if (LLVM_LIKELY(MinCapacity <= A->Capacity))
    return 1;
  unsigned NewCap = A->Capacity == 0 ? 16 : A->Capacity;
  while (NewCap < MinCapacity) {
    unsigned Doubled = NewCap * 2;
    if (LLVM_UNLIKELY(Doubled <= NewCap))
      return 0;
    NewCap = Doubled;
  }
  uint64_t Bytes = static_cast<uint64_t>(NewCap) * A->ElemSize;
  if (LLVM_UNLIKELY(Bytes / A->ElemSize != NewCap))
    return 0;
  char *NewData = static_cast<char *>(bridgeRealloc(A->Data, Bytes));
  if (LLVM_UNLIKELY(!NewData))
    return 0;
  A->Data = NewData;
  A->Capacity = NewCap;
  return 1;
}

static NevercDynArrayRef bridgeDynArrayCreate(uint64_t ElemSize) {
  if (LLVM_UNLIKELY(ElemSize == 0 || ElemSize > SIZE_MAX / 16))
    return nullptr;
  auto *A = static_cast<DynArrayImpl *>(bridgeAlloc(sizeof(DynArrayImpl)));
  if (LLVM_UNLIKELY(!A))
    return nullptr;
  A->ElemSize = ElemSize;
  A->Count = 0;
  A->Capacity = 0;
  A->Data = nullptr;
  return wrapDA(A);
}

static void bridgeDynArrayDestroy(NevercDynArrayRef Arr) {
  if (LLVM_UNLIKELY(!Arr))
    return;
  auto *A = unwrapDA(Arr);
  bridgeFree(A->Data);
  bridgeFree(A);
}

static int bridgeDynArrayPush(NevercDynArrayRef Arr, const void *Elem) {
  if (LLVM_UNLIKELY(!Arr || !Elem))
    return 0;
  auto *A = unwrapDA(Arr);
  unsigned NewCount = A->Count + 1;
  if (LLVM_UNLIKELY(NewCount == 0))
    return 0;
  if (LLVM_UNLIKELY(!dynArrayGrowTo(A, NewCount)))
    return 0;
  std::memcpy(A->Data + static_cast<uint64_t>(A->Count) * A->ElemSize, Elem,
              static_cast<size_t>(A->ElemSize));
  A->Count = NewCount;
  return 1;
}

static void *bridgeDynArrayGet(NevercDynArrayRef Arr, unsigned Idx) {
  if (LLVM_UNLIKELY(!Arr))
    return nullptr;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(Idx >= A->Count))
    return nullptr;
  return A->Data + static_cast<uint64_t>(Idx) * A->ElemSize;
}

static unsigned bridgeDynArrayCount(NevercDynArrayRef Arr) {
  return LLVM_LIKELY(Arr) ? unwrapDA(Arr)->Count : 0;
}

static void *bridgeDynArrayData(NevercDynArrayRef Arr) {
  return LLVM_LIKELY(Arr) ? unwrapDA(Arr)->Data : nullptr;
}

static void bridgeDynArrayClear(NevercDynArrayRef Arr) {
  if (LLVM_LIKELY(Arr))
    unwrapDA(Arr)->Count = 0;
}

static void bridgeDynArraySort(NevercDynArrayRef Arr,
                               int (*Cmp)(const void *, const void *)) {
  if (LLVM_UNLIKELY(!Arr || !Cmp))
    return;
  auto *A = unwrapDA(Arr);
  if (A->Count > 1)
    std::qsort(A->Data, A->Count, static_cast<size_t>(A->ElemSize), Cmp);
}

static void bridgeDynArraySortCtx(
    NevercDynArrayRef Arr,
    int (*Cmp)(const void *, const void *, void *), void *Ctx) {
  if (LLVM_UNLIKELY(!Arr || !Cmp))
    return;
  auto *A = unwrapDA(Arr);
  if (A->Count <= 1)
    return;
  SortCtxGuard G(Cmp, Ctx);
  std::qsort(A->Data, A->Count, static_cast<size_t>(A->ElemSize),
             sortCtxThunk);
}

static void *bridgeDynArrayPop(NevercDynArrayRef Arr) {
  if (LLVM_UNLIKELY(!Arr))
    return nullptr;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(A->Count == 0))
    return nullptr;
  --A->Count;
  return A->Data + static_cast<uint64_t>(A->Count) * A->ElemSize;
}

static int bridgeDynArrayReserve(NevercDynArrayRef Arr, unsigned MinCapacity) {
  if (LLVM_UNLIKELY(!Arr))
    return 0;
  auto *A = unwrapDA(Arr);
  if (A->Capacity >= MinCapacity)
    return 1;
  uint64_t Bytes = static_cast<uint64_t>(MinCapacity) * A->ElemSize;
  if (LLVM_UNLIKELY(Bytes / A->ElemSize != MinCapacity))
    return 0;
  char *NewData = static_cast<char *>(bridgeRealloc(A->Data, Bytes));
  if (LLVM_UNLIKELY(!NewData))
    return 0;
  A->Data = NewData;
  A->Capacity = MinCapacity;
  return 1;
}

static void *bridgeDynArrayDetach(NevercDynArrayRef Arr, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arr))
    return nullptr;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(A->Count == 0)) {
    bridgeFree(A->Data);
    bridgeFree(A);
    return nullptr;
  }
  uint64_t ExactBytes = static_cast<uint64_t>(A->Count) * A->ElemSize;
  if (A->Count < A->Capacity) {
    char *Shrunk = static_cast<char *>(bridgeRealloc(A->Data, ExactBytes));
    if (LLVM_LIKELY(Shrunk))
      A->Data = Shrunk;
  }
  void *Result = A->Data;
  if (OutCount)
    *OutCount = A->Count;
  bridgeFree(A);
  return Result;
}

// ===----------------------------------------------------------------------===
//  StrMap -- opaque string-keyed hash table
//  Backed by LLVM's StringMap: open addressing with quadratic probing,
//  cache-friendly allocation-dense buckets, keys copied inline.
// ===----------------------------------------------------------------------===

static inline StringMap<uint64_t> *unwrapSM(NevercStrMapRef M) {
  return reinterpret_cast<StringMap<uint64_t> *>(M);
}
static inline NevercStrMapRef wrapSM(StringMap<uint64_t> *M) {
  return reinterpret_cast<NevercStrMapRef>(M);
}

static NevercStrMapRef bridgeStrMapCreate() {
  auto *M = new (std::nothrow) StringMap<uint64_t>();
  return wrapSM(M);
}

static void bridgeStrMapDestroy(NevercStrMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return;
  delete unwrapSM(Map);
}

static int bridgeStrMapPut(NevercStrMapRef Map, const char *Key,
                           uint64_t Value) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  (*unwrapSM(Map))[Key] = Value;
  return 1;
}

static int bridgeStrMapGet(NevercStrMapRef Map, const char *Key,
                           uint64_t *OutValue) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  auto *M = unwrapSM(Map);
  auto It = M->find(Key);
  if (It == M->end())
    return 0;
  if (OutValue)
    *OutValue = It->second;
  return 1;
}

static int bridgeStrMapHas(NevercStrMapRef Map, const char *Key) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  return unwrapSM(Map)->count(Key) != 0;
}

static void bridgeStrMapRemove(NevercStrMapRef Map, const char *Key) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return;
  unwrapSM(Map)->erase(Key);
}

static unsigned bridgeStrMapCount(NevercStrMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return 0;
  return static_cast<unsigned>(unwrapSM(Map)->size());
}

static void bridgeStrMapForEach(NevercStrMapRef Map,
                                int (*Fn)(const char *, uint64_t, void *),
                                void *Ctx) {
  if (LLVM_UNLIKELY(!Map || !Fn))
    return;
  for (const auto &E : *unwrapSM(Map))
    if (Fn(E.getKeyData(), E.second, Ctx))
      break;
}

static char **bridgeStrMapCollectKeys(NevercStrMapRef Map,
                                      unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Map || !OutCount))
    return nullptr;
  auto *M = unwrapSM(Map);
  size_t RawCount = M->size();
  if (LLVM_UNLIKELY(RawCount == 0 || RawCount > UINT_MAX))
    return nullptr;
  unsigned Count = static_cast<unsigned>(RawCount);
  auto **Keys = static_cast<char **>(
      bridgeAlloc(static_cast<uint64_t>(Count) * sizeof(char *)));
  if (LLVM_UNLIKELY(!Keys))
    return nullptr;
  unsigned Idx = 0;
  for (const auto &E : *M) {
    size_t Len = E.getKeyLength();
    char *Key = static_cast<char *>(bridgeAlloc(Len + 1));
    if (LLVM_UNLIKELY(!Key)) {
      for (unsigned J = 0; J < Idx; ++J)
        bridgeFree(Keys[J]);
      bridgeFree(Keys);
      return nullptr;
    }
    std::memcpy(Key, E.getKeyData(), Len);
    Key[Len] = '\0';
    Keys[Idx++] = Key;
  }
  *OutCount = Idx;
  return Keys;
}

static uint64_t bridgeStrMapIncrement(NevercStrMapRef Map, const char *Key,
                                      uint64_t Delta) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  auto &Val = (*unwrapSM(Map))[Key];
  Val += Delta;
  return Val;
}

static void bridgeStrMapClear(NevercStrMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return;
  unwrapSM(Map)->clear();
}

// ===----------------------------------------------------------------------===
//  N-bounded StrMap operations (key pointer + length, no null terminator)
//  Uses StringRef directly -- zero-copy lookup, key copied on insert.
// ===----------------------------------------------------------------------===

static inline StringRef toKeyRef(const char *Key, uint64_t KeyLen) {
  return StringRef(Key, clampToSizeT(KeyLen));
}

static int bridgeStrMapPutN(NevercStrMapRef Map, const char *Key,
                            uint64_t KeyLen, uint64_t Value) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  (*unwrapSM(Map))[toKeyRef(Key, KeyLen)] = Value;
  return 1;
}

static int bridgeStrMapGetN(NevercStrMapRef Map, const char *Key,
                            uint64_t KeyLen, uint64_t *OutValue) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  auto *M = unwrapSM(Map);
  auto It = M->find(toKeyRef(Key, KeyLen));
  if (It == M->end())
    return 0;
  if (OutValue)
    *OutValue = It->second;
  return 1;
}

static int bridgeStrMapHasN(NevercStrMapRef Map, const char *Key,
                            uint64_t KeyLen) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  return unwrapSM(Map)->count(toKeyRef(Key, KeyLen)) != 0;
}

static uint64_t bridgeStrMapIncrementN(NevercStrMapRef Map, const char *Key,
                                       uint64_t KeyLen, uint64_t Delta) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  auto &Val = (*unwrapSM(Map))[toKeyRef(Key, KeyLen)];
  Val += Delta;
  return Val;
}

static void bridgeStrMapRemoveN(NevercStrMapRef Map, const char *Key,
                                uint64_t KeyLen) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return;
  unwrapSM(Map)->erase(toKeyRef(Key, KeyLen));
}

// ===----------------------------------------------------------------------===
//  StrBuilder -- opaque incremental string construction
//  Backed by LLVM's SmallString<256>: inline storage avoids heap alloc
//  for strings up to 256 bytes, geometric growth beyond.
// ===----------------------------------------------------------------------===

static inline SmallString<256> *unwrapSB(NevercStrBuilderRef SB) {
  return reinterpret_cast<SmallString<256> *>(SB);
}
static inline NevercStrBuilderRef wrapSB(SmallString<256> *SB) {
  return reinterpret_cast<NevercStrBuilderRef>(SB);
}

static NevercStrBuilderRef bridgeStrBuilderCreate() {
  return wrapSB(new (std::nothrow) SmallString<256>());
}

static void bridgeStrBuilderDestroy(NevercStrBuilderRef SB) {
  if (LLVM_UNLIKELY(!SB))
    return;
  delete unwrapSB(SB);
}

static void bridgeStrBuilderAppend(NevercStrBuilderRef SB, const char *S) {
  if (LLVM_UNLIKELY(!SB || !S))
    return;
  unwrapSB(SB)->append(StringRef(S));
}

static void bridgeStrBuilderAppendN(NevercStrBuilderRef SB, const char *S,
                                    uint64_t Len) {
  if (LLVM_UNLIKELY(!SB || !S))
    return;
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  unwrapSB(SB)->append(StringRef(S, static_cast<size_t>(Len)));
}

static void bridgeStrBuilderAppendChar(NevercStrBuilderRef SB, char C) {
  if (LLVM_UNLIKELY(!SB))
    return;
  unwrapSB(SB)->push_back(C);
}

static void bridgeStrBuilderAppendV(NevercStrBuilderRef SB, const char *Fmt,
                                    va_list Args) {
  if (LLVM_UNLIKELY(!SB || !Fmt))
    return;
  char Stack[1024];
  va_list ArgsCopy;
  va_copy(ArgsCopy, Args);
  int Len = std::vsnprintf(Stack, sizeof(Stack), Fmt, ArgsCopy);
  va_end(ArgsCopy);
  if (LLVM_UNLIKELY(Len < 0))
    return;
  auto *B = unwrapSB(SB);
  size_t Need = static_cast<size_t>(Len);
  if (LLVM_LIKELY(Need < sizeof(Stack))) {
    B->append(StringRef(Stack, Need));
  } else {
    size_t OldSize = B->size();
    B->resize_for_overwrite(OldSize + Need + 1);
    va_list Args2;
    va_copy(Args2, Args);
    std::vsnprintf(B->data() + OldSize, Need + 1, Fmt, Args2);
    va_end(Args2);
    B->pop_back();
  }
}

static void bridgeStrBuilderAppendF(NevercStrBuilderRef SB, const char *Fmt,
                                    ...) {
  va_list Args;
  va_start(Args, Fmt);
  bridgeStrBuilderAppendV(SB, Fmt, Args);
  va_end(Args);
}

static char *bridgeStrBuilderFinish(NevercStrBuilderRef SB) {
  if (LLVM_UNLIKELY(!SB))
    return nullptr;
  auto *B = unwrapSB(SB);
  size_t Len = B->size();
  char *Result = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!Result))
    return nullptr;
  if (Len)
    std::memcpy(Result, B->data(), Len);
  Result[Len] = '\0';
  B->clear();
  return Result;
}

static uint64_t bridgeStrBuilderLen(NevercStrBuilderRef SB) {
  return LLVM_LIKELY(SB) ? unwrapSB(SB)->size() : 0;
}

static void bridgeStrBuilderClear(NevercStrBuilderRef SB) {
  if (LLVM_LIKELY(SB))
    unwrapSB(SB)->clear();
}

static const char *bridgeStrBuilderGetStr(NevercStrBuilderRef SB) {
  if (LLVM_UNLIKELY(!SB))
    return nullptr;
  return unwrapSB(SB)->c_str();
}

// ===----------------------------------------------------------------------===
//  DynArray batch / mutate operations
// ===----------------------------------------------------------------------===

static int bridgeDynArrayPushN(NevercDynArrayRef Arr, const void *Data,
                               unsigned Count) {
  if (LLVM_UNLIKELY(!Arr))
    return 0;
  if (Count == 0)
    return 1;
  if (LLVM_UNLIKELY(!Data))
    return 0;
  auto *A = unwrapDA(Arr);
  unsigned NewCount = A->Count + Count;
  if (LLVM_UNLIKELY(NewCount < A->Count))
    return 0;
  if (LLVM_UNLIKELY(!dynArrayGrowTo(A, NewCount)))
    return 0;
  std::memcpy(A->Data + static_cast<uint64_t>(A->Count) * A->ElemSize, Data,
              static_cast<uint64_t>(Count) * A->ElemSize);
  A->Count = NewCount;
  return 1;
}

static void bridgeDynArrayRemoveSwap(NevercDynArrayRef Arr, unsigned Idx) {
  if (LLVM_UNLIKELY(!Arr))
    return;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(Idx >= A->Count))
    return;
  --A->Count;
  if (Idx != A->Count) {
    std::memcpy(A->Data + static_cast<uint64_t>(Idx) * A->ElemSize,
                A->Data + static_cast<uint64_t>(A->Count) * A->ElemSize,
                static_cast<size_t>(A->ElemSize));
  }
}

static void bridgeDynArrayShrinkToFit(NevercDynArrayRef Arr) {
  if (LLVM_UNLIKELY(!Arr))
    return;
  auto *A = unwrapDA(Arr);
  if (A->Count == 0) {
    bridgeFree(A->Data);
    A->Data = nullptr;
    A->Capacity = 0;
    return;
  }
  if (A->Count == A->Capacity)
    return;
  uint64_t ExactBytes = static_cast<uint64_t>(A->Count) * A->ElemSize;
  char *Shrunk = static_cast<char *>(bridgeRealloc(A->Data, ExactBytes));
  if (LLVM_LIKELY(Shrunk)) {
    A->Data = Shrunk;
    A->Capacity = A->Count;
  }
}

// ===----------------------------------------------------------------------===
//  Order-preserving DynArray mutation: O(N) random insert and remove.
//  Use RemoveSwap when iteration order doesn't matter (O(1)).
// ===----------------------------------------------------------------------===

static int bridgeDynArrayInsert(NevercDynArrayRef Arr, unsigned Idx,
                                const void *Elem) {
  if (LLVM_UNLIKELY(!Arr || !Elem))
    return 0;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(Idx > A->Count))
    return 0;
  unsigned NewCount = A->Count + 1;
  if (LLVM_UNLIKELY(NewCount < A->Count))
    return 0;
  if (LLVM_UNLIKELY(!dynArrayGrowTo(A, NewCount)))
    return 0;
  uint64_t Stride = A->ElemSize;
  char *Slot = A->Data + static_cast<uint64_t>(Idx) * Stride;
  if (Idx < A->Count) {
    std::memmove(Slot + Stride, Slot,
                 static_cast<size_t>((A->Count - Idx) * Stride));
  }
  std::memcpy(Slot, Elem, static_cast<size_t>(Stride));
  A->Count = NewCount;
  return 1;
}

static void bridgeDynArrayRemoveOrdered(NevercDynArrayRef Arr, unsigned Idx) {
  if (LLVM_UNLIKELY(!Arr))
    return;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(Idx >= A->Count))
    return;
  uint64_t Stride = A->ElemSize;
  unsigned Tail = A->Count - Idx - 1;
  if (Tail) {
    char *Slot = A->Data + static_cast<uint64_t>(Idx) * Stride;
    std::memmove(Slot, Slot + Stride, static_cast<size_t>(Tail * Stride));
  }
  --A->Count;
}

// ===----------------------------------------------------------------------===
//  StrMap with pre-allocated capacity
// ===----------------------------------------------------------------------===

static NevercStrMapRef bridgeStrMapCreateSized(unsigned InitialCapacity) {
  auto *M = new (std::nothrow) StringMap<uint64_t>(InitialCapacity);
  return wrapSM(M);
}

// ===----------------------------------------------------------------------===
//  IntMap -- integer-keyed hash table
//  Backed by LLVM DenseMap<uint64_t, uint64_t>: open addressing, quadratic
//  probing, cache-friendly contiguous buckets.  Sentinel keys 0xFFFF...FF
//  and 0xFFFF...FE are rejected at the API boundary.
// ===----------------------------------------------------------------------===

static constexpr uint64_t kIntMapEmptyKey = ~uint64_t(0);
static constexpr uint64_t kIntMapTombstone = ~uint64_t(0) - 1;

static inline bool isIntMapReservedKey(uint64_t Key) {
  return Key >= kIntMapTombstone;
}

static inline DenseMap<uint64_t, uint64_t> *unwrapIM(NevercIntMapRef M) {
  return reinterpret_cast<DenseMap<uint64_t, uint64_t> *>(M);
}
static inline NevercIntMapRef wrapIM(DenseMap<uint64_t, uint64_t> *M) {
  return reinterpret_cast<NevercIntMapRef>(M);
}

static NevercIntMapRef bridgeIntMapCreate() {
  auto *M = new (std::nothrow) DenseMap<uint64_t, uint64_t>();
  return wrapIM(M);
}

static void bridgeIntMapDestroy(NevercIntMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return;
  delete unwrapIM(Map);
}

static int bridgeIntMapPut(NevercIntMapRef Map, uint64_t Key, uint64_t Value) {
  if (LLVM_UNLIKELY(!Map || isIntMapReservedKey(Key)))
    return 0;
  (*unwrapIM(Map))[Key] = Value;
  return 1;
}

static int bridgeIntMapGet(NevercIntMapRef Map, uint64_t Key,
                           uint64_t *OutValue) {
  if (LLVM_UNLIKELY(!Map || isIntMapReservedKey(Key)))
    return 0;
  auto *M = unwrapIM(Map);
  auto It = M->find(Key);
  if (It == M->end())
    return 0;
  if (OutValue)
    *OutValue = It->second;
  return 1;
}

static int bridgeIntMapHas(NevercIntMapRef Map, uint64_t Key) {
  if (LLVM_UNLIKELY(!Map || isIntMapReservedKey(Key)))
    return 0;
  return unwrapIM(Map)->count(Key) != 0;
}

static void bridgeIntMapRemove(NevercIntMapRef Map, uint64_t Key) {
  if (LLVM_UNLIKELY(!Map || isIntMapReservedKey(Key)))
    return;
  unwrapIM(Map)->erase(Key);
}

static unsigned bridgeIntMapCount(NevercIntMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return 0;
  return static_cast<unsigned>(unwrapIM(Map)->size());
}

static uint64_t bridgeIntMapIncrement(NevercIntMapRef Map, uint64_t Key,
                                      uint64_t Delta) {
  if (LLVM_UNLIKELY(!Map || isIntMapReservedKey(Key)))
    return 0;
  auto &Val = (*unwrapIM(Map))[Key];
  Val += Delta;
  return Val;
}

static void bridgeIntMapForEach(NevercIntMapRef Map,
                                int (*Fn)(uint64_t, uint64_t, void *),
                                void *Ctx) {
  if (LLVM_UNLIKELY(!Map || !Fn))
    return;
  for (const auto &E : *unwrapIM(Map))
    if (Fn(E.first, E.second, Ctx))
      break;
}

static void bridgeIntMapClear(NevercIntMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return;
  unwrapIM(Map)->clear();
}

static NevercIntMapRef bridgeIntMapCreateSized(unsigned InitialCapacity) {
  auto *M = new (std::nothrow) DenseMap<uint64_t, uint64_t>(InitialCapacity);
  return wrapIM(M);
}

// ===----------------------------------------------------------------------===
//  DynArray binary search -- requires prior DynArraySort with same Cmp
// ===----------------------------------------------------------------------===

static void *bridgeDynArrayBSearch(NevercDynArrayRef Arr, const void *Key,
                                   int (*Cmp)(const void *, const void *)) {
  if (LLVM_UNLIKELY(!Arr || !Key || !Cmp))
    return nullptr;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(A->Count == 0))
    return nullptr;
  return std::bsearch(Key, A->Data, A->Count,
                      static_cast<size_t>(A->ElemSize), Cmp);
}

// ===----------------------------------------------------------------------===
//  Arena -- bump-pointer allocator for pass-scoped temporaries
//  Backed by LLVM BumpPtrAllocator: 4 KiB slab growth, sequential alloc,
//  zero per-object bookkeeping.  All memory freed in one shot by Destroy
//  or Reset.  Individual pointers MUST NOT be passed to bridgeFree.
// ===----------------------------------------------------------------------===

namespace {
struct ArenaImpl {
  BumpPtrAllocator Alloc;
};
} // namespace

static inline ArenaImpl *unwrapArena(NevercArenaRef A) {
  return reinterpret_cast<ArenaImpl *>(A);
}
static inline NevercArenaRef wrapArena(ArenaImpl *A) {
  return reinterpret_cast<NevercArenaRef>(A);
}

static NevercArenaRef bridgeArenaCreate() {
  auto *A = new (std::nothrow) ArenaImpl();
  return wrapArena(A);
}

static void bridgeArenaDestroy(NevercArenaRef Arena) {
  if (LLVM_UNLIKELY(!Arena))
    return;
  delete unwrapArena(Arena);
}

static void *bridgeArenaAlloc(NevercArenaRef Arena, uint64_t Size) {
  if (LLVM_UNLIKELY(!Arena || Size == 0))
    return nullptr;
  if (LLVM_UNLIKELY(exceedsSizeT(Size)))
    return nullptr;
  return unwrapArena(Arena)->Alloc.Allocate(static_cast<size_t>(Size),
                                            alignof(std::max_align_t));
}

static void *bridgeArenaAllocZeroed(NevercArenaRef Arena, uint64_t Size) {
  void *P = bridgeArenaAlloc(Arena, Size);
  if (LLVM_LIKELY(P))
    std::memset(P, 0, static_cast<size_t>(Size));
  return P;
}

static void *bridgeArenaAllocArray(NevercArenaRef Arena, uint64_t Count,
                                   uint64_t ElemSize) {
  if (LLVM_UNLIKELY(!Arena))
    return nullptr;
  size_t Total = checkedArraySize(Count, ElemSize);
  if (LLVM_UNLIKELY(Total == 0 && Count != 0 && ElemSize != 0))
    return nullptr;
  if (Total == 0)
    Total = 1;
  return unwrapArena(Arena)->Alloc.Allocate(Total, alignof(std::max_align_t));
}

static void *bridgeArenaAllocArrayZeroed(NevercArenaRef Arena, uint64_t Count,
                                         uint64_t ElemSize) {
  if (LLVM_UNLIKELY(!Arena))
    return nullptr;
  size_t Total = checkedArraySize(Count, ElemSize);
  if (LLVM_UNLIKELY(Total == 0 && Count != 0 && ElemSize != 0))
    return nullptr;
  if (Total == 0)
    Total = 1;
  void *P = unwrapArena(Arena)->Alloc.Allocate(
      Total, alignof(std::max_align_t));
  if (LLVM_LIKELY(P))
    std::memset(P, 0, Total);
  return P;
}

static char *bridgeArenaStrDup(NevercArenaRef Arena, const char *S) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(Len + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  std::memcpy(Buf, S, Len + 1);
  return Buf;
}

static char *bridgeArenaStrNDup(NevercArenaRef Arena, const char *S,
                                uint64_t MaxLen) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  // Cap MaxLen at SIZE_MAX-1 so CopyLen + 1 cannot overflow size_t when the
  // caller passes UINT64_MAX or SIZE_MAX as a "copy until NUL" sentinel.
  size_t Cap = (MaxLen > static_cast<uint64_t>(SIZE_MAX - 1))
                   ? (SIZE_MAX - 1)
                   : static_cast<size_t>(MaxLen);
  const void *NulPos = Cap ? std::memchr(S, '\0', Cap) : nullptr;
  size_t CopyLen = NulPos
                       ? static_cast<size_t>(
                             static_cast<const char *>(NulPos) - S)
                       : Cap;
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(CopyLen + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (CopyLen)
    std::memcpy(Buf, S, CopyLen);
  Buf[CopyLen] = '\0';
  return Buf;
}

static void bridgeArenaReset(NevercArenaRef Arena) {
  if (LLVM_UNLIKELY(!Arena))
    return;
  unwrapArena(Arena)->Alloc.Reset();
}

static uint64_t bridgeArenaGetBytesUsed(NevercArenaRef Arena) {
  if (LLVM_UNLIKELY(!Arena))
    return 0;
  return unwrapArena(Arena)->Alloc.getBytesAllocated();
}

// ===----------------------------------------------------------------------===
//  ValueSet -- opaque hash set of NevercValueRef pointers
//  Backed by LLVM DenseSet<void *>: open addressing, quadratic probing,
//  O(1) amortized insert/contains/remove.  Sentinel pointer values
//  ((void*)-1 and (void*)-2) are never produced by the LLVM IR wrapping
//  layer, so no special-case rejection is needed beyond NULL guards.
// ===----------------------------------------------------------------------===

using ValueSetImpl = DenseSet<void *>;

static inline ValueSetImpl *unwrapVS(NevercValueSetRef S) {
  return reinterpret_cast<ValueSetImpl *>(S);
}
static inline NevercValueSetRef wrapVS(ValueSetImpl *S) {
  return reinterpret_cast<NevercValueSetRef>(S);
}

static NevercValueSetRef bridgeValueSetCreate() {
  auto *S = new (std::nothrow) ValueSetImpl();
  return wrapVS(S);
}

static NevercValueSetRef bridgeValueSetCreateSized(unsigned InitialCapacity) {
  auto *S = new (std::nothrow) ValueSetImpl(InitialCapacity);
  return wrapVS(S);
}

static void bridgeValueSetDestroy(NevercValueSetRef Set) {
  if (LLVM_UNLIKELY(!Set))
    return;
  delete unwrapVS(Set);
}

static int bridgeValueSetInsert(NevercValueSetRef Set, NevercValueRef V) {
  if (LLVM_UNLIKELY(!Set || !V))
    return 0;
  return unwrapVS(Set)->insert(static_cast<void *>(V)).second ? 1 : 0;
}

static int bridgeValueSetContains(NevercValueSetRef Set, NevercValueRef V) {
  if (LLVM_UNLIKELY(!Set || !V))
    return 0;
  return unwrapVS(Set)->contains(static_cast<void *>(V)) ? 1 : 0;
}

static void bridgeValueSetRemove(NevercValueSetRef Set, NevercValueRef V) {
  if (LLVM_UNLIKELY(!Set || !V))
    return;
  unwrapVS(Set)->erase(static_cast<void *>(V));
}

static unsigned bridgeValueSetCount(NevercValueSetRef Set) {
  if (LLVM_UNLIKELY(!Set))
    return 0;
  return static_cast<unsigned>(unwrapVS(Set)->size());
}

static void bridgeValueSetClear(NevercValueSetRef Set) {
  if (LLVM_UNLIKELY(!Set))
    return;
  unwrapVS(Set)->clear();
}

static void bridgeValueSetForEach(NevercValueSetRef Set,
                                  int (*Fn)(NevercValueRef V, void *Ctx),
                                  void *Ctx) {
  if (LLVM_UNLIKELY(!Set || !Fn))
    return;
  for (void *V : *unwrapVS(Set)) {
    if (Fn(reinterpret_cast<NevercValueRef>(V), Ctx) != 0)
      return;
  }
}

// ===----------------------------------------------------------------------===
//  Arena string concat / printf-style formatting
//  Allocates straight into the BumpPtrAllocator -- no malloc/Free pair.
//  ArenaStrFormatV uses a stack scratch buffer first; only the final
//  size hits the arena, never a heap intermediate.
// ===----------------------------------------------------------------------===

static char *bridgeArenaStrConcat(NevercArenaRef Arena, const char *L,
                                  const char *R) {
  if (LLVM_UNLIKELY(!Arena))
    return nullptr;
  size_t LL = L ? std::strlen(L) : 0;
  size_t RL = R ? std::strlen(R) : 0;
  if (LLVM_UNLIKELY(RL > SIZE_MAX - 1 || LL > SIZE_MAX - 1 - RL))
    return nullptr;
  size_t Total = LL + RL + 1;
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(Total, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (LL)
    std::memcpy(Buf, L, LL);
  if (RL)
    std::memcpy(Buf + LL, R, RL);
  Buf[LL + RL] = '\0';
  return Buf;
}

static char *bridgeArenaStrFormatV(NevercArenaRef Arena, const char *Fmt,
                                   va_list Args) {
  if (LLVM_UNLIKELY(!Arena || !Fmt))
    return nullptr;
  char Stack[1024];
  va_list ArgsCopy;
  va_copy(ArgsCopy, Args);
  int Len = std::vsnprintf(Stack, sizeof(Stack), Fmt, ArgsCopy);
  va_end(ArgsCopy);
  if (LLVM_UNLIKELY(Len < 0))
    return nullptr;
  size_t Need = static_cast<size_t>(Len) + 1;
  char *Buf =
      static_cast<char *>(unwrapArena(Arena)->Alloc.Allocate(Need, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (Need <= sizeof(Stack)) {
    std::memcpy(Buf, Stack, Need);
  } else {
    va_list Args2;
    va_copy(Args2, Args);
    std::vsnprintf(Buf, Need, Fmt, Args2);
    va_end(Args2);
  }
  return Buf;
}

static char *bridgeArenaStrFormat(NevercArenaRef Arena, const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  char *Result = bridgeArenaStrFormatV(Arena, Fmt, Args);
  va_end(Args);
  return Result;
}

// ===----------------------------------------------------------------------===
//  Arena-backed string transformations -- mirrors of bridgeStrSubstring /
//  StrTrim / StrToLower / StrToUpper / StrJoin / MemDup but allocate from
//  the BumpPtrAllocator.  Saves the malloc/Free pair on plugin hot paths
//  that slice or normalize many strings.
// ===----------------------------------------------------------------------===

static char *bridgeArenaStrSubstring(NevercArenaRef Arena, const char *S,
                                     uint64_t Start, uint64_t Len) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  size_t SLen = std::strlen(S);
  if (LLVM_UNLIKELY(Start > SLen))
    return nullptr;
  size_t Avail = SLen - static_cast<size_t>(Start);
  size_t CopyLen = (Len > Avail) ? Avail : static_cast<size_t>(Len);
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(CopyLen + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (CopyLen)
    std::memcpy(Buf, S + Start, CopyLen);
  Buf[CopyLen] = '\0';
  return Buf;
}

static char *bridgeArenaStrTrim(NevercArenaRef Arena, const char *S) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*S)))
    ++S;
  size_t Len = std::strlen(S);
  while (Len > 0 && bridgeIsWhitespace(static_cast<unsigned char>(S[Len - 1])))
    --Len;
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(Len + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (Len)
    std::memcpy(Buf, S, Len);
  Buf[Len] = '\0';
  return Buf;
}

// Branchless ASCII upper-case fold paired with the existing asciiToLower
// (defined earlier in this file).  Multiplying by 0x20 turns the
// "is in [a-z]" mask into the bit to clear for lower->upper.  No
// std::toupper call so locale and CRT boundaries do not matter.
static inline unsigned char asciiToUpper(unsigned char C) {
  unsigned Diff = static_cast<unsigned>(C) - 'a';
  unsigned Mask = (Diff < 26U) ? 0x20U : 0U;
  return static_cast<unsigned char>(C & ~Mask);
}

static char *bridgeArenaStrToLower(NevercArenaRef Arena, const char *S) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(Len + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  for (size_t I = 0; I < Len; ++I)
    Buf[I] = static_cast<char>(
        asciiToLower(static_cast<unsigned char>(S[I])));
  Buf[Len] = '\0';
  return Buf;
}

static char *bridgeArenaStrToUpper(NevercArenaRef Arena, const char *S) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(Len + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  for (size_t I = 0; I < Len; ++I)
    Buf[I] = static_cast<char>(
        asciiToUpper(static_cast<unsigned char>(S[I])));
  Buf[Len] = '\0';
  return Buf;
}

static char *bridgeArenaStrJoin(NevercArenaRef Arena,
                                const char *const *Strings, unsigned Count,
                                const char *Sep) {
  if (LLVM_UNLIKELY(!Arena))
    return nullptr;
  if (Count == 0) {
    char *Empty = static_cast<char *>(unwrapArena(Arena)->Alloc.Allocate(1, 1));
    if (LLVM_LIKELY(Empty))
      *Empty = '\0';
    return Empty;
  }
  if (LLVM_UNLIKELY(!Strings))
    return nullptr;

  size_t SepLen = (Sep && *Sep) ? std::strlen(Sep) : 0;

  // Stack-cache the lengths so the second pass copies without re-walking.
  // Spills to the arena (not the host heap) when Count exceeds the cache.
  size_t StackLens[64];
  size_t *Lens = StackLens;
  if (LLVM_UNLIKELY(Count > sizeof(StackLens) / sizeof(StackLens[0]))) {
    if (LLVM_UNLIKELY(static_cast<uint64_t>(Count) >
                      SIZE_MAX / sizeof(size_t)))
      return nullptr;
    Lens = static_cast<size_t *>(unwrapArena(Arena)->Alloc.Allocate(
        static_cast<size_t>(Count) * sizeof(size_t), alignof(size_t)));
    if (LLVM_UNLIKELY(!Lens))
      return nullptr;
  }

  size_t Total = 0;
  for (unsigned I = 0; I < Count; ++I) {
    Lens[I] = Strings[I] ? std::strlen(Strings[I]) : 0;
    if (LLVM_UNLIKELY(Lens[I] > SIZE_MAX - 1 ||
                      Total > SIZE_MAX - 1 - Lens[I]))
      return nullptr;
    Total += Lens[I];
    if (I > 0 && SepLen) {
      if (LLVM_UNLIKELY(SepLen > SIZE_MAX - 1 ||
                        Total > SIZE_MAX - 1 - SepLen))
        return nullptr;
      Total += SepLen;
    }
  }

  char *Buf =
      static_cast<char *>(unwrapArena(Arena)->Alloc.Allocate(Total + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;

  char *Dst = Buf;
  for (unsigned I = 0; I < Count; ++I) {
    if (I > 0 && SepLen) {
      std::memcpy(Dst, Sep, SepLen);
      Dst += SepLen;
    }
    if (Lens[I]) {
      std::memcpy(Dst, Strings[I], Lens[I]);
      Dst += Lens[I];
    }
  }
  *Dst = '\0';
  return Buf;
}

static void *bridgeArenaMemDup(NevercArenaRef Arena, const void *Src,
                               uint64_t Len) {
  if (LLVM_UNLIKELY(!Arena || !Src))
    return nullptr;
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return nullptr;
  if (Len == 0) {
    // Return a non-null 1-byte placeholder so callers can distinguish
    // "empty input" from "allocation failure".
    return unwrapArena(Arena)->Alloc.Allocate(1, 1);
  }
  size_t N = static_cast<size_t>(Len);
  void *Buf = unwrapArena(Arena)->Alloc.Allocate(N, alignof(std::max_align_t));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  std::memcpy(Buf, Src, N);
  return Buf;
}

// MonotonicNanos -- exposed via the vtable so plugins can time intervals
// without depending on <chrono> / <time.h> (which would break their
// zero-CRT contract).  std::chrono::steady_clock is guaranteed monotonic
// per [time.clock.steady]/p1.  We reduce the duration to nanoseconds via
// std::chrono::duration_cast which the optimizer compiles down to a single
// multiply on every platform we target.
static uint64_t bridgeMonotonicNanos(void) {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())
          .count());
}

// Module endianness -- thin wrapper over llvm::DataLayout::isLittleEndian.
// Returns 1 for LE / 0 for BE.  Defaults to LE (1) when the module pointer
// is null so passes can use the result unconditionally.
static int bridgeModuleIsLittleEndian(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return 1;
  return unwrap(M)->getDataLayout().isLittleEndian() ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  Zero-allocation callback iteration over IR structures
//  One vtable call replaces N GetNext vtable calls.  The callback runs
//  entirely inside the host process, so per-element overhead is a single
//  indirect call (function pointer) rather than two (vtable lookup +
//  function pointer).  Early exit when the callback returns non-zero.
// ===----------------------------------------------------------------------===

static void bridgeModuleForEachFunction(
    NevercModuleRef M, int (*Fn)(NevercValueRef F, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!M || !Fn))
    return;
  for (auto &F : *unwrap(M))
    if (Fn(wrapV(&F), Ctx) != 0)
      return;
}

static void bridgeModuleForEachDefinedFunction(
    NevercModuleRef M, int (*Fn)(NevercValueRef F, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!M || !Fn))
    return;
  for (auto &F : *unwrap(M))
    if (!F.isDeclaration())
      if (Fn(wrapV(&F), Ctx) != 0)
        return;
}

static void bridgeFunctionForEachBB(
    NevercValueRef F, int (*Fn)(NevercBasicBlockRef BB, void *Ctx),
    void *Ctx) {
  if (LLVM_UNLIKELY(!F || !Fn))
    return;
  auto *Func = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Func || Func->isDeclaration()))
    return;
  for (auto &BB : *Func)
    if (Fn(wrapBB(&BB), Ctx) != 0)
      return;
}

static void bridgeBBForEachInst(
    NevercBasicBlockRef BB, int (*Fn)(NevercValueRef I, void *Ctx),
    void *Ctx) {
  if (LLVM_UNLIKELY(!BB || !Fn))
    return;
  for (auto &I : *unwrapBB(BB))
    if (Fn(wrapV(&I), Ctx) != 0)
      return;
}

static void bridgeModuleForEachGlobal(
    NevercModuleRef M, int (*Fn)(NevercValueRef G, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!M || !Fn))
    return;
  for (auto &G : unwrap(M)->globals())
    if (Fn(wrapV(&G), Ctx) != 0)
      return;
}

static void bridgeModuleForEachInstruction(
    NevercModuleRef M, int (*Fn)(NevercValueRef I, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!M || !Fn))
    return;
  for (auto &F : *unwrap(M)) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      for (auto &I : BB)
        if (Fn(wrapV(&I), Ctx) != 0)
          return;
  }
}

// ===----------------------------------------------------------------------===
//  MIR callback iteration
//  Same pattern as the IR ForEach family.  One vtable call replaces N
//  MFuncGetNextBB / MBBGetNextInst vtable calls.
// ===----------------------------------------------------------------------===

static void bridgeMFuncForEachBB(
    NevercMachineFuncRef MF,
    int (*Fn)(NevercMachineBBRef MBB, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!MF || !Fn))
    return;
  for (auto &MBB : *unwrapMF(MF))
    if (Fn(wrapMBB(&MBB), Ctx) != 0)
      return;
}

static void bridgeMBBForEachInst(
    NevercMachineBBRef MBB,
    int (*Fn)(NevercMachineInstrRef MI, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!MBB || !Fn))
    return;
  for (auto &MI : *unwrapMBB(MBB))
    if (Fn(wrapMI(&MI), Ctx) != 0)
      return;
}

// ===----------------------------------------------------------------------===
//  Alias / Use / per-function-instruction callback iteration
// ===----------------------------------------------------------------------===

static void bridgeModuleForEachAlias(
    NevercModuleRef M, int (*Fn)(NevercValueRef A, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!M || !Fn))
    return;
  for (auto &A : unwrap(M)->aliases())
    if (Fn(wrapV(&A), Ctx) != 0)
      return;
}

static void bridgeValueForEachUse(
    NevercValueRef V, int (*Fn)(NevercUseRef U, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!V || !Fn))
    return;
  for (auto &U : unwrapV(V)->uses())
    if (Fn(reinterpret_cast<NevercUseRef>(&U), Ctx) != 0)
      return;
}

static void bridgeFunctionForEachInst(
    NevercValueRef F, int (*Fn)(NevercValueRef I, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!F || !Fn))
    return;
  auto *Func = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Func || Func->isDeclaration()))
    return;
  for (auto &BB : *Func)
    for (auto &I : BB)
      if (Fn(wrapV(&I), Ctx) != 0)
        return;
}

// ===----------------------------------------------------------------------===
//  Typed plugin argument helpers -- centralized parsing / range checking.
//  All three helpers return Default when the key is absent or the value
//  fails to parse, so callers can write a single-line lookup.
// ===----------------------------------------------------------------------===

static int bridgePluginGetArgBool(const char *Key, int Default) {
  const char *V = bridgePluginGetArg(Key);
  if (LLVM_UNLIKELY(!V))
    return Default;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*V)))
    ++V;
  if (*V == '\0')
    return Default;

  const char *End = V;
  while (*End && !bridgeIsWhitespace(static_cast<unsigned char>(*End)))
    ++End;
  for (const char *T = End; *T; ++T)
    if (LLVM_UNLIKELY(!bridgeIsWhitespace(static_cast<unsigned char>(*T))))
      return Default;

  size_t Len = static_cast<size_t>(End - V);
  if (Len == 1) {
    char C = V[0];
    if (C == '1' || C == 'y' || C == 'Y' || C == 't' || C == 'T')
      return 1;
    if (C == '0' || C == 'n' || C == 'N' || C == 'f' || C == 'F')
      return 0;
    return Default;
  }

  char Buf[8];
  if (LLVM_UNLIKELY(Len >= sizeof(Buf)))
    return Default;
  std::memcpy(Buf, V, Len);
  Buf[Len] = '\0';

  static const char *const TrueWords[] = {"true", "yes", "on"};
  static const char *const FalseWords[] = {"false", "no", "off"};
  for (const char *W : TrueWords)
    if (bridgeStrIEqual(Buf, W))
      return 1;
  for (const char *W : FalseWords)
    if (bridgeStrIEqual(Buf, W))
      return 0;
  return Default;
}

static int64_t bridgePluginGetArgInt64(const char *Key, int64_t Default) {
  const char *V = bridgePluginGetArg(Key);
  if (LLVM_UNLIKELY(!V))
    return Default;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*V)))
    ++V;
  char *End = nullptr;
  errno = 0;
  long long Val = std::strtoll(V, &End, 10);
  if (LLVM_UNLIKELY(errno != 0 || End == V))
    return Default;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*End)))
    ++End;
  if (LLVM_UNLIKELY(*End != '\0'))
    return Default;
  return static_cast<int64_t>(Val);
}

static uint64_t bridgePluginGetArgUInt64(const char *Key, uint64_t Default) {
  const char *V = bridgePluginGetArg(Key);
  if (LLVM_UNLIKELY(!V))
    return Default;
  const char *P = V;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*P)))
    ++P;
  if (LLVM_UNLIKELY(*P == '-'))
    return Default;
  char *End = nullptr;
  errno = 0;
  unsigned long long Val = std::strtoull(P, &End, 10);
  if (LLVM_UNLIKELY(errno != 0 || End == P))
    return Default;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*End)))
    ++End;
  if (LLVM_UNLIKELY(*End != '\0'))
    return Default;
  return static_cast<uint64_t>(Val);
}

// ===----------------------------------------------------------------------===
//  Arena-backed batch collection -- mirror the host-heap variants but
//  allocate the result array straight from the BumpPtrAllocator.  The
//  iteration logic is identical so the count + fill cost matches the
//  existing ModuleCollect* path.  Eliminates the per-call mi_malloc/
//  mi_free pair on the plugin's hot path.
// ===----------------------------------------------------------------------===

static NevercValueRef *
bridgeArenaCollectFunctions(NevercArenaRef Arena, NevercModuleRef M,
                            unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  size_t Count = Mod->size();
  if (LLVM_UNLIKELY(Count == 0 || Count > UINT_MAX ||
                    Count > SIZE_MAX / sizeof(NevercValueRef)))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(unwrapArena(Arena)->Alloc.Allocate(
      Count * sizeof(NevercValueRef), alignof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &F : *Mod)
    Buf[Idx++] = wrapV(&F);
  *OutCount = Idx;
  return Buf;
}

static NevercValueRef *
bridgeArenaCollectDefinedFunctions(NevercArenaRef Arena, NevercModuleRef M,
                                   unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  // Arena allocation is freed in bulk, so over-allocating by the declaration
  // count is harmless.  Use Mod->size() (O(1) on modern LLVM) as the upper
  // bound and avoid a separate counting pass.
  size_t MaxCount = Mod->size();
  if (LLVM_UNLIKELY(MaxCount == 0 || MaxCount > UINT_MAX ||
                    MaxCount > SIZE_MAX / sizeof(NevercValueRef)))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(unwrapArena(Arena)->Alloc.Allocate(
      MaxCount * sizeof(NevercValueRef), alignof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &F : *Mod)
    if (!F.isDeclaration())
      Buf[Idx++] = wrapV(&F);
  if (LLVM_UNLIKELY(Idx == 0))
    return nullptr;
  *OutCount = Idx;
  return Buf;
}

static NevercValueRef *
bridgeArenaCollectInstructions(NevercArenaRef Arena, NevercModuleRef M,
                               unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  size_t Total = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      Total += BB.size();
  }
  if (LLVM_UNLIKELY(Total == 0 || Total > UINT_MAX ||
                    Total > SIZE_MAX / sizeof(NevercValueRef)))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(unwrapArena(Arena)->Alloc.Allocate(
      Total * sizeof(NevercValueRef), alignof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      for (auto &I : BB)
        Buf[Idx++] = wrapV(&I);
  }
  *OutCount = Idx;
  return Buf;
}

static unsigned *bridgeArenaCollectAllOpcodes(NevercArenaRef Arena,
                                              NevercModuleRef M,
                                              unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  size_t Total = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      Total += BB.size();
  }
  if (LLVM_UNLIKELY(Total == 0 || Total > UINT_MAX ||
                    Total > SIZE_MAX / sizeof(unsigned)))
    return nullptr;
  auto *Buf = static_cast<unsigned *>(unwrapArena(Arena)->Alloc.Allocate(
      Total * sizeof(unsigned), alignof(unsigned)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  size_t Idx = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      for (auto &Inst : BB)
        Buf[Idx++] = Inst.getOpcode();
  }
  *OutCount = static_cast<unsigned>(Idx);
  return Buf;
}

// ===----------------------------------------------------------------------===
//  Zero-allocation defined-function census
//  O(N) single scan, zero allocation.  Eliminates the
//  "ModuleCollectDefinedFunctions + Free" pattern when only the count
//  is needed.  (ModuleGetFunctionCount / ModuleGetGlobalCount are
//  implemented earlier in this file.)
// ===----------------------------------------------------------------------===

static unsigned bridgeModuleGetDefinedFunctionCount(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return 0;
  unsigned Count = 0;
  for (auto &F : *unwrap(M))
    if (!F.isDeclaration())
      ++Count;
  return Count;
}

// ===----------------------------------------------------------------------===
//  Arena-backed callee collection
//  Mirrors CallGraphCollectCallees but allocates from the BumpPtrAllocator.
//  Single SmallPtrSet pass for dedup + arena fill -- eliminates the per-
//  iteration mi_malloc/mi_free pair in call-graph-walking plugin loops.
// ===----------------------------------------------------------------------===

static NevercValueRef *
bridgeArenaCollectCallees(NevercArenaRef Arena, NevercCallGraphRef CG,
                          NevercValueRef F, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !CG || !F || !OutCount))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return nullptr;
  auto *State = reinterpret_cast<CallGraphState *>(CG);
  CallGraphNode *Node = State->CG[Fn];
  if (LLVM_UNLIKELY(!Node))
    return nullptr;
  SmallPtrSet<Function *, 16> Seen;
  for (const auto &CR : *Node) {
    if (Function *Callee = CR.second->getFunction())
      Seen.insert(Callee);
  }
  if (Seen.empty())
    return nullptr;
  size_t SeenCount = Seen.size();
  if (LLVM_UNLIKELY(SeenCount > SIZE_MAX / sizeof(NevercValueRef)))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(unwrapArena(Arena)->Alloc.Allocate(
      SeenCount * sizeof(NevercValueRef), alignof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (Function *Callee : Seen)
    Buf[Idx++] = wrapV(Callee);
  *OutCount = Idx;
  return Buf;
}

// ===----------------------------------------------------------------------===
//  Arena-backed BB / MBB collection
//  Single vtable call replaces the GetCount + AllocArray + Fill pattern.
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef *
bridgeArenaCollectBBs(NevercArenaRef Arena, NevercValueRef F,
                      unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !F || !OutCount))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  size_t RawCount = Fn->size();
  if (LLVM_UNLIKELY(RawCount == 0 || RawCount > UINT_MAX ||
                    RawCount > SIZE_MAX / sizeof(NevercBasicBlockRef)))
    return nullptr;
  auto *Buf = static_cast<NevercBasicBlockRef *>(
      unwrapArena(Arena)->Alloc.Allocate(
          RawCount * sizeof(NevercBasicBlockRef),
          alignof(NevercBasicBlockRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &BB : *Fn)
    Buf[Idx++] = wrapBB(&BB);
  *OutCount = Idx;
  return Buf;
}

static NevercMachineBBRef *
bridgeArenaCollectMBBs(NevercArenaRef Arena, NevercMachineFuncRef MF,
                       unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !MF || !OutCount))
    return nullptr;
  size_t RawCount = unwrapMF(MF)->size();
  if (LLVM_UNLIKELY(RawCount == 0 || RawCount > UINT_MAX ||
                    RawCount > SIZE_MAX / sizeof(NevercMachineBBRef)))
    return nullptr;
  auto *Buf = static_cast<NevercMachineBBRef *>(
      unwrapArena(Arena)->Alloc.Allocate(
          RawCount * sizeof(NevercMachineBBRef),
          alignof(NevercMachineBBRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &MBB : *unwrapMF(MF))
    Buf[Idx++] = wrapMBB(&MBB);
  *OutCount = Idx;
  return Buf;
}

// ===----------------------------------------------------------------------===
//  Build the vtable
// ===----------------------------------------------------------------------===

NevercHostAPI buildHostAPI() {
  NevercHostAPI API = {};
  API.Version = NEVERC_PLUGIN_API_VERSION;
  API.StructSize = sizeof(NevercHostAPI);

  API.Alloc = bridgeAlloc;
  API.Realloc = bridgeRealloc;
  API.Free = bridgeFree;

  API.DiagNote = bridgeDiagNote;
  API.DiagWarning = bridgeDiagWarning;
  API.DiagError = bridgeDiagError;

  API.ModuleGetContext = bridgeModuleGetContext;

  API.ModuleGetFirstFunction = bridgeModuleGetFirstFunction;
  API.ModuleGetLastFunction = bridgeModuleGetLastFunction;
  API.ModuleGetNextFunction = bridgeModuleGetNextFunction;
  API.ModuleGetNamedFunction = bridgeModuleGetNamedFunction;
  API.ModuleGetFirstGlobal = bridgeModuleGetFirstGlobal;
  API.ModuleGetNextGlobal = bridgeModuleGetNextGlobal;
  API.ModuleAddFunction = bridgeModuleAddFunction;

  API.ValueGetName = bridgeValueGetName;
  API.ValueGetType = bridgeValueGetType;
  API.ValueReplaceAllUsesWith = bridgeValueReplaceAllUsesWith;
  API.ValueEraseFromParent = bridgeValueEraseFromParent;

  API.FunctionGetFirstBB = bridgeFunctionGetFirstBB;
  API.FunctionGetLastBB = bridgeFunctionGetLastBB;
  API.FunctionGetNextBB = bridgeFunctionGetNextBB;
  API.FunctionGetArgCount = bridgeFunctionGetArgCount;
  API.FunctionGetArg = bridgeFunctionGetArg;
  API.FunctionIsDeclaration = bridgeFunctionIsDeclaration;

  API.BBGetFirstInst = bridgeBBGetFirstInst;
  API.BBGetLastInst = bridgeBBGetLastInst;
  API.BBGetNextInst = bridgeBBGetNextInst;
  API.BBGetTerminator = bridgeBBGetTerminator;
  API.BBGetParentFunction = bridgeBBGetParentFunction;
  API.BBCreate = bridgeBBCreate;

  API.InstGetOpcode = bridgeInstGetOpcode;
  API.InstGetNumOperands = bridgeInstGetNumOperands;
  API.InstGetOperand = bridgeInstGetOperand;
  API.InstSetOperand = bridgeInstSetOperand;
  API.InstEraseFromParent = bridgeInstEraseFromParent;
  API.InstClone = bridgeInstClone;
  API.InstGetParentBB = bridgeInstGetParentBB;

  API.TypeGetInt1 = bridgeTypeGetInt1;
  API.TypeGetInt8 = bridgeTypeGetInt8;
  API.TypeGetInt16 = bridgeTypeGetInt16;
  API.TypeGetInt32 = bridgeTypeGetInt32;
  API.TypeGetInt64 = bridgeTypeGetInt64;
  API.TypeGetIntN = bridgeTypeGetIntN;
  API.TypeGetFloat = bridgeTypeGetFloat;
  API.TypeGetDouble = bridgeTypeGetDouble;
  API.TypeGetVoid = bridgeTypeGetVoid;
  API.TypeGetPtr = bridgeTypeGetPtr;
  API.TypeGetArray = bridgeTypeGetArray;
  API.TypeGetFunction = bridgeTypeGetFunction;
  API.TypeIsInteger = bridgeTypeIsInteger;
  API.TypeIsFloat = bridgeTypeIsFloat;
  API.TypeIsPointer = bridgeTypeIsPointer;
  API.TypeIsVoid = bridgeTypeIsVoid;
  API.TypeGetIntWidth = bridgeTypeGetIntWidth;

  API.ConstInt = bridgeConstInt;
  API.ConstFloat = bridgeConstFloat;
  API.ConstNull = bridgeConstNull;
  API.ConstUndef = bridgeConstUndef;
  API.ConstString = bridgeConstString;

  API.BuilderCreate = bridgeBuilderCreate;
  API.BuilderDispose = bridgeBuilderDispose;
  API.BuilderSetInsertPoint = bridgeBuilderSetInsertPoint;
  API.BuilderSetInsertPointBefore = bridgeBuilderSetInsertPointBefore;

  API.BuildAdd = bridgeBuildAdd;
  API.BuildSub = bridgeBuildSub;
  API.BuildMul = bridgeBuildMul;
  API.BuildUDiv = bridgeBuildUDiv;
  API.BuildSDiv = bridgeBuildSDiv;
  API.BuildAnd = bridgeBuildAnd;
  API.BuildOr = bridgeBuildOr;
  API.BuildXor = bridgeBuildXor;
  API.BuildShl = bridgeBuildShl;
  API.BuildLShr = bridgeBuildLShr;
  API.BuildAShr = bridgeBuildAShr;

  API.BuildICmp = bridgeBuildICmp;
  API.BuildFCmp = bridgeBuildFCmp;

  API.BuildBr = bridgeBuildBr;
  API.BuildCondBr = bridgeBuildCondBr;
  API.BuildRet = bridgeBuildRet;
  API.BuildRetVoid = bridgeBuildRetVoid;

  API.BuildCall = bridgeBuildCall;
  API.BuildGEP = bridgeBuildGEP;
  API.BuildLoad = bridgeBuildLoad;
  API.BuildStore = bridgeBuildStore;
  API.BuildAlloca = bridgeBuildAlloca;

  API.BuildBitCast = bridgeBuildBitCast;
  API.BuildIntToPtr = bridgeBuildIntToPtr;
  API.BuildPtrToInt = bridgeBuildPtrToInt;
  API.BuildZExt = bridgeBuildZExt;
  API.BuildSExt = bridgeBuildSExt;
  API.BuildTrunc = bridgeBuildTrunc;

  API.BuildSelect = bridgeBuildSelect;
  API.BuildPhi = bridgeBuildPhi;
  API.PhiAddIncoming = bridgePhiAddIncoming;

  API.MDStringCreate = bridgeMDStringCreate;
  API.MDNodeCreate = bridgeMDNodeCreate;
  API.InstSetMetadata = bridgeInstSetMetadata;
  API.InstGetMetadata = bridgeInstGetMetadata;
  API.MDKindGetID = bridgeMDKindGetID;

  API.MFuncGetFirstBB = bridgeMFuncGetFirstBB;
  API.MFuncGetNextBB = bridgeMFuncGetNextBB;
  API.MBBGetFirstInst = bridgeMBBGetFirstInst;
  API.MBBGetNextInst = bridgeMBBGetNextInst;
  API.MInstGetOpcode = bridgeMInstGetOpcode;
  API.MInstGetNumOperands = bridgeMInstGetNumOperands;
  API.MInstEraseFromParent = bridgeMInstEraseFromParent;

  API.RegisterModulePass = bridgeNoOpRegisterModulePass;
  API.RegisterMachinePass = bridgeNoOpRegisterMachinePass;
  API.RegisterBinaryPass = bridgeNoOpRegisterBinaryPass;

  API.BinaryResize = bridgeBinaryResize;

  API.ModuleGetDataLayout = bridgeModuleGetDataLayout;
  API.ModuleGetTargetTriple = bridgeModuleGetTargetTriple;
  API.ModulePrint = bridgeModulePrint;

  API.ValueDump = bridgeValueDump;
  API.ValueGetNumUses = bridgeValueGetNumUses;

  API.FunctionGetLinkage = bridgeFunctionGetLinkage;
  API.FunctionSetLinkage = bridgeFunctionSetLinkage;
  API.FunctionGetCallingConv = bridgeFunctionGetCallingConv;
  API.FunctionSetCallingConv = bridgeFunctionSetCallingConv;

  API.InstMoveBefore = bridgeInstMoveBefore;

  API.ModuleSetDataLayout = bridgeModuleSetDataLayout;
  API.ModuleSetTargetTriple = bridgeModuleSetTargetTriple;

  API.ValueGetFirstUse = bridgeValueGetFirstUse;
  API.UseGetNext = bridgeUseGetNext;
  API.UseGetUser = bridgeUseGetUser;

  API.FunctionAddStringAttr = bridgeFunctionAddStringAttr;
  API.FunctionHasStringAttr = bridgeFunctionHasStringAttr;
  API.FunctionGetStringAttr = bridgeFunctionGetStringAttr;
  API.FunctionRemoveStringAttr = bridgeFunctionRemoveStringAttr;

  API.BBRemoveFromParent = bridgeBBRemoveFromParent;
  API.BBEraseFromParent = bridgeBBEraseFromParent;

  API.ValueIsFunction = bridgeValueIsFunction;
  API.ValueIsGlobalVariable = bridgeValueIsGlobalVariable;
  API.ValueIsInstruction = bridgeValueIsInstruction;
  API.ValueIsConstant = bridgeValueIsConstant;
  API.ValueIsArgument = bridgeValueIsArgument;
  API.ValueIsBasicBlock = bridgeValueIsBasicBlock;

  API.TypeGetStruct = bridgeTypeGetStruct;
  API.TypeGetNamedStruct = bridgeTypeGetNamedStruct;
  API.StructSetBody = bridgeStructSetBody;
  API.StructGetNumElements = bridgeStructGetNumElements;
  API.StructGetElementType = bridgeStructGetElementType;
  API.TypeIsStruct = bridgeTypeIsStruct;

  API.ModuleAddGlobal = bridgeModuleAddGlobal;
  API.GlobalGetInitializer = bridgeGlobalGetInitializer;
  API.GlobalSetInitializer = bridgeGlobalSetInitializer;
  API.GlobalHasInitializer = bridgeGlobalHasInitializer;
  API.GlobalIsConstant = bridgeGlobalIsConstant;
  API.GlobalSetConstant = bridgeGlobalSetConstant;

  API.ModuleGetNamedGlobal = bridgeModuleGetNamedGlobal;

  API.InstInsertBefore = bridgeInstInsertBefore;
  API.InstInsertAfter = bridgeInstInsertAfter;

  API.MFuncGetName = bridgeMFuncGetName;
  API.MInstGetOperandImm = bridgeMInstGetOperandImm;
  API.MInstGetOperandIsReg = bridgeMInstGetOperandIsReg;
  API.MInstGetOperandReg = bridgeMInstGetOperandReg;
  API.MInstGetOperandIsImm = bridgeMInstGetOperandIsImm;

  API.MBBGetNumber = bridgeMBBGetNumber;
  API.MBBGetSuccCount = bridgeMBBGetSuccCount;
  API.MBBGetPredCount = bridgeMBBGetPredCount;

  API.ModuleRemoveFunction = bridgeModuleRemoveFunction;
  API.ModuleRemoveGlobal = bridgeModuleRemoveGlobal;

  API.ModuleGetFunctionCount = bridgeModuleGetFunctionCount;
  API.ModuleGetGlobalCount = bridgeModuleGetGlobalCount;
  API.ValueGetTypeAsFunction = bridgeValueGetTypeAsFunction;

  API.FunctionGetReturnType = bridgeFunctionGetReturnType;

  API.InstIsTerminator = bridgeInstIsTerminator;

  API.BuildNeg = bridgeBuildNeg;
  API.BuildNot = bridgeBuildNot;
  API.BuildFNeg = bridgeBuildFNeg;
  API.BuildUnreachable = bridgeBuildUnreachable;

  API.BuildSwitch = bridgeBuildSwitch;
  API.SwitchAddCase = bridgeSwitchAddCase;

  API.ConstPointerNull = bridgeConstPointerNull;

  API.BuildURem = bridgeBuildURem;
  API.BuildSRem = bridgeBuildSRem;

  API.BuildFPToSI = bridgeBuildFPToSI;
  API.BuildSIToFP = bridgeBuildSIToFP;
  API.BuildFPToUI = bridgeBuildFPToUI;
  API.BuildUIToFP = bridgeBuildUIToFP;

  API.InstIsCall = bridgeInstIsCall;
  API.InstIsBranch = bridgeInstIsBranch;
  API.InstIsLoad = bridgeInstIsLoad;
  API.InstIsStore = bridgeInstIsStore;
  API.InstIsAlloca = bridgeInstIsAlloca;
  API.InstIsPHI = bridgeInstIsPHI;
  API.InstIsGEP = bridgeInstIsGEP;
  API.InstIsCast = bridgeInstIsCast;
  API.InstIsBinaryOp = bridgeInstIsBinaryOp;
  API.InstIsUnaryOp = bridgeInstIsUnaryOp;
  API.InstIsSwitch = bridgeInstIsSwitch;
  API.InstIsReturn = bridgeInstIsReturn;
  API.InstIsSelect = bridgeInstIsSelect;

  API.CallGetCalledOperand = bridgeCallGetCalledOperand;
  API.CallGetNumArgs = bridgeCallGetNumArgs;
  API.CallGetArg = bridgeCallGetArg;
  API.CallGetFunctionType = bridgeCallGetFunctionType;

  API.FuncTypeGetParamCount = bridgeFuncTypeGetParamCount;
  API.FuncTypeGetParamType = bridgeFuncTypeGetParamType;
  API.FuncTypeGetReturnType = bridgeFuncTypeGetReturnType;
  API.FuncTypeIsVarArg = bridgeFuncTypeIsVarArg;
  API.TypeIsFunctionTy = bridgeTypeIsFunctionTy;
  API.TypeIsArrayTy = bridgeTypeIsArrayTy;
  API.TypeGetArrayLength = bridgeTypeGetArrayLength;
  API.TypeGetArrayElementType = bridgeTypeGetArrayElementType;

  API.BrIsConditional = bridgeBrIsConditional;
  API.BrGetCondition = bridgeBrGetCondition;
  API.BrGetSuccessor = bridgeBrGetSuccessor;
  API.BrGetNumSuccessors = bridgeBrGetNumSuccessors;

  API.LoadGetPointerOperand = bridgeLoadGetPointerOperand;
  API.StoreGetValueOperand = bridgeStoreGetValueOperand;
  API.StoreGetPointerOperand = bridgeStoreGetPointerOperand;

  API.GEPGetPointerOperand = bridgeGEPGetPointerOperand;
  API.GEPGetNumIndices = bridgeGEPGetNumIndices;

  API.BBGetPredCount = bridgeBBGetPredCount;
  API.BBGetSuccCount = bridgeBBGetSuccCount;

  API.BuildFAdd = bridgeBuildFAdd;
  API.BuildFSub = bridgeBuildFSub;
  API.BuildFMul = bridgeBuildFMul;
  API.BuildFDiv = bridgeBuildFDiv;
  API.BuildFRem = bridgeBuildFRem;

  API.BuildExtractValue = bridgeBuildExtractValue;
  API.BuildInsertValue = bridgeBuildInsertValue;

  API.ConstArray = bridgeConstArray;
  API.ConstStruct = bridgeConstStruct;
  API.ConstNamedStruct = bridgeConstNamedStruct;

  API.BuildGlobalStringPtr = bridgeBuildGlobalStringPtr;

  API.ValueSetName = bridgeValueSetName;

  API.GlobalGetLinkage = bridgeGlobalGetLinkage;
  API.GlobalSetLinkage = bridgeGlobalSetLinkage;

  API.BuildInBoundsGEP = bridgeBuildInBoundsGEP;

  API.ModuleVerify = bridgeModuleVerify;

  API.InstGetNumSuccessors = bridgeInstGetNumSuccessors;
  API.InstGetSuccessor = bridgeInstGetSuccessor;

  API.AllocaGetAllocatedType = bridgeAllocaGetAllocatedType;

  API.GlobalGetAlignment = bridgeGlobalGetAlignment;
  API.GlobalSetAlignment = bridgeGlobalSetAlignment;

  API.BBGetPredecessor = bridgeBBGetPredecessor;
  API.BBGetSuccessor = bridgeBBGetSuccessorBB;

  API.ValueIsNull = bridgeValueIsNull;
  API.ValueIsSameAs = bridgeValueIsSameAs;

  API.PhiGetNumIncoming = bridgePhiGetNumIncoming;
  API.PhiGetIncomingValue = bridgePhiGetIncomingValue;
  API.PhiGetIncomingBlock = bridgePhiGetIncomingBlock;

  API.SelectGetCondition = bridgeSelectGetCondition;
  API.SelectGetTrueValue = bridgeSelectGetTrueValue;
  API.SelectGetFalseValue = bridgeSelectGetFalseValue;

  API.ReturnGetValue = bridgeReturnGetValue;

  API.CastGetSrcTy = bridgeCastGetSrcTy;
  API.CastGetDestTy = bridgeCastGetDestTy;

  API.GEPGetSourceElementType = bridgeGEPGetSourceElementType;

  API.CmpGetPredicate = bridgeCmpGetPredicate;

  API.InstIsICmp = bridgeInstIsICmp;
  API.InstIsFCmp = bridgeInstIsFCmp;

  API.BBGetName = bridgeBBGetName;
  API.FunctionGetBBCount = bridgeFunctionGetBBCount;

  API.InstGetPrevInst = bridgeInstGetPrevInst;
  API.FunctionGetPrevBB = bridgeFunctionGetPrevBB;

  API.TypeGetStructName = bridgeTypeGetStructName;

  API.SwitchGetNumCases = bridgeSwitchGetNumCases;
  API.SwitchGetCaseValue = bridgeSwitchGetCaseValue;
  API.SwitchGetCaseSuccessor = bridgeSwitchGetCaseSuccessor;
  API.SwitchGetDefaultDest = bridgeSwitchGetDefaultDest;

  API.MBBGetSuccessor = bridgeMBBGetSuccessor;
  API.MBBGetPredecessor = bridgeMBBGetPredecessor;

  API.ModuleGetSourceFileName = bridgeModuleGetSourceFileName;

  API.InstIsInvoke = bridgeInstIsInvoke;
  API.InvokeGetNormalDest = bridgeInvokeGetNormalDest;
  API.InvokeGetUnwindDest = bridgeInvokeGetUnwindDest;

  API.BBSplitBefore = bridgeBBSplitBefore;
  API.BuilderGetInsertBlock = bridgeBuilderGetInsertBlock;

  API.ConstIntGetZExtValue = bridgeConstIntGetZExtValue;
  API.ConstIntGetSExtValue = bridgeConstIntGetSExtValue;
  API.ValueIsConstantInt = bridgeValueIsConstantInt;

  API.GlobalGetValueType = bridgeGlobalGetValueType;
  API.FunctionDeleteBody = bridgeFunctionDeleteBody;
  API.ValueIsUndef = bridgeValueIsUndef;
  API.InstGetOperandAsBB = bridgeInstGetOperandAsBB;

  API.BuildFPExt = bridgeBuildFPExt;
  API.BuildFPTrunc = bridgeBuildFPTrunc;
  API.LoadGetType = bridgeLoadGetType;
  API.GEPIsInBounds = bridgeGEPIsInBounds;
  API.InstIsCallLike = bridgeInstIsCallLike;

  API.LoadGetAlignment = bridgeLoadGetAlignment;
  API.StoreGetAlignment = bridgeStoreGetAlignment;
  API.LoadIsVolatile = bridgeLoadIsVolatile;
  API.StoreIsVolatile = bridgeStoreIsVolatile;
  API.BuilderSetInsertPointBeforeTerminator =
      bridgeBuilderSetInsertPointBeforeTerminator;

  API.InstIsIntrinsic = bridgeInstIsIntrinsic;
  API.CallGetIntrinsicID = bridgeCallGetIntrinsicID;

  API.BuildAlignedLoad = bridgeBuildAlignedLoad;
  API.BuildAlignedStore = bridgeBuildAlignedStore;

  API.InstCopyDebugLoc = bridgeInstCopyDebugLoc;
  API.InstHasDebugLoc = bridgeInstHasDebugLoc;
  API.InstGetDebugLocLine = bridgeInstGetDebugLocLine;
  API.InstGetDebugLocCol = bridgeInstGetDebugLocCol;

  API.GlobalGetSection = bridgeGlobalGetSection;
  API.GlobalSetSection = bridgeGlobalSetSection;

  API.FunctionAddParamAttr = bridgeFunctionAddParamAttr;
  API.FunctionHasParamAttr = bridgeFunctionHasParamAttr;

  API.BBGetInstCount = bridgeBBGetInstCount;

  API.ModuleGetFirstAlias = bridgeModuleGetFirstAlias;
  API.ModuleGetNextAlias = bridgeModuleGetNextAlias;

  API.GEPGetIndex = bridgeGEPGetIndex;

  API.ModuleGetLastGlobal = bridgeModuleGetLastGlobal;
  API.ModuleGetPrevGlobal = bridgeModuleGetPrevGlobal;

  API.ValuePrintToString = bridgeValuePrintToString;
  API.TypePrintToString = bridgeTypePrintToString;

  API.ValueIsConstantExpr = bridgeValueIsConstantExpr;
  API.ValueIsAlias = bridgeValueIsAlias;

  API.HostIsShellcodeMode = bridgeHostIsShellcodeMode;
  API.HostGetShellcodeEntrySymbol = bridgeHostGetShellcodeEntrySymbol;

  API.IntrinsicGetDeclaration = bridgeIntrinsicGetDeclaration;
  API.IntrinsicGetName = bridgeIntrinsicGetName;
  API.IntrinsicIsOverloaded = bridgeIntrinsicIsOverloaded;
  API.IntrinsicLookupByName = bridgeIntrinsicLookupByName;

  API.ModuleGetNamedMetadata = bridgeModuleGetNamedMetadata;
  API.ModuleGetOrInsertNamedMetadata = bridgeModuleGetOrInsertNamedMetadata;
  API.NamedMDGetNumOperands = bridgeNamedMDGetNumOperands;
  API.NamedMDGetOperand = bridgeNamedMDGetOperand;
  API.NamedMDAddOperand = bridgeNamedMDAddOperand;

  API.MetadataAsValue = bridgeMetadataAsValue;
  API.ValueAsMetadata = bridgeValueAsMetadata;

  API.PluginGetArg = bridgePluginGetArg;
  API.PluginHasArg = bridgePluginHasArg;
  API.PluginGetArgCount = bridgePluginGetArgCount;

  API.BBMoveAfter = bridgeBBMoveAfter;
  API.BBMoveBefore = bridgeBBMoveBefore;

  API.BuildMemCpy = bridgeBuildMemCpy;
  API.BuildMemSet = bridgeBuildMemSet;
  API.BuildMemMove = bridgeBuildMemMove;

  API.TypeSizeInBits = bridgeTypeSizeInBits;
  API.TypeAllocSize = bridgeTypeAllocSize;
  API.TypeStoreSize = bridgeTypeStoreSize;
  API.TypeABIAlignment = bridgeTypeABIAlignment;
  API.TypePrefAlignment = bridgeTypePrefAlignment;
  API.PointerSizeInBits = bridgePointerSizeInBits;

  API.ConstPoison = bridgeConstPoison;
  API.ValueIsPoison = bridgeValueIsPoison;

  API.TypeGetPtrInAddrSpace = bridgeTypeGetPtrInAddrSpace;
  API.TypeGetPointerAddrSpace = bridgeTypeGetPointerAddrSpace;

  API.TypeGetFixedVector = bridgeTypeGetFixedVector;
  API.TypeIsVector = bridgeTypeIsVector;
  API.TypeGetVectorNumElements = bridgeTypeGetVectorNumElements;
  API.TypeGetVectorElementType = bridgeTypeGetVectorElementType;
  API.ConstVector = bridgeConstVector;

  API.BuildExtractElement = bridgeBuildExtractElement;
  API.BuildInsertElement = bridgeBuildInsertElement;
  API.BuildShuffleVector = bridgeBuildShuffleVector;

  API.BuildFreeze = bridgeBuildFreeze;
  API.BuildAddrSpaceCast = bridgeBuildAddrSpaceCast;

  API.ConstZeroInitializer = bridgeConstZeroInitializer;

  API.GlobalHasUnnamedAddr = bridgeGlobalHasUnnamedAddr;
  API.GlobalSetUnnamedAddr = bridgeGlobalSetUnnamedAddr;

  API.ModuleGetPrevFunction = bridgeModuleGetPrevFunction;

  API.ModuleGetLastAlias = bridgeModuleGetLastAlias;
  API.ModuleGetPrevAlias = bridgeModuleGetPrevAlias;

  API.StrLen = bridgeStrLen;
  API.StrDup = bridgeStrDup;
  API.StrConcat = bridgeStrConcat;
  API.StrEqual = bridgeStrEqual;
  API.IntToStr = bridgeIntToStr;
  API.UIntToStr = bridgeUIntToStr;
  API.StrFormat = bridgeStrFormat;
  API.StrFormatV = bridgeStrFormatV;

  API.MemCopy = bridgeMemCopy;
  API.MemSet = bridgeMemSet;
  API.MemMove = bridgeMemMove;
  API.MemCompare = bridgeMemCompare;

  API.BuildNSWAdd = bridgeBuildNSWAdd;
  API.BuildNUWAdd = bridgeBuildNUWAdd;
  API.BuildNSWSub = bridgeBuildNSWSub;
  API.BuildNUWSub = bridgeBuildNUWSub;
  API.BuildNSWMul = bridgeBuildNSWMul;
  API.BuildNUWMul = bridgeBuildNUWMul;
  API.BuildExactSDiv = bridgeBuildExactSDiv;
  API.BuildExactUDiv = bridgeBuildExactUDiv;
  API.BuildNSWNeg = bridgeBuildNSWNeg;

  API.BuildInvoke = bridgeBuildInvoke;
  API.BuildLandingPad = bridgeBuildLandingPad;
  API.LandingPadAddClause = bridgeLandingPadAddClause;
  API.LandingPadSetCleanup = bridgeLandingPadSetCleanup;
  API.BuildResume = bridgeBuildResume;

  API.GlobalIsThreadLocal = bridgeGlobalIsThreadLocal;
  API.GlobalSetThreadLocal = bridgeGlobalSetThreadLocal;

  API.FunctionAddEnumAttr = bridgeFunctionAddEnumAttr;
  API.FunctionHasEnumAttr = bridgeFunctionHasEnumAttr;
  API.FunctionRemoveEnumAttr = bridgeFunctionRemoveEnumAttr;

  API.ModuleGetOrInsertComdat = bridgeModuleGetOrInsertComdat;
  API.ComdatGetSelectionKind = bridgeComdatGetSelectionKind;
  API.ComdatSetSelectionKind = bridgeComdatSetSelectionKind;
  API.GlobalSetComdat = bridgeGlobalSetComdat;
  API.GlobalGetComdat = bridgeGlobalGetComdat;

  API.InstMoveAfter = bridgeInstMoveAfter;
  API.ConstGetAggregateElement = bridgeConstGetAggregateElement;

  API.MBBGetLastInst = bridgeMBBGetLastInst;
  API.MBBGetPrevInst = bridgeMBBGetPrevInst;
  API.MFuncGetLastBB = bridgeMFuncGetLastBB;
  API.MFuncGetPrevBB = bridgeMFuncGetPrevBB;

  API.MInstSetOperandReg = bridgeMInstSetOperandReg;
  API.MInstSetOperandImm = bridgeMInstSetOperandImm;

  API.MInstGetFlags = bridgeMInstGetFlags;
  API.MInstSetFlags = bridgeMInstSetFlags;
  API.MInstIsBranch = bridgeMInstIsBranch;
  API.MInstIsCall = bridgeMInstIsCall;
  API.MInstIsReturn = bridgeMInstIsReturn;
  API.MInstIsTerminator = bridgeMInstIsTerminator;
  API.MInstIsMoveImmediate = bridgeMInstIsMoveImmediate;
  API.MInstHasDelaySlot = bridgeMInstHasDelaySlot;
  API.MInstGetDesc = bridgeMInstGetDesc;

  API.MInstOperandIsVirtReg = bridgeMInstOperandIsVirtReg;
  API.MInstOperandIsPhysReg = bridgeMInstOperandIsPhysReg;
  API.MInstOperandIsDef = bridgeMInstOperandIsDef;
  API.MInstOperandIsUse = bridgeMInstOperandIsUse;

  API.MInstMoveBefore = bridgeMInstMoveBefore;
  API.MBBGetInstCount = bridgeMBBGetInstCount;
  API.MFuncGetBBCount = bridgeMFuncGetBBCount;

  API.RegisterLinkerPass = bridgeNoOpRegisterLinkerPass;

  API.LinkGetFirstSymbol = bridgeLinkGetFirstSymbol;
  API.LinkGetNextSymbol = bridgeLinkGetNextSymbol;
  API.LinkFindSymbol = bridgeLinkFindSymbol;
  API.LinkSymbolGetName = bridgeLinkSymbolGetName;
  API.LinkSymbolGetValue = bridgeLinkSymbolGetValue;
  API.LinkSymbolGetSize = bridgeLinkSymbolGetSize;
  API.LinkSymbolIsDefined = bridgeLinkSymbolIsDefined;
  API.LinkSymbolIsLocal = bridgeLinkSymbolIsLocal;
  API.LinkSymbolIsHidden = bridgeLinkSymbolIsHidden;
  API.LinkSymbolSetVisibilityHidden = bridgeLinkSymbolSetVisibilityHidden;

  API.LinkGetFirstSection = bridgeLinkGetFirstSection;
  API.LinkGetNextSection = bridgeLinkGetNextSection;
  API.LinkFindSection = bridgeLinkFindSection;
  API.LinkSectionGetName = bridgeLinkSectionGetName;
  API.LinkSectionGetSize = bridgeLinkSectionGetSize;
  API.LinkSectionGetAlignment = bridgeLinkSectionGetAlignment;
  API.LinkSectionGetFlags = bridgeLinkSectionGetFlags;

  API.LinkGetOutputPath = bridgeLinkGetOutputPath;
  API.LinkGetOutputFormat = bridgeLinkGetOutputFormat;

  API.DiagNoteF = bridgeDiagNoteF;
  API.DiagWarningF = bridgeDiagWarningF;
  API.DiagErrorF = bridgeDiagErrorF;
  API.DiagNoteV = bridgeDiagNoteV;
  API.DiagWarningV = bridgeDiagWarningV;
  API.DiagErrorV = bridgeDiagErrorV;

  API.AllocZeroed = bridgeAllocZeroed;

  API.StrStartsWith = bridgeStrStartsWith;
  API.StrEndsWith = bridgeStrEndsWith;
  API.StrContains = bridgeStrContains;
  API.StrCompare = bridgeStrCompare;
  API.StrToInt64 = bridgeStrToInt64;
  API.StrToUInt64 = bridgeStrToUInt64;
  API.StrSubstring = bridgeStrSubstring;
  API.StrTrim = bridgeStrTrim;

  API.MemZero = bridgeMemZero;
  API.StrFindChar = bridgeStrFindChar;
  API.StrFindLastChar = bridgeStrFindLastChar;
  API.ReallocArray = bridgeReallocArray;

  API.ModuleCollectFunctions = bridgeModuleCollectFunctions;
  API.ModuleCollectGlobals = bridgeModuleCollectGlobals;
  API.FunctionCollectBBs = bridgeFunctionCollectBBs;
  API.BBCollectInstructions = bridgeBBCollectInstructions;

  API.MFuncCollectBBs = bridgeMFuncCollectBBs;
  API.MBBCollectInstructions = bridgeMBBCollectInstructions;

  API.StrFindStr = bridgeStrFindStr;
  API.HookPointGetName = bridgeHookPointGetName;
  API.MemDup = bridgeMemDup;
  API.StrReplace = bridgeStrReplace;
  API.StrReplaceAll = bridgeStrReplaceAll;
  API.StrToLower = bridgeStrToLower;
  API.StrToUpper = bridgeStrToUpper;

  API.LinkGetOutputFormatName = bridgeLinkGetOutputFormatName;

  API.ModuleGetAliasCount = bridgeModuleGetAliasCount;
  API.ModuleCollectAliases = bridgeModuleCollectAliases;

  API.ModuleCollectAllFunctions = bridgeModuleCollectAllFunctions;
  API.ModuleCollectAllGlobals = bridgeModuleCollectAllGlobals;
  API.ModuleCollectAllInstructions = bridgeModuleCollectAllInstructions;

  API.StrJoin = bridgeStrJoin;
  API.StrSplit = bridgeStrSplit;
  API.StrHash = bridgeStrHash;

  API.ModuleCollectDefinedFunctions = bridgeModuleCollectDefinedFunctions;

  API.Sort = bridgeSort;
  API.BSearch = bridgeBSearch;

  API.StrFormatBuf = bridgeStrFormatBuf;
  API.StrFormatBufV = bridgeStrFormatBufV;

  API.StrNCompare = bridgeStrNCompare;
  API.StrCopyBuf = bridgeStrCopyBuf;

  API.DynArrayCreate = bridgeDynArrayCreate;
  API.DynArrayDestroy = bridgeDynArrayDestroy;
  API.DynArrayPush = bridgeDynArrayPush;
  API.DynArrayGet = bridgeDynArrayGet;
  API.DynArrayCount = bridgeDynArrayCount;
  API.DynArrayData = bridgeDynArrayData;
  API.DynArrayClear = bridgeDynArrayClear;
  API.DynArraySort = bridgeDynArraySort;
  API.DynArrayPop = bridgeDynArrayPop;
  API.DynArrayReserve = bridgeDynArrayReserve;
  API.DynArrayDetach = bridgeDynArrayDetach;

  API.StrMapCreate = bridgeStrMapCreate;
  API.StrMapDestroy = bridgeStrMapDestroy;
  API.StrMapPut = bridgeStrMapPut;
  API.StrMapGet = bridgeStrMapGet;
  API.StrMapHas = bridgeStrMapHas;
  API.StrMapRemove = bridgeStrMapRemove;
  API.StrMapCount = bridgeStrMapCount;
  API.StrMapForEach = bridgeStrMapForEach;
  API.StrMapCollectKeys = bridgeStrMapCollectKeys;
  API.StrMapIncrement = bridgeStrMapIncrement;

  API.StrBuilderCreate = bridgeStrBuilderCreate;
  API.StrBuilderDestroy = bridgeStrBuilderDestroy;
  API.StrBuilderAppend = bridgeStrBuilderAppend;
  API.StrBuilderAppendN = bridgeStrBuilderAppendN;
  API.StrBuilderAppendChar = bridgeStrBuilderAppendChar;
  API.StrBuilderAppendF = bridgeStrBuilderAppendF;
  API.StrBuilderAppendV = bridgeStrBuilderAppendV;
  API.StrBuilderFinish = bridgeStrBuilderFinish;
  API.StrBuilderLen = bridgeStrBuilderLen;
  API.StrBuilderClear = bridgeStrBuilderClear;

  API.DynArrayPushN = bridgeDynArrayPushN;
  API.DynArrayRemoveSwap = bridgeDynArrayRemoveSwap;
  API.DynArrayShrinkToFit = bridgeDynArrayShrinkToFit;

  API.StrMapCreateSized = bridgeStrMapCreateSized;

  API.IntMapCreate = bridgeIntMapCreate;
  API.IntMapDestroy = bridgeIntMapDestroy;
  API.IntMapPut = bridgeIntMapPut;
  API.IntMapGet = bridgeIntMapGet;
  API.IntMapHas = bridgeIntMapHas;
  API.IntMapRemove = bridgeIntMapRemove;
  API.IntMapCount = bridgeIntMapCount;
  API.IntMapIncrement = bridgeIntMapIncrement;
  API.IntMapForEach = bridgeIntMapForEach;
  API.IntMapClear = bridgeIntMapClear;
  API.IntMapCreateSized = bridgeIntMapCreateSized;

  API.DynArrayBSearch = bridgeDynArrayBSearch;

  API.InstGetOpcodeName = bridgeInstGetOpcodeName;
  API.InstOpcodeToName = bridgeInstOpcodeToName;

  API.StrNDup = bridgeStrNDup;
  API.StrCountChar = bridgeStrCountChar;

  API.StrMapPutN = bridgeStrMapPutN;
  API.StrMapGetN = bridgeStrMapGetN;
  API.StrMapHasN = bridgeStrMapHasN;
  API.StrMapIncrementN = bridgeStrMapIncrementN;
  API.StrMapRemoveN = bridgeStrMapRemoveN;

  API.StrAfterPrefix = bridgeStrAfterPrefix;
  API.MemFind = bridgeMemFind;
  API.MemCount = bridgeMemCount;
  API.StrSpan = bridgeStrSpan;
  API.StrCSpn = bridgeStrCSpn;
  API.StrIEqual = bridgeStrIEqual;
  API.StrICompare = bridgeStrICompare;

  API.PathBaseNameOffset = bridgePathBaseNameOffset;
  API.PathExtOffset = bridgePathExtOffset;

  API.MemReverse = bridgeMemReverse;
  API.DynArrayInsert = bridgeDynArrayInsert;
  API.DynArrayRemoveOrdered = bridgeDynArrayRemoveOrdered;

  API.MInstCollectOperandKinds = bridgeMInstCollectOperandKinds;

  API.BBCollectOpcodes = bridgeBBCollectOpcodes;
  API.ModuleCollectAllOpcodes = bridgeModuleCollectAllOpcodes;

  API.FunctionGetInstructionCount = bridgeFunctionGetInstructionCount;

  API.FunctionBuildDomTree = bridgeFunctionBuildDomTree;
  API.DomTreeDestroy = bridgeDomTreeDestroy;
  API.DomTreeDominates = bridgeDomTreeDominates;
  API.DomTreeProperlyDominates = bridgeDomTreeProperlyDominates;
  API.DomTreeGetIDom = bridgeDomTreeGetIDom;
  API.DomTreeIsReachable = bridgeDomTreeIsReachable;

  API.FunctionBuildLoopInfo = bridgeFunctionBuildLoopInfo;
  API.LoopInfoDestroy = bridgeLoopInfoDestroy;
  API.LoopInfoGetLoopFor = bridgeLoopInfoGetLoopFor;
  API.LoopInfoGetTopLevelLoopCount = bridgeLoopInfoGetTopLevelLoopCount;
  API.LoopGetHeader = bridgeLoopGetHeader;
  API.LoopGetDepth = bridgeLoopGetDepth;
  API.LoopGetParentLoop = bridgeLoopGetParentLoop;
  API.LoopContains = bridgeLoopContains;
  API.LoopGetNumBlocks = bridgeLoopGetNumBlocks;
  API.LoopGetNumSubLoops = bridgeLoopGetNumSubLoops;
  API.LoopIsInnermost = bridgeLoopIsInnermost;

  API.FunctionBuildPostDomTree = bridgeFunctionBuildPostDomTree;
  API.PostDomTreeDestroy = bridgePostDomTreeDestroy;
  API.PostDomTreeDominates = bridgePostDomTreeDominates;
  API.PostDomTreeProperlyDominates = bridgePostDomTreeProperlyDominates;
  API.PostDomTreeGetIPDom = bridgePostDomTreeGetIPDom;

  API.FunctionClone = bridgeFunctionClone;

  API.FunctionBuildSCEV = bridgeFunctionBuildSCEV;
  API.SCEVInfoDestroy = bridgeSCEVInfoDestroy;
  API.SCEVGetTripCount = bridgeSCEVGetTripCount;
  API.SCEVGetMaxTripCount = bridgeSCEVGetMaxTripCount;

  API.ModuleBuildCallGraph = bridgeModuleBuildCallGraph;
  API.CallGraphDestroy = bridgeCallGraphDestroy;
  API.CallGraphGetCalleeCount = bridgeCallGraphGetCalleeCount;
  API.CallGraphCollectCallees = bridgeCallGraphCollectCallees;
  API.CallGraphIsRecursive = bridgeCallGraphIsRecursive;

  API.SplitEdge = bridgeSplitEdge;
  API.MergeBlockIntoPredecessor = bridgeMergeBlockIntoPredecessor;

  API.LoopIsLoopInvariant = bridgeLoopIsLoopInvariant;

  API.ArenaCreate = bridgeArenaCreate;
  API.ArenaDestroy = bridgeArenaDestroy;
  API.ArenaAlloc = bridgeArenaAlloc;
  API.ArenaAllocZeroed = bridgeArenaAllocZeroed;
  API.ArenaAllocArray = bridgeArenaAllocArray;
  API.ArenaStrDup = bridgeArenaStrDup;
  API.ArenaStrNDup = bridgeArenaStrNDup;
  API.ArenaReset = bridgeArenaReset;
  API.ArenaGetBytesUsed = bridgeArenaGetBytesUsed;

  API.ValueSetCreate = bridgeValueSetCreate;
  API.ValueSetCreateSized = bridgeValueSetCreateSized;
  API.ValueSetDestroy = bridgeValueSetDestroy;
  API.ValueSetInsert = bridgeValueSetInsert;
  API.ValueSetContains = bridgeValueSetContains;
  API.ValueSetRemove = bridgeValueSetRemove;
  API.ValueSetCount = bridgeValueSetCount;
  API.ValueSetClear = bridgeValueSetClear;

  API.SortCtx = bridgeSortCtx;
  API.BSearchCtx = bridgeBSearchCtx;
  API.DynArraySortCtx = bridgeDynArraySortCtx;

  API.StrBuilderGetStr = bridgeStrBuilderGetStr;

  API.ArenaAllocArrayZeroed = bridgeArenaAllocArrayZeroed;

  API.ArenaStrConcat = bridgeArenaStrConcat;
  API.ArenaStrFormatV = bridgeArenaStrFormatV;
  API.ArenaStrFormat = bridgeArenaStrFormat;

  API.ValueSetForEach = bridgeValueSetForEach;

  API.PluginGetArgBool = bridgePluginGetArgBool;
  API.PluginGetArgInt64 = bridgePluginGetArgInt64;
  API.PluginGetArgUInt64 = bridgePluginGetArgUInt64;

  API.ArenaCollectFunctions = bridgeArenaCollectFunctions;
  API.ArenaCollectDefinedFunctions = bridgeArenaCollectDefinedFunctions;
  API.ArenaCollectInstructions = bridgeArenaCollectInstructions;
  API.ArenaCollectAllOpcodes = bridgeArenaCollectAllOpcodes;

  API.ModuleGetDefinedFunctionCount = bridgeModuleGetDefinedFunctionCount;

  API.ArenaCollectCallees = bridgeArenaCollectCallees;

  API.ArenaCollectBBs = bridgeArenaCollectBBs;
  API.ArenaCollectMBBs = bridgeArenaCollectMBBs;

  API.DomTreeGetDepth = bridgeDomTreeGetDepth;
  API.PostDomTreeGetDepth = bridgePostDomTreeGetDepth;

  API.MemFindByte = bridgeMemFindByte;

  API.ModuleGetFirstDefinedFunction = bridgeModuleGetFirstDefinedFunction;
  API.ModuleGetNextDefinedFunction = bridgeModuleGetNextDefinedFunction;

  API.StrMapClear = bridgeStrMapClear;

  API.ArenaStrSubstring = bridgeArenaStrSubstring;
  API.ArenaStrTrim = bridgeArenaStrTrim;
  API.ArenaStrToLower = bridgeArenaStrToLower;
  API.ArenaStrToUpper = bridgeArenaStrToUpper;
  API.ArenaStrJoin = bridgeArenaStrJoin;
  API.ArenaMemDup = bridgeArenaMemDup;

  API.MonotonicNanos = bridgeMonotonicNanos;
  API.ModuleIsLittleEndian = bridgeModuleIsLittleEndian;

  API.ModuleForEachFunction = bridgeModuleForEachFunction;
  API.ModuleForEachDefinedFunction = bridgeModuleForEachDefinedFunction;
  API.FunctionForEachBB = bridgeFunctionForEachBB;
  API.BBForEachInst = bridgeBBForEachInst;
  API.ModuleForEachGlobal = bridgeModuleForEachGlobal;
  API.ModuleForEachInstruction = bridgeModuleForEachInstruction;

  API.MFuncForEachBB = bridgeMFuncForEachBB;
  API.MBBForEachInst = bridgeMBBForEachInst;
  API.ModuleForEachAlias = bridgeModuleForEachAlias;
  API.ValueForEachUse = bridgeValueForEachUse;
  API.FunctionForEachInst = bridgeFunctionForEachInst;

  static_assert(
      offsetof(NevercHostAPI, FunctionForEachInst) +
              sizeof(NevercHostAPI::FunctionForEachInst) ==
          sizeof(NevercHostAPI),
      "New fields added after FunctionForEachInst. "
      "Wire them in buildHostAPI and update this static_assert.");

  return API;
}

} // namespace plugin
} // namespace neverc
