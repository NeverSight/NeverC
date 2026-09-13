# Transformation And Ecosystem

## Source-to-Source Transformation

### Clang Rewriter
```cpp
#include "clang/Rewrite/Core/Rewriter.h"

class MyRewriter : public RecursiveASTVisitor<MyRewriter> {
    Rewriter &R;

public:
    MyRewriter(Rewriter &R) : R(R) {}

    bool VisitFunctionDecl(FunctionDecl *FD) {
        // Add comment before function
        R.InsertTextBefore(FD->getBeginLoc(), "// Auto-generated\n");

        // Replace function name
        R.ReplaceText(FD->getLocation(),
                      FD->getName().size(), "new_name");

        return true;
    }
};
```

### RefactoringTool
```cpp
#include "clang/Tooling/Refactoring.h"

class MyRefactoring : public RefactoringCallback {
public:
    void run(const MatchFinder::MatchResult &Result) override {
        // Generate replacements
        Replacement Rep(
            *Result.SourceManager,
            CharSourceRange::getTokenRange(Range),
            "new_text");

        Replacements.insert(Rep);
    }
};
```

## Notable Tools Built with LLVM Tooling

### Analysis Tools
- **cppinsights**: C++ template instantiation visualization
- **ClangBuildAnalyzer**: Build time analysis
- **clazy**: Qt-specific static analysis

### Refactoring Tools
- **clang-rename**: Symbol renaming
- **clang-tidy**: Linting and auto-fixes
- **include-what-you-use**: Include optimization

### Code Generation
- **classgen**: Extract type info for IDA
- **constexpr-everything**: Auto-apply constexpr
- **clang-expand**: Inline function expansion

## Best Practices

1. **Use AST Matchers**: More readable than manual traversal
2. **Preserve Formatting**: Use Rewriter carefully to maintain style
3. **Handle Macros**: Be aware of macro expansion locations
4. **Test Thoroughly**: Edge cases in C++ are numerous
5. **Provide Good Diagnostics**: Clear error messages improve usability
