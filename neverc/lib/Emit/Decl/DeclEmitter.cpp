#include "ABI/EmitterABI.h"
#include "ABI/TargetInfo.h"
#include "Core/ConstantEmitter.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "Debug/DebugEmitterInfo.h"
#include "Decl/PatternInit.h"
#include "Stmt/CleanupEmitterInfo.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Emit/ABI/ABIFunctionInfo.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"

using namespace neverc;
using namespace Emit;

static_assert(neverc::Sema::MaximumAlignment <= llvm::Value::MaximumAlignment,
              "NeverC max alignment greater than what LLVM supports?");

// ===----------------------------------------------------------------------===
// Declaration dispatch
// ===----------------------------------------------------------------------===

__attribute__((hot)) void FunctionEmitter::genDecl(const Decl &D) {
  switch (D.getKind()) {
  case Decl::Record:
    if (DebugEmitter *DI = getDebugInfo())
      if (cast<RecordDecl>(D).getDefinition())
        DI->genAndRetainType(getContext().getRecordType(cast<RecordDecl>(&D)));
    return;
  case Decl::Enum:
    if (DebugEmitter *DI = getDebugInfo())
      if (cast<EnumDecl>(D).getDefinition())
        DI->genAndRetainType(getContext().getEnumType(cast<EnumDecl>(&D)));
    return;
  case Decl::Function:
  case Decl::EnumConstant:
  case Decl::StaticAssert:
  case Decl::Label:
  case Decl::Empty:
    return;

  case Decl::Var: {
    const VarDecl &VD = cast<VarDecl>(D);
    assert(VD.isLocalVarDecl() &&
           "Should not see file-scope variables inside a function!");
    genVarDecl(VD);
    return;
  }

  case Decl::Typedef: {
    QualType Ty = cast<TypedefNameDecl>(D).getUnderlyingType();
    if (DebugEmitter *DI = getDebugInfo())
      DI->genAndRetainType(Ty);
    if (Ty->isVariablyModifiedType())
      genVariablyModifiedType(Ty);
    return;
  }
  default:
    llvm_unreachable("Declaration should not be in declstmts!");
  }
}

void FunctionEmitter::genVarDecl(const VarDecl &D) {
  if (D.hasExternalStorage())
    // Don't emit it now, allow it to be emitted lazily on its first use.
    return;

  // Some function-scope variable does not have static storage but still
  // needs to be emitted like a static variable.
  if (D.getStorageDuration() != SD_Automatic) {
    llvm::GlobalValue::LinkageTypes Linkage =
        ME.getLLVMLinkageVarDefinition(&D);

    return genStaticVarDecl(D, Linkage);
  }

  assert(D.hasLocalStorage());
  return genAutoVarDecl(D);
}

// ===----------------------------------------------------------------------===
// Static variable declarations
// ===----------------------------------------------------------------------===

namespace {
std::string getStaticDeclName(ModuleEmitter &ME, const VarDecl &D) {
  assert(!D.isExternallyVisible() && "name shouldn't matter");
  std::string ContextName;
  const DeclContext *DC = D.getDeclContext();
  if (const auto *FD = dyn_cast<FunctionDecl>(DC))
    ContextName = std::string(ME.getMangledName(FD));
  else
    llvm_unreachable("Unknown context for static var decl");

  ContextName += "." + D.getNameAsString();
  return ContextName;
}
} // namespace

llvm::Constant *ModuleEmitter::getOrCreateStaticVarDecl(
    const VarDecl &D, llvm::GlobalValue::LinkageTypes Linkage) {
  // In general, we don't always emit static var decls once before we reference
  // them. It is possible to reference them before emitting the function that
  // contains them, and it is possible to emit the containing function multiple
  // times.
  if (llvm::Constant *ExistingGV = StaticLocalDeclMap[&D])
    return ExistingGV;

  QualType Ty = D.getType();
  assert(Ty->isConstantSizeType() && "VLAs can't be static");

  // Use the label if the variable is renamed with the asm-label extension.
  std::string Name;
  if (D.hasAttr<AsmLabelAttr>())
    Name = std::string(getMangledName(&D));
  else
    Name = getStaticDeclName(*this, D);

  llvm::Type *LTy = getTypes().convertTypeForMem(Ty);
  LangAS AS = getGlobalVarAddressSpace(&D);
  unsigned TargetAS = getContext().getTargetAddressSpace(AS);

  llvm::Constant *Init = nullptr;
  if (D.hasAttr<LoaderUninitializedAttr>())
    Init = llvm::UndefValue::get(LTy);
  else
    Init = genNullConstant(Ty);

  llvm::GlobalVariable *GV = new llvm::GlobalVariable(
      getModule(), LTy, Ty.isConstant(getContext()), Linkage, Init, Name,
      nullptr, llvm::GlobalVariable::NotThreadLocal, TargetAS);
  GV->setAlignment(getContext().getDeclAlign(&D).getAsAlign());

  if (supportsCOMDAT() && GV->isWeakForLinker())
    GV->setComdat(TheModule.getOrInsertComdat(GV->getName()));

  if (D.getTLSKind())
    setTLSMode(GV, D);

  setGVProperties(GV, &D);

  LangAS ExpectedAS = Ty.getAddressSpace();
  llvm::Constant *Addr = GV;
  if (AS != ExpectedAS) {
    Addr = getTargetCodeGenInfo().performAddrSpaceCast(
        *this, GV, AS, ExpectedAS,
        llvm::PointerType::get(getLLVMContext(),
                               getContext().getTargetAddressSpace(ExpectedAS)));
  }

  setStaticLocalDeclAddress(&D, Addr);

  // Ensure that the static local gets initialized by making sure the parent
  // function gets emitted eventually.
  const Decl *DC = cast<Decl>(D.getDeclContext());

  GlobalDecl GD;
  if (const auto *FD = dyn_cast<FunctionDecl>(DC))
    GD = GlobalDecl(FD);
  else {
    // We should never defer this (only FunctionDecl is valid here).
    llvm_unreachable("unexpected parent code decl");
  }
  if (GD.getDecl()) {
    (void)addrOfGlobal(GD);
  }

  return Addr;
}

llvm::GlobalVariable *
FunctionEmitter::addInitializerToStaticVarDecl(const VarDecl &D,
                                               llvm::GlobalVariable *GV) {
  ConstantEmitter emitter(*this);
  llvm::Constant *Init = emitter.tryEmitForInitializer(D);

  // If constant emission failed, the initializer is not a constant expression.
  if (!Init) {
    ME.errorUnsupported(D.getInit(), "constant l-value expression");
    return GV;
  }

#ifndef NDEBUG
  CharUnits VarSize = ME.getContext().getTypeSizeInChars(D.getType()) +
                      D.getFlexibleArrayInitChars(getContext());
  CharUnits CstSize = CharUnits::fromQuantity(
      ME.getDataLayout().getTypeAllocSize(Init->getType()));
  assert(VarSize == CstSize && "Emitted constant has unexpected size");
#endif

  // The initializer may differ in type from the global. Rewrite
  // the global to match the initializer.  (We have to do this
  // because some types, like unions, can't be completely represented
  // in the LLVM type system.)
  if (GV->getValueType() != Init->getType()) {
    llvm::GlobalVariable *OldGV = GV;

    GV = new llvm::GlobalVariable(
        ME.getModule(), Init->getType(), OldGV->isConstant(),
        OldGV->getLinkage(), Init, "",
        /*InsertBefore*/ OldGV, OldGV->getThreadLocalMode(),
        OldGV->getType()->getPointerAddressSpace());
    GV->setVisibility(OldGV->getVisibility());
    GV->setDSOLocal(OldGV->isDSOLocal());
    GV->setComdat(OldGV->getComdat());

    // Steal the name of the old global
    GV->takeName(OldGV);

    // Replace all uses of the old global with the new global
    OldGV->replaceAllUsesWith(GV);

    // Erase the old global, since it is no longer used.
    OldGV->eraseFromParent();
  }

  GV->setConstant(D.getType().isConstantStorage(getContext(), true, true));
  GV->setInitializer(Init);
  emitter.finalize(GV);

  return GV;
}

void FunctionEmitter::genStaticVarDecl(
    const VarDecl &D, llvm::GlobalValue::LinkageTypes Linkage) {
  // Check to see if we already have a global variable for this
  // declaration.  This can happen when double-emitting function bodies.
  llvm::Constant *addr = ME.getOrCreateStaticVarDecl(D, Linkage);
  CharUnits alignment = getContext().getDeclAlign(&D);

  // Store into LocalDeclMap before generating initializer to handle
  // circular references.
  llvm::Type *elemTy = convertTypeForMem(D.getType());
  setAddrOfLocalVar(&D, Address(addr, elemTy, alignment));

  // We can't have a VLA here, but we can have a pointer to a VLA,
  // even though that doesn't really make any sense.
  // Make sure to evaluate VLA bounds now so that we have them for later.
  if (D.getType()->isVariablyModifiedType())
    genVariablyModifiedType(D.getType());

  // Save the type in case adding the initializer forces a type change.
  llvm::Type *expectedType = addr->getType();

  llvm::GlobalVariable *var =
      cast<llvm::GlobalVariable>(addr->stripPointerCasts());

  if (D.getInit())
    var = addInitializerToStaticVarDecl(D, var);

  var->setAlignment(alignment.getAsAlign());

  if (D.hasAttr<AnnotateAttr>())
    ME.addGlobalAnnotations(&D, var);

  if (auto *SA = D.getAttr<PragmaNeverCBSSSectionAttr>())
    var->addAttribute("bss-section", SA->getName());
  if (auto *SA = D.getAttr<PragmaNeverCDataSectionAttr>())
    var->addAttribute("data-section", SA->getName());
  if (auto *SA = D.getAttr<PragmaNeverCRodataSectionAttr>())
    var->addAttribute("rodata-section", SA->getName());
  if (auto *SA = D.getAttr<PragmaNeverCRelroSectionAttr>())
    var->addAttribute("relro-section", SA->getName());

  if (const SectionAttr *SA = D.getAttr<SectionAttr>())
    var->setSection(SA->getName());

  if (D.hasAttr<RetainAttr>())
    ME.addUsedGlobal(var);
  else if (D.hasAttr<UsedAttr>())
    ME.addUsedOrCompilerUsedGlobal(var);

  if (ME.getCodeGenOpts().KeepPersistentStorageVariables)
    ME.addUsedOrCompilerUsedGlobal(var);

  // We may have to cast the constant because of the initializer
  // mismatch above.
  //
  llvm::Constant *castedAddr =
      llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(var, expectedType);
  LocalDeclMap.find(&D)->second = Address(castedAddr, elemTy, alignment);
  ME.setStaticLocalDeclAddress(&D, castedAddr);

  DebugEmitter *DI = getDebugInfo();
  if (DI && ME.getCodeGenOpts().hasReducedDebugInfo()) {
    DI->setLocation(D.getLocation());
    DI->genGlobalVariable(var, &D);
  }
}

namespace {
struct CallStackRestore final : EHScopeStack::Cleanup {
  Address Stack;
  CallStackRestore(Address Stack) : Stack(Stack) {}
  bool isRedundantBeforeReturn() override { return true; }
  void Emit(FunctionEmitter &FE, Flags flags) override {
    llvm::Value *V = FE.Builder.CreateLoad(Stack);
    FE.Builder.CreateStackRestore(V);
  }
};

struct CallCleanupFunction final : EHScopeStack::Cleanup {
  llvm::Constant *CleanupFn;
  const ABIFunctionInfo &FnInfo;
  const VarDecl &Var;

  CallCleanupFunction(llvm::Constant *CleanupFn, const ABIFunctionInfo *Info,
                      const VarDecl *Var)
      : CleanupFn(CleanupFn), FnInfo(*Info), Var(*Var) {}

  void Emit(FunctionEmitter &FE, Flags flags) override {
    DeclRefExpr DRE(FE.getContext(), const_cast<VarDecl *>(&Var), Var.getType(),
                    VK_LValue, SourceLocation());
    llvm::Value *Addr = FE.genDeclRefLValue(&DRE).getPointer(FE);

    // In some cases, the type of the function argument will be different from
    // the type of the pointer. An example of this is
    // void f(void* arg);
    // __attribute__((cleanup(f))) void *g;
    //
    // To fix this we insert a bitcast here.
    QualType ArgTy = FnInfo.arg_begin()->type;
    llvm::Value *Arg = FE.Builder.CreateBitCast(Addr, FE.convertType(ArgTy));

    CallArgList Args;
    Args.add(RValue::get(Arg), FE.getContext().getPointerType(Var.getType()));
    auto Callee = FnCallee::forDirect(CleanupFn);
    FE.genCall(FnInfo, Callee, ReturnValueSlot(), Args);
  }
};
} // end anonymous namespace

// ===----------------------------------------------------------------------===
// Variable initialization
// ===----------------------------------------------------------------------===

void FunctionEmitter::genScalarInit(const Expr *init, const ValueDecl *D,
                                    LValue lvalue, bool) {
  llvm::Value *value = genScalarExpr(init);
  genStoreThroughLValue(RValue::get(value), lvalue, true);
}

namespace {
bool canEmitInitWithFewStoresAfterBZero(llvm::Constant *Init,
                                        unsigned &NumStores) {
  // Zero and Undef never requires any extra stores.
  if (isa<llvm::ConstantAggregateZero>(Init) ||
      isa<llvm::ConstantPointerNull>(Init) || isa<llvm::UndefValue>(Init))
    return true;
  if (isa<llvm::ConstantInt>(Init) || isa<llvm::ConstantFP>(Init) ||
      isa<llvm::ConstantVector>(Init) || isa<llvm::BlockAddress>(Init) ||
      isa<llvm::ConstantExpr>(Init))
    return Init->isNullValue() || NumStores--;

  // See if we can emit each element.
  if (isa<llvm::ConstantArray>(Init) || isa<llvm::ConstantStruct>(Init)) {
    for (unsigned i = 0, e = Init->getNumOperands(); i != e; ++i) {
      llvm::Constant *Elt = cast<llvm::Constant>(Init->getOperand(i));
      if (!canEmitInitWithFewStoresAfterBZero(Elt, NumStores))
        return false;
    }
    return true;
  }

  if (llvm::ConstantDataSequential *CDS =
          dyn_cast<llvm::ConstantDataSequential>(Init)) {
    for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
      llvm::Constant *Elt = CDS->getElementAsConstant(i);
      if (!canEmitInitWithFewStoresAfterBZero(Elt, NumStores))
        return false;
    }
    return true;
  }

  // Anything else is hard and scary.
  return false;
}

void emitStoresForInitAfterBZero(ModuleEmitter &ME, llvm::Constant *Init,
                                 Address Loc, bool isVolatile,
                                 CGBuilderTy &Builder, bool IsAutoInit) {
  assert(!Init->isNullValue() && !isa<llvm::UndefValue>(Init) &&
         "called emitStoresForInitAfterBZero for zero or undef value.");

  if (isa<llvm::ConstantInt>(Init) || isa<llvm::ConstantFP>(Init) ||
      isa<llvm::ConstantVector>(Init) || isa<llvm::BlockAddress>(Init) ||
      isa<llvm::ConstantExpr>(Init)) {
    auto *I = Builder.CreateStore(Init, Loc, isVolatile);
    if (IsAutoInit)
      I->addAnnotationMetadata("auto-init");
    return;
  }

  if (llvm::ConstantDataSequential *CDS =
          dyn_cast<llvm::ConstantDataSequential>(Init)) {
    for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
      llvm::Constant *Elt = CDS->getElementAsConstant(i);

      // If necessary, get a pointer to the element and emit it.
      if (!Elt->isNullValue() && !isa<llvm::UndefValue>(Elt))
        emitStoresForInitAfterBZero(
            ME, Elt, Builder.CreateConstInBoundsGEP2_32(Loc, 0, i), isVolatile,
            Builder, IsAutoInit);
    }
    return;
  }

  assert((isa<llvm::ConstantStruct>(Init) || isa<llvm::ConstantArray>(Init)) &&
         "Unknown value type!");

  for (unsigned i = 0, e = Init->getNumOperands(); i != e; ++i) {
    llvm::Constant *Elt = cast<llvm::Constant>(Init->getOperand(i));

    // If necessary, get a pointer to the element and emit it.
    if (!Elt->isNullValue() && !isa<llvm::UndefValue>(Elt))
      emitStoresForInitAfterBZero(ME, Elt,
                                  Builder.CreateConstInBoundsGEP2_32(Loc, 0, i),
                                  isVolatile, Builder, IsAutoInit);
  }
}

bool shouldUseBZeroPlusStoresToInitialize(llvm::Constant *Init,
                                          uint64_t GlobalSize) {
  // If a global is all zeros, always use a bzero.
  if (isa<llvm::ConstantAggregateZero>(Init))
    return true;

  // If a non-zero global is <= 32 bytes, always use a memcpy.  If it is large,
  // do it if it will require 6 or fewer scalar stores.
  unsigned StoreBudget = 6;
  uint64_t SizeLimit = 32;

  return GlobalSize > SizeLimit &&
         canEmitInitWithFewStoresAfterBZero(Init, StoreBudget);
}

llvm::Value *shouldUseMemSetToInitialize(llvm::Constant *Init,
                                         uint64_t GlobalSize,
                                         const llvm::DataLayout &DL) {
  uint64_t SizeLimit = 32;
  if (GlobalSize <= SizeLimit)
    return nullptr;
  return llvm::isBytewiseValue(Init, DL);
}

bool shouldSplitConstantStore(ModuleEmitter &ME, uint64_t GlobalByteSize) {
  // Don't break things that occupy more than one cacheline.
  uint64_t ByteSizeLimit = 64;
  if (ME.getCodeGenOpts().OptimizationLevel == 0)
    return false;
  if (GlobalByteSize <= ByteSizeLimit)
    return true;
  return false;
}

enum class IsPattern { No, Yes };

llvm::Constant *patternOrZeroFor(ModuleEmitter &ME, IsPattern isPattern,
                                 llvm::Type *Ty) {
  if (isPattern == IsPattern::Yes)
    return initializationPatternFor(ME, Ty);
  else
    return llvm::Constant::getNullValue(Ty);
}

llvm::Constant *constWithPadding(ModuleEmitter &ME, IsPattern isPattern,
                                 llvm::Constant *constant);

llvm::Constant *constStructWithPadding(ModuleEmitter &ME, IsPattern isPattern,
                                       llvm::StructType *STy,
                                       llvm::Constant *constant) {
  const llvm::DataLayout &DL = ME.getDataLayout();
  const llvm::StructLayout *Layout = DL.getStructLayout(STy);
  llvm::Type *Int8Ty = llvm::IntegerType::getInt8Ty(ME.getLLVMContext());
  unsigned SizeSoFar = 0;
  llvm::SmallVector<llvm::Constant *, 8> Values;
  bool NestedIntact = true;
  for (unsigned i = 0, e = STy->getNumElements(); i != e; i++) {
    unsigned CurOff = Layout->getElementOffset(i);
    if (SizeSoFar < CurOff) {
      assert(!STy->isPacked());
      auto *PadTy = llvm::ArrayType::get(Int8Ty, CurOff - SizeSoFar);
      Values.push_back(patternOrZeroFor(ME, isPattern, PadTy));
    }
    llvm::Constant *CurOp;
    if (constant->isZeroValue())
      CurOp = llvm::Constant::getNullValue(STy->getElementType(i));
    else
      CurOp = cast<llvm::Constant>(constant->getAggregateElement(i));
    auto *NewOp = constWithPadding(ME, isPattern, CurOp);
    if (CurOp != NewOp)
      NestedIntact = false;
    Values.push_back(NewOp);
    SizeSoFar = CurOff + DL.getTypeAllocSize(CurOp->getType());
  }
  unsigned TotalSize = Layout->getSizeInBytes();
  if (SizeSoFar < TotalSize) {
    auto *PadTy = llvm::ArrayType::get(Int8Ty, TotalSize - SizeSoFar);
    Values.push_back(patternOrZeroFor(ME, isPattern, PadTy));
  }
  if (NestedIntact && Values.size() == STy->getNumElements())
    return constant;
  return llvm::ConstantStruct::getAnon(Values, STy->isPacked());
}

llvm::Constant *constWithPadding(ModuleEmitter &ME, IsPattern isPattern,
                                 llvm::Constant *constant) {
  llvm::Type *OrigTy = constant->getType();
  if (const auto STy = dyn_cast<llvm::StructType>(OrigTy))
    return constStructWithPadding(ME, isPattern, STy, constant);
  if (auto *ArrayTy = dyn_cast<llvm::ArrayType>(OrigTy)) {
    llvm::SmallVector<llvm::Constant *, 8> Values;
    uint64_t Size = ArrayTy->getNumElements();
    if (!Size)
      return constant;
    llvm::Type *ElemTy = ArrayTy->getElementType();
    bool ZeroInitializer = constant->isNullValue();
    llvm::Constant *OpValue, *PaddedOp;
    if (ZeroInitializer) {
      OpValue = llvm::Constant::getNullValue(ElemTy);
      PaddedOp = constWithPadding(ME, isPattern, OpValue);
    }
    for (unsigned Op = 0; Op != Size; ++Op) {
      if (!ZeroInitializer) {
        OpValue = constant->getAggregateElement(Op);
        PaddedOp = constWithPadding(ME, isPattern, OpValue);
      }
      Values.push_back(PaddedOp);
    }
    auto *NewElemTy = Values[0]->getType();
    if (NewElemTy == ElemTy)
      return constant;
    auto *NewArrayTy = llvm::ArrayType::get(NewElemTy, Size);
    return llvm::ConstantArray::get(NewArrayTy, Values);
  }
  return constant;
}
} // namespace

Address ModuleEmitter::createUnnamedGlobalFrom(const VarDecl &D,
                                               llvm::Constant *Constant,
                                               CharUnits Align) {
  auto FunctionName = [&](const DeclContext *DC) -> std::string {
    if (const auto *FD = dyn_cast<FunctionDecl>(DC)) {
      return std::string(getMangledName(FD));
    } else {
      llvm_unreachable("expected a function");
    }
  };

  // Form a simple per-variable cache of these values in case we find we
  // want to reuse them.
  llvm::GlobalVariable *&CacheEntry = InitializerConstants[&D];
  if (!CacheEntry || CacheEntry->getInitializer() != Constant) {
    auto *Ty = Constant->getType();
    bool isConstant = true;
    llvm::GlobalVariable *InsertBefore = nullptr;
    unsigned AS =
        getContext().getTargetAddressSpace(getGlobalConstantAddressSpace());
    std::string Name;
    if (D.hasGlobalStorage())
      Name = getMangledName(&D).str() + ".const";
    else if (const DeclContext *DC = D.getParentFunctionOrMethod())
      Name = ("__const." + FunctionName(DC) + "." + D.getName()).str();
    else
      llvm_unreachable("local variable has no parent function");
    llvm::GlobalVariable *GV = new llvm::GlobalVariable(
        getModule(), Ty, isConstant, llvm::GlobalValue::PrivateLinkage,
        Constant, Name, InsertBefore, llvm::GlobalValue::NotThreadLocal, AS);
    GV->setAlignment(Align.getAsAlign());
    GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    CacheEntry = GV;
  } else if (CacheEntry->getAlignment() < uint64_t(Align.getQuantity())) {
    CacheEntry->setAlignment(Align.getAsAlign());
  }

  return Address(CacheEntry, CacheEntry->getValueType(), Align);
}

namespace {
Address createUnnamedGlobalForMemcpyFrom(ModuleEmitter &ME, const VarDecl &D,
                                         CGBuilderTy &Builder,
                                         llvm::Constant *Constant,
                                         CharUnits Align) {
  Address SrcPtr = ME.createUnnamedGlobalFrom(D, Constant, Align);
  return SrcPtr.withElementType(ME.Int8Ty);
}

void emitStoresForConstant(ModuleEmitter &ME, const VarDecl &D, Address Loc,
                           bool isVolatile, CGBuilderTy &Builder,
                           llvm::Constant *constant, bool IsAutoInit) {
  auto *Ty = constant->getType();
  uint64_t ConstantSize = ME.getDataLayout().getTypeAllocSize(Ty);
  if (!ConstantSize)
    return;

  bool canDoSingleStore = Ty->isIntOrIntVectorTy() ||
                          Ty->isPtrOrPtrVectorTy() || Ty->isFPOrFPVectorTy();
  if (canDoSingleStore) {
    auto *I = Builder.CreateStore(constant, Loc, isVolatile);
    if (IsAutoInit)
      I->addAnnotationMetadata("auto-init");
    return;
  }

  auto *SizeVal = llvm::ConstantInt::get(ME.IntPtrTy, ConstantSize);

  // If the initializer is all or mostly the same, codegen with bzero / memset
  // then do a few stores afterward.
  if (shouldUseBZeroPlusStoresToInitialize(constant, ConstantSize)) {
    auto *I = Builder.CreateMemSet(Loc, llvm::ConstantInt::get(ME.Int8Ty, 0),
                                   SizeVal, isVolatile);
    if (IsAutoInit)
      I->addAnnotationMetadata("auto-init");

    bool valueAlreadyCorrect =
        constant->isNullValue() || isa<llvm::UndefValue>(constant);
    if (!valueAlreadyCorrect) {
      Loc = Loc.withElementType(Ty);
      emitStoresForInitAfterBZero(ME, constant, Loc, isVolatile, Builder,
                                  IsAutoInit);
    }
    return;
  }

  // If the initializer is a repeated byte pattern, use memset.
  llvm::Value *Pattern =
      shouldUseMemSetToInitialize(constant, ConstantSize, ME.getDataLayout());
  if (Pattern) {
    uint64_t Value = 0x00;
    if (!isa<llvm::UndefValue>(Pattern)) {
      const llvm::APInt &AP = cast<llvm::ConstantInt>(Pattern)->getValue();
      assert(AP.getBitWidth() <= 8);
      Value = AP.getLimitedValue();
    }
    auto *I = Builder.CreateMemSet(
        Loc, llvm::ConstantInt::get(ME.Int8Ty, Value), SizeVal, isVolatile);
    if (IsAutoInit)
      I->addAnnotationMetadata("auto-init");
    return;
  }

  // If the initializer is small, use a handful of stores.
  if (shouldSplitConstantStore(ME, ConstantSize)) {
    if (auto *STy = dyn_cast<llvm::StructType>(Ty)) {
      const llvm::StructLayout *Layout =
          ME.getDataLayout().getStructLayout(STy);
      for (unsigned i = 0; i != constant->getNumOperands(); i++) {
        CharUnits CurOff = CharUnits::fromQuantity(Layout->getElementOffset(i));
        Address EltPtr = Builder.CreateConstInBoundsByteGEP(
            Loc.withElementType(ME.Int8Ty), CurOff);
        emitStoresForConstant(ME, D, EltPtr, isVolatile, Builder,
                              constant->getAggregateElement(i), IsAutoInit);
      }
      return;
    } else if (auto *ATy = dyn_cast<llvm::ArrayType>(Ty)) {
      for (unsigned i = 0; i != ATy->getNumElements(); i++) {
        Address EltPtr = Builder.CreateConstGEP(
            Loc.withElementType(ATy->getElementType()), i);
        emitStoresForConstant(ME, D, EltPtr, isVolatile, Builder,
                              constant->getAggregateElement(i), IsAutoInit);
      }
      return;
    }
  }

  auto *I =
      Builder.CreateMemCpy(Loc,
                           createUnnamedGlobalForMemcpyFrom(
                               ME, D, Builder, constant, Loc.getAlignment()),
                           SizeVal, isVolatile);
  if (IsAutoInit)
    I->addAnnotationMetadata("auto-init");
}

void emitStoresForZeroInit(ModuleEmitter &ME, const VarDecl &D, Address Loc,
                           bool isVolatile, CGBuilderTy &Builder) {
  llvm::Type *ElTy = Loc.getElementType();
  llvm::Constant *constant =
      constWithPadding(ME, IsPattern::No, llvm::Constant::getNullValue(ElTy));
  emitStoresForConstant(ME, D, Loc, isVolatile, Builder, constant,
                        /*IsAutoInit=*/true);
}

void emitStoresForPatternInit(ModuleEmitter &ME, const VarDecl &D, Address Loc,
                              bool isVolatile, CGBuilderTy &Builder) {
  llvm::Type *ElTy = Loc.getElementType();
  llvm::Constant *constant =
      constWithPadding(ME, IsPattern::Yes, initializationPatternFor(ME, ElTy));
  assert(!isa<llvm::UndefValue>(constant));
  emitStoresForConstant(ME, D, Loc, isVolatile, Builder, constant,
                        /*IsAutoInit=*/true);
}

bool containsUndef(llvm::Constant *constant) {
  auto *Ty = constant->getType();
  if (isa<llvm::UndefValue>(constant))
    return true;
  if (Ty->isStructTy() || Ty->isArrayTy() || Ty->isVectorTy())
    for (llvm::Use &Op : constant->operands())
      if (containsUndef(cast<llvm::Constant>(Op)))
        return true;
  return false;
}

llvm::Constant *replaceUndef(ModuleEmitter &ME, IsPattern isPattern,
                             llvm::Constant *constant) {
  auto *Ty = constant->getType();
  if (isa<llvm::UndefValue>(constant))
    return patternOrZeroFor(ME, isPattern, Ty);
  if (!(Ty->isStructTy() || Ty->isArrayTy() || Ty->isVectorTy()))
    return constant;
  if (!containsUndef(constant))
    return constant;
  llvm::SmallVector<llvm::Constant *, 8> Values(constant->getNumOperands());
  for (unsigned Op = 0, NumOp = constant->getNumOperands(); Op != NumOp; ++Op) {
    auto *OpValue = cast<llvm::Constant>(constant->getOperand(Op));
    Values[Op] = replaceUndef(ME, isPattern, OpValue);
  }
  if (Ty->isStructTy())
    return llvm::ConstantStruct::get(cast<llvm::StructType>(Ty), Values);
  if (Ty->isArrayTy())
    return llvm::ConstantArray::get(cast<llvm::ArrayType>(Ty), Values);
  assert(Ty->isVectorTy());
  return llvm::ConstantVector::get(Values);
}
} // namespace

// ===----------------------------------------------------------------------===
// Automatic variable allocation & lifetime
// ===----------------------------------------------------------------------===

void FunctionEmitter::genAutoVarDecl(const VarDecl &D) {
  AutoVarEmission emission = genAutoVarAlloca(D);
  genAutoVarInit(emission);
  genAutoVarCleanups(emission);
}

llvm::Value *FunctionEmitter::genLifetimeStart(llvm::TypeSize Size,
                                               llvm::Value *Addr) {
  if (!ShouldEmitLifetimeMarkers)
    return nullptr;

  assert(Addr->getType()->getPointerAddressSpace() ==
             ME.getDataLayout().getAllocaAddrSpace() &&
         "Pointer should be in alloca address space");
  llvm::Value *SizeV = llvm::ConstantInt::get(
      Int64Ty, Size.isScalable() ? -1 : Size.getFixedValue());
  llvm::CallInst *C =
      Builder.CreateCall(ME.getLLVMLifetimeStartFn(), {SizeV, Addr});
  C->setDoesNotThrow();
  return SizeV;
}

void FunctionEmitter::genLifetimeEnd(llvm::Value *Size, llvm::Value *Addr) {
  if (!ShouldEmitLifetimeMarkers)
    return;
  assert(Addr->getType()->getPointerAddressSpace() ==
             ME.getDataLayout().getAllocaAddrSpace() &&
         "Pointer should be in alloca address space");
  llvm::CallInst *C =
      Builder.CreateCall(ME.getLLVMLifetimeEndFn(), {Size, Addr});
  C->setDoesNotThrow();
}

void FunctionEmitter::genAndRegisterVariableArrayDimensions(DebugEmitter *DI,
                                                            const VarDecl &D,
                                                            bool genDebugInfo) {
  // For each dimension stores its QualType and corresponding
  // size-expression Value.
  llvm::SmallVector<FunctionEmitter::VlaSizePair, 4> Dimensions;
  llvm::SmallVector<IdentifierInfo *, 4> VLAExprNames;

  // Break down the array into individual dimensions.
  QualType Type1D = D.getType();
  while (getContext().getAsVariableArrayType(Type1D)) {
    auto VlaSize = getVLAElements1D(Type1D);
    if (auto *C = dyn_cast<llvm::ConstantInt>(VlaSize.NumElts))
      Dimensions.emplace_back(C, Type1D.getUnqualifiedType());
    else {
      // Generate a locally unique name for the size expression.
      llvm::Twine Name =
          llvm::Twine("__vla_expr") + llvm::Twine(VLAExprCounter++);
      llvm::SmallString<12> Buffer;
      llvm::StringRef NameRef = Name.toStringRef(Buffer);
      auto &Ident = getContext().Idents.getOwn(NameRef);
      VLAExprNames.push_back(&Ident);
      auto SizeExprAddr =
          createDefaultAlignTempAlloca(VlaSize.NumElts->getType(), NameRef);
      Builder.CreateStore(VlaSize.NumElts, SizeExprAddr);
      Dimensions.emplace_back(SizeExprAddr.getPointer(),
                              Type1D.getUnqualifiedType());
    }
    Type1D = VlaSize.Type;
  }

  if (!genDebugInfo)
    return;

  // Register each dimension's size-expression with a DILocalVariable,
  // so that it can be used by DebugEmitter when instantiating a DISubrange
  // to describe this array.
  unsigned NameIdx = 0;
  for (auto &VlaSize : Dimensions) {
    llvm::Metadata *MD;
    if (auto *C = dyn_cast<llvm::ConstantInt>(VlaSize.NumElts))
      MD = llvm::ConstantAsMetadata::get(C);
    else {
      IdentifierInfo *NameIdent = VLAExprNames[NameIdx++];
      auto QT = getContext().getIntTypeForBitwidth(
          SizeTy->getScalarSizeInBits(), false);
      auto *ArtificialDecl = VarDecl::Create(
          getContext(), const_cast<DeclContext *>(D.getDeclContext()),
          D.getLocation(), D.getLocation(), NameIdent, QT,
          getContext().CreateTypeSourceInfo(QT), SC_Auto);
      ArtificialDecl->setImplicit();

      MD = DI->genDeclareOfAutoVariable(ArtificialDecl, VlaSize.NumElts,
                                        Builder);
    }
    assert(MD && "No Size expression debug node created");
    DI->registerVLASizeExpression(VlaSize.Type, MD);
  }
}

__attribute__((hot)) FunctionEmitter::AutoVarEmission
FunctionEmitter::genAutoVarAlloca(const VarDecl &D) {
  QualType Ty = D.getType();
  assert(Ty.getAddressSpace() == LangAS::Default);

  AutoVarEmission emission(D);

  CharUnits alignment = getContext().getDeclAlign(&D);

  // If the type is variably-modified, emit all the VLA sizes for it.
  if (Ty->isVariablyModifiedType())
    genVariablyModifiedType(Ty);

  auto *DI = getDebugInfo();
  bool genDebugInfo = DI && ME.getCodeGenOpts().hasReducedDebugInfo();

  Address address = Address::invalid();
  Address AllocaAddr = Address::invalid();

  if (Ty->isConstantSizeType()) {
    if (D.getInit() && (Ty->isArrayType() || Ty->isRecordType()) &&
        (D.isConstexpr() ||
         (Ty.isPODType(getContext()) &&
          D.getInit()->isConstantInitializer(getContext(), false)))) {

      if ((ME.getCodeGenOpts().MergeAllConstants &&
           Ty.isConstantStorage(getContext(), true, true))) {
        genStaticVarDecl(D, llvm::GlobalValue::InternalLinkage);

        emission.Addr = Address::invalid();
        assert(emission.wasEmittedAsGlobal());
        return emission;
      }

      emission.IsConstantAggregate = true;
    }

    {
      llvm::Type *allocaTy = convertTypeForMem(Ty);
      CharUnits allocaAlignment = alignment;

      // Name set separately so it persists in no-asserts builds.
      // builds.
      address = createTempAlloca(allocaTy, allocaAlignment, D.getName(),
                                 /*ArraySize=*/nullptr, &AllocaAddr);

      // Emit a lifetime intrinsic if meaningful. There's no point in doing this
      // if we don't have a valid insertion point (?).
      if (haveInsertPoint()) {
        // If there's a jump into the lifetime of this variable, its lifetime
        // gets broken up into several regions in IR, which requires more work
        // to handle correctly. For now, just omit the intrinsics; this is a
        // rare case, and it's better to just be conservatively correct.
        // PR28267.
        //
        // We have to do this in all language modes if there's a jump past the
        // declaration. We also have to do it in C if there's a jump to an
        // earlier point in the current block because non-VLA lifetimes begin as
        // soon as the containing block is entered, not when its variables
        // actually come into scope; suppressing the lifetime annotations
        // completely in this case is unnecessarily pessimistic, but again, this
        // is rare.
        if (!Bypasses.IsBypassed(&D) && !hasLabelBeenSeenInCurrentScope()) {
          llvm::TypeSize Size = ME.getDataLayout().getTypeAllocSize(allocaTy);
          emission.SizeForLifetimeMarkers =
              genLifetimeStart(Size, AllocaAddr.getPointer());
        }
      } else {
        assert(!emission.useLifetimeMarkers());
      }
    }
  } else {
    ensureInsertPoint();

    {
      if (!DidCallStackSave) {
        // Save the stack.
        Address Stack =
            createDefaultAlignTempAlloca(AllocaInt8PtrTy, "saved_stack");

        llvm::Value *V = Builder.CreateStackSave();
        assert(V->getType() == AllocaInt8PtrTy);
        Builder.CreateStore(V, Stack);

        DidCallStackSave = true;

        // Push a cleanup block and restore the stack there.
        pushStackRestore(NormalCleanup, Stack);
      }

      auto VlaSize = getVLASize(Ty);
      llvm::Type *llvmTy = convertTypeForMem(VlaSize.Type);

      // Allocate memory for the array.
      address = createTempAlloca(llvmTy, alignment, "vla", VlaSize.NumElts,
                                 &AllocaAddr);
    }

    // If we have debug info enabled, properly describe the VLA dimensions for
    // this type by registering the vla size expression for each of the
    // dimensions.
    genAndRegisterVariableArrayDimensions(DI, D, genDebugInfo);
  }

  setAddrOfLocalVar(&D, address);
  emission.Addr = address;
  emission.AllocaAddr = AllocaAddr;

  if (genDebugInfo && haveInsertPoint()) {
    DI->setLocation(D.getLocation());
    (void)DI->genDeclareOfAutoVariable(&D, AllocaAddr.getPointer(), Builder,
                                       false);
  }

  if (D.hasAttr<AnnotateAttr>() && haveInsertPoint())
    genVarAnnotations(&D, address.getPointer());

  if (emission.useLifetimeMarkers())
    EHStack.pushCleanup<CallLifetimeEnd>(NormalEHLifetimeMarker,
                                         emission.getOriginalAllocatedAddress(),
                                         emission.getSizeForLifetimeMarkers());

  return emission;
}

bool FunctionEmitter::isTrivialInitializer(const Expr *Init) {
  if (!Init)
    return true;

  return false;
}

void FunctionEmitter::emitZeroOrPatternForAutoVarInit(QualType type,
                                                      const VarDecl &D,
                                                      Address Loc) {
  auto trivialAutoVarInit = getContext().getLangOpts().getTrivialAutoVarInit();
  CharUnits Size = getContext().getTypeSizeInChars(type);
  bool isVolatile = type.isVolatileQualified();
  if (!Size.isZero()) {
    switch (trivialAutoVarInit) {
    case LangOptions::TrivialAutoVarInitKind::Uninitialized:
      llvm_unreachable("Uninitialized handled by caller");
    case LangOptions::TrivialAutoVarInitKind::Zero:
      if (ME.stopAutoInit())
        return;
      emitStoresForZeroInit(ME, D, Loc, isVolatile, Builder);
      break;
    case LangOptions::TrivialAutoVarInitKind::Pattern:
      if (ME.stopAutoInit())
        return;
      emitStoresForPatternInit(ME, D, Loc, isVolatile, Builder);
      break;
    }
    return;
  }

  // VLAs look zero-sized to getTypeInfo. We can't emit constant stores to
  // them, so emit a memcpy with the VLA size to initialize each element.
  // Technically zero-sized or negative-sized VLAs are undefined, and UBSan
  // will catch that code, but there exists code which generates zero-sized
  // VLAs. Be nice and initialize whatever they requested.
  const auto *VlaType = getContext().getAsVariableArrayType(type);
  if (!VlaType)
    return;
  auto VlaSize = getVLASize(VlaType);
  auto SizeVal = VlaSize.NumElts;
  CharUnits EltSize = getContext().getTypeSizeInChars(VlaSize.Type);
  switch (trivialAutoVarInit) {
  case LangOptions::TrivialAutoVarInitKind::Uninitialized:
    llvm_unreachable("Uninitialized handled by caller");

  case LangOptions::TrivialAutoVarInitKind::Zero: {
    if (ME.stopAutoInit())
      return;
    if (!EltSize.isOne())
      SizeVal = Builder.CreateNUWMul(SizeVal, ME.getSize(EltSize));
    auto *I = Builder.CreateMemSet(Loc, llvm::ConstantInt::get(Int8Ty, 0),
                                   SizeVal, isVolatile);
    I->addAnnotationMetadata("auto-init");
    break;
  }

  case LangOptions::TrivialAutoVarInitKind::Pattern: {
    if (ME.stopAutoInit())
      return;
    llvm::Type *ElTy = Loc.getElementType();
    llvm::Constant *Constant = constWithPadding(
        ME, IsPattern::Yes, initializationPatternFor(ME, ElTy));
    CharUnits ConstantAlign = getContext().getTypeAlignInChars(VlaSize.Type);
    llvm::BasicBlock *SetupBB = createBasicBlock("vla-setup.loop");
    llvm::BasicBlock *LoopBB = createBasicBlock("vla-init.loop");
    llvm::BasicBlock *ContBB = createBasicBlock("vla-init.cont");
    llvm::Value *IsZeroSizedVLA = Builder.CreateICmpEQ(
        SizeVal, llvm::ConstantInt::get(SizeVal->getType(), 0),
        "vla.iszerosized");
    Builder.CreateCondBr(IsZeroSizedVLA, ContBB, SetupBB);
    genBlock(SetupBB);
    if (!EltSize.isOne())
      SizeVal = Builder.CreateNUWMul(SizeVal, ME.getSize(EltSize));
    llvm::Value *BaseSizeInChars =
        llvm::ConstantInt::get(IntPtrTy, EltSize.getQuantity());
    Address Begin = Loc.withElementType(Int8Ty);
    llvm::Value *End = Builder.CreateInBoundsGEP(
        Begin.getElementType(), Begin.getPointer(), SizeVal, "vla.end");
    llvm::BasicBlock *OriginBB = Builder.GetInsertBlock();
    genBlock(LoopBB);
    llvm::PHINode *Cur = Builder.CreatePHI(Begin.getType(), 2, "vla.cur");
    Cur->addIncoming(Begin.getPointer(), OriginBB);
    CharUnits CurAlign = Loc.getAlignment().alignmentOfArrayElement(EltSize);
    auto *I = Builder.CreateMemCpy(Address(Cur, Int8Ty, CurAlign),
                                   createUnnamedGlobalForMemcpyFrom(
                                       ME, D, Builder, Constant, ConstantAlign),
                                   BaseSizeInChars, isVolatile);
    I->addAnnotationMetadata("auto-init");
    llvm::Value *Next =
        Builder.CreateInBoundsGEP(Int8Ty, Cur, BaseSizeInChars, "vla.next");
    llvm::Value *Done = Builder.CreateICmpEQ(Next, End, "vla-init.isdone");
    Builder.CreateCondBr(Done, ContBB, LoopBB);
    Cur->addIncoming(Next, LoopBB);
    genBlock(ContBB);
  } break;
  }
}

void FunctionEmitter::genAutoVarInit(const AutoVarEmission &emission) {
  assert(emission.Variable && "emission was not valid!");

  if (emission.wasEmittedAsGlobal())
    return;

  const VarDecl &D = *emission.Variable;
  auto DL = ApplyDebugLocation::CreateDefaultArtificial(*this, D.getLocation());
  QualType type = D.getType();
  const Expr *Init = D.getInit();

  // If we are at an unreachable point, we don't need to emit the initializer
  // unless it contains a label.
  if (!haveInsertPoint()) {
    if (!Init || !containsLabel(Init))
      return;
    ensureInsertPoint();
  }

  const Address Loc = emission.getObjectAddress(*this);

  // Note: constexpr already initializes everything correctly.
  LangOptions::TrivialAutoVarInitKind trivialAutoVarInit =
      (D.isConstexpr()
           ? LangOptions::TrivialAutoVarInitKind::Uninitialized
           : (D.getAttr<UninitializedAttr>()
                  ? LangOptions::TrivialAutoVarInitKind::Uninitialized
                  : getContext().getLangOpts().getTrivialAutoVarInit()));

  auto initializeWhatIsTechnicallyUninitialized = [&](Address Loc) {
    if (trivialAutoVarInit ==
        LangOptions::TrivialAutoVarInitKind::Uninitialized)
      return;

    return emitZeroOrPatternForAutoVarInit(type, D, Loc);
  };

  if (isTrivialInitializer(Init))
    return initializeWhatIsTechnicallyUninitialized(Loc);

  llvm::Constant *constant = nullptr;
  if (emission.IsConstantAggregate) {
    constant = ConstantEmitter(*this).tryEmitAbstractForInitializer(D);
    if (constant && !constant->isZeroValue() &&
        (trivialAutoVarInit !=
         LangOptions::TrivialAutoVarInitKind::Uninitialized)) {
      IsPattern isPattern =
          (trivialAutoVarInit == LangOptions::TrivialAutoVarInitKind::Pattern)
              ? IsPattern::Yes
              : IsPattern::No;
      // C guarantees that brace-init with fewer initializers than members in
      // the aggregate will initialize the rest of the aggregate as-if it were
      // static initialization. In turn static initialization guarantees that
      // padding is initialized to zero bits. We could instead pattern-init if D
      // has any ImplicitValueInitExpr, but that seems to be unintuitive
      // behavior.
      constant = constWithPadding(ME, IsPattern::No,
                                  replaceUndef(ME, isPattern, constant));
    }
  }

  if (!constant) {
    initializeWhatIsTechnicallyUninitialized(Loc);
    LValue lv = makeAddrLValue(Loc, type);
    return genExprAsInit(Init, &D, lv, false);
  }

  if (!emission.IsConstantAggregate) {
    LValue lv = makeAddrLValue(Loc, type);
    return genStoreThroughLValue(RValue::get(constant), lv, true);
  }

  emitStoresForConstant(ME, D, Loc.withElementType(ME.Int8Ty),
                        type.isVolatileQualified(), Builder, constant,
                        /*IsAutoInit=*/false);
}

void FunctionEmitter::genExprAsInit(const Expr *init, const ValueDecl *D,
                                    LValue lvalue, bool) {
  QualType type = D->getType();

  switch (getEvaluationKind(type)) {
  case TEK_Scalar:
    genScalarInit(init, D, lvalue, false);
    return;
  case TEK_Complex: {
    ComplexPairTy complex = genComplexExpr(init);
    genStoreOfComplex(complex, lvalue, /*init*/ true);
    return;
  }
  case TEK_Aggregate:
    if (type->isAtomicType()) {
      genAtomicInit(const_cast<Expr *>(init), lvalue);
    } else {
      AggValueSlot::Overlap_t Overlap = AggValueSlot::MayOverlap;
      if (isa<VarDecl>(D))
        Overlap = AggValueSlot::DoesNotOverlap;
      else if (auto *FD = dyn_cast<FieldDecl>(D))
        Overlap = getOverlapForFieldInit(FD);
      genAggExpr(init, AggValueSlot::forLValue(
                           lvalue, *this, AggValueSlot::IsDestructed,
                           AggValueSlot::IsNotAliased, Overlap));
    }
    return;
  }
  llvm_unreachable("bad evaluation kind");
}

void FunctionEmitter::genAutoVarCleanups(const AutoVarEmission &emission) {
  assert(emission.Variable && "emission was not valid!");

  if (emission.wasEmittedAsGlobal())
    return;

  if (!haveInsertPoint())
    return;

  const VarDecl &D = *emission.Variable;

  if (const CleanupAttr *CA = D.getAttr<CleanupAttr>()) {
    const FunctionDecl *FD = CA->getFunctionDecl();

    llvm::Constant *F = ME.addrOfFunction(FD);
    assert(F && "Could not find function!");

    const ABIFunctionInfo &Info = ME.getTypes().arrangeFunctionDeclaration(FD);
    EHStack.pushCleanup<CallCleanupFunction>(NormalAndEHCleanup, F, &Info, &D);
  }
}

void FunctionEmitter::pushStackRestore(CleanupKind Kind, Address SPMem) {
  EHStack.pushCleanup<CallStackRestore>(Kind, SPMem);
}

llvm::Function *ModuleEmitter::getLLVMLifetimeStartFn() {
  if (LifetimeStartFn)
    return LifetimeStartFn;
  LifetimeStartFn = llvm::Intrinsic::getDeclaration(
      &getModule(), llvm::Intrinsic::lifetime_start, AllocaInt8PtrTy);
  return LifetimeStartFn;
}

llvm::Function *ModuleEmitter::getLLVMLifetimeEndFn() {
  if (LifetimeEndFn)
    return LifetimeEndFn;
  LifetimeEndFn = llvm::Intrinsic::getDeclaration(
      &getModule(), llvm::Intrinsic::lifetime_end, AllocaInt8PtrTy);
  return LifetimeEndFn;
}

__attribute__((hot)) void
FunctionEmitter::genParmDecl(const VarDecl &D, ParamValue Arg, unsigned ArgNo) {
  assert((isa<ParmVarDecl>(D) || isa<ImplicitParamDecl>(D)) &&
         "Invalid argument to genParmDecl");

  // Set the name of the parameter's initial value to make IR easier to
  // read. Don't modify the names of globals.
  if (!isa<llvm::GlobalValue>(Arg.getAnyValue()))
    Arg.getAnyValue()->setName(D.getName());

  QualType Ty = D.getType();

  Address DeclPtr = Address::invalid();
  Address AllocaPtr = Address::invalid();
  bool DoStore = false;
  bool UseIndirectDebugAddress = false;

  // If we already have a pointer to the argument, reuse the input pointer.
  if (Arg.isIndirect()) {
    DeclPtr = Arg.getIndirectAddress();
    DeclPtr = DeclPtr.withElementType(convertTypeForMem(Ty));
    // Indirect argument is in alloca address space, which may be different
    // from the default address space.
    auto AllocaAS = ME.getASTAllocaAddressSpace();
    auto *V = DeclPtr.getPointer();
    AllocaPtr = DeclPtr;

    // For truly ABI indirect arguments -- those that are not `byval` -- store
    // the address of the argument on the stack to preserve debug information.
    ABIArgInfo ArgInfo = CurFnInfo->arguments()[ArgNo - 1].info;
    if (ArgInfo.isIndirect())
      UseIndirectDebugAddress = !ArgInfo.getIndirectByVal();
    if (UseIndirectDebugAddress) {
      auto PtrTy = getContext().getPointerType(Ty);
      AllocaPtr = createMemTemp(PtrTy, getContext().getTypeAlignInChars(PtrTy),
                                D.getName() + ".indirect_addr");
      genStoreOfScalar(V, AllocaPtr, /* Volatile */ false, PtrTy);
    }

    auto SrcLangAS = AllocaAS;
    auto DestLangAS = LangAS::Default;
    if (SrcLangAS != DestLangAS) {
      assert(getContext().getTargetAddressSpace(SrcLangAS) ==
             ME.getDataLayout().getAllocaAddrSpace());
      auto DestAS = getContext().getTargetAddressSpace(DestLangAS);
      auto *T = llvm::PointerType::get(getLLVMContext(), DestAS);
      DeclPtr =
          DeclPtr.withPointer(getTargetHooks().performAddrSpaceCast(
                                  *this, V, SrcLangAS, DestLangAS, T, true),
                              DeclPtr.isKnownNonNull());
    }

  } else {
    DeclPtr = createMemTemp(Ty, getContext().getDeclAlign(&D),
                            D.getName() + ".addr", &AllocaPtr);
    DoStore = true;
  }

  llvm::Value *ArgVal = (DoStore ? Arg.getDirectValue() : nullptr);
  LValue lv = makeAddrLValue(DeclPtr, Ty);

  if (DoStore)
    genStoreOfScalar(ArgVal, lv, /* isInitialization */ true);

  setAddrOfLocalVar(&D, DeclPtr);

  if (DebugEmitter *DI = getDebugInfo()) {
    if (ME.getCodeGenOpts().hasReducedDebugInfo()) {
      DI->genDeclareOfArgVariable(&D, AllocaPtr.getPointer(), ArgNo, Builder,
                                  UseIndirectDebugAddress);
    }
  }

  if (D.hasAttr<AnnotateAttr>())
    genVarAnnotations(&D, DeclPtr.getPointer());

  if (const CleanupAttr *CA = D.getAttr<CleanupAttr>()) {
    const FunctionDecl *FD = CA->getFunctionDecl();

    llvm::Constant *F = ME.addrOfFunction(FD);
    assert(F && "Could not find function!");

    const ABIFunctionInfo &Info = ME.getTypes().arrangeFunctionDeclaration(FD);
    EHStack.pushCleanup<CallCleanupFunction>(NormalAndEHCleanup, F, &Info, &D);
  }
}
