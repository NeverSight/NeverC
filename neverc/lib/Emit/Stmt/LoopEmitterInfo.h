#ifndef NEVERC_LIB_CODEGEN_STMT_CGLOOPINFO_H
#define NEVERC_LIB_CODEGEN_STMT_CGLOOPINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class BasicBlock;
class Instruction;
class MDNode;
} // end namespace llvm

namespace neverc {
class Attr;
class TreeContext;
class CodeGenOptions;
namespace Emit {

struct LoopAttributes {
  explicit LoopAttributes(bool IsParallel = false);
  void clear();

  bool hasOnlyDefaultAttrs() const {
    return !IsParallel && VectorizeWidth == 0 &&
           VectorizeScalable == Unspecified && InterleaveCount == 0 &&
           UnrollCount == 0 && UnrollAndJamCount == 0 && !PipelineDisabled &&
           PipelineInitiationInterval == 0 &&
           VectorizePredicateEnable == Unspecified &&
           VectorizeEnable == Unspecified && UnrollEnable == Unspecified &&
           UnrollAndJamEnable == Unspecified &&
           DistributeEnable == Unspecified && CodeAlign == 0;
  }

  bool IsParallel;

  enum LVEnableState { Unspecified, Enable, Disable, Full };

  LVEnableState VectorizeEnable;

  LVEnableState UnrollEnable;

  LVEnableState UnrollAndJamEnable;

  LVEnableState VectorizePredicateEnable;

  unsigned VectorizeWidth;

  // Value for llvm.loop.vectorize.scalable.enable
  LVEnableState VectorizeScalable;

  unsigned InterleaveCount;

  unsigned UnrollCount;

  unsigned UnrollAndJamCount;

  LVEnableState DistributeEnable;

  bool PipelineDisabled;

  unsigned PipelineInitiationInterval;

  unsigned CodeAlign;

  bool MustProgress;
};

class LoopInfo {
public:
  LoopInfo(llvm::BasicBlock *Header, const LoopAttributes &Attrs,
           const llvm::DebugLoc &StartLoc, const llvm::DebugLoc &EndLoc,
           LoopInfo *Parent);

  llvm::MDNode *getLoopID() const { return TempLoopID.get(); }

  llvm::BasicBlock *getHeader() const { return Header; }

  const LoopAttributes &getAttributes() const { return Attrs; }

  llvm::MDNode *getAccessGroup() const { return AccGroup; }

  void finish();

private:
  llvm::TempMDTuple TempLoopID;
  llvm::BasicBlock *Header;
  LoopAttributes Attrs;
  llvm::MDNode *AccGroup = nullptr;
  llvm::DebugLoc StartLoc;
  llvm::DebugLoc EndLoc;
  LoopInfo *Parent;
  llvm::MDNode *UnrollAndJamInnerFollowup = nullptr;

  llvm::MDNode *
  createLoopPropertiesMetadata(llvm::ArrayRef<llvm::Metadata *> LoopProperties);

  llvm::MDNode *
  createPipeliningMetadata(const LoopAttributes &Attrs,
                           llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                           bool &HasUserTransforms);
  llvm::MDNode *
  createPartialUnrollMetadata(const LoopAttributes &Attrs,
                              llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                              bool &HasUserTransforms);
  llvm::MDNode *
  createUnrollAndJamMetadata(const LoopAttributes &Attrs,
                             llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                             bool &HasUserTransforms);
  llvm::MDNode *
  createLoopVectorizeMetadata(const LoopAttributes &Attrs,
                              llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                              bool &HasUserTransforms);
  llvm::MDNode *
  createLoopDistributeMetadata(const LoopAttributes &Attrs,
                               llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                               bool &HasUserTransforms);
  llvm::MDNode *
  createFullUnrollMetadata(const LoopAttributes &Attrs,
                           llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                           bool &HasUserTransforms);

  llvm::MDNode *createMetadata(const LoopAttributes &Attrs,
                               llvm::ArrayRef<llvm::Metadata *> LoopProperties,
                               bool &HasUserTransforms);
};

class LoopInfoStack {
  LoopInfoStack(const LoopInfoStack &) = delete;
  void operator=(const LoopInfoStack &) = delete;

public:
  LoopInfoStack() {}

  void push(llvm::BasicBlock *Header, const llvm::DebugLoc &StartLoc,
            const llvm::DebugLoc &EndLoc);

  void push(llvm::BasicBlock *Header, neverc::TreeContext &Ctx,
            const neverc::CodeGenOptions &CGOpts,
            llvm::ArrayRef<const Attr *> Attrs, const llvm::DebugLoc &StartLoc,
            const llvm::DebugLoc &EndLoc, bool MustProgress = false);

  void pop();

  llvm::MDNode *getCurLoopID() const { return getInfo().getLoopID(); }

  bool getCurLoopParallel() const {
    return hasInfo() ? getInfo().getAttributes().IsParallel : false;
  }

  void InsertHelper(llvm::Instruction *I) const;

  void setParallel(bool Enable = true) { StagedAttrs.IsParallel = Enable; }

  void setVectorizeEnable(bool Enable = true) {
    StagedAttrs.VectorizeEnable =
        Enable ? LoopAttributes::Enable : LoopAttributes::Disable;
  }

  void setDistributeState(bool Enable = true) {
    StagedAttrs.DistributeEnable =
        Enable ? LoopAttributes::Enable : LoopAttributes::Disable;
  }

  void setUnrollState(const LoopAttributes::LVEnableState &State) {
    StagedAttrs.UnrollEnable = State;
  }

  void setVectorizePredicateState(const LoopAttributes::LVEnableState &State) {
    StagedAttrs.VectorizePredicateEnable = State;
  }

  void setUnrollAndJamState(const LoopAttributes::LVEnableState &State) {
    StagedAttrs.UnrollAndJamEnable = State;
  }

  void setVectorizeWidth(unsigned W) { StagedAttrs.VectorizeWidth = W; }

  void setVectorizeScalable(const LoopAttributes::LVEnableState &State) {
    StagedAttrs.VectorizeScalable = State;
  }

  void setInterleaveCount(unsigned C) { StagedAttrs.InterleaveCount = C; }

  void setUnrollCount(unsigned C) { StagedAttrs.UnrollCount = C; }

  void setUnrollAndJamCount(unsigned C) { StagedAttrs.UnrollAndJamCount = C; }

  void setPipelineDisabled(bool S) { StagedAttrs.PipelineDisabled = S; }

  void setPipelineInitiationInterval(unsigned C) {
    StagedAttrs.PipelineInitiationInterval = C;
  }

  void setCodeAlign(unsigned C) { StagedAttrs.CodeAlign = C; }

  void setMustProgress(bool P) { StagedAttrs.MustProgress = P; }

private:
  bool hasInfo() const { return !Active.empty(); }
  const LoopInfo &getInfo() const { return *Active.back(); }
  LoopAttributes StagedAttrs;
  llvm::SmallVector<std::unique_ptr<LoopInfo>, 4> Active;
};

} // end namespace Emit
} // end namespace neverc

#endif
