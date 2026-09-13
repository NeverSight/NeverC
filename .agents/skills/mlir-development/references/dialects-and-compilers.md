# Dialects And Compilers

## Built-in Dialects

### Affine Dialect
For polyhedral compilation and loop optimizations:
```mlir
affine.for %i = 0 to 100 {
    affine.for %j = 0 to 100 {
        %val = affine.load %A[%i, %j] : memref<100x100xf32>
        affine.store %val, %B[%j, %i] : memref<100x100xf32>
    }
}
```

### Linalg Dialect
For linear algebra operations:
```mlir
linalg.matmul ins(%A, %B : tensor<MxKxf32>, tensor<KxNxf32>)
              outs(%C : tensor<MxNxf32>) -> tensor<MxNxf32>
```

### SCF Dialect (Structured Control Flow)
```mlir
%result = scf.for %i = %lb to %ub step %step iter_args(%sum = %init) {
    %val = memref.load %A[%i] : memref<?xf32>
    %new_sum = arith.addf %sum, %val : f32
    scf.yield %new_sum : f32
}
```

## CIR (Clang IR)

### Overview
CIR is an MLIR-based representation for C/C++, providing:
- Higher-level representation than LLVM IR
- Better debugging and tooling
- Language-specific optimizations

```mlir
// CIR example
cir.func @add(%a: !s32i, %b: !s32i) -> !s32i {
    %result = cir.binop(add, %a, %b) : !s32i
    cir.return %result : !s32i
}
```

### CIR Projects
- **llvm/clangir**: Official ClangIR implementation
- **facebookincubator/clangir**: Facebook's CIR experiments

## ML/AI Compilation

### TensorFlow MLIR
```mlir
// TensorFlow dialect
%result = "tf.MatMul"(%A, %B) {
    transpose_a = false,
    transpose_b = false
} : (tensor<4x8xf32>, tensor<8x16xf32>) -> tensor<4x16xf32>
```

### PyTorch MLIR (torch-mlir)
```mlir
// Torch dialect
%result = torch.aten.mm %A, %B :
    !torch.vtensor<[4,8],f32>, !torch.vtensor<[8,16],f32>
    -> !torch.vtensor<[4,16],f32>
```

### IREE (Intermediate Representation Execution Environment)
End-to-end MLIR compiler for ML models:
- Portable deployment
- Efficient runtime execution
- Multi-target support (CPU, GPU, TPU)
