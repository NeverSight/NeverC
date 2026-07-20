#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus mcStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

MCPluginBridge *resolveBridge(void *Context, NevercTaskHandle Task,
                              NevercStatus &Status) {
  if (!Context) {
    Status = mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return nullptr;
  }
  auto *Bridge = static_cast<MCPluginBridge *>(Context);
  NevercTaskHandle Expected = Bridge->taskHandle();
  if (Expected.Owner != Task.Owner || Expected.Value != Task.Value) {
    Status = mcStatus(NEVERC_STATUS_WRONG_SCOPE);
    return nullptr;
  }
  Status = neverc_status_ok();
  return Bridge;
}

bool validOutput(const NevercMCSchemaTokenInfo *Output) {
  return Output && Output->Header.StructSize >= sizeof(*Output) &&
         Output->Header.Major == NEVERC_MC_API_MAJOR &&
         Output->Header.Minor <= NEVERC_MC_API_MINOR;
}

NevercStatus NEVERC_CALL getSchemaToken(
    void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
    NevercMCSchemaTokenHandle *OutToken) {
  NevercStatus Status;
  MCPluginBridge *Bridge = resolveBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!OutToken)
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutToken = {};
  PluginMCUnit *ResolvedUnit = nullptr;
  Status = Bridge->resolveUnit(Unit, &ResolvedUnit);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Bridge->targetSchema())
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  auto Token = Bridge->schemaToken();
  if (!Token) {
    consumeError(Token.takeError());
    return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutToken = *Token;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getSchemaTokenInfo(
    void *Context, NevercTaskHandle Task, NevercMCSchemaTokenHandle Token,
    NevercMCSchemaTokenInfo *OutInfo) {
  NevercStatus Status;
  MCPluginBridge *Bridge = resolveBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validOutput(OutInfo))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Bridge->checkSchemaToken(Token);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const PluginTargetSnapshot::NamedRecord *Schema =
      Bridge->targetSchema();
  if (!Schema)
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  OutInfo->SchemaID = Schema->ID;
  OutInfo->TargetID = Schema->TargetID;
  OutInfo->Digest = {Schema->Digest.data(), Schema->Digest.size()};
  OutInfo->UnitGeneration = Bridge->unitGeneration();
  return neverc_status_ok();
}

} // namespace

void initializeMCSchemaAPI(NevercMCAPI &API, MCPluginBridge &Bridge) {
  API.Context = &Bridge;
  API.GetSchemaToken = getSchemaToken;
  API.GetSchemaTokenInfo = getSchemaTokenInfo;
}

} // namespace neverc::plugin
