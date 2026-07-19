#ifndef NEVERC_PLUGIN_HOST_MIRPLUGINBRIDGE_H
#define NEVERC_PLUGIN_HOST_MIRPLUGINBRIDGE_H

#include "neverc/Plugin/PluginMIR.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MachineMemOperand;
class MachineOperand;
} // namespace llvm

namespace neverc::plugin {

class PluginProcessServices;
class PluginTaskContext;

class MIRPluginBridge {
public:
  MIRPluginBridge(PluginTaskContext &Task, llvm::MachineFunction &Function,
                  uint64_t FunctionGeneration = 1,
                  bool TargetSchemaEnabled = false,
                  llvm::StringRef PluginID = {});
  ~MIRPluginBridge();

  MIRPluginBridge(const MIRPluginBridge &) = delete;
  MIRPluginBridge &operator=(const MIRPluginBridge &) = delete;

  llvm::Expected<NevercMachineFunctionHandle> machineFunction();
  llvm::Expected<NevercMachineBasicBlockHandle>
  wrapBasicBlock(llvm::MachineBasicBlock &Block);
  llvm::Expected<NevercMachineInstrHandle>
  wrapInstruction(llvm::MachineInstr &Instruction);
  llvm::Expected<NevercMachineOperandHandle>
  wrapOperand(llvm::MachineOperand &Operand);
  llvm::Expected<NevercMachineMemOperandHandle>
  wrapMemoryOperand(llvm::MachineMemOperand &Operand);

  NevercStatus
  resolveMachineFunction(NevercMachineFunctionHandle Handle,
                         llvm::MachineFunction **OutFunction) const;
  NevercStatus resolveBasicBlock(NevercMachineBasicBlockHandle Handle,
                                 llvm::MachineBasicBlock **OutBlock) const;
  NevercStatus resolveInstruction(NevercMachineInstrHandle Handle,
                                  llvm::MachineInstr **OutInstruction) const;
  NevercStatus resolveOperand(NevercMachineOperandHandle Handle,
                              llvm::MachineOperand **OutOperand) const;
  NevercStatus
  resolveMemoryOperand(NevercMachineMemOperandHandle Handle,
                       llvm::MachineMemOperand **OutOperand) const;

  NevercStatus invalidateOperand(llvm::MachineInstr &Instruction,
                                 unsigned Index);
  NevercStatus invalidateMemoryOperand(llvm::MachineMemOperand &Operand);
  NevercStatus invalidateInstruction(llvm::MachineInstr &Instruction);
  NevercStatus invalidateBasicBlock(llvm::MachineBasicBlock &Block);

  const NevercMIRAPI &api() const { return API; }
  NevercTaskHandle taskHandle() const;
  uint64_t functionGeneration() const { return FunctionGeneration; }
  uint64_t mutationGeneration() const { return MutationGeneration; }
  bool targetSchemaEnabled() const { return TargetSchemaEnabled; }
  llvm::StringRef pluginID() const { return PluginID; }

  llvm::Expected<NevercMIRMutationHandle> beginMutation();
  NevercStatus endMutation(NevercMIRMutationHandle Mutation);
  NevercStatus commitMutation(NevercMIRMutationHandle Mutation);
  NevercStatus abandonMutation(NevercMIRMutationHandle Mutation);
  NevercStatus checkMutation(NevercMIRMutationHandle Mutation) const;
  void addMutationUndo(std::function<void()> Action);
  void addMutationCommit(std::function<void()> Action);
  void invalidateMutationProperties();
  void noteMutation();

  llvm::Expected<NevercMIRReferenceHandle>
  wrapReference(const void *Reference, NevercMIROperandKind Kind);
  NevercStatus resolveReference(NevercMIRReferenceHandle Handle,
                                NevercMIROperandKind ExpectedKind,
                                const void **OutReference) const;
  llvm::ArrayRef<uint64_t>
  setScratchWords(llvm::ArrayRef<uint64_t> Words) const;
  const char *ownString(llvm::StringRef Value);
  llvm::ArrayRef<uint32_t> ownRegisterMask(llvm::ArrayRef<uint32_t> Words);
  llvm::ArrayRef<int> ownShuffleMask(llvm::ArrayRef<int> Values);

private:
  struct MIROperandReference {
    llvm::MachineInstr *Instruction = nullptr;
    unsigned Index = 0;
  };

  struct OperandHandleEntry {
    NevercHandle Handle{};
    std::unique_ptr<MIROperandReference> Reference;
  };

  struct MIRReferenceRecord {
    const void *Reference = nullptr;
    NevercMIROperandKind Kind = 0;
  };

  struct ReferenceHandleEntry {
    NevercHandle Handle{};
    std::unique_ptr<MIRReferenceRecord> Record;
  };

  template <typename ValueT>
  llvm::Expected<NevercHandle>
  wrap(ValueT &Value, uint16_t Kind,
       llvm::DenseMap<ValueT *, NevercHandle> &Handles);

  template <typename ValueT>
  NevercStatus resolve(NevercHandle Handle, uint16_t Kind,
                       const llvm::DenseMap<ValueT *, NevercHandle> &Handles,
                       ValueT **OutValue) const;
  NevercStatus finishMutation(NevercMIRMutationHandle Mutation, bool Verify);
  void rollbackMutation();
  void advanceGeneration();

  PluginTaskContext &Task;
  llvm::MachineFunction &Function;
  uint64_t FunctionGeneration;
  uint64_t MutationGeneration;
  bool TargetSchemaEnabled;
  std::string PluginID;
  NevercMIRAPI API{};
  NevercMachineFunctionHandle FunctionHandle{};
  NevercMIRMutationHandle MutationHandle{};
  llvm::DenseMap<llvm::MachineBasicBlock *, NevercHandle> BlockHandles;
  llvm::DenseMap<llvm::MachineInstr *, NevercHandle> InstructionHandles;
  llvm::DenseMap<std::pair<llvm::MachineInstr *, unsigned>, OperandHandleEntry>
      OperandHandles;
  llvm::DenseMap<llvm::MachineMemOperand *, NevercHandle> MemoryOperandHandles;
  llvm::DenseMap<std::pair<const void *, NevercMIROperandKind>,
                 ReferenceHandleEntry>
      ReferenceHandles;
  std::vector<std::function<void()>> MutationUndoActions;
  std::vector<std::function<void()>> MutationCommitActions;
  bool MutationChanged = false;
  mutable std::vector<uint64_t> ScratchWords;
  std::deque<std::string> OwnedStrings;
  std::deque<std::vector<uint32_t>> OwnedRegisterMasks;
  std::deque<std::vector<int>> OwnedShuffleMasks;
};

llvm::Error registerPluginMIRInterface(PluginProcessServices &Services);
void initializeMIRSchemaAPI(NevercMIRAPI &API, void *Context);
void initializeMIRCoreAPI(NevercMIRAPI &API);
void initializeMIRBuilderAPI(NevercMIRAPI &API);
void initializeMIRRegisterAPI(NevercMIRAPI &API);
void initializeMIRFrameAPI(NevercMIRAPI &API);
void initializeMIRConstantPoolAPI(NevercMIRAPI &API);
void initializeMIRMemoryAPI(NevercMIRAPI &API);
void initializeMIRPropertyAPI(NevercMIRAPI &API);

} // namespace neverc::plugin

#endif
