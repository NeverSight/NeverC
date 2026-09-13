# Loops Vectorization And Lto

## Loop Optimization Techniques

### Loop Analysis
```cpp
void analyzeLoops(llvm::Function &F, llvm::LoopInfo &LI) {
    for (auto *L : LI) {
        // Get loop trip count
        if (auto *TC = L->getTripCount()) {
            llvm::errs() << "Trip count: " << *TC << "\n";
        }

        // Check if loop is simple
        if (L->isLoopSimplifyForm()) {
            llvm::BasicBlock *Header = L->getHeader();
            llvm::BasicBlock *Latch = L->getLoopLatch();
            llvm::BasicBlock *Exit = L->getExitBlock();
        }

        // Get induction variables
        llvm::PHINode *IV = L->getCanonicalInductionVariable();
    }
}
```

### Loop Unrolling
```cpp
// Manually trigger loop unrolling
#pragma unroll 4
for (int i = 0; i < N; i++) {
    // Loop body will be unrolled 4x
}

// LLVM unroll metadata
!llvm.loop.unroll.count = !{i32 4}
```

## Vectorization

### Auto-Vectorization Hints
```cpp
// Enable vectorization
#pragma clang loop vectorize(enable)
for (int i = 0; i < N; i++) {
    a[i] = b[i] + c[i];
}

// Specify vector width
#pragma clang loop vectorize_width(8)
for (int i = 0; i < N; i++) {
    a[i] = b[i] * c[i];
}
```

### SLP Vectorization
Superword Level Parallelism - vectorize straight-line code:
```cpp
// Before SLP
a[0] = b[0] + c[0];
a[1] = b[1] + c[1];
a[2] = b[2] + c[2];
a[3] = b[3] + c[3];

// After SLP (conceptual)
<4 x float> tmp = load <4 x float> b
<4 x float> tmp2 = load <4 x float> c
<4 x float> result = fadd tmp, tmp2
store result to a
```

## Link-Time Optimization (LTO)

### Enabling LTO
```bash
# Full LTO
clang -flto source1.c source2.c -o program

# Thin LTO (faster, parallel)
clang -flto=thin source1.c source2.c -o program
```

### LTO Benefits
- Whole-program dead code elimination
- Interprocedural constant propagation
- Cross-module inlining
- Better devirtualization
