# Clang Tools

## Clang Plugin Development

### Plugin Architecture
Clang plugins are dynamically loaded libraries that extend Clang's functionality during compilation.

```cpp
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"

class MyVisitor : public clang::RecursiveASTVisitor<MyVisitor> {
public:
    bool VisitFunctionDecl(clang::FunctionDecl *FD) {
        llvm::outs() << "Found function: " << FD->getName() << "\n";
        return true;
    }
};

class MyConsumer : public clang::ASTConsumer {
    MyVisitor Visitor;
public:
    void HandleTranslationUnit(clang::ASTContext &Context) override {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }
};

class MyPlugin : public clang::PluginASTAction {
protected:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance &CI, llvm::StringRef) override {
        return std::make_unique<MyConsumer>();
    }

    bool ParseArgs(const clang::CompilerInstance &CI,
                   const std::vector<std::string> &args) override {
        return true;
    }
};

static clang::FrontendPluginRegistry::Add<MyPlugin>
    X("my-plugin", "My custom plugin description");
```

### Running Clang Plugins
```bash
# Build plugin
clang++ -shared -fPIC -o MyPlugin.so MyPlugin.cpp \
    $(llvm-config --cxxflags --ldflags)

# Run plugin
clang -Xclang -load -Xclang ./MyPlugin.so \
      -Xclang -plugin -Xclang my-plugin \
      source.cpp
```

## LibTooling

### Standalone Tools
```cpp
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::tooling;
using namespace clang::ast_matchers;

// Define matcher
auto functionMatcher = functionDecl(hasName("targetFunction")).bind("func");

// Callback handler
class FunctionCallback : public MatchFinder::MatchCallback {
public:
    void run(const MatchFinder::MatchResult &Result) override {
        if (auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("func")) {
            llvm::outs() << "Found: " << FD->getQualifiedNameAsString() << "\n";
        }
    }
};

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyCategory);
    ClangTool Tool(ExpectedParser->getCompilations(),
                   ExpectedParser->getSourcePathList());

    FunctionCallback Callback;
    MatchFinder Finder;
    Finder.addMatcher(functionMatcher, &Callback);

    return Tool.run(newFrontendActionFactory(&Finder).get());
}
```

### AST Matchers Reference
```cpp
// Declaration matchers
functionDecl()           // Match function declarations
varDecl()               // Match variable declarations
recordDecl()            // Match struct/class/union
fieldDecl()             // Match class fields
cxxMethodDecl()         // Match C++ methods
cxxConstructorDecl()    // Match constructors

// Expression matchers
callExpr()              // Match function calls
binaryOperator()        // Match binary operators
unaryOperator()         // Match unary operators
memberExpr()            // Match member access

// Narrowing matchers
hasName("name")         // Filter by name
hasType(asString("int")) // Filter by type
isPublic()              // Filter by access specifier
hasAnyParameter(...)    // Match parameters

// Traversal matchers
hasDescendant(...)      // Match anywhere in subtree
hasAncestor(...)        // Match in parent chain
has(...)                // Match direct children
forEach(...)            // Match all children
```
