#include "LTOInputSet.h"
#include "../Link/LinkGraph.h"
#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus ltoStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

std::pair<uint64_t, uint64_t> taskKey(NevercTaskHandle Task) {
  return {Task.Owner, Task.Value};
}

Error ltoError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

template <typename T>
bool validRecord(const T *Value) {
  return Value && Value->Header.StructSize >= sizeof(T) &&
         Value->Header.Major == NEVERC_LTO_API_MAJOR &&
         Value->Header.Minor <= NEVERC_LTO_API_MINOR;
}

template <typename T>
NevercStructArrayView arrayView(const std::vector<T> &Values) {
  return {Values.data(), Values.size(), sizeof(T)};
}

NevercLinkSymbolVisibility
mapVisibility(GlobalValue::VisibilityTypes Visibility) {
  switch (Visibility) {
  case GlobalValue::DefaultVisibility:
    return NEVERC_LINK_SYMBOL_VISIBILITY_DEFAULT;
  case GlobalValue::HiddenVisibility:
    return NEVERC_LINK_SYMBOL_VISIBILITY_HIDDEN;
  case GlobalValue::ProtectedVisibility:
    return NEVERC_LINK_SYMBOL_VISIBILITY_PROTECTED;
  }
  return NEVERC_LINK_SYMBOL_VISIBILITY_DEFAULT;
}

Error verifyTargetCompatibility(StringRef ModuleTriple,
                                NevercTargetKey Target) {
  StringRef TargetText(Target.RawTriple.Data ? Target.RawTriple.Data : "",
                       Target.RawTriple.Length);
  Triple ModuleTarget(Triple::normalize(ModuleTriple));
  Triple RequestedTarget(Triple::normalize(TargetText));
  if (ModuleTarget.getArch() == Triple::UnknownArch)
    return ltoError("bitcode module has an unknown target architecture");
  if (RequestedTarget.getArch() != Triple::UnknownArch &&
      ModuleTarget.getArch() != RequestedTarget.getArch())
    return ltoError("bitcode target architecture '" +
                    ModuleTarget.getArchName() +
                    "' does not match requested architecture '" +
                    RequestedTarget.getArchName() + "'");
  if (Target.PointerWidth != 0 &&
      ModuleTarget.isArch64Bit() != (Target.PointerWidth == 64))
    return ltoError("bitcode target pointer width is incompatible");
  const Triple::ObjectFormatType ModuleFormat =
      ModuleTarget.getObjectFormat();
  const Triple::ObjectFormatType RequestedFormat =
      RequestedTarget.getObjectFormat();
  if (ModuleFormat != Triple::UnknownObjectFormat &&
      RequestedFormat != Triple::UnknownObjectFormat &&
      ModuleFormat != RequestedFormat)
    return ltoError("bitcode object format is incompatible with link target");
  return Error::success();
}

NevercLTOOptionFlags knownOptionFlags() {
  return NEVERC_LTO_OPTION_EMIT_OPTIMIZED_BITCODE |
         NEVERC_LTO_OPTION_EMIT_INDEX |
         NEVERC_LTO_OPTION_SAVE_TEMPS |
         NEVERC_LTO_OPTION_WHOLE_PROGRAM_VISIBILITY |
         NEVERC_LTO_OPTION_UNIFIED_LTO |
         NEVERC_LTO_OPTION_DETERMINISTIC;
}

NevercLTOSymbolResolutionFlags knownResolutionFlags() {
  return NEVERC_LTO_SYMBOL_PREVAILING |
         NEVERC_LTO_SYMBOL_VISIBLE_TO_REGULAR_OBJECT |
         NEVERC_LTO_SYMBOL_EXPORTED |
         NEVERC_LTO_SYMBOL_FINAL_DEFINITION |
         NEVERC_LTO_SYMBOL_CAN_INLINE |
         NEVERC_LTO_SYMBOL_CAN_INTERNALIZE |
         NEVERC_LTO_SYMBOL_LINKER_REDEFINED |
         NEVERC_LTO_SYMBOL_REFERENCED_BY_REGULAR_OBJECT;
}

template <typename Output, typename Record, typename Fill>
NevercStatus fillRecordPage(ArrayRef<Record> Values, uint64_t Cursor,
                            NevercLinkEntityPage *Page,
                            Fill FillRecord) {
  if (!Page || Page->Header.StructSize < sizeof(*Page) ||
      Page->ElementStride < sizeof(Output) ||
      (Page->ElementCapacity != 0 && !Page->Data) ||
      Cursor > Values.size())
    return ltoStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Page->OutCount = 0;
  Page->NextCursor = Cursor;
  Page->Reserved = 0;
  auto *Bytes = static_cast<uint8_t *>(Page->Data);
  while (Page->NextCursor < Values.size() &&
         Page->OutCount < Page->ElementCapacity) {
    auto *Out = reinterpret_cast<Output *>(
        Bytes + Page->OutCount * Page->ElementStride);
    std::memset(Out, 0, sizeof(*Out));
    Out->Header = {sizeof(*Out), NEVERC_LTO_API_MAJOR,
                   NEVERC_LTO_API_MINOR, 0};
    NevercStatus Status =
        FillRecord(Values[Page->NextCursor], Out);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    ++Page->OutCount;
    ++Page->NextCursor;
  }
  Page->HasMore =
      Page->NextCursor < Values.size() ? NEVERC_TRUE : NEVERC_FALSE;
  return neverc_status_ok();
}

struct NativeSymbolFacts {
  bool HasRegularDefinition = false;
  bool HasStrongRegularDefinition = false;
  bool HasRegularReference = false;
  bool HasSharedDefinition = false;
  bool Exported = false;
  bool LinkerRedefined = false;
  uint64_t RepresentativeID = 0;
};

} // namespace

Error inspectPluginBitcodeModule(PluginLinkBitcodeModule &ModuleValue,
                                 MemoryBufferRef Buffer,
                                 NevercTargetKey Target) {
  auto Input = lto::InputFile::create(Buffer);
  if (!Input)
    return joinErrors(ltoError("cannot parse LLVM bitcode input"),
                      Input.takeError());
  if ((*Input)->Mods.size() != 1)
    return ltoError(
        "multi-module bitcode containers require explicit module splitting");
  if (Error E = verifyTargetCompatibility(
          (*Input)->getTargetTriple(), Target))
    return E;

  BitcodeModule &Bitcode = (*Input)->getSingleBitcodeModule();
  auto LTOInfo = Bitcode.getLTOInfo();
  if (!LTOInfo)
    return LTOInfo.takeError();
  LLVMContext Context;
  auto LazyModule = Bitcode.getLazyModule(
      Context, /*ShouldLazyLoadMetadata=*/true,
      /*IsImporting=*/false);
  if (!LazyModule)
    return LazyModule.takeError();
  auto Producer = getBitcodeProducerString(Buffer);
  if (!Producer)
    return Producer.takeError();

  ModuleValue.ModuleIdentifier =
      Bitcode.getModuleIdentifier().empty()
          ? (*Input)->getName().str()
          : Bitcode.getModuleIdentifier().str();
  ModuleValue.TargetTriple = (*Input)->getTargetTriple().str();
  ModuleValue.DataLayout = (*LazyModule)->getDataLayoutStr();
  ModuleValue.ProducerBuild = std::move(*Producer);
  ModuleValue.HasSummary = LTOInfo->HasSummary;
  ModuleValue.SummaryDigest =
      ModuleValue.HasSummary
          ? SHA256::hash(ArrayRef<uint8_t>(
                reinterpret_cast<const uint8_t *>(
                    Buffer.getBufferStart()),
                Buffer.getBufferSize()))
          : std::array<uint8_t, 32>{};
  ModuleValue.Symbols.clear();
  ModuleValue.Symbols.reserve((*Input)->symbols().size());
  ArrayRef<std::pair<StringRef, Comdat::SelectionKind>> Comdats =
      (*Input)->getComdatTable();
  for (const lto::InputFile::Symbol &Symbol :
       (*Input)->symbols()) {
    PluginLinkBitcodeSymbol Value;
    Value.Name = Symbol.getName().str();
    Value.Visibility = mapVisibility(Symbol.getVisibility());
    Value.Undefined = Symbol.isUndefined();
    Value.Weak = Symbol.isWeak();
    Value.Common = Symbol.isCommon();
    Value.TLS = Symbol.isTLS();
    Value.Executable = Symbol.isExecutable();
    Value.Used = Symbol.isUsed();
    Value.CommonSize = Symbol.getCommonSize();
    Value.CommonAlignment = Symbol.getCommonAlignment();
    const int ComdatIndex = Symbol.getComdatIndex();
    if (ComdatIndex >= 0 &&
        static_cast<size_t>(ComdatIndex) < Comdats.size())
      Value.ComdatName = Comdats[ComdatIndex].first.str();
    ModuleValue.Symbols.push_back(std::move(Value));
  }
  return Error::success();
}

LTOProcessService::LTOProcessService() {
  API.Header = {sizeof(API), NEVERC_LTO_API_MAJOR,
                NEVERC_LTO_API_MINOR, 0};
  API.Context = this;
  API.GetRequest = getRequest;
  API.GetModulePage = getModulePage;
  API.GetResolutionPage = getResolutionPage;
  API.GetModuleInfo = getModuleInfo;
  API.GetResolutionInfo = getResolutionInfo;
}

Error LTOProcessService::attach(LTOInputSet &Runtime) {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (!Active.emplace(taskKey(Runtime.taskHandle()), &Runtime).second)
    return ltoError("an LTO request is already active for this task");
  return Error::success();
}

void LTOProcessService::detach(NevercTaskHandle Task) {
  std::lock_guard<std::mutex> Lock(Mutex);
  Active.erase(taskKey(Task));
}

void LTOProcessService::taskScopeUnregistered(
    NevercTaskHandle Task) noexcept {
  detach(Task);
}

LTOInputSet *LTOProcessService::find(NevercTaskHandle Task) {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto It = Active.find(taskKey(Task));
  return It == Active.end() ? nullptr : It->second;
}

NevercStatus NEVERC_CALL LTOProcessService::getRequest(
    void *Context, NevercTaskHandle Task,
    NevercLTORequestHandle Request,
    NevercLTORequest *OutRequest) {
  if (!Context)
    return ltoStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  LTOInputSet *Runtime =
      static_cast<LTOProcessService *>(Context)->find(Task);
  return Runtime ? Runtime->fillRequest(Request, OutRequest)
                 : ltoStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL LTOProcessService::getModulePage(
    void *Context, NevercTaskHandle Task,
    NevercLTORequestHandle Request, uint64_t Cursor,
    NevercLinkEntityPage *Page) {
  if (!Context)
    return ltoStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  LTOInputSet *Runtime =
      static_cast<LTOProcessService *>(Context)->find(Task);
  return Runtime ? Runtime->fillModulePage(Request, Cursor, Page)
                 : ltoStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL LTOProcessService::getResolutionPage(
    void *Context, NevercTaskHandle Task,
    NevercLTORequestHandle Request, uint64_t Cursor,
    NevercLinkEntityPage *Page) {
  if (!Context)
    return ltoStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  LTOInputSet *Runtime =
      static_cast<LTOProcessService *>(Context)->find(Task);
  return Runtime ? Runtime->fillResolutionPage(Request, Cursor, Page)
                 : ltoStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL LTOProcessService::getModuleInfo(
    void *Context, NevercTaskHandle Task,
    NevercLTOInputModuleHandle Module,
    NevercLTOInputModuleInfo *OutInfo) {
  if (!Context)
    return ltoStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  LTOInputSet *Runtime =
      static_cast<LTOProcessService *>(Context)->find(Task);
  return Runtime ? Runtime->fillModuleInfo(Module, OutInfo)
                 : ltoStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus NEVERC_CALL LTOProcessService::getResolutionInfo(
    void *Context, NevercTaskHandle Task,
    NevercLTOResolutionHandle Resolution,
    NevercLTOSymbolResolution *OutInfo) {
  if (!Context)
    return ltoStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  LTOInputSet *Runtime =
      static_cast<LTOProcessService *>(Context)->find(Task);
  return Runtime ? Runtime->fillResolutionInfo(Resolution, OutInfo)
                 : ltoStatus(NEVERC_STATUS_STALE_HANDLE);
}

LTOInputSet::LTOInputSet(
    PluginTaskContext &TaskValue,
    std::shared_ptr<const LinkRequest> RequestValue,
    std::unique_ptr<LinkInputSet> InputsValue,
    LTOInputSetOptions OptionsValue)
    : Task(TaskValue), Request(std::move(RequestValue)),
      Inputs(std::move(InputsValue)), Options(std::move(OptionsValue)) {}

Expected<std::unique_ptr<LTOInputSet>>
LTOInputSet::create(PluginTaskContext &Task,
                    std::shared_ptr<const LinkRequest> Request,
                    std::unique_ptr<LinkInputSet> Inputs,
                    LTOInputSetOptions Options) {
  if (!Request || !Inputs)
    return ltoError("LTO input construction requires request and inputs");
  auto Result = std::unique_ptr<LTOInputSet>(
      new LTOInputSet(Task, std::move(Request), std::move(Inputs),
                      std::move(Options)));
  if (Error E = Result->initialize())
    return std::move(E);
  return Result;
}

LTOInputSet::~LTOInputSet() {
  if (Attached && Service)
    Service->detach(taskHandle());
  for (const LTOSymbolResolutionRecord &Resolution :
       Resolutions)
    (void)Task.handles().release(
        Resolution.Handle, PluginLTOResolutionHandleKind);
  for (const LTOInputModuleRecord &Module : Modules) {
    if (!neverc_handle_is_null(Module.SummaryHandle))
      (void)Task.handles().release(
          Module.SummaryHandle, PluginLTOSummaryHandleKind);
    (void)Task.handles().release(
        Module.Handle, PluginLTOInputModuleHandleKind);
  }
  if (!neverc_handle_is_null(RequestHandle))
    (void)Task.handles().release(
        RequestHandle, PluginLTORequestHandleKind);
}

NevercTaskHandle LTOInputSet::taskHandle() const {
  return Task.handle();
}

Error LTOInputSet::initialize() {
  if (Options.Mode != NEVERC_LTO_FULL &&
      Options.Mode != NEVERC_LTO_THIN)
    return ltoError("LTO input set requires full or thin mode");
  if ((Options.Flags & ~knownOptionFlags()) != 0)
    return ltoError("LTO input set has unknown option flags");
  GraphBridge =
      std::make_unique<LinkGraphPluginBridge>(Task, Inputs->graph(),
                                              /*AllowMutation=*/false);
  auto Graph = GraphBridge->graph();
  if (!Graph)
    return Graph.takeError();
  GraphHandle = *Graph;
  auto RequestValue = Task.handles().create(
      PluginLTORequestHandleKind, this);
  if (!RequestValue)
    return RequestValue.takeError();
  RequestHandle = *RequestValue;
  if (Error E = inspectModules())
    return E;
  if (Error E = buildResolutions())
    return E;
  if (Error E = verify())
    return E;
  Service = findLTOProcessService(Task.processServices());
  if (!Service)
    return ltoError("LTO interface is not registered");
  if (Error E = Service->attach(*this))
    return E;
  Attached = true;
  return Error::success();
}

Error LTOInputSet::inspectModules() {
  PluginLinkGraph &Graph = Inputs->graph();
  Modules.reserve(Graph.bitcodeModules().size());
  for (PluginLinkBitcodeModule &Module : Graph.bitcodeModules()) {
    auto Buffer = Inputs->bitcodeBufferForModule(Module.ID);
    if (!Buffer)
      return Buffer.takeError();
    if (Error E =
            inspectPluginBitcodeModule(Module, *Buffer, Graph.targetKey()))
      return joinErrors(
          ltoError("cannot inspect bitcode module '" + Module.Name + "'"),
          std::move(E));

    LTOInputModuleRecord Record;
    Record.Module = &Module;
    if (Module.HasSummary) {
      auto Summary = getModuleSummaryIndex(*Buffer);
      if (!Summary)
        return Summary.takeError();
      Record.Summary =
          std::shared_ptr<ModuleSummaryIndex>(std::move(*Summary));
      auto SummaryHandle = Task.handles().create(
          PluginLTOSummaryHandleKind, Record.Summary.get());
      if (!SummaryHandle)
        return SummaryHandle.takeError();
      Record.SummaryHandle = *SummaryHandle;
      Module.Summary = *SummaryHandle;
    }
    Modules.push_back(std::move(Record));
  }

  for (LTOInputModuleRecord &Record : Modules) {
    auto Handle = Task.handles().create(
        PluginLTOInputModuleHandleKind, &Record);
    if (!Handle)
      return Handle.takeError();
    Record.Handle = *Handle;
  }
  return Error::success();
}

Error LTOInputSet::buildResolutions() {
  std::map<std::string, NativeSymbolFacts> Native;
  for (const PluginLinkSymbol &Symbol : Inputs->graph().symbols()) {
    NativeSymbolFacts &Facts = Native[Symbol.Name];
    if (Facts.RepresentativeID == 0)
      Facts.RepresentativeID = Symbol.ID;
    Facts.Exported |= Symbol.IsExported;
    if (Symbol.Definition == NEVERC_LINK_SYMBOL_UNDEFINED) {
      Facts.HasRegularReference = true;
      continue;
    }
    if (Symbol.Definition == NEVERC_LINK_SYMBOL_SHARED) {
      Facts.HasSharedDefinition = true;
      continue;
    }
    Facts.HasRegularDefinition = true;
    Facts.HasStrongRegularDefinition |=
        Symbol.Binding == NEVERC_LINK_SYMBOL_BINDING_GLOBAL &&
        Symbol.Definition != NEVERC_LINK_SYMBOL_COMMON;
  }

  struct Candidate {
    size_t Module = 0;
    size_t Symbol = 0;
  };
  std::map<std::string, std::vector<Candidate>> Candidates;
  size_t ResolutionCount = 0;
  for (size_t ModuleIndex = 0; ModuleIndex != Modules.size();
       ++ModuleIndex) {
    const auto &Symbols = Modules[ModuleIndex].Module->Symbols;
    ResolutionCount += Symbols.size();
    for (size_t SymbolIndex = 0; SymbolIndex != Symbols.size();
         ++SymbolIndex)
      if (!Symbols[SymbolIndex].Undefined)
        Candidates[Symbols[SymbolIndex].Name].push_back(
            {ModuleIndex, SymbolIndex});
  }

  std::map<std::string, Candidate> Winners;
  for (auto &[Name, Values] : Candidates) {
    const NativeSymbolFacts &Facts = Native[Name];
    if (Facts.HasStrongRegularDefinition)
      continue;
    auto Strong = llvm::find_if(Values, [&](const Candidate &Value) {
      const PluginLinkBitcodeSymbol &Symbol =
          Modules[Value.Module].Module->Symbols[Value.Symbol];
      return !Symbol.Weak && !Symbol.Common;
    });
    if (Strong != Values.end())
      Winners.emplace(Name, *Strong);
    else if (!Facts.HasRegularDefinition && !Values.empty())
      Winners.emplace(Name, Values.front());
  }

  Resolutions.reserve(ResolutionCount);
  for (size_t ModuleIndex = 0; ModuleIndex != Modules.size();
       ++ModuleIndex) {
    LTOInputModuleRecord &Module = Modules[ModuleIndex];
    for (size_t SymbolIndex = 0;
         SymbolIndex != Module.Module->Symbols.size(); ++SymbolIndex) {
      const PluginLinkBitcodeSymbol &Symbol =
          Module.Module->Symbols[SymbolIndex];
      const NativeSymbolFacts &Facts = Native[Symbol.Name];
      LTOSymbolResolutionRecord Record;
      Record.Module = Module.Handle;
      Record.SymbolName = Symbol.Name;
      Record.ComdatName = Symbol.ComdatName;
      Record.Undefined = Symbol.Undefined;
      auto Winner = Winners.find(Symbol.Name);
      const bool Prevailing =
          Winner != Winners.end() &&
          Winner->second.Module == ModuleIndex &&
          Winner->second.Symbol == SymbolIndex;
      if (Prevailing)
        Record.Flags |= NEVERC_LTO_SYMBOL_PREVAILING;
      const bool Visible =
          Facts.HasRegularDefinition || Facts.HasRegularReference ||
          Request->outputKind() == NEVERC_LINK_OUTPUT_RELOCATABLE;
      if (Visible)
        Record.Flags |=
            NEVERC_LTO_SYMBOL_VISIBLE_TO_REGULAR_OBJECT;
      if (Facts.Exported)
        Record.Flags |= NEVERC_LTO_SYMBOL_EXPORTED;
      if (Facts.HasRegularReference)
        Record.Flags |=
            NEVERC_LTO_SYMBOL_REFERENCED_BY_REGULAR_OBJECT;
      if (Facts.LinkerRedefined)
        Record.Flags |= NEVERC_LTO_SYMBOL_LINKER_REDEFINED;
      if (Prevailing && !Facts.HasSharedDefinition &&
          !Facts.LinkerRedefined)
        Record.Flags |= NEVERC_LTO_SYMBOL_FINAL_DEFINITION |
                        NEVERC_LTO_SYMBOL_CAN_INLINE;
      if (Prevailing && !Visible && !Facts.Exported)
        Record.Flags |= NEVERC_LTO_SYMBOL_CAN_INTERNALIZE;
      if (Facts.RepresentativeID != 0) {
        auto LinkSymbol = GraphBridge->wrapEntity(
            LinkGraphPluginBridge::EntityKind::Symbol,
            Facts.RepresentativeID);
        if (!LinkSymbol)
          return LinkSymbol.takeError();
        Record.LinkSymbol = *LinkSymbol;
      }
      Resolutions.push_back(std::move(Record));
    }
  }

  for (LTOSymbolResolutionRecord &Record : Resolutions) {
    auto Handle = Task.handles().create(
        PluginLTOResolutionHandleKind, &Record);
    if (!Handle)
      return Handle.takeError();
    Record.Handle = *Handle;
  }
  rebuildResolutionDigest();
  return Error::success();
}

void LTOInputSet::rebuildResolutionDigest() {
  std::string Encoded;
  raw_string_ostream OS(Encoded);
  for (const LTOSymbolResolutionRecord &Resolution : Resolutions)
    OS << Resolution.Module.Owner << ":" << Resolution.Module.Value
       << ":" << Resolution.SymbolName << ":" << Resolution.ComdatName
       << ":" << Resolution.Version << ":" << Resolution.Flags << ";";
  OS.flush();
  ResolutionDigest = SHA256::hash(ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(Encoded.data()), Encoded.size()));
}

Error LTOInputSet::verify() const {
  std::map<std::string, unsigned> Prevailing;
  std::set<std::pair<uint64_t, uint64_t>> ModuleHandles;
  for (const LTOInputModuleRecord &Module : Modules)
    ModuleHandles.emplace(Module.Handle.Owner, Module.Handle.Value);
  for (const LTOSymbolResolutionRecord &Resolution : Resolutions) {
    if ((Resolution.Flags & ~knownResolutionFlags()) != 0)
      return ltoError("LTO resolution has unknown flags");
    if (!ModuleHandles.count(
            {Resolution.Module.Owner, Resolution.Module.Value}))
      return ltoError("LTO resolution refers to a foreign module");
    const bool IsPrevailing =
        (Resolution.Flags & NEVERC_LTO_SYMBOL_PREVAILING) != 0;
    if (IsPrevailing && Resolution.Undefined)
      return ltoError("undefined LTO symbol cannot be prevailing");
    if (IsPrevailing && ++Prevailing[Resolution.SymbolName] > 1)
      return ltoError("more than one prevailing LTO definition for '" +
                      Resolution.SymbolName + "'");
    if ((Resolution.Flags & NEVERC_LTO_SYMBOL_CAN_INTERNALIZE) != 0 &&
        (!IsPrevailing ||
         (Resolution.Flags &
          (NEVERC_LTO_SYMBOL_VISIBLE_TO_REGULAR_OBJECT |
           NEVERC_LTO_SYMBOL_EXPORTED)) != 0))
      return ltoError(
          "LTO internalization conflicts with visibility or prevailing state");
    if ((Resolution.Flags &
         (NEVERC_LTO_SYMBOL_FINAL_DEFINITION |
          NEVERC_LTO_SYMBOL_CAN_INLINE)) != 0 &&
        !IsPrevailing)
      return ltoError(
          "non-prevailing LTO symbol has final or inline permission");
  }
  return verifyPluginLinkGraph(Inputs->graph());
}

Error LTOInputSet::setResolutionFlags(
    NevercLTOResolutionHandle Resolution,
    NevercLTOSymbolResolutionFlags Flags) {
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Resolution, PluginLTOResolutionHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK || !Payload)
    return ltoError("stale or foreign LTO resolution handle");
  auto *Record = static_cast<LTOSymbolResolutionRecord *>(Payload);
  const NevercLTOSymbolResolutionFlags Previous = Record->Flags;
  Record->Flags = Flags;
  if (Error E = verify()) {
    Record->Flags = Previous;
    return E;
  }
  rebuildResolutionDigest();
  return Error::success();
}

NevercStatus LTOInputSet::fillModuleInfo(
    NevercLTOInputModuleHandle ModuleHandle,
    NevercLTOInputModuleInfo *OutInfo) {
  if (!validRecord(OutInfo))
    return ltoStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      ModuleHandle, PluginLTOInputModuleHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK || !Payload)
    return Status;
  const auto &Record =
      *static_cast<const LTOInputModuleRecord *>(Payload);
  const PluginLinkBitcodeModule &Module = *Record.Module;
  std::memset(OutInfo, 0, sizeof(*OutInfo));
  OutInfo->Header = {sizeof(*OutInfo), NEVERC_LTO_API_MAJOR,
                     NEVERC_LTO_API_MINOR, 0};
  OutInfo->Module = Record.Handle;
  auto LinkInput = GraphBridge->wrapEntity(
      LinkGraphPluginBridge::EntityKind::Input, Module.InputID);
  if (!LinkInput) {
    consumeError(LinkInput.takeError());
    return ltoStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  OutInfo->LinkInput = *LinkInput;
  OutInfo->LogicalURI = {Module.Name.data(), Module.Name.size()};
  OutInfo->ModuleIdentifier = {Module.ModuleIdentifier.data(),
                               Module.ModuleIdentifier.size()};
  OutInfo->DataLayout = {Module.DataLayout.data(),
                         Module.DataLayout.size()};
  std::copy(Module.ContentDigest.begin(), Module.ContentDigest.end(),
            OutInfo->ContentDigest);
  OutInfo->SymbolCount = Module.Symbols.size();
  OutInfo->Extensions = Module.Extensions.view();
  return neverc_status_ok();
}

NevercStatus LTOInputSet::fillResolutionInfo(
    NevercLTOResolutionHandle ResolutionHandle,
    NevercLTOSymbolResolution *OutInfo) {
  if (!validRecord(OutInfo))
    return ltoStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      ResolutionHandle, PluginLTOResolutionHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK || !Payload)
    return Status;
  const auto &Resolution =
      *static_cast<const LTOSymbolResolutionRecord *>(Payload);
  std::memset(OutInfo, 0, sizeof(*OutInfo));
  OutInfo->Header = {sizeof(*OutInfo), NEVERC_LTO_API_MAJOR,
                     NEVERC_LTO_API_MINOR, 0};
  OutInfo->Resolution = Resolution.Handle;
  OutInfo->Module = Resolution.Module;
  OutInfo->SymbolName = {Resolution.SymbolName.data(),
                         Resolution.SymbolName.size()};
  OutInfo->LinkSymbol = Resolution.LinkSymbol;
  OutInfo->Flags = Resolution.Flags;
  OutInfo->ComdatName = {Resolution.ComdatName.data(),
                         Resolution.ComdatName.size()};
  OutInfo->Version = {Resolution.Version.data(),
                      Resolution.Version.size()};
  return neverc_status_ok();
}

NevercStatus LTOInputSet::fillRequest(
    NevercLTORequestHandle Handle, NevercLTORequest *OutRequest) {
  if (!validRecord(OutRequest) ||
      !sameHandle(Handle, RequestHandle))
    return ltoStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  ModuleViews.resize(Modules.size());
  for (size_t Index = 0; Index != Modules.size(); ++Index) {
    ModuleViews[Index].Header = {
        sizeof(NevercLTOInputModuleInfo), NEVERC_LTO_API_MAJOR,
        NEVERC_LTO_API_MINOR, 0};
    NevercStatus Status =
        fillModuleInfo(Modules[Index].Handle, &ModuleViews[Index]);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  ResolutionViews.resize(Resolutions.size());
  for (size_t Index = 0; Index != Resolutions.size(); ++Index) {
    ResolutionViews[Index].Header = {
        sizeof(NevercLTOSymbolResolution), NEVERC_LTO_API_MAJOR,
        NEVERC_LTO_API_MINOR, 0};
    NevercStatus Status = fillResolutionInfo(
        Resolutions[Index].Handle, &ResolutionViews[Index]);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }

  std::memset(OutRequest, 0, sizeof(*OutRequest));
  OutRequest->Header = {sizeof(*OutRequest), NEVERC_LTO_API_MAJOR,
                        NEVERC_LTO_API_MINOR, 0};
  OutRequest->Request = RequestHandle;
  OutRequest->Task = taskHandle();
  OutRequest->LinkRequest = Request->handle();
  OutRequest->LinkGraph = GraphHandle;
  OutRequest->Target = Request->target();
  OutRequest->OutputFormat = Request->outputFormat();
  OutRequest->Options.Header = {
      sizeof(NevercLTOOptions), NEVERC_LTO_API_MAJOR,
      NEVERC_LTO_API_MINOR, 0};
  OutRequest->Options.Mode = Options.Mode;
  OutRequest->Options.CacheScope = Options.CacheScope;
  OutRequest->Options.Flags = Options.Flags;
  OutRequest->Options.OptimizationLevel = Options.OptimizationLevel;
  OutRequest->Options.CodeGenOptimizationLevel =
      Options.CodeGenOptimizationLevel;
  OutRequest->Options.ThreadBudget = Options.ThreadBudget;
  OutRequest->Options.ThinBackendPartitions =
      Options.ThinBackendPartitions;
  OutRequest->Options.CPU = {Options.CPU.data(), Options.CPU.size()};
  OutRequest->Options.Features = {Options.Features.data(),
                                  Options.Features.size()};
  OutRequest->Options.CacheNamespace = {
      Options.CacheNamespace.data(), Options.CacheNamespace.size()};
  OutRequest->Modules = arrayView(ModuleViews);
  OutRequest->Resolutions = arrayView(ResolutionViews);
  std::copy(ResolutionDigest.begin(), ResolutionDigest.end(),
            OutRequest->ResolutionDigest);
  ArrayRef<uint8_t> RequestBytes = Request->digest();
  std::copy(RequestBytes.begin(), RequestBytes.end(),
            OutRequest->RequestDigest);
  return neverc_status_ok();
}

NevercStatus LTOInputSet::fillModulePage(
    NevercLTORequestHandle Handle, uint64_t Cursor,
    NevercLinkEntityPage *Page) {
  if (!sameHandle(Handle, RequestHandle))
    return ltoStatus(NEVERC_STATUS_STALE_HANDLE);
  return fillRecordPage<NevercLTOInputModuleInfo>(
      ArrayRef<LTOInputModuleRecord>(Modules), Cursor, Page,
      [&](const LTOInputModuleRecord &Record,
          NevercLTOInputModuleInfo *Out) {
        return fillModuleInfo(Record.Handle, Out);
      });
}

NevercStatus LTOInputSet::fillResolutionPage(
    NevercLTORequestHandle Handle, uint64_t Cursor,
    NevercLinkEntityPage *Page) {
  if (!sameHandle(Handle, RequestHandle))
    return ltoStatus(NEVERC_STATUS_STALE_HANDLE);
  return fillRecordPage<NevercLTOSymbolResolution>(
      ArrayRef<LTOSymbolResolutionRecord>(Resolutions), Cursor, Page,
      [&](const LTOSymbolResolutionRecord &Record,
          NevercLTOSymbolResolution *Out) {
        return fillResolutionInfo(Record.Handle, Out);
      });
}

std::shared_ptr<LTOProcessService>
findLTOProcessService(PluginProcessServices &Services) {
  return std::static_pointer_cast<LTOProcessService>(
      Services.findHostService(ltoInterfaceID()));
}

Error registerPluginLTOInterface(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return ltoError(
        "cannot register LTO interface after interface freeze");
  auto Service = std::make_shared<LTOProcessService>();
  if (Error E =
          Services.registerHostService(ltoInterfaceID(), Service))
    return E;
  return Services.interfaces().registerInterface(
      ltoInterfaceID(), NEVERC_LTO_INTERFACE_STABILITY,
      &Service->api(), {});
}

} // namespace neverc::plugin
