#ifndef NEVERC_PLUGIN_HOST_MCPLUGINBRIDGE_H
#define NEVERC_PLUGIN_HOST_MCPLUGINBRIDGE_H

#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/PluginMC.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <list>
#include <memory>
#include <utility>
#include <vector>

namespace neverc::plugin {

class PluginMCUnit {
public:
  using Storage = std::list<std::unique_ptr<llvm::MCInst>>;

  llvm::MCInst &append(std::unique_ptr<llvm::MCInst> Instruction);
  size_t size() const { return Instructions.size(); }
  llvm::MCInst *at(size_t Index);
  const llvm::MCInst *at(size_t Index) const;
  Storage &instructions() { return Instructions; }
  const Storage &instructions() const { return Instructions; }

private:
  Storage Instructions;
  friend class MCPluginBridge;
};

class MCPluginBridge {
public:
  struct OperandReference {
    llvm::MCInst *Instruction = nullptr;
    uint64_t Index = 0;
  };

  MCPluginBridge(
      PluginTaskContext &Task, PluginMCUnit &Unit,
      const PluginTargetSnapshot::NamedRecord *Schema = nullptr);
  ~MCPluginBridge();

  MCPluginBridge(const MCPluginBridge &) = delete;
  MCPluginBridge &operator=(const MCPluginBridge &) = delete;

  const NevercMCAPI &api() const { return API; }
  NevercTaskHandle taskHandle() const { return Task.handle(); }
  llvm::Expected<NevercMCUnitHandle> unit();
  llvm::Expected<NevercMCInstHandle>
  wrapInstruction(llvm::MCInst &Instruction);
  llvm::Expected<NevercMCOperandHandle>
  wrapOperand(llvm::MCInst &Instruction, uint64_t Index);
  llvm::Expected<NevercMCExprHandle>
  wrapExpression(const llvm::MCExpr &Expression);
  NevercStatus resolveUnit(NevercMCUnitHandle Handle,
                           PluginMCUnit **OutUnit) const;
  NevercStatus resolveInstruction(NevercMCInstHandle Handle,
                                  llvm::MCInst **OutInstruction) const;
  NevercStatus resolveOperand(NevercMCOperandHandle Handle,
                              OperandReference **OutOperand) const;
  NevercStatus resolveExpression(NevercMCExprHandle Handle,
                                 const llvm::MCExpr **OutExpression) const;
  llvm::Expected<NevercMCMutationHandle> beginMutation();
  NevercStatus checkMutation(NevercMCMutationHandle Mutation) const;
  NevercStatus commitMutation(NevercMCMutationHandle Mutation);
  NevercStatus abandonMutation(NevercMCMutationHandle Mutation);
  llvm::Expected<uint32_t> stableOpcode(uint32_t Backend) const;
  llvm::Expected<uint32_t> backendOpcode(uint32_t Stable) const;
  llvm::Expected<uint32_t> stableRegister(uint32_t Backend) const;
  llvm::Expected<uint32_t> backendRegister(uint32_t Stable) const;
  bool containsInstruction(const llvm::MCInst *Instruction) const;
  llvm::Expected<NevercMCInstHandle>
  createInstruction(NevercMCMutationHandle Mutation, uint32_t Opcode);
  NevercStatus appendOperand(NevercMCMutationHandle Mutation,
                             NevercMCInstHandle Instruction,
                             const NevercMCOperandValue &Value);
  NevercStatus insertInstructionBefore(
      NevercMCMutationHandle Mutation, NevercMCInstHandle Position,
      NevercMCInstHandle Instruction);
  NevercStatus appendInstruction(NevercMCMutationHandle Mutation,
                                 NevercMCUnitHandle Unit,
                                 NevercMCInstHandle Instruction);
  NevercStatus replaceInstruction(
      NevercMCMutationHandle Mutation, NevercMCInstHandle Instruction,
      NevercMCInstHandle Replacement);
  NevercStatus eraseInstruction(NevercMCMutationHandle Mutation,
                                NevercMCInstHandle Instruction);

private:
  void rollbackMutation();
  void finishBorrowedHandles();
  void invalidateInstruction(llvm::MCInst *Instruction);

  PluginTaskContext &Task;
  PluginMCUnit &Unit;
  NevercMCAPI API{};
  NevercMCUnitHandle UnitHandle{};
  NevercMCMutationHandle MutationHandle{};
  std::list<std::unique_ptr<llvm::MCInst>> Detached;
  std::list<std::unique_ptr<llvm::MCInst>> Removed;
  std::vector<llvm::MCInst *> Created;
  std::vector<std::function<void()>> UndoActions;
  std::vector<std::pair<NevercMCInstHandle, llvm::MCInst *>>
      InstructionHandles;
  std::vector<NevercMCOperandHandle> BorrowedOperandHandles;
  std::vector<NevercMCExprHandle> BorrowedExpressionHandles;
  llvm::DenseMap<uint32_t, uint32_t> StableToBackendOpcodes;
  llvm::DenseMap<uint32_t, uint32_t> BackendToStableOpcodes;
  llvm::DenseMap<uint32_t, uint32_t> StableToBackendRegisters;
  llvm::DenseMap<uint32_t, uint32_t> BackendToStableRegisters;
};

} // namespace neverc::plugin

#endif
