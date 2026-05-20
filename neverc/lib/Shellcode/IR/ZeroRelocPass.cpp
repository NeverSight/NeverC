#include "neverc/Shellcode/IR/ZeroRelocPass.h"
#include "ExtractorCommon.h"
#include "neverc/Shellcode/IR/ZeroRelocABI.h"
#include "neverc/Shellcode/Pipeline/ShellcodeIRHelperNames.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

namespace neverc {
namespace shellcode {

namespace {

bool hadHardError(Module &M) {
  return M.getNamedMetadata(ZeroRelocABI::HardErrorSentinel) != nullptr;
}

void reportError(Module &M, const Twine &Msg) {
  if (M.begin() != M.end())
    M.getContext().diagnose(
        DiagnosticInfoUnsupported(*M.begin(), "shellcode: " + Msg));
  else
    errs() << "error: shellcode: " << Msg << "\n";
  M.getOrInsertNamedMetadata(ZeroRelocABI::HardErrorSentinel);
}

Function *findEntry(Module &M, StringRef UserEntry) {
  Function *First = nullptr;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (isShellcodeEntryCandidate(F.getName(), UserEntry))
      return &F;
    if (!First)
      First = &F;
  }
  return First;
}

bool prep(Module &M, StringRef UserEntry, Function *Entry) {
  bool Changed = false;

  for (const char *Name : {"llvm.global_ctors", "llvm.global_dtors"}) {
    auto *GV = M.getNamedGlobal(Name);
    if (!GV)
      continue;
    if (auto *Init = GV->getInitializer())
      if (auto *Arr = dyn_cast<ConstantArray>(Init))
        if (Arr->getNumOperands() > 0) {
          reportError(M, StringRef(Name) == "llvm.global_ctors"
                             ? "global constructors are not allowed; move the "
                               "initialization into the entry function"
                             : "global destructors are not allowed");
          return false;
        }
  }

  for (GlobalVariable &GV : M.globals()) {
    StringRef Name = GV.getName();
    if (Name.starts_with(ir::kLlvmDotPrefix))
      continue;
    if (GV.isThreadLocal()) {
      GV.setThreadLocalMode(GlobalValue::NotThreadLocal);
      Changed = true;
    }
    if (GV.hasExternalWeakLinkage()) {
      reportError(M, "external_weak global '" + Name + "' is not allowed");
      return false;
    }
  }

  for (const char *Name : {"llvm.used", "llvm.compiler.used"}) {
    if (auto *GV = M.getNamedGlobal(Name)) {
      GV->eraseFromParent();
      Changed = true;
    }
  }

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (&F == Entry) {
      F.setLinkage(GlobalValue::ExternalLinkage);
      F.setDSOLocal(true);
      F.removeFnAttr(Attribute::OptimizeNone);
      continue;
    }
    if (!F.hasLocalLinkage()) {
      F.setLinkage(GlobalValue::InternalLinkage);
      Changed = true;
    }
    if (!F.hasFnAttribute(Attribute::AlwaysInline) &&
        !F.hasFnAttribute(Attribute::NoInline)) {
      F.addFnAttr(Attribute::AlwaysInline);
      F.removeFnAttr(Attribute::OptimizeNone);
      Changed = true;
    }
  }

  return Changed;
}

bool inlineFunctionIntoCallers(Function &F) {
  if (F.isDeclaration() || !F.hasLocalLinkage())
    return false;

  F.removeFnAttr(Attribute::NoInline);
  F.removeFnAttr(Attribute::OptimizeNone);
  F.addFnAttr(Attribute::AlwaysInline);

  SmallVector<CallBase *, 8> Calls;
  for (User *U : F.users()) {
    auto *CB = dyn_cast<CallBase>(U);
    if (!CB || CB->getCalledFunction() != &F)
      continue;
    Calls.push_back(CB);
  }

  bool Changed = false;
  for (CallBase *CB : Calls) {
    InlineFunctionInfo IFI;
    InlineResult Result = InlineFunction(*CB, IFI, /*MergeAttributes=*/true);
    Changed |= Result.isSuccess();
  }
  return Changed;
}

bool inlineNonEntryUsersOfMutableGlobals(ArrayRef<GlobalVariable *> Work,
                                         Function &Entry) {
  SmallVector<Function *, 16> Candidates;
  SmallPtrSet<Function *, 8> Seen;
  SmallPtrSet<User *, 16> Visited;
  SmallVector<User *, 32> Stack;
  for (GlobalVariable *GV : Work) {
    for (User *U : GV->users())
      Stack.push_back(U);
    while (!Stack.empty()) {
      User *U = Stack.pop_back_val();
      if (!Visited.insert(U).second)
        continue;
      if (auto *I = dyn_cast<Instruction>(U)) {
        Function *Owner = I->getFunction();
        if (Owner && Owner != &Entry && Seen.insert(Owner).second)
          Candidates.push_back(Owner);
        continue;
      }
      if (auto *C = dyn_cast<Constant>(U))
        for (User *UU : C->users())
          Stack.push_back(UU);
    }
  }

  bool Changed = false;
  for (Function *F : Candidates)
    if (inlineFunctionIntoCallers(*F))
      Changed = true;
  return Changed;
}

Instruction *materializeConstantExpr(ConstantExpr *CE,
                                     Instruction *InsertBefore, Value *From,
                                     Value *To) {
  Instruction *NewI = CE->getAsInstruction();
  NewI->insertBefore(InsertBefore->getIterator());

  for (Use &Op : NewI->operands()) {
    if (Op.get() == From) {
      Op.set(To);
      continue;
    }
    if (auto *Nested = dyn_cast<ConstantExpr>(Op.get())) {
      Instruction *NestedI = materializeConstantExpr(Nested, NewI, From, To);
      Op.set(NestedI);
    }
  }

  return NewI;
}

bool stackifyMutableGlobals(Module &M, Function *Entry) {
  if (!Entry || Entry->isDeclaration())
    return false;

  bool Changed = false;
  SmallVector<GlobalVariable *, 8> Work;
  Work.reserve(M.global_size());
  for (GlobalVariable &GV : M.globals()) {
    if (GV.getName().starts_with(ir::kLlvmDotPrefix))
      continue;
    if (GV.isConstant())
      continue;
    if (!GV.hasInitializer())
      continue;
    Work.push_back(&GV);
  }

  Changed |= inlineNonEntryUsersOfMutableGlobals(Work, *Entry);

  SmallVector<Instruction *, 16> DirectInstructionUsers;
  SmallVector<ConstantExpr *, 16> ConstantExprUsers;
  SmallPtrSet<User *, 16> Visited;
  SmallPtrSet<ConstantExpr *, 8> SeenCEs;
  SmallVector<User *, 16> Stack;

  for (GlobalVariable *GV : Work) {
    DirectInstructionUsers.clear();
    ConstantExprUsers.clear();
    Visited.clear();
    SeenCEs.clear();
    Stack.clear();
    Stack.append(GV->users().begin(), GV->users().end());

    bool AllInEntry = true;
    StringRef OffendingFunction;

    while (!Stack.empty()) {
      User *U = Stack.pop_back_val();
      if (!Visited.insert(U).second)
        continue;
      if (auto *I = dyn_cast<Instruction>(U)) {
        DirectInstructionUsers.push_back(I);
        if (I->getFunction() != Entry && AllInEntry) {
          AllInEntry = false;
          OffendingFunction = I->getFunction()->getName();
        }
        continue;
      }
      if (auto *CE = dyn_cast<ConstantExpr>(U)) {
        if (SeenCEs.insert(CE).second)
          ConstantExprUsers.push_back(CE);
        for (User *UU : CE->users())
          Stack.push_back(UU);
        continue;
      }
      if (auto *C = dyn_cast<Constant>(U)) {
        for (User *UU : C->users())
          Stack.push_back(UU);
        continue;
      }
      AllInEntry = false;
    }

    if (!AllInEntry) {
      reportError(M,
                  "mutable global '" + GV->getName() +
                      "' is referenced outside the entry function; "
                      "first remaining reference is in function '" +
                      OffendingFunction +
                      "'; "
                      "the shellcode optimizer could not inline every caller. "
                      "Mark those helpers `static inline` or give them "
                      "internal linkage.");
      return false;
    }

    BasicBlock &EntryBB = Entry->getEntryBlock();
    IRBuilder<> Builder(&EntryBB, EntryBB.getFirstInsertionPt());

    Type *ValTy = GV->getValueType();
    auto *Alloca =
        Builder.CreateAlloca(ValTy, nullptr, GV->getName() + ".slot");
    if (MaybeAlign A = GV->getAlign())
      Alloca->setAlignment(*A);

    Constant *Init = GV->getInitializer();
    if (Init && !isa<UndefValue>(Init))
      Builder.CreateStore(Init, Alloca);

    for (Instruction *I : DirectInstructionUsers)
      I->replaceUsesOfWith(GV, Alloca);

    for (ConstantExpr *CE : ConstantExprUsers) {
      SmallVector<User *, 4> CEUsers(CE->users());
      for (User *CEU : CEUsers) {
        auto *IC = dyn_cast<Instruction>(CEU);
        if (!IC || IC->getFunction() != Entry)
          continue;
        Instruction *NewI = materializeConstantExpr(CE, IC, GV, Alloca);
        IC->replaceUsesOfWith(CE, NewI);
      }
    }

    GV->removeDeadConstantUsers();

    if (GV->use_empty()) {
      GV->eraseFromParent();
      Changed = true;
    } else {
      std::string UserKind = "unknown";
      if (User *U = *GV->user_begin()) {
        raw_string_ostream OS(UserKind);
        U->printAsOperand(OS, /*PrintType=*/false);
      }
      reportError(M, "internal: could not remove global '" + GV->getName() +
                         "' after stackifying; remaining user is " + UserKind);
      return false;
    }
  }

  return Changed;
}

void classifyInitializer(const Constant *C, bool &HasGlobalRef,
                         bool &HasBlockAddress) {
  if (!C || (HasGlobalRef && HasBlockAddress))
    return;
  if (isa<GlobalValue>(C))
    HasGlobalRef = true;
  if (isa<BlockAddress>(C))
    HasBlockAddress = true;
  if (HasGlobalRef && HasBlockAddress)
    return;
  if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    for (const Use &U : CE->operands())
      if (auto *OpC = dyn_cast<Constant>(U.get()))
        classifyInitializer(OpC, HasGlobalRef, HasBlockAddress);
    return;
  }
  if (isa<ConstantAggregate>(C)) {
    for (const Use &U : C->operands())
      if (auto *OpC = dyn_cast<Constant>(U.get()))
        classifyInitializer(OpC, HasGlobalRef, HasBlockAddress);
  }
}

void placeEntryFirst(Module &M, Function *Entry) {
  if (!Entry || Entry->isDeclaration())
    return;
  auto &FnList = M.getFunctionList();
  if (FnList.empty() || &FnList.front() == Entry)
    return;
  FnList.remove(Entry);
  FnList.push_front(Entry);
}

bool validate(Module &M) {
  for (GlobalVariable &GV : M.globals()) {
    if (GV.getName().starts_with(ir::kLlvmDotPrefix))
      continue;
    Constant *Init = GV.hasInitializer() ? GV.getInitializer() : nullptr;
    Twine Name = GV.getName();

    bool HasGlobalRef = false, HasBlockAddress = false;
    if (Init)
      classifyInitializer(Init, HasGlobalRef, HasBlockAddress);

    if (HasBlockAddress) {
      reportError(M, "'" + Name +
                         "' contains a BlockAddress (`&&label` from GCC's "
                         "computed-goto extension). Shellcode cannot carry "
                         "the load-time relocations the backend needs to "
                         "materialise a basic-block address; rewrite the "
                         "`goto *labels[...]` dispatch as a plain `switch` "
                         "- the compiler lowers it to a compare-branch "
                         "chain that needs no data section.");
      return false;
    }
    if (GV.isConstant() && HasGlobalRef) {
      reportError(M, "constant '" + Name +
                         "' contains pointers to other globals or string "
                         "literals; shellcode cannot stackify such an "
                         "initializer because the pointers would need the "
                         "runtime load address. Rewrite it so the strings / "
                         "targets live inside the function body, or build "
                         "the table at runtime in the entry function.");
      return false;
    }
    if (!GV.isConstant() && HasGlobalRef) {
      reportError(M, "mutable global '" + Name +
                         "' is initialised with a pointer to another global; "
                         "shellcode has no loader to relocate that pointer. "
                         "Initialise the field at runtime in the entry "
                         "function instead.");
      return false;
    }
    reportError(M, "internal: leftover global variable '" + Name +
                       "' after shellcode pipeline");
    return false;
  }
  return true;
}

} // namespace

PreservedAnalyses ZeroRelocPass::run(Module &M, ModuleAnalysisManager &) {
  StringRef UserEntry(EntrySymbol);

  if (hadHardError(M))
    return PreservedAnalyses::all();

  Function *Entry = findEntry(M, UserEntry);

  bool Changed = prep(M, UserEntry, Entry);

  NamedMDNode *Sentinel = M.getNamedMetadata(ZeroRelocABI::StackifiedSentinel);
  if (!Sentinel) {
    M.getOrInsertNamedMetadata(ZeroRelocABI::StackifiedSentinel);
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

  if (!hadHardError(M)) {
    if (stackifyMutableGlobals(M, Entry))
      Changed = true;
    if (!hadHardError(M)) {
      placeEntryFirst(M, Entry);
      (void)validate(M);
    }
  }

  if (auto *N = M.getNamedMetadata(ZeroRelocABI::StackifiedSentinel))
    N->eraseFromParent();
  if (auto *N = M.getNamedMetadata(ZeroRelocABI::HardErrorSentinel))
    N->eraseFromParent();

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace shellcode
} // namespace neverc
