# Llvm Fundamentals

## LLVM IR Essentials

### Basic Structure
```llvm
; Module level
@global_var = global i32 42

; Function definition
define i32 @add(i32 %a, i32 %b) {
entry:
    %sum = add i32 %a, %b
    ret i32 %sum
}

; External declaration
declare i32 @printf(i8*, ...)
```

### Common Instructions
```llvm
; Arithmetic
%result = add i32 %a, %b
%result = sub i32 %a, %b
%result = mul i32 %a, %b

; Memory
%ptr = alloca i32
store i32 %value, i32* %ptr
%loaded = load i32, i32* %ptr

; Control flow
br i1 %cond, label %then, label %else
br label %next

; Function calls
%retval = call i32 @function(i32 %arg)
```

### Type System
```llvm
; Integer types: i1, i8, i16, i32, i64, i128
; Floating point: half, float, double
; Pointers: i32*, [10 x i32]*, { i32, i64 }*
; Arrays: [10 x i32]
; Structs: { i32, i64, i8* }
; Vectors: <4 x i32>
```

## Writing LLVM Passes

### New Pass Manager (Recommended)
```cpp
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"

struct MyPass : public llvm::PassInfoMixin<MyPass> {
    llvm::PreservedAnalyses run(llvm::Function &F,
                                 llvm::FunctionAnalysisManager &FAM) {
        for (auto &BB : F) {
            for (auto &I : BB) {
                llvm::errs() << I << "\n";
            }
        }
        return llvm::PreservedAnalyses::all();
    }
};

// Plugin registration
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "MyPass", LLVM_VERSION_STRING,
            [](llvm::PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](llvm::StringRef Name, llvm::FunctionPassManager &FPM,
                       llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
                        if (Name == "my-pass") {
                            FPM.addPass(MyPass());
                            return true;
                        }
                        return false;
                    });
            }};
}
```

### Running Custom Passes
```bash
# Build as shared library
clang++ -shared -fPIC -o MyPass.so MyPass.cpp `llvm-config --cxxflags --ldflags`

# Run with opt
opt -load-pass-plugin=./MyPass.so -passes="my-pass" input.ll -o output.ll
```

## Clang Development

### AST Exploration
```bash
# Dump AST
clang -Xclang -ast-dump -fsyntax-only source.cpp

# AST as JSON
clang -Xclang -ast-dump=json -fsyntax-only source.cpp
```

### LibTooling Basics
```cpp
#include "clang/Tooling/Tooling.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

// Match all function declarations
auto matcher = functionDecl().bind("func");

class Callback : public MatchFinder::MatchCallback {
    void run(const MatchFinder::MatchResult &Result) override {
        if (auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("func")) {
            llvm::outs() << "Found: " << FD->getName() << "\n";
        }
    }
};
```

### AST Matchers Reference
```cpp
// Common matchers
functionDecl()               // Match function declarations
varDecl()                    // Match variable declarations
binaryOperator()             // Match binary operators
callExpr()                   // Match function calls
ifStmt()                     // Match if statements

// Narrowing matchers
hasName("foo")               // Match by name
hasType(asString("int"))     // Match by type
isPrivate()                  // Match private members

// Traversal matchers
hasDescendant(...)           // Match in subtree
hasAncestor(...)             // Match in parent tree
hasBody(...)                 // Match function body
```
