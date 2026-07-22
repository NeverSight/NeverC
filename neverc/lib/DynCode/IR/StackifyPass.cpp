#include "neverc/DynCode/IR/StackifyPass.h"
#include "DynCodeIRStageSupport.h"
#include "neverc/DynCode/Pipeline/DynCodeIRHelperNames.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
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
namespace dyncode {

namespace {

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
      ir_stage::reportError(
          M, "mutable global '" + GV->getName() +
                 "' is referenced outside the entry function; "
                 "first remaining reference is in function '" +
                 OffendingFunction +
                 "'; "
                 "the dyncode optimizer could not inline every caller. "
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
      ir_stage::reportError(M, "internal: could not remove global '" +
                                   GV->getName() +
                                   "' after stackifying; remaining user is " +
                                   UserKind);
      return false;
    }
  }

  return Changed;
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

} // namespace

PreservedAnalyses StackifyPass::run(Module &M, ModuleAnalysisManager &) {
  if (ir_stage::hadHardError(M))
    return PreservedAnalyses::all();

  Function *Entry = ir_stage::findEntry(M, EntrySymbol);

  // Re-run prepare so functions/globals introduced by the intervening dyncode
  // transforms are normalised before stackifying (this mirrors the second
  // ZeroRelocPass run in the original monolithic pipeline).
  bool Changed = ir_stage::prep(M, Entry, InlineAll);

  if (!ir_stage::hadHardError(M)) {
    if (stackifyMutableGlobals(M, Entry))
      Changed = true;
    if (!ir_stage::hadHardError(M))
      placeEntryFirst(M, Entry);
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace dyncode
} // namespace neverc
