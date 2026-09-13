# Development Tools And Apis

## Development Tools

### Essential Commands
```bash
# Compile to LLVM IR
clang -S -emit-llvm source.c -o source.ll

# Optimize IR
opt -O2 source.ll -o optimized.ll

# Disassemble bitcode
llvm-dis source.bc -o source.ll

# Assemble to bitcode
llvm-as source.ll -o source.bc

# Generate native code
llc source.ll -o source.s

# View CFG
opt -passes=view-cfg source.ll
```

### Debugging LLVM IR
```bash
# With debug info
clang -g -S -emit-llvm source.c

# Debug IR transformation
opt -debug-pass=Structure -O2 source.ll

# Verify IR validity
opt -passes=verify source.ll
```

## Common Analysis APIs

```cpp
// Dominator Tree
#include "llvm/Analysis/Dominators.h"
DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
if (DT.dominates(BB1, BB2)) { /* BB1 dominates BB2 */ }

// Loop Analysis
#include "llvm/Analysis/LoopInfo.h"
LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
for (Loop *L : LI) { /* process loops */ }

// Alias Analysis
#include "llvm/Analysis/AliasAnalysis.h"
AAResults &AA = FAM.getResult<AAManager>(F);
AliasResult R = AA.alias(Ptr1, Ptr2);

// Call Graph
#include "llvm/Analysis/CallGraph.h"
CallGraph &CG = MAM.getResult<CallGraphAnalysis>(M);
```
