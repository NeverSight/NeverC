# Taint And Dependencies

## Taint Analysis

### Source-Sink Model
```cpp
// Define taint sources (user input, network, files)
bool isTaintSource(llvm::CallInst* CI) {
    llvm::Function* F = CI->getCalledFunction();
    if (!F) return false;

    static const std::set<std::string> sources = {
        "read", "recv", "fread", "getenv", "gets", "scanf"
    };
    return sources.count(F->getName().str()) > 0;
}

// Define sensitive sinks (SQL queries, system calls, format strings)
bool isSensitiveSink(llvm::CallInst* CI) {
    llvm::Function* F = CI->getCalledFunction();
    if (!F) return false;

    static const std::set<std::string> sinks = {
        "system", "exec", "printf", "strcpy", "memcpy", "sql_query"
    };
    return sinks.count(F->getName().str()) > 0;
}
```

### Interprocedural Analysis
- Track taint across function boundaries
- Handle indirect calls via call graph analysis
- Context-sensitivity for precision

## Dependency Analysis

### Data Dependency Graph (DDG)
```cpp
#include "llvm/Analysis/DDG.h"

void buildDDG(llvm::Function& F, llvm::FunctionAnalysisManager& FAM) {
    auto& LI = FAM.getResult<llvm::LoopAnalysis>(F);

    for (auto* L : LI) {
        llvm::DataDependenceGraph DDG(*L, FAM.getResult<llvm::AAManager>(F));

        for (auto& Node : DDG) {
            // Analyze data dependencies in loop
        }
    }
}
```

### Program Slicing
- Forward slicing: Find all statements affected by a variable
- Backward slicing: Find all statements affecting a variable
- Use for debugging, testing, and security analysis
