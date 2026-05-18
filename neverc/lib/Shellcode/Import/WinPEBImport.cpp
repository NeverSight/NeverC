#include "neverc/Shellcode/Import/WinPEBImport.h"
#include "ExtractorCommon.h"
#include "neverc/Shellcode/Import/WinImportTables.h"
#include "neverc/Shellcode/Pipeline/ShellcodeIRHelperNames.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

void setInternalResolverAttrs(Function *F) {
  AttrBuilder AB(F->getContext());
  AB.addAttribute(Attribute::NoUnwind);
  AB.addAttribute(Attribute::WillReturn);
  AB.addAttribute(Attribute::NoSync);
  AB.addAttribute(Attribute::NoRecurse);
  AB.addAttribute(Attribute::NoFree);
  AB.addAttribute(Attribute::MustProgress);
  F->addFnAttrs(AB);
  F->setDSOLocal(true);
}

uint32_t ror13Hash(StringRef Name) {
  uint32_t H = 0;
  for (char C : Name) {
    uint8_t U = static_cast<uint8_t>(C);
    if (U >= 'a' && U <= 'z')
      U = static_cast<uint8_t>(U - 'a' + 'A');
    H = ((H >> 13) | (H << 19)) + U;
  }
  return H;
}

uint32_t hashDllName(StringRef Name) { return ror13Hash(Name); }

Value *emitReadPEB(IRBuilder<> &B, const TargetDesc &T) {
  LLVMContext &Ctx = B.getContext();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *FTy = FunctionType::get(PtrTy, {}, false);

  InlineAsm *Asm =
      InlineAsm::get(FTy, T.TCBReadAsm.str(), T.TCBReadConstraint.str(),
                     /*hasSideEffects=*/true,
                     /*isAlignStack=*/false, InlineAsm::AD_ATT);
  return B.CreateCall(FTy, Asm);
}

Function *getOrCreateBaseHashHelper(Module &M) {
  StringRef Name = ir::kNevercWinBasehash;
  if (Function *F = M.getFunction(Name))
    return F;

  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I16 = Type::getInt16Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);

  FunctionType *FTy = FunctionType::get(I32, {PtrTy, I32}, false);
  Function *F = Function::Create(FTy, GlobalValue::InternalLinkage, Name, &M);
  setInternalResolverAttrs(F);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Loop = BasicBlock::Create(Ctx, "loop", F);
  BasicBlock *Body = BasicBlock::Create(Ctx, "body", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  Value *PStr = F->getArg(0);
  Value *Len = F->getArg(1);
  PStr->setName("wstr");
  Len->setName("len");

  IRBuilder<> B(Entry);
  B.CreateBr(Loop);

  MDBuilder MDB(Ctx);

  B.SetInsertPoint(Loop);
  PHINode *I = B.CreatePHI(I32, 2, "i");
  PHINode *H = B.CreatePHI(I32, 2, "h");
  I->addIncoming(ConstantInt::get(I32, 0), Entry);
  H->addIncoming(ConstantInt::get(I32, 0), Entry);
  Value *Cond = B.CreateICmpULT(I, Len, "cond");
  auto *LBr = B.CreateCondBr(Cond, Body, Done);
  LBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(2000, 1));

  B.SetInsertPoint(Body);
  Value *ElemPtr = B.CreateInBoundsGEP(I16, PStr, I, "elem.ptr");
  Value *Wc = B.CreateLoad(I16, ElemPtr, "wc");
  Value *Low = B.CreateTrunc(Wc, B.getInt8Ty(), "low");
  Value *IsLower =
      B.CreateAnd(B.CreateICmpUGE(Low, ConstantInt::get(B.getInt8Ty(), 'a')),
                  B.CreateICmpULE(Low, ConstantInt::get(B.getInt8Ty(), 'z')));
  Value *Adj = B.CreateSub(Low, ConstantInt::get(B.getInt8Ty(), 32));
  Value *Chr = B.CreateSelect(IsLower, Adj, Low);
  Value *ChrZ = B.CreateZExt(Chr, I32);
  Value *Shr = B.CreateLShr(H, ConstantInt::get(I32, 13));
  Value *Shl = B.CreateShl(H, ConstantInt::get(I32, 19));
  Value *Rot = B.CreateOr(Shr, Shl);
  Value *NewH = B.CreateAdd(Rot, ChrZ, "newh");
  Value *NewI = B.CreateNUWAdd(I, ConstantInt::get(I32, 1), "inc");
  I->addIncoming(NewI, Body);
  H->addIncoming(NewH, Body);
  B.CreateBr(Loop);

  B.SetInsertPoint(Done);
  B.CreateRet(H);

  return F;
}

Function *getOrCreateCStrHashHelper(Module &M) {
  StringRef Name = ir::kNevercWinCstrhash;
  if (Function *F = M.getFunction(Name))
    return F;

  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I8 = Type::getInt8Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);

  FunctionType *FTy = FunctionType::get(I32, {PtrTy}, false);
  Function *F = Function::Create(FTy, GlobalValue::InternalLinkage, Name, &M);
  setInternalResolverAttrs(F);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Loop = BasicBlock::Create(Ctx, "loop", F);
  BasicBlock *Body = BasicBlock::Create(Ctx, "body", F);
  BasicBlock *Done = BasicBlock::Create(Ctx, "done", F);

  Value *PStr = F->getArg(0);
  PStr->setName("s");

  IRBuilder<> B(Entry);
  B.CreateBr(Loop);

  B.SetInsertPoint(Loop);
  PHINode *P = B.CreatePHI(PtrTy, 2, "p");
  PHINode *H = B.CreatePHI(I32, 2, "h");
  P->addIncoming(PStr, Entry);
  H->addIncoming(ConstantInt::get(I32, 0), Entry);
  MDBuilder MDB(Ctx);
  Value *Ch = B.CreateLoad(I8, P, "ch");
  Value *IsEnd = B.CreateICmpEQ(Ch, ConstantInt::get(I8, 0));
  auto *LBr = B.CreateCondBr(IsEnd, Done, Body);
  LBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Body);
  Value *IsLower = B.CreateAnd(B.CreateICmpUGE(Ch, ConstantInt::get(I8, 'a')),
                               B.CreateICmpULE(Ch, ConstantInt::get(I8, 'z')));
  Value *Adj = B.CreateSub(Ch, ConstantInt::get(I8, 32));
  Value *Upper = B.CreateSelect(IsLower, Adj, Ch);
  Value *UpperZ = B.CreateZExt(Upper, I32);
  Value *Shr = B.CreateLShr(H, ConstantInt::get(I32, 13));
  Value *Shl = B.CreateShl(H, ConstantInt::get(I32, 19));
  Value *Rot = B.CreateOr(Shr, Shl);
  Value *NewH = B.CreateAdd(Rot, UpperZ, "newh");
  Value *NextP = B.CreateInBoundsGEP(I8, P, ConstantInt::get(I32, 1));
  P->addIncoming(NextP, Body);
  H->addIncoming(NewH, Body);
  B.CreateBr(Loop);

  B.SetInsertPoint(Done);
  B.CreateRet(H);
  return F;
}

Function *getOrCreateFindExportHelper(Module &M) {
  StringRef Name = ir::kNevercWinFindExport;
  if (Function *F = M.getFunction(Name))
    return F;

  LLVMContext &Ctx = M.getContext();
  Type *I8 = Type::getInt8Ty(Ctx);
  Type *I16 = Type::getInt16Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);

  FunctionType *FTy = FunctionType::get(PtrTy, {PtrTy, I32}, false);
  Function *F = Function::Create(FTy, GlobalValue::InternalLinkage, Name, &M);
  setInternalResolverAttrs(F);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Loop = BasicBlock::Create(Ctx, "loop", F);
  BasicBlock *Cmp = BasicBlock::Create(Ctx, "cmp", F);
  BasicBlock *Hit = BasicBlock::Create(Ctx, "hit", F);
  BasicBlock *Miss = BasicBlock::Create(Ctx, "miss", F);

  Value *DllBase = F->getArg(0);
  Value *Want = F->getArg(1);
  DllBase->setName("dll_base");
  Want->setName("want");

  Function *CStrHash = getOrCreateCStrHashHelper(M);

  IRBuilder<> B(Entry);

  Value *ElfPtr = B.CreateInBoundsGEP(I8, DllBase, ConstantInt::get(I32, 0x3C),
                                      "e_lfanew.ptr");
  Value *ElfRva = B.CreateLoad(I32, ElfPtr, "e_lfanew");
  Value *NtHdr = B.CreateInBoundsGEP(I8, DllBase, ElfRva, "nt_hdr");
  Value *ExpDirRvaPtr = B.CreateInBoundsGEP(
      I8, NtHdr, ConstantInt::get(I32, 0x88), "exp_dir_rva.ptr");
  Value *ExpDirRva = B.CreateLoad(I32, ExpDirRvaPtr, "exp_dir_rva");

  Value *HasExports =
      B.CreateICmpNE(ExpDirRva, ConstantInt::get(I32, 0), "has_exports");
  BasicBlock *HaveExports = BasicBlock::Create(Ctx, "have_exports", F);
  B.CreateCondBr(HasExports, HaveExports, Miss);

  B.SetInsertPoint(HaveExports);
  Value *ExpDir = B.CreateInBoundsGEP(I8, DllBase, ExpDirRva, "exp_dir");

  Value *NumNamesPtr = B.CreateInBoundsGEP(
      I8, ExpDir, ConstantInt::get(I32, 0x18), "num_names.ptr");
  Value *NumNames = B.CreateLoad(I32, NumNamesPtr, "num_names");
  Value *AddrFuncsPtrPtr = B.CreateInBoundsGEP(
      I8, ExpDir, ConstantInt::get(I32, 0x1C), "addr_funcs_rva.ptr");
  Value *AddrFuncsRva = B.CreateLoad(I32, AddrFuncsPtrPtr, "addr_funcs_rva");
  Value *AddrNamesPtrPtr = B.CreateInBoundsGEP(
      I8, ExpDir, ConstantInt::get(I32, 0x20), "addr_names_rva.ptr");
  Value *AddrNamesRva = B.CreateLoad(I32, AddrNamesPtrPtr, "addr_names_rva");
  Value *AddrOrdsPtrPtr = B.CreateInBoundsGEP(
      I8, ExpDir, ConstantInt::get(I32, 0x24), "addr_ords_rva.ptr");
  Value *AddrOrdsRva = B.CreateLoad(I32, AddrOrdsPtrPtr, "addr_ords_rva");

  Value *AddrFuncs =
      B.CreateInBoundsGEP(I8, DllBase, AddrFuncsRva, "addr_funcs");
  Value *AddrNames =
      B.CreateInBoundsGEP(I8, DllBase, AddrNamesRva, "addr_names");
  Value *AddrOrds = B.CreateInBoundsGEP(I8, DllBase, AddrOrdsRva, "addr_ords");
  B.CreateBr(Loop);

  MDBuilder MDB(Ctx);

  B.SetInsertPoint(Loop);
  PHINode *I = B.CreatePHI(I32, 2, "i");
  I->addIncoming(ConstantInt::get(I32, 0), HaveExports);
  Value *Stop = B.CreateICmpUGE(I, NumNames);
  auto *SBr = B.CreateCondBr(Stop, Miss, Cmp);
  SBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Cmp);
  Value *NameRvaPtr = B.CreateInBoundsGEP(I32, AddrNames, I, "name_rva.ptr");
  Value *NameRva = B.CreateLoad(I32, NameRvaPtr, "name_rva");
  Value *NameStr = B.CreateInBoundsGEP(I8, DllBase, NameRva, "name_str");
  Value *H = B.CreateCall(CStrHash, {NameStr}, "h");
  Value *Match = B.CreateICmpEQ(H, Want);
  Value *NextI = B.CreateNUWAdd(I, ConstantInt::get(I32, 1));
  I->addIncoming(NextI, Cmp);
  auto *MBr = B.CreateCondBr(Match, Hit, Loop);
  MBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Hit);
  Value *OrdPtr = B.CreateInBoundsGEP(I16, AddrOrds, I, "ord.ptr");
  Value *OrdHalf = B.CreateLoad(I16, OrdPtr, "ord");
  Value *OrdZ = B.CreateZExt(OrdHalf, I32);
  Value *FnRvaPtr = B.CreateInBoundsGEP(I32, AddrFuncs, OrdZ, "fn_rva.ptr");
  Value *FnRva = B.CreateLoad(I32, FnRvaPtr, "fn_rva");
  Value *FnAddr = B.CreateInBoundsGEP(I8, DllBase, FnRva, "fn_addr");
  B.CreateRet(FnAddr);

  B.SetInsertPoint(Miss);
  B.CreateRet(ConstantPointerNull::get(PtrTy));
  return F;
}

Function *getOrCreateFindModuleHelper(Module &M, const TargetDesc &T) {
  StringRef Name = ir::kNevercWinFindModule;
  if (Function *F = M.getFunction(Name))
    return F;

  LLVMContext &Ctx = M.getContext();
  Type *I8 = Type::getInt8Ty(Ctx);
  Type *I16 = Type::getInt16Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);

  FunctionType *FTy = FunctionType::get(PtrTy, {I32}, false);
  Function *F = Function::Create(FTy, GlobalValue::InternalLinkage, Name, &M);
  setInternalResolverAttrs(F);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Loop = BasicBlock::Create(Ctx, "loop", F);
  BasicBlock *Chk = BasicBlock::Create(Ctx, "check", F);
  BasicBlock *Hit = BasicBlock::Create(Ctx, "hit", F);
  BasicBlock *Miss = BasicBlock::Create(Ctx, "miss", F);

  Value *Want = F->getArg(0);
  Want->setName("dll_hash");

  Function *BaseHash = getOrCreateBaseHashHelper(M);

  IRBuilder<> B(Entry);

  Value *PEB = emitReadPEB(B, T);
  Value *LdrPtr =
      B.CreateInBoundsGEP(I8, PEB, ConstantInt::get(I32, 0x18), "ldr.ptr");
  Value *Ldr = B.CreateLoad(PtrTy, LdrPtr, "ldr");
  Value *ListHead =
      B.CreateInBoundsGEP(I8, Ldr, ConstantInt::get(I32, 0x20), "list_head");
  Value *FirstEntry = B.CreateLoad(PtrTy, ListHead, "first_entry");
  B.CreateBr(Loop);

  MDBuilder MDB(Ctx);

  B.SetInsertPoint(Loop);
  PHINode *Cur = B.CreatePHI(PtrTy, 2, "cur");
  Cur->addIncoming(FirstEntry, Entry);
  Value *IsHead = B.CreateICmpEQ(Cur, ListHead);
  auto *HBr = B.CreateCondBr(IsHead, Miss, Chk);
  HBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Chk);
  Value *LenPtr =
      B.CreateInBoundsGEP(I8, Cur, ConstantInt::get(I32, 0x48), "bdn_len.ptr");
  Value *LenBytes = B.CreateLoad(I16, LenPtr, "bdn_len_bytes");
  Value *LenBytesZ = B.CreateZExt(LenBytes, I32);
  Value *LenChars = B.CreateLShr(LenBytesZ, ConstantInt::get(I32, 1));
  Value *BufPtrSlot =
      B.CreateInBoundsGEP(I8, Cur, ConstantInt::get(I32, 0x50), "bdn_buf_slot");
  Value *BufPtr = B.CreateLoad(PtrTy, BufPtrSlot, "bdn_buf");
  Value *H = B.CreateCall(BaseHash, {BufPtr, LenChars}, "h");
  Value *Match = B.CreateICmpEQ(H, Want);
  Value *NextFlink = B.CreateLoad(PtrTy, Cur, "next_flink");
  Cur->addIncoming(NextFlink, Chk);
  auto *MBr = B.CreateCondBr(Match, Hit, Loop);
  MBr->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(1, 2000));

  B.SetInsertPoint(Hit);
  Value *BasePtr =
      B.CreateInBoundsGEP(I8, Cur, ConstantInt::get(I32, 0x20), "dllbase.ptr");
  Value *Base = B.CreateLoad(PtrTy, BasePtr, "dllbase");
  B.CreateRet(Base);

  B.SetInsertPoint(Miss);
  B.CreateRet(ConstantPointerNull::get(PtrTy));
  return F;
}

Function *getOrCreateResolver(Module &M, const TargetDesc &T) {
  StringRef Name = ir::kNevercWinResolve;
  if (Function *F = M.getFunction(Name))
    return F;

  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);

  FunctionType *FTy = FunctionType::get(PtrTy, {I32, I32}, false);
  Function *F = Function::Create(FTy, GlobalValue::InternalLinkage, Name, &M);
  setInternalResolverAttrs(F);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *Have = BasicBlock::Create(Ctx, "have_base", F);
  BasicBlock *Miss = BasicBlock::Create(Ctx, "miss", F);

  Value *DllHash = F->getArg(0);
  Value *ApiHash = F->getArg(1);
  DllHash->setName("dll_hash");
  ApiHash->setName("api_hash");

  Function *FindModule = getOrCreateFindModuleHelper(M, T);
  Function *FindExport = getOrCreateFindExportHelper(M);

  IRBuilder<> B(Entry);
  Value *DllBase = B.CreateCall(FindModule, {DllHash}, "dll.base");
  Value *BaseIsNull = B.CreateICmpEQ(DllBase, ConstantPointerNull::get(PtrTy));
  B.CreateCondBr(BaseIsNull, Miss, Have);

  B.SetInsertPoint(Have);
  Value *Fn = B.CreateCall(FindExport, {DllBase, ApiHash}, "fn");
  B.CreateRet(Fn);

  B.SetInsertPoint(Miss);
  B.CreateRet(ConstantPointerNull::get(PtrTy));
  return F;
}

Function *createWrapper(Module &M, const TargetDesc &T, Function &Decl,
                        Function *Resolver, StringRef Dll) {
  LLVMContext &Ctx = M.getContext();
  FunctionType *FTy = Decl.getFunctionType();

  Function *Wrap =
      Function::Create(FTy, GlobalValue::InternalLinkage,
                       Twine(ir::kScWinPrefix) + Decl.getName(), &M);
  Wrap->addFnAttr(Attribute::AlwaysInline);
  Wrap->addFnAttr(Attribute::NoUnwind);
  Wrap->setDSOLocal(true);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", Wrap);
  IRBuilder<> B(Entry);

  uint32_t DllH = hashDllName(Dll);
  uint32_t ApiH = ror13Hash(Decl.getName());
  Value *DllHashV = B.getInt32(DllH);
  Value *ApiHashV = B.getInt32(ApiH);
  Value *FnPtr = B.CreateCall(Resolver->getFunctionType(), Resolver,
                              {DllHashV, ApiHashV}, "fn");

  SmallVector<Value *, 8> CallArgs;
  for (Argument &A : Wrap->args())
    CallArgs.push_back(&A);

  CallInst *Call = B.CreateCall(FTy, FnPtr, CallArgs);
  Call->setCallingConv(Decl.getCallingConv());

  if (FTy->getReturnType()->isVoidTy())
    B.CreateRetVoid();
  else
    B.CreateRet(Call);
  return Wrap;
}

Function *createPosixWrap(Module &M, Function &Decl, const Twine &WrapName,
                          FunctionType *CanonFTy, unsigned MinParams = 0,
                          bool ForceCanon = false) {
  FunctionType *FTy = Decl.getFunctionType();
  if (CanonFTy && (ForceCanon || FTy->getNumParams() < MinParams))
    FTy = CanonFTy;
  Function *Wrap =
      Function::Create(FTy, GlobalValue::InternalLinkage, WrapName, &M);
  Wrap->addFnAttr(Attribute::AlwaysInline);
  Wrap->addFnAttr(Attribute::NoUnwind);
  Wrap->setDSOLocal(true);
  BasicBlock::Create(M.getContext(), "entry", Wrap);
  return Wrap;
}

Function *emitFdToHandleHelper(Module &M) {
  StringRef Name = ir::kScPosixFdToHandle;
  if (Function *F = M.getFunction(Name))
    return F;

  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);

  FunctionType *GshTy = FunctionType::get(PtrTy, {I32}, false);
  Function *GetStdHandle =
      cast<Function>(M.getOrInsertFunction("GetStdHandle", GshTy).getCallee());

  FunctionType *FTy = FunctionType::get(PtrTy, {I32}, false);
  Function *F = Function::Create(FTy, GlobalValue::InternalLinkage, Name, &M);
  F->addFnAttr(Attribute::AlwaysInline);
  F->addFnAttr(Attribute::NoUnwind);
  F->setDSOLocal(true);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> B(Entry);

  Value *Fd = F->getArg(0);
  Value *StdId = B.CreateSub(ConstantInt::get(I32, static_cast<uint32_t>(-10)),
                             Fd, "std_id");
  Value *Handle = B.CreateCall(GshTy, GetStdHandle, {StdId}, "handle");
  B.CreateRet(Handle);
  return F;
}

void emitBoolAsPosixStatusReturn(IRBuilder<> &B, Value *Ok, Type *RetTy) {
  LLVMContext &Ctx = B.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  Value *Success = B.CreateICmpNE(Ok, ConstantInt::get(Ok->getType(), 0), "ok");
  Value *Status = B.CreateSelect(Success, ConstantInt::get(I32, 0),
                                 ConstantInt::getSigned(I32, -1), "status");
  if (RetTy->isVoidTy())
    B.CreateRetVoid();
  else if (RetTy->isIntegerTy() && RetTy != I32)
    B.CreateRet(B.CreateSExtOrTrunc(Status, RetTy));
  else if (RetTy->isIntegerTy())
    B.CreateRet(Status);
  else
    B.CreateRet(Constant::getNullValue(RetTy));
}

void emitBoolAndCountAsPosixReturn(IRBuilder<> &B, Value *Ok, Value *Count,
                                   Type *RetTy) {
  LLVMContext &Ctx = B.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  Value *Success = B.CreateICmpNE(Ok, ConstantInt::get(Ok->getType(), 0), "ok");
  Value *Fail = ConstantInt::getSigned(I32, -1);
  Value *PosixRetI32 = B.CreateSelect(Success, Count, Fail, "posix_ret");
  if (RetTy->isVoidTy()) {
    B.CreateRetVoid();
  } else if (RetTy->isIntegerTy() && RetTy != I32) {
    B.CreateRet(B.CreateSExtOrTrunc(PosixRetI32, RetTy));
  } else if (RetTy->isIntegerTy()) {
    B.CreateRet(PosixRetI32);
  } else {
    B.CreateRet(Constant::getNullValue(RetTy));
  }
}

void emitMmapResultAsPosixReturn(IRBuilder<> &B, Value *Ptr, Type *RetTy) {
  LLVMContext &Ctx = B.getContext();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Value *NullP = ConstantPointerNull::get(PtrTy);
  Value *IsNull = B.CreateICmpEQ(Ptr, NullP, "is_null");
  Value *MapFailedInt = ConstantInt::getSigned(Type::getInt64Ty(Ctx), -1);
  Value *MapFailedPtr = B.CreateIntToPtr(MapFailedInt, PtrTy);
  Value *PosixPtr = B.CreateSelect(IsNull, MapFailedPtr, Ptr, "mmap_ret");

  if (RetTy->isVoidTy()) {
    B.CreateRetVoid();
  } else if (RetTy->isPointerTy()) {
    if (RetTy != PtrTy)
      B.CreateRet(B.CreateBitCast(PosixPtr, RetTy));
    else
      B.CreateRet(PosixPtr);
  } else {
    B.CreateRet(Constant::getNullValue(RetTy));
  }
}

FunctionType *canonicalWriteFTy(LLVMContext &Ctx) {
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  return FunctionType::get(I64, {I32, PtrTy, I64}, false);
}

Function *createPosixWriteWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);

  Function *Wrap =
      createPosixWrap(M, Decl, ir::kScPosixWrite, canonicalWriteFTy(Ctx), 3);
  FunctionType *FTy = Wrap->getFunctionType();

  FunctionType *WfTy =
      FunctionType::get(I32, {PtrTy, PtrTy, I32, PtrTy, PtrTy}, false);
  Function *WriteFile =
      cast<Function>(M.getOrInsertFunction("WriteFile", WfTy).getCallee());

  Function *FdHelper = emitFdToHandleHelper(M);

  IRBuilder<> B(&Wrap->getEntryBlock());

  Value *Fd = Wrap->getArg(0);
  Value *Buf = Wrap->getArg(1);
  Value *Count = Wrap->getArg(2);

  if (Fd->getType() != I32)
    Fd = B.CreateTrunc(Fd, I32);
  Value *Handle = B.CreateCall(FdHelper, {Fd}, "h");

  Value *DwCount = Count;
  if (DwCount->getType() != I32)
    DwCount = B.CreateTrunc(DwCount, I32);

  Value *Written = B.CreateAlloca(I32, nullptr, "written");
  B.CreateStore(ConstantInt::get(I32, 0), Written);

  Value *Ok = B.CreateCall(
      WfTy, WriteFile,
      {Handle, Buf, DwCount, Written, ConstantPointerNull::get(PtrTy)}, "ok");

  Value *NWritten = B.CreateLoad(I32, Written, "n_written");
  emitBoolAndCountAsPosixReturn(B, Ok, NWritten, FTy->getReturnType());
  return Wrap;
}

FunctionType *canonicalReadFTy(LLVMContext &Ctx) {
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  return FunctionType::get(I64, {I32, PtrTy, I64}, false);
}

Function *createPosixReadWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);

  Function *Wrap =
      createPosixWrap(M, Decl, ir::kScPosixRead, canonicalReadFTy(Ctx), 3);
  FunctionType *FTy = Wrap->getFunctionType();

  FunctionType *RfTy =
      FunctionType::get(I32, {PtrTy, PtrTy, I32, PtrTy, PtrTy}, false);
  Function *ReadFile =
      cast<Function>(M.getOrInsertFunction("ReadFile", RfTy).getCallee());

  Function *FdHelper = emitFdToHandleHelper(M);

  IRBuilder<> B(&Wrap->getEntryBlock());

  Value *Fd = Wrap->getArg(0);
  Value *Buf = Wrap->getArg(1);
  Value *Count = Wrap->getArg(2);

  if (Fd->getType() != I32)
    Fd = B.CreateTrunc(Fd, I32);
  Value *Handle = B.CreateCall(FdHelper, {Fd}, "h");

  Value *DwCount = Count;
  if (DwCount->getType() != I32)
    DwCount = B.CreateTrunc(DwCount, I32);

  Value *NRead = B.CreateAlloca(I32, nullptr, "nread");
  B.CreateStore(ConstantInt::get(I32, 0), NRead);

  Value *Ok = B.CreateCall(
      RfTy, ReadFile,
      {Handle, Buf, DwCount, NRead, ConstantPointerNull::get(PtrTy)}, "ok");

  Value *NReadVal = B.CreateLoad(I32, NRead, "n_read");
  emitBoolAndCountAsPosixReturn(B, Ok, NReadVal, FTy->getReturnType());
  return Wrap;
}

FunctionType *canonicalExitFTy(LLVMContext &Ctx) {
  Type *I32 = Type::getInt32Ty(Ctx);
  return FunctionType::get(Type::getVoidTy(Ctx), {I32}, false);
}

Function *createPosixExitWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);

  Function *Wrap =
      createPosixWrap(M, Decl, Twine(ir::kScPosixPrefix) + Decl.getName(),
                      canonicalExitFTy(Ctx), 1);
  Wrap->addFnAttr(Attribute::NoReturn);

  FunctionType *EpTy = FunctionType::get(Type::getVoidTy(Ctx), {I32}, false);
  Function *ExitProcess =
      cast<Function>(M.getOrInsertFunction("ExitProcess", EpTy).getCallee());

  IRBuilder<> B(&Wrap->getEntryBlock());

  Value *Code = Wrap->getArg(0);
  if (Code->getType() != I32)
    Code = B.CreateTrunc(Code, I32);

  B.CreateCall(EpTy, ExitProcess, {Code});
  B.CreateUnreachable();
  return Wrap;
}

Function *emitProtToPageHelper(Module &M) {
  StringRef Name = ir::kScPosixProtToPage;
  if (Function *F = M.getFunction(Name))
    return F;

  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  FunctionType *FTy = FunctionType::get(I32, {I32}, false);
  Function *F = Function::Create(FTy, GlobalValue::InternalLinkage, Name, &M);
  F->addFnAttr(Attribute::AlwaysInline);
  F->addFnAttr(Attribute::NoUnwind);
  F->setDSOLocal(true);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> B(Entry);
  Value *Prot = F->getArg(0);

  Value *HasExec = B.CreateAnd(Prot, ConstantInt::get(I32, 4));
  Value *IsExec = B.CreateICmpNE(HasExec, ConstantInt::get(I32, 0), "exec");
  Value *HasWrite = B.CreateAnd(Prot, ConstantInt::get(I32, 2));
  Value *IsWrite = B.CreateICmpNE(HasWrite, ConstantInt::get(I32, 0), "wr");
  Value *HasRead = B.CreateAnd(Prot, ConstantInt::get(I32, 1));
  Value *IsRead = B.CreateICmpNE(HasRead, ConstantInt::get(I32, 0), "rd");

  Value *ExecRW = ConstantInt::get(I32, 0x40);
  Value *ExecR = ConstantInt::get(I32, 0x20);
  Value *Exec = ConstantInt::get(I32, 0x10);
  Value *RW = ConstantInt::get(I32, 0x04);
  Value *RO = ConstantInt::get(I32, 0x02);
  Value *NA = ConstantInt::get(I32, 0x01);

  Value *NoExecVal =
      B.CreateSelect(IsWrite, RW, B.CreateSelect(IsRead, RO, NA));
  Value *ExecVal =
      B.CreateSelect(IsWrite, ExecRW, B.CreateSelect(IsRead, ExecR, Exec));
  Value *Result = B.CreateSelect(IsExec, ExecVal, NoExecVal, "page_prot");
  B.CreateRet(Result);
  return F;
}

Function *createPosixMmapWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);

  Function *Wrap = createPosixWrap(
      M, Decl, ir::kScPosixMmap,
      FunctionType::get(PtrTy, {PtrTy, I64, I32, I32, I32, I64}, false), 6);
  FunctionType *FTy = Wrap->getFunctionType();

  FunctionType *VaTy = FunctionType::get(PtrTy, {PtrTy, I64, I32, I32}, false);
  Function *VirtualAlloc =
      cast<Function>(M.getOrInsertFunction("VirtualAlloc", VaTy).getCallee());

  Function *ProtHelper = emitProtToPageHelper(M);

  IRBuilder<> B(&Wrap->getEntryBlock());

  Value *Addr = Wrap->getArg(0);
  Value *Len = Wrap->getArg(1);
  Value *Prot = Wrap->getArg(2);
  if (Prot->getType() != I32)
    Prot = B.CreateTrunc(Prot, I32);

  Value *PageProt = B.CreateCall(ProtHelper, {Prot}, "page_prot");
  Value *AllocType = ConstantInt::get(I32, 0x3000); // MEM_COMMIT | MEM_RESERVE
  Value *Result =
      B.CreateCall(VaTy, VirtualAlloc, {Addr, Len, AllocType, PageProt}, "mem");
  emitMmapResultAsPosixReturn(B, Result, FTy->getReturnType());
  return Wrap;
}

Function *createPosixMunmapWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);

  Function *Wrap =
      createPosixWrap(M, Decl, ir::kScPosixMunmap,
                      FunctionType::get(I32, {PtrTy, I64}, false), 2);
  FunctionType *FTy = Wrap->getFunctionType();

  FunctionType *VfTy = FunctionType::get(I32, {PtrTy, I64, I32}, false);
  Function *VirtualFree =
      cast<Function>(M.getOrInsertFunction("VirtualFree", VfTy).getCallee());

  IRBuilder<> B(&Wrap->getEntryBlock());

  Value *Addr = Wrap->getArg(0);
  Value *Zero = ConstantInt::get(I64, 0);
  Value *MemRelease = ConstantInt::get(I32, 0x8000);

  Value *Ret = B.CreateCall(VfTy, VirtualFree, {Addr, Zero, MemRelease}, "ok");
  emitBoolAsPosixStatusReturn(B, Ret, FTy->getReturnType());
  return Wrap;
}

Function *createPosixMprotectWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);

  Function *Wrap =
      createPosixWrap(M, Decl, ir::kScPosixMprotect,
                      FunctionType::get(I32, {PtrTy, I64, I32}, false), 3);
  FunctionType *FTy = Wrap->getFunctionType();

  FunctionType *VpTy = FunctionType::get(I32, {PtrTy, I64, I32, PtrTy}, false);
  Function *VirtualProtect =
      cast<Function>(M.getOrInsertFunction("VirtualProtect", VpTy).getCallee());

  Function *ProtHelper = emitProtToPageHelper(M);

  IRBuilder<> B(&Wrap->getEntryBlock());

  Value *Addr = Wrap->getArg(0);
  Value *Len = Wrap->getArg(1);
  Value *Prot = Wrap->getArg(2);
  if (Prot->getType() != I32)
    Prot = B.CreateTrunc(Prot, I32);

  Value *PageProt = B.CreateCall(ProtHelper, {Prot}, "page_prot");
  Value *OldProt = B.CreateAlloca(I32, nullptr, "old_prot");
  B.CreateStore(ConstantInt::get(I32, 0), OldProt);

  Value *Ret =
      B.CreateCall(VpTy, VirtualProtect, {Addr, Len, PageProt, OldProt}, "ok");
  emitBoolAsPosixStatusReturn(B, Ret, FTy->getReturnType());
  return Wrap;
}

Function *createPosixGetpidWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  FunctionType *CanonFTy = FunctionType::get(I32, {}, false);

  FunctionType *DeclFTy = Decl.getFunctionType();
  bool ForceCanon =
      DeclFTy->getNumParams() != 0 || DeclFTy->getReturnType()->isVoidTy();
  Function *Wrap = createPosixWrap(M, Decl, ir::kScPosixGetpid, CanonFTy,
                                   /*MinParams=*/0, ForceCanon);
  FunctionType *FTy = Wrap->getFunctionType();

  Function *GetCurrentProcessId = cast<Function>(
      M.getOrInsertFunction("GetCurrentProcessId", CanonFTy).getCallee());

  IRBuilder<> B(&Wrap->getEntryBlock());
  Value *Pid = B.CreateCall(CanonFTy, GetCurrentProcessId, {}, "pid");
  Type *RetTy = FTy->getReturnType();
  if (RetTy != Pid->getType())
    B.CreateRet(B.CreateSExtOrTrunc(Pid, RetTy));
  else
    B.CreateRet(Pid);
  return Wrap;
}

Function *createPosixCloseWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);

  Function *Wrap = createPosixWrap(M, Decl, ir::kScPosixClose,
                                   FunctionType::get(I32, {I32}, false), 1);
  FunctionType *FTy = Wrap->getFunctionType();

  FunctionType *ChTy = FunctionType::get(I32, {PtrTy}, false);
  Function *CloseHandle =
      cast<Function>(M.getOrInsertFunction("CloseHandle", ChTy).getCallee());

  Function *FdHelper = emitFdToHandleHelper(M);

  IRBuilder<> B(&Wrap->getEntryBlock());

  Value *Fd = Wrap->getArg(0);
  if (Fd->getType() != I32)
    Fd = B.CreateTrunc(Fd, I32);
  Value *Handle = B.CreateCall(FdHelper, {Fd}, "h");
  Value *Ok = B.CreateCall(ChTy, CloseHandle, {Handle}, "ok");
  emitBoolAsPosixStatusReturn(B, Ok, FTy->getReturnType());
  return Wrap;
}

Function *createPosixSleepWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);

  Function *Wrap = createPosixWrap(M, Decl, ir::kScPosixSleep,
                                   FunctionType::get(I32, {I32}, false), 1);
  FunctionType *FTy = Wrap->getFunctionType();

  FunctionType *SlTy = FunctionType::get(Type::getVoidTy(Ctx), {I32}, false);
  Function *WinSleep =
      cast<Function>(M.getOrInsertFunction("Sleep", SlTy).getCallee());

  IRBuilder<> B(&Wrap->getEntryBlock());

  Value *Secs = Wrap->getArg(0);
  if (Secs->getType() != I32)
    Secs = B.CreateTrunc(Secs, I32);
  Value *Ms = B.CreateMul(Secs, ConstantInt::get(I32, 1000), "ms");
  B.CreateCall(SlTy, WinSleep, {Ms});

  Type *RetTy = FTy->getReturnType();
  if (RetTy->isVoidTy())
    B.CreateRetVoid();
  else
    B.CreateRet(ConstantInt::get(RetTy, 0));
  return Wrap;
}

Function *createSimpleBoolBridgeWrapper(
    Module &M, Function &Decl, StringRef WrapName, FunctionType *CanonFTy,
    unsigned MinParams, StringRef Win32Name, FunctionType *Win32FTy,
    llvm::function_ref<SmallVector<Value *, 4>(IRBuilder<> &, Function *)>
        FormWin32Args) {
  Function *Wrap = createPosixWrap(M, Decl, WrapName, CanonFTy, MinParams);
  FunctionType *FTy = Wrap->getFunctionType();

  Function *Win32Fn =
      cast<Function>(M.getOrInsertFunction(Win32Name, Win32FTy).getCallee());

  IRBuilder<> B(&Wrap->getEntryBlock());
  SmallVector<Value *, 4> Args = FormWin32Args(B, Wrap);
  Value *Ok = B.CreateCall(Win32FTy, Win32Fn, Args, "ok");
  emitBoolAsPosixStatusReturn(B, Ok, FTy->getReturnType());
  return Wrap;
}

Function *createPosixUnlinkWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *ApiTy = FunctionType::get(I32, {PtrTy}, false);
  return createSimpleBoolBridgeWrapper(
      M, Decl, ir::kScPosixUnlink, FunctionType::get(I32, {PtrTy}, false), 1,
      "DeleteFileA", ApiTy,
      [](IRBuilder<> &, Function *W) -> SmallVector<Value *, 4> {
        return {W->getArg(0)};
      });
}

Function *createPosixRmdirWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *ApiTy = FunctionType::get(I32, {PtrTy}, false);
  return createSimpleBoolBridgeWrapper(
      M, Decl, ir::kScPosixRmdir, FunctionType::get(I32, {PtrTy}, false), 1,
      "RemoveDirectoryA", ApiTy,
      [](IRBuilder<> &, Function *W) -> SmallVector<Value *, 4> {
        return {W->getArg(0)};
      });
}

Function *createPosixMkdirWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *ApiTy = FunctionType::get(I32, {PtrTy, PtrTy}, false);
  return createSimpleBoolBridgeWrapper(
      M, Decl, ir::kScPosixMkdir, FunctionType::get(I32, {PtrTy, I32}, false),
      2, "CreateDirectoryA", ApiTy,
      [PtrTy](IRBuilder<> &, Function *W) -> SmallVector<Value *, 4> {
        return {W->getArg(0), ConstantPointerNull::get(PtrTy)};
      });
}

Function *createPosixRenameWrapper(Module &M, Function &Decl) {
  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  FunctionType *ApiTy = FunctionType::get(I32, {PtrTy, PtrTy}, false);
  return createSimpleBoolBridgeWrapper(
      M, Decl, ir::kScPosixRename,
      FunctionType::get(I32, {PtrTy, PtrTy}, false), 2, "MoveFileA", ApiTy,
      [](IRBuilder<> &, Function *W) -> SmallVector<Value *, 4> {
        return {W->getArg(0), W->getArg(1)};
      });
}

Function *tryCreatePosixCompatWrapper(Module &M, Function &Decl) {
  StringRef Name = Decl.getName();
  StringRef Bare = Name.starts_with("_") ? Name.drop_front(1) : Name;
#define NEVERC_WIN32_POSIX_COMPAT(bareName, wrapperBuilder)                    \
  if (Bare == #bareName)                                                       \
    return wrapperBuilder(M, Decl);
#include "neverc/Shellcode/Tables/UserExtra_Win32PosixCompat.def"
#include "neverc/Shellcode/Tables/Win32PosixCompat.def"
#undef NEVERC_WIN32_POSIX_COMPAT
  return nullptr;
}

} // namespace

PreservedAnalyses WinPEBImportPass::run(Module &M, ModuleAnalysisManager &) {
  if (Target.OS != ShellcodeOS::Windows)
    return PreservedAnalyses::all();
  if (Target.Arch != ShellcodeArch::X86_64 &&
      Target.Arch != ShellcodeArch::AArch64)
    return PreservedAnalyses::all();
  if (Target.Level == ExecutionLevel::Kernel)
    return PreservedAnalyses::all();

  SmallVector<Function *, 4> PosixCompat;
  SmallVector<std::pair<Function *, StringRef>, 8> ToReplace;
  for (Function &F : M) {
    if (!F.isDeclaration())
      continue;
    StringRef Name = F.getName();
    if (isShellcodeInternalRuntimeName(Name))
      continue;
    Win32ApiLookup L = lookupWin32Api(Name);
    if (L.Found) {
      ToReplace.push_back({&F, L.Dll});
    } else {
      PosixCompat.push_back(&F);
    }
  }

  bool Changed = false;

  for (Function *Decl : PosixCompat) {
    Function *Wrap = tryCreatePosixCompatWrapper(M, *Decl);
    if (!Wrap)
      continue;
    Decl->replaceAllUsesWith(Wrap);
    Decl->eraseFromParent();
    Changed = true;
  }

  if (Changed) {
    DenseSet<Function *> Already;
    Already.reserve(ToReplace.size());
    for (auto &[D, _] : ToReplace)
      Already.insert(D);
    for (Function &F : M) {
      if (!F.isDeclaration())
        continue;
      StringRef Name = F.getName();
      if (isShellcodeInternalRuntimeName(Name))
        continue;
      Win32ApiLookup L = lookupWin32Api(Name);
      if (!L.Found)
        continue;
      if (Already.insert(&F).second)
        ToReplace.push_back({&F, L.Dll});
    }
  }

  if (ToReplace.empty())
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();

  Function *Resolver = getOrCreateResolver(M, Target);

  DenseMap<StringRef, Function *> ByName;
  for (auto &[Decl, Dll] : ToReplace) {
    Function *&Slot = ByName[Decl->getName()];
    if (!Slot)
      Slot = createWrapper(M, Target, *Decl, Resolver, Dll);
    Decl->replaceAllUsesWith(Slot);
    Decl->eraseFromParent();
  }
  return PreservedAnalyses::none();
}

} // namespace shellcode
} // namespace neverc
