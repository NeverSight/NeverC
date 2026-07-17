#include "SemaBridgeInternal.h"
#include "neverc/Analyze/Lookup.h"
#include "neverc/Tree/Decl/Decl.h"
#include <new>

using namespace llvm;

namespace neverc::plugin {

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::lookupName(
    void *Context, NevercTaskHandle Task,
    const NevercSemaLookupRequest *Request,
    NevercLookupResultHandle *OutResult) {
  if (!Context || !Request || !OutResult)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutResult = {};
  if (!semaValidHeader(Request->Header, sizeof(*Request)) ||
      Request->IncludeHidden > NEVERC_TRUE || Request->Reserved[0] != 0 ||
      Request->Reserved[1] != 0)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  Impl &Bridge = *static_cast<Impl *>(Context);
  ScopePayload *Scope = nullptr;
  NevercStatus Status = Bridge.resolveScope(Task, Request->Scope, &Scope);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  StringRef Name;
  if (!semaStringView(Request->Name, Name))
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  ResolveNameKind NativeKind;
  switch (Request->Kind) {
  case NEVERC_SEMA_LOOKUP_ORDINARY:
    NativeKind = ResolveOrdinary;
    break;
  case NEVERC_SEMA_LOOKUP_TAG:
    NativeKind = ResolveTag;
    break;
  case NEVERC_SEMA_LOOKUP_MEMBER:
    NativeKind = ResolveMember;
    if (!Scope->Context->isRecord())
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    break;
  default:
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }

  IdentifierInfo &Identifier = Bridge.SemanticAnalyzer.Context.Idents.get(Name);
  LookupResult Native(Bridge.SemanticAnalyzer, &Identifier, SourceLocation(),
                      NativeKind);
  Native.suppressDiagnostics();
  Native.setAllowHidden(Request->IncludeHidden == NEVERC_TRUE);
  Bridge.SemanticAnalyzer.LookupQualifiedName(Native, Scope->Context);

  auto *Payload = new (std::nothrow) LookupPayload;
  if (!Payload)
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Payload->Owner = &Bridge;
  if (Native.isAmbiguous())
    Payload->Kind = NEVERC_SEMA_LOOKUP_AMBIGUOUS;
  else if (Native.empty())
    Payload->Kind = NEVERC_SEMA_LOOKUP_NOT_FOUND;
  else
    Payload->Kind = NEVERC_SEMA_LOOKUP_FOUND;
  for (NamedDecl *Declaration : Native)
    Payload->Candidates.push_back(Declaration);

  auto Handle = Bridge.Task.handles().create(
      PluginSemaLookupResultHandleKind, Payload,
      [](void *Raw) { delete static_cast<LookupPayload *>(Raw); });
  if (!Handle) {
    delete Payload;
    consumeError(Handle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutResult = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getLookupResultInfo(
    void *Context, NevercTaskHandle Task, NevercLookupResultHandle Result,
    NevercSemaLookupResultInfo *OutInfo) {
  if (!Context || !OutInfo)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  LookupPayload *Payload = nullptr;
  NevercStatus Status = Bridge.resolveLookup(Task, Result, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercSemaLookupResultInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Info.Kind = Payload->Kind;
  Info.Ambiguous =
      Payload->Kind == NEVERC_SEMA_LOOKUP_AMBIGUOUS ? NEVERC_TRUE : NEVERC_FALSE;
  Info.CandidateCount = static_cast<uint64_t>(Payload->Candidates.size());
  return writeSemaRecord(OutInfo, Info);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getLookupCandidate(
    void *Context, NevercTaskHandle Task, NevercLookupResultHandle Result,
    uint64_t Index, NevercDeclHandle *OutDeclaration) {
  if (!Context || !OutDeclaration)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutDeclaration = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  LookupPayload *Payload = nullptr;
  NevercStatus Status = Bridge.resolveLookup(Task, Result, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Index >= Payload->Candidates.size())
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto Handle = Bridge.AST.publishDecl(
      Payload->Candidates[static_cast<size_t>(Index)]);
  if (!Handle) {
    consumeError(Handle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutDeclaration = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::destroyLookupResult(
    void *Context, NevercTaskHandle Task, NevercLookupResultHandle Result) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  LookupPayload *Payload = nullptr;
  NevercStatus Status = Bridge.resolveLookup(Task, Result, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Bridge.Task.handles().release(Result,
                                       PluginSemaLookupResultHandleKind);
}

} // namespace neverc::plugin
