# Passes And Conversion

## Writing MLIR Passes

### Transform Pass
```cpp
#include "mlir/Pass/Pass.h"
#include "mlir/IR/PatternMatch.h"

struct MyOptimizationPass
    : public mlir::PassWrapper<MyOptimizationPass,
                                mlir::OperationPass<mlir::func::FuncOp>> {

    void runOnOperation() override {
        mlir::func::FuncOp func = getOperation();

        // Walk all operations
        func.walk([](mlir::Operation *op) {
            // Transform operations
            if (auto addOp = llvm::dyn_cast<MyAddOp>(op)) {
                optimizeAdd(addOp);
            }
        });
    }

    llvm::StringRef getArgument() const final {
        return "my-optimization";
    }

    llvm::StringRef getDescription() const final {
        return "My custom optimization pass";
    }
};
```

### Pattern-Based Rewriting
```cpp
// Define rewrite pattern
struct SimplifyRedundantAdd : public mlir::OpRewritePattern<MyAddOp> {
    using OpRewritePattern<MyAddOp>::OpRewritePattern;

    mlir::LogicalResult matchAndRewrite(
        MyAddOp op,
        mlir::PatternRewriter &rewriter) const override {

        // Match: add(x, 0) -> x
        if (auto constOp = op.getRhs().getDefiningOp<ConstantOp>()) {
            if (isZero(constOp)) {
                rewriter.replaceOp(op, op.getLhs());
                return mlir::success();
            }
        }
        return mlir::failure();
    }
};

// Apply patterns
void runOnOperation() override {
    mlir::RewritePatternSet patterns(&getContext());
    patterns.add<SimplifyRedundantAdd>(&getContext());

    if (mlir::failed(mlir::applyPatternsAndFoldGreedily(
            getOperation(), std::move(patterns)))) {
        signalPassFailure();
    }
}
```

## Dialect Conversion

### Lowering Between Dialects
```cpp
// Convert high-level ops to lower-level ops
struct MyAddOpLowering : public mlir::OpConversionPattern<MyAddOp> {
    using OpConversionPattern<MyAddOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(
        MyAddOp op,
        OpAdaptor adaptor,
        mlir::ConversionPatternRewriter &rewriter) const override {

        // Lower to arith dialect
        rewriter.replaceOpWithNewOp<mlir::arith::AddFOp>(
            op, adaptor.getLhs(), adaptor.getRhs());
        return mlir::success();
    }
};

// Conversion pass
struct LowerToArithPass : public mlir::PassWrapper<
    LowerToArithPass,
    mlir::OperationPass<mlir::ModuleOp>> {

    void runOnOperation() override {
        mlir::ConversionTarget target(getContext());
        target.addLegalDialect<mlir::arith::ArithDialect>();
        target.addIllegalDialect<MyDialect>();

        mlir::RewritePatternSet patterns(&getContext());
        patterns.add<MyAddOpLowering>(&getContext());

        if (mlir::failed(mlir::applyPartialConversion(
                getOperation(), target, std::move(patterns)))) {
            signalPassFailure();
        }
    }
};
```
