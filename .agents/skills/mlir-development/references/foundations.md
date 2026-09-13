# Foundations

## MLIR Overview

### What is MLIR?
MLIR is a compiler infrastructure that enables building reusable and extensible compiler components. It provides:
- Hierarchical, multi-level IR representation
- Extensible operation and type system
- Progressive lowering between abstraction levels
- Rich transformation infrastructure

### Architecture
```
High-Level DSL
     ↓
Domain-Specific Dialects (e.g., TensorFlow, PyTorch)
     ↓
Mid-Level Dialects (e.g., Linalg, Affine)
     ↓
Low-Level Dialects (e.g., LLVM, GPU)
     ↓
Target Code
```

## Core Concepts

### Dialects
Dialects are groupings of operations, types, and attributes:

```cpp
// Define a custom dialect
class MyDialect : public mlir::Dialect {
public:
    explicit MyDialect(mlir::MLIRContext *context)
        : Dialect("my_dialect", context,
                  mlir::TypeID::get<MyDialect>()) {
        addOperations<
            MyAddOp,
            MyMulOp,
            MyFuncOp
        >();
        addTypes<MyTensorType>();
    }

    static llvm::StringRef getDialectNamespace() {
        return "my_dialect";
    }
};
```

### Operations
```cpp
// Define using ODS (Operation Definition Specification)
// In TableGen file (.td)
def MyAddOp : Op<MyDialect, "add", [Pure]> {
    let summary = "Add two tensors";
    let description = [{
        Performs element-wise addition of two tensors.
    }];

    let arguments = (ins
        AnyTensor:$lhs,
        AnyTensor:$rhs
    );

    let results = (outs
        AnyTensor:$result
    );

    let assemblyFormat = [{
        $lhs `,` $rhs attr-dict `:` type($result)
    }];
}
```

### Types and Attributes
```cpp
// Custom type definition
class MyTensorType : public mlir::Type::TypeBase<
    MyTensorType, mlir::Type, MyTensorTypeStorage> {
public:
    using Base::Base;

    static MyTensorType get(mlir::MLIRContext *context,
                            llvm::ArrayRef<int64_t> shape,
                            mlir::Type elementType) {
        return Base::get(context, shape, elementType);
    }

    llvm::ArrayRef<int64_t> getShape() const;
    mlir::Type getElementType() const;
};
```
