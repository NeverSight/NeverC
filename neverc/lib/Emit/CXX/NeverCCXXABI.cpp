//===--- NeverCCXXABI.cpp - NeverC-only C++ ABI v1 ------------------------===//
#include "neverc/Emit/CXXABI.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/GlobalDecl.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

using namespace neverc;
using namespace neverc::Emit;

namespace {

class NeverCCXXABI final : public CXXABI {
  ModuleEmitter &ME;
  llvm::DenseMap<const void *, llvm::GlobalVariable *> VTables;
  llvm::DenseMap<const void *, llvm::GlobalVariable *> VTTs;
  llvm::DenseMap<const void *, llvm::Constant *> RTTIGlobals;

  llvm::LLVMContext &getLLVMCtx() { return ME.getLLVMContext(); }

  std::string mangleVTableName(const CXXRecordDecl *RD) {
    std::string Out;
    llvm::raw_string_ostream OS(Out);
    OS << "_ZTV";
    if (const IdentifierInfo *II = RD->getIdentifier())
      OS << II->getName().size() << II->getName();
    else
      OS << "4anon";
    return OS.str();
  }

  std::string mangleVTTName(const CXXRecordDecl *RD) {
    std::string Out;
    llvm::raw_string_ostream OS(Out);
    OS << "_ZTT";
    if (const IdentifierInfo *II = RD->getIdentifier())
      OS << II->getName().size() << II->getName();
    else
      OS << "4anon";
    return OS.str();
  }

  bool hasVirtualBases(const CXXRecordDecl *RD) {
    const CXXRecordDecl *Def = RD->getDefinition() ? RD->getDefinition() : RD;
    for (const CXXBaseSpecifier *B = Def->bases_begin(), *E = Def->bases_end();
         B != E; ++B) {
      if (B->isVirtual())
        return true;
    }
    return false;
  }

  std::string mangleRTTIName(QualType Ty) {
    std::string Out;
    llvm::raw_string_ostream OS(Out);
    OS << "_ZTI";
    if (const IdentifierInfo *II = Ty.getBaseTypeIdentifier())
      OS << II->getName().size() << II->getName();
    else
      OS << "v";
    return OS.str();
  }

  void collectVirtualMethods(const CXXRecordDecl *RD,
                             llvm::SmallVectorImpl<const CXXMethodDecl *> &Out) {
    const CXXRecordDecl *Def = RD->getDefinition() ? RD->getDefinition() : RD;
    for (Decl *D : Def->decls()) {
      if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
        if (MD->isVirtual())
          Out.push_back(MD);
      }
    }
  }

  llvm::FunctionCallee getRuntimeFn(llvm::StringRef Name, llvm::FunctionType *FTy) {
    return ME.createRuntimeFunction(FTy, Name);
  }

public:
  explicit NeverCCXXABI(ModuleEmitter &ME) : ME(ME) {}

  llvm::Value *emitCXXNew(FunctionEmitter &FE, const CXXNewExpr *E) override {
    QualType AllocTy = E->getType();
    if (const Type *T = AllocTy.getTypePtrOrNull()) {
      if (T->isPointerType())
        AllocTy = T->getPointeeType();
    }

    uint64_t SizeBits = ME.getContext().getTypeSize(AllocTy);
    uint64_t SizeBytes = (SizeBits + 7) / 8;
    if (SizeBytes == 0)
      SizeBytes = 1;

    llvm::Type *SizeTy = FE.SizeTy;
    llvm::Value *SizeV = llvm::ConstantInt::get(SizeTy, SizeBytes);

    llvm::FunctionType *NewTy = llvm::FunctionType::get(
        FE.VoidPtrTy, {SizeTy}, /*isVarArg=*/false);
    // Itanium-style mangled operator new(unsigned long)
    llvm::FunctionCallee NewFn = getRuntimeFn("_Znwm", NewTy);
    llvm::Value *Mem = FE.Builder.CreateCall(NewFn, {SizeV}, "new.ptr");

    if (Expr *Init = const_cast<CXXNewExpr *>(E)->getInitializer()) {
      (void)Init;
    }

    // NeverC ABI v1: dynamic class new — store vptr at object start when known.
    if (const CXXRecordDecl *RD = AllocTy->getAsCXXRecordDecl()) {
      if (RD->isDynamicClass()) {
        emitVTable(ME, RD);
        const void *Key = RD->getCanonicalDecl();
        auto It = VTables.find(Key);
        if (It != VTables.end()) {
          llvm::Value *VT = It->second;
          if (VT->getType() != FE.VoidPtrTy)
            VT = FE.Builder.CreateBitCast(VT, FE.VoidPtrTy);
          llvm::Value *Obj = Mem;
          if (Obj->getType() != FE.VoidPtrTy)
            Obj = FE.Builder.CreateBitCast(Obj, FE.VoidPtrTy);
          llvm::Type *PPTy = llvm::PointerType::getUnqual(getLLVMCtx());
          llvm::Value *Slot = FE.Builder.CreateBitCast(Obj, PPTy);
          FE.Builder.CreateStore(VT, Slot);
        }
      }
    }

    llvm::Type *ResultTy = FE.convertType(E->getType());
    if (Mem->getType() != ResultTy)
      Mem = FE.Builder.CreateBitCast(Mem, ResultTy);
    return Mem;
  }

  void emitCXXDelete(FunctionEmitter &FE, const CXXDeleteExpr *E) override {
    Expr *Arg = const_cast<CXXDeleteExpr *>(E)->getArgument();
    if (!Arg)
      return;
    llvm::Value *Ptr = FE.genScalarExpr(Arg);
    if (!Ptr)
      return;
    if (Ptr->getType() != FE.VoidPtrTy)
      Ptr = FE.Builder.CreateBitCast(Ptr, FE.VoidPtrTy);

    // Call complete-object destructor; array form walks a small fixed bound
    // cookie scaffold (NeverC ABI v1: no cookie — single-element dtor once,
    // then _ZdaPv). When array form and element has dtor, call dtor once on
    // the first element as a minimal element-dtor hook (full cookie later).
    {
      QualType Pointee = Arg->getType();
      if (const Type *PT = Pointee.getTypePtrOrNull()) {
        if (PT->isPointerType())
          Pointee = PT->getPointeeType();
      }
      // Array new T[N] pointee may be array type — strip to element.
      if (!Pointee.isNull() && Pointee->isArrayType())
        Pointee = FE.getContext().getBaseElementType(Pointee);
      if (!Pointee.isNull()) {
        if (const CXXRecordDecl *RD = Pointee->getAsCXXRecordDecl()) {
          const CXXRecordDecl *Def =
              RD->getDefinition() ? RD->getDefinition() : RD;
          const CXXDestructorDecl *Dtor = nullptr;
          for (const Decl *D : Def->decls()) {
            if (const auto *DD = dyn_cast<CXXDestructorDecl>(D)) {
              Dtor = DD;
              break;
            }
          }
          if (Dtor) {
            if (llvm::Constant *CFn = ME.addrOfFunction(GlobalDecl(Dtor))) {
              if (auto *Fn = llvm::dyn_cast<llvm::Function>(
                      CFn->stripPointerCasts())) {
                // Always destroy the complete object / first element.
                FE.Builder.CreateCall(Fn, {Ptr});
                if (E->isArrayForm()) {
                  // Element dtor loop scaffold: destroy one additional
                  // hypothetical element at +size when size known; else once.
                  uint64_t SizeBits = ME.getContext().getTypeSize(Pointee);
                  uint64_t SizeBytes = (SizeBits + 7) / 8;
                  if (SizeBytes == 0)
                    SizeBytes = 1;
                  llvm::Type *I8 = llvm::Type::getInt8Ty(getLLVMCtx());
                  llvm::Value *BaseI8 = Ptr;
                  if (BaseI8->getType() !=
                      llvm::PointerType::getUnqual(getLLVMCtx()))
                    BaseI8 = FE.Builder.CreateBitCast(
                        BaseI8, llvm::PointerType::getUnqual(getLLVMCtx()));
                  // Loop i=1..0 disabled; call dtor on element 0 only above.
                  // Marker keeps array-delete element-dtor path live for
                  // future cookie-based N. (void) keeps SizeBytes used.
                  (void)SizeBytes;
                  (void)I8;
                  (void)BaseI8;
                }
              }
            }
          }
        }
      }
    }

    llvm::FunctionType *DelTy = llvm::FunctionType::get(
        FE.VoidTy, {FE.VoidPtrTy}, /*isVarArg=*/false);
    const char *Name = E->isArrayForm() ? "_ZdaPv" : "_ZdlPv";
    llvm::FunctionCallee DelFn = getRuntimeFn(Name, DelTy);
    FE.Builder.CreateCall(DelFn, {Ptr});
  }

  llvm::Value *emitDynamicCast(FunctionEmitter &FE, llvm::Value *Value,
                               QualType SrcTy, QualType DestTy) override {
    if (!Value)
      return Value;

    llvm::Value *SrcPtr = Value;
    if (SrcPtr->getType() != FE.VoidPtrTy)
      SrcPtr = FE.Builder.CreateBitCast(SrcPtr, FE.VoidPtrTy);

    llvm::Constant *SrcRTTI = getAddrOfRTTIDescriptor(SrcTy);
    llvm::Constant *DstRTTI = getAddrOfRTTIDescriptor(DestTy);
    if (SrcRTTI->getType() != FE.VoidPtrTy)
      SrcRTTI = llvm::ConstantExpr::getBitCast(SrcRTTI, FE.VoidPtrTy);
    if (DstRTTI->getType() != FE.VoidPtrTy)
      DstRTTI = llvm::ConstantExpr::getBitCast(DstRTTI, FE.VoidPtrTy);

    llvm::Type *I64Ty = FE.Int64Ty;
    llvm::FunctionType *DCTy = llvm::FunctionType::get(
        FE.VoidPtrTy,
        {FE.VoidPtrTy, FE.VoidPtrTy, FE.VoidPtrTy, I64Ty},
        /*isVarArg=*/false);
    llvm::FunctionCallee DCFn =
        getRuntimeFn("__neverc_dynamic_cast", DCTy);
    llvm::Value *Off = llvm::ConstantInt::get(I64Ty, 0);
    llvm::Value *Res = FE.Builder.CreateCall(
        DCFn, {SrcPtr, SrcRTTI, DstRTTI, Off}, "dcast");

    llvm::Type *ResultTy = FE.convertType(DestTy);
    if (Res->getType() != ResultTy)
      Res = FE.Builder.CreateBitCast(Res, ResultTy);
    return Res;
  }

  void emitRethrow(FunctionEmitter &FE, bool isNoReturn) override {
    (void)isNoReturn;
    llvm::FunctionType *FnTy =
        llvm::FunctionType::get(FE.VoidTy, {}, /*isVarArg=*/false);
    llvm::FunctionCallee Fn = getRuntimeFn("__cxa_rethrow", FnTy);
    FE.Builder.CreateCall(Fn);
  }

  void emitThrow(FunctionEmitter &FE, const CXXThrowExpr *E) override {
    Expr *Op = const_cast<CXXThrowExpr *>(E)->getSubExpr();
    if (!Op) {
      emitRethrow(FE, /*isNoReturn=*/true);
      return;
    }

    QualType ThrownTy = Op->getType();
    uint64_t SizeBits = ME.getContext().getTypeSize(ThrownTy);
    uint64_t SizeBytes = (SizeBits + 7) / 8;
    if (SizeBytes == 0)
      SizeBytes = 1;

    llvm::Type *SizeTy = FE.SizeTy;
    llvm::FunctionType *AllocTy = llvm::FunctionType::get(
        FE.VoidPtrTy, {SizeTy}, /*isVarArg=*/false);
    llvm::FunctionCallee AllocFn =
        getRuntimeFn("__cxa_allocate_exception", AllocTy);
    llvm::Value *Ex = FE.Builder.CreateCall(
        AllocFn, {llvm::ConstantInt::get(SizeTy, SizeBytes)}, "ex.slot");

    // Best-effort store of the thrown value into the exception slot.
    if (llvm::Value *V = FE.genScalarExpr(Op)) {
      llvm::Type *ValTy = FE.convertType(ThrownTy);
      llvm::Value *Slot = FE.Builder.CreateBitCast(
          Ex, llvm::PointerType::getUnqual(getLLVMCtx()));
      FE.Builder.CreateStore(V, Slot);
      (void)ValTy;
    }

    llvm::Constant *TypeInfo = getAddrOfRTTIDescriptor(ThrownTy);
    if (TypeInfo->getType() != FE.VoidPtrTy)
      TypeInfo = llvm::ConstantExpr::getBitCast(TypeInfo, FE.VoidPtrTy);

    llvm::FunctionType *ThrowTy = llvm::FunctionType::get(
        FE.VoidTy,
        {FE.VoidPtrTy, FE.VoidPtrTy, FE.VoidPtrTy},
        /*isVarArg=*/false);
    llvm::FunctionCallee ThrowFn = getRuntimeFn("__cxa_throw", ThrowTy);
    llvm::Value *NullDtor = llvm::ConstantPointerNull::get(FE.VoidPtrTy);
    FE.Builder.CreateCall(ThrowFn, {Ex, TypeInfo, NullDtor});
  }

  void emitVTT(ModuleEmitter &MERef, const CXXRecordDecl *RD) override {
    (void)MERef;
    if (!RD || !hasVirtualBases(RD))
      return;
    const void *Key = RD->getCanonicalDecl();
    if (VTTs.count(Key))
      return;
    // Ensure primary + virtual-base vtables exist.
    emitVTable(ME, RD);
    llvm::SmallVector<llvm::Constant *, 8> Slots;
    llvm::Type *PtrTy = llvm::PointerType::getUnqual(getLLVMCtx());
    auto pushVT = [&](const CXXRecordDecl *R) {
      emitVTable(ME, R);
      const void *K = R->getCanonicalDecl();
      auto It = VTables.find(K);
      llvm::Constant *C = It != VTables.end()
                              ? static_cast<llvm::Constant *>(It->second)
                              : llvm::ConstantPointerNull::get(PtrTy);
      if (C->getType() != PtrTy)
        C = llvm::ConstantExpr::getBitCast(C, PtrTy);
      // Address point: bitcast whole array to ptr (NeverC simplified).
      Slots.push_back(C);
    };
    pushVT(RD);
    llvm::SmallPtrSet<const CXXRecordDecl *, 4> Seen;
    const CXXRecordDecl *Def = RD->getDefinition() ? RD->getDefinition() : RD;
    for (const CXXBaseSpecifier *B = Def->bases_begin(), *E = Def->bases_end();
         B != E; ++B) {
      if (!B->isVirtual())
        continue;
      QualType BT = B->getType();
      if (BT.isNull())
        continue;
      const CXXRecordDecl *BRD = BT->getAsCXXRecordDecl();
      if (!BRD)
        continue;
      BRD = cast<CXXRecordDecl>(BRD->getCanonicalDecl());
      if (!Seen.insert(BRD).second)
        continue;
      pushVT(BRD);
    }
    if (Slots.empty())
      return;
    llvm::ArrayType *ArrTy =
        llvm::ArrayType::get(PtrTy, static_cast<uint64_t>(Slots.size()));
    llvm::Constant *Init = llvm::ConstantArray::get(ArrTy, Slots);
    auto *GV = new llvm::GlobalVariable(
        ME.getModule(), ArrTy, /*isConstant=*/true,
        llvm::GlobalValue::LinkOnceODRLinkage, Init, mangleVTTName(RD));
    GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    VTTs[Key] = GV;
  }

  void emitVTable(ModuleEmitter &MERef, const CXXRecordDecl *RD) override {
    (void)MERef;
    if (!RD || !RD->isDynamicClass())
      return;

    const void *Key = RD->getCanonicalDecl();
    if (VTables.count(Key))
      return;

    llvm::SmallVector<const CXXMethodDecl *, 8> Virtuals;
    collectVirtualMethods(RD, Virtuals);

    // NeverC ABI v1 layout: [offset-to-top, RTTI, vfunc*]
    llvm::Type *PtrTy = llvm::PointerType::getUnqual(getLLVMCtx());
    llvm::SmallVector<llvm::Constant *, 16> slots;
    // offset-to-top encoded as a null pointer slot (complete-object simplified).
    slots.push_back(llvm::ConstantPointerNull::get(PtrTy));

    QualType RecordTy =
        ME.getContext().getRecordType(const_cast<CXXRecordDecl *>(RD));
    llvm::Constant *RTTI = getAddrOfRTTIDescriptor(RecordTy);
    if (RTTI->getType() != PtrTy)
      RTTI = llvm::ConstantExpr::getBitCast(RTTI, PtrTy);
    slots.push_back(RTTI);

    for (const CXXMethodDecl *MD : Virtuals) {
      llvm::Constant *FnAddr = nullptr;
      if (MD->isPureVirtual()) {
        llvm::FunctionType *PVTy =
            llvm::FunctionType::get(llvm::Type::getVoidTy(getLLVMCtx()),
                                    {}, false);
        llvm::FunctionCallee PV =
            ME.createRuntimeFunction(PVTy, "__cxa_pure_virtual");
        FnAddr = llvm::cast<llvm::Constant>(PV.getCallee());
      } else {
        FnAddr = ME.addrOfFunction(GlobalDecl(MD));
      }
      if (!FnAddr)
        FnAddr = llvm::ConstantPointerNull::get(PtrTy);
      if (FnAddr->getType() != PtrTy)
        FnAddr = llvm::ConstantExpr::getBitCast(FnAddr, PtrTy);
      slots.push_back(FnAddr);
    }

    llvm::ArrayType *ArrTy =
        llvm::ArrayType::get(PtrTy, static_cast<uint64_t>(slots.size()));
    llvm::Constant *Init = llvm::ConstantArray::get(ArrTy, slots);

    auto *GV = new llvm::GlobalVariable(
        ME.getModule(), ArrTy, /*isConstant=*/true,
        llvm::GlobalValue::LinkOnceODRLinkage, Init, mangleVTableName(RD));
    GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    VTables[Key] = GV;
  }

  llvm::Value *getVirtualFunctionPointer(FunctionEmitter &FE, llvm::Value *This,
                                         const CXXMethodDecl *MD) override {
    if (!This || !MD)
      return nullptr;
    const CXXRecordDecl *RD = MD->getParent();
    if (!RD)
      return nullptr;
    emitVTable(ME, RD);
    const void *Key = RD->getCanonicalDecl();
    auto It = VTables.find(Key);
    if (It == VTables.end())
      return nullptr;

    // Determine virtual slot index among virtual methods of RD.
    llvm::SmallVector<const CXXMethodDecl *, 8> Virtuals;
    collectVirtualMethods(RD, Virtuals);
    int Slot = -1;
    for (unsigned I = 0, E = Virtuals.size(); I != E; ++I) {
      if (Virtuals[I]->getCanonicalDecl() == MD->getCanonicalDecl()) {
        Slot = static_cast<int>(I);
        break;
      }
    }
    if (Slot < 0)
      return nullptr;

    // Object layout: vptr at offset 0. Vtable: [off, RTTI, vfunc0, ...]
    // Address point is slots[2] (first vfunc); index Slot at 2+Slot.
    llvm::LLVMContext &Ctx = getLLVMCtx();
    llvm::Type *PtrTy = llvm::PointerType::getUnqual(Ctx);
    llvm::Value *Obj = This;
    if (Obj->getType() != PtrTy)
      Obj = FE.Builder.CreateBitCast(Obj, PtrTy);
    llvm::Type *PPTy = llvm::PointerType::getUnqual(Ctx);
    llvm::Value *VPtrAddr = FE.Builder.CreateBitCast(Obj, PPTy);
    llvm::Value *VTable = FE.Builder.CreateLoad(PtrTy, VPtrAddr, "vtable");
    // VTable points at address point (first vfunc) in Itanium; NeverC stores
    // the full array and stores &slots[0] or address-point. Use GEP from
    // loaded vptr assuming it points at slot 0 of the constant array.
    unsigned Idx = 2u + static_cast<unsigned>(Slot);
    llvm::Value *SlotPtr = FE.Builder.CreateConstGEP1_32(PtrTy, VTable, Idx, "vfn.slot");
    return FE.Builder.CreateLoad(PtrTy, SlotPtr, "vfn");
  }


  llvm::Constant *getAddrOfRTTIDescriptor(QualType Ty) override {
    const void *Key = Ty.getAsOpaquePtr();
    if (auto It = RTTIGlobals.find(Key); It != RTTIGlobals.end())
      return It->second;

    llvm::Type *PtrTy = llvm::PointerType::getUnqual(getLLVMCtx());
    llvm::ArrayType *ArrTy = llvm::ArrayType::get(PtrTy, 2);

    std::string Name = mangleRTTIName(Ty);
    llvm::Constant *NameStr =
        llvm::ConstantDataArray::getString(getLLVMCtx(), Name, true);
    auto *NameGV = new llvm::GlobalVariable(
        ME.getModule(), NameStr->getType(), true,
        llvm::GlobalValue::LinkOnceODRLinkage, NameStr, Name + ".name");
    NameGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

    llvm::Constant *Slots[] = {
        llvm::ConstantPointerNull::get(PtrTy),
        llvm::ConstantExpr::getBitCast(NameGV, PtrTy),
    };
    llvm::Constant *Init = llvm::ConstantArray::get(ArrTy, Slots);
    auto *GV = new llvm::GlobalVariable(
        ME.getModule(), ArrTy, true, llvm::GlobalValue::LinkOnceODRLinkage, Init,
        Name);
    GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    RTTIGlobals[Key] = GV;
    return GV;
  }
};

} // namespace

CXXABI *neverc::Emit::CreateNeverCCXXABI(ModuleEmitter &ME) {
  return new NeverCCXXABI(ME);
}
