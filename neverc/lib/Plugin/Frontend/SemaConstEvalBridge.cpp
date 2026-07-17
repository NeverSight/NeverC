#include "SemaBridgeInternal.h"
#include "neverc/Tree/Type/Type.h"
#include <iterator>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStringView constantString(StringRef Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

NevercSemaConstantKind constantKind(APValue::ValueKind Kind) {
  switch (Kind) {
  case APValue::None:
    return NEVERC_SEMA_CONSTANT_NONE;
  case APValue::Indeterminate:
    return NEVERC_SEMA_CONSTANT_INDETERMINATE;
  case APValue::Int:
    return NEVERC_SEMA_CONSTANT_INTEGER;
  case APValue::Float:
    return NEVERC_SEMA_CONSTANT_FLOAT;
  case APValue::FixedPoint:
    return NEVERC_SEMA_CONSTANT_FIXED_POINT;
  case APValue::ComplexInt:
    return NEVERC_SEMA_CONSTANT_COMPLEX_INTEGER;
  case APValue::ComplexFloat:
    return NEVERC_SEMA_CONSTANT_COMPLEX_FLOAT;
  case APValue::LValue:
    return NEVERC_SEMA_CONSTANT_ADDRESS;
  case APValue::Vector:
    return NEVERC_SEMA_CONSTANT_VECTOR;
  case APValue::Array:
    return NEVERC_SEMA_CONSTANT_ARRAY;
  case APValue::Struct:
    return NEVERC_SEMA_CONSTANT_STRUCT;
  case APValue::Union:
    return NEVERC_SEMA_CONSTANT_UNION;
  case APValue::AddrLabelDiff:
    return NEVERC_SEMA_CONSTANT_ADDRESS_LABEL_DIFFERENCE;
  }
  llvm_unreachable("unknown APValue kind");
}

uint64_t constantElementCount(const APValue &Value) {
  if (Value.isComplexInt() || Value.isComplexFloat() ||
      Value.isAddrLabelDiff())
    return 2;
  if (Value.isVector())
    return Value.getVectorLength();
  if (Value.isArray())
    return Value.getArraySize();
  if (Value.isStruct())
    return Value.getStructNumFields();
  if (Value.isUnion())
    return Value.getUnionField() ? 1 : 0;
  return 0;
}

template <typename PayloadT>
bool getConstantElementValue(const PayloadT &P, uint64_t Index,
                             APValue &OutValue, QualType &OutType) {
  const APValue &Value = P.Value;
  if (Value.isComplexInt()) {
    const auto *Complex = P.Type->template getAs<ComplexType>();
    if (!Complex || Index > 1)
      return false;
    OutType = Complex->getElementType();
    OutValue = APValue(Index == 0 ? Value.getComplexIntReal()
                                  : Value.getComplexIntImag());
    return true;
  }
  if (Value.isComplexFloat()) {
    const auto *Complex = P.Type->template getAs<ComplexType>();
    if (!Complex || Index > 1)
      return false;
    OutType = Complex->getElementType();
    OutValue = APValue(Index == 0 ? Value.getComplexFloatReal()
                                  : Value.getComplexFloatImag());
    return true;
  }
  if (Value.isVector()) {
    const auto *Vector = P.Type->template getAs<VectorType>();
    if (!Vector || Index >= Value.getVectorLength())
      return false;
    OutType = Vector->getElementType();
    OutValue = Value.getVectorElt(static_cast<unsigned>(Index));
    return true;
  }
  if (Value.isArray()) {
    const auto *Array = P.Type->getAsArrayTypeUnsafe();
    if (!Array || Index >= Value.getArraySize())
      return false;
    OutType = Array->getElementType();
    if (Index < Value.getArrayInitializedElts())
      OutValue = Value.getArrayInitializedElt(static_cast<unsigned>(Index));
    else if (Value.hasArrayFiller())
      OutValue = Value.getArrayFiller();
    else
      OutValue = APValue();
    return true;
  }
  if (Value.isStruct()) {
    const auto *Record = P.Type->template getAs<RecordType>();
    if (!Record || Index >= Value.getStructNumFields())
      return false;
    auto Field = Record->getDecl()->field_begin();
    std::advance(Field, static_cast<ptrdiff_t>(Index));
    OutType = (*Field)->getType();
    OutValue = Value.getStructField(static_cast<unsigned>(Index));
    return true;
  }
  if (Value.isUnion()) {
    const FieldDecl *Field = Value.getUnionField();
    if (!Field || Index != 0)
      return false;
    OutType = Field->getType();
    OutValue = Value.getUnionValue();
    return true;
  }
  return false;
}

} // namespace

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::evaluateConstant(
    void *Context, NevercTaskHandle Task, NevercExprHandle Expression,
    NevercConstantValueHandle *OutValue) {
  if (!Context || !OutValue)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutValue = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  const void *Native = nullptr;
  NevercStatus Status = Bridge.AST.resolvePublishedNode(
      Task, Expression, NEVERC_AST_SCHEMA_DOMAIN_STMT, &Native);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto *ExpressionValue =
      dyn_cast<Expr>(static_cast<const Stmt *>(Native));
  if (!ExpressionValue)
    return semaStatus(NEVERC_STATUS_WRONG_TYPE);
  Expr::EvalResult Result;
  if (!ExpressionValue->EvaluateAsRValue(
          Result, Bridge.SemanticAnalyzer.Context,
          /*InConstantContext=*/true))
    return semaStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  auto Handle = Bridge.createConstant(Result.Val, ExpressionValue->getType(),
                                      Result.HasSideEffects);
  if (!Handle) {
    consumeError(Handle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutValue = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getConstantValueInfo(
    void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value,
    NevercSemaConstantValueInfo *OutInfo) {
  if (!Context || !OutInfo)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  ConstantPayload *Payload = nullptr;
  NevercStatus Status = Bridge.resolveConstant(Task, Value, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercSemaConstantValueInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Info.Kind = constantKind(Payload->Value.getKind());
  Info.HasSideEffects = Payload->HasSideEffects ? NEVERC_TRUE : NEVERC_FALSE;
  Info.ElementCount = constantElementCount(Payload->Value);
  Info.Text = constantString(Payload->Text);
  if (Payload->Value.isInt()) {
    Info.IsSigned =
        Payload->Value.getInt().isSigned() ? NEVERC_TRUE : NEVERC_FALSE;
    Info.BitWidth = Payload->Value.getInt().getBitWidth();
  } else if (Payload->Value.isLValue()) {
    Info.IsNullPointer =
        Payload->Value.isNullPointer() ? NEVERC_TRUE : NEVERC_FALSE;
    Info.IsOnePastTheEnd =
        Payload->Value.isLValueOnePastTheEnd() ? NEVERC_TRUE : NEVERC_FALSE;
    Info.AddressOffsetBytes =
        Payload->Value.getLValueOffset().getQuantity();
    APValue::LValueBase Base = Payload->Value.getLValueBase();
    if (const auto *Declaration = Base.dyn_cast<const ValueDecl *>()) {
      auto Handle = Bridge.AST.publishDecl(Declaration);
      if (!Handle) {
        consumeError(Handle.takeError());
        return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
      Info.AddressBase = *Handle;
    } else if (const auto *Expression = Base.dyn_cast<const Expr *>()) {
      auto Handle = Bridge.AST.publishExpr(Expression);
      if (!Handle) {
        consumeError(Handle.takeError());
        return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
      Info.AddressBase = *Handle;
    }
  }
  return writeSemaRecord(OutInfo, Info);
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getConstantIntegerWord(
    void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value,
    uint64_t Index, uint64_t *OutWord) {
  if (!Context || !OutWord)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutWord = 0;
  Impl &Bridge = *static_cast<Impl *>(Context);
  ConstantPayload *Payload = nullptr;
  NevercStatus Status = Bridge.resolveConstant(Task, Value, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Payload->Value.isInt())
    return semaStatus(NEVERC_STATUS_WRONG_TYPE);
  const APSInt &Integer = Payload->Value.getInt();
  if (Index >= Integer.getNumWords())
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutWord = Integer.getRawData()[static_cast<size_t>(Index)];
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::getConstantElement(
    void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value,
    uint64_t Index, NevercConstantValueHandle *OutElement) {
  if (!Context || !OutElement)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutElement = {};
  Impl &Bridge = *static_cast<Impl *>(Context);
  ConstantPayload *Payload = nullptr;
  NevercStatus Status = Bridge.resolveConstant(Task, Value, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  APValue Element;
  QualType ElementType;
  if (!getConstantElementValue(*Payload, Index, Element, ElementType))
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto Handle =
      Bridge.createConstant(Element, ElementType, Payload->HasSideEffects);
  if (!Handle) {
    consumeError(Handle.takeError());
    return semaStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutElement = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginSemaBridge::Impl::destroyConstantValue(
    void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value) {
  if (!Context)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Impl &Bridge = *static_cast<Impl *>(Context);
  ConstantPayload *Payload = nullptr;
  NevercStatus Status = Bridge.resolveConstant(Task, Value, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return Bridge.Task.handles().release(Value,
                                       PluginSemaConstantValueHandleKind);
}

} // namespace neverc::plugin
