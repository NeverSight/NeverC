#include "neverc/Plugin/Host/MIRPluginBridge.h"
#include "neverc/Plugin/Host/MIRPassPlugin.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cstddef>
#include <memory>

using namespace llvm;

namespace neverc::plugin {
namespace {

struct SchemaRecord {
  uint32_t StableID;
  uint32_t LLVMValue;
  NevercBool RequiresTargetSchema;
  StringLiteral Name;
};

#define NEVERC_MIR_SCHEMA_ENTITY(Internal, Symbol, ID, LLVMValue, Name)        \
  {ID, LLVMValue, NEVERC_FALSE, Name},
constexpr SchemaRecord EntityRecords[] = {
#include "neverc/Plugin/Schema/PluginMIRSchema.inc"
};
#undef NEVERC_MIR_SCHEMA_ENTITY

#define NEVERC_MIR_SCHEMA_OPERAND(Internal, Symbol, ID, LLVMValue, Name)       \
  {ID, LLVMValue, NEVERC_FALSE, Name},
constexpr SchemaRecord OperandRecords[] = {
#include "neverc/Plugin/Schema/PluginMIRSchema.inc"
};
#undef NEVERC_MIR_SCHEMA_OPERAND

#define NEVERC_MIR_SCHEMA_GENERIC_OPCODE(Internal, Symbol, ID, LLVMValue,      \
                                         Name, RequiresTarget)                 \
  {ID, LLVMValue, RequiresTarget, Name},
constexpr SchemaRecord GenericOpcodeRecords[] = {
#include "neverc/Plugin/Schema/PluginMIRSchema.inc"
};
#undef NEVERC_MIR_SCHEMA_GENERIC_OPCODE

#define NEVERC_MIR_SCHEMA_PROPERTY(Internal, Symbol, ID, LLVMValue, Name)      \
  {ID, LLVMValue, NEVERC_FALSE, Name},
constexpr SchemaRecord PropertyRecords[] = {
#include "neverc/Plugin/Schema/PluginMIRSchema.inc"
};
#undef NEVERC_MIR_SCHEMA_PROPERTY

NevercStatus schemaStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

NevercStatus writeRecord(ArrayRef<SchemaRecord> Records, uint32_t StableID,
                         NevercMIRSchemaEntry *OutInfo) {
  if (!OutInfo || OutInfo->Header.StructSize < sizeof(*OutInfo) ||
      OutInfo->Header.Major != NEVERC_MIR_API_MAJOR)
    return schemaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  for (const SchemaRecord &Record : Records) {
    if (Record.StableID != StableID)
      continue;
    NevercMIRSchemaEntry Result{};
    Result.Header = {sizeof(Result), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                     0};
    Result.StableID = Record.StableID;
    Result.LLVMValue = Record.LLVMValue;
    Result.RequiresTargetSchema = Record.RequiresTargetSchema;
    Result.CanonicalName = {Record.Name.data(), Record.Name.size()};
    *OutInfo = Result;
    return neverc_status_ok();
  }
  return schemaStatus(NEVERC_STATUS_NOT_FOUND);
}

class MIRProcessBridge final : public PluginHostService {
public:
  MIRProcessBridge() { initialize(API, this); }

  static void initialize(NevercMIRAPI &API, void *Context) {
    API = {};
    API.Header = {sizeof(API), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR, 0};
    API.Context = Context;
    API.GetSchemaDigest = getSchemaDigest;
    API.GetEntityInfo = getEntityInfo;
    API.GetOperandKindInfo = getOperandKindInfo;
    API.GetGenericOpcodeInfo = getGenericOpcodeInfo;
    API.GetMachinePropertyInfo = getMachinePropertyInfo;
  }

  const NevercMIRAPI &api() const { return API; }

private:
  static NevercStatus NEVERC_CALL getSchemaDigest(void *Context,
                                                  NevercStringView *OutDigest) {
    if (!Context || !OutDigest)
      return schemaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    static constexpr StringLiteral Digest = NEVERC_MIR_SCHEMA_DIGEST;
    *OutDigest = {Digest.data(), Digest.size()};
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getEntityInfo(void *Context,
                                                NevercMIREntityKind Kind,
                                                NevercMIRSchemaEntry *OutInfo) {
    if (!Context)
      return schemaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return writeRecord(EntityRecords, Kind, OutInfo);
  }

  static NevercStatus NEVERC_CALL getOperandKindInfo(
      void *Context, NevercMIROperandKind Kind, NevercMIRSchemaEntry *OutInfo) {
    if (!Context)
      return schemaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return writeRecord(OperandRecords, Kind, OutInfo);
  }

  static NevercStatus NEVERC_CALL
  getGenericOpcodeInfo(void *Context, NevercMIRGenericOpcode Opcode,
                       NevercMIRSchemaEntry *OutInfo) {
    if (!Context)
      return schemaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return writeRecord(GenericOpcodeRecords, Opcode, OutInfo);
  }

  static NevercStatus NEVERC_CALL
  getMachinePropertyInfo(void *Context, NevercMIRMachineProperty Property,
                         NevercMIRSchemaEntry *OutInfo) {
    if (!Context)
      return schemaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return writeRecord(PropertyRecords, Property, OutInfo);
  }

  NevercMIRAPI API{};
};

NevercInterfaceID mirInterfaceID() {
  return {NEVERC_INTERFACE_MIR_HIGH, NEVERC_INTERFACE_MIR_LOW};
}

} // namespace

Error registerPluginMIRInterface(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register plugin MIR interface after freeze");
  auto Bridge = std::make_shared<MIRProcessBridge>();
  if (Error E = Services.registerHostService(mirInterfaceID(), Bridge))
    return std::move(E);
  if (Error E = Services.interfaces().registerInterface(
          mirInterfaceID(), NEVERC_MIR_INTERFACE_STABILITY, &Bridge->api(), {}))
    return E;
  return registerPluginMIRPassInterface(Services);
}

void initializeMIRSchemaAPI(NevercMIRAPI &API, void *Context) {
  MIRProcessBridge::initialize(API, Context);
}

} // namespace neverc::plugin
