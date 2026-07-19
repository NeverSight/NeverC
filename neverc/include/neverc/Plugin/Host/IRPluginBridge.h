#ifndef NEVERC_PLUGIN_HOST_IRPLUGINBRIDGE_H
#define NEVERC_PLUGIN_HOST_IRPLUGINBRIDGE_H

#include "neverc/Plugin/PluginIR.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace llvm {
class Comdat;
class LLVMContext;
class Metadata;
class Module;
class NamedMDNode;
class Type;
class Value;
} // namespace llvm

namespace neverc::plugin {

class PluginTaskContext;
class IRBuilderPluginBridge;
struct IRPluginAttributeRecord;

class IRPluginBridge {
public:
  static llvm::Expected<std::unique_ptr<IRPluginBridge>>
  create(PluginTaskContext &Task, llvm::StringRef ModuleIdentifier);
  static llvm::Expected<std::unique_ptr<IRPluginBridge>>
  createInContext(PluginTaskContext &Task, llvm::LLVMContext &Context,
                  llvm::StringRef ModuleIdentifier);
  static llvm::Expected<std::unique_ptr<IRPluginBridge>>
  adopt(PluginTaskContext &Task,
        std::unique_ptr<llvm::LLVMContext> Context,
        std::unique_ptr<llvm::Module> Module);
  static llvm::Expected<std::unique_ptr<IRPluginBridge>>
  borrow(PluginTaskContext &Task, llvm::Module &Module);

  ~IRPluginBridge();

  IRPluginBridge(const IRPluginBridge &) = delete;
  IRPluginBridge &operator=(const IRPluginBridge &) = delete;

  llvm::LLVMContext &context() const;
  llvm::Module &module() const;
  std::unique_ptr<llvm::Module> releaseModule();
  bool ownsModule() const { return OwnedModule != nullptr; }
  NevercTaskHandle taskHandle() const;
  uint64_t mutationGeneration() const { return MutationGeneration; }
  void noteExternalMutation() { noteMutation(); }
  void enterReadOnly();
  void leaveReadOnly();
  NevercStatus checkMutationAllowed() const;
  const NevercIRCoreAPI &coreAPI() const { return CoreAPI; }
  const NevercIRBuilderAPI &builderAPI() const;
  NevercIRContextHandle contextHandle() const { return ContextHandle; }
  NevercIRModuleHandle moduleHandle() const { return ModuleHandle; }
  NevercStatus importModule(NevercIRSerializationFormat Format,
                            NevercByteView Bytes);
  llvm::Expected<NevercIRSerializedBufferHandle>
  exportModule(NevercIRSerializationFormat Format);
  NevercStatus getSerializedBufferView(
      NevercIRSerializedBufferHandle Buffer, NevercByteView *OutBytes) const;
  NevercStatus
  releaseSerializedBuffer(NevercIRSerializedBufferHandle Buffer);

  llvm::Expected<NevercIRContextHandle>
  wrapContext(llvm::LLVMContext &Context);
  NevercStatus resolveContext(NevercIRContextHandle Handle,
                              llvm::LLVMContext **OutContext) const;
  llvm::Expected<NevercIRModuleHandle> wrapModule(llvm::Module &Module);
  NevercStatus resolveModule(NevercIRModuleHandle Handle,
                             llvm::Module **OutModule) const;
  llvm::Expected<NevercIRValueHandle> wrapValue(llvm::Value &Value);
  NevercStatus resolveValue(NevercIRValueHandle Handle,
                            llvm::Value **OutValue) const;
  NevercStatus getValueKind(NevercIRValueHandle Handle,
                            NevercIRValueKind *OutKind) const;
  NevercStatus replaceAllUsesWith(NevercIRValueHandle Original,
                                  NevercIRValueHandle Replacement);
  NevercStatus eraseValue(NevercIRValueHandle Handle);
  llvm::Expected<NevercIRTypeHandle> wrapType(llvm::Type &Type);
  NevercStatus resolveType(NevercIRTypeHandle Handle,
                           llvm::Type **OutType) const;
  NevercStatus getTypeKind(NevercIRTypeHandle Handle,
                           NevercIRTypeKind *OutKind) const;
  llvm::Expected<NevercIRTypeHandle>
  getPrimitiveType(NevercIRTypeKind Kind);
  llvm::Expected<NevercIRTypeHandle> getIntegerType(uint32_t BitWidth);
  llvm::Expected<NevercIRTypeHandle> getPointerType(uint32_t AddressSpace);
  llvm::Expected<NevercIRTypeHandle>
  getArrayType(NevercIRTypeHandle ElementType, uint64_t ElementCount);
  llvm::Expected<NevercIRTypeHandle>
  getVectorType(NevercIRTypeHandle ElementType, uint32_t MinimumElementCount,
                bool Scalable);
  llvm::Expected<NevercIRTypeHandle>
  getStructType(llvm::StringRef Name,
                llvm::ArrayRef<NevercIRTypeHandle> ElementTypes,
                bool Packed);
  llvm::Expected<NevercIRTypeHandle>
  getFunctionType(NevercIRTypeHandle ReturnType,
                  llvm::ArrayRef<NevercIRTypeHandle> ParameterTypes,
                  bool Variadic);
  llvm::Expected<NevercIRValueHandle>
  getIntegerConstant(NevercIRTypeHandle Type,
                     llvm::ArrayRef<uint64_t> LittleEndianWords);
  llvm::Expected<NevercIRValueHandle>
  getFloatingConstant(NevercIRTypeHandle Type,
                      llvm::ArrayRef<uint64_t> LittleEndianWords);
  llvm::Expected<NevercIRValueHandle>
  getNullConstant(NevercIRTypeHandle Type);
  llvm::Expected<NevercIRValueHandle>
  getPoisonConstant(NevercIRTypeHandle Type);
  llvm::Expected<NevercIRValueHandle>
  getUndefConstant(NevercIRTypeHandle Type);
  llvm::Expected<NevercIRValueHandle>
  getAggregateConstant(NevercIRTypeHandle Type,
                       llvm::ArrayRef<NevercIRValueHandle> Elements);
  llvm::Expected<NevercIRValueHandle>
  getConstantBinaryExpression(NevercIROpcode Opcode,
                              NevercIRValueHandle Left,
                              NevercIRValueHandle Right);
  llvm::Expected<NevercIRValueHandle>
  getConstantCastExpression(NevercIROpcode Opcode,
                            NevercIRValueHandle Operand,
                            NevercIRTypeHandle DestinationType);
  llvm::Expected<NevercIRValueHandle>
  getConstantCompareExpression(NevercIRPredicate Predicate,
                               NevercIRValueHandle Left,
                               NevercIRValueHandle Right);
  llvm::Expected<NevercIRValueHandle>
  getConstantGEPExpression(NevercIRTypeHandle SourceElementType,
                           NevercIRValueHandle Pointer,
                           llvm::ArrayRef<NevercIRValueHandle> Indices,
                           bool InBounds);
  llvm::Expected<NevercIRValueHandle>
  getGlobalAddressConstant(NevercIRValueHandle Global);
  llvm::Expected<NevercIRMetadataHandle>
  wrapMetadata(llvm::Metadata &Value);
  NevercStatus resolveMetadata(NevercIRMetadataHandle Handle,
                               llvm::Metadata **OutMetadata) const;
  llvm::Expected<NevercIRNamedMetadataHandle>
  wrapNamedMetadata(llvm::NamedMDNode &Value);
  NevercStatus
  resolveNamedMetadata(NevercIRNamedMetadataHandle Handle,
                       llvm::NamedMDNode **OutMetadata) const;
  NevercStatus getMetadataKind(NevercIRMetadataHandle Handle,
                               NevercIRMetadataKind *OutKind) const;
  llvm::Expected<NevercIRMetadataHandle>
  getMetadataString(llvm::StringRef Bytes);
  llvm::Expected<llvm::StringRef>
  getMetadataStringBytes(NevercIRMetadataHandle Handle) const;
  llvm::Expected<NevercIRMetadataHandle>
  getMetadataNode(llvm::ArrayRef<NevercIRMetadataHandle> Operands,
                  bool Distinct);
  llvm::Expected<NevercIRMetadataHandle>
  getValueAsMetadata(NevercIRValueHandle Value);
  llvm::Expected<NevercIRValueHandle>
  getMetadataAsValue(NevercIRMetadataHandle Metadata);
  NevercStatus getMetadataOperandCount(NevercIRMetadataHandle Node,
                                       uint64_t *OutCount) const;
  llvm::Expected<NevercIRMetadataHandle>
  getMetadataOperand(NevercIRMetadataHandle Node, uint64_t Index);
  llvm::Expected<NevercIRNamedMetadataHandle>
  getOrInsertNamedMetadata(llvm::StringRef Name);
  NevercStatus
  appendNamedMetadata(NevercIRNamedMetadataHandle Named,
                      NevercIRMetadataHandle Node);
  NevercStatus
  getNamedMetadataOperandCount(NevercIRNamedMetadataHandle Named,
                               uint64_t *OutCount) const;
  llvm::Expected<NevercIRMetadataHandle>
  getNamedMetadataOperand(NevercIRNamedMetadataHandle Named, uint64_t Index);
  NevercStatus
  getDebugLocationInfo(NevercIRMetadataHandle Location,
                       NevercIRDebugLocationInfo *OutInfo);
  llvm::Expected<NevercIRAttributeHandle>
  createEnumAttribute(llvm::StringRef Kind);
  llvm::Expected<NevercIRAttributeHandle>
  createIntegerAttribute(llvm::StringRef Kind, uint64_t Value);
  llvm::Expected<NevercIRAttributeHandle>
  createStringAttribute(llvm::StringRef Kind, llvm::StringRef Value);
  llvm::Expected<NevercIRAttributeHandle>
  createTypeAttribute(llvm::StringRef Kind, NevercIRTypeHandle Type);
  NevercStatus
  getAttributeValueKind(NevercIRAttributeHandle Attribute,
                        NevercIRAttributeValueKind *OutKind) const;
  llvm::Expected<llvm::StringRef>
  getAttributeKindName(NevercIRAttributeHandle Attribute) const;
  llvm::Expected<uint64_t>
  getAttributeIntegerValue(NevercIRAttributeHandle Attribute) const;
  llvm::Expected<llvm::StringRef>
  getAttributeStringValue(NevercIRAttributeHandle Attribute) const;
  llvm::Expected<NevercIRTypeHandle>
  getAttributeTypeValue(NevercIRAttributeHandle Attribute);
  NevercStatus
  addFunctionAttribute(NevercIRValueHandle Function,
                       NevercIRAttributeLocation Location,
                       uint32_t ParameterIndex,
                       NevercIRAttributeHandle Attribute);
  NevercStatus hasFunctionAttribute(NevercIRValueHandle Function,
                                    llvm::StringRef Kind,
                                    NevercBool *OutPresent) const;
  llvm::Expected<llvm::StringRef>
  getFunctionStringAttribute(NevercIRValueHandle Function,
                             llvm::StringRef Kind) const;
  llvm::StringRef getModuleIdentifier() const;
  NevercStatus setModuleIdentifier(llvm::StringRef Identifier);
  llvm::StringRef getModuleTargetTriple() const;
  NevercStatus setModuleTargetTriple(llvm::StringRef Triple);
  llvm::StringRef getModuleDataLayout() const;
  NevercStatus setModuleDataLayout(llvm::StringRef DataLayout);
  llvm::StringRef getModuleInlineAssembly() const;
  NevercStatus setModuleInlineAssembly(llvm::StringRef Assembly);
  NevercStatus beginValueCursor(NevercHandle Container,
                                NevercIRValueCollection Collection,
                                NevercIRValueCursor *OutCursor) const;
  NevercStatus collectValueCursor(
      NevercIRValueCursor *Cursor,
      llvm::MutableArrayRef<NevercIRValueHandle> OutValues,
      uint64_t *OutCount);
  llvm::Expected<llvm::StringRef>
  getValueName(NevercIRValueHandle Value) const;
  NevercStatus setValueName(NevercIRValueHandle Value, llvm::StringRef Name);
  llvm::Expected<NevercIRTypeHandle>
  getValueType(NevercIRValueHandle Value);
  NevercStatus getValueUseCount(NevercIRValueHandle Value,
                                uint64_t *OutCount) const;
  NevercStatus getValueUse(NevercIRValueHandle Value, uint64_t Index,
                           NevercIRUseInfo *OutUse);
  NevercStatus getOperandCount(NevercIRValueHandle Value,
                               uint64_t *OutCount) const;
  llvm::Expected<NevercIRValueHandle>
  getOperand(NevercIRValueHandle Value, uint64_t Index);
  NevercStatus setOperand(NevercIRValueHandle Value, uint64_t Index,
                          NevercIRValueHandle Operand);
  NevercStatus getGlobalLinkage(NevercIRValueHandle Global,
                                NevercIRLinkage *OutLinkage) const;
  NevercStatus setGlobalLinkage(NevercIRValueHandle Global,
                                NevercIRLinkage Linkage);
  NevercStatus getGlobalVisibility(NevercIRValueHandle Global,
                                   NevercIRVisibility *OutVisibility) const;
  NevercStatus setGlobalVisibility(NevercIRValueHandle Global,
                                   NevercIRVisibility Visibility);
  llvm::Expected<llvm::StringRef>
  getGlobalSection(NevercIRValueHandle Global) const;
  NevercStatus setGlobalSection(NevercIRValueHandle Global,
                                llvm::StringRef Section);
  llvm::Expected<NevercIRComdatHandle>
  getOrInsertComdat(llvm::StringRef Name);
  llvm::Expected<NevercIRComdatHandle>
  getGlobalComdat(NevercIRValueHandle Global);
  NevercStatus setGlobalComdat(NevercIRValueHandle Global,
                               NevercIRComdatHandle Comdat);
  NevercStatus getFunctionCallingConvention(
      NevercIRValueHandle Function,
      NevercIRCallingConvention *OutCallingConvention) const;
  NevercStatus setFunctionCallingConvention(
      NevercIRValueHandle Function,
      NevercIRCallingConvention CallingConvention);
  llvm::Expected<NevercIRValueHandle>
  getFunctionPersonality(NevercIRValueHandle Function);
  NevercStatus setFunctionPersonality(NevercIRValueHandle Function,
                                      NevercIRValueHandle Personality);
  llvm::Expected<llvm::StringRef>
  getFunctionGC(NevercIRValueHandle Function) const;
  NevercStatus setFunctionGC(NevercIRValueHandle Function,
                             llvm::StringRef GC);
  llvm::Expected<llvm::StringRef>
  getFunctionSection(NevercIRValueHandle Function) const;
  NevercStatus setFunctionSection(NevercIRValueHandle Function,
                                  llvm::StringRef Section);
  llvm::Expected<NevercIRValueHandle>
  getTerminator(NevercIRValueHandle Block);
  NevercStatus getPredecessorCount(NevercIRValueHandle Block,
                                   uint64_t *OutCount) const;
  llvm::Expected<NevercIRValueHandle>
  getPredecessor(NevercIRValueHandle Block, uint64_t Index);
  NevercStatus getSuccessorCount(NevercIRValueHandle Block,
                                 uint64_t *OutCount) const;
  llvm::Expected<NevercIRValueHandle>
  getSuccessor(NevercIRValueHandle Block, uint64_t Index);
  NevercStatus getInstructionOpcode(NevercIRValueHandle Instruction,
                                    NevercIROpcode *OutOpcode) const;
  NevercStatus getInstructionProperty(
      NevercIRValueHandle Instruction, NevercIRPropertyID Property,
      NevercIRPropertyValue *OutValue);
  NevercStatus setInstructionProperty(
      NevercIRValueHandle Instruction, NevercIRPropertyID Property,
      const NevercIRPropertyValue &Value);
  NevercStatus getPHIIncomingCount(NevercIRValueHandle Phi,
                                   uint64_t *OutCount) const;
  NevercStatus getPHIIncoming(NevercIRValueHandle Phi, uint64_t Index,
                              NevercIRPhiIncoming *OutIncoming);
  NevercStatus setPHIIncoming(NevercIRValueHandle Phi, uint64_t Index,
                              const NevercIRPhiIncoming &Incoming);

private:
  friend class IRBuilderPluginBridge;

  static NevercStatus makeStatus(NevercStatusCode Code,
                                 llvm::StringRef = {}) {
    NevercStatus Status = neverc_status_ok();
    Status.Code = Code;
    return Status;
  }
  static NevercStatus successStatus() { return neverc_status_ok(); }
  static NevercStatus viewToString(NevercStringView View,
                                   llvm::StringRef *OutValue) {
    if (OutValue == nullptr || (View.Data == nullptr && View.Length != 0))
      return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutValue =
        llvm::StringRef(View.Data != nullptr ? View.Data : "", View.Length);
    return successStatus();
  }

  explicit IRPluginBridge(PluginTaskContext &Task);
  llvm::Expected<NevercIRAttributeHandle>
  adoptAttributeRecord(std::unique_ptr<IRPluginAttributeRecord> Record);
  NevercStatus
  resolveAttributeRecord(NevercIRAttributeHandle Handle,
                         IRPluginAttributeRecord **OutRecord) const;
  llvm::Expected<NevercIRComdatHandle> wrapComdat(llvm::Comdat &Comdat);
  NevercStatus resolveComdat(NevercIRComdatHandle Handle,
                             llvm::Comdat **OutComdat) const;
  NevercStatus invalidateValueMetadataHandle(llvm::Value &Value);
  void noteMutation();
  void initializeCoreAPI();
  void initializeSerializationAPI();
  llvm::Error initialize(llvm::StringRef ModuleIdentifier);
  llvm::Error initializeOwned(
      std::unique_ptr<llvm::LLVMContext> OwnedContext,
      std::unique_ptr<llvm::Module> OwnedModule);
  llvm::Error initializeInContext(llvm::LLVMContext &Context,
                                  llvm::StringRef ModuleIdentifier);
  llvm::Error initializeModule(llvm::LLVMContext &Context,
                               std::unique_ptr<llvm::Module> Module);
  llvm::Error initializeBorrowed(llvm::Module &Module);
  llvm::Error initializeModuleHandles(llvm::LLVMContext &Context,
                                      llvm::Module &Module);
  void invalidateHandles();
  void invalidateModuleHandles();
  void invalidateSerializedBuffers();

  PluginTaskContext &Task;
  std::unique_ptr<llvm::LLVMContext> OwnedContext;
  llvm::LLVMContext *Context = nullptr;
  std::unique_ptr<llvm::Module> OwnedModule;
  llvm::Module *Module = nullptr;
  NevercIRContextHandle ContextHandle{};
  NevercIRModuleHandle ModuleHandle{};
  NevercIRCoreAPI CoreAPI{};
  std::unique_ptr<IRBuilderPluginBridge> BuilderBridge;
  uint64_t MutationGeneration = 1;
  std::atomic<uint32_t> ReadOnlyDepth{0};
  std::unordered_map<const llvm::Value *, NevercIRValueHandle> ValueHandles;
  std::unordered_map<const llvm::Type *, NevercIRTypeHandle> TypeHandles;
  std::unordered_map<const llvm::Metadata *, NevercIRMetadataHandle>
      MetadataHandles;
  std::unordered_map<const llvm::NamedMDNode *,
                     NevercIRNamedMetadataHandle>
      NamedMetadataHandles;
  std::unordered_map<const IRPluginAttributeRecord *,
                     NevercIRAttributeHandle>
      AttributeHandles;
  std::unordered_map<const llvm::Comdat *, NevercIRComdatHandle>
      ComdatHandles;
  std::vector<NevercIRSerializedBufferHandle> SerializedBuffers;
};

} // namespace neverc::plugin

#endif
