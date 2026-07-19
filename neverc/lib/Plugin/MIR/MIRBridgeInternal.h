#ifndef NEVERC_PLUGIN_MIR_MIRBRIDGEINTERNAL_H
#define NEVERC_PLUGIN_MIR_MIRBRIDGEINTERNAL_H

#include "neverc/Plugin/Host/MIRPluginBridge.h"

namespace neverc::plugin {

NevercStatus mirStatus(NevercStatusCode Code);
MIRPluginBridge *getMIRBridge(void *Context, NevercTaskHandle Task,
                              NevercStatus *OutStatus);
NevercMIROperandKind stableMIROperandKind(unsigned LLVMKind);
NevercMIRGenericOpcode stableMIRGenericOpcode(unsigned LLVMOpcode);
llvm::Expected<unsigned>
llvmMIRGenericOpcode(NevercMIRGenericOpcode StableOpcode);
llvm::Expected<llvm::MachineOperand>
createMIROperand(MIRPluginBridge &Bridge, llvm::MachineFunction &Function,
                 const NevercMIROperandValue &Value);
bool verifyMIRStructure(const llvm::MachineFunction &Function,
                        std::string &OutError);

NevercStatus NEVERC_CALL getMIROperandValue(void *Context,
                                            NevercTaskHandle Task,
                                            NevercMachineOperandHandle Operand,
                                            NevercMIROperandValue *OutValue);
NevercStatus NEVERC_CALL setMIROperandValue(void *Context,
                                            NevercTaskHandle Task,
                                            NevercMIRMutationHandle Mutation,
                                            NevercMachineOperandHandle Operand,
                                            const NevercMIROperandValue *Value);
NevercStatus NEVERC_CALL getMIROperandInstruction(
    void *Context, NevercTaskHandle Task, NevercMachineOperandHandle Operand,
    NevercMachineInstrHandle *OutInstruction);

} // namespace neverc::plugin

#endif
