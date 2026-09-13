# Dataflow And Pointers

## Analysis Categories

### Dataflow Analysis
- **Forward Analysis**: Track values from definitions to uses
- **Backward Analysis**: Track from uses back to definitions
- **May/Must Analysis**: Conservative vs precise approximations

### Control Flow Analysis
- **Dominator Trees**: Identify code dominance relationships
- **Post-Dominator Trees**: Control dependence analysis
- **Loop Analysis**: Detect and characterize loops

### Pointer Analysis
- **Flow-Insensitive**: Andersen's, Steensgaard's algorithms
- **Flow-Sensitive**: Track pointer values at each program point
- **Context-Sensitive**: Distinguish calling contexts

## LLVM Analysis Infrastructure

### Using Built-in Analyses
```cpp
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DominatorTree.h"

void analyze(llvm::Function& F, llvm::FunctionAnalysisManager& FAM) {
    // Get dominator tree
    auto& DT = FAM.getResult<llvm::DominatorTreeAnalysis>(F);

    // Get loop info
    auto& LI = FAM.getResult<llvm::LoopAnalysis>(F);

    // Get alias analysis
    auto& AA = FAM.getResult<llvm::AAManager>(F);

    // Check if two pointers may alias
    llvm::AliasResult AR = AA.alias(Ptr1, Ptr2);
}
```

### Implementing Custom Analysis
```cpp
class TaintAnalysis {
    std::set<llvm::Value*> taintedValues;

public:
    void markTainted(llvm::Value* V) {
        taintedValues.insert(V);
    }

    bool isTainted(llvm::Value* V) {
        return taintedValues.count(V) > 0;
    }

    void propagate(llvm::Instruction* I) {
        // Propagate taint through operations
        for (auto& Op : I->operands()) {
            if (isTainted(Op)) {
                markTainted(I);
                break;
            }
        }
    }
};
```

## Pointer Analysis Frameworks

### Using Andersen's Analysis
```cpp
#include "llvm/Analysis/CFLAndersAliasAnalysis.h"

// Check may-alias relationship
void checkAlias(llvm::AAResults& AA, llvm::Value* P1, llvm::Value* P2) {
    switch (AA.alias(P1, P2)) {
        case llvm::AliasResult::NoAlias:
            // Definitely don't alias
            break;
        case llvm::AliasResult::MayAlias:
            // Might alias
            break;
        case llvm::AliasResult::MustAlias:
            // Always alias
            break;
    }
}
```

### Points-To Sets
```cpp
// Track what each pointer may point to
class PointsToAnalysis {
    std::map<llvm::Value*, std::set<llvm::Value*>> pointsTo;

public:
    void addPointsTo(llvm::Value* ptr, llvm::Value* target) {
        pointsTo[ptr].insert(target);
    }

    const std::set<llvm::Value*>& getPointsTo(llvm::Value* ptr) {
        return pointsTo[ptr];
    }
};
```
