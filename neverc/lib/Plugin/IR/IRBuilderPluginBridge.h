#ifndef NEVERC_LIB_PLUGIN_IR_IRBUILDERPLUGINBRIDGE_H
#define NEVERC_LIB_PLUGIN_IR_IRBUILDERPLUGINBRIDGE_H

#include "neverc/Plugin/PluginIR.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <vector>

namespace llvm {
class BasicBlock;
class Function;
class Instruction;
class Value;
} // namespace llvm

namespace neverc::plugin {

class IRPluginBridge;

class IRBuilderPluginBridge {
public:
  static llvm::Expected<std::unique_ptr<IRBuilderPluginBridge>>
  create(IRPluginBridge &Bridge);
  ~IRBuilderPluginBridge();

  IRBuilderPluginBridge(const IRBuilderPluginBridge &) = delete;
  IRBuilderPluginBridge &operator=(const IRBuilderPluginBridge &) = delete;

  const NevercIRBuilderAPI &api() const { return API; }
  bool hasActiveMutation() const;

private:
  struct Mutation;
  struct Builder;

  explicit IRBuilderPluginBridge(IRPluginBridge &Bridge);
  llvm::Error initialize();
  bool validTask(NevercTaskHandle Task) const;
  NevercStatus resolveMutation(NevercIRMutationHandle Handle,
                               Mutation **OutMutation) const;
  NevercStatus resolveBuilder(NevercIRBuilderHandle Handle,
                              Builder **OutBuilder) const;
  NevercStatus validateBuilder(Builder &BuilderValue) const;
  NevercStatus publishInstruction(Builder &BuilderValue,
                                  llvm::Instruction &InstructionValue,
                                  NevercIRValueHandle *OutInstruction);
  bool blockInScope(const Mutation &MutationValue,
                    const llvm::BasicBlock &Block) const;
  void rollback(Mutation &MutationValue);
  void forgetMutationHandle(NevercIRMutationHandle Handle);
  void forgetBuilderHandle(NevercIRBuilderHandle Handle);

  static NevercStatus NEVERC_CALL beginMutation(
      void *Context, NevercTaskHandle Task, NevercIRMutationScope Scope,
      NevercIRValueHandle ScopeRoot, NevercIRMutationHandle *OutMutation);
  static NevercStatus NEVERC_CALL commitMutation(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation);
  static NevercStatus NEVERC_CALL abortMutation(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation);
  static NevercStatus NEVERC_CALL destroyMutation(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation);
  static NevercStatus NEVERC_CALL createBuilder(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation,
      NevercIRBuilderHandle *OutBuilder);
  static NevercStatus NEVERC_CALL destroyBuilder(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder);
  static NevercStatus NEVERC_CALL setInsertBlock(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Block);
  static NevercStatus NEVERC_CALL setInsertBefore(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Instruction);
  static NevercStatus NEVERC_CALL setDebugLocation(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRMetadataHandle Location);
  static NevercStatus NEVERC_CALL setFastMathFlags(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRFastMathFlags Flags);
  static NevercStatus NEVERC_CALL buildBinary(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIROpcode Opcode, NevercIRValueHandle Left,
      NevercIRValueHandle Right, NevercStringView Name,
      NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildUnary(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIROpcode Opcode, NevercIRValueHandle Operand,
      NevercStringView Name, NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildCompare(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRPredicate Predicate, NevercIRValueHandle Left,
      NevercIRValueHandle Right, NevercStringView Name,
      NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildCast(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIROpcode Opcode, NevercIRValueHandle Operand,
      NevercIRTypeHandle DestinationType, NevercStringView Name,
      NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildSelect(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Condition, NevercIRValueHandle TrueValue,
      NevercIRValueHandle FalseValue, NevercStringView Name,
      NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildAlloca(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRTypeHandle AllocatedType, uint32_t AddressSpace,
      NevercIRValueHandle ArraySize, NevercStringView Name,
      NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildLoad(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRTypeHandle LoadedType, NevercIRValueHandle Pointer,
      NevercStringView Name, NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildStore(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle StoredValue, NevercIRValueHandle Pointer,
      NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildGetElementPtr(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRTypeHandle SourceElementType, NevercIRValueHandle Pointer,
      const NevercIRValueHandle *Indices, uint64_t IndexCount,
      NevercStringView Name, NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildCall(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRTypeHandle FunctionType, NevercIRValueHandle Callee,
      const NevercIRValueHandle *Arguments, uint64_t ArgumentCount,
      NevercStringView Name, NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildPhi(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRTypeHandle Type, uint32_t ReservedIncomingCount,
      NevercStringView Name, NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL addPhiIncoming(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation,
      NevercIRValueHandle Phi, NevercIRValueHandle IncomingValue,
      NevercIRValueHandle IncomingBlock);
  static NevercStatus NEVERC_CALL buildBranch(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Destination, NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildConditionalBranch(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Condition, NevercIRValueHandle TrueDestination,
      NevercIRValueHandle FalseDestination,
      NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildUnreachable(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildReturn(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle Value, NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL buildReturnVoid(
      void *Context, NevercTaskHandle Task, NevercIRBuilderHandle Builder,
      NevercIRValueHandle *OutInstruction);
  static NevercStatus NEVERC_CALL createFunction(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation,
      NevercIRTypeHandle FunctionType, NevercStringView Name,
      NevercIRValueHandle *OutFunction);
  static NevercStatus NEVERC_CALL createBasicBlock(
      void *Context, NevercTaskHandle Task, NevercIRMutationHandle Mutation,
      NevercIRValueHandle Function, NevercStringView Name,
      NevercIRValueHandle *OutBlock);

  IRPluginBridge &Bridge;
  NevercIRBuilderAPI API{};
  std::vector<NevercIRMutationHandle> MutationHandles;
  std::vector<NevercIRBuilderHandle> BuilderHandles;
};

} // namespace neverc::plugin

#endif
