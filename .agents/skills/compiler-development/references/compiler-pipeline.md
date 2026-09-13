# Compiler Pipeline

## Compiler Architecture Overview

### Classic Three-Phase Design

```
Source Code → Frontend → Middle-End (Optimizer) → Backend → Machine Code
                ↓              ↓                      ↓
             AST/IR      LLVM IR Passes          Target Code
```

### NeverC-Specific Extensions

```
Source (.c/.nc) → Frontend → DynCode IR Passes → MIR Passes → Backend → Extractor → .bin
                                    ↓                   ↓                      ↓
                             ZeroReloc, Import    MIRPrepPass         MachO/ELF/COFF extract
                             Syscall, String      Rewrite patterns
```

## Frontend Development

### Lexical Analysis

```cpp
enum class TokenKind {
    Identifier, Number, String, Keyword,
    Operator, Punctuation, EndOfFile
};

struct Token {
    TokenKind kind;
    std::string value;
    SourceLocation location;
};
```

### Parser Implementation

- Recursive Descent: Easy to implement, good error messages
- Operator Precedence Parsing: Efficient for expression parsing
- LALR/LR: Use tools like Bison for complex grammars

### AST Design

```cpp
class Expr {
public:
    virtual ~Expr() = default;
    virtual llvm::Value* codegen() = 0;
};

class BinaryExpr : public Expr {
    std::unique_ptr<Expr> LHS, RHS;
    char Op;
public:
    llvm::Value* codegen() override {
        llvm::Value* L = LHS->codegen();
        llvm::Value* R = RHS->codegen();
        switch (Op) {
            case '+': return Builder.CreateFAdd(L, R, "addtmp");
            case '-': return Builder.CreateFSub(L, R, "subtmp");
            case '*': return Builder.CreateFMul(L, R, "multmp");
            case '/': return Builder.CreateFDiv(L, R, "divtmp");
        }
    }
};
```

## LLVM IR Generation

### Module and Context Setup

```cpp
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"

class CodeGen {
    std::unique_ptr<llvm::LLVMContext> Context;
    std::unique_ptr<llvm::Module> Module;
    std::unique_ptr<llvm::IRBuilder<>> Builder;

public:
    CodeGen() {
        Context = std::make_unique<llvm::LLVMContext>();
        Module = std::make_unique<llvm::Module>("my_module", *Context);
        Builder = std::make_unique<llvm::IRBuilder<>>(*Context);
    }
};
```

### Target-Aware Codegen Dispatch

```cpp
std::unique_ptr<TargetCodeGenInfo> createTargetCodeGenInfo(ModuleEmitter &ME) {
    const llvm::Triple &Triple = ME.getTarget().getTriple();

    switch (Triple.getArch()) {
    case llvm::Triple::aarch64:
        switch (Triple.getOS()) {
        case llvm::Triple::Win32:
            return createWindowsAArch64TargetCodeGenInfo(ME, Kind);
        default:
            return createAArch64TargetCodeGenInfo(ME, Kind);
        }
    case llvm::Triple::x86_64:
        switch (Triple.getOS()) {
        case llvm::Triple::Win32:
            return createWinX86_64TargetCodeGenInfo(ME, AVXLevel);
        default:
            return createX86_64TargetCodeGenInfo(ME, AVXLevel);
        }
    }
}
```

## Optimization Pass Pipeline

### New Pass Manager

```cpp
#include "llvm/Passes/PassBuilder.h"

void optimizeModule(llvm::Module& M) {
    llvm::PassBuilder PB;
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(
        llvm::OptimizationLevel::O2);
    MPM.run(M, MAM);
}
```

## JIT Compilation

### LLVM ORC JIT

```cpp
#include "llvm/ExecutionEngine/Orc/LLJIT.h"

auto JIT = llvm::orc::LLJITBuilder().create();
if (!JIT) handleError(JIT.takeError());

(*JIT)->addIRModule(llvm::orc::ThreadSafeModule(
    std::move(Module), std::move(Context)));

auto Sym = (*JIT)->lookup("main");
auto* MainFn = (int(*)())Sym->getAddress();
int result = MainFn();
```

## Language Implementation Patterns

### Memory-Safe Languages

- Use LLVM's memory sanitizer hooks
- Implement bounds checking with GEP introspection
- Reference counting or garbage collection integration

### Type Systems

- Implement type inference during AST construction
- Generate appropriate LLVM types (i32, float, struct, ptr)
- Handle generic types via monomorphization or boxing

### Error Handling

- Generate exception handling via LLVM's landingpad/invoke
- Implement Result/Option types as tagged unions
- Use LLVM's personality functions for unwinding
