#ifndef NEVERC_PLUGIN_HOST_MCPLUGINBRIDGE_H
#define NEVERC_PLUGIN_HOST_MCPLUGINBRIDGE_H

#include "neverc/Plugin/Host/MCUnit.h"
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

class MCPluginBridge {
public:
  struct OperandReference {
    llvm::MCInst *Instruction = nullptr;
    uint64_t Index = 0;
  };

  MCPluginBridge(
      PluginTaskContext &Task, PluginMCUnit &Unit,
      const PluginTargetSnapshot::NamedRecord *Schema = nullptr,
      bool AllowMutation = true);
  ~MCPluginBridge();

  MCPluginBridge(const MCPluginBridge &) = delete;
  MCPluginBridge &operator=(const MCPluginBridge &) = delete;

  const NevercMCAPI &api() const { return API; }
  NevercTaskHandle taskHandle() const { return Task.handle(); }
  uint64_t unitGeneration() const { return UnitGeneration; }
  const PluginTargetSnapshot::NamedRecord *targetSchema() const {
    return Schema;
  }
  PluginMCUnit &unitValue() const { return Unit; }
  bool hasActiveMutation() const {
    return !neverc_handle_is_null(MutationHandle);
  }
  llvm::Expected<NevercMCUnitHandle> unit();
  llvm::Expected<NevercMCSchemaTokenHandle> schemaToken();
  NevercStatus
  checkSchemaToken(NevercMCSchemaTokenHandle Token) const;
  llvm::Expected<NevercMCInstHandle>
  wrapInstruction(llvm::MCInst &Instruction);
  llvm::Expected<NevercMCOperandHandle>
  wrapOperand(llvm::MCInst &Instruction, uint64_t Index);
  llvm::Expected<NevercMCExprHandle>
  wrapExpression(PluginMCExpression &Expression);
  llvm::Expected<NevercMCSectionHandle>
  wrapSection(PluginMCSection &Section);
  llvm::Expected<NevercMCSymbolHandle>
  wrapSymbol(PluginMCSymbol &Symbol);
  llvm::Expected<NevercMCFragmentHandle>
  wrapFragment(PluginMCFragment &Fragment);
  llvm::Expected<NevercMCFixupHandle>
  wrapFixup(PluginMCFixup &Fixup);
  NevercStatus resolveUnit(NevercMCUnitHandle Handle,
                           PluginMCUnit **OutUnit) const;
  NevercStatus resolveInstruction(NevercMCInstHandle Handle,
                                  llvm::MCInst **OutInstruction) const;
  NevercStatus resolveOperand(NevercMCOperandHandle Handle,
                              OperandReference **OutOperand) const;
  NevercStatus resolveExpression(NevercMCExprHandle Handle,
                                 PluginMCExpression **OutExpression) const;
  NevercStatus resolveSection(NevercMCSectionHandle Handle,
                              PluginMCSection **OutSection) const;
  NevercStatus resolveSymbol(NevercMCSymbolHandle Handle,
                             PluginMCSymbol **OutSymbol) const;
  NevercStatus resolveFragment(NevercMCFragmentHandle Handle,
                               PluginMCFragment **OutFragment) const;
  NevercStatus resolveFixup(NevercMCFixupHandle Handle,
                            PluginMCFixup **OutFixup) const;
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
  createInstruction(NevercMCMutationHandle Mutation,
                    NevercMCSchemaTokenHandle SchemaToken,
                    uint32_t Opcode);
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
  NevercStatus appendInstructionToFragment(
      NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment,
      NevercMCInstHandle Instruction);
  NevercStatus insertOperand(NevercMCMutationHandle Mutation,
                             NevercMCInstHandle Instruction, uint64_t Index,
                             const NevercMCOperandValue &Value,
                             NevercMCOperandHandle *OutOperand);
  NevercStatus eraseOperand(NevercMCMutationHandle Mutation,
                            NevercMCInstHandle Instruction, uint64_t Index);
  llvm::Expected<NevercMCSectionHandle>
  createSection(NevercMCMutationHandle Mutation,
                const NevercMCSectionDescriptor &Descriptor);
  NevercStatus moveSectionBefore(NevercMCMutationHandle Mutation,
                                 NevercMCSectionHandle Section,
                                 NevercMCSectionHandle Position);
  NevercStatus eraseSection(NevercMCMutationHandle Mutation,
                            NevercMCSectionHandle Section);
  llvm::Expected<NevercMCSymbolHandle>
  createSymbol(NevercMCMutationHandle Mutation,
               const NevercMCSymbolDescriptor &Descriptor);
  NevercStatus moveSymbolBefore(NevercMCMutationHandle Mutation,
                                NevercMCSymbolHandle Symbol,
                                NevercMCSymbolHandle Position);
  NevercStatus eraseSymbol(NevercMCMutationHandle Mutation,
                           NevercMCSymbolHandle Symbol);
  llvm::Expected<NevercMCExprHandle>
  createExpression(NevercMCMutationHandle Mutation,
                   const NevercMCExpressionDescriptor &Descriptor);
  NevercStatus setExpressionOperands(NevercMCMutationHandle Mutation,
                                     NevercMCExprHandle Expression,
                                     NevercMCExprHandle Left,
                                     NevercMCExprHandle Right);
  NevercStatus eraseExpression(NevercMCMutationHandle Mutation,
                               NevercMCExprHandle Expression);
  llvm::Expected<NevercMCFragmentHandle>
  createFragment(NevercMCMutationHandle Mutation,
                 NevercMCSectionHandle Section,
                 const NevercMCFragmentDescriptor &Descriptor);
  NevercStatus moveFragmentBefore(NevercMCMutationHandle Mutation,
                                  NevercMCFragmentHandle Fragment,
                                  NevercMCFragmentHandle Position);
  NevercStatus eraseFragment(NevercMCMutationHandle Mutation,
                             NevercMCFragmentHandle Fragment);
  llvm::Expected<NevercMCFixupHandle>
  createFixup(NevercMCMutationHandle Mutation,
              NevercMCFragmentHandle Fragment,
              const NevercMCFixupDescriptor &Descriptor);
  NevercStatus moveFixupBefore(NevercMCMutationHandle Mutation,
                               NevercMCFixupHandle Fixup,
                               NevercMCFixupHandle Position);
  NevercStatus eraseFixup(NevercMCMutationHandle Mutation,
                          NevercMCFixupHandle Fixup);

private:
  void rollbackMutation();
  void finishBorrowedHandles();
  void invalidateInstruction(llvm::MCInst *Instruction);
  void invalidateSection(PluginMCSection *Section);
  void invalidateSymbol(PluginMCSymbol *Symbol);
  void invalidateExpression(PluginMCExpression *Expression);
  void invalidateFragment(PluginMCFragment *Fragment);
  void invalidateFixup(PluginMCFixup *Fixup);
  void advanceUnitGeneration();

  PluginTaskContext &Task;
  PluginMCUnit &Unit;
  const PluginTargetSnapshot::NamedRecord *Schema = nullptr;
  bool MutationAllowed = true;
  uint64_t UnitGeneration = 1;
  NevercMCAPI API{};
  NevercMCUnitHandle UnitHandle{};
  NevercMCSchemaTokenHandle SchemaTokenHandle{};
  NevercMCMutationHandle MutationHandle{};
  std::list<std::unique_ptr<llvm::MCInst>> Detached;
  std::list<std::unique_ptr<llvm::MCInst>> Removed;
  std::vector<llvm::MCInst *> Created;
  std::vector<std::function<void()>> UndoActions;
  std::vector<std::pair<NevercMCInstHandle, llvm::MCInst *>>
      InstructionHandles;
  std::vector<NevercMCOperandHandle> BorrowedOperandHandles;
  std::vector<std::pair<NevercMCSectionHandle, PluginMCSection *>>
      SectionHandles;
  std::vector<std::pair<NevercMCSymbolHandle, PluginMCSymbol *>>
      SymbolHandles;
  std::vector<std::pair<NevercMCExprHandle, PluginMCExpression *>>
      ExpressionHandles;
  std::vector<std::pair<NevercMCFragmentHandle, PluginMCFragment *>>
      FragmentHandles;
  std::vector<std::pair<NevercMCFixupHandle, PluginMCFixup *>>
      FixupHandles;
  PluginMCUnit::SectionStorage RemovedSections;
  PluginMCUnit::SymbolStorage RemovedSymbols;
  PluginMCUnit::ExpressionStorage RemovedExpressions;
  PluginMCSection::FragmentStorage RemovedFragments;
  PluginMCFragment::FixupStorage RemovedFixups;
  std::vector<PluginMCSection *> CreatedSections;
  std::vector<PluginMCSymbol *> CreatedSymbols;
  std::vector<PluginMCExpression *> CreatedExpressions;
  std::vector<PluginMCFragment *> CreatedFragments;
  std::vector<PluginMCFixup *> CreatedFixups;
  llvm::DenseMap<uint32_t, uint32_t> StableToBackendOpcodes;
  llvm::DenseMap<uint32_t, uint32_t> BackendToStableOpcodes;
  llvm::DenseMap<uint32_t, uint32_t> StableToBackendRegisters;
  llvm::DenseMap<uint32_t, uint32_t> BackendToStableRegisters;
};

void initializeMCSchemaAPI(NevercMCAPI &API, MCPluginBridge &Bridge);
void initializeMCBuilderAPI(NevercMCAPI &API, MCPluginBridge &Bridge);
void initializeMCExpressionAPI(NevercMCAPI &API, MCPluginBridge &Bridge);

} // namespace neverc::plugin

#endif
