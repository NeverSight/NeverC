# Testing Tools And Resources

## Testing MLIR

### Verify Tests
```mlir
// RUN: mlir-opt %s -my-pass -verify-diagnostics

// expected-remark @below {{optimized}}
// CHECK-LABEL: func @test_optimization
// CHECK-NOT: my_dialect.add
func @test_optimization(%a: i32, %b: i32) -> i32 {
    %result = my_dialect.add %a, %b : i32
    return %result : i32
}
```

### Unit Testing
```cpp
TEST(MyDialect, AddOpConstantFolding) {
    mlir::MLIRContext context;
    context.loadDialect<MyDialect>();

    mlir::OpBuilder builder(&context);
    auto loc = builder.getUnknownLoc();

    // Create and test operations
    auto constA = builder.create<ConstantOp>(loc, 5);
    auto constB = builder.create<ConstantOp>(loc, 3);
    auto add = builder.create<MyAddOp>(loc, constA, constB);

    // Verify folding
    EXPECT_TRUE(add.fold().succeeded());
}
```

## Development Tools

### mlir-opt
```bash
# Run passes
mlir-opt input.mlir -my-pass -o output.mlir

# Convert between dialects
mlir-opt input.mlir -convert-my-to-llvm

# Debug printing
mlir-opt input.mlir -debug-only=my-pass
```

### mlir-translate
```bash
# MLIR to LLVM IR
mlir-translate input.mlir --mlir-to-llvmir -o output.ll

# LLVM IR to MLIR
mlir-translate input.ll --import-llvm -o output.mlir
```

## Best Practices

1. **Progressive Lowering**: Lower in multiple stages, not directly to LLVM
2. **Preserve Semantics**: Each lowering should be semantics-preserving
3. **Use ODS**: Define operations in TableGen for consistency
4. **Test Thoroughly**: Use `-verify-diagnostics` for transformation tests
5. **Document Dialects**: Clear operation semantics documentation

## Resources

See MLIR and CIR sections in README.md for tutorials and example projects.

## Getting Detailed Information

When you need detailed and up-to-date resource links, tool lists, or project references, fetch the latest data from:

```
https://raw.githubusercontent.com/gmh5225/awesome-llvm-security/refs/heads/main/README.md
```

This README contains comprehensive curated lists of:
- MLIR tutorials and sample dialects (MLIR section)
- CIR (Clang IR) projects and documentation (CIR section)
- ML/AI compiler frameworks (torch-mlir, IREE, XLA)
