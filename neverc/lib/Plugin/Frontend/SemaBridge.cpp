#include "SemaBridgeInternal.h"
#include "FrontendPluginInterfaces.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Tree/Core/Attr.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/JSON.h"
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <new>

using namespace llvm;

namespace neverc::plugin {

namespace {

constexpr uint64_t MaximumSemaStringBytes = UINT64_C(1) << 20;

NevercStringView semaString(StringRef Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

class PluginSemaProcessBridge final : public PluginHostService {
public:
  PluginSemaProcessBridge() {
    API.Header = {sizeof(API), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
    API.Context = this;
    API.GetCurrentScope = GetCurrentScope;
    API.GetScopeInfo = GetScopeInfo;
    API.GetScopeDeclaration = GetScopeDeclaration;
    API.LookupName = LookupName;
    API.GetLookupResultInfo = GetLookupResultInfo;
    API.GetLookupCandidate = GetLookupCandidate;
    API.DestroyLookupResult = DestroyLookupResult;
    API.AcquireMutationLease = AcquireMutationLease;
    API.ReleaseMutationLease = ReleaseMutationLease;
    API.GetBuiltinType = GetBuiltinType;
    API.CreatePointerType = CreatePointerType;
    API.CreateConstantArrayType = CreateConstantArrayType;
    API.CreateFunctionType = CreateFunctionType;
    API.GetTagType = GetTagType;
    API.CreateAtomicType = CreateAtomicType;
    API.CreateVectorType = CreateVectorType;
    API.GetCanonicalType = GetCanonicalType;
    API.AreTypesCompatible = AreTypesCompatible;
    API.ClassifyImplicitConversion = ClassifyImplicitConversion;
    API.GetConversionSequenceInfo = GetConversionSequenceInfo;
    API.ApplyImplicitConversion = ApplyImplicitConversion;
    API.CreateExplicitCast = CreateExplicitCast;
    API.DestroyConversionSequence = DestroyConversionSequence;
    API.EvaluateConstant = EvaluateConstant;
    API.GetConstantValueInfo = GetConstantValueInfo;
    API.GetConstantIntegerWord = GetConstantIntegerWord;
    API.GetConstantElement = GetConstantElement;
    API.DestroyConstantValue = DestroyConstantValue;
    API.HasDeclAttribute = HasDeclAttribute;
    API.GetBuiltinInfo = GetBuiltinInfo;
    API.EmitDiagnostic = EmitDiagnostic;
    API.GetExpressionExtensionInput = GetExpressionExtensionInput;
    API.CreateExpressionExtensionOutput = CreateExpressionExtensionOutput;
    API.GetStatementExtensionInput = GetStatementExtensionInput;
    API.CreateStatementExtensionOutput = CreateStatementExtensionOutput;
    API.GetDeclarationExtensionInput = GetDeclarationExtensionInput;
    API.CreateDeclarationExtensionOutput = CreateDeclarationExtensionOutput;
    API.GetTypeExtensionInput = GetTypeExtensionInput;
    API.CreateTypeExtensionOutput = CreateTypeExtensionOutput;
    API.GetLookupExtensionInput = GetLookupExtensionInput;
    API.CreateLookupExtensionOutput = CreateLookupExtensionOutput;
    API.GetConversionExtensionInput = GetConversionExtensionInput;
    API.CreateConversionExtensionOutput = CreateConversionExtensionOutput;
    API.GetAnalyzePhaseInput = GetAnalyzePhaseInput;
    API.CreateSemanticUnit = CreateSemanticUnit;
    API.GetSemanticUnitInfo = GetSemanticUnitInfo;
  }

  const NevercSemaAPI &api() const { return API; }

  Error attach(PluginTaskContext &Task, PluginSemaBridge &Bridge) {
    const auto Key = std::make_pair(Task.handle().Owner, Task.handle().Value);
    std::lock_guard<std::mutex> Lock(Mutex);
    if (!Tasks.try_emplace(Key, &Bridge).second)
      return createStringError(inconvertibleErrorCode(),
                               "Sema bridge is already attached to task");
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
  template <typename Callback>
  NevercStatus forward(NevercTaskHandle Task, Callback &&Call) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Tasks.find(std::make_pair(Task.Owner, Task.Value));
    if (It == Tasks.end() || !It->second)
      return semaStatus(NEVERC_STATUS_STALE_HANDLE);
    return Call(It->second->semaAPI());
  }

  static PluginSemaProcessBridge *bridge(void *Context) {
    return static_cast<PluginSemaProcessBridge *>(Context);
  }

#define NEVERC_FORWARD_SEMA(Name, Signature, ...)                              \
  static NevercStatus NEVERC_CALL Name Signature {                             \
    if (!Context)                                                              \
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);                       \
    return bridge(Context)->forward(Task, [&](const NevercSemaAPI &Local) {    \
      return Local.Name(Local.Context, __VA_ARGS__);                           \
    });                                                                        \
  }

  NEVERC_FORWARD_SEMA(GetCurrentScope,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaScopeHandle *OutScope),
                      Task, OutScope)
  NEVERC_FORWARD_SEMA(GetScopeInfo,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaScopeHandle Scope,
                       NevercSemaScopeInfo *OutInfo),
                      Task, Scope, OutInfo)
  NEVERC_FORWARD_SEMA(GetScopeDeclaration,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaScopeHandle Scope, uint64_t Index,
                       NevercDeclHandle *OutDeclaration),
                      Task, Scope, Index, OutDeclaration)
  NEVERC_FORWARD_SEMA(LookupName,
                      (void *Context, NevercTaskHandle Task,
                       const NevercSemaLookupRequest *Request,
                       NevercLookupResultHandle *OutResult),
                      Task, Request, OutResult)
  NEVERC_FORWARD_SEMA(GetLookupResultInfo,
                      (void *Context, NevercTaskHandle Task,
                       NevercLookupResultHandle Result,
                       NevercSemaLookupResultInfo *OutInfo),
                      Task, Result, OutInfo)
  NEVERC_FORWARD_SEMA(GetLookupCandidate,
                      (void *Context, NevercTaskHandle Task,
                       NevercLookupResultHandle Result, uint64_t Index,
                       NevercDeclHandle *OutDeclaration),
                      Task, Result, Index, OutDeclaration)
  NEVERC_FORWARD_SEMA(DestroyLookupResult,
                      (void *Context, NevercTaskHandle Task,
                       NevercLookupResultHandle Result),
                      Task, Result)
  NEVERC_FORWARD_SEMA(AcquireMutationLease,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaMutationLeaseHandle *OutLease),
                      Task, OutLease)
  NEVERC_FORWARD_SEMA(ReleaseMutationLease,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaMutationLeaseHandle Lease),
                      Task, Lease)
  NEVERC_FORWARD_SEMA(GetBuiltinType,
                      (void *Context, NevercTaskHandle Task,
                       NevercBuiltinTypeKind Kind, NevercTypeHandle *OutType),
                      Task, Kind, OutType)
  NEVERC_FORWARD_SEMA(CreatePointerType,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaMutationLeaseHandle Lease,
                       NevercTypeHandle Pointee, NevercTypeHandle *OutType),
                      Task, Lease, Pointee, OutType)
  NEVERC_FORWARD_SEMA(CreateConstantArrayType,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaMutationLeaseHandle Lease,
                       NevercTypeHandle Element, uint64_t ElementCount,
                       NevercTypeHandle *OutType),
                      Task, Lease, Element, ElementCount, OutType)
  NEVERC_FORWARD_SEMA(CreateFunctionType,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaMutationLeaseHandle Lease,
                       const NevercSemaFunctionTypeDescriptor *Descriptor,
                       NevercTypeHandle *OutType),
                      Task, Lease, Descriptor, OutType)
  NEVERC_FORWARD_SEMA(GetTagType,
                      (void *Context, NevercTaskHandle Task,
                       NevercDeclHandle Declaration,
                       NevercTypeHandle *OutType),
                      Task, Declaration, OutType)
  NEVERC_FORWARD_SEMA(CreateAtomicType,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaMutationLeaseHandle Lease,
                       NevercTypeHandle ValueType, NevercTypeHandle *OutType),
                      Task, Lease, ValueType, OutType)
  NEVERC_FORWARD_SEMA(CreateVectorType,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaMutationLeaseHandle Lease,
                       NevercTypeHandle Element, uint32_t ElementCount,
                       NevercSemaVectorKind Kind, NevercTypeHandle *OutType),
                      Task, Lease, Element, ElementCount, Kind, OutType)
  NEVERC_FORWARD_SEMA(GetCanonicalType,
                      (void *Context, NevercTaskHandle Task,
                       NevercTypeHandle Type, NevercTypeHandle *OutType),
                      Task, Type, OutType)
  NEVERC_FORWARD_SEMA(AreTypesCompatible,
                      (void *Context, NevercTaskHandle Task,
                       NevercTypeHandle Left, NevercTypeHandle Right,
                       NevercBool *OutCompatible),
                      Task, Left, Right, OutCompatible)
  NEVERC_FORWARD_SEMA(ClassifyImplicitConversion,
                      (void *Context, NevercTaskHandle Task,
                       NevercTypeHandle Source, NevercTypeHandle Destination,
                       NevercConversionSequenceHandle *OutSequence),
                      Task, Source, Destination, OutSequence)
  NEVERC_FORWARD_SEMA(GetConversionSequenceInfo,
                      (void *Context, NevercTaskHandle Task,
                       NevercConversionSequenceHandle Sequence,
                       NevercSemaConversionSequenceInfo *OutInfo),
                      Task, Sequence, OutInfo)
  NEVERC_FORWARD_SEMA(ApplyImplicitConversion,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaMutationLeaseHandle Lease,
                       NevercConversionSequenceHandle Sequence,
                       NevercExprHandle Expression,
                       NevercSemaConversionContext ConversionContext,
                       NevercExprHandle *OutExpression),
                      Task, Lease, Sequence, Expression, ConversionContext,
                      OutExpression)
  NEVERC_FORWARD_SEMA(CreateExplicitCast,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaMutationLeaseHandle Lease,
                       NevercExprHandle Expression,
                       NevercTypeHandle Destination,
                       NevercExprHandle *OutExpression),
                      Task, Lease, Expression, Destination, OutExpression)
  NEVERC_FORWARD_SEMA(DestroyConversionSequence,
                      (void *Context, NevercTaskHandle Task,
                       NevercConversionSequenceHandle Sequence),
                      Task, Sequence)
  NEVERC_FORWARD_SEMA(EvaluateConstant,
                      (void *Context, NevercTaskHandle Task,
                       NevercExprHandle Expression,
                       NevercConstantValueHandle *OutValue),
                      Task, Expression, OutValue)
  NEVERC_FORWARD_SEMA(GetConstantValueInfo,
                      (void *Context, NevercTaskHandle Task,
                       NevercConstantValueHandle Value,
                       NevercSemaConstantValueInfo *OutInfo),
                      Task, Value, OutInfo)
  NEVERC_FORWARD_SEMA(GetConstantIntegerWord,
                      (void *Context, NevercTaskHandle Task,
                       NevercConstantValueHandle Value, uint64_t Index,
                       uint64_t *OutWord),
                      Task, Value, Index, OutWord)
  NEVERC_FORWARD_SEMA(GetConstantElement,
                      (void *Context, NevercTaskHandle Task,
                       NevercConstantValueHandle Value, uint64_t Index,
                       NevercConstantValueHandle *OutElement),
                      Task, Value, Index, OutElement)
  NEVERC_FORWARD_SEMA(DestroyConstantValue,
                      (void *Context, NevercTaskHandle Task,
                       NevercConstantValueHandle Value),
                      Task, Value)
  NEVERC_FORWARD_SEMA(HasDeclAttribute,
                      (void *Context, NevercTaskHandle Task,
                       NevercDeclHandle Declaration,
                       NevercStringView AttributeName,
                       NevercBool *OutPresent),
                      Task, Declaration, AttributeName, OutPresent)
  NEVERC_FORWARD_SEMA(GetBuiltinInfo,
                      (void *Context, NevercTaskHandle Task,
                       NevercStringView Name, NevercSemaBuiltinInfo *OutInfo),
                      Task, Name, OutInfo)
  NEVERC_FORWARD_SEMA(EmitDiagnostic,
                      (void *Context, NevercTaskHandle Task,
                       NevercSemaMutationLeaseHandle Lease,
                       const NevercSemaDiagnosticDescriptor *Descriptor),
                      Task, Lease, Descriptor)

#undef NEVERC_FORWARD_SEMA

  static NevercStatus NEVERC_CALL GetExpressionExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaExpressionExtensionInput *OutInput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.GetExpressionExtensionInput(Local.Context, Frame, Input,
                                                   OutInput);
        });
  }

  static NevercStatus NEVERC_CALL CreateExpressionExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaExpressionExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.CreateExpressionExtensionOutput(
              Local.Context, Frame, Continuation, Descriptor, OutOutput);
        });
  }

  static NevercStatus NEVERC_CALL GetStatementExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaStatementExtensionInput *OutInput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.GetStatementExtensionInput(Local.Context, Frame, Input,
                                                  OutInput);
        });
  }

  static NevercStatus NEVERC_CALL CreateStatementExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaStatementExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.CreateStatementExtensionOutput(
              Local.Context, Frame, Continuation, Descriptor, OutOutput);
        });
  }

  static NevercStatus NEVERC_CALL GetDeclarationExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaDeclarationExtensionInput *OutInput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.GetDeclarationExtensionInput(Local.Context, Frame, Input,
                                                    OutInput);
        });
  }

  static NevercStatus NEVERC_CALL CreateDeclarationExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaDeclarationExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.CreateDeclarationExtensionOutput(
              Local.Context, Frame, Continuation, Descriptor, OutOutput);
        });
  }

  static NevercStatus NEVERC_CALL GetTypeExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaTypeExtensionInput *OutInput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.GetTypeExtensionInput(Local.Context, Frame, Input,
                                             OutInput);
        });
  }

  static NevercStatus NEVERC_CALL CreateTypeExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaTypeExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.CreateTypeExtensionOutput(
              Local.Context, Frame, Continuation, Descriptor, OutOutput);
        });
  }

  static NevercStatus NEVERC_CALL GetLookupExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaLookupExtensionInput *OutInput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.GetLookupExtensionInput(Local.Context, Frame, Input,
                                               OutInput);
        });
  }

  static NevercStatus NEVERC_CALL CreateLookupExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaLookupExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.CreateLookupExtensionOutput(
              Local.Context, Frame, Continuation, Descriptor, OutOutput);
        });
  }

  static NevercStatus NEVERC_CALL GetConversionExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaConversionExtensionInput *OutInput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.GetConversionExtensionInput(Local.Context, Frame, Input,
                                                   OutInput);
        });
  }

  static NevercStatus NEVERC_CALL CreateConversionExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaConversionExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.CreateConversionExtensionOutput(
              Local.Context, Frame, Continuation, Descriptor, OutOutput);
        });
  }

  static NevercStatus NEVERC_CALL GetAnalyzePhaseInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaPhaseInput *OutInput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.GetAnalyzePhaseInput(Local.Context, Frame, Input,
                                            OutInput);
        });
  }

  static NevercStatus NEVERC_CALL CreateSemanticUnit(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercSemanticUnitDescriptor *Descriptor,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.CreateSemanticUnit(Local.Context, Frame, Descriptor,
                                          OutOutput);
        });
  }

  static NevercStatus NEVERC_CALL GetSemanticUnitInfo(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Unit,
      NevercSemanticUnitInfo *OutInfo) {
    if (!Context || !Frame)
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->forward(
        Frame->Task, [&](const NevercSemaAPI &Local) {
          return Local.GetSemanticUnitInfo(Local.Context, Frame, Unit, OutInfo);
        });
  }

  NevercSemaAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>, PluginSemaBridge *> Tasks;
};

std::shared_ptr<PluginSemaProcessBridge>
findSemaProcessBridge(PluginProcessServices &Services) {
  return std::static_pointer_cast<PluginSemaProcessBridge>(
      Services.findHostService(semaPluginInterfaceID()));
}

} // namespace

NevercStatus semaStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool semaSameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool semaValidHeader(const NevercABITableHeader &Header, size_t Size) {
  return Header.StructSize >= Size && Header.Major == NEVERC_SEMA_API_MAJOR &&
         Header.Minor <= NEVERC_SEMA_API_MINOR && Header.Flags == 0;
}

bool semaStringView(NevercStringView View, StringRef &Out, bool AllowEmpty) {
  if (View.Length > MaximumSemaStringBytes ||
      View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  Out = StringRef(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  return (AllowEmpty || !Out.empty()) && !Out.contains('\0') &&
         json::isUTF8(Out);
}

PluginSemaBridge::Impl::Impl(PluginTaskContext &TaskValue,
                             Sema &SemanticAnalyzerValue,
                             PluginASTBridge &ASTValue,
                             FrontendPluginBridge &LocationsValue)
    : Task(TaskValue), SemanticAnalyzer(SemanticAnalyzerValue), AST(ASTValue),
      Locations(LocationsValue) {
  API.Header = {sizeof(API), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  API.Context = this;
  API.GetCurrentScope = getCurrentScope;
  API.GetScopeInfo = getScopeInfo;
  API.GetScopeDeclaration = getScopeDeclaration;
  API.LookupName = lookupName;
  API.GetLookupResultInfo = getLookupResultInfo;
  API.GetLookupCandidate = getLookupCandidate;
  API.DestroyLookupResult = destroyLookupResult;
  API.AcquireMutationLease = acquireMutationLease;
  API.ReleaseMutationLease = releaseMutationLease;
  API.GetBuiltinType = getBuiltinType;
  API.CreatePointerType = createPointerType;
  API.CreateConstantArrayType = createConstantArrayType;
  API.CreateFunctionType = createFunctionType;
  API.GetTagType = getTagType;
  API.CreateAtomicType = createAtomicType;
  API.CreateVectorType = createVectorType;
  API.GetCanonicalType = getCanonicalType;
  API.AreTypesCompatible = areTypesCompatible;
  API.ClassifyImplicitConversion = classifyImplicitConversion;
  API.GetConversionSequenceInfo = getConversionSequenceInfo;
  API.ApplyImplicitConversion = applyImplicitConversion;
  API.CreateExplicitCast = createExplicitCast;
  API.DestroyConversionSequence = destroyConversionSequence;
  API.EvaluateConstant = evaluateConstant;
  API.GetConstantValueInfo = getConstantValueInfo;
  API.GetConstantIntegerWord = getConstantIntegerWord;
  API.GetConstantElement = getConstantElement;
  API.DestroyConstantValue = destroyConstantValue;
  API.HasDeclAttribute = hasDeclAttribute;
  API.GetBuiltinInfo = getBuiltinInfo;
  API.EmitDiagnostic = emitDiagnostic;
  API.GetExpressionExtensionInput = getExpressionExtensionInput;
  API.CreateExpressionExtensionOutput = createExpressionExtensionOutput;
  API.GetStatementExtensionInput = getStatementExtensionInput;
  API.CreateStatementExtensionOutput = createStatementExtensionOutput;
  API.GetDeclarationExtensionInput = getDeclarationExtensionInput;
  API.CreateDeclarationExtensionOutput = createDeclarationExtensionOutput;
  API.GetTypeExtensionInput = getTypeExtensionInput;
  API.CreateTypeExtensionOutput = createTypeExtensionOutput;
  API.GetLookupExtensionInput = getLookupExtensionInput;
  API.CreateLookupExtensionOutput = createLookupExtensionOutput;
  API.GetConversionExtensionInput = getConversionExtensionInput;
  API.CreateConversionExtensionOutput = createConversionExtensionOutput;
  API.GetAnalyzePhaseInput = getAnalyzePhaseInput;
  API.CreateSemanticUnit = createSemanticUnit;
  API.GetSemanticUnitInfo = getSemanticUnitInfo;
}

bool PluginSemaBridge::Impl::validTask(NevercTaskHandle Handle) const {
  return !Task.isEnded() && semaSameHandle(Handle, Task.handle());
}

Expected<NevercSemaScopeHandle>
PluginSemaBridge::Impl::createScope(DeclContext *Context) {
  if (!Context)
    return createStringError(inconvertibleErrorCode(),
                             "cannot publish a null Sema scope");
  if (auto It = ScopeHandles.find(Context); It != ScopeHandles.end())
    return It->second;
  auto *Payload = new (std::nothrow) ScopePayload{this, Context};
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "cannot allocate Sema scope");
  auto Handle = Task.handles().create(
      PluginSemaScopeHandleKind, Payload,
      [](void *Value) { delete static_cast<ScopePayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  ScopeHandles[Context] = *Handle;
  return *Handle;
}

Expected<NevercConstantValueHandle>
PluginSemaBridge::Impl::createConstant(const APValue &Value, QualType Type,
                                       bool HasSideEffects) {
  auto *Payload = new (std::nothrow) ConstantPayload;
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "cannot allocate Sema constant value");
  Payload->Owner = this;
  Payload->Value = Value;
  Payload->Type = Type;
  Payload->HasSideEffects = HasSideEffects;
  if (!Type.isNull())
    Payload->Text = Value.getAsString(SemanticAnalyzer.Context, Type);
  auto Handle = Task.handles().create(
      PluginSemaConstantValueHandleKind, Payload,
      [](void *Raw) { delete static_cast<ConstantPayload *>(Raw); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

#define NEVERC_DEFINE_SEMA_RESOLVER(Name, HandleType, PayloadType, Kind)        \
  NevercStatus PluginSemaBridge::Impl::Name(                                   \
      NevercTaskHandle TaskHandle, HandleType Handle, PayloadType **Out) {      \
    if (!Out)                                                                  \
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);                       \
    *Out = nullptr;                                                            \
    if (!validTask(TaskHandle))                                                \
      return semaStatus(NEVERC_STATUS_WRONG_SCOPE);                            \
    void *Raw = nullptr;                                                       \
    NevercStatus Status = Task.handles().resolve(Handle, Kind, &Raw);          \
    if (Status.Code != NEVERC_STATUS_OK)                                       \
      return Status;                                                           \
    auto *Payload = static_cast<PayloadType *>(Raw);                           \
    if (Payload->Owner != this)                                                \
      return semaStatus(NEVERC_STATUS_WRONG_SCOPE);                            \
    *Out = Payload;                                                            \
    return neverc_status_ok();                                                 \
  }

NEVERC_DEFINE_SEMA_RESOLVER(resolveScope, NevercSemaScopeHandle, ScopePayload,
                            PluginSemaScopeHandleKind)
NEVERC_DEFINE_SEMA_RESOLVER(resolveLookup, NevercLookupResultHandle,
                            LookupPayload, PluginSemaLookupResultHandleKind)
NEVERC_DEFINE_SEMA_RESOLVER(resolveLease, NevercSemaMutationLeaseHandle,
                            MutationLeasePayload,
                            PluginSemaMutationLeaseHandleKind)
NEVERC_DEFINE_SEMA_RESOLVER(resolveConversion,
                            NevercConversionSequenceHandle, ConversionPayload,
                            PluginSemaConversionSequenceHandleKind)
NEVERC_DEFINE_SEMA_RESOLVER(resolveConstant, NevercConstantValueHandle,
                            ConstantPayload, PluginSemaConstantValueHandleKind)

#undef NEVERC_DEFINE_SEMA_RESOLVER

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getCurrentScope(
    void *Context, NevercTaskHandle Task, NevercSemaScopeHandle *OutScope) {
  if (!Context || !OutScope)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutScope = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.validTask(Task))
    return semaStatus(NEVERC_STATUS_WRONG_SCOPE);
  DeclContext *Current = Bridge.SemanticAnalyzer.CurContext;
  if (!Current)
    Current = Bridge.SemanticAnalyzer.Context.getTranslationUnitDecl();
  auto Handle = Bridge.createScope(Current);
  if (!Handle) {
    consumeError(Handle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutScope = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getScopeInfo(
    void *Context, NevercTaskHandle Task, NevercSemaScopeHandle Scope,
    NevercSemaScopeInfo *OutInfo) {
  if (!Context || !OutInfo)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  ScopePayload *Payload = nullptr;
  NevercStatus Status = Bridge.resolveScope(Task, Scope, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercSemaScopeInfo Result{};
  Result.Header = {sizeof(Result), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR,
                   0};
  DeclContext *DC = Payload->Context;
  if (DeclContext *Parent = DC->getLexicalParent()) {
    auto ParentHandle = Bridge.createScope(Parent);
    if (!ParentHandle) {
      consumeError(ParentHandle.takeError());
      return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Result.Parent = *ParentHandle;
  }
  auto ContextHandle = Bridge.AST.publishDecl(Decl::castFromDeclContext(DC));
  if (!ContextHandle) {
    consumeError(ContextHandle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Result.DeclContext = *ContextHandle;
  if (DC->isFileContext())
    Result.Flags |= NEVERC_SEMA_SCOPE_FILE;
  else if (DC->isFunctionOrMethod())
    Result.Flags |= NEVERC_SEMA_SCOPE_FUNCTION;
  else if (DC->isRecord())
    Result.Flags |= NEVERC_SEMA_SCOPE_RECORD;
  else
    Result.Flags |= NEVERC_SEMA_SCOPE_BLOCK;
  Result.DeclarationCount =
      static_cast<uint64_t>(std::distance(DC->decls_begin(), DC->decls_end()));
  return writeSemaRecord(OutInfo, Result);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getScopeDeclaration(
    void *Context, NevercTaskHandle Task, NevercSemaScopeHandle Scope,
    uint64_t Index, NevercDeclHandle *OutDeclaration) {
  if (!Context || !OutDeclaration)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutDeclaration = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  ScopePayload *Payload = nullptr;
  NevercStatus Status = Bridge.resolveScope(Task, Scope, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const uint64_t Count = static_cast<uint64_t>(
      std::distance(Payload->Context->decls_begin(),
                    Payload->Context->decls_end()));
  if (Index >= Count)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto It = Payload->Context->decls_begin();
  std::advance(It, static_cast<ptrdiff_t>(Index));
  auto Handle = Bridge.AST.publishDecl(*It);
  if (!Handle) {
    consumeError(Handle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutDeclaration = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::acquireMutationLease(
    void *Context, NevercTaskHandle Task,
    NevercSemaMutationLeaseHandle *OutLease) {
  if (!Context || !OutLease)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutLease = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.validTask(Task))
    return semaStatus(NEVERC_STATUS_WRONG_SCOPE);
  if (Bridge.MutationLeaseActive)
    return semaStatus(NEVERC_STATUS_BUSY);
  auto *Payload = new (std::nothrow) MutationLeasePayload{&Bridge, true};
  if (!Payload)
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Bridge.Task.handles().create(
      PluginSemaMutationLeaseHandleKind, Payload,
      [](void *Raw) { delete static_cast<MutationLeasePayload *>(Raw); });
  if (!Handle) {
    delete Payload;
    consumeError(Handle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Bridge.MutationLeaseActive = true;
  *OutLease = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::releaseMutationLease(
    void *Context, NevercTaskHandle Task, NevercSemaMutationLeaseHandle Lease) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  MutationLeasePayload *Payload = nullptr;
  NevercStatus Status = Bridge.resolveLease(Task, Lease, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Payload->Active || !Bridge.MutationLeaseActive)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  Payload->Active = false;
  Bridge.MutationLeaseActive = false;
  return Bridge.Task.handles().release(Lease,
                                       PluginSemaMutationLeaseHandleKind);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::hasDeclAttribute(
    void *Context, NevercTaskHandle Task, NevercDeclHandle Declaration,
    NevercStringView AttributeName, NevercBool *OutPresent) {
  if (!Context || !OutPresent)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutPresent = NEVERC_FALSE;
  StringRef Name;
  if (!semaStringView(AttributeName, Name))
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  const void *Native = nullptr;
  NevercStatus Status = Bridge.AST.resolvePublishedNode(
      Task, Declaration, NEVERC_AST_SCHEMA_DOMAIN_DECL, &Native);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto *D = static_cast<const Decl *>(Native);
  for (const Attr *Attribute : D->attrs())
    if (Name == Attribute->getSpelling()) {
      *OutPresent = NEVERC_TRUE;
      break;
    }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getBuiltinInfo(
    void *Context, NevercTaskHandle Task, NevercStringView NameView,
    NevercSemaBuiltinInfo *OutInfo) {
  if (!Context || !OutInfo)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef Name;
  if (!semaStringView(NameView, Name))
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.validTask(Task))
    return semaStatus(NEVERC_STATUS_WRONG_SCOPE);
  auto It = Bridge.SemanticAnalyzer.Context.Idents.find(Name);
  if (It == Bridge.SemanticAnalyzer.Context.Idents.end())
    return semaStatus(NEVERC_STATUS_NOT_FOUND);
  const unsigned ID = It->second->getBuiltinID();
  if (ID == 0)
    return semaStatus(NEVERC_STATUS_NOT_FOUND);

  Builtin::Context &Builtins = Bridge.SemanticAnalyzer.Context.BuiltinInfo;
  NevercSemaBuiltinInfo Result{};
  Result.Header = {sizeof(Result), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR,
                   0};
  Result.BuiltinID = ID;
  if (Builtins.isTSBuiltin(ID))
    Result.Flags |= NEVERC_SEMA_BUILTIN_TARGET_SPECIFIC;
  if (Builtins.isPure(ID))
    Result.Flags |= NEVERC_SEMA_BUILTIN_PURE;
  if (Builtins.isConst(ID))
    Result.Flags |= NEVERC_SEMA_BUILTIN_CONST;
  if (Builtins.isNoThrow(ID))
    Result.Flags |= NEVERC_SEMA_BUILTIN_NOTHROW;
  if (Builtins.isNoReturn(ID))
    Result.Flags |= NEVERC_SEMA_BUILTIN_NORETURN;
  if (Builtins.isReturnsTwice(ID))
    Result.Flags |= NEVERC_SEMA_BUILTIN_RETURNS_TWICE;
  if (Builtins.isUnevaluated(ID))
    Result.Flags |= NEVERC_SEMA_BUILTIN_UNEVALUATED;
  if (Builtins.isLibFunction(ID) || Builtins.isPredefinedLibFunction(ID))
    Result.Flags |= NEVERC_SEMA_BUILTIN_LIBRARY_FUNCTION;
  if (Builtins.hasCustomTypechecking(ID))
    Result.Flags |= NEVERC_SEMA_BUILTIN_CUSTOM_TYPECHECK;
  Result.Name = semaString(Builtins.getName(ID));
  Result.TypeEncoding = semaString(Builtins.getTypeString(ID));
  Result.RequiredFeatures = semaString(Builtins.getRequiredFeatures(ID));
  Result.HeaderName = semaString(Builtins.getHeaderName(ID));
  return writeSemaRecord(OutInfo, Result);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::emitDiagnostic(
    void *Context, NevercTaskHandle Task, NevercSemaMutationLeaseHandle Lease,
    const NevercSemaDiagnosticDescriptor *Descriptor) {
  if (!Context || !Descriptor ||
      !semaValidHeader(Descriptor->Header, sizeof(*Descriptor)) ||
      Descriptor->Reserved != 0)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  MutationLeasePayload *LeasePayload = nullptr;
  NevercStatus Status = Bridge.resolveLease(Task, Lease, &LeasePayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!LeasePayload->Active)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  StringRef Message;
  if (!semaStringView(Descriptor->Message, Message))
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  DiagnosticsEngine::Level Level;
  switch (Descriptor->Level) {
  case NEVERC_SEMA_DIAGNOSTIC_NOTE:
    Level = DiagnosticsEngine::Note;
    break;
  case NEVERC_SEMA_DIAGNOSTIC_REMARK:
    Level = DiagnosticsEngine::Remark;
    break;
  case NEVERC_SEMA_DIAGNOSTIC_WARNING:
    Level = DiagnosticsEngine::Warning;
    break;
  case NEVERC_SEMA_DIAGNOSTIC_ERROR:
    Level = DiagnosticsEngine::Error;
    break;
  case NEVERC_SEMA_DIAGNOSTIC_FATAL:
    Level = DiagnosticsEngine::Fatal;
    break;
  default:
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }

  SourceLocation Location;
  if (!neverc_handle_is_null(Descriptor->Location)) {
    Status = Bridge.Locations.resolvePublishedLocation(
        Task, Descriptor->Location, &Location);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  DiagnosticsEngine &Diagnostics = Bridge.SemanticAnalyzer.getDiagnostics();
  const unsigned ID = Diagnostics.getCustomDiagID(Level, "%0");
  Diagnostics.Report(Location, ID) << Message.str();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL
PluginSemaBridge::Impl::getExpressionExtensionInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercSemaExpressionExtensionInput *OutInput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->getExpressionExtensionInput(Frame, Input, OutInput);
}

NevercStatus NEVERC_CALL
PluginSemaBridge::Impl::createExpressionExtensionOutput(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation,
    const NevercSemaExpressionExtensionOutput *Descriptor,
    NevercArtifactHandle *OutOutput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->createExpressionExtensionOutput(
      Frame, Continuation, Descriptor, OutOutput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getStatementExtensionInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercSemaStatementExtensionInput *OutInput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->getStatementExtensionInput(Frame, Input, OutInput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::createStatementExtensionOutput(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation,
    const NevercSemaStatementExtensionOutput *Descriptor,
    NevercArtifactHandle *OutOutput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->createStatementExtensionOutput(
      Frame, Continuation, Descriptor, OutOutput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getDeclarationExtensionInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercSemaDeclarationExtensionInput *OutInput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->getDeclarationExtensionInput(Frame, Input,
                                                          OutInput);
}

NevercStatus NEVERC_CALL
PluginSemaBridge::Impl::createDeclarationExtensionOutput(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation,
    const NevercSemaDeclarationExtensionOutput *Descriptor,
    NevercArtifactHandle *OutOutput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->createDeclarationExtensionOutput(
      Frame, Continuation, Descriptor, OutOutput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getTypeExtensionInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercSemaTypeExtensionInput *OutInput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->getTypeExtensionInput(Frame, Input, OutInput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::createTypeExtensionOutput(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation,
    const NevercSemaTypeExtensionOutput *Descriptor,
    NevercArtifactHandle *OutOutput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->createTypeExtensionOutput(
      Frame, Continuation, Descriptor, OutOutput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getLookupExtensionInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercSemaLookupExtensionInput *OutInput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->getLookupExtensionInput(Frame, Input, OutInput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::createLookupExtensionOutput(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation,
    const NevercSemaLookupExtensionOutput *Descriptor,
    NevercArtifactHandle *OutOutput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->createLookupExtensionOutput(
      Frame, Continuation, Descriptor, OutOutput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getConversionExtensionInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercSemaConversionExtensionInput *OutInput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->getConversionExtensionInput(Frame, Input, OutInput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::createConversionExtensionOutput(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation,
    const NevercSemaConversionExtensionOutput *Descriptor,
    NevercArtifactHandle *OutOutput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Extensions)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Extensions->createConversionExtensionOutput(
      Frame, Continuation, Descriptor, OutOutput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getAnalyzePhaseInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercSemaPhaseInput *OutInput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Phases)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Phases->getAnalyzePhaseInput(Frame, Input, OutInput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::createSemanticUnit(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercSemanticUnitDescriptor *Descriptor,
    NevercArtifactHandle *OutOutput) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Phases)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Phases->createSemanticUnit(Frame, Descriptor, OutOutput);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getSemanticUnitInfo(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Unit,
    NevercSemanticUnitInfo *OutInfo) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.Phases)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Phases->getSemanticUnitInfo(Frame, Unit, OutInfo);
}

PluginSemaBridge::PluginSemaBridge(PluginTaskContext &Task,
                                   Sema &SemanticAnalyzer,
                                   PluginASTBridge &AST,
                                   FrontendPluginBridge &Locations)
    : State(std::make_unique<Impl>(Task, SemanticAnalyzer, AST, Locations)) {}

PluginSemaBridge::~PluginSemaBridge() { detachProcessInterface(); }

const NevercSemaAPI &PluginSemaBridge::semaAPI() const { return State->API; }

void PluginSemaBridge::setExtensionAPI(
    PluginSemaExtensionAPI *ExtensionAPI) {
  State->Extensions = ExtensionAPI;
}

void PluginSemaBridge::setPhaseAPI(PluginSemaPhaseAPI *PhaseAPI) {
  State->Phases = PhaseAPI;
}

Error PluginSemaBridge::attachProcessInterface() {
  if (AttachedToProcess)
    return Error::success();
  auto ProcessBridge = findSemaProcessBridge(State->Task.processServices());
  if (!ProcessBridge)
    return createStringError(inconvertibleErrorCode(),
                             "plugin Sema interface is not registered");
  if (Error E = ProcessBridge->attach(State->Task, *this))
    return E;
  AttachedToProcess = true;
  return Error::success();
}

void PluginSemaBridge::detachProcessInterface() {
  if (!AttachedToProcess)
    return;
  if (auto ProcessBridge =
          findSemaProcessBridge(State->Task.processServices()))
    ProcessBridge->detach(State->Task.handle());
  AttachedToProcess = false;
}

Error registerPluginSemaInterface(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register plugin Sema interface after interface freeze");
  auto Bridge = std::make_shared<PluginSemaProcessBridge>();
  if (Error E = Services.registerHostService(semaPluginInterfaceID(), Bridge))
    return E;
  return Services.interfaces().registerInterface(
      semaPluginInterfaceID(), NEVERC_INTERFACE_STABLE, &Bridge->api(), {});
}

} // namespace neverc::plugin
