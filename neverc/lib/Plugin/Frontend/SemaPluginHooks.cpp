#include "neverc/Analyze/SemaPluginHooks.h"
#include "FrontendPluginInterfaces.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "llvm/ADT/ScopeExit.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus extensionStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool validHeader(const NevercABITableHeader &Header, size_t RequiredSize) {
  return Header.StructSize >= RequiredSize &&
         Header.Major == NEVERC_SEMA_API_MAJOR &&
         Header.Minor <= NEVERC_SEMA_API_MINOR && Header.Flags == 0;
}

template <typename T>
NevercStatus writeCallerRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return extensionStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value)
             ? extensionStatus(NEVERC_STATUS_ABI_MISMATCH)
             : neverc_status_ok();
}

NevercInterfaceID expressionExtensionPhaseID() {
  return {NEVERC_PHASE_SEMA_EXTENSION_EXPRESSION_HIGH,
          NEVERC_PHASE_SEMA_EXTENSION_EXPRESSION_LOW};
}

NevercInterfaceID statementExtensionPhaseID() {
  return {NEVERC_PHASE_SEMA_EXTENSION_STATEMENT_HIGH,
          NEVERC_PHASE_SEMA_EXTENSION_STATEMENT_LOW};
}

NevercInterfaceID declarationExtensionPhaseID() {
  return {NEVERC_PHASE_SEMA_EXTENSION_DECLARATION_HIGH,
          NEVERC_PHASE_SEMA_EXTENSION_DECLARATION_LOW};
}

NevercInterfaceID typeExtensionPhaseID() {
  return {NEVERC_PHASE_SEMA_EXTENSION_TYPE_HIGH,
          NEVERC_PHASE_SEMA_EXTENSION_TYPE_LOW};
}

NevercInterfaceID lookupExtensionPhaseID() {
  return {NEVERC_PHASE_SEMA_EXTENSION_LOOKUP_HIGH,
          NEVERC_PHASE_SEMA_EXTENSION_LOOKUP_LOW};
}

NevercInterfaceID conversionExtensionPhaseID() {
  return {NEVERC_PHASE_SEMA_EXTENSION_CONVERSION_HIGH,
          NEVERC_PHASE_SEMA_EXTENSION_CONVERSION_LOW};
}

bool mapConversionContext(unsigned Context,
                          NevercSemaConversionContext &OutContext) {
  switch (Context) {
  case Sema::AA_Assigning:
    OutContext = NEVERC_SEMA_CONVERSION_ASSIGNMENT;
    return true;
  case Sema::AA_Passing:
  case Sema::AA_Sending:
    OutContext = NEVERC_SEMA_CONVERSION_ARGUMENT;
    return true;
  case Sema::AA_Returning:
    OutContext = NEVERC_SEMA_CONVERSION_RETURN;
    return true;
  case Sema::AA_Initializing:
    OutContext = NEVERC_SEMA_CONVERSION_INITIALIZATION;
    return true;
  case Sema::AA_Converting:
  case Sema::AA_Casting:
    OutContext = NEVERC_SEMA_CONVERSION_EXPLICIT_CAST;
    return true;
  }
  return false;
}

bool validConversionContext(NevercSemaConversionContext Context) {
  return Context >= NEVERC_SEMA_CONVERSION_ASSIGNMENT &&
         Context <= NEVERC_SEMA_CONVERSION_EXPLICIT_CAST;
}

enum class SemaExtensionKind {
  Expression,
  Statement,
  Declaration,
  Type,
  Lookup,
  Conversion,
};

struct SemaExtensionArtifact {
  SemaExtensionKind Kind = SemaExtensionKind::Expression;
  NevercExprHandle Left{};
  NevercExprHandle Right{};
  NevercSourceLocation OperatorLocation{};
  std::vector<NevercStmtHandle> Statements;
  NevercSourceLocation LeftBrace{};
  NevercSourceLocation RightBrace{};
  NevercDeclHandle Declaration{};
  NevercSourceLocation NameLocation{};
  std::string TypeName;
  NevercSourceLocation TypeNameLocation{};
  std::string LookupName;
  NevercSourceLocation LookupNameLocation{};
  NevercSemaLookupKind LookupKind = NEVERC_SEMA_LOOKUP_ORDINARY;
  std::vector<NevercDeclHandle> LookupCandidates;
  NevercExprHandle ConversionExpression{};
  NevercTypeHandle ConversionSourceType{};
  NevercTypeHandle ConversionDestinationType{};
  NevercSemaConversionContext ConversionContext =
      NEVERC_SEMA_CONVERSION_ASSIGNMENT;
  NevercSemaExtensionDisposition Disposition =
      NEVERC_SEMA_EXTENSION_UNHANDLED;
  NevercExprHandle Expression{};
  NevercStmtHandle Statement{};
  NevercDeclHandle ReplacementDeclaration{};
  NevercTypeHandle Type{};
};

Expected<void *> cloneSemaExtension(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "Sema extension payload is null");
  return static_cast<void *>(new SemaExtensionArtifact(
      *static_cast<const SemaExtensionArtifact *>(Payload)));
}

Error verifySemaExtension(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "Sema extension payload is null");
  const auto &Extension =
      *static_cast<const SemaExtensionArtifact *>(Payload);
  if (Extension.Kind == SemaExtensionKind::Expression &&
      (neverc_handle_is_null(Extension.Left) ||
       neverc_handle_is_null(Extension.Right)))
    return createStringError(inconvertibleErrorCode(),
                             "Sema expression input is incomplete");
  if (Extension.Kind == SemaExtensionKind::Declaration &&
      neverc_handle_is_null(Extension.Declaration))
    return createStringError(inconvertibleErrorCode(),
                             "Sema declaration input is incomplete");
  if (Extension.Kind == SemaExtensionKind::Type && Extension.TypeName.empty())
    return createStringError(inconvertibleErrorCode(),
                             "Sema type input is incomplete");
  if (Extension.Kind == SemaExtensionKind::Conversion &&
      (neverc_handle_is_null(Extension.ConversionExpression) ||
       neverc_handle_is_null(Extension.ConversionSourceType) ||
       neverc_handle_is_null(Extension.ConversionDestinationType) ||
       !validConversionContext(Extension.ConversionContext)))
    return createStringError(inconvertibleErrorCode(),
                             "Sema conversion input is incomplete");
  if (Extension.Kind == SemaExtensionKind::Lookup) {
    if (Extension.LookupName.empty())
      return createStringError(inconvertibleErrorCode(),
                               "Sema lookup input is incomplete");
    if (Extension.Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED) {
      if (!Extension.LookupCandidates.empty())
        return createStringError(inconvertibleErrorCode(),
                                 "unhandled Sema lookup has candidates");
      return Error::success();
    }
    if (Extension.Disposition != NEVERC_SEMA_EXTENSION_HANDLED ||
        Extension.LookupCandidates.empty())
      return createStringError(inconvertibleErrorCode(),
                               "handled Sema lookup has no candidates");
    return Error::success();
  }
  NevercHandle Result{};
  switch (Extension.Kind) {
  case SemaExtensionKind::Expression:
    Result = Extension.Expression;
    break;
  case SemaExtensionKind::Statement:
    Result = Extension.Statement;
    break;
  case SemaExtensionKind::Declaration:
    Result = Extension.ReplacementDeclaration;
    break;
  case SemaExtensionKind::Type:
    Result = Extension.Type;
    break;
  case SemaExtensionKind::Lookup:
    llvm_unreachable("lookup extension handled above");
  case SemaExtensionKind::Conversion:
    Result = Extension.Expression;
    break;
  }
  if (Extension.Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED) {
    if (!neverc_handle_is_null(Result))
      return createStringError(inconvertibleErrorCode(),
                               "unhandled Sema extension has a result");
    return Error::success();
  }
  if (Extension.Disposition != NEVERC_SEMA_EXTENSION_HANDLED ||
      neverc_handle_is_null(Result))
    return createStringError(inconvertibleErrorCode(),
                             "handled Sema extension has no result");
  return Error::success();
}

class SemaPluginHooksBridge final : public SemaPluginHooks,
                                    public PluginSemaExtensionAPI {
public:
  SemaPluginHooksBridge(PluginTaskContext &TaskValue,
                        PluginArtifactRegistry &ArtifactsValue,
                        PluginPhaseExecutor &ExecutorValue,
                        PluginASTBridge &ASTBridgeValue,
                        FrontendPluginBridge &LocationsValue,
                        PluginSemaBridge &SemaBridgeValue)
      : Task(TaskValue), Artifacts(ArtifactsValue), Executor(ExecutorValue),
        ASTBridge(ASTBridgeValue), Locations(LocationsValue),
        SemaBridge(SemaBridgeValue) {
    SemaBridge.setExtensionAPI(this);
  }

  ~SemaPluginHooksBridge() override { SemaBridge.setExtensionAPI(nullptr); }

  SemaPluginOutcome analyzeBinaryExpression(
      Sema &SemanticAnalyzer, SourceLocation OperatorLocation,
      tok::TokenKind Operator, Expr *Left, Expr *Right,
      Expr *&Replacement) override {
    (void)Operator;
    Replacement = nullptr;
    if (Current) {
      reportError(SemanticAnalyzer, "recursive Sema extension invocation");
      return SemaPluginOutcome::Error;
    }

    auto LeftHandle = ASTBridge.publishExpr(Left);
    auto RightHandle = ASTBridge.publishExpr(Right);
    auto LocationHandle = Locations.createLocation(OperatorLocation);
    if (!LeftHandle || !RightHandle || !LocationHandle) {
      if (!LeftHandle)
        consumeError(LeftHandle.takeError());
      if (!RightHandle)
        consumeError(RightHandle.takeError());
      if (!LocationHandle)
        consumeError(LocationHandle.takeError());
      reportError(SemanticAnalyzer,
                  "cannot publish Sema expression extension input");
      return SemaPluginOutcome::Error;
    }

    Invocation Call;
    Call.Left = *LeftHandle;
    Call.Right = *RightHandle;
    Call.OperatorLocation = *LocationHandle;
    Current = &Call;
    auto ResetCurrent = make_scope_exit([&] { Current = nullptr; });

    SemaExtensionArtifact Input;
    Input.Left = Call.Left;
    Input.Right = Call.Right;
    Input.OperatorLocation = Call.OperatorLocation;
    auto InputHandle = Executor.createArtifactView(
        Task, semaExtensionArtifactID(), &Input, 1);
    if (!InputHandle) {
      reportError(SemanticAnalyzer,
                  toString(InputHandle.takeError()).str().str());
      return SemaPluginOutcome::Error;
    }
    Call.InputHandle = *InputHandle;
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(*InputHandle, PluginArtifactHandleKind);
    });

    PluginArtifactSlot Output(Artifacts.find(semaExtensionArtifactID()));
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    if (Error E = Executor.execute(Task.session(), Task,
                                   expressionExtensionPhaseID(), Route,
                                   *InputHandle, Output)) {
      reportError(SemanticAnalyzer, toString(std::move(E)).str().str());
      return SemaPluginOutcome::Error;
    }

    PluginArtifactSlot::Snapshot Snapshot = Output.snapshot();
    if (!Snapshot.Payload) {
      reportError(SemanticAnalyzer, "Sema extension produced no output");
      return SemaPluginOutcome::Error;
    }
    const auto &Result =
        *static_cast<const SemaExtensionArtifact *>(Snapshot.Payload);
    if (Result.Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED)
      return SemaPluginOutcome::NotHandled;

    const void *Native = nullptr;
    NevercStatus Status = ASTBridge.resolvePublishedNode(
        Task.handle(), Result.Expression, NEVERC_AST_SCHEMA_DOMAIN_STMT,
        &Native);
    const auto *Expression =
        Status.Code == NEVERC_STATUS_OK
            ? dyn_cast<Expr>(static_cast<const Stmt *>(Native))
            : nullptr;
    if (!Expression || Expression->getType().isNull()) {
      reportError(SemanticAnalyzer,
                  "Sema expression replacement failed verification");
      return SemaPluginOutcome::Error;
    }
    Replacement = const_cast<Expr *>(Expression);
    return SemaPluginOutcome::Handled;
  }

  SemaPluginOutcome analyzeCompoundStatement(
      Sema &SemanticAnalyzer, SourceLocation LeftBrace,
      SourceLocation RightBrace, ArrayRef<Stmt *> Statements,
      Stmt *&Replacement) override {
    Replacement = nullptr;
    if (Current) {
      reportError(SemanticAnalyzer, "recursive Sema extension invocation");
      return SemaPluginOutcome::Error;
    }

    Invocation Call;
    Call.Kind = SemaExtensionKind::Statement;
    Call.Statements.reserve(Statements.size());
    for (const Stmt *Statement : Statements) {
      auto Handle = ASTBridge.publishStmt(Statement);
      if (!Handle) {
        consumeError(Handle.takeError());
        reportError(SemanticAnalyzer,
                    "cannot publish Sema statement extension input");
        return SemaPluginOutcome::Error;
      }
      Call.Statements.push_back(*Handle);
    }
    auto LeftHandle = Locations.createLocation(LeftBrace);
    auto RightHandle = Locations.createLocation(RightBrace);
    if (!LeftHandle || !RightHandle) {
      if (!LeftHandle)
        consumeError(LeftHandle.takeError());
      if (!RightHandle)
        consumeError(RightHandle.takeError());
      reportError(SemanticAnalyzer,
                  "cannot publish Sema statement extension locations");
      return SemaPluginOutcome::Error;
    }
    Call.LeftBrace = *LeftHandle;
    Call.RightBrace = *RightHandle;
    Current = &Call;
    auto ResetCurrent = make_scope_exit([&] { Current = nullptr; });

    SemaExtensionArtifact Input;
    Input.Kind = SemaExtensionKind::Statement;
    Input.Statements = Call.Statements;
    Input.LeftBrace = Call.LeftBrace;
    Input.RightBrace = Call.RightBrace;
    auto InputHandle = Executor.createArtifactView(
        Task, semaExtensionArtifactID(), &Input, 1);
    if (!InputHandle) {
      reportError(SemanticAnalyzer,
                  toString(InputHandle.takeError()).str().str());
      return SemaPluginOutcome::Error;
    }
    Call.InputHandle = *InputHandle;
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(*InputHandle, PluginArtifactHandleKind);
    });

    PluginArtifactSlot Output(Artifacts.find(semaExtensionArtifactID()));
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    if (Error E =
            Executor.execute(Task.session(), Task, statementExtensionPhaseID(),
                             Route, *InputHandle, Output)) {
      reportError(SemanticAnalyzer, toString(std::move(E)).str().str());
      return SemaPluginOutcome::Error;
    }

    PluginArtifactSlot::Snapshot Snapshot = Output.snapshot();
    if (!Snapshot.Payload) {
      reportError(SemanticAnalyzer, "Sema extension produced no output");
      return SemaPluginOutcome::Error;
    }
    const auto &Result =
        *static_cast<const SemaExtensionArtifact *>(Snapshot.Payload);
    if (Result.Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED)
      return SemaPluginOutcome::NotHandled;

    const void *Native = nullptr;
    NevercStatus Status = ASTBridge.resolvePublishedNode(
        Task.handle(), Result.Statement, NEVERC_AST_SCHEMA_DOMAIN_STMT,
        &Native);
    if (Status.Code != NEVERC_STATUS_OK || !Native) {
      reportError(SemanticAnalyzer,
                  "Sema statement replacement failed verification");
      return SemaPluginOutcome::Error;
    }
    Replacement = const_cast<Stmt *>(static_cast<const Stmt *>(Native));
    return SemaPluginOutcome::Handled;
  }

  SemaPluginOutcome analyzeDeclarationReference(
      Sema &SemanticAnalyzer, SourceLocation NameLocation,
      NamedDecl *Declaration, NamedDecl *&Replacement) override {
    Replacement = nullptr;
    if (Current) {
      reportError(SemanticAnalyzer, "recursive Sema extension invocation");
      return SemaPluginOutcome::Error;
    }

    auto DeclarationHandle = ASTBridge.publishDecl(Declaration);
    auto LocationHandle = Locations.createLocation(NameLocation);
    if (!DeclarationHandle || !LocationHandle) {
      if (!DeclarationHandle)
        consumeError(DeclarationHandle.takeError());
      if (!LocationHandle)
        consumeError(LocationHandle.takeError());
      reportError(SemanticAnalyzer,
                  "cannot publish Sema declaration extension input");
      return SemaPluginOutcome::Error;
    }

    Invocation Call;
    Call.Kind = SemaExtensionKind::Declaration;
    Call.Declaration = *DeclarationHandle;
    Call.NameLocation = *LocationHandle;
    Current = &Call;
    auto ResetCurrent = make_scope_exit([&] { Current = nullptr; });

    SemaExtensionArtifact Input;
    Input.Kind = SemaExtensionKind::Declaration;
    Input.Declaration = Call.Declaration;
    Input.NameLocation = Call.NameLocation;
    auto InputHandle = Executor.createArtifactView(
        Task, semaExtensionArtifactID(), &Input, 1);
    if (!InputHandle) {
      reportError(SemanticAnalyzer,
                  toString(InputHandle.takeError()).str().str());
      return SemaPluginOutcome::Error;
    }
    Call.InputHandle = *InputHandle;
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(*InputHandle, PluginArtifactHandleKind);
    });

    PluginArtifactSlot Output(Artifacts.find(semaExtensionArtifactID()));
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    if (Error E = Executor.execute(Task.session(), Task,
                                   declarationExtensionPhaseID(), Route,
                                   *InputHandle, Output)) {
      reportError(SemanticAnalyzer, toString(std::move(E)).str().str());
      return SemaPluginOutcome::Error;
    }

    PluginArtifactSlot::Snapshot Snapshot = Output.snapshot();
    if (!Snapshot.Payload) {
      reportError(SemanticAnalyzer, "Sema extension produced no output");
      return SemaPluginOutcome::Error;
    }
    const auto &Result =
        *static_cast<const SemaExtensionArtifact *>(Snapshot.Payload);
    if (Result.Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED)
      return SemaPluginOutcome::NotHandled;

    const void *Native = nullptr;
    NevercStatus Status = ASTBridge.resolvePublishedNode(
        Task.handle(), Result.ReplacementDeclaration,
        NEVERC_AST_SCHEMA_DOMAIN_DECL, &Native);
    const auto *Named =
        Status.Code == NEVERC_STATUS_OK
            ? dyn_cast<NamedDecl>(static_cast<const Decl *>(Native))
            : nullptr;
    if (!Named) {
      reportError(SemanticAnalyzer,
                  "Sema declaration replacement failed verification");
      return SemaPluginOutcome::Error;
    }
    Replacement = const_cast<NamedDecl *>(Named);
    return SemaPluginOutcome::Handled;
  }

  SemaPluginOutcome analyzeTypeName(
      Sema &SemanticAnalyzer, SourceLocation NameLocation, StringRef Name,
      QualType &Replacement) override {
    Replacement = QualType();
    if (Current) {
      reportError(SemanticAnalyzer, "recursive Sema extension invocation");
      return SemaPluginOutcome::Error;
    }

    auto LocationHandle = Locations.createLocation(NameLocation);
    if (!LocationHandle) {
      consumeError(LocationHandle.takeError());
      reportError(SemanticAnalyzer, "cannot publish Sema type extension input");
      return SemaPluginOutcome::Error;
    }

    Invocation Call;
    Call.Kind = SemaExtensionKind::Type;
    Call.TypeName = Name.str();
    Call.TypeNameLocation = *LocationHandle;
    Current = &Call;
    auto ResetCurrent = make_scope_exit([&] { Current = nullptr; });

    SemaExtensionArtifact Input;
    Input.Kind = SemaExtensionKind::Type;
    Input.TypeName = Call.TypeName;
    Input.TypeNameLocation = Call.TypeNameLocation;
    auto InputHandle = Executor.createArtifactView(
        Task, semaExtensionArtifactID(), &Input, 1);
    if (!InputHandle) {
      reportError(SemanticAnalyzer,
                  toString(InputHandle.takeError()).str().str());
      return SemaPluginOutcome::Error;
    }
    Call.InputHandle = *InputHandle;
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(*InputHandle, PluginArtifactHandleKind);
    });

    PluginArtifactSlot Output(Artifacts.find(semaExtensionArtifactID()));
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    if (Error E = Executor.execute(Task.session(), Task, typeExtensionPhaseID(),
                                   Route, *InputHandle, Output)) {
      reportError(SemanticAnalyzer, toString(std::move(E)).str().str());
      return SemaPluginOutcome::Error;
    }

    PluginArtifactSlot::Snapshot Snapshot = Output.snapshot();
    if (!Snapshot.Payload) {
      reportError(SemanticAnalyzer, "Sema extension produced no output");
      return SemaPluginOutcome::Error;
    }
    const auto &Result =
        *static_cast<const SemaExtensionArtifact *>(Snapshot.Payload);
    if (Result.Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED)
      return SemaPluginOutcome::NotHandled;

    NevercStatus Status = ASTBridge.resolvePublishedType(
        Task.handle(), Result.Type, &Replacement);
    if (Status.Code != NEVERC_STATUS_OK || Replacement.isNull()) {
      reportError(SemanticAnalyzer,
                  "Sema type replacement failed verification");
      return SemaPluginOutcome::Error;
    }
    return SemaPluginOutcome::Handled;
  }

  SemaPluginOutcome analyzeLookup(
      Sema &SemanticAnalyzer, SourceLocation NameLocation, StringRef Name,
      SmallVectorImpl<NamedDecl *> &Candidates) override {
    Candidates.clear();
    if (Current) {
      reportError(SemanticAnalyzer, "recursive Sema extension invocation");
      return SemaPluginOutcome::Error;
    }

    auto LocationHandle = Locations.createLocation(NameLocation);
    if (!LocationHandle) {
      consumeError(LocationHandle.takeError());
      reportError(SemanticAnalyzer,
                  "cannot publish Sema lookup extension input");
      return SemaPluginOutcome::Error;
    }

    Invocation Call;
    Call.Kind = SemaExtensionKind::Lookup;
    Call.LookupName = Name.str();
    Call.LookupNameLocation = *LocationHandle;
    Current = &Call;
    auto ResetCurrent = make_scope_exit([&] { Current = nullptr; });

    SemaExtensionArtifact Input;
    Input.Kind = SemaExtensionKind::Lookup;
    Input.LookupName = Call.LookupName;
    Input.LookupNameLocation = Call.LookupNameLocation;
    Input.LookupKind = Call.LookupKind;
    auto InputHandle = Executor.createArtifactView(
        Task, semaExtensionArtifactID(), &Input, 1);
    if (!InputHandle) {
      reportError(SemanticAnalyzer,
                  toString(InputHandle.takeError()).str().str());
      return SemaPluginOutcome::Error;
    }
    Call.InputHandle = *InputHandle;
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(*InputHandle, PluginArtifactHandleKind);
    });

    PluginArtifactSlot Output(Artifacts.find(semaExtensionArtifactID()));
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    if (Error E = Executor.execute(Task.session(), Task,
                                   lookupExtensionPhaseID(), Route,
                                   *InputHandle, Output)) {
      reportError(SemanticAnalyzer, toString(std::move(E)).str().str());
      return SemaPluginOutcome::Error;
    }

    PluginArtifactSlot::Snapshot Snapshot = Output.snapshot();
    if (!Snapshot.Payload) {
      reportError(SemanticAnalyzer, "Sema extension produced no output");
      return SemaPluginOutcome::Error;
    }
    const auto &Result =
        *static_cast<const SemaExtensionArtifact *>(Snapshot.Payload);
    if (Result.Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED)
      return SemaPluginOutcome::NotHandled;

    for (NevercDeclHandle Candidate : Result.LookupCandidates) {
      const void *Native = nullptr;
      NevercStatus Status = ASTBridge.resolvePublishedNode(
          Task.handle(), Candidate, NEVERC_AST_SCHEMA_DOMAIN_DECL, &Native);
      const auto *Named =
          Status.Code == NEVERC_STATUS_OK
              ? dyn_cast<NamedDecl>(static_cast<const Decl *>(Native))
              : nullptr;
      if (!Named) {
        reportError(SemanticAnalyzer,
                    "Sema lookup replacement failed verification");
        Candidates.clear();
        return SemaPluginOutcome::Error;
      }
      Candidates.push_back(const_cast<NamedDecl *>(Named));
    }
    return SemaPluginOutcome::Handled;
  }

  SemaPluginOutcome analyzeImplicitConversion(
      Sema &SemanticAnalyzer, Expr *Expression, QualType DestinationType,
      unsigned ConversionContext, Expr *&Replacement) override {
    Replacement = nullptr;
    if (Current) {
      reportError(SemanticAnalyzer, "recursive Sema extension invocation");
      return SemaPluginOutcome::Error;
    }

    NevercSemaConversionContext PublicContext = 0;
    if (!Expression || Expression->getType().isNull() ||
        DestinationType.isNull() ||
        !mapConversionContext(ConversionContext, PublicContext)) {
      reportError(SemanticAnalyzer, "invalid Sema conversion extension input");
      return SemaPluginOutcome::Error;
    }

    auto ExpressionHandle = ASTBridge.publishExpr(Expression);
    auto SourceTypeHandle = ASTBridge.publishType(Expression->getType());
    auto DestinationTypeHandle = ASTBridge.publishType(DestinationType);
    if (!ExpressionHandle || !SourceTypeHandle || !DestinationTypeHandle) {
      if (!ExpressionHandle)
        consumeError(ExpressionHandle.takeError());
      if (!SourceTypeHandle)
        consumeError(SourceTypeHandle.takeError());
      if (!DestinationTypeHandle)
        consumeError(DestinationTypeHandle.takeError());
      reportError(SemanticAnalyzer,
                  "cannot publish Sema conversion extension input");
      return SemaPluginOutcome::Error;
    }

    Invocation Call;
    Call.Kind = SemaExtensionKind::Conversion;
    Call.ConversionExpression = *ExpressionHandle;
    Call.ConversionSourceType = *SourceTypeHandle;
    Call.ConversionDestinationType = *DestinationTypeHandle;
    Call.ConversionContext = PublicContext;
    Current = &Call;
    auto ResetCurrent = make_scope_exit([&] { Current = nullptr; });

    SemaExtensionArtifact Input;
    Input.Kind = SemaExtensionKind::Conversion;
    Input.ConversionExpression = Call.ConversionExpression;
    Input.ConversionSourceType = Call.ConversionSourceType;
    Input.ConversionDestinationType = Call.ConversionDestinationType;
    Input.ConversionContext = Call.ConversionContext;
    auto InputHandle = Executor.createArtifactView(
        Task, semaExtensionArtifactID(), &Input, 1);
    if (!InputHandle) {
      reportError(SemanticAnalyzer,
                  toString(InputHandle.takeError()).str().str());
      return SemaPluginOutcome::Error;
    }
    Call.InputHandle = *InputHandle;
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(*InputHandle, PluginArtifactHandleKind);
    });

    PluginArtifactSlot Output(Artifacts.find(semaExtensionArtifactID()));
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    if (Error E = Executor.execute(Task.session(), Task,
                                   conversionExtensionPhaseID(), Route,
                                   *InputHandle, Output)) {
      reportError(SemanticAnalyzer, toString(std::move(E)).str().str());
      return SemaPluginOutcome::Error;
    }

    PluginArtifactSlot::Snapshot Snapshot = Output.snapshot();
    if (!Snapshot.Payload) {
      reportError(SemanticAnalyzer, "Sema extension produced no output");
      return SemaPluginOutcome::Error;
    }
    const auto &Result =
        *static_cast<const SemaExtensionArtifact *>(Snapshot.Payload);
    if (Result.Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED)
      return SemaPluginOutcome::NotHandled;

    const void *Native = nullptr;
    NevercStatus Status = ASTBridge.resolvePublishedNode(
        Task.handle(), Result.Expression, NEVERC_AST_SCHEMA_DOMAIN_STMT,
        &Native);
    const auto *Converted =
        Status.Code == NEVERC_STATUS_OK
            ? dyn_cast<Expr>(static_cast<const Stmt *>(Native))
            : nullptr;
    if (!Converted ||
        !SemanticAnalyzer.Context.hasSameType(Converted->getType(),
                                              DestinationType)) {
      reportError(SemanticAnalyzer,
                  "Sema conversion replacement failed verification");
      return SemaPluginOutcome::Error;
    }
    Replacement = const_cast<Expr *>(Converted);
    return SemaPluginOutcome::Handled;
  }

  NevercStatus getExpressionExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaExpressionExtensionInput *OutInput) override {
    if (!Frame || !OutInput)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Invocation *Call = Current;
    if (!Call || !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, expressionExtensionPhaseID()) ||
        !sameHandle(Input, Call->InputHandle))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Executor.resolveArtifactPayload(
        Task, Input, semaExtensionArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto &Extension =
        *static_cast<const SemaExtensionArtifact *>(Payload);
    NevercSemaExpressionExtensionInput Value{};
    Value.Header = {sizeof(Value), NEVERC_SEMA_API_MAJOR,
                    NEVERC_SEMA_API_MINOR, 0};
    Value.Left = Extension.Left;
    Value.Right = Extension.Right;
    Value.OperatorLocation = Extension.OperatorLocation;
    return writeCallerRecord(OutInput, Value);
  }

  NevercStatus createExpressionExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaExpressionExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) override {
    if (!Frame || !Continuation || !Descriptor || !OutOutput ||
        !validHeader(Descriptor->Header, sizeof(*Descriptor)) ||
        Descriptor->Reserved != 0)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutOutput = {};
    Invocation *Call = Current;
    if (!Call || !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, expressionExtensionPhaseID()) ||
        !Executor.isActiveContinuation(Frame, Continuation))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Descriptor->Disposition != NEVERC_SEMA_EXTENSION_UNHANDLED &&
        Descriptor->Disposition != NEVERC_SEMA_EXTENSION_HANDLED)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_HANDLED &&
        neverc_handle_is_null(Descriptor->Expression))
      return extensionStatus(NEVERC_STATUS_WRONG_TYPE);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED &&
        !neverc_handle_is_null(Descriptor->Expression))
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    auto *Candidate = new (std::nothrow) SemaExtensionArtifact;
    if (!Candidate)
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    Candidate->Left = Call->Left;
    Candidate->Right = Call->Right;
    Candidate->OperatorLocation = Call->OperatorLocation;
    Candidate->Disposition = Descriptor->Disposition;
    Candidate->Expression = Descriptor->Expression;
    auto Handle = Executor.createCandidate(Task, semaExtensionArtifactID(),
                                           Candidate);
    if (!Handle) {
      delete Candidate;
      consumeError(Handle.takeError());
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutOutput = *Handle;
    return neverc_status_ok();
  }

  NevercStatus getStatementExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaStatementExtensionInput *OutInput) override {
    if (!Frame || !OutInput)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Invocation *Call = Current;
    if (!Call || Call->Kind != SemaExtensionKind::Statement ||
        !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, statementExtensionPhaseID()) ||
        !sameHandle(Input, Call->InputHandle))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Executor.resolveArtifactPayload(
        Task, Input, semaExtensionArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto &Extension =
        *static_cast<const SemaExtensionArtifact *>(Payload);
    NevercSemaStatementExtensionInput Value{};
    Value.Header = {sizeof(Value), NEVERC_SEMA_API_MAJOR,
                    NEVERC_SEMA_API_MINOR, 0};
    Value.Statements = Extension.Statements.data();
    Value.StatementCount = Extension.Statements.size();
    Value.LeftBrace = Extension.LeftBrace;
    Value.RightBrace = Extension.RightBrace;
    return writeCallerRecord(OutInput, Value);
  }

  NevercStatus createStatementExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaStatementExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) override {
    if (!Frame || !Continuation || !Descriptor || !OutOutput ||
        !validHeader(Descriptor->Header, sizeof(*Descriptor)) ||
        Descriptor->Reserved != 0)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutOutput = {};
    Invocation *Call = Current;
    if (!Call || Call->Kind != SemaExtensionKind::Statement ||
        !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, statementExtensionPhaseID()) ||
        !Executor.isActiveContinuation(Frame, Continuation))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Descriptor->Disposition != NEVERC_SEMA_EXTENSION_UNHANDLED &&
        Descriptor->Disposition != NEVERC_SEMA_EXTENSION_HANDLED)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_HANDLED &&
        neverc_handle_is_null(Descriptor->Statement))
      return extensionStatus(NEVERC_STATUS_WRONG_TYPE);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED &&
        !neverc_handle_is_null(Descriptor->Statement))
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    auto *Candidate = new (std::nothrow) SemaExtensionArtifact;
    if (!Candidate)
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    Candidate->Kind = SemaExtensionKind::Statement;
    Candidate->Statements = Call->Statements;
    Candidate->LeftBrace = Call->LeftBrace;
    Candidate->RightBrace = Call->RightBrace;
    Candidate->Disposition = Descriptor->Disposition;
    Candidate->Statement = Descriptor->Statement;
    auto Handle = Executor.createCandidate(Task, semaExtensionArtifactID(),
                                           Candidate);
    if (!Handle) {
      delete Candidate;
      consumeError(Handle.takeError());
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutOutput = *Handle;
    return neverc_status_ok();
  }

  NevercStatus getDeclarationExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaDeclarationExtensionInput *OutInput) override {
    if (!Frame || !OutInput)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Invocation *Call = Current;
    if (!Call || Call->Kind != SemaExtensionKind::Declaration ||
        !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, declarationExtensionPhaseID()) ||
        !sameHandle(Input, Call->InputHandle))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Executor.resolveArtifactPayload(
        Task, Input, semaExtensionArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto &Extension =
        *static_cast<const SemaExtensionArtifact *>(Payload);
    NevercSemaDeclarationExtensionInput Value{};
    Value.Header = {sizeof(Value), NEVERC_SEMA_API_MAJOR,
                    NEVERC_SEMA_API_MINOR, 0};
    Value.Declaration = Extension.Declaration;
    Value.NameLocation = Extension.NameLocation;
    return writeCallerRecord(OutInput, Value);
  }

  NevercStatus createDeclarationExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaDeclarationExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) override {
    if (!Frame || !Continuation || !Descriptor || !OutOutput ||
        !validHeader(Descriptor->Header, sizeof(*Descriptor)) ||
        Descriptor->Reserved != 0)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutOutput = {};
    Invocation *Call = Current;
    if (!Call || Call->Kind != SemaExtensionKind::Declaration ||
        !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, declarationExtensionPhaseID()) ||
        !Executor.isActiveContinuation(Frame, Continuation))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Descriptor->Disposition != NEVERC_SEMA_EXTENSION_UNHANDLED &&
        Descriptor->Disposition != NEVERC_SEMA_EXTENSION_HANDLED)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_HANDLED &&
        neverc_handle_is_null(Descriptor->Declaration))
      return extensionStatus(NEVERC_STATUS_WRONG_TYPE);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED &&
        !neverc_handle_is_null(Descriptor->Declaration))
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    auto *Candidate = new (std::nothrow) SemaExtensionArtifact;
    if (!Candidate)
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    Candidate->Kind = SemaExtensionKind::Declaration;
    Candidate->Declaration = Call->Declaration;
    Candidate->NameLocation = Call->NameLocation;
    Candidate->Disposition = Descriptor->Disposition;
    Candidate->ReplacementDeclaration = Descriptor->Declaration;
    auto Handle = Executor.createCandidate(Task, semaExtensionArtifactID(),
                                           Candidate);
    if (!Handle) {
      delete Candidate;
      consumeError(Handle.takeError());
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutOutput = *Handle;
    return neverc_status_ok();
  }

  NevercStatus getTypeExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaTypeExtensionInput *OutInput) override {
    if (!Frame || !OutInput)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Invocation *Call = Current;
    if (!Call || Call->Kind != SemaExtensionKind::Type ||
        !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, typeExtensionPhaseID()) ||
        !sameHandle(Input, Call->InputHandle))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Executor.resolveArtifactPayload(
        Task, Input, semaExtensionArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto &Extension =
        *static_cast<const SemaExtensionArtifact *>(Payload);
    NevercSemaTypeExtensionInput Value{};
    Value.Header = {sizeof(Value), NEVERC_SEMA_API_MAJOR,
                    NEVERC_SEMA_API_MINOR, 0};
    Value.Name = {Extension.TypeName.data(), Extension.TypeName.size()};
    Value.NameLocation = Extension.TypeNameLocation;
    return writeCallerRecord(OutInput, Value);
  }

  NevercStatus createTypeExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaTypeExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) override {
    if (!Frame || !Continuation || !Descriptor || !OutOutput ||
        !validHeader(Descriptor->Header, sizeof(*Descriptor)) ||
        Descriptor->Reserved != 0)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutOutput = {};
    Invocation *Call = Current;
    if (!Call || Call->Kind != SemaExtensionKind::Type ||
        !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, typeExtensionPhaseID()) ||
        !Executor.isActiveContinuation(Frame, Continuation))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Descriptor->Disposition != NEVERC_SEMA_EXTENSION_UNHANDLED &&
        Descriptor->Disposition != NEVERC_SEMA_EXTENSION_HANDLED)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_HANDLED &&
        neverc_handle_is_null(Descriptor->Type))
      return extensionStatus(NEVERC_STATUS_WRONG_TYPE);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED &&
        !neverc_handle_is_null(Descriptor->Type))
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    auto *Candidate = new (std::nothrow) SemaExtensionArtifact;
    if (!Candidate)
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    Candidate->Kind = SemaExtensionKind::Type;
    Candidate->TypeName = Call->TypeName;
    Candidate->TypeNameLocation = Call->TypeNameLocation;
    Candidate->Disposition = Descriptor->Disposition;
    Candidate->Type = Descriptor->Type;
    auto Handle = Executor.createCandidate(Task, semaExtensionArtifactID(),
                                           Candidate);
    if (!Handle) {
      delete Candidate;
      consumeError(Handle.takeError());
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutOutput = *Handle;
    return neverc_status_ok();
  }

  NevercStatus getLookupExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaLookupExtensionInput *OutInput) override {
    if (!Frame || !OutInput)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Invocation *Call = Current;
    if (!Call || Call->Kind != SemaExtensionKind::Lookup ||
        !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, lookupExtensionPhaseID()) ||
        !sameHandle(Input, Call->InputHandle))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Executor.resolveArtifactPayload(
        Task, Input, semaExtensionArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto &Extension =
        *static_cast<const SemaExtensionArtifact *>(Payload);
    NevercSemaLookupExtensionInput Value{};
    Value.Header = {sizeof(Value), NEVERC_SEMA_API_MAJOR,
                    NEVERC_SEMA_API_MINOR, 0};
    Value.Name = {Extension.LookupName.data(), Extension.LookupName.size()};
    Value.NameLocation = Extension.LookupNameLocation;
    Value.Kind = Extension.LookupKind;
    return writeCallerRecord(OutInput, Value);
  }

  NevercStatus createLookupExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaLookupExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) override {
    if (!Frame || !Continuation || !Descriptor || !OutOutput ||
        !validHeader(Descriptor->Header, sizeof(*Descriptor)) ||
        Descriptor->Reserved != 0 ||
        (Descriptor->CandidateCount != 0 && !Descriptor->Candidates))
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutOutput = {};
    Invocation *Call = Current;
    if (!Call || Call->Kind != SemaExtensionKind::Lookup ||
        !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, lookupExtensionPhaseID()) ||
        !Executor.isActiveContinuation(Frame, Continuation))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Descriptor->Disposition != NEVERC_SEMA_EXTENSION_UNHANDLED &&
        Descriptor->Disposition != NEVERC_SEMA_EXTENSION_HANDLED)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_HANDLED &&
        Descriptor->CandidateCount == 0)
      return extensionStatus(NEVERC_STATUS_WRONG_TYPE);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED &&
        Descriptor->CandidateCount != 0)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    auto *Candidate = new (std::nothrow) SemaExtensionArtifact;
    if (!Candidate)
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    Candidate->Kind = SemaExtensionKind::Lookup;
    Candidate->LookupName = Call->LookupName;
    Candidate->LookupNameLocation = Call->LookupNameLocation;
    Candidate->LookupKind = Call->LookupKind;
    Candidate->Disposition = Descriptor->Disposition;
    if (Descriptor->CandidateCount != 0)
      Candidate->LookupCandidates.assign(
          Descriptor->Candidates,
          Descriptor->Candidates + Descriptor->CandidateCount);
    auto Handle = Executor.createCandidate(Task, semaExtensionArtifactID(),
                                           Candidate);
    if (!Handle) {
      delete Candidate;
      consumeError(Handle.takeError());
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutOutput = *Handle;
    return neverc_status_ok();
  }

  NevercStatus getConversionExtensionInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaConversionExtensionInput *OutInput) override {
    if (!Frame || !OutInput)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Invocation *Call = Current;
    if (!Call || Call->Kind != SemaExtensionKind::Conversion ||
        !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, conversionExtensionPhaseID()) ||
        !sameHandle(Input, Call->InputHandle))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Executor.resolveArtifactPayload(
        Task, Input, semaExtensionArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto &Extension =
        *static_cast<const SemaExtensionArtifact *>(Payload);
    NevercSemaConversionExtensionInput Value{};
    Value.Header = {sizeof(Value), NEVERC_SEMA_API_MAJOR,
                    NEVERC_SEMA_API_MINOR, 0};
    Value.Expression = Extension.ConversionExpression;
    Value.SourceType = Extension.ConversionSourceType;
    Value.DestinationType = Extension.ConversionDestinationType;
    Value.Context = Extension.ConversionContext;
    return writeCallerRecord(OutInput, Value);
  }

  NevercStatus createConversionExtensionOutput(
      const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaConversionExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) override {
    if (!Frame || !Continuation || !Descriptor || !OutOutput ||
        !validHeader(Descriptor->Header, sizeof(*Descriptor)) ||
        Descriptor->Reserved != 0)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutOutput = {};
    Invocation *Call = Current;
    if (!Call || Call->Kind != SemaExtensionKind::Conversion ||
        !sameHandle(Frame->Task, Task.handle()) ||
        !samePluginInterfaceID(Frame->Phase, conversionExtensionPhaseID()) ||
        !Executor.isActiveContinuation(Frame, Continuation))
      return extensionStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Descriptor->Disposition != NEVERC_SEMA_EXTENSION_UNHANDLED &&
        Descriptor->Disposition != NEVERC_SEMA_EXTENSION_HANDLED)
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_HANDLED &&
        neverc_handle_is_null(Descriptor->Expression))
      return extensionStatus(NEVERC_STATUS_WRONG_TYPE);
    if (Descriptor->Disposition == NEVERC_SEMA_EXTENSION_UNHANDLED &&
        !neverc_handle_is_null(Descriptor->Expression))
      return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    auto *Candidate = new (std::nothrow) SemaExtensionArtifact;
    if (!Candidate)
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    Candidate->Kind = SemaExtensionKind::Conversion;
    Candidate->ConversionExpression = Call->ConversionExpression;
    Candidate->ConversionSourceType = Call->ConversionSourceType;
    Candidate->ConversionDestinationType = Call->ConversionDestinationType;
    Candidate->ConversionContext = Call->ConversionContext;
    Candidate->Disposition = Descriptor->Disposition;
    Candidate->Expression = Descriptor->Expression;
    auto Handle = Executor.createCandidate(Task, semaExtensionArtifactID(),
                                           Candidate);
    if (!Handle) {
      delete Candidate;
      consumeError(Handle.takeError());
      return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutOutput = *Handle;
    return neverc_status_ok();
  }

private:
  struct Invocation {
    SemaExtensionKind Kind = SemaExtensionKind::Expression;
    NevercArtifactHandle InputHandle{};
    NevercExprHandle Left{};
    NevercExprHandle Right{};
    NevercSourceLocation OperatorLocation{};
    std::vector<NevercStmtHandle> Statements;
    NevercSourceLocation LeftBrace{};
    NevercSourceLocation RightBrace{};
    NevercDeclHandle Declaration{};
    NevercSourceLocation NameLocation{};
    std::string TypeName;
    NevercSourceLocation TypeNameLocation{};
    std::string LookupName;
    NevercSourceLocation LookupNameLocation{};
    NevercSemaLookupKind LookupKind = NEVERC_SEMA_LOOKUP_ORDINARY;
    NevercExprHandle ConversionExpression{};
    NevercTypeHandle ConversionSourceType{};
    NevercTypeHandle ConversionDestinationType{};
    NevercSemaConversionContext ConversionContext =
        NEVERC_SEMA_CONVERSION_ASSIGNMENT;
  };

  static void reportError(Sema &SemanticAnalyzer, StringRef Detail) {
    std::string Message = "Sema extension phase failed: ";
    Message += Detail;
    SemanticAnalyzer.getDiagnostics().Report(diag::err_drv_plugin_phase)
        << Message;
  }

  PluginTaskContext &Task;
  PluginArtifactRegistry &Artifacts;
  PluginPhaseExecutor &Executor;
  PluginASTBridge &ASTBridge;
  FrontendPluginBridge &Locations;
  PluginSemaBridge &SemaBridge;
  Invocation *Current = nullptr;
};

NevercStatus semaExtensionBuiltin(PluginTaskContext &Task,
                                  PluginPhaseExecutor &Executor,
                                  const NevercPhaseFrame *Frame,
                                  NevercPhaseResult *Result) {
  if (!Frame || !Result)
    return extensionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Status = Executor.resolveArtifactPayload(
      Task, Frame->Input, semaExtensionArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Candidate = new (std::nothrow)
      SemaExtensionArtifact(*static_cast<const SemaExtensionArtifact *>(
          Payload));
  if (!Candidate)
    return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Candidate->Disposition = NEVERC_SEMA_EXTENSION_UNHANDLED;
  Candidate->Expression = {};
  Candidate->Statement = {};
  Candidate->ReplacementDeclaration = {};
  Candidate->Type = {};
  Candidate->LookupCandidates.clear();
  auto Handle =
      Executor.createCandidate(Task, semaExtensionArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return extensionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = *Handle;
  return neverc_status_ok();
}

} // namespace

NevercInterfaceID semaExtensionArtifactID() {
  return {NEVERC_PHASE_SEMA_EXTENSION_EXPRESSION_INPUT_HIGH,
          NEVERC_PHASE_SEMA_EXTENSION_EXPRESSION_INPUT_LOW};
}

Error registerSemaExtensionArtifactType(PluginArtifactRegistry &Artifacts) {
  auto Type = Artifacts.registerType(
      {semaExtensionArtifactID(), "sema.expression_extension",
       PluginArtifactOwnership::Owned, cloneSemaExtension,
       [](void *Payload) {
         delete static_cast<SemaExtensionArtifact *>(Payload);
       },
       verifySemaExtension});
  if (!Type)
    return Type.takeError();
  return Error::success();
}

Error registerSemaBuiltinProviders(PluginTaskContext &Task,
                                   PluginPhaseExecutor &Executor) {
  auto Provider = [&Task, &Executor](const NevercPhaseFrame *Frame,
                                     NevercPhaseResult *Result) {
    return semaExtensionBuiltin(Task, Executor, Frame, Result);
  };
  if (Error E =
          Executor.setBuiltinProvider(expressionExtensionPhaseID(), Provider))
    return E;
  if (Error E =
          Executor.setBuiltinProvider(statementExtensionPhaseID(), Provider))
    return E;
  if (Error E =
          Executor.setBuiltinProvider(declarationExtensionPhaseID(), Provider))
    return E;
  if (Error E = Executor.setBuiltinProvider(typeExtensionPhaseID(), Provider))
    return E;
  if (Error E =
          Executor.setBuiltinProvider(lookupExtensionPhaseID(), Provider))
    return E;
  return Executor.setBuiltinProvider(conversionExtensionPhaseID(), Provider);
}

bool hasSemaExtensionBindings(const PluginPhaseExecutor &Executor) {
  return Executor.hasBindings(expressionExtensionPhaseID()) ||
         Executor.hasBindings(statementExtensionPhaseID()) ||
         Executor.hasBindings(declarationExtensionPhaseID()) ||
         Executor.hasBindings(typeExtensionPhaseID()) ||
         Executor.hasBindings(lookupExtensionPhaseID()) ||
         Executor.hasBindings(conversionExtensionPhaseID());
}

Expected<std::unique_ptr<SemaPluginHooks>> createSemaPluginHooks(
    PluginTaskContext &Task, PluginArtifactRegistry &Artifacts,
    PluginPhaseExecutor &Executor, PluginASTBridge &ASTBridge,
    FrontendPluginBridge &Locations, PluginSemaBridge &SemaBridge) {
  return std::unique_ptr<SemaPluginHooks>(new SemaPluginHooksBridge(
      Task, Artifacts, Executor, ASTBridge, Locations, SemaBridge));
}

} // namespace neverc::plugin
