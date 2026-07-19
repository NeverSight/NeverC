#include "neverc/Plugin/Host/PluginABILowering.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/MathExtras.h"
#include <cstddef>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

Error classificationError(StringRef ABIName, StringRef Detail) {
  return createStringError(inconvertibleErrorCode(),
                           "target ABI '" + ABIName +
                               "' produced an invalid classification: " +
                               Detail);
}

bool validHeader(const NevercABITableHeader &Header, size_t Required) {
  return Header.StructSize >= Required &&
         Header.Major == NEVERC_TARGET_ABI_API_MAJOR &&
         Header.Minor <= NEVERC_TARGET_ABI_API_MINOR &&
         Header.Flags == 0;
}

Error validateClassification(
    StringRef ABIName, const NevercABITypeDescriptor &Type,
    const NevercABIArgumentClassification &Classification) {
  constexpr size_t Required =
      offsetof(NevercABIArgumentClassification, Flags) +
      sizeof(NevercABIArgumentClassification::Flags);
  constexpr NevercABIArgumentFlags KnownFlags =
      NEVERC_ABI_ARGUMENT_BYVAL | NEVERC_ABI_ARGUMENT_REALIGN |
      NEVERC_ABI_ARGUMENT_INREG |
      NEVERC_ABI_ARGUMENT_SRET_AFTER_THIS |
      NEVERC_ABI_ARGUMENT_CAN_BE_FLATTENED |
      NEVERC_ABI_ARGUMENT_SIGN_EXTEND;
  if (!validHeader(Classification.Header, Required))
    return classificationError(ABIName, "bad ABI table header");
  if (Classification.Flags & ~KnownFlags)
    return classificationError(ABIName, "unknown argument flags");
  if (Classification.Coercion > NEVERC_ABI_COERCE_POINTER)
    return classificationError(ABIName, "unknown coercion kind");
  if (Classification.Coercion == NEVERC_ABI_COERCE_NONE &&
      Classification.CoercionBitWidth != 0)
    return classificationError(
        ABIName, "coercion bit width is set without a coercion");
  if (Classification.Coercion != NEVERC_ABI_COERCE_NONE &&
      Classification.CoercionBitWidth == 0)
    return classificationError(ABIName,
                               "coercion has no bit width");
  if (Classification.Alignment != 0 &&
      !isPowerOf2_32(Classification.Alignment))
    return classificationError(ABIName,
                               "alignment is not a power of two");

  switch (Classification.Kind) {
  case NEVERC_ABI_ARGUMENT_DIRECT:
    if (Classification.Flags &
        (NEVERC_ABI_ARGUMENT_BYVAL |
         NEVERC_ABI_ARGUMENT_REALIGN |
         NEVERC_ABI_ARGUMENT_SRET_AFTER_THIS |
         NEVERC_ABI_ARGUMENT_SIGN_EXTEND))
      return classificationError(ABIName,
                                 "direct argument has indirect flags");
    return Error::success();
  case NEVERC_ABI_ARGUMENT_EXTEND:
    if (Type.Kind != NEVERC_ABI_TYPE_BOOLEAN &&
        Type.Kind != NEVERC_ABI_TYPE_INTEGER &&
        Type.Kind != NEVERC_ABI_TYPE_ENUM)
      return classificationError(
          ABIName, "extend classification requires an integer type");
    if (Classification.Coercion != NEVERC_ABI_COERCE_NONE &&
        Classification.Coercion != NEVERC_ABI_COERCE_INTEGER)
      return classificationError(
          ABIName, "extend classification has a non-integer coercion");
    if (Classification.Flags &
        (NEVERC_ABI_ARGUMENT_BYVAL |
         NEVERC_ABI_ARGUMENT_REALIGN |
         NEVERC_ABI_ARGUMENT_SRET_AFTER_THIS |
         NEVERC_ABI_ARGUMENT_CAN_BE_FLATTENED))
      return classificationError(ABIName,
                                 "extend argument has invalid flags");
    return Error::success();
  case NEVERC_ABI_ARGUMENT_INDIRECT:
    if (Classification.Coercion != NEVERC_ABI_COERCE_NONE ||
        Classification.DirectOffset != 0)
      return classificationError(
          ABIName, "indirect argument cannot request coercion");
    if (Classification.Alignment == 0)
      return classificationError(
          ABIName, "indirect argument has no alignment");
    if (Classification.Flags &
        (NEVERC_ABI_ARGUMENT_CAN_BE_FLATTENED |
         NEVERC_ABI_ARGUMENT_SIGN_EXTEND))
      return classificationError(ABIName,
                                 "indirect argument has direct flags");
    return Error::success();
  case NEVERC_ABI_ARGUMENT_IGNORE:
    if (Classification.Coercion != NEVERC_ABI_COERCE_NONE ||
        Classification.Alignment != 0 ||
        Classification.DirectOffset != 0 ||
        Classification.Flags != 0)
      return classificationError(
          ABIName, "ignored argument carries lowering data");
    return Error::success();
  case NEVERC_ABI_ARGUMENT_EXPAND:
    if (!(Type.Flags & NEVERC_ABI_TYPE_AGGREGATE))
      return classificationError(
          ABIName, "expand classification requires an aggregate");
    if (Classification.Coercion != NEVERC_ABI_COERCE_NONE ||
        Classification.DirectOffset != 0 ||
        Classification.Flags != 0)
      return classificationError(ABIName,
                                 "expanded argument carries lowering data");
    return Error::success();
  default:
    return classificationError(ABIName, "unknown argument kind");
  }
}

NevercABIArgumentClassification emptyClassification() {
  NevercABIArgumentClassification Result{};
  Result.Header = {sizeof(Result), NEVERC_TARGET_ABI_API_MAJOR,
                   NEVERC_TARGET_ABI_API_MINOR, 0};
  return Result;
}

} // namespace

Expected<PluginABIFunctionClassification>
PluginABILowering::classify(
    const NevercABITypeDescriptor &ReturnType,
    ArrayRef<NevercABITypeDescriptor> Parameters, bool Variadic,
    uint32_t RequiredArguments) const {
  if (!ABI.ClassifyFunction)
    return createStringError(inconvertibleErrorCode(),
                             "target ABI '" + ABI.CanonicalName +
                                 "' has no function classifier");
  if (RequiredArguments > Parameters.size() ||
      (!Variadic && RequiredArguments != Parameters.size()))
    return createStringError(
        inconvertibleErrorCode(),
        "target ABI lowering received an invalid required argument count");

  PluginABIFunctionClassification Result;
  Result.ReturnValue = emptyClassification();
  Result.Arguments.assign(Parameters.size(), emptyClassification());
  Result.LLVMCallingConvention =
      CallingConvention ? CallingConvention->LLVMCallingConvention : 0;

  NevercABIFunctionQuery Query{};
  Query.Header = {sizeof(Query), NEVERC_TARGET_ABI_API_MAJOR,
                  NEVERC_TARGET_ABI_API_MINOR, 0};
  Query.ReturnType = ReturnType;
  Query.Parameters = {Parameters.data(), Parameters.size(),
                      sizeof(NevercABITypeDescriptor)};
  Query.Variadic = Variadic ? NEVERC_TRUE : NEVERC_FALSE;
  Query.RequiredArgumentCount = RequiredArguments;

  NevercABIArgumentClassificationArray Arguments{
      Result.Arguments.data(), Result.Arguments.size(),
      sizeof(NevercABIArgumentClassification)};
  auto Invoke = [&] {
    return ABI.ClassifyFunction(ABI.CallbackUserData, &Query,
                                &Result.ReturnValue, &Arguments);
  };
  Expected<NevercStatus> Status =
      Task ? Task->invokeCallback(ABI.PluginID, "ClassifyABIFunction",
                                  Invoke)
           : Expected<NevercStatus>(Invoke());
  if (!Status)
    return Status.takeError();
  if (!neverc_status_is_ok(*Status))
    return createStringError(
        inconvertibleErrorCode(),
        "target ABI '" + ABI.CanonicalName +
            "' classifier failed with status " +
            std::to_string(Status->Code));
  if (Arguments.Data != Result.Arguments.data() ||
      Arguments.Count != Result.Arguments.size() ||
      Arguments.ElementStride !=
          sizeof(NevercABIArgumentClassification))
    return classificationError(
        ABI.CanonicalName, "classifier replaced the host output buffer");

  if (Error E = validateClassification(
          ABI.CanonicalName, ReturnType, Result.ReturnValue))
    return std::move(E);
  for (size_t I = 0; I != Parameters.size(); ++I)
    if (Error E = validateClassification(
            ABI.CanonicalName, Parameters[I], Result.Arguments[I]))
      return joinErrors(
          createStringError(inconvertibleErrorCode(),
                            "while classifying ABI argument " +
                                std::to_string(I)),
          std::move(E));
  return Result;
}
