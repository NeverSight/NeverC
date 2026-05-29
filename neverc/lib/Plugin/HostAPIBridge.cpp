#include "HostAPIBridge.h"
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
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace llvm;

namespace neverc {
namespace plugin {

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
//  Memory — route through host process allocator
//  When NEVERC_ENABLE_MIMALLOC=ON the neverc binary links mimalloc with
//  MI_OVERRIDE=ON + whole-archive, so ::malloc/::realloc/::free are
//  transparently replaced by mi_malloc/mi_realloc/mi_free at the process
//  level.  Plugin allocations through these pointers thus go through the
//  host's mimalloc heap — no CRT boundary crossing on Windows.
// ===----------------------------------------------------------------------===

static void *bridgeAlloc(uint64_t Size) { return ::malloc(Size); }
static void *bridgeRealloc(void *Ptr, uint64_t Size) {
  return ::realloc(Ptr, Size);
}
static void bridgeFree(void *Ptr) { ::free(Ptr); }

// ===----------------------------------------------------------------------===
//  Diagnostics — simple stderr output for now
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
  if (!M)
    return nullptr;
  return wrapCtx(&unwrap(M)->getContext());
}

// ===----------------------------------------------------------------------===
//  Module iteration
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeModuleGetFirstFunction(NevercModuleRef M) {
  if (!M)
    return nullptr;
  auto *Mod = unwrap(M);
  if (Mod->empty())
    return nullptr;
  return wrapV(&*Mod->begin());
}

static NevercValueRef bridgeModuleGetLastFunction(NevercModuleRef M) {
  if (!M)
    return nullptr;
  auto *Mod = unwrap(M);
  if (Mod->empty())
    return nullptr;
  return wrapV(&*Mod->rbegin());
}

static NevercValueRef bridgeModuleGetNextFunction(NevercValueRef F) {
  if (!F)
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn || !Fn->getParent())
    return nullptr;
  auto It = std::next(Fn->getIterator());
  if (It == Fn->getParent()->end())
    return nullptr;
  return wrapV(&*It);
}

static NevercValueRef bridgeModuleGetNamedFunction(NevercModuleRef M,
                                                   const char *Name) {
  if (!M || !Name)
    return nullptr;
  auto *Fn = unwrap(M)->getFunction(Name);
  return Fn ? wrapV(Fn) : nullptr;
}

static NevercValueRef bridgeModuleGetFirstGlobal(NevercModuleRef M) {
  if (!M)
    return nullptr;
  auto *Mod = unwrap(M);
  if (Mod->global_empty())
    return nullptr;
  return wrapV(&*Mod->global_begin());
}

static NevercValueRef bridgeModuleGetNextGlobal(NevercValueRef G) {
  if (!G)
    return nullptr;
  auto *GV = dyn_cast<GlobalVariable>(unwrapV(G));
  if (!GV || !GV->getParent())
    return nullptr;
  auto It = std::next(GV->getIterator());
  if (It == GV->getParent()->global_end())
    return nullptr;
  return wrapV(&*It);
}

static NevercValueRef bridgeModuleAddFunction(NevercModuleRef M,
                                              const char *Name,
                                              NevercTypeRef FnTy) {
  if (!M || !Name || !FnTy)
    return nullptr;
  auto *Mod = unwrap(M);
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  if (!FT)
    return nullptr;
  auto *Fn = Function::Create(FT, GlobalValue::ExternalLinkage, Name, Mod);
  return wrapV(Fn);
}

// ===----------------------------------------------------------------------===
//  Value ops
// ===----------------------------------------------------------------------===

static const char *bridgeValueGetName(NevercValueRef V) {
  if (!V)
    return "";
  auto Name = unwrapV(V)->getName();
  return Name.data() ? Name.data() : "";
}

static NevercTypeRef bridgeValueGetType(NevercValueRef V) {
  if (!V)
    return nullptr;
  return wrapTy(unwrapV(V)->getType());
}

static void bridgeValueReplaceAllUsesWith(NevercValueRef Old,
                                          NevercValueRef New) {
  if (!Old || !New)
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
  if (!V)
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
  if (!F)
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn || Fn->empty())
    return nullptr;
  return wrapBB(&Fn->front());
}

static NevercBasicBlockRef bridgeFunctionGetLastBB(NevercValueRef F) {
  if (!F)
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn || Fn->empty())
    return nullptr;
  return wrapBB(&Fn->back());
}

static NevercBasicBlockRef bridgeFunctionGetNextBB(NevercBasicBlockRef BB) {
  if (!BB)
    return nullptr;
  auto *Block = unwrapBB(BB);
  if (!Block->getParent())
    return nullptr;
  auto It = std::next(Block->getIterator());
  if (It == Block->getParent()->end())
    return nullptr;
  return wrapBB(&*It);
}

static unsigned bridgeFunctionGetArgCount(NevercValueRef F) {
  if (!F)
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  return Fn ? Fn->arg_size() : 0;
}

static NevercValueRef bridgeFunctionGetArg(NevercValueRef F, unsigned Idx) {
  if (!F)
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn || Idx >= Fn->arg_size())
    return nullptr;
  return wrapV(Fn->getArg(Idx));
}

static int bridgeFunctionIsDeclaration(NevercValueRef F) {
  if (!F)
    return 1;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  return (!Fn || Fn->isDeclaration()) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  BasicBlock ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBBGetFirstInst(NevercBasicBlockRef BB) {
  if (!BB)
    return nullptr;
  auto *Block = unwrapBB(BB);
  if (Block->empty())
    return nullptr;
  return wrapV(&Block->front());
}

static NevercValueRef bridgeBBGetLastInst(NevercBasicBlockRef BB) {
  if (!BB)
    return nullptr;
  auto *Block = unwrapBB(BB);
  if (Block->empty())
    return nullptr;
  return wrapV(&Block->back());
}

static NevercValueRef bridgeBBGetNextInst(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (!Inst || !Inst->getParent())
    return nullptr;
  auto It = std::next(Inst->getIterator());
  if (It == Inst->getParent()->end())
    return nullptr;
  return wrapV(&*It);
}

static NevercValueRef bridgeBBGetTerminator(NevercBasicBlockRef BB) {
  if (!BB)
    return nullptr;
  auto *T = unwrapBB(BB)->getTerminator();
  return T ? wrapV(T) : nullptr;
}

static NevercValueRef bridgeBBGetParentFunction(NevercBasicBlockRef BB) {
  if (!BB)
    return nullptr;
  return wrapV(unwrapBB(BB)->getParent());
}

static NevercBasicBlockRef bridgeBBCreate(NevercContextRef C, const char *Name,
                                          NevercValueRef F) {
  if (!C)
    return nullptr;
  auto *Fn = F ? dyn_cast<Function>(unwrapV(F)) : nullptr;
  return wrapBB(BasicBlock::Create(*unwrapCtx(C), Name ? Name : "", Fn));
}

// ===----------------------------------------------------------------------===
//  Instruction ops
// ===----------------------------------------------------------------------===

static unsigned bridgeInstGetOpcode(NevercValueRef I) {
  if (!I)
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return Inst ? Inst->getOpcode() : 0;
}

static unsigned bridgeInstGetNumOperands(NevercValueRef I) {
  if (!I)
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return Inst ? Inst->getNumOperands() : 0;
}

static NevercValueRef bridgeInstGetOperand(NevercValueRef I, unsigned Idx) {
  if (!I)
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (!Inst || Idx >= Inst->getNumOperands())
    return nullptr;
  return wrapV(Inst->getOperand(Idx));
}

static void bridgeInstSetOperand(NevercValueRef I, unsigned Idx,
                                 NevercValueRef V) {
  if (!I || !V)
    return;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (!Inst || Idx >= Inst->getNumOperands())
    return;
  Inst->setOperand(Idx, unwrapV(V));
}

static void bridgeInstEraseFromParent(NevercValueRef I) {
  if (!I)
    return;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (!Inst)
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
  if (!I)
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return Inst ? wrapV(Inst->clone()) : nullptr;
}

static NevercBasicBlockRef bridgeInstGetParentBB(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return Inst ? wrapBB(Inst->getParent()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  Type ops
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeTypeGetInt1(NevercContextRef C) {
  if (!C)
    return nullptr;
  return wrapTy(Type::getInt1Ty(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetInt8(NevercContextRef C) {
  if (!C)
    return nullptr;
  return wrapTy(Type::getInt8Ty(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetInt16(NevercContextRef C) {
  if (!C)
    return nullptr;
  return wrapTy(Type::getInt16Ty(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetInt32(NevercContextRef C) {
  if (!C)
    return nullptr;
  return wrapTy(Type::getInt32Ty(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetInt64(NevercContextRef C) {
  if (!C)
    return nullptr;
  return wrapTy(Type::getInt64Ty(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetIntN(NevercContextRef C, unsigned NumBits) {
  if (!C || NumBits == 0)
    return nullptr;
  return wrapTy(Type::getIntNTy(*unwrapCtx(C), NumBits));
}
static NevercTypeRef bridgeTypeGetFloat(NevercContextRef C) {
  if (!C)
    return nullptr;
  return wrapTy(Type::getFloatTy(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetDouble(NevercContextRef C) {
  if (!C)
    return nullptr;
  return wrapTy(Type::getDoubleTy(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetVoid(NevercContextRef C) {
  if (!C)
    return nullptr;
  return wrapTy(Type::getVoidTy(*unwrapCtx(C)));
}
static NevercTypeRef bridgeTypeGetPtr(NevercContextRef C) {
  if (!C)
    return nullptr;
  return wrapTy(PointerType::get(*unwrapCtx(C), 0));
}
static NevercTypeRef bridgeTypeGetArray(NevercTypeRef ElemTy, uint64_t Count) {
  if (!ElemTy)
    return nullptr;
  return wrapTy(ArrayType::get(unwrapTy(ElemTy), Count));
}
static NevercTypeRef bridgeTypeGetFunction(NevercTypeRef RetTy,
                                           NevercTypeRef *ParamTys,
                                           unsigned ParamCount, int IsVarArg) {
  if (!RetTy || (ParamCount > 0 && !ParamTys))
    return nullptr;
  SmallVector<Type *, 8> Params;
  for (unsigned I = 0; I < ParamCount; ++I) {
    if (!ParamTys[I])
      return nullptr;
    Params.push_back(unwrapTy(ParamTys[I]));
  }
  return wrapTy(FunctionType::get(unwrapTy(RetTy), Params, IsVarArg != 0));
}
static int bridgeTypeIsInteger(NevercTypeRef T) {
  if (!T)
    return 0;
  return unwrapTy(T)->isIntegerTy() ? 1 : 0;
}
static int bridgeTypeIsFloat(NevercTypeRef T) {
  if (!T)
    return 0;
  return unwrapTy(T)->isFloatingPointTy() ? 1 : 0;
}
static int bridgeTypeIsPointer(NevercTypeRef T) {
  if (!T)
    return 0;
  return unwrapTy(T)->isPointerTy() ? 1 : 0;
}
static int bridgeTypeIsVoid(NevercTypeRef T) {
  if (!T)
    return 0;
  return unwrapTy(T)->isVoidTy() ? 1 : 0;
}
static unsigned bridgeTypeGetIntWidth(NevercTypeRef T) {
  if (!T || !unwrapTy(T)->isIntegerTy())
    return 0;
  return unwrapTy(T)->getIntegerBitWidth();
}

// ===----------------------------------------------------------------------===
//  Constant creation
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeConstInt(NevercTypeRef IntTy, uint64_t Val,
                                     int SignExtend) {
  if (!IntTy)
    return nullptr;
  Type *T = unwrapTy(IntTy);
  if (!T->isIntegerTy())
    return nullptr;
  return wrapV(ConstantInt::get(T, Val, SignExtend != 0));
}

static NevercValueRef bridgeConstFloat(NevercTypeRef FloatTy, double Val) {
  if (!FloatTy)
    return nullptr;
  Type *T = unwrapTy(FloatTy);
  if (!T->isFloatingPointTy())
    return nullptr;
  return wrapV(ConstantFP::get(T, Val));
}

static NevercValueRef bridgeConstNull(NevercTypeRef Ty) {
  if (!Ty)
    return nullptr;
  return wrapV(Constant::getNullValue(unwrapTy(Ty)));
}

static NevercValueRef bridgeConstUndef(NevercTypeRef Ty) {
  if (!Ty)
    return nullptr;
  return wrapV(UndefValue::get(unwrapTy(Ty)));
}

static NevercValueRef bridgeConstString(NevercContextRef C, const char *Str,
                                        uint32_t Len,
                                        int DontNullTerminate) {
  if (!C || !Str)
    return nullptr;
  return wrapV(ConstantDataArray::getString(
      *unwrapCtx(C), StringRef(Str, Len), DontNullTerminate == 0));
}

// ===----------------------------------------------------------------------===
//  IRBuilder
// ===----------------------------------------------------------------------===

static NevercBuilderRef bridgeBuilderCreate(NevercContextRef C) {
  if (!C)
    return nullptr;
  return wrapB(new IRBuilder<>(*unwrapCtx(C)));
}

static void bridgeBuilderDispose(NevercBuilderRef B) {
  if (B)
    delete unwrapB(B);
}

static void bridgeBuilderSetInsertPoint(NevercBuilderRef B,
                                        NevercBasicBlockRef BB) {
  if (!B || !BB)
    return;
  unwrapB(B)->SetInsertPoint(unwrapBB(BB));
}

static void bridgeBuilderSetInsertPointBefore(NevercBuilderRef B,
                                              NevercValueRef Inst) {
  if (!B || !Inst)
    return;
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  if (!I || !I->getParent())
    return;
  unwrapB(B)->SetInsertPoint(I->getIterator());
}

static NevercValueRef bridgeBuildAdd(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateAdd(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildSub(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateSub(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildMul(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateMul(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildUDiv(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateUDiv(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildSDiv(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateSDiv(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildAnd(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateAnd(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildOr(NevercBuilderRef B, NevercValueRef LHS,
                                    NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateOr(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildXor(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateXor(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildShl(NevercBuilderRef B, NevercValueRef LHS,
                                     NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateShl(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildLShr(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateLShr(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildAShr(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateAShr(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildICmp(NevercBuilderRef B, unsigned Pred,
                                      NevercValueRef LHS, NevercValueRef RHS,
                                      const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateICmp(static_cast<CmpInst::Predicate>(Pred),
                                      unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}
static NevercValueRef bridgeBuildFCmp(NevercBuilderRef B, unsigned Pred,
                                      NevercValueRef LHS, NevercValueRef RHS,
                                      const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(unwrapB(B)->CreateFCmp(static_cast<CmpInst::Predicate>(Pred),
                                      unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildBr(NevercBuilderRef B,
                                    NevercBasicBlockRef Dest) {
  if (!B || !Dest)
    return nullptr;
  return wrapV(unwrapB(B)->CreateBr(unwrapBB(Dest)));
}
static NevercValueRef bridgeBuildCondBr(NevercBuilderRef B,
                                        NevercValueRef Cond,
                                        NevercBasicBlockRef Then,
                                        NevercBasicBlockRef Else) {
  if (!B || !Cond || !Then || !Else)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateCondBr(unwrapV(Cond), unwrapBB(Then), unwrapBB(Else)));
}
static NevercValueRef bridgeBuildRet(NevercBuilderRef B, NevercValueRef V) {
  if (!B || !V)
    return nullptr;
  return wrapV(unwrapB(B)->CreateRet(unwrapV(V)));
}
static NevercValueRef bridgeBuildRetVoid(NevercBuilderRef B) {
  if (!B)
    return nullptr;
  return wrapV(unwrapB(B)->CreateRetVoid());
}

static NevercValueRef bridgeBuildCall(NevercBuilderRef B, NevercTypeRef FnTy,
                                      NevercValueRef Fn, NevercValueRef *Args,
                                      unsigned ArgCount, const char *Name) {
  if (!B || !FnTy || !Fn || (ArgCount > 0 && !Args))
    return nullptr;
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  if (!FT)
    return nullptr;
  SmallVector<Value *, 8> ArgVec;
  for (unsigned I = 0; I < ArgCount; ++I) {
    if (!Args[I])
      return nullptr;
    ArgVec.push_back(unwrapV(Args[I]));
  }
  return wrapV(unwrapB(B)->CreateCall(FT, unwrapV(Fn), ArgVec, Name ? Name : ""));
}

static NevercValueRef bridgeBuildGEP(NevercBuilderRef B, NevercTypeRef Ty,
                                     NevercValueRef Ptr,
                                     NevercValueRef *Indices,
                                     unsigned NumIndices, const char *Name) {
  if (!B || !Ty || !Ptr || (NumIndices > 0 && !Indices))
    return nullptr;
  SmallVector<Value *, 4> Idxs;
  for (unsigned I = 0; I < NumIndices; ++I) {
    if (!Indices[I])
      return nullptr;
    Idxs.push_back(unwrapV(Indices[I]));
  }
  return wrapV(
      unwrapB(B)->CreateGEP(unwrapTy(Ty), unwrapV(Ptr), Idxs, Name ? Name : ""));
}

static NevercValueRef bridgeBuildLoad(NevercBuilderRef B, NevercTypeRef Ty,
                                      NevercValueRef Ptr, const char *Name) {
  if (!B || !Ty || !Ptr)
    return nullptr;
  return wrapV(unwrapB(B)->CreateLoad(unwrapTy(Ty), unwrapV(Ptr), Name ? Name : ""));
}

static NevercValueRef bridgeBuildStore(NevercBuilderRef B, NevercValueRef Val,
                                       NevercValueRef Ptr) {
  if (!B || !Val || !Ptr)
    return nullptr;
  return wrapV(unwrapB(B)->CreateStore(unwrapV(Val), unwrapV(Ptr)));
}

static NevercValueRef bridgeBuildAlloca(NevercBuilderRef B, NevercTypeRef Ty,
                                        const char *Name) {
  if (!B || !Ty)
    return nullptr;
  return wrapV(unwrapB(B)->CreateAlloca(unwrapTy(Ty), nullptr, Name ? Name : ""));
}

static NevercValueRef bridgeBuildBitCast(NevercBuilderRef B, NevercValueRef V,
                                         NevercTypeRef DestTy,
                                         const char *Name) {
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(unwrapB(B)->CreateBitCast(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}
static NevercValueRef bridgeBuildIntToPtr(NevercBuilderRef B, NevercValueRef V,
                                          NevercTypeRef DestTy,
                                          const char *Name) {
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateIntToPtr(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}
static NevercValueRef bridgeBuildPtrToInt(NevercBuilderRef B, NevercValueRef V,
                                          NevercTypeRef DestTy,
                                          const char *Name) {
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreatePtrToInt(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}
static NevercValueRef bridgeBuildZExt(NevercBuilderRef B, NevercValueRef V,
                                      NevercTypeRef DestTy, const char *Name) {
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(unwrapB(B)->CreateZExt(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}
static NevercValueRef bridgeBuildSExt(NevercBuilderRef B, NevercValueRef V,
                                      NevercTypeRef DestTy, const char *Name) {
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(unwrapB(B)->CreateSExt(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}
static NevercValueRef bridgeBuildTrunc(NevercBuilderRef B, NevercValueRef V,
                                       NevercTypeRef DestTy,
                                       const char *Name) {
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(unwrapB(B)->CreateTrunc(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

static NevercValueRef bridgeBuildSelect(NevercBuilderRef B, NevercValueRef Cond,
                                        NevercValueRef Then,
                                        NevercValueRef Else,
                                        const char *Name) {
  if (!B || !Cond || !Then || !Else)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSelect(unwrapV(Cond), unwrapV(Then), unwrapV(Else), Name ? Name : ""));
}

static NevercValueRef bridgeBuildPhi(NevercBuilderRef B, NevercTypeRef Ty,
                                     const char *Name) {
  if (!B || !Ty)
    return nullptr;
  return wrapV(unwrapB(B)->CreatePHI(unwrapTy(Ty), 2, Name ? Name : ""));
}

static void bridgePhiAddIncoming(NevercValueRef Phi, NevercValueRef *Values,
                                 NevercBasicBlockRef *Blocks, unsigned Count) {
  if (!Phi || !Values || !Blocks || Count == 0)
    return;
  auto *PN = dyn_cast<PHINode>(unwrapV(Phi));
  if (!PN)
    return;
  for (unsigned I = 0; I < Count; ++I) {
    if (!Values[I] || !Blocks[I])
      continue;
    PN->addIncoming(unwrapV(Values[I]), unwrapBB(Blocks[I]));
  }
}

// ===----------------------------------------------------------------------===
//  Metadata
// ===----------------------------------------------------------------------===

static NevercMetadataRef bridgeMDStringCreate(NevercContextRef C,
                                              const char *Str, uint32_t Len) {
  if (!C || !Str)
    return nullptr;
  return wrapMD(MDString::get(*unwrapCtx(C), StringRef(Str, Len)));
}

static NevercMetadataRef bridgeMDNodeCreate(NevercContextRef C,
                                            NevercMetadataRef *Vals,
                                            unsigned Count) {
  if (!C || (Count > 0 && !Vals))
    return nullptr;
  SmallVector<Metadata *, 4> Ops;
  for (unsigned I = 0; I < Count; ++I) {
    if (!Vals[I])
      return nullptr;
    Ops.push_back(unwrapMD(Vals[I]));
  }
  return wrapMD(MDNode::get(*unwrapCtx(C), Ops));
}

static void bridgeInstSetMetadata(NevercValueRef Inst, unsigned KindID,
                                  NevercMetadataRef MD) {
  if (!Inst || !MD)
    return;
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  auto *Node = dyn_cast<MDNode>(unwrapMD(MD));
  if (I && Node)
    I->setMetadata(KindID, Node);
}

static NevercMetadataRef bridgeInstGetMetadata(NevercValueRef Inst,
                                               unsigned KindID) {
  if (!Inst)
    return nullptr;
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  if (!I)
    return nullptr;
  auto *N = I->getMetadata(KindID);
  return N ? wrapMD(N) : nullptr;
}

static unsigned bridgeMDKindGetID(NevercContextRef C, const char *Name) {
  if (!C || !Name)
    return 0;
  return unwrapCtx(C)->getMDKindID(Name);
}

// ===----------------------------------------------------------------------===
//  MIR ops
// ===----------------------------------------------------------------------===

static NevercMachineBBRef bridgeMFuncGetFirstBB(NevercMachineFuncRef MF) {
  if (!MF)
    return nullptr;
  auto *Func = unwrapMF(MF);
  if (Func->empty())
    return nullptr;
  return wrapMBB(&Func->front());
}

static NevercMachineBBRef bridgeMFuncGetNextBB(NevercMachineBBRef MBB) {
  if (!MBB)
    return nullptr;
  auto *Block = unwrapMBB(MBB);
  if (!Block->getParent())
    return nullptr;
  auto It = std::next(Block->getIterator());
  if (It == Block->getParent()->end())
    return nullptr;
  return wrapMBB(&*It);
}

static NevercMachineInstrRef bridgeMBBGetFirstInst(NevercMachineBBRef MBB) {
  if (!MBB)
    return nullptr;
  auto *Block = unwrapMBB(MBB);
  if (Block->empty())
    return nullptr;
  return wrapMI(&Block->front());
}

static NevercMachineInstrRef bridgeMBBGetNextInst(NevercMachineInstrRef MI) {
  if (!MI)
    return nullptr;
  auto *Inst = unwrapMI(MI);
  if (!Inst->getParent())
    return nullptr;
  auto It = std::next(Inst->getIterator());
  if (It == Inst->getParent()->end())
    return nullptr;
  return wrapMI(&*It);
}

static unsigned bridgeMInstGetOpcode(NevercMachineInstrRef MI) {
  if (!MI)
    return 0;
  return unwrapMI(MI)->getOpcode();
}

static unsigned bridgeMInstGetNumOperands(NevercMachineInstrRef MI) {
  if (!MI)
    return 0;
  return unwrapMI(MI)->getNumOperands();
}

static void bridgeMInstEraseFromParent(NevercMachineInstrRef MI) {
  if (!MI)
    return;
  unwrapMI(MI)->eraseFromParent();
}

// ===----------------------------------------------------------------------===
//  Module info
// ===----------------------------------------------------------------------===

static const char *bridgeModuleGetDataLayout(NevercModuleRef M) {
  if (!M)
    return "";
  return unwrap(M)->getDataLayoutStr().c_str();
}

static const char *bridgeModuleGetTargetTriple(NevercModuleRef M) {
  if (!M)
    return "";
  return unwrap(M)->getTargetTriple().c_str();
}

static void bridgeModulePrint(NevercModuleRef M) {
  if (!M)
    return;
  unwrap(M)->print(errs(), nullptr);
}

// ===----------------------------------------------------------------------===
//  Value debug
// ===----------------------------------------------------------------------===

static void bridgeValueDump(NevercValueRef V) {
  if (!V)
    return;
  unwrapV(V)->print(errs());
  errs() << "\n";
}

static unsigned bridgeValueGetNumUses(NevercValueRef V) {
  if (!V)
    return 0;
  return unwrapV(V)->getNumUses();
}

// ===----------------------------------------------------------------------===
//  Function linkage & calling convention
// ===----------------------------------------------------------------------===

static unsigned bridgeFunctionGetLinkage(NevercValueRef F) {
  if (!F)
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  return Fn ? static_cast<unsigned>(Fn->getLinkage()) : 0;
}

static void bridgeFunctionSetLinkage(NevercValueRef F, unsigned Linkage) {
  if (!F)
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (Fn)
    Fn->setLinkage(static_cast<GlobalValue::LinkageTypes>(Linkage));
}

static unsigned bridgeFunctionGetCallingConv(NevercValueRef F) {
  if (!F)
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  return Fn ? static_cast<unsigned>(Fn->getCallingConv()) : 0;
}

static void bridgeFunctionSetCallingConv(NevercValueRef F, unsigned CC) {
  if (!F)
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (Fn)
    Fn->setCallingConv(static_cast<CallingConv::ID>(CC));
}

// ===----------------------------------------------------------------------===
//  Instruction positioning
// ===----------------------------------------------------------------------===

static void bridgeInstMoveBefore(NevercValueRef I, NevercValueRef Before) {
  if (!I || !Before)
    return;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  auto *BeforeInst = dyn_cast<Instruction>(unwrapV(Before));
  if (!Inst || !BeforeInst || !BeforeInst->getParent())
    return;
  Inst->moveBefore(BeforeInst);
}

// ===----------------------------------------------------------------------===
//  Module set ops
// ===----------------------------------------------------------------------===

static void bridgeModuleSetDataLayout(NevercModuleRef M, const char *DL) {
  if (!M || !DL)
    return;
  unwrap(M)->setDataLayout(DL);
}

static void bridgeModuleSetTargetTriple(NevercModuleRef M, const char *Triple) {
  if (!M || !Triple)
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
  if (!V)
    return nullptr;
  auto *Val = unwrapV(V);
  if (Val->use_empty())
    return nullptr;
  return wrapUse(&*Val->use_begin());
}

static NevercUseRef bridgeUseGetNext(NevercUseRef U) {
  if (!U)
    return nullptr;
  Use *Next = unwrapUse(U)->getNext();
  return Next ? wrapUse(Next) : nullptr;
}

static NevercValueRef bridgeUseGetUser(NevercUseRef U) {
  if (!U)
    return nullptr;
  return wrapV(unwrapUse(U)->getUser());
}

// ===----------------------------------------------------------------------===
//  Function string attributes
// ===----------------------------------------------------------------------===

static void bridgeFunctionAddStringAttr(NevercValueRef F, const char *Kind,
                                        const char *Val) {
  if (!F || !Kind)
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn)
    return;
  Fn->addFnAttr(Kind, Val ? Val : "");
}

static int bridgeFunctionHasStringAttr(NevercValueRef F, const char *Kind) {
  if (!F || !Kind)
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn)
    return 0;
  return Fn->hasFnAttribute(Kind) ? 1 : 0;
}

static const char *bridgeFunctionGetStringAttr(NevercValueRef F,
                                               const char *Kind) {
  if (!F || !Kind)
    return "";
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn || !Fn->hasFnAttribute(Kind))
    return "";
  auto Val = Fn->getFnAttribute(Kind).getValueAsString();
  return Val.data() ? Val.data() : "";
}

static void bridgeFunctionRemoveStringAttr(NevercValueRef F,
                                           const char *Kind) {
  if (!F || !Kind)
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (Fn)
    Fn->removeFnAttr(Kind);
}

// ===----------------------------------------------------------------------===
//  BasicBlock removal
// ===----------------------------------------------------------------------===

static void bridgeBBRemoveFromParent(NevercBasicBlockRef BB) {
  if (!BB)
    return;
  unwrapBB(BB)->removeFromParent();
}

static void bridgeBBEraseFromParent(NevercBasicBlockRef BB) {
  if (!BB)
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
  return (V && isa<Function>(unwrapV(V))) ? 1 : 0;
}
static int bridgeValueIsGlobalVariable(NevercValueRef V) {
  return (V && isa<GlobalVariable>(unwrapV(V))) ? 1 : 0;
}
static int bridgeValueIsInstruction(NevercValueRef V) {
  return (V && isa<Instruction>(unwrapV(V))) ? 1 : 0;
}
static int bridgeValueIsConstant(NevercValueRef V) {
  return (V && isa<Constant>(unwrapV(V))) ? 1 : 0;
}
static int bridgeValueIsArgument(NevercValueRef V) {
  return (V && isa<Argument>(unwrapV(V))) ? 1 : 0;
}
static int bridgeValueIsBasicBlock(NevercValueRef V) {
  return (V && isa<BasicBlock>(unwrapV(V))) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  Struct type ops
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeTypeGetStruct(NevercContextRef C,
                                         NevercTypeRef *ElemTys,
                                         unsigned ElemCount, int IsPacked) {
  if (!C || (ElemCount > 0 && !ElemTys))
    return nullptr;
  SmallVector<Type *, 8> Elems;
  for (unsigned I = 0; I < ElemCount; ++I) {
    if (!ElemTys[I])
      return nullptr;
    Elems.push_back(unwrapTy(ElemTys[I]));
  }
  return wrapTy(StructType::get(*unwrapCtx(C), Elems, IsPacked != 0));
}

static NevercTypeRef bridgeTypeGetNamedStruct(NevercContextRef C,
                                              const char *Name) {
  if (!C || !Name)
    return nullptr;
  return wrapTy(StructType::create(*unwrapCtx(C), Name));
}

static void bridgeStructSetBody(NevercTypeRef StructTy,
                                NevercTypeRef *ElemTys, unsigned ElemCount,
                                int IsPacked) {
  if (!StructTy || (ElemCount > 0 && !ElemTys))
    return;
  auto *ST = dyn_cast<StructType>(unwrapTy(StructTy));
  if (!ST)
    return;
  if (!ST->isOpaque()) {
    WithColor::warning(errs(), "neverc-plugin")
        << "StructSetBody called on already-defined struct '"
        << (ST->hasName() ? ST->getName() : "<anonymous>") << "'; ignoring\n";
    return;
  }
  SmallVector<Type *, 8> Elems;
  for (unsigned I = 0; I < ElemCount; ++I) {
    if (!ElemTys[I])
      return;
    Elems.push_back(unwrapTy(ElemTys[I]));
  }
  ST->setBody(Elems, IsPacked != 0);
}

static unsigned bridgeStructGetNumElements(NevercTypeRef StructTy) {
  if (!StructTy)
    return 0;
  auto *ST = dyn_cast<StructType>(unwrapTy(StructTy));
  return ST ? ST->getNumElements() : 0;
}

static NevercTypeRef bridgeStructGetElementType(NevercTypeRef StructTy,
                                                unsigned Idx) {
  if (!StructTy)
    return nullptr;
  auto *ST = dyn_cast<StructType>(unwrapTy(StructTy));
  if (!ST || Idx >= ST->getNumElements())
    return nullptr;
  return wrapTy(ST->getElementType(Idx));
}

static int bridgeTypeIsStruct(NevercTypeRef T) {
  return (T && unwrapTy(T)->isStructTy()) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  GlobalVariable ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeModuleAddGlobal(NevercModuleRef M,
                                            NevercTypeRef Ty, int IsConstant,
                                            const char *Name) {
  if (!M || !Ty || !Name)
    return nullptr;
  auto *GV = new GlobalVariable(*unwrap(M), unwrapTy(Ty), IsConstant != 0,
                                GlobalValue::ExternalLinkage, nullptr, Name);
  return wrapV(GV);
}

static NevercValueRef bridgeGlobalGetInitializer(NevercValueRef GV) {
  if (!GV)
    return nullptr;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (!G || !G->hasInitializer())
    return nullptr;
  return wrapV(G->getInitializer());
}

static void bridgeGlobalSetInitializer(NevercValueRef GV,
                                       NevercValueRef Init) {
  if (!GV)
    return;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (!G)
    return;
  if (!Init) {
    G->setInitializer(nullptr);
    return;
  }
  auto *C = dyn_cast<Constant>(unwrapV(Init));
  if (!C) {
    WithColor::warning(errs(), "neverc-plugin")
        << "GlobalSetInitializer called with non-constant value; ignoring\n";
    return;
  }
  G->setInitializer(C);
}

static int bridgeGlobalHasInitializer(NevercValueRef GV) {
  if (!GV)
    return 0;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  return (G && G->hasInitializer()) ? 1 : 0;
}

static int bridgeGlobalIsConstant(NevercValueRef GV) {
  if (!GV)
    return 0;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  return (G && G->isConstant()) ? 1 : 0;
}

static void bridgeGlobalSetConstant(NevercValueRef GV, int IsConstant) {
  if (!GV)
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
  if (!M || !Name)
    return nullptr;
  auto *GV = unwrap(M)->getGlobalVariable(Name, true);
  return GV ? wrapV(GV) : nullptr;
}

// ===----------------------------------------------------------------------===
//  Instruction insertion
// ===----------------------------------------------------------------------===

static void bridgeInstInsertBefore(NevercValueRef Inst,
                                   NevercValueRef Before) {
  if (!Inst || !Before)
    return;
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  auto *BeforeI = dyn_cast<Instruction>(unwrapV(Before));
  if (!I || !BeforeI || !BeforeI->getParent())
    return;
  I->insertBefore(BeforeI);
}

static void bridgeInstInsertAfter(NevercValueRef Inst,
                                  NevercValueRef After) {
  if (!Inst || !After)
    return;
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  auto *AfterI = dyn_cast<Instruction>(unwrapV(After));
  if (!I || !AfterI || !AfterI->getParent())
    return;
  I->insertAfter(AfterI);
}

// ===----------------------------------------------------------------------===
//  MIR name & operand access
// ===----------------------------------------------------------------------===

static const char *bridgeMFuncGetName(NevercMachineFuncRef MF) {
  if (!MF)
    return "";
  auto Name = unwrapMF(MF)->getName();
  return Name.data() ? Name.data() : "";
}

static int64_t bridgeMInstGetOperandImm(NevercMachineInstrRef MI,
                                        unsigned Idx) {
  if (!MI)
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isImm())
    return 0;
  return Inst->getOperand(Idx).getImm();
}

static int bridgeMInstGetOperandIsReg(NevercMachineInstrRef MI, unsigned Idx) {
  if (!MI)
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands())
    return 0;
  return Inst->getOperand(Idx).isReg() ? 1 : 0;
}

static unsigned bridgeMInstGetOperandReg(NevercMachineInstrRef MI,
                                         unsigned Idx) {
  if (!MI)
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).getReg().id();
}

static int bridgeMInstGetOperandIsImm(NevercMachineInstrRef MI, unsigned Idx) {
  if (!MI)
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands())
    return 0;
  return Inst->getOperand(Idx).isImm() ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  MBB navigation
// ===----------------------------------------------------------------------===

static unsigned bridgeMBBGetNumber(NevercMachineBBRef MBB) {
  if (!MBB)
    return 0;
  return unwrapMBB(MBB)->getNumber();
}

static unsigned bridgeMBBGetSuccCount(NevercMachineBBRef MBB) {
  if (!MBB)
    return 0;
  return unwrapMBB(MBB)->succ_size();
}

static unsigned bridgeMBBGetPredCount(NevercMachineBBRef MBB) {
  if (!MBB)
    return 0;
  return unwrapMBB(MBB)->pred_size();
}

// ===----------------------------------------------------------------------===
//  Function/global removal from module
// ===----------------------------------------------------------------------===

static void bridgeModuleRemoveFunction(NevercModuleRef M, NevercValueRef F) {
  if (!M || !F)
    return;
  auto *Mod = unwrap(M);
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn || Fn->getParent() != Mod)
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
  if (!M || !GV)
    return;
  auto *Mod = unwrap(M);
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (!G || G->getParent() != Mod)
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
  if (!M)
    return 0;
  return unwrap(M)->size();
}

static unsigned bridgeModuleGetGlobalCount(NevercModuleRef M) {
  if (!M)
    return 0;
  return unwrap(M)->global_size();
}

static NevercTypeRef bridgeValueGetTypeAsFunction(NevercValueRef F) {
  if (!F)
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn)
    return nullptr;
  return wrapTy(Fn->getFunctionType());
}

// ===----------------------------------------------------------------------===
//  Function return type
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeFunctionGetReturnType(NevercValueRef F) {
  if (!F)
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn)
    return nullptr;
  return wrapTy(Fn->getReturnType());
}

// ===----------------------------------------------------------------------===
//  Instruction queries
// ===----------------------------------------------------------------------===

static int bridgeInstIsTerminator(NevercValueRef I) {
  if (!I)
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return (Inst && Inst->isTerminator()) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  Unary builder ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildNeg(NevercBuilderRef B, NevercValueRef V,
                                     const char *Name) {
  if (!B || !V)
    return nullptr;
  return wrapV(unwrapB(B)->CreateNeg(unwrapV(V), Name ? Name : ""));
}

static NevercValueRef bridgeBuildNot(NevercBuilderRef B, NevercValueRef V,
                                     const char *Name) {
  if (!B || !V)
    return nullptr;
  return wrapV(unwrapB(B)->CreateNot(unwrapV(V), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFNeg(NevercBuilderRef B, NevercValueRef V,
                                      const char *Name) {
  if (!B || !V)
    return nullptr;
  return wrapV(unwrapB(B)->CreateFNeg(unwrapV(V), Name ? Name : ""));
}

static NevercValueRef bridgeBuildUnreachable(NevercBuilderRef B) {
  if (!B)
    return nullptr;
  return wrapV(unwrapB(B)->CreateUnreachable());
}

// ===----------------------------------------------------------------------===
//  Switch instruction
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildSwitch(NevercBuilderRef B, NevercValueRef V,
                                        NevercBasicBlockRef DefaultBB,
                                        unsigned NumCases) {
  if (!B || !V || !DefaultBB)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSwitch(unwrapV(V), unwrapBB(DefaultBB), NumCases));
}

static void bridgeSwitchAddCase(NevercValueRef Switch, NevercValueRef OnVal,
                                NevercBasicBlockRef Dest) {
  if (!Switch || !OnVal || !Dest)
    return;
  auto *SI = dyn_cast<SwitchInst>(unwrapV(Switch));
  if (!SI)
    return;
  auto *CI = dyn_cast<ConstantInt>(unwrapV(OnVal));
  if (!CI) {
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
  if (!PtrTy)
    return nullptr;
  auto *PT = dyn_cast<PointerType>(unwrapTy(PtrTy));
  if (!PT)
    return nullptr;
  return wrapV(ConstantPointerNull::get(PT));
}

// ===----------------------------------------------------------------------===
//  Remainder ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildURem(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateURem(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildSRem(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
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
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFPToSI(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

static NevercValueRef bridgeBuildSIToFP(NevercBuilderRef B, NevercValueRef V,
                                        NevercTypeRef DestTy,
                                        const char *Name) {
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateSIToFP(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFPToUI(NevercBuilderRef B, NevercValueRef V,
                                        NevercTypeRef DestTy,
                                        const char *Name) {
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFPToUI(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

static NevercValueRef bridgeBuildUIToFP(NevercBuilderRef B, NevercValueRef V,
                                        NevercTypeRef DestTy,
                                        const char *Name) {
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateUIToFP(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Register stubs — safe no-ops so the vtable is never null for these slots.
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

// ===----------------------------------------------------------------------===
//  Binary buffer ops
// ===----------------------------------------------------------------------===

static int bridgeBinaryResize(uint8_t **Data, uint64_t *Len,
                               uint64_t *Capacity, uint64_t NewLen) {
  if (!Data || !Len || !Capacity)
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
  if (!New)
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
  return (I && isa<CallInst>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsBranch(NevercValueRef I) {
  return (I && isa<BranchInst>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsLoad(NevercValueRef I) {
  return (I && isa<LoadInst>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsStore(NevercValueRef I) {
  return (I && isa<StoreInst>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsAlloca(NevercValueRef I) {
  return (I && isa<AllocaInst>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsPHI(NevercValueRef I) {
  return (I && isa<PHINode>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsGEP(NevercValueRef I) {
  return (I && isa<GetElementPtrInst>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsCast(NevercValueRef I) {
  return (I && isa<CastInst>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsBinaryOp(NevercValueRef I) {
  return (I && isa<BinaryOperator>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsUnaryOp(NevercValueRef I) {
  return (I && isa<UnaryOperator>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsSwitch(NevercValueRef I) {
  return (I && isa<SwitchInst>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsReturn(NevercValueRef I) {
  return (I && isa<ReturnInst>(unwrapV(I))) ? 1 : 0;
}
static int bridgeInstIsSelect(NevercValueRef I) {
  return (I && isa<SelectInst>(unwrapV(I))) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  CallInst ops
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeCallGetCalledOperand(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  return CB ? wrapV(CB->getCalledOperand()) : nullptr;
}

static unsigned bridgeCallGetNumArgs(NevercValueRef I) {
  if (!I)
    return 0;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  return CB ? CB->arg_size() : 0;
}

static NevercValueRef bridgeCallGetArg(NevercValueRef I, unsigned Idx) {
  if (!I)
    return nullptr;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  if (!CB || Idx >= CB->arg_size())
    return nullptr;
  return wrapV(CB->getArgOperand(Idx));
}

static NevercTypeRef bridgeCallGetFunctionType(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  return CB ? wrapTy(CB->getFunctionType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  FunctionType / ArrayType access
// ===----------------------------------------------------------------------===

static unsigned bridgeFuncTypeGetParamCount(NevercTypeRef FnTy) {
  if (!FnTy)
    return 0;
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  return FT ? FT->getNumParams() : 0;
}

static NevercTypeRef bridgeFuncTypeGetParamType(NevercTypeRef FnTy,
                                                unsigned Idx) {
  if (!FnTy)
    return nullptr;
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  if (!FT || Idx >= FT->getNumParams())
    return nullptr;
  return wrapTy(FT->getParamType(Idx));
}

static NevercTypeRef bridgeFuncTypeGetReturnType(NevercTypeRef FnTy) {
  if (!FnTy)
    return nullptr;
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  return FT ? wrapTy(FT->getReturnType()) : nullptr;
}

static int bridgeFuncTypeIsVarArg(NevercTypeRef FnTy) {
  if (!FnTy)
    return 0;
  auto *FT = dyn_cast<FunctionType>(unwrapTy(FnTy));
  return (FT && FT->isVarArg()) ? 1 : 0;
}

static int bridgeTypeIsFunctionTy(NevercTypeRef T) {
  return (T && unwrapTy(T)->isFunctionTy()) ? 1 : 0;
}

static int bridgeTypeIsArrayTy(NevercTypeRef T) {
  return (T && unwrapTy(T)->isArrayTy()) ? 1 : 0;
}

static uint64_t bridgeTypeGetArrayLength(NevercTypeRef T) {
  if (!T)
    return 0;
  auto *AT = dyn_cast<ArrayType>(unwrapTy(T));
  return AT ? AT->getNumElements() : 0;
}

static NevercTypeRef bridgeTypeGetArrayElementType(NevercTypeRef T) {
  if (!T)
    return nullptr;
  auto *AT = dyn_cast<ArrayType>(unwrapTy(T));
  return AT ? wrapTy(AT->getElementType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  BranchInst ops
// ===----------------------------------------------------------------------===

static int bridgeBrIsConditional(NevercValueRef I) {
  if (!I)
    return 0;
  auto *BI = dyn_cast<BranchInst>(unwrapV(I));
  return (BI && BI->isConditional()) ? 1 : 0;
}

static NevercValueRef bridgeBrGetCondition(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *BI = dyn_cast<BranchInst>(unwrapV(I));
  if (!BI || !BI->isConditional())
    return nullptr;
  return wrapV(BI->getCondition());
}

static NevercBasicBlockRef bridgeBrGetSuccessor(NevercValueRef I,
                                                unsigned Idx) {
  if (!I)
    return nullptr;
  auto *BI = dyn_cast<BranchInst>(unwrapV(I));
  if (!BI || Idx >= BI->getNumSuccessors())
    return nullptr;
  return wrapBB(BI->getSuccessor(Idx));
}

static unsigned bridgeBrGetNumSuccessors(NevercValueRef I) {
  if (!I)
    return 0;
  auto *BI = dyn_cast<BranchInst>(unwrapV(I));
  return BI ? BI->getNumSuccessors() : 0;
}

// ===----------------------------------------------------------------------===
//  Load/Store operand access
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeLoadGetPointerOperand(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *LI = dyn_cast<LoadInst>(unwrapV(I));
  return LI ? wrapV(LI->getPointerOperand()) : nullptr;
}

static NevercValueRef bridgeStoreGetValueOperand(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *SI = dyn_cast<StoreInst>(unwrapV(I));
  return SI ? wrapV(SI->getValueOperand()) : nullptr;
}

static NevercValueRef bridgeStoreGetPointerOperand(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *SI = dyn_cast<StoreInst>(unwrapV(I));
  return SI ? wrapV(SI->getPointerOperand()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  GEP access
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeGEPGetPointerOperand(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *GEP = dyn_cast<GetElementPtrInst>(unwrapV(I));
  return GEP ? wrapV(GEP->getPointerOperand()) : nullptr;
}

static unsigned bridgeGEPGetNumIndices(NevercValueRef I) {
  if (!I)
    return 0;
  auto *GEP = dyn_cast<GetElementPtrInst>(unwrapV(I));
  return GEP ? GEP->getNumIndices() : 0;
}

// ===----------------------------------------------------------------------===
//  BasicBlock predecessor/successor count
// ===----------------------------------------------------------------------===

static unsigned bridgeBBGetPredCount(NevercBasicBlockRef BB) {
  if (!BB)
    return 0;
  return std::distance(pred_begin(unwrapBB(BB)), pred_end(unwrapBB(BB)));
}

static unsigned bridgeBBGetSuccCount(NevercBasicBlockRef BB) {
  if (!BB)
    return 0;
  return std::distance(succ_begin(unwrapBB(BB)), succ_end(unwrapBB(BB)));
}

// ===----------------------------------------------------------------------===
//  Floating-point arithmetic
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeBuildFAdd(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFAdd(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFSub(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFSub(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFMul(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFMul(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFDiv(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFDiv(unwrapV(LHS), unwrapV(RHS), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFRem(NevercBuilderRef B, NevercValueRef LHS,
                                      NevercValueRef RHS, const char *Name) {
  if (!B || !LHS || !RHS)
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
  if (!B || !Agg)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateExtractValue(unwrapV(Agg), Idx, Name ? Name : ""));
}

static NevercValueRef bridgeBuildInsertValue(NevercBuilderRef B,
                                             NevercValueRef Agg,
                                             NevercValueRef Val, unsigned Idx,
                                             const char *Name) {
  if (!B || !Agg || !Val)
    return nullptr;
  return wrapV(unwrapB(B)->CreateInsertValue(unwrapV(Agg), unwrapV(Val), Idx,
                                             Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Constant aggregate creation
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeConstArray(NevercTypeRef ElemTy,
                                       NevercValueRef *Vals, unsigned Count) {
  if (!ElemTy || (Count > 0 && !Vals))
    return nullptr;
  SmallVector<Constant *, 8> Elems;
  for (unsigned I = 0; I < Count; ++I) {
    auto *C = dyn_cast_or_null<Constant>(unwrapV(Vals[I]));
    if (!C)
      return nullptr;
    Elems.push_back(C);
  }
  return wrapV(ConstantArray::get(ArrayType::get(unwrapTy(ElemTy), Count),
                                  Elems));
}

static NevercValueRef bridgeConstStruct(NevercContextRef C,
                                        NevercValueRef *Vals, unsigned Count,
                                        int IsPacked) {
  if (!C || (Count > 0 && !Vals))
    return nullptr;
  SmallVector<Constant *, 8> Elems;
  for (unsigned I = 0; I < Count; ++I) {
    auto *CV = dyn_cast_or_null<Constant>(unwrapV(Vals[I]));
    if (!CV)
      return nullptr;
    Elems.push_back(CV);
  }
  return wrapV(ConstantStruct::getAnon(*unwrapCtx(C), Elems, IsPacked != 0));
}

static NevercValueRef bridgeConstNamedStruct(NevercTypeRef StructTy,
                                             NevercValueRef *Vals,
                                             unsigned Count) {
  if (!StructTy || (Count > 0 && !Vals))
    return nullptr;
  auto *ST = dyn_cast<StructType>(unwrapTy(StructTy));
  if (!ST)
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
    if (!CV)
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
  if (!B || !Str)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateGlobalStringPtr(Str, Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Value name mutation
// ===----------------------------------------------------------------------===

static void bridgeValueSetName(NevercValueRef V, const char *Name) {
  if (!V)
    return;
  unwrapV(V)->setName(Name ? Name : "");
}

// ===----------------------------------------------------------------------===
//  GlobalVariable linkage
// ===----------------------------------------------------------------------===

static unsigned bridgeGlobalGetLinkage(NevercValueRef GV) {
  if (!GV)
    return 0;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  return G ? static_cast<unsigned>(G->getLinkage()) : 0;
}

static void bridgeGlobalSetLinkage(NevercValueRef GV, unsigned Linkage) {
  if (!GV)
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
  if (!B || !Ty || !Ptr || (NumIndices > 0 && !Indices))
    return nullptr;
  SmallVector<Value *, 4> Idxs;
  for (unsigned I = 0; I < NumIndices; ++I) {
    if (!Indices[I])
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
  if (!M)
    return 1;
  std::string Err;
  raw_string_ostream OS(Err);
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
  if (!I)
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (!Inst || !Inst->isTerminator())
    return 0;
  return Inst->getNumSuccessors();
}

static NevercBasicBlockRef bridgeInstGetSuccessor(NevercValueRef I,
                                                  unsigned Idx) {
  if (!I)
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (!Inst || !Inst->isTerminator() || Idx >= Inst->getNumSuccessors())
    return nullptr;
  return wrapBB(Inst->getSuccessor(Idx));
}

// ===----------------------------------------------------------------------===
//  AllocaInst source element type
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeAllocaGetAllocatedType(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *AI = dyn_cast<AllocaInst>(unwrapV(I));
  return AI ? wrapTy(AI->getAllocatedType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  Global variable alignment
// ===----------------------------------------------------------------------===

static unsigned bridgeGlobalGetAlignment(NevercValueRef GV) {
  if (!GV)
    return 0;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (!G)
    return 0;
  MaybeAlign A = G->getAlign();
  return A ? A->value() : 0;
}

static void bridgeGlobalSetAlignment(NevercValueRef GV, unsigned Align) {
  if (!GV)
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
  if (!BB)
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
  if (!BB)
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
  return (C && C->isNullValue()) ? 1 : 0;
}

static int bridgeValueIsSameAs(NevercValueRef A, NevercValueRef B) {
  if (!A || !B)
    return (!A && !B) ? 1 : 0;
  return (unwrapV(A) == unwrapV(B)) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  PHI node access
// ===----------------------------------------------------------------------===

static unsigned bridgePhiGetNumIncoming(NevercValueRef Phi) {
  if (!Phi)
    return 0;
  auto *PN = dyn_cast<PHINode>(unwrapV(Phi));
  return PN ? PN->getNumIncomingValues() : 0;
}

static NevercValueRef bridgePhiGetIncomingValue(NevercValueRef Phi,
                                                unsigned Idx) {
  if (!Phi)
    return nullptr;
  auto *PN = dyn_cast<PHINode>(unwrapV(Phi));
  if (!PN || Idx >= PN->getNumIncomingValues())
    return nullptr;
  return wrapV(PN->getIncomingValue(Idx));
}

static NevercBasicBlockRef bridgePhiGetIncomingBlock(NevercValueRef Phi,
                                                     unsigned Idx) {
  if (!Phi)
    return nullptr;
  auto *PN = dyn_cast<PHINode>(unwrapV(Phi));
  if (!PN || Idx >= PN->getNumIncomingValues())
    return nullptr;
  return wrapBB(PN->getIncomingBlock(Idx));
}

// ===----------------------------------------------------------------------===
//  SelectInst operand access
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeSelectGetCondition(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *SI = dyn_cast<SelectInst>(unwrapV(I));
  return SI ? wrapV(SI->getCondition()) : nullptr;
}

static NevercValueRef bridgeSelectGetTrueValue(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *SI = dyn_cast<SelectInst>(unwrapV(I));
  return SI ? wrapV(SI->getTrueValue()) : nullptr;
}

static NevercValueRef bridgeSelectGetFalseValue(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *SI = dyn_cast<SelectInst>(unwrapV(I));
  return SI ? wrapV(SI->getFalseValue()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  ReturnInst operand access
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeReturnGetValue(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *RI = dyn_cast<ReturnInst>(unwrapV(I));
  return (RI && RI->getReturnValue()) ? wrapV(RI->getReturnValue()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  CastInst type access
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeCastGetSrcTy(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *CI = dyn_cast<CastInst>(unwrapV(I));
  return CI ? wrapTy(CI->getSrcTy()) : nullptr;
}

static NevercTypeRef bridgeCastGetDestTy(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *CI = dyn_cast<CastInst>(unwrapV(I));
  return CI ? wrapTy(CI->getDestTy()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  GEP source element type
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeGEPGetSourceElementType(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *GEP = dyn_cast<GetElementPtrInst>(unwrapV(I));
  return GEP ? wrapTy(GEP->getSourceElementType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  ICmp/FCmp predicate access
// ===----------------------------------------------------------------------===

static unsigned bridgeCmpGetPredicate(NevercValueRef I) {
  if (!I)
    return 0;
  auto *CI = dyn_cast<CmpInst>(unwrapV(I));
  return CI ? static_cast<unsigned>(CI->getPredicate()) : 0;
}

static int bridgeInstIsICmp(NevercValueRef I) {
  return (I && isa<ICmpInst>(unwrapV(I))) ? 1 : 0;
}

static int bridgeInstIsFCmp(NevercValueRef I) {
  return (I && isa<FCmpInst>(unwrapV(I))) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  BasicBlock name
// ===----------------------------------------------------------------------===

static const char *bridgeBBGetName(NevercBasicBlockRef BB) {
  if (!BB)
    return "";
  auto Name = unwrapBB(BB)->getName();
  return Name.data() ? Name.data() : "";
}

// ===----------------------------------------------------------------------===
//  Function BasicBlock count
// ===----------------------------------------------------------------------===

static unsigned bridgeFunctionGetBBCount(NevercValueRef F) {
  if (!F)
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  return Fn ? Fn->size() : 0;
}

// ===----------------------------------------------------------------------===
//  Reverse iteration
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeInstGetPrevInst(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (!Inst || !Inst->getParent())
    return nullptr;
  auto It = Inst->getIterator();
  if (It == Inst->getParent()->begin())
    return nullptr;
  return wrapV(&*std::prev(It));
}

static NevercBasicBlockRef bridgeFunctionGetPrevBB(NevercBasicBlockRef BB) {
  if (!BB)
    return nullptr;
  auto *Block = unwrapBB(BB);
  if (!Block->getParent())
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
  if (!T)
    return nullptr;
  auto *ST = dyn_cast<StructType>(unwrapTy(T));
  if (!ST || !ST->hasName())
    return nullptr;
  return ST->getName().data();
}

// ===----------------------------------------------------------------------===
//  SwitchInst inspection
// ===----------------------------------------------------------------------===

static unsigned bridgeSwitchGetNumCases(NevercValueRef I) {
  if (!I)
    return 0;
  auto *SI = dyn_cast<SwitchInst>(unwrapV(I));
  return SI ? SI->getNumCases() : 0;
}

static NevercValueRef bridgeSwitchGetCaseValue(NevercValueRef I, unsigned Idx) {
  if (!I)
    return nullptr;
  auto *SI = dyn_cast<SwitchInst>(unwrapV(I));
  if (!SI || Idx >= SI->getNumCases())
    return nullptr;
  auto It = SI->case_begin();
  std::advance(It, Idx);
  return wrapV(It->getCaseValue());
}

static NevercBasicBlockRef bridgeSwitchGetCaseSuccessor(NevercValueRef I,
                                                        unsigned Idx) {
  if (!I)
    return nullptr;
  auto *SI = dyn_cast<SwitchInst>(unwrapV(I));
  if (!SI || Idx >= SI->getNumCases())
    return nullptr;
  auto It = SI->case_begin();
  std::advance(It, Idx);
  return wrapBB(It->getCaseSuccessor());
}

static NevercBasicBlockRef bridgeSwitchGetDefaultDest(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *SI = dyn_cast<SwitchInst>(unwrapV(I));
  return SI ? wrapBB(SI->getDefaultDest()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  MIR BasicBlock navigation by index
// ===----------------------------------------------------------------------===

static NevercMachineBBRef bridgeMBBGetSuccessor(NevercMachineBBRef MBB,
                                                unsigned Idx) {
  if (!MBB)
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
  if (!MBB)
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
  if (!M)
    return "";
  return unwrap(M)->getSourceFileName().c_str();
}

// ===----------------------------------------------------------------------===
//  InvokeInst support
// ===----------------------------------------------------------------------===

static int bridgeInstIsInvoke(NevercValueRef I) {
  return (I && isa<InvokeInst>(unwrapV(I))) ? 1 : 0;
}

static NevercBasicBlockRef bridgeInvokeGetNormalDest(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *II = dyn_cast<InvokeInst>(unwrapV(I));
  return II ? wrapBB(II->getNormalDest()) : nullptr;
}

static NevercBasicBlockRef bridgeInvokeGetUnwindDest(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *II = dyn_cast<InvokeInst>(unwrapV(I));
  return II ? wrapBB(II->getUnwindDest()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  BasicBlock splitting
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef bridgeBBSplitBefore(NevercBasicBlockRef BB,
                                                NevercValueRef Inst) {
  if (!BB || !Inst)
    return nullptr;
  auto *Block = unwrapBB(BB);
  auto *I = dyn_cast<Instruction>(unwrapV(Inst));
  if (!I || I->getParent() != Block)
    return nullptr;
  // splitBasicBlock asserts the BB has a terminator and I != end().
  if (!Block->getTerminator()) {
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
  if (!B)
    return nullptr;
  auto *BB = unwrapB(B)->GetInsertBlock();
  return BB ? wrapBB(BB) : nullptr;
}

// ===----------------------------------------------------------------------===
//  ConstantInt value extraction
// ===----------------------------------------------------------------------===

static uint64_t bridgeConstIntGetZExtValue(NevercValueRef V) {
  if (!V)
    return 0;
  auto *CI = dyn_cast<ConstantInt>(unwrapV(V));
  return CI ? CI->getZExtValue() : 0;
}

static int64_t bridgeConstIntGetSExtValue(NevercValueRef V) {
  if (!V)
    return 0;
  auto *CI = dyn_cast<ConstantInt>(unwrapV(V));
  return CI ? CI->getSExtValue() : 0;
}

static int bridgeValueIsConstantInt(NevercValueRef V) {
  return (V && isa<ConstantInt>(unwrapV(V))) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  GlobalVariable value type
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeGlobalGetValueType(NevercValueRef GV) {
  if (!GV)
    return nullptr;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  return G ? wrapTy(G->getValueType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  Function body removal
// ===----------------------------------------------------------------------===

static void bridgeFunctionDeleteBody(NevercValueRef F) {
  if (!F)
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (Fn)
    Fn->deleteBody();
}

// ===----------------------------------------------------------------------===
//  Value undef check
// ===----------------------------------------------------------------------===

static int bridgeValueIsUndef(NevercValueRef V) {
  return (V && isa<UndefValue>(unwrapV(V))) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  Instruction operand as BasicBlock
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef bridgeInstGetOperandAsBB(NevercValueRef I,
                                                    unsigned Idx) {
  if (!I)
    return nullptr;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (!Inst || Idx >= Inst->getNumOperands())
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
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateFPExt(unwrapV(V), unwrapTy(DestTy), Name ? Name : ""));
}

static NevercValueRef bridgeBuildFPTrunc(NevercBuilderRef B, NevercValueRef V,
                                         NevercTypeRef DestTy,
                                         const char *Name) {
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(unwrapB(B)->CreateFPTrunc(unwrapV(V), unwrapTy(DestTy),
                                         Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  LoadInst source type
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeLoadGetType(NevercValueRef I) {
  if (!I)
    return nullptr;
  auto *LI = dyn_cast<LoadInst>(unwrapV(I));
  return LI ? wrapTy(LI->getType()) : nullptr;
}

// ===----------------------------------------------------------------------===
//  GEP inbounds query
// ===----------------------------------------------------------------------===

static int bridgeGEPIsInBounds(NevercValueRef I) {
  if (!I)
    return 0;
  auto *GEP = dyn_cast<GetElementPtrInst>(unwrapV(I));
  return (GEP && GEP->isInBounds()) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  CallBase convenience (matches call, invoke, callbr)
// ===----------------------------------------------------------------------===

static int bridgeInstIsCallLike(NevercValueRef I) {
  return (I && isa<CallBase>(unwrapV(I))) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  Load/Store alignment & volatile
// ===----------------------------------------------------------------------===

static unsigned bridgeLoadGetAlignment(NevercValueRef I) {
  if (!I)
    return 0;
  auto *LI = dyn_cast<LoadInst>(unwrapV(I));
  return LI ? LI->getAlign().value() : 0;
}

static unsigned bridgeStoreGetAlignment(NevercValueRef I) {
  if (!I)
    return 0;
  auto *SI = dyn_cast<StoreInst>(unwrapV(I));
  return SI ? SI->getAlign().value() : 0;
}

static int bridgeLoadIsVolatile(NevercValueRef I) {
  if (!I)
    return 0;
  auto *LI = dyn_cast<LoadInst>(unwrapV(I));
  return (LI && LI->isVolatile()) ? 1 : 0;
}

static int bridgeStoreIsVolatile(NevercValueRef I) {
  if (!I)
    return 0;
  auto *SI = dyn_cast<StoreInst>(unwrapV(I));
  return (SI && SI->isVolatile()) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  Builder: insert before terminator
// ===----------------------------------------------------------------------===

static void bridgeBuilderSetInsertPointBeforeTerminator(NevercBuilderRef B,
                                                        NevercBasicBlockRef BB) {
  if (!B || !BB)
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
  if (!I)
    return 0;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  if (!CB)
    return 0;
  auto *Callee = CB->getCalledFunction();
  return (Callee && Callee->isIntrinsic()) ? 1 : 0;
}

static unsigned bridgeCallGetIntrinsicID(NevercValueRef I) {
  if (!I)
    return 0;
  auto *CB = dyn_cast<CallBase>(unwrapV(I));
  if (!CB)
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
  if (!B || !Ty || !Ptr)
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
  if (!B || !Val || !Ptr)
    return nullptr;
  return wrapV(unwrapB(B)->CreateAlignedStore(
      unwrapV(Val), unwrapV(Ptr), MaybeAlign(AlignVal), IsVolatile != 0));
}

// ===----------------------------------------------------------------------===
//  Debug location
// ===----------------------------------------------------------------------===

static void bridgeInstCopyDebugLoc(NevercValueRef Dst, NevercValueRef Src) {
  if (!Dst || !Src)
    return;
  auto *DstI = dyn_cast<Instruction>(unwrapV(Dst));
  auto *SrcI = dyn_cast<Instruction>(unwrapV(Src));
  if (DstI && SrcI)
    DstI->setDebugLoc(SrcI->getDebugLoc());
}

static int bridgeInstHasDebugLoc(NevercValueRef I) {
  if (!I)
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  return (Inst && Inst->getDebugLoc()) ? 1 : 0;
}

static unsigned bridgeInstGetDebugLocLine(NevercValueRef I) {
  if (!I)
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (!Inst)
    return 0;
  const auto &DL = Inst->getDebugLoc();
  return DL ? DL.getLine() : 0;
}

static unsigned bridgeInstGetDebugLocCol(NevercValueRef I) {
  if (!I)
    return 0;
  auto *Inst = dyn_cast<Instruction>(unwrapV(I));
  if (!Inst)
    return 0;
  const auto &DL = Inst->getDebugLoc();
  return DL ? DL.getCol() : 0;
}

// ===----------------------------------------------------------------------===
//  GlobalObject section (works on both Function and GlobalVariable)
// ===----------------------------------------------------------------------===

static const char *bridgeGlobalGetSection(NevercValueRef GV) {
  if (!GV)
    return "";
  auto *GO = dyn_cast<GlobalObject>(unwrapV(GV));
  if (!GO)
    return "";
  StringRef Sec = GO->getSection();
  return Sec.data() ? Sec.data() : "";
}

static void bridgeGlobalSetSection(NevercValueRef GV, const char *Section) {
  if (!GV)
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
  if (!F || !Kind)
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn || ParamIdx >= Fn->arg_size())
    return;
  Fn->addParamAttr(ParamIdx, Attribute::get(Fn->getContext(), Kind,
                                            Val ? Val : ""));
}

static int bridgeFunctionHasParamAttr(NevercValueRef F, unsigned ParamIdx,
                                      const char *Kind) {
  if (!F || !Kind)
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn || ParamIdx >= Fn->arg_size())
    return 0;
  return Fn->getAttributes()
                 .hasParamAttr(ParamIdx, Kind)
             ? 1
             : 0;
}

// ===----------------------------------------------------------------------===
//  BasicBlock instruction count
// ===----------------------------------------------------------------------===

static unsigned bridgeBBGetInstCount(NevercBasicBlockRef BB) {
  if (!BB)
    return 0;
  return unwrapBB(BB)->size();
}

// ===----------------------------------------------------------------------===
//  Module alias iteration
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeModuleGetFirstAlias(NevercModuleRef M) {
  if (!M)
    return nullptr;
  auto *Mod = unwrap(M);
  if (Mod->alias_empty())
    return nullptr;
  return wrapV(&*Mod->alias_begin());
}

static NevercValueRef bridgeModuleGetNextAlias(NevercValueRef A) {
  if (!A)
    return nullptr;
  auto *GA = dyn_cast<GlobalAlias>(unwrapV(A));
  if (!GA || !GA->getParent())
    return nullptr;
  auto It = std::next(GA->getIterator());
  if (It == GA->getParent()->alias_end())
    return nullptr;
  return wrapV(&*It);
}

static NevercValueRef bridgeModuleGetLastAlias(NevercModuleRef M) {
  if (!M)
    return nullptr;
  auto *Mod = unwrap(M);
  if (Mod->alias_empty())
    return nullptr;
  return wrapV(&*std::prev(Mod->alias_end()));
}

static NevercValueRef bridgeModuleGetPrevAlias(NevercValueRef A) {
  if (!A)
    return nullptr;
  auto *GA = dyn_cast<GlobalAlias>(unwrapV(A));
  if (!GA || !GA->getParent())
    return nullptr;
  auto It = GA->getIterator();
  if (It == GA->getParent()->alias_begin())
    return nullptr;
  return wrapV(&*std::prev(It));
}

static NevercValueRef bridgeModuleGetPrevFunction(NevercValueRef F) {
  if (!F)
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn || !Fn->getParent())
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
  if (!I)
    return nullptr;
  auto *GEP = dyn_cast<GetElementPtrInst>(unwrapV(I));
  if (!GEP || Idx >= GEP->getNumIndices())
    return nullptr;
  return wrapV(GEP->getOperand(Idx + 1));
}

// ===----------------------------------------------------------------------===
//  Reverse global iteration
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeModuleGetLastGlobal(NevercModuleRef M) {
  if (!M)
    return nullptr;
  auto *Mod = unwrap(M);
  if (Mod->global_empty())
    return nullptr;
  return wrapV(&*std::prev(Mod->global_end()));
}

static NevercValueRef bridgeModuleGetPrevGlobal(NevercValueRef G) {
  if (!G)
    return nullptr;
  auto *GV = dyn_cast<GlobalVariable>(unwrapV(G));
  if (!GV || !GV->getParent())
    return nullptr;
  auto It = GV->getIterator();
  if (It == GV->getParent()->global_begin())
    return nullptr;
  return wrapV(&*std::prev(It));
}

// ===----------------------------------------------------------------------===
//  Text representation — returns host-allocated string, caller must Free()
// ===----------------------------------------------------------------------===

static char *bridgeValuePrintToString(NevercValueRef V) {
  if (!V)
    return nullptr;
  std::string Buf;
  raw_string_ostream OS(Buf);
  unwrapV(V)->print(OS, /*IsForDebug=*/true);
  char *Result = static_cast<char *>(bridgeAlloc(Buf.size() + 1));
  if (!Result)
    return nullptr;
  std::memcpy(Result, Buf.data(), Buf.size());
  Result[Buf.size()] = '\0';
  return Result;
}

static char *bridgeTypePrintToString(NevercTypeRef T) {
  if (!T)
    return nullptr;
  std::string Buf;
  raw_string_ostream OS(Buf);
  unwrapTy(T)->print(OS);
  char *Result = static_cast<char *>(bridgeAlloc(Buf.size() + 1));
  if (!Result)
    return nullptr;
  std::memcpy(Result, Buf.data(), Buf.size());
  Result[Buf.size()] = '\0';
  return Result;
}

// ===----------------------------------------------------------------------===
//  Additional Value kind queries
// ===----------------------------------------------------------------------===

static int bridgeValueIsConstantExpr(NevercValueRef V) {
  return (V && isa<ConstantExpr>(unwrapV(V))) ? 1 : 0;
}

static int bridgeValueIsAlias(NevercValueRef V) {
  return (V && isa<GlobalAlias>(unwrapV(V))) ? 1 : 0;
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
  std::string EntrySymbol;
};
ShellcodeModeState &shellcodeModeStorage() {
  static ShellcodeModeState S;
  return S;
}
} // namespace

void setShellcodeModeState(bool Enabled, llvm::StringRef EntrySymbol) {
  auto &S = shellcodeModeStorage();
  S.Enabled = Enabled;
  S.EntrySymbol = EntrySymbol.str();
}

static int bridgeHostIsShellcodeMode(void) {
  return shellcodeModeStorage().Enabled ? 1 : 0;
}

static const char *bridgeHostGetShellcodeEntrySymbol(void) {
  const auto &S = shellcodeModeStorage();
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
  StringMap<std::string> Args;
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
    size_t Eq = A.find('=');
    if (Eq == std::string::npos) {
      Store.Args[A] = "";
    } else {
      Store.Args[A.substr(0, Eq)] = A.substr(Eq + 1);
    }
  }
}

static const char *bridgePluginGetArg(const char *Key) {
  if (!Key)
    return nullptr;
  const auto &Args = pluginArgStorage().Args;
  auto It = Args.find(Key);
  if (It == Args.end())
    return nullptr;
  return It->second.c_str();
}

static int bridgePluginHasArg(const char *Key) {
  if (!Key)
    return 0;
  return pluginArgStorage().Args.count(Key) ? 1 : 0;
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
  if (!M || IntrinsicID == 0)
    return nullptr;
  auto *Mod = unwrap(M);
  SmallVector<Type *, 4> Tys;
  for (unsigned I = 0; I < NumTys; ++I) {
    if (!OverloadTys || !OverloadTys[I])
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
  if (!Buf)
    return nullptr;
  std::memcpy(Buf, Name.data(), Name.size());
  Buf[Name.size()] = '\0';
  return Buf;
}

static int bridgeIntrinsicIsOverloaded(unsigned IntrinsicID) {
  if (IntrinsicID == 0)
    return 0;
  return Intrinsic::isOverloaded(static_cast<Intrinsic::ID>(IntrinsicID)) ? 1
                                                                          : 0;
}

static unsigned bridgeIntrinsicLookupByName(const char *Name) {
  if (!Name)
    return 0;
  auto ID = Function::lookupIntrinsicID(Name);
  return static_cast<unsigned>(ID);
}

// ===----------------------------------------------------------------------===
//  NamedMetadata
// ===----------------------------------------------------------------------===

static NevercNamedMDRef bridgeModuleGetNamedMetadata(NevercModuleRef M,
                                                      const char *Name) {
  if (!M || !Name)
    return nullptr;
  return wrapNMD(unwrap(M)->getNamedMetadata(Name));
}

static NevercNamedMDRef bridgeModuleGetOrInsertNamedMetadata(NevercModuleRef M,
                                                              const char *Name) {
  if (!M || !Name)
    return nullptr;
  return wrapNMD(unwrap(M)->getOrInsertNamedMetadata(Name));
}

static unsigned bridgeNamedMDGetNumOperands(NevercNamedMDRef NMD) {
  if (!NMD)
    return 0;
  return unwrapNMD(NMD)->getNumOperands();
}

static NevercMetadataRef bridgeNamedMDGetOperand(NevercNamedMDRef NMD,
                                                  unsigned Idx) {
  if (!NMD)
    return nullptr;
  auto *Node = unwrapNMD(NMD);
  if (Idx >= Node->getNumOperands())
    return nullptr;
  return wrapMD(Node->getOperand(Idx));
}

static void bridgeNamedMDAddOperand(NevercNamedMDRef NMD,
                                     NevercMetadataRef MD) {
  if (!NMD || !MD)
    return;
  auto *Node = dyn_cast<MDNode>(unwrapMD(MD));
  if (!Node) {
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
  if (!C || !MD)
    return nullptr;
  return wrapV(MetadataAsValue::get(*unwrapCtx(C), unwrapMD(MD)));
}

static NevercMetadataRef bridgeValueAsMetadata(NevercValueRef V) {
  if (!V)
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
  if (!BB || !AfterBB)
    return;
  auto *Block = unwrapBB(BB);
  auto *After = unwrapBB(AfterBB);
  if (Block->getParent() != After->getParent())
    return;
  Block->moveAfter(After);
}

static void bridgeBBMoveBefore(NevercBasicBlockRef BB,
                                NevercBasicBlockRef BeforeBB) {
  if (!BB || !BeforeBB)
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
  if (!B || !Dst || !Src || !Len)
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
  if (!B || !Dst || !Val || !Len)
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
  if (!B || !Dst || !Src || !Len)
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
  if (!M || !T)
    return 0;
  auto *Ty = unwrapTy(T);
  if (!Ty->isSized())
    return 0;
  return unwrap(M)->getDataLayout().getTypeSizeInBits(Ty);
}

static uint64_t bridgeTypeAllocSize(NevercModuleRef M, NevercTypeRef T) {
  if (!M || !T)
    return 0;
  auto *Ty = unwrapTy(T);
  if (!Ty->isSized())
    return 0;
  return unwrap(M)->getDataLayout().getTypeAllocSize(Ty).getFixedValue();
}

static uint64_t bridgeTypeStoreSize(NevercModuleRef M, NevercTypeRef T) {
  if (!M || !T)
    return 0;
  auto *Ty = unwrapTy(T);
  if (!Ty->isSized())
    return 0;
  return unwrap(M)->getDataLayout().getTypeStoreSize(Ty).getFixedValue();
}

static unsigned bridgeTypeABIAlignment(NevercModuleRef M, NevercTypeRef T) {
  if (!M || !T)
    return 0;
  auto *Ty = unwrapTy(T);
  if (!Ty->isSized())
    return 0;
  return unwrap(M)->getDataLayout().getABITypeAlign(Ty).value();
}

static unsigned bridgeTypePrefAlignment(NevercModuleRef M, NevercTypeRef T) {
  if (!M || !T)
    return 0;
  auto *Ty = unwrapTy(T);
  if (!Ty->isSized())
    return 0;
  return unwrap(M)->getDataLayout().getPrefTypeAlign(Ty).value();
}

static unsigned bridgePointerSizeInBits(NevercModuleRef M,
                                        unsigned AddrSpace) {
  if (!M)
    return 0;
  return unwrap(M)->getDataLayout().getPointerSizeInBits(AddrSpace);
}

// ===----------------------------------------------------------------------===
//  PoisonValue
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeConstPoison(NevercTypeRef Ty) {
  if (!Ty)
    return nullptr;
  return wrapV(PoisonValue::get(unwrapTy(Ty)));
}

static int bridgeValueIsPoison(NevercValueRef V) {
  return (V && isa<PoisonValue>(unwrapV(V))) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  Address-space-aware pointer types
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeTypeGetPtrInAddrSpace(NevercContextRef C,
                                                  unsigned AddrSpace) {
  if (!C)
    return nullptr;
  return wrapTy(PointerType::get(*unwrapCtx(C), AddrSpace));
}

static unsigned bridgeTypeGetPointerAddrSpace(NevercTypeRef T) {
  if (!T)
    return 0;
  auto *PT = dyn_cast<PointerType>(unwrapTy(T));
  return PT ? PT->getAddressSpace() : 0;
}

// ===----------------------------------------------------------------------===
//  Vector types
// ===----------------------------------------------------------------------===

static NevercTypeRef bridgeTypeGetFixedVector(NevercTypeRef ElemTy,
                                               unsigned Count) {
  if (!ElemTy || Count == 0)
    return nullptr;
  return wrapTy(FixedVectorType::get(unwrapTy(ElemTy), Count));
}

static int bridgeTypeIsVector(NevercTypeRef T) {
  return (T && unwrapTy(T)->isVectorTy()) ? 1 : 0;
}

static unsigned bridgeTypeGetVectorNumElements(NevercTypeRef T) {
  if (!T)
    return 0;
  auto *VT = dyn_cast<FixedVectorType>(unwrapTy(T));
  return VT ? VT->getNumElements() : 0;
}

static NevercTypeRef bridgeTypeGetVectorElementType(NevercTypeRef T) {
  if (!T)
    return nullptr;
  auto *VT = dyn_cast<VectorType>(unwrapTy(T));
  return VT ? wrapTy(VT->getElementType()) : nullptr;
}

static NevercValueRef bridgeConstVector(NevercValueRef *Vals,
                                         unsigned Count) {
  if (!Vals || Count == 0)
    return nullptr;
  SmallVector<Constant *, 8> Elems;
  for (unsigned I = 0; I < Count; ++I) {
    if (!Vals[I])
      return nullptr;
    auto *C = dyn_cast<Constant>(unwrapV(Vals[I]));
    if (!C)
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
  if (!B || !Vec || !Idx)
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
  if (!B || !Vec || !Val || !Idx)
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
  if (!B || !V1 || !V2 || !MaskVals || MaskLen == 0)
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
  if (!B || !V)
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
  if (!B || !V || !DestTy)
    return nullptr;
  return wrapV(
      unwrapB(B)->CreateAddrSpaceCast(unwrapV(V), unwrapTy(DestTy),
                                      Name ? Name : ""));
}

// ===----------------------------------------------------------------------===
//  Aggregate zero initializer
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeConstZeroInitializer(NevercTypeRef Ty) {
  if (!Ty)
    return nullptr;
  return wrapV(ConstantAggregateZero::get(unwrapTy(Ty)));
}

// ===----------------------------------------------------------------------===
//  GlobalValue unnamed_addr
// ===----------------------------------------------------------------------===

static int bridgeGlobalHasUnnamedAddr(NevercValueRef GV) {
  if (!GV)
    return 0;
  auto *G = dyn_cast<GlobalValue>(unwrapV(GV));
  return (G && G->hasGlobalUnnamedAddr()) ? 1 : 0;
}

static void bridgeGlobalSetUnnamedAddr(NevercValueRef GV,
                                       int HasUnnamedAddr) {
  if (!GV)
    return;
  auto *G = dyn_cast<GlobalValue>(unwrapV(GV));
  if (G)
    G->setUnnamedAddr(HasUnnamedAddr ? GlobalValue::UnnamedAddr::Global
                                     : GlobalValue::UnnamedAddr::None);
}

// ===----------------------------------------------------------------------===
//  String utilities
// ===----------------------------------------------------------------------===

static uint64_t bridgeStrLen(const char *S) {
  if (!S)
    return 0;
  return std::strlen(S);
}

static char *bridgeStrDup(const char *S) {
  if (!S)
    return nullptr;
  size_t Len = std::strlen(S);
  char *Buf = static_cast<char *>(std::malloc(Len + 1));
  if (!Buf)
    return nullptr;
  std::memcpy(Buf, S, Len + 1);
  return Buf;
}

static char *bridgeStrConcat(const char *A, const char *B) {
  size_t LenA = A ? std::strlen(A) : 0;
  size_t LenB = B ? std::strlen(B) : 0;
  char *Buf = static_cast<char *>(std::malloc(LenA + LenB + 1));
  if (!Buf)
    return nullptr;
  if (LenA)
    std::memcpy(Buf, A, LenA);
  if (LenB)
    std::memcpy(Buf + LenA, B, LenB);
  Buf[LenA + LenB] = '\0';
  return Buf;
}

static int bridgeStrEqual(const char *A, const char *B) {
  if (A == B)
    return 1;
  if (!A || !B)
    return 0;
  return std::strcmp(A, B) == 0 ? 1 : 0;
}

static char *bridgeIntToStr(int64_t Val) {
  char Tmp[32];
  int Len = std::snprintf(Tmp, sizeof(Tmp), "%" PRId64, Val);
  if (Len < 0)
    return nullptr;
  char *Buf = static_cast<char *>(std::malloc(Len + 1));
  if (!Buf)
    return nullptr;
  std::memcpy(Buf, Tmp, Len + 1);
  return Buf;
}

static char *bridgeUIntToStr(uint64_t Val) {
  char Tmp[32];
  int Len = std::snprintf(Tmp, sizeof(Tmp), "%" PRIu64, Val);
  if (Len < 0)
    return nullptr;
  char *Buf = static_cast<char *>(std::malloc(Len + 1));
  if (!Buf)
    return nullptr;
  std::memcpy(Buf, Tmp, Len + 1);
  return Buf;
}

static char *bridgeStrFormatV(const char *Fmt, va_list Args) {
  if (!Fmt)
    return nullptr;
  char Stack[256];
  va_list ArgsCopy;
  va_copy(ArgsCopy, Args);
  int Len = std::vsnprintf(Stack, sizeof(Stack), Fmt, ArgsCopy);
  va_end(ArgsCopy);
  if (Len < 0)
    return nullptr;
  char *Buf = static_cast<char *>(std::malloc(Len + 1));
  if (!Buf)
    return nullptr;
  if (static_cast<size_t>(Len) < sizeof(Stack))
    std::memcpy(Buf, Stack, Len + 1);
  else
    std::vsnprintf(Buf, Len + 1, Fmt, Args);
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
  if (Dst && Src && Len)
    std::memcpy(Dst, Src, static_cast<size_t>(Len));
}

static void bridgeMemSet(void *Dst, int Val, uint64_t Len) {
  if (Dst && Len)
    std::memset(Dst, Val, static_cast<size_t>(Len));
}

static void bridgeMemMove(void *Dst, const void *Src, uint64_t Len) {
  if (Dst && Src && Len)
    std::memmove(Dst, Src, static_cast<size_t>(Len));
}

static int bridgeMemCompare(const void *A, const void *B, uint64_t Len) {
  if (!A || !B || !Len)
    return 0;
  return std::memcmp(A, B, static_cast<size_t>(Len));
}

// ===----------------------------------------------------------------------===
//  NSW/NUW arithmetic
// ===----------------------------------------------------------------------===

#define BRIDGE_BINOP_FLAG(NAME, METHOD)                                        \
  static NevercValueRef bridge##NAME(NevercBuilderRef B, NevercValueRef LHS,   \
                                     NevercValueRef RHS, const char *Name) {   \
    if (!B || !LHS || !RHS)                                                    \
      return nullptr;                                                          \
    return wrapV(unwrapB(B)->METHOD(unwrapV(LHS), unwrapV(RHS), Name));        \
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
  if (!B || !V)
    return nullptr;
  return wrapV(unwrapB(B)->CreateNSWNeg(unwrapV(V), Name));
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
  if (!B || !FnTy || !Fn || !NormalDest || !UnwindDest)
    return nullptr;
  SmallVector<Value *, 8> ArgVec;
  for (unsigned I = 0; I < ArgCount; ++I)
    ArgVec.push_back(unwrapV(Args[I]));
  auto *FTy = dyn_cast<FunctionType>(unwrapTy(FnTy));
  if (!FTy)
    return nullptr;
  return wrapV(unwrapB(B)->CreateInvoke(FTy, unwrapV(Fn), unwrapBB(NormalDest),
                                        unwrapBB(UnwindDest), ArgVec, Name));
}

static NevercValueRef bridgeBuildLandingPad(NevercBuilderRef B,
                                            NevercTypeRef Ty,
                                            unsigned NumClauses,
                                            const char *Name) {
  if (!B || !Ty)
    return nullptr;
  return wrapV(unwrapB(B)->CreateLandingPad(unwrapTy(Ty), NumClauses, Name));
}

static void bridgeLandingPadAddClause(NevercValueRef LPad,
                                      NevercValueRef ClauseVal) {
  if (!LPad || !ClauseVal)
    return;
  auto *LP = dyn_cast<LandingPadInst>(unwrapV(LPad));
  if (LP)
    LP->addClause(cast<Constant>(unwrapV(ClauseVal)));
}

static void bridgeLandingPadSetCleanup(NevercValueRef LPad, int IsCleanup) {
  if (!LPad)
    return;
  auto *LP = dyn_cast<LandingPadInst>(unwrapV(LPad));
  if (LP)
    LP->setCleanup(IsCleanup != 0);
}

static NevercValueRef bridgeBuildResume(NevercBuilderRef B,
                                        NevercValueRef Val) {
  if (!B || !Val)
    return nullptr;
  return wrapV(unwrapB(B)->CreateResume(unwrapV(Val)));
}

// ===----------------------------------------------------------------------===
//  Thread-local globals
// ===----------------------------------------------------------------------===

static int bridgeGlobalIsThreadLocal(NevercValueRef GV) {
  if (!GV)
    return 0;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  return (G && G->isThreadLocal()) ? 1 : 0;
}

static void bridgeGlobalSetThreadLocal(NevercValueRef GV, int IsThreadLocal) {
  if (!GV)
    return;
  auto *G = dyn_cast<GlobalVariable>(unwrapV(GV));
  if (G)
    G->setThreadLocal(IsThreadLocal != 0);
}

// ===----------------------------------------------------------------------===
//  Function enum attributes
// ===----------------------------------------------------------------------===

static void bridgeFunctionAddEnumAttr(NevercValueRef F, unsigned AttrKind) {
  if (!F)
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (Fn && AttrKind < Attribute::EndAttrKinds)
    Fn->addFnAttr(static_cast<Attribute::AttrKind>(AttrKind));
}

static int bridgeFunctionHasEnumAttr(NevercValueRef F, unsigned AttrKind) {
  if (!F)
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (!Fn || AttrKind >= Attribute::EndAttrKinds)
    return 0;
  return Fn->hasFnAttribute(static_cast<Attribute::AttrKind>(AttrKind)) ? 1
                                                                        : 0;
}

static void bridgeFunctionRemoveEnumAttr(NevercValueRef F, unsigned AttrKind) {
  if (!F)
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
  if (!M || !Name)
    return nullptr;
  return wrapComdat(unwrap(M)->getOrInsertComdat(Name));
}

static unsigned bridgeComdatGetSelectionKind(NevercComdatRef C) {
  if (!C)
    return 0;
  return static_cast<unsigned>(unwrapComdat(C)->getSelectionKind());
}

static void bridgeComdatSetSelectionKind(NevercComdatRef C, unsigned Kind) {
  if (!C)
    return;
  unwrapComdat(C)->setSelectionKind(static_cast<Comdat::SelectionKind>(Kind));
}

static void bridgeGlobalSetComdat(NevercValueRef GV, NevercComdatRef C) {
  if (!GV)
    return;
  auto *GO = dyn_cast<GlobalObject>(unwrapV(GV));
  if (GO)
    GO->setComdat(unwrapComdat(C));
}

static NevercComdatRef bridgeGlobalGetComdat(NevercValueRef GV) {
  if (!GV)
    return nullptr;
  auto *GO = dyn_cast<GlobalObject>(unwrapV(GV));
  if (!GO)
    return nullptr;
  return wrapComdat(const_cast<Comdat *>(GO->getComdat()));
}

// ===----------------------------------------------------------------------===
//  InstMoveAfter
// ===----------------------------------------------------------------------===

static void bridgeInstMoveAfter(NevercValueRef I, NevercValueRef After) {
  if (!I || !After)
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
  if (!Agg)
    return nullptr;
  auto *C = dyn_cast<Constant>(unwrapV(Agg));
  if (!C)
    return nullptr;
  Constant *Elem = C->getAggregateElement(Idx);
  return Elem ? wrapV(Elem) : nullptr;
}

// ===----------------------------------------------------------------------===
//  MIR extended navigation
// ===----------------------------------------------------------------------===

static NevercMachineInstrRef bridgeMBBGetLastInst2(NevercMachineBBRef MBB) {
  if (!MBB)
    return nullptr;
  auto *BB = unwrapMBB(MBB);
  if (BB->empty())
    return nullptr;
  return wrapMI(&BB->back());
}

static NevercMachineInstrRef bridgeMBBGetPrevInst(NevercMachineInstrRef MI) {
  if (!MI)
    return nullptr;
  auto *Inst = unwrapMI(MI);
  auto *MBB = Inst->getParent();
  if (!MBB)
    return nullptr;
  auto It = Inst->getIterator();
  if (It == MBB->begin())
    return nullptr;
  return wrapMI(&*std::prev(It));
}

static NevercMachineBBRef bridgeMFuncGetLastBB(NevercMachineFuncRef MF) {
  if (!MF)
    return nullptr;
  auto *Func = unwrapMF(MF);
  if (Func->empty())
    return nullptr;
  return wrapMBB(&Func->back());
}

static NevercMachineBBRef bridgeMFuncGetPrevBB(NevercMachineBBRef MBB) {
  if (!MBB)
    return nullptr;
  auto *BB = unwrapMBB(MBB);
  auto *MF = BB->getParent();
  if (!MF)
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
  if (!MI)
    return;
  auto *Inst = unwrapMI(MI);
  if (Idx < Inst->getNumOperands() && Inst->getOperand(Idx).isReg())
    Inst->getOperand(Idx).setReg(Register(Reg));
}

static void bridgeMInstSetOperandImm(NevercMachineInstrRef MI, unsigned Idx,
                                     int64_t Val) {
  if (!MI)
    return;
  auto *Inst = unwrapMI(MI);
  if (Idx < Inst->getNumOperands() && Inst->getOperand(Idx).isImm())
    Inst->getOperand(Idx).setImm(Val);
}

// ===----------------------------------------------------------------------===
//  MIR instruction flags & properties
// ===----------------------------------------------------------------------===

static unsigned bridgeMInstGetFlags(NevercMachineInstrRef MI) {
  if (!MI)
    return 0;
  return unwrapMI(MI)->getFlags();
}

static void bridgeMInstSetFlags(NevercMachineInstrRef MI, unsigned Flags) {
  if (!MI)
    return;
  unwrapMI(MI)->setFlags(Flags);
}

static int bridgeMInstIsBranch(NevercMachineInstrRef MI) {
  if (!MI)
    return 0;
  return unwrapMI(MI)->isBranch() ? 1 : 0;
}

static int bridgeMInstIsCall(NevercMachineInstrRef MI) {
  if (!MI)
    return 0;
  return unwrapMI(MI)->isCall() ? 1 : 0;
}

static int bridgeMInstIsReturn(NevercMachineInstrRef MI) {
  if (!MI)
    return 0;
  return unwrapMI(MI)->isReturn() ? 1 : 0;
}

static int bridgeMInstIsTerminator(NevercMachineInstrRef MI) {
  if (!MI)
    return 0;
  return unwrapMI(MI)->isTerminator() ? 1 : 0;
}

static int bridgeMInstIsMoveImmediate(NevercMachineInstrRef MI) {
  if (!MI)
    return 0;
  return unwrapMI(MI)->isMoveImmediate() ? 1 : 0;
}

static int bridgeMInstHasDelaySlot(NevercMachineInstrRef MI) {
  if (!MI)
    return 0;
  return unwrapMI(MI)->hasDelaySlot() ? 1 : 0;
}

static const char *bridgeMInstGetDesc(NevercMachineInstrRef MI) {
  if (!MI)
    return "";
  auto *Inst = unwrapMI(MI);
  auto *MBB = Inst->getParent();
  if (!MBB)
    return "";
  auto *MF = MBB->getParent();
  if (!MF)
    return "";
  const auto *TII = MF->getSubtarget().getInstrInfo();
  if (!TII)
    return "";
  return TII->getName(Inst->getOpcode()).data();
}

// ===----------------------------------------------------------------------===
//  MIR register queries
// ===----------------------------------------------------------------------===

static int bridgeMInstOperandIsVirtReg(NevercMachineInstrRef MI, unsigned Idx) {
  if (!MI)
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).getReg().isVirtual() ? 1 : 0;
}

static int bridgeMInstOperandIsPhysReg(NevercMachineInstrRef MI, unsigned Idx) {
  if (!MI)
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).getReg().isPhysical() ? 1 : 0;
}

static int bridgeMInstOperandIsDef(NevercMachineInstrRef MI, unsigned Idx) {
  if (!MI)
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).isDef() ? 1 : 0;
}

static int bridgeMInstOperandIsUse(NevercMachineInstrRef MI, unsigned Idx) {
  if (!MI)
    return 0;
  auto *Inst = unwrapMI(MI);
  if (Idx >= Inst->getNumOperands() || !Inst->getOperand(Idx).isReg())
    return 0;
  return Inst->getOperand(Idx).isUse() ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  MIR instruction movement & counting
// ===----------------------------------------------------------------------===

static void bridgeMInstMoveBefore(NevercMachineInstrRef MI,
                                  NevercMachineInstrRef Before) {
  if (!MI || !Before)
    return;
  auto *Inst = unwrapMI(MI);
  auto *BeforeInst = unwrapMI(Before);
  auto *MBB = BeforeInst->getParent();
  if (MBB)
    MBB->splice(BeforeInst->getIterator(), Inst->getParent(),
                Inst->getIterator());
}

static unsigned bridgeMBBGetInstCount(NevercMachineBBRef MBB) {
  if (!MBB)
    return 0;
  return static_cast<unsigned>(unwrapMBB(MBB)->size());
}

static unsigned bridgeMFuncGetBBCount(NevercMachineFuncRef MF) {
  if (!MF)
    return 0;
  return static_cast<unsigned>(unwrapMF(MF)->size());
}

// ===----------------------------------------------------------------------===
//  Linker API stubs
//  These are populated with real implementations when the linker backends
//  set up their per-invocation context.  During compilation (non-linking)
//  they return safe defaults (NULL / 0 / empty string).
// ===----------------------------------------------------------------------===

static NevercLinkerSymbolRef stubLinkGetFirstSymbol(void) { return nullptr; }
static NevercLinkerSymbolRef stubLinkGetNextSymbol(NevercLinkerSymbolRef) {
  return nullptr;
}
static NevercLinkerSymbolRef stubLinkFindSymbol(const char *) {
  return nullptr;
}
static const char *stubLinkSymbolGetName(NevercLinkerSymbolRef) { return ""; }
static uint64_t stubLinkSymbolGetValue(NevercLinkerSymbolRef) { return 0; }
static uint64_t stubLinkSymbolGetSize(NevercLinkerSymbolRef) { return 0; }
static int stubLinkSymbolIsDefined(NevercLinkerSymbolRef) { return 0; }
static int stubLinkSymbolIsLocal(NevercLinkerSymbolRef) { return 0; }
static int stubLinkSymbolIsHidden(NevercLinkerSymbolRef) { return 0; }
static void stubLinkSymbolSetVisibilityHidden(NevercLinkerSymbolRef, int) {}
static NevercLinkerSectionRef stubLinkGetFirstSection(void) { return nullptr; }
static NevercLinkerSectionRef stubLinkGetNextSection(NevercLinkerSectionRef) {
  return nullptr;
}
static NevercLinkerSectionRef stubLinkFindSection(const char *) {
  return nullptr;
}
static const char *stubLinkSectionGetName(NevercLinkerSectionRef) { return ""; }
static uint64_t stubLinkSectionGetSize(NevercLinkerSectionRef) { return 0; }
static uint64_t stubLinkSectionGetAlignment(NevercLinkerSectionRef) {
  return 0;
}
static unsigned stubLinkSectionGetFlags(NevercLinkerSectionRef) { return 0; }
static const char *stubLinkGetOutputPath(void) { return ""; }
static unsigned stubLinkGetOutputFormat(void) { return 0; }

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

  API.MBBGetLastInst = bridgeMBBGetLastInst2;
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

  API.RegisterLinkerPass = nullptr;

  API.LinkGetFirstSymbol = stubLinkGetFirstSymbol;
  API.LinkGetNextSymbol = stubLinkGetNextSymbol;
  API.LinkFindSymbol = stubLinkFindSymbol;
  API.LinkSymbolGetName = stubLinkSymbolGetName;
  API.LinkSymbolGetValue = stubLinkSymbolGetValue;
  API.LinkSymbolGetSize = stubLinkSymbolGetSize;
  API.LinkSymbolIsDefined = stubLinkSymbolIsDefined;
  API.LinkSymbolIsLocal = stubLinkSymbolIsLocal;
  API.LinkSymbolIsHidden = stubLinkSymbolIsHidden;
  API.LinkSymbolSetVisibilityHidden = stubLinkSymbolSetVisibilityHidden;

  API.LinkGetFirstSection = stubLinkGetFirstSection;
  API.LinkGetNextSection = stubLinkGetNextSection;
  API.LinkFindSection = stubLinkFindSection;
  API.LinkSectionGetName = stubLinkSectionGetName;
  API.LinkSectionGetSize = stubLinkSectionGetSize;
  API.LinkSectionGetAlignment = stubLinkSectionGetAlignment;
  API.LinkSectionGetFlags = stubLinkSectionGetFlags;

  API.LinkGetOutputPath = stubLinkGetOutputPath;
  API.LinkGetOutputFormat = stubLinkGetOutputFormat;

  static_assert(
      offsetof(NevercHostAPI, LinkGetOutputFormat) +
              sizeof(NevercHostAPI::LinkGetOutputFormat) ==
          sizeof(NevercHostAPI),
      "LinkGetOutputFormat is no longer the last field in "
      "NevercHostAPI. Add bridge assignments for the new field(s) and "
      "update this check.");

  return API;
}

} // namespace plugin
} // namespace neverc
