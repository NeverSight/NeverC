#include "BridgeCastHelpers.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace neverc {
namespace plugin {


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


void populateIRBridge(NevercHostAPI &API) {
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
  API.MDStringCreate = bridgeMDStringCreate;
  API.MDNodeCreate = bridgeMDNodeCreate;
  API.InstSetMetadata = bridgeInstSetMetadata;
  API.InstGetMetadata = bridgeInstGetMetadata;
  API.MDKindGetID = bridgeMDKindGetID;
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
  API.ModuleRemoveFunction = bridgeModuleRemoveFunction;
  API.ModuleRemoveGlobal = bridgeModuleRemoveGlobal;
  API.ModuleGetFunctionCount = bridgeModuleGetFunctionCount;
  API.ModuleGetGlobalCount = bridgeModuleGetGlobalCount;
  API.ValueGetTypeAsFunction = bridgeValueGetTypeAsFunction;
  API.FunctionGetReturnType = bridgeFunctionGetReturnType;
  API.InstIsTerminator = bridgeInstIsTerminator;
  API.ConstPointerNull = bridgeConstPointerNull;
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
  API.ConstArray = bridgeConstArray;
  API.ConstStruct = bridgeConstStruct;
  API.ConstNamedStruct = bridgeConstNamedStruct;
  API.ValueSetName = bridgeValueSetName;
  API.GlobalGetLinkage = bridgeGlobalGetLinkage;
  API.GlobalSetLinkage = bridgeGlobalSetLinkage;
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
  API.ModuleGetSourceFileName = bridgeModuleGetSourceFileName;
  API.InstIsInvoke = bridgeInstIsInvoke;
  API.InvokeGetNormalDest = bridgeInvokeGetNormalDest;
  API.InvokeGetUnwindDest = bridgeInvokeGetUnwindDest;
  API.BBSplitBefore = bridgeBBSplitBefore;
  API.ConstIntGetZExtValue = bridgeConstIntGetZExtValue;
  API.ConstIntGetSExtValue = bridgeConstIntGetSExtValue;
  API.ValueIsConstantInt = bridgeValueIsConstantInt;
  API.GlobalGetValueType = bridgeGlobalGetValueType;
  API.FunctionDeleteBody = bridgeFunctionDeleteBody;
  API.ValueIsUndef = bridgeValueIsUndef;
  API.InstGetOperandAsBB = bridgeInstGetOperandAsBB;
  API.LoadGetType = bridgeLoadGetType;
  API.GEPIsInBounds = bridgeGEPIsInBounds;
  API.InstIsCallLike = bridgeInstIsCallLike;
  API.LoadGetAlignment = bridgeLoadGetAlignment;
  API.StoreGetAlignment = bridgeStoreGetAlignment;
  API.LoadIsVolatile = bridgeLoadIsVolatile;
  API.StoreIsVolatile = bridgeStoreIsVolatile;
  API.InstIsIntrinsic = bridgeInstIsIntrinsic;
  API.CallGetIntrinsicID = bridgeCallGetIntrinsicID;
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
  API.BBMoveAfter = bridgeBBMoveAfter;
  API.BBMoveBefore = bridgeBBMoveBefore;
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
  API.ConstZeroInitializer = bridgeConstZeroInitializer;
  API.GlobalHasUnnamedAddr = bridgeGlobalHasUnnamedAddr;
  API.GlobalSetUnnamedAddr = bridgeGlobalSetUnnamedAddr;
  API.ModuleGetPrevFunction = bridgeModuleGetPrevFunction;
  API.ModuleGetLastAlias = bridgeModuleGetLastAlias;
  API.ModuleGetPrevAlias = bridgeModuleGetPrevAlias;
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
  API.InstGetOpcodeName = bridgeInstGetOpcodeName;
  API.InstOpcodeToName = bridgeInstOpcodeToName;
}

} // namespace plugin
} // namespace neverc
