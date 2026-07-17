#include "SemaBridgeInternal.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/APInt.h"
#include <limits>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus publishType(PluginASTBridge &AST, QualType Type,
                         NevercTypeHandle *OutType) {
  if (!OutType)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutType = {};
  auto Handle = AST.publishType(Type);
  if (!Handle) {
    consumeError(Handle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutType = *Handle;
  return neverc_status_ok();
}

bool validFunctionDescriptor(const NevercSemaFunctionTypeDescriptor &Value) {
  return semaValidHeader(Value.Header, sizeof(Value)) &&
         Value.Variadic <= NEVERC_TRUE && Value.Reserved[0] == 0 &&
         Value.Reserved[1] == 0 && Value.Reserved[2] == 0 &&
         Value.ParameterCount <= std::numeric_limits<unsigned>::max() &&
         (Value.ParameterCount == 0 || Value.ParameterTypes);
}

} // namespace

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getBuiltinType(
    void *Context, NevercTaskHandle Task, NevercBuiltinTypeKind Kind,
    NevercTypeHandle *OutType) {
  if (!Context || !OutType)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  if (!Bridge.validTask(Task)) {
    *OutType = {};
    return semaStatus(NEVERC_STATUS_WRONG_SCOPE);
  }
  const NevercASTAPI &API = Bridge.AST.astAPI();
  return API.GetBuiltinType(API.Context, Task, Kind, OutType);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::createPointerType(
    void *Context, NevercTaskHandle Task,
    NevercSemaMutationLeaseHandle Lease, NevercTypeHandle Pointee,
    NevercTypeHandle *OutType) {
  if (!Context || !OutType)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutType = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  MutationLeasePayload *LeasePayload = nullptr;
  NevercStatus Status = Bridge.resolveLease(Task, Lease, &LeasePayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!LeasePayload->Active)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  QualType NativePointee;
  Status = Bridge.AST.resolvePublishedType(Task, Pointee, &NativePointee);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return publishType(Bridge.AST,
                     Bridge.SemanticAnalyzer.Context.getPointerType(
                         NativePointee),
                     OutType);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::createConstantArrayType(
    void *Context, NevercTaskHandle Task,
    NevercSemaMutationLeaseHandle Lease, NevercTypeHandle Element,
    uint64_t ElementCount, NevercTypeHandle *OutType) {
  if (!Context || !OutType || ElementCount == 0)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutType = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  MutationLeasePayload *LeasePayload = nullptr;
  NevercStatus Status = Bridge.resolveLease(Task, Lease, &LeasePayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!LeasePayload->Active)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  QualType NativeElement;
  Status = Bridge.AST.resolvePublishedType(Task, Element, &NativeElement);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (NativeElement->isFunctionType() || NativeElement->isVoidType())
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  QualType Result = Bridge.SemanticAnalyzer.Context.getConstantArrayType(
      NativeElement, APInt(64, ElementCount), nullptr, ArraySizeModifier::Normal,
      0);
  return publishType(Bridge.AST, Result, OutType);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::createFunctionType(
    void *Context, NevercTaskHandle Task,
    NevercSemaMutationLeaseHandle Lease,
    const NevercSemaFunctionTypeDescriptor *Descriptor,
    NevercTypeHandle *OutType) {
  if (!Context || !Descriptor || !OutType ||
      !validFunctionDescriptor(*Descriptor))
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutType = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  MutationLeasePayload *LeasePayload = nullptr;
  NevercStatus Status = Bridge.resolveLease(Task, Lease, &LeasePayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!LeasePayload->Active)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);

  QualType ResultType;
  Status = Bridge.AST.resolvePublishedType(Task, Descriptor->ResultType,
                                           &ResultType);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (ResultType->isArrayType() || ResultType->isFunctionType())
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  SmallVector<QualType, 8> Parameters;
  Parameters.reserve(static_cast<size_t>(Descriptor->ParameterCount));
  for (uint64_t Index = 0; Index != Descriptor->ParameterCount; ++Index) {
    QualType Parameter;
    Status = Bridge.AST.resolvePublishedType(
        Task, Descriptor->ParameterTypes[Index], &Parameter);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Parameter->isVoidType())
      return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Parameters.push_back(Parameter);
  }
  FunctionProtoType::ExtProtoInfo Info;
  Info.Variadic = Descriptor->Variadic == NEVERC_TRUE;
  return publishType(
      Bridge.AST,
      Bridge.SemanticAnalyzer.Context.getFunctionType(ResultType, Parameters,
                                                      Info),
      OutType);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getTagType(
    void *Context, NevercTaskHandle Task, NevercDeclHandle Declaration,
    NevercTypeHandle *OutType) {
  if (!Context || !OutType)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutType = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  const void *Native = nullptr;
  NevercStatus Status = Bridge.AST.resolvePublishedNode(
      Task, Declaration, NEVERC_AST_SCHEMA_DOMAIN_DECL, &Native);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto *Tag = dyn_cast<TagDecl>(static_cast<const Decl *>(Native));
  if (!Tag)
    return semaStatus(NEVERC_STATUS_WRONG_TYPE);
  return publishType(
      Bridge.AST,
      Bridge.SemanticAnalyzer.Context.getTypeDeclType(
          const_cast<TagDecl *>(Tag)),
      OutType);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::createAtomicType(
    void *Context, NevercTaskHandle Task,
    NevercSemaMutationLeaseHandle Lease, NevercTypeHandle ValueType,
    NevercTypeHandle *OutType) {
  if (!Context || !OutType)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutType = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  MutationLeasePayload *LeasePayload = nullptr;
  NevercStatus Status = Bridge.resolveLease(Task, Lease, &LeasePayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!LeasePayload->Active)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  QualType NativeValue;
  Status = Bridge.AST.resolvePublishedType(Task, ValueType, &NativeValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (NativeValue->isArrayType() || NativeValue->isFunctionType() ||
      NativeValue->isAtomicType() || NativeValue->isVoidType())
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return publishType(
      Bridge.AST,
      Bridge.SemanticAnalyzer.Context.getAtomicType(NativeValue), OutType);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::createVectorType(
    void *Context, NevercTaskHandle Task,
    NevercSemaMutationLeaseHandle Lease, NevercTypeHandle Element,
    uint32_t ElementCount, NevercSemaVectorKind Kind,
    NevercTypeHandle *OutType) {
  if (!Context || !OutType || ElementCount == 0)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutType = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  MutationLeasePayload *LeasePayload = nullptr;
  NevercStatus Status = Bridge.resolveLease(Task, Lease, &LeasePayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!LeasePayload->Active)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  QualType NativeElement;
  Status = Bridge.AST.resolvePublishedType(Task, Element, &NativeElement);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!NativeElement->isIntegerType() && !NativeElement->isRealFloatingType())
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  QualType Result;
  switch (Kind) {
  case NEVERC_SEMA_VECTOR_GENERIC:
    Result = Bridge.SemanticAnalyzer.Context.getVectorType(
        NativeElement, ElementCount, VectorKind::Generic);
    break;
  case NEVERC_SEMA_VECTOR_EXTENDED:
    Result = Bridge.SemanticAnalyzer.Context.getExtVectorType(NativeElement,
                                                              ElementCount);
    break;
  case NEVERC_SEMA_VECTOR_SCALABLE:
    Result = Bridge.SemanticAnalyzer.Context.getScalableVectorType(
        NativeElement, ElementCount);
    break;
  default:
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  return publishType(Bridge.AST, Result, OutType);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getCanonicalType(
    void *Context, NevercTaskHandle Task, NevercTypeHandle Type,
    NevercTypeHandle *OutType) {
  if (!Context || !OutType)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutType = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  QualType Native;
  NevercStatus Status = Bridge.AST.resolvePublishedType(Task, Type, &Native);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return publishType(
      Bridge.AST, Bridge.SemanticAnalyzer.Context.getCanonicalType(Native),
      OutType);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::areTypesCompatible(
    void *Context, NevercTaskHandle Task, NevercTypeHandle Left,
    NevercTypeHandle Right, NevercBool *OutCompatible) {
  if (!Context || !OutCompatible)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCompatible = NEVERC_FALSE;
  Impl &Bridge = *static_cast<Impl *>(Context);
  QualType NativeLeft;
  NevercStatus Status =
      Bridge.AST.resolvePublishedType(Task, Left, &NativeLeft);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  QualType NativeRight;
  Status = Bridge.AST.resolvePublishedType(Task, Right, &NativeRight);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCompatible = Bridge.SemanticAnalyzer.Context.typesAreCompatible(
                       NativeLeft, NativeRight)
                       ? NEVERC_TRUE
                       : NEVERC_FALSE;
  return neverc_status_ok();
}

namespace {

NevercSemaConversionKind mapConversionKind(Sema::AssignConvertType Kind) {
  switch (Kind) {
  case Sema::Compatible:
    return NEVERC_SEMA_CONVERSION_COMPATIBLE;
  case Sema::PointerToInt:
    return NEVERC_SEMA_CONVERSION_POINTER_TO_INTEGER;
  case Sema::IntToPointer:
    return NEVERC_SEMA_CONVERSION_INTEGER_TO_POINTER;
  case Sema::FunctionVoidPointer:
    return NEVERC_SEMA_CONVERSION_FUNCTION_VOID_POINTER;
  case Sema::IncompatiblePointer:
    return NEVERC_SEMA_CONVERSION_INCOMPATIBLE_POINTER;
  case Sema::IncompatibleFunctionPointer:
    return NEVERC_SEMA_CONVERSION_INCOMPATIBLE_FUNCTION_POINTER;
  case Sema::IncompatibleFunctionPointerStrict:
    return NEVERC_SEMA_CONVERSION_STRICT_FUNCTION_POINTER;
  case Sema::IncompatiblePointerSign:
    return NEVERC_SEMA_CONVERSION_POINTER_SIGN;
  case Sema::CompatiblePointerDiscardsQualifiers:
    return NEVERC_SEMA_CONVERSION_DISCARDS_QUALIFIERS;
  case Sema::IncompatiblePointerDiscardsQualifiers:
    return NEVERC_SEMA_CONVERSION_INVALID_QUALIFIERS;
  case Sema::IncompatibleNestedPointerAddressSpaceMismatch:
    return NEVERC_SEMA_CONVERSION_ADDRESS_SPACE_MISMATCH;
  case Sema::IncompatibleNestedPointerQualifiers:
    return NEVERC_SEMA_CONVERSION_NESTED_QUALIFIERS;
  case Sema::IncompatibleVectors:
    return NEVERC_SEMA_CONVERSION_VECTOR;
  case Sema::Incompatible:
    return NEVERC_SEMA_CONVERSION_INCOMPATIBLE;
  }
  llvm_unreachable("unknown assignment conversion kind");
}

bool conversionIsViable(Sema::AssignConvertType Kind) {
  return Kind != Sema::Incompatible &&
         Kind != Sema::IncompatiblePointerDiscardsQualifiers &&
         Kind != Sema::IncompatibleNestedPointerAddressSpaceMismatch;
}

Expected<Sema::AssignmentAction>
mapConversionContext(NevercSemaConversionContext Context) {
  switch (Context) {
  case NEVERC_SEMA_CONVERSION_ASSIGNMENT:
    return Sema::AA_Assigning;
  case NEVERC_SEMA_CONVERSION_ARGUMENT:
    return Sema::AA_Passing;
  case NEVERC_SEMA_CONVERSION_RETURN:
    return Sema::AA_Returning;
  case NEVERC_SEMA_CONVERSION_INITIALIZATION:
    return Sema::AA_Initializing;
  case NEVERC_SEMA_CONVERSION_EXPLICIT_CAST:
    return Sema::AA_Casting;
  default:
    return createStringError(inconvertibleErrorCode(),
                             "invalid Sema conversion context");
  }
}

} // namespace

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::classifyImplicitConversion(
    void *Context, NevercTaskHandle Task, NevercTypeHandle Source,
    NevercTypeHandle Destination,
    NevercConversionSequenceHandle *OutSequence) {
  if (!Context || !OutSequence)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSequence = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  QualType NativeSource;
  NevercStatus Status =
      Bridge.AST.resolvePublishedType(Task, Source, &NativeSource);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  QualType NativeDestination;
  Status = Bridge.AST.resolvePublishedType(Task, Destination,
                                           &NativeDestination);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  auto *Payload = new (std::nothrow) ConversionPayload;
  if (!Payload)
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Payload->Owner = &Bridge;
  Payload->SourceHandle = Source;
  Payload->DestinationHandle = Destination;
  Payload->Source = NativeSource;
  Payload->Destination = NativeDestination;
  Payload->NativeKind = Bridge.SemanticAnalyzer.CheckAssignmentConstraints(
      SourceLocation(), NativeDestination, NativeSource);
  Payload->Kind = mapConversionKind(Payload->NativeKind);

  auto Handle = Bridge.Task.handles().create(
      PluginSemaConversionSequenceHandleKind, Payload,
      [](void *Raw) { delete static_cast<ConversionPayload *>(Raw); });
  if (!Handle) {
    delete Payload;
    consumeError(Handle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutSequence = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getConversionSequenceInfo(
    void *Context, NevercTaskHandle Task,
    NevercConversionSequenceHandle Sequence,
    NevercSemaConversionSequenceInfo *OutInfo) {
  if (!Context || !OutInfo)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  ConversionPayload *Payload = nullptr;
  NevercStatus Status =
      Bridge.resolveConversion(Task, Sequence, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  NevercSemaConversionSequenceInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Info.SourceType = Payload->SourceHandle;
  Info.DestinationType = Payload->DestinationHandle;
  Info.Kind = Payload->Kind;
  Info.Viable =
      conversionIsViable(Payload->NativeKind) ? NEVERC_TRUE : NEVERC_FALSE;
  Info.RequiresDiagnostic =
      Payload->NativeKind == Sema::Compatible ? NEVERC_FALSE : NEVERC_TRUE;
  return writeSemaRecord(OutInfo, Info);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::applyImplicitConversion(
    void *Context, NevercTaskHandle Task,
    NevercSemaMutationLeaseHandle Lease,
    NevercConversionSequenceHandle Sequence, NevercExprHandle Expression,
    NevercSemaConversionContext ConversionContext,
    NevercExprHandle *OutExpression) {
  if (!Context || !OutExpression)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutExpression = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  MutationLeasePayload *LeasePayload = nullptr;
  NevercStatus Status = Bridge.resolveLease(Task, Lease, &LeasePayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!LeasePayload->Active)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  ConversionPayload *Payload = nullptr;
  Status = Bridge.resolveConversion(Task, Sequence, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!conversionIsViable(Payload->NativeKind))
    return semaStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  const void *NativeExpression = nullptr;
  Status = Bridge.AST.resolvePublishedNode(
      Task, Expression, NEVERC_AST_SCHEMA_DOMAIN_STMT, &NativeExpression);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *ExpressionValue = dyn_cast<Expr>(
      const_cast<Stmt *>(static_cast<const Stmt *>(NativeExpression)));
  if (!ExpressionValue)
    return semaStatus(NEVERC_STATUS_WRONG_TYPE);
  if (!Bridge.SemanticAnalyzer.Context.hasSameType(ExpressionValue->getType(),
                                                   Payload->Source))
    return semaStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  auto Action = mapConversionContext(ConversionContext);
  if (!Action) {
    consumeError(Action.takeError());
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  ExprResult Converted = Bridge.SemanticAnalyzer.PerformImplicitConversion(
      ExpressionValue, Payload->Destination, *Action);
  if (Converted.isInvalid())
    return semaStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  auto Handle = Bridge.AST.publishExpr(Converted.get());
  if (!Handle) {
    consumeError(Handle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutExpression = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::createExplicitCast(
    void *Context, NevercTaskHandle Task,
    NevercSemaMutationLeaseHandle Lease, NevercExprHandle Expression,
    NevercTypeHandle Destination, NevercExprHandle *OutExpression) {
  if (!Context || !OutExpression)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutExpression = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  MutationLeasePayload *LeasePayload = nullptr;
  NevercStatus Status = Bridge.resolveLease(Task, Lease, &LeasePayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!LeasePayload->Active)
    return semaStatus(NEVERC_STATUS_INVALID_STATE);
  const void *NativeExpression = nullptr;
  Status = Bridge.AST.resolvePublishedNode(
      Task, Expression, NEVERC_AST_SCHEMA_DOMAIN_STMT, &NativeExpression);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *ExpressionValue = dyn_cast<Expr>(
      const_cast<Stmt *>(static_cast<const Stmt *>(NativeExpression)));
  if (!ExpressionValue)
    return semaStatus(NEVERC_STATUS_WRONG_TYPE);
  QualType NativeDestination;
  Status = Bridge.AST.resolvePublishedType(Task, Destination,
                                           &NativeDestination);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  TypeSourceInfo *TypeInfo =
      Bridge.SemanticAnalyzer.Context.getTrivialTypeSourceInfo(
          NativeDestination);
  ExprResult Cast = Bridge.SemanticAnalyzer.FormCStyleCastExpr(
      SourceLocation(), TypeInfo, SourceLocation(), ExpressionValue);
  if (Cast.isInvalid())
    return semaStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  auto Handle = Bridge.AST.publishExpr(Cast.get());
  if (!Handle) {
    consumeError(Handle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutExpression = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::destroyConversionSequence(
    void *Context, NevercTaskHandle Task,
    NevercConversionSequenceHandle Sequence) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  ConversionPayload *Payload = nullptr;
  NevercStatus Status =
      Bridge.resolveConversion(Task, Sequence, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Bridge.Task.handles().release(
      Sequence, PluginSemaConversionSequenceHandleKind);
}

} // namespace neverc::plugin
