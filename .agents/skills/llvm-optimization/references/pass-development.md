# Pass Development

## Optimization Pipeline Overview

### Pipeline Stages
```
Source → Frontend → LLVM IR → Optimization Passes → CodeGen → Machine Code
                        ↓
                 [Transform Passes]
                 [Analysis Passes]
```

### Optimization Levels
```bash
# No optimization
clang -O0 source.c

# Basic optimization (most optimizations enabled)
clang -O1 source.c

# Full optimization (aggressive inlining, vectorization)
clang -O2 source.c

# Maximum optimization (may increase code size)
clang -O3 source.c

# Size optimization
clang -Os source.c  # Optimize for size
clang -Oz source.c  # Aggressive size optimization
```

## Core Optimization Passes

### Scalar Optimizations
- **Constant Propagation**: Replace variables with known constant values
- **Dead Code Elimination (DCE)**: Remove unreachable or unused code
- **Common Subexpression Elimination (CSE)**: Avoid redundant computations
- **Instruction Combining**: Merge multiple instructions into simpler forms
- **Scalar Replacement of Aggregates (SROA)**: Break up aggregate allocations

### Loop Optimizations
- **Loop Invariant Code Motion (LICM)**: Hoist invariant computations
- **Loop Unrolling**: Duplicate loop body to reduce overhead
- **Loop Vectorization**: Convert scalar loops to vector operations
- **Loop Fusion/Fission**: Combine or split loops
- **Induction Variable Simplification**: Optimize loop counters

### Interprocedural Optimizations
- **Inlining**: Replace call sites with function body
- **Dead Argument Elimination**: Remove unused function parameters
- **Interprocedural Constant Propagation**: Propagate constants across functions
- **Link-Time Optimization (LTO)**: Whole-program optimization

## Writing Custom Optimization Passes

### New Pass Manager (LLVM 13+)
```cpp
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

struct MyOptimizationPass : public llvm::PassInfoMixin<MyOptimizationPass> {
    llvm::PreservedAnalyses run(llvm::Function &F,
                                 llvm::FunctionAnalysisManager &FAM) {
        bool Changed = false;

        for (auto &BB : F) {
            for (auto &I : BB) {
                // Implement optimization logic
                if (optimizeInstruction(I)) {
                    Changed = true;
                }
            }
        }

        if (Changed)
            return llvm::PreservedAnalyses::none();
        return llvm::PreservedAnalyses::all();
    }

private:
    bool optimizeInstruction(llvm::Instruction &I) {
        // Example: Replace add x, 0 with x
        if (auto *BinOp = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
            if (BinOp->getOpcode() == llvm::Instruction::Add) {
                if (auto *C = llvm::dyn_cast<llvm::ConstantInt>(BinOp->getOperand(1))) {
                    if (C->isZero()) {
                        I.replaceAllUsesWith(BinOp->getOperand(0));
                        return true;
                    }
                }
            }
        }
        return false;
    }
};

// Plugin registration
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "MyOptPass", LLVM_VERSION_STRING,
            [](llvm::PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](llvm::StringRef Name, llvm::FunctionPassManager &FPM,
                       llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
                        if (Name == "my-opt") {
                            FPM.addPass(MyOptimizationPass());
                            return true;
                        }
                        return false;
                    });
            }};
}
```

### Analysis Dependencies
```cpp
struct MyAnalysis : public llvm::AnalysisInfoMixin<MyAnalysis> {
    using Result = MyAnalysisResult;

    Result run(llvm::Function &F, llvm::FunctionAnalysisManager &FAM) {
        // Compute analysis result
        return Result();
    }

    static llvm::AnalysisKey Key;
};

// Using analysis in a pass
llvm::PreservedAnalyses run(llvm::Function &F,
                             llvm::FunctionAnalysisManager &FAM) {
    auto &DT = FAM.getResult<llvm::DominatorTreeAnalysis>(F);
    auto &LI = FAM.getResult<llvm::LoopAnalysis>(F);
    auto &AA = FAM.getResult<llvm::AAManager>(F);

    // Use analysis results...
}
```

## Instruction Patterns

### Strength Reduction
```cpp
// Replace expensive operations with cheaper ones
// x * 2  →  x << 1
// x / 4  →  x >> 2
// x % 8  →  x & 7

bool reduceStrength(llvm::BinaryOperator *BO) {
    if (BO->getOpcode() == llvm::Instruction::Mul) {
        if (auto *C = llvm::dyn_cast<llvm::ConstantInt>(BO->getOperand(1))) {
            if (C->getValue().isPowerOf2()) {
                unsigned Shift = C->getValue().exactLogBase2();
                auto *Shl = llvm::BinaryOperator::CreateShl(
                    BO->getOperand(0),
                    llvm::ConstantInt::get(C->getType(), Shift));
                BO->replaceAllUsesWith(Shl);
                return true;
            }
        }
    }
    return false;
}
```

### Algebraic Simplification
```cpp
// x + 0 → x
// x * 1 → x
// x * 0 → 0
// x - x → 0
// x | x → x
// x & 0 → 0
```

## Dominator Tree Usage

### Finding Optimization Opportunities
```cpp
void optimizeWithDominators(llvm::Function &F,
                             llvm::DominatorTree &DT) {
    // Use dominance for safe code motion
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *Load = llvm::dyn_cast<llvm::LoadInst>(&I)) {
                // Check if we can hoist this load
                if (canHoist(Load, DT)) {
                    hoistInstruction(Load, DT);
                }
            }
        }
    }
}

bool canHoist(llvm::Instruction *I, llvm::DominatorTree &DT) {
    llvm::BasicBlock *DefBB = I->getParent();

    // Check all uses are dominated
    for (auto *U : I->users()) {
        if (auto *UI = llvm::dyn_cast<llvm::Instruction>(U)) {
            if (!DT.dominates(DefBB, UI->getParent())) {
                return false;
            }
        }
    }
    return true;
}
```
