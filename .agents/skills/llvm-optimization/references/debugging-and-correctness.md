# Debugging And Correctness

## Debugging Optimizations

### Viewing Pass Execution
```bash
# Print passes being run
opt -debug-pass-manager input.ll -O2

# Print IR after each pass
opt -print-after-all input.ll -O2

# Print specific pass output
opt -print-after=instcombine input.ll -O2

# Statistics
opt -stats input.ll -O2
```

### Optimization Remarks
```bash
# Enable all optimization remarks
clang -Rpass=.* source.c

# Specific remarks
clang -Rpass=loop-vectorize source.c
clang -Rpass-missed=inline source.c
clang -Rpass-analysis=loop-vectorize source.c
```

## Correctness Verification

### Alive2
Automatic verification of LLVM optimizations:
```bash
# Verify transformation correctness
alive-tv before.ll after.ll

# Check specific optimization
opt -instcombine input.ll | alive-tv input.ll -
```
