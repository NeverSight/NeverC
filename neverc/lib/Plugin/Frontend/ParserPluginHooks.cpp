#include "neverc/Syntax/ParserPluginHooks.h"
#include "ASTUnitArtifact.h"
#include "FrontendPluginInterfaces.h"
#include "TokenStream.h"
#include "neverc/Analyze/ParsedAttr.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus parserStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool validParserHeader(const NevercABITableHeader &Header,
                       uint64_t RequiredSize) {
  return Header.StructSize >= RequiredSize &&
         Header.Major == NEVERC_PARSER_API_MAJOR &&
         Header.Minor <= NEVERC_PARSER_API_MINOR && Header.Flags == 0;
}

template <typename T>
NevercStatus writeCallerRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return parserStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value) ? parserStatus(NEVERC_STATUS_ABI_MISMATCH)
                                  : neverc_status_ok();
}

Expected<std::string> copyString(NevercStringView View) {
  if ((!View.Data && View.Length != 0) ||
      View.Length > std::numeric_limits<size_t>::max())
    return createStringError(inconvertibleErrorCode(),
                             "parser string view is invalid");
  StringRef Text(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  if (Text.empty() || Text.contains('\0') || !json::isUTF8(Text))
    return createStringError(inconvertibleErrorCode(),
                             "parser string is not valid UTF-8");
  return Text.str();
}

NevercInterfaceID declarationExtensionPhaseID() {
  return {NEVERC_PHASE_SYNTAX_EXTENSION_DECLARATION_HIGH,
          NEVERC_PHASE_SYNTAX_EXTENSION_DECLARATION_LOW};
}

NevercInterfaceID statementExtensionPhaseID() {
  return {NEVERC_PHASE_SYNTAX_EXTENSION_STATEMENT_HIGH,
          NEVERC_PHASE_SYNTAX_EXTENSION_STATEMENT_LOW};
}

NevercInterfaceID expressionExtensionPhaseID() {
  return {NEVERC_PHASE_SYNTAX_EXTENSION_EXPRESSION_HIGH,
          NEVERC_PHASE_SYNTAX_EXTENSION_EXPRESSION_LOW};
}

NevercInterfaceID typeNameExtensionPhaseID() {
  return {NEVERC_PHASE_SYNTAX_EXTENSION_TYPE_NAME_HIGH,
          NEVERC_PHASE_SYNTAX_EXTENSION_TYPE_NAME_LOW};
}

NevercInterfaceID attributeExtensionPhaseID() {
  return {NEVERC_PHASE_SYNTAX_EXTENSION_ATTRIBUTE_HIGH,
          NEVERC_PHASE_SYNTAX_EXTENSION_ATTRIBUTE_LOW};
}

NevercInterfaceID keywordExtensionPhaseID() {
  return {NEVERC_PHASE_SYNTAX_EXTENSION_KEYWORD_HIGH,
          NEVERC_PHASE_SYNTAX_EXTENSION_KEYWORD_LOW};
}

StringRef parserExtensionPhaseName(NevercInterfaceID Phase) {
  if (samePluginInterfaceID(Phase, declarationExtensionPhaseID()))
    return NEVERC_PHASE_SYNTAX_EXTENSION_DECLARATION_NAME;
  if (samePluginInterfaceID(Phase, statementExtensionPhaseID()))
    return NEVERC_PHASE_SYNTAX_EXTENSION_STATEMENT_NAME;
  if (samePluginInterfaceID(Phase, expressionExtensionPhaseID()))
    return NEVERC_PHASE_SYNTAX_EXTENSION_EXPRESSION_NAME;
  if (samePluginInterfaceID(Phase, typeNameExtensionPhaseID()))
    return NEVERC_PHASE_SYNTAX_EXTENSION_TYPE_NAME_NAME;
  if (samePluginInterfaceID(Phase, attributeExtensionPhaseID()))
    return NEVERC_PHASE_SYNTAX_EXTENSION_ATTRIBUTE_NAME;
  if (samePluginInterfaceID(Phase, keywordExtensionPhaseID()))
    return NEVERC_PHASE_SYNTAX_EXTENSION_KEYWORD_NAME;
  return "neverc.syntax.extension.unknown";
}

struct ParserExtensionArtifact {
  NevercParserTokenCursorHandle Cursor{};
  NevercParserResultKind ExpectedResult = 0;
  NevercParserExtensionDisposition Disposition =
      NEVERC_PARSER_EXTENSION_UNHANDLED;
  NevercParserResultKind ResultKind = 0;
  NevercASTNodeHandle Node{};
};

bool validResultKind(NevercParserResultKind Kind) {
  return Kind >= NEVERC_PARSER_RESULT_DECL &&
         Kind <= NEVERC_PARSER_RESULT_ATTRIBUTE;
}

Expected<void *> cloneParserExtension(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "parser extension payload is null");
  return static_cast<void *>(new ParserExtensionArtifact(
      *static_cast<const ParserExtensionArtifact *>(Payload)));
}

Error verifyParserExtension(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "parser extension payload is null");
  const auto &Extension =
      *static_cast<const ParserExtensionArtifact *>(Payload);
  if (neverc_handle_is_null(Extension.Cursor) ||
      !validResultKind(Extension.ExpectedResult))
    return createStringError(inconvertibleErrorCode(),
                             "parser extension input is invalid");
  if (Extension.Disposition == NEVERC_PARSER_EXTENSION_UNHANDLED) {
    if (!neverc_handle_is_null(Extension.Node) || Extension.ResultKind != 0)
      return createStringError(inconvertibleErrorCode(),
                               "unhandled parser extension has a result");
    return Error::success();
  }
  if (Extension.Disposition != NEVERC_PARSER_EXTENSION_HANDLED ||
      Extension.ResultKind != Extension.ExpectedResult ||
      neverc_handle_is_null(Extension.Node))
    return createStringError(inconvertibleErrorCode(),
                             "handled parser extension result is invalid");
  return Error::success();
}

class ParserAPITaskBridge {
public:
  virtual ~ParserAPITaskBridge() = default;
  virtual const NevercParserAPI &parserAPI() const = 0;
};

class ParserPluginProcessBridge final : public PluginHostService {
public:
  ParserPluginProcessBridge() {
    API.Header = {sizeof(API), NEVERC_PARSER_API_MAJOR, NEVERC_PARSER_API_MINOR,
                  0};
    API.Context = this;
    API.GetExtensionInput = GetExtensionInput;
    API.CursorPeek = CursorPeek;
    API.CursorConsume = CursorConsume;
    API.CursorCheckpoint = CursorCheckpoint;
    API.CursorCommit = CursorCommit;
    API.CursorRollback = CursorRollback;
    API.CreateExtensionOutput = CreateExtensionOutput;
    API.CreateParsedAttribute = CreateParsedAttribute;
    API.GetParsePhaseInput = GetParsePhaseInput;
    API.CreateASTUnit = CreateASTUnit;
    API.GetASTUnitInfo = GetASTUnitInfo;
  }

  const NevercParserAPI &api() const { return API; }

  Error attach(PluginTaskContext &Task, ParserAPITaskBridge &Bridge) {
    const auto Key = std::make_pair(Task.handle().Owner, Task.handle().Value);
    std::lock_guard<std::mutex> Lock(Mutex);
    auto [It, Inserted] = Tasks.try_emplace(Key, &Bridge);
    if (!Inserted && It->second != &Bridge)
      return createStringError(inconvertibleErrorCode(),
                               "plugin parser task is already attached");
    return Error::success();
  }

  void detach(NevercTaskHandle Task) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Tasks.erase(std::make_pair(Task.Owner, Task.Value));
  }

  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override {
    detach(Task);
  }

private:
  template <typename CallbackT>
  NevercStatus forward(NevercTaskHandle Task, CallbackT &&Callback) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Tasks.find(std::make_pair(Task.Owner, Task.Value));
    if (It == Tasks.end() || !It->second)
      return parserStatus(NEVERC_STATUS_STALE_HANDLE);
    return Callback(It->second->parserAPI());
  }

  static ParserPluginProcessBridge *bridge(void *Context) {
    return static_cast<ParserPluginProcessBridge *>(Context);
  }

  static NevercStatus NEVERC_CALL GetExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercParserExtensionInput *OutInput) {
    if (!Context || !Frame)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercParserAPI &Local) {
          return Local.GetExtensionInput(Local.Context, Frame, Input, OutInput);
        });
  }

#define NEVERC_FORWARD_PARSER(Name, Signature, ...)                            \
  static NevercStatus NEVERC_CALL Name Signature {                             \
    if (!Context)                                                              \
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);                     \
    return bridge(Context)->forward(Task, [&](const NevercParserAPI &Local) {  \
      return Local.Name(Local.Context, __VA_ARGS__);                           \
    });                                                                        \
  }

  NEVERC_FORWARD_PARSER(CursorPeek,
                        (void *Context, NevercTaskHandle Task,
                         NevercParserTokenCursorHandle Cursor, uint64_t Offset,
                         NevercTokenHandle *OutToken),
                        Task, Cursor, Offset, OutToken)
  NEVERC_FORWARD_PARSER(CursorConsume,
                        (void *Context, NevercTaskHandle Task,
                         NevercParserTokenCursorHandle Cursor,
                         NevercTokenHandle *OutToken),
                        Task, Cursor, OutToken)
  NEVERC_FORWARD_PARSER(CursorCheckpoint,
                        (void *Context, NevercTaskHandle Task,
                         NevercParserTokenCursorHandle Cursor,
                         NevercParserCheckpointHandle *OutCheckpoint),
                        Task, Cursor, OutCheckpoint)
  NEVERC_FORWARD_PARSER(CursorCommit,
                        (void *Context, NevercTaskHandle Task,
                         NevercParserTokenCursorHandle Cursor,
                         NevercParserCheckpointHandle Checkpoint),
                        Task, Cursor, Checkpoint)
  NEVERC_FORWARD_PARSER(CursorRollback,
                        (void *Context, NevercTaskHandle Task,
                         NevercParserTokenCursorHandle Cursor,
                         NevercParserCheckpointHandle Checkpoint),
                        Task, Cursor, Checkpoint)

#undef NEVERC_FORWARD_PARSER

  static NevercStatus NEVERC_CALL
  CreateExtensionOutput(void *Context, const NevercPhaseFrame *Frame,
                        NevercPhaseContinuation *Continuation,
                        const NevercParserExtensionOutput *Descriptor,
                        NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercParserAPI &Local) {
          return Local.CreateExtensionOutput(Local.Context, Frame, Continuation,
                                             Descriptor, OutOutput);
        });
  }

  static NevercStatus NEVERC_CALL
  CreateParsedAttribute(void *Context, NevercTaskHandle Task,
                        NevercParserTokenCursorHandle Cursor,
                        const NevercParserParsedAttributeDescriptor *Descriptor,
                        NevercAttrHandle *OutAttribute) {
    if (!Context)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(Task, [&](const NevercParserAPI &Local) {
      return Local.CreateParsedAttribute(Local.Context, Task, Cursor,
                                         Descriptor, OutAttribute);
    });
  }

  static NevercStatus NEVERC_CALL GetParsePhaseInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercParserPhaseInput *OutInput) {
    if (!Context || !Frame)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercParserAPI &Local) {
          return Local.GetParsePhaseInput(Local.Context, Frame, Input,
                                          OutInput);
        });
  }

  static NevercStatus NEVERC_CALL CreateASTUnit(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercParserASTUnitDescriptor *Descriptor,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercParserAPI &Local) {
          return Local.CreateASTUnit(Local.Context, Frame, Descriptor,
                                     OutOutput);
        });
  }

  static NevercStatus NEVERC_CALL GetASTUnitInfo(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Unit,
      NevercParserASTUnitInfo *OutInfo) {
    if (!Context || !Frame)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercParserAPI &Local) {
          return Local.GetASTUnitInfo(Local.Context, Frame, Unit, OutInfo);
        });
  }

  NevercParserAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>, ParserAPITaskBridge *> Tasks;
};

std::shared_ptr<ParserPluginProcessBridge>
findParserProcessBridge(PluginProcessServices &Services) {
  return std::static_pointer_cast<ParserPluginProcessBridge>(
      Services.findHostService(parserPluginInterfaceID()));
}

class ParserPluginHooksBridge final : public ParserPluginHooks,
                                      public ParserAPITaskBridge {
  struct Invocation;

  struct CursorPayload {
    Invocation *Owner = nullptr;
  };

  struct CheckpointPayload {
    Invocation *Owner = nullptr;
    CursorState State;
  };

  struct ParsedAttributePayload {
    Invocation *Owner = nullptr;
    ParsedAttr *Attribute = nullptr;
  };

  struct Invocation {
    ParserPluginHooksBridge &Bridge;
    Parser &ParserRef;
    NevercInterfaceID Phase{};
    NevercParserResultKind ExpectedResult = 0;
    CursorState OuterState;
    ParsedAttributes ScratchAttributes;
    NevercParserTokenCursorHandle Cursor{};
    NevercArtifactHandle InputHandle{};
    std::vector<NevercParserCheckpointHandle> Checkpoints;
    std::vector<NevercAttrHandle> ParsedAttributeHandles;
    bool Active = true;

    Invocation(ParserPluginHooksBridge &BridgeValue, Parser &ParserValue,
               NevercInterfaceID PhaseValue,
               NevercParserResultKind ExpectedResultValue)
        : Bridge(BridgeValue), ParserRef(ParserValue), Phase(PhaseValue),
          ExpectedResult(ExpectedResultValue),
          OuterState(saveCursor(ParserValue)),
          ScratchAttributes(ParserValue.getAttrFactory()) {}

    bool begin() {
      auto *Payload = new (std::nothrow) CursorPayload{this};
      if (!Payload)
        return false;
      auto Handle = Bridge.Task.handles().create(
          PluginParserCursorHandleKind, Payload,
          [](void *Value) { delete static_cast<CursorPayload *>(Value); });
      if (!Handle) {
        delete Payload;
        consumeError(Handle.takeError());
        return false;
      }
      Cursor = *Handle;
      return true;
    }

    bool hasOpenCheckpoints() const { return !Checkpoints.empty(); }

    void closeOutstandingCheckpoints() {
      while (!Checkpoints.empty()) {
        NevercParserCheckpointHandle Handle = Checkpoints.back();
        void *Raw = nullptr;
        if (Bridge.Task.handles()
                .resolve(Handle, PluginParserCheckpointHandleKind, &Raw)
                .Code == NEVERC_STATUS_OK) {
          auto *Checkpoint = static_cast<CheckpointPayload *>(Raw);
          restoreCursor(ParserRef, Checkpoint->State);
        }
        Checkpoints.pop_back();
        (void)Bridge.Task.handles().release(Handle,
                                            PluginParserCheckpointHandleKind);
      }
    }

    void finish(bool Commit) {
      Active = false;
      closeOutstandingCheckpoints();
      if (Commit)
        commitCursor(ParserRef);
      else
        restoreCursor(ParserRef, OuterState);
      for (NevercAttrHandle Handle : ParsedAttributeHandles)
        (void)Bridge.Task.handles().release(Handle,
                                            PluginParsedAttributeHandleKind);
      ParsedAttributeHandles.clear();
      if (!neverc_handle_is_null(Cursor))
        (void)Bridge.Task.handles().release(Cursor,
                                            PluginParserCursorHandleKind);
      Cursor = {};
    }
  };

public:
  ParserPluginHooksBridge(
      PluginTaskContext &TaskValue, PluginArtifactRegistry &ArtifactsValue,
      PluginPhaseExecutor &ExecutorValue, PluginPrepBridge &PrepBridgeValue,
      PluginASTBridge &ASTBridgeValue, FrontendPluginBridge &LocationsValue,
      std::shared_ptr<ParserPluginProcessBridge> ProcessBridgeValue)
      : Task(TaskValue), Artifacts(ArtifactsValue), Executor(ExecutorValue),
        PrepBridge(PrepBridgeValue), ASTBridge(ASTBridgeValue),
        Locations(LocationsValue),
        ProcessBridge(std::move(ProcessBridgeValue)) {
    API.Header = {sizeof(API), NEVERC_PARSER_API_MAJOR, NEVERC_PARSER_API_MINOR,
                  0};
    API.Context = this;
    API.GetExtensionInput = getExtensionInput;
    API.CursorPeek = cursorPeek;
    API.CursorConsume = cursorConsume;
    API.CursorCheckpoint = cursorCheckpoint;
    API.CursorCommit = cursorCommit;
    API.CursorRollback = cursorRollback;
    API.CreateExtensionOutput = createExtensionOutput;
    API.CreateParsedAttribute = createParsedAttribute;
    API.GetParsePhaseInput = getParsePhaseInput;
    API.CreateASTUnit = createASTUnit;
    API.GetASTUnitInfo = getASTUnitInfo;
  }

  ~ParserPluginHooksBridge() override {
    if (ProcessBridge)
      ProcessBridge->detach(Task.handle());
  }

  const NevercParserAPI &parserAPI() const override { return API; }

  ParserPluginOutcome parseDeclaration(Parser &P, Decl *&Result) override {
    const void *Native = nullptr;
    ParserPluginOutcome Outcome =
        runExtension(P, declarationExtensionPhaseID(),
                     NEVERC_PARSER_RESULT_DECL, &Native, nullptr);
    Result = Outcome == ParserPluginOutcome::Handled
                 ? const_cast<Decl *>(static_cast<const Decl *>(Native))
                 : nullptr;
    return Outcome;
  }

  ParserPluginOutcome parseStatement(Parser &P, Stmt *&Result) override {
    const void *Native = nullptr;
    ParserPluginOutcome Outcome =
        runExtension(P, statementExtensionPhaseID(), NEVERC_PARSER_RESULT_STMT,
                     &Native, nullptr);
    Result = Outcome == ParserPluginOutcome::Handled
                 ? const_cast<Stmt *>(static_cast<const Stmt *>(Native))
                 : nullptr;
    return Outcome;
  }

  ParserPluginOutcome parseExpression(Parser &P, Expr *&Result) override {
    const void *Native = nullptr;
    ParserPluginOutcome Outcome =
        runExtension(P, expressionExtensionPhaseID(), NEVERC_PARSER_RESULT_EXPR,
                     &Native, nullptr);
    Result = Outcome == ParserPluginOutcome::Handled
                 ? const_cast<Expr *>(static_cast<const Expr *>(Native))
                 : nullptr;
    return Outcome;
  }

  ParserPluginOutcome parseTypeName(Parser &P, QualType &Result) override {
    const void *Native = nullptr;
    ParserPluginOutcome Outcome =
        runExtension(P, typeNameExtensionPhaseID(), NEVERC_PARSER_RESULT_TYPE,
                     &Native, nullptr);
    Result = Outcome == ParserPluginOutcome::Handled
                 ? QualType::getFromOpaquePtr(Native)
                 : QualType();
    return Outcome;
  }

  ParserPluginOutcome parseAttribute(Parser &P,
                                     ParsedAttributes &Result) override {
    return runExtension(P, attributeExtensionPhaseID(),
                        NEVERC_PARSER_RESULT_ATTRIBUTE, nullptr, &Result);
  }

  ParserPluginOutcome parseKeyword(Parser &P, Expr *&Result) override {
    const void *Native = nullptr;
    ParserPluginOutcome Outcome =
        runExtension(P, keywordExtensionPhaseID(), NEVERC_PARSER_RESULT_EXPR,
                     &Native, nullptr);
    Result = Outcome == ParserPluginOutcome::Handled
                 ? const_cast<Expr *>(static_cast<const Expr *>(Native))
                 : nullptr;
    return Outcome;
  }

private:
  void reportError(Parser &P, NevercInterfaceID Phase, Error E) {
    std::string Message = "parser extension phase '";
    Message += parserExtensionPhaseName(Phase);
    Message += "' failed: ";
    Message += toString(std::move(E)).str().str();
    P.getPrepEngine().getDiagnostics().Report(diag::err_drv_plugin_phase)
        << Message;
  }

  NevercStatus resolveCursor(NevercTaskHandle TaskHandle,
                             NevercParserTokenCursorHandle Handle,
                             Invocation **OutInvocation) {
    if (!OutInvocation)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutInvocation = nullptr;
    if (!sameHandle(TaskHandle, Task.handle()))
      return parserStatus(NEVERC_STATUS_WRONG_SCOPE);
    void *Raw = nullptr;
    NevercStatus Status =
        Task.handles().resolve(Handle, PluginParserCursorHandleKind, &Raw);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Payload = static_cast<CursorPayload *>(Raw);
    if (!Payload->Owner || !Payload->Owner->Active || Payload->Owner != Current)
      return parserStatus(NEVERC_STATUS_STALE_HANDLE);
    *OutInvocation = Payload->Owner;
    return neverc_status_ok();
  }

  NevercStatus closeCheckpoint(NevercTaskHandle TaskHandle,
                               NevercParserTokenCursorHandle Cursor,
                               NevercParserCheckpointHandle Handle,
                               bool Commit) {
    Invocation *CurrentInvocation = nullptr;
    NevercStatus Status = resolveCursor(TaskHandle, Cursor, &CurrentInvocation);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (CurrentInvocation->Checkpoints.empty() ||
        !sameHandle(CurrentInvocation->Checkpoints.back(), Handle))
      return parserStatus(NEVERC_STATUS_INVALID_STATE);
    void *Raw = nullptr;
    Status =
        Task.handles().resolve(Handle, PluginParserCheckpointHandleKind, &Raw);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Checkpoint = static_cast<CheckpointPayload *>(Raw);
    if (Checkpoint->Owner != CurrentInvocation)
      return parserStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Commit)
      commitCursor(CurrentInvocation->ParserRef);
    else
      restoreCursor(CurrentInvocation->ParserRef, Checkpoint->State);
    CurrentInvocation->Checkpoints.pop_back();
    return Task.handles().release(Handle, PluginParserCheckpointHandleKind);
  }

  ParserPluginOutcome runExtension(Parser &P, NevercInterfaceID Phase,
                                   NevercParserResultKind ExpectedResult,
                                   const void **OutNative,
                                   ParsedAttributes *OutAttributes) {
    if (OutNative)
      *OutNative = nullptr;
    if (Current) {
      reportError(P, Phase,
                  createStringError(inconvertibleErrorCode(),
                                    "recursive parser extension invocation"));
      return ParserPluginOutcome::Error;
    }

    Invocation Call(*this, P, Phase, ExpectedResult);
    if (!Call.begin()) {
      Call.finish(false);
      reportError(P, Phase,
                  createStringError(inconvertibleErrorCode(),
                                    "cannot allocate parser cursor"));
      return ParserPluginOutcome::Error;
    }
    Current = &Call;
    auto ResetCurrent = make_scope_exit([&] { Current = nullptr; });

    ParserExtensionArtifact Input;
    Input.Cursor = Call.Cursor;
    Input.ExpectedResult = ExpectedResult;
    auto InputHandle = Executor.createArtifactView(
        Task, parserExtensionArtifactID(), &Input, 1);
    if (!InputHandle) {
      Error E = InputHandle.takeError();
      Call.finish(false);
      reportError(P, Phase, std::move(E));
      return ParserPluginOutcome::Error;
    }
    Call.InputHandle = *InputHandle;
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(*InputHandle, PluginArtifactHandleKind);
    });

    PluginArtifactSlot Output(Artifacts.find(parserExtensionArtifactID()));
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    if (Error E = Executor.execute(Task.session(), Task, Phase, Route,
                                   *InputHandle, Output)) {
      Call.finish(false);
      reportError(P, Phase, std::move(E));
      return ParserPluginOutcome::Error;
    }
    if (Call.hasOpenCheckpoints()) {
      Call.finish(false);
      reportError(
          P, Phase,
          createStringError(inconvertibleErrorCode(),
                            "parser interceptor left a checkpoint open"));
      return ParserPluginOutcome::Error;
    }

    PluginArtifactSlot::Snapshot Snapshot = Output.snapshot();
    if (!Snapshot.Payload) {
      Call.finish(false);
      reportError(P, Phase,
                  createStringError(inconvertibleErrorCode(),
                                    "parser extension produced no output"));
      return ParserPluginOutcome::Error;
    }
    const auto &Result =
        *static_cast<const ParserExtensionArtifact *>(Snapshot.Payload);
    if (Result.Disposition == NEVERC_PARSER_EXTENSION_UNHANDLED) {
      Call.finish(false);
      return ParserPluginOutcome::NotHandled;
    }

    const void *Native = nullptr;
    NevercStatus Resolve = neverc_status_ok();
    if (ExpectedResult == NEVERC_PARSER_RESULT_ATTRIBUTE) {
      void *Raw = nullptr;
      Resolve = Task.handles().resolve(Result.Node,
                                       PluginParsedAttributeHandleKind, &Raw);
      if (Resolve.Code == NEVERC_STATUS_OK) {
        auto *Attribute = static_cast<ParsedAttributePayload *>(Raw);
        if (Attribute->Owner != &Call || !Attribute->Attribute)
          Resolve = parserStatus(NEVERC_STATUS_WRONG_SCOPE);
        else if (!OutAttributes)
          Resolve = parserStatus(NEVERC_STATUS_INVALID_STATE);
        else
          OutAttributes->takeOneFrom(Call.ScratchAttributes,
                                     Attribute->Attribute);
      }
    } else {
      NevercASTSchemaDomain Domain =
          ExpectedResult == NEVERC_PARSER_RESULT_DECL
              ? NEVERC_AST_SCHEMA_DOMAIN_DECL
          : ExpectedResult == NEVERC_PARSER_RESULT_TYPE
              ? NEVERC_AST_SCHEMA_DOMAIN_TYPE
              : NEVERC_AST_SCHEMA_DOMAIN_STMT;
      Resolve = ASTBridge.resolvePublishedNode(Task.handle(), Result.Node,
                                               Domain, &Native);
      if (Resolve.Code == NEVERC_STATUS_OK &&
          ExpectedResult == NEVERC_PARSER_RESULT_EXPR &&
          !isa<Expr>(static_cast<const Stmt *>(Native)))
        Resolve = parserStatus(NEVERC_STATUS_WRONG_TYPE);
    }
    if (Resolve.Code != NEVERC_STATUS_OK) {
      Call.finish(false);
      reportError(
          P, Phase,
          createStringError(inconvertibleErrorCode(),
                            "parser extension result failed verification"));
      return ParserPluginOutcome::Error;
    }

    if (OutNative)
      *OutNative = Native;
    Call.finish(true);
    return ParserPluginOutcome::Handled;
  }

  static ParserPluginHooksBridge *bridge(void *Context) {
    return static_cast<ParserPluginHooksBridge *>(Context);
  }

  static NevercStatus NEVERC_CALL getExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercParserExtensionInput *OutInput) {
    if (!Context || !Frame || !OutInput)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    ParserPluginHooksBridge &Bridge = *bridge(Context);
    Invocation *Call = Bridge.Current;
    if (!Call || !Call->Active ||
        !sameHandle(Frame->Task, Bridge.Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, Call->Phase) ||
        !sameHandle(Input, Call->InputHandle))
      return parserStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Bridge.Executor.resolveArtifactPayload(
        Bridge.Task, Input, parserExtensionArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto &Extension =
        *static_cast<const ParserExtensionArtifact *>(Payload);
    NevercParserExtensionInput Value{};
    Value.Header = {sizeof(Value), NEVERC_PARSER_API_MAJOR,
                    NEVERC_PARSER_API_MINOR, 0};
    Value.Cursor = Extension.Cursor;
    Value.ExpectedResult = Extension.ExpectedResult;
    return writeCallerRecord(OutInput, Value);
  }

  static NevercStatus NEVERC_CALL
  cursorPeek(void *Context, NevercTaskHandle TaskHandle,
             NevercParserTokenCursorHandle Cursor, uint64_t Offset,
             NevercTokenHandle *OutToken) {
    if (!Context || !OutToken || Offset > std::numeric_limits<unsigned>::max())
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutToken = {};
    ParserPluginHooksBridge &Bridge = *bridge(Context);
    Invocation *Call = nullptr;
    NevercStatus Status = Bridge.resolveCursor(TaskHandle, Cursor, &Call);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const Token &TokenValue =
        peekCursor(Call->ParserRef, static_cast<unsigned>(Offset));
    auto Handle = Bridge.PrepBridge.createToken(TokenValue);
    if (!Handle) {
      consumeError(Handle.takeError());
      return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutToken = *Handle;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL cursorConsume(
      void *Context, NevercTaskHandle TaskHandle,
      NevercParserTokenCursorHandle Cursor, NevercTokenHandle *OutToken) {
    if (!Context || !OutToken)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutToken = {};
    ParserPluginHooksBridge &Bridge = *bridge(Context);
    Invocation *Call = nullptr;
    NevercStatus Status = Bridge.resolveCursor(TaskHandle, Cursor, &Call);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const Token &CurrentToken = peekCursor(Call->ParserRef, 0);
    if (CurrentToken.is(tok::eof))
      return parserStatus(NEVERC_STATUS_INVALID_STATE);
    auto Handle = Bridge.PrepBridge.createToken(CurrentToken);
    if (!Handle) {
      consumeError(Handle.takeError());
      return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    consumeCursor(Call->ParserRef);
    *OutToken = *Handle;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL
  cursorCheckpoint(void *Context, NevercTaskHandle TaskHandle,
                   NevercParserTokenCursorHandle Cursor,
                   NevercParserCheckpointHandle *OutCheckpoint) {
    if (!Context || !OutCheckpoint)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutCheckpoint = {};
    ParserPluginHooksBridge &Bridge = *bridge(Context);
    Invocation *Call = nullptr;
    NevercStatus Status = Bridge.resolveCursor(TaskHandle, Cursor, &Call);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Payload =
        new (std::nothrow) CheckpointPayload{Call, saveCursor(Call->ParserRef)};
    if (!Payload)
      return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    auto Handle = Bridge.Task.handles().create(
        PluginParserCheckpointHandleKind, Payload,
        [](void *Value) { delete static_cast<CheckpointPayload *>(Value); });
    if (!Handle) {
      commitCursor(Call->ParserRef);
      delete Payload;
      consumeError(Handle.takeError());
      return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Call->Checkpoints.push_back(*Handle);
    *OutCheckpoint = *Handle;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL
  cursorCommit(void *Context, NevercTaskHandle TaskHandle,
               NevercParserTokenCursorHandle Cursor,
               NevercParserCheckpointHandle Checkpoint) {
    if (!Context)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->closeCheckpoint(TaskHandle, Cursor, Checkpoint,
                                            true);
  }

  static NevercStatus NEVERC_CALL
  cursorRollback(void *Context, NevercTaskHandle TaskHandle,
                 NevercParserTokenCursorHandle Cursor,
                 NevercParserCheckpointHandle Checkpoint) {
    if (!Context)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->closeCheckpoint(TaskHandle, Cursor, Checkpoint,
                                            false);
  }

  static NevercStatus NEVERC_CALL
  createExtensionOutput(void *Context, const NevercPhaseFrame *Frame,
                        NevercPhaseContinuation *Continuation,
                        const NevercParserExtensionOutput *Descriptor,
                        NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame || !Continuation || !Descriptor || !OutOutput ||
        !validParserHeader(Descriptor->Header, sizeof(*Descriptor)) ||
        Descriptor->Reserved[0] != 0 || Descriptor->Reserved[1] != 0)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutOutput = {};
    ParserPluginHooksBridge &Bridge = *bridge(Context);
    Invocation *Call = Bridge.Current;
    if (!Call || !Call->Active ||
        !sameHandle(Frame->Task, Bridge.Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, Call->Phase) ||
        !Bridge.Executor.isActiveContinuation(Frame, Continuation))
      return parserStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Descriptor->Disposition != NEVERC_PARSER_EXTENSION_UNHANDLED &&
        Descriptor->Disposition != NEVERC_PARSER_EXTENSION_HANDLED)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (Descriptor->Disposition == NEVERC_PARSER_EXTENSION_HANDLED &&
        (Descriptor->ResultKind != Call->ExpectedResult ||
         neverc_handle_is_null(Descriptor->Node)))
      return parserStatus(NEVERC_STATUS_WRONG_TYPE);
    if (Descriptor->Disposition == NEVERC_PARSER_EXTENSION_UNHANDLED &&
        (!neverc_handle_is_null(Descriptor->Node) ||
         Descriptor->ResultKind != 0))
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    auto *Candidate = new (std::nothrow) ParserExtensionArtifact;
    if (!Candidate)
      return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    Candidate->Cursor = Call->Cursor;
    Candidate->ExpectedResult = Call->ExpectedResult;
    Candidate->Disposition = Descriptor->Disposition;
    Candidate->ResultKind = Descriptor->ResultKind;
    Candidate->Node = Descriptor->Node;
    auto Handle = Bridge.Executor.createCandidate(
        Bridge.Task, parserExtensionArtifactID(), Candidate);
    if (!Handle) {
      delete Candidate;
      consumeError(Handle.takeError());
      return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutOutput = *Handle;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL
  createParsedAttribute(void *Context, NevercTaskHandle TaskHandle,
                        NevercParserTokenCursorHandle Cursor,
                        const NevercParserParsedAttributeDescriptor *Descriptor,
                        NevercAttrHandle *OutAttribute) {
    if (!Context || !Descriptor || !OutAttribute ||
        !validParserHeader(Descriptor->Header, sizeof(*Descriptor)) ||
        Descriptor->Reserved != 0)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutAttribute = {};
    ParserPluginHooksBridge &Bridge = *bridge(Context);
    Invocation *Call = nullptr;
    NevercStatus Status = Bridge.resolveCursor(TaskHandle, Cursor, &Call);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Call->ExpectedResult != NEVERC_PARSER_RESULT_ATTRIBUTE)
      return parserStatus(NEVERC_STATUS_WRONG_TYPE);
    auto Name = copyString(Descriptor->Name);
    if (!Name) {
      consumeError(Name.takeError());
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    CharSourceRange Range;
    Status = Bridge.Locations.resolvePublishedRange(TaskHandle,
                                                    Descriptor->Range, &Range);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Range.isInvalid())
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    ParsedAttr::Form Form = ParsedAttr::Form::C23();
    switch (Descriptor->Form) {
    case NEVERC_PARSER_ATTRIBUTE_GNU:
      Form = ParsedAttr::Form::GNU();
      break;
    case NEVERC_PARSER_ATTRIBUTE_C23:
      Form = ParsedAttr::Form::C23();
      break;
    case NEVERC_PARSER_ATTRIBUTE_DECLSPEC:
      Form = ParsedAttr::Form::Declspec();
      break;
    default:
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }

    IdentifierInfo *Identifier =
        Call->ParserRef.getPrepEngine().getIdentifierInfo(*Name);
    ParsedAttr *Attribute =
        Call->ScratchAttributes.addNew(Identifier, Range.getAsRange(), nullptr,
                                       SourceLocation(), nullptr, 0, Form);
    auto *Payload = new (std::nothrow) ParsedAttributePayload{Call, Attribute};
    if (!Payload)
      return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    auto Handle = Bridge.Task.handles().create(
        PluginParsedAttributeHandleKind, Payload, [](void *Value) {
          delete static_cast<ParsedAttributePayload *>(Value);
        });
    if (!Handle) {
      delete Payload;
      consumeError(Handle.takeError());
      return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Call->ParsedAttributeHandles.push_back(*Handle);
    *OutAttribute = *Handle;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getParsePhaseInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercParserPhaseInput *OutInput) {
    if (!Context || !Frame || !OutInput ||
        !samePluginInterfaceID(Frame->Phase, syntaxParsePhaseID()))
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    ParserPluginHooksBridge &Bridge = *bridge(Context);
    if (!sameHandle(Frame->Task, Bridge.Task.handle()) ||
        !sameHandle(Frame->Input, Input))
      return parserStatus(NEVERC_STATUS_WRONG_SCOPE);

    const void *Payload = nullptr;
    NevercStatus Status = Bridge.Executor.resolveArtifactPayload(
        Bridge.Task, Input, prepTokenStreamArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto &Stream =
        *static_cast<const prep_bridge_detail::PrepTokenStreamArtifact *>(
            Payload);
    auto TokenStream = Bridge.PrepBridge.createTokenStream(Stream.Tokens);
    if (!TokenStream) {
      consumeError(TokenStream.takeError());
      return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }

    NevercParserPhaseInput Result{};
    Result.Header = {sizeof(Result), NEVERC_PARSER_API_MAJOR,
                     NEVERC_PARSER_API_MINOR, 0};
    Result.TokenStream = *TokenStream;
    Status = Bridge.ASTBridge.astAPI().GetTranslationUnit(
        Bridge.ASTBridge.astAPI().Context, Bridge.Task.handle(),
        &Result.TranslationUnit);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return writeCallerRecord(OutInput, Result);
  }

  static NevercStatus NEVERC_CALL createASTUnit(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercParserASTUnitDescriptor *Descriptor,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame || !Descriptor || !OutOutput ||
        !validParserHeader(Descriptor->Header, sizeof(*Descriptor)) ||
        Descriptor->Reserved[0] != 0 || Descriptor->Reserved[1] != 0)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutOutput = {};
    ParserPluginHooksBridge &Bridge = *bridge(Context);
    if (!sameHandle(Frame->Task, Bridge.Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, syntaxParsePhaseID()))
      return parserStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (!samePluginInterfaceID(Descriptor->Product, standardASTProductID()))
      return parserStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    if (neverc_handle_is_null(Descriptor->TranslationUnit))
      return parserStatus(NEVERC_STATUS_VERIFICATION_FAILED);

    const void *Native = nullptr;
    NevercStatus Status = Bridge.ASTBridge.resolvePublishedNode(
        Bridge.Task.handle(), Descriptor->TranslationUnit,
        NEVERC_AST_SCHEMA_DOMAIN_DECL, &Native);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Root = dyn_cast_or_null<TranslationUnitDecl>(
        const_cast<Decl *>(static_cast<const Decl *>(Native)));
    if (!Root || Root != Root->getTreeContext().getTranslationUnitDecl())
      return parserStatus(NEVERC_STATUS_VERIFICATION_FAILED);

    const void *InputPayload = nullptr;
    Status = Bridge.Executor.resolveArtifactPayload(
        Bridge.Task, Frame->Input, prepTokenStreamArtifactID(), &InputPayload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto &Stream =
        *static_cast<const prep_bridge_detail::PrepTokenStreamArtifact *>(
            InputPayload);
    auto Source = llvm::find_if(
        Stream.Dependencies, [](const PluginDependencySnapshot &Dependency) {
          return Dependency.Kind == NEVERC_INPUT_DEPENDENCY_SOURCE &&
                 !Dependency.CanonicalPath.empty();
        });
    if (Source == Stream.Dependencies.end())
      return parserStatus(NEVERC_STATUS_VERIFICATION_FAILED);

    auto *Candidate = new (std::nothrow) ASTUnitArtifact;
    if (!Candidate)
      return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    Candidate->Context = &Root->getTreeContext();
    Candidate->TranslationUnit = Root;
    Candidate->Product = Descriptor->Product;
    Candidate->SemanticState = NEVERC_AST_UNIT_UNANALYZED;
    Candidate->SourceIdentity = Source->CanonicalPath;
    Candidate->SourceDigest = Source->ContentDigest;
    Candidate->HasSourceDigest = true;
    auto Handle = Bridge.Executor.createCandidate(
        Bridge.Task, astUnitArtifactID(), Candidate);
    if (!Handle) {
      delete Candidate;
      consumeError(Handle.takeError());
      return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutOutput = *Handle;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getASTUnitInfo(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Unit,
      NevercParserASTUnitInfo *OutInfo) {
    if (!Context || !Frame || !OutInfo)
      return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    ParserPluginHooksBridge &Bridge = *bridge(Context);
    if (!sameHandle(Frame->Task, Bridge.Task.handle()))
      return parserStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Bridge.Executor.resolveArtifactPayload(
        Bridge.Task, Unit, astUnitArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto &AST = *static_cast<const ASTUnitArtifact *>(Payload);

    NevercParserASTUnitInfo Result{};
    Result.Header = {sizeof(Result), NEVERC_PARSER_API_MAJOR,
                     NEVERC_PARSER_API_MINOR, 0};
    Result.Product = AST.Product;
    Result.SemanticState = AST.SemanticState;
    Result.SourceIdentity = {AST.SourceIdentity.data(),
                             static_cast<uint64_t>(AST.SourceIdentity.size())};
    Result.SourceDigest = {AST.SourceDigest.data(), AST.SourceDigest.size()};
    Status = Bridge.ASTBridge.astAPI().GetTranslationUnit(
        Bridge.ASTBridge.astAPI().Context, Bridge.Task.handle(),
        &Result.TranslationUnit);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    return writeCallerRecord(OutInfo, Result);
  }

  PluginTaskContext &Task;
  PluginArtifactRegistry &Artifacts;
  PluginPhaseExecutor &Executor;
  PluginPrepBridge &PrepBridge;
  PluginASTBridge &ASTBridge;
  FrontendPluginBridge &Locations;
  std::shared_ptr<ParserPluginProcessBridge> ProcessBridge;
  NevercParserAPI API{};
  Invocation *Current = nullptr;
};

NevercStatus parserExtensionBuiltin(PluginTaskContext &Task,
                                    PluginPhaseExecutor &Executor,
                                    const NevercPhaseFrame *Frame,
                                    NevercPhaseResult *Result) {
  if (!Frame || !Result)
    return parserStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Status = Executor.resolveArtifactPayload(
      Task, Frame->Input, parserExtensionArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Candidate = new (std::nothrow) ParserExtensionArtifact(
      *static_cast<const ParserExtensionArtifact *>(Payload));
  if (!Candidate)
    return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Candidate->Disposition = NEVERC_PARSER_EXTENSION_UNHANDLED;
  Candidate->ResultKind = 0;
  Candidate->Node = {};
  auto Handle =
      Executor.createCandidate(Task, parserExtensionArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return parserStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = *Handle;
  return neverc_status_ok();
}

} // namespace

NevercInterfaceID parserExtensionArtifactID() {
  return {NEVERC_PHASE_SYNTAX_EXTENSION_DECLARATION_INPUT_HIGH,
          NEVERC_PHASE_SYNTAX_EXTENSION_DECLARATION_INPUT_LOW};
}

Error registerParserExtensionArtifactType(PluginArtifactRegistry &Artifacts) {
  auto Type = Artifacts.registerType(
      {parserExtensionArtifactID(), "syntax.parser_extension",
       PluginArtifactOwnership::Owned, cloneParserExtension,
       [](void *Payload) {
         delete static_cast<ParserExtensionArtifact *>(Payload);
       },
       verifyParserExtension});
  if (!Type)
    return Type.takeError();
  return Error::success();
}

Error registerParserBuiltinProviders(PluginTaskContext &Task,
                                     PluginPhaseExecutor &Executor) {
  for (NevercInterfaceID Phase :
       {declarationExtensionPhaseID(), statementExtensionPhaseID(),
        expressionExtensionPhaseID(), typeNameExtensionPhaseID(),
        attributeExtensionPhaseID(), keywordExtensionPhaseID()})
    if (Error E = Executor.setBuiltinProvider(
            Phase, [&Task, &Executor](const NevercPhaseFrame *Frame,
                                      NevercPhaseResult *Result) {
              return parserExtensionBuiltin(Task, Executor, Frame, Result);
            }))
      return E;
  return Error::success();
}

bool hasParserExtensionBindings(const PluginPhaseExecutor &Executor) {
  for (NevercInterfaceID Phase :
       {declarationExtensionPhaseID(), statementExtensionPhaseID(),
        expressionExtensionPhaseID(), typeNameExtensionPhaseID(),
        attributeExtensionPhaseID(), keywordExtensionPhaseID()})
    if (Executor.hasBindings(Phase))
      return true;
  return false;
}

Expected<std::unique_ptr<ParserPluginHooks>> createParserPluginHooks(
    PluginTaskContext &Task, PluginArtifactRegistry &Artifacts,
    PluginPhaseExecutor &Executor, PluginPrepBridge &PrepBridge,
    PluginASTBridge &ASTBridge, FrontendPluginBridge &Locations) {
  auto ProcessBridge = findParserProcessBridge(Task.processServices());
  if (!ProcessBridge)
    return createStringError(inconvertibleErrorCode(),
                             "plugin parser interface is not registered");
  auto Hooks = std::make_unique<ParserPluginHooksBridge>(
      Task, Artifacts, Executor, PrepBridge, ASTBridge, Locations,
      ProcessBridge);
  if (Error E = ProcessBridge->attach(Task, *Hooks))
    return std::move(E);
  return std::unique_ptr<ParserPluginHooks>(std::move(Hooks));
}

Error registerPluginParserInterface(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register plugin parser interface after interface freeze");
  auto Bridge = std::make_shared<ParserPluginProcessBridge>();
  if (Error E = Services.registerHostService(parserPluginInterfaceID(), Bridge))
    return E;
  return Services.interfaces().registerInterface(
      parserPluginInterfaceID(), NEVERC_INTERFACE_STABLE, &Bridge->api(), {});
}

} // namespace neverc::plugin
